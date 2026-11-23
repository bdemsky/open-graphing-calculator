/*
 * Generic input form ("Z-Test" style screens) and the result screen of
 * the statistics app. A form is a list of fields: numbers (any expression,
 * evaluated when the action runs), list names, choices (LEFT/RIGHT) and
 * actions ("Calculate", "Draw"). Field values are remembered by label across
 * visits of the same form (the last mu0, sigma, ... are kept).
 *
 * Used by every CALC / TESTS / DISTR form (app/stat_menu.c), the stat plot
 * settings (stat_plot.c) and CSV export (stat_csv.c). A client declares a
 * static sform_t (stat.h) and calls sf_open(); when an action field is
 * chosen, its `action(act)` callback reads the values with sf_num / sf_list /
 * sf_choice / sf_text and either shows results (sr_show), another screen, or
 * an error line on the form (sf_error).
 *
 * Field values are kept as text in val[] (choices as one digit '0'+index),
 * so a number field may hold any expression ("2π", "mean(L1)"); it is only
 * evaluated when the action asks for it. A field can be conditional
 * (sfield_t.cond): it is shown only while an earlier choice field has a given
 * value (the tests' "Inpt: Data|Stats" choice shows either the list fields
 * or the summary-statistics fields).
 *
 * Screens:
 *   scr_stat_form    the form: UP/DOWN move between visible fields, LEFT/RIGHT
 *                    change a choice, typing edits a field (the first key
 *                    replaces the old value), ENTER moves on or runs an
 *                    action, F5 runs the form's first action, CLEAR clears
 *                    the field or leaves; tap selects / runs.
 *   scr_stat_result  the result lines (label=value, then y=RegEQ); ENTER draws
 *                    the shaded distribution when the result has one.
 *
 * State lives in this file's statics (one form open at a time); the
 * remembered values (mem[]) are RAM only, not saved with the session.
 *
 * Geometry: title strip at BODY_Y (18 px), rows of RH = 18 px from TOP = 38,
 * ROWS = 9 visible rows, error/hint line at UI_BOTTOM - 15.
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os") /* flash budget: size over speed (not time-critical) */
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sections.h"
#include "stat.h"

#define FF (&font_medium)
#define RH 18
#define TOP (BODY_Y + 20)
#define ROWS ((UI_BOTTOM - TOP - 16) / RH)
#define VAL_N 32

static const sform_t *F;          /* the open form */
static int nf;                    /* its fields in use: F->n, at most SF_MAXF (val[] and the drawing hold that many) */
static char val[SF_MAXF][VAL_N];  /* field values as text (choices: '0'+index) */
static int sel, top;              /* selected field index; first visible row */
static editor_t ed;               /* editor of the selected field */
static bool fresh;                /* field just loaded: the first typed key replaces it */
static char err[80];              /* error line ("" = none) */

/* remembered values per form and field (labels are static strings, so they
 * are compared by pointer: two forms sharing a label string still have
 * separate entries because the form pointer is part of the key) */
#define MEM_N 48
static struct {
    const sform_t *form;
    const char *label;
    char v[24];
} mem[MEM_N];
static int nmem;

/* Store the value of field `label` of the open form (truncated to 23 chars).
 * When the table is full the last slot is overwritten. */
static void remember(const char *label, const char *v)
{
    int i;
    for (i = 0; i < nmem; i++)
        if (mem[i].form == F && mem[i].label == label)
            break;
    if (i == nmem) {
        if (nmem == MEM_N)
            i = nmem - 1; /* full: reuse the last slot */
        else
            nmem++;
        mem[i].form = F, mem[i].label = label;
    }
    snprintf(mem[i].v, sizeof mem[i].v, "%s", v);
}

static const char *recall(const char *label)
{
    for (int i = 0; i < nmem; i++)
        if (mem[i].form == F && mem[i].label == label)
            return mem[i].v;
    return NULL;
}

/* Field i is shown: unconditional, or its controlling choice field (cond-1)
 * currently has the value cond_val. */
static bool visible(int i)
{
    const sfield_t *f = &F->f[i];
    if (!f->cond)
        return true;
    int c = val[f->cond - 1][0] - '0';
    if (f->cond_val & SF_ANYOF) /* one of several values: a bit mask of the choice indices */
        return c >= 0 && c < 7 && (f->cond_val >> c & 1);
    return c == f->cond_val;
}

/* choice-like fields: LEFT/RIGHT change them, val[] holds '0'+index */
static bool is_choice(const sfield_t *f) { return f->type == SF_CHOICE || f->type == SF_COLOR; }

static bool editable(int i) { return F->f[i].type == SF_NUM || F->f[i].type == SF_LIST || F->f[i].type == SF_TEXT; }

/* number of options in "a|b|c" */
static int nopts(const char *o)
{
    int n = 1;
    for (; *o; o++)
        n += *o == '|';
    return n;
}

/* option k of "a|b|c" into b */
static void opt(const char *o, int k, char *b, size_t n)
{
    while (k > 0 && *o)
        if (*o++ == '|')
            k--;
    size_t l = 0;
    while (o[l] && o[l] != '|')
        l++;
    if (l >= n)
        l = n - 1;
    memcpy(b, o, l);
    b[l] = 0;
}

/* Put the selected field's text into the editor (text fields only). */
static void load(void)
{
    if (editable(sel)) {
        ed_set_text(&ed, val[sel]);
        fresh = true;
    }
}

/* Copy the editor back into the selected field, remember it, and notify the
 * client (sform_t.changed, e.g. the plot settings apply immediately). Called
 * before the selection moves and before any action runs. */
static void commit(void)
{
    if (editable(sel)) {
        ed_display(&ed, val[sel], sizeof val[sel]);
        remember(F->f[sel].label, val[sel]);
    }
    if (F->changed)
        F->changed();
}

static char form_help[24]; /* help topic of the open form (scr_stat_form.help points here) */

/* Open form `def` (must stay valid while open; fields after the first
 * SF_MAXF are ignored). Each field starts with its remembered value, else its
 * default (`opts` for text fields, option 0 for choices). The first field is
 * selected. The caller may then override values with sf_set_text() (the plot
 * settings do). */
void sf_open(const sform_t *def)
{
    F = def;
    nf = def->n < SF_MAXF ? def->n : SF_MAXF;
    snprintf(form_help, sizeof form_help, "%s", def->help ? def->help : "stat");
    for (int i = 0; i < nf; i++) {
        const sfield_t *f = &def->f[i];
        const char *r = f->type == SF_ACTION ? NULL : recall(f->label);
        if (is_choice(f)) {
            val[i][0] = (char)('0' + (r && r[0] - '0' < nopts(f->opts) ? r[0] - '0' : 0));
            val[i][1] = 0;
        } else {
            snprintf(val[i], VAL_N, "%s", r ? r : f->opts ? f->opts : "");
        }
    }
    sel = top = 0;
    err[0] = 0;
    load();
    ui_switch(&scr_stat_form);
}

const sform_t *sf_current(void) { return F; }
int sf_selected(void) { return sel; }

const char *sf_text(int i) { return val[i]; }
int sf_choice(int i) { return val[i][0] - '0'; }

void sf_set_text(int i, const char *t)
{
    snprintf(val[i], VAL_N, "%s", t);
    if (i == sel)
        load();
}

void sf_error(const char *msg) { snprintf(err, sizeof err, "ERR: %.70s", msg); }

/* Evaluate number field i (display text -> CAS syntax -> eval_number, which
 * does not touch `ans`). On an empty field or an evaluation error returns NaN
 * and clears *ok; only the first error of a sequence is shown (the caller
 * sets its flag to true, reads every field, then checks the flag once). */
double sf_num(int i, bool *ok)
{
    editor_t e;
    ed_set_text(&e, val[i]);
    char cas[ED_MAX * 4], m[64];
    ed_cas(&e, cas, sizeof cas);
    double v;
    if (!cas[0] || eval_number(cas, &v, m, sizeof m)) {
        if (*ok) {
            char b[80];
            snprintf(b, sizeof b, "%s %s", F->f[i].label, cas[0] ? m : "missing, type a value");
            sf_error(b);
        }
        *ok = false;
        return DNAN;
    }
    return v;
}

/* The list named in field i: *v points into the list storage (valid until
 * the lists change), *n its length. optional (frequency fields): "" or "1"
 * means no list, *v = NULL and *n = -1. false (error shown) if the list does
 * not exist or is empty. */
bool sf_list(int i, const double **v, int *n, bool optional)
{
    const char *t = val[i];
    if (optional && (!t[0] || !strcmp(t, "1"))) {
        *v = NULL;
        *n = -1;
        return true;
    }
    const slist_t *l = sl_find(t);
    if (!l || l->len == 0) {
        char b[64];
        if (!t[0])
            snprintf(b, sizeof b, "%s missing, type a list name", F->f[i].label);
        else if (l)
            snprintf(b, sizeof b, "%s %s is empty, enter data first", F->f[i].label, t);
        else
            snprintf(b, sizeof b, "%s no list named '%s'", F->f[i].label, t);
        sf_error(b);
        return false;
    }
    *v = l->v;
    *n = l->len;
    return true;
}

/* ------------------------------------------------------------ form screen */
/* Draw the title strip and the visible fields. vis[] maps screen rows to the
 * indices of the currently visible fields; `top` scrolls so the selected one
 * is on screen. Rows: an action is a button-like label; other fields are
 * "label" (at least 70 px wide) followed by the options of a choice (the
 * chosen one highlighted), the live editor for the selected text field, or
 * the stored text. */
static void form_draw(void)
{
    ui_cursor_hide();
    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, C_WHITE);
    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, 18, C_LTGRAY);
    gfx_text(FF, 4, BODY_Y + 2, F->title, C_BLACK, GFX_TRANSPARENT);
    if (sel < top)
        top = sel;
    int vis[SF_MAXF], nv = 0, selrow = 0;
    for (int i = 0; i < nf; i++)
        if (visible(i)) {
            if (i == sel)
                selrow = nv;
            vis[nv++] = i;
        }
    if (selrow < top)
        top = selrow;
    if (selrow >= top + ROWS)
        top = selrow - ROWS + 1;
    for (int r = 0; r < ROWS && top + r < nv; r++) {
        int i = vis[top + r], y = TOP + r * RH;
        const sfield_t *f = &F->f[i];
        bool focus = i == sel;
        if (f->type == SF_ACTION) {
            int w = gfx_text_width(FF, f->label) + 8;
            if (focus)
                gfx_fill_rect(4, y - 1, w, RH - 2, C_SEL_BG);
            gfx_text(FF, 8, y, f->label, focus ? C_WHITE : C_BLACK, GFX_TRANSPARENT);
            continue;
        }
        int lw = gfx_text(FF, 6, y, f->label, focus ? C_SEL_BG : C_DKGRAY, GFX_TRANSPARENT) + 12;
        if (lw < 70)
            lw = 70;
        if (f->type == SF_COLOR) {
            /* colour swatches: the option number in white on its colour, the chosen one framed */
            int x = lw, k = nopts(f->opts);
            for (int j = 0; j < k && j < NCOLORS; j++) {
                char b[4];
                opt(f->opts, j, b, sizeof b);
                bool on = j == sf_choice(i);
                if (on)
                    gfx_fill_rect(x - 3, y - 3, 22, RH, focus ? C_SEL_BG : C_DKGRAY);
                gfx_fill_rect(x - 1, y - 1, 18, RH - 4, func_colors[j]);
                gfx_text(FF, x + 8 - gfx_text_width(FF, b) / 2, y, b, C_WHITE, GFX_TRANSPARENT);
                x += 26;
            }
        } else if (f->type == SF_CHOICE) {
            int x = lw, k = nopts(f->opts);
            for (int j = 0; j < k; j++) {
                char b[24];
                opt(f->opts, j, b, sizeof b);
                int w = gfx_text_width(FF, b) + 6;
                bool on = j == sf_choice(i);
                if (on)
                    gfx_fill_rect(x - 3, y - 1, w, RH - 2, focus ? C_SEL_BG : C_GRAY);
                gfx_text(FF, x, y, b, on ? C_WHITE : C_BLACK, GFX_TRANSPARENT);
                x += w + 6;
            }
        } else if (focus) {
            ui_draw_editor_line(&ed, lw, y - 3, (LCD_WIDTH - lw - 4) / CW, C_BLACK, C_WHITE, true);
        } else if (!val[i][0] && f->type != SF_TEXT) {
            gfx_text(FF, lw, y, "(enter a value)", C_GRAY, GFX_TRANSPARENT);
        } else {
            gfx_text(FF, lw, y, val[i], C_BLACK, GFX_TRANSPARENT);
        }
    }
    if (err[0]) {
        gfx_text(FF, 4, UI_BOTTOM - 15, err, C_RED, GFX_TRANSPARENT);
    } else {
        /* what the selected field takes: the form's own hint, else one by field type */
        const sfield_t *f = &F->f[sel];
        const char *h = F->hint ? F->hint() : NULL;
        if (!h && is_choice(f))
            h = "◀ ▶ change the choice";
        else if (!h && f->type == SF_LIST)
            h = !strncmp(f->label, "Freq", 4) ? "frequency list, 1 = none" : "a list name, or VARS > Lists";
        if (h)
            gfx_text(FF, 4, UI_BOTTOM - 15, h, C_GRAY, GFX_TRANSPARENT);
    }
}

/* Next visible field from `from` in direction dir (+1/-1), wrapping around. */
static int step(int from, int dir)
{
    for (int k = 1; k <= nf; k++) {
        int i = ((from + dir * k) % nf + nf) % nf;
        if (visible(i))
            return i;
    }
    return from;
}

/* Form keys (see the file header). An action may switch screens (results,
 * graph, message): then this screen must not redraw over it, hence the
 * ui_current() checks. */
static void form_key(int key, int mod)
{
    const sfield_t *f = &F->f[sel];
    switch (key) {
    case KEY_UP:
    case KEY_DOWN:
        commit();
        sel = step(sel, key == KEY_UP ? -1 : 1);
        load();
        break;
    case KEY_F5: /* soft keys: the form's first action (Calculate), F4 its second (Draw, Graph) */
    case KEY_F4: {
        commit();
        int want = key == KEY_F5 ? 0 : 1;
        for (int i = 0; i < nf; i++)
            if (F->f[i].type == SF_ACTION && want-- == 0) {
                err[0] = 0;
                F->action(F->f[i].act);
                if (ui_current() != &scr_stat_form)
                    return;
                break;
            }
        break;
    }
    case KEY_ENTER:
        commit();
        if (f->type == SF_ACTION) {
            err[0] = 0;
            F->action(f->act);
            if (ui_current() != &scr_stat_form)
                return;
        } else {
            sel = step(sel, 1);
            load();
        }
        break;
    case KEY_LEFT:
    case KEY_RIGHT:
        if (is_choice(f)) {
            int k = nopts(f->opts), c = (sf_choice(sel) + (key == KEY_LEFT ? k - 1 : 1)) % k;
            val[sel][0] = (char)('0' + c);
            remember(f->label, val[sel]);
            if (F->changed)
                F->changed();
        } else if (editable(sel)) {
            fresh = false;
            ui_editor_key(&ed, key, mod);
        }
        break;
    case KEY_CLEAR:
        if (editable(sel) && !ed_empty(&ed)) {
            ed_clear(&ed);
            fresh = false;
        } else {
            commit();
            ui_switch(ui_home());
            return;
        }
        break;
    default:
        if (!editable(sel))
            return;
        if (key == KEY_DEL) {
            fresh = false;
            ui_editor_key(&ed, key, mod);
            break;
        }
        {
            char t[16];
            if (!ui_key_text(key, mod, g.alpha_snapshot_for_key, t, sizeof t))
                return;
            if (fresh) {
                ed_clear(&ed);
                fresh = false;
            }
            ed_insert_text(&ed, t, NULL);
        }
    }
    form_draw();
}

/* Tap on a row: select that field; tapping an action runs it. Drag scrolls
 * the visible fields; a selection pushed off screen moves to the nearest
 * visible field (after storing the edited one). */
static void form_touch(const touch_event_t *ev)
{
    if (ev->type == TEV_DRAG) {
        int vis[SF_MAXF], nv = 0, selrow = 0;
        for (int i = 0; i < nf; i++)
            if (visible(i)) {
                if (i == sel)
                    selrow = nv;
                vis[nv++] = i;
            }
        if (!ui_drag_scroll(ev, UI_DRAG_Y, RH, &top, nv - ROWS))
            return;
        int r = ui_drag_focus(selrow, top, ROWS);
        if (r != selrow && r < nv) {
            commit();
            sel = vis[r];
            load();
        }
        form_draw();
        return;
    }
    if (ev->type != TEV_TAP || ev->y < TOP)
        return;
    int row = (ev->y - TOP) / RH + top, nv = 0;
    for (int i = 0; i < nf; i++) {
        if (!visible(i))
            continue;
        if (nv++ == row) {
            commit();
            sel = i;
            load();
            if (F->f[i].type == SF_ACTION) {
                F->action(F->f[i].act);
                if (ui_current() != &scr_stat_form)
                    return;
            }
            break;
        }
    }
    form_draw();
}

static void form_enter(void) {}

/* Menus (VARS > Lists, MATH, ...) insert into the selected text field, after
 * its current text (fresh is cleared so the insertion does not replace it). */
static editor_t *form_editor(void)
{
    fresh = false;
    return editable(sel) ? &ed : NULL;
}

/* Soft keys: F5 = the form's first action (Calculate, Draw, Sort, Clear,
 * Export, ZoomStat), F4 = its second one (Draw on the tests, Graph on the
 * plot settings). Labels come from the action fields' labels by prefix
 * ("Calculate" -> calc); an unknown label leaves the key blank. The keys
 * have no fn, so they reach form_key() as KEY_F4 / KEY_F5. */
static const softkeys_t *form_softkeys(void)
{
    static const char *const names[] = {"Calc", "Draw", "Sort", "Clear", "Export", "ZoomStat", "Graph"};
    static const char *const keys[] = {"calc", "draw", "sort", "clear", "export", "zoomstat", "graph"};
    static softkeys_t sk;
    memset(&sk, 0, sizeof sk);
    int slot = 4;
    for (int i = 0; F && i < nf && slot >= 3; i++)
        if (F->f[i].type == SF_ACTION) {
            for (int k = 0; k < 7; k++)
                if (!strncmp(F->f[i].label, names[k], strlen(names[k])))
                    sk.key[slot].label = keys[k];
            slot--;
        }
    return &sk;
}
const screen_t scr_stat_form = {"statform", "STAT", form_enter, form_draw, form_key, NULL, form_editor, form_touch,
                                form_help, NULL, form_softkeys};

/* ------------------------------------------------------------ results */
void stat_fmt(double v, char *b, size_t n)
{
    if (isnan(v))
        snprintf(b, n, "-");
    else if (v == floor(v) && fabs(v) < 1e12 && g.fmt == FMT_NORMAL && g.fix < 0)
        snprintf(b, n, "%.0f", v); /* counts: "10", not "10.0" */
    else
        numfmt_double(v, b, n);
}

static sres_t R;                 /* copy of the result shown */
static bool R_draw, R_from_form; /* draw offered; CLEAR returns to the form (else home) */
static int rtop;                 /* first visible line */
static char R_note[48];          /* right of the title: "Ha: mu>5", "RegEQ in Y1" ("" = none) */
static char next_note[48];       /* set by sr_note() for the next sr_show() */
static void (*R_plot)(void), (*next_plot)(void); /* "plot" soft key action (regressions) */

void sr_note(const char *note) { snprintf(next_note, sizeof next_note, "%s", note ? note : ""); }
void sr_set_plot(void (*plot)(void)) { next_plot = plot; }
void sr_update_note(const char *note) { snprintf(R_note, sizeof R_note, "%s", note); }

/* Show result `r` (copied) on the result screen and make it the "last
 * result" of VARS > Statistics. drawable: F1 (draw) / ENTER shade the test
 * statistic's p-value region (only if the result names a distribution).
 * The note and plot action set just before (sr_note, sr_set_plot) are
 * taken over and reset, so a later sr_show without them shows none.
 * Remembers whether it was opened from a form so CLEAR can go back there
 * with the form's values intact (ui_returning). */
void sr_show(const sres_t *r, bool drawable)
{
    R = *r;
    R_draw = drawable && r->dist != DIST_NONE;
    rtop = 0;
    R_from_form = ui_current() == &scr_stat_form;
    snprintf(R_note, sizeof R_note, "%s", next_note);
    R_plot = next_plot;
    next_note[0] = 0;
    next_plot = NULL;
    stat_last = *r;
    ui_switch(&scr_stat_result);
}

#define RROWS ((UI_BOTTOM - TOP - 16) / RH)

/* Plain-language meaning of the result labels of stat_math.c, shown dimmed
 * at the right of each line so "Sx" or "sigmax" need no manual. The labels
 * themselves stay short: they are also the names in VARS > Statistics. */
static const char *const descr[][2] = {
    {"xbar", "mean"}, {"ybar", "mean of y"}, {"Σx", "sum"}, {"Σx²", "sum of squares"}, {"Σy", "sum of y"},
    {"Σy²", "sum of y²"}, {"Σxy", "sum of x·y"}, {"Sx", "sample std. dev."}, {"Sy", "sample std. dev. of y"},
    {"sigmax", "population std. dev."}, {"sigmay", "pop. std. dev. of y"}, {"n", "number of values"},
    {"minX", "smallest"}, {"maxX", "largest"}, {"minY", "smallest y"}, {"maxY", "largest y"},
    {"Q1", "1st quartile"}, {"Med", "median"}, {"Q3", "3rd quartile"}, {"mode", "most frequent (- none)"},
    {"r²", "fit quality (0..1)"}, {"R²", "fit quality (0..1)"}, {"r", "correlation (-1..1)"},
    {"s", "std. error of the fit"}, {"z", "test statistic"}, {"t", "test statistic"}, {"chi²", "test statistic"},
    {"F", "test statistic"}, {"p", "p-value"}, {"df", "degrees of freedom"}, {"xbar1", "mean, sample 1"},
    {"xbar2", "mean, sample 2"}, {"Sx1", "std. dev., sample 1"}, {"Sx2", "std. dev., sample 2"},
    {"Sxp", "pooled std. dev."}, {"n1", "size of sample 1"}, {"n2", "size of sample 2"},
    {"p^", "sample proportion"}, {"p^1", "proportion, sample 1"}, {"p^2", "proportion, sample 2"},
    {"lower", "interval low end"}, {"upper", "interval high end"}, {"ME", "margin of error"},
};

static const char *describe(const char *lab)
{
    for (size_t i = 0; i < sizeof descr / sizeof descr[0]; i++)
        if (!strcmp(descr[i][0], lab))
            return descr[i][1];
    return NULL;
}

/* Result lines "label=value" (stat_fmt) with the label's meaning dimmed at
 * the right (left out when the value is too long for both), then, for
 * regressions, the line "y=RegEQ". The equation's CAS text has
 * full-precision coefficients (st_num_text); for display each number is
 * shortened with fmt_num(10 chars) and the text prettified
 * (pretty_from_cas). The note (hypothesis, RegEQ destination) is right of
 * the title. Arrows at the right edge show that more lines are above /
 * below; the bottom line says where the values can be used. */
static void res_draw(void)
{
    ui_cursor_hide();
    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, C_WHITE);
    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, 18, C_LTGRAY);
    int tw = gfx_text(FF, 4, BODY_Y + 2, R.title, C_BLACK, GFX_TRANSPARENT);
    if (R_note[0]) {
        int nw = gfx_text_width(FF, R_note);
        if (4 + tw + 12 + nw <= LCD_WIDTH - 4)
            gfx_text(FF, LCD_WIDTH - 4 - nw, BODY_Y + 2, R_note, C_BLUE, GFX_TRANSPARENT);
    }
    int nl = R.n + (R.eq[0] ? 1 : 0);
    for (int r = 0; r < RROWS && rtop + r < nl; r++) {
        int i = rtop + r, y = TOP + r * RH;
        char b[200], num[40];
        if (i == R.n) {
            /* the equation with short numbers */
            char sh[200];
            size_t o = 0;
            for (const char *q = R.eq; *q && o < sizeof sh - 16;) {
                if ((*q >= '0' && *q <= '9') || *q == '.') {
                    char *end;
                    double v = strtod(q, &end);
                    fmt_num(v, 10, sh + o, sizeof sh - o);
                    o += strlen(sh + o);
                    q = end;
                } else {
                    sh[o++] = *q++;
                }
            }
            sh[o] = 0;
            pretty_from_cas(sh, b, sizeof b);
            char t[220];
            snprintf(t, sizeof t, "y=%s", b);
            gfx_text(FF, 6, y, t, C_BLUE, GFX_TRANSPARENT);
            continue;
        }
        stat_fmt(R.val[i], num, sizeof num);
        snprintf(b, sizeof b, "%s=%s", R.lab[i], num);
        int w = gfx_text(FF, 6, y, b, C_BLACK, GFX_TRANSPARENT);
        const char *d = describe(R.lab[i]);
        if (d) {
            int dw = gfx_text_width(FF, d);
            if (6 + w + 10 + dw <= LCD_WIDTH - 14)
                gfx_text(FF, LCD_WIDTH - 14 - dw, y, d, C_GRAY, GFX_TRANSPARENT);
        }
    }
    if (rtop > 0)
        gfx_text(FF, LCD_WIDTH - 10, TOP, "↑", C_GRAY, GFX_TRANSPARENT);
    if (rtop + RROWS < nl)
        gfx_text(FF, LCD_WIDTH - 10, UI_BOTTOM - 30, "↓", C_GRAY, GFX_TRANSPARENT);
    gfx_text(FF, 4, UI_BOTTOM - 15, "values: VARS > Statistics", C_GRAY, GFX_TRANSPARENT);
}

/* UP/DOWN scroll; F1 (draw) or ENTER draws when the result has a
 * distribution; F2 (plot) runs the plot action (regressions: scatter plot
 * and equation on the graph); ENTER otherwise, CLEAR and F5 (back) go back
 * to the form (or home when the result was not opened from one). */
static void res_key(int key, int mod)
{
    int nl = R.n + (R.eq[0] ? 1 : 0);
    if (key == KEY_UP && rtop > 0)
        rtop--;
    else if (key == KEY_DOWN && rtop + RROWS < nl)
        rtop++;
    else if ((key == KEY_ENTER || key == KEY_F1) && R_draw) {
        sd_draw_result(&R);
        return;
    } else if (key == KEY_F2 && R_plot) {
        R_plot();
        return;
    } else if (key == KEY_CLEAR || key == KEY_ENTER || key == KEY_F5) {
        if (R_from_form) {
            ui_returning = true;
            ui_switch(&scr_stat_form);
            ui_returning = false;
        } else {
            ui_switch(ui_home());
        }
        return;
    } else {
        return;
    }
    res_draw();
}

/* Drag scrolls the result lines (as UP/DOWN do). */
static void res_touch(const touch_event_t *ev)
{
    if (ev->type == TEV_DRAG && ui_drag_scroll(ev, UI_DRAG_Y, RH, &rtop, R.n + (R.eq[0] ? 1 : 0) - RROWS))
        res_draw();
}

/* Soft keys: draw (results with a distribution), plot (results with a plot
 * action), back. Labels without fn reach res_key() as KEY_F1.. KEY_F5. */
static const softkeys_t *res_softkeys(void)
{
    static softkeys_t sk;
    memset(&sk, 0, sizeof sk);
    if (R_draw)
        sk.key[0].label = "draw";
    if (R_plot)
        sk.key[1].label = "plot";
    sk.key[4].label = "back";
    return &sk;
}
const screen_t scr_stat_result = {"statresult", "STAT", NULL, res_draw, res_key, NULL, NULL, res_touch,
                                  "stat-results", NULL, res_softkeys};
