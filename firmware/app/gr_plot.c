/* Plotting for every graph mode: function curves with styles (line, thick,
 * thin, dotted, shading above/below), parametric and polar curves, sequences
 * (time and web plots), implicit relations (marching squares on a 2 px grid,
 * inequalities shaded). Called by graph.c's render() with the clip set to the
 * plot area. */
/*
 * Coordinates: unless stated otherwise, "pixel" coordinates here are relative
 * to the plot's top-left corner (GX0, GY0) and are converted to and from graph
 * units with the gv_* helpers of graph.h (x grows right, y grows down). Every
 * drawing call adds GX0/GY0.
 *
 * Sampling strategy per mode:
 *   FUNC  one sample per pixel column; consecutive samples are joined, except
 *         across asymptotes (see plot_func).
 *   PAR/POL  t (or θ) from min to max in the WINDOW's step, joined by
 *         segments (at most 20000 steps).
 *   SEQ   time plot: u(n) against n via gx_seq_run (one pass for all
 *         sequences); web plot: y = x, the map u(n-1) -> u(n), and the cobweb.
 *   IMPL  marching squares on an IC x IC pixel grid of f(x, y) = lhs - rhs.
 *   DE    slope fields and RK4 solution curves (gr_de.c).
 *
 * Shading (styles "above"/"below", inequalities, the CALC integral) blends a
 * 30 % tint of the curve colour into the framebuffer directly, so overlapping
 * shadings stay visible. The plot is drawn once into the graph cache by
 * graph.c; nothing here keeps state between renders except the two scratch
 * rows of the implicit plotter.
 */
#include <math.h>
#include <string.h>
#include "anim.h" /* gr_comet */
#include "graph.h"

/* Clamp a pixel y (or x) coordinate to [-GH, 2*GH]: far off-screen points
 * (values near a pole can be ~1e300 px) would overflow the int conversion and
 * the line rasteriser. Clamping keeps the direction of the line on screen. */
#define LIM(v) ((v) < -GH ? -GH : (v) > 2 * GH ? 2 * GH : (v))

/* ------------------------------------------------------------ helpers */
static inline uint16_t blend(uint16_t dst, uint16_t c)
{
    /* 70 % of what is there + 30 % of the colour */
    unsigned r = ((dst >> 11) & 31) * 7 + ((c >> 11) & 31) * 3;
    unsigned gg = ((dst >> 5) & 63) * 7 + ((c >> 5) & 63) * 3;
    unsigned b = (dst & 31) * 7 + (c & 31) * 3;
    return (uint16_t)(((r / 10) << 11) | ((gg / 10) << 5) | (b / 10));
}

/* Tint the pixels of a plot-relative rectangle with colour c (shading). Reads
 * and writes the framebuffer directly (a translucent fill has no gfx_*
 * primitive), so it clips to the plot itself, maps c through the theme's
 * colour map (gfx_map: in dark mode the framebuffer holds mapped colours) and
 * marks the area dirty for the display flush. */
void gr_blend_rect(int x, int y, int w, int h, uint16_t c)
{
    /* graph-relative coordinates, clipped to the plot */
    if (x < 0)
        w += x, x = 0;
    if (y < 0)
        h += y, y = 0;
    if (x + w > GW)
        w = GW - x;
    if (y + h > GH)
        h = GH - y;
    if (w <= 0 || h <= 0)
        return;
    c = gfx_map(c);
    uint16_t *fb = gfx_framebuffer();
    for (int yy = 0; yy < h; yy++) {
        uint16_t *p = fb + (GY0 + y + yy) * LCD_WIDTH + GX0 + x;
        for (int xx = 0; xx < w; xx++)
            p[xx] = blend(p[xx], c);
    }
    gfx_mark_dirty(GX0 + x, GY0 + y, w, h);
}

/* Filled square of (2r+1) x (2r+1) pixels centred on plot pixel (px, py). */
void gr_dot(int px, int py, uint16_t c, int r)
{
    gfx_fill_rect(GX0 + px - r, GY0 + py - r, 2 * r + 1, 2 * r + 1, c);
}

/* line offsets (pixels below the centre line) for the styles: a line is drawn
 * once per offset in [lo, hi], so thin = 1 px, normal = 2 px, thick = 4 px */
static void style_offsets(int style, int *lo, int *hi)
{
    switch (style) {
    case GS_THICK: *lo = -1, *hi = 2; break;
    case GS_THIN: *lo = 0, *hi = 0; break;
    default: *lo = 0, *hi = 1; break;
    }
}

/* Segment between plot pixel coordinates (x0, y0) and (x1, y1) in a line
 * style. Wide styles repeat the line with offsets perpendicular to its main
 * direction: shifted in x for steep lines, in y for flat ones, so the
 * apparent width is the same in every direction. */
void gr_seg(double x0, double y0, double x1, double y1, uint16_t c, int style)
{
    x0 = LIM(x0), x1 = LIM(x1), y0 = LIM(y0), y1 = LIM(y1);
    int lo, hi;
    style_offsets(style, &lo, &hi);
    int ax = (int)lround(x0), ay = (int)lround(y0), bx = (int)lround(x1), by = (int)lround(y1);
    bool steep = fabs(y1 - y0) > fabs(x1 - x0);
    for (int o = lo; o <= hi; o++) {
        if (steep)
            gfx_line(GX0 + ax + o, GY0 + ay, GX0 + bx + o, GY0 + by, c);
        else
            gfx_line(GX0 + ax, GY0 + ay + o, GX0 + bx, GY0 + by + o, c);
    }
}

/* ------------------------------------------------------------ functions */
/* Plot Y(i+1) (FUNC mode): one sample per pixel column, joined to the previous
 * column's sample by a vertical-ish line so steep parts stay connected.
 * Dotted style draws a dot every 4th column instead. Undefined samples (NAN)
 * break the curve; an isolated defined sample after a gap is a single pixel. */
static void plot_func(int i, int style, uint16_t c)
{
    double prev = NAN;
    int lo, hi;
    style_offsets(style, &lo, &hi);
    for (int px = 0; px < GW; px++) {
        double y = gx_f(i, gv_px_to_x(px));
        double py = isfinite(y) ? gv_y_to_py(y) : (double)NAN;
        if (style == GS_DOT) {
            if (isfinite(py) && py > -2 && py < GH + 2 && (px & 3) == 0)
                gr_dot(px, (int)lround(py), c, 1);
            prev = py;
            continue;
        }
        if (isfinite(py) && isfinite(prev)) {
            /* skip near-vertical jumps across the whole window (asymptotes):
             * consecutive samples above the top and below the bottom (e.g.
             * tan(x) around π/2) are on different branches. A steep but
             * continuous curve has at least one sample on screen in between. */
            bool jump = fabs(py - prev) > GH && ((py < 0 && prev > GH) || (py > GH && prev < 0));
            if (!jump) {
                double a = LIM(prev), b = LIM(py);
                for (int o = lo; o <= hi; o++)
                    gfx_line(GX0 + px - 1, GY0 + (int)lround(a) + o, GX0 + px, GY0 + (int)lround(b) + o, c);
                if (style == GS_THICK && fabs(b - a) > 1) /* steep: widen sideways too */
                    for (int o = lo; o <= hi; o++)
                        gfx_line(GX0 + px - 1 + o, GY0 + (int)lround(a), GX0 + px + o, GY0 + (int)lround(b), c);
            }
        } else if (isfinite(py) && py >= 0 && py < GH) {
            gfx_pixel(GX0 + px, GY0 + (int)lround(py), c);
        }
        prev = py;
    }
}

/* Styles "shade above" / "shade below": tint each column from the curve to the
 * top / bottom of the plot (drawn before the curves so they stay on top). */
static void shade_func(int i, int style, uint16_t c)
{
    for (int px = 0; px < GW; px++) {
        double y = gx_f(i, gv_px_to_x(px));
        if (!isfinite(y))
            continue;
        int py = (int)lround(LIM(gv_y_to_py(y)));
        if (style == GS_ABOVE)
            gr_blend_rect(px, 0, 1, py, c);
        else
            gr_blend_rect(px, py + 1, 1, GH - py - 1, c);
    }
}

/* Shade between FUNC curve fa and curve fb (fb < 0: the x axis y = 0) for x in
 * [a, b] (any order), one tinted column per pixel whose centre lies inside
 * the interval. Columns where either curve is undefined are left blank.
 * Used for the CALC integral (calc_shade). */
void gr_shade_between(int fa, int fb, double a, double b, uint16_t c)
{
    if (a > b) {
        double t = a;
        a = b, b = t;
    }
    int p0 = (int)ceil(gv_x_to_px(a)), p1 = (int)floor(gv_x_to_px(b));
    if (p0 < 0)
        p0 = 0;
    if (p1 >= GW)
        p1 = GW - 1;
    double zero = gv_y_to_py(0);
    for (int px = p0; px <= p1; px++) {
        double x = gv_px_to_x(px), y = gx_f(fa, x);
        if (!isfinite(y))
            continue;
        double y2 = fb >= 0 ? gv_y_to_py(gx_f(fb, x)) : zero;
        if (!isfinite(y2))
            continue;
        int u = (int)lround(LIM(gv_y_to_py(y))), v = (int)lround(LIM(y2));
        if (u > v) {
            int t = u;
            u = v, v = t;
        }
        gr_blend_rect(px, u, 1, v - u + 1, c);
    }
}

/* ------------------------------------------------------------ PAR / POL */
/* Parametric (x(t), y(t)) or polar (r(θ), converted by gx_par) curve i: sample
 * the parameter from min to max in steps of the WINDOW's Tstep / θstep (the
 * last sample exactly at max; at most 20000 steps, the step grows if needed)
 * and join consecutive samples. Segments with an end more than 4 plot widths /
 * heights away are skipped: they come from values near a singularity and would
 * otherwise draw long spurious lines across the plot. */
/* Comet (anim.h): the fraction of each curve's parameter range drawn while
 * an animation plays it (PAR/POL: t or θ up to tmin + f (tmax - tmin), with
 * a dot at the head; SEQ: terms up to nMin + f (nMax - nMin)); NAN = all. */
double gr_comet = NAN;

static void plot_curve(int i, int style, uint16_t c)
{
    double t0, t1, dt;
    if (gx.mode == GM_PAR)
        t0 = gx.tmin, t1 = gx.tmax, dt = gx.tstep;
    else
        t0 = gx.thmin, t1 = gx.thmax, dt = gx.thstep;
    if (!(dt > 0) || !(t1 >= t0))
        return;
    bool comet = !isnan(gr_comet);
    if (comet)
        t1 = t0 + gr_comet * (t1 - t0);
    long n = (long)ceil((t1 - t0) / dt);
    if (n > 20000)
        n = 20000, dt = (t1 - t0) / n;
    double px0 = NAN, py0 = NAN;
    for (long k = 0; k <= n; k++) {
        double t = k == n ? t1 : t0 + k * dt, x, y, px = NAN, py = NAN;
        if (gx_par(i, t, &x, &y))
            px = gv_x_to_px(x), py = gv_y_to_py(y);
        if (style == GS_DOT) {
            if (isfinite(px) && px > -2 && px < GW + 2 && py > -2 && py < GH + 2)
                gr_dot((int)lround(px), (int)lround(py), c, 1);
        } else if (isfinite(px) && isfinite(px0) && fabs(px) < 4 * GW && fabs(px0) < 4 * GW && fabs(py) < 4 * GH &&
                   fabs(py0) < 4 * GH) {
            gr_seg(px0, py0, px, py, c, style);
        }
        px0 = px, py0 = py;
    }
    if (comet && isfinite(px0) && px0 > -4 && px0 < GW + 4 && py0 > -4 && py0 < GH + 4)
        gr_dot((int)lround(px0), (int)lround(py0), c, 3); /* the comet's head */
}

/* ------------------------------------------------------------ SEQ */
/* Time plot state: the previous plotted point (plot pixels) of each sequence,
 * NAN when there is none (start, or the previous term was undefined). */
typedef struct {
    double px[GNENT], py[GNENT];
} seqplot_t;

/* gx_seq_run callback for term n of all sequences (u[i] = u_i(n)): plot the
 * point (n, u_i(n)) of every plottable sequence if n is one of the plotted
 * terms (PlotStart, PlotStart + PlotStep, ...). Dot-like styles (dotted and
 * the shading styles, which have no meaning here) draw dots; line styles join
 * to the previous plotted term. */
static void seq_cb(long n, const double *u, void *user)
{
    seqplot_t *s = user;
    if (n < (long)gx.plotstart || ((n - (long)gx.plotstart) % (long)gx.plotstep) != 0)
        return;
    for (int i = 0; i < GNENT; i++) {
        if (!gx_plottable(i))
            continue;
        uint16_t c = func_colors[gx_color(i)];
        double px = gv_x_to_px((double)n), py = isfinite(u[i]) ? gv_y_to_py(u[i]) : (double)NAN;
        int st = gx_style(i);
        if (isfinite(py)) {
            if (st == GS_DOT || st == GS_ABOVE || st == GS_BELOW) {
                if (px > -3 && px < GW + 3 && py > -3 && py < GH + 3)
                    gr_dot((int)lround(px), (int)lround(py), c, 1);
            } else if (isfinite(s->py[i])) {
                gr_seg(s->px[i], s->py[i], px, py, c, st);
            } else {
                gr_dot((int)lround(px), (int)lround(LIM(py)), c, 1);
            }
        }
        s->px[i] = px, s->py[i] = py;
    }
}

/* SEQ time plot: terms n = max(nMin, PlotStart) .. nMax against n on the x
 * axis, computed in one pass for all sequences (sequences may refer to each
 * other's earlier terms). Stops at the right edge of the window. */
static void plot_seq_time(void)
{
    seqplot_t s;
    for (int i = 0; i < GNENT; i++)
        s.px[i] = s.py[i] = NAN;
    long n0 = (long)fmax(gx.nmin, gx.plotstart), n1 = (long)gx.nmax;
    /* only the visible part of the n axis */
    if (n1 > (long)floor(g.xmax) + 1)
        n1 = (long)floor(g.xmax) + 1;
    if (gx.plotstep < 1)
        return;
    if (!isnan(gr_comet)) { /* comet: the terms up to the animated n */
        long nc = (long)gx.nmin + (long)floor(gr_comet * (gx.nmax - gx.nmin) + 1e-9);
        if (n1 > nc)
            n1 = nc;
        if (n1 < n0)
            return;
    }
    gx_seq_run(n0, n1, seq_cb, &s);
}

/* SEQ web (cobweb) plot of sequence i, with the x axis u(n-1) and the y axis
 * u(n):
 *   1. the diagonal y = x (grey) over the part of the window where it is
 *      visible;
 *   2. the map y = F(x) (u(n) as a function of u(n-1), gx_seq_step), one
 *      sample per pixel column, broken at jumps of more than a plot height;
 *   3. the cobweb: from (u0, 0) go vertically to the curve (u0, F(u0)), then
 *      horizontally to the diagonal (F(u0), F(u0)), and repeat from there
 *      (vertical from the diagonal to the curve), for nMax - nMin steps (at
 *      most 2000) or until a term is undefined. A cobweb spiralling in shows
 *      convergence to a fixed point, one spiralling out divergence. */
static void plot_seq_web(int i, uint16_t c)
{
    /* y = x, then the step function and the cobweb */
    double a = fmax(g.xmin, g.ymin), b = fmin(g.xmax, g.ymax);
    if (a < b)
        gfx_line(GX0 + (int)lround(gv_x_to_px(a)), GY0 + (int)lround(gv_y_to_py(a)), GX0 + (int)lround(gv_x_to_px(b)),
                 GY0 + (int)lround(gv_y_to_py(b)), C_GRAY);
    double prev = NAN;
    for (int px = 0; px < GW; px++) {
        double y = gx_seq_step(i, gv_px_to_x(px)), py = isfinite(y) ? gv_y_to_py(y) : (double)NAN;
        if (isfinite(py) && isfinite(prev) && fabs(py - prev) < GH)
            gr_seg(px - 1, prev, px, py, c, GS_THIN);
        prev = py;
    }
    double u = gx_seq(i, (long)gx.nmin), y0 = 0;
    long steps = (long)(gx.nmax - gx.nmin);
    if (steps > 2000)
        steps = 2000;
    if (!isnan(gr_comet)) /* comet: the cobweb grows step by step */
        steps = (long)floor(gr_comet * steps + 1e-9);
    for (long k = 0; k < steps && isfinite(u); k++) {
        double v = gx_seq_step(i, u);
        if (!isfinite(v))
            break;
        gr_seg(gv_x_to_px(u), gv_y_to_py(y0), gv_x_to_px(u), gv_y_to_py(v), c, GS_THIN);
        gr_seg(gv_x_to_px(u), gv_y_to_py(v), gv_x_to_px(v), gv_y_to_py(v), c, GS_THIN);
        y0 = v;
        u = v;
    }
}

/* ------------------------------------------------------------ IMPL */
#define IC 2                    /* cell size in pixels */
#define INX (LCD_WIDTH / IC + 1) /* (the widest plot) */
/* Two rows of grid samples of f(x, y) (the cell row's top and bottom edges),
 * swapped as the scan moves down. float halves the RAM; the maths is done in
 * double. Static rather than on the stack (2 x 162 floats). */
static float irow[2][INX + 1];

/* Does the inequality hold for v = lhs - rhs? (REL_EQ: never; it is not shaded.) */
static bool rel_holds(int rel, double v)
{
    switch (rel) {
    case REL_LT: return v < 0;
    case REL_LE: return v <= 0;
    case REL_GT: return v > 0;
    case REL_GE: return v >= 0;
    }
    return false;
}

/* One contour segment of an implicit curve in cell column cx. Strict
 * inequalities (<, >) draw the boundary dashed, like on paper: cells are
 * skipped in alternate runs of 3 columns. Dot and shading styles draw the
 * boundary thin. */
static void iseg(double ax, double ay, double bx, double by, uint16_t c, int style, bool dashed, int cx)
{
    if (dashed && ((cx / 3) & 1))
        return;
    gr_seg(ax, ay, bx, by, c, style == GS_DOT || style == GS_ABOVE || style == GS_BELOW ? GS_THIN : style);
}

/* Implicit relation i (lhs REL rhs, gx_impl gives lhs - rhs) by marching
 * squares (W. E. Lorensen and H. E. Cline's marching cubes, in 2D).
 * The plot is covered by cells of IC x IC pixels. f is sampled at the cell
 * corners, one row at a time; corners are numbered
 *     v0 (top-left)    v1 (top-right)
 *     v3 (bottom-left) v2 (bottom-right)
 * and the 4-bit case index has bit k set when vk > 0. Cases 0 and 15 (all
 * corners on one side) contain no curve. Otherwise the curve crosses the edges
 * whose ends differ in sign, at the point found by linear interpolation of f
 * along the edge (e.g. top: x = x0 + IC * v0 / (v0 - v1)). Each case and its
 * complement (k and 15 - k) give the same segment. The ambiguous saddle cases
 * 5 and 10 (diagonal corners positive) are resolved with the value at the
 * centre: if the centre has the sign of v0/v2, those two corners are
 * connected and the segments cut off v1 and v3, else the reverse.
 * Cells with a non-finite corner are skipped, and so are sign changes across
 * a pole (1/x = 0 would draw the line x = 0): there |f| at the centre is
 * larger than twice the largest corner value.
 * shade == true runs the shading pass instead (inequalities only): a cell is
 * tinted when the relation holds for the mean of its corner values.
 * Cost: (GW/IC + 2) * (GH/IC + 1) evaluations, plus one centre evaluation per
 * crossed cell. */
static void plot_impl(int i, int style, uint16_t c, bool shade)
{
    int rel = gx_rel(i);
    if (shade && rel == REL_EQ)
        return;
    bool dashed = rel == REL_LT || rel == REL_GT;
    uint16_t tint = c;
    int nx = GW / IC + 1, ny = GH / IC + 1;
    float *a = irow[0], *b = irow[1];
    for (int k = 0; k <= nx; k++)
        a[k] = (float)gx_impl(i, gv_px_to_x(k * IC), gv_py_to_y(0));
    for (int r = 1; r <= ny; r++) {
        double y1 = r * IC;
        for (int k = 0; k <= nx; k++)
            b[k] = (float)gx_impl(i, gv_px_to_x(k * IC), gv_py_to_y(y1));
        double y0 = y1 - IC;
        for (int k = 0; k < nx; k++) {
            double v0 = a[k], v1 = a[k + 1], v2 = b[k + 1], v3 = b[k]; /* float storage, double maths */
            double x0 = k * IC, x1 = x0 + IC;
            if (shade) {
                if (isfinite(v0) && rel_holds(rel, (v0 + v1 + v2 + v3) / 4))
                    gr_blend_rect((int)x0, (int)y0, IC, IC, tint);
                continue;
            }
            if (!isfinite(v0) || !isfinite(v1) || !isfinite(v2) || !isfinite(v3))
                continue;
            int idx = (v0 > 0) | (v1 > 0) << 1 | (v2 > 0) << 2 | (v3 > 0) << 3;
            if (idx == 0 || idx == 15)
                continue;
            /* reject sign changes across poles: the centre must not blow up */
            double vc = gx_impl(i, gv_px_to_x(x0 + IC / 2.0), gv_py_to_y(y0 + IC / 2.0));
            double mx = fmax(fmax(fabs(v0), fabs(v1)), fmax(fabs(v2), fabs(v3)));
            if (!isfinite(vc) || fabs(vc) > 2 * mx)
                continue;
            /* edge points: top (v0-v1), right (v1-v2), bottom (v3-v2), left (v0-v3) */
            double tx = x0 + IC * v0 / (v0 - v1), ry = y0 + IC * v1 / (v1 - v2);
            double bx = x0 + IC * v3 / (v3 - v2), ly = y0 + IC * v0 / (v0 - v3);
            switch (idx) {
            case 1: case 14: iseg(tx, y0, x0, ly, c, style, dashed, k); break;
            case 2: case 13: iseg(tx, y0, x1, ry, c, style, dashed, k); break;
            case 3: case 12: iseg(x0, ly, x1, ry, c, style, dashed, k); break;
            case 4: case 11: iseg(x1, ry, bx, y1, c, style, dashed, k); break;
            case 6: case 9: iseg(tx, y0, bx, y1, c, style, dashed, k); break;
            case 7: case 8: iseg(x0, ly, bx, y1, c, style, dashed, k); break;
            case 5: case 10: /* saddle: decide with the centre */
                if ((vc > 0) == (idx == 5)) {
                    iseg(tx, y0, x1, ry, c, style, dashed, k);
                    iseg(x0, ly, bx, y1, c, style, dashed, k);
                } else {
                    iseg(tx, y0, x0, ly, c, style, dashed, k);
                    iseg(x1, ry, bx, y1, c, style, dashed, k);
                }
                break;
            }
        }
        float *t = a; /* this row's bottom edge is the next row's top edge */
        a = b, b = t;
    }
}

/* ------------------------------------------------------------ all */
/* Plot every enabled, compiled entry of the current graph mode in its colour
 * and style (graph.c's render(), after the axes, grid and graph overlays such
 * as stat plots). Order matters: shadings first so the curves are drawn over
 * them; in FUNC mode the CALC integral shading too. The caller has compiled
 * the entries (gx_compile) and set the clip to the plot area. */
void gr_plot_all(void)
{
    switch (gx.mode) {
    case GM_FUNC:
        for (int i = 0; i < NFUNC; i++)
            if (gx_plottable(i) && (gx.fstyle[i] == GS_ABOVE || gx.fstyle[i] == GS_BELOW))
                shade_func(i, gx.fstyle[i], func_colors[g.f[i].color]);
        if (calc_shade.on)
            gr_shade_between(calc_shade.fn, calc_shade.fn2, calc_shade.a, calc_shade.b,
                             func_colors[g.f[calc_shade.fn].color]);
        for (int i = 0; i < NFUNC; i++)
            if (gx_plottable(i))
                plot_func(i, gx.fstyle[i], func_colors[g.f[i].color]);
        break;
    case GM_PAR:
    case GM_POL:
        for (int i = 0; i < GNENT; i++)
            if (gx_plottable(i))
                plot_curve(i, gx_style(i), func_colors[gx_color(i)]);
        break;
    case GM_SEQ:
        if (gx.seqplot == SEQ_WEB) {
            for (int i = 0; i < GNENT; i++)
                if (gx_plottable(i))
                    plot_seq_web(i, func_colors[gx_color(i)]);
        } else {
            plot_seq_time();
        }
        break;
    case GM_IMPL:
        for (int pass = 0; pass < 2; pass++) /* all shading first, then the curves */
            for (int i = 0; i < GNENT; i++)
                if (gx_plottable(i))
                    plot_impl(i, gx_style(i), func_colors[gx_color(i)], pass == 0);
        break;
    case GM_DE: /* slope fields and solution curves (gr_de.c) */
        gr_de_plot();
        break;
    }
}
