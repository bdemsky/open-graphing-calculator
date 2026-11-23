/*
 * Help viewer: decompresses a topic of the built-in manual (help_data.h,
 * generated from firmware/help/ (*.txt)), lays out its markup (headings,
 * paragraphs, bullets, code, links, key caps) with word wrap, and shows it
 * scrollable with link navigation and a back history.
 *
 * Keys: UP/DOWN scroll, + / - page, LEFT/RIGHT select the previous/next link,
 * ENTER follows it, ⌫ goes back, CLEAR leaves help, SHIFT HELP opens the index; soft keys index, back, page up/down, exit.
 * Touch: drag to scroll, tap a link to follow it.
 *
 * Where pages come from (docs/QSPI_CONTENT.md): the CONTENT=internal
 * (core tier) build has the whole compressed manual in flash. Otherwise
 * flash holds the tables (ids, titles, CATALOG) and the @essentials pages,
 * and the full manual is the "manual" module in the QSPI (core/modstore.h),
 * read through mod_read(): a self-contained payload with its own topic table
 * and Huffman code (format "help ABI", tools/gen_help.py). A page is taken
 * from the module when it is installed and compatible, else from the
 * built-in essentials; any other page shows "Manual not installed: see
 * Memory > Install content" with the reason and the list of built-in
 * pages (show_missing), so help never fails.
 *
 * Pipeline for a page:
 *   1. inflate_topic(): decompress the topic's markup into src[] (format in
 *      app/help_data.h); a module page's compressed bytes are first read
 *      from the QSPI into text[], which layout() overwrites afterwards;
 *      generated pages (help_show_text, e.g. About) are written into src[]
 *      directly.
 *   2. layout(): parse the markup line by line (headings, bullets, code,
 *      paragraphs joined across lines), convert inline markup into text[]
 *      with one-byte style codes (S_*), and word-wrap it into display lines
 *      (lines[]: offset/length into text[], line kind, style at the line
 *      start). Links are collected in links[] with their target ids.
 *   3. help_draw(): draw the visible lines from `top`, run by run of
 *      constant style, plus a scroll bar.
 * The three buffers come from one workspace carved from the arena by
 * app_init() (HELP_WORK = 128 KB; 16 KB with the small fallback arena, where
 * long topics are truncated), so the viewer needs no
 * static RAM beyond its link table and history.
 *
 * Plugs in as: scr_help (opened with help_open / help_context, never
 * directly), the app "help" (APPS > Help), and help_context() for the
 * SHIFT HELP global action, which resolves the topic for the current screen,
 * menu item or settings row. Leaving help returns to the screen it was opened
 * from with ui_returning set, so that screen keeps its state.
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "help_data.h"
#include "modstore.h"
#include "modules_ui.h"
#include "registry.h"
#include "safemode.h"
#include "ui.h"

#define MANUAL_MODULE "manual"

/* ------------------------------------------------------------ decoder */
/* Canonical Huffman decoding table in the compact form used by zlib's
 * "puff" decoder: the number of codes of each length, and the symbols sorted
 * by (code length, symbol value). For a canonical code that is enough to
 * decode: codes of one length are consecutive integers, and the first code of
 * length L+1 is (first code of length L + count[L]) << 1. */
typedef struct {
    uint16_t count[HELP_MAXBITS + 1]; /* codes per length; count[0] unused */
    uint16_t sym[HELP_NLIT];          /* symbols in canonical order */
} huff_t;
static huff_t hlit, hdist;  /* literal/length and distance tables, built on first use */
static const uint8_t *tables_for; /* code lengths the tables were built from (flash or the module's) */

/* Build a decoding table from the code lengths of n symbols (0 = unused):
 * count codes per length, compute where each length's symbols start in
 * sym[] (offs), then place the symbols in increasing order. */
static void huff_build(huff_t *h, const uint8_t *lens, int n)
{
    uint16_t offs[HELP_MAXBITS + 2];
    memset(h->count, 0, sizeof h->count);
    for (int s = 0; s < n; s++)
        h->count[lens[s]]++;
    h->count[0] = 0;
    offs[1] = 0;
    for (int l = 1; l <= HELP_MAXBITS; l++)
        offs[l + 1] = (uint16_t)(offs[l] + h->count[l]);
    for (int s = 0; s < n; s++)
        if (lens[s])
            h->sym[offs[lens[s]]++] = (uint16_t)s;
}

/* MSB-first bit reader over [p, end). */
typedef struct {
    const uint8_t *p, *end;
    uint32_t bit; /* bits consumed */
} bits_t;

/* The manual module (help ABI 1 payload, see tools/gen_help.py): its header
 * and code lengths, read on first use after boot or after an install. */
static struct {
    bool tried, ok;
    uint32_t ntopics, topics_off, strings_off, strings_len, lens_off, blob_off, blob_len;
    uint8_t lit[HELP_NLIT], dist[HELP_NDIST];
} mm;

/* One topic row of the module (help_topic_t layout, offsets into its strings / blob). */
typedef struct {
    uint32_t id, title, off, clen, ulen;
} mm_topic_t;

void help_content_changed(void)
{
    mm.tried = mm.ok = false;
    tables_for = NULL;
}

/* Header of the manual module, if it is installed, compatible and sane. */
static bool mm_open(void)
{
    if (help_all_resident)
        return false;
    if (mm.tried)
        return mm.ok;
    mm.tried = true;
    mm.ok = false;
    if (mod_state(MANUAL_MODULE) != MOD_OK)
        return false;
    uint32_t h[8];
    if (mod_read(MANUAL_MODULE, 0, h, sizeof h) || memcmp(h, "HLP1", 4))
        return false;
    mm.ntopics = h[1], mm.topics_off = h[2], mm.strings_off = h[3], mm.strings_len = h[4];
    mm.lens_off = h[5], mm.blob_off = h[6], mm.blob_len = h[7];
    if (mm.ntopics > 4096 || mod_read(MANUAL_MODULE, mm.lens_off, mm.lit, HELP_NLIT) ||
        mod_read(MANUAL_MODULE, mm.lens_off + HELP_NLIT, mm.dist, HELP_NDIST))
        return false;
    mm.ok = true;
    return true;
}

/* NUL-terminated string at offset `off` of the module's strings (truncated to n-1). */
static bool mm_string(uint32_t off, char *out, size_t n)
{
    out[0] = 0;
    if (off >= mm.strings_len)
        return false;
    size_t m = mm.strings_len - off < n - 1 ? mm.strings_len - off : n - 1;
    if (mod_read(MANUAL_MODULE, mm.strings_off + off, out, m))
        return false;
    out[m] = 0;
    return true;
}

/* Binary search of the module's topic table (sorted by id with strcmp). */
static bool mm_find(const char *id, mm_topic_t *t)
{
    if (!mm_open())
        return false;
    int lo = 0, hi = (int)mm.ntopics - 1;
    char buf[44];
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (mod_read(MANUAL_MODULE, mm.topics_off + (uint32_t)mid * sizeof *t, t, sizeof *t) ||
            !mm_string(t->id, buf, sizeof buf))
            return false;
        int c = strcmp(id, buf);
        if (!c)
            return true;
        if (c < 0)
            hi = mid - 1;
        else
            lo = mid + 1;
    }
    return false;
}

/* Next bit (0/1), or -1 past the end of the data given (all of the topic, or
 * as much of it as fitted the read buffer). */
static int getbit(bits_t *b)
{
    const uint8_t *q = b->p + (b->bit >> 3);
    if (q >= b->end)
        return -1;
    int v = (*q >> (7 - (b->bit & 7))) & 1;
    b->bit++;
    return v;
}

/* Decode one symbol, one bit at a time. After reading `len` bits, `code` is
 * the code so far, `first` the first canonical code of that length and
 * `index` the position of that length's first symbol in sym[]; if
 * code - first < count[len] the code is complete. Returns -1 on a truncated
 * stream or an invalid code. Bit-serial: slower than a lookup table, but it
 * needs no memory beyond huff_t. */
static int huff_decode(bits_t *b, const huff_t *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= HELP_MAXBITS; len++) {
        int v = getbit(b);
        if (v < 0)
            return -1;
        code |= v;
        int c = h->count[len];
        if (code - first < c)
            return h->sym[index + code - first];
        index += c;
        first = (first + c) << 1;
        code <<= 1;
    }
    return -1;
}

/* Decompress one topic (clen compressed bytes at data, coded with the code
 * lengths lit/dist) into out (max bytes); returns the length or -1.
 * If the topic is larger than `max` the output is cut there (the caller
 * compares with the topic's raw size and shows "(truncated)"); `cut` says
 * the input itself was cut (a small read buffer), so running out of input
 * ends the page instead of failing. Matches copy byte by byte because they
 * may overlap their own output (distance < length repeats a pattern). A
 * distance reaching before the start of the topic is corrupt data (-1). */
static long inflate_topic(const uint8_t *data, uint32_t clen, bool cut, const uint8_t *lit, const uint8_t *dist,
                          char *out, size_t max)
{
    if (tables_for != lit) {
        huff_build(&hlit, lit, HELP_NLIT);
        huff_build(&hdist, dist, HELP_NDIST);
        tables_for = lit;
    }
    bits_t b = {data, data + clen, 0};
    size_t o = 0;
    for (;;) {
        int s = huff_decode(&b, &hlit);
        if (s < 0)
            return cut ? (long)o : -1;
        if (s < 256) {
            if (o >= max)
                return (long)o; /* truncated (small workspace) */
            out[o++] = (char)s;
        } else if (s == 256) {
            break;
        } else {
            /* match: length from the symbol, then distance code c:
             * 0 -> 1, c >= 1 -> 2^(c-1) + 1 + (c-1 extra bits, MSB first) */
            int len = s - 257 + HELP_MIN_MATCH;
            int c = huff_decode(&b, &hdist);
            if (c < 0)
                return cut ? (long)o : -1;
            size_t d = 1;
            if (c > 0) {
                uint32_t extra = 0;
                for (int k = 0; k < c - 1; k++) {
                    int v = getbit(&b);
                    if (v < 0)
                        return cut ? (long)o : -1;
                    extra = extra << 1 | (uint32_t)v;
                }
                d = ((size_t)1 << (c - 1)) + extra + 1;
            }
            if (d > o)
                return -1;
            for (int k = 0; k < len; k++) {
                if (o >= max)
                    return (long)o;
                out[o] = out[o - d];
                o++;
            }
        }
    }
    return (long)o;
}

/* ------------------------------------------------------------ topics */
/* Binary search of help_topics[] (sorted by id with strcmp); NULL if absent. */
static const help_topic_t *find_topic(const char *id)
{
    int lo = 0, hi = help_ntopics - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = strcmp(id, help_strings + help_topics[mid].id);
        if (!c)
            return &help_topics[mid];
        if (c < 0)
            hi = mid - 1;
        else
            lo = mid + 1;
    }
    return NULL;
}

/* A topic exists if the firmware's tables know it or the installed manual
 * module has it (a newer manual may add pages). */
bool help_exists(const char *topic)
{
    mm_topic_t t;
    return topic && (find_topic(topic) != NULL || mm_find(topic, &t));
}

/* Flash copy of topic index k (an @essentials page, or every page with
 * help_all_resident): its compressed bytes in help_blob, or NULL. */
static const uint8_t *resident(int k)
{
    if (help_all_resident)
        return help_blob + help_topics[k].off;
    int lo = 0, hi = help_ness - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (help_ess[mid].topic == k)
            return help_blob + help_ess[mid].off;
        if (help_ess[mid].topic < k)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}

/* Topic for a menu item's text (ui.h): the text up to "(" if that is a topic
 * id ("factor(" -> "factor"); otherwise a CATALOG entry whose insert text is
 * exactly this text gives its topic (display forms such as "√(" -> sqrt).
 * Returns a static buffer or a help_strings pointer; NULL if none. */
const char *help_topic_for_insert(const char *insert)
{
    static char id[40];
    if (!insert)
        return NULL;
    size_t n = 0;
    while (insert[n] && insert[n] != '(' && n < sizeof id - 1) {
        id[n] = insert[n];
        n++;
    }
    id[n] = 0;
    if (!n)
        return NULL;
    if (help_exists(id))
        return id;
    /* catalog labels and display forms: "√(" -> sqrt, "π" -> pi, ... */
    for (int i = 0; i < help_ncatalog; i++) {
        const help_cat_t *c = &help_catalog[i];
        if (c->topic >= 0 && !strcmp(help_strings + c->insert, insert))
            return help_strings + help_topics[c->topic].id;
    }
    return NULL;
}

/* ------------------------------------------------------------ layout */
/* inline style codes inside the laid-out text (control bytes that never occur
 * in help text; the build only allows printable characters) */
#define S_LINK 0x01 /* followed by (link index + 1) */
#define S_LINK_END 0x02
#define S_CODE 0x03
#define S_CODE_END 0x04
#define S_KEY 0x05
#define S_KEY_END 0x06

/* Display line kinds: "# " title, "## " subheading, paragraph, first line of
 * a "- " bullet, its continuation lines (indented, no dot), a 4-space code
 * line, and a small vertical gap. */
enum { L_H1, L_H2, L_PARA, L_BULLET, L_BCONT, L_CODE, L_BLANK };

/* One display line: a slice of text[] (may contain style codes). */
typedef struct {
    uint32_t off;   /* in text[] */
    uint16_t len;
    uint8_t kind;
    uint8_t style;  /* style active at the start of the line (S_LINK / S_CODE / S_KEY or 0) */
    uint8_t link;   /* link index + 1 when style == S_LINK */
} hline_t;

typedef struct {
    uint16_t target; /* offset of the NUL-terminated target id in tgt_pool[] */
    int16_t first;   /* first display line (-1 until layout() has placed it) */
} hlink_t;

/* Per-page limits: links beyond MAX_LINKS (or TGT_POOL bytes of ids) are
 * shown as plain text. Text uses font_medium (7 px cells, BODY_COLS = 44
 * columns), titles the 10 px large font (H1_COLS = 30). */
#define MAX_LINKS 160
#define TGT_POOL 3072
#define MARGIN 4
#define BODY_COLS ((LCD_WIDTH - MARGIN - 8) / 7) /* font_medium cells */
#define H1_COLS ((LCD_WIDTH - MARGIN - 8) / 10)

/* Workspace slices (help_init) and the current page's layout. */
static char *src, *text;   /* markup source / styled text of the page */
static hline_t *lines;     /* display lines */
static size_t src_cap, text_cap;
static int lines_cap;
static int nlines, top, sel_link = -1; /* top: first visible line; sel_link: -1 = none */
static size_t tlen;        /* bytes used in text[] */
static hlink_t links[MAX_LINKS];
static char tgt_pool[TGT_POOL];
static int nlinks, tgt_used;
static char cur_id[40];      /* topic shown ("" for generated pages) */
static const char *gen_title, *gen_markup; /* the generated page (help_show_text), shown again by back() */
static char cur_title[64];   /* its first "# " heading (console state) */
static bool truncated;       /* the topic did not fit in src[] */

/* Back history of topic ids (oldest dropped beyond HIST; "" stands for the
 * generated page) and the screen help was opened from. */
#define HIST 12
static char hist[HIST][40];
static int nhist;
static const screen_t *return_to;

/* Split the workspace (ui.h, from app_init): 3/16 for the decompressed
 * markup (24 KB of 128 KB, the maximum topic size enforced by the build;
 * only 3 KB with the 16 KB fallback workspace),
 * 6/16 for the styled text (style codes and link titles make it larger than
 * the source), the rest (8-byte aligned) for display lines. */
void help_init(void *work, size_t size)
{
    uint8_t *w = work;
    src_cap = size * 3 / 16;
    text_cap = size * 6 / 16;
    src = (char *)w;
    text = (char *)w + src_cap;
    size_t used = src_cap + text_cap;
    used = (used + 7) & ~(size_t)7;
    lines = (hline_t *)(w + used);
    lines_cap = (int)((size - used) / sizeof(hline_t));
    nlines = 0;
    nhist = 0;
    cur_id[0] = 0;
}

/* Pixel height of a display line of each kind. */
static int line_height(int kind)
{
    switch (kind) {
    case L_H1: return 24;
    case L_H2: return 18;
    case L_BLANK: return 6;
    default: return 15;
    }
}

/* Append to text[]; silently drops bytes when it is full. */
static void tput(char c)
{
    if (tlen + 1 < text_cap)
        text[tlen++] = c;
}

static void tputs(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
        tput(s[i]);
}

/* Title of a link target: the firmware's tables, else the module's (a page
 * only a newer manual has), else the id itself. Static buffer for the latter. */
static const char *topic_title(const char *id)
{
    const help_topic_t *t = find_topic(id);
    if (t)
        return help_strings + t->title;
    static char title[64];
    mm_topic_t m;
    if (mm_find(id, &m) && mm_string(m.title, title, sizeof title))
        return title;
    return id;
}

/* Convert inline markup of s[0..n) into styled text; returns its start.
 *   [[id]] / [[id|text]]  link: S_LINK, (link index + 1), the topic's title
 *                         or the given text, S_LINK_END. Ids are letters,
 *                         digits and _ . -, shorter than 40; anything else
 *                         (a matrix "[[1,2]]") is copied as text.
 *   `code`                S_CODE text S_CODE_END
 *   {key}                 key cap, 1..12 characters: S_KEY text S_KEY_END
 * Unmatched delimiters are copied as they are. */
static size_t inline_markup(const char *s, size_t n)
{
    size_t start = tlen;
    size_t i = 0;
    while (i < n) {
        if (s[i] == '[' && i + 1 < n && s[i + 1] == '[') {
            const char *e = memchr(s + i + 2, ']', n - i - 2);
            if (e && e + 1 < s + n && e[1] == ']') {
                const char *id = s + i + 2;
                const char *bar = memchr(id, '|', (size_t)(e - id));
                size_t idl = (size_t)((bar ? bar : e) - id);
                bool valid = idl > 0 && idl < 40;
                for (size_t k = 0; k < idl && valid; k++)
                    valid = isalnum((unsigned char)id[k]) || id[k] == '_' || id[k] == '.' || id[k] == '-';
                if (!valid) { /* not a link (e.g. a matrix [[1,2]]) */
                    tput(s[i++]);
                    continue;
                }
                if (nlinks < MAX_LINKS && tgt_used + idl + 1 <= TGT_POOL) {
                    memcpy(tgt_pool + tgt_used, id, idl);
                    tgt_pool[tgt_used + idl] = 0;
                    links[nlinks].target = (uint16_t)tgt_used;
                    links[nlinks].first = -1;
                    tgt_used += (int)idl + 1;
                    tput(S_LINK);
                    tput((char)(nlinks + 1));
                    nlinks++;
                    if (bar)
                        tputs(bar + 1, (size_t)(e - bar - 1));
                    else
                        tputs(topic_title(tgt_pool + links[nlinks - 1].target),
                              strlen(topic_title(tgt_pool + links[nlinks - 1].target)));
                    tput(S_LINK_END);
                } else {
                    tputs(bar ? bar + 1 : id, (size_t)(e - (bar ? bar + 1 : id)));
                }
                i = (size_t)(e + 2 - s);
                continue;
            }
        }
        if (s[i] == '`') {
            const char *e = memchr(s + i + 1, '`', n - i - 1);
            if (e) {
                tput(S_CODE);
                tputs(s + i + 1, (size_t)(e - s - i - 1));
                tput(S_CODE_END);
                i = (size_t)(e + 1 - s);
                continue;
            }
        }
        if (s[i] == '{') {
            const char *e = memchr(s + i + 1, '}', n - i - 1);
            if (e && e - s - i - 1 > 0 && e - s - i - 1 <= 12) {
                tput(S_KEY);
                tputs(s + i + 1, (size_t)(e - s - i - 1));
                tput(S_KEY_END);
                i = (size_t)(e + 1 - s);
                continue;
            }
        }
        tput(s[i++]);
    }
    return start;
}

/* Append a display line (dropped when lines[] is full). */
static void add_line(uint32_t off, size_t len, int kind, int style, int link)
{
    if (nlines >= lines_cap)
        return;
    lines[nlines++] = (hline_t){off, (uint16_t)len, (uint8_t)kind, (uint8_t)style, (uint8_t)link};
}

static bool is_cont(unsigned char c) { return (c & 0xC0) == 0x80; } /* UTF-8 continuation byte */

/* Word-wrap styled text [start, tlen) into display lines of `cols` columns.
 * Greedy: walk the text counting columns (one per codepoint; style codes and
 * UTF-8 continuation bytes take none), remember the last space and the style
 * active there, and when the line is full break at that space (the space is
 * dropped) or, if there is none or `hard` is set (code lines), in the middle
 * of the word. Each line records the style active at its start so drawing
 * can begin inside a link or code span. The first line gets `kind`, the
 * following ones `cont_kind` (bullet continuations). Empty text still gives
 * one (empty) line. */
static void wrap(size_t start, int kind, int cont_kind, int cols, bool hard)
{
    size_t end = tlen;
    int style = 0, link = 0;
    size_t ls = start;
    while (ls < end || ls == start) {
        int col = 0;
        size_t p = ls, brk = 0;
        int bstyle = style, blink = link;
        int st = style, lk = link;
        while (p < end) {
            unsigned char c = (unsigned char)text[p];
            if (c == S_LINK) {
                st = S_LINK, lk = (unsigned char)text[p + 1];
                p += 2;
                continue;
            }
            if (c == S_LINK_END || c == S_CODE_END || c == S_KEY_END) {
                st = 0, lk = 0;
                p++;
                continue;
            }
            if (c == S_CODE || c == S_KEY) {
                st = c;
                p++;
                continue;
            }
            if (is_cont(c)) {
                p++;
                continue;
            }
            if (c == ' ' && !hard) {
                brk = p;
                bstyle = st, blink = lk;
            }
            if (col == cols) {
                break;
            }
            col++;
            p++;
        }
        if (p >= end) {
            add_line((uint32_t)ls, end - ls, kind, style, link);
            break;
        }
        if (brk > ls && !hard) {
            add_line((uint32_t)ls, brk - ls, kind, style, link);
            ls = brk + 1;
            style = bstyle, link = blink;
        } else {
            add_line((uint32_t)ls, p - ls, kind, style, link);
            ls = p;
            style = st, link = lk;
        }
        kind = cont_kind;
    }
}

/* Lay out the markup src[0..n) (syntax: app/EXTENDING.md, "Help pages") into
 * lines[]. Lines of a paragraph are joined with spaces in para[] and wrapped
 * when the paragraph ends (FLUSH); a paragraph longer than para[] (1 KB) is
 * flushed at a space when the buffer is full and continues on a new line.
 * Blank lines become small gaps, never two in a row or right after the
 * title; "## " gets a gap before it. The first "# " heading is the page
 * title. Finally each link gets the first display line it appears on, which
 * LEFT/RIGHT navigation uses. */
static void layout(size_t n)
{
    nlines = 0;
    tlen = 0;
    nlinks = 0;
    tgt_used = 0;
    cur_title[0] = 0;
    char para[1024];
    size_t plen = 0;
    int last = L_BLANK;
    size_t i = 0;
#define FLUSH()                                                                  \
    do {                                                                         \
        if (plen) {                                                              \
            size_t st = inline_markup(para, plen);                               \
            wrap(st, L_PARA, L_PARA, BODY_COLS, false);                          \
            plen = 0;                                                            \
            last = L_PARA;                                                       \
        }                                                                        \
    } while (0)
    while (i < n) {
        const char *ln = src + i;
        const char *nl = memchr(ln, '\n', n - i);
        size_t len = nl ? (size_t)(nl - ln) : n - i;
        i += len + (nl ? 1 : 0);
        if (len >= 4 && !memcmp(ln, "    ", 4)) {
            FLUSH();
            size_t st = tlen;
            tputs(ln + 4, len - 4);
            wrap(st, L_CODE, L_CODE, BODY_COLS - 1, true);
            last = L_CODE;
        } else if (len >= 2 && !memcmp(ln, "# ", 2)) {
            FLUSH();
            size_t st = inline_markup(ln + 2, len - 2);
            if (!cur_title[0]) {
                size_t tl = len - 2 < sizeof cur_title - 1 ? len - 2 : sizeof cur_title - 1;
                memcpy(cur_title, ln + 2, tl);
                cur_title[tl] = 0;
            }
            wrap(st, L_H1, L_H1, H1_COLS, false);
            last = L_H1;
        } else if (len >= 3 && !memcmp(ln, "## ", 3)) {
            FLUSH();
            if (last != L_BLANK)
                add_line((uint32_t)tlen, 0, L_BLANK, 0, 0);
            size_t st = inline_markup(ln + 3, len - 3);
            wrap(st, L_H2, L_H2, BODY_COLS, false);
            last = L_H2;
        } else if (len >= 2 && !memcmp(ln, "- ", 2)) {
            FLUSH();
            size_t st = inline_markup(ln + 2, len - 2);
            wrap(st, L_BULLET, L_BCONT, BODY_COLS - 2, false);
            last = L_BULLET;
        } else if (len == 0) {
            FLUSH();
            if (last != L_BLANK && last != L_H1)
                add_line((uint32_t)tlen, 0, L_BLANK, 0, 0);
            last = L_BLANK;
        } else {
            if (plen && plen < sizeof para - 1)
                para[plen++] = ' ';
            while (len > sizeof para - 1 - plen) { /* para[] full: wrap what fits, up to its last space */
                size_t c = sizeof para - 1 - plen;
                while (c > 0 && ln[c] != ' ')
                    c--;
                if (c == 0 && plen == 0) /* one word longer than para[]: cut it */
                    c = sizeof para - 1;
                memcpy(para + plen, ln, c);
                plen += c;
                FLUSH();
                while (c < len && ln[c] == ' ')
                    c++;
                ln += c, len -= c;
            }
            memcpy(para + plen, ln, len);
            plen += len;
        }
    }
    FLUSH();
#undef FLUSH
    /* first display line of every link (for LEFT/RIGHT navigation) */
    for (int l = 0; l < nlines; l++) {
        const hline_t *h = &lines[l];
        if (h->style == S_LINK && links[h->link - 1].first < 0)
            links[h->link - 1].first = (int16_t)l;
        for (uint32_t p = h->off; p < h->off + h->len; p++)
            if (text[p] == S_LINK) {
                int k = (unsigned char)text[p + 1] - 1;
                if (links[k].first < 0)
                    links[k].first = (int16_t)l;
                p++;
            }
    }
}

/* ------------------------------------------------------------ drawing */
static int view_h(void) { return BODY_H; }

/* Index of the last line that fits completely when drawing from `top`. */
static int last_visible(void)
{
    int y = 0, l = top;
    while (l < nlines && y + line_height(lines[l].kind) <= view_h()) {
        y += line_height(lines[l].kind);
        l++;
    }
    return l - 1;
}

/* Largest useful `top`: the first line of the last screenful (0 if the page
 * fits). */
static int max_top(void)
{
    int y = 0, l = nlines;
    while (l > 0 && y + line_height(lines[l - 1].kind) <= view_h()) {
        y += line_height(lines[l - 1].kind);
        l--;
    }
    return l;
}

/* Draw one display line at y: kind-specific font, colour and decoration
 * (code background, bullet dot, rule under the title), then the text in runs
 * of constant style: code green, key caps white on dark grey, links blue and
 * underlined (white on the selection colour when selected). */
static void draw_line(const hline_t *h, int y)
{
    const font_t *f = h->kind == L_H1 ? &font_large : &font_medium;
    int x = MARGIN;
    uint16_t base = C_BLACK;
    if (h->kind == L_H1)
        base = C_NAVY;
    else if (h->kind == L_H2)
        base = C_BLUE;
    if (h->kind == L_CODE) {
        gfx_fill_rect(MARGIN, y, LCD_WIDTH - MARGIN - 8, 15, RGB565(236, 240, 236));
        x += 4;
    }
    if (h->kind == L_BULLET)
        gfx_text(f, x + 2, y, "·", C_DKGRAY, GFX_TRANSPARENT);
    if (h->kind == L_BULLET || h->kind == L_BCONT)
        x += 2 * f->w;
    int style = h->style, link = h->link;
    uint32_t p = h->off, end = h->off + h->len;
    char run[160];
    while (p < end) {
        /* one run of constant style */
        size_t r = 0;
        while (p < end && r < sizeof run - 1) {
            unsigned char c = (unsigned char)text[p];
            if (c == S_LINK || c == S_LINK_END || c == S_CODE || c == S_CODE_END || c == S_KEY || c == S_KEY_END)
                break;
            run[r++] = (char)c;
            p++;
        }
        run[r] = 0;
        if (r) {
            uint16_t fg = base;
            int bg = GFX_TRANSPARENT;
            if (style == S_CODE || h->kind == L_CODE)
                fg = RGB565(0, 110, 40);
            if (style == S_KEY) {
                bg = C_DKGRAY;
                fg = C_WHITE;
            }
            if (style == S_LINK) {
                bool selected = link - 1 == sel_link;
                fg = selected ? C_WHITE : C_BLUE;
                if (selected)
                    bg = C_SEL_BG;
            }
            int w = gfx_text_width(f, run);
            if (bg != GFX_TRANSPARENT)
                gfx_fill_rect(x, y, w, f->h, (uint16_t)bg);
            gfx_text(f, x, y, run, fg, GFX_TRANSPARENT);
            if (style == S_LINK)
                gfx_hline(x, y + f->h - 1, w, fg);
            x += w;
        }
        if (p >= end)
            break;
        unsigned char c = (unsigned char)text[p++];
        if (c == S_LINK)
            style = S_LINK, link = (unsigned char)text[p++];
        else if (c == S_CODE || c == S_KEY)
            style = c;
        else
            style = 0, link = 0;
    }
    if (h->kind == L_H1)
        gfx_hline(MARGIN, y + 21, LCD_WIDTH - 2 * MARGIN, C_LTGRAY);
}

/* Draw the visible lines and the scroll bar. The bar's size and position are
 * estimated with 15 px per hidden line (the common line height). */
static void help_draw(void)
{
    ui_cursor_hide();
    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, C_WHITE);
    int y = BODY_Y;
    for (int l = top; l < nlines; l++) {
        int hgt = line_height(lines[l].kind);
        if (y + hgt > UI_BOTTOM)
            break;
        if (lines[l].kind != L_BLANK)
            draw_line(&lines[l], y);
        y += hgt;
    }
    /* scroll bar */
    int mt = max_top();
    if (mt > 0) {
        int bh = BODY_H * BODY_H / (BODY_H + mt * 15);
        if (bh < 12)
            bh = 12;
        int by = BODY_Y + (BODY_H - bh) * top / mt;
        gfx_fill_rect(LCD_WIDTH - 4, BODY_Y, 3, BODY_H, C_LTGRAY);
        gfx_fill_rect(LCD_WIDTH - 4, by, 3, bh, C_GRAY);
    }
    if (truncated)
        gfx_text(&font_small, LCD_WIDTH - 70, UI_BOTTOM - 13, "(truncated)", C_RED, GFX_TRANSPARENT);
}

/* ------------------------------------------------------------ navigation */
static bool missing; /* the page shown is the "Manual not installed" stand-in */

/* Why the full manual is not available, as one sentence for show_missing. */
static const char *missing_reason(void)
{
    if (help_all_resident)
        return "";
#if defined(CONFIG_CONTENT_INTERNAL)
    return "This firmware (the core / rollback build) has only the essential pages built in.";
#else
    if (safemode_active())
        return "The calculator is in safe mode, which does not use installed content: switch off and on to "
               "leave it ([[safe-mode]]).";
    switch (mod_state(MANUAL_MODULE)) {
    case MOD_OK: return "The installed manual does not contain this page: install the manual of this firmware's "
                        "release.";
    case MOD_NODEV: return "The QSPI flash chip that holds the manual is not responding (Memory > About).";
    case MOD_ABI: return "The installed manual module was made for an incompatible firmware: install the one from "
                         "this firmware's release.";
    case MOD_CORRUPT: return "The installed manual is damaged: install it again.";
    case MOD_DEPS: return "The installed manual needs another module that is missing.";
    default: return "Only the essential pages are built into the firmware; the full manual is a separate module.";
    }
#endif
}

/* The stand-in page for topic `id` when neither the manual module nor the
 * built-in essentials have it: the request, the reason, how to install, and
 * links to the built-in pages. Composed in src[] like a generated page, but
 * cur_id stays the requested topic (tests, back history). */
static void show_missing(const char *id, const char *title)
{
#if defined(CONFIG_CONTENT_INTERNAL)
    const char *head = "This page is not in this build."; /* the core build cannot install content */
#else
    const char *head = "Manual not installed: see Memory > Install content.";
#endif
    size_t n = (size_t)snprintf(src, src_cap, "# %s\n\n%s\n\n%s\n\n## Built-in pages\n", title, head,
                                missing_reason());
    for (int i = 0; i < help_ness && n < src_cap; i++) {
        const help_topic_t *t = &help_topics[help_ess[i].topic];
        n += (size_t)snprintf(src + n, src_cap - n, "- [[%s]]\n", help_strings + t->id);
    }
    if (n >= src_cap)
        n = src_cap - 1;
    truncated = false;
    missing = true;
    snprintf(cur_id, sizeof cur_id, "%s", id);
    layout(n);
    top = 0;
    sel_link = -1;
}

/* Decompress and lay out topic `id` and show it from the top: from the
 * manual module if it has the page, else the flash copy, else the stand-in
 * page. False only if the topic is unknown to both or its data is corrupt
 * (the old page stays). */
static bool load(const char *id)
{
    const help_topic_t *t = find_topic(id);
    mm_topic_t m;
    long n = -1;
    uint32_t ulen = 0;
    if (mm_find(id, &m)) {
        /* compressed bytes into text[] (overwritten by layout() afterwards) */
        uint32_t len = m.clen < text_cap ? m.clen : (uint32_t)text_cap;
        if (m.off <= mm.blob_len && len <= mm.blob_len - m.off &&
            mod_read(MANUAL_MODULE, mm.blob_off + m.off, text, len) == 0)
            n = inflate_topic((const uint8_t *)text, len, len < m.clen, mm.lit, mm.dist, src, src_cap);
        ulen = m.ulen;
    }
    if (n < 0 && t) {
        const uint8_t *data = resident((int)(t - help_topics));
        if (!data) {
            show_missing(id, help_strings + t->title);
            return true;
        }
        n = inflate_topic(data, t->clen, false, help_lit_lens, help_dist_lens, src, src_cap);
        ulen = t->ulen;
    }
    if (n < 0)
        return false;
    missing = false;
    truncated = (size_t)n < ulen;
    snprintf(cur_id, sizeof cur_id, "%s", id);
    layout((size_t)n);
    top = 0;
    sel_link = -1;
    return true;
}

/* Redraw if help is already on screen, else switch to it. */
static void show_current(void)
{
    if (ui_current() == &scr_help)
        help_draw();
    else
        ui_switch(&scr_help);
}

/* Show topic `id` (unknown or NULL: the index). Within help, push the current
 * topic on the back history; opened from another screen, start a new
 * history and remember that screen for leaving. No-op before help_init(). */
static void open_topic(const char *id, bool push)
{
    if (!src)
        return;
    if (!id || !help_exists(id))
        id = "index";
    if (push && (cur_id[0] || gen_markup) && ui_current() == &scr_help) {
        if (nhist == HIST) {
            memmove(hist[0], hist[1], sizeof hist[0] * (HIST - 1));
            nhist--;
        }
        snprintf(hist[nhist++], sizeof hist[0], "%s", cur_id);
    }
    if (ui_current() != &scr_help) {
        nhist = 0;
        const screen_t *c = ui_current();
        return_to = c;
    }
    if (!load(id))
        return;
    show_current();
}

void help_open(const char *topic) { open_topic(topic, true); }

/* Show generated markup (ui.h) as a page titled `title`. The strings are not
 * copied: the caller keeps them alive while help is open (static buffers), so
 * that back() can show the page again after a link followed from it (the
 * history records it as the id ""). */
void help_show_text(const char *title, const char *markup)
{
    if (!src)
        return;
    if (ui_current() != &scr_help) {
        nhist = 0;
        return_to = ui_current();
    }
    gen_title = title, gen_markup = markup;
    missing = false;
    size_t n = (size_t)snprintf(src, src_cap, "# %s\n\n%s", title, markup);
    if (n >= src_cap)
        n = src_cap - 1;
    snprintf(cur_id, sizeof cur_id, "%s", "");
    truncated = false;
    layout(n);
    top = 0;
    sel_link = -1;
    show_current();
}

/* SHIFT HELP (ui.h). Topic resolution, first existing wins:
 *   in help        the index;
 *   in a menu      the item's help, the topic named by its insert text, by its
 *                  label, then the menu's own topic;
 *   in settings    the highlighted row's topic, else "mode";
 *   a message box  as for the screen it was shown over;
 *   other screens  screen.help, else the topic named like the screen;
 * and finally the index. */
void help_context(void)
{
    const screen_t *c = ui_current();
    const char *topic = NULL;
    if (c && !strcmp(c->name, "message") && ui_previous()) /* a message box: help for the screen below */
        c = ui_previous();
    if (c == &scr_help) {
        topic = "index";
    } else if (c == &scr_menu) {
        menu_item_t it;
        if (menu_current_item(&it)) {
            topic = it.help;
            if (!help_exists(topic))
                topic = help_topic_for_insert(it.insert);
            if (!help_exists(topic))
                topic = help_topic_for_insert(it.label);
        }
        const menu_def_t *m = menu_current();
        if (!help_exists(topic) && m)
            topic = m->help;
    } else if (c == &scr_mode) {
        const mode_row_t *r = mode_current_row();
        topic = r && r->help ? r->help : "mode";
    } else if (c) {
        topic = c->help && help_exists(c->help) ? c->help : c->name;
    }
    help_open(help_exists(topic) ? topic : "index");
}

/* ⌫ / back: previous topic of the history, or leave help (to the screen it
 * was opened from, or home) when the history is empty. */
static void back(void)
{
    if (nhist > 0) {
        char id[40];
        snprintf(id, sizeof id, "%s", hist[--nhist]);
        if (!id[0]) /* the generated page */
            help_show_text(gen_title, gen_markup);
        else {
            load(id);
            help_draw();
        }
        return;
    }
    ui_returning = true;
    ui_switch(return_to && return_to != &scr_help ? return_to : ui_home());
    ui_returning = false;
}

/* Scroll the least amount so that `line` is visible. */
static void ensure_visible(int line)
{
    if (line < top)
        top = line;
    while (line > last_visible() && top < max_top())
        top++;
}

/* LEFT (dir -1) / RIGHT (+1): select the previous/next link. If no link is
 * selected or the selected one scrolled off screen, start with the first
 * (last) link visible on screen, else the next link below (above) the
 * screen; scroll to it. */
static void select_link(int dir)
{
    if (!nlinks)
        return;
    int lv = last_visible();
    if (sel_link < 0 || links[sel_link].first < top || links[sel_link].first > lv) {
        /* first/last link on screen */
        int pick = -1;
        for (int k = 0; k < nlinks; k++) {
            int f = links[dir > 0 ? k : nlinks - 1 - k].first;
            if (f >= top && f <= lv) {
                pick = dir > 0 ? k : nlinks - 1 - k;
                break;
            }
        }
        if (pick < 0) {
            for (int k = 0; k < nlinks; k++) {
                int idx = dir > 0 ? k : nlinks - 1 - k;
                if ((dir > 0 && links[idx].first > lv) || (dir < 0 && links[idx].first < top)) {
                    pick = idx;
                    break;
                }
            }
        }
        if (pick < 0)
            return;
        sel_link = pick;
    } else {
        int n = sel_link + dir;
        if (n < 0 || n >= nlinks)
            return;
        sel_link = n;
    }
    ensure_visible(links[sel_link].first);
}

/* Whether a page key can scroll help up / down (ui_softkey_label hides it if not). */
void help_page_room(bool *up, bool *down)
{
    *up = top > 0;
    *down = top < max_top();
}

/* Keys (see the header). Scrolling deselects a link that leaves the screen. */
static void help_key(int key, int mod)
{
    int mt = max_top();
    switch (key) {
    case KEY_UP:
        if (top > 0)
            top--;
        break;
    case KEY_DOWN:
        if (top < mt)
            top++;
        break;
    case KEY_F3: /* soft keys: ▲page ▼page index exit back (paging and back where menus have them) */
        open_topic("index", true);
        return;
    case KEY_F5:
        back();
        return;
    case KEY_F2:
    case KEY_ADD:
        top = last_visible() + 1 < mt ? last_visible() + 1 : mt;
        break;
    case KEY_F1:
    case KEY_SUB:
        for (int y = 0; top > 0 && y + line_height(lines[top - 1].kind) <= BODY_H;)
            y += line_height(lines[--top].kind);
        break;
    case KEY_RIGHT:
        select_link(1);
        break;
    case KEY_LEFT:
        select_link(-1);
        break;
    case KEY_ENTER:
        if (sel_link >= 0) {
            open_topic(tgt_pool + links[sel_link].target, true);
            return;
        }
        break;
    case KEY_DEL:
        back();
        return;
    case KEY_F4:
    case KEY_CLEAR:
        nhist = 0;
        back();
        return;
    default:
        return;
    }
    if (sel_link >= 0 && (links[sel_link].first < top || links[sel_link].first > last_visible()))
        sel_link = -1;
    help_draw();
}

/* Touch: drag scrolls, a tap on a link follows it. */

/* Link index under the point (x, y), or -1. Re-walks the line's text with the
 * fixed cell width of its font (as draw_line places it), with 2 px slack on
 * each side of a character. */
static int link_at(int x, int y)
{
    int yy = BODY_Y;
    for (int l = top; l < nlines; l++) {
        int hgt = line_height(lines[l].kind);
        if (y >= yy && y < yy + hgt) {
            const hline_t *h = &lines[l];
            const font_t *f = h->kind == L_H1 ? &font_large : &font_medium;
            int cx = MARGIN + (h->kind == L_CODE ? 4 : 0) + (h->kind == L_BULLET || h->kind == L_BCONT ? 2 * f->w : 0);
            int style = h->style, link = h->link;
            for (uint32_t p = h->off; p < h->off + h->len; p++) {
                unsigned char c = (unsigned char)text[p];
                if (c == S_LINK) {
                    style = S_LINK, link = (unsigned char)text[++p];
                    continue;
                }
                if (c == S_CODE || c == S_KEY) {
                    style = c;
                    continue;
                }
                if (c == S_LINK_END || c == S_CODE_END || c == S_KEY_END) {
                    style = 0;
                    continue;
                }
                if (is_cont(c))
                    continue;
                if (x >= cx - 2 && x < cx + f->w + 2 && style == S_LINK)
                    return link - 1;
                cx += f->w;
            }
            return -1;
        }
        yy += hgt;
        if (yy > UI_BOTTOM)
            break;
    }
    return -1;
}

static void help_touch(const touch_event_t *ev)
{
    switch (ev->type) {
    case TEV_DRAG: /* 15 px of movement per line, content follows the finger */
        if (ui_drag_scroll(ev, UI_DRAG_Y, 15, &top, max_top()))
            help_draw();
        break;
    case TEV_TAP: {
        int k = link_at(ev->x, ev->y);
        if (k >= 0) {
            sel_link = k;
            open_topic(tgt_pool + links[k].target, true);
        }
        break;
    }
    case TEV_SWIPE: /* sideways: right goes back (like F5), left turns to the next page (like F2) */
        if (touch_swipe_dir(ev) == SWIPE_RIGHT)
            help_key(KEY_F5, MOD_NONE);
        else if (touch_swipe_dir(ev) == SWIPE_LEFT)
            help_key(KEY_F2, MOD_NONE);
        break;
    default:
        break;
    }
}

/* Soft keys without actions: help_key() gets KEY_F1..KEY_F5. */
static const softkeys_t help_soft = {{{"▲ page"}, {"▼ page"}, {"index"}, {"exit"}, {"back"}}};
const screen_t scr_help = {"help", "HELP", NULL, help_draw, help_key, NULL, NULL, help_touch, "help", &help_soft};

/* Console `state` keys while help is shown (UI tests). */
void help_state(void (*kv)(const char *, const char *, void *), void *user)
{
    if (ui_current() != &scr_help)
        return;
    char b[48];
    kv("help_topic", cur_id, user);
    kv("help_title", cur_title, user);
    snprintf(b, sizeof b, "%d", top);
    kv("help_top", b, user);
    snprintf(b, sizeof b, "%d", nlines);
    kv("help_lines", b, user);
    snprintf(b, sizeof b, "%d", nlinks);
    kv("help_links", b, user);
    kv("help_link", sel_link >= 0 ? tgt_pool + links[sel_link].target : "", user);
    snprintf(b, sizeof b, "%d", nhist);
    kv("help_history", b, user);
    kv("help_missing", missing ? "1" : "0", user);
    kv("help_source", missing ? "none" : !cur_id[0] ? "generated" : help_page_source(cur_id), user);
}

/* Where topic `id` would be read from: "module", "builtin" or "none". */
const char *help_page_source(const char *id)
{
    mm_topic_t m;
    if (mm_find(id, &m))
        return "module";
    const help_topic_t *t = find_topic(id);
    return t && resident((int)(t - help_topics)) ? "builtin" : "none";
}

/* One line for MEM > About: where the manual comes from. */
void help_manual_status(char *out, size_t n)
{
    if (help_all_resident) {
        snprintf(out, n, "Manual: full, built into the firmware (%u pages)", (unsigned)help_ntopics);
        return;
    }
    const mod_hdr_t *h = mm_open() ? mod_header(MANUAL_MODULE) : NULL;
    if (h)
        snprintf(out, n, "Manual: module `%s` %u.%u.%u from the QSPI (%u pages)", MANUAL_MODULE, h->ver[0], h->ver[1],
                 h->ver[2], (unsigned)mm.ntopics);
    else {
#if defined(CONFIG_CONTENT_INTERNAL)
        const char *why = "essential pages only (core build)";
#else
        mod_state_t st = mod_state(MANUAL_MODULE);
        const char *why = safemode_active() ? "not used in safe mode"
                          : st == MOD_ABSENT ? "not installed (Memory > Install content)"
                                             : mod_state_text(st);
#endif
        snprintf(out, n, "Manual: %s; %u built-in pages", why, (unsigned)help_ness);
    }
}

/* ------------------------------------------------------------ app entries */
static void help_app_open(void) { help_open("index"); }
const app_def_t app_help = {"help", "Help", help_app_open, {{0}}, "help"};
