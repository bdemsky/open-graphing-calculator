/*
 * The home screen's view in 2D mode (app/home.c keeps the history and the
 * editor and calls in here): history entries in textbook form (input left,
 * result right), the Textbook entry line with a box caret, the live preview
 * under it, repeating decimals, the large font. Entries that cannot be laid
 * out (or Classic mode) are shown as wrapped linear text exactly like the
 * Classic home screen. A selected entry wider than the screen scrolls with
 * LEFT/RIGHT.
 *
 * Box pools live on the stack while drawing (no static layout state except
 * what touch and the caret blink need).
 *
 * Layout model: the history, the entry line and the preview are stacked in a
 * "virtual column" of blocks (two per history entry: input left-aligned,
 * result right-aligned; then the entry; then the preview), each as tall as
 * its 2D layout (at least one text line). The view shows the bottom of the
 * column (the entry line is always visible) unless the selected history
 * item is above it, in which case it scrolls up to that item.
 *
 * Interaction (keys reach here from app/home.c): with a history item
 * selected, LEFT/RIGHT scroll it horizontally when it is wider than the
 * screen; on the entry line UP/DOWN move between boxes (numerator <->
 * denominator, limits, matrix rows) and the other Textbook keys go to
 * m2d_edit_key(). A tap selects a history item or puts the caret at the
 * nearest position of the entry line.
 *
 * The caret of the Textbook entry is our own: a 2 px bar (3 px and coloured
 * while SHIFT/ALPHA is pending) as tall as the box it is in, drawn straight
 * into the framebuffer and blinked by m2d_home_tick() by saving and
 * restoring the pixels under it, so blinking never redraws the screen.
 *
 * Also here: the MODE rows DISPLAY, PREVIEW and REPEAT and their persist
 * blob "textbook" (registered in app/registry.c), the console state keys, and
 * the textbook renderer of the RPN stack levels (m2d_rpn_renderer).
 * Text drawn as boxes bypasses the graphics text log; the linear text is
 * noted instead (gfx_textlog_note) so UI tests can `expect text`.
 */
#include <stdio.h>
#include <string.h>
#include "hal.h"
#include "m2d.h"
#include "m2d_box.h"
#include "nbdoc.h"
#include "py_core.h"
#include "registry.h"
#include "rpn.h"
#include "sections.h"
#include "session.h"
#include "theme.h"

#define POOL 400                  /* boxes per layout (~16 KB of stack) */
#define MAXROWS 64                /* slot rows tracked for the entry caret */
#define MAX_BLOCKS (2 * NB_MAX_CELLS + 2) /* the notebook's cells x 2 + entry + preview */
#define TEXT_MAX (ED_MAX * 4 + 16) /* linear text of a full line (UTF-8) */

enum { B_INPUT, B_OUTPUT, B_ENTRY, B_PREVIEW };

typedef struct {
    int item;  /* history item (2*i + 0/1), -1 entry, -3 preview */
    int y, h;  /* in the virtual column (pixels from the top of the oldest entry) */
} block_t;

DTCM_BSS static block_t blocks[MAX_BLOCKS]; /* (AXI SRAM is full) */
static int nblocks, view_top; /* last draw, for touch */
static int view_max, view_h;  /* last draw: view_top showing the newest content, view height */
static int vback;             /* drag: pixels scrolled back from view_max */
/* last_sel: selection of the previous draw (a new selection resets hscroll);
 * hscroll: horizontal scroll of the selected wide entry (pixels);
 * escroll: horizontal scroll of the Textbook entry line (keeps the caret
 * visible) */
static int last_sel = -1, hscroll, escroll;
/* The blinking caret: position and size on screen, whether it is visible
 * now (on), whether the last draw placed one (shown), the pixels under it
 * (3 px wide, up to 48 px tall) and the time of the last blink. */
static struct {
    bool on, shown;
    int x, y, h;
    uint16_t under[3 * 48];
    uint32_t last;
} caret;

/* ------------------------------------------------------------ settings */
/* The 2D view replaces the Classic home drawing whenever any of its features
 * is on (Textbook, preview, repeating decimals) or the large font is used
 * (the Classic code only knows the 10x20 font). */
bool m2d_home_active(void) { return m2d_textbook || m2d_preview || m2d_repeat || theme_large(); }

/* repeating decimals only make sense for MODE NORMAL FLOAT results */
static bool plain_number_format(void) { return g.fmt == FMT_NORMAL && g.fix < 0; }

/* significant digits of the CAS output (MODE DIGITS; 12 if out of range) */
static int sig_digits(void) { return g.cs.digits > 0 && g.cs.digits <= 15 ? g.cs.digits : 12; }

/* the text to show for an entry (repeating decimals applied to results);
 * item = 2*entry + (0 input, 1 result); *err only for error results */
static void block_text(int item, char *out, size_t n, bool *err)
{
    *err = false;
    const char *s = home_hist_text(item / 2, item % 2, err);
    if (item % 2 && !*err && m2d_repeat && plain_number_format() && home_cell_kind(item / 2) == NBK_MATH)
        repdec_apply(s, out, n, sig_digits());
    else
        snprintf(out, n, "%s", s);
    if (item % 2 == 0)
        *err = false;
}

/* the preview text as shown: pt with repeating decimals applied (when on);
 * used both to measure and to draw it, so the block has the drawn height */
static void preview_shown(const char *pt, char *out, size_t n)
{
    if (m2d_repeat && plain_number_format())
        repdec_apply(pt, out, n, sig_digits());
    else
        snprintf(out, n, "%s", pt);
}

/* text columns of the current home font */
static int cols(void) { return LCD_WIDTH / UI_CW; }

/* number of screen lines of s wrapped at cols() (at least 1) */
static int wrapped_lines(const char *s)
{
    int n = utf8_len(s), c = cols();
    return n ? (n + c - 1) / c : 1;
}

/* 2D layout of linear text: root box (measured; wider than the screen, it
 * is broken into lines, bx_break) or -1 when Textbook display is off, the
 * text is an error, cannot be parsed, overflows the pool or is taller than
 * the body minus a line (it is then shown as wrapped text) */
static int layout_text(bxpool_t *p, const char *s, int lvl)
{
    if (!m2d_textbook || !*s || !strncmp(s, "ERR:", 4))
        return -1;
    int r = m2d_parse(p, s, lvl);
    if (r < 0 || p->overflow)
        return -1;
    bx_measure(p, r);
    r = bx_break(p, r, LCD_WIDTH - 4); /* wider than the screen: on several lines */
    if (p->overflow || p->b[r].asc + p->b[r].desc > BODY_H - UI_CH)
        return -1;
    return r;
}

/* block height of a laid-out entry: its ink + 2 px, at least one text line */
static int box_height(const bx_t *b) { return b->asc + b->desc + 2 > UI_CH ? b->asc + b->desc + 2 : UI_CH; }

/* text-log note clipped to the body (a note overlapping the status bar would
 * drop its runs) */
static void note(int x, int y, int w, int h, const char *s)
{
    if (y < BODY_Y) {
        h -= BODY_Y - y;
        y = BODY_Y;
    }
    if (h > 0 && w > 0)
        gfx_textlog_note(x, y, w, h, s);
}

/* ------------------------------------------------------------ entry line */
/* layout of the Textbook entry line and its caret maps (m2d_layout_editor) */
typedef struct {
    int16_t tokbox[ED_MAX], rows[MAXROWS];
    int nrows, root;
    int lines; /* the BX_LINES box when the line was broken (bx_break), else -1 */
} entry_lay_t;

/* lay out and measure the entry line, broken into lines when wider than the
 * screen (bx_break); the box to place and draw, or -1 on pool overflow */
static int entry_layout(bxpool_t *p, const editor_t *ed, entry_lay_t *L)
{
    L->root = m2d_layout_editor(p, ed, 0, L->tokbox, L->rows, &L->nrows, MAXROWS);
    bx_measure(p, L->root);
    if (p->overflow)
        return -1;
    int r = bx_break(p, L->root, LCD_WIDTH - 4);
    L->lines = r != L->root ? r : -1;
    return p->overflow ? -1 : r;
}

/* broken entry line: the line (row) whose height holds y (placed boxes), else the nearest */
static int entry_line_at(const bxpool_t *p, const entry_lay_t *L, int y)
{
    int best = -1, bd = 1 << 30;
    for (int c = p->b[L->lines].child; c >= 0; c = p->b[c].next) {
        const bx_t *k = &p->b[c];
        int d = y < k->y - k->asc ? k->y - k->asc - y : y > k->y + k->desc ? y - k->y - k->desc : 0;
        if (d < bd) {
            bd = d;
            best = c;
        }
    }
    return best;
}

/* caret rectangle for token position pos (boxes placed): the innermost row
 * (smallest token range) containing pos decides the height (one line of that
 * row's font) and the left edge for an empty slot; otherwise the caret sits
 * at the right edge of the box of the token before it (tokbox). */
static void caret_rect(const bxpool_t *p, const entry_lay_t *L, int pos, int *x, int *top, int *h)
{
    int best = -1, span = 1 << 30;
    for (int k = 0; k < L->nrows; k++) {
        const bx_t *r = &p->b[L->rows[k]];
        if (r->tb <= pos && pos <= r->te && r->te - r->tb < span) {
            span = r->te - r->tb;
            best = L->rows[k];
        }
    }
    if (best < 0)
        best = L->root;
    const bx_t *r = &p->b[best];
    int bx = pos > r->tb ? L->tokbox[pos - 1] : -1;
    int a = bx_asc(r->lvl), d = bx_desc(r->lvl);
    if (best == L->root && L->lines >= 0) {
        /* the top level of a broken line: its boxes moved to the lines (the root row is not placed) */
        int ln = entry_line_at(p, L, bx >= 0 ? p->b[bx].y : -32768);
        *x = bx >= 0 ? p->b[bx].x + p->b[bx].w : p->b[ln].x;
        *top = p->b[ln].y - a - 1;
        *h = a + d + 2;
        return;
    }
    *x = bx >= 0 ? p->b[bx].x + p->b[bx].w : r->x + (r->child < 0 && (r->flags & BXF_SLOT) ? 1 : 0);
    if (*x > r->x + r->w && r->child < 0)
        *x = r->x;
    *top = r->y - a - 1;
    *h = a + d + 2;
}

/* Draw (on: save the pixels underneath first) or erase (restore them) the
 * caret, directly in the framebuffer and clipped to the body. */
static void caret_paint(bool on)
{
    if (!caret.shown)
        return;
    uint16_t *fb = gfx_framebuffer();
    if (on) {
        for (int y = 0; y < caret.h; y++)
            for (int x = 0; x < 3; x++)
                caret.under[3 * y + x] = gfx_get_pixel(caret.x + x, caret.y + y);
        /* straight into the framebuffer: a fill would drop text-log runs */
        uint16_t c = gfx_map(g.shift ? C_YELLOW : g.alpha ? C_LTBLUE : TH_FG);
        int w = g.shift || g.alpha ? 3 : 2;
        for (int y = 0; y < caret.h; y++)
            for (int x = 0; x < w; x++) {
                int xx = caret.x + x, yy = caret.y + y;
                if (xx >= 0 && xx < LCD_WIDTH && yy >= BODY_Y && yy < UI_BOTTOM)
                    fb[yy * LCD_WIDTH + xx] = c;
            }
        gfx_mark_dirty(caret.x, caret.y, 3, caret.h);
    } else {
        for (int y = 0; y < caret.h; y++)
            for (int x = 0; x < 3; x++) {
                int xx = caret.x + x, yy = caret.y + y;
                if (xx >= 0 && xx < LCD_WIDTH && yy >= BODY_Y && yy < UI_BOTTOM)
                    fb[yy * LCD_WIDTH + xx] = caret.under[3 * y + x];
            }
        gfx_mark_dirty(caret.x, caret.y, 3, caret.h);
    }
    caret.on = on;
}

/* place the caret (clamped to the body, at most 48 px tall) and show it;
 * the blink phase restarts so it is visible right after a key */
static void caret_show(int x, int y, int h)
{
    caret.shown = true;
    caret.x = x;
    caret.y = y < BODY_Y ? BODY_Y : y;
    caret.h = h > 48 ? 48 : h;
    caret.last = hal_millis();
    caret_paint(true);
}

/* ------------------------------------------------------------ blocks */
/* Height in pixels of a block: item >= 0 a history text, -1 the entry line
 * (2D, or Classic lines plus one if the cursor is past the last line), -3
 * the preview (0 if there is none). Reuses the pool p (reset here). */
/* lines of a text with line breaks, each wrapped at the screen width */
static int para_lines(const char *s)
{
    int n = 0;
    for (;;) {
        const char *e = strchr(s, '\n');
        int k = 0, c = cols();
        for (const char *q = s; *q && q != e; utf8_next(&q))
            k++;
        n += k ? (k + c - 1) / c : 1;
        if (!e)
            return n;
        s = e + 1;
    }
}

static int md_text(const char *s, int y, bool draw, uint16_t fg, bool hl);
static int block_height(int item, const editor_t *ed, bxpool_t *p, entry_lay_t *L)
{
    char s[TEXT_MAX];
    bool err;
    if (item >= 0) {
        int kind = home_cell_kind(item / 2);
        if (kind == NBK_GRAPH)
            return gcell_height(item / 2);
        if (kind == NBK_TEXT)
            return md_text(home_hist_text(item / 2, 0, &err), 0, false, TH_FG, false);
        if (kind != NBK_MATH) {
            int n = para_lines(home_hist_text(item / 2, item % 2, &err));
            return (n > 40 ? 40 : n) * UI_CH + 2;
        }
        int16_t *hc = &nbd->cell[item / 2].hh[item % 2];
        if (*hc > 0)
            return *hc;
        block_text(item, s, sizeof s, &err);
        int h = wrapped_lines(s) * UI_CH;
        if (!err) {
            bx_init(p, p->b, p->cap);
            int r = layout_text(p, s, 0);
            if (r >= 0)
                h = box_height(&p->b[r]);
        }
        return *hc = (int16_t)h;
    }
    if (item == -1) {
        if (m2d_textbook) {
            bx_init(p, p->b, p->cap);
            int r = entry_layout(p, ed, L);
            if (r >= 0)
                return box_height(&p->b[r]) + 2;
        }
        ed_display(ed, s, sizeof s);
        int n = wrapped_lines(s), ccol = ed_cursor_col(ed);
        if (ccol / cols() >= n)
            n++;
        return n * UI_CH;
    }
    /* preview */
    const char *pt = m2d_preview_text(ed);
    if (!pt)
        return 0;
    preview_shown(pt, s, sizeof s);
    bx_init(p, p->b, p->cap);
    if (m2d_textbook) {
        int r = m2d_parse(p, s, 1);
        if (r >= 0 && !p->overflow) {
            bx_measure(p, r);
            if (!p->overflow && p->b[r].asc + p->b[r].desc < BODY_H / 3)
                return p->b[r].asc + p->b[r].desc + 3;
        }
    }
    return font_medium.h + 2;
}

/* linear text, wrapped at the screen width (the Classic look); `right`
 * aligns each line right (results), `hl` highlights it (selection) */
static int draw_lines(const char *s, int y, bool right, uint16_t fg, bool hl)
{
    const font_t *f = UI_FONT;
    int c = cols(), x0 = -1000;
    char tmp[64 * 4 + 1];
    const char *p = s;
    do {
        const char *q = p;
        for (int i = 0; i < c && *q; i++)
            utf8_next(&q);
        size_t l = (size_t)(q - p);
        if (l > sizeof tmp - 1)
            l = sizeof tmp - 1;
        memcpy(tmp, p, l);
        tmp[l] = 0;
        int w = gfx_text_width(f, tmp);
        int x = right ? LCD_WIDTH - w - 2 : 0;
        if (x0 == -1000)
            x0 = right ? x : w;
        if (y >= BODY_Y) { /* (a line cut by the status bar is not drawn) */
            if (hl)
                gfx_fill_rect(x, y, w, UI_CH, TH_SEL_BG);
            if (l)
                gfx_text(f, x, y, tmp, hl ? TH_SEL_FG : fg, GFX_TRANSPARENT);
        }
        y += UI_CH;
        p = q;
    } while (*p);
    return x0; /* (the first line's left edge, or its right edge when left-aligned) */
}

void home_count_draw(int i, bool right, int x1, int top, int h)
{
    if (i < 0 || i >= nbd->ncells || nbd->cell[i].count <= 0)
        return;
    char t[12];
    snprintf(t, sizeof t, "[%d]", nbd->cell[i].count);
    int w = gfx_text_width(&font_small, t), x = right ? LCD_WIDTH - w - 2 : 2;
    int y = top + (h - font_small.h) / 2;
    if ((right ? x1 + 8 > x : x + w + 6 > x1) || y < BODY_Y || y + font_small.h > UI_BOTTOM)
        return;
    gfx_text(&font_small, x, y, t, TH_DIM, GFX_TRANSPARENT);
}

/* a text with line breaks (text and Python cells), each line wrapped; at most 40 lines */
static int draw_para(const char *s, int y, uint16_t fg, bool hl, int h)
{
    static char line[TEXT_MAX];
    int x1 = -1;
    if (hl)
        gfx_fill_rect(0, y, LCD_WIDTH, h, TH_SEL_BG);
    for (int n = 0; n < 40; n++) {
        const char *e = strchr(s, '\n');
        size_t l = e ? (size_t)(e - s) : strlen(s);
        if (l > sizeof line - 1)
            l = sizeof line - 1;
        memcpy(line, s, l);
        line[l] = 0;
        int w = draw_lines(line, y + 1, false, hl ? TH_SEL_FG : fg, false);
        if (x1 < 0)
            x1 = para_lines(line) > 1 ? LCD_WIDTH : w;
        y += para_lines(line) * UI_CH;
        if (!e)
            return x1;
        s = e + 1;
    }
    return x1; /* (the first line's right edge) */
}

/* ------------------------------------------------------------ Markdown (text cells) */
/* A text cell is shown formatted (the browser page's subset, web/notebook): # ## ### headings (#
 * and ## in the large font, # and ### bold), paragraphs (lines next to each other join, word
 * wrapped), **bold**, *italic* (no italic font: plain, without the stars), `code` (on a panel),
 * - * + bullets and 1. numbered lists (hanging indent), > quotes and ``` code blocks (lines as
 * they are). md_text() measures (draw false) and draws with the same layout, so the block has
 * the drawn height. Bold is drawn twice, a pixel apart. Editing shows the Markdown itself. */
enum { MD_B = 1, MD_I = 2, MD_C = 4 };
static char md_s[TEXT_MAX];     /* a paragraph's text without the marks */
static uint8_t md_st[TEXT_MAX]; /* the style of each of its bytes */

/* the inline marks of s (n bytes) appended to md_s/md_st at *k; base: the style of all of it */
static void md_inline(const char *s, int n, int *k, uint8_t base)
{
    uint8_t st = base;
    for (int i = 0; i < n && *k < TEXT_MAX - 1; i++) {
        char c = s[i];
        if ((c == '`' || c == '*') && !(base & MD_C)) {
            int m = c == '*' && i + 1 < n && s[i + 1] == '*' ? 2 : 1;
            uint8_t bit = c == '`' ? MD_C : m == 2 ? MD_B : MD_I;
            /* a mark opens only when it closes later (2*3 stays); no marks inside code */
            bool on = st & bit, close = false;
            for (int j = i + m + 1; !on && j + m <= n && !close; j++)
                close = s[j] == c && (m == 1 || s[j + 1] == c);
            if ((on || close) && (!(st & MD_C) || bit == MD_C)) {
                st ^= bit;
                i += m - 1;
                continue;
            }
        }
        md_st[*k] = st;
        md_s[(*k)++] = c;
    }
    md_s[*k] = 0;
}

/* draw md_s[a..b) at x, y in runs of one style */
static void md_runs(const font_t *f, int a0, int b, int x0, int y, uint16_t fg, bool hl)
{
    static char t[TEXT_MAX];
    for (int pass = hl; pass < 2; pass++) /* code panels first: a fill drops text-log runs */
        for (int a = a0, x = x0; a < b;) {
            int e = a;
            while (e < b && md_st[e] == md_st[a])
                e++;
            memcpy(t, md_s + a, (size_t)(e - a));
            t[e - a] = 0;
            int w = gfx_text_width(f, t);
            if (!pass && (md_st[a] & MD_C))
                gfx_fill_rect(x, y, w, f->h, TH_PANEL);
            if (pass)
                gfx_text(f, x, y, t, fg, GFX_TRANSPARENT);
            if (pass && (md_st[a] & MD_B)) { /* (the text log keeps the first) */
                gfx_textlog_enable(false);
                gfx_text(f, x + 1, y, t, fg, GFX_TRANSPARENT);
                gfx_textlog_enable(true);
            }
            x += w;
            a = e;
        }
}

/* md_s word wrapped from x0 in font f (its next lines too: a hanging indent); *y advances */
static void md_wrap(const font_t *f, int x0, int *y, bool draw, uint16_t fg, bool hl)
{
    int c = (LCD_WIDTH - x0) / f->w, a = 0, n = (int)strlen(md_s);
    if (c < 4)
        c = 4;
    do {
        int b = a, k = 0, brk = -1;
        while (b < n && k < c) {
            if (md_s[b] == ' ')
                brk = b;
            const char *q = md_s + b;
            utf8_next(&q);
            b = (int)(q - md_s);
            k++;
        }
        if (b < n && md_s[b] != ' ' && brk > a)
            b = brk;
        if (draw && *y > BODY_Y - f->h && *y < UI_BOTTOM)
            md_runs(f, a, b, x0, *y, fg, hl);
        *y += f->h;
        a = b;
        while (a < n && md_s[a] == ' ')
            a++;
    } while (a < n);
}

/* the cell's Markdown s from y (drawn, or only measured); its height (at most about 40 lines) */
static int md_text(const char *s, int y, bool draw, uint16_t fg, bool hl)
{
    const font_t *f = UI_FONT, *big = f == &font_medium ? &font_large : f;
    int y0 = y, cw = f->w;
    bool fence = false;
    while (*s && y - y0 < 40 * UI_CH) {
        const char *e = strchr(s, '\n');
        int n = e ? (int)(e - s) : (int)strlen(s), k = 0;
        const char *next = e ? e + 1 : s + n;
        if (n >= 3 && !strncmp(s, "```", 3)) {
            fence = !fence;
            s = next;
            continue;
        }
        if (fence || !n) { /* a code block line as it is; a blank line: a paragraph break */
            if (n) {
                md_inline(s, n, &k, MD_C);
                md_wrap(f, 0, &y, draw, fg, hl);
            } else
                y += UI_CH / 2;
            s = next;
            continue;
        }
        int h = 0;
        while (h < n && s[h] == '#')
            h++;
        if (h && h <= 3 && h < n && s[h] == ' ') {
            md_inline(s + h + 1, n - h - 1, &k, h != 2 ? MD_B : 0);
            md_wrap(h < 3 ? big : f, 0, &y, draw, fg, hl);
            s = next;
            continue;
        }
        int ind = 0;
        while (ind < n && s[ind] == ' ')
            ind++;
        const char *b = s + ind;
        int bn = n - ind, num = 0;
        while (num < bn && num < 6 && b[num] >= '0' && b[num] <= '9')
            num++;
        int mark = bn >= 2 && (*b == '-' || *b == '*' || *b == '+') && b[1] == ' ' ? 2
                   : num && num < bn - 1 && b[num] == '.' && b[num + 1] == ' ' ? num + 2
                   : *b == '>' ? 1 + (bn > 1 && b[1] == ' ')
                               : 0;
        int x0 = (ind / 2) * 2 * cw;
        if (mark) {
            md_inline(b + mark, bn - mark, &k, 0);
            if (!draw)
                ;
            else if (*b == '>')
                gfx_fill_rect(x0 + 2, y, 2, f->h, fg);
            else if (mark > 2) {
                char t[8];
                snprintf(t, sizeof t, "%.*s", num + 1, b);
                gfx_text(f, x0, y, t, fg, GFX_TRANSPARENT);
            } else
                gfx_fill_rect(x0 + cw / 2 - 1, y + f->h / 2 - 2, 4, 4, fg);
            md_wrap(f, x0 + (mark > 2 ? mark : 2) * cw, &y, draw, fg, hl);
            s = next;
            continue;
        }
        /* a paragraph: this line and the plain lines after it */
        for (;;) {
            md_inline(s, n, &k, 0);
            s = next;
            if (!*s || *s == '\n' || *s == '#' || *s == '>' || *s == ' ' || *s == '`' ||
                ((*s == '-' || *s == '*' || *s == '+') && s[1] == ' ') || (*s >= '0' && *s <= '9'))
                break;
            e = strchr(s, '\n');
            n = e ? (int)(e - s) : (int)strlen(s);
            next = e ? e + 1 : s + n;
            md_inline(" ", 1, &k, 0);
        }
        md_wrap(f, x0, &y, draw, fg, hl);
    }
    return y - y0 + 2 > UI_CH ? y - y0 + 2 : UI_CH;
}

/* Draw a measured history layout in the block [top, top+h): centred
 * vertically, left- or right-aligned, or for a layout wider than the screen
 * starting at the left and shifted by hscroll when selected, with ◀ ▶ marks
 * where more is hidden. `linear` is noted in the text log. */
static int draw_2d(bxpool_t *p, int r, const char *linear, int top, int h, bool right, bool hl, uint16_t fg)
{
    bx_t *b = &p->b[r];
    int avail = LCD_WIDTH - 4;
    int x;
    bool wide = b->w > avail;
    if (wide) {
        int maxs = b->w - avail;
        if (!hl)
            hscroll = 0;
        if (hscroll > maxs)
            hscroll = maxs;
        x = 2 - (hl ? hscroll : 0);
    } else {
        x = right ? LCD_WIDTH - 2 - b->w : 0;
    }
    int base = top + (h - (b->asc + b->desc)) / 2 + b->asc;
    bx_place(p, r, x, base);
    if (hl)
        gfx_fill_rect(wide ? 0 : x - 1, top, wide ? LCD_WIDTH : b->w + 2, h, TH_SEL_BG);
    gfx_textlog_enable(false);
    bx_draw(p, r, hl ? TH_SEL_FG : fg);
    if (wide) { /* more to the right / left */
        if (x + b->w > LCD_WIDTH - 2)
            gfx_text(&font_small, LCD_WIDTH - 6, top + (h - 13) / 2, "▶", hl ? TH_SEL_FG : TH_DIM, hl ? TH_SEL_BG : TH_BG);
        if (x < 0)
            gfx_text(&font_small, 0, top + (h - 13) / 2, "◀", hl ? TH_SEL_FG : TH_DIM, hl ? TH_SEL_BG : TH_BG);
    }
    gfx_textlog_enable(true);
    int nx = x < 0 ? 0 : x, nw = b->w < LCD_WIDTH - nx ? b->w : LCD_WIDTH - nx;
    note(nx, top, nw, h, linear);
    return x; /* (its left edge) */
}

/* Draw the entry line being edited and its caret. Textbook: lay out, compute
 * escroll so the caret stays between 8 px from the left and 12 px from the
 * right edge (no scrolling when everything fits), then lay out again (the
 * first bx_place consumed the relative offsets) and draw shifted. Classic:
 * wrapped lines with the app's block cursor, or our bar caret in the large
 * font and for a Textbook line that could not be laid out. */
static void draw_entry(const editor_t *ed, bxpool_t *p, entry_lay_t *L, int top, int h)
{
    char s[TEXT_MAX];
    ed_display(ed, s, sizeof s);
    bool app_cursor = !theme_large() && !m2d_textbook;
    if (m2d_textbook) {
        bx_init(p, p->b, p->cap);
        int r = entry_layout(p, ed, L);
        if (r >= 0) {
            bx_t *b = &p->b[r];
            int base = top + 1 + (h - 2 - (b->asc + b->desc)) / 2 + b->asc;
            /* horizontal scroll: keep the caret on screen */
            bx_place(p, r, 0, base);
            int cx, ct, chh;
            caret_rect(p, L, ed->cur, &cx, &ct, &chh);
            if (cx - escroll > LCD_WIDTH - 12)
                escroll = cx - (LCD_WIDTH - 12);
            if (cx - escroll < 8)
                escroll = cx - 8 > 0 ? cx - 8 : 0;
            if (b->w + 4 <= LCD_WIDTH)
                escroll = 0;
            bx_init(p, p->b, p->cap);
            r = entry_layout(p, ed, L);
            bx_place(p, r, 2 - escroll, base);
            gfx_textlog_enable(false);
            bx_draw(p, r, TH_FG);
            gfx_textlog_enable(true);
            note(0, top, LCD_WIDTH, h, s);
            caret_rect(p, L, ed->cur, &cx, &ct, &chh);
            ui_cursor_hide();
            caret_show(cx, ct, chh);
            return;
        }
    }
    /* linear entry (Classic) */
    const font_t *f = UI_FONT;
    int c = cols(), ccol = ed_cursor_col(ed);
    draw_lines(s, top, false, TH_FG, false);
    int cy = top + (ccol / c) * UI_CH, cxp = (ccol % c) * UI_CW;
    if (cy + UI_CH > UI_BOTTOM) {
        ui_cursor_hide();
        return;
    }
    if (app_cursor) {
        ui_cursor_at(cxp, cy, ed_char_at_cursor(ed), C_BLACK, C_WHITE);
    } else {
        ui_cursor_hide();
        caret_show(cxp, cy + (UI_CH - f->h) / 2, UI_CH);
    }
}

/* ------------------------------------------------------------ draw */
/* Draw the whole home body. sel = selected history item (2*i + 0/1) or -1
 * when editing. Two passes: compute every block's height (laying each out
 * once), choose the view, then lay out and draw the visible blocks. The
 * entry line is drawn without caret while a history item is selected. */
void m2d_home_draw(editor_t *ed, int sel)
{
    bx_t store[POOL];
    bxpool_t pool;
    bx_init(&pool, store, POOL);
    entry_lay_t L;
    caret.shown = false;
    if (sel != last_sel) {
        hscroll = 0;
        last_sel = sel;
    }
    /* the cached block heights hold for these settings */
    static uint32_t lay_key;
    uint32_t key = (uint32_t)m2d_textbook | (uint32_t)m2d_repeat << 1 | (uint32_t)theme_large() << 2 |
                   (uint32_t)g.fmt << 3 | (uint32_t)(g.fix + 1) << 6 | (uint32_t)g.cs.digits << 12 | 1u << 31;
    if (key != lay_key) {
        lay_key = key;
        for (int i = 0; i < nbd->ncells; i++)
            nbd->cell[i].hh[0] = nbd->cell[i].hh[1] = -1;
    }
    /* virtual column: history, entry, preview */
    int nh = home_hist_count();
    nblocks = 0;
    int y = 0, sel_y = -1;
    for (int i = 0; i < nh && nblocks < MAX_BLOCKS - 2; i++)
        for (int k = 0; k < 2; k++) {
            if (!home_item_shown(2 * i + k))
                continue;
            block_t *bl = &blocks[nblocks++];
            bl->item = 2 * i + k;
            bl->y = y;
            bl->h = block_height(bl->item, ed, &pool, &L);
            if (bl->item == sel)
                sel_y = y;
            y += bl->h;
        }
    blocks[nblocks] = (block_t){-1, y, block_height(-1, ed, &pool, &L)};
    y += blocks[nblocks++].h;
    int ph = block_height(-3, ed, &pool, &L);
    if (ph) {
        blocks[nblocks] = (block_t){-3, y, ph};
        y += blocks[nblocks++].h;
    }
    int vh = (BODY_H / UI_CH) * UI_CH;
    int vtop = y > vh ? y - vh : 0;
    view_max = vtop, view_h = vh;
    vtop -= vback < vtop ? vback : vtop;
    if (sel_y >= 0 && sel_y < vtop)
        vtop = sel_y;
    view_top = vtop;

    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, TH_BG);
    gfx_set_clip(0, BODY_Y, LCD_WIDTH, BODY_H);
    bool cursor_drawn = false;
    for (int b = 0; b < nblocks; b++) {
        const block_t *bl = &blocks[b];
        int top = BODY_Y + bl->y - vtop;
        if (top + bl->h <= BODY_Y || top >= UI_BOTTOM)
            continue;
        char s[TEXT_MAX];
        bool err;
        if (bl->item >= 0 && home_cell_kind(bl->item / 2) == NBK_GRAPH) {
            gcell_draw(bl->item / 2, 0, top, LCD_WIDTH, bl->h, bl->item == sel, bl->item == sel && home_graph_active());
            continue;
        }
        if (bl->item >= 0 && home_cell_kind(bl->item / 2) != NBK_MATH) {
            bool e2;
            const char *t = home_hist_text(bl->item / 2, bl->item % 2, &e2);
            bool py = home_cell_kind(bl->item / 2) == NBK_PY;
            if (!py) { /* a text cell: its Markdown formatted */
                bool hl = bl->item == sel;
                if (hl)
                    gfx_fill_rect(0, top, LCD_WIDTH, bl->h, TH_SEL_BG);
                md_text(t, top + 1, true, hl ? TH_SEL_FG : TH_FG, hl);
                continue;
            }
            int x1 = draw_para(t, top, e2 ? TH_ERR : py && bl->item % 2 == 0 ? TH_LINK : TH_FG, bl->item == sel, bl->h);
            if (py && bl->item % 2 == 0 && bl->item != sel)
                gfx_fill_rect(0, top + 1, 2, bl->h - 2, TH_LINK);
            if (py && bl->item % 2 && !e2 && bl->item != sel) /* its [n], at the right of the first line */
                home_count_draw(bl->item / 2, true, x1, top + 1, UI_CH);
            continue;
        }
        if (bl->item >= 0) {
            bool hl = bl->item == sel;
            bool right = bl->item % 2;
            block_text(bl->item, s, sizeof s, &err);
            uint16_t fg = err ? TH_ERR : TH_FG;
            bx_init(&pool, store, POOL);
            int r = err ? -1 : layout_text(&pool, s, 0), x;
            if (r >= 0)
                x = draw_2d(&pool, r, s, top, bl->h, right, hl, fg);
            else
                x = draw_lines(s, top, right, fg, hl);
            if (right && !err) /* the result's [n] at the left (Out[n]) */
                home_count_draw(bl->item / 2, false, x, top, r >= 0 ? bl->h : UI_CH);
        } else if (bl->item == -1) {
            const char *tag = home_entry_tag();
            if (tag) { /* a text / Python line, or a cell edited in place */
                int tw = gfx_text_width(&font_small, tag);
                gfx_fill_rect(LCD_WIDTH - tw - 6, top + 1, tw + 6, 14, TH_PANEL);
                gfx_text(&font_small, LCD_WIDTH - tw - 3, top + 1, tag, TH_LABEL, GFX_TRANSPARENT);
            }
            if (sel < 0) {
                draw_entry(ed, &pool, &L, top, bl->h);
                cursor_drawn = true;
            } else {
                ed_display(ed, s, sizeof s);
                if (m2d_textbook) {
                    bx_init(&pool, store, POOL);
                    int r = entry_layout(&pool, ed, &L);
                    if (r >= 0) {
                        bx_place(&pool, r, 2, top + 1 + (bl->h - 2 - (store[r].asc + store[r].desc)) / 2 +
                                                 store[r].asc);
                        gfx_textlog_enable(false);
                        bx_draw(&pool, r, TH_FG);
                        gfx_textlog_enable(true);
                        note(0, top, LCD_WIDTH, bl->h, s);
                        continue;
                    }
                }
                draw_lines(s, top, false, TH_FG, false);
            }
        } else { /* preview: "= result" right-aligned, dimmed, one level smaller */
            const char *pt = m2d_preview_text(ed);
            if (!pt)
                continue;
            char shown[TEXT_MAX];
            preview_shown(pt, shown, sizeof shown);
            bx_init(&pool, store, POOL);
            int r = m2d_textbook ? m2d_parse(&pool, shown, 1) : -1;
            if (r >= 0 && !pool.overflow) {
                bx_measure(&pool, r);
                int w = store[r].w;
                bx_place(&pool, r, LCD_WIDTH - 2 - w, top + 1 + store[r].asc);
                gfx_textlog_enable(false);
                bx_draw(&pool, r, TH_DIM);
                gfx_text(&font_medium, LCD_WIDTH - 2 - w - 14, top + 1 + store[r].asc - font_medium.ascent, "=",
                         TH_DIM, -1);
                gfx_textlog_enable(true);
                note(LCD_WIDTH - 2 - w, top, w, bl->h, shown);
            } else {
                char line[TEXT_MAX + 4];
                snprintf(line, sizeof line, "= %s", shown);
                int w = gfx_text_width(&font_medium, line);
                gfx_text(&font_medium, w > LCD_WIDTH - 2 ? 0 : LCD_WIDTH - 2 - w, top + 1, line, TH_DIM, -1);
            }
        }
    }
    gfx_reset_clip();
    if (!cursor_drawn)
        ui_cursor_hide();
}

/* ------------------------------------------------------------ keys */
/* index in blocks[] of a history item, or -1 if it was not laid out */
static int block_index(int item)
{
    for (int b = 0; b < nblocks; b++)
        if (blocks[b].item == item)
            return b;
    return -1;
}

/* UP/DOWN between boxes of the entry (numerator <-> denominator, limits,
 * matrix rows): the nearest caret position in the target box, by the x
 * coordinate of the current caret; at the end of a box (or into an empty
 * box) the target box's end */
static bool vertical_move(editor_t *ed, int dir)
{
    int tb, te;
    bool at_end;
    if (!m2d_vertical_target(ed, dir, &tb, &te, &at_end))
        return false;
    if (at_end || tb == te) {
        ed->cur = te;
        return true;
    }
    bx_t store[POOL];
    bxpool_t pool;
    bx_init(&pool, store, POOL);
    entry_lay_t L;
    int r = entry_layout(&pool, ed, &L);
    if (r < 0) {
        ed->cur = te;
        return true;
    }
    bx_place(&pool, r, 0, 100);
    int cx, ct, ch;
    caret_rect(&pool, &L, ed->cur, &cx, &ct, &ch);
    int best = te, score = 1 << 30;
    for (int p = tb; p <= te; p++) {
        int x, t, h;
        caret_rect(&pool, &L, p, &x, &t, &h);
        int dx = x > cx ? x - cx : cx - x;
        if (dx < score)
            score = dx, best = p;
    }
    ed->cur = best;
    return true;
}

/* Keys for the 2D view; true if consumed (home.c then redraws). */
bool m2d_home_key(editor_t *ed, int key, int mod, int sel)
{
    if (sel >= 0) {
        if ((key == KEY_LEFT || key == KEY_RIGHT) && mod == MOD_NONE) {
            /* scroll a wide 2D entry */
            int b = block_index(sel);
            if (b < 0 || !m2d_textbook)
                return false;
            char s[TEXT_MAX];
            bool err;
            block_text(sel, s, sizeof s, &err);
            bx_t store[POOL];
            bxpool_t pool;
            bx_init(&pool, store, POOL);
            int r = err ? -1 : layout_text(&pool, s, 0);
            if (r < 0 || store[r].w <= LCD_WIDTH - 4)
                return false;
            int step = 4 * UI_CW;
            hscroll += key == KEY_RIGHT ? step : -step;
            if (hscroll < 0)
                hscroll = 0;
            return true;
        }
        return false;
    }
    if ((key == KEY_UP || key == KEY_DOWN) && mod == MOD_NONE && m2d_textbook &&
        vertical_move(ed, key == KEY_UP ? -1 : 1))
        return true;
    return m2d_edit_key(ed, key, mod);
}

/* Home screen tick (main loop): run the live preview when the entry has been
 * idle (redraw if it changed and home is showing), else blink the caret
 * every 500 ms. */
void m2d_home_tick(uint32_t now)
{
    editor_t *ed = scr_home.editor ? scr_home.editor() : NULL;
    if (ed && m2d_preview_tick(ed, now) && ui_current() == &scr_home) {
        scr_home.draw();
        return;
    }
    if (caret.shown && now - caret.last >= 500) {
        caret.last = now;
        caret_paint(!caret.on);
    }
}

/* ------------------------------------------------------------ touch */
/* Map a tap to a block of the last draw (see m2d.h for the return value).
 * On the Textbook entry line the caret goes to the position minimising
 * |dx| + 2*dy to the tap (dy = distance outside the caret's vertical span),
 * so taps prefer positions in the box at the tapped height. */
int m2d_home_touch(const touch_event_t *ev, editor_t *ed)
{
    int vy = ev->y - BODY_Y + view_top;
    for (int b = 0; b < nblocks; b++) {
        const block_t *bl = &blocks[b];
        if (vy < bl->y || vy >= bl->y + bl->h)
            continue;
        if (bl->item >= 0)
            return bl->item;
        if (bl->item != -1)
            return -2;
        /* entry line: nearest caret position */
        int top = BODY_Y + bl->y - view_top;
        if (m2d_textbook) {
            bx_t store[POOL];
            bxpool_t pool;
            bx_init(&pool, store, POOL);
            entry_lay_t L;
            int r = entry_layout(&pool, ed, &L);
            if (r >= 0) {
                bx_place(&pool, r, 2 - escroll,
                         top + 1 + (bl->h - 2 - (store[r].asc + store[r].desc)) / 2 + store[r].asc);
                int best = ed->len, score = 1 << 30;
                for (int p = 0; p <= ed->len; p++) {
                    int x, t, h;
                    caret_rect(&pool, &L, p, &x, &t, &h);
                    int dx = x - ev->x, dy = ev->y < t ? t - ev->y : ev->y > t + h ? ev->y - t - h : 0;
                    int sc = (dx < 0 ? -dx : dx) + 2 * dy;
                    if (sc < score)
                        score = sc, best = p;
                }
                ed->cur = best;
                return -1;
            }
        }
        int line = (ev->y - top) / UI_CH;
        ui_editor_tap(ed, line * cols() + (ev->x + UI_CW / 2) / UI_CW);
        return -1;
    }
    return -2;
}

bool m2d_home_hit(int y, int *item, int *top, int *h)
{
    int vy = y - BODY_Y + view_top;
    for (int b = 0; b < nblocks; b++)
        if (vy >= blocks[b].y && vy < blocks[b].y + blocks[b].h) {
            *item = blocks[b].item;
            *top = BODY_Y + blocks[b].y - view_top;
            *h = blocks[b].h;
            return true;
        }
    return false;
}

bool m2d_home_box(int item, int *top, int *h)
{
    for (int b = 0; b < nblocks; b++)
        if (blocks[b].item == item) {
            *top = BODY_Y + blocks[b].y - view_top, *h = blocks[b].h;
            return *top < UI_BOTTOM && *top + *h > BODY_Y;
        }
    return false;
}

/* Drag (m2d.h): blocks whose top is in [t, t + view_h) count as visible. */
bool m2d_home_drag(const touch_event_t *ev, int *sel)
{
    /* whole UI_CH lines; the view stops at the oldest / newest content */
    int t = view_top, up = (t + UI_CH - 1) / UI_CH, down = (view_max - t + UI_CH - 1) / UI_CH;
    t += ui_drag_steps(ev, UI_DRAG_Y, UI_CH, -up, down) * UI_CH;
    t = t < 0 ? 0 : t > view_max ? view_max : t;
    if (t == view_top)
        return false;
    vback = view_max - t;
    int b = *sel >= 0 ? block_index(*sel) : -1;
    if (b >= 0 && (blocks[b].y < t || blocks[b].y >= t + view_h)) {
        int s = -1;
        for (int k = 0; k < nblocks; k++)
            if (blocks[k].item >= 0 && blocks[k].y >= t && blocks[k].y < t + view_h) {
                s = blocks[k].item;
                if (blocks[b].y < t)
                    break; /* pushed off the top: the first visible item */
            }
        *sel = s;
    }
    return true;
}

void m2d_home_unscroll(void) { vback = 0; }

/* console `state` keys of the 2D view (textbook, preview_on, preview,
 * repeat, theme, font, hscroll) */
void m2d_home_state(void (*kv)(const char *, const char *, void *), void *user)
{
    char b[16];
    kv("textbook", m2d_textbook ? "1" : "0", user);
    kv("preview_on", m2d_preview ? "1" : "0", user);
    editor_t *ed = scr_home.editor ? scr_home.editor() : NULL;
    const char *pt = ed ? m2d_preview_text(ed) : NULL;
    kv("preview", pt ? pt : "", user);
    kv("repeat", m2d_repeat ? "1" : "0", user);
    kv("theme", theme_is_dark() ? "dark" : "light", user);
    kv("font", theme_large() ? "large" : "normal", user);
    snprintf(b, sizeof b, "%d", hscroll);
    kv("hscroll", b, user);
}

/* ------------------------------------------------------------ MODE rows */
/* option 0 is the default (TEXTBOOK / ON / 0.(3)), so get() returns !flag */
static int display_get(void) { return !m2d_textbook; }
static void display_set(int c)
{
    m2d_textbook = c == 0;
    editor_t *ed = scr_home.editor ? scr_home.editor() : NULL;
    if (!m2d_textbook && ed)
        m2d_flatten(ed); /* Classic edits linear text */
}
const mode_row_t mode_m2d_display = {"DISPLAY", {"TEXTBOOK", "CLASSIC"}, 2, display_get, display_set,
                                     "mode-display"};

static int preview_get(void) { return !m2d_preview; }
static void preview_set(int c)
{
    m2d_preview = c == 0;
    m2d_preview_reset();
}
const mode_row_t mode_m2d_preview = {"PREVIEW", {"ON", "OFF"}, 2, preview_get, preview_set, "mode-preview"};

static int repeat_get(void) { return !m2d_repeat; }
static void repeat_set(int c) { m2d_repeat = c == 0; }
const mode_row_t mode_m2d_repeat = {"REPEAT", {"0.(3)", "OFF"}, 2, repeat_get, repeat_set, "mode-repeat"};

/* ------------------------------------------------------------ persistence */
/* blob "textbook" version 1: three bytes, textbook / preview / repeat */
static void m2d_save(pw_t *w)
{
    pw_u8(w, m2d_textbook);
    pw_u8(w, m2d_preview);
    pw_u8(w, m2d_repeat);
}

static void m2d_load(pr_t *r, unsigned version)
{
    unsigned mp = pr_u8(r), pv = pr_u8(r), rp = pr_u8(r);
    if (r->err)
        return;
    m2d_textbook = mp != 0;
    m2d_preview = pv != 0;
    m2d_repeat = rp != 0;
}

/* MEM > Reset all: everything on (the defaults) */
static void m2d_reset(void)
{
    m2d_textbook = m2d_preview = m2d_repeat = true;
    m2d_preview_reset();
}

const persist_def_t persist_m2d = {"textbook", 1, m2d_save, m2d_load, m2d_reset};

/* ------------------------------------------------------------ RPN stack levels */
/* The 2D renderer of the RPN stack (rpn.h; installed by app_init with
 * rpn_set_renderer). A level is shown like a home-screen result: its display
 * text with repeating decimals (when on) laid out in textbook form,
 * right-aligned and centred in its box. When Textbook display is off, or the
 * text cannot be laid out or is wider than the level, measure() returns -1
 * and the RPN screen draws its built-in wrapped text for that level. */
static int rpn_layout(bxpool_t *p, const char *disp, int w, char *s, size_t n)
{
    preview_shown(disp, s, n); /* the same repeating-decimal rule as results */
    int r = layout_text(p, s, 0);
    return r >= 0 && p->b[r].w <= w ? r : -1;
}

static int rpn_measure(const char *cas, const char *disp, int w)
{
    bx_t store[POOL];
    bxpool_t p;
    char s[TEXT_MAX];
    bx_init(&p, store, POOL);
    int r = rpn_layout(&p, disp, w, s, sizeof s);
    return r < 0 ? -1 : box_height(&store[r]);
}

static void rpn_draw_level(const char *cas, const char *disp, int x, int y, int w, int h, uint16_t fg, uint16_t bg)
{
    bx_t store[POOL];
    bxpool_t p;
    char s[TEXT_MAX];
    bx_init(&p, store, POOL);
    int r = rpn_layout(&p, disp, w, s, sizeof s);
    if (r < 0)
        return;
    const bx_t *b = &store[r];
    int bx = x + w - b->w;
    bx_place(&p, r, bx, y + (h - (b->asc + b->desc)) / 2 + b->asc);
    gfx_textlog_enable(false);
    bx_draw(&p, r, fg);
    gfx_textlog_enable(true);
    note(bx, y, b->w, h, s);
}

const rpn_renderer_t m2d_rpn_renderer = {rpn_measure, rpn_draw_level};

/* The Notebook app's textbook output (app/py_core.c NBIO_MATH, nbio_math_t): display text laid
 * out like a home-screen result (repeating decimals when on), measured, and drawn at its top-left
 * when asked; -1 when it is not shown in 2D (Textbook display off, not understood, wider than
 * maxw or taller than the body): the module then shows the text itself. */
int m2d_math_box(void *arg)
{
    nbio_math_t *m = arg;
    bx_t store[POOL];
    bxpool_t p;
    char s[TEXT_MAX];
    bx_init(&p, store, POOL);
    preview_shown(m->disp, s, sizeof s);
    int r = layout_text(&p, s, 0);
    if (r < 0 || store[r].w > m->maxw)
        return -1;
    m->w = store[r].w, m->h = store[r].asc + store[r].desc;
    if (m->draw) {
        bx_place(&p, r, m->x, m->y + store[r].asc);
        gfx_textlog_enable(false);
        bx_draw(&p, r, m->fg);
        gfx_textlog_enable(true);
        note(m->x, m->y, m->w, m->h, s);
    }
    return 0;
}
