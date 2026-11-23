/* Numeric forms: WINDOW (Xmin..Yscl plus the graph mode's T/θ/n ranges),
 * TBLSET (TblStart, ΔTbl) and the settings of a graph slider.
 *
 * One small form engine (form_t) drives three screens. Each field is a
 * label and a pointer to the double it edits (in `g` or `gx`, the graph
 * state); the selected field has a line editor, the others show their value.
 * Fields accept expressions ("2π", "-10/3"), evaluated to a double with
 * eval_number(). UP/DOWN/ENTER commit the field and move (wrapping); a value
 * that does not evaluate, or makes the form's validate() fail, is refused
 * with a red message naming the problem and the old value is kept. Without
 * an error the message line explains the selected field (field_t.hint). The first key typed into a
 * freshly loaded field replaces it; LEFT/RIGHT/⌫ or a menu insertion edit it
 * instead. Tapping a field selects it. Soft keys are the shared graph bar.
 *
 * Changing a WINDOW value invalidates the compiled functions and the graph
 * cache; the slider form re-applies the parameter value (clamped/snapped)
 * and stores it in its CAS variable. The values themselves are saved with
 * the graph state (not here).
 *
 * Also here: fmt_num() and eval_number(), the number helpers declared in
 * ui.h for tables, trace and forms. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "anim.h"
#include "g3d.h"
#include "graph.h"

typedef struct {
    const char *label; /* "Xmin=", drawn before the value */
    double *val;       /* the value edited in place */
    const char *hint;  /* what the field means, shown under the form while it is selected */
} field_t;

typedef struct {
    const field_t *fields;
    int n;
    int sel;         /* selected field */
    editor_t ed;     /* editor for the selected field */
    bool fresh;      /* the next character typed replaces the field */
    char err[64];    /* message line ("ERR: ..."), empty if none */
    /* NULL if the values (after the change) are acceptable, else what is wrong
     * ("Xmin must be less than Xmax") */
    const char *(*validate)(void);
    void (*after)(void); /* after a field was changed (optional) */
    int top;         /* first visible field (scrolling) */
} form_t;

/* Field lists. The WINDOW form shows the graph mode's parameter range first
 * (see win_enter); the x/y window rows are shared. */
#define WIN_XY                                                                                                     \
    {"Xmin=", &g.xmin, "left edge of the graph"}, {"Xmax=", &g.xmax, "right edge of the graph"},                  \
        {"Xscl=", &g.xscl, "distance between x tick marks (0: none)"},                                             \
        {"Ymin=", &g.ymin, "bottom edge of the graph"}, {"Ymax=", &g.ymax, "top edge of the graph"},              \
    {                                                                                                              \
        "Yscl=", &g.yscl, "distance between y tick marks (0: none)"                                                \
    }
static const field_t window_fields[] = {WIN_XY};
static const field_t window_par[] = {{"Tmin=", &gx.tmin, "first value of t"},
                                     {"Tmax=", &gx.tmax, "last value of t"},
                                     {"Tstep=", &gx.tstep, "step of t: smaller is smoother but slower"},
                                     WIN_XY};
static const field_t window_pol[] = {{"θmin=", &gx.thmin, "first value of θ"},
                                     {"θmax=", &gx.thmax, "last value of θ (360 in DEGREE mode)"},
                                     {"θstep=", &gx.thstep, "step of θ: smaller is smoother but slower"},
                                     WIN_XY};
static const field_t window_seq[] = {{"nMin=", &gx.nmin, "first n: the initial values belong to it"},
                                     {"nMax=", &gx.nmax, "last n computed"},
                                     {"PlotStart=", &gx.plotstart, "first n drawn"},
                                     {"PlotStep=", &gx.plotstep, "draw every k-th term"},
                                     WIN_XY};
static const field_t tbl_fields[] = {
    {"TblStart=", &g.tblstart, "x of the first row"},
    {"ΔTbl=", &g.tbldelta, "step between rows (not 0; negative counts down)"},
};

/* x/y window sanity (ui.h): non-empty ranges and non-negative tick spacing
 * (0 = no ticks). 0 if valid, -1 if not. */
int window_check(void)
{
    return (g.xmin < g.xmax && g.ymin < g.ymax && g.xscl >= 0 && g.yscl >= 0) ? 0 : -1;
}
/* What is wrong with the x/y window (NULL: nothing), for the form's message. */
static const char *window_problem(void)
{
    if (!(g.xmin < g.xmax))
        return "Xmin must be less than Xmax";
    if (!(g.ymin < g.ymax))
        return "Ymin must be less than Ymax";
    if (!(g.xscl >= 0 && g.yscl >= 0))
        return "Xscl and Yscl must be 0 or more";
    return NULL;
}
/* WINDOW form validation: the x/y window plus the current mode's range.
 * In SEQ mode the n values are checked rounded to integers, and stored
 * rounded only when they are valid (a refused edit leaves every field as it
 * was); the plotted range is limited to 10^6 terms. */
static const char *win_check(void)
{
    const char *p = window_problem();
    if (p)
        return p;
    switch (gx.mode) {
    case GM_PAR:
        return !(gx.tmin < gx.tmax) ? "Tmin must be less than Tmax" : !(gx.tstep > 0) ? "Tstep must be more than 0" : NULL;
    case GM_POL:
        return !(gx.thmin < gx.thmax) ? "θmin must be less than θmax"
               : !(gx.thstep > 0)     ? "θstep must be more than 0"
                                      : NULL;
    case GM_SEQ: {
        double n0 = round(gx.nmin), n1 = round(gx.nmax), ps = round(gx.plotstart), st = round(gx.plotstep);
        if (!(n0 >= 0))
            return "nMin must be 0 or more";
        if (!(n1 >= n0))
            return "nMax must not be less than nMin";
        if (!(st >= 1))
            return "PlotStep must be 1 or more";
        if (!(n1 - n0 <= 1e6))
            return "at most 10^6 terms (nMax-nMin)";
        gx.nmin = n0, gx.nmax = n1, gx.plotstart = ps, gx.plotstep = st;
        return NULL;
    }
    }
    return NULL;
}
static const char *tbl_check(void) { return g.tbldelta != 0 ? NULL : "ΔTbl must not be 0"; }
static void win_after(void) { funcs_invalidate(); }

/* The WINDOW and TBLSET forms; their fields point into g / gx. Zero-initialised
 * (.bss, not .data: a form holds an editor, so an initialiser would store
 * ~580 bytes per form in flash); the field lists and callbacks are set when
 * the screen is entered (win_enter, tbl_enter, slider_open_settings). */
static form_t win, tbl;

/* Shortest readable form of v in at most `width` characters (ui.h): try %g
 * with 10 significant digits and fewer until it fits (1 digit is accepted
 * even if too wide). Exponents are written "1E-5". Values below 1e-12 in
 * magnitude print as 0 (hides round-off such as 1e-17 on a grid line). */
void fmt_num(double v, int width, char *out, size_t n)
{
    if (isnan(v)) {
        snprintf(out, n, "undef");
        return;
    }
    if (fabs(v) < 1e-12)
        v = 0;
    for (int prec = 10; prec >= 1; prec--) {
        char tmp[48];
        snprintf(tmp, sizeof tmp, "%.*g", prec, v);
        /* tidy exponent: 1e-05 -> 1E-5 */
        char *e = strchr(tmp, 'e');
        if (e) {
            char mant[32], ex[16];
            size_t ml = (size_t)(e - tmp);
            memcpy(mant, tmp, ml);
            mant[ml] = 0;
            int exp = 0;
            sscanf(e + 1, "%d", &exp);
            snprintf(ex, sizeof ex, "E%d", exp);
            snprintf(tmp, sizeof tmp, "%s%s", mant, ex);
        }
        if ((int)strlen(tmp) <= width || prec == 1) {
            snprintf(out, n, "%s", tmp);
            return;
        }
    }
}

/* Evaluate an expression to a double without touching `ans` (ui.h). It is
 * compiled as a function of x with cas_compile() and evaluated at x = NaN, so
 * an expression that still depends on a free x gives NaN and is refused like
 * any other non-number. Returns 0 and *out on success, -1 with a message in
 * err (CAS error text, "syntax" or "not a number") otherwise. */
int eval_number(const char *cas_text, double *out, char *err, size_t errn)
{
    cas_fn *fn = NULL;
    cas_status st = cas_compile(g.cas, cas_text, "x", &fn);
    if (st != CAS_OK || !fn) {
        snprintf(err, errn, "%s", cas_last_error(g.cas) ? cas_last_error(g.cas) : "syntax");
        return -1;
    }
    double v = cas_fn_eval(fn, (double)NAN);
    cas_fn_free(g.cas, fn);
    if (isnan(v)) {
        snprintf(err, errn, "not a number");
        return -1;
    }
    *out = v;
    return 0;
}

/* Put the selected field's value into the editor (cursor at the start,
 * fresh: the next typed key replaces it). */
static void load_field(form_t *f)
{
    char b[48];
    fmt_num(*f->fields[f->sel].val, 24, b, sizeof b);
    /* negative numbers display with the negation sign ⁻ */
    char d[64];
    if (b[0] == '-')
        snprintf(d, sizeof d, "⁻%s", b + 1);
    else
        snprintf(d, sizeof d, "%s", b);
    ed_set_text(&f->ed, d);
    ed_home(&f->ed);
    f->fresh = true;
}

/* Commit the edited field; returns false (and sets err) if invalid.
 * Invalidates the graph cache; calls `after` only if the value changed. */
static bool commit(form_t *f)
{
    char cas[ED_MAX * 4];
    ed_cas(&f->ed, cas, sizeof cas);
    double v, old = *f->fields[f->sel].val;
    char err[64];
    if (eval_number(cas, &v, err, sizeof err)) {
        snprintf(f->err, sizeof f->err, "ERR: %.56s", err);
        return false;
    }
    *f->fields[f->sel].val = v;
    const char *bad = f->validate ? f->validate() : NULL;
    if (bad) {
        *f->fields[f->sel].val = old;
        snprintf(f->err, sizeof f->err, "ERR: %s", bad);
        return false;
    }
    f->err[0] = 0;
    g.graph_valid = false;
    if (f->after && v != old)
        f->after();
    return true;
}

/* Layout: fields every FPITCH px from BODY_Y + 6; FROWS (7) fit above the
 * 20 px message area, longer forms scroll (top). */
#define FPITCH (CH + 6)
#define FROWS ((BODY_H - 20) / FPITCH)
static int field_y(form_t *f, int i) { return BODY_Y + 6 + (i - f->top) * FPITCH; }
static void form_scroll(form_t *f)
{
    if (f->sel < f->top)
        f->top = f->sel;
    if (f->sel >= f->top + FROWS)
        f->top = f->sel - FROWS + 1;
    if (f->top < 0)
        f->top = 0;
}

/* Draw labels, values (selected field: its editor), ▲/▼ scroll marks and
 * the error line. */
static void form_draw(form_t *f)
{
    ui_cursor_hide();
    form_scroll(f);
    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, C_WHITE);
    if (f->top > 0)
        gfx_text(&font_small, LCD_WIDTH - 10, BODY_Y + 1, "▲", C_GRAY, GFX_TRANSPARENT);
    if (f->top + FROWS < f->n)
        gfx_text(&font_small, LCD_WIDTH - 10, UI_BOTTOM - 32, "▼", C_GRAY, GFX_TRANSPARENT);
    for (int i = f->top; i < f->n && i < f->top + FROWS; i++) {
        int y = field_y(f, i);
        gfx_text(FONT, 8, y, f->fields[i].label, C_DKGRAY, GFX_TRANSPARENT);
        int lw = gfx_text_width(FONT, f->fields[i].label) + 8;
        if (i == f->sel) {
            ui_draw_editor_line(&f->ed, lw, y, (LCD_WIDTH - lw) / CW, C_BLACK, C_WHITE, true);
        } else {
            char b[48];
            fmt_num(*f->fields[i].val, 24, b, sizeof b);
            gfx_text(FONT, lw, y, b, C_BLACK, GFX_TRANSPARENT);
        }
    }
    if (f->err[0])
        gfx_text(&font_medium, 8, UI_BOTTOM - 18, f->err, C_RED, GFX_TRANSPARENT);
    else if (f->fields[f->sel].hint) /* what the selected field means */
        gfx_text(&font_medium, 8, UI_BOTTOM - 18, f->fields[f->sel].hint, C_GRAY, GFX_TRANSPARENT);
}

/* Keys of a form screen (see the header). CLEAR empties the field, a second
 * CLEAR brings its value back; it never leaves the screen, so a stray CLEAR
 * loses nothing. Unused keys return without a redraw. */
static void form_key(form_t *f, int key, int mod)
{
    switch (key) {
    case KEY_UP:
    case KEY_DOWN:
    case KEY_ENTER:
        if (!commit(f))
            break;
        if (key == KEY_UP)
            f->sel = (f->sel + f->n - 1) % f->n;
        else
            f->sel = (f->sel + 1) % f->n;
        load_field(f);
        break;
    case KEY_CLEAR: /* empty the field; on an empty field bring its value back */
        if (ed_empty(&f->ed)) {
            load_field(f);
        } else {
            ed_clear(&f->ed);
            f->fresh = false;
        }
        break;
    default:
        if (key == KEY_LEFT || key == KEY_RIGHT || key == KEY_DEL) {
            f->fresh = false;
            ui_editor_key(&f->ed, key, mod);
            break;
        }
        {
            char t[16];
            if (!ui_key_text(key, mod, g.alpha_snapshot_for_key, t, sizeof t))
                return;
            if (f->fresh) {
                ed_clear(&f->ed);
                f->fresh = false;
            }
            ed_insert_text(&f->ed, t, NULL);
        }
    }
    form_draw(f);
}

/* Touch: tap a field to commit the current one and edit the tapped one;
 * drag to scroll (a field pushed off screen hands the focus to the nearest
 * visible one after a commit; a refused value keeps it, and form_scroll
 * brings it back into view). */
static void form_touch(form_t *f, const touch_event_t *ev)
{
    if (ev->type == TEV_DRAG) {
        int top = f->top;
        if (!ui_drag_scroll(ev, UI_DRAG_Y, FPITCH, &top, f->n - FROWS))
            return;
        int s = ui_drag_focus(f->sel, top, FROWS);
        f->top = top;
        if (s != f->sel && commit(f)) {
            f->sel = s;
            load_field(f);
        }
        form_draw(f);
        return;
    }
    if (ev->type != TEV_TAP)
        return;
    for (int i = f->top; i < f->n && i < f->top + FROWS; i++) {
        int y = field_y(f, i);
        if (ev->y < y - 3 || ev->y >= y + CH + 3)
            continue;
        if (i != f->sel) {
            if (commit(f)) {
                f->sel = i;
                load_field(f);
            }
        } else {
            int lw = gfx_text_width(FONT, f->fields[i].label) + 8;
            f->fresh = false;
            ui_editor_tap(&f->ed, f->ed.scroll + (ev->x - lw) / CW);
        }
        form_draw(f);
        return;
    }
}

/* WINDOW: choose the field list for the current graph mode (index =
 * GM_FUNC, GM_PAR, GM_POL, GM_SEQ, GM_IMPL, GM_DE; implicit relations and
 * differential equations use the plain x/y window) and restart at the first field if the mode changed. Coming back
 * from a menu (ui_returning) keeps the edit in progress. */
static void win_enter(void)
{
    win.validate = win_check;
    win.after = win_after;
    if (ui_returning && win.fields)
        return;
    static const struct {
        const field_t *f;
        int n;
    } per_mode[GM_NMODES] = {
        {window_fields, 6}, {window_par, 9}, {window_pol, 9}, {window_seq, 10}, {window_fields, 6}, {window_fields, 6},
    };
    if (win.fields != per_mode[gx.mode].f) {
        win.fields = per_mode[gx.mode].f;
        win.n = per_mode[gx.mode].n;
        win.sel = win.top = 0;
    }
    win.err[0] = 0;
    load_field(&win);
}
static void win_draw(void) { form_draw(&win); }
static void win_key(int key, int mod) { form_key(&win, key, mod); }
static void win_touch(const touch_event_t *ev) { form_touch(&win, ev); }
/* menus (MATH, CAS, VARS) insert into the selected field, keeping its text */
static editor_t *win_editor(void)
{
    win.fresh = false;
    return &win.ed;
}
const screen_t scr_window = {"window", "WINDOW", win_enter, win_draw, win_key, NULL, win_editor, win_touch,
                             NULL, &softkeys_graph};

static void tbl_enter(void)
{
    tbl.validate = tbl_check;
    if (ui_returning && tbl.fields)
        return;
    tbl.fields = tbl_fields;
    tbl.n = 2;
    tbl.err[0] = 0;
    load_field(&tbl);
}
static void tbl_draw(void) { form_draw(&tbl); }
static void tbl_key(int key, int mod) { form_key(&tbl, key, mod); }
static void tbl_touch(const touch_event_t *ev) { form_touch(&tbl, ev); }
static editor_t *tbl_editor(void)
{
    tbl.fresh = false;
    return &tbl.ed;
}
const screen_t scr_tblset = {"tblset", "TABLE SETUP", tbl_enter, tbl_draw, tbl_key, NULL, tbl_editor, tbl_touch,
                             NULL, &softkeys_graph};

/* ------------------------------------------------------------ slider settings */
/* Form for one graph slider parameter (graph screen: ENTER with sliders shown,
 * or a tap on a slider outside its track): value, min, max and step, and
 * (QSPI tiers: the Animation module, app/anim.h) how F5 "anim" plays it:
 * start, end, seconds per sweep, mode 0 loop / 1 bounce / 2 once. The field
 * list is built when it is opened. The same form shows only the animation
 * rows for the 3D variable and the comet (anim_settings_open). */
#if defined(CONFIG_CONTENT_QSPI)
#define ANIM_ROWS 4
#else
#define ANIM_ROWS 0
#endif
static field_t slider_fields[4 + 4];
static char slider_label[8]; /* "a=" */
static int slider_k;         /* parameter index in gx.prm[], -1: animation rows only */
static anim_cfg_t *slider_anim; /* the animation settings shown (NULL: none) */
/* the animation rows: start != end (not for the comet), 0.1 <= seconds <= 3600, mode 0..2 */
static const char *anim_check(void)
{
    anim_cfg_t *c = slider_anim;
    if (!c)
        return NULL;
    if (c->name != '@' && !(c->start != c->end))
        return "start and end must differ";
    if (!(c->secs >= 0.1 && c->secs <= 3600))
        return "seconds must be 0.1 to 3600";
    c->mode = round(c->mode);
    return c->mode >= 0 && c->mode <= 2 ? NULL : "mode is 0 (loop), 1 (bounce) or 2 (once)";
}
/* min < max and 0 <= step <= max - min (step 0: no snapping). */
static const char *slider_check(void)
{
    if (slider_k < 0)
        return anim_check();
    gparam_t *p = &gx.prm[slider_k];
    if (!(p->min < p->max))
        return "min must be less than max";
    if (!(p->step >= 0 && p->step <= p->max - p->min))
        return "step must be 0 to max-min";
    return anim_check();
}
/* append the animation rows of settings c to the form's fields at n */
static int anim_rows(anim_cfg_t *c, int n)
{
    slider_anim = c;
    if (!c || !ANIM_ROWS)
        return n;
    if (c->name != '@') {
        slider_fields[n++] = (field_t){"start=", &c->start, "animation (F5 anim): first value"};
        slider_fields[n++] = (field_t){"end=", &c->end, "animation: last value"};
    }
    slider_fields[n++] = (field_t){"seconds=", &c->secs, "animation: seconds from start to end"};
    slider_fields[n++] = (field_t){"mode=", &c->mode, "animation: 0 loop, 1 bounce (back and forth), 2 once"};
    return n;
}
static void slider_after(void)
{
    if (slider_k < 0)
        return;
    gx_param_set(slider_k, gx.prm[slider_k].val); /* clamp / snap */
    gx_param_commit(slider_k);
}
static form_t sld; /* fields and callbacks set by slider_open_settings (see win) */

/* Open the slider settings form for parameter k (ignored if k is not a used
 * parameter) and make k the selected slider. */
void slider_open_settings(int k)
{
    if (k < 0 || k >= GNPARAM || !gx.prm[k].used)
        return;
    gparam_t *p = &gx.prm[k];
    slider_k = k;
    gx.sel_param = (uint8_t)k;
    snprintf(slider_label, sizeof slider_label, "%s=", p->name);
    slider_fields[0] = (field_t){slider_label, &p->val, "value now (kept between min and max)"};
    slider_fields[1] = (field_t){"min=", &p->min, "left end of the slider"};
    slider_fields[2] = (field_t){"max=", &p->max, "right end of the slider"};
    slider_fields[3] = (field_t){"step=", &p->step, "step of + − and of dragging (0: any value)"};
    sld.fields = slider_fields;
    sld.n = anim_rows(ANIM_ROWS ? anim_cfg(p->name[0], true, p->min, p->max) : NULL, 4);
    sld.validate = slider_check;
    sld.after = slider_after;
    sld.sel = sld.top = 0;
    sld.err[0] = 0;
    load_field(&sld);
    ui_switch(&scr_slider);
}

/* Slider screen: the form plus a hint (form_key/form_touch redraw the form
 * only, so the hint is drawn again after them). */
static void sld_draw(void)
{
    form_draw(&sld);
    gfx_text(&font_small, 8, UI_BOTTOM - 34, "F5 (graph) returns to the graph", C_GRAY,
             GFX_TRANSPARENT);
}
static void sld_key(int key, int mod)
{
    form_key(&sld, key, mod);
    sld_draw();
}
static void sld_touch(const touch_event_t *ev)
{
    form_touch(&sld, ev);
    if (ev->type == TEV_TAP || ev->type == TEV_DRAG)
        sld_draw();
}
static editor_t *sld_editor(void)
{
    sld.fresh = false;
    return &sld.ed;
}
static void sld_enter(void) {}
const screen_t scr_slider = {"slider", "SLIDER", sld_enter, sld_draw, sld_key, NULL, sld_editor, sld_touch,
                             "graph-sliders", &softkeys_graph};

/* The animation settings of the 3D graph's variable or of the comet (MENU
 * of F5 "anim", app/anim_core.c): the same form with the animation rows
 * only; F5 goes back to the graph it came from. */
static const softkeys_t anim_sk3 = {{{"z=", NULL, &scr_g3d_eq}, {"window", NULL, &scr_g3d_win}, {NULL}, {NULL},
                                     {"graph", NULL, &scr_g3d}}};
static int animset_scr;
static const softkeys_t *animset_softkeys(void) { return animset_scr == ANIM_SCR_3D ? &anim_sk3 : &softkeys_graph; }
static void animset_draw(void) { form_draw(&sld); }
const screen_t scr_animset = {"animset", "ANIMATION", sld_enter, animset_draw, sld_key, NULL, sld_editor,
                              sld_touch, "animation", &softkeys_graph, animset_softkeys};

void anim_settings_open(int scr)
{
    anim_target_t t;
    anim_target(&t);
    if (!t.kind) {
        ui_message("NOTHING TO ANIMATE", scr == ANIM_SCR_3D ? "Use a free variable in z=, e.g. z=sin(x-t)."
                                                            : "Use a free variable in y=, e.g. y=sin(x-t).");
        return;
    }
    if (t.kind == ANIM_T_PARAM) { /* 2D parameter: its slider form */
        for (int k = 0; k < GNPARAM; k++)
            if (gx.prm[k].used && gx.prm[k].name[0] == t.name[0]) {
                slider_open_settings(k);
                return;
            }
    }
    animset_scr = scr;
    slider_k = -1;
    sld.fields = slider_fields;
    sld.n = anim_rows(anim_cfg(t.kind == ANIM_T_COMET ? '@' : t.name[0], true, t.start, t.end), 0);
    sld.validate = slider_check;
    sld.after = slider_after;
    sld.sel = sld.top = 0;
    sld.err[0] = 0;
    load_field(&sld);
    ui_switch(&scr_animset);
}
