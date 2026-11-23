/*
 * Graph cells (nb.h): y = f(x) plotted into an indexed image, the same
 * pixels on the screen and in the notebook's PNG. The function is compiled
 * by the CAS once (cas_compile_vars in x) and evaluated at every column; the
 * y range fits the values (near a pole, where the extremes are far out, without the
 * extreme 2 % at each end, so the pole does not flatten the rest); the curve is not drawn across a jump that
 * leaves the view (a pole). Axes where they cross the view, the corner values
 * in a small digit font.
 */
#include <math.h>
#include <string.h>
#include "py_mod.h"
#include "nb.h"
#include "py/formatfloat.h"

/* white, light grey, grey, blue, dark grey */
const uint16_t nb_plot_pal[NBP_N] = {0xFFFF, 0xE73C, 0x8410, 0x22BB, 0x4208};

/* 3 x 5 digits for the corner values: 0-9 - . e + (rows top to bottom, 3 bits each) */
static const char glyph_chars[] = "0123456789-.e+";
static const uint8_t glyphs[][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7},
    {7, 4, 7, 5, 7}, {7, 1, 1, 2, 2}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}, {0, 0, 7, 0, 0}, {0, 0, 0, 0, 2},
    {0, 7, 5, 6, 3}, {0, 2, 7, 2, 0},
};

static void px(uint8_t *pix, int w, int h, int x, int y, uint8_t c)
{
    if (x >= 0 && x < w && y >= 0 && y < h)
        pix[y * w + x] = c;
}

static void label(uint8_t *pix, int w, int h, int x, int y, const char *s, bool right)
{
    int n = (int)strlen(s);
    if (right)
        x -= 4 * n;
    for (int i = 0; i < n; i++) {
        const char *g = strchr(glyph_chars, s[i]);
        if (!g)
            continue;
        const uint8_t *rows = glyphs[g - glyph_chars];
        for (int r = -1; r < 6; r++) /* a white halo so the digits read over the grid */
            for (int c = -1; c < 4; c++)
                px(pix, w, h, x + 4 * i + c, y + r, NBP_BG);
        for (int r = 0; r < 5; r++)
            for (int c = 0; c < 3; c++)
                if (rows[r] >> (2 - c) & 1)
                    px(pix, w, h, x + 4 * i + c, y + r, NBP_TEXT);
    }
}

static void num(double v, char *b, size_t n)
{
    if (fabs(v) < 1e-12)
        v = 0;
    mp_format_float(v, b, n, 'g', 4, '\0');
}

static void line(uint8_t *pix, int w, int h, int x0, int y0, int x1, int y1, uint8_t c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (int guard = 0; guard < 4 * (w + h); guard++) {
        px(pix, w, h, x0, y0, c);
        px(pix, w, h, x0, y0 + 1, c); /* two pixels thick */
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy)
            err += dy, x0 += sx;
        if (e2 <= dx)
            err += dx, y0 += sy;
    }
}

static int cmp_d(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}
/* (no qsort in the module's C library use: a small insertion sort of <= NB_PLOT_W values) */
static void sort_d(double *v, int n)
{
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && cmp_d(&v[j - 1], &v[j]) > 0; j--) {
            double t = v[j];
            v[j] = v[j - 1], v[j - 1] = t;
        }
}

int nb_plot(const char *cas, double xmin, double xmax, uint8_t *pix, int w, int h, char *err, size_t n)
{
    static double ys[NB_PLOT_W], sorted[NB_PLOT_W];
    memset(pix, NBP_BG, (size_t)w * (size_t)h);
    if (w > NB_PLOT_W)
        w = NB_PLOT_W;
    if (!(xmin < xmax)) {
        py_api->snprintf(err, n, "the range needs xmin < xmax");
        return -1;
    }
    static const char *const vars[1] = {"x"};
    cas_fn *fn = NULL;
    cas_status s = py_api->cas_compile_vars(py_api->cas(), cas, vars, 1, &fn);
    if (s != CAS_OK || !fn) {
        py_api->snprintf(err, n, "cannot graph %s", cas);
        return -1;
    }
    int k = 0;
    for (int i = 0; i < w; i++) {
        double x = xmin + (xmax - xmin) * i / (w - 1);
        ys[i] = py_api->cas_fn_eval_vars(fn, &x);
        if (ys[i] == ys[i] && fabs(ys[i]) < 1e300)
            sorted[k++] = ys[i];
    }
    py_api->cas_fn_free(py_api->cas(), fn);
    if (!k) {
        py_api->snprintf(err, n, "no real values for %g < x < %g", xmin, xmax);
        return -1;
    }
    sort_d(sorted, k);
    double lo = sorted[0], hi = sorted[k - 1], tlo = sorted[k / 50], thi = sorted[k - 1 - k / 50];
    if (hi - lo > 20 * (thi - tlo)) /* a pole: the extreme 2 % left out */
        lo = tlo, hi = thi;
    if (hi - lo < 1e-9 * (fabs(lo) + fabs(hi)) || hi - lo < 1e-12)
        lo -= 1, hi += 1;
    double pad = (hi - lo) * 0.08;
    lo -= pad, hi += pad;
    /* grid lines at the axes' crossing, then the curve */
    int ax = (int)lround(-xmin / (xmax - xmin) * (w - 1)), ay = (int)lround((hi - 0) / (hi - lo) * (h - 1));
    for (int i = 0; i < w; i += 4)
        px(pix, w, h, i, h / 2, NBP_GRID);
    for (int j = 0; j < h; j += 4)
        px(pix, w, h, w / 2, j, NBP_GRID);
    if (ay >= 0 && ay < h)
        for (int i = 0; i < w; i++)
            px(pix, w, h, i, ay, NBP_AXIS);
    if (ax >= 0 && ax < w)
        for (int j = 0; j < h; j++)
            px(pix, w, h, ax, j, NBP_AXIS);
    int px0 = 0, py0 = 0;
    bool have = false;
    for (int i = 0; i < w; i++) {
        double y = ys[i];
        if (!(y == y) || fabs(y) > 1e300) {
            have = false;
            continue;
        }
        double fy = (hi - y) / (hi - lo) * (h - 1);
        int iy = fy < -h ? -h : fy > 2 * h ? 2 * h : (int)lround(fy); /* (a segment's off-view end) */
        if (have && !((iy < 0 && py0 >= h) || (iy >= h && py0 < 0))) /* not across a pole */
            line(pix, w, h, px0, py0, i, iy, NBP_CURVE);
        else
            px(pix, w, h, i, iy, NBP_CURVE);
        px0 = i, py0 = iy, have = true;
    }
    /* the frame and the corner values */
    for (int i = 0; i < w; i++)
        px(pix, w, h, i, 0, NBP_GRID), px(pix, w, h, i, h - 1, NBP_GRID);
    for (int j = 0; j < h; j++)
        px(pix, w, h, 0, j, NBP_GRID), px(pix, w, h, w - 1, j, NBP_GRID);
    char b[40];
    num(xmin, b, sizeof b), label(pix, w, h, 2, h - 7, b, false);
    num(xmax, b, sizeof b), label(pix, w, h, w - 2, h - 7, b, true);
    num(hi, b, sizeof b), label(pix, w, h, 2, 2, b, false);
    num(lo, b, sizeof b), label(pix, w, h, 2, h - 14, b, false);
    return 0;
}
