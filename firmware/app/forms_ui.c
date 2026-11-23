/*
 * Alternate forms browser (REWRITE menu "forms"; help page "forms-browser").
 *
 * Shows the forms the CAS command forms(e, 1) finds for the current result
 * (cas/src/forms.c: rewritings up to two deep, deduplicated, checked
 * numerically, ranked by size), one per row in textbook (2D) form, or as
 * linear text in Classic display or when a form is too wide. Rows of
 * "equally simple" forms (within 20 % of the smallest) come first and have
 * a marker in the left margin; a dashed line separates them from the other
 * forms. A form that equals the original only on part of the domain carries
 * its condition under it in small print ("if x≠1", "if x>0").
 *
 * Keys: ▲ ▼ select (the list scrolls), F1 / F2 page, ENTER (or a tap on
 * the selected row, or F3 use) takes the selected form, F4 help, F5 back or
 * CLEAR return without a change; MENU F3 (edit bar copy) or SHIFT F3 copies
 * the selected form (clip_copy_from calls forms_clip_text). Drag scrolls.
 *
 * Taking a form: on the home screen it becomes a new history entry whose
 * input is the command, e.g. "forms(Ans)", and whose result is the form (so
 * Ans is the form; an entry line that was the argument is consumed); in
 * RPN it replaces level 1 (undoable, LASTARG keeps the old level). The
 * screen that opened the browser does this through the paste callback given
 * to forms_open (home_result_forms in app/home.c, rpn_result_forms in
 * app/rpn.c).
 *
 * Evaluation: forms((arg),1) once, quietly (Ans unchanged), with a 1 s wall
 * clock time box on top of the CAS's own work budget (about 1 s on the MCU,
 * 50-100 ms on a PC); the CAS returns the forms found so far when either
 * runs out, so the list may be shorter on a slow device but is never an
 * error because of time.
 *
 * Memory: the work area from forms_mem_init (an arena carve-out in
 * app_init: 16 KB, 4 KB in the small arena) holds the history input of a
 * taken form, then the texts of the parsed rows (CAS text, display text,
 * condition) in the first half of the rest and the CAS output in the
 * second half. Nothing is saved with the session: the
 * browser is a transient view of one result.
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os") /* flash budget: UI code, size over speed */
#endif
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "editor.h"
#include "forms_ui.h"
#include "hal.h"
#include "input.h"
#include "m2d.h"
#include "m2d_box.h"
#include "theme.h"

#define FORMS_WORK (16u << 10)
#define FORMS_WORK_SMALL (4u << 10)
#define FI_MAX 16        /* rows kept */
#define FT_MAX 190       /* longest form (CAS text bytes): what a history result holds */
#define FORMS_BUDGET_MS 1000
#define POOL 300         /* boxes per row layout */
#define GUTTER 12        /* left margin with the "equally simple" marker */

typedef struct {
    uint16_t raw, disp, cond; /* offsets of the texts in the work area */
    bool eq;                  /* equally simple */
} fitem_t;

static char *work;     /* forms_mem_init */
static size_t wcap;
static size_t used;    /* bytes of row texts in the first half */
static fitem_t items[FI_MAX];
static int nitems, neq; /* rows, of which the first neq are equally simple */
static int sel, top, nvis; /* selection, first row shown, rows shown by the last draw */
static int row_y[FI_MAX], row_h[FI_MAX]; /* last draw: rows top..top+nvis-1 (touch) */
#define IN_MAX (ED_MAX * 4 + 16)
static char *in_disp; /* history input of a taken form ("forms(Ans)"): the start of the work area */
static forms_paste_fn paste_fn;

size_t forms_mem_init(void *w, bool big)
{
    in_disp = w;
    in_disp[0] = 0;
    work = in_disp + IN_MAX;
    wcap = (big ? FORMS_WORK : FORMS_WORK_SMALL) - IN_MAX;
    nitems = 0;
    return wcap + IN_MAX;
}

/* Copy s[0..n) into the row text area; offset, or -1 if it is full. */
static int store(const char *s, size_t n)
{
    if (used + n + 1 > wcap / 2)
        return -1;
    memcpy(work + used, s, n);
    work[used + n] = 0;
    int off = (int)used;
    used += n + 1;
    return off;
}

#define TXT(off) (work + (off))

/* End of the list element that starts at p: the first ',' or closing
 * bracket at depth 0 outside a string; NULL if the text ends first (the CAS
 * output was truncated). */
static const char *elem_end(const char *p)
{
    int d = 0;
    bool q = false;
    for (; *p; p++) {
        if (q) {
            q = *p != '"';
            continue;
        }
        if (*p == '"')
            q = true;
        else if (*p == '(' || *p == '[' || *p == '{')
            d++;
        else if (*p == ')' || *p == ']' || *p == '}') {
            if (!d)
                return p;
            d--;
        } else if (*p == ',' && !d)
            return p;
    }
    return NULL;
}

/* Parse the output of forms(e, 1): [[form, cost, eq, "cond"], ...] into
 * items[]. Rows whose form is longer than FT_MAX are skipped; parsing stops
 * at a truncated row. */
static void parse(const char *s)
{
    nitems = neq = 0;
    used = 0;
    if (*s++ != '[')
        return;
    while (*s == '[' && nitems < FI_MAX) {
        const char *f = s + 1, *fe = elem_end(f), *ce, *ee, *ke;
        if (!fe || *fe != ',' || !(ce = elem_end(fe + 1)) || *ce != ',' || !(ee = elem_end(ce + 1)) ||
            *ee != ',' || !(ke = elem_end(ee + 1)) || *ke != ']')
            return;
        s = ke + 1;
        if (*s == ',')
            s++;
        size_t fl = (size_t)(fe - f);
        if (fl > FT_MAX)
            continue;
        char raw[FT_MAX + 1], tmp[2 * FT_MAX + 64], disp[2 * FT_MAX + 64];
        memcpy(raw, f, fl);
        raw[fl] = 0;
        numfmt_apply(raw, tmp, sizeof tmp); /* as home results are shown */
        pretty_from_cas(tmp, disp, sizeof disp);
        const char *c = ee + 1;
        size_t cl = (size_t)(ke - c);
        if (cl >= 2 && c[0] == '"') /* strip the quotes */
            c++, cl -= 2;
        int r = store(raw, fl), d = store(disp, strlen(disp)), k = store(c, cl);
        if (r < 0 || d < 0 || k < 0)
            return;
        fitem_t *it = &items[nitems++];
        it->raw = (uint16_t)r;
        it->disp = (uint16_t)d;
        it->cond = (uint16_t)k;
        it->eq = ce[1] == '1';
        if (it->eq)
            neq++;
    }
}

static uint32_t t0;
/* CAS poll callback during the search: the wall-clock time box (the CAS
 * then returns what it has), and the watchdog. */
static int budget_cb(void *user)
{
    (void)user;
    hal_watchdog_kick();
    return hal_millis() - t0 > FORMS_BUDGET_MS;
}

/* Open the browser for the CAS expression `arg` (a result text, Ans, an RPN
 * level variable). `disp` is the history input a taken form gets
 * ("forms(Ans)"); `paste` takes the chosen form (called after returning to
 * the home screen). Shows a message instead when the CAS fails. */
void forms_open(const char *arg, const char *disp, forms_paste_fn paste)
{
    if (!work)
        return;
    char *out = work + wcap / 2;
    size_t on = wcap / 2;
    char cmd[ED_MAX * 4 + 32];
    snprintf(cmd, sizeof cmd, "forms((%s),1)", arg);
    snprintf(in_disp, IN_MAX, "%s", disp);
    paste_fn = paste;
    t0 = hal_millis();
    cas_set_poll(g.cas, budget_cb, NULL);
    cas_status st = cas_eval_quiet(g.cas, cmd, out, on);
    cas_set_poll(g.cas, app_cas_poll, NULL);
    if (st == CAS_OK || st == CAS_ERR_OUTPUT_TRUNC)
        parse(out);
    else
        nitems = 0;
    if (!nitems) {
        const char *m = st != CAS_OK ? cas_last_error(g.cas) : NULL;
        char b[96];
        snprintf(b, sizeof b, "%s.", m && *m ? m : "No form of this value could be found");
        ui_message("FORMS FAILED", b);
        return;
    }
    sel = top = 0;
    ui_switch(&scr_forms);
}

/* ------------------------------------------------------------ drawing */
/* 2D layout of row i in pool p for width w: root box, or -1 (Classic
 * display, no layout, too wide or too tall: drawn as linear text). */
static int layout(bxpool_t *p, int i, int w)
{
    if (!m2d_textbook)
        return -1;
    int r = m2d_parse(p, TXT(items[i].disp), 0);
    if (r < 0 || p->overflow)
        return -1;
    bx_measure(p, r);
    if (p->overflow || p->b[r].w > w || p->b[r].asc + p->b[r].desc > BODY_H / 2)
        return -1;
    return r;
}

/* Lines of linear text of row i at width w (at most 3, the rest clipped). */
static int text_lines(int i, int w)
{
    int cols = w / UI_CW, len = gfx_text_width(UI_FONT, TXT(items[i].disp)) / UI_CW;
    int n = cols > 0 ? (len + cols - 1) / cols : 1;
    return n < 1 ? 1 : n > 3 ? 3 : n;
}

/* Draw row i at y (or only measure it: draw false); returns its height. */
static int draw_row(int i, int y, bool draw)
{
    bx_t store_[POOL];
    bxpool_t p;
    bx_init(&p, store_, POOL);
    int w = LCD_WIDTH - GUTTER - 4;
    int r = layout(&p, i, w);
    const bx_t *b = r >= 0 ? &store_[r] : NULL;
    int h = b ? b->asc + b->desc + 4 : text_lines(i, w) * UI_CH + 4;
    if (h < UI_CH + 4)
        h = UI_CH + 4;
    int hc = *TXT(items[i].cond) ? font_small.h + 1 : 0;
    if (!draw)
        return h + hc;
    bool s = i == sel;
    uint16_t fg = s ? TH_SEL_FG : TH_FG, bg = s ? TH_SEL_BG : TH_BG;
    gfx_fill_rect(0, y, LCD_WIDTH, h + hc, bg);
    if (items[i].eq) /* marker: equally simple */
        gfx_fill_rect(3, y + h / 2 - 3, 6, 6, s ? TH_SEL_FG : TH_LINK);
    if (b) {
        bx_place(&p, r, GUTTER, y + (h - (b->asc + b->desc)) / 2 + b->asc);
        gfx_textlog_enable(false);
        bx_draw(&p, r, fg);
        gfx_textlog_enable(true);
        gfx_textlog_note(GUTTER, y, b->w, h, TXT(items[i].disp));
    } else {
        /* linear text, wrapped at character boundaries (UTF-8 aware) */
        const char *t = TXT(items[i].disp);
        int cols = w / UI_CW, n = text_lines(i, w);
        for (int ln = 0; ln < n && *t; ln++) {
            char line[256];
            size_t o = 0;
            for (int c = 0; c < cols && *t && o + 4 < sizeof line; c++) {
                do
                    line[o++] = *t++;
                while ((*t & 0xC0) == 0x80);
            }
            line[o] = 0;
            gfx_text(UI_FONT, GUTTER, y + 2 + ln * UI_CH, line, fg, bg);
        }
    }
    if (hc) {
        char c[96];
        snprintf(c, sizeof c, "if %s", TXT(items[i].cond));
        gfx_text(&font_small, GUTTER, y + h, c, s ? TH_SEL_FG : TH_DIM, bg);
    }
    return h + hc;
}

/* Draw the rows from `top` down as far as they fit; a dashed line separates
 * the equally simple forms from the others. Keeps the selection visible
 * (moves top and draws again when the selected row would be cut off). */
static void forms_draw(void)
{
    for (int pass = 0; pass <= nitems; pass++) {
        gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, TH_BG);
        int y = BODY_Y;
        nvis = 0;
        for (int i = top; i < nitems; i++) {
            int h = draw_row(i, y, false);
            if (y + h > UI_BOTTOM && nvis)
                break;
            row_y[nvis] = y;
            row_h[nvis] = draw_row(i, y, true);
            if (i + 1 == neq && neq < nitems)
                for (int x = 0; x < LCD_WIDTH; x += 6)
                    gfx_hline(x, y + h - 1, 3, TH_DIM);
            y += h;
            nvis++;
        }
        if (sel < top + nvis)
            break;
        top++; /* bring the selection in, draw again */
    }
}

/* ------------------------------------------------------------ actions */
/* Take the selected form: back to the screen below (state kept), then the
 * paste callback makes it a result there. */
static void take(void)
{
    char v[FT_MAX + 1];
    snprintf(v, sizeof v, "%s", TXT(items[sel].raw));
    ui_returning = true;
    ui_switch(ui_home());
    ui_returning = false;
    if (paste_fn)
        paste_fn(in_disp, v);
    ui_switch(ui_home()); /* redraw with the new result */
}

static void back(void)
{
    ui_returning = true;
    ui_switch(ui_home());
    ui_returning = false;
}

bool forms_clip_text(char *out, size_t n)
{
    if (!nitems)
        return false;
    snprintf(out, n, "%s", TXT(items[sel].disp));
    return true;
}

static void sk_copy(void)
{
    char b[2 * FT_MAX + 64];
    if (forms_clip_text(b, sizeof b)) {
        clip_put(b);
        ui_toast("copied");
    }
}
static void sk_help(void) { help_open("forms-browser"); }

static void forms_key(int key, int mod)
{
    (void)mod;
    switch (key) {
    case KEY_UP: sel = sel > 0 ? sel - 1 : nitems - 1; break;
    case KEY_DOWN: sel = sel + 1 < nitems ? sel + 1 : 0; break;
    case KEY_F1: /* ▲ page */
        sel = sel - (nvis > 1 ? nvis - 1 : 1);
        if (sel < 0)
            sel = 0;
        break;
    case KEY_F2: /* ▼ page */
        sel = sel + (nvis > 1 ? nvis - 1 : 1);
        if (sel >= nitems)
            sel = nitems - 1;
        break;
    case KEY_ENTER: take(); return;
    case KEY_CLEAR: back(); return;
    default: return;
    }
    if (sel < top)
        top = sel;
    forms_draw();
}

/* Touch: drag scrolls (whole rows, the selection pulled into view); a tap
 * selects a row, a tap on the selected row takes it. */
static void forms_touch(const touch_event_t *ev)
{
    if (ev->type == TEV_DRAG) {
        if (ui_drag_list(ev, 2 * UI_CH, &top, nitems, nvis, &sel))
            forms_draw();
        return;
    }
    if (ev->type != TEV_TAP)
        return;
    for (int k = 0; k < nvis; k++)
        if (ev->y0 >= row_y[k] && ev->y0 < row_y[k] + row_h[k]) {
            if (top + k == sel) {
                take();
                return;
            }
            sel = top + k;
            forms_draw();
            return;
        }
}

static void forms_enter(void)
{
    if (!ui_returning)
        sel = top = 0;
}

/* console `state`: forms_n, forms_eq (equally simple rows), forms_sel, the
 * selected form (forms_item: CAS text) and its condition (forms_cond) */
void forms_state(void (*kv)(const char *, const char *, void *), void *user)
{
    char b[16];
    snprintf(b, sizeof b, "%d", nitems);
    kv("forms_n", b, user);
    snprintf(b, sizeof b, "%d", neq);
    kv("forms_eq", b, user);
    snprintf(b, sizeof b, "%d", sel);
    kv("forms_sel", b, user);
    kv("forms_item", nitems ? TXT(items[sel].raw) : "", user);
    kv("forms_cond", nitems ? TXT(items[sel].cond) : "", user);
}

static const softkeys_t forms_soft = {
    {{"▲ page"}, {"▼ page"}, {"use", take}, {"help", sk_help}, {"back", back}},
    {{NULL}, {NULL}, {"copy", sk_copy}},
};

const screen_t scr_forms = {"forms", "FORMS", forms_enter, forms_draw, forms_key, NULL, NULL, forms_touch,
                            "forms-browser", &forms_soft};
