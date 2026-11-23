/*
 * Shaded distribution curves: DISTR > DRAW (ShadeNorm, Shade_t, Shadechi²,
 * ShadeF) and the "Draw" of a hypothesis test (p-value region). The window
 * is chosen from the distribution (not the graph WINDOW).
 *
 * Entry points: sd_draw_open() (DISTR > DRAW forms in stat_menu.c: shade the
 * area between two bounds and print it) and sd_draw_result() (ENTER on a
 * test result: shade the region whose area is the p-value). Both fill the
 * statics below and switch to scr_stat_draw ("statdraw", help "stat-distr").
 * CLEAR, ENTER or F5 (back) return to the previous screen with
 * ui_returning set, so the form or result keeps its state.
 *
 * Distributions: DIST_NORM (p1 = μ, p2 = σ), DIST_T (p1 = df), DIST_CHI2
 * (p1 = df), DIST_F (p1, p2 = numerator, denominator df); densities and CDFs
 * come from cas_stat.h (double precision, also in the stub CAS build).
 *
 * Geometry: one text line at the top (distribution and area / statistic),
 * the curve area x 0..319, y GY = 34 .. GY+GH-1 = 205 (the x axis is the
 * bottom row), xmin / xmax labels above the axis, and a second text line
 * at UI_BOTTOM - 16. The curve is sampled once per pixel column; shading is
 * a vertical line from the axis up to the curve in each column whose x lies
 * in a shaded interval.
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os") /* flash budget: size over speed (not time-critical) */
#endif
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "cas_stat.h"
#include "stat.h"

#define FF (&font_medium)
#define GX 0
#define GY (BODY_Y + 16)
#define GW LCD_WIDTH
#define GH (UI_BOTTOM - GY - 18)

static int dist;                  /* DIST_* */
static double p1, p2;             /* distribution parameters (see the header) */
static double lo1, hi1, lo2, hi2; /* shaded intervals (second may be empty) */
static double area;
static char line1[96], line2[96]; /* top and bottom text lines */
static double xmin, xmax, ymax;   /* drawing window (ymin is 0) */

/* density of the current distribution at x */
static double pdf(double x)
{
    switch (dist) {
    case DIST_NORM: return sd_normpdf(x, p1, p2);
    case DIST_T: return sd_tpdf(x, p1);
    case DIST_CHI2: return sd_chi2pdf(x, p1);
    case DIST_F: return sd_fpdf(x, p1, p2);
    }
    return DNAN;
}

/* probability of [a, b] under the current distribution */
static double cdf(double a, double b)
{
    switch (dist) {
    case DIST_NORM: return sd_normcdf(a, b, p1, p2);
    case DIST_T: return sd_tcdf(a, b, p1);
    case DIST_CHI2: return sd_chi2cdf(a, b, p1);
    case DIST_F: return sd_fcdf(a, b, p1, p2);
    }
    return DNAN;
}

/* Choose xmin/xmax/ymax for the distribution so nearly all of its mass is
 * visible:
 *   normal  μ ± 4σ
 *   t       ± the 99.5 % quantile, kept within 4..12 (few df have heavy tails)
 *   χ²      0 .. the 99.9 % quantile
 *   F       0 .. the 99.5 % quantile, at most 50
 * A finite test statistic outside that range widens it (plus 10 %), so the
 * shaded tail is always visible. ymax is the largest density over the pixel
 * columns, plus 10 %. For χ² and F the first columns are ignored because the
 * density can be infinite or very large at x = 0 (df < 2 / d1 < 2) and would
 * flatten the rest of the curve; the curve is then clipped at the top. */
static void window(double stat)
{
    switch (dist) {
    case DIST_NORM:
        xmin = p1 - 4 * p2, xmax = p1 + 4 * p2;
        break;
    case DIST_T: {
        double q = sd_invt(0.995, p1);
        double w = fmin(fmax(4, q), 12);
        xmin = -w, xmax = w;
        break;
    }
    case DIST_CHI2:
        xmin = 0, xmax = sd_invchi2(0.999, p1);
        break;
    default:
        xmin = 0, xmax = fmin(sd_invf(0.995, p1, p2), 50);
        break;
    }
    if (isfinite(stat)) {
        if (stat > xmax)
            xmax = stat + 0.1 * (xmax - xmin);
        if (stat < xmin)
            xmin = stat - 0.1 * (xmax - xmin);
    }
    if (!(xmax > xmin))
        xmax = xmin + 1;
    ymax = 0;
    for (int i = 1; i < GW; i++) {
        double y = pdf(xmin + (xmax - xmin) * i / (GW - 1));
        if (isfinite(y) && y > ymax && (dist == DIST_NORM || dist == DIST_T || i > 3))
            ymax = y;
    }
    if (!(ymax > 0))
        ymax = 1;
    ymax *= 1.1;
}

/* x lies in a shaded interval (NaN bounds never match: empty interval). */
static bool shaded(double x)
{
    return (x >= lo1 && x <= hi1) || (x >= lo2 && x <= hi2);
}

/* Draw the screen: for each pixel column px, x = xmin + (xmax - xmin) px / (GW - 1)
 * and py = ay - pdf(x) / ymax (GH - 1) (clipped to the top); shade, then
 * join to the previous column's point. Then the x axis, a tick at x = 0 when
 * it is inside the window, and the end labels. */
static void draw_draw(void)
{
    ui_cursor_hide();
    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, C_WHITE);
    gfx_text(FF, 4, BODY_Y + 1, line1, C_BLACK, GFX_TRANSPARENT);
    int ay = GY + GH - 1;
    double prev = DNAN;
    for (int px = 0; px < GW; px++) {
        double x = xmin + (xmax - xmin) * px / (GW - 1), y = pdf(x);
        double py = isfinite(y) ? ay - y / ymax * (GH - 1) : (double)GY;
        if (py < GY)
            py = GY;
        if (shaded(x))
            gfx_vline(GX + px, (int)lround(py), ay - (int)lround(py), C_LTBLUE);
        if (px > 0 && isfinite(prev))
            gfx_line(GX + px - 1, (int)lround(prev), GX + px, (int)lround(py), C_BLACK);
        prev = py;
    }
    gfx_hline(GX, ay, GW, C_AXES);
    if (xmin < 0 && xmax > 0) {
        int zx = (int)lround(-xmin / (xmax - xmin) * (GW - 1));
        gfx_vline(GX + zx, ay - 3, 4, C_AXES);
    }
    char a[24], b[24];
    fmt_num(xmin, 8, a, sizeof a);
    fmt_num(xmax, 8, b, sizeof b);
    gfx_text(&font_small, 2, ay - 14, a, C_GRAY, GFX_TRANSPARENT);
    gfx_text(&font_small, LCD_WIDTH - 2 - gfx_text_width(&font_small, b), ay - 14, b, C_GRAY, GFX_TRANSPARENT);
    gfx_text(FF, 4, UI_BOTTOM - 16, line2, C_BLUE, GFX_TRANSPARENT);
}

static void draw_key(int key, int mod)
{
    if (key == KEY_CLEAR || key == KEY_ENTER || key == KEY_F5) {
        ui_returning = true;
        ui_switch(ui_previous() ? ui_previous() : ui_home());
        ui_returning = false;
    }
}

static const softkeys_t draw_soft = {{{NULL}, {NULL}, {NULL}, {NULL}, {"back"}}};
const screen_t scr_stat_draw = {"statdraw", "DISTR DRAW", NULL, draw_draw, draw_key, NULL, NULL, NULL, "stat-distr",
                                &draw_soft};

static const char *dname(void)
{
    static const char *const n[] = {"", "Normal", "t", "chi²", "F"};
    return n[dist];
}

/* DISTR > DRAW: shade [lo, hi] (swapped if reversed; ±1e99 stand for ±∞ as
 * in normalcdf) under distribution d with parameters a, b, and show the area
 * (computed with the CDF, not from the pixels). If the area is not finite
 * (invalid parameters, e.g. σ <= 0) a message is shown instead. `res` is
 * unused. */
void sd_draw_open(int d, double a, double b, double lo, double hi, const sres_t *res)
{
    dist = d, p1 = a, p2 = b;
    if (lo > hi) {
        double t = lo;
        lo = hi, hi = t;
    }
    lo1 = lo, hi1 = hi;
    lo2 = hi2 = DNAN;
    area = cdf(lo, hi);
    window(DNAN);
    char n1[32], n2[32], ar[32];
    numfmt_double(lo, n1, sizeof n1);
    numfmt_double(hi, n2, sizeof n2);
    numfmt_double(area, ar, sizeof ar);
    snprintf(line1, sizeof line1, "%s  Area=%s", dname(), ar);
    snprintf(line2, sizeof line2, "low=%s  up=%s", lo <= -1e99 ? "-inf" : n1, hi >= 1e99 ? "inf" : n2);
    if (!isfinite(area)) {
        ui_message("DISTR DRAW", "Invalid distribution parameters.");
        return;
    }
    (void)res;
    ui_switch(&scr_stat_draw);
}

/* Draw of a test result r: the sampling distribution of its statistic
 * (z tests use the standard normal; t, χ², F use r->df1 / r->df2) with the
 * rejection side shaded: right tail for ALT_GT, left tail for ALT_LT, both
 * tails beyond ±|stat| for a two-sided z or t test. A two-sided F test
 * (2-SampFTest) shades only the tail on the statistic's side, chosen by the
 * upper-tail probability sd_f_q: < 0.5 means the statistic is in the right
 * tail. (χ² tests and ANOVA are always right-tailed: ALT_GT.) */
void sd_draw_result(const sres_t *r)
{
    dist = r->dist, p1 = dist == DIST_NORM ? 0 : r->df1, p2 = dist == DIST_NORM ? 1 : r->df2;
    double s = r->stat;
    lo2 = hi2 = DNAN;
    switch (r->alt) {
    case ALT_GT: lo1 = s, hi1 = DINF; break;
    case ALT_LT: lo1 = -DINF, hi1 = s; break;
    default:
        if (dist == DIST_NORM || dist == DIST_T) {
            lo1 = -DINF, hi1 = -fabs(s), lo2 = fabs(s), hi2 = DINF;
        } else { /* F: the tail on the statistic's side */
            double up = sd_f_q(s, p1, p2);
            if (up < 0.5)
                lo1 = s, hi1 = DINF;
            else
                lo1 = -DINF, hi1 = s;
        }
    }
    window(s);
    char a[32], b[32];
    numfmt_double(s, a, sizeof a);
    numfmt_double(r->p, b, sizeof b);
    static const char *const sn[] = {"", "z", "t", "chi²", "F"};
    snprintf(line1, sizeof line1, "%s  %s=%s", r->title, sn[dist], a);
    snprintf(line2, sizeof line2, "p=%s", b);
    ui_switch(&scr_stat_draw);
}
