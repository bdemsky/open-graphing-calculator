/* Numeric methods for graph analysis (double precision, no allocation):
 * Brent's root finder, Brent's minimizer, Richardson derivatives and adaptive
 * Gauss-Kronrod 7-15 quadrature. Unit-tested in tests/unit/test_graph.c.
 *
 * Every method takes the function as a callback `gn_fn f(x, user)` plus an
 * opaque `user` pointer, so the same code serves Y= functions, parametric
 * components, |f|, differences of two curves, arc-length integrands, ... (see
 * app/gr_calc.c). Functions may return NAN/inf where they are undefined; the
 * methods treat non-finite values as "no answer here" rather than trapping.
 * They are pure computations: no UI, no CAS calls, no global state except the
 * subdivision budget of the integrator (single-threaded main loop only).
 *
 * References: R. P. Brent, "Algorithms for Minimization without Derivatives"
 * (Prentice-Hall 1973), ch. 4 (zero) and ch. 5 (minimum); the code follows the
 * well-known zbrent/brent formulation (Numerical Recipes). Gauss-Kronrod nodes
 * and weights are QUADPACK's QK15 (Piessens et al., 1983). */
#include <float.h>
#include <math.h>
#include "graph.h"

/* Brent's method: find a root of f in [a, b] given a sign change. Combines
 * bisection (always safe) with secant / inverse quadratic interpolation (fast
 * near a simple root), and falls back to bisection whenever the interpolated
 * step does not shrink the bracket fast enough.
 * Returns false when f(a) or f(b) is non-finite, when they have the same sign,
 * or when an iterate hits a non-finite value (e.g. the bracket straddles a
 * pole). On success *root holds the root to about 2*DBL_EPSILON relative
 * precision (or the last iterate after 200 steps). */
bool gn_root(gn_fn f, void *u, double a, double b, double *root)
{
    double fa = f(a, u), fb = f(b, u);
    if (!isfinite(fa) || !isfinite(fb))
        return false;
    if (fa == 0) {
        *root = a;
        return true;
    }
    if (fb == 0) {
        *root = b;
        return true;
    }
    if ((fa > 0) == (fb > 0))
        return false;
    /* Invariants: b is the best estimate, [b, c] brackets the root (f(b) and
     * f(c) have opposite signs), a is the previous b. d is the current step and
     * e the step before it (used to check that interpolation is converging). */
    double c = a, fc = fa, d = b - a, e = d;
    for (int it = 0; it < 200; it++) {
        if ((fb > 0) == (fc > 0)) { /* the root is between a and b now: make a the new c */
            c = a, fc = fa;
            d = e = b - a;
        }
        if (fabs(fc) < fabs(fb)) { /* keep |f(b)| smallest so b is the best estimate */
            a = b, b = c, c = a;
            fa = fb, fb = fc, fc = fa;
        }
        /* m = half the bracket (the bisection step); done when the bracket is at rounding level */
        double tol = 2 * DBL_EPSILON * fabs(b) + 1e-300, m = 0.5 * (c - b);
        if (fabs(m) <= tol || fb == 0) {
            *root = b;
            return true;
        }
        if (fabs(e) >= tol && fabs(fa) > fabs(fb)) {
            /* try interpolation; the proposed step is p/q (signs arranged so p >= 0) */
            double s = fb / fa, p, q;
            if (a == c) { /* only two distinct points: secant step */
                p = 2 * m * s;
                q = 1 - s;
            } else { /* three points: inverse quadratic interpolation (x as a quadratic in f) */
                double qq = fa / fc, r = fb / fc;
                p = s * (2 * m * qq * (qq - r) - (b - a) * (r - 1));
                q = (qq - 1) * (r - 1) * (s - 1);
            }
            if (p > 0)
                q = -q;
            else
                p = -p;
            /* accept it only if it lands well inside the bracket and is smaller
             * than half the step before last (guaranteed shrinkage); else bisect */
            if (2 * p < fmin(3 * m * q - fabs(tol * q), fabs(e * q))) {
                e = d;
                d = p / q;
            } else {
                d = m, e = m;
            }
        } else {
            d = m, e = m;
        }
        a = b, fa = fb;
        /* step at least tol so the bracket keeps shrinking near convergence */
        b += fabs(d) > tol ? d : (m > 0 ? tol : -tol);
        fb = f(b, u);
        if (!isfinite(fb))
            return false;
    }
    *root = b;
    return true;
}

/* |f| as a function to minimise for touching zeros */
typedef struct {
    gn_fn f;
    void *u;
} absf_t;
static double absf(double x, void *u)
{
    absf_t *a = u;
    return fabs(a->f(x, a->u));
}

/* Root of f in [a, b] closest to `guess` (CALC zero/intersect: the user's
 * bounds and cursor position).
 * 1. Sample f at n+1 evenly spaced points; refine every sign change with
 *    gn_root and keep the root nearest the guess. A sign change across a pole
 *    (tan(x) at π/2, 1/x at 0) also gives Brent a "bracket", so a candidate is
 *    accepted only if |f(root)| is tiny relative to the bracketing samples.
 * 2. With no sign change, look for a touching zero (x^2 at 0, where f does not
 *    change sign): minimise |f| with gn_min and accept the minimum if |f| is
 *    below 1e-7 of the largest |f| on 9 probe points.
 * Returns false if a >= b, n < 2, or no zero was found. */
bool gn_root_near(gn_fn f, void *u, double a, double b, double guess, int n, double *root)
{
    if (!(a < b) || n < 2)
        return false;
    double best = NAN, bestd = INFINITY;
    double h = (b - a) / n, x0 = a, f0 = f(a, u);
    for (int k = 1; k <= n; k++) {
        double x1 = k == n ? b : a + k * h, f1 = f(x1, u);
        double r;
        if (isfinite(f0) && isfinite(f1) && ((f0 <= 0 && f1 >= 0) || (f0 >= 0 && f1 <= 0)) &&
            gn_root(f, u, x0, x1, &r)) {
            /* reject sign changes across poles: |f| must be small at the root */
            double fr = f(r, u), scale = fmax(fabs(f0), fabs(f1));
            if (isfinite(fr) && fabs(fr) <= 1e-6 * fmax(1.0, scale)) {
                double d = fabs(r - guess);
                if (d < bestd)
                    bestd = d, best = r;
            }
        }
        x0 = x1, f0 = f1;
    }
    if (isfinite(best)) {
        *root = best;
        return true;
    }
    /* no sign change: a touching zero (x^2 at 0) is a minimum of |f| */
    absf_t af = {f, u};
    double m;
    if (gn_min(absf, &af, a, b, n, &m)) {
        double fm = fabs(f(m, u)), scale = 0;
        for (int k = 0; k <= 8; k++) {
            double v = fabs(f(a + k * (b - a) / 8, u));
            if (isfinite(v) && v > scale)
                scale = v;
        }
        if (fm <= 1e-7 * fmax(1.0, scale)) {
            *root = m;
            return true;
        }
    }
    return false;
}

/* Brent's minimisation on [a, b] (golden section + parabolic steps), starting
 * at x. Keeps the three best points: x (lowest f), w (second lowest) and v
 * (the previous w). Fits a parabola through them and steps to its vertex when
 * that lies inside [a, b] and is less than half the step before last;
 * otherwise takes a golden-section step (cg = (3 - sqrt(5)) / 2) into the
 * larger part of the interval. Finds a local minimum to ~1.5e-8 relative.
 * Non-finite values count as +inf, so undefined regions are avoided.
 * Returns the abscissa of the minimum; *fxout gets f there. */
static double brent_min(gn_fn f, void *u, double a, double b, double x, double *fxout)
{
    const double cg = 0.3819660112501051, tol = 1.5e-8;
    double w = x, v = x, fx = f(x, u), fw = fx, fv = fx, d = 0, e = 0;
    for (int it = 0; it < 200; it++) {
        double xm = 0.5 * (a + b), tol1 = tol * fabs(x) + 1e-12, tol2 = 2 * tol1;
        if (fabs(x - xm) <= tol2 - 0.5 * (b - a)) /* [a, b] is within tolerance around x */
            break;
        bool golden = true;
        if (fabs(e) > tol1) {
            /* parabola through (x,fx), (w,fw), (v,fv): vertex at x + p/q */
            double r = (x - w) * (fx - fv), q = (x - v) * (fx - fw), p = (x - v) * q - (x - w) * r;
            q = 2 * (q - r);
            if (q > 0)
                p = -p;
            q = fabs(q);
            double etemp = e;
            e = d;
            if (!(fabs(p) >= fabs(0.5 * q * etemp) || p <= q * (a - x) || p >= q * (b - x))) {
                d = p / q;
                double uu = x + d;
                if (uu - a < tol2 || b - uu < tol2)
                    d = xm - x >= 0 ? tol1 : -tol1;
                golden = false;
            }
        }
        if (golden) {
            e = x >= xm ? a - x : b - x;
            d = cg * e;
        }
        double uu = x + (fabs(d) >= tol1 ? d : (d >= 0 ? tol1 : -tol1));
        double fu = f(uu, u);
        if (!isfinite(fu))
            fu = INFINITY;
        /* shrink [a, b] so it still brackets the minimum, then update x, w, v */
        if (fu <= fx) {
            if (uu >= x)
                a = x;
            else
                b = x;
            v = w, fv = fw;
            w = x, fw = fx;
            x = uu, fx = fu;
        } else {
            if (uu < x)
                a = uu;
            else
                b = uu;
            if (fu <= fw || w == x) {
                v = w, fv = fw;
                w = uu, fw = fu;
            } else if (fu <= fv || v == x || v == w) {
                v = uu, fv = fu;
            }
        }
    }
    *fxout = fx;
    return x;
}

/* Minimum of f on [a, b]: sample n+1 evenly spaced points, take the lowest
 * finite sample and polish it with brent_min inside its two neighbouring
 * sample intervals. Sampling first makes the result (nearly) global; Brent on
 * its own would stop at whichever local minimum it reaches. Maxima are found
 * by minimising -f (gr_calc.c). Returns false if a >= b, n < 2 or every sample
 * is non-finite. */
bool gn_min(gn_fn f, void *u, double a, double b, int n, double *xmin)
{
    if (!(a < b) || n < 2)
        return false;
    int bk = -1;
    double bf = INFINITY, h = (b - a) / n;
    for (int k = 0; k <= n; k++) {
        double v = f(a + k * h, u);
        if (isfinite(v) && v < bf)
            bf = v, bk = k;
    }
    if (bk < 0)
        return false;
    double lo = a + (bk > 0 ? bk - 1 : 0) * h, hi = a + (bk < n ? bk + 1 : n) * h, fx;
    double x = brent_min(f, u, lo, hi, a + bk * h, &fx);
    if (!(fx <= bf)) /* keep the sample if refinement did not improve it */
        x = a + bk * h;
    *xmin = x;
    return true;
}

/* Numeric derivative of order 1 or 2 at x (dy/dx, inflection points, arc length).
 * Central differences
 *     D1(h) = (f(x+h) - f(x-h)) / 2h,   D2(h) = (f(x+h) - 2 f(x) + f(x-h)) / h^2
 * have error series in even powers of h, so with steps h, h/2, h/4, h/8 a
 * Richardson (Romberg-style) table removes the h^2, h^4 and h^6 terms:
 *     T[i][k] = T[i][k-1] + (T[i][k-1] - T[i-1][k-1]) / (4^k - 1).
 * The base step scales with max(1, |x|): 1e-3 for f', 1e-2 for f'' (whose
 * difference quotient loses more digits to cancellation). NAN if f is
 * undefined near x. */
double gn_deriv(gn_fn f, void *u, double x, int order)
{
    /* Richardson extrapolation of central differences, steps h, h/2, h/4, h/8 */
    double h = (order == 1 ? 1e-3 : 1e-2) * fmax(1.0, fabs(x));
    double t[4][4];
    for (int i = 0; i < 4; i++) {
        double hi = h / (1 << i), d;
        if (order == 1)
            d = (f(x + hi, u) - f(x - hi, u)) / (2 * hi);
        else
            d = (f(x + hi, u) - 2 * f(x, u) + f(x - hi, u)) / (hi * hi);
        t[i][0] = d;
        for (int k = 1; k <= i; k++)
            t[i][k] = t[i][k - 1] + (t[i][k - 1] - t[i - 1][k - 1]) / ((1 << (2 * k)) - 1);
    }
    return t[3][3];
}

/* Gauss-Kronrod 7-15 nodes and weights on [-1, 1] (QUADPACK QK15; the nodes
 * are symmetric, so only x >= 0 is stored and xgk[7] = 0 is the centre).
 * xgk[1], xgk[3], xgk[5] and 0 are also the 7-point Gauss-Legendre nodes, with
 * the Gauss weights wg[0..3]; the remaining Kronrod nodes were chosen so the
 * 15-point rule reuses all 7 Gauss evaluations. */
static const double xgk[8] = {0.991455371120812639206854697526329, 0.949107912342758524526189684047851,
                              0.864864423359769072789712788640926, 0.741531185599394439863864773280788,
                              0.586087235467691130294144845693013, 0.405845151377397166906606412076961,
                              0.207784955007898467600689403773245, 0.000000000000000000000000000000000};
static const double wgk[8] = {0.022935322010529224963732008058970, 0.063092092629978553290700663189204,
                              0.104790010322250183839876322541518, 0.140653259715525918745189590510238,
                              0.169004726639267902826583426598550, 0.190350578064785409913256402421014,
                              0.204432940075298892414161999234649, 0.209482141084727828012999174891714};
static const double wg[4] = {0.129484966168869693270611432679082, 0.279705391489276667901467771423780,
                             0.381830050505118944950369775488975, 0.417959183673469387755102040816327};

/* One 15-point Kronrod estimate of the integral over [a, b] (15 evaluations,
 * nodes mapped by x = c ± h*xgk). The embedded 7-point Gauss result reuses 7
 * of them; *err = |K15 - G7| is a (pessimistic) error estimate. */
static double gk15(gn_fn f, void *u, double a, double b, double *err)
{
    double c = 0.5 * (a + b), h = 0.5 * (b - a);
    double fc = f(c, u), rk = fc * wgk[7], rg = fc * wg[3];
    for (int j = 0; j < 7; j++) {
        double dx = h * xgk[j], f1 = f(c - dx, u), f2 = f(c + dx, u);
        rk += wgk[j] * (f1 + f2);
        if (j & 1) /* the odd Kronrod nodes are the Gauss nodes */
            rg += wg[j / 2] * (f1 + f2);
    }
    *err = fabs((rk - rg) * h);
    return rk * h;
}

static int gk_budget; /* subdivisions left (bounds the time on non-smooth integrands) */
/* Recursive adaptive bisection. `whole` / `werr` are the GK15 result and error
 * estimate for [a, b]: accept them when werr <= tol, else split at the
 * midpoint and integrate each half with tol/2. Gives up refining (accepting
 * the current estimate) at depth 30, on a non-finite estimate (singular
 * integrand) or when the shared budget is spent. *err gets the sum of the
 * accepted pieces' error estimates. */
static double gk_adapt(gn_fn f, void *u, double a, double b, double tol, double whole, double werr, int depth,
                       double *err)
{
    if (werr <= tol || depth >= 30 || !isfinite(whole) || gk_budget-- <= 0) {
        *err = werr;
        return whole;
    }
    double m = 0.5 * (a + b), e1, e2;
    double l = gk15(f, u, a, m, &e1), r = gk15(f, u, m, b, &e2);
    double ea, eb;
    double vl = gk_adapt(f, u, a, m, tol / 2, l, e1, depth + 1, &ea);
    double vr = gk_adapt(f, u, m, b, tol / 2, r, e2, depth + 1, &eb);
    *err = ea + eb;
    return vl + vr;
}

/* Definite integral of f over [a, b] (a > b gives the negated value) by
 * adaptive Gauss-Kronrod 7-15 with at most 2000 subdivisions. The effective
 * absolute tolerance is max(tol, 1e-13 * |first estimate|), so large integrals
 * do not chase unreachable absolute accuracy. *err (may be NULL) gets the
 * estimated absolute error. Non-finite values propagate to the result. */
double gn_integrate(gn_fn f, void *u, double a, double b, double tol, double *err)
{
    double e, dummy;
    if (a == b) {
        if (err)
            *err = 0;
        return 0;
    }
    gk_budget = 2000;
    double whole = gk15(f, u, a, b, &e);
    double t = fmax(tol, 1e-13 * fabs(whole));
    double v = gk_adapt(f, u, a, b, t, whole, e, 0, err ? err : &dummy);
    return v;
}
