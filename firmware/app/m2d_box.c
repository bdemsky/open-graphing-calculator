/*
 * 2D math box layout: measure and draw (see m2d_box.h).
 *
 * This is the typesetting engine behind Textbook display (history entries,
 * the Textbook entry line, the live preview on the home screen). It knows
 * nothing about math syntax: m2d_parse.c (linear text) and m2d_edit.c
 * (template tokens of the editor) build a tree of bx_t boxes, and this file
 *   1. measures it bottom-up (bx_measure: width, ascent, descent of every box,
 *      and each child's offset relative to its parent's left edge/baseline),
 *   2. converts the relative offsets into absolute screen coordinates
 *      (bx_place: x = left edge, y = baseline), and
 *   3. draws it (bx_draw: text, fraction bars, radical signs, delimiters,
 *      big operators, matrix brackets).
 *
 * The layout follows the TeX idea of boxes with a baseline, simplified for a
 * 320x240 bitmap display: all spacing constants are small integer pixel
 * counts tuned by eye on the 10x20 / 7x14 / 6x13 fonts (14x28 / 10x20 /
 * 7x14 in Large font mode). The "math axis"
 * (bx_axis) is the height of the minus sign above the baseline; fraction bars
 * and the vertical centre of big operators and matrices sit on it, like in
 * TeX.
 *
 * Memory: boxes come from a caller-provided pool (bxpool_t, usually an array
 * on the caller's stack). There is no malloc; on exhaustion the pool sets
 * `overflow`, keeps returning box 0 so building code need not check every
 * call, and measure/place/draw then do nothing. Callers check `overflow` and
 * fall back to linear text.
 */
#include "m2d_box.h"
#include <string.h>
#include "gfx.h"
#include "theme.h"

/* ------------------------------------------------------------ pool */
/* Start an empty pool over `store` (cap boxes, owned by the caller). */
void bx_init(bxpool_t *p, bx_t *store, int cap)
{
    p->b = store;
    p->n = 0;
    p->cap = cap;
    p->overflow = false;
}

/* Allocate a zeroed box of `kind` at nesting level `lvl` (clamped to 3; the
 * fonts stop shrinking at level 2, see theme_math_font) with no children and
 * no editor token. Returns its index. On exhaustion sets p->overflow and
 * returns 0 (or -1 for a zero-capacity pool) so that callers can keep
 * building without checking; the result is then discarded as a whole. */
int bx_new(bxpool_t *p, int kind, int lvl)
{
    if (p->n >= p->cap) {
        p->overflow = true;
        return p->cap ? 0 : -1; /* keep going on box 0; the caller checks overflow */
    }
    bx_t *b = &p->b[p->n];
    memset(b, 0, sizeof *b);
    b->kind = (uint8_t)kind;
    b->lvl = (uint8_t)(lvl > 3 ? 3 : lvl);
    b->child = b->next = b->last = -1;
    b->tok = b->tb = b->te = -1;
    return p->n++;
}

/* Append `child` to the end of `parent`'s child list. Children form a singly
 * linked list (child = first, next = sibling, last = tail for O(1) append).
 * Ignored after an overflow or for invalid indices, so a failed bx_new()
 * cannot create a cycle. */
void bx_add(bxpool_t *p, int parent, int child)
{
    if (p->overflow || parent < 0 || child < 0 || parent == child)
        return;
    bx_t *b = &p->b[parent];
    p->b[child].next = -1;
    if (b->last < 0)
        b->child = child;
    else
        p->b[b->last].next = (int16_t)child;
    b->last = (int16_t)child;
}

/* A text box for s[0..len) (UTF-8). The box points into `s`, which must stay
 * valid until the tree has been drawn (parser input, editor token strings or
 * string literals). `flags` are BXF_* text flags. */
int bx_text(bxpool_t *p, int lvl, const char *s, int len, int flags)
{
    int i = bx_new(p, BX_TEXT, lvl);
    if (p->overflow)
        return i;
    p->b[i].s = s;
    p->b[i].len = (uint16_t)len;
    p->b[i].flags = (uint8_t)flags;
    return i;
}

int bx_wrap(bxpool_t *p, int kind, int lvl, int child)
{
    int i = bx_new(p, kind, lvl);
    bx_add(p, i, child);
    return i;
}

int bx_nchildren(const bxpool_t *p, int i)
{
    int n = 0;
    for (int c = p->b[i].child; c >= 0; c = p->b[c].next)
        n++;
    return n;
}

/* ------------------------------------------------------------ metrics */
/* Tight vertical metrics of a font, measured from its bitmaps rather than
 * from the font cell: the fonts' cells include blank rows above and below the
 * ink, which would make stacked fractions and exponents look loose. */
typedef struct {
    const font_t *f;        /* font these metrics belong to (NULL: free slot) */
    int8_t asc, desc, axis; /* ink above / below the baseline, math axis height */
} fmetrics_t;

/* Metrics of font `f`, computed on first use and cached (6 slots: more than
 * the distinct math fonts of both font modes; if the cache were ever full the
 * last slot is overwritten). asc/desc are the extents of the ink of a sample
 * of letters, digits and delimiters relative to the baseline row f->ascent;
 * axis is the height above the baseline of the lowest inked row of '-'
 * (falling back to a quarter of the cell below the ascent). */
static const fmetrics_t *metrics(const font_t *f)
{
    static fmetrics_t cache[6];
    for (int i = 0; i < 6; i++)
        if (cache[i].f == f)
            return &cache[i];
    int slot = 0;
    while (slot < 5 && cache[slot].f)
        slot++;
    /* ink extents of letters, digits and parentheses */
    static const char sample[] = "0123456789abcdfghjklpqtyACGJQ()|";
    int top = f->h, bot = -1;
    for (const char *c = sample; *c; c++) {
        const uint8_t *gl = font_glyph(f, (uint32_t)*c);
        for (int r = 0; r < f->h; r++)
            for (int k = 0; k < f->bytes_per_row; k++)
                if (gl[r * f->bytes_per_row + k]) {
                    if (r < top)
                        top = r;
                    if (r > bot)
                        bot = r;
                }
    }
    int axis_row = f->ascent - f->h / 4;
    const uint8_t *gl = font_glyph(f, '-');
    for (int r = 0; r < f->h; r++)
        for (int k = 0; k < f->bytes_per_row; k++)
            if (gl[r * f->bytes_per_row + k])
                axis_row = r;
    fmetrics_t *m = &cache[slot];
    m->f = f;
    m->asc = (int8_t)(bot >= 0 ? f->ascent - top : f->ascent);
    m->desc = (int8_t)(bot >= 0 ? bot + 1 - f->ascent : f->h - f->ascent);
    m->axis = (int8_t)(f->ascent - axis_row);
    return m;
}

/* Nesting level -> font: level 0 is the normal text size, 1 and 2 are the
 * smaller sizes used for exponents, fraction parts inside fractions, limits of
 * big operators, etc. (theme.c picks the sizes for Normal/Large font mode). */
const font_t *bx_font(int lvl) { return theme_math_font(lvl); }
int bx_asc(int lvl) { return metrics(bx_font(lvl))->asc; }
int bx_desc(int lvl) { return metrics(bx_font(lvl))->desc; }
int bx_axis(int lvl) { return metrics(bx_font(lvl))->axis; }

/* font for big operators (one size up from the level). At level 0 this is
 * font_xlarge, which in Large font mode is also the level-0 font itself, so
 * there is no bigger size (delim_tier() then skips tier 1). */
static const font_t *big_font(int lvl) { return lvl == 0 ? &font_xlarge : bx_font(lvl - 1); }

static int max(int a, int b) { return a > b ? a : b; }

/* horizontal padding on each side of a text box: relations 4 px, binary
 * operators 2 px, everything else none */
static int pad_of(const bx_t *b) { return b->flags & BXF_WIDEPAD ? 4 : b->flags & BXF_PAD ? 2 : 0; }

/* How a delimiter around content of height h is drawn: 0 = the level's
 * glyph, 1 = the glyph of the font one size up, 2 = drawn to size.
 * Tier 0 when the content is no taller than one line of text (+2 px slack),
 * tier 1 when it fits the bigger font's ink, else tier 2 (vector-drawn
 * parenthesis/bracket/brace/bar of exactly the content height). */
static int delim_tier(int lvl, int h)
{
    if (h <= bx_asc(lvl) + bx_desc(lvl) + 2)
        return 0;
    const font_t *bf = big_font(lvl);
    const fmetrics_t *m = metrics(bf);
    if (bf != bx_font(lvl) && h <= m->asc + m->desc + 1)
        return 1;
    return 2;
}

/* width of a delimiter `ch` (0 = none) for content height h; drawn
 * delimiters grow slowly with the height so tall parentheses still bulge */
static int fence_w(int lvl, int ch, int h)
{
    if (!ch)
        return 0;
    switch (delim_tier(lvl, h)) {
    case 0:
        return ch == '|' ? 4 : bx_font(lvl)->w;
    case 1:
        return ch == '|' ? 5 : big_font(lvl)->w * 3 / 4;
    default:
        return ch == '|' ? 5 : max(6, 4 + h / 9);
    }
}

/* ------------------------------------------------------------ measure */
/* Lay out a row: children left to right on the row's baseline. Ordinary
 * children sit on the baseline (y = 0). BX_SUP / BX_SUB attach to the box
 * before them: an exponent is raised so that a third of its ascent overlaps
 * the previous box's ascent (but at least half an ascent of the row's font),
 * a subscript is lowered by half its ascent + 1 or so that it hangs half its
 * descent below the previous box, whichever is lower. The row's ascent and
 * descent enclose all children; an empty row gets one line of the level's
 * font, and an empty editor slot (BXF_SLOT) gets a minimum width so its
 * dotted placeholder box is visible and tappable. */
static void measure_row(bxpool_t *p, int i)
{
    bx_t *b = &p->b[i];
    int x = 0, asc = 0, desc = 0;
    int prev_asc = bx_asc(b->lvl), prev_desc = bx_desc(b->lvl);
    bool any = false;
    for (int c = b->child; c >= 0; c = p->b[c].next) {
        bx_measure(p, c);
        bx_t *k = &p->b[c];
        k->x = (int16_t)x;
        k->y = 0;
        if (k->kind == BX_SUP) {
            int raise = max(prev_asc - (k->asc * 2) / 3, bx_asc(b->lvl) / 2);
            k->y = (int16_t)-raise;
        } else if (k->kind == BX_SUB) {
            int drop = max(k->asc / 2 + 1, prev_desc - k->desc / 2);
            k->y = (int16_t)drop;
        } else {
            prev_asc = k->asc;
            prev_desc = k->desc;
        }
        asc = max(asc, k->asc - k->y);
        desc = max(desc, k->desc + k->y);
        x += k->w;
        any = true;
    }
    if (!any) {
        asc = bx_asc(b->lvl);
        desc = bx_desc(b->lvl);
        if (b->flags & BXF_SLOT)
            x = max(5, bx_font(b->lvl)->w - 2);
    }
    b->w = (int16_t)x;
    b->asc = (int16_t)asc;
    b->desc = (int16_t)desc;
}

/* Measure the subtree at i (recursively, children first): set w/asc/desc of
 * every box and the x/y of every child *relative to its parent* (x from the
 * parent's left edge, y from the parent's baseline, positive = down).
 * bx_place() must run afterwards (exactly once) to make them absolute. */
void bx_measure(bxpool_t *p, int i)
{
    if (i < 0 || p->overflow)
        return;
    bx_t *b = &p->b[i];
    int lvl = b->lvl;
    switch (b->kind) {
    case BX_ROW:
        measure_row(p, i);
        break;
    case BX_TEXT: { /* monospaced: width = code points x cell width + padding */
        const char *s = b->s;
        int n = 0;
        while (s < b->s + b->len && utf8_next(&s))
            n++;
        b->w = (int16_t)(n * bx_font(lvl)->w + 2 * pad_of(b) + !!(b->flags & BXF_BOLD));
        b->asc = (int16_t)bx_asc(lvl);
        b->desc = (int16_t)bx_desc(lvl);
        if (b->flags & BXF_OVERLINE)
            b->asc += 3; /* room for the repeating-decimal bar */
        break;
    }
    case BX_GAP:
        b->w = b->aux;
        b->asc = b->desc = 0;
        break;
    case BX_LINES: { /* first line on the baseline, the others below it, 3 px apart */
        int y = 0, w = 0, d = 0;
        for (int c = b->child; c >= 0; c = p->b[c].next) {
            bx_measure(p, c);
            bx_t *k = &p->b[c];
            if (c == b->child) {
                k->x = 0;
                b->asc = k->asc;
            } else {
                k->x = b->aux;
                y += d + 3 + k->asc;
            }
            k->y = (int16_t)y;
            d = k->desc;
            w = max(w, k->x + k->w);
        }
        b->w = (int16_t)w;
        b->desc = (int16_t)(y + d);
        break;
    }
    case BX_SUP:
    case BX_SUB: { /* the script itself, 1 px margin each side; the vertical
                      shift is applied by the enclosing row (measure_row) */
        int c = b->child;
        bx_measure(p, c);
        if (c < 0)
            break;
        bx_t *k = &p->b[c];
        k->x = 1;
        k->y = 0;
        b->w = (int16_t)(k->w + 2);
        b->asc = k->asc;
        b->desc = k->desc;
        break;
    }
    case BX_FRAC: {
        /* Bar on the math axis (y = -axis), 2 px wider than the wider part on
         * each side plus 1 px outer margin. Numerator: its bottom ink 2 px
         * above the bar; denominator: its top ink 3 px below it (the bar row
         * itself is 1 px). Both parts are centred horizontally. */
        int n = b->child, d = n >= 0 ? p->b[n].next : -1;
        bx_measure(p, n);
        bx_measure(p, d);
        if (n < 0 || d < 0)
            break;
        bx_t *bn = &p->b[n], *bd = &p->b[d];
        int w = max(bn->w, bd->w) + 4;
        int axis = bx_axis(lvl);
        bn->x = (int16_t)((w - bn->w) / 2);
        bn->y = (int16_t)(-axis - 2 - bn->desc);
        bd->x = (int16_t)((w - bd->w) / 2);
        bd->y = (int16_t)(-axis + 3 + bd->asc);
        b->w = (int16_t)(w + 2);
        bn->x++;
        bd->x++;
        b->asc = (int16_t)(axis + 2 + bn->desc + bn->asc);
        b->desc = (int16_t)(bd->asc + bd->desc + 3 - axis);
        break;
    }
    case BX_ROOT: {
        /* [index] radical sign | radicand. The sign is as tall as the
         * radicand + 3 px (2 px gap + the 1 px vinculum on top) and its width
         * grows with the height. An index (n-th root) is drawn small, above
         * the sign's hook (2/5 of the height from the bottom), shifted right
         * so it overlaps the left half of the sign; `off` is the extra room it
         * needs on the left, kept in aux for bx_draw. */
        int r = b->child, ix = r >= 0 ? p->b[r].next : -1;
        bx_measure(p, r);
        bx_measure(p, ix);
        if (r < 0)
            break;
        bx_t *br = &p->b[r];
        int h = br->asc + br->desc + 3;
        int sw = 6 + h / 8;       /* width of the radical sign */
        int off = 0;              /* room for the index on the left */
        if (ix >= 0) {
            bx_t *bi = &p->b[ix];
            off = max(0, bi->w - sw / 2);
            bi->x = (int16_t)(off + sw / 2 - bi->w);
            if (bi->x < 0)
                bi->x = 0;
            bi->y = (int16_t)(br->desc - h * 2 / 5 - 1 - bi->desc);
        }
        br->x = (int16_t)(off + sw + 1);
        br->y = 0;
        b->w = (int16_t)(br->x + br->w + 1);
        b->asc = (int16_t)(br->asc + 3);
        b->desc = (int16_t)(br->desc + 1);
        b->aux = (uint8_t)off;
        if (ix >= 0)
            b->asc = (int16_t)max(b->asc, p->b[ix].asc - p->b[ix].y);
        break;
    }
    case BX_FENCE: {
        /* left delimiter | content | right delimiter, sized to the content
         * height; tall (tier 1/2) delimiters get a 1 px margin above/below */
        int c = b->child;
        bx_measure(p, c);
        int asc = c >= 0 ? p->b[c].asc : bx_asc(lvl), desc = c >= 0 ? p->b[c].desc : bx_desc(lvl);
        int h = asc + desc;
        int lw = fence_w(lvl, b->aux, h), rw = fence_w(lvl, b->aux2, h);
        if (c >= 0) {
            p->b[c].x = (int16_t)lw;
            p->b[c].y = 0;
        }
        b->w = (int16_t)(lw + (c >= 0 ? p->b[c].w : 0) + rw);
        bool tall = delim_tier(lvl, h) > 0;
        b->asc = (int16_t)(asc + (tall ? 1 : 0));
        b->desc = (int16_t)(desc + (tall ? 1 : 0));
        break;
    }
    case BX_BIGOP: {
        /* Σ, Π, ∫ in the next bigger font, vertically centred on the math
         * axis (sasc/sdesc = the symbol's extent around the baseline, trimmed
         * by 1 px), or the word "lim" in the level's font. Limits: for Σ/Π/lim
         * centred above (upper) and below (lower) the symbol, 1 px apart; for
         * ∫ to the right of the symbol at its top and bottom, the lower limit
         * tucked 3 px left under the slanted foot (textbook style). Empty
         * limit rows that are not editor slots take no space. The operand
         * follows as the next box of the enclosing row. */
        int lo = b->child, up = lo >= 0 ? p->b[lo].next : -1;
        bx_measure(p, lo);
        bx_measure(p, up);
        const font_t *bf = big_font(lvl);
        int sw = b->aux == BIG_LIM ? 3 * bx_font(lvl)->w : bf->w;
        int sasc = b->aux == BIG_LIM ? bx_asc(lvl) : bf->h / 2 + bx_axis(lvl) - 1;
        int sdesc = b->aux == BIG_LIM ? bx_desc(lvl) : bf->h - (bf->h / 2 + bx_axis(lvl)) - 1;
        bool have_lo = lo >= 0 && (p->b[lo].child >= 0 || (p->b[lo].flags & BXF_SLOT));
        bool have_up = up >= 0 && (p->b[up].child >= 0 || (p->b[up].flags & BXF_SLOT));
        int asc = sasc, desc = sdesc, w = sw;
        if (b->aux == BIG_INT) {
            int lw = have_lo ? p->b[lo].w : 0, uw = have_up ? p->b[up].w : 0;
            if (have_up) {
                p->b[up].x = (int16_t)(sw);
                p->b[up].y = (int16_t)(-sasc + p->b[up].asc);
            }
            if (have_lo) {
                p->b[lo].x = (int16_t)(sw - 3);
                p->b[lo].y = (int16_t)(sdesc - p->b[lo].desc);
            }
            w = sw + max(lw - 3, uw) + 2;
        } else {
            int lw = have_lo ? p->b[lo].w : 0, uw = have_up ? p->b[up].w : 0;
            w = max(sw, max(lw, uw)) + 2;
            if (have_up) {
                p->b[up].x = (int16_t)((w - uw) / 2);
                p->b[up].y = (int16_t)(-sasc - 1 - p->b[up].desc);
                asc = sasc + 1 + p->b[up].asc + p->b[up].desc;
            }
            if (have_lo) {
                p->b[lo].x = (int16_t)((w - lw) / 2);
                p->b[lo].y = (int16_t)(sdesc + 1 + p->b[lo].asc);
                desc = sdesc + 1 + p->b[lo].asc + p->b[lo].desc;
            }
        }
        b->w = (int16_t)w;
        b->asc = (int16_t)asc;
        b->desc = (int16_t)desc;
        break;
    }
    case BX_MATRIX: {
        /* Grid of cells (row-major children, aux columns, at most 12x12):
         * column width = widest cell, row height = tallest ascent + deepest
         * descent of the row, 3 px between rows, 10 px between columns, cells
         * centred in their column on the row's baseline. The grid is centred
         * vertically on the math axis (top = its top edge relative to the
         * baseline); brackets 5 px wide + 2 px gap on both sides. */
        int cols = b->aux ? b->aux : 1;
        int n = bx_nchildren(p, i), rows = (n + cols - 1) / cols;
        int16_t cw[12] = {0}, ra[12] = {0}, rd[12] = {0};
        if (cols > 12 || rows > 12) {
            p->overflow = true;
            return;
        }
        int k = 0;
        for (int c = b->child; c >= 0; c = p->b[c].next, k++) {
            bx_measure(p, c);
            bx_t *e = &p->b[c];
            int r = k / cols, q = k % cols;
            if (e->w > cw[q]) cw[q] = e->w;
            if (e->asc > ra[r]) ra[r] = e->asc;
            if (e->desc > rd[r]) rd[r] = e->desc;
        }
        int h = 0;
        for (int r = 0; r < rows; r++)
            h += ra[r] + rd[r] + (r ? 3 : 0);
        int br = 5, top = -(h / 2 + bx_axis(b->lvl));
        k = 0;
        int y = top;
        int xs[12];
        int x = br + 2;
        for (int q = 0; q < cols; q++) {
            xs[q] = x;
            x += cw[q] + 10;
        }
        int w = x - 10 + 2 + br;
        for (int c = b->child; c >= 0; c = p->b[c].next, k++) {
            int r = k / cols, q = k % cols;
            if (q == 0) {
                if (r)
                    y += rd[r - 1] + 3;
                y += ra[r];
            }
            bx_t *e = &p->b[c];
            e->x = (int16_t)(xs[q] + (cw[q] - e->w) / 2);
            e->y = (int16_t)y;
        }
        b->w = (int16_t)w;
        b->asc = (int16_t)(-top + 1);
        b->desc = (int16_t)(h + top + 1);
        break;
    }
    default:
        break;
    }
}

/* Turn the relative offsets left by bx_measure() into absolute coordinates:
 * box i gets left edge x and baseline y, its children x + their offset, and
 * so on down the tree. The offsets are overwritten, so call it once per
 * measure (re-measure before placing the tree at a different position). */
void bx_place(bxpool_t *p, int i, int x, int y)
{
    if (i < 0 || p->overflow)
        return;
    for (int c = p->b[i].child; c >= 0; c = p->b[c].next)
        bx_place(p, c, x + p->b[c].x, y + p->b[c].y);
    p->b[i].x = (int16_t)x;
    p->b[i].y = (int16_t)y;
}

/* Line breaking (bx_break): the class of a break just before unit k,
 * 0 none, 1 before a product's or quotient's operator or inside a long
 * number, 2 before + − ± or a relation, or after a comma. */
static int brk_class(const bx_t *b, const bx_t *prev, const bx_t *prev2)
{
    if (prev && prev->kind == BX_GAP && prev2 && prev2->kind == BX_TEXT && prev2->len == 1 && prev2->s[0] == ',')
        return 2; /* "a, b": after the comma's gap */
    if (b->kind != BX_TEXT || !b->len)
        return 0;
    if (b->flags & BXF_WIDEPAD)
        return 2;
    if (b->s[0] == '+' || !strncmp(b->s, "−", 3) || !strncmp(b->s, "±", 2))
        return (b->flags & BXF_PAD) || prev ? 2 : 0;
    if (!strncmp(b->s, "·", 2) || !strncmp(b->s, "×", 2) || !strncmp(b->s, "÷", 2) || b->s[0] == '/')
        return 1;
    return 0;
}

#define BRK_MAX 384 /* units of one broken layout (more: it scrolls instead) */
typedef struct {
    int16_t u[BRK_MAX];
    uint8_t cls[BRK_MAX];
    int n;
    bool full;
} brk_t;

/* append unit c to the break list */
static void brk_push(bxpool_t *p, brk_t *k, int c, int cls)
{
    if (k->n >= BRK_MAX) {
        k->full = true;
        return;
    }
    k->u[k->n] = (int16_t)c;
    k->cls[k->n] = (uint8_t)(k->n ? cls : 0);
    k->n++;
    (void)p;
}

/* The children of row r as a flat list of units: plain nested rows (not an
 * editor slot, not followed by an exponent or subscript) are opened up, and
 * a text wider than `chunk` pixels (a long number) is cut into pieces that
 * fit, with a class 1 break between them. */
static void brk_collect(bxpool_t *p, brk_t *k, int r, int chunk)
{
    for (int c = p->b[r].child; c >= 0 && !k->full && !p->overflow; c = p->b[c].next) {
        bx_t *b = &p->b[c];
        int nx = b->next;
        bool script = nx >= 0 && (p->b[nx].kind == BX_SUP || p->b[nx].kind == BX_SUB);
        if (b->kind == BX_ROW && !(b->flags & BXF_SLOT) && b->child >= 0 && !script) {
            brk_collect(p, k, c, chunk);
            continue;
        }
        const bx_t *prev = k->n ? &p->b[k->u[k->n - 1]] : NULL;
        const bx_t *prev2 = k->n > 1 ? &p->b[k->u[k->n - 2]] : NULL;
        int cls = brk_class(b, prev, prev2);
        int cw = bx_font(b->lvl)->w, per = cw > 0 ? chunk / cw : 0;
        if (b->kind == BX_TEXT && !pad_of(b) && b->w > chunk && per > 0 && !script) {
            /* cut at code points: pieces of `per` characters, each its own text box */
            const char *s = b->s, *end = b->s + b->len;
            int tok = b->tok, flags = b->flags, lvl = b->lvl;
            bool first = true;
            while (s < end && !p->overflow) {
                const char *e = s;
                for (int n = 0; n < per && e < end && utf8_next(&e); n++)
                    ;
                int t = bx_text(p, lvl, s, (int)(e - s), flags);
                if (t < 0)
                    break;
                p->b[t].tok = e >= end ? (int16_t)tok : -1;
                bx_measure(p, t);
                brk_push(p, k, t, first ? cls : 1);
                first = false;
                s = e;
            }
            continue;
        }
        brk_push(p, k, c, cls);
    }
}

/* Line breaking of a layout wider than the screen: the measured layout i,
 * when wider than avail, has its top level (the root row, or its only child
 * row, with plain nested rows opened up) split greedily into lines, each as
 * full as fits: before a + − ± or a relation or after a comma; a term that
 * still does not fit on a line of its own is split before its · × ÷ /, and
 * a long number between its digits. Continuation lines are indented by one
 * character so the operator that starts them stands out. A piece that
 * cannot be split (a wide fraction or matrix) keeps its own line, wider
 * than avail (the caller scrolls it). Returns the new BX_LINES box,
 * measured, or i when it fits or cannot be broken. */
int bx_break(bxpool_t *p, int i, int avail)
{
    if (i < 0 || p->overflow || p->b[i].w <= avail)
        return i;
    int s = i;
    while (p->b[s].kind == BX_ROW && p->b[s].child >= 0 && p->b[s].child == p->b[s].last &&
           p->b[p->b[s].child].kind == BX_ROW && !(p->b[p->b[s].child].flags & BXF_SLOT))
        s = p->b[s].child;
    if (p->b[s].kind != BX_ROW || p->b[s].child < 0)
        return i;
    int lvl = p->b[s].lvl, ind = bx_font(lvl)->w;
    static brk_t k; /* (too big for the stack next to a pool) */
    k.n = 0;
    k.full = false;
    brk_collect(p, &k, s, avail - ind);
    int nbrk = 0;
    for (int j = 1; j < k.n; j++)
        nbrk += k.cls[j] > 0;
    if (k.full || p->overflow || !nbrk)
        return i; /* (boxes may have moved into the list: still in their rows) */
    int L = bx_new(p, BX_LINES, lvl), line = -1, w = 0;
    if (L < 0)
        return i;
    p->b[L].aux = (uint8_t)ind;
    for (int j = 0; j < k.n; j++) /* the units leave their rows */
        p->b[k.u[j]].next = -1;
    for (int j = 0; j < k.n && !p->overflow;) {
        int e = j + 1, sw = p->b[k.u[j]].w; /* segment [j, e): up to the next class 2 break */
        for (; e < k.n && k.cls[e] < 2; e++)
            sw += p->b[k.u[e]].w;
        int start = line < 0 || p->b[L].child == line ? 0 : ind;
        if (line < 0 || (w + sw > avail && w > start)) {
            line = bx_new(p, BX_ROW, lvl);
            bx_add(p, L, line);
            w = start = p->b[L].child == line ? 0 : ind;
        }
        if (w + sw <= avail) {
            for (; j < e; j++)
                bx_add(p, line, k.u[j]);
            w += sw;
            continue;
        }
        /* too wide even on its own line: pieces up to the next break of any class */
        while (j < e && !p->overflow) {
            int f = j + 1, pw = p->b[k.u[j]].w;
            for (; f < e && !k.cls[f]; f++)
                pw += p->b[k.u[f]].w;
            if (w + pw > avail && w > start) {
                line = bx_new(p, BX_ROW, lvl);
                bx_add(p, L, line);
                w = start = ind;
            }
            for (; j < f; j++)
                bx_add(p, line, k.u[j]);
            w += pw;
        }
    }
    bx_measure(p, L);
    return L;
}

/* ------------------------------------------------------------ draw */
/* 2 px wide line (the thick down-stroke of the radical sign) */
static void thick_line(int x0, int y0, int x1, int y1, uint16_t c)
{
    gfx_line(x0, y0, x1, y1, c);
    gfx_line(x0 + 1, y0, x1 + 1, y1, c);
}

/* delimiter `ch` in the box [x, x+w) x [top, top+h); `left` = opening side
 * (closing delimiters are drawn mirrored), `base` = baseline for glyphs.
 * Tier 2 shapes: a parenthesis is a 2 px wide parabola, x offset =
 * bulge * u^2 / (h-1)^2 with u running from -(h-1) to h-1 over the height, so
 * the ends lean outwards by `bulge` and the middle touches the inner edge;
 * brackets are a 2 px bar with 1 px serifs; braces a centre line with
 * hooked ends and a point at mid-height. */
static void draw_delim(int ch, bool left, int lvl, int x, int base, int top, int w, int h, uint16_t fg)
{
    if (!ch || w <= 0)
        return;
    /* (h includes the 1 px margins of a tall fence) */
    int tier = delim_tier(lvl, h > bx_asc(lvl) + bx_desc(lvl) + 2 ? h - 2 : h);
    if (ch == '|' && tier < 2) {
        gfx_fill_rect(x + 1, top, 2, h, fg);
        return;
    }
    if (tier == 0) { /* text size: the font's glyph */
        const font_t *f = bx_font(lvl);
        gfx_char(f, x, base - f->ascent, (uint32_t)ch, fg, GFX_TRANSPARENT);
        return;
    }
    if (tier == 1) { /* the bigger font's glyph, centred on the content */
        const font_t *f = big_font(lvl);
        const fmetrics_t *m = metrics(f);
        int ink_mid = f->ascent + (m->desc - m->asc) / 2;
        gfx_char(f, x - f->w / 8, top + h / 2 - ink_mid, (uint32_t)ch, fg, GFX_TRANSPARENT);
        return;
    }
    int bot = top + h - 1;
    if (ch == ')' || ch == ']' || ch == '}')
        ch = ch == ')' ? '(' : ch == ']' ? '[' : '{';
    switch (ch) {
    case '(': {
        int bulge = w - 4;
        for (int t = 0; t < h; t++) {
            int u = 2 * t - (h - 1); /* -(h-1) .. h-1 */
            int off = (int)((long)bulge * u * u / ((long)(h - 1) * (h - 1)));
            int xx = left ? x + 1 + off : x + w - 2 - off;
            gfx_fill_rect(left ? xx : xx - 1, top + t, 2, 1, fg);
        }
        break;
    }
    case '[':
        if (left) {
            gfx_fill_rect(x + 1, top, 2, h, fg);
            gfx_hline(x + 1, top, w - 2, fg);
            gfx_hline(x + 1, bot, w - 2, fg);
        } else {
            gfx_fill_rect(x + w - 3, top, 2, h, fg);
            gfx_hline(x + 1, top, w - 2, fg);
            gfx_hline(x + 1, bot, w - 2, fg);
        }
        break;
    case '{': {
        int mid = top + h / 2, xs = left ? x + w / 2 : x + w / 2 - 1;
        gfx_fill_rect(xs, top + 2, 1, h - 4, fg);
        gfx_line(xs, top + 2, left ? xs + 2 : xs - 2, top, fg);
        gfx_line(xs, bot - 2, left ? xs + 2 : xs - 2, bot, fg);
        gfx_line(xs, mid, left ? xs - 2 : xs + 2, mid, fg);
        break;
    }
    case '|':
    default:
        gfx_fill_rect(x + w / 2 - 1, top, 2, h, fg);
        break;
    }
}

/* Draw a text box (copied to a NUL-terminated buffer; text boxes are short
 * tokens, longer text is truncated to 127 bytes) and its repeating-decimal
 * overline. BXF_DIM text uses the theme's dim colour instead of fg. */
static void draw_text(const bx_t *b, uint16_t fg)
{
    char tmp[128];
    int n = b->len < sizeof tmp - 1 ? b->len : (int)sizeof tmp - 1;
    memcpy(tmp, b->s, (size_t)n);
    tmp[n] = 0;
    const font_t *f = bx_font(b->lvl);
    int x = b->x + pad_of(b);
    uint16_t c = b->flags & BXF_DIM ? TH_DIM : fg;
    gfx_text(f, x, b->y - f->ascent, tmp, c, GFX_TRANSPARENT);
    if (b->flags & BXF_BOLD)
        gfx_text(f, x + 1, b->y - f->ascent, tmp, c, GFX_TRANSPARENT);
    if (b->flags & BXF_OVERLINE)
        gfx_hline(x, b->y - b->asc, b->w - 2 * pad_of(b), c);
}

/* Draw a measured and placed subtree in colour fg: the box's own decoration
 * first (bar, radical, delimiters, operator symbol, brackets, placeholder),
 * then its children. Draws through the gfx clip; the caller disables the text
 * log (gfx_textlog_enable(false)) and logs the linear text itself. */
void bx_draw(const bxpool_t *p, int i, uint16_t fg)
{
    if (i < 0 || p->overflow)
        return;
    const bx_t *b = &p->b[i];
    int top = b->y - b->asc, h = b->asc + b->desc;
    switch (b->kind) {
    case BX_TEXT:
        draw_text(b, fg);
        return;
    case BX_ROW:
        if (b->child < 0 && (b->flags & BXF_SLOT)) { /* empty slot: dotted box */
            for (int x = b->x; x < b->x + b->w; x += 2) {
                gfx_pixel(x, top, TH_DIM);
                gfx_pixel(x, top + h - 1, TH_DIM);
            }
            for (int y = top; y < top + h; y += 2) {
                gfx_pixel(b->x, y, TH_DIM);
                gfx_pixel(b->x + b->w - 1, y, TH_DIM);
            }
        }
        break;
    case BX_FRAC:
        gfx_fill_rect(b->x + 1, b->y - bx_axis(b->lvl), b->w - 2, 1, fg);
        break;
    case BX_ROOT: {
        int r = b->child;
        if (r < 0)
            break;
        const bx_t *br = &p->b[r];
        int x0 = b->x + b->aux, xr = br->x - 1, rt = br->y - br->asc - 2, bot = br->y + br->desc;
        /* radical: short tick up to the hook at 2/5 of the height, thick
         * stroke down to the bottom at mid-width, thin stroke up to the top
         * right, then the vinculum over the radicand */
        int sw = xr - x0;
        int ym = bot - (bot - rt) * 2 / 5;
        gfx_line(x0, ym, x0 + 2, ym - 1, fg);
        thick_line(x0 + 2, ym - 1, x0 + sw / 2, bot, fg);
        gfx_line(x0 + sw / 2 + 1, bot, xr, rt, fg);
        gfx_hline(xr, rt, br->w + 2, fg);
        break;
    }
    case BX_FENCE: {
        int c = b->child;
        int cw = c >= 0 ? p->b[c].w : 0;
        int lw = (c >= 0 ? p->b[c].x : b->x + b->w) - b->x;
        int rw = b->w - lw - cw;
        draw_delim(b->aux, true, b->lvl, b->x, b->y, top, lw, h, fg);
        draw_delim(b->aux2, false, b->lvl, b->x + lw + cw, b->y, top, rw, h, fg);
        break;
    }
    case BX_BIGOP: {
        const font_t *bf = big_font(b->lvl);
        if (b->aux == BIG_LIM) {
            const font_t *f = bx_font(b->lvl);
            int sw = 3 * f->w;
            gfx_text(f, b->x + (b->w - sw) / 2, b->y - f->ascent, "lim", fg, GFX_TRANSPARENT);
            break;
        }
        static const char *const sym[] = {"", "Σ", "Π", "∫"};
        int sx = b->aux == BIG_INT ? b->x : b->x + (b->w - bf->w) / 2;
        int cy = b->y - bx_axis(b->lvl) - bf->h / 2;
        gfx_text(bf, sx, cy, sym[b->aux & 3], fg, GFX_TRANSPARENT);
        break;
    }
    case BX_MATRIX:
        draw_delim('[', true, b->lvl, b->x, b->y, top, 5, h, fg);
        draw_delim('[', false, b->lvl, b->x + b->w - 5, b->y, top, 5, h, fg);
        break;
    default:
        break;
    }
    for (int c = b->child; c >= 0; c = p->b[c].next)
        bx_draw(p, c, fg);
}
