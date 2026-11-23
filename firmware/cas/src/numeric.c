/*
 * numeric.c -- double / complex-double evaluation, evalf, float formatting,
 * and cas_compile (expression -> stack bytecode for fast graphing).
 *
 * The symbolic engine is exact; this module is where expressions meet
 * hardware floating point.  It has four parts:
 *
 *  1. Scalar kernels.  num_fn_double() evaluates every builtin function on
 *     real doubles (returning 0 outside the real domain), using libm plus a
 *     few special functions implemented here:
 *       Si, Ci   power series for |x| < 24, otherwise the asymptotic
 *                expansions of the auxiliary functions f, g (Abramowitz &
 *                Stegun ch. 5.2; DLMF ch. 6);
 *       Ei       power series gamma + ln|x| + sum x^k/(k k!) for |x| < 40,
 *                asymptotic series e^x/x sum k!/x^k beyond (A&S ch. 5.1);
 *       erfi     power series (DLMF 7.6.1 with x -> ix);
 *       zeta     Borwein's alternating-series algorithm (P. Borwein, "An
 *                efficient algorithm for the Riemann zeta function", 2000,
 *                Algorithm 2) with n = 30 for real s >= 1/2, s != 1.
 *     A small complex-double library (cplx, below) adds the principal
 *     branches of exp, ln, pow, sqrt and the trig / inverse trig functions.
 *
 *  2. Evaluators.  ceval() walks an expression and computes a complex double,
 *     optionally binding one symbol to a value.  Public wrappers:
 *       eval_complex / to_double         closed constant expressions
 *       eval_double_at / eval_complex_at  f(x) at a point (solve, integ,
 *                                         nint, limits, sign checks, plots)
 *     They return 1 on success and 0 when the expression is not numeric
 *     (free symbols, unknown functions, lists) or the result is NaN; they
 *     never throw except through stack_check on absurdly deep input.
 *
 *  3. evalf / evalf_digits: replace exact numbers and constants by floats
 *     (approx(), approximate mode in cas.c).  Constant subexpressions are
 *     evaluated as a whole in complex double, or with arbitrary precision
 *     (bf_evalf, bf.c) when more than 15 digits are requested; non-constant
 *     expressions are rebuilt with their arguments evalf'd.
 *     Float formatting (fmt_double, fmt_float_node) converts the *exact*
 *     binary value of the double (num_from_double) to decimal with bignum
 *     arithmetic and round-half-even, so the printed digits are correctly
 *     rounded and no printf is needed (the library has no stdio).
 *
 *  4. cas_compile: compile_expr_vars() translates a simplified expression
 *     into a compact stack bytecode stored in the persistent heap;
 *     cas_fn_eval runs it with doubles and no allocation, which is what the
 *     grapher calls once per plotted pixel.  Domain errors give NaN, and
 *     rational powers with odd denominators use the real root, so x^(1/3)
 *     graphs for negative x as on a calculator.
 *
 * Memory: the evaluators use only the C stack.  evalf builds its result in
 * scratch; formatting releases its bignum temporaries with smark/srelease.
 * A compiled function is one halloc block owned by the caller (cas_fn_free
 * in cas.c releases it; cas_reset invalidates all of them).
 */
#include "cas_int.h"
#include "casx.h"
#include <math.h>
#include <string.h>

/* complex double (re + i*im); passed and returned by value */
typedef struct {
    double re, im;
} cplx;

/* ---- complex helpers: principal branches, as in C99 <complex.h> except
 * where noted (the freestanding ARM build has no complex library). */
static cplx C(double r, double i) {
    cplx z = {r, i};
    return z;
}
static cplx cadd(cplx a, cplx b) { return C(a.re + b.re, a.im + b.im); }
static cplx cmul(cplx a, cplx b) { return C(a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re); }
/* a / b by the textbook formula (no Smith scaling: |b|^2 may overflow or
 * underflow for extreme magnitudes; b = 0 gives inf/NaN components). */
static cplx cdiv(cplx a, cplx b) {
    double d = b.re * b.re + b.im * b.im;
    return C((a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d);
}
/* e^(x+iy) = e^x (cos y + i sin y) */
static cplx cexp_(cplx a) {
    double m = exp(a.re);
    return C(m * cos(a.im), m * sin(a.im));
}
/* principal log: ln|a| + i*arg(a), arg in (-pi, pi] */
static cplx clog_(cplx a) { return C(log(hypot(a.re, a.im)), atan2(a.im, a.re)); }
/*
 * Principal power a^b = exp(b ln a).  Special cases:
 *   0^b = 0 for Re b > 0, NaN otherwise (0^0 is left to the exact
 *   simplifier);
 *   a real with a > 0 or an integer real exponent: real libm pow(), which is
 *   exact for small integer powers and keeps (-2)^3 = -8 real instead of
 *   picking up a rounding-error imaginary part from exp(3 ln(-2)).
 */
static cplx cpow_(cplx a, cplx b) {
    if (a.re == 0 && a.im == 0) {
        if (b.re > 0) return C(0, 0);
        return C(NAN, NAN);
    }
    if (b.im == 0 && a.im == 0 && (a.re > 0 || b.re == floor(b.re))) return C(pow(a.re, b.re), 0);
    if (b.im == 0 && b.re == floor(b.re) && fabs(b.re) <= 64) {
        /* small integer power by squaring: (iy)^2 must stay real, or a
         * following sqrt lands on the wrong side of its branch cut */
        cplx r = C(1, 0), s = a;
        for (int k = (int)fabs(b.re); k; k >>= 1, s = cmul(s, s))
            if (k & 1) r = cmul(r, s);
        return b.re < 0 ? cdiv(C(1, 0), r) : r;
    }
    return cexp_(cmul(b, clog_(a)));
}
/* principal square root; exact sqrt() on the real axis (sqrt(-4) = 2i) */
static cplx csqrt_(cplx a) {
    if (a.im == 0) return a.re >= 0 ? C(sqrt(a.re), 0) : C(0, sqrt(-a.re));
    return cpow_(a, C(0.5, 0));
}
/* sin(x+iy) = sin x cosh y + i cos x sinh y;  cos(x+iy) = cos x cosh y - i sin x sinh y */
static cplx csin_(cplx a) { return C(sin(a.re) * cosh(a.im), cos(a.re) * sinh(a.im)); }
static cplx ccos_(cplx a) { return C(cos(a.re) * cosh(a.im), -sin(a.re) * sinh(a.im)); }
static const cplx CI = {0, 1};
/* asin z = -i ln(iz + sqrt(1-z^2)) (logarithmic form, DLMF 4.23.19); used
 * only when the real evaluation failed, i.e. off the real interval [-1, 1] */
static cplx casin_(cplx z) {
    cplx w = clog_(cadd(cmul(CI, z), csqrt_(cadd(C(1, 0), cmul(C(-1, 0), cmul(z, z))))));
    return cmul(C(0, -1), w);
}
/* acos z = pi/2 - asin z */
static cplx cacos_(cplx z) { return cadd(C(M_PI / 2, 0), cmul(C(-1, 0), casin_(z))); }
/* atan z = i/2 (ln(1 - iz) - ln(1 + iz)) */
static cplx catan_(cplx z) {
    cplx iz = cmul(CI, z);
    cplx d = cadd(clog_(cadd(C(1, 0), cmul(C(-1, 0), iz))), cmul(C(-1, 0), clog_(cadd(C(1, 0), iz))));
    return cmul(C(0, 0.5), d);
}

#define EULER_GAMMA 0.57721566490153286061

/*
 * Sine and cosine integrals: Si(x) (want_ci = 0) or Ci(x) (want_ci = 1).
 *
 *   Si(x) = sum_{k>=0} (-1)^k x^(2k+1) / ((2k+1) (2k+1)!)
 *   Ci(x) = gamma + ln x + sum_{k>=1} (-1)^k x^(2k) / (2k (2k)!)
 * for |x| < 24.  The terms grow to about e^|x| / sqrt(|x|) before they
 * decay, so near the switch point roughly 9-10 of the 16 significant digits
 * are lost to cancellation (absolute error around 1e-7); the code accepts
 * this.  `term` holds (-1)^k x^(2k+1)/(2k+1)! (resp. x^(2k)/(2k)!) and is
 * updated by the ratio of consecutive terms.
 *
 * For |x| >= 24 the asymptotic auxiliary functions are used:
 *   f(x) ~ (1/x)   sum (-1)^k (2k)!   / x^(2k)
 *   g(x) ~ (1/x^2) sum (-1)^k (2k+1)! / x^(2k)
 *   Si(x) = pi/2 - f cos x - g sin x,   Ci(x) = f sin x - g cos x
 * with a fixed 12 terms (the series diverges; its smallest term near
 * k ~ x would be far smaller, but 12 terms at x = 24 leave a relative error
 * of order 26!/24^26 ~ 1e-9).  Si is odd; Ci is only called for x > 0
 * (num_fn_double rejects x <= 0).
 */
static double si_ci(double x, int want_ci) {
    double ax = fabs(x);
    if (ax < 24) {
        /* power series (cancellation is acceptable below 24) */
        double sum = 0, term;
        if (!want_ci) {
            term = x;
            for (int k = 0; k < 200; k++) {
                double t = term / (2 * k + 1);
                sum += t;
                term *= -x * x / ((2.0 * k + 2) * (2.0 * k + 3));
                if (fabs(t) < 1e-18 * fabs(sum)) break;
            }
            return sum;
        }
        term = -x * x / 2;
        for (int k = 1; k < 200; k++) {
            double t = term / (2 * k);
            sum += t;
            term *= -x * x / ((2.0 * k + 1) * (2.0 * k + 2));
            if (fabs(t) < 1e-18 * fabs(sum) + 1e-300) break;
        }
        return EULER_GAMMA + log(ax) + sum;
    }
    /* asymptotic auxiliary functions */
    double f = 0, g = 0, t = 1 / ax, x2 = 1 / (ax * ax);
    double tf = t, tg = x2;
    for (int k = 0; k < 12; k++) {
        f += tf;
        g += tg;
        tf *= -(2.0 * k + 1) * (2.0 * k + 2) * x2;
        tg *= -(2.0 * k + 2) * (2.0 * k + 3) * x2;
    }
    if (want_ci) return f * sin(ax) - g * cos(ax);
    double r = M_PI / 2 - f * cos(ax) - g * sin(ax);
    return x < 0 ? -r : r;
}

/*
 * Exponential integral Ei(x) (Cauchy principal value for x > 0).
 *   -1 <= x < 40: Ei(x) = gamma + ln|x| + sum_{k>=1} x^k / (k k!)
 *   x >= 40:      Ei(x) ~ e^x / x * sum_{k>=0} k! / x^k, summed while the terms
 *                 decrease (optimal truncation of the divergent series).
 *   x < -1:       Ei(x) = -E1(-x) by a continued fraction (the series would
 *                 alternate with terms near e^|x| and cancel catastrophically).
 * Ei(0) = -inf.
 */
static double ei(double x) {
    if (x == 0) return -HUGE_VAL;
    if (x < -1) {
        /* Ei(x) = -E1(t), t = -x > 1: continued fraction for E1 evaluated
         * with the modified Lentz method (Numerical Recipes 6.3), free of
         * the cancellation of the alternating series */
        double t = -x, b = t + 1, c = 1e300, d = 1 / b, h = d;
        for (int i = 1; i < 200; i++) {
            double an = -(double)i * i;
            b += 2;
            d = 1 / (an * d + b);
            c = b + an / c;
            double del = c * d;
            h *= del;
            if (fabs(del - 1) < 1e-16) break;
        }
        return -h * exp(-t);
    }
    if (fabs(x) < 40) {
        double sum = 0, term = 1;
        for (int k = 1; k < 500; k++) {
            term *= x / k;
            double t = term / k;
            sum += t;
            if (fabs(t) < 1e-18 * fabs(sum)) break;
        }
        return EULER_GAMMA + log(fabs(x)) + sum;
    }
    double sum = 1, term = 1;
    for (int k = 1; k < 40; k++) {
        double nt = term * k / x;
        if (fabs(nt) > fabs(term)) break;
        term = nt;
        sum += term;
    }
    return exp(x) / x * sum;
}

/* Imaginary error function erfi(x) = -i erf(ix)
 *   = 2/sqrt(pi) * sum_{k>=0} x^(2k+1) / (k! (2k+1)).
 * All terms have the sign of x, so there is no cancellation; `term` is
 * x^(2k+1)/k!.  Overflows to inf for large |x| like the function itself. */
static double erfi_d(double x) {
    double sum = 0, term = x;
    for (int k = 0; k < 400; k++) {
        double t = term / (2 * k + 1);
        sum += t;
        term *= x * x / (k + 1);
        if (fabs(t) < 1e-18 * fabs(sum)) break;
    }
    return 2 / sqrt(M_PI) * sum;
}

/*
 * Real value of builtin function `id` applied to n (1 or 2) double
 * arguments x[0..n-1].  Returns 1 and stores the value in *out, or 0 when
 * the arguments are outside the real domain (sqrt(-1), ln(0), asin(2) ...),
 * the function is not numeric (unknown id) or the result is NaN.  Infinite
 * results (tan near pi/2, gamma at poles) are returned as is.
 * Conventions match the exact simplifier: ln(x, b) and log(x, b) are logs to
 * base b, log(x) is base 10 (calculator convention), acot has range
 * (0, pi), round is half away from zero, heaviside(0) = 1/2, arg of a
 * negative real is pi, mod is the floored remainder (sign of the divisor),
 * binomial is extended to reals through Gamma.
 * Used by ceval (below), by compiled bytecode (fn_run) and by other modules
 * (declared in cas_int.h).  Pure function, no allocation.
 */
int num_fn_double(int id, int n, const double *x, double *out) {
    double v = x[0], r;
    switch (id) {
    case F_SQRT: if (v < 0) return 0; r = sqrt(v); break;
    case F_EXP: r = exp(v); break;
    case F_LN: if (v <= 0) return 0; r = log(v); if (n == 2) { if (x[1] <= 0) return 0; r /= log(x[1]); } break;
    case F_LOG: if (v <= 0) return 0; r = log10(v); if (n == 2) { if (x[1] <= 0) return 0; r = log(v) / log(x[1]); } break;
    case F_LOG10: if (v <= 0) return 0; r = log10(v); break;
    case F_LOG2: if (v <= 0) return 0; r = log2(v); break;
    case F_SIN: r = sin(v); break;
    case F_COS: r = cos(v); break;
    case F_TAN: r = tan(v); break;
    case F_COT: r = 1.0 / tan(v); break;
    case F_SEC: r = 1.0 / cos(v); break;
    case F_CSC: r = 1.0 / sin(v); break;
    case F_ASIN: if (v < -1 || v > 1) return 0; r = asin(v); break;
    case F_ACOS: if (v < -1 || v > 1) return 0; r = acos(v); break;
    case F_ATAN: r = atan(v); break;
    case F_ACOT: r = M_PI / 2 - atan(v); break;
    case F_ASEC: if (fabs(v) < 1) return 0; r = acos(1 / v); break;
    case F_ACSC: if (fabs(v) < 1) return 0; r = asin(1 / v); break;
    case F_SINH: r = sinh(v); break;
    case F_COSH: r = cosh(v); break;
    case F_TANH: r = tanh(v); break;
    case F_ASINH: r = asinh(v); break;
    case F_ACOSH: if (v < 1) return 0; r = acosh(v); break;
    case F_ATANH: if (v <= -1 || v >= 1) return 0; r = atanh(v); break;
    case F_ABS: r = fabs(v); break;
    case F_SIGN: r = v > 0 ? 1 : v < 0 ? -1 : 0; break;
    case F_FLOOR: r = floor(v); break;
    case F_CEIL: r = ceil(v); break;
    case F_ROUND: r = v < 0 ? -floor(-v + 0.5) : floor(v + 0.5); break;
    case F_FRAC: r = v - floor(v); break;
    case F_GAMMA: r = tgamma(v); break;
    case F_FACT: r = tgamma(v + 1); break;
    case F_ERF: r = erf(v); break;
    case F_SI: r = si_ci(v, 0); break;
    case F_CI: if (v <= 0) return 0; r = si_ci(v, 1); break;
    case F_EI: r = ei(v); break;
    case F_ERFC: r = erfc(v); break;
    case F_ERFI: r = erfi_d(v); break;
    case F_ZETA: {
        /* Borwein's alternating-series algorithm (s != 1):
         *   zeta(s) = -1 / (d_N (1 - 2^(1-s))) *
         *             sum_{k=0}^{N-1} (-1)^k (d_k - d_N) / (k+1)^s
         * The error is about 3 / (3 + sqrt 8)^N relative, i.e. ~1e-23 for
         * N = 30, well below double precision, for Re s >= 1/2.  Smaller s
         * (where the bound degrades and the functional equation would be
         * needed) is rejected.  The factorials in d_i are formed through
         * lgamma to avoid overflow; the cost is O(N^2) lgamma calls. */
        if (v == 1) return 0;
        if (v < 0.5) return 0;
        const int N = 30;
        double d[31], term = 0, sumv = 0;
        for (int i = 0; i <= N; i++) {
            /* d_i = N sum_{j=0}^{i} (N+j-1)! 4^j / ((N-j)! (2j)!) */
            term = 0;
            for (int j = 0; j <= i; j++) {
                double lg = lgamma(N + j) + j * log(4.0) - lgamma(N - j + 1) - lgamma(2 * j + 1);
                term += exp(lg);
            }
            d[i] = N * term;
        }
        for (int k2 = 0; k2 < N; k2++) sumv += ((k2 & 1) ? -1 : 1) * (d[k2] - d[N]) / pow(k2 + 1, v);
        r = -sumv / (d[N] * (1 - pow(2.0, 1 - v)));
        break;
    }
    case F_HEAVISIDE: r = v > 0 ? 1 : v < 0 ? 0 : 0.5; break;
    case F_RE: r = v; break;
    case F_IM: r = 0; break;
    case F_CONJ: r = v; break;
    case F_ARG: r = v >= 0 ? 0 : M_PI; break;
    case F_ATAN2: if (n != 2) return 0; r = atan2(v, x[1]); break;
    case F_MOD: if (n != 2 || x[1] == 0) return 0; r = v - x[1] * floor(v / x[1]); break;
    case F_MAX: r = n == 2 ? (v > x[1] ? v : x[1]) : v; break;
    case F_MIN: r = n == 2 ? (v < x[1] ? v : x[1]) : v; break;
    case F_BINOM: if (n != 2) return 0; r = tgamma(v + 1) / (tgamma(x[1] + 1) * tgamma(v - x[1] + 1)); break;
    default: return 0;
    }
    if (r != r) return 0;
    *out = r;
    return 1;
}

/* Evaluation environment for ceval: symbol `var` is bound to `val` when
 * has_var is set (one free variable is all the numeric callers need). */
typedef struct {
    int var;
    cplx val;
    int has_var;
} cenv;

/*
 * Evaluate e in complex double arithmetic; env (may be NULL) binds one
 * symbol.  Returns 1 and the value in *out, or 0 if e contains anything that
 * has no numeric value: another free symbol, a list, a string, a command, a
 * function with more than two arguments (except the statistics
 * distributions), or a function outside the supported set.  NaN results are
 * not filtered here (the public wrappers do that).
 *
 * Constants: pi, e, i and euler_gamma.  Functions are evaluated with the
 * real kernel num_fn_double when all arguments are real and inside its
 * domain, and only otherwise with the complex principal branches, so real
 * results never pick up rounding noise in the imaginary part and e.g.
 * sqrt(-4) still evaluates to 2i.  Functions without a complex branch here
 * (cot, sec, csc, tanh, asinh, gamma, ... of a non-real argument, and every
 * two-argument function of complex arguments) make the evaluation fail.
 *
 * Powers: e^z uses cexp_; exponent 1/2 uses csqrt_ and -1 a division, so
 * the very common sqrt and reciprocal nodes avoid exp/log rounding.  In real
 * mode a negative real base with an exact exponent p/q, q odd, gives the
 * real root (real_odd_root, simp.c), as s_pow does.
 * Recursion depth is guarded by stack_check (may throw CAS_ERR_MEMORY).
 */
static E *cplx_to_expr(double re, double im);
static int ceval(E *e, cplx *out, cenv *env) {
    stack_check();
    switch (e->t) {
    case E_INT: case E_RAT: case E_FLT: *out = C(num_to_double(e), 0); return 1;
    case E_SYM:
        if (env && env->has_var && e->s == env->var) { *out = env->val; return 1; }
        if (e->s == S_PI) { *out = C(M_PI, 0); return 1; }
        if (e->s == S_E) { *out = C(M_E, 0); return 1; }
        if (e->s == S_I) { *out = C(0, 1); return 1; }
        if (e->s == S_EULERG) { *out = C(EULER_GAMMA, 0); return 1; }
        return 0;
    case E_ADD: {
        cplx s = C(0, 0), t;
        for (uint32_t i = 0; i < e->n; i++) {
            if (!ceval(EK(e)[i], &t, env)) return 0;
            s = cadd(s, t);
        }
        *out = s;
        return 1;
    }
    case E_MUL: {
        cplx s = C(1, 0), t;
        for (uint32_t i = 0; i < e->n; i++) {
            if (!ceval(EK(e)[i], &t, env)) return 0;
            s = cmul(s, t);
        }
        *out = s;
        return 1;
    }
    case E_POW: {
        cplx b, x;
        if (!ceval(K1(e), &x, env)) return 0;
        if (ISYM(K0(e), S_E)) {
            *out = cexp_(x);
            return 1;
        }
        if (!ceval(K0(e), &b, env)) return 0;
        int podd;
        if (b.im == 0 && b.re < 0 && real_odd_root(K1(e), &podd)) {
            /* real mode, exponent p/q with q odd: the real root */
            double r = pow(-b.re, x.re);
            *out = C(podd ? -r : r, 0);
        } else if (x.im == 0 && x.re == 0.5) *out = csqrt_(b);
        else if (x.im == 0 && x.re == -1) *out = cdiv(C(1, 0), b);
        else *out = cpow_(b, x);
        return 1;
    }
    case E_FN: {
        if (STATS_IS_CMD(e->s) && e->n >= 1 && e->n <= 4) { /* distributions (stats.c) */
            double xs[4];
            cplx t;
            for (uint32_t i = 0; i < e->n; i++) {
                if (!ceval(EK(e)[i], &t, env) || t.im != 0) return 0;
                xs[i] = t.re;
            }
            *out = C(stats_num(e->s, (int)e->n, xs), 0);
            return out->re == out->re;
        }
        if (CASX_SLOTS && e->s >= S__COUNT && e->n >= 1 && e->n <= 4) { /* cas/ext/int/fnx.c */
            E *v[4];
            cplx t;
            char *m = smark(); /* quadrature and plots evaluate it many times: no scratch kept */
            for (uint32_t i = 0; i < e->n; i++) {
                if (!ceval(EK(e)[i], &t, env)) {
                    srelease(m);
                    return 0;
                }
                v[i] = cplx_to_expr(t.re, t.im);
            }
            E *f = mk_fn(e->s, e->n, v);
            E *r = casx_eslot(CASX_FN_CEVAL, 1, &f);
            int ok = r && is_const_expr(r) && eval_complex(r, &out->re, &out->im);
            srelease(m);
            return ok;
        }
        /* ordinary functions: one argument, or one of the two-argument
         * functions listed below; first try the real kernel */
        if (e->n < 1 || e->n > 2) return 0;
        cplx a[2];
        for (uint32_t i = 0; i < e->n; i++)
            if (!ceval(EK(e)[i], &a[i], env)) return 0;
        cplx z = a[0];
        int id = e->s;
        if (e->n == 1 || id == F_ATAN2 || id == F_MOD || id == F_MAX || id == F_MIN || id == F_BINOM ||
            id == F_LN || id == F_LOG) {
            int real = a[0].im == 0 && (e->n == 1 || a[1].im == 0);
            double xs[2] = {a[0].re, e->n > 1 ? a[1].re : 0}, r;
            if (real && num_fn_double(id, (int)e->n, xs, &r)) {
                *out = C(r, 0);
                return 1;
            }
            /* complex argument or outside the real domain: principal
             * branches of the one-argument functions */
            if (e->n != 1) return 0;
            switch (id) {
            case F_SQRT: *out = csqrt_(z); return 1;
            case F_EXP: *out = cexp_(z); return 1;
            case F_LN: *out = clog_(z); return 1;
            case F_LOG10: case F_LOG: *out = cdiv(clog_(z), C(log(10.0), 0)); return 1;
            case F_SIN: *out = csin_(z); return 1;
            case F_COS: *out = ccos_(z); return 1;
            case F_TAN: *out = cdiv(csin_(z), ccos_(z)); return 1;
            case F_ASIN: *out = casin_(z); return 1;
            case F_ACOS: *out = cacos_(z); return 1;
            case F_ATAN: *out = catan_(z); return 1;
            case F_ABS: *out = C(hypot(z.re, z.im), 0); return 1;
            case F_RE: *out = C(z.re, 0); return 1;
            case F_IM: *out = C(z.im, 0); return 1;
            case F_CONJ: *out = C(z.re, -z.im); return 1;
            case F_ARG: *out = C(atan2(z.im, z.re), 0); return 1;
            case F_SINH: *out = cmul(C(0.5, 0), cadd(cexp_(z), cmul(C(-1, 0), cexp_(cmul(C(-1, 0), z))))); return 1;
            case F_COSH: *out = cmul(C(0.5, 0), cadd(cexp_(z), cexp_(cmul(C(-1, 0), z)))); return 1;
            case F_ACOSH: /* ln(z + sqrt(z+1) sqrt(z-1)): principal branch
                           * (ln(z + sqrt(z^2-1)) is wrong for z < -1) */
                *out = clog_(cadd(z, cmul(csqrt_(cadd(z, C(1, 0))), csqrt_(cadd(z, C(-1, 0))))));
                return 1;
            case F_ATANH: /* 1/2 ln((1+z)/(1-z)) */
                *out = cmul(C(0.5, 0), clog_(cdiv(cadd(C(1, 0), z), cadd(C(1, 0), cmul(C(-1, 0), z)))));
                return 1;
            }
            return 0;
        }
        return 0;
    }
    default: return 0;
    }
}

/* Value of a closed (variable-free) expression as a complex double.
 * Returns 0 if it is not numeric or a component is NaN. */
int eval_complex(E *e, double *re, double *im) {
    cplx z;
    if (!ceval(e, &z, NULL)) return 0;
    if (z.re != z.re || z.im != z.im) return 0;
    *re = z.re;
    *im = z.im;
    return 1;
}

/* Real value of a closed expression.  Fails (returns 0) when the value has
 * an imaginary part that is not negligible: |im| > 1e-12 |re|.  Used
 * throughout the engine for numeric sign tests and comparisons. */
int to_double(E *e, double *out) {
    double re, im;
    if (!eval_complex(e, &re, &im)) return 0;
    if (fabs(im) > 1e-12 * (fabs(re) + 1e-300) && im != 0) return 0;
    *out = re;
    return 1;
}

/* Real value of e with symbol `var` = x.  The imaginary part may be up to
 * 1e-9 (|re| + 1) (a looser, partly absolute tolerance than to_double,
 * because intermediate complex steps such as cube roots of negatives leave
 * rounding noise).  Returns 0 if not numeric, NaN or genuinely complex. */
int eval_double_at(E *e, int var, double x, double *out) {
    cenv env = {var, C(x, 0), 1};
    cplx z;
    if (!ceval(e, &z, &env)) return 0;
    if (z.re != z.re) return 0;
    if (fabs(z.im) > 1e-9 * (fabs(z.re) + 1)) return 0;
    *out = z.re;
    return 1;
}

/* Complex value of e with symbol `var` = xr + i*xi (used e.g. to test
 * identities at complex points).  Returns 0 if not numeric or NaN. */
int eval_complex_at(E *e, int var, double xr, double xi, double *re, double *im) {
    cenv env = {var, C(xr, xi), 1};
    cplx z;
    if (!ceval(e, &z, &env)) return 0;
    if (z.re != z.re || z.im != z.im) return 0;
    *re = z.re;
    *im = z.im;
    return 1;
}

/* Float expression re + im*i (just re when im == 0), simplified, in scratch. */
static E *cplx_to_expr(double re, double im) {
    if (im == 0) return mk_flt(re);
    return s_add2(mk_flt(re), s_mul2(mk_flt(im), K->i_));
}

/* Requested precision of the evalf in progress (0 = plain double).  Set only
 * by evalf_digits, which restores it on every exit path. */
static int g_evalf_digits;
void numeric_reset_statics(void); /* cas.c eval_impl: state an error may have left set */
void numeric_reset_statics(void) { g_evalf_digits = 0; }
E *bf_evalf(E *e, int digits);

/*
 * evalf with `digits` significant digits.  Above 15 digits, constant
 * subexpressions are evaluated with binary bigfloats (bf_evalf); otherwise
 * this is plain evalf.  The precision is passed to the recursion through
 * g_evalf_digits; a try frame restores the previous value if evalf throws,
 * then rethrows the error to the caller's handler.
 * Callers: cas.c (approximate mode), approx/evalf commands (cmds.c, xcmds.c).
 */
E *evalf_digits(E *e, int digits) {
    int old = g_evalf_digits;
    g_evalf_digits = digits;
    tryf tf;
    try_enter(&tf);
    E *volatile r = NULL;
    if (!setjmp(tf.jb)) {
        r = evalf(e);
        try_leave(&tf);
    } else {
        try_leave(&tf);
        g_evalf_digits = old;
        rethrow();
    }
    g_evalf_digits = old;
    return r;
}

/*
 * Approximate e numerically, returning a new expression in scratch:
 *   - high precision (g_evalf_digits > 15): a constant expression that is not
 *     already an extended float goes through bf_evalf; NULL from bf_evalf
 *     (unsupported function) falls back to the double path below;
 *   - numbers become doubles; pi, e, euler_gamma become their values; other
 *     symbols and strings stay as they are;
 *   - any other constant expression (no free symbols) is evaluated as a
 *     whole in complex double; a real or imaginary part smaller than 1e-14
 *     times the other is rounded to zero (cancellation noise), so
 *     cos(pi/3)+i*sin(pi/3) style values do not grow spurious components;
 *   - otherwise the node is rebuilt from evalf'd children (rebuild
 *     re-simplifies), keeping integer exponents exact so x^2 does not turn
 *     into x^2.0 (in real mode also odd roots: x^(1/3) stays x^(1/3)).  Lists (vectors, matrices) are handled elementwise this way.
 * Throws only through stack_check / allocation failure.
 */
E *evalf(E *e) {
    stack_check();
    if (g_evalf_digits > 15 && e->t != E_LIST && e->t != E_STR && is_const_expr(e) && !(e->t == E_FLT && e->n)) {
        E *r = bf_evalf(e, g_evalf_digits);
        if (r) return r;
    }
    switch (e->t) {
    case E_INT: case E_RAT: return mk_flt(num_to_double(e));
    case E_FLT: case E_STR: return e;
    case E_SYM:
        if (e->s == S_PI) return mk_flt(M_PI);
        if (e->s == S_E) return mk_flt(M_E);
        if (e->s == S_EULERG) return mk_flt(EULER_GAMMA);
        return e;
    default: break;
    }
    if (e->t != E_LIST && is_const_expr(e)) {
        double re, im;
        int ok = eval_complex(e, &re, &im);
        if (g_evalf_digits <= 15 && (!ok || (re == 0 && im == 0) || isinf(re))) {
            /* under- or overflow of a nonzero value (exp(-10^6)): bigfloat */
            E *r = bf_evalf(e, g_evalf_digits > 0 ? g_evalf_digits : K->set.digits);
            if (r && r->n && (EF(r) == 0 || isinf(EF(r)))) return r;
        }
        if (ok) {
            if (fabs(im) < 1e-14 * fabs(re)) im = 0;
            if (fabs(re) < 1e-14 * fabs(im)) re = 0;
            return cplx_to_expr(re, im);
        }
    }
    E **k = salloc(sizeof(E *) * (e->n + 1));
    for (uint32_t i = 0; i < e->n; i++) {
        /* keep integer exponents exact so x^2 stays x^2, and in real mode
         * odd roots too: x^(1/3) stays the real root (x^0.333 would be the
         * principal power once x is negative) */
        if (e->t == E_POW && i == 1 && (EK(e)[1]->t == E_INT || real_odd_root(EK(e)[1], NULL))) k[i] = EK(e)[1];
        else k[i] = evalf(EK(e)[i]);
    }
    extern E *rebuild(E *e, E **k);
    E *r = rebuild(e, k);
    double re, im; /* a function of the casx module (fnx.c) at numbers */
    if (CASX_SLOTS && r->t == E_FN && r->s >= S__COUNT && eval_complex(r, &re, &im)) return cplx_to_expr(re, im);
    return r;
}

/* ----------------------------------------------------- float printing --- */
/*
 * Print an exact positive rational ex with `digits` significant digits,
 * correctly rounded (round half to even).
 *
 * Algorithm: estimate the decimal exponent d = floor(log10 ex) from the
 * double value (or, if that over/underflows, from the bit lengths of the
 * numerator and denominator times log10 2).  Then compute
 *   N = round(ex * 10^(digits-1-d))
 * exactly with rationals.  N must have exactly `digits` digits,
 * 10^(digits-1) <= N < 10^digits; if the estimate of d was off by one (or
 * rounding carried into a new digit, 9.99 -> 10.0) adjust d and retry, at
 * most 4 times.
 * Output: trailing zeros of N are dropped; fixed notation for
 * -5 <= d < digits ("1234.5", "0.000123", always with a '.' so the parser
 * reads a float back), otherwise scientific "1.2345e-7" / "1.0e20".
 * Scratch is released before returning.
 */
static void fmt_rat(sbuf *b, E *ex, int digits, long dexp) {
    char *mark = smark();
    double approx = num_to_double(ex);
    int d;
    if (approx > 0 && approx < HUGE_VAL) d = (int)floor(log10(approx));
    else {
        long bn = (long)int_bits(num_numer(ex)) - (long)int_bits(num_denom(ex));
        d = (int)floor(bn * 0.30102999566398);
    }
    E *N = NULL;
    for (int attempt = 0; attempt < 4; attempt++) {
        int sh = digits - 1 - d;
        E *scaled = sh >= 0 ? num_mul(ex, int_pow(mk_int(10), (uint32_t)sh))
                            : num_div(ex, int_pow(mk_int(10), (uint32_t)(-sh)));
        E *fl = num_floor(scaled);
        E *fr = num_sub(scaled, fl);
        int c = num_cmp(fr, K->chalf);
        if (c > 0 || (c == 0 && !int_is_even(fl))) fl = int_add(fl, K->c1);
        E *lo = int_pow(mk_int(10), (uint32_t)(digits - 1)), *hi = int_mul(lo, mk_int(10));
        if (int_cmp(fl, hi) >= 0) { d++; continue; }
        if (int_cmp(fl, lo) < 0) { d--; continue; }
        N = fl;
        break;
    }
    if (!N) N = mk_int(0);
    size_t cap = (size_t)digits + 32;
    char *dig = salloc(cap);
    sbuf ds = {dig, cap, 0, 0};
    int_to_dec(&ds, N);
    int nd = (int)ds.len;
    while (nd > 1 && dig[nd - 1] == '0') nd--;
    if (d >= -5 && d < digits && !dexp) {
        if (d >= 0) {
            for (int i = 0; i <= d; i++) sb_putc(b, i < nd ? dig[i] : '0');
            sb_putc(b, '.');
            if (nd > d + 1)
                for (int i = d + 1; i < nd; i++) sb_putc(b, dig[i]);
            else
                sb_putc(b, '0');
        } else {
            sb_puts(b, "0.");
            for (int i = 0; i < -d - 1; i++) sb_putc(b, '0');
            for (int i = 0; i < nd; i++) sb_putc(b, dig[i]);
        }
    } else {
        sb_putc(b, dig[0]);
        sb_putc(b, '.');
        if (nd > 1)
            for (int i = 1; i < nd; i++) sb_putc(b, dig[i]);
        else
            sb_putc(b, '0');
        sb_putc(b, 'e');
        sb_int(b, d + dexp);
    }
    srelease(mark);
}

/*
 * Print a double with `digits` significant digits (clamped to 1..17: 17
 * digits identify any double uniquely, more would only print binary noise).
 * NaN prints as "undef" and infinities as "inf" / "-inf", which are the
 * parser's names for them.  The double is converted to its exact rational
 * value (num_from_double) and printed by fmt_rat.
 */
void fmt_double(sbuf *b, double x, int digits) {
    if (x != x) { sb_puts(b, "undef"); return; }
    if (x == HUGE_VAL) { sb_puts(b, "inf"); return; }
    if (x == -HUGE_VAL) { sb_puts(b, "-inf"); return; }
    if (digits < 1) digits = 1;
    if (digits > 17) digits = 17;
    if (x == 0) { sb_puts(b, "0.0"); return; }
    if (x < 0) {
        sb_putc(b, '-');
        x = -x;
    }
    char *mark = smark();
    fmt_rat(b, num_from_double(x), digits, 0);
    srelease(mark);
}

int bigflt_parts(E *node, E **val, int *digits, long *dexp);

/* Print a float node.  An extended-precision node (bf.c, n > 0) carries its
 * own exact mantissa and digit count, which are used instead of `digits`;
 * a plain double is printed with fmt_double(digits).  Used by print.c. */
void fmt_float_node(sbuf *b, E *e, int digits) {
    E *v;
    int nd;
    long dexp;
    char *mark = smark();
    if (bigflt_parts(e, &v, &nd, &dexp)) {
        if (num_is_zero(v)) sb_puts(b, "0.0");
        else {
            if (num_sgn(v) < 0) {
                sb_putc(b, '-');
                v = num_neg(v);
            }
            fmt_rat(b, v, nd, dexp);
        }
    } else
        fmt_double(b, EF(e), digits);
    srelease(mark);
}

/* ------------------------------------------------------------ compile --- *
 * Stack bytecode.  An expression compiles to postfix code for a stack
 * machine of doubles; each instruction pops its operands and pushes one
 * result:
 *   OP_C     push consts[arg]            OP_X     push vals[arg] (variable)
 *   OP_ADD   a b -> a+b                  OP_MUL   a b -> a*b
 *   OP_DIV   a b -> a/b                  OP_NEG   a -> -a (not emitted)
 *   OP_POW   a b -> pow(a, b)            OP_POWI  a -> a^arg (integer power)
 *   OP_RPOW  a -> real a^(p/q), q odd, arg = (p << 16) | q
 *   OP_SQRT  a -> sqrt(a)                OP_EXP   a -> exp(a)
 *   OP_FN1   a -> fn(a)                  OP_FN2   a b -> fn(a, b)
 *   OP_FNN   arg operands -> fn(...)     (statistics distributions)
 * fn is a builtin function id evaluated with num_fn_double (NaN outside the
 * real domain) or stats_num.  Example: x^2+sin(x) compiles to
 *   X0 POWI(2) X0 FN1(sin) ADD.                                           */
enum { OP_C, OP_X, OP_ADD, OP_MUL, OP_DIV, OP_NEG, OP_POW, OP_POWI, OP_RPOW, OP_FN1, OP_FN2, OP_SQRT, OP_EXP, OP_FNN, OP_GRID, OP_CMP, OP_SEL };
/* OP_GRID  x y -> bilinear interpolation in the table at consts[arg]: rows, cols, a, b, c, d,
 * then the rows (gridval(M, [a,b], [c,d], x, y) of cas/ext/sci/pdenum.c; NaN outside) */

/* one instruction (8 bytes); aux is unused (always 0) */
typedef struct {
    uint8_t op;
    uint8_t aux;
    uint16_t fn;
    int32_t arg;
} insn;

/* A compiled function (opaque cas_fn in cas.h).  Stored in a single
 * persistent-heap block: this header, then (8-byte aligned) the constant
 * pool, then the code.  maxstack <= 64 is guaranteed by the compiler, so the
 * evaluator can use a fixed-size stack. */
struct cas_fn {
    uint32_t ninsn, nconst, maxstack, nvars;
    insn *code;
    double *consts;
};

#define CFN_MAXVARS 24 /* cas_compile_vars: independent variables per function */
/* Compiler state: code and constant buffers in scratch (fixed capacities
 * cap / ccap), current and maximum simulated stack depth, and the symbol ids
 * of the variables (vars[i] is loaded by OP_X i). */
typedef struct {
    insn *code;
    double *consts;
    uint32_t n, nc, cap, ccap, depth, maxdepth;
    int vars[CFN_MAXVARS], nvars;
} cbuild;

/* 1 if e contains none of the function's variables (a constant to fold). */
static int cb_freeof(cbuild *cb, E *e) {
    for (int i = 0; i < cb->nvars; i++)
        if (!freeof_sym(e, cb->vars[i])) return 0;
    return 1;
}

/* Append one instruction; dstack is its net effect on the stack depth,
 * used to track the maximum depth.  Throws CAS_ERR_MEMORY when the code
 * buffer (4096 instructions) is full. */
static void emit(cbuild *cb, int op, int fn, int32_t arg, int dstack) {
    if (cb->n >= cb->cap) cas_throw(CAS_ERR_MEMORY, "expression too large to compile");
    insn *i = &cb->code[cb->n++];
    i->op = (uint8_t)op;
    i->aux = 0;
    i->fn = (uint16_t)fn;
    i->arg = arg;
    cb->depth = (uint32_t)((int)cb->depth + dstack);
    if (cb->depth > cb->maxdepth) cb->maxdepth = cb->depth;
}

/* Push constant v (appended to the pool, 1024 entries max, no dedup). */
static void emit_c(cbuild *cb, double v) {
    if (cb->nc >= cb->ccap) cas_throw(CAS_ERR_MEMORY, "expression too large to compile");
    cb->consts[cb->nc] = v;
    emit(cb, OP_C, 0, (int32_t)cb->nc++, 1);
}

/*
 * Compile e (post-order: operands first, then the operator).
 *   - Constant folding: any subexpression free of the variables whose value
 *     is a real double (to_double) becomes one OP_C, so sqrt(2)*pi costs a
 *     single push at run time.
 *   - Sums and products are left folds; a factor u^(-1) in a product becomes
 *     a division instead of a reciprocal and a multiplication.
 *   - Powers: e^u -> OP_EXP; small integer exponent -> OP_POWI (repeated
 *     squaring, valid for negative bases); exponent 1/2 -> OP_SQRT (NaN for
 *     negative arguments); p/q with q odd -> OP_RPOW, the sign-preserving
 *     real root used for graphing (x^(1/3) is defined for x < 0); anything
 *     else -> pow(), which is NaN for a negative base with non-integer
 *     exponent.
 *   - Functions with one or two arguments -> OP_FN1 / OP_FN2; statistics
 *     distributions with 1..4 arguments -> OP_FNN.
 *   - Relations (< > <= >= = !=), and / or / not -> OP_CMP (1 or 0, NaN
 *     propagates); piecewise(c1, v1, c2, v2, ..., [else]) and when(c, a, b)
 *     -> OP_SEL (both branches are computed, the condition picks one; no
 *     else: NaN, as is undef), so splines and other piecewise results plot.
 * Errors (thrown): CAS_ERR_UNDEFINED for a free symbol that is not one of
 * the variables, CAS_ERR_UNSUPPORTED for nodes with no numeric meaning
 * (lists, strings, functions of 3+ arguments, complex constants such as i),
 * CAS_ERR_MEMORY when the buffers overflow or the recursion is too deep.
 */
static void comp(cbuild *cb, E *e);

/* gridval comes with cas/ext/sci, which the core tier leaves out unless CONFIG_CASX_CORE_SCI */
#if defined(CONFIG_CONTENT_INTERNAL) && !defined(CONFIG_CASX_CORE_SCI)
#define OP_GRID_ON 0
#else
#define OP_GRID_ON 1
#endif

/* gridval(M, [a,b], [c,d], x, y) with a numeric matrix: the table goes into the constant pool
 * (grown as needed), x and y are compiled, OP_GRID looks the value up; 0 if M is not numeric */
#if OP_GRID_ON
static int comp_grid(cbuild *cb, E *e) {
    uint32_t r, c;
    E **k = EK(e);
    if (!is_matrix(k[0], &r, &c) || k[1]->t != E_LIST || k[1]->n != 2 || k[2]->t != E_LIST || k[2]->n != 2) return 0;
    uint32_t need = 6 + r * c;
    if (need > 300000) return 0;
    if (cb->nc + need > cb->ccap) {
        uint32_t cap = cb->nc + need + 1024;
        double *p = salloc(sizeof(double) * cap);
        memcpy(p, cb->consts, sizeof(double) * cb->nc);
        cb->consts = p, cb->ccap = cap;
    }
    double *g = cb->consts + cb->nc;
    g[0] = r, g[1] = c;
    for (int i = 0; i < 4; i++)
        if (!to_double(EK(k[1 + i / 2])[i % 2], &g[2 + i])) return 0;
    for (uint32_t i = 0; i < r; i++)
        for (uint32_t j = 0; j < c; j++)
            if (!to_double(EK(EK(k[0])[i])[j], &g[6 + i * c + j])) return 0;
    uint32_t base = cb->nc;
    cb->nc += need;
    comp(cb, k[3]);
    comp(cb, k[4]);
    emit(cb, OP_GRID, 0, (int32_t)base, -1);
    return 1;
}

static double grid_eval(const double *g, double x, double y) {
    long r = (long)g[0], c = (long)g[1];
    if (!(x >= g[2] && x <= g[3] && y >= g[4] && y <= g[5]) || !(g[2] < g[3] && g[4] < g[5])) return NAN;
    double fx = c > 1 ? (x - g[2]) / (g[3] - g[2]) * (double)(c - 1) : 0, fy = r > 1 ? (y - g[4]) / (g[5] - g[4]) * (double)(r - 1) : 0;
    long i = (long)fx, j = (long)fy;
    if (i > c - 2) i = c > 1 ? c - 2 : 0;
    if (j > r - 2) j = r > 1 ? r - 2 : 0;
    double tx = fx - (double)i, ty = fy - (double)j;
    const double *m = g + 6 + j * c + i;
    long di = c > 1, dj = r > 1 ? c : 0;
    return (1 - tx) * (1 - ty) * m[0] + tx * (1 - ty) * m[di] + (1 - tx) * ty * m[dj] + tx * ty * m[dj + di];
}
#else
#define comp_grid(cb, e) 0
#define grid_eval(g, x, y) NAN
#endif

static void comp(cbuild *cb, E *e) {
    stack_check();
    double v;
    if (cb_freeof(cb, e) && to_double(e, &v)) {
        emit_c(cb, v);
        return;
    }
    switch (e->t) {
    case E_SYM:
        if (e->s == S_UNDEF) {
            emit_c(cb, NAN);
            return;
        }
        for (int i = 0; i < cb->nvars; i++)
            if (e->s == cb->vars[i]) { emit(cb, OP_X, 0, i, 1); return; }
        cas_throw(CAS_ERR_UNDEFINED, "undefined variable in function");
    case E_ADD:
        comp(cb, EK(e)[0]);
        for (uint32_t i = 1; i < e->n; i++) {
            comp(cb, EK(e)[i]);
            emit(cb, OP_ADD, 0, 0, -1);
        }
        return;
    case E_MUL:
        comp(cb, EK(e)[0]);
        for (uint32_t i = 1; i < e->n; i++) {
            E *f = EK(e)[i];
            if (f->t == E_POW && is_int_val(K1(f), -1)) {
                comp(cb, K0(f));
                emit(cb, OP_DIV, 0, 0, -1);
            } else {
                comp(cb, f);
                emit(cb, OP_MUL, 0, 0, -1);
            }
        }
        return;
    case E_POW: {
        E *b = K0(e), *x = K1(e);
        long k;
        if (ISYM(b, S_E)) {
            comp(cb, x);
            emit(cb, OP_EXP, 0, 0, 0);
            return;
        }
        if (num_is_small_int(x, &k)) {
            comp(cb, b);
            emit(cb, OP_POWI, 0, (int32_t)k, 0);
            return;
        }
        if (x->t == E_RAT && is_int_val(K0(x), 1) && is_int_val(K1(x), 2)) {
            comp(cb, b);
            emit(cb, OP_SQRT, 0, 0, 0);
            return;
        }
        if (x->t == E_RAT && !int_is_even(K1(x))) {
            /* real odd root: sign-preserving for graphing */
            long p, q;
            if (num_is_small_int(K0(x), &p) && num_is_small_int(K1(x), &q) && q < 0x10000 &&
                p > -0x8000 && p < 0x8000) { /* must fit the packed arg */
                comp(cb, b);
                emit(cb, OP_RPOW, 0, (int32_t)((p << 16) | (q & 0xffff)), 0);
                return;
            }
        }
        comp(cb, b);
        comp(cb, x);
        emit(cb, OP_POW, 0, 0, -1);
        return;
    }
    case E_FN:
        switch (e->s) {
        case F_LT: case F_GT: case F_LE: case F_GE: case F_EQ: case F_NE: case F_AND: case F_OR:
            if (e->n < 2) break;
            comp(cb, EK(e)[0]);
            for (uint32_t i = 1; i < e->n; i++) {
                comp(cb, EK(e)[i]);
                emit(cb, OP_CMP, e->s, 0, -1);
            }
            return;
        case F_NOT:
            if (e->n != 1) break;
            comp(cb, K0(e));
            emit(cb, OP_CMP, F_NOT, 0, 0);
            return;
        case F_PIECEWISE:
        case C_WHEN: {
            if (e->n < 2 || (e->s == C_WHEN && e->n > 3)) break;
            uint32_t np = e->n / 2;
            if (e->n & 1) comp(cb, EK(e)[e->n - 1]);
            else emit_c(cb, NAN);
            for (uint32_t k = np; k-- > 0;) {
                comp(cb, EK(e)[2 * k + 1]);
                comp(cb, EK(e)[2 * k]);
                emit(cb, OP_SEL, 0, 0, -2);
            }
            return;
        }
        default: break;
        }
        if (OP_GRID_ON && e->n == 5 && e->s == mk_sym_name("gridval")->s && comp_grid(cb, e)) return;
        if (STATS_IS_CMD(e->s) && e->n >= 1 && e->n <= 4) { /* distributions (stats.c) */
            for (uint32_t i = 0; i < e->n; i++) comp(cb, EK(e)[i]);
            emit(cb, OP_FNN, e->s, (int32_t)e->n, 1 - (int)e->n);
            return;
        }
        if (e->n == 1) {
            comp(cb, K0(e));
            emit(cb, OP_FN1, e->s, 0, 0);
            return;
        }
        if (e->n == 2) {
            comp(cb, K0(e));
            comp(cb, K1(e));
            emit(cb, OP_FN2, e->s, 0, -1);
            return;
        }
        break;
    default: break;
    }
    if (is_num(e)) {
        emit_c(cb, num_to_double(e));
        return;
    }
    cas_throw(CAS_ERR_UNSUPPORTED, "cannot compile expression");
}

/*
 * Compile e as a function of the symbols vars[0..nvars-1] (cas_compile and
 * cas_compile_vars in cas.c call this after evaluating the input with those
 * variables hidden from user bindings).  The code is built in scratch
 * buffers and then copied into one persistent-heap block (halloc), which the
 * caller owns and frees with cas_fn_free.
 * Throws CAS_ERR_UNSUPPORTED for more than CFN_MAXVARS variables or when the
 * evaluation stack would exceed 64 entries, plus the errors of comp().
 */
cas_fn *compile_expr_vars(E *e, const int *vars, int nvars) {
    cbuild cb;
    memset(&cb, 0, sizeof cb);
    cb.cap = 4096;
    cb.ccap = 1024;
    cb.code = salloc(sizeof(insn) * cb.cap);
    cb.consts = salloc(sizeof(double) * cb.ccap);
    if (nvars > CFN_MAXVARS) cas_throw(CAS_ERR_UNSUPPORTED, "too many variables to compile");
    for (int i = 0; i < nvars; i++) cb.vars[i] = vars[i];
    cb.nvars = nvars;
    comp(&cb, e);
    if (cb.maxdepth > 64) cas_throw(CAS_ERR_UNSUPPORTED, "expression too deep to compile");
    /* layout: [cas_fn][pad to 8][consts][code]; +16 covers the padding */
    size_t sz = sizeof(cas_fn) + sizeof(insn) * cb.n + sizeof(double) * cb.nc + 16;
    cas_fn *f = halloc(sz);
    char *p = (char *)(f + 1);
    p = (char *)(((uintptr_t)p + 7) & ~(uintptr_t)7);
    f->consts = (double *)p;
    memcpy(f->consts, cb.consts, sizeof(double) * cb.nc);
    f->code = (insn *)(p + sizeof(double) * cb.nc);
    memcpy(f->code, cb.code, sizeof(insn) * cb.n);
    f->ninsn = cb.n;
    f->nconst = cb.nc;
    f->maxstack = cb.maxdepth;
    f->nvars = (uint32_t)nvars;
    return f;
}

/* Single-variable form used by cas_compile. */
cas_fn *compile_expr(E *e, int var) { return compile_expr_vars(e, &var, 1); }

/* b^k for an integer k by binary exponentiation (Knuth TAOCP vol. 2,
 * 4.6.3): O(log |k|) multiplications; negative k gives 1/b^|k|. */
static double powi_d(double b, long k) {
    int neg = k < 0;
    unsigned long u = neg ? (unsigned long)(-k) : (unsigned long)k;
    double r = 1;
    while (u) {
        if (u & 1) r *= b;
        b *= b;
        u >>= 1;
    }
    return neg ? 1.0 / r : r;
}

/*
 * Bytecode interpreter: run fn with variable values vals[0..nvars-1] and
 * return the top of the stack (NaN for empty code).  The stack is a local
 * array; the compiler guarantees maxstack <= 64, so no bounds checks are
 * needed.  No allocation, no K access: safe to call from the grapher's inner
 * loop.  Domain errors propagate as NaN / inf.
 */
static double fn_run(const cas_fn *fn, const double *vals) {
    double st[66];
    int sp = 0;
    for (uint32_t i = 0; i < fn->ninsn; i++) {
        const insn *in = &fn->code[i];
        switch (in->op) {
        case OP_C: st[sp++] = fn->consts[in->arg]; break;
        case OP_X: st[sp++] = vals[in->arg]; break;
        case OP_ADD: sp--; st[sp - 1] += st[sp]; break;
        case OP_MUL: sp--; st[sp - 1] *= st[sp]; break;
        case OP_DIV: sp--; st[sp - 1] /= st[sp]; break;
        case OP_NEG: st[sp - 1] = -st[sp - 1]; break;
        case OP_POW: sp--; st[sp - 1] = pow(st[sp - 1], st[sp]); break;
        case OP_POWI: st[sp - 1] = powi_d(st[sp - 1], in->arg); break;
        case OP_SQRT: st[sp - 1] = sqrt(st[sp - 1]); break;
        case OP_EXP: st[sp - 1] = exp(st[sp - 1]); break;
        case OP_RPOW: {
            /* q is odd, so the real root exists: (-|b|)^(p/q) =
             * (-1)^p |b|^(p/q) */
            long p = in->arg >> 16, q = in->arg & 0xffff;
            double b = st[sp - 1];
            double r = pow(fabs(b), (double)p / (double)q);
            if (b < 0 && (p & 1)) r = -r;
            st[sp - 1] = r;
            break;
        }
        case OP_FN1: {
            double r;
            if (!num_fn_double(in->fn, 1, &st[sp - 1], &r)) r = NAN;
            st[sp - 1] = r;
            break;
        }
        case OP_FNN: sp -= in->arg - 1; st[sp - 1] = stats_num(in->fn, in->arg, &st[sp - 1]); break;
        case OP_GRID: sp--; st[sp - 1] = grid_eval(&fn->consts[in->arg], st[sp - 1], st[sp]); break;
        case OP_FN2: {
            double r;
            sp--;
            if (!num_fn_double(in->fn, 2, &st[sp - 1], &r)) r = NAN;
            st[sp - 1] = r;
            break;
        }
        case OP_CMP: {
            double a = st[sp - 1], b = 0, r;
            if (in->fn != F_NOT) sp--, a = st[sp - 1], b = st[sp];
            switch (in->fn) {
            case F_LT: r = a < b; break;
            case F_GT: r = a > b; break;
            case F_LE: r = a <= b; break;
            case F_GE: r = a >= b; break;
            case F_EQ: r = a == b; break;
            case F_NE: r = a != b; break;
            case F_AND: r = a != 0 && b != 0; break;
            case F_OR: r = a != 0 || b != 0; break;
            default: r = a == 0; break;
            }
            st[sp - 1] = isnan(a) || isnan(b) ? NAN : r;
            break;
        }
        case OP_SEL: /* else, then, condition */
            sp -= 2;
            st[sp - 1] = isnan(st[sp + 1]) ? NAN : st[sp + 1] != 0 ? st[sp] : st[sp - 1];
            break;
        }
    }
    return sp ? st[sp - 1] : NAN;
}

/* Public API (cas.h): f(x) for a compiled function; for a multi-variable
 * function x is vars[0] and the other variables are 0.  NaN if fn is NULL. */
double cas_fn_eval(const cas_fn *fn, double x) {
    if (!fn) return NAN;
    if (fn->nvars <= 1) return fn_run(fn, &x);
    double v[CFN_MAXVARS] = {0};
    v[0] = x;
    return fn_run(fn, v);
}

/* Public API (cas.h): f(vals[0], ..., vals[nvars-1]); vals must hold one
 * value per compiled variable.  NaN if fn is NULL. */
double cas_fn_eval_vars(const cas_fn *fn, const double *vals) {
    if (!fn) return NAN;
    return fn_run(fn, vals);
}
