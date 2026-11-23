#!/usr/bin/env python3
"""Compile the help manual (firmware/help/*.txt) into a compressed C table.

    gen_help.py [--help-dir help] --out-c OUT.c [--examples OUT.txt] [--report]
                [--content internal|qspi --out-module-payload MANUAL.bin]

Source format (documented in app/EXTENDING.md, "Help pages"):

    %% comment
    @group ID Title words        command group; following markup = intro of the
                                 auto-generated listing page (topic id = ID)
    @page ID Title words         free page; following lines = markup body
    @cmd NAME                    command page (topic id = NAME), then fields:
    group: ID                    (required) listed on that group's page
    summary: text                (required) one line
    syntax: text                 (1+)
    insert: text                 CATALOG inserts this (default "NAME(")
    suffix: text                 ... and this after the cursor
    label: text                  CATALOG label (default = insert)
    catalog: no                  not listed in CATALOG
    example: input               (0+) each optionally followed by
    result: output               the exact CAS output (verified by make test)
    see: id, id                  related topics
    (blank line, then the markup body)
    A @cmd body that starts with "## Refresher" has that section (up to a
    line "## Syntax") shown above the Syntax; the rest follows the Syntax.
    In any body (@page or @cmd), a line "example: INPUT" is shown as the
    code line INPUT and a following "result: OUTPUT" as "▶ OUTPUT"; both
    are checked like the @cmd fields (examples of a page share one CAS).
    @category ID Title words     CATALOG category (topic id = ID: a listing page
                                 of its commands, like @group), then fields:
    groups: id, id               every command of these groups ...
    commands: name, name         ... plus these (they win over another
                                 category's groups:)
    apps: id, id                 app ids (app/registry.c) listed with it
    (blank line, then the intro markup)
    Categories are shown in source order; commands in no category go to a
    generated "Other" category.

    @essentials id id ...        (once, anywhere) the pages kept in internal
                                 flash when the manual lives in the QSPI
                                 content image (--content qspi): shown even
                                 when no content is installed

Markup: "# Title", "## Subheading", "- bullet", 4-space indented code lines,
paragraphs (blank line separated), inline `code`, [[id]] / [[id|text]] links,
{KEY} key caps. Only ASCII and the calculator font's extra glyphs are allowed.

Output: topics sorted by id, each compressed independently (LZ77 + one
static canonical Huffman code for the whole manual, so the firmware decodes
a topic with no tables beyond ~1 KB of RAM). Placed in internal flash bank 2.

Pipeline: parse_files() reads the sources into Topic objects; build() expands
them into final markup per topic (group listings, the @cmd page layout, the
CATALOG list, the checked examples) and validates links and sizes;
compress_all() compresses; main() checks every topic round-trips through the
reference decoder and writes the C tables and, with --examples, a console
script that `make help-test` runs in the simulator against the real CAS.

Placement (--content, docs/QSPI_CONTENT.md):
  * internal: every compressed topic is in help_blob in internal flash bank 2
    (the rollback build, make CONTENT=internal);
  * qspi (default build): the full manual is the payload of the "manual"
    module (--out-module-payload; tools/mkmodule.py wraps it into
    MANUA010.MOD, core/modstore.h). The payload is self-contained, so any
    manual version works with any firmware that speaks the same "help"
    interface ABI (HELP_ABI below, app/help.c): it carries its own topic
    table, strings and Huffman code. Internal flash keeps the tables (ids,
    titles, CATALOG), the Huffman code lengths and a copy of the @essentials
    topics (help_blob, indexed by help_ess[]).

Manual module payload, help ABI 1 (little-endian u32 unless noted):
    "HLP1", ntopics, topics_off, strings_off, strings_len, lens_off,
    blob_off, blob_len                                  (32-byte header)
    topics: ntopics x {id, title (offsets into strings), off (into the
            blob), clen, ulen}, sorted by id (strcmp)
    strings: NUL-terminated ids and titles
    lens: NLIT literal/length then NDIST distance code lengths (bytes)
    blob: the compressed topics (format below)
Changing this layout or the compression format means a new HELP_ABI.

Compressed format (decoded by app/help.c, mirrored by decompress() below):
  * Bits are packed MSB first (unlike DEFLATE, which is LSB first).
  * Literal/length alphabet (NLIT = 321 symbols): 0..255 literal bytes,
    256 = end of topic, 257 + (len - 3) = match of length 3..66.
  * A match symbol is followed by a distance code 0..15 (NDIST) from a second
    alphabet: code 0 = distance 1; code k >= 1 is followed by k-1 extra bits
    e, distance = 2^(k-1) + e + 1. Distances up to 32768 cover MAX_TOPIC.
  * Both codes are canonical Huffman codes (lengths <= 15 bits), so the C
    tables only hold the code lengths per symbol (help_lit_lens,
    help_dist_lens); the decoder rebuilds count-per-length tables from them.
  * Each topic starts byte-aligned at its offset in help_blob; a match never
    reaches into another topic, so any topic decodes on its own.
"""
import argparse
import glob
import hashlib
import heapq
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# keep in sync with tools/gen_font.py EXTRA and EXTRA_MATH (read from there when possible)
FONT_EXTRA = "θπ√²³⁻¹→←↑↓▶◀▲▼≤≥≠∫∂∞ΣΔ×÷−·°±≈ⁿ█▒ᴇʳⁱˣ∠½"
try:
    _src = open(os.path.join(HERE, "gen_font.py"), encoding="utf-8").read()
    _m = re.search(r'^EXTRA = "(.*)"$', _src, re.M)
    if _m:
        FONT_EXTRA = _m.group(1)
    _m = re.search(r'^EXTRA_MATH = "(.*)"$', _src, re.M)
    if _m:
        FONT_EXTRA += _m.group(1)
except OSError:
    pass
ALLOWED = set(chr(c) for c in range(32, 127)) | set(FONT_EXTRA) | {"\n"}

MAX_TOPIC = 24 * 1024       # bytes, uncompressed (viewer buffer)
HELP_ABI = 1                # version of the manual module payload format (app/help.c HELP_ABI)
MIN_MATCH, MAX_MATCH = 3, 66  # 64 length symbols
NLIT = 256 + 1 + (MAX_MATCH - MIN_MATCH + 1)   # literals, end, lengths
END = 256                   # end-of-topic symbol
NDIST = 16                  # distance codes (see dist_code)
MAXBITS = 15                # longest Huffman code the decoder accepts
ID_RE = re.compile(r"^[A-Za-z0-9_.-]+$")
LINK_RE = re.compile(r"\[\[([A-Za-z0-9_.-]+)(?:\|([^\]]*))?\]\]")
CODE_SPAN_RE = re.compile(r"`[^`]*`")


class HelpError(Exception):
    """A source error; main() prints it (with file:line) and exits with status 1."""
    pass


# ------------------------------------------------------------------ parsing
class Topic:
    """One @group/@page/@cmd block: id, title, kind, source location, body
    lines and (for @cmd) the field values in source order."""

    def __init__(self, tid, title, kind, where):
        self.id, self.title, self.kind, self.where = tid, title, kind, where
        self.body = []          # markup lines
        self.fields = {}        # @cmd fields (lists)


def key_legends():
    """{KEY} caps must name a key as printed on it (keys/layout.py legends)."""
    sys.path.insert(0, os.path.join(HERE, "../keys"))
    import layout
    return {k["legend"] for k in layout.KEYS}


KEYCAP_RE = re.compile(r"\{([^{}\s]+)\}")


def check_keycaps(topics):
    """Raise HelpError for a {KEY} cap that is not a key legend. Code lines and
    `code` spans are skipped, so literal braces can be written there."""
    legends = key_legends()
    for t in topics.values():
        for line in t.body:
            if line.startswith("    "):
                continue
            for cap in KEYCAP_RE.findall(CODE_SPAN_RE.sub("", line)):
                if cap not in legends:
                    raise HelpError(f"{t.where}: topic {t.id}: {{{cap}}} is not a key legend "
                                    f"(keys/layout.py; put other braces in `code`)")


def parse_files(paths):
    """Parse the source files (format in the module docstring).

    Returns (topics, groups, order): all topics by id, the @group topics by id,
    and the ids in source order. Every character must be printable ASCII or a
    calculator-font glyph, since the viewer can draw nothing else. An @cmd's
    field block ends at the first blank line; everything after is body.
    """
    topics, groups, order = {}, {}, []
    for path in paths:
        cur = None
        in_fields = False
        for n, raw in enumerate(open(path, encoding="utf-8"), 1):
            line = raw.rstrip("\n").rstrip()
            where = f"{os.path.basename(path)}:{n}"
            if line.startswith("%%") or line.startswith("@essentials"):
                continue  # @essentials: read_essentials()
            bad = [c for c in line if c not in ALLOWED]
            if bad:
                raise HelpError(f"{where}: characters not in the calculator font: {''.join(sorted(set(bad)))!r}")
            if line.startswith("@"):
                m = re.match(r"^@(group|page|cmd|category)\s+(\S+)\s*(.*)$", line)
                if not m:
                    raise HelpError(f"{where}: bad directive {line!r}")
                kind, tid, title = m.groups()
                if not ID_RE.match(tid):
                    raise HelpError(f"{where}: bad topic id {tid!r}")
                if tid in topics:
                    raise HelpError(f"{where}: duplicate topic id {tid!r} (first at {topics[tid].where})")
                if kind != "cmd" and not title:
                    raise HelpError(f"{where}: @{kind} needs a title")
                cur = Topic(tid, title or tid, kind, where)
                topics[tid] = cur
                order.append(tid)
                if kind == "group":
                    groups[tid] = cur
                in_fields = kind in ("cmd", "category")
                continue
            if cur is None:
                if line.strip():
                    raise HelpError(f"{where}: text before the first @page/@cmd/@group")
                continue
            if in_fields:
                if not line.strip():
                    in_fields = False
                    continue
                m = re.match(r"^([a-z]+):\s?(.*)$", line)
                allowed = (("groups", "commands", "apps") if cur.kind == "category" else
                           ("group", "summary", "syntax", "insert", "suffix", "label", "catalog", "example",
                            "result", "see", "title"))
                if not m or m.group(1) not in allowed:
                    raise HelpError(f"{where}: expected a field (group/summary/syntax/...) or a blank line, "
                                    f"got {line!r}")
                k, v = m.groups()
                if k == "result":
                    ex = cur.fields.get("example")
                    if not ex or ex[-1][1] is not None:
                        raise HelpError(f"{where}: result: without a preceding example:")
                    ex[-1] = (ex[-1][0], v)
                elif k == "example":
                    cur.fields.setdefault("example", []).append((v, None))
                else:
                    cur.fields.setdefault(k, []).append(v)
                continue
            cur.body.append(line)
    return topics, groups, order


def read_essentials(paths):
    """Topic ids named by @essentials lines (in order, without duplicates)."""
    out = []
    for path in paths:
        for line in open(path, encoding="utf-8"):
            if line.startswith("@essentials"):
                for tid in line.split()[1:]:
                    if tid not in out:
                        out.append(tid)
    return out


def strip_blank(lines):
    """Drop leading and trailing blank lines."""
    while lines and not lines[0].strip():
        lines = lines[1:]
    while lines and not lines[-1].strip():
        lines = lines[:-1]
    return lines


def one(t, k, default=None, required=False):
    """Value of single-valued @cmd field `k` of topic t (default if absent);
    HelpError if it is given twice, or missing and required."""
    v = t.fields.get(k)
    if not v:
        if required:
            raise HelpError(f"{t.where}: @cmd {t.id} needs '{k}:'")
        return default
    if len(v) > 1:
        raise HelpError(f"{t.where}: @cmd {t.id}: '{k}:' given twice")
    return v[0]


def split_list(t, k):
    """The comma-separated values of the (possibly repeated) field `k` of topic t."""
    return [v.strip() for v in ",".join(t.fields.get(k, [])).split(",") if v.strip()]


def assign_categories(topics, order):
    """CATALOG categories: returns ([(id, title, [app ids])], {command id: category index}).

    Categories come in source order. A command listed by name in `commands:`
    belongs to that category; otherwise the first category whose `groups:`
    names its group takes it; the rest go to a generated last category
    "Other" (id None, no page), added only when needed. Unknown groups or
    command names are errors, so a renamed command cannot silently drop out.
    """
    cats = [topics[tid] for tid in order if topics[tid].kind == "category"]
    by_name, by_group = {}, {}
    for k, c in enumerate(cats):
        for name in split_list(c, "commands"):
            if name not in topics or topics[name].kind != "cmd":
                raise HelpError(f"{c.where}: category {c.id}: unknown command {name!r}")
            if name in by_name:
                raise HelpError(f"{c.where}: command {name!r} is already in category {cats[by_name[name]].id}")
            by_name[name] = k
        for gid in split_list(c, "groups"):
            if gid not in topics or topics[gid].kind != "group":
                raise HelpError(f"{c.where}: category {c.id}: unknown group {gid!r}")
            by_group.setdefault(gid, k)
    out = [(c.id, c.title, split_list(c, "apps")) for c in cats]
    member = {}
    for tid in order:
        t = topics[tid]
        if t.kind != "cmd":
            continue
        k = by_name.get(tid, by_group.get(one(t, "group")))
        if k is None:
            if not out or out[-1][0] is not None:
                out.append((None, "Other", []))
            k = len(out) - 1
        member[tid] = k
    if len(out) > 255:
        raise HelpError("more than 255 categories")
    return out, member


BODY_EX_RE = re.compile(r"^(example|result): ?(.*)$")


def body_examples(t, lines, examples):
    """Expand "example: INPUT" / "result: OUTPUT" lines of a body into code lines
    ("    INPUT", "    ▶ OUTPUT") and append (topic id, input, output) to examples."""
    out, last = [], None
    for ln in lines:
        m = BODY_EX_RE.match(ln)
        if not m:
            out.append(ln)
            last = None
            continue
        k, v = m.groups()
        if k == "example":
            out.append("    " + v)
            last = v
        else:
            if last is None:
                raise HelpError(f"{t.where}: topic {t.id}: result: without a preceding example: in the body")
            out.append("    ▶ " + v)
            examples.append((t.id, last, v))
            last = None
    return out


def build(topics, groups, order):
    """Turn sources into final markup texts; returns (texts, catalog, examples, categories).

    texts:    {topic id: markup text}. A group page is its intro followed by a
              bullet per member command (sorted case-insensitively); a command
              page is laid out as title, summary, Syntax, body, Examples (with
              "▶ result" lines), "See also" and the group link.
    catalog:  [(label, insert, suffix or None, topic id, summary, category
              index)] for CATALOG, sorted by label (case-insensitive).
    examples: [(topic id, input, expected output)] for every example with a
              result: line.
    categories: [(topic id or None, title, [app ids])] (assign_categories);
              a category page lists its commands like a group page.
    Raises HelpError for unknown groups/links, oversized topics, or a missing
    'index' page (the viewer's start page).
    """
    members = {g: [] for g in groups}
    for tid in order:
        t = topics[tid]
        if t.kind == "cmd":
            g = one(t, "group", required=True)
            if g not in groups:
                raise HelpError(f"{t.where}: unknown group {g!r}")
            members[g].append(t)
    categories, member = assign_categories(topics, order)
    texts, catalog, examples = {}, [], []
    for tid in order:
        t = topics[tid]
        if t.kind == "category":
            k = next(i for i, c in enumerate(categories) if c[0] == tid)
            out = [f"# {t.title}", ""] + strip_blank(t.body)
            cmds = sorted((topics[c] for c, m in member.items() if m == k), key=lambda c: c.id.lower())
            if cmds:
                out += [""] if strip_blank(t.body) else []
                for c in cmds:
                    out.append(f"- [[{c.id}]] {one(c, 'summary', '')}")
        elif t.kind == "page":
            out = [f"# {t.title}", ""] + body_examples(t, strip_blank(t.body), examples)
        elif t.kind == "group":
            out = [f"# {t.title}", ""] + strip_blank(t.body)
            if members[tid]:
                out += [""] if strip_blank(t.body) else []
                for c in sorted(members[tid], key=lambda c: c.id.lower()):
                    out.append(f"- [[{c.id}]] {one(c, 'summary', '')}")
        else:
            title = one(t, "title", t.id)
            t.title = title
            summary = one(t, "summary", required=True)
            syntax = t.fields.get("syntax")
            if not syntax:
                raise HelpError(f"{t.where}: @cmd {t.id} needs 'syntax:'")
            body = body_examples(t, strip_blank(t.body), examples)
            out = [f"# {title}", "", summary + "."]
            if body and body[0] == "## Refresher":
                # the refresher block (up to a "## Syntax" line, which is dropped) goes above the syntax
                if "## Syntax" not in body:
                    raise HelpError(f"{t.where}: @cmd {t.id}: '## Refresher' needs a '## Syntax' line after it")
                end = body.index("## Syntax")
                out += [""] + strip_blank(body[:end])
                body = strip_blank(body[end + 1:])
            out += ["", "## Syntax"]
            out += ["    " + s for s in syntax]
            if body:
                out += [""] + body
            exs = t.fields.get("example", [])
            if exs:
                out += ["", "## Example" + ("s" if len(exs) > 1 else "")]
                for inp, res in exs:
                    out.append("    " + inp)
                    if res is not None:
                        out.append("    ▶ " + res)
                        examples.append((t.id, inp, res))
            see = [s.strip() for s in ",".join(t.fields.get("see", [])).split(",") if s.strip()]
            g = one(t, "group")
            links = ", ".join(f"[[{s}]]" for s in see)
            out += ["", ("See also: " + links + ". " if links else "") + f"Group: [[{g}]]."]
            if one(t, "catalog", "yes") != "no":
                insert = one(t, "insert", t.id + "(")
                label = one(t, "label", insert)
                catalog.append((label, insert, one(t, "suffix", None), tid, one(t, "summary"), member[tid]))
        texts[tid] = "\n".join(out) + "\n"
    # links (only in prose: code lines and `code` spans may contain [[ ]] literally)
    for tid, text in texts.items():
        prose = "\n".join(CODE_SPAN_RE.sub("", ln) for ln in text.split("\n") if not ln.startswith("    "))
        for m in LINK_RE.finditer(prose):
            if m.group(1) not in texts:
                raise HelpError(f"{topics[tid].where}: link to unknown topic [[{m.group(1)}]]")
        if len(text.encode()) > MAX_TOPIC:
            raise HelpError(f"{topics[tid].where}: topic {tid} is {len(text.encode())} bytes (max {MAX_TOPIC})")
    for req in ("index",):
        if req not in texts:
            raise HelpError(f"missing required page '{req}'")
    catalog.sort(key=lambda c: (c[0].lower(), c[0]))
    return texts, catalog, examples, categories


# ------------------------------------------------------------------ compression
def lz77(data):
    """Greedy LZ77 with one-step lazy matching. Returns tokens: int literal or
    (length, distance).

    Candidate positions are found through a hash chain keyed on the next 3
    bytes (`heads`: every earlier position, in order); only the 48 most recent
    candidates are tried, which bounds the run time and keeps distances short
    (cheaper distance codes). The window is the whole topic, since topics are
    at most MAX_TOPIC bytes. Lazy matching: if the match starting one byte
    later is at least 2 bytes longer, emit a literal and take that one instead.
    """
    n = len(data)
    heads = {}
    out = []

    def best_at(i):
        """Longest match (length, distance) for position i, or (0, 0)."""
        if i + MIN_MATCH > n:
            return 0, 0
        key = data[i:i + 3]
        cands = heads.get(key)
        if not cands:
            return 0, 0
        bl, bd = 0, 0
        lim = min(MAX_MATCH, n - i)
        for p in reversed(cands[-48:]):
            # quick reject: a candidate can only beat the best so far if it
            # also matches at offset bl
            if bl and (p + bl >= n or data[p + bl] != data[i + bl]):
                continue
            ln = 3
            while ln < lim and data[p + ln] == data[i + ln]:
                ln += 1
            if ln > bl:
                bl, bd = ln, i - p
                if ln == lim:
                    break
        return bl, bd

    def insert(i):
        if i + 3 <= n:
            heads.setdefault(data[i:i + 3], []).append(i)

    i = 0
    while i < n:
        ln, d = best_at(i)
        if ln >= MIN_MATCH:
            ln2, _ = best_at(i + 1) if i + 1 < n else (0, 0)
            if ln2 > ln + 1:
                out.append(data[i])
                insert(i)
                i += 1
                continue
            out.append((ln, d))
            for k in range(i, i + ln):
                insert(k)
            i += ln
        else:
            out.append(data[i])
            insert(i)
            i += 1
    return out


def dist_code(d):
    """Split distance d >= 1 into (code, number of extra bits, extra value).

    Code 0 is distance 1. Otherwise with v = d - 1 and k = bit length of v,
    code k covers v in [2^(k-1), 2^k) and the extra bits hold v - 2^(k-1)
    (k - 1 bits): a log2 bucket plus the offset inside it.
    """
    if d == 1:
        return 0, 0, 0
    v = d - 1
    k = v.bit_length()          # 1..15
    return k, k - 1, v - (1 << (k - 1))


def huff_lengths(freq, maxbits=MAXBITS):
    """Huffman code length per symbol (0 = unused) for the given frequencies.

    Standard Huffman construction: repeatedly merge the two lightest subtrees,
    adding one bit to every symbol in both. If the longest code exceeds
    maxbits, all frequencies are halved (rounding up, so used symbols stay
    used) and the tree is rebuilt; flatter frequencies give a shallower tree.
    With fewer than two used symbols, two codes of length 1 are returned so
    the code is still complete and decodable.
    """
    freq = list(freq)
    while True:
        syms = [(f, i) for i, f in enumerate(freq) if f]
        lens = [0] * len(freq)
        if len(syms) == 0:
            lens[0] = lens[1] = 1
            return lens
        if len(syms) == 1:
            i = syms[0][1]
            lens[i] = 1
            lens[1 if i == 0 else 0] = 1
            return lens
        # (weight, tie-break counter, symbols in the subtree); the counter
        # keeps heap order deterministic without comparing the lists
        heap = [(f, k, [i]) for k, (f, i) in enumerate(syms)]
        heapq.heapify(heap)
        cnt = len(heap)
        while len(heap) > 1:
            f1, _, a = heapq.heappop(heap)
            f2, _, b = heapq.heappop(heap)
            for s in a + b:
                lens[s] += 1
            heapq.heappush(heap, (f1 + f2, cnt, a + b))
            cnt += 1
        if max(lens) <= maxbits:
            return lens
        freq = [(f + 1) // 2 if f else 0 for f in freq]


def canonical(lens):
    """Canonical Huffman codes for the given lengths (RFC 1951 section 3.2.2).

    Codes of each length are consecutive integers assigned in symbol order,
    and the first code of length b is (first code of length b-1 + count of
    length b-1) << 1. The decoder can therefore rebuild the code from the
    lengths alone.
    """
    bl = [0] * (MAXBITS + 2)
    for ln in lens:
        if ln:
            bl[ln] += 1
    code, nxt = 0, [0] * (MAXBITS + 2)
    for b in range(1, MAXBITS + 1):
        code = (code + bl[b - 1]) << 1
        nxt[b] = code
    codes = [0] * len(lens)
    for s, ln in enumerate(lens):
        if ln:
            codes[s] = nxt[ln]
            nxt[ln] += 1
    return codes


class BitWriter:
    """Accumulates bits MSB first into bytes; flush() pads the last byte with
    zero bits."""

    def __init__(self):
        self.out, self.acc, self.nb = bytearray(), 0, 0

    def put(self, v, n):  # MSB first
        for k in range(n - 1, -1, -1):
            self.acc = (self.acc << 1) | ((v >> k) & 1)
            self.nb += 1
            if self.nb == 8:
                self.out.append(self.acc)
                self.acc, self.nb = 0, 0

    def flush(self):
        if self.nb:
            self.out.append(self.acc << (8 - self.nb))
            self.acc, self.nb = 0, 0
        return bytes(self.out)


def compress_all(blobs):
    """Compress each byte string separately with one shared pair of codes.

    Pass 1 tokenizes every topic and counts symbol frequencies over the whole
    manual (one END per topic); the two Huffman codes are built from those
    totals; pass 2 encodes each topic into its own byte-aligned bit stream.
    Returns (compressed topics, literal/length code lengths, distance code
    lengths).
    """
    toks = [lz77(b) for b in blobs]
    lf, df = [0] * NLIT, [0] * NDIST
    for ts in toks:
        for t in ts:
            if isinstance(t, int):
                lf[t] += 1
            else:
                lf[257 + t[0] - MIN_MATCH] += 1
                df[dist_code(t[1])[0]] += 1
        lf[END] += 1
    llen, dlen = huff_lengths(lf), huff_lengths(df)
    lcode, dcode = canonical(llen), canonical(dlen)
    outs = []
    for ts in toks:
        w = BitWriter()
        for t in ts:
            if isinstance(t, int):
                w.put(lcode[t], llen[t])
            else:
                s = 257 + t[0] - MIN_MATCH
                w.put(lcode[s], llen[s])
                c, nb, extra = dist_code(t[1])
                w.put(dcode[c], dlen[c])
                if nb:
                    w.put(extra, nb)
        w.put(lcode[END], llen[END])
        outs.append(w.flush())
    return outs, llen, dlen


def decompress(data, llen, dlen, ulen):
    """Reference decoder (mirrors app/help.c) used to self-check the output.

    Decodes canonical Huffman symbols one bit at a time using only the count
    of codes per length and the symbols sorted by (length, symbol), the same
    small tables the firmware builds (the "puff" method from zlib's contrib).
    """
    def table(lens):
        count = [0] * (MAXBITS + 1)
        for ln in lens:
            if ln:
                count[ln] += 1
        syms = sorted((ln, s) for s, ln in enumerate(lens) if ln)
        return count, [s for _, s in syms]

    lt, dt = table(llen), table(dlen)
    pos = [0]

    def bit():
        b = (data[pos[0] >> 3] >> (7 - (pos[0] & 7))) & 1
        pos[0] += 1
        return b

    def sym(t):
        # code: bits read so far; first: first canonical code of the current
        # length; index: position in syms of that code's symbol. A code of
        # length ln is valid iff code - first < count[ln].
        count, syms = t
        code = first = index = 0
        for ln in range(1, MAXBITS + 1):
            code |= bit()
            c = count[ln]
            if code - first < c:
                return syms[index + code - first]
            index += c
            first = (first + c) << 1
            code <<= 1
        raise HelpError("bad code")

    out = bytearray()
    while True:
        s = sym(lt)
        if s < 256:
            out.append(s)
        elif s == END:
            break
        else:
            ln = s - 257 + MIN_MATCH
            c = sym(dt)
            if c == 0:
                d = 1
            else:
                extra = 0
                for _ in range(c - 1):
                    extra = (extra << 1) | bit()
                d = (1 << (c - 1)) + extra + 1
            for _ in range(ln):
                out.append(out[-d])
    if len(out) != ulen:
        raise HelpError("self-check: length mismatch")
    return bytes(out)


# ------------------------------------------------------------------ output
def c_bytes(name, data, attrs="FLASH2_RODATA "):
    """C definition of a uint8_t array (at least 1 element, as C forbids empty
    arrays), placed in flash bank 2 by default (core/sections.h)."""
    lines = [f"{attrs}const uint8_t {name}[{max(len(data), 1)}] = {{"]
    for i in range(0, len(data), 20):
        lines.append("    " + ", ".join(str(b) for b in data[i:i + 20]) + ",")
    if not data:
        lines.append("    0,")
    lines.append("};")
    return "\n".join(lines)


def main():
    """Command line entry (see the module docstring). Returns the exit status."""
    ap = argparse.ArgumentParser()
    ap.add_argument("--help-dir", default=os.path.join(HERE, "../help"))
    ap.add_argument("--out-c")
    ap.add_argument("--examples")
    ap.add_argument("--report", action="store_true")
    ap.add_argument("--extra", action="append", default=[], help="more source files (generated pages)")
    ap.add_argument("--content", choices=("internal", "qspi"), default="internal",
                    help="where the compressed topics go (see the module docstring)")
    ap.add_argument("--out-module-payload", help="qspi: write the manual module payload here")
    a = ap.parse_args()
    paths = sorted(glob.glob(os.path.join(a.help_dir, "*.txt"))) + a.extra
    try:
        topics, groups, order = parse_files(paths)
        check_keycaps(topics)
        texts, catalog, examples, categories = build(topics, groups, order)
        essentials = read_essentials(paths)
        for tid in essentials:
            if tid not in texts:
                raise HelpError(f"@essentials: unknown topic {tid!r}")
        if "index" not in essentials:
            raise HelpError("@essentials must include 'index' (the viewer's start page)")
    except HelpError as e:
        print("gen_help:", e, file=sys.stderr)
        return 1
    ids = sorted(texts)                     # bytewise order = strcmp order
    raw = [texts[i].encode("utf-8") for i in ids]
    comp, llen, dlen = compress_all(raw)
    for r, c in zip(raw, comp):
        assert decompress(c, llen, dlen, len(r)) == r
    # string pool: ids, titles, catalog strings, NUL-terminated and
    # deduplicated; tables refer to them by byte offset (offset 0 = "")
    pool = bytearray()
    offs = {}

    def s(text):
        if text not in offs:
            offs[text] = len(pool)
            pool.extend(text.encode("utf-8") + b"\0")
        return offs[text]

    s("")
    index = {tid: k for k, tid in enumerate(ids)}
    # topic rows: (id, title, offset in help_blob, compressed size, raw size);
    # catalog rows: (label, insert, suffix or 0, topic index)
    trows, blob = [], bytearray()
    for tid, r, c in zip(ids, raw, comp):
        trows.append((s(tid), s(topics[tid].title), len(blob), len(c), len(r)))
        blob.extend(c)
    crows = [(s(lb), s(ins), s(suf) if suf else 0, index[tid]) for lb, ins, suf, tid, _, _ in catalog]
    # CATALOG extras: per entry the summary (search) and category; per category
    # its title, page (topic index or -1) and app ids ("id,id" or 0)
    xrows = [(s(summ), cat) for _, _, _, _, summ, cat in catalog]
    krows = [(s(title), s(",".join(apps)) if apps else 0, index[cid] if cid else -1)
             for cid, title, apps in categories]
    total_raw = sum(len(r) for r in raw)
    # the manual module payload (help ABI 1, see the module docstring)
    mpool, moffs = bytearray(), {}

    def ms(text):
        if text not in moffs:
            moffs[text] = len(mpool)
            mpool.extend(text.encode("utf-8") + b"\0")
        return moffs[text]

    ms("")
    mtopics = b"".join(struct.pack("<IIIII", ms(tid), ms(topics[tid].title), r[2], r[3], r[4])
                       for tid, r in zip(ids, trows))
    topics_off = 32
    strings_off = topics_off + len(mtopics)
    lens_off = strings_off + len(mpool)
    blob_off = lens_off + NLIT + NDIST
    payload = (b"HLP1" + struct.pack("<7I", len(trows), topics_off, strings_off, len(mpool), lens_off, blob_off,
                                     len(blob))
               + mtopics + bytes(mpool) + bytes(llen) + bytes(dlen) + bytes(blob))
    # in internal flash: everything (internal), or only the essentials (qspi)
    ess_rows, flash_blob = [], bytearray()
    if a.content == "qspi":
        for tid in sorted(essentials):
            k = index[tid]
            ess_rows.append((k, len(flash_blob)))
            flash_blob.extend(comp[k])
    else:
        flash_blob = blob
    if a.out_module_payload:
        out = a.out_module_payload
        os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
        with open(out + ".tmp", "wb") as f:
            f.write(payload)
        os.replace(out + ".tmp", out)
    if a.out_c:
        o = ["/* Generated by tools/gen_help.py from firmware/help/ (*.txt). Do not edit. */",
             '#include "help_data.h"', '#include "sections.h"', "",
             c_bytes("help_strings_bytes", bytes(pool)),
             "const char *const help_strings = (const char *)help_strings_bytes;", "",
             f"FLASH2_RODATA const help_topic_t help_topics[{len(trows)}] = {{"]
        for r in trows:
            o.append("    {%d, %d, %d, %d, %d}," % r)
        o += ["};", f"const uint16_t help_ntopics = {len(trows)};", "",
              f"FLASH2_RODATA const help_cat_t help_catalog[{max(len(crows), 1)}] = {{"]
        for r in crows:
            o.append("    {%d, %d, %d, %d}," % r)
        if not crows:
            o.append("    {0, 0, 0, -1},")
        o += ["};", f"const uint16_t help_ncatalog = {len(crows)};", "",
              f"FLASH2_RODATA const uint32_t help_catalog_summary[{max(len(xrows), 1)}] = {{"]
        o += ["    %d," % r[0] for r in xrows] or ["    0,"]
        o += ["};", f"FLASH2_RODATA const uint8_t help_catalog_cat[{max(len(xrows), 1)}] = {{"]
        o += ["    %d," % r[1] for r in xrows] or ["    0,"]
        o += ["};", f"FLASH2_RODATA const help_category_t help_categories[{max(len(krows), 1)}] = {{"]
        o += ["    {%d, %d, %d}," % r for r in krows] or ["    {0, 0, -1},"]
        o += ["};", f"const uint16_t help_ncategories = {len(krows)};", "",
              c_bytes("help_lit_lens", bytes(llen)), c_bytes("help_dist_lens", bytes(dlen)),
              c_bytes("help_blob", bytes(flash_blob)), f"const uint32_t help_blob_size = {len(blob)};",
              f"const uint32_t help_raw_size = {total_raw};",
              f"const uint8_t help_all_resident = {1 if a.content == 'internal' else 0};",
              f"FLASH2_RODATA const help_ess_t help_ess[{max(len(ess_rows), 1)}] = {{"]
        o += ["    {%d, %d}," % r for r in ess_rows] or ["    {0, 0},"]
        o += ["};", f"const uint16_t help_ness = {len(ess_rows)};",
              ""]
        os.makedirs(os.path.dirname(os.path.abspath(a.out_c)), exist_ok=True)
        tmp = a.out_c + ".tmp"  # write + rename: make never sees a half-written file
        with open(tmp, "w", encoding="utf-8") as f:
            f.write("\n".join(o))
        os.replace(tmp, a.out_c)
    if a.examples:
        with open(a.examples, "w", encoding="utf-8") as f:
            f.write("# Generated by tools/gen_help.py: every help example with a result: line,\n"
                    "# evaluated by the real CAS (each command page starts from a clean CAS).\n"
                    "# Commands of the advanced CAS module (casx) need it installed: the standard\n"
                    "# tier installs the release's modules from the simulated card first.\n"
                    "ifcfg CONTENT_QSPI ifcfg !MOD_CASX fs insert 64\n"
                    "ifcfg CONTENT_QSPI ifcfg !MOD_CASX fs format\n"
                    "ifcfg CONTENT_QSPI ifcfg !MOD_CASX module library\n"
                    "ifcfg CONTENT_QSPI ifcfg !MOD_CASX module installall\n")
            last = None
            for tid, inp, res in examples:
                if tid != last:
                    f.write(f"# {tid}\nifcas src casreset\n")
                    last = tid
                q = lambda t: '"' + t.replace("\\", "\\\\").replace('"', '\\"') + '"'
                f.write(f"ifcas src expect eval {q(inp)} {q(res)}\n")
    if a.report or a.out_c:
        # help_topic_t / help_cat_t / help_category_t (app/help_data.h) are 5, 4 and 3
        # 32-bit fields; each catalog entry also has a summary offset and a category byte
        fixed = len(pool) + len(trows) * 20 + len(crows) * 21 + len(krows) * 12 + NLIT + NDIST
        where = (f"in flash bank 2" if a.content == "internal" else
                 f"in the manual module ({len(payload)} B payload); flash bank 2 keeps "
                 f"{len(ess_rows)} essential topics ({len(flash_blob)} B)")
        print(f"help: {len(ids)} topics, {len(catalog)} catalog entries in {len(categories)} categories, "
              f"{len(examples)} checked examples; "
              f"text {total_raw} B -> {len(blob)} B compressed ({100.0 * len(blob) / max(total_raw, 1):.1f} %) "
              f"{where}, + {fixed} B tables (flash bank 2)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
