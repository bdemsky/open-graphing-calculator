/*
 * The Notebook's screens (help page "notebook"): the list of notebooks and the
 * editor of a cell, with a popup menu, a name prompt and a yes/no question of
 * their own. Drawn by the module on the core's Notebook screen (app/py_core.c
 * scr_notebook); keys the module does not use go to the core's entry line,
 * which edits the current line of a cell. The notebook itself is shown on the
 * home screen (app/home.c): its MENU Open (and APPS > Notebook) shows the list
 * here, ENTER on a text or Python cell edits it here; opening a notebook and
 * finishing an edit go back to the home screen.
 *
 * List: UP/DOWN choose, ENTER (F1) opens (the home screen shows it), F2 new, F3
 * rename, F4 delete, F5 more (copy to the card / to the flash, previous
 * version); tap chooses, a tap on the chosen row or a double tap opens, drag
 * scrolls. CLEAR leaves. The built-in example notebooks (nb_list_all,
 * nb_examples.c) come last, marked "example": they open like stored ones and
 * can be copied to the card, not renamed or deleted.
 *
 * Editing: the notebook's cells are shown with the edited cell's lines in
 * place, the current one is the entry line at the bottom (the core's editor:
 * menus, catalog, clipboard). ENTER runs a Math or Graph cell; in Python and
 * Text cells it starts a new line (indented like this one, more after ':');
 * UP/DOWN move between lines; F1 runs; F2 indents (SHIFT F1 dedents); F3
 * characters; F4 cancels, F5 done; CLEAR clears the line, then (empty) removes
 * it, then ends editing. A tap on another cell ends editing.
 *
 * Changes are kept in memory and saved at power-off, when another notebook is
 * opened, and two minutes after the last change (nb_sync.c). While the browser
 * page follows the notebook a cell edited in the browser cannot be edited here.
 */
#include <string.h>
#include "py_mod.h"
#include "nb.h"
#include "gen/keymap_gen.h"
#include "touch.h"

#define W 320
#define TOP 18
#define BOT 224
#define HDR 14
#define VTOP (TOP + HDR)
#define GUT 26
#define CX 28
#define CWID (W - CX - 4)
#define COLS (CWID / 7)
#define LH 14
#define PAD 3
#define FG 0x0000
#define BG 0xFFFF
#define C_SEL 0x02BB
#define C_SELBG 0xDF1F
#define C_DIM 0x8410
#define C_ERR 0xD8E3
#define C_OUT 0x4208
#define C_SEP 0xC618
#define C_CODEBG 0xEF5D
#define C_ITAL 0x2A69
#define C_MATHTXT 0x5014
#define OUT_LINES 8   /* output lines shown before "more" */
#define ELINE 230     /* longest line the entry line takes */

/* V_PAGE: the screen is being left for the home screen, which shows the notebook */
enum { V_LIST, V_PAGE, V_EDIT, V_PROMPT, V_CONFIRM };
enum { P_NEW = 1, P_RENAME };
/* the list's menu actions */
enum { A_NONE, A_OPEN, A_NEW, A_RENAME, A_DELETE, A_COPY, A_REVERT };

typedef struct {
    const char *title;
    int n, sel, top;
    const char *item[8];
    uint8_t act[8];
} menu_t;

static struct {
    int view;
    bool menu_open;
    menu_t m;
    nb_item_t items[80];
    int nitems, lsel, ltop;
    int sel, scroll;
    int ecell, eline;           /* editing: the cell, the line in the entry */
    int prompt;
    char qmsg[80];
    int drag_y, drag_acc, down_y, down_x;
    bool dragged;
    uint32_t tap_t;
    int tap_cell;
    uint32_t last_draw;
    char sk[10][10];
    int clip0, clip1;           /* drawing: rows that may be drawn */
    bool picker;
    int psel;
} U;

/* the cell being edited: its lines, '\n' separated, the current one in the entry line */
static char ebuf[64 << 10];

static const modx_api_t *A(void) { return py_api; }
static int io(int op, uint32_t a, void *p, size_t n) { return py_pc->nb_io(op, a, p, n); }

/* ---------------------------------------------------------------- soft keys */
static void softkeys(const char *const k[10])
{
    for (int i = 0; i < 10; i++) {
        const char *l = k[i] ? k[i] : "";
        if (strcmp(U.sk[i], l)) {
            A()->snprintf(U.sk[i], sizeof U.sk[i], "%s", l);
            io(NBIO_SOFTKEY, (uint32_t)i, (void *)l, 0);
        }
    }
}
static void set_softkeys(void)
{
    static const char *const list[10] = {"open", "new", "rename", "delete", "more", NULL, NULL, NULL, NULL, NULL};
    static const char *const edit[10] = {"run", "tab", "chars", "cancel", "done", "dedent", "calc.", NULL, NULL, NULL};
    static const char *const prompt[10] = {"ok", NULL, NULL, NULL, "cancel", NULL, NULL, NULL, NULL, NULL};
    static const char *const ask[10] = {"yes", NULL, NULL, NULL, "no", NULL, NULL, NULL, NULL, NULL};
    static const char *const menu[10] = {"choose", NULL, NULL, NULL, "close", NULL, NULL, NULL, NULL, NULL};
    static const char *const none[10] = {NULL};
    softkeys(U.menu_open ? menu
             : U.view == V_LIST ? list
             : U.view == V_PAGE ? none
             : U.view == V_EDIT ? edit
             : U.view == V_PROMPT ? prompt
                                 : ask);
}

/* ---------------------------------------------------------------- text */
static bool visible(int y, int h) { return y >= U.clip0 && y + h <= U.clip1; }

/* bytes of the first `cols` code points of s (up to len) */
static size_t cp_bytes(const char *s, size_t len, int cols)
{
    size_t i = 0;
    for (int k = 0; i < len && k < cols; k++) {
        i++;
        while (i < len && ((unsigned char)s[i] & 0xC0) == 0x80)
            i++;
    }
    return i;
}
static int cp_count(const char *s, size_t len)
{
    int k = 0;
    for (size_t i = 0; i < len; i++)
        k += ((unsigned char)s[i] & 0xC0) != 0x80;
    return k;
}

/* s wrapped at cols (and at its new lines), lines from `skip` on, at most maxl (0: all); drawn at
 * (x, y) in font f, right-aligned at x + cols * cw when `right`. -> lines drawn; *total all lines */
static int text_lines(const char *s, int x, int y, int cols, uint16_t c, int skip, int maxl, int *total, bool draw,
                      bool right, const font_t *f)
{
    char line[COLS * 4 + 8];
    int n = 0, shown = 0, lh = f->h;
    while (s && *s) {
        const char *nl = strchr(s, '\n');
        size_t len = nl ? (size_t)(nl - s) : strlen(s);
        size_t at = 0;
        do {
            size_t k = cp_bytes(s + at, len - at, cols);
            if (n >= skip && (!maxl || shown < maxl)) {
                if (draw && visible(y + shown * lh, lh)) {
                    memcpy(line, s + at, k < sizeof line - 1 ? k : sizeof line - 1);
                    line[k < sizeof line - 1 ? k : sizeof line - 1] = 0;
                    int lx = right ? x + (cols - cp_count(line, strlen(line))) * f->w : x;
                    A()->text(f, lx, y + shown * lh, line, c, -1);
                }
                shown++;
            }
            n++;
            at += k;
        } while (at < len);
        s = nl ? nl + 1 : s + len;
    }
    if (total)
        *total = n;
    return shown;
}

/* ---------------------------------------------------------------- textbook math */
/* LaTeX (as Markdown has it) -> the calculator's linear form, for the common commands: \frac,
 * \sqrt, ^{}, _{}, \cdot, \pi, ... (others leave their name). Brace groups are converted
 * recursively; the output is cut at n. */
typedef struct {
    char *o;
    size_t k, n;
} lx_t;
static void lx_put(lx_t *x, const char *s, size_t len)
{
    for (size_t i = 0; i < len && x->k + 1 < x->n; i++)
        x->o[x->k++] = s[i];
}
/* the end of the brace group that starts at s ('{'): just after its '}' */
static const char *lx_group_end(const char *s, const char *e)
{
    int d = 0;
    for (; s < e; s++) {
        if (*s == '{')
            d++;
        else if (*s == '}' && !--d)
            return s + 1;
    }
    return e;
}
static void lx_conv(lx_t *x, const char *s, const char *e);
/* a group {..} (or one character) converted, in parentheses when `paren` */
static const char *lx_arg(lx_t *x, const char *s, const char *e, bool paren)
{
    while (s < e && *s == ' ')
        s++;
    if (s >= e)
        return e;
    const char *g = *s == '{' ? lx_group_end(s, e) : s + 1;
    if (paren)
        lx_put(x, "(", 1);
    if (*s == '{')
        lx_conv(x, s + 1, g - 1 > s ? g - 1 : g);
    else
        lx_put(x, s, 1);
    if (paren)
        lx_put(x, ")", 1);
    return g;
}
static void lx_conv(lx_t *x, const char *s, const char *e)
{
    static const struct {
        char tex[10], lin[8];
    } m[] = {{"\\cdot", "*"}, {"\\times", "\xC3\x97"}, {"\\pi", "\xCF\x80"}, {"\\le", "\xE2\x89\xA4"},
             {"\\ge", "\xE2\x89\xA5"}, {"\\ne", "\xE2\x89\xA0"}, {"\\infty", "\xE2\x88\x9E"}, {"\\theta", "\xCE\xB8"},
             {"\\left", ""}, {"\\right", ""}, {"\\,", ""}, {"\\;", ""}, {"\\!", ""}, {"\\quad", " "},
             {"\\sin", "sin"}, {"\\cos", "cos"}, {"\\tan", "tan"}, {"\\ln", "ln"}, {"\\log", "log"}, {"\\exp", "exp"}};
    while (s < e) {
        if (*s == '\\') {
            if (!strncmp(s, "\\frac", 5) || !strncmp(s, "\\dfrac", 6)) {
                s += s[1] == 'd' ? 6 : 5;
                s = lx_arg(x, s, e, true);
                lx_put(x, "/", 1);
                s = lx_arg(x, s, e, true);
                continue;
            }
            if (!strncmp(s, "\\sqrt", 5)) {
                lx_put(x, "\xE2\x88\x9A", 3);
                s = lx_arg(x, s + 5, e, true);
                continue;
            }
            size_t i = 0;
            for (; i < sizeof m / sizeof m[0]; i++) {
                size_t l = strlen(m[i].tex);
                if ((size_t)(e - s) >= l && !strncmp(s, m[i].tex, l) && !(s + l < e && s[l] >= 'a' && s[l] <= 'z')) {
                    lx_put(x, m[i].lin, strlen(m[i].lin));
                    s += l;
                    break;
                }
            }
            if (i == sizeof m / sizeof m[0])
                s++; /* another command: its name as text */
            continue;
        }
        if (*s == '^' || *s == '_') { /* an exponent or index: its group in parentheses */
            lx_put(x, s, 1);
            s = lx_arg(x, s + 1, e, s[1] == '{');
            continue;
        }
        if (*s == '{') {
            const char *g = lx_group_end(s, e);
            lx_put(x, "(", 1);
            lx_conv(x, s + 1, g - 1 > s ? g - 1 : g);
            lx_put(x, ")", 1);
            s = g;
            continue;
        }
        if (*s != '}')
            lx_put(x, s, 1);
        s++;
    }
}
static void latex_linear(const char *s, char *o, size_t n)
{
    lx_t x = {o, 0, n};
    lx_conv(&x, s, s + strlen(s));
    o[x.k] = 0;
}

/* 2D layout of display text: 0 and its size (drawn at x, y when draw), -1 not in 2D */
static int math_box(const char *disp, int x, int y, int maxw, uint16_t c, bool draw, int *w, int *h)
{
    nbio_math_t m = {disp, x, y, maxw, c, false, 0, 0};
    if (io(NBIO_MATH, 0, &m, 0))
        return -1;
    *w = m.w, *h = m.h;
    if (draw && visible(y, m.h)) {
        m.draw = true;
        io(NBIO_MATH, 0, &m, 0);
    }
    return 0;
}

static void pretty(const char *cas, char *out, size_t n)
{
    nbio_text_t t = {cas, out, n};
    if (io(NBIO_PRETTY, 0, &t, 0))
        A()->snprintf(out, n, "%s", cas);
}

/* ---------------------------------------------------------------- Markdown */
enum { S_PLAIN, S_BOLD, S_ITAL, S_CODE, S_MATH };
typedef struct {
    char t[COLS * 4 + 8];
    uint8_t style;
    int w, h, x;
    bool space; /* a space before it */
} atom_t;
#define MAXATOM 48

static const font_t *md_font;
static bool md_bold; /* a heading: every atom bold */

/* lay out one line of atoms (already in a[0..n)) at y: -> its height */
static int md_flush(atom_t *a, int n, int y, bool draw)
{
    int h = md_font->h;
    for (int i = 0; i < n; i++)
        if (a[i].h + 2 > h)
            h = a[i].h + 2;
    if (!draw)
        return h;
    for (int i = 0; i < n; i++) {
        atom_t *t = &a[i];
        int ty = y + (h - md_font->h) / 2 + 1;
        if (t->style == S_MATH && t->h) {
            int mw, mh;
            math_box(t->t, t->x, y + (h - t->h) / 2, CWID, FG, true, &mw, &mh);
            continue;
        }
        if (!visible(ty, md_font->h))
            continue;
        uint16_t c = t->style == S_ITAL ? C_ITAL : t->style == S_MATH ? C_MATHTXT : FG;
        if (t->style == S_CODE)
            A()->fill_rect(t->x - 1, ty - 1, t->w + 2, md_font->h + 1, C_CODEBG);
        A()->text(md_font, t->x, ty, t->t, c, -1);
        if (t->style == S_BOLD || md_bold)
            A()->text(md_font, t->x + 1, ty, t->t, c, -1);
    }
    return h;
}

/* inline text (one paragraph or item) wrapped into [x0, x0 + w): -> height */
static int md_inline(const char *s, size_t len, int x0, int y, int w, bool draw)
{
    static atom_t a[MAXATOM];
    int n = 0, x = x0, h = 0, cw = md_font->w;
    int style = S_PLAIN;
    size_t i = 0;
    bool space = false;
    while (i < len) {
        char c = s[i];
        if (c == ' ' || c == '\t') {
            space = true, i++;
            continue;
        }
        /* style markers */
        if ((c == '*' || c == '_') && i + 1 < len && s[i + 1] == c) {
            style = style == S_BOLD ? S_PLAIN : S_BOLD;
            i += 2;
            continue;
        }
        if ((c == '*' || c == '_') && style != S_CODE && (style == S_ITAL || (i + 1 < len && s[i + 1] != ' '))) {
            style = style == S_ITAL ? S_PLAIN : S_ITAL;
            i++;
            continue;
        }
        atom_t t;
        memset(&t, 0, sizeof t);
        t.space = space && n;
        space = false;
        if (c == '`' || c == '$') { /* code or math: up to the closing mark */
            size_t e = i + 1;
            while (e < len && s[e] != c)
                e++;
            size_t k = e - i - 1;
            if (k > sizeof t.t - 1)
                k = sizeof t.t - 1;
            t.style = c == '`' ? S_CODE : S_MATH;
            if (t.style == S_MATH) {
                char raw[COLS * 4 + 8];
                memcpy(raw, s + i + 1, k);
                raw[k] = 0;
                latex_linear(raw, t.t, sizeof t.t);
                int mw, mh;
                if (!math_box(t.t, 0, 0, w, FG, false, &mw, &mh))
                    t.w = mw, t.h = mh;
                else
                    memcpy(t.t, raw, k + 1), t.h = 0;
            } else {
                memcpy(t.t, s + i + 1, k);
                t.t[k] = 0;
            }
            if (!t.h)
                t.w = cp_count(t.t, strlen(t.t)) * cw;
            i = e < len ? e + 1 : e;
        } else { /* a word: up to a space or a marker */
            size_t k = 0;
            while (i < len && s[i] != ' ' && s[i] != '\t' && s[i] != '`' && s[i] != '$' && k < sizeof t.t - 4 &&
                   !((s[i] == '*' || s[i] == '_') && (k == 0 || i + 1 >= len || s[i + 1] == ' ' || s[i + 1] == s[i] ||
                                                       style != S_PLAIN))) {
                if (s[i] == '\\' && i + 1 < len)
                    i++;
                t.t[k++] = s[i++];
            }
            if (!k)
                t.t[k++] = s[i++]; /* (a lone marker) */
            t.t[k] = 0;
            t.style = (uint8_t)style;
            t.w = cp_count(t.t, k) * cw;
        }
        int sp = t.space ? cw : 0;
        if (n && x + sp + t.w > x0 + w) { /* the line is full */
            h += md_flush(a, n, y + h, draw);
            n = 0, x = x0, sp = 0;
        }
        while (t.style != S_MATH && t.w > w) { /* longer than a line: in pieces */
            size_t k = cp_bytes(t.t, strlen(t.t), w / cw);
            atom_t piece = t;
            piece.t[k] = 0;
            piece.w = cp_count(piece.t, k) * cw, piece.x = x0;
            h += md_flush(&piece, 1, y + h, draw);
            memmove(t.t, t.t + k, strlen(t.t + k) + 1);
            t.w = cp_count(t.t, strlen(t.t)) * cw;
        }
        if (t.style == S_MATH && t.h && t.w > w)
            t.w = w;
        t.x = x + sp;
        x = t.x + t.w;
        if (n < MAXATOM)
            a[n++] = t;
    }
    if (n)
        h += md_flush(a, n, y + h, draw);
    return h;
}

/* A Markdown text: headings, paragraphs, lists, quotes, code blocks, $$ display math $$. -> height */
static int md(const char *s, int x, int y, int w, bool draw)
{
    int h = 0;
    bool code = false;
    static char para[8192];
    size_t pn = 0;
    int pind = 0; /* paragraph's indent (list item) */
    const char *bullet = NULL;
    while (s) {
        const char *nl = strchr(s, '\n');
        size_t len = nl ? (size_t)(nl - s) : strlen(s);
        const char *line = s;
        s = nl ? nl + 1 : NULL;
        bool fence = len >= 3 && !strncmp(line, "```", 3);
        size_t lead = 0;
        while (lead < len && line[lead] == ' ')
            lead++;
        bool blank = lead == len;
        bool block = fence || code || blank || line[lead] == '#' || (len - lead >= 2 && strchr("-*+", line[lead]) &&
                     line[lead + 1] == ' ') || (len - lead > 2 && line[lead] >= '0' && line[lead] <= '9' &&
                     (line[lead + 1] == '.' || (line[lead + 2] == '.' && line[lead + 1] >= '0' && line[lead + 1] <= '9'))) ||
                     line[lead] == '>' || (len - lead >= 2 && !strncmp(line + lead, "$$", 2));
        if (block || !s) { /* the paragraph so far ends */
            if (!block && pn + len + 1 < sizeof para) { /* (the last line of the text) */
                if (pn)
                    para[pn++] = ' ';
                memcpy(para + pn, line, len), pn += len;
                len = 0, blank = true, block = false;
            }
            if (pn) {
                md_font = A()->font_medium;
                if (bullet && draw && visible(y + h + 6, 4))
                    A()->fill_rect(x + pind - 9, y + h + 6, 4, 4, C_OUT);
                h += md_inline(para, pn, x + pind, y + h, w - pind, draw);
                pn = 0, pind = 0, bullet = NULL;
            }
            if (!block && blank && !len)
                break;
        }
        if (fence) {
            code = !code;
            h += code ? 2 : 4;
            continue;
        }
        if (code) {
            char cl[COLS * 4 + 8];
            size_t m = cp_bytes(line, len, w / 7);
            memcpy(cl, line, m);
            cl[m] = 0;
            if (draw && visible(y + h, LH)) {
                A()->fill_rect(x, y + h, w, LH, C_CODEBG);
                A()->text(A()->font_medium, x + 2, y + h, cl, C_OUT, -1);
            }
            h += LH;
            continue;
        }
        if (blank) {
            h += 5;
            continue;
        }
        const char *t = line + lead;
        size_t tl = len - lead;
        if (t[0] == '#') { /* heading */
            int lvl = 0;
            while (lvl < (int)tl && t[lvl] == '#')
                lvl++;
            size_t sk = (size_t)lvl;
            while (sk < tl && t[sk] == ' ')
                sk++;
            md_font = lvl == 1 ? A()->font_large : A()->font_medium;
            md_bold = lvl <= 2;
            int hh = md_inline(t + sk, tl - sk, x, y + h + 1, w, draw);
            md_bold = false;
            if (lvl <= 2 && draw && visible(y + h + hh + 3, 1))
                A()->fill_rect(x, y + h + hh + 3, w, 1, C_SEP);
            h += hh + 6;
            continue;
        }
        if (tl >= 2 && !strncmp(t, "$$", 2)) { /* display math, to the closing $$ */
            static char m[1024], lin[1024];
            size_t mn = 0;
            const char *p = t + 2;
            size_t pl = tl - 2;
            for (;;) {
                const char *end = NULL;
                for (size_t i = 0; i + 1 < pl; i++)
                    if (p[i] == '$' && p[i + 1] == '$')
                        end = p + i;
                size_t k = end ? (size_t)(end - p) : pl;
                if (mn + k + 1 < sizeof m)
                    memcpy(m + mn, p, k), mn += k, m[mn++] = ' ';
                if (end || !s)
                    break;
                const char *nl2 = strchr(s, '\n');
                p = s, pl = nl2 ? (size_t)(nl2 - s) : strlen(s);
                s = nl2 ? nl2 + 1 : NULL;
            }
            m[mn] = 0;
            latex_linear(m, lin, sizeof lin);
            int mw, mh;
            if (!math_box(lin, 0, 0, w, FG, false, &mw, &mh)) {
                math_box(lin, x + (w - mw) / 2, y + h + 2, w, FG, draw, &mw, &mh);
                h += mh + 6;
            } else {
                md_font = A()->font_medium;
                h += md_inline(m, mn, x + 14, y + h, w - 14, draw) + 2;
            }
            continue;
        }
        if (t[0] == '>') { /* a quote: indented with a bar */
            size_t sk = 1;
            while (sk < tl && t[sk] == ' ')
                sk++;
            md_font = A()->font_medium;
            int hh = md_inline(t + sk, tl - sk, x + 10, y + h, w - 10, draw);
            if (draw && visible(y + h, hh))
                A()->fill_rect(x + 2, y + h, 3, hh, C_SEP);
            h += hh + 2;
            continue;
        }
        if (block) { /* a list item starts a paragraph of its own */
            size_t sk = 0;
            if (strchr("-*+", t[0]))
                sk = 2, bullet = "-";
            else {
                while (sk < tl && t[sk] != '.')
                    sk++;
                sk += 2;
                bullet = NULL;
                if (draw && visible(y + h + 1, LH)) {
                    char num[8];
                    A()->snprintf(num, sizeof num, "%.*s", (int)(sk - 1 < 6 ? sk - 1 : 6), t);
                    A()->text(A()->font_medium, x + (int)lead * 3, y + h + 1, num, C_OUT, -1);
                }
            }
            pind = (bullet ? 14 : 20) + (int)lead * 3;
            if (sk > tl)
                sk = tl;
            memcpy(para, t + sk, tl - sk);
            pn = tl - sk;
            continue;
        }
        if (pn + len + 1 < sizeof para) { /* a paragraph line */
            if (pn)
                para[pn++] = ' ';
            memcpy(para + pn, t, tl);
            pn += tl;
        }
    }
    if (pn) {
        md_font = A()->font_medium;
        if (bullet && draw && visible(y + h + 6, 4))
            A()->fill_rect(x + pind - 9, y + h + 6, 4, 4, C_OUT);
        h += md_inline(para, pn, x + pind, y + h, w - pind, draw);
    }
    return h;
}

/* ---------------------------------------------------------------- plots */
static void blit(const uint8_t *pix, int x, int y)
{
    for (int r = 0; r < NB_PLOT_H; r++) {
        if (y + r < U.clip0 || y + r >= U.clip1)
            continue;
        const uint8_t *row = pix + r * NB_PLOT_W;
        for (int c = 0; c < NB_PLOT_W;) {
            int e = c + 1;
            while (e < NB_PLOT_W && row[e] == row[c])
                e++;
            A()->fill_rect(x + c, y + r, e - c, 1, nb_plot_pal[row[c]]);
            c = e;
        }
    }
}

/* ---------------------------------------------------------------- a cell */
static int edit_lines(void)
{
    int n = 1;
    for (const char *p = ebuf; *p; p++)
        n += *p == '\n';
    return n;
}
/* line i of the edit buffer: its start and length */
static char *eline_at(int i, size_t *len)
{
    char *p = ebuf;
    for (; i > 0 && p; i--) {
        p = strchr(p, '\n');
        if (p)
            p++;
    }
    if (!p)
        p = ebuf + strlen(ebuf);
    char *e = strchr(p, '\n');
    *len = e ? (size_t)(e - p) : strlen(p);
    return p;
}

static int16_t layout_h(int i);

/* Draw (or measure) cell i at y: -> its height */
static int cell(int i, int y0, bool draw)
{
    nb_cell_t *c = &NB.cell[i];
    bool editing = U.view == V_EDIT && U.ecell == i;
    bool sel = i == U.sel;
    int y = y0 + PAD;
    const font_t *fm = A()->font_medium, *fs = A()->font_small;
    static const char *const tags[NBK_N] = {"T", "M", "Py", "G"};
    static const uint16_t tagc[NBK_N] = {0x8410, 0x02BB, 0x2C47, 0xC300};
    /* input */
    const char *src = editing ? ebuf : nb_get(c, NBF_SRC);
    int ih = 0;
    if (editing) { /* the lines, the one in the entry line marked */
        int n = edit_lines();
        for (int k = 0; k < n; k++) {
            size_t len;
            char *p = eline_at(k, &len);
            char save = p[len];
            p[len] = 0;
            int lines, hh = LH * (k == U.eline ? 1 : 0);
            if (k == U.eline) {
                if (draw && visible(y + ih, LH))
                    A()->fill_rect(CX - 2, y + ih, CWID + 4, LH, C_SELBG);
                static char live[ELINE * 4 + 8]; /* the entry line as it is now */
                py_pc->line_get(live, sizeof live);
                lines = text_lines(live, CX, y + ih, COLS, C_SEL, 0, 1, NULL, draw, false, fm);
                (void)lines;
            } else {
                hh = LH * text_lines(p, CX, y + ih, COLS, FG, 0, 0, NULL, draw, false, fm);
                if (!hh)
                    hh = LH;
            }
            p[len] = save;
            ih += hh;
        }
    } else if (!src[0]) {
        static const char *const empty[NBK_N] = {"(empty text)", "(empty math)", "(empty Python)", "(empty graph)"};
        if (draw && visible(y, LH))
            A()->text(fm, CX, y, empty[c->kind], C_DIM, -1);
        ih = LH;
    } else if (c->kind == NBK_TEXT) {
        ih = md(src, CX, y, CWID, draw);
    } else if (c->kind == NBK_GRAPH) {
        static char gl[600];
        A()->snprintf(gl, sizeof gl, "y = %s", src);
        ih = LH * text_lines(gl, CX, y, COLS, FG, 0, 0, NULL, draw, false, fm);
    } else {
        ih = LH * text_lines(src, CX, y, COLS, FG, 0, 0, NULL, draw, false, fm);
    }
    if (ih < LH)
        ih = LH;
    y += ih;
    if (!editing && nb_page_edits(i)) { /* (the page's edit shows here when it is sent) */
        if (draw && visible(y, fs->h))
            A()->text(fs, CX, y, "being edited in the browser", C_SEL, -1);
        y += fs->h;
    }
    /* output */
    bool expand = c->flags & NBC_EXPAND;
    if (c->f[NBF_OUT]) {
        int total, n = text_lines(c->f[NBF_OUT], CX, y + 2, COLS, C_OUT, 0, expand ? 0 : OUT_LINES, &total, draw, false, fm);
        y += 2 + n * LH;
        if (total > n) {
            static char more[48];
            A()->snprintf(more, sizeof more, "... %d more lines (tap or F4 expand)", total - n);
            if (draw && visible(y, fs->h))
                A()->text(fs, CX, y, more, C_DIM, -1);
            y += fs->h;
        }
    }
    if (c->kind == NBK_GRAPH && c->f[NBF_GRAPH]) { /* the home screen's graph cell: drawn by the core */
        if (draw && visible(y, NB_PLOT_H + 6))
            nb_core->graph_draw(i, CX, y + 3, NB_PLOT_W, NB_PLOT_H);
        y += NB_PLOT_H + 6;
    } else if (c->f[NBF_PLOT] || (c->kind == NBK_GRAPH && (c->flags & NBC_RAN) && !c->f[NBF_ENAME])) {
        static char cas[600];
        if (c->f[NBF_PLOT])
            A()->snprintf(cas, sizeof cas, "%s", c->f[NBF_PLOT]);
        else
            cas[0] = 0;
        if (cas[0]) {
            const uint8_t *pix = draw ? nb_plot_get(cas, c->xmin, c->xmax, NULL, 0) : NULL;
            if (pix)
                blit(pix, CX, y + 3);
            y += NB_PLOT_H + 6;
        }
    }
    if (c->f[NBF_ENAME]) {
        static char e[400];
        const char *tb = nb_get(c, NBF_TB);
        A()->snprintf(e, sizeof e, "%s%s%s", nb_get(c, NBF_ENAME), c->f[NBF_EVALUE] ? ": " : "", nb_get(c, NBF_EVALUE));
        int tbl = 0;
        if (expand && tb[0]) {
            y += 2 + LH * text_lines(tb, CX, y + 2, COLS, C_ERR, 0, 0, NULL, draw, false, fm);
        } else {
            text_lines(tb, 0, 0, COLS, 0, 0, 0, &tbl, false, false, fm);
            y += 2 + LH * text_lines(e, CX, y + 2, COLS, C_ERR, 0, 3, NULL, draw, false, fm);
            if (tbl > 1) {
                if (draw && visible(y, fs->h))
                    A()->text(fs, CX, y, "... traceback (tap or F4 expand)", C_DIM, -1);
                y += fs->h;
            }
        }
    } else if (c->f[NBF_VAL]) {
        static char disp[2048];
        pretty(c->f[NBF_VAL], disp, sizeof disp);
        int mw, mh;
        if ((c->kind == NBK_MATH || c->f[NBF_TEX]) && !math_box(disp, 0, 0, CWID, FG, false, &mw, &mh)) {
            math_box(disp, CX + CWID - mw, y + 3, CWID, FG, draw, &mw, &mh);
            y += mh + 6;
        } else {
            int total, n = cp_count(disp, strlen(disp)) <= COLS && !strchr(disp, '\n');
            n = text_lines(disp, CX, y + 2, COLS, c->kind == NBK_PY ? C_SEL : FG, 0, expand ? 0 : OUT_LINES, &total,
                           draw, n != 0, fm);
            y += 2 + n * LH;
            if (total > n) {
                if (draw && visible(y, fs->h))
                    A()->text(fs, CX, y, "... (tap or F4 expand)", C_DIM, -1);
                y += fs->h;
            }
        }
    }
    if (c->flags & NBC_IMAGE) {
        if (draw && visible(y + 1, fs->h))
            A()->text(fs, CX, y + 1, "(an image output: open the notebook on a computer)", C_DIM, -1);
        y += fs->h + 2;
    }
    y += PAD;
    int h = y - y0;
    if (draw) {
        /* gutter: the kind, the count; the chosen cell's bar */
        int gy0 = y0 > U.clip0 ? y0 : U.clip0, gy1 = y0 + h < U.clip1 ? y0 + h : U.clip1;
        if (sel && gy1 > gy0) {
            A()->fill_rect(0, gy0, 3, gy1 - gy0, C_SEL);
            A()->fill_rect(3, gy0, GUT - 4, gy1 - gy0, C_SELBG);
        }
        if (visible(y0 + PAD, fs->h))
            A()->text(fs, 5, y0 + PAD, tags[c->kind], tagc[c->kind], -1);
        if (c->kind != NBK_TEXT && visible(y0 + PAD + 13, fs->h)) {
            char cnt[12];
            if (i == nb_running_cell())
                A()->snprintf(cnt, sizeof cnt, "[*]");
            else if (c->count)
                A()->snprintf(cnt, sizeof cnt, "%d", c->count);
            else
                cnt[0] = 0;
            A()->text(fs, 4, y0 + PAD + 13, cnt, C_DIM, -1);
        }
        if (visible(y0 + h - 1, 1))
            A()->fill_rect(CX, y0 + h - 1, CWID, 1, C_SEP);
    }
    return h;
}

static int16_t layout_h(int i)
{
    nb_cell_t *c = &NB.cell[i];
    if (c->h < 0 || (U.view == V_EDIT && U.ecell == i)) {
        int s0 = U.clip0, s1 = U.clip1;
        int h = cell(i, 0, false);
        U.clip0 = s0, U.clip1 = s1;
        if (U.view == V_EDIT && U.ecell == i)
            return (int16_t)h;
        c->h = (int16_t)h;
    }
    return c->h;
}

static int cell_y(int i)
{
    int y = 0;
    for (int k = 0; k < i; k++)
        y += layout_h(k);
    return y;
}
static int doc_h(void) { return cell_y(NB.ncells); }
static int view_bot(void) { return U.view == V_EDIT ? BOT - py_pc->line_height() - 4 : BOT; }
static int view_h(void) { return view_bot() - VTOP; }

static void clamp_scroll(void)
{
    int max = doc_h() - view_h() + 20;
    if (U.scroll > max)
        U.scroll = max;
    if (U.scroll < 0)
        U.scroll = 0;
}

/* scroll so that cell i (or, editing, its current line) can be seen */
static void show_cell(int i)
{
    if (i < 0 || i >= NB.ncells)
        return;
    int y = cell_y(i), h = layout_h(i), vh = view_h();
    if (U.view == V_EDIT) {
        int ly = y + PAD + LH * U.eline; /* (about: a wrapped line above counts as one) */
        if (ly < U.scroll)
            U.scroll = ly - LH;
        else if (ly + 2 * LH > U.scroll + vh)
            U.scroll = ly + 2 * LH - vh;
    } else if (y < U.scroll || h > vh) {
        U.scroll = y;
    } else if (y + h > U.scroll + vh) {
        U.scroll = y + h - vh;
    }
    clamp_scroll();
}

/* ---------------------------------------------------------------- drawing */
static void draw_header(const char *left, const char *right)
{
    const font_t *fs = A()->font_small;
    A()->fill_rect(0, TOP, W, HDR, 0xEF7D);
    A()->text(fs, 4, TOP + 1, left, C_OUT, -1);
    A()->text(fs, W - 4 - A()->text_width(fs, right), TOP + 1, right, C_DIM, -1);
}

static void draw_list(void)
{
    const font_t *fm = A()->font_medium, *fs = A()->font_small;
    char r[40];
    nbs_t *s = nb_store();
    if (s->mounted)
        A()->snprintf(r, sizeof r, "%lu KB free", (unsigned long)(nbs_free(s) >> 10));
    else
        A()->snprintf(r, sizeof r, "no flash store");
    draw_header("Notebooks", r);
    A()->fill_rect(0, VTOP, W, BOT - VTOP, BG);
    if (!U.nitems) {
        A()->text(fm, 8, VTOP + 10, "No notebooks yet.", FG, -1);
        A()->text(fm, 8, VTOP + 30, "F2 new makes one; notebooks", C_DIM, -1);
        A()->text(fm, 8, VTOP + 44, "copied to the USB drive's", C_DIM, -1);
        A()->text(fm, 8, VTOP + 58, "NOTEBOOKS folder appear here.", C_DIM, -1);
        return;
    }
    int rows = (BOT - VTOP) / 22;
    for (int k = 0; k < rows && U.ltop + k < U.nitems; k++) {
        int i = U.ltop + k, y = VTOP + k * 22;
        nb_item_t *it = &U.items[i];
        bool sel = i == U.lsel;
        if (sel)
            A()->fill_rect(0, y, W, 22, C_SEL);
        A()->text(fm, 8, y + 4, it->name, sel ? BG : FG, -1);
        char info[40];
        A()->snprintf(info, sizeof info, "%s%lu KB",
                      it->where == NB_EXAMPLE ? "example  " : it->where ? "card  " : "",
                      (unsigned long)((it->size + 1023) >> 10));
        A()->text(fs, W - 6 - A()->text_width(fs, info), y + 5, info, sel ? BG : C_DIM, -1);
        if (!sel)
            A()->fill_rect(8, y + 21, W - 16, 1, 0xEF7D);
    }
    if (U.nitems > rows) { /* a scroll bar */
        int bh = (BOT - VTOP) * rows / U.nitems, by = VTOP + (BOT - VTOP - bh) * U.ltop / (U.nitems - rows);
        A()->fill_rect(W - 3, by, 2, bh, C_DIM);
    }
}

static void draw_page(void)
{
    char r[64], l[48];
    A()->snprintf(r, sizeof r, "%s%s%s", nb_synced() ? "browser, " : "", NB.where ? "card" : "flash",
                  nb_running() ? ", running (ON stops)" : NB.dirty ? ", not saved" : "");
    A()->snprintf(l, sizeof l, "%s%s", NB.dirty ? "* " : "", NB.name); /* (a star: not saved) */
    draw_header(l, r);
    int vb = view_bot();
    A()->fill_rect(0, VTOP, W, vb - VTOP, BG);
    U.clip0 = VTOP, U.clip1 = vb;
    int y = VTOP - U.scroll;
    for (int i = 0; i < NB.ncells && y < vb; i++) {
        int h = layout_h(i);
        if (y + h > VTOP)
            cell(i, y, true);
        y += h;
    }
    if (NB.note[0] && U.scroll == 0 && y < vb - 30) {
        A()->text(A()->font_small, CX, vb - 14, NB.note, C_DIM, -1);
    }
    U.clip0 = TOP, U.clip1 = BOT;
}

static void draw_entry(const char *prompt)
{
    int lh = py_pc->line_height(), ly = BOT - lh - 2;
    A()->fill_rect(0, ly - 2, W, BOT - ly + 2, BG);
    A()->fill_rect(0, ly - 2, W, 1, C_SEP);
    A()->text(A()->font_medium, 2, ly + 3, prompt, C_SEL, -1);
    int px = 4 + 7 * (int)strlen(prompt);
    py_pc->line_draw(px, ly, (W - px) / 10, !U.picker && !U.menu_open);
}

static const char picks[][8] = {"'", "\"", "#", "%", "!", "&", "|", ";", "@", "~", "\\", "?", "$", "`", "*", "_",
                                "[", "]", "{", "}", "<", ">", "==", "**", "//", ":", "def ", "for ", " in ",
                                "range(", "if ", "else:", "return ", "print(", "import ", "True", "False", "None"};
#define NPICK ((int)(sizeof picks / sizeof picks[0]))
#define PCOLS 6
static void draw_picker(void)
{
    int rows = (NPICK + PCOLS - 1) / PCOLS, cw = W / PCOLS, y0 = BOT - py_pc->line_height() - 8 - rows * 18;
    A()->fill_rect(0, y0 - 4, W, rows * 18 + 6, 0xE71C);
    for (int i = 0; i < NPICK; i++) {
        int x = (i % PCOLS) * cw, y = y0 + (i / PCOLS) * 18;
        if (i == U.psel)
            A()->fill_rect(x + 1, y - 1, cw - 2, 17, C_SEL);
        char lab[10];
        A()->snprintf(lab, sizeof lab, "%s", picks[i] + strspn(picks[i], " "));
        A()->text(A()->font_medium, x + 4, y, lab, i == U.psel ? BG : FG, -1);
    }
}

/* the popup menu: centred, rows of 18 px */
#define MROWS 9
static void menu_geom(int *x, int *y, int *w, int *h)
{
    int rows = U.m.n < MROWS ? U.m.n : MROWS;
    *w = 200, *h = 20 + rows * 18 + 4, *x = (W - *w) / 2, *y = TOP + (BOT - TOP - *h) / 2;
}
static void draw_menu(void)
{
    int x, y, w, h;
    menu_geom(&x, &y, &w, &h);
    A()->fill_rect(x - 2, y - 2, w + 4, h + 4, C_OUT);
    A()->fill_rect(x, y, w, h, BG);
    A()->fill_rect(x, y, w, 18, 0xEF7D);
    A()->text(A()->font_small, x + 6, y + 3, U.m.title, C_OUT, -1);
    for (int k = 0; k < MROWS && U.m.top + k < U.m.n; k++) {
        int i = U.m.top + k, ry = y + 20 + k * 18;
        bool s = i == U.m.sel;
        if (s)
            A()->fill_rect(x + 2, ry, w - 4, 18, C_SEL);
        char lab[40];
        A()->snprintf(lab, sizeof lab, "%d %s", i + 1 < 10 ? i + 1 : 0, U.m.item[i]);
        A()->text(A()->font_medium, x + 6, ry + 2, lab, s ? BG : FG, -1);
    }
}

static void draw_confirm(void)
{
    int x = 20, y = 80, w = W - 40, h = 70;
    A()->fill_rect(x - 2, y - 2, w + 4, h + 4, C_OUT);
    A()->fill_rect(x, y, w, h, BG);
    text_lines(U.qmsg, x + 8, y + 8, (w - 16) / 7, FG, 0, 2, NULL, true, false, A()->font_medium);
    A()->text(A()->font_small, x + 8, y + h - 18, "ENTER or F1 yes, CLEAR or F5 no", C_DIM, -1);
}

bool nbui_draw(void)
{
    set_softkeys();
    U.clip0 = TOP, U.clip1 = BOT;
    if (U.view == V_LIST || U.view == V_PROMPT || U.view == V_CONFIRM)
        draw_list();
    else
        draw_page();
    if (U.view == V_EDIT) {
        static const char *const pre[NBK_N] = {"T", "M", "Py", "y="};
        draw_entry(pre[NB.cell[U.ecell].kind]);
    } else if (U.view == V_PROMPT) {
        draw_entry(U.prompt == P_NEW ? "Name:" : "New name:");
    }
    if (U.picker)
        draw_picker();
    if (U.view == V_CONFIRM)
        draw_confirm();
    if (U.menu_open)
        draw_menu();
    U.last_draw = A()->millis();
    return true;
}

static void redraw_now(void)
{
    nbui_draw();
    A()->flush();
}

/* while a cell runs: the page now and then */
void nbui_progress(void)
{
    if (A()->millis() - U.last_draw < 250 && !(nb_running() && U.last_draw == 0))
        return;
    if (io(NBIO_SCREEN, 0, NULL, 0) != 1)
        return;
    if (nb_running())
        NB.cell[nb_running_cell()].h = -1;
    redraw_now();
}

/* ---------------------------------------------------------------- actions */
static void toast(const char *s) { A()->toast(s); }

/* a change of the open notebook (nb_note: dirty, the page told) */
static void changed(int what, int i) { nb_note(what, i); }

/* the cell is being edited in the browser: not here */
static bool page_has(int i)
{
    if (!nb_page_edits(i))
        return false;
    toast("This cell is being edited in the browser");
    return true;
}

static void refresh_list(void)
{
    U.nitems = nb_list_all(U.items, 80); /* (the built-in examples last) */
    if (U.lsel >= U.nitems)
        U.lsel = U.nitems - 1;
    if (U.lsel < 0)
        U.lsel = 0;
    int rows = (BOT - VTOP) / 22;
    if (U.lsel < U.ltop)
        U.ltop = U.lsel;
    if (U.lsel >= U.ltop + rows)
        U.ltop = U.lsel - rows + 1;
    if (U.ltop > U.nitems - rows)
        U.ltop = U.nitems - rows > 0 ? U.nitems - rows : 0;
}

/* where this screen used to show the page of cells: the home screen shows the notebook */
static void to_home(void)
{
    U.view = V_PAGE;
    U.menu_open = false;
    py_pc->show(PY_SHOW_HOME);
}

/* the list of notebooks (the open one stays open: the home screen's) */
static void go_list(void)
{
    nb_flush();
    U.view = V_LIST;
    io(NBIO_EDITING, 0, NULL, 0);
    refresh_list();
}

static void open_doc(const char *name, int where)
{
    static char msg[160];
    nb_flush();
    nb_keep_scratch();
    if (nb_load(name, where, msg, sizeof msg)) {
        A()->message("NOT OPENED", msg);
        go_list();
        changed(NBN_DOC, -1);
        return;
    }
    nb_restart_python();
    changed(NBN_DOC, -1);
    U.sel = 0, U.scroll = 0;
    to_home();
}

static void start_edit(int i)
{
    if (i < 0 || i >= NB.ncells || page_has(i))
        return;
    nb_cell_t *c = &NB.cell[i];
    const char *s = nb_get(c, NBF_SRC);
    size_t n = strlen(s);
    if (n >= sizeof ebuf - 1) {
        toast("This cell is too long to edit here");
        return;
    }
    /* long lines: a text's are wrapped at spaces; others cannot be edited on the calculator */
    size_t o = 0, run = 0, sp = 0;
    for (size_t k = 0; k <= n && o < sizeof ebuf - 1; k++) {
        char ch = k < n ? s[k] : 0;
        if (ch == '\n' || !ch) {
            run = 0;
        } else if (++run > ELINE) {
            if (c->kind != NBK_TEXT || !sp) {
                toast("A line is too long to edit on the calculator");
                return;
            }
            ebuf[sp] = '\n';
            run = o - sp;
            sp = 0;
        } else if (ch == ' ') {
            sp = o;
        }
        ebuf[o++] = ch;
    }
    ebuf[o] = 0;
    U.view = V_EDIT;
    U.ecell = U.sel = i;
    int nl = edit_lines();
    U.eline = nl - 1;
    size_t len;
    char *p = eline_at(U.eline, &len);
    static char line[ELINE * 4 + 8];
    A()->snprintf(line, sizeof line, "%.*s", (int)len, p);
    py_pc->line_set(line);
    io(NBIO_EDITING, 1, NULL, 0);
    nb_calc_edit(i);
    show_cell(i);
}

/* the entry line back into line eline of the buffer (Python text: calculator characters translated) */
static void take_line(void)
{
    static char disp[ELINE * 4 + 8], py[ELINE * 6 + 8];
    py_pc->line_get(disp, sizeof disp);
    const char *t = disp;
    if (NB.cell[U.ecell].kind == NBK_PY) {
        py_translate(disp, py, sizeof py);
        t = py;
    }
    size_t len, tl = strlen(t);
    char *p = eline_at(U.eline, &len);
    size_t total = strlen(ebuf);
    if (total - len + tl >= sizeof ebuf - 1)
        tl = sizeof ebuf - 1 - (total - len);
    memmove(p + tl, p + len, strlen(p + len) + 1);
    memcpy(p, t, tl);
}

static void load_line(int k, int col)
{
    size_t len;
    char *p = eline_at(k, &len);
    static char line[ELINE * 4 + 8];
    A()->snprintf(line, sizeof line, "%.*s", (int)len, p);
    py_pc->line_set(line);
    if (col >= 0)
        io(NBIO_LINE_CURSOR, (uint32_t)col, NULL, 0);
    U.eline = k;
}

static void end_edit(bool keep)
{
    if (keep) {
        take_line();
        nb_cell_t *c = &NB.cell[U.ecell];
        size_t n = strlen(ebuf);
        while (n && ebuf[n - 1] == '\n' && c->kind != NBK_TEXT)
            ebuf[--n] = 0; /* (trailing empty lines) */
        if (strcmp(nb_get(c, NBF_SRC), ebuf)) {
            nb_set(c, NBF_SRC, ebuf);
            changed(NBN_CELL, U.ecell);
        }
    }
    U.view = V_PAGE;
    U.picker = false;
    io(NBIO_EDITING, 0, NULL, 0);
    nb_calc_edit(-1);
    py_pc->line_set("");
    NB.cell[U.ecell].h = -1;
    show_cell(U.sel);
    to_home();
}

/* run cell i (an edit just ended) */
static void run_cell(int i)
{
    if (i < 0 || i >= NB.ncells)
        return;
    U.sel = i;
    U.last_draw = 0;
    nb_run(i);
}

/* ---------------------------------------------------------------- menus */
static void menu_add(const char *label, int act)
{
    if (U.m.n < (int)(sizeof U.m.item / sizeof U.m.item[0]))
        U.m.item[U.m.n] = label, U.m.act[U.m.n++] = (uint8_t)act;
}
static void menu_start(const char *title)
{
    memset(&U.m, 0, sizeof U.m);
    U.m.title = title;
    U.menu_open = true;
}

static void prompt(int which, const char *initial)
{
    U.view = V_PROMPT, U.prompt = which;
    py_pc->line_set(initial);
}

static void ask(const char *q)
{
    U.view = V_CONFIRM;
    A()->snprintf(U.qmsg, sizeof U.qmsg, "%s", q);
}

static void do_action(int act)
{
    static char msg[160];
    nb_item_t *it = U.nitems ? &U.items[U.lsel] : NULL;
    U.menu_open = false;
    switch (act) {
    case A_OPEN:
        if (it)
            open_doc(it->name, it->where);
        break;
    case A_NEW: {
        char def[NBS_NAME];
        nb_default_name(def, sizeof def);
        prompt(P_NEW, def);
        break;
    }
    case A_RENAME:
        if (it && it->where == NB_EXAMPLE)
            toast("Examples cannot be renamed");
        else if (it)
            prompt(P_RENAME, it->name);
        break;
    case A_DELETE:
        if (it && it->where == NB_EXAMPLE) {
            toast("Examples cannot be deleted");
        } else if (it) {
            char q[80];
            A()->snprintf(q, sizeof q, "Delete the notebook %s%s?", it->name, it->where ? " on the card" : "");
            ask(q);
        }
        break;
    case A_COPY:
        if (it) {
            int to = it->where == 1 ? 0 : 1; /* (an example goes to the card) */
            if (nb_copy_to(it->name, it->where, to, msg, sizeof msg))
                A()->message("NOT COPIED", msg);
            else
                toast(to ? "Copied to the card" : "Copied to the flash");
            changed(NBN_LIST, -1);
            refresh_list();
        }
        break;
    case A_REVERT:
        if (it && !it->where) {
            if (nb_revert(it->name, msg, sizeof msg))
                A()->message("PREVIOUS VERSION", msg);
            else
                toast("The previous version is back");
            changed(NBN_LIST, -1);
            refresh_list();
        }
        break;
    default: break;
    }
}

static void list_more(void)
{
    menu_start("Notebook");
    nb_item_t *it = U.nitems ? &U.items[U.lsel] : NULL;
    menu_add("Open", A_OPEN);
    menu_add("New", A_NEW);
    if (it && it->where == NB_EXAMPLE) { /* (built in: it can be opened and copied) */
        menu_add("Copy to the card", A_COPY);
    } else if (it) {
        menu_add("Rename", A_RENAME);
        menu_add(it->where ? "Copy to the flash" : "Copy to the card", A_COPY);
        if (!it->where)
            menu_add("Previous version", A_REVERT);
        menu_add("Delete", A_DELETE);
    }
}
static int menu_key(int key)
{
    int c = A()->key_char(key, 0);
    if (c >= '1' && c <= '9' && c - '1' < U.m.n) {
        U.m.sel = c - '1';
        key = KEY_ENTER;
    }
    switch (key) {
    case KEY_UP: U.m.sel = (U.m.sel + U.m.n - 1) % U.m.n; break;
    case KEY_DOWN: U.m.sel = (U.m.sel + 1) % U.m.n; break;
    case KEY_ENTER:
    case KEY_F1:
        do_action(U.m.act[U.m.sel]);
        return 1;
    case KEY_CLEAR:
    case KEY_F5:
    case KEY_LEFT: U.menu_open = false; return 1;
    default: return 1;
    }
    if (U.m.sel < U.m.top)
        U.m.top = U.m.sel;
    if (U.m.sel >= U.m.top + MROWS)
        U.m.top = U.m.sel - MROWS + 1;
    return 1;
}

/* ---------------------------------------------------------------- keys */
static int list_key(int key, int mod)
{
    if (mod == 1)
        return key == KEY_F5 ? 1 : 0;
    switch (key) {
    case KEY_UP:
        if (U.lsel > 0)
            U.lsel--;
        break;
    case KEY_DOWN:
        if (U.lsel + 1 < U.nitems)
            U.lsel++;
        break;
    case KEY_ENTER:
    case KEY_F1: do_action(A_OPEN); return 1;
    case KEY_F2: do_action(A_NEW); return 1;
    case KEY_F3: do_action(A_RENAME); return 1;
    case KEY_F4:
    case KEY_DEL: do_action(A_DELETE); return 1;
    case KEY_F5: list_more(); return 1;
    case KEY_CLEAR: py_pc->show(PY_SHOW_HOME); return 1;
    default: return 1;
    }
    refresh_list();
    return 1;
}

static int edit_key(int key, int mod)
{
    nb_cell_t *c = &NB.cell[U.ecell];
    bool multi = c->kind == NBK_PY || c->kind == NBK_TEXT;
    static char line[ELINE * 4 + 8];
    if (U.picker) {
        switch (key) {
        case KEY_LEFT: U.psel = (U.psel + NPICK - 1) % NPICK; break;
        case KEY_RIGHT: U.psel = (U.psel + 1) % NPICK; break;
        case KEY_UP: U.psel = (U.psel + NPICK - PCOLS) % NPICK; break;
        case KEY_DOWN: U.psel = (U.psel + PCOLS) % NPICK; break;
        case KEY_ENTER: py_pc->line_insert(picks[U.psel]); U.picker = false; break;
        default: U.picker = false; break;
        }
        return 1;
    }
    if (mod == 1) {
        if (key == KEY_F1) { /* dedent */
            py_pc->line_get(line, sizeof line);
            size_t k = strspn(line, " ");
            py_pc->line_set(line + (k > 4 ? 4 : k));
            return 1;
        }
        if (key == KEY_F2) {
            py_pc->line_insert("calc.");
            return 1;
        }
        return 0;
    }
    switch (key) {
    case KEY_ENTER:
        if (!multi) {
            int i = U.ecell;
            end_edit(true);
            run_cell(i);
            return 1;
        } else {
            /* a new line: the text after the cursor moves to it, indented like this one */
            int cur = io(NBIO_LINE_CURSOR, (uint32_t)-1, NULL, 0);
            take_line();
            size_t len;
            char *p = eline_at(U.eline, &len);
            size_t cut = cp_bytes(p, len, cur);
            size_t ind = c->kind == NBK_PY ? strspn(p, " ") : 0;
            if (ind > cut)
                ind = cut;
            size_t l = cut;
            while (l && p[l - 1] == ' ')
                l--;
            if (c->kind == NBK_PY && l && p[l - 1] == ':')
                ind += 4;
            size_t total = strlen(ebuf);
            if (total + ind + 2 >= sizeof ebuf)
                return 1;
            /* "....abc|def" -> "....abc\n    def" */
            memmove(p + cut + 1 + ind, p + cut, strlen(p + cut) + 1);
            p[cut] = '\n';
            memset(p + cut + 1, ' ', ind);
            load_line(U.eline + 1, (int)ind);
            show_cell(U.ecell);
            return 1;
        }
    case KEY_UP:
    case KEY_DOWN: {
        int k = U.eline + (key == KEY_UP ? -1 : 1);
        if (k < 0 || k >= edit_lines())
            return 1;
        int cur = io(NBIO_LINE_CURSOR, (uint32_t)-1, NULL, 0);
        take_line();
        load_line(k, cur);
        show_cell(U.ecell);
        return 1;
    }
    case KEY_F1: {
        int i = U.ecell;
        end_edit(true);
        run_cell(i);
        return 1;
    }
    case KEY_F2: py_pc->line_insert("    "); return 1;
    case KEY_F3: U.picker = true; return 1;
    case KEY_F4: end_edit(false); return 1;
    case KEY_F5: end_edit(true); return 1;
    case KEY_DEL: /* at the start of a line: joined with the line above */
        if (multi && U.eline > 0 && io(NBIO_LINE_CURSOR, (uint32_t)-1, NULL, 0) == 0) {
            take_line();
            size_t len;
            char *p = eline_at(U.eline, &len);
            size_t plen;
            eline_at(U.eline - 1, &plen);
            char *prev = eline_at(U.eline - 1, &plen);
            int col = cp_count(prev, plen);
            memmove(p - 1, p, strlen(p) + 1); /* the '\n' goes */
            load_line(U.eline - 1, col);
            return 1;
        }
        return 0;
    case KEY_CLEAR:
        py_pc->line_get(line, sizeof line);
        if (line[0]) {
            py_pc->line_set("");
        } else if (multi && edit_lines() > 1) { /* an empty line: removed */
            size_t len;
            char *p = eline_at(U.eline, &len);
            if (p[len] == '\n')
                memmove(p, p + len + 1, strlen(p + len + 1) + 1);
            else if (p > ebuf)
                p[-1] = 0;
            int k = U.eline > 0 && U.eline >= edit_lines() ? U.eline - 1 : U.eline;
            if (k >= edit_lines())
                k = edit_lines() - 1;
            load_line(k, -1);
        } else {
            end_edit(true);
        }
        return 1;
    default: return 0;
    }
}

static int prompt_key(int key, int mod)
{
    static char line[200], msg[160];
    (void)mod;
    if (key == KEY_ENTER || key == KEY_F1) {
        py_pc->line_get(line, sizeof line);
        char *s = line;
        while (*s == ' ')
            s++;
        size_t n = strlen(s);
        while (n && s[n - 1] == ' ')
            s[--n] = 0;
        if (!nbs_name_ok(s)) {
            toast("A name of 1 to 35 letters, digits and spaces");
            return 1;
        }
        int r = 0;
        if (U.prompt == P_NEW) {
            bool dup = false;
            for (int i = 0; i < U.nitems && !dup; i++)
                dup = U.items[i].where == 0 && nbs_name_same(U.items[i].name, s);
            if (dup) {
                toast("There is a notebook of that name");
                return 1;
            }
            nb_flush();
            nb_keep_scratch();
            nb_new_doc(s, 0);
            r = nb_save(msg, sizeof msg);
            changed(NBN_DOC, -1);
            changed(NBN_LIST, -1);
            if (r) {
                A()->message("NOT CREATED", msg);
                nb_close_doc();
            } else {
                nb_restart_python();
                py_pc->line_set("");
                U.sel = 0, U.scroll = 0;
                to_home();
                return 1;
            }
        } else {
            nb_item_t *it = &U.items[U.lsel];
            r = nb_rename(it->name, it->where, s, msg, sizeof msg);
        }
        if (r && U.prompt != P_NEW)
            A()->message("NOT RENAMED", msg);
        if (!r) {
            changed(NBN_LIST, -1);
            changed(NBN_META, -1);
        }
        py_pc->line_set("");
        U.view = V_LIST;
        refresh_list();
        return 1;
    }
    if (key == KEY_F5 || (key == KEY_CLEAR && !(py_pc->line_get(line, sizeof line), line[0]))) {
        py_pc->line_set("");
        U.view = V_LIST;
        return 1;
    }
    if (key == KEY_CLEAR) { /* a name there: cleared first */
        py_pc->line_set("");
        return 1;
    }
    if (key == KEY_UP || key == KEY_DOWN)
        return 1;
    return 0;
}

static int confirm_key(int key)
{
    static char msg[160];
    bool yes = key == KEY_ENTER || key == KEY_F1;
    if (!yes && key != KEY_CLEAR && key != KEY_F5)
        return 1;
    U.view = V_LIST;
    if (!yes)
        return 1;
    if (U.nitems) {
        nb_item_t *it = &U.items[U.lsel];
        if (nb_remove(it->name, it->where, msg, sizeof msg))
            A()->message("NOT DELETED", msg);
        else
            toast("Deleted");
        changed(NBN_LIST, -1);
        refresh_list();
    }
    return 1;
}

/* ---------------------------------------------------------------- touch */
static int cell_at(int y)
{
    int dy = y - VTOP + U.scroll, acc = 0;
    for (int i = 0; i < NB.ncells; i++) {
        int h = layout_h(i);
        if (dy >= acc && dy < acc + h)
            return i;
        acc += h;
    }
    return -1;
}

static int touch_menu(int t, int x, int y)
{
    int mx, my, mw, mh;
    menu_geom(&mx, &my, &mw, &mh);
    if (t == TEV_TAP) {
        if (x < mx || x >= mx + mw || y < my || y >= my + mh) {
            U.menu_open = false;
            return 2;
        }
        int k = (y - my - 20) / 18;
        if (y >= my + 20 && U.m.top + k < U.m.n) {
            U.m.sel = U.m.top + k;
            do_action(U.m.act[U.m.sel]);
        }
        return 2;
    }
    if (t == TEV_DOWN)
        U.drag_y = y, U.drag_acc = 0;
    else if (t == TEV_DRAG && U.m.n > MROWS) {
        U.drag_acc += y - U.drag_y, U.drag_y = y;
        while (U.drag_acc >= 18 && U.m.top > 0)
            U.drag_acc -= 18, U.m.top--;
        while (U.drag_acc <= -18 && U.m.top + MROWS < U.m.n)
            U.drag_acc += 18, U.m.top++;
    }
    return 2;
}

static int touch(int t, int x, int y)
{
    if (U.menu_open)
        return touch_menu(t, x, y);
    if (U.view == V_CONFIRM) {
        if (t == TEV_TAP)
            confirm_key(y >= 80 && y < 150 && x >= 20 && x < W - 20 ? KEY_ENTER : KEY_CLEAR);
        return 2;
    }
    if (U.picker) {
        if (t != TEV_TAP)
            return 1;
        int rows = (NPICK + PCOLS - 1) / PCOLS, cw = W / PCOLS, y0 = BOT - py_pc->line_height() - 8 - rows * 18;
        int k = (y - y0) / 18 * PCOLS + x / cw;
        if (y >= y0 && k < NPICK)
            py_pc->line_insert(picks[k]);
        U.picker = false;
        return 2;
    }
    if (t == TEV_DOWN) {
        U.drag_y = U.down_y = y, U.down_x = x, U.drag_acc = 0, U.dragged = false;
        return 1;
    }
    if (U.view == V_LIST) {
        int rows = (BOT - VTOP) / 22;
        if (t == TEV_DRAG) {
            U.drag_acc += y - U.drag_y, U.drag_y = y;
            int top = U.ltop;
            while (U.drag_acc >= 22 && U.ltop > 0)
                U.drag_acc -= 22, U.ltop--;
            while (U.drag_acc <= -22 && U.ltop + rows < U.nitems)
                U.drag_acc += 22, U.ltop++;
            if (U.ltop != top) {
                U.dragged = true;
                if (U.lsel < U.ltop)
                    U.lsel = U.ltop;
                if (U.lsel >= U.ltop + rows)
                    U.lsel = U.ltop + rows - 1;
            }
            return 2;
        }
        if (t == TEV_TAP && y >= VTOP) {
            int i = U.ltop + (y - VTOP) / 22;
            if (i >= U.nitems)
                return 1;
            uint32_t now = A()->millis();
            bool again = i == U.lsel || (now - U.tap_t < 450 && U.tap_cell == i);
            U.tap_t = now, U.tap_cell = i;
            U.lsel = i;
            if (again)
                do_action(A_OPEN);
            return 2;
        }
        return 1;
    }
    if (U.view == V_EDIT) {
        if (t == TEV_DRAG) {
            U.scroll -= y - U.drag_y;
            U.drag_y = y;
            if (y - U.down_y > 6 || U.down_y - y > 6)
                U.dragged = true;
            clamp_scroll();
            return 2;
        }
        if (t == TEV_TAP && U.view == V_EDIT && y < view_bot() && y >= VTOP) { /* another cell: done editing */
            int i = cell_at(y);
            if (i != U.ecell) {
                end_edit(true);
                if (i >= 0)
                    U.sel = i;
                return 2;
            }
        }
        return 1;
    }
    return 1;
}

/* ---------------------------------------------------------------- events */
int nbui_event(int ev, int arg)
{
    switch (ev) {
    case MODX_EV_ENTER:
        U.menu_open = false, U.picker = false;
        memset(U.sk, 0xFF, sizeof U.sk); /* (labels sent again) */
        for (int i = 0; i < 10; i++)
            U.sk[i][1] = 0;
        nb_store_mount();
        if (!(arg & 1) && U.view != V_EDIT) /* opened (not back from a message): the list */
            U.view = V_LIST;
        if (U.view == V_LIST)
            refresh_list();
        if (U.view != V_LIST)
            for (int i = 0; i < NB.ncells; i++)
                NB.cell[i].h = -1;
        io(NBIO_EDITING, U.view == V_EDIT, NULL, 0);
        set_softkeys();
        return 0;
    case MODX_EV_KEY: {
        int key = arg & 0xFF, mod = arg >> 8;
        int r;
        if (U.menu_open)
            r = menu_key(key);
        else if (U.view == V_LIST)
            r = list_key(key, mod);
        else if (U.view == V_PAGE)
            to_home(), r = 1;
        else if (U.view == V_EDIT)
            r = edit_key(key, mod);
        else if (U.view == V_PROMPT)
            r = prompt_key(key, mod);
        else
            r = confirm_key(key);
        set_softkeys();
        return r;
    }
    case MODX_EV_TOUCH: {
        int r = touch((int)MODX_TOUCH_TYPE(arg), MODX_TOUCH_X(arg), MODX_TOUCH_Y(arg));
        set_softkeys();
        return r;
    }
    case MODX_EV_SCROLL:
        if (U.view == V_LIST) {
            U.lsel += MODX_SCROLL_DY(arg);
            refresh_list();
        } else {
            U.scroll += MODX_SCROLL_DY(arg) * 3 * LH;
            clamp_scroll();
        }
        return 2;
    default:
        return 0;
    }
}

static char sel_id[24], ed_id[24]; /* nbui_mark: the chosen and the edited cell */
void nbui_mark(void)
{
    sel_id[0] = ed_id[0] = 0;
    if (U.sel >= 0 && U.sel < NB.ncells)
        A()->snprintf(sel_id, sizeof sel_id, "%s", NB.cell[U.sel].id);
    if (U.view == V_EDIT && U.ecell < NB.ncells)
        A()->snprintf(ed_id, sizeof ed_id, "%s", NB.cell[U.ecell].id);
}

void nbui_commit(void)
{
    if (U.view == V_EDIT && nbui_active())
        end_edit(true);
}

/* the edited cell's lines with the entry line as it is now: put in pieces, or (put NULL) a hash */
uint32_t nbui_draft(void (*put)(const char *, size_t))
{
    if (U.view != V_EDIT)
        return 0;
    static char live[ELINE * 4 + 8];
    py_pc->line_get(live, sizeof live);
    uint32_t h = crc32_update(0, live, strlen(live)) ^ (uint32_t)U.eline;
    int n = edit_lines();
    for (int k = 0; k < n; k++) {
        size_t len;
        char *p = eline_at(k, &len);
        if (put) {
            if (k)
                put("\n", 1);
            put(k == U.eline ? live : p, k == U.eline ? strlen(live) : len);
        } else if (k != U.eline) {
            h = crc32_update(h, p, len + 1);
        }
    }
    return h | 1;
}

void nbui_list(void)
{
    go_list();
    nbui_changed();
}

/* ENTER on a text or Python cell of the home screen: cell i in the editor here (0), or -1 */
int nbui_edit(int i)
{
    if (i < 0 || i >= NB.ncells)
        return -1;
    U.menu_open = false;
    U.sel = i;
    start_edit(i);
    return U.view == V_EDIT ? 0 : -1;
}

void nbui_changed(void)
{
    for (int i = 0; i < NB.ncells && (sel_id[0] || ed_id[0]); i++) {
        if (sel_id[0] && !strcmp(NB.cell[i].id, sel_id))
            U.sel = i, sel_id[0] = 0;
        if (ed_id[0] && !strcmp(NB.cell[i].id, ed_id))
            U.ecell = i, ed_id[0] = 0;
    }
    if (ed_id[0] && U.view == V_EDIT) { /* (the edited cell is gone) */
        U.view = V_PAGE;
        io(NBIO_EDITING, 0, NULL, 0);
        nb_calc_edit(-1);
    }
    sel_id[0] = ed_id[0] = 0;
    if (U.sel >= NB.ncells)
        U.sel = NB.ncells - 1;
    if (U.sel < 0)
        U.sel = 0;
    for (int i = 0; i < NB.ncells; i++)
        NB.cell[i].h = -1;
    if (U.view == V_EDIT && U.ecell >= NB.ncells) {
        U.view = V_PAGE;
        io(NBIO_EDITING, 0, NULL, 0);
        nb_calc_edit(-1);
    }
    if (U.view == V_LIST)
        refresh_list();
    else
        clamp_scroll();
}

void nbui_init(void)
{
    memset(&U, 0, sizeof U);
    U.view = V_LIST;
}

void nbui_state(void (*kv)(const char *, const char *, void *), void *user)
{
    static const char *const views[] = {"list", "page", "edit", "prompt", "confirm"};
    char b[24];
    kv("nb_view", U.menu_open ? "menu" : views[U.view], user);
    A()->snprintf(b, sizeof b, "%d", U.view == V_LIST ? U.lsel + 1 : U.sel + 1);
    kv("nb_sel", b, user);
    int ex = 0; /* (nb_items: the notebooks of the store and the card; nb_examples: the examples listed) */
    for (int i = 0; i < U.nitems; i++)
        ex += U.items[i].where == NB_EXAMPLE;
    A()->snprintf(b, sizeof b, "%d", U.view == V_LIST ? U.nitems - ex : U.scroll);
    kv(U.view == V_LIST ? "nb_items" : "nb_scroll", b, user);
    A()->snprintf(b, sizeof b, "%d", ex);
    kv("nb_examples", b, user);
    kv("nb_menu", U.menu_open ? U.m.title : "", user);
    const nb_cell_t *c = U.sel < NB.ncells ? &NB.cell[U.sel] : NULL;
    kv("nb_kind", c ? nb_kind_name(c->kind) : "", user);
    kv("nb_src", c ? nb_get(c, NBF_SRC) : "", user);
    kv("nb_val", c ? nb_get(c, NBF_VAL) : "", user);
    kv("nb_out", c ? nb_get(c, NBF_OUT) : "", user);
    static char e[160];
    A()->snprintf(e, sizeof e, "%s%s%s", c ? nb_get(c, NBF_ENAME) : "", c && c->f[NBF_EVALUE] ? ": " : "",
                  c ? nb_get(c, NBF_EVALUE) : "");
    kv("nb_err", e, user);
    A()->snprintf(b, sizeof b, "%d", U.view == V_EDIT ? U.eline + 1 : 0);
    kv("nb_line", b, user);
    kv("nb_list_sel", U.nitems && U.lsel < U.nitems ? U.items[U.lsel].name : "", user);
}
