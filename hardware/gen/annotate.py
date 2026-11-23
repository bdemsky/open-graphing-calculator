#!/usr/bin/env python3
"""Board metadata pass on the committed, routed kicad/calc.kicad_pcb.

  annotate.py                 sync footprint values from build/calc.net and the
                              title block from design.TITLE_BLOCK (rev A.1)
  annotate.py --key-labels    also add key-function silkscreen labels
                              (FUTURE ORDERS ONLY, off by default; see below)

The routed board is not regenerated (pcb_build.py re-runs Freerouting and would
not reproduce the ordered copper), so documentation changes are applied here as
surgical text edits, like cleanup.py: footprint "Value" properties (F.Fab, not
exported to any fabrication layer) and the title block. Nothing that reaches
the Gerbers, drill files, BOM or CPL changes; references and positions are never
touched. Run build.sh first (it exports build/calc.net).

--key-labels (FUTURE ORDERS ONLY): puts the logical key name (firmware/keys/
layout.py, e.g. SHIFT, ASSIGN) on F.SilkS beside each keypad switch, rotated
90 deg. Each label tries a few spots in turn (right of the switch, left, a
little up or down), spots that clear every via first; KiCad DRC on a scratch
copy then decides: a label that causes any silkscreen-over-pad, silkscreen
overlap or edge violation is moved to its next spot. It fails if any label
finds no clean spot. This changes F.SilkS (and only F.SilkS), so it is not what
was ordered for rev A; the committed board and fab outputs are made without it.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from kicad_pcb import load_netlist  # noqa: E402
from kicad_sch import uid  # noqa: E402

KICAD_CLI = "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"
BOARD = os.path.join(HERE, "../kicad/calc.kicad_pcb")
NETLIST = os.path.join(HERE, "../build/calc.net")

LABEL_SIZE = 0.8             # mm text height (refdes on this board are 1.0, the J3 pin-1 mark 0.8)
# Candidate spots (dx, dy, rot) relative to the switch centre, tried in order. Switch pads
# reach x = +-3.5, the refdes sits at y = -5.85; rotated labels beside the pads stay clear of
# both and of the neighbouring keys (15 mm column pitch, D-pad 8.5 mm).
LABEL_SPOTS = [(4.6, 0, 90), (-4.6, 0, 90), (4.6, 1.5, 90), (4.6, -1.5, 90), (-4.6, 1.5, 90),
               (-4.6, -1.5, 90), (4.6, 2.5, 90), (4.6, -2.5, 90), (-4.6, 2.5, 90), (-4.6, -2.5, 90)]
VIA_MARGIN = 0.15            # mm kept between a label and a via pad (vias are tented; this is cosmetic)
SILK_CHECKS = {"silk_over_copper", "silk_overlap", "silk_edge_clearance", "text_height",
               "text_thickness", "courtyards_overlap", "silk_clearance"}


def title_block_text(tb, indent="\t"):
    """KiCad-formatted title_block for the board, from design.TITLE_BLOCK."""
    q = lambda s: '"' + s.replace('"', '\\"') + '"'
    lines = [f"{indent}(title_block", f"{indent}\t(title {q(tb['title'])})"]
    if tb.get("date"):
        lines.append(f"{indent}\t(date {q(tb['date'])})")
    if tb.get("rev"):
        lines.append(f"{indent}\t(rev {q(tb['rev'])})")
    if tb.get("company"):
        lines.append(f"{indent}\t(company {q(tb['company'])})")
    lines += [f"{indent}\t(comment {i} {q(c)})" for i, c in enumerate(tb.get("comments", []), 1)]
    return "\n".join(lines + [f"{indent})"])


def footprint_blocks(text):
    """{ref: (start, end)} of every top-level footprint block (one tab of indentation)."""
    out = {}
    for m in re.finditer(r"\n\t\(footprint ", text):
        s = m.start() + 1
        e = text.find("\n\t)", s) + 3
        r = re.search(r'\(property "Reference" "([^"]*)"', text[s:e])
        if r:
            out[r.group(1)] = (s, e)
    return out


def sync_values(text, comps):
    """Set each footprint's Value property to the netlist value; returns (text, changes)."""
    changes = []
    blocks = footprint_blocks(text)
    for ref, (s, e) in sorted(blocks.items(), key=lambda kv: -kv[1][0]):   # back to front
        if ref not in comps:
            continue
        blk = text[s:e]
        m = re.search(r'\(property "Value" "((?:[^"\\]|\\.)*)"', blk)
        new = comps[ref]["value"].replace('"', '\\"')
        if m and m.group(1) != new:
            changes.append((ref, m.group(1), new))
            blk = blk[:m.start(1)] + new + blk[m.end(1):]
            text = text[:s] + blk + text[e:]
    return text, sorted(changes)


def set_title_block(text, tb):
    """Replace the board's title_block."""
    new = title_block_text(tb)
    out, n = re.subn(r"\t\(title_block\n(?:\t\t[^\n]*\n)*\t\)", lambda _: new, text, count=1)
    assert n == 1, "no title_block in the board"
    return out


def key_switches(text, comps):
    """[(ref, name, x, y)] for the keypad switches (value = logical key name)."""
    out = []
    for ref, (s, e) in footprint_blocks(text).items():
        c = comps.get(ref)
        if not c or c.get("sheetname") != "/Keypad/" or not ref.startswith("SW"):
            continue
        at = re.search(r"\n\t\t\(at ([-\d.]+) ([-\d.]+)", text[s:e])
        out.append((ref, c["value"], float(at.group(1)), float(at.group(2))))
    return sorted(out, key=lambda k: int(k[0][2:]))


def label_text(ref, name, x, y, spot):
    """One gr_text block on F.SilkS for a key label at candidate spot."""
    dx, dy, rot = spot
    f = lambda v: f"{round(v, 4):g}"
    return (f'\t(gr_text "{name}"\n\t\t(at {f(x + dx)} {f(y + dy)} {rot})\n\t\t(layer "F.SilkS")\n'
            f'\t\t(uuid {uid("keylabel", ref)})\n\t\t(effects\n\t\t\t(font\n'
            f'\t\t\t\t(size {f(LABEL_SIZE)} {f(LABEL_SIZE)})\n\t\t\t\t(thickness {f(LABEL_SIZE * 0.15)})\n'
            f'\t\t\t)\n\t\t)\n\t)\n')


def vias(text):
    """[(x, y, radius)] of every via on the board."""
    return [(float(m.group(1)), float(m.group(2)), float(m.group(3)) / 2)
            for m in re.finditer(r"\n\t\(via\n\t\t\(at ([-\d.]+) ([-\d.]+)\)\n\t\t\(size ([\d.]+)\)", text)]


def label_box(name, x, y, spot):
    """Conservative bounding box (x0, y0, x1, y1) of a label (stroke font: ~0.85 x size per
    character along the text, size + stroke across it)."""
    dx, dy, rot = spot
    along, across = len(name) * LABEL_SIZE * 0.85 / 2, LABEL_SIZE * 1.15 / 2
    hx, hy = (across, along) if rot in (90, 270) else (along, across)
    return x + dx - hx, y + dy - hy, x + dx + hx, y + dy + hy


def candidates(keys, via_list):
    """({ref: [spot index, ...]}, {ref: number of via-clear spots}): the spots that keep
    VIA_MARGIN from every via first, in LABEL_SPOTS order, then the others."""
    out, n_clear = {}, {}
    for ref, name, x, y in keys:
        clear, hit = [], []
        for i, spot in enumerate(LABEL_SPOTS):
            x0, y0, x1, y1 = label_box(name, x, y, spot)
            bad = any(x0 - r - VIA_MARGIN < vx < x1 + r + VIA_MARGIN and y0 - r - VIA_MARGIN < vy < y1 + r + VIA_MARGIN
                      for vx, vy, r in via_list)
            (hit if bad else clear).append(i)
        out[ref], n_clear[ref] = clear + hit, len(clear)
    return out, n_clear


def strip_labels(text, keys):
    """Board text without key labels from an earlier --key-labels run (matched by uuid)."""
    for ref, *_ in keys:
        u = re.escape(uid("keylabel", ref))
        text = re.sub(r"\t\(gr_text \"[^\"]*\"\n(?:\t\t[^\n]*\n)*?\t\t\(uuid " + u + r"\)\n(?:\t\t[^\n]*\n)*?\t\)\n",
                      "", text)
    return text


def with_labels(text, keys, choice):
    """Board text with one label per key (choice: ref -> LABEL_SPOTS index) before the tracks."""
    text = strip_labels(text, keys)
    labels ="".join(label_text(ref, name, x, y, LABEL_SPOTS[choice[ref]]) for ref, name, x, y in keys)
    i = text.find("\n\t(segment")
    if i < 0:
        i = text.rstrip().rfind("\n)")
    return text[:i + 1] + labels + text[i + 1:]


def drc(text, workdir):
    """KiCad DRC (JSON) of board text in workdir (with the project's rules); returns the report."""
    pcb = os.path.join(workdir, "calc.kicad_pcb")
    open(pcb, "w").write(text)
    rpt = os.path.join(workdir, "drc.json")
    subprocess.run([KICAD_CLI, "pcb", "drc", "--severity-all", "--format", "json", "-o", rpt, pcb],
                   check=True, capture_output=True)
    return json.load(open(rpt))


def silk_violations(report, uuids):
    """{uuid: [violation types]} for the given text uuids."""
    bad = {}
    for v in report["violations"]:
        if v["type"] not in SILK_CHECKS:
            continue
        for it in v.get("items", []):
            if it.get("uuid") in uuids:
                bad.setdefault(it["uuid"], []).append(v["type"])
    return bad


def place_labels(text, keys):
    """Choose a DRC-clean spot per key label, via-clear spots first. Returns (text with
    labels, {ref: LABEL_SPOTS index}, [refs whose label still crosses a via])."""
    cand, n_clear = candidates(keys, vias(text))
    pos = {ref: 0 for ref, *_ in keys}             # index into cand[ref]
    uuid_ref = {uid("keylabel", ref).strip('"'): ref for ref, *_ in keys}
    work = tempfile.mkdtemp(prefix="keylabels-")
    try:
        shutil.copy(os.path.join(HERE, "../kicad/calc.kicad_pro"), work)
        for _ in range(len(LABEL_SPOTS)):
            choice = {ref: cand[ref][i] for ref, i in pos.items()}
            out = with_labels(text, keys, choice)
            bad = silk_violations(drc(out, work), set(uuid_ref))
            if not bad:
                return out, choice, sorted(r for r, i in pos.items() if i >= n_clear[r])
            for u, types in bad.items():
                ref = uuid_ref[u]
                if pos[ref] + 1 >= len(LABEL_SPOTS):
                    sys.exit(f"no clean spot for the {ref} label: {types}")
                pos[ref] += 1
        sys.exit("key labels did not converge")
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--key-labels", action="store_true",
                    help="add key-function labels on F.SilkS (FUTURE ORDERS ONLY; changes the silkscreen)")
    ap.add_argument("--board", default=BOARD)
    ap.add_argument("--netlist", default=NETLIST)
    args = ap.parse_args()
    import design   # noqa: E402  (title block; builds the design in memory, writes nothing)
    comps, _ = load_netlist(args.netlist)
    text = open(args.board).read()
    text, changes = sync_values(text, comps)
    text = set_title_block(text, design.TITLE_BLOCK)
    for ref, old, new in changes:
        print(f"  {ref}: value {old} -> {new}")
    if args.key_labels:
        keys = key_switches(text, comps)
        text, choice, over_via = place_labels(text, keys)
        moved = {r: LABEL_SPOTS[c] for r, c in choice.items() if c}
        print(f"key labels: {len(keys)} on F.SilkS (FUTURE ORDERS ONLY); DRC clean; "
              f"off the default spot: {moved or 'none'}; crossing a (tented) via: {over_via or 'none'}")
    else:   # default: the board as ordered, without labels from an earlier --key-labels run
        text = strip_labels(text, key_switches(text, comps))
    open(args.board, "w").write(text)
    print(f"{len(changes)} values updated, title block rev {design.TITLE_BLOCK.get('rev')}; "
          f"wrote {os.path.relpath(args.board)}")


if __name__ == "__main__":
    main()
