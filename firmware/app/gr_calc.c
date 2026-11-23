/* CALC menu (SHIFT F4 on the graph screens): graph analysis with numeric methods (gr_num.c).
 * Results are drawn on the graph (point + label, shaded integral) and stored
 * in ans. Also: special points for long-press snapping.
 *
 * Division of labour with app/graph.c: this file owns the menu (menu_calc,
 * also the APPS entry "Graph analysis"), which operations each graph mode
 * allows, the prompts and number of inputs of every operation
 * (calc_prompt / calc_steps) and the computation itself (calc_run). The graph
 * screen runs the interaction: graph_calc_start() starts an operation, the
 * user picks bounds / a guess / curves with the trace cursor, typed values or
 * taps, and graph.c then calls calc_run(op, fn, fn2, lo, hi, guess). Single
 * input operations (value, dy/dx) get lo = hi = guess = the chosen point;
 * intersect gets the second curve in fn2 and the window's x range as bounds.
 *
 * Every result is shown in the result band (graph_set_result), usually with a
 * marked point (graph_show_point), and stored in the CAS `ans` by evaluating
 * the number (calc_store_ans). The integral additionally sets calc_shade:
 * gr_plot_all() (gr_plot.c) shades the area with gr_shade_between() and
 * graph.c draws the bound markers, until graph.c clears it (CLEAR, the next
 * CALC operation, or a change of graph mode).
 *
 * All numerics run in double precision on the compiled Y= functions (gx_f,
 * gx_par), not in the CAS: fast enough for interactive use, accurate to ~1e-10.
 * State: only calc_shade (global, read by graph.c). */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "graph.h"
#include "registry.h"

struct calc_shade calc_shade;

/* ------------------------------------------------------------ functions for the numerics */
/* The `user` argument of the gn_fn callbacks: which Y= function(s) and which
 * derived quantity cfun() computes from them. Minimising kind 1 (-f) finds a
 * maximum; zeros of kind 2 (f - g) are intersections; zeros of kind 3 (f'')
 * are inflection candidates; integrating kind 4 gives the arc length of
 * y = f(x). (Kind 5 is not used by cfun: curves use speed() directly.) */
typedef struct {
    int fn, fn2;
    int kind; /* 0 f, 1 -f, 2 f-g, 3 f'', 4 sqrt(1+f'^2), 5 speed of a curve */
} cf_t;

/* f(x) of the FUNC entry c->fn (base function for the numeric derivatives) */
static double fval(double x, void *u)
{
    cf_t *c = u;
    return gx_f(c->fn, x);
}

/* The quantity selected by c->kind at x (see cf_t); NAN where undefined. */
static double cfun(double x, void *u)
{
    cf_t *c = u;
    switch (c->kind) {
    case 1: return -gx_f(c->fn, x);
    case 2: return gx_f(c->fn, x) - gx_f(c->fn2, x);
    case 3: return gn_deriv(fval, c, x, 2);
    case 4: {
        double d = gn_deriv(fval, c, x, 1);
        return sqrt(1 + d * d);
    }
    default: return gx_f(c->fn, x);
    }
}

/* curves (PAR/POL): x(t), y(t) of entry c->fn (for POL, t is θ and gx_par
 * converts r(θ) to Cartesian coordinates) */
static double cx_t(double t, void *u)
{
    cf_t *c = u;
    double x, y;
    gx_par(c->fn, t, &x, &y);
    return x;
}
static double cy_t(double t, void *u)
{
    cf_t *c = u;
    double x, y;
    gx_par(c->fn, t, &x, &y);
    return y;
}
/* |(x'(t), y'(t))|: the arc-length integrand of a parametric / polar curve,
 * L = ∫ sqrt(x'(t)^2 + y'(t)^2) dt */
static double speed(double t, void *u)
{
    double a = gn_deriv(cx_t, u, t, 1), b = gn_deriv(cy_t, u, t, 1);
    return sqrt(a * a + b * b);
}

/* ------------------------------------------------------------ prompts */
/* Number of user inputs an operation needs before calc_run: 3 for bounded
 * searches (left, right, guess) and intersect (curve, curve, guess), 2 for the
 * integrals (lower, upper), 1 for point operations. */
int calc_steps(int op)
{
    switch (op) {
    case CALC_ZERO: case CALC_MIN: case CALC_MAX: case CALC_INFL: case CALC_ISECT: return 3;
    case CALC_INTEG: case CALC_ARC: return 2;
    default: return 1;
    }
}

/* Prompt shown in the band above the graph for input `step` (0-based) of
 * operation op. (graph.h mentions a NULL return when all inputs are given;
 * this function always returns a string: graph.c ends the operation itself
 * once calc_steps() inputs are confirmed.) */
const char *calc_prompt(int op, int step)
{
    switch (op) {
    case CALC_VALUE: return "Value at?";
    case CALC_DYDX: return "dy/dx at?";
    case CALC_ISECT: return step == 0 ? "First curve?" : step == 1 ? "Second curve?" : "Guess?";
    case CALC_INTEG: return step == 0 ? "Lower limit?" : "Upper limit?";
    case CALC_ARC: return step == 0 ? "From?" : "To?";
    default: return step == 0 ? "Left bound?" : step == 1 ? "Right bound?" : "Guess?";
    }
}

/* How to answer prompt `step` of op, drawn right-aligned in the top band
 * next to calc_prompt() (the prompts alone do not say which keys work):
 * picking a curve takes ▲▼, a point takes ◀▶ or a typed number (value
 * opens the number editor at once). */
const char *calc_hint(int op, int step)
{
    if (op == CALC_ISECT && step < 2)
        return "▲ ▼ curve, enter";
    if (op == CALC_VALUE)
        return "type a number, enter";
    return "◀ ▶ or type, enter";
}

/* Make v the CAS `ans`: evaluating the number with cas_eval (not
 * cas_eval_quiet) updates ans like a home-screen entry. %.17g round-trips
 * every double exactly. Non-finite results leave ans unchanged. */
void calc_store_ans(double v)
{
    if (!isfinite(v))
        return;
    char cmd[40], out[64];
    snprintf(cmd, sizeof cmd, "%.17g", v);
    cas_eval(g.cas, cmd, out, sizeof out);
}

/* result numbers are shown in at most 10 characters (like the trace readout) */
static void fmtv(double v, char *b, size_t n) { fmt_num(v, 10, b, n); }

/* Show a point result: move the trace cursor to (x, y) on curve fn with the
 * label `what`, and print "what  X=..  Y=.." in the result band. */
static void point_result(const char *what, int fn, double x, double y)
{
    char a[24], b[24], band[96];
    fmtv(x, a, sizeof a);
    fmtv(y, b, sizeof b);
    snprintf(band, sizeof band, "%s  X=%s  Y=%s", what, a, b);
    graph_show_point(fn, x, y, what);
    graph_set_result(band);
}

/* show an error message in the result band (nothing is marked or stored) */
static void fail(const char *msg) { graph_set_result(msg); }

/* Run CALC operation op on entry fn of the current graph mode (fn2: second
 * curve for intersect) with the bounds lo..hi (swapped if given in the wrong
 * order) and the guess (point operations use lo). Shows the result on the
 * graph, stores it in ans, and for the integral requests shading and redraws
 * the graph immediately. Mode restrictions are enforced earlier by
 * op_allowed(); CALC_TABLE is handled by graph.c and never reaches here. */
void calc_run(int op, int fn, int fn2, double lo, double hi, double guess)
{
    if (lo > hi) {
        double t = lo;
        lo = hi, hi = t;
    }
    cf_t c = {fn, fn2, 0};
    char a[24], b[24], band[96];
    double x, y;
    int m = gx.mode;
    switch (op) {
    case CALC_VALUE: /* FUNC: Y(x); SEQ: u(n) for the integer part of n; PAR/POL: (x, y) at t / θ */
        if (m == GM_FUNC) {
            y = gx_f(fn, lo);
            point_result("Value", fn, lo, y);
            calc_store_ans(y);
        } else if (m == GM_SEQ) {
            y = gx_seq(fn, (long)lo);
            graph_show_point(fn, lo, y, NULL);
            fmtv(y, b, sizeof b);
            snprintf(band, sizeof band, "u%d(%ld)=%s", fn + 1, (long)lo, b);
            graph_set_result(band);
            calc_store_ans(y);
        } else {
            gx_par(fn, lo, &x, &y);
            graph_show_point(fn, lo, NAN, NULL);
            fmtv(x, a, sizeof a);
            fmtv(y, b, sizeof b);
            snprintf(band, sizeof band, "%s=%g  X=%s  Y=%s", gx_indep_name(), lo, a, b);
            graph_set_result(band);
            calc_store_ans(y);
        }
        return;
    case CALC_ZERO: /* the zero nearest to the guess (96 scan intervals) */
        if (!gn_root_near(cfun, &c, lo, hi, guess, 96, &x))
            return fail("No zero between the bounds: widen them");
        point_result("Zero", fn, x, gx_f(fn, x));
        calc_store_ans(x);
        return;
    case CALC_MIN:
    case CALC_MAX:
        /* the extreme value on [lo, hi] (sampled, then Brent); the guess is not
         * used. A maximum is the minimum of -f (kind 1). */
        c.kind = op == CALC_MAX;
        if (!gn_min(cfun, &c, lo, hi, 96, &x))
            return fail("No value between the bounds: move them");
        point_result(op == CALC_MIN ? "Minimum" : "Maximum", fn, x, gx_f(fn, x));
        calc_store_ans(x);
        return;
    case CALC_ISECT: /* zero of f - g nearest the guess; lo..hi is the window's x range */
        c.kind = 2;
        if (!gn_root_near(cfun, &c, lo, hi, guess, 160, &x))
            return fail("No intersection in the window: zoom out");
        point_result("Intersection", fn, x, gx_f(fn, x));
        calc_store_ans(x);
        return;
    case CALC_DYDX: {
        double d;
        if (m == GM_FUNC) {
            d = gn_deriv(fval, &c, lo, 1);
            graph_show_point(fn, lo, NAN, NULL);
        } else { /* curve: dy/dx = (dy/dt) / (dx/dt) at t (or θ) */
            double dx = gn_deriv(cx_t, &c, lo, 1), dy = gn_deriv(cy_t, &c, lo, 1);
            d = dy / dx;
            graph_show_point(fn, lo, NAN, NULL);
        }
        fmtv(d, a, sizeof a);
        snprintf(band, sizeof band, "dy/dx=%s", a);
        graph_set_result(band);
        calc_store_ans(d);
        return;
    }
    case CALC_INTEG: {
        double err;
        double v = gn_integrate(cfun, &c, lo, hi, 1e-10, &err);
        /* shade the area between f and y = 0 when the graph is re-plotted (the
         * cache is invalidated so gr_plot_all draws the shading) */
        calc_shade = (struct calc_shade){true, fn, -1, lo, hi};
        g.graph_valid = false;
        fmtv(v, a, sizeof a);
        snprintf(band, sizeof band, "∫f(x)dx=%s", a);
        graph_redraw_now();
        graph_set_result(band);
        calc_store_ans(v);
        return;
    }
    case CALC_INFL: {
        c.kind = 3;
        /* sign changes of f'' (where f is defined), nearest to the guess. Unlike
         * gn_root_near there is no pole test and no touching-zero fallback: an
         * inflection point needs f'' to change sign, and a "root" of f'' where
         * f itself is undefined (a pole) is rejected by the isfinite test. */
        int n = 96;
        double best = NAN, bd = INFINITY, h = (hi - lo) / n, x0 = lo, d0 = cfun(lo, &c);
        for (int k = 1; k <= n; k++) {
            double x1 = lo + k * h, d1 = cfun(x1, &c), r;
            if (isfinite(d0) && isfinite(d1) && (d0 > 0) != (d1 > 0) && gn_root(cfun, &c, x0, x1, &r) &&
                isfinite(gx_f(fn, r)) && fabs(r - guess) < bd)
                bd = fabs(r - guess), best = r;
            x0 = x1, d0 = d1;
        }
        if (!isfinite(best))
            return fail("No inflection between the bounds: widen them");
        point_result("Inflection", fn, best, gx_f(fn, best));
        calc_store_ans(best);
        return;
    }
    case CALC_ARC: {
        double err, v;
        if (m == GM_FUNC) { /* L = ∫ sqrt(1 + f'(x)^2) dx */
            c.kind = 4;
            v = gn_integrate(cfun, &c, lo, hi, 1e-9, &err);
        } else {
            v = gn_integrate(speed, &c, lo, hi, 1e-9, &err);
        }
        fmtv(v, a, sizeof a);
        snprintf(band, sizeof band, "Arc length=%s", a);
        graph_set_result(band);
        calc_store_ans(v);
        return;
    }
    }
}

/* ------------------------------------------------------------ special points */
/* Long-press snapping (graph.c touch handler): find the special point of the
 * FUNC curves nearest to a touch point. snap_t is the running best candidate:
 * the touch point in pixels, its distance and what/where it is. */
typedef struct {
    int px, py;
    double bestd;
    int fn;
    double x, y;
    const char *what;
} snap_t;

/* Candidate point (x, y) on curve fn: keep it if it is closer (in screen
 * pixels) to the touch than the best so far. */
static void consider(snap_t *s, int fn, double x, double y, const char *what)
{
    if (!isfinite(x) || !isfinite(y))
        return;
    double d = hypot(gv_x_to_px(x) - s->px, gv_y_to_py(y) - s->py);
    if (!strcmp(what, "y-intercept"))
        d += 0.75; /* a zero or extremum at x = 0 keeps its name */
    if (d < s->bestd)
        s->bestd = d, s->fn = fn, s->x = x, s->y = y, s->what = what;
}

/* Nearest zero, extremum, intersection or y-intercept of the plottable FUNC
 * curves within `radius` pixels of the touch (px, py are plot-relative pixel
 * coordinates). Scans the pixel columns px ± (radius + 2), one sample per
 * column, and refines what it detects between two neighbouring samples:
 *   - zero: sign change of f -> Brent, rejected if |f| is not tiny (pole);
 *   - extremum: the sign of the finite difference changes -> gn_min on the
 *     span since the previous nonzero difference (maximum: minimise -f);
 *   - intersection with every later curve j: sign change of f_i - f_j;
 *   - y-intercept: x = 0 inside the scanned columns.
 * Returns false (outputs untouched) if nothing is within the radius. */
bool calc_snap(int px, int py, int radius, int *fn, double *x, double *y, const char **what)
{
    snap_t s = {px, py, radius + 0.5, -1, 0, 0, NULL};
    int p0 = px - radius - 2, p1 = px + radius + 2; /* columns scanned (a little wider than the radius) */
    for (int i = 0; i < NFUNC; i++) {
        if (!gx_plottable(i))
            continue;
        cf_t c = {i, -1, 0};
        double xa = gv_px_to_x(p0 - 1), fa = gx_f(i, xa), da = NAN, xd = xa;
        for (int p = p0; p <= p1; p++) {
            double xb = gv_px_to_x(p), fb = gx_f(i, xb), r;
            if (isfinite(fa) && isfinite(fb)) {
                if ((fa > 0) != (fb > 0) && gn_root(cfun, &c, xa, xb, &r) && fabs(gx_f(i, r)) < 1e-6 * fmax(1, fabs(fa) + fabs(fb)))
                    consider(&s, i, r, 0, "Zero");
                /* extremum: the slope changes sign (da = last nonzero difference, starting at xd) */
                double db = fb - fa;
                if (db != 0) {
                    if (isfinite(da) && (da > 0) != (db > 0)) {
                        cf_t cm = {i, -1, da > 0};
                        double xm;
                        if (gn_min(cfun, &cm, xd, xb, 8, &xm))
                            consider(&s, i, xm, gx_f(i, xm), da > 0 ? "Maximum" : "Minimum");
                    }
                    da = db, xd = xa;
                }
            } else {
                da = NAN; /* a gap in the curve breaks the slope history */
            }
            for (int j = i + 1; j < NFUNC; j++) {
                if (!gx_plottable(j))
                    continue;
                cf_t ci = {i, j, 2};
                double ga = cfun(xa, &ci), gb = cfun(xb, &ci);
                if (isfinite(ga) && isfinite(gb) && (ga > 0) != (gb > 0) && gn_root(cfun, &ci, xa, xb, &r) &&
                    fabs(cfun(r, &ci)) < 1e-6 * fmax(1, fabs(ga) + fabs(gb)))
                    consider(&s, i, r, gx_f(i, r), "Intersection");
            }
            xa = xb, fa = fb;
        }
        if (gv_px_to_x(p0) <= 0 && gv_px_to_x(p1) >= 0)
            consider(&s, i, 0, gx_f(i, 0), "y-intercept");
    }
    if (s.fn < 0)
        return false;
    *fn = s.fn, *x = s.x, *y = s.y, *what = s.what;
    return true;
}

/* ------------------------------------------------------------ menu */
/* CALCULATE menu; `action` is the CALC_* operation passed to graph_calc_start */
static const menu_item_t calc_items[] = {
    {"value", NULL, NULL, CALC_VALUE, "graph-calc"},
    {"zero", NULL, NULL, CALC_ZERO, "graph-calc"},
    {"minimum", NULL, NULL, CALC_MIN, "graph-calc"},
    {"maximum", NULL, NULL, CALC_MAX, "graph-calc"},
    {"intersect", NULL, NULL, CALC_ISECT, "graph-calc"},
    {"dy/dx", NULL, NULL, CALC_DYDX, "graph-calc"},
    {"∫f(x)dx", NULL, NULL, CALC_INTEG, "graph-calc"},
    {"inflection", NULL, NULL, CALC_INFL, "graph-calc"},
    {"arc length", NULL, NULL, CALC_ARC, "graph-calc"},
    {"table at cursor", NULL, NULL, CALC_TABLE, "graph-calc"},
};

/* Which operations make sense in the current graph mode: all in FUNC; curves
 * (PAR/POL) have value, dy/dx, arc length and table; sequences value and
 * table; implicit relations none. */
static bool op_allowed(int op)
{
    switch (gx.mode) {
    case GM_FUNC: return true;
    case GM_PAR:
    case GM_POL: return op == CALC_VALUE || op == CALC_DYDX || op == CALC_ARC || op == CALC_TABLE;
    case GM_SEQ: return op == CALC_VALUE || op == CALC_TABLE;
    default: return false;
    }
}

/* Menu selection: start the operation on the graph screen, or explain (in a
 * message box over the graph) that the mode does not support it. */
static void calc_select(const menu_item_t *it)
{
    if (!op_allowed(it->action)) {
        static char msg[64];
        snprintf(msg, sizeof msg, "Not available in %s mode.", gmode_names[gx.mode]);
        ui_switch(&scr_graph);
        ui_message("CALC", msg);
        return;
    }
    graph_calc_start(it->action);
}

const menu_def_t menu_calc = {"CALCULATE", calc_items, sizeof calc_items / sizeof calc_items[0], calc_select, false,
                              "graph-calc"};

/* APPS > Graph analysis: show the graph, then the CALCULATE menu over it (the
 * menu returns to the graph, where the operation runs). */
static void calc_open(void)
{
    if (ui_current() != &scr_graph)
        ui_switch(&scr_graph);
    menu_open(&menu_calc);
}

const app_def_t app_graph_calc = {"calc", "Graph analysis", calc_open, {{0}}, "graph-calc"};
