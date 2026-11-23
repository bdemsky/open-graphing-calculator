/*
 * statdist.c -- probability distributions in double precision (see
 * include/cas_stat.h). No CAS context: pure functions of doubles.
 *
 * Methods (from the literature, implemented from the formulas):
 *  - normal: erfc; inverse by Acklam's rational approximation refined with
 *    Halley steps on erfc (full double precision);
 *  - binomial / Poisson point probabilities: Loader's saddle-point form
 *    (C. Loader, "Fast and accurate computation of binomial probabilities",
 *    2000) with the Stirling-series remainder and the deviance term bd0;
 *  - regularized incomplete beta / gamma: power series and continued
 *    fractions (DLMF 8.17.22, 8.9.2) evaluated with the modified Lentz
 *    method; their prefactors x^a y^b / B(a,b) and x^a e^-x / Gamma(a) are
 *    computed through the saddle-point forms, so large parameters keep
 *    their accuracy;
 *  - t, chi-square, F: through the incomplete beta/gamma functions, upper
 *    tails computed directly (no 1 - p cancellation);
 *  - inverses: Newton steps safeguarded by a bisection bracket.
 *
 * Where it fits: the CAS commands normalcdf(...), invT(...), binompdf(...)
 * etc. (stats.c: stats_cmd for evaluation, stats_num for graphing through
 * numeric.c) are thin wrappers that convert their arguments to doubles and
 * call the sd_* functions below.  The calculator's statistics app calls the
 * same functions directly (hypothesis tests use the upper-tail sd_*_q
 * functions).  Because the stub CAS build (cas_stub/statdist.c) #includes
 * this file verbatim, it must stay plain C: only <math.h>, <stddef.h> and
 * cas_stat.h, no CAS context K, no allocation, no cas_throw.  Every function
 * is a pure function of its arguments; errors are reported as NAN.
 *
 * Numerical strategy, in one paragraph: every probability is computed as
 * either a lower tail L(x) = P(X <= x) or an upper tail U(x) = P(X >= x),
 * each obtained *directly* (never as 1 - the other) whenever it is the small
 * one, so results keep full relative accuracy far out in the tails.  The
 * expensive-to-get-right part of the incomplete beta/gamma functions is the
 * prefactor x^a y^b / B(a,b) (resp. x^a e^-x / Gamma(a)): computing it as
 * exp(a ln x + b ln y - lnB) loses digits for large a, b because of
 * cancellation between big logarithms.  Loader's binomial saddle-point form
 * (dbinom_raw / dpois_raw below) evaluates exactly these quantities as
 * exp(-(Stirling remainders) - (deviances bd0)) / sqrt(...), where every term
 * is small, so the relative error stays near machine precision.
 *
 * References:
 *  - C. Loader, "Fast and accurate computation of binomial probabilities",
 *    2000 (stirlerr, bd0, dbinom_raw, dpois_raw);
 *  - DLMF 8.7 / 8.9 / 8.17 (incomplete gamma/beta series and continued
 *    fractions), Abramowitz & Stegun 26.5 / 26.7 for the t, chi-square and F
 *    relations to I_x(a,b) and P(a,x);
 *  - W. J. Lentz (1976) and Thompson & Barnett (1986): the modified Lentz
 *    algorithm for continued fractions (forward evaluation, TINY guard);
 *  - P. J. Acklam, "An algorithm for computing the inverse normal
 *    cumulative distribution function" (rational approximation + one-step
 *    Halley refinement), 2003;
 *  - E. B. Wilson & M. M. Hilferty (1931), cube-root normal approximation
 *    used as the starting point of invChi2; Cornish-Fisher expansion for invT.
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os") /* flash budget: size over speed (not time-critical) */
#endif
#include <math.h>
#include <stddef.h>
#include "../include/cas_stat.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_LN2
#define M_LN2 0.693147180559945309417232121458
#endif
/* Constants: ln sqrt(2 pi), ln(2 pi), 1/sqrt(2).  BIG: |bound| >= 1e99 counts
 * as infinite (calculator convention, e.g. normalcdf(-1E99, 1.96)).  EPS:
 * relative convergence threshold of the series and continued fractions
 * (slightly below DBL_EPSILON / 2).  TINY: the modified-Lentz guard that
 * replaces an exactly-zero denominator. */
#define LN_SQRT_2PI 0.918938533204672741780329736406
#define LN_2PI 1.83787706640934548356065947281
#define SQRT1_2 0.707106781186547524400844362105
#define BIG 1e99
#define EPS 1e-16
#define TINY 1e-300

/* ------------------------------------------------------- saddle point */
/* Stirling-formula remainder (Loader's stirlerr):
 *   stirlerr(n) = lgamma(n+1) - ((n+0.5) ln n - n + ln sqrt(2 pi)),
 * i.e. ln(n!) minus Stirling's approximation, for real n > 0.  It is small
 * (about 1/(12 n)), which is what makes the saddle-point forms accurate.
 * For n <= 15 it is computed from lgamma directly (the cancellation there
 * costs only a few bits because n is small).  For larger n it uses the
 * asymptotic Stirling series
 *   1/(12n) - 1/(360n^3) + 1/(1260n^5) - 1/(1680n^7) + 1/(1188n^9) - ...
 * truncated after fewer terms as n grows (each cutoff is where the next term
 * drops below double precision).  stirlerr(0) is +inf; callers never pass 0. */
static double stirlerr(double n)
{
    if (n <= 15)
        return lgamma(n + 1) - (n + 0.5) * log(n) + n - LN_SQRT_2PI;
    const double S0 = 1.0 / 12, S1 = 1.0 / 360, S2 = 1.0 / 1260, S3 = 1.0 / 1680, S4 = 1.0 / 1188;
    double nn = n * n;
    if (n > 500)
        return (S0 - S1 / nn) / n;
    if (n > 80)
        return (S0 - (S1 - S2 / nn) / nn) / n;
    if (n > 35)
        return (S0 - (S1 - (S2 - S3 / nn) / nn) / nn) / n;
    return (S0 - (S1 - (S2 - (S3 - S4 / nn) / nn) / nn) / nn) / n;
}

/* Deviance term bd0(x, m) = x ln(x/m) + m - x  (>= 0, zero at x = m), for
 * x, m > 0, accurate when x ~ m (Loader 2000).
 * The direct formula cancels catastrophically when x is close to m.  With
 * v = (x-m)/(x+m) one has ln(x/m) = 2 atanh(v) = 2 (v + v^3/3 + v^5/5 + ...),
 * which gives
 *   bd0 = (x-m) v + 2x (v^3/3 + v^5/5 + ...),
 * a series of positive terms with ratio v^2 < 0.0083 when |x-m| < 0.1 (x+m),
 * summed until adding a term no longer changes the double result. */
static double bd0(double x, double m)
{
    if (fabs(x - m) < 0.1 * (x + m)) {
        double v = (x - m) / (x + m), s = (x - m) * v, ej = 2 * x * v;
        v *= v;
        for (int j = 1; j < 1000; j++) {
            ej *= v;
            double s1 = s + ej / (2 * j + 1);
            if (s1 == s)
                return s1;
            s = s1;
        }
        return s;
    }
    return x * log(x / m) + m - x;
}

/* Binomial point probability C(n,x) p^x q^(n-x) = Gamma(n+1) / (Gamma(x+1)
 * Gamma(n-x+1)) p^x q^(n-x), for real (not necessarily integer) 0 <= x <= n;
 * q = 1-p is passed separately so callers that know q exactly (e.g. the
 * incomplete beta with y = 1-x computed without cancellation) lose nothing.
 * Returns 0 outside [0, n].
 *
 * Loader's saddle-point form: with Stirling's formula for the three
 * factorials, everything large cancels analytically and
 *   ln P = stirlerr(n) - stirlerr(x) - stirlerr(n-x)
 *          - bd0(x, np) - bd0(n-x, nq) - 0.5 ln(2 pi x (n-x)/n),
 * where every term is O(1) or small, so exp() of it has nearly full relative
 * accuracy even for n in the millions.  The endpoints x = 0 and x = n reduce
 * to q^n and p^n, computed via bd0 when p (resp. q) is small so that
 * n ln(1-p) does not lose digits. */
static double dbinom_raw(double x, double n, double p, double q)
{
    if (p == 0)
        return x == 0 ? 1 : 0;
    if (q == 0)
        return x == n ? 1 : 0;
    if (x == 0) {
        if (n == 0)
            return 1;
        return exp(p < 0.1 ? -bd0(n, n * q) - n * p : n * log(q));
    }
    if (x == n)
        return exp(q < 0.1 ? -bd0(n, n * p) - n * q : n * log(p));
    if (x < 0 || x > n)
        return 0;
    double lc = stirlerr(n) - stirlerr(x) - stirlerr(n - x) - bd0(x, n * p) - bd0(n - x, n * q);
    double lf = LN_2PI + log(x) + log1p(-x / n);
    return exp(lc - 0.5 * lf);
}

/* Poisson point probability lam^x e^-lam / Gamma(x+1) for real x >= 0 and
 * lam >= 0 (0 for x < 0).  Saddle-point form (Loader):
 *   exp(-stirlerr(x) - bd0(x, lam)) / sqrt(2 pi x),
 * accurate for large x and lam.  Besides poissonpdf it supplies the
 * incomplete-gamma prefactor x^a e^-x / Gamma(a+1) = dpois_raw(a, x) and the
 * chi-square density. */
static double dpois_raw(double x, double lam)
{
    if (lam == 0)
        return x == 0 ? 1 : 0;
    if (x == 0)
        return exp(-lam);
    if (x < 0)
        return 0;
    return exp(-stirlerr(x) - bd0(x, lam)) / sqrt(2 * M_PI * x);
}

/* --------------------------------------------------- incomplete gamma */
/* Power series of the lower incomplete gamma function (DLMF 8.7 / A&S
 * 6.5):  gamma(a,x) = x^a e^-x / a * S  with
 *   S = sum_{n>=0} x^n / ((a+1)(a+2)...(a+n)).
 * Returns S.  All terms are positive (no cancellation); used for x < a + 1,
 * where the term ratio x/(a+n) < 1 from the start, so convergence is
 * geometric and the 100000-iteration cap is only a safety net. */
static double gamma_series(double a, double x) /* sum_{n>=0} x^n / ((a+1)...(a+n)) */
{
    double ap = a, sum = 1, del = 1;
    for (int n = 0; n < 100000; n++) {
        ap += 1;
        del *= x / ap;
        sum += del;
        if (fabs(del) < fabs(sum) * EPS)
            break;
    }
    return sum;
}

/* Continued fraction for the upper incomplete gamma function (Legendre's
 * fraction, DLMF 8.9.2 in its even-part form):
 *   Gamma(a,x) = e^-x x^a * 1/(x+1-a - 1(1-a)/(x+3-a - 2(2-a)/(x+5-a - ...)))
 * Returns the fraction, i.e. Q(a,x) Gamma(a) e^x x^-a.  Evaluated forward with
 * the modified Lentz method: h_n = h_{n-1} C_n D_n, where C and D are ratios
 * of successive numerators / denominators; a zero C or D is replaced by TINY
 * instead of failing.  Used for x >= a + 1, where it converges quickly
 * (b_n = x + 2n + 1 - a is at least 2). */
static double gamma_cf(double a, double x) /* Q(a,x) * Gamma(a) e^x x^-a */
{
    double b = x + 1 - a, c = 1 / TINY, d = 1 / b, h = d;
    for (int i = 1; i < 100000; i++) {
        double an = -i * (i - a);
        b += 2;
        d = an * d + b;
        if (fabs(d) < TINY)
            d = TINY;
        c = b + an / c;
        if (fabs(c) < TINY)
            c = TINY;
        d = 1 / d;
        double del = d * c;
        h *= del;
        if (fabs(del - 1) < EPS)
            break;
    }
    return h;
}

/* Regularized lower incomplete gamma P(a,x) (see cas_stat.h for the clamping
 * and NAN rules).  Choice of method (the classic split, e.g. Numerical
 * Recipes 6.2): for x < a + 1 the series gives P directly,
 *   P = x^a e^-x / Gamma(a+1) * S = dpois_raw(a, x) * gamma_series(a, x);
 * otherwise the continued fraction gives Q and P = 1 - Q (Q < ~0.5 there,
 * so the subtraction is benign). */
double sd_gammp(double a, double x)
{
    if (!(a > 0) || isnan(x))
        return NAN;
    if (x <= 0)
        return 0;
    if (isinf(x))
        return 1;
    if (x < a + 1)
        return dpois_raw(a, x) * gamma_series(a, x);
    return 1 - a * dpois_raw(a, x) * gamma_cf(a, x);
}

/* Regularized upper incomplete gamma Q(a,x) = 1 - P(a,x), computed with the
 * same split as sd_gammp but mirrored: for x >= a + 1 (the upper tail is the
 * small one) Q comes straight from the continued fraction,
 *   Q = x^a e^-x / Gamma(a) * CF = a * dpois_raw(a, x) * gamma_cf(a, x),
 * so tiny p-values (poissoncdf complements, chi-square tests) keep full
 * relative accuracy. */
double sd_gammq(double a, double x)
{
    if (!(a > 0) || isnan(x))
        return NAN;
    if (x <= 0)
        return 1;
    if (isinf(x))
        return 0;
    if (x < a + 1)
        return 1 - dpois_raw(a, x) * gamma_series(a, x);
    return a * dpois_raw(a, x) * gamma_cf(a, x);
}

/* ---------------------------------------------------- incomplete beta */
/* ln B(a,b) via lgamma (public helper for the statistics app; not used
 * internally, the prefactors go through dbinom_raw instead). */
double sd_lnbeta(double a, double b) { return lgamma(a) + lgamma(b) - lgamma(a + b); }

/* Continued fraction for the incomplete beta function (DLMF 8.17.22):
 *   I_x(a,b) = x^a (1-x)^b / (a B(a,b)) * 1/(1+ d1/(1+ d2/(1+ ...)))
 * with d_{2m+1} = -(a+m)(a+b+m) x / ((a+2m)(a+2m+1)),
 *      d_{2m}   =  m (b-m) x / ((a+2m-1)(a+2m)).
 * Returns the fraction.  Each loop iteration applies one even and one odd
 * step with the modified Lentz method (TINY guards against zero
 * denominators); it converges rapidly for x < (a+1)/(a+b+2), which is the
 * caller's responsibility (betainc2 uses the symmetry I_x(a,b) =
 * 1 - I_{1-x}(b,a) otherwise). */
static double beta_cf(double a, double b, double x)
{
    double qab = a + b, qap = a + 1, qam = a - 1, c = 1, d = 1 - qab * x / qap;
    if (fabs(d) < TINY)
        d = TINY;
    d = 1 / d;
    double h = d;
    for (int m = 1; m < 100000; m++) {
        int m2 = 2 * m;
        double aa = m * (b - m) * x / ((qam + m2) * (a + m2));
        d = 1 + aa * d;
        if (fabs(d) < TINY)
            d = TINY;
        c = 1 + aa / c;
        if (fabs(c) < TINY)
            c = TINY;
        d = 1 / d;
        h *= d * c;
        aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2));
        d = 1 + aa * d;
        if (fabs(d) < TINY)
            d = TINY;
        c = 1 + aa / c;
        if (fabs(c) < TINY)
            c = TINY;
        d = 1 / d;
        double del = d * c;
        h *= del;
        if (fabs(del - 1) < EPS)
            break;
    }
    return h;
}

/* Regularized incomplete beta I_x(a,b) with y = 1-x given separately (no
 * cancellation): callers such as the t and F tails compute x and y as two
 * independent ratios (df/(df+t^2), t^2/(df+t^2)), so a tail probability near
 * x = 1 is not destroyed by forming 1 - x.  a, b > 0 required (NAN
 * otherwise); x <= 0 gives 0 and y <= 0 gives 1.
 * The prefactor x^a y^b / B(a,b) is obtained from Loader's binomial form
 * (dbinom_raw with real "trials" a+b, see the identity in the code), then the
 * continued fraction is applied on the side where it converges, returning
 * either I directly or 1 - I_y(b,a). */
static double betainc2(double a, double b, double x, double y)
{
    if (!(a > 0) || !(b > 0) || isnan(x) || isnan(y))
        return NAN;
    if (x <= 0)
        return 0;
    if (y <= 0)
        return 1;
    /* x^a y^b / B(a,b) = dbinom_raw(a; a+b, x, y) * a b / (a+b) */
    double pre = dbinom_raw(a, a + b, x, y) * (a * b / (a + b));
    if (x < (a + 1) / (a + b + 2))
        return pre * beta_cf(a, b, x) / a;
    return 1 - pre * beta_cf(b, a, y) / b;
}

/* Public I_x(a,b) (y = 1 - x formed here, so callers with an exact y should
 * use the tails functions instead). */
double sd_betainc(double a, double b, double x) { return betainc2(a, b, x, 1 - x); }

/* -------------------------------------------------- generic helpers */
/* Map |v| >= 1e99 to +-infinity (calculator convention for open bounds). */
static double clampinf(double v) { return v >= BIG ? INFINITY : v <= -BIG ? -INFINITY : v; }

/* A continuous distribution is described to the generic area / invert
 * helpers by its "tails" function: for a point x and the parameter array par
 * (df, or d1/d2; unused for the standard normal) it stores both
 * L = P(X <= x) and U = P(X >= x), each computed directly so whichever is
 * small is accurate. */
typedef void (*tails_fn)(double x, const double *par, double *L, double *U);

/* Signed probability P(lo <= X <= hi) from the tails (the ..cdf commands).
 * lo > hi is allowed and negates the result (calculator convention);
 * infinite / >= 1e99 bounds are fine, NaN bounds give NAN.
 * To avoid cancellation it picks, after ordering lo <= hi:
 *   U(lo) < 1/2  -> both points in the upper half:  U(lo) - U(hi)
 *   L(hi) < 1/2  -> both points in the lower half:  L(hi) - L(lo)
 *   otherwise    -> the interval straddles the median: 1 - L(lo) - U(hi)
 * so a difference of two nearly-equal numbers close to 1 is never formed. */
static double area(tails_fn f, const double *par, double lo, double hi)
{
    lo = clampinf(lo);
    hi = clampinf(hi);
    if (isnan(lo) || isnan(hi))
        return NAN;
    double s = 1;
    if (lo > hi) {
        double t = lo;
        lo = hi;
        hi = t;
        s = -1;
    }
    double La, Ua, Lb, Ub;
    f(lo, par, &La, &Ua);
    f(hi, par, &Lb, &Ub);
    double r;
    if (Ua < 0.5)
        r = Ua - Ub;
    else if (Lb < 0.5)
        r = Lb - La;
    else
        r = 1 - La - Ub;
    return s * r;
}

/* Inverse cdf: the x in the support (dlo, dhi) with L(x) = p.  f gives the
 * tails, pdf the density (the derivative of L, used for Newton steps), x0 is
 * a starting guess from an asymptotic approximation.  p = 0 / 1 return the
 * support ends, other p outside (0,1) or NaN give NAN; NAN is also returned if
 * no sign-changing bracket can be found.
 *
 * Accuracy: for p > 1/2 the equation is solved as U(x) = 1 - p instead,
 * so the residual is always formed from the *small* tail.  The residual
 * r(x) is oriented to be increasing in x in both cases (dr/dx = pdf).
 *
 * Algorithm:
 *  1. Bracket: starting from x0, step left (doubling steps, or geometrically
 *     towards a finite dlo) until r(a) <= 0, and right until r(b) >= 0.
 *  2. Safeguarded Newton: take x - r/pdf; if that leaves the current bracket
 *     [a, b] (or pdf is 0) bisect instead.  The bracket shrinks every step,
 *     so the iteration cannot diverge; stop when the step or the bracket is
 *     at the relative rounding level (~2 ulp), or after 300 steps. */
static double invert(tails_fn f, double (*pdf)(double, const double *), const double *par, double p,
                     double x0, double dlo, double dhi)
{
    if (!(p > 0) || !(p < 1))
        return p == 0 ? dlo : p == 1 ? dhi : NAN;
    int upper = p > 0.5;
    double target = upper ? 1 - p : p;
#define RESID(x, out)                                                                                    \
    do {                                                                                                 \
        double L_, U_;                                                                                   \
        f((x), par, &L_, &U_);                                                                           \
        (out) = upper ? target - U_ : L_ - target;                                                       \
    } while (0)
    /* bracket [a, b] with r(a) < 0 < r(b) */
    double a = x0, b = x0, ra, rb;
    RESID(x0, ra);
    rb = ra;
    double step = fabs(x0) > 1 ? fabs(x0) : 1;
    for (int i = 0; i < 2000 && ra > 0; i++) {
        b = a, rb = ra;
        a = dlo > -INFINITY ? dlo + (a - dlo) / 4 : a - step;
        step *= 2;
        RESID(a, ra);
    }
    step = fabs(x0) > 1 ? fabs(x0) : 1;
    for (int i = 0; i < 2000 && rb < 0; i++) {
        a = b, ra = rb;
        b = dhi < INFINITY ? dhi - (dhi - b) / 4 : b + step;
        step *= 2;
        RESID(b, rb);
    }
    if (ra > 0 || rb < 0)
        return NAN;
    double x = x0 > a && x0 < b ? x0 : 0.5 * (a + b);
    for (int it = 0; it < 300; it++) {
        double r;
        RESID(x, r);
        if (r == 0)
            return x;
        if (r < 0)
            a = x;
        else
            b = x;
        double d = pdf(x, par), xn = d > 0 ? x - r / d : NAN;
        if (!(xn > a && xn < b))
            xn = 0.5 * (a + b);
        if (fabs(xn - x) <= 4e-16 * fabs(xn) || b - a <= 4e-16 * fabs(a + b))
            return xn;
        x = xn;
    }
#undef RESID
    return x;
}

/* ---------------------------------------------------------- normal */
/* Standard normal upper tail P(Z >= z) = erfc(z/sqrt 2)/2 (erfc keeps full
 * relative accuracy for large z, unlike 1 - Phi). */
double sd_norm_q(double z) { return 0.5 * erfc(z * SQRT1_2); }

/* Normal density exp(-z^2/2) / (sigma sqrt(2 pi)), z = (x-mu)/sigma;
 * NAN unless sigma > 0. */
double sd_normpdf(double x, double mu, double sigma)
{
    if (!(sigma > 0))
        return NAN;
    double z = (x - mu) / sigma;
    return exp(-0.5 * z * z - LN_SQRT_2PI) / sigma;
}

/* tails_fn of the standard normal: L = Phi(z), U = 1 - Phi(z), both via erfc
 * (par unused). */
static void norm_tails(double z, const double *par, double *L, double *U)
{
    *L = 0.5 * erfc(-z * SQRT1_2);
    *U = 0.5 * erfc(z * SQRT1_2);
}

/* normalcdf: the bounds are standardised and handed to the generic signed
 * area (infinite bounds stay infinite after standardising). */
double sd_normcdf(double lo, double hi, double mu, double sigma)
{
    if (!(sigma > 0))
        return NAN;
    return area(norm_tails, NULL, (clampinf(lo) - mu) / sigma, (clampinf(hi) - mu) / sigma);
}

/* invNorm: the x with P(X <= x) = p for X ~ N(mu, sigma^2), 0 < p < 1
 * (NAN otherwise).
 * Works with the lower tail q = min(p, 1-p) and its quantile x <= 0, then
 * reflects.  Acklam's rational approximations (relative error ~1.15e-9) give
 * the start: a rational function of r = (q-1/2)^2 in the central region,
 * one of t = sqrt(-2 ln q) in the tail (q < 0.02425).  Then Halley's method
 * on e(x) = Phi(x) - q, with u = e / phi(x):
 *   x <- x - u / (1 + x u / 2)
 * (cubically convergent; three steps reach full double precision).  Phi is
 * evaluated as erfc(-x/sqrt 2)/2, accurate in the lower tail. */
double sd_invnorm(double p, double mu, double sigma)
{
    if (!(sigma > 0) || !(p > 0) || !(p < 1))
        return NAN;
    static const double a[] = {-3.969683028665376e+01, 2.209460984245205e+02, -2.759285104469687e+02,
                               1.383577518672690e+02,  -3.066479806614716e+01, 2.506628277459239e+00};
    static const double b[] = {-5.447609879822406e+01, 1.615858368580409e+02, -1.556989798598866e+02,
                               6.680131188771972e+01, -1.328068155288572e+01};
    static const double c[] = {-7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00,
                               -2.549732539343734e+00, 4.374664141464968e+00,  2.938163982698783e+00};
    static const double d[] = {7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00,
                               3.754408661907416e+00};
    double q = p < 0.5 ? p : 1 - p, x;
    if (q < 0.02425) {
        double t = sqrt(-2 * log(q));
        x = (((((c[0] * t + c[1]) * t + c[2]) * t + c[3]) * t + c[4]) * t + c[5]) /
            ((((d[0] * t + d[1]) * t + d[2]) * t + d[3]) * t + 1);
    } else {
        double t = q - 0.5, r = t * t;
        x = (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * t /
            (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1);
        x = -fabs(x); /* lower half */
    }
    /* x solves Phi(x) = q (lower tail, x <= 0): Halley refinement */
    for (int i = 0; i < 3; i++) {
        double e = 0.5 * erfc(-x * SQRT1_2) - q;
        double u = e * sqrt(2 * M_PI) * exp(0.5 * x * x);
        x = x - u / (1 + 0.5 * x * u);
    }
    if (p > 0.5)
        x = -x;
    return mu + sigma * x;
}

/* ---------------------------------------------------------- Student t */
/* Upper tail of Student's t for t >= 0 (A&S 26.7):
 *   P(T >= t) = I_{df/(df+t^2)}(df/2, 1/2) / 2,
 * with x = df/(df+t^2) and y = t^2/(df+t^2) passed separately so large t
 * (x -> 0) and small t (y -> 0) are both accurate. */
static double t_tail(double t, double df) /* P(T >= t), t >= 0 */
{
    double t2 = t * t;
    if (isinf(t))
        return 0;
    return 0.5 * betainc2(0.5 * df, 0.5, df / (df + t2), t2 / (df + t2));
}

/* P(T >= t) for any real t, by symmetry of the t distribution. */
double sd_t_q(double t, double df)
{
    if (!(df > 0) || isnan(t))
        return NAN;
    return t >= 0 ? t_tail(t, df) : 1 - t_tail(-t, df);
}

/* tails_fn of Student's t (par[0] = df): the tail on the far side of 0 is
 * computed directly, the other as its complement (that one is >= 1/2, so
 * 1 - q loses nothing relative to its size). */
static void t_tails(double t, const double *par, double *L, double *U)
{
    double q = t_tail(fabs(t), par[0]);
    if (t >= 0)
        *U = q, *L = 1 - q;
    else
        *L = q, *U = 1 - q;
}

/* Student t density Gamma((df+1)/2) / (sqrt(df pi) Gamma(df/2))
 * * (1 + x^2/df)^(-(df+1)/2), evaluated in logs (log1p for small x^2/df).
 * par[0] = df > 0 (checked by the callers). */
static double t_pdf(double x, const double *par)
{
    double df = par[0];
    return exp(lgamma(0.5 * (df + 1)) - lgamma(0.5 * df) - 0.5 * (df + 1) * log1p(x * x / df)) / sqrt(df * M_PI);
}

double sd_tpdf(double x, double df) { return df > 0 ? t_pdf(x, &df) : NAN; }

double sd_tcdf(double lo, double hi, double df) { return df > 0 ? area(t_tails, &df, lo, hi) : NAN; }

/* invT: quantile of Student's t.  df = 1 (Cauchy) has the closed form
 * tan(pi (p - 1/2)).  Otherwise the starting point is the first-order
 * Cornish-Fisher correction of the normal quantile z,
 *   x0 = z + (z^3 + z) / (4 df)   (A&S 26.7),
 * refined by the generic safeguarded Newton inversion. */
double sd_invt(double p, double df)
{
    if (!(df > 0) || !(p > 0) || !(p < 1))
        return NAN;
    if (df == 1)
        return p == 0.5 ? 0 : tan(M_PI * (p - 0.5));
    double z = sd_invnorm(p, 0, 1);
    double x0 = z + (z * z * z + z) / (4 * df);
    return invert(t_tails, t_pdf, &df, p, x0, -INFINITY, INFINITY);
}

/* ---------------------------------------------------------- chi-square */
/* tails_fn of chi-square with par[0] = k degrees of freedom:
 * L = P(k/2, x/2), U = Q(k/2, x/2) (A&S 26.4). */
static void chi2_tails(double x, const double *par, double *L, double *U)
{
    *L = sd_gammp(0.5 * par[0], 0.5 * x);
    *U = sd_gammq(0.5 * par[0], 0.5 * x);
}

/* Chi-square density x^(k/2-1) e^(-x/2) / (2^(k/2) Gamma(k/2)), k = par[0].
 * At x = 0 the density is +inf for k < 2, 1/2 for k = 2 and 0 for k > 2.
 * For k >= 2 the saddle-point Poisson form is used (accurate for large k);
 * for k < 2 dpois_raw would need a negative "x", so plain logs are used. */
static double chi2_pdf(double x, const double *par)
{
    double k = par[0];
    if (x < 0)
        return 0;
    if (x == 0)
        return k < 2 ? INFINITY : k == 2 ? 0.5 : 0;
    /* x^(k/2-1) e^(-x/2) / (2^(k/2) Gamma(k/2)) = dpois_raw(k/2-1, x/2) / 2 */
    if (k >= 2)
        return 0.5 * dpois_raw(0.5 * k - 1, 0.5 * x);
    return exp((0.5 * k - 1) * log(x) - 0.5 * x - 0.5 * k * M_LN2 - lgamma(0.5 * k));
}

/* Public chi-square density and upper tail P(X >= x) (df > 0, else NAN). */
double sd_chi2pdf(double x, double df) { return df > 0 ? chi2_pdf(x, &df) : NAN; }
double sd_chi2_q(double x, double df) { return df > 0 ? sd_gammq(0.5 * df, 0.5 * x) : NAN; }

/* chi2cdf: signed area; negative bounds are clamped to 0 (support [0, inf)). */
double sd_chi2cdf(double lo, double hi, double df)
{
    if (!(df > 0))
        return NAN;
    return area(chi2_tails, &df, lo < 0 ? 0 : lo, hi < 0 ? 0 : hi);
}

/* invChi2: quantile of chi-square.  Start from the Wilson-Hilferty
 * approximation (X/k)^(1/3) ~ N(1 - 2/(9k), 2/(9k)), i.e.
 *   x0 = k (1 - h + z sqrt(h))^3,  h = 2/(9k),
 * which can be <= 0 for small k and p, in which case a plain positive guess
 * is used; then the safeguarded Newton inversion on (0, inf). */
double sd_invchi2(double p, double df)
{
    if (!(df > 0) || !(p > 0) || !(p < 1))
        return NAN;
    double z = sd_invnorm(p, 0, 1), h = 2 / (9 * df);
    double x0 = df * pow(1 - h + z * sqrt(h), 3);
    if (!(x0 > 0))
        x0 = df > 1 ? df : 0.5;
    return invert(chi2_tails, chi2_pdf, &df, p, x0, 0, INFINITY);
}

/* ---------------------------------------------------------- F */
/* tails_fn of the F distribution (par = {d1, d2}).  With
 * u = d1 x / (d1 x + d2) and v = d2 / (d1 x + d2) = 1 - u (A&S 26.6):
 *   L = I_u(d1/2, d2/2),   U = I_v(d2/2, d1/2),
 * both computed from the two independently formed ratios (no 1 - u). */
static void f_tails(double x, const double *par, double *L, double *U)
{
    double d1 = par[0], d2 = par[1];
    if (x <= 0) {
        *L = 0, *U = 1;
        return;
    }
    if (isinf(x)) {
        *L = 1, *U = 0;
        return;
    }
    double den = d1 * x + d2, u = d1 * x / den, v = d2 / den;
    *L = betainc2(0.5 * d1, 0.5 * d2, u, v);
    *U = betainc2(0.5 * d2, 0.5 * d1, v, u);
}

/* F density (par = {d1, d2}); at x = 0 it is +inf for d1 < 2, 1 for d1 = 2
 * and 0 for d1 > 2. */
static double f_pdf(double x, const double *par)
{
    double d1 = par[0], d2 = par[1];
    if (x < 0)
        return 0;
    if (x == 0)
        return d1 < 2 ? INFINITY : d1 == 2 ? 1 : 0;
    /* From the beta density: f(x) = u^a v^b / (x B(a,b)) with a = d1/2,
     * b = d2/2, u = d1 x/(d1 x + d2), v = 1 - u.  The factor u^a v^b / B(a,b)
     * equals dbinom_raw(a; a+b, u, v) * a b / (a+b), since
     * 1/B(a,b) = Gamma(a+b+1) / (Gamma(a+1) Gamma(b+1)) * a b / (a+b). */
    double den = d1 * x + d2, u = d1 * x / den, v = d2 / den, a = 0.5 * d1, b = 0.5 * d2;
    return dbinom_raw(a, a + b, u, v) * (a * b / (a + b)) / x;
}

/* Public F density (NAN unless d1, d2 > 0). */
double sd_fpdf(double x, double d1, double d2)
{
    double par[2] = {d1, d2};
    return d1 > 0 && d2 > 0 ? f_pdf(x, par) : NAN;
}

/* Upper tail P(F >= x) for the F test (ANOVA, two-variance test). */
double sd_f_q(double x, double d1, double d2)
{
    double par[2] = {d1, d2}, L, U;
    if (!(d1 > 0) || !(d2 > 0))
        return NAN;
    f_tails(x, par, &L, &U);
    return U;
}

/* Fcdf: signed area; negative bounds are clamped to 0 (support [0, inf)). */
double sd_fcdf(double lo, double hi, double d1, double d2)
{
    double par[2] = {d1, d2};
    if (!(d1 > 0) || !(d2 > 0))
        return NAN;
    return area(f_tails, par, lo < 0 ? 0 : lo, hi < 0 ? 0 : hi);
}

/* invF: quantile of F by the safeguarded Newton inversion, started at x0 = 1
 * (near the median for moderate degrees of freedom; the bracketing phase
 * copes with anything else). */
double sd_invf(double p, double d1, double d2)
{
    double par[2] = {d1, d2};
    if (!(d1 > 0) || !(d2 > 0) || !(p > 0) || !(p < 1))
        return NAN;
    return invert(f_tails, f_pdf, par, p, 1, 0, INFINITY);
}

/* ---------------------------------------------------------- discrete */
/* 1 if v is an integer of moderate size (|v| < 1e15, so it is exact in a
 * double and n - x etc. stay exact). */
static int is_int(double v) { return v == floor(v) && fabs(v) < 1e15; }

/* binompdf: P(X = x) for X ~ Bin(n, p) via Loader's saddle-point form;
 * NAN for invalid n or p, 0 for non-integer or out-of-range x. */
double sd_binompdf(double n, double p, double x)
{
    if (!is_int(n) || n < 0 || !(p >= 0 && p <= 1) || isnan(x))
        return NAN;
    if (!is_int(x) || x < 0 || x > n)
        return 0;
    return dbinom_raw(x, n, p, 1 - p);
}

/* binomcdf: P(X <= floor(x)).  Instead of summing up to n+1 terms it uses
 * the identity (A&S 26.5)
 *   P(X <= k) = I_{1-p}(n-k, k+1),
 * one incomplete-beta evaluation whatever n is; q = 1-p and p are passed as
 * the (x, y) pair.  p = 0 and p = 1 are handled up front (degenerate
 * distributions at 0 and n). */
double sd_binomcdf(double n, double p, double x)
{
    if (!is_int(n) || n < 0 || !(p >= 0 && p <= 1) || isnan(x))
        return NAN;
    x = floor(x);
    if (x < 0)
        return 0;
    if (x >= n)
        return 1;
    if (p == 0)
        return 1;
    if (p == 1)
        return 0;
    return betainc2(n - x, x + 1, 1 - p, p);
}

/* poissonpdf: P(X = x) = mu^x e^-mu / x! (saddle-point form). */
double sd_poissonpdf(double mu, double x)
{
    if (!(mu > 0) || isnan(x))
        return NAN;
    if (!is_int(x) || x < 0)
        return 0;
    return dpois_raw(x, mu);
}

/* poissoncdf: P(X <= floor(x)) = Q(floor(x) + 1, mu) (A&S 26.4), the
 * regularized upper incomplete gamma, so no term-by-term summation. */
double sd_poissoncdf(double mu, double x)
{
    if (!(mu > 0) || isnan(x))
        return NAN;
    x = floor(x);
    if (x < 0)
        return 0;
    return sd_gammq(x + 1, mu);
}

/* geometpdf: P(first success on trial x) = p (1-p)^(x-1), with (1-p)^(x-1)
 * computed as exp((x-1) log1p(-p)) so a small p loses nothing.  p = 1 is
 * special-cased: log1p(-1) = -inf and 0 * -inf would give NaN at x = 1. */
double sd_geometpdf(double p, double x)
{
    if (!(p > 0 && p <= 1) || isnan(x))
        return NAN;
    if (!is_int(x) || x < 1)
        return 0;
    if (p == 1)
        return x == 1 ? 1.0 : 0.0;
    return p * exp((x - 1) * log1p(-p));
}

/* geometcdf: P(X <= floor(x)) = 1 - (1-p)^x, as -expm1(x log1p(-p)) so the
 * result is accurate even when it is tiny (small p). */
double sd_geometcdf(double p, double x)
{
    if (!(p > 0 && p <= 1) || isnan(x))
        return NAN;
    x = floor(x);
    if (x < 1)
        return 0;
    return -expm1(x * log1p(-p));
}
