/*
 * Stat plots (SHIFT F1 on the graph screens): up to three plots of list data drawn on the graph
 * screen together with the Y= functions: scatter, xyLine, histogram,
 * modified box plot (outliers marked), box plot, normal probability plot.
 * ZoomStat fits the window to the data. Settings are saved ("stat.plots").
 *
 * Drawing hook: stat_plots_draw() takes the window and the pixel rectangle;
 * the graph screen calls it through the graph overlay graph_overlay_stat
 * (app/stat_overlay.c, registered in app/registry.c), so the plots share the
 * WINDOW and are cached with the graph. Whatever changes the data or the
 * settings clears g.graph_valid so the graph is re-rendered.
 *
 * Plot types (splot_t in stat.h: on, type, mark, color, X/Y/frequency list):
 *   Scatter / xyLine  points (x[i], y[i]) over the shorter of the two lists;
 *                     xyLine joins consecutive points in list order.
 *   Histogram         bars of width Xscl starting at Xmin, bin [a, a+Xscl);
 *                     height = count (or summed frequencies).
 *   Box / ModBox      five-number summary from st_onevar (quartiles as
 *                     medians of the lower / upper halves, see stat_math.c).
 *                     ModBox: whiskers stop at the furthest data within
 *                     1.5 IQR of the box (Tukey's fences) and points beyond
 *                     are drawn as marks. Several box plots are stacked in
 *                     horizontal bands from the top of the plot.
 *   NormProb          sorted data against normal quantiles
 *                     z_i = Φ⁻¹((i + 0.5) / n) (st_normprob_z): data from a
 *                     normal distribution lie on a straight line.
 *
 * UI: the STAT PLOTS menu (Plot1..3 with a summary, PlotsOff, PlotsOn,
 * ZoomStat) and a settings form per plot (stat_form.c). Settings are saved in
 * the "stat.plots" blob; the list names are stored, not the data.
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os") /* flash budget: size over speed (not time-critical) */
#endif
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "registry.h"
#include "session.h"
#include "stat.h"

splot_t stat_plots[SP_N];
static const char *const type_names[SP_NTYPES] = {"Scatter", "xyLine", "Histogram", "ModBox", "Box", "NormProb"};

/* All plots off, scatter of L1/L2 with no frequency list, box marks;
 * colours 1, 4, 3 of func_colors (red, green, magenta): the three plots
 * differ, and none has the blue of Y1, where a regression equation usually
 * goes, so the points stay visible under the fitted curve. */
static void plot_defaults(void)
{
    for (int i = 0; i < SP_N; i++) {
        splot_t *p = &stat_plots[i];
        memset(p, 0, sizeof *p);
        snprintf(p->xl, sizeof p->xl, "L1");
        snprintf(p->yl, sizeof p->yl, "L2");
        snprintf(p->fl, sizeof p->fl, "1");
        p->color = (uint8_t)(i == 0 ? 1 : i == 1 ? 4 : 3);
    }
}

bool stat_plots_any(void)
{
    for (int i = 0; i < SP_N; i++)
        if (stat_plots[i].on)
            return true;
    return false;
}

/* ------------------------------------------------------------ data */
/* The data of one plot: pointers into the list storage (not copies). */
typedef struct {
    const double *x, *y, *f; /* y: scatter/xyLine only; f: frequencies or NULL */
    int n;
} pdata_t;

/* Resolve the plot's list names. false (plot skipped) if the X list (or, for
 * scatter/xyLine, the Y list) is missing or empty, or if a frequency list is
 * named but missing or of a different length than X. Frequencies are ignored
 * by the normal probability plot. */
static bool get_data(const splot_t *p, pdata_t *d)
{
    const slist_t *x = sl_find(p->xl);
    if (!x || !x->len)
        return false;
    d->x = x->v;
    d->n = x->len;
    d->y = d->f = NULL;
    if (p->type == SP_SCATTER || p->type == SP_XYLINE) {
        const slist_t *y = sl_find(p->yl);
        if (!y || !y->len)
            return false;
        d->y = y->v;
        if (y->len < d->n)
            d->n = y->len;
    } else if (p->type != SP_NORMPROB && strcmp(p->fl, "1") && p->fl[0]) {
        const slist_t *f = sl_find(p->fl);
        if (!f || f->len != x->len)
            return false;
        d->f = f->v;
    }
    return true;
}

/* ------------------------------------------------------------ drawing */
/* Window and pixel rectangle of the current stat_plots_draw() call. */
static double X0, X1, Y0, Y1;
static int PX, PY, PW, PH;

/* Graph -> pixel: X0 maps to PX, X1 to PX+PW-1; Y1 (top) to PY, Y0 to PY+PH-1. */
static int tx(double x) { return PX + (int)lround((x - X0) * (PW - 1) / (X1 - X0)); }
static int ty(double y) { return PY + (int)lround((Y1 - y) * (PH - 1) / (Y1 - Y0)); }

/* A window-relative coordinate (0..1 inside the window) that is safe to
 * convert to int pixels: far-away points are skipped instead of overflowing. */
static bool in_range(double v) { return isfinite(v) && fabs(v) < 1e7; }

/* Point mark m: 0 = 3x3 box, 1 = 5x5 plus, 2 = 2x2 dot. */
static void mark(int x, int y, int m, uint16_t c)
{
    if (m == 0)
        gfx_rect(x - 1, y - 1, 3, 3, c);
    else if (m == 1)
        gfx_hline(x - 2, y, 5, c), gfx_vline(x, y - 2, 5, c);
    else
        gfx_fill_rect(x, y, 2, 2, c);
}

/* Scatter (line = false) or xyLine. A non-finite or far-off point breaks the
 * line: the next valid point starts a new segment. */
static void draw_points(const splot_t *p, const pdata_t *d, uint16_t c, bool line)
{
    int lx = 0, ly = 0;
    bool have = false;
    for (int i = 0; i < d->n; i++) {
        if (!isfinite(d->x[i]) || !isfinite(d->y[i])) {
            have = false;
            continue;
        }
        double fx = (d->x[i] - X0) / (X1 - X0), fy = (Y1 - d->y[i]) / (Y1 - Y0);
        if (!in_range(fx) || !in_range(fy)) {
            have = false;
            continue;
        }
        int x = tx(d->x[i]), y = ty(d->y[i]);
        if (line && have)
            gfx_line(lx, ly, x, y, c);
        mark(x, y, p->mark, c);
        lx = x, ly = y, have = true;
    }
}

/* Histogram. Bin width is the window's Xscl (a tenth of the window if Xscl
 * is not positive) and bins are aligned to Xmin, as on other graphing
 * calculators: ZoomStat chooses a "nice" Xscl and Xmin so the bins look
 * right. Values outside [Xmin, Xmax) are not counted. More than 400 bins
 * (one every < 1 px) are not drawn at all. Empty bins draw nothing; bars
 * taller than the window are cut at the top edge. O(bins x n). */
static void draw_hist(const pdata_t *d, uint16_t c)
{
    double w = g.xscl > 0 ? g.xscl : (X1 - X0) / 10;
    if ((X1 - X0) / w > 400)
        return;
    int nb = (int)ceil((X1 - X0) / w);
    for (int b = 0; b < nb; b++) {
        double a = X0 + b * w, e = a + w, cnt = 0;
        for (int i = 0; i < d->n; i++)
            if (d->x[i] >= a && d->x[i] < e)
                cnt += d->f ? d->f[i] : 1;
        if (cnt <= 0)
            continue;
        int x0 = tx(a), x1 = tx(e), y0 = ty(0), y1 = ty(cnt);
        if (y1 < PY - 1)
            y1 = PY - 1;
        gfx_rect(x0, y1, x1 - x0 + 1, y0 - y1 + 1, c);
    }
}

/* Box plot number `slot` of `nslots` box plots on screen. Layout: the plot
 * height is divided into nslots + 1 bands of at most 40 px from the top
 * (+6 px margin); the box is 60 % of the band high, centred in it. Box from
 * Q1 to Q3 with the median line; whiskers to min/max (Box) or to the most
 * extreme data inside the fences Q1 - 1.5 IQR and Q3 + 1.5 IQR (ModBox,
 * with outliers marked on the centre line). The y axis of the window plays no
 * role. Needs st_scratch (quartiles); without it st_onevar gives NaN and
 * nothing is drawn. */
static void draw_box(const splot_t *p, const pdata_t *d, uint16_t c, int slot, int nslots)
{
    sres_t r;
    if (st_onevar(d->x, d->f, d->n, &r))
        return;
    double q1 = sr_get(&r, "Q1"), med = sr_get(&r, "Med"), q3 = sr_get(&r, "Q3");
    double lo = sr_get(&r, "minX"), hi = sr_get(&r, "maxX");
    if (isnan(q1))
        return;
    int band = PH / (nslots + 1) < 40 ? PH / (nslots + 1) : 40;
    int yc = PY + 6 + band * slot + band / 2, h = band * 6 / 10;
    if (p->type == SP_MODBOX) {
        double iqr = q3 - q1, flo = q1 - 1.5 * iqr, fhi = q3 + 1.5 * iqr;
        double wlo = q1, whi = q3; /* whisker ends: start at the box, grow outwards to the fences */
        for (int i = 0; i < d->n; i++) {
            if (d->f && !(d->f[i] > 0))
                continue;
            double v = d->x[i];
            if (v >= flo && v < wlo)
                wlo = v;
            if (v <= fhi && v > whi)
                whi = v;
            if (v < flo || v > fhi)
                mark(tx(v), yc, p->mark, c);
        }
        lo = wlo, hi = whi;
    }
    int xq1 = tx(q1), xq3 = tx(q3);
    gfx_rect(xq1, yc - h / 2, xq3 - xq1 + 1, h, c);
    gfx_vline(tx(med), yc - h / 2, h, c);
    gfx_hline(tx(lo), yc, xq1 - tx(lo), c);
    gfx_hline(xq3, yc, tx(hi) - xq3 + 1, c);
    gfx_vline(tx(lo), yc - 3, 7, c);
    gfx_vline(tx(hi), yc - 3, 7, c);
}

/* Normal probability plot: the i-th smallest value (x) against the normal
 * quantile z_i (y). Sorts a copy in st_scratch (the list keeps its order);
 * skipped when the list is longer than the scratch area. */
static void draw_normprob(const splot_t *p, const pdata_t *d, uint16_t c)
{
    if (d->n > st_scratch_cap)
        return;
    double *s = st_scratch;
    memcpy(s, d->x, sizeof(double) * (size_t)d->n);
    st_sort(s, d->n);
    for (int i = 0; i < d->n; i++) {
        double z = st_normprob_z(i, d->n);
        if (in_range((s[i] - X0) / (X1 - X0)) && in_range((Y1 - z) / (Y1 - Y0)))
            mark(tx(s[i]), ty(z), p->mark, c);
    }
}

/* Draw every enabled plot, clipped to the pixel rectangle (gx, gy, gw, gh)
 * that shows [xmin, xmax] x [ymin, ymax]. Box plots are counted first so
 * each gets its own band. Called during graph rendering (main loop). */
void stat_plots_draw(double xmin, double xmax, double ymin, double ymax, int gx, int gy, int gw, int gh)
{
    if (!stat_plots_any() || !(xmax > xmin) || !(ymax > ymin))
        return;
    X0 = xmin, X1 = xmax, Y0 = ymin, Y1 = ymax;
    PX = gx, PY = gy, PW = gw, PH = gh;
    gfx_set_clip(gx, gy, gw, gh);
    int nbox = 0, slot = 0;
    for (int i = 0; i < SP_N; i++)
        nbox += stat_plots[i].on && (stat_plots[i].type == SP_BOX || stat_plots[i].type == SP_MODBOX);
    for (int i = 0; i < SP_N; i++) {
        const splot_t *p = &stat_plots[i];
        pdata_t d;
        if (!p->on || !get_data(p, &d))
            continue;
        uint16_t c = func_colors[p->color % NCOLORS];
        switch (p->type) {
        case SP_SCATTER: draw_points(p, &d, c, false); break;
        case SP_XYLINE: draw_points(p, &d, c, true); break;
        case SP_HIST: draw_hist(&d, c); break;
        case SP_MODBOX: case SP_BOX: draw_box(p, &d, c, slot++, nbox); break;
        case SP_NORMPROB: draw_normprob(p, &d, c); break;
        }
    }
    gfx_reset_clip();
}

/* ------------------------------------------------------------ ZoomStat */
/* Widen [*lo, *hi] to include the finite values of v. */
static void minmax(const double *v, int n, double *lo, double *hi)
{
    for (int i = 0; i < n; i++)
        if (isfinite(v[i])) {
            if (v[i] < *lo) *lo = v[i];
            if (v[i] > *hi) *hi = v[i];
        }
}

/* ZoomStat: set the graph WINDOW (g.xmin..g.yscl) to show the data of every
 * enabled plot, and invalidate the graph cache. Returns -1 (window unchanged)
 * if no enabled plot has data or no X value is finite (minmax() skips
 * non-finite values; Y without a finite value counts as no Y data).
 *   x range   the data range of all plots (±1 if it is a single value).
 *   histogram if any histogram is on: about 8 bins over the data with a
 *             "nice" width (range/8 rounded up to k x 10^m, k = 1..10),
 *             Xmin a multiple of hw, Xmax a whole number of bins above;
 *             Xscl = hw so draw_hist uses the same bins. The bins are counted
 *             here as draw_hist will count them; without other y data the y
 *             range is -25 % .. +125 % of the tallest bin, otherwise it is
 *             widened to include 0 and the tallest bin.
 *   otherwise 10 % padding on each side; Xscl = 2 x 10^(floor(log10 range) - 1),
 *             i.e. 1/50 to 1/5 of the range.
 *   y range   Y data, ±z of the extreme normal quantile (NormProb), or
 *             -1..1 when only box plots are on (they ignore y), then 10 %
 *             padding (not with a histogram); Yscl by the same rule as Xscl.
 */
int stat_zoomstat(void)
{
    double x0 = DINF, x1 = -DINF, y0 = DINF, y1 = -DINF;
    bool any = false, yset = false, hist = false;
    double hw = 0, hmax = 0;
    for (int i = 0; i < SP_N; i++) {
        const splot_t *p = &stat_plots[i];
        pdata_t d;
        if (!p->on || !get_data(p, &d))
            continue;
        any = true;
        minmax(d.x, d.n, &x0, &x1);
        if (d.y) {
            minmax(d.y, d.n, &y0, &y1);
            yset = true;
        } else if (p->type == SP_NORMPROB) {
            double z = st_normprob_z(d.n - 1, d.n);
            y0 = fmin(y0, -z), y1 = fmax(y1, z);
            yset = true;
        } else if (p->type == SP_HIST) {
            hist = true;
        }
    }
    if (!any || !(x1 >= x0)) /* no data, or no finite X (the bounds are still +-inf) */
        return -1;
    if (!(y1 >= y0))
        yset = false;
    if (!(x1 > x0))
        x0 -= 1, x1 += 1;
    if (hist) {
        /* about 8 bins over the data */
        hw = (x1 - x0) / 8;
        double mag = pow(10, floor(log10(hw)));
        hw = ceil(hw / mag) * mag;
        x0 = floor(x0 / hw) * hw;
        /* + 1e-9: a maximum exactly on a bin edge gets its own bin (bins are [a, a+hw)) */
        x1 = x0 + hw * ceil((x1 - x0) / hw + 1e-9);
        if (x1 <= x0)
            x1 = x0 + hw;
        g.xscl = hw;
        for (int i = 0; i < SP_N; i++) {
            const splot_t *p = &stat_plots[i];
            pdata_t d;
            if (!p->on || p->type != SP_HIST || !get_data(p, &d))
                continue;
            for (double a = x0; a < x1 - hw / 2; a += hw) {
                double cnt = 0;
                for (int k = 0; k < d.n; k++)
                    if (d.x[k] >= a && d.x[k] < a + hw)
                        cnt += d.f ? d.f[k] : 1;
                hmax = fmax(hmax, cnt);
            }
        }
        if (!yset)
            y0 = -hmax * 0.25, y1 = hmax * 1.25;
        else
            y0 = fmin(y0, 0), y1 = fmax(y1, hmax);
        yset = true;
    } else {
        double pad = (x1 - x0) * 0.1;
        x0 -= pad, x1 += pad;
        g.xscl = pow(10, floor(log10(x1 - x0)) - 1) * 2;
    }
    if (!yset)
        y0 = -1, y1 = 1;
    if (!(y1 > y0))
        y0 -= 1, y1 += 1;
    if (!hist) {
        double pad = (y1 - y0) * 0.1;
        y0 -= pad, y1 += pad;
    }
    g.xmin = x0, g.xmax = x1, g.ymin = y0, g.ymax = y1;
    g.yscl = pow(10, floor(log10(y1 - y0)) - 1) * 2;
    g.graph_valid = false;
    return 0;
}

/* ------------------------------------------------------------ settings UI */
static int editing; /* plot being edited */

/* Settings form of one plot. Field order matters: plot_changed() and
 * edit_plot() address the fields by index 0..6; actions 1 = ZoomStat (then
 * graph), 2 = Graph. Choice indices equal the SP_* type and mark numbers.
 * Fields a type does not use are hidden (cond on the Type field, index 1):
 * Ylist for all but scatter / xyLine, Freq for scatter, xyLine and the
 * normal probability plot, Mark for the histogram and the plain box plot
 * (which draw no points). Hidden fields keep their values. */
#define ONLY(mask) 2, SF_ANYOF | (mask)
static const sfield_t f_plot[] = {
    {"Plot:", SF_CHOICE, 0, 0, 0, "Off|On"},
    {"Type:", SF_CHOICE, 0, 0, 0, "Scat|Line|Hist|MBox|Box|NPrb"},
    {"Xlist:", SF_LIST, 0, 0, 0, "L1"},
    {"Ylist:", SF_LIST, ONLY(1 << SP_SCATTER | 1 << SP_XYLINE), 0, "L2"},
    {"Freq:", SF_LIST, ONLY(1 << SP_HIST | 1 << SP_MODBOX | 1 << SP_BOX), 0, "1"},
    {"Mark:", SF_CHOICE, ONLY(1 << SP_SCATTER | 1 << SP_XYLINE | 1 << SP_MODBOX | 1 << SP_NORMPROB), 0, "box|+|dot"},
    {"Color:", SF_COLOR, 0, 0, 0, "1|2|3|4|5|6|7|8"},
    {"ZoomStat", SF_ACTION, 0, 0, 1, NULL},
    {"Graph", SF_ACTION, 0, 0, 2, NULL},
};

/* Hint line of the plot form: on the Type row, what the chosen type draws
 * (the options are abbreviated to fit one row). */
static const char *plot_hint(void)
{
    static const char *const what[SP_NTYPES] = {
        "◀ ▶  Scatter: points (x, y)",          "◀ ▶  xyLine: points joined in order",
        "◀ ▶  Histogram: bars Xscl wide",       "◀ ▶  Modified box: outliers as points",
        "◀ ▶  Box plot: min Q1 Med Q3 max",     "◀ ▶  Normal probability: data vs z",
    };
    if (sf_selected() == 1)
        return what[sf_choice(1) % SP_NTYPES];
    return NULL;
}

/* sform_t.changed: copy every field into the plot as soon as it changes (no
 * separate "save" step) and invalidate the graph. List names are not checked
 * here; a plot with an unknown list is simply not drawn. */
static void plot_changed(void)
{
    splot_t *p = &stat_plots[editing];
    p->on = sf_choice(0);
    p->type = (uint8_t)sf_choice(1);
    snprintf(p->xl, sizeof p->xl, "%s", sf_text(2));
    snprintf(p->yl, sizeof p->yl, "%s", sf_text(3));
    snprintf(p->fl, sizeof p->fl, "%s", sf_text(4));
    p->mark = (uint8_t)sf_choice(5);
    p->color = (uint8_t)sf_choice(6);
    g.graph_valid = false;
}

static void plot_action(int a)
{
    plot_changed();
    if (a == 1 && stat_zoomstat()) {
        sf_error("no plot with data is on");
        return;
    }
    ui_switch(&scr_graph);
}

static char plot_title[16];
static const sform_t plot_form = {plot_title, f_plot, (int)(sizeof f_plot / sizeof f_plot[0]), plot_action,
                                  "stat-plots", 0, plot_changed, plot_hint};

/* Open the settings form for plot i, filled from the plot's current settings
 * (overriding the form's remembered values). */
static void edit_plot(int i)
{
    editing = i;
    snprintf(plot_title, sizeof plot_title, "Plot%d", i + 1);
    sf_open(&plot_form);
    const splot_t *p = &stat_plots[i];
    char c[2] = {0};
    c[0] = (char)('0' + p->on), sf_set_text(0, c);
    c[0] = (char)('0' + p->type), sf_set_text(1, c);
    sf_set_text(2, p->xl);
    sf_set_text(3, p->yl);
    sf_set_text(4, p->fl);
    c[0] = (char)('0' + p->mark), sf_set_text(5, c);
    c[0] = (char)('0' + p->color % 8), sf_set_text(6, c);
}

/* One-step plot (regression result, spreadsheet columns): show lists xl
 * (and yl for scatter / xyLine) as a plot of `type`. The plot used is the
 * one already showing exactly that, else the first plot that is off, else
 * Plot1; it is turned on without a frequency list, keeping its mark and
 * colour. Then ZoomStat and the graph screen. */
void stat_plot_show(int type, const char *xl, const char *yl)
{
    int k = -1;
    for (int i = 0; i < SP_N && k < 0; i++) {
        const splot_t *p = &stat_plots[i];
        if (p->on && p->type == type && !strcmp(p->xl, xl) && (!yl || !strcmp(p->yl, yl)))
            k = i;
    }
    for (int i = 0; i < SP_N && k < 0; i++)
        if (!stat_plots[i].on)
            k = i;
    if (k < 0)
        k = 0;
    splot_t *p = &stat_plots[k];
    p->on = true;
    p->type = (uint8_t)type;
    snprintf(p->xl, sizeof p->xl, "%s", xl);
    if (yl)
        snprintf(p->yl, sizeof p->yl, "%s", yl);
    snprintf(p->fl, sizeof p->fl, "1");
    g.graph_valid = false;
    stat_zoomstat();
    ui_switch(&scr_graph);
}

/* STAT PLOTS menu (dynamic): items 0..SP_N-1 are the plots (action = plot
 * index, opens its form), then PlotsOff / PlotsOn / ZoomStat (actions
 * PM_OFF.., chosen above any plot index). */
enum { PM_OFF = 10, PM_ON, PM_ZOOM };

static int pm_count(void) { return SP_N + 3; }

/* Item i; plot labels summarise the settings ("Plot1 On  Scatter L1,L2"). */
static void pm_get(int i, menu_item_t *out)
{
    static char lab[48];
    if (i < SP_N) {
        const splot_t *p = &stat_plots[i];
        if (p->type == SP_SCATTER || p->type == SP_XYLINE)
            snprintf(lab, sizeof lab, "Plot%d %s %s %s,%s", i + 1, p->on ? "On " : "Off", type_names[p->type],
                     p->xl, p->yl);
        else
            snprintf(lab, sizeof lab, "Plot%d %s %s %s", i + 1, p->on ? "On " : "Off", type_names[p->type], p->xl);
        *out = (menu_item_t){lab, NULL, NULL, i, "stat-plots"};
        return;
    }
    static const char *const n[] = {"PlotsOff", "PlotsOn", "ZoomStat"};
    *out = (menu_item_t){n[i - SP_N], NULL, NULL, PM_OFF + i - SP_N, "stat-plots"};
}

/* Plot -> its form; ZoomStat -> graph (or a message without data);
 * PlotsOff/PlotsOn switch all three and return home. */
static void pm_select(const menu_item_t *it)
{
    if (it->action < SP_N) {
        edit_plot(it->action);
        return;
    }
    if (it->action == PM_ZOOM) {
        if (stat_zoomstat()) {
            ui_message("ZOOMSTAT", "No stat plot with data is on.");
            return;
        }
        ui_switch(&scr_graph);
        return;
    }
    for (int i = 0; i < SP_N; i++)
        stat_plots[i].on = it->action == PM_ON;
    g.graph_valid = false;
    ui_switch(ui_home());
}

static const menu_def_t menu_plots = {"STAT PLOTS", NULL, 0, pm_select, false, "stat-plots", pm_count, pm_get};

void stat_plot_menu_open(void) { menu_open(&menu_plots); }

/* ------------------------------------------------------------ persistence */
/* Blob "stat.plots", version 1: per plot u8 on, u8 type, u8 mark, u8 color,
 * str xlist, str ylist, str freq. Loading clamps the small fields into range
 * (modulo) so a corrupt blob cannot index past the tables. */
static void plots_save(pw_t *w)
{
    for (int i = 0; i < SP_N; i++) {
        const splot_t *p = &stat_plots[i];
        pw_u8(w, p->on);
        pw_u8(w, p->type);
        pw_u8(w, p->mark);
        pw_u8(w, p->color);
        pw_str(w, p->xl);
        pw_str(w, p->yl);
        pw_str(w, p->fl);
    }
}

static void plots_load(pr_t *r, unsigned version)
{
    plot_defaults();
    for (int i = 0; i < SP_N; i++) {
        splot_t *p = &stat_plots[i];
        p->on = pr_u8(r) != 0;
        p->type = (uint8_t)(pr_u8(r) % SP_NTYPES);
        p->mark = (uint8_t)(pr_u8(r) % 3);
        p->color = (uint8_t)(pr_u8(r) % NCOLORS);
        pr_str(r, p->xl, sizeof p->xl);
        pr_str(r, p->yl, sizeof p->yl);
        pr_str(r, p->fl, sizeof p->fl);
    }
    if (r->err)
        plot_defaults();
}

static void plots_reset(void) { plot_defaults(); }

const persist_def_t persist_stat_plots = {"stat.plots", 1, plots_save, plots_load, plots_reset};

void stat_plot_init(void) { plot_defaults(); }
