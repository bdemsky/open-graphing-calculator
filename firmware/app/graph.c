/* GRAPH: axes/grid, plots of every graph mode (gr_plot.c), free cursor, TRACE,
 * CALC operations (gr_calc.c), sliders (gr_slider.c), tap-to-label and snapping
 * to special points, touch pan/zoom, ZOOM operations, animation frames.
 *
 * Screen: scr_graph (soft keys: the shared softkeys_graph bar, F4 = TRACE is
 * delivered to graph_key as KEY_F4; F5 is "anim" here: the ANIMATION menu,
 * app/anim_core.c). Help topic "graph".
 *
 * Animation (app/anim.h): the screen's tick runs the animation module's
 * frame timer; each frame sets the animated value and calls
 * graph_anim_frame(), which re-runs the last CALC operation while its result
 * is shown (lastc), else follows the TRACE cursor, re-plots and redraws the
 * decorations, among them the module's play bar.
 *
 * Layers. The plot area is the whole body (GX0, GY0, GW x GH; see the view
 * mapping in graph.h). Drawing is split in two layers:
 *   1. The plot: background, grid/axes, graph overlays (stat plots), every
 *      enabled entry, error line. render() draws it and copies it into
 *      g.graph_cache (a full-frame RGB565 buffer carved from the arena in
 *      app_init(); NULL when the arena is small) and sets g.graph_valid. Any
 *      change of WINDOW, Y=, settings or theme clears g.graph_valid.
 *   2. The decorations ("overlays" in this file, not to be confused with the
 *      graph_overlay_t stat-plot layers): sliders, trace/free cursor, the top
 *      band (traced entry or CALC prompt), the bottom band (coordinates, a
 *      typed value or a CALC result), point labels and CALC bound markers.
 *      draw_overlays() records each decoration's rectangle; the next call
 *      first restores just those rectangles from the cache, so moving the
 *      cursor costs a few small copies instead of a re-plot. Without a cache
 *      the whole plot is rendered again.
 *
 * Cursor state machine (vs):
 *   VS_PLAIN  just the graph (and a CALC result band, if any);
 *   VS_FREE   a free crosshair at (fx, fy) moved pixel by pixel with the
 *             arrows, readout X/Y (IMPL mode also shows F = lhs - rhs);
 *   VS_TRACE  the cursor follows a "trace target" tfn: entry 0..GNENT-1 of the
 *             current graph mode (DE mode: solution curve 0..DE_MAXC-1), or
 *             (tfn >= NTE) a point set of an active graph overlay. tpar is
 *             the traced parameter (x, t, θ, n or point index) and (tx, ty)
 *             the resulting point.
 * The first arrow press from VS_PLAIN enters VS_FREE at the centre; TRACE (F4)
 * enters VS_TRACE; CLEAR steps back (result/label -> cursor -> home screen).
 *
 * CALC (SHIFT F4, menu in gr_calc.c): graph_calc_start() enters TRACE and asks
 * for 0..3 inputs (bounds, guess, second curve) confirmed with ENTER or a tap
 * (calc_confirm); then calc_run() computes and fills the result band.
 *
 * Touch: see the comment above preview(). State lives in the statics below
 * (not saved; a mode change resets them in graph_enter). */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "anim.h"
#include "graph.h"
#include "hal.h"
#include "ui.h"

#define BAND_H 16 /* height of the text bands at the top and bottom of the plot */

gr_view_t gr_vp = {0, BODY_Y, LCD_WIDTH, BODY_H};

enum { VS_PLAIN, VS_FREE, VS_TRACE }; /* cursor state (see the file comment) */
static int vs;
static int fx, fy;          /* free cursor (pixels, graph-relative) */
/* trace targets below NTE are entries (DE: solution curves), from NTE on overlay point sets */
#define NTE (DE_MAXC > GNENT ? DE_MAXC : GNENT)
static int tfn = -1;        /* trace target: entry 0..GNENT-1 (DE: curve), or NTE + overlay set */
static double tpar;         /* FUNC: x; PAR: t; POL: θ; SEQ: n; overlay: point index */
static double tx, ty;       /* trace coordinates */
static int nplotted;        /* entries plotted by the last render (console state) */
static char errline[80];    /* first compile error, shown at the top of the plot */
static uint32_t render_us;  /* duration of the last render (console state graph_render_us) */

/* tap-to-label / results */
static char label[48];      /* box next to the trace cursor ("" = none) */
static char result[96];     /* CALC result band ("" = none) */

/* CALC interaction: calc_op is the CALC_* operation in progress (0 = none),
 * calc_step the number of inputs confirmed so far. calc_lo/calc_hi hold the
 * confirmed left/right bounds; for intersect calc_fn1 is the first curve and
 * calc_hi (reused) the index of the second one. */
static int calc_op, calc_step, calc_fn1;
static double calc_lo, calc_hi;
static bool num_active;     /* typing a value for the current step */
static bool value_follow;   /* CALC value result shown: UP/DOWN evaluate the other curves */
static editor_t num_ed;     /* the typed value (bottom band; menus insert here) */
/* the last CALC operation run, re-run on every animation frame while its
 * result is shown (graph_anim_frame): the result follows the moving curve */
static struct {
    int op, fn, fn2;
    double lo, hi, guess;
} lastc;

/* touch: slider being dragged */
static int drag_slider = -1;  /* index into gx.prm, -1 = none */
static bool down_on_slider;   /* the gesture started on a slider row: no panning */

/* overlay rectangles to restore from the cache before redrawing overlays
 * (screen coordinates). When the table is full the last entry becomes the whole
 * plot, so nothing is ever left behind on the screen. */
typedef struct { int x, y, w, h; } orect_t;
static orect_t overlays[16];
static int noverlays;

/* Short names for the view mapping (graph.h): plot-relative pixels <-> graph
 * coordinates. Add GX0/GY0 to get screen coordinates. */
static double px_to_x(double px) { return gv_px_to_x(px); }
static double py_to_y(double py) { return gv_py_to_y(py); }
static double x_to_px(double x) { return gv_x_to_px(x); }
static double y_to_py(double y) { return gv_y_to_py(y); }

/* ------------------------------------------------------------ trace targets */
/* Trace targets are numbered 0..GNENT-1 for the entries of the current graph
 * mode (DE mode: 0..DE_MAXC-1 for its solution curves), then NTE.. for the
 * point sets of the active graph overlays, in registry order. */

/* Total number of traceable point sets over all active overlays (an overlay
 * needs nsets() and point() to be traceable). */
static int ov_nsets(void)
{
    int n = 0;
    for (int i = 0; i < graph_overlay_registry_count; i++) {
        const graph_overlay_t *o = graph_overlay_registry[i];
        if (o->nsets && o->point && (!o->active || o->active()))
            n += o->nsets();
    }
    return n;
}

/* Is any graph overlay (stat plot, geometry) drawing something? */
static bool ov_any_active(void)
{
    for (int i = 0; i < graph_overlay_registry_count; i++) {
        const graph_overlay_t *o = graph_overlay_registry[i];
        if (o->draw && (!o->active || o->active()))
            return true;
    }
    return false;
}

/* Map overlay point-set number j (0-based over all active overlays) to its
 * overlay and the set index within it (*set). NULL if j is out of range. */
static const graph_overlay_t *ov_set(int j, int *set)
{
    for (int i = 0; i < graph_overlay_registry_count; i++) {
        const graph_overlay_t *o = graph_overlay_registry[i];
        if (!o->nsets || !o->point || (o->active && !o->active()))
            continue;
        int n = o->nsets();
        if (j < n) {
            *set = j;
            return o;
        }
        j -= n;
    }
    return NULL;
}

/* Can target t be traced? Entries must be enabled and compiled; implicit
 * relations (IMPL) have no parameter to walk along and are never traced; in
 * DE mode the targets are the solution curves. */
static bool target_ok(int t)
{
    if (t < 0)
        return false;
    if (t < NTE) {
        if (gx.mode == GM_DE) {
            int e, tp;
            double x, y;
            return gx_de_curve(t, &e, &x, &y, &tp);
        }
        return gx.mode != GM_IMPL && gx_plottable(t);
    }
    return t - NTE < ov_nsets();
}

/* The next traceable target after `from` in direction dir (+1/-1), wrapping
 * around; `from` itself is returned last if it is the only one. -1 if none.
 * next_target(-1, 1) is the first traceable target. */
static int next_target(int from, int dir)
{
    int n = NTE + ov_nsets();
    for (int k = 1; k <= n; k++) {
        int i = ((from + dir * k) % n + n) % n;
        if (target_ok(i))
            return i;
    }
    return -1;
}

/* Step of the trace parameter for one LEFT/RIGHT press in the curve modes
 * (FUNC moves by whole pixels in trace_move instead). */
static double trace_step(void)
{
    switch (gx.mode) {
    case GM_PAR: return gx.tstep;
    case GM_POL: return gx.thstep;
    case GM_SEQ: return 1;
    default: return (g.xmax - g.xmin) / (GW - 1);
    }
}

/* Recompute the traced point (tx, ty) from tfn and tpar. For overlay point
 * sets tpar is clamped to a valid point index. Undefined points give NAN
 * (the cursor is then hidden and the readout shows the NAN). */
static void trace_update(void)
{
    tx = ty = NAN;
    if (tfn >= NTE) {
        int set;
        const graph_overlay_t *o = ov_set(tfn - NTE, &set);
        int n = o && o->npoints ? o->npoints(set) : 0;
        if (tpar >= n)
            tpar = n - 1;
        if (tpar < 0)
            tpar = 0;
        if (o && n > 0)
            o->point(set, (int)tpar, &tx, &ty);
        return;
    }
    switch (gx.mode) {
    case GM_FUNC:
        tx = tpar;
        ty = gx_f(tfn, tx);
        break;
    case GM_PAR:
    case GM_POL:
        gx_par(tfn, tpar, &tx, &ty);
        break;
    case GM_SEQ:
        if (gx.seqplot == SEQ_WEB) { /* on the web: (u(n-1), u(n)) */
            tx = gx_seq(tfn, (long)tpar - 1);
            ty = gx_seq(tfn, (long)tpar);
        } else {
            tx = tpar;
            ty = gx_seq(tfn, (long)tpar);
        }
        break;
    case GM_DE: /* the solution curve at x */
        tx = tpar;
        ty = gx_de_solve(tfn, tpar);
        break;
    }
}

/* graph.h: the traced parameter while tracing an entry (not an overlay set);
 * used by the CALC menu and TABLE to start from the cursor. */
bool graph_trace_value(double *v)
{
    if (vs != VS_TRACE || tfn < 0 || tfn >= NTE)
        return false;
    *v = tpar;
    return true;
}

int graph_trace_fn(void) { return vs == VS_TRACE ? tfn : -1; }

/* Starting value of the trace parameter when TRACE begins: the screen centre
 * (FUNC), the start of the t/θ range, the first visible integer n >= nMin, or
 * the first point of an overlay set. */
static void trace_home(void)
{
    switch (gx.mode) {
    case GM_PAR: tpar = gx.tmin; break;
    case GM_POL: tpar = gx.thmin; break;
    case GM_SEQ: tpar = fmax(gx.nmin, ceil(fmax(g.xmin, gx.nmin))); break;
    case GM_DE: { /* the curve's initial point */
        int e, tp;
        double y;
        if (!gx_de_curve(tfn, &e, &tpar, &y, &tp))
            tpar = px_to_x(GW / 2);
        break;
    }
    default: tpar = px_to_x(GW / 2); break;
    }
    if (tfn >= NTE)
        tpar = 0;
}

/* ------------------------------------------------------------ rendering */
/* Grid and axes for the current WINDOW. Grid lines (and a dot at every grid
 * point) at multiples of Xscl/Yscl, only when there are fewer than 160 x 120 of
 * them (otherwise the grid would be a solid fill and the loops slow); axes
 * through 0 when 0 is inside the range, with 5 px tick marks at the scale
 * multiples (skipped beyond 200 ticks). */
static void draw_axes_grid(void)
{
    if (g.grid && g.xscl > 0 && g.yscl > 0 && (g.xmax - g.xmin) / g.xscl < 160 &&
        (g.ymax - g.ymin) / g.yscl < 120) {
        for (double x = ceil(g.xmin / g.xscl) * g.xscl; x <= g.xmax; x += g.xscl)
            for (double y = ceil(g.ymin / g.yscl) * g.yscl; y <= g.ymax; y += g.yscl) {
                int px = (int)lround(x_to_px(x)), py = (int)lround(y_to_py(y));
                gfx_pixel(GX0 + px, GY0 + py, C_GRAY);
            }
        for (double x = ceil(g.xmin / g.xscl) * g.xscl; x <= g.xmax; x += g.xscl)
            gfx_vline(GX0 + (int)lround(x_to_px(x)), GY0, GH, C_GRID);
        for (double y = ceil(g.ymin / g.yscl) * g.yscl; y <= g.ymax; y += g.yscl)
            gfx_hline(GX0, GY0 + (int)lround(y_to_py(y)), GW, C_GRID);
    }
    if (!g.axes)
        return;
    int ax = (int)lround(x_to_px(0)), ay = (int)lround(y_to_py(0));
    bool xaxis = g.ymin <= 0 && g.ymax >= 0, yaxis = g.xmin <= 0 && g.xmax >= 0;
    if (xaxis) {
        gfx_hline(GX0, GY0 + ay, GW, C_AXES);
        if (g.xscl > 0 && (g.xmax - g.xmin) / g.xscl < 200)
            for (double x = ceil(g.xmin / g.xscl) * g.xscl; x <= g.xmax; x += g.xscl)
                gfx_vline(GX0 + (int)lround(x_to_px(x)), GY0 + ay - 2, 5, C_AXES);
    }
    if (yaxis) {
        gfx_vline(GX0 + ax, GY0, GH, C_AXES);
        if (g.yscl > 0 && (g.ymax - g.ymin) / g.yscl < 200)
            for (double y = ceil(g.ymin / g.yscl) * g.yscl; y <= g.ymax; y += g.yscl)
                gfx_hline(GX0 + ax - 2, GY0 + (int)lround(y_to_py(y)), 5, C_AXES);
    }
}

/* Plot layer: compile what changed, draw background, grid/axes, graph overlays
 * (stat plots, under the curves) and every enabled entry, clipped to the plot.
 * Also counts the plotted entries and puts the first problem (compile error,
 * or a web plot of a sequence that depends on other sequences) in errline,
 * drawn at the top left. Saves the result in g.graph_cache (marking it valid)
 * and forgets the decoration rectangles, which the plot has just covered.
 * Cost: one evaluation per pixel column per entry (more in the curve modes),
 * timed into render_us. */
static void render(void)
{
    uint32_t c0 = hal_cycles();
    gx_compile();
    gfx_set_clip(GX0, GY0, GW, GH);
    gfx_fill_rect(GX0, GY0, GW, GH, C_WHITE);
    draw_axes_grid();
    gr_overlays_draw();
    gr_plot_all();
    nplotted = 0;
    errline[0] = 0;
    for (int i = 0; i < GNENT; i++) {
        if (gx_plottable(i)) {
            nplotted++;
            if (gx.mode == GM_SEQ && gx.seqplot == SEQ_WEB && !gx_seq_selfonly(i) && !errline[0])
                snprintf(errline, sizeof errline, "u%d: web plot needs u%d(n-1)", i + 1, i + 1);
        } else if (gx_enabled(i) && gx_error(i)[0] && !errline[0]) {
            char lb[16];
            snprintf(errline, sizeof errline, "%s: %s", gx_entry_label(i, 0, lb, sizeof lb), gx_error(i));
        }
    }
    gfx_reset_clip();
    if (errline[0]) {
        int w = gfx_text_width(&font_medium, errline);
        gfx_fill_rect(0, GY0, w + 8, 16, C_WHITE);
        gfx_text(&font_medium, 4, GY0 + 1, errline, C_RED, GFX_TRANSPARENT);
    } else if (!nplotted && !ov_any_active()) {
        /* an empty graph says where its content comes from (like the 3D graph) */
        static const char *const what[GM_NMODES] = {"a function", "a curve x(t), y(t)", "a curve r(θ)",
                                                    "a sequence", "a relation", "an equation y'=f(x,y)"};
        char b[64];
        snprintf(b, sizeof b, "y= (F1): enter %s", what[gx.mode]);
        int w = gfx_text_width(&font_medium, b);
        gfx_fill_rect(0, GY0, w + 8, 16, C_WHITE);
        gfx_text(&font_medium, 4, GY0 + 1, b, C_GRAY, GFX_TRANSPARENT);
    }
    if (g.graph_cache) {
        gfx_save_rect(g.graph_cache, GX0, GY0, GW, GH);
        g.graph_valid = true;
    }
    noverlays = 0;
    render_us = (hal_cycles() - c0) / hal_cycles_per_us();
}

/* The plot layer of a notebook graph cell (graph.h): like render() without the overlays, the
 * cache and the decorations; text sized for a small plot. */
void graph_plot_view(const char *empty)
{
    bool shade = calc_shade.on;
    calc_shade.on = false; /* (the graph screen's CALC integral is not the cell's) */
    gx_compile();
    gfx_set_clip(GX0, GY0, GW, GH);
    gfx_fill_rect(GX0, GY0, GW, GH, C_WHITE);
    draw_axes_grid();
    gr_plot_all();
    const char *msg = NULL;
    char lb[16], b[80];
    bool any = false;
    for (int i = 0; i < GNENT && !msg; i++) {
        if (gx_plottable(i))
            any = true;
        else if (gx_enabled(i) && gx_error(i)[0]) {
            snprintf(b, sizeof b, "%s: %s", gx_entry_label(i, 0, lb, sizeof lb), gx_error(i));
            msg = b;
        }
    }
    if (!msg && !any)
        msg = empty;
    if (msg) {
        int w = gfx_text_width(&font_small, msg);
        gfx_fill_rect(GX0, GY0, w + 6, 14, C_WHITE);
        gfx_text(&font_small, GX0 + 3, GY0 + 1, msg, any ? C_RED : C_GRAY, GFX_TRANSPARENT);
    }
    gfx_reset_clip();
    calc_shade.on = shade;
}

/* Remember a decoration rectangle (screen coordinates) for clear_overlays(). */
static void add_overlay(int x, int y, int w, int h)
{
    if (noverlays < (int)(sizeof overlays / sizeof overlays[0]))
        overlays[noverlays++] = (orect_t){x, y, w, h};
    else
        overlays[noverlays - 1] = (orect_t){0, GY0, GW, GH}; /* too many: restore everything */
}

/* Erase the decorations drawn by the previous draw_overlays(): copy their
 * rectangles back from the plot cache (or re-render without a cache). */
static void clear_overlays(void)
{
    if (!g.graph_cache) { /* no cache (small arena): redraw the plot */
        if (noverlays)
            render();
        noverlays = 0;
        return;
    }
    for (int i = 0; i < noverlays; i++)
        gfx_restore_rect(g.graph_cache, overlays[i].x, overlays[i].y, overlays[i].w, overlays[i].h);
    noverlays = 0;
}

/* Cursor at screen point (x, y): a 3x3 box with arms from 2 to 5 px away
 * (the gap keeps the point itself visible), clipped to the plot. */
static void draw_cross(int x, int y, uint16_t c)
{
    gfx_set_clip(GX0, GY0, GW, GH);
    for (int d = -5; d <= 5; d++) {
        if (d > -2 && d < 2)
            continue;
        gfx_pixel(x + d, y, c);
        gfx_pixel(x, y + d, c);
    }
    gfx_rect(x - 1, y - 1, 3, 3, c);
    gfx_reset_clip();
    add_overlay(x - 5, y - 5, 11, 11);
}

/* Full-width text band at screen row y (GY0: top band, separator below it;
 * UI_BOTTOM - BAND_H: bottom band, separator above it). */
static void draw_band(int y, const char *text, uint16_t fg)
{
    gfx_fill_rect(0, y, LCD_WIDTH, BAND_H, C_WHITE);
    gfx_hline(0, y == GY0 ? y + BAND_H - 1 : y, LCD_WIDTH, C_LTGRAY);
    gfx_text(&font_medium, 4, y + 1, text, fg, GFX_TRANSPARENT);
    add_overlay(0, y, LCD_WIDTH, BAND_H);
}

/* Boxed label (border in colour c) next to the point at screen (px, py):
 * preferably above right of it; flipped to the left at the right edge and
 * below the point when it would cover the top band; kept above the soft keys. */
static void draw_label(int px, int py, const char *text, uint16_t c)
{
    int w = gfx_text_width(&font_small, text) + 6, h = 15;
    int x = px + 6, y = py - h - 4;
    if (x + w > GW)
        x = px - w - 6;
    if (x < 0)
        x = 0;
    if (y < GY0 + BAND_H)
        y = py + 6;
    if (y + h > UI_BOTTOM)
        y = UI_BOTTOM - h;
    gfx_fill_rect(x, y, w, h, C_WHITE);
    gfx_rect(x, y, w, h, c);
    gfx_text(&font_small, x + 3, y + 1, text, C_BLACK, GFX_TRANSPARENT);
    add_overlay(x, y, w, h);
}

/* Dashed vertical line at graph x (a confirmed CALC bound or an integral limit)
 * between the two bands; nothing when x is off screen. */
static void bound_marker(double x)
{
    int px = (int)lround(x_to_px(x));
    if (px < 0 || px >= GW)
        return;
    gfx_set_clip(GX0, GY0, GW, GH);
    for (int y = GY0 + BAND_H; y < UI_BOTTOM - BAND_H; y += 4)
        gfx_vline(GX0 + px, y, 2, C_DKGRAY);
    gfx_reset_clip();
    add_overlay(GX0 + px, GY0, 1, GH);
}

/* Shorten the UTF-8 string s in place, a whole character at a time, until it
 * is at most `w` pixels wide in font f. */
static void fit_text(const font_t *f, char *s, int w)
{
    size_t n = strlen(s);
    while (n > 0 && gfx_text_width(f, s) > w) {
        do
            n--;
        while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80);
        s[n] = 0;
    }
}

/* "name=value" with the value in at most 10 characters (fmt_num). */
static void num_text(const char *name, double v, char *out, size_t n)
{
    char a[20];
    fmt_num(v, 10, a, sizeof a);
    snprintf(out, n, "%.8s=%.19s", name, a);
}

/* DE: the slope y' of entry i at (x, y) as " y'=..." (empty without an entry) */
static void slope_text(int i, double x, double y, char *out, size_t n)
{
    out[0] = 0;
    if (i >= 0 && i < GNENT && gx_plottable(i))
        num_text(" y'", gx_impl(i, x, y), out, n);
}

/* Bottom-band readout of the trace cursor: X/Y (FUNC, overlays), n and u(n)
 * (SEQ), T X Y (PAR), θ R X Y (POL), X Y y' (DE). */
static void coord_text(char *out, size_t n)
{
    char a[32], b[32], c[32];
    if (tfn < NTE && gx.mode == GM_DE) {
        int e = -1, tp;
        double x0, y0;
        gx_de_curve(tfn, &e, &x0, &y0, &tp);
        num_text("X", tx, a, sizeof a);
        num_text("Y", ty, b, sizeof b);
        slope_text(e, tx, ty, c, sizeof c);
        snprintf(out, n, "%s  %s %s", a, b, c);
    } else if (tfn >= NTE || gx.mode == GM_FUNC || gx.mode == GM_IMPL) {
        num_text("X", tx, a, sizeof a);
        num_text("Y", ty, b, sizeof b);
        snprintf(out, n, "%s   %s", a, b);
    } else if (gx.mode == GM_SEQ) {
        char nm[16];
        snprintf(nm, sizeof nm, "u%d", tfn + 1);
        snprintf(a, sizeof a, "n=%ld", (long)tpar);
        num_text(nm, ty, b, sizeof b);
        snprintf(out, n, "%s   %s", a, b);
    } else {
        num_text(gx.mode == GM_PAR ? "T" : "θ", tpar, a, sizeof a);
        num_text("X", tx, b, sizeof b);
        num_text("Y", ty, c, sizeof c);
        if (gx.mode == GM_POL) {
            char r[32];
            num_text("R", gx_pol_r(tfn, tpar), r, sizeof r);
            snprintf(out, n, "%.24s %.24s %.24s %.24s", a, r, b, c);
        } else {
            snprintf(out, n, "%s  %s  %s", a, b, c);
        }
    }
}

/* Top-band text naming the trace target ("Y1=x^2", "X1T=..  Y1T=..", or the
 * overlay's own label) and its colour in *col. Uses the shared scratch editor
 * gx_tmp_ed to turn stored tokens into display text. */
static void target_label(char *out, size_t n, uint16_t *col)
{
    if (tfn >= NTE) {
        int set;
        const graph_overlay_t *o = ov_set(tfn - NTE, &set);
        out[0] = 0;
        if (o && o->label)
            o->label(set, out, n);
        *col = o && o->color ? o->color(set) : C_BLACK;
        return;
    }
    char disp[ED_MAX * 4], lb[16];
    if (gx.mode == GM_DE) { /* "y1'=x-y  (0, 1)": the equation and the curve's initial point */
        int e = 0, tp;
        double x0 = NAN, y0 = NAN;
        char a[16], b[16];
        gx_de_curve(tfn, &e, &x0, &y0, &tp);
        *col = func_colors[gx_color(e)];
        gtext_to_editor(&gx_entry(e)->ex[0], &gx_tmp_ed);
        ed_display(&gx_tmp_ed, disp, sizeof disp);
        fmt_num(x0, 8, a, sizeof a);
        fmt_num(y0, 8, b, sizeof b);
        snprintf(out, n, "y%d'=%.30s  (%s, %s)", e + 1, disp, a, b);
        return;
    }
    *col = func_colors[gx_color(tfn)];
    if (gx.mode == GM_FUNC) {
        ed_display(&g.f[tfn].ed, disp, sizeof disp);
        snprintf(out, n, "Y%d=%.40s", tfn + 1, disp);
        return;
    }
    editor_t *e = &gx_tmp_ed;
    gentry_t *en = gx_entry(tfn);
    gtext_to_editor(&en->ex[0], e);
    ed_display(e, disp, sizeof disp);
    if (gx.mode == GM_PAR) {
        char d2[ED_MAX * 4];
        gtext_to_editor(&en->ex[1], e);
        ed_display(e, d2, sizeof d2);
        snprintf(out, n, "X%dT=%.18s  Y%dT=%.18s", tfn + 1, disp, tfn + 1, d2);
    } else {
        snprintf(out, n, "%s=%.40s", gx_entry_label(tfn, 0, lb, sizeof lb), disp);
    }
}

/* Decoration layer (see the file comment): erase the previous decorations,
 * then draw, in this order, the sliders, the integral bound markers, and for
 * the cursor state: TRACE: top band (target or CALC prompt + target), CALC
 * bound markers, bottom band (typed value, result, or coordinates), cursor and
 * point label; FREE: coordinates and crosshair; PLAIN: only a pending result.
 * Every decoration registers its rectangle for the next clear. */
static void draw_overlays(void)
{
    clear_overlays();
    char buf[112];
    int nsl = slider_rows();
    if (nsl) {
        slider_draw();
        add_overlay(0, slider_top(), LCD_WIDTH, nsl * SLIDER_H);
    }
    if (calc_shade.on && gx.mode == GM_FUNC) {
        bound_marker(calc_shade.a);
        bound_marker(calc_shade.b);
    }
    const char *prompt = calc_op ? calc_prompt(calc_op, calc_step) : NULL;
    if (vs == VS_TRACE && tfn >= 0) {
        uint16_t col;
        if (prompt) {
            /* "Left bound?  Y1=x^2" on the left, how to answer in grey on the right */
            char lb[64];
            const char *hint = calc_hint(calc_op, calc_step);
            int hw = gfx_text_width(&font_small, hint);
            target_label(lb, sizeof lb, &col);
            if (calc_op == CALC_ISECT && calc_step == 2) /* the guess: name both curves */
                snprintf(lb, sizeof lb, "Y%d and Y%d", calc_fn1 + 1, (int)calc_hi + 1);
            snprintf(buf, sizeof buf, "%s  %.30s", prompt, lb);
            fit_text(&font_medium, buf, LCD_WIDTH - hw - 16);
            draw_band(GY0, buf, C_BLACK);
            gfx_text(&font_small, LCD_WIDTH - hw - 4, GY0 + 2, hint, C_GRAY, GFX_TRANSPARENT);
            if (calc_step >= 1 && calc_op != CALC_ISECT)
                bound_marker(calc_lo);
            if (calc_step >= 2 && calc_op != CALC_ISECT)
                bound_marker(calc_hi);
        } else {
            target_label(buf, sizeof buf, &col);
            draw_band(GY0, buf, col);
        }
        if (num_active) {
            char disp[ED_MAX * 4];
            ed_display(&num_ed, disp, sizeof disp);
            snprintf(buf, sizeof buf, "%s=%.100s", gx.mode == GM_FUNC ? "X" : gx_indep_name(), disp);
            draw_band(UI_BOTTOM - BAND_H, buf, C_BLUE);
        } else if (result[0]) {
            draw_band(UI_BOTTOM - BAND_H, result, C_BLACK);
        } else {
            coord_text(buf, sizeof buf);
            draw_band(UI_BOTTOM - BAND_H, buf, C_BLACK);
        }
        if (isfinite(tx) && isfinite(ty)) {
            double px = x_to_px(tx), py = y_to_py(ty);
            if (px >= 0 && px < GW && py >= 0 && py < GH) {
                draw_cross(GX0 + (int)lround(px), GY0 + (int)lround(py), C_BLACK);
                if (label[0] && !prompt)
                    draw_label(GX0 + (int)lround(px), GY0 + (int)lround(py), label, col);
            }
        }
    } else if (vs == VS_FREE) {
        tx = px_to_x(fx), ty = py_to_y(fy);
        char a[32], b[32];
        num_text("X", tx, a, sizeof a);
        num_text("Y", ty, b, sizeof b);
        if (gx.mode == GM_IMPL || gx.mode == GM_DE) {
            /* value of the first relation's left side minus right side; DE: the first slope */
            for (int i = 0; i < GNENT; i++)
                if (gx_plottable(i)) {
                    char c[32];
                    if (gx.mode == GM_DE)
                        slope_text(i, tx, ty, c, sizeof c);
                    else
                        num_text("F", gx_impl(i, tx, ty), c, sizeof c);
                    snprintf(buf, sizeof buf, "%s  %s %s", a, b, c);
                    goto band;
                }
        }
        snprintf(buf, sizeof buf, "%s   %s", a, b);
    band:
        draw_band(UI_BOTTOM - BAND_H, buf, C_BLACK);
        draw_cross(GX0 + fx, GY0 + fy, C_BLACK);
    } else if (result[0]) {
        draw_band(UI_BOTTOM - BAND_H, result, C_BLACK);
    }
    int ar[4]; /* the animation's play bar (modules/animation), if playing or paused */
    if (anim_overlay(ANIM_SCR_2D, ar))
        add_overlay(ar[0], ar[1], ar[2], ar[3]);
}

/* the coordinates as a label "(x, y)", or "what (x, y)" (e.g. "zero (2, 0)") */
static void set_coord_label(const char *what)
{
    char a[16], b[16];
    fmt_num(tx, 8, a, sizeof a);
    fmt_num(ty, 8, b, sizeof b);
    if (what)
        snprintf(label, sizeof label, "%s (%s, %s)", what, a, b);
    else
        snprintf(label, sizeof label, "(%s, %s)", a, b);
}

/* screen draw(): the plot from the cache when it is valid (cheap: menus and
 * messages return here often), else a full render; then the decorations. The
 * whole plot area was just restored, so no decoration rectangles are pending. */
static void graph_draw(void)
{
    if (!g.graph_valid || !g.graph_cache)
        render();
    else
        gfx_restore_rect(g.graph_cache, GX0, GY0, GW, GH);
    noverlays = 0;
    draw_overlays();
}

/* graph.h: re-plot immediately (slider values changed) and redraw the
 * decorations, following the trace cursor onto the changed curve. */
void graph_redraw_now(void)
{
    render();
    if (vs == VS_TRACE)
        trace_update();
    draw_overlays();
}

/* anim.h: one animation frame. The animated value (slider parameter or
 * comet fraction) has been set: a CALC result that is shown is computed
 * again for the moved curve (same operation, curve and bounds), else a
 * TRACE cursor follows its curve at the same parameter; then re-plot and
 * redraw the decorations (with the play bar). */
void graph_anim_frame(void)
{
    if (lastc.op && result[0] && vs == VS_TRACE && !calc_op)
        calc_run(lastc.op, lastc.fn, lastc.fn2, lastc.lo, lastc.hi, lastc.guess);
    else if (vs == VS_TRACE)
        trace_update();
    render();
    draw_overlays();
}

/* Shift the window by a fraction of its width/height (positive = towards
 * larger x / y). Invalidates the plot; the caller renders. */
static void pan(double frac_x, double frac_y)
{
    double dx = (g.xmax - g.xmin) * frac_x, dy = (g.ymax - g.ymin) * frac_y;
    g.xmin += dx, g.xmax += dx, g.ymin += dy, g.ymax += dy;
    g.graph_valid = false;
}

/* keep the trace point visible: pan to centre it when it leaves the plot. In
 * FUNC mode only x is followed (a function trace keeps its y range, like a
 * graphing calculator; the cursor may go off the top or bottom); curves,
 * sequences and overlay points are followed in both directions. */
static void follow_trace(void)
{
    if (!isfinite(tx) || !isfinite(ty))
        return;
    double px = x_to_px(tx), py = y_to_py(ty);
    if (px < 0 || px >= GW || ((gx.mode != GM_FUNC || tfn >= NTE) && (py < 0 || py >= GH))) {
        bool fy_ok = gx.mode == GM_FUNC && tfn < NTE;
        pan((tx - (g.xmin + g.xmax) / 2) / (g.xmax - g.xmin),
            fy_ok ? 0 : (ty - (g.ymin + g.ymax) / 2) / (g.ymax - g.ymin));
        render();
    }
}

/* the first plottable entry of the current mode (GNENT if none) */
static int next_plottable(void)
{
    int i = 0;
    while (i < GNENT && !gx_plottable(i))
        i++;
    return i;
}

/* DE: add a solution curve through (x, y) for the traced curve's entry (else the first one) and
 * trace it from that point, labelled; a message in the result band when it cannot be added. */
static void de_add_at(double x, double y)
{
    int e = -1, tp;
    double x0, y0;
    gx_compile();
    if (vs == VS_TRACE)
        gx_de_curve(tfn, &e, &x0, &y0, &tp);
    int k = gx_de_add(e, x, y);
    render();
    if (k < 0) {
        snprintf(result, sizeof result, "%s",
                 gx_plottable(next_plottable()) ? "No room for another curve: remove one (⌫ on it)"
                                                : "Nothing to solve: enter y1'=f(x,y) in y=");
        return;
    }
    vs = VS_TRACE;
    tfn = k;
    tpar = x;
    trace_update();
    set_coord_label(NULL);
}

/* Enter TRACE on the current target (or the first traceable one). With
 * nothing to trace: IMPL mode falls back to the free cursor at the centre,
 * the other modes to the plain graph. Re-entering TRACE keeps tpar. */
static void start_trace(void)
{
    gx_compile();
    if (!target_ok(tfn))
        tfn = next_target(-1, 1);
    if (tfn < 0) { /* (DE without solution curves: the free cursor, ENTER adds one) */
        vs = gx.mode == GM_IMPL || (gx.mode == GM_DE && gx_plottable(next_plottable())) ? VS_FREE : VS_PLAIN;
        if (vs == VS_FREE)
            fx = GW / 2, fy = GH / 2;
        return;
    }
    if (vs != VS_TRACE)
        trace_home();
    vs = VS_TRACE;
    trace_update();
}

/* graph.h: put the trace cursor on entry fn at parameter x (CALC results,
 * long-press snapping) and label it with `what` and the coordinates. A finite
 * y overrides the evaluated one (the exact y of a computed special point). */
void graph_show_point(int fn, double x, double y, const char *what)
{
    vs = VS_TRACE;
    tfn = fn;
    tpar = x;
    trace_update();
    if (isfinite(y))
        ty = y;
    follow_trace();
    set_coord_label(what);
}

/* graph.h: text for the bottom result band (CALC); the point itself is
 * shown with graph_show_point(). */
void graph_set_result(const char *band) { snprintf(result, sizeof result, "%s", band); }

/* graph.h: the WINDOW was changed from outside (ZoomStat, forms): re-plot on
 * the next draw; a free cursor's pixel position no longer means the same
 * point, so it is dropped. */
void graph_view_changed(void)
{
    g.graph_valid = false;
    if (vs == VS_FREE)
        vs = VS_PLAIN;
}

/* ------------------------------------------------------------ CALC */
/* calc_run, remembered for the animation (lastc) */
static void run_calc_run(int op, int fn, int fn2, double lo, double hi, double guess)
{
    lastc.op = op, lastc.fn = fn, lastc.fn2 = fn2, lastc.lo = lo, lastc.hi = hi, lastc.guess = guess;
    calc_run(op, fn, fn2, lo, hi, guess);
}

/* leave CALC input (the result band and label stay) */
static void calc_end(void)
{
    calc_op = 0;
    calc_step = 0;
    num_active = false;
}

/* graph.h: begin CALC operation op (CALC_*), from the CALC menu. Clears the
 * previous result, label and integral shading, switches to the graph and
 * starts TRACE (a message if nothing can be traced). CALC_TABLE jumps straight
 * to TABLE centred on the cursor; CALC_VALUE opens the typed-value editor at
 * once (it asks for x); the other operations wait for ENTER/taps. */
void graph_calc_start(int op)
{
    calc_end();
    result[0] = label[0] = 0;
    if (calc_shade.on) {
        calc_shade.on = false;
        g.graph_valid = false;
    }
    if (ui_current() != &scr_graph)
        ui_switch(&scr_graph);
    start_trace();
    if (vs != VS_TRACE) {
        ui_message("CALC", "Nothing to analyse: switch on a function in Y=.");
        return;
    }
    if (op == CALC_TABLE) {
        table_center_on(tpar, tfn);
        ui_switch(&scr_table);
        return;
    }
    calc_op = op;
    calc_step = 0;
    if (op == CALC_ISECT) {
        /* count the curves: with exactly two there is nothing to choose, go
         * straight to the guess; with one there is nothing to intersect */
        int fa = -1, fb = -1, n = 0;
        for (int i = 0; i < GNENT; i++)
            if (target_ok(i)) {
                if (n == 0)
                    fa = i;
                else if (n == 1)
                    fb = i;
                n++;
            }
        if (n < 2) {
            calc_end();
            ui_message("CALC", "Intersect needs two functions: switch on another one in Y=.");
            return;
        }
        if (n == 2) {
            calc_fn1 = fa;
            calc_hi = fb; /* the second curve (see calc_confirm) */
            calc_step = 2;
            if (tfn != fa && tfn != fb)
                tfn = fa;
            trace_update();
        }
    }
    if (op == CALC_VALUE) {
        num_active = true;
        ed_clear(&num_ed);
    }
    graph_draw();
}

/* confirm the current step at the trace position.
 *   value, dy/dx:   one step, run at once (value then "follows" UP/DOWN);
 *   intersect:      step 0 picks the first curve and moves the cursor to the
 *                   next one, step 1 the second curve (must differ), step 2
 *                   the guess; the search spans the whole window;
 *   others:         left bound, right bound, then (if calc_steps() is 3) a
 *                   guess; with 2 steps the guess is the midpoint. */
static void calc_confirm(void)
{
    double v = tpar;
    int fn = tfn;
    switch (calc_op) {
    case CALC_VALUE:
    case CALC_DYDX:
        run_calc_run(calc_op, fn, -1, v, v, v);
        value_follow = calc_op == CALC_VALUE;
        calc_end();
        return;
    case CALC_ISECT:
        if (calc_step == 0) {
            calc_fn1 = fn;
            calc_step = 1;
            int n = next_target(fn, 1);
            if (n >= 0 && n != fn && n < NTE)
                tfn = n;
            trace_update();
            return;
        }
        if (calc_step == 1) {
            if (fn == calc_fn1) {
                snprintf(result, sizeof result, "Choose a different second curve");
                return;
            }
            calc_step = 2;
            calc_hi = fn; /* the second curve */
            return;
        }
        run_calc_run(CALC_ISECT, calc_fn1, (int)calc_hi, g.xmin, g.xmax, v);
        calc_end();
        return;
    default:
        break;
    }
    int steps = calc_steps(calc_op);
    if (calc_step == 0) {
        calc_lo = v;
        calc_step = 1;
    } else if (calc_step == 1) {
        calc_hi = v;
        calc_step = 2;
        if (steps == 2) {
            run_calc_run(calc_op, fn, -1, calc_lo, calc_hi, (calc_lo + calc_hi) / 2);
            calc_end();
        }
    } else {
        run_calc_run(calc_op, fn, -1, calc_lo, calc_hi, v);
        calc_end();
    }
}

/* move the trace parameter to a typed value: evaluates num_ed (expressions
 * such as 2π work) and closes it. false when empty or on an error (the error
 * goes to the result band). SEQ rounds to an integer n. */
static bool calc_goto_typed(void)
{
    char cas[ED_MAX * 4], err[64];
    double v;
    ed_cas(&num_ed, cas, sizeof cas);
    num_active = false;
    if (!cas[0])
        return false;
    if (eval_number(cas, &v, err, sizeof err)) {
        snprintf(result, sizeof result, "ERR: %.60s", err);
        return false;
    }
    if (gx.mode == GM_SEQ)
        v = round(v);
    tpar = v;
    trace_update();
    follow_trace();
    return true;
}

/* ------------------------------------------------------------ keys */
/* One trace step left (d = -1) or right (+1). FUNC: exactly one pixel column
 * (tpar is snapped to the column's x); stepping past an edge pans by a quarter
 * screen and re-plots. Overlay sets: the next point index. PAR/POL/SEQ: one
 * Tstep/θstep/1, clamped to the parameter range, then follow_trace pans. */
static void trace_move(int d)
{
    if (gx.mode == GM_FUNC && tfn < NTE) {
        int px = (int)lround(x_to_px(tpar)) + d;
        if (px < 0 || px >= GW) {
            pan(d * 0.25, 0);
            px -= d * (GW / 4);
            render();
        }
        tpar = px_to_x(px);
    } else if (tfn >= NTE) {
        tpar += d;
    } else {
        double st = trace_step(), lo = -INFINITY, hi = INFINITY;
        if (gx.mode == GM_PAR)
            lo = gx.tmin, hi = gx.tmax;
        else if (gx.mode == GM_POL)
            lo = gx.thmin, hi = gx.thmax;
        else if (gx.mode == GM_SEQ)
            lo = gx.nmin;
        tpar += d * st;
        if (tpar < lo)
            tpar = lo;
        if (tpar > hi)
            tpar = hi;
    }
    trace_update();
    follow_trace();
}

/* screen key(). While a value is being typed (num_active) the keys go to
 * num_ed: ENTER moves the cursor there (and confirms the CALC step), CLEAR
 * clears it or, when empty, cancels; UP/DOWN still switch curves. Otherwise:
 *   F4 TRACE, LEFT/RIGHT/UP/DOWN move the cursor or switch the trace target,
 *   ENTER confirms a CALC step / re-centres on the trace point / opens the
 *   selected slider's settings, + - step the selected slider, `,` selects the
 *   next slider, CLEAR steps back (see the file comment), digits . (-) π start
 *   typing a trace value.
 * Any key except the slider keys clears the result band unless the action
 * produced a new one. Redraws the decorations (render() is called directly by
 * the actions that change the view). */
static void graph_key(int key, int mod)
{
    bool keep_result = false;
    if (num_active) {
        if (key == KEY_ENTER) {
            bool ok = ed_empty(&num_ed) ? (num_active = false, true) : calc_goto_typed();
            if (ok && calc_op)
                calc_confirm();
            draw_overlays();
            return;
        }
        if (key == KEY_CLEAR) {
            if (ed_empty(&num_ed))
                num_active = false, calc_end();
            else
                ed_clear(&num_ed);
            draw_overlays();
            return;
        }
        if (key != KEY_UP && key != KEY_DOWN) {
            if (ui_editor_key(&num_ed, key, mod))
                draw_overlays();
            return;
        }
    }
    switch (key) {
    case KEY_F4: /* TRACE (soft key) */
        calc_end();
        start_trace();
        label[0] = 0;
        if (vs == VS_PLAIN) { /* say why nothing happened */
            snprintf(result, sizeof result, "Nothing to trace: switch on an entry in y=");
            keep_result = true;
        }
        ui_draw_status();
        break;
    case KEY_LEFT:
    case KEY_RIGHT: {
        int d = key == KEY_LEFT ? -1 : 1;
        label[0] = 0;
        if (vs == VS_TRACE) {
            trace_move(d);
        } else {
            if (vs == VS_PLAIN)
                vs = VS_FREE, fx = GW / 2, fy = GH / 2;
            else
                fx += d;
            if (fx < 0) fx = 0;
            if (fx >= GW) fx = GW - 1;
        }
        break;
    }
    case KEY_UP:
    case KEY_DOWN: {
        int d = key == KEY_UP ? -1 : 1;
        label[0] = 0;
        if (vs == VS_TRACE) {
            int n = next_target(tfn, d); /* DOWN = next function, UP = previous */
            if (n >= 0) {
                /* crossing between entries and overlay sets: tpar means something
                 * else there (point index vs. x, t, θ or n), so restart it where
                 * TRACE starts on the new target */
                bool cross = (n >= NTE) != (tfn >= NTE);
                tfn = n;
                if (cross || gx.mode == GM_DE) /* (DE: from the new curve's initial point) */
                    trace_home();
            }
            trace_update();
            if (value_follow && result[0] && tfn < NTE)
                run_calc_run(CALC_VALUE, tfn, -1, tpar, tpar, tpar), keep_result = true;
        } else {
            if (vs == VS_PLAIN)
                vs = VS_FREE, fx = GW / 2, fy = GH / 2;
            else
                fy += d;
            if (fy < 0) fy = 0;
            if (fy >= GH) fy = GH - 1;
        }
        break;
    }
    case KEY_ENTER:
        if (vs == VS_TRACE && calc_op) {
            calc_confirm();
            keep_result = result[0] != 0;
        } else if (vs == VS_FREE && gx.mode == GM_DE) { /* a solution curve through the cursor */
            de_add_at(px_to_x(fx), py_to_y(fy));
            keep_result = result[0] != 0;
        } else if (vs == VS_TRACE && isfinite(ty)) { /* re-centre on the trace cursor */
            pan((tx - (g.xmin + g.xmax) / 2) / (g.xmax - g.xmin), (ty - (g.ymin + g.ymax) / 2) / (g.ymax - g.ymin));
            render();
            if (gx.mode == GM_FUNC && tfn < NTE)
                tpar = px_to_x(GW / 2);
            trace_update();
        } else if (vs != VS_TRACE && slider_rows()) {
            slider_open_settings(gx.sel_param);
            return;
        }
        break;
    case KEY_ADD:
    case KEY_SUB:
        if (!slider_rows())
            return;
        slider_step(key == KEY_ADD ? 1 : -1);
        render();
        if (vs == VS_TRACE)
            trace_update();
        break;
    case KEY_COMMA:
        if (!slider_rows())
            return;
        slider_select_next();
        break;
    case KEY_DEL: { /* DE: remove the traced solution curve if it was added on the graph */
        int e, tp;
        double x0, y0;
        if (gx.mode != GM_DE || vs != VS_TRACE || !gx_de_curve(tfn, &e, &x0, &y0, &tp) || tp < 0)
            return;
        gx_de_remove_tap(tp);
        render();
        label[0] = 0;
        if (!target_ok(tfn))
            tfn = next_target(-1, 1);
        if (tfn < 0)
            vs = VS_PLAIN;
        else
            trace_home(), trace_update();
        ui_draw_status();
        break;
    }
    case KEY_CLEAR:
        if (calc_shade.on) {
            calc_shade.on = false;
            g.graph_valid = false;
            render();
        }
        if (calc_op || result[0] || label[0]) {
            calc_end();
            result[0] = label[0] = 0;
            break;
        }
        if (vs != VS_PLAIN) {
            vs = VS_PLAIN;
            ui_draw_status();
            break;
        }
        ui_switch(ui_home());
        return;
    default: {
        /* typing a number moves the trace cursor there */
        char t[16];
        if (vs == VS_TRACE && tfn < NTE && mod != MOD_ALPHA && ui_key_text(key, mod, 0, t, sizeof t) &&
            (strchr("0123456789.", t[0]) || !strcmp(t, "⁻") || !strcmp(t, "π"))) {
            num_active = true;
            ed_clear(&num_ed);
            ed_insert_text(&num_ed, t, NULL);
            break;
        }
        return;
    }
    }
    if (!keep_result && key != KEY_ADD && key != KEY_SUB && key != KEY_COMMA)
        result[0] = 0, value_follow = false;
    draw_overlays();
}

/* screen enter(). Resets the trace/CALC state when the graph mode (or the SEQ
 * plot type) changed since the last visit; drops a free cursor; on a fresh
 * visit (not returning from a menu) picks up slider parameters changed in the
 * CAS elsewhere (e.g. `a:=3` on the home screen). */
static void graph_enter(void)
{
    static int last_mode = -1;
    if (!ui_returning)
        home_graph_enter(false); /* (graphing from the home screen: the graph app on a new cell) */
    if (gx.mode * 2 + gx.seqplot != last_mode) { /* another graph mode: nothing to keep tracing */
        last_mode = gx.mode * 2 + gx.seqplot;
        vs = VS_PLAIN;
        tfn = -1;
        calc_end();
        result[0] = label[0] = 0;
        calc_shade.on = false;
        lastc.op = 0;
    }
    if (vs == VS_FREE)
        vs = VS_PLAIN;
    if (vs == VS_TRACE && !ui_returning) { /* the entries may have changed meanwhile (DE: the curves) */
        gx_compile();
        if (target_ok(tfn))
            trace_update();
        else
            vs = VS_PLAIN;
    }
    if (!ui_returning) {
        if (gx_params_sync())
            g.graph_valid = false;
        drag_slider = -1;
    }
    if (calc_op && vs != VS_TRACE)
        calc_end();
}

/* menus (MATH, CATALOG, ...) insert into the typed trace value, if any */
static editor_t *graph_editor(void) { return num_active ? &num_ed : NULL; }

/* ------------------------------------------------------------ touch */
/* One finger drags the graph (live preview shifted from the cache, re-plotted
 * on release); two fingers pinch-zoom about the point between them; a tap puts
 * the TRACE cursor on the curve nearest to the tap and labels the point; a long
 * press snaps to the nearest special point (zero, extremum, intersection,
 * y-intercept). Sliders: drag the knob, tap the name for its settings.
 *
 * Live preview while a finger moves: the cached plot is resampled into the
 * framebuffer (nearest neighbour, no evaluation), so dragging stays smooth
 * however expensive the plot is. For destination pixel (x, y) (plot-relative)
 * the source pixel is
 *     s = c0 + (d - c) / scale - (dx, dy)
 * where c0 is the gesture's start centre, c the current centre (screen
 * coordinates minus GX0/GY0): the point that was under the fingers at the
 * start stays under them, magnified by `scale`. A pan is the special case
 * scale = 1, c0 = c (all passed as 0) and a shift by (dx, dy). Pixels coming
 * from outside the cache are filled with the (theme-mapped) background.
 * The decorations are gone after a preview (noverlays = 0); the gesture's end
 * computes the new window and re-plots (TEV_UP / TEV_PINCH_END). Without a
 * cache there is no preview. */
void gr_preview(const uint16_t *src, int stride, int vx, int vy, int w, int h, int dx, int dy, float scale, int cx0,
                int cy0, int cx, int cy)
{
    uint16_t *fb = gfx_framebuffer();
    const uint16_t bg = gfx_map(C_WHITE); /* (theme) */
    float inv = 1.0f / scale;
    int y0 = vy < BODY_Y ? BODY_Y - vy : 0, y1 = vy + h > UI_BOTTOM ? UI_BOTTOM - vy : h;
    for (int y = y0; y < y1; y++) {
        int sy = (int)lroundf((float)(cy0 - vy) + (float)(y - (cy - vy)) * inv) - dy;
        uint16_t *d = fb + (vy + y) * LCD_WIDTH + vx;
        if (sy < 0 || sy >= h) {
            for (int x = 0; x < w; x++)
                d[x] = bg;
            continue;
        }
        const uint16_t *row = src + sy * stride;
        for (int x = 0; x < w; x++) {
            int sx = (int)lroundf((float)(cx0 - vx) + (float)(x - (cx - vx)) * inv) - dx;
            d[x] = (sx < 0 || sx >= w) ? bg : row[sx];
        }
    }
    if (y1 > y0)
        gfx_mark_dirty(vx, vy + y0, w, y1 - y0);
}

static void preview(int dx, int dy, float scale, int cx0, int cy0, int cx, int cy)
{
    if (!g.graph_cache)
        return;
    gr_preview(g.graph_cache + GY0 * LCD_WIDTH + GX0, LCD_WIDTH, GX0, GY0, GW, GH, dx, dy, scale, cx0, cy0, cx, cy);
    noverlays = 0;
}

bool gr_pinch_window(double *xmin, double *xmax, double *ymin, double *ymax, int w, int h, double x0, double y0,
                     double x1, double y1, double s)
{
    if (s < 0.05)
        s = 0.05;
    if (s > 20)
        s = 20;
    /* (X, Y): graph point under the start centre. The new window is 1/s the size and placed so
     * that (X, Y) lands under the final centre, matching what the preview showed. */
    double ww = *xmax - *xmin, hh = *ymax - *ymin;
    double X = *xmin + x0 * ww / (w - 1), Y = *ymax - y0 * hh / (h - 1);
    double nw = ww / s, nh = hh / s;
    if (!(nw > 1e-12 && nh > 1e-12 && nw < 1e12 && nh < 1e12))
        return false; /* refuse degenerate windows */
    *xmin = X - x1 * nw / (w - 1);
    *xmax = *xmin + nw;
    *ymax = Y + y1 * nh / (h - 1);
    *ymin = *ymax - nh;
    return true;
}

/* the window changed by a gesture: re-plot, keep tracing, drop a free cursor */
static void after_view_change(void)
{
    g.graph_valid = false;
    render();
    if (vs == VS_TRACE)
        trace_update();
    else if (vs == VS_FREE)
        vs = VS_PLAIN;
    draw_overlays();
}

/* nearest trace target to a screen point; sets tfn/tpar.
 * (px, py) are plot-relative pixels. FUNC entries are measured vertically at
 * the tapped column (tpar = that column's x). Curves, sequences and overlay
 * sets are sampled along their parameter (Tstep, θstep, every visible n, every
 * point; skipped above 20000 samples) and the sample closest in pixels wins;
 * tfn/tpar are borrowed for trace_update() and restored between candidates.
 * keep_fn: only consider the current target (CALC: move along it). A target
 * is picked only within maxd pixels of the tap (INFINITY: no limit). false if
 * nothing is traceable that close (tfn/tpar unchanged). */
#define PICK_DIST 24 /* px: a plain tap farther from every curve puts the free cursor there */
/* DE solution curves: the point of a curve nearest to the tap (gx_de_walk callback) */
struct de_pick {
    double px, py, d, x;
};
static void de_pick_cb(double x, double y, bool first, void *u)
{
    (void)first;
    struct de_pick *p = u;
    double d = hypot(x_to_px(x) - p->px, y_to_py(y) - p->py);
    if (d < p->d)
        p->d = d, p->x = x;
}
static bool pick_at(int px, int py, bool keep_fn, double maxd)
{
    gx_compile();
    int best = -1;
    double bestd = INFINITY, bestp = 0;
    int n = NTE + ov_nsets();
    for (int i = 0; i < n; i++) {
        if (!target_ok(i) || (keep_fn && i != tfn))
            continue;
        if (i < NTE && gx.mode == GM_DE) { /* solution curves: the nearest of their points */
            struct de_pick dp = {px, py, INFINITY, 0};
            gx_de_walk(i, de_pick_cb, &dp);
            if (dp.d < bestd)
                bestd = dp.d, best = i, bestp = dp.x;
            continue;
        }
        if (i < NTE && gx.mode == GM_FUNC) {
            double v = gx_f(i, px_to_x(px));
            if (!isfinite(v))
                continue;
            double d = fabs(y_to_py(v) - py);
            if (d < bestd)
                bestd = d, best = i, bestp = px_to_x(px);
            continue;
        }
        /* point sets and curves: nearest sample */
        double save_p = tpar;
        int save_f = tfn;
        tfn = i;
        double lo, hi, st;
        if (i >= NTE) {
            int set;
            const graph_overlay_t *o = ov_set(i - NTE, &set);
            lo = 0, hi = o && o->npoints ? o->npoints(set) - 1 : -1, st = 1;
        } else if (gx.mode == GM_PAR) {
            lo = gx.tmin, hi = gx.tmax, st = gx.tstep;
        } else if (gx.mode == GM_POL) {
            lo = gx.thmin, hi = gx.thmax, st = gx.thstep;
        } else {
            lo = fmax(gx.nmin, ceil(g.xmin)), hi = fmin(gx.nmax, floor(g.xmax)), st = 1;
        }
        if (st > 0 && hi >= lo && (hi - lo) / st < 20000)
            for (double p = lo; p <= hi + st * 1e-9; p += st) {
                tpar = p;
                trace_update();
                if (!isfinite(tx) || !isfinite(ty))
                    continue;
                double d = hypot(x_to_px(tx) - px, y_to_py(ty) - py);
                if (d < bestd)
                    bestd = d, best = i, bestp = p;
            }
        tpar = save_p;
        tfn = save_f;
    }
    if (best < 0 || bestd > maxd)
        return false;
    tfn = best;
    tpar = bestp;
    return true;
}

/* Tap at plot-relative (px, py): trace the nearest curve with a coordinate
 * label, or put the free cursor there when nothing is plotted. During CALC the
 * tap confirms the current step at the tapped point instead (for the first two
 * intersect steps it chooses the curve). */
static void trace_at(int px, int py)
{
    if (calc_op && vs == VS_TRACE) {
        /* during CALC a tap picks the curve (intersect) or sets the point and confirms */
        bool pick_curve = calc_op == CALC_ISECT && calc_step < 2;
        if (pick_at(px, py, !pick_curve, INFINITY)) {
            if (gx.mode == GM_FUNC && tfn < NTE)
                tpar = px_to_x(px);
            trace_update();
            calc_confirm();
        }
        draw_overlays();
        return;
    }
    result[0] = 0;
    if (!pick_at(px, py, false, PICK_DIST)) { /* nothing plotted near the tap: free cursor there */
        vs = VS_FREE;
        fx = px, fy = py;
        label[0] = 0;
    } else {
        vs = VS_TRACE;
        trace_update();
        set_coord_label(NULL);
    }
    ui_draw_status();
    draw_overlays();
}

/* Long press at plot-relative (px, py): in FUNC mode snap to the special point
 * (zero, extremum, intersection, y-intercept; calc_snap in gr_calc.c) within
 * 14 px and label it; in DE mode add a solution curve through the point;
 * otherwise behave like a tap. */
static void snap_at(int px, int py)
{
    int fn;
    double x, y;
    const char *what;
    gx_compile();
    if (gx.mode == GM_FUNC && calc_snap(px, py, 14, &fn, &x, &y, &what)) {
        result[0] = 0;
        graph_show_point(fn, x, y, what);
        ui_draw_status();
        draw_overlays();
        return;
    }
    if (gx.mode == GM_DE && gx_plottable(next_plottable())) {
        de_add_at(px_to_x(px), py_to_y(py));
        ui_draw_status();
        draw_overlays();
        return;
    }
    trace_at(px, py);
}

/* screen touch(): the gesture state machine (core/touch.h delivers the
 * events). A gesture that starts on a slider row belongs to the slider:
 *   DOWN on a track grabs that slider (value follows the finger at once),
 *   DRAG moves it (re-plot per event, no CAS store), UP commits the value to
 *   the CAS variable; a TAP on the track sets and commits the value, a tap on
 *   the name opens the slider settings.
 * Otherwise:
 *   TAP on the bottom band while tracing: TABLE centred on the traced value;
 *   TAP elsewhere: trace_at; DRAG: pan preview; UP after a drag: pan the window
 *   by the drag distance (a drag right moves the view left); UP without
 *   movement is a long press: snap_at; PINCH: zoom preview; PINCH_END: zoom
 *   (scale clamped to 0.05..20); CANCEL (second finger): undo the preview. */
static void graph_touch(const touch_event_t *ev)
{
    int k;
    bool on_track;
    switch (ev->type) {
    case TEV_DOWN:
        drag_slider = -1;
        down_on_slider = slider_hit(ev->x, ev->y, &k, &on_track);
        if (down_on_slider && on_track) {
            drag_slider = k;
            slider_drag_to(k, ev->x);
            graph_redraw_now();
        }
        break;
    case TEV_TAP:
        if (slider_hit(ev->x0, ev->y0, &k, &on_track)) {
            if (on_track) {
                slider_drag_to(k, ev->x0);
                gx_param_commit(k);
                graph_redraw_now();
            } else {
                slider_open_settings(k);
            }
            drag_slider = -1;
            break;
        }
        if (vs == VS_TRACE && !calc_op && tfn < NTE && gx.mode != GM_DE && ev->y0 >= UI_BOTTOM - BAND_H) {
            /* tap on the coordinates: table centred on the traced value */
            table_center_on(tpar, tfn);
            ui_switch(&scr_table);
            break;
        }
        if (ev->y >= GY0)
            trace_at(ev->x - GX0, ev->y - GY0);
        break;
    case TEV_DRAG:
        if (drag_slider >= 0) {
            slider_drag_to(drag_slider, ev->x);
            graph_redraw_now();
            break;
        }
        if (down_on_slider)
            break;
        preview(ev->x - ev->x0, ev->y - ev->y0, 1.0f, 0, 0, 0, 0);
        break;
    case TEV_UP: {
        if (drag_slider >= 0) {
            gx_param_commit(drag_slider);
            drag_slider = -1;
            graph_redraw_now();
            break;
        }
        if (down_on_slider)
            break;
        int dx = ev->x - ev->x0, dy = ev->y - ev->y0;
        if (!dx && !dy) { /* long press */
            if (ev->y0 >= GY0)
                snap_at(ev->x0 - GX0, ev->y0 - GY0);
            break;
        }
        pan(-dx / (double)(GW - 1), dy / (double)(GH - 1));
        after_view_change();
        break;
    }
    case TEV_CANCEL:
        drag_slider = -1;
        graph_draw();
        break;
    case TEV_PINCH:
        drag_slider = -1;
        preview(0, 0, ev->scale, ev->x0, ev->y0, ev->x, ev->y);
        break;
    case TEV_PINCH_END: {
        if (!gr_pinch_window(&g.xmin, &g.xmax, &g.ymin, &g.ymax, GW, GH, ev->x0 - GX0, ev->y0 - GY0, ev->x - GX0,
                             ev->y - GY0, ev->scale))
            break;
        after_view_change();
        break;
    }
    default:
        break;
    }
}

/* the animation runs from the screen's tick (frame timer in the module) */
static void graph_tick(uint32_t now) { anim_tick(now, ANIM_SCR_2D); }
/* the shared graph bar, with F5 "anim" here (anim_softkeys) */
static const softkeys_t *graph_softkeys(void) { return anim_softkeys(&softkeys_graph, ANIM_SCR_2D); }

const screen_t scr_graph = {"graph", "GRAPH", graph_enter, graph_draw, graph_key, graph_tick, graph_editor,
                            graph_touch, "graph", &softkeys_graph, graph_softkeys};

/* ------------------------------------------------------------ zoom */
/* Grow bounding box b = {xmin, xmax, ymin, ymax} to include (x, y); points
 * with a non-finite coordinate are ignored. */
static void fit_extend(double x, double y, double *b)
{
    if (!isfinite(x) || !isfinite(y))
        return;
    if (x < b[0]) b[0] = x;
    if (x > b[1]) b[1] = x;
    if (y < b[2]) b[2] = y;
    if (y > b[3]) b[3] = y;
}

/* gx_seq_run callback for ZoomFit in SEQ mode: (n, u(n)) for time plots, the
 * values on both axes for web plots. */
static void seq_fit_cb(long n, const double *u, void *user)
{
    for (int i = 0; i < GNENT; i++)
        if (gx_plottable(i))
            fit_extend(gx.seqplot == SEQ_WEB ? u[i] : (double)n, u[i], user);
}

/* ui.h: ZOOM menu action (numbers match the ZOOM menu items):
 *   1 In / 2 Out: halve / double both ranges about the trace point, the free
 *     cursor, or the window centre;
 *   3 ZStandard: [-10, 10] x [-10, 10];
 *   4 ZDecimal: exactly 0.1 per pixel, origin at the plot centre;
 *   5 ZSquare: the larger of the two pixel sizes on both axes, so circles are
 *     round, about the window centre;
 *   6 ZTrig: x in ±47π/24 (±352.5°) with Xscl = π/2 (90°), y in [-4, 4];
 *   7 ZoomFit: FUNC fits y over the visible x (one sample per pixel column);
 *     PAR/POL fit both ranges over the t/θ range; SEQ over n in
 *     [max(nMin, PlotStart), nMax] (web plots: square, including 0); active
 *     overlays' bounds are included; 5 % margins, degenerate ranges widened
 *     by ±1.
 * Then invalidates the plot (the caller redraws) and keeps a FUNC trace
 * cursor on screen. Unknown actions do nothing. */
void graph_zoom(int action)
{
    double cx = (g.xmin + g.xmax) / 2, cy = (g.ymin + g.ymax) / 2;
    double w = g.xmax - g.xmin, h = g.ymax - g.ymin;
    if (vs == VS_TRACE && isfinite(ty))
        cx = tx, cy = ty;
    else if (vs == VS_FREE)
        cx = px_to_x(fx), cy = py_to_y(fy);
    switch (action) {
    case 1: /* in */
        w /= 2, h /= 2;
        break;
    case 2: /* out */
        w *= 2, h *= 2;
        break;
    case 3: /* standard */
        g.xmin = -10, g.xmax = 10, g.ymin = -10, g.ymax = 10, g.xscl = 1, g.yscl = 1;
        goto done;
    case 4: /* decimal: 0.1 per pixel */
        g.xmin = -0.1 * (GW / 2), g.xmax = g.xmin + 0.1 * (GW - 1);
        g.ymin = -0.1 * (GH / 2), g.ymax = g.ymin + 0.1 * (GH - 1);
        g.xscl = 1, g.yscl = 1;
        goto done;
    case 5: { /* square pixels */
        double dx = w / (GW - 1), dy = h / (GH - 1), d = dx > dy ? dx : dy;
        cx = (g.xmin + g.xmax) / 2, cy = (g.ymin + g.ymax) / 2;
        w = d * (GW - 1), h = d * (GH - 1);
        break;
    }
    case 6: /* trig */
        if (g.cs.angle == CAS_ANGLE_DEG)
            g.xmin = -352.5, g.xmax = 352.5, g.xscl = 90;
        else
            g.xmin = -47.0 * M_PI / 24, g.xmax = 47.0 * M_PI / 24, g.xscl = M_PI / 2;
        g.ymin = -4, g.ymax = 4, g.yscl = 1;
        goto done;
    case 7: { /* fit: y range of the functions (FUNC), both ranges for curves/sequences/overlays */
        gx_compile();
        double b[4] = {INFINITY, -INFINITY, INFINITY, -INFINITY};
        bool fitx = true;
        if (gx.mode == GM_FUNC) {
            fitx = false;
            for (int i = 0; i < NFUNC; i++)
                if (gx_plottable(i))
                    for (int px = 0; px < GW; px++)
                        fit_extend(px_to_x(px), gx_f(i, px_to_x(px)), b);
        } else if (gx.mode == GM_PAR || gx.mode == GM_POL) {
            double t0 = gx.mode == GM_PAR ? gx.tmin : gx.thmin, t1 = gx.mode == GM_PAR ? gx.tmax : gx.thmax;
            double st = gx.mode == GM_PAR ? gx.tstep : gx.thstep;
            for (int i = 0; i < GNENT; i++)
                if (gx_plottable(i) && st > 0 && (t1 - t0) / st < 20000)
                    for (double t = t0; t <= t1 + st * 1e-9; t += st) {
                        double x, y;
                        if (gx_par(i, t, &x, &y))
                            fit_extend(x, y, b);
                    }
        } else if (gx.mode == GM_SEQ) {
            gx_seq_run((long)fmax(gx.nmin, gx.plotstart), (long)gx.nmax, seq_fit_cb, b);
            if (gx.seqplot == SEQ_WEB) { /* square: both axes over the values (and 0) */
                fit_extend(0, 0, b);
                b[0] = b[2] = fmin(b[0], b[2]);
                b[1] = b[3] = fmax(b[1], b[3]);
            }
        } else {
            fitx = false;
        }
        double o[4];
        if (gr_overlays_bounds(&o[0], &o[1], &o[2], &o[3])) {
            fit_extend(o[0], o[2], b);
            fit_extend(o[1], o[3], b);
            fitx = true;
        }
        if (fitx && isfinite(b[0]) && isfinite(b[1])) {
            if (b[1] - b[0] < 1e-9)
                b[0] -= 1, b[1] += 1;
            double m = (b[1] - b[0]) * 0.05;
            g.xmin = b[0] - m, g.xmax = b[1] + m;
        }
        if (isfinite(b[2]) && isfinite(b[3])) {
            if (b[3] - b[2] < 1e-9)
                b[2] -= 1, b[3] += 1;
            double m = (b[3] - b[2]) * 0.05;
            g.ymin = b[2] - m, g.ymax = b[3] + m;
        }
        goto done;
    }
    default:
        return;
    }
    g.xmin = cx - w / 2, g.xmax = cx + w / 2;
    g.ymin = cy - h / 2, g.ymax = cy + h / 2;
done:
    g.graph_valid = false;
    if (vs == VS_TRACE && gx.mode == GM_FUNC && tfn < NTE) {
        double px = x_to_px(tpar);
        if (px < 0 || px >= GW)
            tpar = px_to_x(GW / 2);
    }
    if (vs == VS_TRACE)
        trace_update();
    if (vs == VS_FREE)
        vs = VS_PLAIN;
}

/* ui.h: console `state` keys of the graph screen (UI tests): cursor state,
 * graph mode, plotted count, error, render time, trace/cursor coordinates,
 * label, result, CALC prompt, the Y= state and the shown sliders' values. */
void graph_state(void (*kv)(const char *, const char *, void *), void *user)
{
    static const char *const names[] = {"plain", "free", "trace"};
    char b[112];
    kv("graph_mode", names[vs], user);
    kv("graph_type", gmode_names[gx.mode], user);
    snprintf(b, sizeof b, "%d", nplotted);
    kv("graph_plotted", b, user);
    kv("graph_error", errline, user);
    snprintf(b, sizeof b, "%lu", (unsigned long)render_us);
    kv("graph_render_us", b, user);
    if (vs == VS_TRACE) {
        snprintf(b, sizeof b, "%d", tfn + 1);
        kv("trace_fn", b, user);
        snprintf(b, sizeof b, "%.10g", tx);
        kv("trace_x", b, user);
        snprintf(b, sizeof b, "%.10g", ty);
        kv("trace_y", b, user);
        snprintf(b, sizeof b, "%.10g", tpar);
        kv("trace_t", b, user);
    }
    if (vs == VS_FREE) {
        snprintf(b, sizeof b, "%.6g", px_to_x(fx));
        kv("cursor_x", b, user);
        snprintf(b, sizeof b, "%.6g", py_to_y(fy));
        kv("cursor_y", b, user);
    }
    if (gx.mode == GM_DE) {
        gx_compile();
        snprintf(b, sizeof b, "%d", gx_de_ncurves());
        kv("de_curves", b, user);
        snprintf(b, sizeof b, "%d", gx.ndtap);
        kv("de_added", b, user);
    }
    kv("graph_label", label, user);
    kv("graph_result", result, user);
    const char *p = calc_op ? calc_prompt(calc_op, calc_step) : NULL;
    kv("calc_prompt", p ? p : "", user);
    yeq_state(kv, user);
    int n = gx_nparams_shown();
    snprintf(b, sizeof b, "%d", n);
    kv("sliders", b, user);
    for (int i = 0; i < n; i++) {
        const gparam_t *q = &gx.prm[gx_param_shown(i)];
        char key[16];
        snprintf(key, sizeof key, "slider_%s", q->name);
        snprintf(b, sizeof b, "%.10g", q->val);
        kv(key, b, user);
    }
}
