/*
 * Statistics computations of the STAT app (interface: stat_math.h).
 *
 * Everything here is a pure function of double arrays: no UI, no CAS, no
 * globals except the scratch area below. The STAT menus (stat_menu.c) read the
 * lists named in a form, call one of these functions and show the sres_t it
 * fills; tests/unit/test_stat.c checks the results against published values.
 *
 * Contents
 *   - sres_t helpers (sr_add / sr_get): a result is a list of labelled values.
 *   - Sorting (heap sort carrying a frequency array) and quartiles.
 *   - 1-Var / 2-Var statistics with optional frequency weights.
 *   - Regressions: linear (two forms), polynomial degree 2..4 (weighted normal
 *     equations on centred/scaled x), ln / exp / power (linear fit of
 *     log-transformed data), logistic and sinusoidal (Levenberg-Marquardt
 *     non-linear least squares from a computed initial guess), median-median.
 *   - Hypothesis tests (z, t, two-sample z/t, proportions, chi² GOF and
 *     two-way, two-sample F, LinRegTTest, one-way ANOVA) and confidence
 *     intervals. p-values and critical values come from the double-precision
 *     distribution functions of cas/include/cas_stat.h (sd_*_q = upper tail
 *     probability, sd_inv* = quantile).
 *
 * Conventions
 *   - Frequencies f[i] (NULL = all 1) are weights: N = Σf, means are weighted.
 *     Quartiles, median and mode need integer frequencies (the data are
 *     conceptually expanded, each x[i] repeated f[i] times).
 *   - Sample standard deviation Sx divides by N-1, population σx by N.
 *   - Functions return 0 on success, -1 with r->err set on failure (FAIL()).
 *   - Formulas follow the usual introductory-statistics definitions (e.g.
 *     Moore, McCabe & Craig, "Introduction to the Practice of Statistics";
 *     NIST/SEMATECH e-Handbook of Statistical Methods) and the result labels
 *     of common graphing calculators.
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os") /* flash budget: size over speed (not time-critical) */
#endif
#include "stat_math.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cas_stat.h"
#include "sections.h"

/* scratch for sorting (set by the app from its arena: stat_lists.c; tests).
 * st_scratch_cap is its size in doubles. Functions that need it check the
 * capacity and either skip optional results (quartiles) or fail. The STAT
 * menus also borrow it (stat_menu.c), so nothing here keeps data in it
 * between calls. */
double *st_scratch;
int st_scratch_cap;

/* Fail the current computation: set r->err to a user-visible message and
 * return -1 from the enclosing function (which must have an `sres_t *r`). */
#define FAIL(msg) return (r->err = (msg), -1)

/* Append the labelled value `lab` = v to the result (silently dropped when
 * SR_MAX lines are already used). `lab` must be a string literal or otherwise
 * outlive the result: only the pointer is stored. */
void sr_add(sres_t *r, const char *lab, double v)
{
    if (r->n < SR_MAX) {
        r->lab[r->n] = lab;
        r->val[r->n++] = v;
    }
}

/* Value of the line labelled `lab` (exact string match), NAN if absent. */
double sr_get(const sres_t *r, const char *lab)
{
    for (int i = 0; i < r->n; i++)
        if (!strcmp(r->lab[i], lab))
            return r->val[i];
    return DNAN;
}

/* Start a fresh result: everything zeroed (no lines, no error, DIST_NONE so
 * nothing to draw, empty equation) and the title set. */
static void sr_begin(sres_t *r, const char *title)
{
    memset(r, 0, sizeof *r);
    r->title = title;
}

/* ------------------------------------------------------------ sorting */
#define SWAP(arr, i, j) do { double t_ = arr[i]; arr[i] = arr[j]; arr[j] = t_; } while (0)
/* Heap-sort helper: restore the max-heap property of k[root..end) below
 * `root` by moving the root down ("sift down"), children of i at 2i+1, 2i+2.
 * Every swap in k is mirrored in c (when not NULL) so paired data stay
 * aligned. */
static void sift(double *k, double *c, int root, int end)
{
    for (int ch; (ch = 2 * root + 1) < end; root = ch) {
        if (ch + 1 < end && k[ch + 1] > k[ch])
            ch++;
        if (!(k[ch] > k[root]))
            return;
        SWAP(k, root, ch);
        if (c)
            SWAP(c, root, ch);
    }
}

/* heap sort of k[] ascending, carrying c[] (may be NULL) along.
 * Heap sort was chosen because it is in place (no scratch), O(n log n) in the
 * worst case and small; it is not stable, which does not matter for sorting
 * values (equal keys are interchangeable; their c[] entries are only summed
 * or paired with a key of the same value). First loop: build a max-heap
 * bottom-up; second loop: repeatedly move the maximum to the end. */
static void hsort(double *k, double *c, int n)
{
    for (int i = n / 2 - 1; i >= 0; i--)
        sift(k, c, i, n);
    for (int end = n - 1; end > 0; end--) {
        SWAP(k, 0, end);
        if (c)
            SWAP(c, 0, end);
        sift(k, c, 0, end);
    }
}

void st_sort(double *v, int n) { hsort(v, NULL, n); }

/* value at 0-based rank k of the expanded data (sorted keys, integer freqs).
 * Walks the cumulative frequencies: x[i] occupies ranks [cum_before, cum).
 * Linear in n; fine for the few ranks the quartiles need. */
static double at_rank(const double *x, const double *f, int n, double k)
{
    double cum = 0;
    for (int i = 0; i < n; i++) {
        cum += f ? f[i] : 1;
        if (k < cum)
            return x[i];
    }
    return x[n - 1];
}

/* median of expanded ranks [lo, lo+cnt): the middle element for an odd
 * count, the mean of the two middle elements for an even count. */
static double med_range(const double *x, const double *f, int n, double lo, double cnt)
{
    double m = lo + floor((cnt - 1) / 2);
    double a = at_rank(x, f, n, m);
    return fmod(cnt, 2) ? a : 0.5 * (a + at_rank(x, f, n, m + 1));
}

/* Median and quartiles of N expanded observations (sorted x, integer
 * frequencies f or NULL). The usual calculator method (Moore & McCabe; unlike
 * Tukey's hinges the median of odd N is excluded from both halves):
 * Q1 = median of the lower floor(N/2)
 * observations, Q3 = median of the upper floor(N/2); for odd N the median
 * itself belongs to neither half. With N = 1 all three equal the value. */
static void quartiles_f(const double *x, const double *f, int n, double N, double *q1, double *med, double *q3)
{
    *med = med_range(x, f, n, 0, N);
    double h = floor(N / 2);
    if (h < 1) {
        *q1 = *q3 = *med;
        return;
    }
    *q1 = med_range(x, f, n, 0, h);
    *q3 = med_range(x, f, n, N - h, h);
}

/* quartiles of n sorted values, each counted once (used for the Med-Med
 * group medians) */
void st_quartiles(const double *sorted, int n, double *q1, double *med, double *q3)
{
    quartiles_f(sorted, NULL, n, n, q1, med, q3);
}

/* ------------------------------------------------------------ 1-var */
/* Validate frequencies: each must be finite and >= 0 (NaN fails too).
 * Returns -1 on a bad frequency; otherwise 0 with *N = Σf and *integral =
 * whether every frequency is a whole number (needed for quartiles/mode). */
static int check_freq(const double *f, int n, double *N, bool *integral)
{
    *N = 0;
    *integral = true;
    for (int i = 0; i < n; i++) {
        double w = f ? f[i] : 1;
        if (!(w >= 0) || isinf(w))
            return -1;
        if (w != floor(w))
            *integral = false;
        *N += w;
    }
    return 0;
}

/* Weighted count, mean and sample standard deviation of x (the "Data" input
 * of the tests and intervals). Two passes for accuracy: mean first, then the
 * centred sum of squares Σf(x-x̄)², Sx = sqrt(SS/(N-1)) (NAN when N <= 1).
 * Returns -1 if Σf is not positive. Frequencies are not validated here
 * (st_onevar does that with check_freq first). */
int st_summary(const double *x, const double *f, int n, double *cnt, double *mean, double *sx)
{
    double N = 0, s = 0;
    for (int i = 0; i < n; i++) {
        double w = f ? f[i] : 1;
        N += w;
        s += w * x[i];
    }
    if (!(N > 0))
        return -1;
    double m = s / N, ss = 0;
    for (int i = 0; i < n; i++)
        ss += (f ? f[i] : 1) * (x[i] - m) * (x[i] - m);
    *cnt = N;
    *mean = m;
    *sx = N > 1 ? sqrt(ss / (N - 1)) : DNAN;
    return 0;
}

/* 1-Var Stats: x̄, Σx, Σx², Sx, σx, n, minX, Q1, Med, Q3, maxX, mode.
 * Values with frequency 0 are ignored for min/max. The order-based results
 * (quartiles, median, mode) need integer frequencies and 2n doubles of
 * scratch (sorted copy of x + its frequencies); otherwise they are NAN and
 * the moment-based results are still given. */
int st_onevar(const double *x, const double *f, int n, sres_t *r)
{
    sr_begin(r, "1-Var Stats");
    double N, mean, sx;
    bool integral;
    if (n < 1 || check_freq(f, n, &N, &integral) || st_summary(x, f, n, &N, &mean, &sx))
        FAIL("invalid data or frequencies");
    double sum = 0, sum2 = 0, mn = DINF, mx = -DINF, ss = 0;
    for (int i = 0; i < n; i++) {
        double w = f ? f[i] : 1;
        sum += w * x[i];
        sum2 += w * x[i] * x[i];
        ss += w * (x[i] - mean) * (x[i] - mean);
        if (w > 0) {
            if (x[i] < mn) mn = x[i];
            if (x[i] > mx) mx = x[i];
        }
    }
    sr_add(r, "xbar", mean);
    sr_add(r, "Σx", sum);
    sr_add(r, "Σx²", sum2);
    sr_add(r, "Sx", sx);
    sr_add(r, "sigmax", sqrt(ss / N));
    sr_add(r, "n", N);
    sr_add(r, "minX", mn);
    double q1 = DNAN, med = DNAN, q3 = DNAN, mode = DNAN;
    if (integral && st_scratch && 2 * n <= st_scratch_cap) {
        double *k = st_scratch, *c = st_scratch + n;
        memcpy(k, x, sizeof(double) * (size_t)n);
        for (int i = 0; i < n; i++)
            c[i] = f ? f[i] : 1;
        hsort(k, c, n);
        quartiles_f(k, c, n, N, &q1, &med, &q3);
        /* mode: value with the largest total frequency (none if all equal).
         * Walk runs of equal values in the sorted copy, summing their
         * frequencies. tie_all stays true when every distinct value has the
         * same total count (no mode, e.g. 1,2,3), except when there is only
         * one distinct value (first == N), which is then the mode. With
         * several values sharing the largest count, the smallest wins. */
        double best = 0, bestv = DNAN;
        bool tie_all = true;
        double first = -1;
        for (int i = 0; i < n;) {
            double cnt = 0, v = k[i];
            while (i < n && k[i] == v)
                cnt += c[i++];
            if (cnt <= 0)
                continue;
            if (first < 0)
                first = cnt;
            else if (cnt != first)
                tie_all = false;
            if (cnt > best)
                best = cnt, bestv = v;
        }
        if (!tie_all || first == N)
            mode = bestv;
    }
    sr_add(r, "Q1", q1);
    sr_add(r, "Med", med);
    sr_add(r, "Q3", q3);
    sr_add(r, "maxX", mx);
    sr_add(r, "mode", mode);
    return 0;
}

/* weighted centred sums of paired data, the basis of 2-Var Stats, the linear
 * fits and LinRegTTest:
 *   N = Σf, xm = x̄, ym = ȳ,
 *   sxx = Σf(x-x̄)², syy = Σf(y-ȳ)², sxy = Σf(x-x̄)(y-ȳ).
 * Centring before squaring (two passes) avoids the cancellation of the
 * one-pass formula Σx² - (Σx)²/N for data with a large mean. */
typedef struct {
    double N, xm, ym, sxx, syy, sxy;
} sums_t;

/* Compute sums_t over n pairs. Fails (-1) on a negative / infinite / NaN
 * frequency, a non-finite x or y (e.g. the NAN that linfit puts for the
 * logarithm of a non-positive value), or Σf = 0. */
static int sums(const double *x, const double *y, const double *f, int n, sums_t *s)
{
    memset(s, 0, sizeof *s);
    for (int i = 0; i < n; i++) {
        double w = f ? f[i] : 1;
        if (!(w >= 0) || isinf(w) || !isfinite(x[i]) || !isfinite(y[i]))
            return -1;
        s->N += w;
        s->xm += w * x[i];
        s->ym += w * y[i];
    }
    if (!(s->N > 0))
        return -1;
    s->xm /= s->N;
    s->ym /= s->N;
    for (int i = 0; i < n; i++) {
        double w = f ? f[i] : 1, dx = x[i] - s->xm, dy = y[i] - s->ym;
        s->sxx += w * dx * dx;
        s->syy += w * dy * dy;
        s->sxy += w * dx * dy;
    }
    return 0;
}

/* 2-Var Stats: means, raw sums (Σx, Σx², Σy, Σy², Σxy), sample and
 * population standard deviations of x and y, n and the ranges. The standard
 * deviations come from the centred sums (sums()); the raw sums are only
 * reported. Sx, Sy are NAN for N <= 1. */
int st_twovar(const double *x, const double *y, const double *f, int n, sres_t *r)
{
    sr_begin(r, "2-Var Stats");
    sums_t s;
    if (n < 1 || sums(x, y, f, n, &s))
        FAIL("invalid data or frequencies");
    double sx = 0, sx2 = 0, sy = 0, sy2 = 0, sxy = 0, mnx = DINF, mxx = -DINF, mny = DINF,
           mxy = -DINF;
    for (int i = 0; i < n; i++) {
        double w = f ? f[i] : 1;
        sx += w * x[i], sx2 += w * x[i] * x[i], sy += w * y[i], sy2 += w * y[i] * y[i], sxy += w * x[i] * y[i];
        if (w > 0) {
            mnx = fmin(mnx, x[i]), mxx = fmax(mxx, x[i]);
            mny = fmin(mny, y[i]), mxy = fmax(mxy, y[i]);
        }
    }
    double d = s.N > 1 ? s.N - 1 : DNAN;
    sr_add(r, "xbar", s.xm);
    sr_add(r, "Σx", sx);
    sr_add(r, "Σx²", sx2);
    sr_add(r, "Sx", sqrt(s.sxx / d));
    sr_add(r, "sigmax", sqrt(s.sxx / s.N));
    sr_add(r, "n", s.N);
    sr_add(r, "ybar", s.ym);
    sr_add(r, "Σy", sy);
    sr_add(r, "Σy²", sy2);
    sr_add(r, "Sy", sqrt(s.syy / d));
    sr_add(r, "sigmay", sqrt(s.syy / s.N));
    sr_add(r, "Σxy", sxy);
    sr_add(r, "minX", mnx);
    sr_add(r, "maxX", mxx);
    sr_add(r, "minY", mny);
    sr_add(r, "maxY", mxy);
    return 0;
}

/* ------------------------------------------------------------ linear algebra */
/* solve A x = b (m x m, row-major), partial pivoting; b receives x. 0 = ok.
 * Gaussian elimination: for each column pick the row with the largest
 * |pivot| (numerical stability), swap it up, eliminate below; then back
 * substitution. A is destroyed. Fails only on an exactly zero pivot, so a
 * nearly singular system returns large values; callers check isfinite. Used
 * for the small (m <= 5) normal-equation systems of the regressions. */
static int solve(double *A, double *b, int m)
{
    for (int c = 0; c < m; c++) {
        int p = c;
        for (int i = c + 1; i < m; i++)
            if (fabs(A[i * m + c]) > fabs(A[p * m + c]))
                p = i;
        if (!(fabs(A[p * m + c]) > 0))
            return -1;
        if (p != c) {
            for (int j = 0; j < m; j++) {
                double t = A[c * m + j]; A[c * m + j] = A[p * m + j]; A[p * m + j] = t;
            }
            double t = b[c]; b[c] = b[p]; b[p] = t;
        }
        for (int i = c + 1; i < m; i++) {
            double q = A[i * m + c] / A[c * m + c];
            for (int j = c; j < m; j++)
                A[i * m + j] -= q * A[c * m + j];
            b[i] -= q * b[c];
        }
    }
    for (int c = m - 1; c >= 0; c--) {
        double s = b[c];
        for (int j = c + 1; j < m; j++)
            s -= A[c * m + j] * b[j];
        b[c] = s / A[c * m + c];
    }
    return 0;
}

/* ------------------------------------------------------------ regressions */
/* Menu labels / result titles, indexed by REG_* (stat_menu.c builds the
 * CALC menu entries in the same order). */
const char *const reg_names[REG_COUNT] = {"LinReg(ax+b)", "LinReg(a+bx)", "QuadReg", "CubicReg", "QuartReg",
                                          "LnReg", "ExpReg", "PwrReg", "Logistic", "SinReg", "Med-Med"};

/* weighted polynomial least squares of degree d; c[0] = leading coefficient
 * (QuadReg d=2, CubicReg d=3, QuartReg d=4).
 *
 * Minimises Σ f (y - p(x))². Solving the normal equations (VᵀWV) b = VᵀWy
 * directly in powers of x is badly conditioned (e.g. x = years ~2000 makes
 * x⁴ ~ 1.6e13 next to 1). So the fit is done in the standardised variable
 * t = (x - x̄)/σx, where the Vandermonde columns 1, t, t², ... are of similar
 * size, and the coefficients are converted back to powers of x afterwards.
 * R² = 1 - SSE/SST with SST = Σf(y-ȳ)² (NAN when all y are equal).
 * Returns -1 if all x are equal or the system is singular (fewer distinct
 * x than d+1). */
static int polyfit(const double *x, const double *y, const double *f, int n, int d, double *c, double *r2)
{
    sums_t s;
    if (sums(x, y, f, n, &s))
        return -1;
    double sc = sqrt(s.sxx / s.N);
    if (!(sc > 0))
        return -1;
    int m = d + 1;
    /* accumulate the normal equations A b = rhs: A[j][k] = Σ f t^(j+k),
     * rhs[j] = Σ f t^j y (p[] holds the powers t^0..t^d of this point) */
    double A[25] = {0}, b[5] = {0};
    for (int i = 0; i < n; i++) {
        double w = f ? f[i] : 1, t = (x[i] - s.xm) / sc, p[5];
        p[0] = 1;
        for (int k = 1; k < m; k++)
            p[k] = p[k - 1] * t;
        for (int j = 0; j < m; j++) {
            b[j] += w * p[j] * y[i];
            for (int k = 0; k < m; k++)
                A[j * m + k] += w * p[j] * p[k];
        }
    }
    if (solve(A, b, m))
        return -1;
    /* y = sum b_k t^k, t = (x - xm)/sc  ->  coefficients of x^j
     * by the binomial theorem: (x - xm)^k = Σ_j C(k,j) x^j (-xm)^(k-j), so
     * coef(x^j) = Σ_{k>=j} b_k C(k,j) (-xm)^(k-j) / sc^k. c[] is stored
     * leading coefficient first (c[d-j] = coefficient of x^j). */
    for (int j = 0; j < m; j++) {
        double cj = 0;
        for (int k = j; k < m; k++) {
            double binom = 1;
            for (int i = 0; i < j; i++)
                binom = binom * (k - i) / (i + 1);
            cj += b[k] * binom * pow(-s.xm, k - j) / pow(sc, k);
        }
        c[d - j] = cj;
    }
    /* residual sum of squares, evaluated in t (Horner) to avoid the
     * rounding of the converted coefficients */
    double sse = 0;
    for (int i = 0; i < n; i++) {
        double w = f ? f[i] : 1, t = (x[i] - s.xm) / sc, v = 0;
        for (int k = m - 1; k >= 0; k--)
            v = v * t + b[k];
        sse += w * (y[i] - v) * (y[i] - v);
    }
    *r2 = s.syy > 0 ? 1 - sse / s.syy : DNAN;
    return 0;
}

/* model functions for Levenberg-Marquardt: value and gradient.
 * Returns model(p, x) and writes ∂model/∂p[j] to grad[j] (np entries). */
typedef double (*model_fn)(const double *p, double x, double *grad);

/* Logistic y = c/(1+a e^(-bx)), p = {a, b, c}. With E = e^(-bx), D = 1+aE:
 * ∂/∂a = -cE/D², ∂/∂b = c a x E/D², ∂/∂c = 1/D. */
static double m_logistic(const double *p, double x, double *g) /* c/(1+a e^(-bx)) */
{
    double e = exp(-p[1] * x), den = 1 + p[0] * e, v = p[2] / den;
    g[0] = -p[2] * e / (den * den);
    g[1] = p[2] * p[0] * x * e / (den * den);
    g[2] = 1 / den;
    return v;
}

/* Sinusoid y = a sin(bx+c)+d, p = {a, b, c, d}: ∂/∂a = sin(bx+c),
 * ∂/∂b = a x cos(bx+c), ∂/∂c = a cos(bx+c), ∂/∂d = 1. */
static double m_sin(const double *p, double x, double *g) /* a sin(bx+c)+d */
{
    double s = sin(p[1] * x + p[2]), co = cos(p[1] * x + p[2]);
    g[0] = s;
    g[1] = p[0] * x * co;
    g[2] = p[0] * co;
    g[3] = 1;
    return p[0] * s + p[3];
}

/* Weighted residual sum of squares Σ f (y - model(p, x))² (gradient unused). */
static double sse_of(model_fn fn, const double *p, const double *x, const double *y, const double *f, int n)
{
    double s = 0, g[4];
    for (int i = 0; i < n; i++) {
        double d = y[i] - fn(p, x[i], g);
        s += (f ? f[i] : 1) * d * d;
    }
    return s;
}

/* Levenberg-Marquardt non-linear least squares (Levenberg 1944, Marquardt
 * 1963; see Press et al., "Numerical Recipes", §15.5): refine the np <= 4
 * parameters p (in: initial guess, out: fit) of model `fn` to minimise
 * Σ f (y - model)².
 *
 * Each outer iteration linearises the model around p (Jacobian J from the
 * gradients) and builds the Gauss-Newton normal equations
 *     (JᵀWJ) δ = JᵀW r,     r = y - model(p).
 * Marquardt's damping adds λ·diag(JᵀWJ) to the matrix: small λ gives the
 * fast Gauss-Newton step, large λ a short step along the scaled gradient.
 * A step is accepted only if it does not increase the SSE (then λ /= 3);
 * otherwise λ *= 4 (×10 if the system is singular) and the step is retried,
 * up to 30 times. Stops when the relative SSE improvement drops below 1e-15
 * or when no damped step helps any more (both count as success; the caller
 * checks the parameters are finite). Returns -1 when the initial guess
 * already gives a non-finite SSE, 1 when 400 iterations did not converge
 * (still improving by more than 1e-9 relative in the last one: a failed fit;
 * slower progress at the cap counts as converged). */
static int lm_fit(model_fn fn, double *p, int np, const double *x, const double *y, const double *f, int n)
{
    double lambda = 1e-3, sse = sse_of(fn, p, x, y, f, n), rel = 0;
    if (!isfinite(sse))
        return -1;
    for (int it = 0; it < 400; it++) {
        /* A = JᵀWJ, b = JᵀW r at the current p */
        double A[16] = {0}, b[4] = {0}, g[4];
        for (int i = 0; i < n; i++) {
            double w = f ? f[i] : 1, d = y[i] - fn(p, x[i], g);
            for (int j = 0; j < np; j++) {
                b[j] += w * g[j] * d;
                for (int k = 0; k < np; k++)
                    A[j * np + k] += w * g[j] * g[k];
            }
        }
        /* try damped steps with increasing λ until one lowers the SSE
         * (a zero diagonal entry is replaced by 1e-12 so the damping still
         * regularises that parameter) */
        bool better = false;
        for (int tries = 0; tries < 30 && !better; tries++) {
            double M[16], dlt[4], q[4];
            memcpy(M, A, sizeof M);
            memcpy(dlt, b, sizeof dlt);
            for (int j = 0; j < np; j++)
                M[j * np + j] += lambda * (A[j * np + j] > 0 ? A[j * np + j] : 1e-12);
            if (solve(M, dlt, np)) {
                lambda *= 10;
                continue;
            }
            for (int j = 0; j < np; j++)
                q[j] = p[j] + dlt[j];
            double s2 = sse_of(fn, q, x, y, f, n);
            if (isfinite(s2) && s2 <= sse) {
                rel = (sse - s2) / (sse > 0 ? sse : 1);
                memcpy(p, q, sizeof(double) * (size_t)np);
                sse = s2;
                lambda = fmax(lambda / 3, 1e-12);
                better = true;
                if (rel < 1e-15)
                    return 0;
            } else {
                lambda *= 4;
            }
        }
        if (!better)
            return 0; /* no further progress: converged */
    }
    return rel > 1e-9 ? 1 : 0; /* iteration cap */
}

/* linear fit on transformed data: y' = a + b x' (weights f)
 * tx / ty select x' = ln x, y' = ln y (else the raw values). Ordinary least
 * squares: b = Sxy/Sxx, a = ȳ' - b x̄', and Pearson's correlation
 * r = Sxy/sqrt(Sxx Syy) of the (transformed) data (NAN if all y' are equal).
 * This gives LinReg, LnReg (y = a + b ln x), ExpReg (ln y = ln a + x ln b)
 * and PwrReg (ln y = ln a + b ln x); like other calculators, the exponential
 * and power fits minimise the error of ln y, not of y, and their r is that of
 * the transformed data. A logarithm of a value <= 0 becomes NAN, which makes
 * sums() fail. Uses 2n doubles of scratch for the transformed copies.
 * Returns -1 on failure (no scratch, bad data, all x' equal). */
static int linfit(const double *x, const double *y, const double *f, int n, int tx, int ty, double *a,
                  double *b, double *rr)
{
    if (!st_scratch || 2 * n > st_scratch_cap)
        return -1;
    double *X = st_scratch, *Y = st_scratch + n;
    for (int i = 0; i < n; i++) {
        X[i] = tx ? (x[i] > 0 ? log(x[i]) : DNAN) : x[i];
        Y[i] = ty ? (y[i] > 0 ? log(y[i]) : DNAN) : y[i];
    }
    sums_t s;
    if (sums(X, Y, f, n, &s) || !(s.sxx > 0))
        return -1;
    *b = s.sxy / s.sxx;
    *a = s.ym - *b * s.xm;
    *rr = s.syy > 0 ? s.sxy / sqrt(s.sxx * s.syy) : DNAN;
    return 0;
}

/* Initial guess for SinReg, p = {a, b, c, d} of a sin(bx+c)+d.
 *
 * Levenberg-Marquardt on a sinusoid converges only from a frequency close to
 * the right one (the SSE has many local minima in b), so the frequency is
 * found by a grid search. For a fixed angular frequency w the model
 *     y ≈ A sin(wx) + B cos(wx) + D
 * is linear in A, B, D: solve its 3x3 normal equations and keep the w with the
 * smallest SSE (a least-squares periodogram, cf. Lomb-Scargle). The amplitude
 * and phase follow from A sin + B cos = R sin(wx + φ) with R = hypot(A, B),
 * φ = atan2(B, A).
 * Grid: from w = π/range (half a period across the data) to π/dmin (the
 * Nyquist-like limit of two samples per period at the smallest x spacing), in
 * steps of 2π/(8·range) (an eighth of a cycle of phase drift across the data),
 * capped at ~2000 steps. Needs n doubles of scratch (sorted x). Returns -1 if
 * all x are equal or no grid point gave a solvable system. */
static int sin_guess(const double *x, const double *y, const double *f, int n, double *p)
{
    if (!st_scratch || n > st_scratch_cap)
        return -1;
    double *k = st_scratch;
    memcpy(k, x, sizeof(double) * (size_t)n);
    hsort(k, NULL, n);
    double range = k[n - 1] - k[0], dmin = DINF;
    for (int i = 1; i < n; i++)
        if (k[i] > k[i - 1] && k[i] - k[i - 1] < dmin)
            dmin = k[i] - k[i - 1];
    if (!(range > 0) || !isfinite(dmin))
        return -1;
    double wlo = M_PI / range, whi = M_PI / dmin, step = 2 * M_PI / (range * 8);
    if ((whi - wlo) / step > 2000)
        step = (whi - wlo) / 2000;
    double best = DINF;
    for (double w = wlo; w <= whi * 1.0000001; w += step) {
        /* y ~ A sin(wx) + B cos(wx) + D */
        double M[9] = {0}, v[3] = {0};
        for (int i = 0; i < n; i++) {
            double wt = f ? f[i] : 1, q[3] = {sin(w * x[i]), cos(w * x[i]), 1};
            for (int j = 0; j < 3; j++) {
                v[j] += wt * q[j] * y[i];
                for (int l = 0; l < 3; l++)
                    M[j * 3 + l] += wt * q[j] * q[l];
            }
        }
        if (solve(M, v, 3))
            continue;
        double pp[4] = {hypot(v[0], v[1]), w, atan2(v[1], v[0]), v[2]};
        double s = sse_of(m_sin, pp, x, y, f, n);
        if (s < best) {
            best = s;
            memcpy(p, pp, sizeof pp);
        }
    }
    return isfinite(best) ? 0 : -1;
}

/* Median-median line y = a x + b (resistant line; Tukey 1977, the
 * common "Med-Med" convention):
 *  1. sort the points by x and split them into three groups of sizes
 *     (g, g, g), (g, g+1, g) or (g+1, g, g+1) for n = 3g, 3g+1, 3g+2 (the
 *     extra points keep the outer groups equal in size);
 *  2. summary point of each group = (median of its x, median of its y),
 *     the medians taken independently;
 *  3. slope a from the outer summary points: (my3 - my1)/(mx3 - mx1);
 *  4. intercept b = mean of the three intercepts my_j - a·mx_j, i.e. the line
 *     is moved a third of the way toward the middle point.
 * Needs 3n doubles of scratch (sorted x, carried y, one group). a and b are
 * not finite when mx1 == mx3 (the caller reports that). */
static void medmed(const double *x, const double *y, int n, double *a, double *b)
{
    double *k = st_scratch, *c = st_scratch + n;
    memcpy(k, x, sizeof(double) * (size_t)n);
    memcpy(c, y, sizeof(double) * (size_t)n);
    hsort(k, c, n);
    int g = n / 3, s1, s2;
    if (n % 3 == 0)
        s1 = g, s2 = g;
    else if (n % 3 == 1)
        s1 = g, s2 = g + 1;
    else
        s1 = g + 1, s2 = g;
    int start[3] = {0, s1, s1 + s2}, len[3] = {s1, s2, n - s1 - s2};
    double mx[3], my[3];
    double *tmp = st_scratch + 2 * n;
    for (int j = 0; j < 3; j++) {
        double q1, q3;
        memcpy(tmp, k + start[j], sizeof(double) * (size_t)len[j]);
        st_quartiles(tmp, len[j], &q1, &mx[j], &q3);
        memcpy(tmp, c + start[j], sizeof(double) * (size_t)len[j]);
        hsort(tmp, NULL, len[j]);
        st_quartiles(tmp, len[j], &q1, &my[j], &q3);
    }
    *a = (my[2] - my[0]) / (mx[2] - mx[0]);
    *b = (my[0] + my[1] + my[2] - *a * (mx[0] + mx[1] + mx[2])) / 3;
}

static const char *const coef_names[] = {"a", "b", "c", "d", "e"};

/* Fit regression `type` (REG_*) to the points (x[i], y[i]) with frequencies
 * f (NULL = 1). Fills g (model, coefficients, r / r²) and the result r: the
 * coefficients a, b, ... in the model's naming, then R² (polynomials: the
 * coefficient of determination) or r² and r (linear and linearised models),
 * and r->eq, the fitted equation in CAS syntax for RegEQ (stat_menu.c stores
 * it into a Y= function). Logistic, SinReg and Med-Med report no r.
 * Returns 0, or -1 with r->err set (too few points, bad data, no
 * convergence). Coefficient storage per model (st_reg_eval / st_reg_expr):
 *   LinReg(ax+b), Med-Med  c = {a, b}       y = a x + b
 *   LinReg(a+bx), LnReg    c = {a, b}       y = a + b x,  y = a + b ln x
 *   ExpReg, PwrReg         c = {a, b}       y = a b^x,    y = a x^b
 *   Quad/Cubic/QuartReg    c = {a, b, ...}  leading coefficient first
 *   Logistic               c = {a, b, c}    y = c/(1 + a e^(-bx))
 *   SinReg                 c = {a, b, c, d} y = a sin(bx + c) + d */
int st_regress(int type, const double *x, const double *y, const double *f, int n, sreg_t *g, sres_t *r)
{
    sr_begin(r, reg_names[type]);
    memset(g, 0, sizeof *g);
    g->type = type;
    g->r = g->r2 = DNAN;
    /* fewest points that determine each model (one per parameter; Med-Med
     * needs one per group) */
    static const uint8_t minpts[REG_COUNT] = {2, 2, 3, 4, 5, 2, 2, 2, 3, 4, 3};
    if (n < minpts[type])
        FAIL("not enough data points");
    double a, b, rr;
    switch (type) {
    case REG_LINAXB: case REG_LINABX: case REG_LN: case REG_EXP: case REG_PWR: {
        /* one straight-line fit y' = a + b x' on (possibly log-) transformed
         * data, then map (a, b) back to the model's coefficients:
         * ExpReg y = A·B^x: ln y = ln A + x ln B -> A = e^a, B = e^b;
         * PwrReg y = A·x^B: ln y = ln A + B ln x -> A = e^a, B = b. */
        int tx = type == REG_LN || type == REG_PWR, ty = type == REG_EXP || type == REG_PWR;
        if (linfit(x, y, f, n, tx, ty, &a, &b, &rr))
            FAIL(tx || ty ? "data must be positive (logarithm)" : "x values must not all be equal");
        g->nc = 2;
        g->r = rr;
        g->r2 = rr * rr;
        if (type == REG_LINAXB)
            g->c[0] = b, g->c[1] = a;
        else if (type == REG_LINABX || type == REG_LN)
            g->c[0] = a, g->c[1] = b;
        else if (type == REG_EXP)
            g->c[0] = exp(a), g->c[1] = exp(b);
        else
            g->c[0] = exp(a), g->c[1] = b;
        break;
    }
    case REG_QUAD: case REG_CUBIC: case REG_QUART: {
        int d = type - REG_QUAD + 2;
        if (polyfit(x, y, f, n, d, g->c, &g->r2))
            FAIL("singular data (x values?)");
        g->nc = d + 1;
        break;
    }
    case REG_LOGISTIC: {
        /* initial guess: c above max y, then linearise ln(c/y - 1) = ln a - b x
         * (from y = c/(1 + a e^(-bx)) => c/y - 1 = a e^(-bx)); an unweighted
         * line through the points with y > 0 gives ln a (intercept) and -b
         * (slope). If that is impossible the guess stays a = b = 1. Then
         * Levenberg-Marquardt refines all three parameters. */
        double mx = -DINF;
        for (int i = 0; i < n; i++)
            mx = fmax(mx, y[i]);
        if (!(mx > 0))
            FAIL("logistic fit needs positive y values");
        double p[3] = {1, 1, mx * 1.05};
        if (st_scratch && 2 * n <= st_scratch_cap) {
            double *Y = st_scratch + n, *X = st_scratch;
            int m = 0;
            for (int i = 0; i < n; i++)
                if (y[i] > 0) {
                    X[m] = x[i];
                    Y[m++] = log(p[2] / y[i] - 1);
                }
            sums_t s;
            if (m >= 2 && !sums(X, Y, NULL, m, &s) && s.sxx > 0) {
                double sl = s.sxy / s.sxx;
                p[0] = exp(s.ym - sl * s.xm);
                p[1] = -sl;
            }
        }
        if (lm_fit(m_logistic, p, 3, x, y, f, n) || !isfinite(p[0] + p[1] + p[2]))
            FAIL("logistic fit did not converge");
        memcpy(g->c, p, sizeof p);
        g->nc = 3;
        break;
    }
    case REG_SIN: {
        /* grid-search guess + Levenberg-Marquardt, then normalise the result
         * so that a > 0, b > 0 and c in [-π, π] (the same curve has many
         * parameterisations: a sin(θ) = -a sin(θ + π)). */
        double p[4];
        if (sin_guess(x, y, f, n, p) || lm_fit(m_sin, p, 4, x, y, f, n) || !isfinite(p[0] + p[1] + p[2] + p[3]))
            FAIL("sinusoidal fit did not converge");
        if (p[0] < 0)
            p[0] = -p[0], p[2] += M_PI;
        if (p[1] < 0)
            p[1] = -p[1], p[2] = M_PI - p[2]; /* a sin(-bx+c) = a sin(bx + pi - c) */
        p[2] = remainder(p[2], 2 * M_PI);
        memcpy(g->c, p, sizeof p);
        g->nc = 4;
        break;
    }
    case REG_MEDMED:
        /* medians of groups are defined on individual points, so only unit
         * frequencies are accepted */
        if (f)
            for (int i = 0; i < n; i++)
                if (f[i] != 1)
                    FAIL("Med-Med does not take frequencies");
        if (!st_scratch || 3 * n > st_scratch_cap)
            FAIL("too much data");
        medmed(x, y, n, &a, &b);
        if (!isfinite(a) || !isfinite(b))
            FAIL("x values of the outer groups must differ");
        g->c[0] = a, g->c[1] = b;
        g->nc = 2;
        break;
    }
    for (int i = 0; i < g->nc; i++)
        sr_add(r, coef_names[i], g->c[i]);
    if (!isnan(g->r2))
        sr_add(r, type >= REG_QUAD && type <= REG_QUART ? "R²" : "r²", g->r2);
    if (!isnan(g->r))
        sr_add(r, "r", g->r);
    st_reg_expr(g, r->eq, sizeof r->eq);
    return 0;
}

/* Value of the fitted model at x (residuals RESID, stat plots). Polynomial
 * coefficients are leading-first, so Horner's rule runs over c[0..nc). LnReg
 * outside its domain (x <= 0) gives NAN. */
double st_reg_eval(const sreg_t *g, double x)
{
    const double *c = g->c;
    switch (g->type) {
    case REG_LINAXB: case REG_MEDMED: return c[0] * x + c[1];
    case REG_LINABX: return c[0] + c[1] * x;
    case REG_QUAD: case REG_CUBIC: case REG_QUART: {
        double v = 0;
        for (int i = 0; i < g->nc; i++)
            v = v * x + c[i];
        return v;
    }
    case REG_LN: return x > 0 ? c[0] + c[1] * log(x) : DNAN;
    case REG_EXP: return c[0] * pow(c[1], x);
    case REG_PWR: return c[0] * pow(x, c[1]);
    case REG_LOGISTIC: return c[2] / (1 + c[0] * exp(-c[1] * x));
    case REG_SIN: return c[0] * sin(c[1] * x + c[2]) + c[3];
    }
    return DNAN;
}

/* number as CAS text: shortest of %.15g / %.17g that round-trips; parenthesised if negative
 * (%.17g always round-trips an IEEE double; %.15g is tried first because it
 * avoids noise digits such as 0.10000000000000001). The C exponent form is
 * rewritten for the CAS, and the parentheses make the text safe to splice
 * into an expression ("2*x+(-3)", "x^(1.5*10^(-7))"). */
void st_num_text(char *b, size_t n, double v)
{
    char t[40];
    snprintf(t, sizeof t, "%.15g", v);
    if (strtod(t, NULL) != v)
        snprintf(t, sizeof t, "%.17g", v);
    char *e = strchr(t, 'e');
    if (e) { /* 1e-05 -> 1*10^(-5) */
        *e = 0;
        snprintf(b, n, "(%s*10^(%d))", t, atoi(e + 1));
        return;
    }
    snprintf(b, n, v < 0 ? "(%s)" : "%s", t);
}

/* The fitted equation in CAS syntax with full-precision coefficients
 * (st_num_text), in x: e.g. "0.5*x+(-2)". Empty for an unknown type. The
 * text is what RegEQ stores into Y= and what VARS > Statistics shows. */
void st_reg_expr(const sreg_t *g, char *buf, size_t n)
{
    char c[5][48];
    for (int i = 0; i < g->nc; i++)
        st_num_text(c[i], sizeof c[i], g->c[i]);
    switch (g->type) {
    case REG_LINAXB: case REG_MEDMED: snprintf(buf, n, "%s*x+%s", c[0], c[1]); break;
    case REG_LINABX: snprintf(buf, n, "%s+%s*x", c[0], c[1]); break;
    case REG_QUAD: snprintf(buf, n, "%s*x^2+%s*x+%s", c[0], c[1], c[2]); break;
    case REG_CUBIC: snprintf(buf, n, "%s*x^3+%s*x^2+%s*x+%s", c[0], c[1], c[2], c[3]); break;
    case REG_QUART: snprintf(buf, n, "%s*x^4+%s*x^3+%s*x^2+%s*x+%s", c[0], c[1], c[2], c[3], c[4]); break;
    case REG_LN: snprintf(buf, n, "%s+%s*ln(x)", c[0], c[1]); break;
    case REG_EXP: snprintf(buf, n, "%s*%s^x", c[0], c[1]); break;
    case REG_PWR: snprintf(buf, n, "%s*x^%s", c[0], c[1]); break;
    case REG_LOGISTIC: snprintf(buf, n, "%s/(1+%s*e^(-%s*x))", c[2], c[0], c[1]); break;
    case REG_SIN: snprintf(buf, n, "%s*sin(%s*x+%s)+%s", c[0], c[1], c[2], c[3]); break;
    default: buf[0] = 0;
    }
}

/* ------------------------------------------------------------ tests */
/* Every test fills r with the statistic, its p-value and the inputs echoed
 * back, and sets r->dist / alt / stat / df1 / df2 so stat_draw.c can draw the
 * distribution of the statistic under H0 with the p-value area shaded.
 * `alt` is the alternative hypothesis: ALT_NE (two-sided), ALT_LT, ALT_GT. */

/* Confidence level as a fraction: 95 means 0.95, 0.95 stays 0.95. */
static double conf(double C) { return C > 1 ? C / 100 : C; }

/* p-value of a statistic with upper-tail function q (symmetric: q(-s) = 1-q(s))
 *   H1 ">": P(S >= s) = q(s);  H1 "<": P(S <= s) = q(-s);
 *   H1 "≠": 2 P(S >= |s|) = 2 q(|s|). */
static double pval_sym(double z, int alt, double (*q)(double, double), double df)
{
    if (alt == ALT_GT)
        return q(z, df);
    if (alt == ALT_LT)
        return q(-z, df);
    return 2 * q(fabs(z), df);
}
/* standard normal upper tail with pval_sym's signature (df unused) */
static double nq(double z, double df) { (void)df; return sd_norm_q(z); }

/* Record a z statistic: drawing data (standard normal), p-value and the
 * "z" and "p" lines. Fails if z is not finite (e.g. a zero standard error). */
static int ztail(sres_t *r, double z, int alt)
{
    r->dist = DIST_NORM, r->alt = alt, r->stat = z;
    r->p = pval_sym(z, alt, nq, 0);
    sr_add(r, "z", z);
    sr_add(r, "p", r->p);
    return isfinite(z) ? 0 : (r->err = "invalid input", -1);
}

/* Same for a Student t statistic with df degrees of freedom (df may be
 * fractional: Welch). Adds the "t", "p" and "df" lines. */
static int ttail(sres_t *r, double t, double df, int alt)
{
    r->dist = DIST_T, r->alt = alt, r->stat = t, r->df1 = df;
    r->p = pval_sym(t, alt, sd_t_q, df);
    sr_add(r, "t", t);
    sr_add(r, "p", r->p);
    sr_add(r, "df", df);
    return isfinite(t) && df > 0 ? 0 : (r->err = "invalid input", -1);
}

/* One-sample z test of H0: μ = μ0 with known σ: z = (x̄ - μ0)/(σ/√n). */
int st_ztest(double mu0, double sigma, double xbar, double n, int alt, sres_t *r)
{
    sr_begin(r, "Z-Test");
    if (!(sigma > 0) || !(n >= 1))
        FAIL("need sigma > 0 and n >= 1");
    if (ztail(r, (xbar - mu0) / (sigma / sqrt(n)), alt))
        return -1;
    sr_add(r, "xbar", xbar);
    sr_add(r, "n", n);
    return 0;
}

/* One-sample t test of H0: μ = μ0 with unknown σ:
 * t = (x̄ - μ0)/(Sx/√n), df = n - 1. */
int st_ttest(double mu0, double xbar, double sx, double n, int alt, sres_t *r)
{
    sr_begin(r, "T-Test");
    if (!(sx > 0) || !(n >= 2))
        FAIL("need Sx > 0 and n >= 2");
    if (ttail(r, (xbar - mu0) / (sx / sqrt(n)), n - 1, alt))
        return -1;
    sr_add(r, "xbar", xbar);
    sr_add(r, "Sx", sx);
    sr_add(r, "n", n);
    return 0;
}

/* Two-sample z test of H0: μ1 = μ2 with known σ1, σ2:
 * z = (x̄1 - x̄2)/sqrt(σ1²/n1 + σ2²/n2). */
int st_2sampz(double s1, double s2, double x1, double n1, double x2, double n2, int alt, sres_t *r)
{
    sr_begin(r, "2-SampZTest");
    if (!(s1 > 0 && s2 > 0 && n1 >= 1 && n2 >= 1))
        FAIL("need sigma > 0 and n >= 1");
    if (ztail(r, (x1 - x2) / sqrt(s1 * s1 / n1 + s2 * s2 / n2), alt))
        return -1;
    sr_add(r, "xbar1", x1);
    sr_add(r, "xbar2", x2);
    sr_add(r, "n1", n1);
    sr_add(r, "n2", n2);
    return 0;
}

/* standard error and df of a two-sample t statistic
 * Pooled (equal variances assumed):
 *   Sp² = ((n1-1)S1² + (n2-1)S2²)/(n1+n2-2),  SE = Sp sqrt(1/n1 + 1/n2),
 *   df = n1 + n2 - 2.
 * Not pooled (Welch's test): SE = sqrt(S1²/n1 + S2²/n2) and the
 * Welch-Satterthwaite approximation of the degrees of freedom
 *   df = (v1 + v2)² / (v1²/(n1-1) + v2²/(n2-1)),  vi = Si²/ni
 * (usually not an integer; *sp is NAN then). */
static void twot(double s1, double n1, double s2, double n2, bool pooled, double *se, double *df, double *sp)
{
    double v1 = s1 * s1 / n1, v2 = s2 * s2 / n2;
    if (pooled) {
        *df = n1 + n2 - 2;
        *sp = sqrt(((n1 - 1) * s1 * s1 + (n2 - 1) * s2 * s2) / *df);
        *se = *sp * sqrt(1 / n1 + 1 / n2);
    } else {
        *se = sqrt(v1 + v2);
        *df = (v1 + v2) * (v1 + v2) / (v1 * v1 / (n1 - 1) + v2 * v2 / (n2 - 1));
        *sp = DNAN;
    }
}

/* Two-sample t test of H0: μ1 = μ2: t = (x̄1 - x̄2)/SE with SE and df from
 * twot() (pooled or Welch). */
int st_2sampt(double x1, double s1, double n1, double x2, double s2, double n2, int alt, bool pooled, sres_t *r)
{
    sr_begin(r, "2-SampTTest");
    if (!(s1 > 0 && s2 > 0 && n1 >= 2 && n2 >= 2))
        FAIL("need Sx > 0 and n >= 2");
    double se, df, sp;
    twot(s1, n1, s2, n2, pooled, &se, &df, &sp);
    if (ttail(r, (x1 - x2) / se, df, alt))
        return -1;
    sr_add(r, "xbar1", x1);
    sr_add(r, "xbar2", x2);
    sr_add(r, "Sx1", s1);
    sr_add(r, "Sx2", s2);
    if (pooled)
        sr_add(r, "Sxp", sp);
    sr_add(r, "n1", n1);
    sr_add(r, "n2", n2);
    return 0;
}

/* One-proportion z test of H0: p = p0 (x successes in n trials), normal
 * approximation with the standard error under H0:
 * z = (p̂ - p0)/sqrt(p0(1-p0)/n), p̂ = x/n. */
int st_1propz(double p0, double x, double n, int alt, sres_t *r)
{
    sr_begin(r, "1-PropZTest");
    if (!(p0 > 0 && p0 < 1) || !(n >= 1) || !(x >= 0 && x <= n))
        FAIL("need 0 < p0 < 1 and 0 <= x <= n");
    double ph = x / n;
    if (ztail(r, (ph - p0) / sqrt(p0 * (1 - p0) / n), alt))
        return -1;
    sr_add(r, "p^", ph);
    sr_add(r, "n", n);
    return 0;
}

/* Two-proportion z test of H0: p1 = p2. Under H0 both samples share one
 * proportion, estimated by pooling: p̂ = (x1 + x2)/(n1 + n2), and
 * z = (p̂1 - p̂2)/sqrt(p̂(1-p̂)(1/n1 + 1/n2)). */
int st_2propz(double x1, double n1, double x2, double n2, int alt, sres_t *r)
{
    sr_begin(r, "2-PropZTest");
    if (!(n1 >= 1 && n2 >= 1 && x1 >= 0 && x2 >= 0 && x1 <= n1 && x2 <= n2))
        FAIL("need 0 <= x <= n");
    double p1 = x1 / n1, p2 = x2 / n2, p = (x1 + x2) / (n1 + n2);
    if (!(p > 0 && p < 1))
        FAIL("pooled proportion must be between 0 and 1");
    if (ztail(r, (p1 - p2) / sqrt(p * (1 - p) * (1 / n1 + 1 / n2)), alt))
        return -1;
    sr_add(r, "p^1", p1);
    sr_add(r, "p^2", p2);
    sr_add(r, "p^", p);
    sr_add(r, "n1", n1);
    sr_add(r, "n2", n2);
    return 0;
}

/* Record a χ² statistic: always an upper-tail test (large χ² = poor fit),
 * p = P(χ²(df) >= x2). Adds "chi²", "p", "df". */
static void chi2tail(sres_t *r, double x2, double df)
{
    r->dist = DIST_CHI2, r->alt = ALT_GT, r->stat = x2, r->df1 = df;
    r->p = sd_chi2_q(x2, df);
    sr_add(r, "chi²", x2);
    sr_add(r, "p", r->p);
    sr_add(r, "df", df);
}

/* Pearson χ² goodness-of-fit test: χ² = Σ (O - E)²/E over the n categories,
 * with df given by the user (normally n - 1 minus estimated parameters).
 * contrib (may be NULL) receives each category's term (O - E)²/E; the menu
 * stores it as the list CNTRB. Every expected count must be > 0. */
int st_chi2gof(const double *obs, const double *exp, int n, double df, sres_t *r, double *contrib)
{
    sr_begin(r, "chi² GOF-Test");
    if (n < 1 || !(df >= 1))
        FAIL("need data and df >= 1");
    double x2 = 0;
    for (int i = 0; i < n; i++) {
        if (!(exp[i] > 0))
            FAIL("expected values must be positive");
        double c = (obs[i] - exp[i]) * (obs[i] - exp[i]) / exp[i];
        if (contrib)
            contrib[i] = c;
        x2 += c;
    }
    chi2tail(r, x2, df);
    return 0;
}

/* χ² test of independence / homogeneity on an r x c contingency table
 * (at most 32 x 32). Expected counts under independence:
 * E_ij = (row total i)(column total j)/(grand total); χ² = Σ (O - E)²/E,
 * df = (r - 1)(c - 1). A zero row or column total (E = 0, or 0/0 for an
 * all-zero table) is refused. */
int st_chi2test(const double *obs, int rows, int cols, sres_t *r, double *expected)
{
    sr_begin(r, "chi²-Test");
    if (rows < 2 || cols < 2 || rows > 32 || cols > 32)
        FAIL("observed matrix must be at least 2x2");
    double rs[32] = {0}, cs[32] = {0}, tot = 0;
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < cols; j++) {
            double o = obs[i * cols + j];
            if (!(o >= 0))
                FAIL("observed counts must be >= 0");
            rs[i] += o, cs[j] += o, tot += o;
        }
    double x2 = 0;
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < cols; j++) {
            double e = rs[i] * cs[j] / tot;
            if (!(e > 0))
                FAIL("a row or column total is zero");
            if (expected)
                expected[i * cols + j] = e;
            double d = obs[i * cols + j] - e;
            x2 += d * d / e;
        }
    chi2tail(r, x2, (double)(rows - 1) * (cols - 1));
    return 0;
}

/* Two-sample F test of H0: σ1 = σ2: F = S1²/S2² with (n1-1, n2-1) degrees of
 * freedom. The F distribution is not symmetric, so the two-sided p-value is
 * twice the smaller tail. */
int st_2sampf(double s1, double n1, double s2, double n2, int alt, sres_t *r)
{
    sr_begin(r, "2-SampFTest");
    if (!(s1 > 0 && s2 > 0 && n1 >= 2 && n2 >= 2))
        FAIL("need Sx > 0 and n >= 2");
    double F = s1 * s1 / (s2 * s2), d1 = n1 - 1, d2 = n2 - 1;
    double up = sd_f_q(F, d1, d2), lo = 1 - up;
    r->dist = DIST_F, r->alt = alt, r->stat = F, r->df1 = d1, r->df2 = d2;
    r->p = alt == ALT_GT ? up : alt == ALT_LT ? lo : 2 * fmin(up, lo);
    sr_add(r, "F", F);
    sr_add(r, "p", r->p);
    sr_add(r, "Sx1", s1);
    sr_add(r, "Sx2", s2);
    sr_add(r, "n1", n1);
    sr_add(r, "n2", n2);
    return 0;
}

/* Linear regression t test of H0: β = 0 (slope; equivalently ρ = 0).
 * Least-squares line y = a + b x with b = Sxy/Sxx; residual sum of squares
 * SSE = Syy - b Sxy (clamped at 0 against rounding); standard error of the
 * regression s = sqrt(SSE/(N-2)); SE(b) = s/sqrt(Sxx); t = b/SE(b), df = N-2.
 * Also gives r, r² and the equation (RegEQ, as LinReg(ax+b)). */
int st_linregt(const double *x, const double *y, const double *f, int n, int alt, sres_t *r)
{
    sr_begin(r, "LinRegTTest");
    sums_t s;
    if (sums(x, y, f, n, &s) || !(s.N > 2) || !(s.sxx > 0))
        FAIL("need at least 3 points with different x");
    double b = s.sxy / s.sxx, a = s.ym - b * s.xm;
    double sse = s.syy - b * s.sxy;
    if (sse < 0)
        sse = 0;
    double df = s.N - 2, se = sqrt(sse / df), t = b / (se / sqrt(s.sxx));
    double rr = s.sxy / sqrt(s.sxx * s.syy);
    if (ttail(r, t, df, alt))
        return -1;
    sr_add(r, "a", a);
    sr_add(r, "b", b);
    sr_add(r, "s", se);
    sr_add(r, "r²", rr * rr);
    sr_add(r, "r", rr);
    sreg_t g = {REG_LINAXB, {b, a}, 2, rr, rr * rr};
    st_reg_expr(&g, r->eq, sizeof r->eq);
    return 0;
}

/* One-way ANOVA of H0: all k group means are equal.
 *   SS factor (between) = Σ_j n_j (x̄_j - x̄)²,      df = k - 1
 *   SS error  (within)  = Σ_j Σ_i (x_ij - x̄_j)²,   df = N - k
 *   MS = SS/df, F = MS factor / MS error, p = P(F(k-1, N-k) >= F);
 *   Sxp = sqrt(MS error), the pooled standard deviation.
 * Fails with fewer than 2 groups, an empty group, or no variation within
 * the groups (F undefined). */
int st_anova(const double *const *data, const int *len, int k, sres_t *r)
{
    sr_begin(r, "One-way ANOVA");
    if (k < 2)
        FAIL("need at least 2 groups");
    double N = 0, tot = 0, ssw = 0, ssb = 0;
    for (int j = 0; j < k; j++) {
        if (len[j] < 1)
            FAIL("empty group");
        for (int i = 0; i < len[j]; i++)
            tot += data[j][i];
        N += len[j];
    }
    double gm = tot / N;
    for (int j = 0; j < k; j++) {
        double m = 0;
        for (int i = 0; i < len[j]; i++)
            m += data[j][i];
        m /= len[j];
        for (int i = 0; i < len[j]; i++)
            ssw += (data[j][i] - m) * (data[j][i] - m);
        ssb += len[j] * (m - gm) * (m - gm);
    }
    double dfb = k - 1, dfw = N - k;
    if (!(dfw > 0) || !(ssw > 0))
        FAIL("not enough variation within groups");
    double msb = ssb / dfb, msw = ssw / dfw, F = msb / msw;
    r->dist = DIST_F, r->alt = ALT_GT, r->stat = F, r->df1 = dfb, r->df2 = dfw;
    r->p = sd_f_q(F, dfb, dfw);
    sr_add(r, "F", F);
    sr_add(r, "p", r->p);
    sr_add(r, "df factor", dfb);
    sr_add(r, "SS factor", ssb);
    sr_add(r, "MS factor", msb);
    sr_add(r, "df error", dfw);
    sr_add(r, "SS error", ssw);
    sr_add(r, "MS error", msw);
    sr_add(r, "Sxp", sqrt(msw));
    return 0;
}

/* ------------------------------------------------------------ intervals */
/* Confidence intervals are estimate ± critical value × standard error, the
 * critical value being the (1+C)/2 quantile of z or t. */

/* Add the "lower", "upper" and "ME" (margin of error = crit·se) lines of
 * the interval est ± crit·se. r->est gets the estimate and r->level the
 * confidence level as a fraction (r->dist stays DIST_NONE: intervals have no
 * drawing). Fails if the margin is not finite. */
static int interval(sres_t *r, double est, double crit, double se, double C)
{
    double m = crit * se;
    sr_add(r, "lower", est - m);
    sr_add(r, "upper", est + m);
    sr_add(r, "ME", m);
    r->est = est;
    r->level = conf(C);
    return isfinite(m) ? 0 : (r->err = "invalid input", -1);
}

/* z* = Φ⁻¹((1 + C)/2), e.g. 1.959964 for C = 0.95 */
static double zcrit(double C) { return sd_invnorm(0.5 + conf(C) / 2, 0, 1); }

/* reject a confidence level outside (0, 1) after conf() (percent allowed) */
#define CHECKC(C) \
    if (!(conf(C) > 0 && conf(C) < 1)) FAIL("C-Level must be between 0 and 1")

/* ZInterval for μ with known σ: x̄ ± z*·σ/√n. */
int st_zint(double sigma, double xbar, double n, double C, sres_t *r)
{
    sr_begin(r, "ZInterval");
    CHECKC(C);
    if (!(sigma > 0) || !(n >= 1))
        FAIL("need sigma > 0 and n >= 1");
    if (interval(r, xbar, zcrit(C), sigma / sqrt(n), C))
        return -1;
    sr_add(r, "xbar", xbar);
    sr_add(r, "n", n);
    return 0;
}

/* TInterval for μ: x̄ ± t*·Sx/√n, t* = t quantile (1+C)/2 with n-1 df. */
int st_tint(double xbar, double sx, double n, double C, sres_t *r)
{
    sr_begin(r, "TInterval");
    CHECKC(C);
    if (!(sx > 0) || !(n >= 2))
        FAIL("need Sx > 0 and n >= 2");
    if (interval(r, xbar, sd_invt(0.5 + conf(C) / 2, n - 1), sx / sqrt(n), C))
        return -1;
    sr_add(r, "xbar", xbar);
    sr_add(r, "df", n - 1);
    sr_add(r, "Sx", sx);
    sr_add(r, "n", n);
    return 0;
}

/* 2-SampZInt for μ1 - μ2 with known σ: (x̄1 - x̄2) ± z*·sqrt(σ1²/n1 + σ2²/n2). */
int st_2sampzint(double s1, double s2, double x1, double n1, double x2, double n2, double C, sres_t *r)
{
    sr_begin(r, "2-SampZInt");
    CHECKC(C);
    if (!(s1 > 0 && s2 > 0 && n1 >= 1 && n2 >= 1))
        FAIL("need sigma > 0 and n >= 1");
    if (interval(r, x1 - x2, zcrit(C), sqrt(s1 * s1 / n1 + s2 * s2 / n2), C))
        return -1;
    sr_add(r, "xbar1", x1);
    sr_add(r, "xbar2", x2);
    sr_add(r, "n1", n1);
    sr_add(r, "n2", n2);
    return 0;
}

/* 2-SampTInt for μ1 - μ2: (x̄1 - x̄2) ± t*·SE, SE and df from twot()
 * (pooled or Welch-Satterthwaite). */
int st_2samptint(double x1, double s1, double n1, double x2, double s2, double n2, double C, bool pooled,
                 sres_t *r)
{
    sr_begin(r, "2-SampTInt");
    CHECKC(C);
    if (!(s1 > 0 && s2 > 0 && n1 >= 2 && n2 >= 2))
        FAIL("need Sx > 0 and n >= 2");
    double se, df, sp;
    twot(s1, n1, s2, n2, pooled, &se, &df, &sp);
    if (interval(r, x1 - x2, sd_invt(0.5 + conf(C) / 2, df), se, C))
        return -1;
    sr_add(r, "df", df);
    sr_add(r, "xbar1", x1);
    sr_add(r, "xbar2", x2);
    sr_add(r, "Sx1", s1);
    sr_add(r, "Sx2", s2);
    if (pooled)
        sr_add(r, "Sxp", sp);
    sr_add(r, "n1", n1);
    sr_add(r, "n2", n2);
    return 0;
}

/* 1-PropZInt (Wald interval): p̂ ± z*·sqrt(p̂(1-p̂)/n), p̂ = x/n. With x = 0 or
 * x = n the standard error is 0 and the interval collapses to p̂ (a known
 * weakness of the Wald interval, kept for calculator compatibility). */
int st_1propzint(double x, double n, double C, sres_t *r)
{
    sr_begin(r, "1-PropZInt");
    CHECKC(C);
    if (!(n >= 1 && x >= 0 && x <= n))
        FAIL("need 0 <= x <= n");
    double p = x / n;
    if (interval(r, p, zcrit(C), sqrt(p * (1 - p) / n), C))
        return -1;
    sr_add(r, "p^", p);
    sr_add(r, "n", n);
    return 0;
}

/* 2-PropZInt for p1 - p2: (p̂1 - p̂2) ± z*·sqrt(p̂1(1-p̂1)/n1 + p̂2(1-p̂2)/n2)
 * (unpooled standard error, unlike the test, because H0 p1 = p2 is not
 * assumed). */
int st_2propzint(double x1, double n1, double x2, double n2, double C, sres_t *r)
{
    sr_begin(r, "2-PropZInt");
    CHECKC(C);
    if (!(n1 >= 1 && n2 >= 1 && x1 >= 0 && x2 >= 0 && x1 <= n1 && x2 <= n2))
        FAIL("need 0 <= x <= n");
    double p1 = x1 / n1, p2 = x2 / n2;
    if (interval(r, p1 - p2, zcrit(C), sqrt(p1 * (1 - p1) / n1 + p2 * (1 - p2) / n2), C))
        return -1;
    sr_add(r, "p^1", p1);
    sr_add(r, "p^2", p2);
    sr_add(r, "n1", n1);
    sr_add(r, "n2", n2);
    return 0;
}

/* ------------------------------------------------------------ misc */
double st_normprob_z(int i, int n)
{
    /* the usual normal-probability-plot convention: z of the (i+1-0.5)/n quantile
     * (Hazen plotting position (i - 0.5)/n for the 1-based rank i; points
     * from normal data then lie close to a straight line) */
    return sd_invnorm((i + 0.5) / n, 0, 1);
}
