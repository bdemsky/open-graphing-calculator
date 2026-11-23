/* DE graph mode: first-order differential equations y' = f(x, y). Slope fields
 * (direction fields) and solution curves through initial points, integrated
 * with an adaptive fourth-order Runge-Kutta method. */
/*
 * Entries (gr_model.c): ex[0] is f(x, y), compiled in x and y like an IMPL
 * relation (gx_impl evaluates it); ex[1], the IC line, lists initial points
 * "(x0,y0),(x0,y0)" (a bare number y0 means x0 = 0). Points added on the graph
 * (ENTER on the free cursor, a long press) are gx.dtap, saved with the session.
 *
 * Curves. The solution curves are numbered: the typed initial points of the
 * plottable entries in entry order, then the added ones (DE_MAXC at most).
 * They are the trace targets of DE mode (graph.c): TRACE walks along x on the
 * curve, gx_de_solve() integrates from the initial point to the traced x.
 *
 * Slope field: a grid of DE_CELL x DE_CELL pixel cells over the plot; in each
 * cell centre a short segment with the slope f(x, y) as it looks on the
 * screen (the window's x and y scales differ). An infinite slope draws a
 * vertical segment, an undefined one (NAN) nothing. The grid is in screen
 * terms, so panning and zooming keep its density.
 *
 * Integration. x is the independent variable: from (x0, y0) forward (x
 * increasing) and backward. Each step is sized in screen terms: the step h
 * makes the tangent move about DE_PX pixels, so steep parts take small steps
 * in x and flat parts long ones. A step is accepted when the RK4 result is
 * finite, its length on the screen stays below 3 DE_PX and none of the
 * slopes it used (k2, k3, k4, and the slope where it lands) points more than
 * DE_TURN radians away from k1 on the screen, so a step cannot jump across a
 * vertical tangent where the slope changes sign (y' = -x/y at y = 0);
 * otherwise h is halved (at most DE_HALVE times). The solution ends where the
 * slope is undefined, where it becomes vertical on the screen (steeper than
 * DE_STEEP px per px: a vertical tangent such as y' = -x/y at y = 0, or a
 * blow-up such as y' = y^2 + x^2) and, when drawing, at the x
 * edges of the window or more than a window height above or below it while
 * moving away. The step count is bounded too, so a curve costs at most a few
 * thousand slope evaluations.
 */
#include <math.h>
#include "graph.h"

#define DE_CELL 16     /* slope field: cell size (px) */
#define DE_HALF 5.5    /* slope field: half length of a segment (px) */
#define DE_PX 2.0      /* integration: tangent length of a step on the screen (px) */
#define DE_TURN 0.35   /* integration: largest direction change in one step (radians on the screen) */
#define DE_HALVE 12    /* integration: halvings of a rejected step before the solution ends */
#define DE_STEEP 1024  /* integration: a slope this many times steeper than 1 px/px is vertical */
#define DE_STEPS 6000  /* drawing: steps per direction at most */
#define DE_SOLVE 40000 /* gx_de_solve: steps at most */

/* ------------------------------------------------------------ curves */
/* Curve k (see the file comment); false if there is none. Needs compiled entries (gx_compile). */
bool gx_de_curve(int k, int *ent, double *x0, double *y0, int *tap)
{
    if (gx.mode != GM_DE || k < 0 || k >= DE_MAXC)
        return false;
    for (int i = 0; i < GNENT; i++) {
        if (!gx_plottable(i))
            continue;
        int n = gx_de_nic(i);
        if (k < n) {
            *ent = i, *tap = -1;
            gx_de_ic(i, k, x0, y0);
            return true;
        }
        k -= n;
    }
    for (int t = 0; t < gx.ndtap; t++) {
        if (!gx_plottable(gx.dtap[t].ent))
            continue;
        if (k-- == 0) {
            *ent = gx.dtap[t].ent, *tap = t;
            *x0 = gx.dtap[t].x, *y0 = gx.dtap[t].y;
            return true;
        }
    }
    return false;
}

int gx_de_ncurves(void)
{
    int n = 0, e, t;
    double x, y;
    while (gx_de_curve(n, &e, &x, &y, &t))
        n++;
    return n;
}

/* Add a curve through (x, y) for entry ent (not plottable: the first plottable entry). When
 * DE_NTAP curves were added already the oldest one goes. Returns the new curve's number (-1: no
 * plottable entry, or the curve would be beyond DE_MAXC: then it is not kept). */
int gx_de_add(int ent, double x, double y)
{
    if (gx.mode != GM_DE || !isfinite(x) || !isfinite(y))
        return -1;
    if (!gx_plottable(ent))
        for (ent = 0; ent < GNENT && !gx_plottable(ent); ent++)
            ;
    if (ent >= GNENT)
        return -1;
    if (gx.ndtap == DE_NTAP)
        gx_de_remove_tap(0);
    gx.dtap[gx.ndtap].x = x, gx.dtap[gx.ndtap].y = y, gx.dtap[gx.ndtap].ent = (uint8_t)ent;
    gx.ndtap++;
    g.graph_valid = false;
    int e, t;
    double a, b;
    for (int k = 0; gx_de_curve(k, &e, &a, &b, &t); k++)
        if (t == gx.ndtap - 1)
            return k;
    gx_de_remove_tap(gx.ndtap - 1); /* past DE_MAXC: it would not be drawn */
    return -1;
}

void gx_de_remove_tap(int tap)
{
    if (tap < 0 || tap >= gx.ndtap)
        return;
    for (int t = tap; t + 1 < gx.ndtap; t++)
        gx.dtap[t] = gx.dtap[t + 1];
    gx.ndtap--;
    g.graph_valid = false;
}

void gx_de_clear_taps(int ent)
{
    for (int t = gx.ndtap - 1; t >= 0; t--)
        if (ent < 0 || gx.dtap[t].ent == ent)
            gx_de_remove_tap(t);
}

/* ------------------------------------------------------------ integration */
typedef struct {
    int ent;
    double ax, ay; /* pixels per unit of x and of y */
} de_t;

static void de_init(de_t *d, int ent)
{
    d->ent = ent;
    d->ax = (GW - 1) / (g.xmax - g.xmin);
    d->ay = (GH - 1) / (g.ymax - g.ymin);
}

/* One RK4 step from (*x, *y) in direction dir (+1 forward, -1 backward), at most hmax long in x
 * (see the file comment for the step size and acceptance). *k1 gets the slope at the start.
 * false: the solution ends here (nothing moved). */
static bool de_step(const de_t *d, double *x, double *y, int dir, double hmax, double *k1)
{
    double f = gx_impl(d->ent, *x, *y);
    *k1 = f;
    if (!isfinite(f))
        return false;
    double r = d->ay * f / d->ax; /* slope on the screen (px per px) */
    if (fabs(r) > DE_STEEP)
        return false; /* vertical tangent or blow-up */
    double h = DE_PX / (d->ax * sqrt(1 + r * r));
    if (h > hmax)
        h = hmax;
    double a1 = atan(r);
    for (int k = 0; k <= DE_HALVE; k++, h /= 2) {
        double s = dir * h;
        double k2 = gx_impl(d->ent, *x + s / 2, *y + s / 2 * f);
        double k3 = gx_impl(d->ent, *x + s / 2, *y + s / 2 * k2);
        double k4 = gx_impl(d->ent, *x + s, *y + s * k3);
        double y1 = *y + s / 6 * (f + 2 * k2 + 2 * k3 + k4);
        double k5 = gx_impl(d->ent, *x + s, y1); /* the slope where the step lands */
        if (!isfinite(y1) || !isfinite(k5) || hypot(h * d->ax, (y1 - *y) * d->ay) > 3 * DE_PX)
            continue;
        const double ks[4] = {k2, k3, k4, k5};
        bool turned = false;
        for (int j = 0; j < 4; j++)
            turned |= !(fabs(atan(d->ay * ks[j] / d->ax) - a1) <= DE_TURN);
        if (turned)
            continue;
        *x += s;
        *y = y1;
        return true;
    }
    return false;
}

/* Walk from (x, y) in direction dir across the window: cb at the start (first = true) and after
 * every step, until the solution ends or leaves the window (see the file comment). */
static void walk_dir(const de_t *d, double x, double y, int dir, void (*cb)(double, double, bool, void *),
                     void *u)
{
    double hh = g.ymax - g.ymin, ylo = g.ymin - hh, yhi = g.ymax + hh, k;
    cb(x, y, true, u);
    for (int n = 0; n < DE_STEPS; n++) {
        if (dir > 0 ? x >= g.xmax : x <= g.xmin)
            break;
        if (!de_step(d, &x, &y, dir, INFINITY, &k))
            break;
        cb(x, y, false, u);
        if ((y > yhi && dir * k > 0) || (y < ylo && dir * k < 0))
            break;
    }
}

void gx_de_walk(int k, void (*cb)(double x, double y, bool first, void *u), void *u)
{
    int e, t;
    double x0, y0;
    if (!gx_de_curve(k, &e, &x0, &y0, &t))
        return;
    de_t d;
    de_init(&d, e);
    walk_dir(&d, x0, y0, -1, cb, u);
    walk_dir(&d, x0, y0, 1, cb, u);
}

double gx_de_solve(int k, double x)
{
    int e, t;
    double cx, cy, f;
    if (!isfinite(x) || !gx_de_curve(k, &e, &cx, &cy, &t))
        return NAN;
    de_t d;
    de_init(&d, e);
    int dir = x >= cx ? 1 : -1;
    double eps = 1e-9 / d.ax; /* (a billionth of a pixel) */
    for (int n = 0; n < DE_SOLVE; n++) {
        if (dir * (x - cx) <= eps)
            return cy;
        if (!de_step(&d, &cx, &cy, dir, dir * (x - cx), &f))
            return NAN;
    }
    return NAN;
}

/* ------------------------------------------------------------ drawing */
/* slope field of entry i (see the file comment) */
static void field(int i, uint16_t c)
{
    int nc = GW / DE_CELL, nr = GH / DE_CELL;
    if (nc < 2)
        nc = 2;
    if (nr < 2)
        nr = 2;
    double ax = (GW - 1) / (g.xmax - g.xmin), ay = (GH - 1) / (g.ymax - g.ymin);
    for (int r = 0; r < nr; r++)
        for (int q = 0; q < nc; q++) {
            double px = (q + 0.5) * GW / nc, py = (r + 0.5) * GH / nr;
            double f = gx_impl(i, gv_px_to_x(px), gv_py_to_y(py));
            if (isnan(f))
                continue;
            double dx = 0, dy = 1; /* infinite: vertical */
            if (isfinite(f)) {
                double l = hypot(ax, ay * f);
                if (isfinite(l))
                    dx = ax / l, dy = -ay * f / l;
            }
            gr_seg(px - DE_HALF * dx, py - DE_HALF * dy, px + DE_HALF * dx, py + DE_HALF * dy, c, GS_THIN);
        }
}

typedef struct {
    uint16_t c;
    int style, n;
    double px, py; /* the previous point (plot pixels) */
} dplot_t;

/* gx_de_walk callback: join the points (dotted: a dot every third one) */
static void plot_cb(double x, double y, bool first, void *u)
{
    dplot_t *p = u;
    double px = gv_x_to_px(x), py = gv_y_to_py(y);
    if (p->style == GS_DOT) {
        if (p->n++ % 3 == 0 && px > -2 && px < GW + 2 && py > -2 && py < GH + 2)
            gr_dot((int)lround(px), (int)lround(py), p->c, 1);
    } else if (!first) {
        gr_seg(p->px, p->py, px, py, p->c, p->style);
    }
    p->px = px, p->py = py;
}

/* DE mode's plot (gr_plot_all): every plottable entry's slope field, then the solution curves
 * in their entry's colour and style, each with a dot on its initial point. */
void gr_de_plot(void)
{
    for (int i = 0; i < GNENT; i++)
        if (gx_plottable(i))
            field(i, func_colors[gx_color(i)]);
    int e, t;
    double x0, y0;
    for (int k = 0; gx_de_curve(k, &e, &x0, &y0, &t); k++) {
        dplot_t p = {func_colors[gx_color(e)], gx_style(e), 0, 0, 0};
        gx_de_walk(k, plot_cb, &p);
        double px = gv_x_to_px(x0), py = gv_y_to_py(y0);
        if (px > -3 && px < GW + 3 && py > -3 && py < GH + 3)
            gr_dot((int)lround(px), (int)lround(py), p.c, 2);
    }
}
