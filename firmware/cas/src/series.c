/*
 * series.c -- truncated generalised power series (Laurent/Puiseux with
 * symbolic ln(h) in coefficients) and limits.
 *
 * A series in h (h -> 0+) is  sum_{i<n} c[i] h^((val+i)/d)  + O(h^((val+n)/d)).
 * Relative precision n is preserved by products; sums can lose precision
 * through cancellation, in which case the whole computation is retried with a
 * larger working precision.  Elementary functions are composed through their
 * known expansions; other differentiable functions through Taylor
 * coefficients computed by symbolic differentiation.
 *
 * Limits: direct evaluation when continuous, otherwise series expansion at
 * the point (x = a + h, a - h, 1/h, -1/h); when the expansion fails
 * (essential singularities) a recursive limit calculus with l'Hopital's rule
 * for indeterminate quotients is used.
 *
 * Role in the CAS
 * ---------------
 * Public entry points (declared in cas_int.h):
 *   series_expr(e, x, a, n)  -- series/taylor command (cmds.c), pade
 *                               (xcmds.c).  Throws CAS_ERR_UNSUPPORTED if
 *                               the expansion fails.
 *   limit_expr(e, x, a, dir) -- limit command (cmds.c), definite integrals
 *                               at endpoints and poles (integ.c), sums to
 *                               infinity (sum.c).  Returns NULL when the
 *                               limit cannot be determined.
 * It uses diff() (diff.c) for Taylor coefficients and l'Hopital,
 * rat_normal() (poly.c) to normalise coefficients, subst() and the s_*
 * constructors (simp.c), and numeric evaluation (numeric.c) for sign tests.
 *
 * Series representation (struct S)
 * --------------------------------
 *   val, d : the leading exponent is val/d (d >= 1 is a common denominator
 *            of all exponents: Puiseux series);
 *   n, c[] : c[i] is the coefficient of h^((val+i)/d), i = 0..n-1; the
 *            series is known up to O(h^((val+n)/d)).  n is the number of
 *            known coefficients ("relative precision").
 * Coefficients are canonical expressions free of h.  They may contain the
 * symbol g_L (printed _lnh) standing for ln(h): ln h is not a power of h,
 * but it is "slower" than any power, so treating it as a constant in the
 * coefficient ring gives correct expansions of things like h^h or
 * ln(sin h) (the usual device for generalised power series, cf. Gruntz's
 * thesis cited below).  A series with n = 0 carries no information
 * except "zero to order val/d"; the exact zero is n = 0, val = SR_INFVAL.
 *
 * Arithmetic (cf. Knuth, TAOCP vol. 2, sec. 4.7 "Manipulation of power
 * series"): sums align exponents and keep the smaller absolute order;
 * products use the Cauchy convolution of the first min(n_a, n_b) terms
 * (relative precision is preserved); the reciprocal uses the standard
 * triangular recurrence; exp/ln/(1+u)^r/sin/cos are composed from their
 * Maclaurin series after splitting off the constant term (exp(c+u) =
 * e^c exp(u), ln(c h^q (1+u)) = ln c + q ln h + ln(1+u), ...).  Functions
 * without a built-in rule get Taylor coefficients by repeated symbolic
 * differentiation at the constant term.  Every coefficient goes through cz()
 * (rational normal form plus a numeric zero test), because cancellation
 * that is not recognised would make a zero coefficient look nonzero and
 * produce a wrong leading term.  Because sums can lose relative precision,
 * callers retry with a larger working precision g_prec (ser_retry,
 * series_expr).
 *
 * Limits (limit_expr -> lim_side, for each requested side)
 * --------------------------------------------------------
 *   1. Substitute x = a + h, a - h, 1/h or -1/h (h -> 0+); try direct
 *      substitution h = 0, then the leading term of the series in h.
 *   2. Gruntz's algorithm (D. Gruntz, "On Computing Limits in a Symbolic
 *      Manipulation System", PhD thesis, ETH Zurich, 1996) for x -> +inf
 *      after writing finite points as a +- 1/t.  Implemented: the mrv set
 *      (most rapidly varying subexpressions) with comparability classes
 *      decided by limits of ratios of logarithms, the upward move x -> e^x
 *      when x itself is in the mrv set, rewriting of all mrv elements as
 *      A * w^(-c) in one representative w = exp(-u) -> 0+, and the leading
 *      term of the series in w, recursing on the leading coefficient (of a
 *      lower comparability class).  ln w is replaced by -u.  As an
 *      extension, if the series engine cannot expand in w (rationally
 *      independent rates like 3^x vs e^x give irrational exponents of w),
 *      a leading-term-only computation with real exponents is used
 *      (lt_real).  Not implemented from the thesis: the full treatment of
 *      the sign/zero-equivalence problem (signs are found by sign_of, the
 *      recursive limit, or numeric sampling), and functions beyond
 *      exp-log except a few with known limits at infinity (atan, tanh,
 *      erf, Si, acot, abs, sqrt).
 *   3. A heuristic recursive limit calculus (lim0/lim_rec) that works on
 *      the expression tree with signed infinities, a "bounded but
 *      oscillating" marker (squeeze rule for bounded * 0), dominant-term
 *      factoring for inf - inf and ln of sums, exp(sum of logs) for 0*inf,
 *      and l'Hopital's rule applied only while the form is still 0/0 or
 *      inf/inf.
 *   Two-sided limits compare the right and left results (undef if they
 *   differ).
 *
 * State and memory
 * ----------------
 * The module keeps its working state in file-scope globals (g_prec, g_h,
 * g_L, g_fail, g_bounded, budgets), so it is not reentrant across threads,
 * though nested calls (lead_limit -> limit_expr) are tolerated: g_h is
 * saved/restored around each use.  Failure inside the series engine is
 * signalled by g_fail + NULL (the FAIL macro) rather than by cas_throw, so
 * a failed expansion can fall back to another method cheaply; real errors
 * (out of memory, interrupt, "coefficient expression swell") use
 * cas_throw and are caught by the try_enter/setjmp blocks in the limit
 * code, which rethrow interrupts.  All series and intermediate expressions
 * live in scratch; a few failed attempts release their scratch with
 * smark/srelease.  Work is bounded by depth limits and the budgets
 * g_lim_budget / g_gr_budget so that pathological inputs terminate.
 */
#include "cas_int.h"
#include <math.h>
#include <string.h>

/* 1 if e contains a discontinuous function (floor, ceil, round, frac, sign,
 * heaviside, mod).  Such expressions must not be evaluated by plain
 * substitution at the limit point, and Gruntz is not applied to them. */
static int has_discont(E *e) {
    stack_check();
    if (e->t == E_FN && (e->s == F_FLOOR || e->s == F_CEIL || e->s == F_ROUND || e->s == F_FRAC ||
                         e->s == F_SIGN || e->s == F_HEAVISIDE || e->s == F_MOD))
        return 1;
    if (e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_discont(EK(e)[i])) return 1;
    return 0;
}

/* Truncated generalised power series in g_h (see the file header):
 *   sum_{i<n} c[i] h^((val+i)/d) + O(h^((val+n)/d)).
 * Allocated in scratch with sr_new; treated as immutable once returned
 * (operations build new series, though they may return an argument
 * unchanged).
 *   val: exponent of c[0], in units of 1/d
 *   d:   common exponent denominator, >= 1
 *   n:   number of known coefficients (relative precision)
 *   c:   c[0..n-1], canonical expressions free of g_h */
typedef struct {
    long val;
    long d;
    long n;
    E **c;
} S;

/* g_prec: working relative precision for new series (constants, h itself);
 * g_lim_budget: remaining lim0 calls for one outermost lim_side;
 * g_fail: set by FAIL, the current expansion failed. */
static long g_prec;
static E *g_bounded; /* marker: bounded but oscillating */
static int g_lim_budget;
static E *g_h;    /* series variable symbol */
static E *g_L;    /* symbol standing for ln(h) */
static int g_fail;

/* Soft failure inside the series engine: mark the expansion as failed and
 * return NULL from the current function (msg is documentation only). */
#define FAIL(msg) do { g_fail = 1; return NULL; } while (0)

/*
 * cz -- normalise a series coefficient.  Exact numbers pass through; other
 * coefficients are put in rational normal form, and a constant (symbol-free)
 * nonzero result that evaluates numerically to |z| < 1e-25 is taken to be 0
 * (e.g. sin(1)^2 + cos(1)^2 - 1, which rat_normal cannot see).  This is a
 * heuristic zero test; missing a zero would make the series report a wrong
 * leading term.  Throws CAS_ERR_UNSUPPORTED if the coefficient exceeds 1500
 * nodes (expression swell), which aborts the whole expansion.
 */
/* has_fn -- e contains a function application or a non-integer power
 * (something rat_normal cannot decide zero-equivalence of) */
static int has_fn(E *e) {
    stack_check();
    if (e->t == E_FN) return 1;
    if (e->t == E_POW && K1(e)->t != E_INT) return 1;
    if (e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_fn(EK(e)[i])) return 1;
    return 0;
}

/* sym_zero -- heuristic zero test of an expression with symbols, such as
 * 1 - sin(a)^2 - cos(a)^2: a sum that is numerically zero (|z| < 1e-11
 * relative to the size of its terms) at two pseudo-random points of its free
 * symbols (constants: at their value). */
static int sym_zero(E *e) {
    E *sy[8], *vs[8];
    uint32_t n = collect_syms(e, sy, 8);
    if (e->t != E_ADD || n > 8 || !has_fn(e)) return 0;
    static const double pts[2][8] = {{0.6180339887, 1.3247179572, 0.4142135624, 2.7182818285, 0.5772156649,
                                      1.6066951524, 0.9159655942, 1.2020569032},
                                     {1.4142135624, 0.3183098862, 1.7320508076, 0.6931471806, 2.2360679775,
                                      0.8862269255, 1.0986122887, 0.2614972128}};
    for (int k = 0; k < (n ? 2 : 1); k++) {
        for (uint32_t j = 0; j < n; j++) vs[j] = mk_flt(pts[k][j]);
        double re, im, sc = 0, tr, ti;
        E *v = subst_many(e, n, sy, vs);
        if (!eval_complex(v, &re, &im) || !isfinite(re) || !isfinite(im)) return 0;
        for (uint32_t j = 0; j < e->n; j++)
            if (eval_complex(subst_many(EK(e)[j], n, sy, vs), &tr, &ti)) sc += hypot(tr, ti);
        if (hypot(re, im) > 1e-11 * sc) return 0;
    }
    return 1;
}

static E *cz(E *e) {
    /* coefficient normalisation */
    if (is_exactnum(e)) return e;
    if (expr_size(e) > 1500) cas_throw(CAS_ERR_UNSUPPORTED, "series: coefficient expression swell");
    E *r = rat_normal(e);
    if (!is_zero(r) && is_const_expr(r) && !has_float(r)) {
        double re, im;
        if (eval_complex(r, &re, &im) && fabs(re) < 1e-25 && fabs(im) < 1e-25) {
            /* numerically zero constant (e.g. sin(1)^2+cos(1)^2-1) */
            return K->c0;
        }
    }
    if (!is_zero(r) && !has_float(r) && expr_size(r) < 400 && sym_zero(r)) return K->c0;
    return r;
}

/* New series with leading exponent val/d and n zero coefficients (n < 0 is
 * clamped to 0).  Scratch allocated. */
static S *sr_new(long val, long d, long n) {
    S *s = salloc(sizeof(S));
    s->val = val;
    s->d = d;
    s->n = n < 0 ? 0 : n;
    s->c = salloc(sizeof(E *) * (size_t)(s->n + 1));
    for (long i = 0; i < s->n; i++) s->c[i] = K->c0;
    return s;
}

/* strip leading zeros
 * Each removed zero raises val by one and lowers the relative precision n
 * by one (the absolute order val+n is unchanged).  Returns s itself if
 * c[0] != 0; a series that is zero to all known orders ends with n = 0. */
static S *sr_trim(S *s) {
    long k = 0;
    while (k < s->n && is_zero(s->c[k])) k++;
    if (k == 0) return s;
    S *r = sr_new(s->val + k, s->d, s->n - k);
    for (long i = 0; i < r->n; i++) r->c[i] = s->c[i + k];
    return r;
}

/* gcd of |a| and |b| (Euclid); gcdl(0, 0) = 0. */
static long gcdl(long a, long b) {
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b) {
        long t = a % b;
        a = b;
        b = t;
    }
    return a;
}

/* Leading exponent used for the exact zero series (n = 0): larger than any
 * real exponent, so it never wins a min() in sr_add. */
#define SR_INFVAL (1L << 28)

/* re-express with denominator D (multiple of s->d)
 * Exponent (val+i)/d becomes (k(val+i))/D with k = D/d, so coefficients
 * move to index k*i and the gaps are zero; n grows to k*n.  The exact zero
 * series stays the exact zero. */
static S *sr_redenom(S *s, long D) {
    if (D == s->d) return s;
    if (s->n == 0 && s->val >= SR_INFVAL / 2) {
        S *z = sr_new(SR_INFVAL, D, 0);
        return z;
    }
    long k = D / s->d;
    S *r = sr_new(s->val * k, D, s->n * k);
    for (long i = 0; i < s->n; i++) r->c[i * k] = s->c[i];
    return r;
}

/* Series of the constant c (free of h): c + O(h^g_prec).  An exact 0 gives
 * the exact zero series; a float 0.0 is kept as a coefficient (then
 * trimmed away, leaving "zero to order g_prec"). */
static S *sr_const(E *c) {
    if (is_zero(c) && c->t != E_FLT) return sr_new(SR_INFVAL, 1, 0); /* exact zero */
    S *s = sr_new(0, 1, g_prec);
    if (g_prec > 0) s->c[0] = c;
    return sr_trim(s);
}

/*
 * sr_add -- a + b.  Exponents are brought to a common denominator; the
 * result starts at min(val) and is known up to the smaller absolute order
 * min(val_a + n_a, val_b + n_b).  Leading terms may cancel, so after
 * sr_trim the relative precision can be much smaller than either input's;
 * this is the precision loss handled by the retry loops.
 */
static S *sr_add(S *a, S *b) {
    if (a->d != b->d) {
        long D = a->d / gcdl(a->d, b->d) * b->d;
        a = sr_redenom(a, D);
        b = sr_redenom(b, D);
    }
    long v = a->val < b->val ? a->val : b->val;
    long oa = a->val + a->n, ob = b->val + b->n;
    long o = oa < ob ? oa : ob;
    S *r = sr_new(v, a->d, o - v);
    for (long i = 0; i < r->n; i++) {
        long e = v + i;
        E *x = (e >= a->val && e < oa) ? a->c[e - a->val] : K->c0;
        E *y = (e >= b->val && e < ob) ? b->c[e - b->val] : K->c0;
        r->c[i] = cz(s_add2(x, y));
    }
    return sr_trim(r);
}

/* c * a for a constant (h-free) expression c. */
static S *sr_scale(S *a, E *c) {
    S *r = sr_new(a->val, a->d, a->n);
    for (long i = 0; i < a->n; i++) r->c[i] = cz(s_mul2(a->c[i], c));
    return sr_trim(r);
}

/* -a */
static S *sr_neg(S *a) { return sr_scale(a, K->cm1); }
static S *sraddc(S *a, E *c);

/*
 * sr_mul -- a * b by truncated Cauchy product.  Leading exponents add;
 * relative precision is min(n_a, n_b), so products never lose precision
 * (inputs are normally trimmed, i.e. c[0] != 0).  O(n^2) coefficient
 * multiplications, each sum normalised by cz().  Polls for interrupts.
 */
static S *sr_mul(S *a, S *b) {
    if (a->d != b->d) {
        long D = a->d / gcdl(a->d, b->d) * b->d;
        a = sr_redenom(a, D);
        b = sr_redenom(b, D);
    }
    long n = a->n < b->n ? a->n : b->n;
    S *r = sr_new(a->val + b->val, a->d, n);
    for (long i = 0; i < n; i++) {
        poll_intr();
        E **t = salloc(sizeof(E *) * (size_t)(i + 1));
        for (long j = 0; j <= i; j++) t[j] = s_mul2(a->c[j], b->c[i - j]);
        r->c[i] = cz(s_add((uint32_t)(i + 1), t));
    }
    return sr_trim(r);
}

/* a + c for a constant c. */
static S *sraddc(S *a, E *c) { return sr_add(a, sr_const(c)); }

/* 1/a
 * Writes a = h^(val/d) (c0 + c1 h^(1/d) + ...); then 1/a = h^(-val/d) (r0 +
 * r1 h^(1/d) + ...) with r0 = 1/c0 and the triangular recurrence below
 * (Knuth TAOCP vol. 2, 4.7).  Relative precision is preserved.  FAILs
 * (returns NULL, sets g_fail) if a has no known nonzero term.  O(n^2). */
static S *sr_inv(S *a) {
    a = sr_trim(a);
    if (a->n == 0) FAIL("precision");
    E *c0 = a->c[0];
    E *ic0 = cz(s_inv(c0));
    long n = a->n;
    S *r = sr_new(-a->val, a->d, n);
    /* r_k = -(1/c0) sum_{j=1..k} a_j r_{k-j} */
    for (long k = 0; k < n; k++) {
        poll_intr();
        if (k == 0) {
            r->c[0] = ic0;
            continue;
        }
        E **t = salloc(sizeof(E *) * (size_t)k);
        for (long j = 1; j <= k; j++) t[j - 1] = s_mul2(a->c[j], r->c[k - j]);
        r->c[k] = cz(s_mul2(s_neg(ic0), s_add((uint32_t)k, t)));
    }
    return r;
}

/* a^k for an integer k by binary powering (negative k via sr_inv).
 * NULL (g_fail set) if a must be inverted and is zero to all known orders. */
static S *sr_pow_int(S *a, long k) {
    if (k < 0) {
        a = sr_inv(a);
        if (!a) return NULL;
        k = -k;
    }
    S *r = sr_const(K->c1);
    while (k) {
        if (k & 1) r = sr_mul(r, a);
        k >>= 1;
        if (k) a = sr_mul(a, a);
    }
    return r;
}

/* split s = s0 + s1 where s0 is the h^0 coefficient and s1 -> 0.
 * Fails if s has negative exponents.
 * This is the "constant term + infinitesimal" split used before composing
 * with exp, sin, Taylor series, ...  Returns 1 on success, 0 (without
 * setting g_fail) if s starts with a negative power, i.e. s -> infinity. */
static int sr_split0(S *s, E **s0, S **s1) {
    if (s->val < 0) return 0;
    *s0 = K->c0;
    S *r = sr_new(s->val, s->d, s->n);
    for (long i = 0; i < s->n; i++) r->c[i] = s->c[i];
    if (s->val == 0 && s->n > 0) {
        *s0 = s->c[0];
        r->c[0] = K->c0;
    }
    *s1 = sr_trim(r);
    return 1;
}

/* sum_k coef[k] * u^k, u -> 0
 * Composition of a power series (given by ncoef coefficients) with an
 * infinitesimal series u (leading exponent > 0).  Powers of u are built
 * incrementally; the loop stops early once u^k lies beyond the precision
 * of the accumulated result (with a safety margin of 2*u->d exponent units)
 * or u^k is zero to all known orders.  O(ncoef * n^2). */
static S *sr_compose(E **coef, long ncoef, S *u) {
    S *r = sr_const(coef[0]);
    S *p = sr_const(K->c1);
    for (long k = 1; k < ncoef; k++) {
        poll_intr();
        p = sr_mul(p, u);
        if (p->n == 0) break;
        /* stop when power's valuation exceeds precision of result */
        if (!is_zero(coef[k])) r = sr_add(r, sr_scale(p, coef[k]));
        if (p->val > r->val + r->n + 2 * u->d) break;
    }
    return r;
}

/* subst that returns NULL instead of throwing (division by zero etc.)
 * Interrupts and out-of-memory are rethrown. */
static E *safe_subst(E *e, E *x, E *v) {
    tryf tf;
    try_enter(&tf);
    E *volatile r = NULL;
    if (!setjmp(tf.jb)) {
        r = subst(e, x, v);
        try_leave(&tf);
    } else {
        try_leave(&tf);
        if (K->err == CAS_ERR_INTERRUPTED || K->err == CAS_ERR_MEMORY) rethrow();
        r = NULL;
    }
    return r;
}

/* Number of Maclaurin terms f_0..f_{N-1} needed to compose f with the
 * infinitesimal u to u's own absolute precision; capped at 400. */
static long nterms(S *u) {
    /* number of terms needed for composition with u -> 0 */
    if (u->n == 0) return 1;
    long v = u->val > 0 ? u->val : 1;
    /* u^k has absolute order k v + n; terms with k v >= v + n are beyond precision */
    long need = (v + u->n) / v + 1;
    if (need > 400) need = 400;
    return need;
}

static S *ser(E *e);

/* exp(a) = exp(s0) * sum_k s1^k / k!  where a = s0 + s1, s1 -> 0.
 * FAILs if a -> infinity (an essential singularity in h: exp(1/h) has no
 * power series; such cases are left to Gruntz / the limit calculus). */
static S *sr_exp(S *a) {
    E *s0;
    S *s1;
    if (!sr_split0(a, &s0, &s1)) FAIL("essential singularity");
    /* e^(s0) with s0 depending on L = ln h is not a constant coefficient: it
     * grows or decays faster than any power of h for e^(L ln(-L)), which
     * breaks the ordering of terms (a "small" term c h^4 with such a c need
     * not be small: gruntz instead) */
    if (!freeof(s0, g_L)) {
        /* e^(q L + b) = h^q e^b (q, b free of L) is kept as a coefficient
         * (lead_limit compares it with powers of h); anything else fails */
        long dg;
        if (!poly_degree_in(s0, g_L, &dg) || dg > 1 || !freeof(poly_coeff(s0, g_L, 1), g_L)) FAIL("exp of ln h");
    }
    long n = nterms(s1);
    E **c = salloc(sizeof(E *) * (size_t)n);
    E *f = K->c1;
    for (long k = 0; k < n; k++) {
        c[k] = s_inv(f);
        f = s_mul2(f, mk_int(k + 1));
    }
    S *r = sr_compose(c, n, s1);
    if (!is_zero(s0)) r = sr_scale(r, s_exp(s0));
    return r;
}

/* ln(a) with a = c0 h^(val/d) (1 + u), u -> 0:
 *   ln a = ln c0 + (val/d) ln h + sum_{k>=1} (-1)^(k+1) u^k / k,
 * where ln h is represented by the symbol g_L inside the constant term.
 * ln c0 is taken literally (complex for c0 < 0).  FAILs if a is zero to all
 * known orders. */
static S *sr_ln(S *a) {
    a = sr_trim(a);
    if (a->n == 0) FAIL("precision");
    E *c0 = a->c[0];
    /* a = c0 h^(val/d) (1 + u) */
    S *u = sr_scale(a, s_inv(c0));
    S *un = sr_new(u->val - a->val, u->d, u->n);
    for (long i = 0; i < u->n; i++) un->c[i] = u->c[i];
    un->c[0] = K->c0; /* subtract 1 */
    un = sr_trim(un);
    long n = nterms(un);
    E **c = salloc(sizeof(E *) * (size_t)n);
    c[0] = K->c0;
    for (long k = 1; k < n; k++) c[k] = mk_frac((k & 1) ? 1 : -1, k);
    S *r = sr_compose(c, n, un);
    E *lead = s_ln(c0);
    if (a->val) lead = s_add2(lead, s_mul2(mk_frac(a->val, a->d), g_L));
    return sr_add(r, sr_const(lead));
}

/* a^r for rational or symbolic constant r
 * Binomial series: with a = c0 h^q (1 + u),
 *   a^r = c0^r h^(q r) sum_k binomial(r, k) u^k,
 * binomial(r, k) computed by the recurrence b_{k+1} = b_k (r - k)/(k + 1).
 * The new leading exponent q r must be rational with denominator <= 1000
 * (the series denominator d is enlarged to a common multiple); a symbolic r
 * is allowed only when q = 0.  c0^r uses the principal branch.  FAILs on
 * zero series, symbolic or too complicated exponents. */
static S *sr_pow_gen(S *a, E *r) {
    a = sr_trim(a);
    if (a->n == 0) FAIL("precision");
    E *c0 = a->c[0];
    E *ev = mk_frac(a->val, a->d);
    E *ne = s_mul2(ev, r); /* new leading exponent */
    if (!is_exactnum(ne)) {
        if (a->val != 0) FAIL("symbolic exponent");
        ne = K->c0;
    }
    S *u = sr_scale(a, s_inv(c0));
    S *un = sr_new(0, u->d, u->n);
    for (long i = 0; i < u->n; i++) un->c[i] = u->c[i];
    un->c[0] = K->c0;
    un = sr_trim(un);
    long n = nterms(un);
    E **c = salloc(sizeof(E *) * (size_t)n);
    E *b = K->c1;
    for (long k = 0; k < n; k++) {
        c[k] = b;
        b = cz(s_div(s_mul2(b, s_sub(r, mk_int(k))), mk_int(k + 1)));
    }
    S *res = sr_compose(c, n, un);
    res = sr_scale(res, s_pow(c0, r));
    /* multiply by h^ne */
    E *nd = num_denom(ne), *nn = num_numer(ne);
    long dd, nv;
    if (!num_is_small_int(nd, &dd) || !num_is_small_int(nn, &nv) || dd > 1000) FAIL("exponent");
    long D = res->d / gcdl(res->d, dd) * dd;
    res = sr_redenom(res, D);
    res->val += nv * (D / dd);
    return res;
}

/* Taylor coefficients of F at s0 via symbolic derivatives
 * Generic rule for a unary function F = id(z) with no dedicated expansion:
 * F(s0 + s1) = sum_k F^(k)(s0)/k! s1^k, where the derivatives are taken
 * with diff() with respect to a placeholder symbol _sz and evaluated by
 * substitution.  FAILs if the argument tends to infinity, if F has more
 * than one argument (the `other` parameter is unused), or if some
 * derivative is singular at s0 (substitution throws or gives inf/undef),
 * e.g. sqrt or asin at a branch point.  Cost grows with the size of the
 * k-th derivative, which can swell for nested functions. */
static S *sr_taylor(int id, uint32_t nargs, E **other, S *a) {
    E *s0;
    S *s1;
    if (!sr_split0(a, &s0, &s1)) FAIL("argument tends to infinity");
    long n = nterms(s1);
    E *z = mk_sym_name("_sz");
    E *F;
    if (nargs == 1) F = s_fn1(id, z);
    else {
        (void)other;
        FAIL("multi-arg");
    }
    E **c = salloc(sizeof(E *) * (size_t)n);
    E *fact = K->c1;
    E *Fk = F;
    for (long k = 0; k < n; k++) {
        poll_intr();
        E *v = safe_subst(Fk, z, s0);
        if (!v) FAIL("singular");
        if (!freeof_sym(v, S_UNDEF) || !freeof_sym(v, S_INF)) FAIL("singular");
        c[k] = cz(s_div(v, fact));
        fact = s_mul2(fact, mk_int(k + 1));
        if (k + 1 < n) Fk = diff(Fk, z);
    }
    return sr_compose(c, n, s1);
}

/* sin(a) (want_sin = 1) or cos(a) (want_sin = 0), a = s0 + s1, s1 -> 0:
 *   sin(s0 + s1) = sin s0 cos s1 + cos s0 sin s1,
 *   cos(s0 + s1) = cos s0 cos s1 - sin s0 sin s1,
 * with sin s1, cos s1 from their Maclaurin series.  FAILs if a -> infinity
 * (oscillation: no series). */
static S *sr_sincos(S *a, int want_sin) {
    E *s0;
    S *s1;
    if (!sr_split0(a, &s0, &s1)) FAIL("trig at infinity");
    long n = nterms(s1);
    E **cs = salloc(sizeof(E *) * (size_t)n), **cc = salloc(sizeof(E *) * (size_t)n);
    E *f = K->c1;
    for (long k = 0; k < n; k++) {
        E *t = s_inv(f);
        /* sin: k odd, sign (-1)^((k-1)/2); cos: k even, (-1)^(k/2) */
        cs[k] = (k & 1) ? (((k / 2) & 1) ? s_neg(t) : t) : K->c0;
        cc[k] = (k & 1) ? K->c0 : (((k / 2) & 1) ? s_neg(t) : t);
        f = s_mul2(f, mk_int(k + 1));
    }
    S *sn = sr_compose(cs, n, s1), *cn = sr_compose(cc, n, s1);
    if (is_zero(s0)) return want_sin ? sn : cn;
    E *ss = s_fn1(F_SIN, s0), *cc0 = s_fn1(F_COS, s0);
    if (want_sin) return sr_add(sr_scale(cn, ss), sr_scale(sn, cc0));
    return sr_add(sr_scale(cn, cc0), sr_neg(sr_scale(sn, ss)));
}

/* Sign (+1/-1) of the leading coefficient of a, i.e. of a itself as
 * h -> 0+; 0 if a is zero to all known orders, if the coefficient involves
 * ln h, or if sign_of() cannot decide. */
static int lead_sign(S *a) {
    a = sr_trim(a);
    if (a->n == 0) return 0;
    E *c = a->c[0];
    if (!freeof(c, g_L)) return 0;
    return sign_of(c);
}

/*
 * ser -- series of the expression e in g_h as g_h -> 0+, at working
 * precision g_prec.  Recursive over the tree: sums and products combine
 * the children's series; powers, exponentials and functions are composed
 * using the sr_* rules above.  Returns NULL with g_fail set when some
 * subexpression has no generalised power series in h (essential
 * singularity, oscillation, discontinuity of unknown side, non-rational
 * exponent, multi-argument function ...).  May throw (cz swell,
 * interrupts, out of memory, stack depth).  Scratch allocated.
 */
static S *ser(E *e) {
    stack_check();
    if (g_fail) return NULL;
    if (freeof(e, g_h)) return sr_const(e);
    if (expr_eq(e, g_h)) {
        S *s = sr_new(1, 1, g_prec);
        if (g_prec > 0) s->c[0] = K->c1;
        return s;
    }
    switch (e->t) {
    case E_ADD: {
        S *r = ser(EK(e)[0]);
        for (uint32_t i = 1; i < e->n && r; i++) {
            S *t = ser(EK(e)[i]);
            if (!t) return NULL;
            r = sr_add(r, t);
        }
        return r;
    }
    case E_MUL: {
        S *r = ser(EK(e)[0]);
        for (uint32_t i = 1; i < e->n && r; i++) {
            S *t = ser(EK(e)[i]);
            if (!t) return NULL;
            r = sr_mul(r, t);
        }
        return r;
    }
    case E_POW: {
        /* exp(u) is stored as the power e^u */
        E *b = K0(e), *x = K1(e);
        if (ISYM(b, S_E)) {
            S *a = ser(x);
            return a ? sr_exp(a) : NULL;
        }
        if (!freeof(x, g_h)) {
            /* b^x = exp(x ln b) */
            S *a = ser(s_mul2(x, s_ln(b)));
            return a ? sr_exp(a) : NULL;
        }
        S *a = ser(b);
        if (!a) return NULL;
        long k;
        if (num_is_small_int(x, &k)) return sr_pow_int(a, k);
        return sr_pow_gen(a, x);
    }
    case E_FN: {
        if (e->n != 1) FAIL("function");
        E *u = K0(e);
        S *a = ser(u);
        if (!a) return NULL;
        switch (e->s) {
        case F_LN: return sr_ln(a);
        case F_SIN: return sr_sincos(a, 1);
        case F_COS: return sr_sincos(a, 0);
        case F_TAN: {
            S *s = sr_sincos(a, 1), *c = sr_sincos(a, 0);
            if (!s || !c) return NULL;
            S *ic = sr_inv(c);
            return ic ? sr_mul(s, ic) : NULL;
        }
        case F_COT: case F_SEC: case F_CSC: {
            S *s = sr_sincos(a, 1), *c = sr_sincos(a, 0);
            if (!s || !c) return NULL;
            if (e->s == F_SEC) return sr_inv(c);
            if (e->s == F_CSC) return sr_inv(s);
            S *is = sr_inv(s);
            return is ? sr_mul(c, is) : NULL;
        }
        case F_SINH: case F_COSH: case F_TANH: {
            S *p = sr_exp(a), *m = sr_exp(sr_neg(a));
            if (!p || !m) return NULL;
            S *sh = sr_scale(sr_add(p, sr_neg(m)), K->chalf), *ch = sr_scale(sr_add(p, m), K->chalf);
            if (e->s == F_SINH) return sh;
            if (e->s == F_COSH) return ch;
            S *ic = sr_inv(ch);
            return ic ? sr_mul(sh, ic) : NULL;
        }
        case F_ATAN:
            /* argument -> +-inf: use the reflection identity so the Taylor
             * rule is applied to the infinitesimal 1/s */
            if (a->val < 0) {
                /* atan(s) = sign*pi/2 - atan(1/s) */
                int sg = lead_sign(a);
                if (!sg) FAIL("sign");
                S *ia = sr_inv(a);
                if (!ia) return NULL;
                S *t = sr_taylor(F_ATAN, 1, NULL, ia);
                if (!t) return NULL;
                return sr_add(sr_const(s_mul2(mk_frac(sg, 2), K->pi_)), sr_neg(t));
            }
            return sr_taylor(F_ATAN, 1, NULL, a);
        /* abs and sign need the sign of the argument as h -> 0+ */
        case F_ABS: {
            int sg = lead_sign(a);
            if (!sg) FAIL("abs sign");
            return sg > 0 ? a : sr_neg(a);
        }
        case F_SIGN: {
            int sg = lead_sign(a);
            if (!sg) FAIL("sign");
            return sr_const(mk_int(sg));
        }
        case F_SI: case F_CI: case F_EI: {
            E *s0;
            S *s1;
            if (!sr_split0(a, &s0, &s1)) FAIL("argument tends to infinity");
            if (!is_zero(s0)) return sr_taylor(e->s, 1, NULL, a);
            /* expansions at 0: Si = sum (-1)^k u^(2k+1)/((2k+1)(2k+1)!),
             * Ci = gamma + ln u + sum (-1)^k u^(2k)/(2k (2k)!), Ei = gamma + ln u + sum u^k/(k k!)
             * (DLMF 6.6; gamma = Euler's constant).  Below, c[k] is the
             * coefficient of u^k, so for Si/Ci the sign is (-1)^(k/2).
             * Ci and Ei have a logarithmic singularity at 0, hence the
             * sr_ln(s1) term (which introduces ln h via g_L). */
            long n = nterms(s1);
            E **c = salloc(sizeof(E *) * (size_t)(n + 1));
            for (long k = 0; k < n; k++) {
                c[k] = K->c0;
                if (e->s == F_SI && (k & 1)) {
                    E *v = mk_rat(K->c1, int_mul(mk_int(k), int_fact((uint32_t)k)));
                    c[k] = ((k / 2) & 1) ? num_neg(v) : v;
                } else if (e->s == F_CI && k > 0 && !(k & 1)) {
                    E *v = mk_rat(K->c1, int_mul(mk_int(k), int_fact((uint32_t)k)));
                    c[k] = ((k / 2) & 1) ? num_neg(v) : v;
                } else if (e->s == F_EI && k > 0)
                    c[k] = mk_rat(K->c1, int_mul(mk_int(k), int_fact((uint32_t)k)));
            }
            S *r = sr_compose(c, n, s1);
            if (e->s == F_SI) return r;
            /* Ei(u) = gamma + ln|u| + ... for real u < 0 (DLMF 6.6.1; Ei is real there) */
            S *lg;
            if (e->s == F_EI) {
                int sg = lead_sign(s1);
                if (!sg) FAIL("Ei sign");
                lg = sr_ln(sg > 0 ? s1 : sr_neg(s1));
            } else
                lg = sr_ln(s1);
            if (!lg) return NULL;
            return sr_add(sr_add(r, lg), sr_const(mk_sym(S_EULERG)));
        }
        case F_FLOOR: case F_CEIL: case F_HEAVISIDE: case F_FRAC: {
            /* Piecewise-constant functions: locally constant unless the
             * argument tends to a jump point (an integer, or 0 for
             * heaviside), in which case the side is given by the sign of
             * the infinitesimal part s1.  s0 is located numerically
             * (to_double), so v == floor(v) is a floating-point test. */
            E *s0;
            S *s1;
            if (!sr_split0(a, &s0, &s1)) FAIL("discontinuous at infinity");
            double v;
            if (!to_double(s0, &v)) FAIL("discontinuous");
            int sg = lead_sign(s1);
            int atint = v == floor(v);
            if (e->s == F_HEAVISIDE) {
                if (v != 0) return sr_const(v > 0 ? K->c1 : K->c0);
                if (!sg) FAIL("heaviside");
                return sr_const(sg > 0 ? K->c1 : K->c0);
            }
            if (!atint) {
                E *fl = mk_int((int64_t)floor(v));
                if (e->s == F_FLOOR) return sr_const(fl);
                if (e->s == F_CEIL) return sr_const(int_add(fl, K->c1));
                return sraddc(a, s_neg(fl));
            }
            /* side unknown: sign of s1 undecidable, or s1 zero to all
             * known orders (h-free arguments never get here) */
            if (!sg) FAIL("discontinuous");
            E *iv = mk_int((int64_t)v);
            if (e->s == F_FLOOR) return sr_const(sg > 0 ? iv : int_sub(iv, K->c1));
            if (e->s == F_CEIL) return sr_const(sg > 0 ? int_add(iv, K->c1) : iv);
            /* frac */
            return sg > 0 ? s1 : sr_add(s1, sr_const(K->c1));
        }
        case F_ROUND: case F_MOD: FAIL("discontinuous");
        default:
            return sr_taylor(e->s, 1, NULL, a);
        }
    }
    default: FAIL("unsupported");
    }
}

/* compute series of e (in g_h) with retries; returns trimmed series with at
 * least `need` known terms if possible
 * Working precisions tried: need, need+4, 2 need+8, 3 need+20.  Accepts the
 * first result with at least max(1, need/2) known terms.  Returns NULL if
 * the expansion fails (g_fail) or never yields a nonzero term, except that
 * a result that is zero to all known orders after the last attempt is
 * reported as the sentinel series 1 * h^1000000 ("practically zero"), which
 * lead_limit turns into the limit 0.  Caller sets g_h; g_prec is
 * clobbered.  Earlier attempts' scratch is not released. */
static S *ser_retry(E *e, long need) {
    long precs[] = {need, need + 4, 2 * need + 8, 3 * need + 20};
    S *s = NULL;
    for (int i = 0; i < 4; i++) {
        g_prec = precs[i];
        g_fail = 0;
        s = ser(e);
        if (g_fail || !s) return NULL;
        s = sr_trim(s);
        if (s->n >= 1 && s->n >= (need < 2 ? 1 : need / 2)) return s;
    }
    if (s && s->n == 0) {
        /* identically zero to high order: report as a zero series */
        S *z = sr_new(1000000, 1, 1);
        z->c[0] = K->c1;
        return z;
    }
    return s && s->n > 0 ? s : NULL;
}

/* display form of the last series_expr result (see there); NULL when none */
E *g_sdisp;

/* Intern the internal symbols _h (series variable) and _lnh (ln h) and
 * store them in g_h / g_L. */
static void setup_syms(void) {
    g_h = mk_sym_name("_h");
    g_L = mk_sym_name("_lnh");
}

/* ------------------------------------------------------------ series --- */
/*
 * series_expr -- truncated series of e in x about a, up to (but excluding)
 * order n:  terms c (x-a)^q with q < n, or in powers of 1/x when a = +inf
 * (-1/x when a = -inf).  Exponents may be fractional (Puiseux) or negative
 * (Laurent); coefficients may contain ln(x-a) (resp. ln(1/x)) where the
 * expansion needs ln h.  The expansion is one-sided: x = a + h with h -> 0+.
 * The result is a plain sum (no O() term); its display form (the terms in
 * ascending order and the order term) is left in g_sdisp for print_series.
 * Error: throws CAS_ERR_UNSUPPORTED "series expansion failed" if the
 * series engine fails (e.g. essential singularity), or a cz() swell error.
 * Scratch allocated; up to 5 attempts, doubling g_prec each time, until the
 * absolute order reaches n (or the relative precision is generous);
 * scratch of an unsuccessful attempt is released before the next one.
 */
E *series_expr(E *e, E *x, E *a, long n) {
    setup_syms();
    int inf = 0;
    E *sub;
    if (ISYM(a, S_INF)) {
        inf = 1;
        sub = s_inv(g_h);
    } else if (is_negative_term(a) && !freeof_sym(a, S_INF)) {
        inf = -1;
        sub = s_neg(s_inv(g_h));
    } else
        sub = s_add2(a, g_h);
    E *f = subst(e, x, sub);
    g_prec = n + 1;
    S *s = NULL;
    for (int attempt = 0; attempt < 5; attempt++) {
        g_fail = 0;
        char *mark = smark();
        s = ser(f);
        if (g_fail || !s) cas_throw(CAS_ERR_UNSUPPORTED, "series expansion failed");
        s = sr_trim(s);
        /* enough terms to reach absolute order n? */
        if ((s->val + s->n) >= n * s->d || s->n >= 3 * (n + 1) || attempt == 4) break;
        srelease(mark); /* (never after the last attempt: s is used below) */
        g_prec = g_prec * 2 + 2;
    }
    /* build polynomial in (x - a) or 1/x; term i has exponent (val+i)/d and
     * ln h in the coefficients is substituted back as ln of that variable */
    E *var = inf == 0 ? s_sub(x, a) : (inf > 0 ? s_inv(x) : s_neg(s_inv(x)));
    E *lnv = inf == 0 ? s_ln(s_sub(x, a)) : s_ln(var);
    E **t = salloc(sizeof(E *) * (size_t)(s->n + 1));
    E **d = salloc(sizeof(E *) * (size_t)(2 * s->n + 2));
    uint32_t nt = 0;
    for (long i = 0; i < s->n; i++) {
        long num = s->val + i;
        /* keep exponents < n (in h units) */
        if (num >= n * s->d) break;
        E *c = s->c[i];
        if (is_zero(c)) continue;
        c = subst(c, g_L, lnv);
        E *pw = s_pow(var, mk_frac(num, s->d));
        d[2 + 2 * nt] = c; /* (coefficient, power) pairs for the display */
        d[3 + 2 * nt] = pw;
        t[nt++] = s_mul2(c, pw);
    }
    /* display form for a top-level series command (cas.c, print_series):
     * [O-term base^n, x, c0, p0, c1, p1, ...] in ascending order */
    d[0] = s_pow(inf ? s_inv(x) : var, mk_int(n));
    d[1] = x;
    g_sdisp = mk_list(2 + 2 * nt, d);
    return s_add(nt, t);
}

/* ------------------------------------------------------------- limits --- */
/* -inf is represented canonically as the product (-1)*inf. */
static int is_minf(E *e) { return e->t == E_MUL && e->n == 2 && num_is_m1(K0(e)) && ISYM(K1(e), S_INF); }
static int is_pinf(E *e) { return ISYM(e, S_INF); }
/* 1 if e contains an unsimplified power 0^u (e.g. 0^(-1) left by
 * substitution), which signals a singular value. */
static int has_zero_pow(E *e) {
    stack_check();
    if (e->t == E_POW && is_zero(K0(e))) return 1;
    if (e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_zero_pow(EK(e)[i])) return 1;
    return 0;
}

/* 1 if e is a usable finite value: no inf, undef, unevaluated limit_ or
 * 0^u inside. */
static int is_finite_val(E *e) {
    return freeof_sym(e, S_INF) && freeof_sym(e, S_UNDEF) && freeof_sym(e, F_LIMIT_U) && !has_zero_pow(e);
}

/* limit of leading term c h^v as h->0+
 * s is a trimmed series; its leading term decides the limit:
 *   v > 0 -> 0, v = 0 -> c, v < 0 -> +-inf by the sign of c
 * (complex c: undef, i.e. complex infinity; unknown sign: NULL).
 * When c contains L = ln h (-> -inf):
 *   - c polynomial in L of degree k: c ~ lc L^k, so for v <= 0 the limit
 *     is +-inf with sign sign(lc) (-1)^k; for v > 0 powers of h win (0);
 *   - otherwise: for v = 0 the limit of c as L -> -inf (recursive
 *     limit_expr); for v != 0 the limit of ln|c| + v L decides between 0,
 *     +-inf and a finite exp(q), with the sign of c sampled numerically at
 *     L = -1000.
 * Returns NULL when undecided. */
static E *lead_limit(S *s) {
    if (!s || s->n == 0) return NULL;
    E *c = s->c[0];
    long v = s->val;
    if (!freeof(c, g_L)) {
        /* coefficient depends on L = ln h -> -inf */
        long deg;
        int sg;
        if (poly_degree_in(c, g_L, &deg)) {
            if (v > 0) return K->c0; /* polynomials in L are negligible against h^v */
            /* a non-real coefficient (ln(-h) = i pi + L): |value| -> inf with a
             * non-real part, i.e. complex infinity, not +-inf */
            for (long k = 0; k <= deg; k++) {
                double re, im;
                E *ck = poly_coeff(c, g_L, k);
                if (eval_complex(ck, &re, &im) && fabs(im) > 1e-12 * (1 + fabs(re))) return K->undef_;
            }
            E *lc = poly_coeff(c, g_L, deg);
            int s0 = sign_of(lc);
            if (!s0) return NULL;
            sg = s0 * ((deg & 1) ? -1 : 1);
        } else {
            if (v == 0) return limit_expr(c, g_L, s_neg(K->inf_), 0);
            /* compare |c| with h^-v: limit of ln|c| + v L as L -> -inf */
            double cv;
            E *Lp = mk_flt(-1000.0);
            E *cn = evalf(subst(c, g_L, Lp));
            if (!to_double(cn, &cv) || cv == 0) return NULL;
            sg = cv > 0 ? 1 : -1;
            extern E *ln_expand(E *e);
            E *lc = ln_expand(s_ln(sg > 0 ? c : s_neg(c)));
            E *q = limit_expr(s_add2(lc, s_mul2(mk_frac(v, s->d), g_L)), g_L, s_neg(K->inf_), 0);
            if (!q) return NULL;
            if (is_minf(q)) return K->c0;
            if (is_pinf(q)) return sg > 0 ? K->inf_ : s_neg(K->inf_);
            if (!is_finite_val(q)) return NULL;
            return s_mul2(mk_int(sg), s_exp(q));
        }
        if (v > 0) return K->c0;
        return sg > 0 ? K->inf_ : s_neg(K->inf_);
    }
    if (v > 0) return K->c0;
    if (v == 0) return c;
    int sg = sign_of(c);
    if (!sg) {
        double re, im;
        if (eval_complex(c, &re, &im)) return K->undef_; /* complex infinity */
        return NULL;
    }
    return sg > 0 ? K->inf_ : s_neg(K->inf_);
}

static E *lim_rec(E *e, E *h, int depth);

/* Limit by continuity: substitute h = 0 and accept the result if it is a
 * finite value.  NULL for discontinuous expressions, when substitution
 * throws (e.g. division by zero; only interrupts are rethrown) or when the
 * value contains inf/undef. */
/* sing_at0 -- 1 if a function application in e is singular at h = 0 (its
 * value there is numeric but not finite, e.g. Ci(0)), which a direct
 * substitution could cancel away: Ci(3h) - Ci(h) -> Ci(0) - Ci(0) = 0 */
static int sing_at0(E *e, E *h) {
    stack_check();
    if (freeof(e, h) || e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (sing_at0(EK(e)[i], h)) return 1;
    if (e->t == E_FN) {
        E *v = subst(e, h, K->c0);
        double re, im;
        if (!freeof_sym(v, S_INF) || !freeof_sym(v, S_UNDEF)) return 1;
        if (is_const_expr(v) && (!eval_complex(v, &re, &im) || !isfinite(re) || !isfinite(im))) return 1;
    }
    return 0;
}

/* zero_den_at0 -- 1 if a negative power u^(-k) in e has a base u that vanishes
 * at h = 0 without the automatic simplifier seeing it (1 - sin(a)^2 - cos(a)^2,
 * (sqrt(5)/2 - 1/2)^2 + sqrt(5)/2 - 3/2): substitution would turn 0/0 into 0 */
static int zero_den_at0(E *e, E *h) {
    stack_check();
    if (freeof(e, h) || e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR) return 0;
    if (e->t == E_POW && is_num(K1(e)) && num_sgn(K1(e)) < 0 && !freeof(K0(e), h) && expr_size(K0(e)) < 200) {
        E *b0 = subst(K0(e), h, K->c0);
        if (is_zero(b0) || sym_zero(b0) || (has_fn(b0) && is_zero(rat_normal(b0)))) return 1;
    }
    for (uint32_t i = 0; i < e->n; i++)
        if (zero_den_at0(EK(e)[i], h)) return 1;
    return 0;
}

static E *try_direct(E *e, E *h) {
    if (has_discont(e)) return NULL;
    tryf tf;
    try_enter(&tf);
    E *volatile r = NULL;
    if (!setjmp(tf.jb)) {
        /* a denominator that vanishes at h = 0 without rat_normal seeing it
         * (1 - sin(a)^2 - cos(a)^2) would turn 0/0 into 0 */
        if (zero_den_at0(e, h) || sing_at0(e, h)) {
            try_leave(&tf);
            return NULL;
        }
        r = subst(e, h, K->c0);
        try_leave(&tf);
    } else {
        try_leave(&tf);
        if (K->err == CAS_ERR_INTERRUPTED) rethrow();
        return NULL;
    }
    if (!is_finite_val(r)) return NULL;
    return r;
}

static E *try_direct(E *e, E *h);

#ifdef LIMDEBUG
#include <stdio.h>
/* Debug trace (host builds with -DLIMDEBUG only): prints e indented by
 * recursion depth to stderr. */
static void dbg(const char *tag, E *e, int depth) {
    static char buf[2048];
    sbuf b = {buf, sizeof buf, 0, 0};
    print_expr(&b, e);
    fprintf(stderr, "%*s%s %s\n", depth * 2, "", tag, buf);
}
#else
#define dbg(t, e, d) ((void)0)
#endif

/*
 * lim0 -- limit of e as h -> 0+ (h appears freely); the workhorse of the
 * heuristic limit calculus.  Order: continuity (try_direct), leading term
 * of a short series (ser_retry with 3 terms), then the structural rules of
 * lim_rec.  Returns a value, +inf, -inf (as (-1)*inf), undef, the marker
 * g_bounded (bounded, no limit), or NULL (undecided).  Gives up past depth
 * 24 or when the global g_lim_budget is exhausted; may throw (callers in
 * lim_side catch).  g_h is saved and restored around the series call.
 */
static E *lim0(E *e, E *h, int depth) {
    stack_check();
    dbg("lim0", e, depth);
    if (depth > 24 || --g_lim_budget < 0) return NULL;
    if (!has_discont(e)) {
        E *d = try_direct(e, h);
        if (d) return d;
    }
    E *saved_h = g_h;
    g_h = h;
    S *s = ser_retry(e, 3);
    g_h = saved_h;
    if (s) {
        E *r = lead_limit(s);
        if (r) return r;
    }
    return lim_rec(e, h, depth + 1);
}


/* Classify a limit result for the combination rules of lim_rec. */
static int classify(E *l) {
    /* 0 finite nonzero/unknown, 1 +inf, -1 -inf, 2 zero, 3 undef, 4 bounded */
    if (!l) return 3;
    if (l == g_bounded) return 4;
    if (is_pinf(l)) return 1;
    if (is_minf(l)) return -1;
    if (ISYM(l, S_UNDEF)) return 3;
    if (is_zero(l)) return 2;
    return 0;
}

/* l'Hopital: valid only while num/den is 0/0 or inf/inf, and only if the
 * limit of the derivative quotient exists (bounded/oscillating results are
 * inconclusive).
 * Up to 6 rounds; each round re-checks the indeterminate form, replaces
 * num/den by their derivatives, normalises the quotient and tries lim0 on
 * it.  Gives up (NULL) if the form is no longer indeterminate, if the
 * derivatives grow beyond 3000 nodes, or after 6 rounds. */
static E *lhopital(E *num, E *den, E *h, int depth) {
    for (int it = 0; it < 6; it++) {
        poll_intr();
        int cn = classify(lim0(num, h, depth + 1)), cd = classify(lim0(den, h, depth + 1));
        int indet = (cn == 2 && cd == 2) || ((cn == 1 || cn == -1) && (cd == 1 || cd == -1));
        if (!indet) return NULL;
        num = diff(num, h);
        den = diff(den, h);
        if (expr_size(num) + expr_size(den) > 3000) return NULL;
        E *q = rat_normal(s_div(num, den));
        E *r = lim0(q, h, depth + 1);
        if (r && r != g_bounded) return r;
        if (r == g_bounded) return NULL;
    }
    return NULL;
}

/* ln(u) with u -> inf a sum: ln(u) = ln(D) + ln(u/D) for a dominant term D
 * Walks e and rewrites the first such ln it finds (a term D whose ratio
 * u/D tends to a finite positive limit); ln(D) is expanded with ln_expand
 * so e.g. ln(e^x + x) becomes x + ln(1 + x e^(-x)) and the infinities in
 * a surrounding sum can cancel symbolically.  Sets *changed when a rewrite
 * happened; returns e (shared) otherwise. */
static E *ln_dom_rewrite(E *e, E *h, int depth, int *changed) {
    stack_check();
    if (freeof(e, h) || e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR) return e;
    if (IFN(e, F_LN) && e->n == 1 && K0(e)->t == E_ADD && depth < 8) {
        E *u = K0(e);
        if (classify(lim0(u, h, depth + 1)) == 1)
            for (uint32_t i = 0; i < u->n; i++) {
                E *D = EK(u)[i];
                if (freeof(D, h)) continue;
                E **o = salloc(sizeof(E *) * u->n);
                uint32_t no = 0;
                for (uint32_t j = 0; j < u->n; j++)
                    if (j != i) o[no++] = s_div(EK(u)[j], D);
                E *ratio = s_add2(K->c1, s_add(no, o));
                E *lr = lim0(ratio, h, depth + 1);
                if (classify(lr) != 0 || sign_of(lr) <= 0) continue;
                extern E *ln_expand(E *e);
                *changed = 1;
                return s_add2(ln_expand(s_ln(D)), s_ln(ratio));
            }
    }
    E **k = salloc(sizeof(E *) * (e->n + 1));
    int ch = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        k[i] = ln_dom_rewrite(EK(e)[i], h, depth, changed);
        if (k[i] != EK(e)[i]) ch = 1;
    }
    return ch ? rebuild(e, k) : e;
}

/* recursive limit calculus at h -> 0+
 * Structural rules by node type (called by lim0 after continuity and series
 * have failed):
 *   E_ADD: sum of the children's limits unless they are +inf and -inf
 *          together or undecided; then ln-dominant rewriting, factoring
 *          out an infinite term T (e = T (1 + rest/T)), and finally a
 *          common denominator with l'Hopital.
 *   E_MUL: product of limits with sign bookkeeping; bounded * 0 = 0
 *          (squeeze); for 0 * inf: expansion, division by the dominant
 *          term of a growing denominator sum, exp(sum of logs) for
 *          positive factors, and l'Hopital on zero_part / (1/inf_part).
 *   E_POW: e^u by the limit of u; u^c, c^u for constant c, and the general
 *          u^v as exp(v ln u).
 *   E_FN:  continuous functions of the limit, plus known values at +-inf
 *          (ln, atan, tanh, erf, Si, Ci, Ei, ...); sin/cos of an infinite
 *          or bounded argument give the bounded marker.
 * Same return convention as lim0. */
static E *lim_rec(E *e, E *h, int depth) {
    stack_check();
    if (depth > 24) return NULL;
    if (freeof(e, h)) return e;
    if (expr_eq(e, h)) return K->c0;
    E *d = try_direct(e, h);
    if (d) return d;
    switch (e->t) {
    case E_ADD: {
        E **v = salloc(sizeof(E *) * e->n);
        int pinf = 0, minf = 0, bad = 0, bnd = 0;
        for (uint32_t i = 0; i < e->n; i++) {
            v[i] = lim0(EK(e)[i], h, depth + 1);
            int c = classify(v[i]);
            if (c == 3) bad = 1;
            if (c == 1) pinf = 1;
            if (c == -1) minf = 1;
            if (c == 4) bnd = 1;
        }
        /* no conflict: inf wins over finite and bounded terms */
        if (!bad && !(pinf && minf)) {
            if (pinf) return K->inf_;
            if (minf) return s_neg(K->inf_);
            if (bnd) return g_bounded;
            return s_add(e->n, v);
        }
        {
            int chg = 0;
            E *rw = ln_dom_rewrite(e, h, depth, &chg);
            if (chg) {
                E *r = lim0(expand(rw), h, depth + 1);
                if (r) return r;
            }
        }
        /* inf - inf: factor out one of the infinite terms, e = T (1 + rest/T) */
        if (pinf && minf && depth < 8) {
            for (uint32_t i = 0; i < e->n; i++) {
                int c = classify(v[i]);
                if (c != 1 && c != -1) continue;
                E *T = EK(e)[i];
                E **o = salloc(sizeof(E *) * e->n);
                uint32_t no = 0;
                for (uint32_t j = 0; j < e->n; j++)
                    if (j != i) o[no++] = s_div(EK(e)[j], T);
                E *inner = s_add2(K->c1, rat_normal(s_add(no, o)));
                E *li = lim0(inner, h, depth + 1);
                int ci = classify(li);
                if (ci == 3 || ci == 4) continue;
                if (ci != 2) {
                    /* T -> inf times a nonzero limit */
                    if (ci == 1 || ci == -1) return c * ci > 0 ? K->inf_ : s_neg(K->inf_);
                    int sg = sign_of(li);
                    if (!sg) continue;
                    return c * sg > 0 ? K->inf_ : s_neg(K->inf_);
                }
                E *r = lim0(s_mul2(T, inner), h, depth + 1);
                if (r && r != g_bounded) return r;
            }
        }
        /* inf - inf: combine into one fraction and use l'Hopital / series */
        E *num, *den;
        numden(e, &num, &den);
        if (!is_one(den)) {
            E *ln = lim0(num, h, depth + 1), *ld = lim0(den, h, depth + 1);
            int cn = classify(ln), cd = classify(ld);
            if (cn != 3 && cd != 3 && !((cn == 2 && cd == 2) || ((cn == 1 || cn == -1) && (cd == 1 || cd == -1)))) {
                if (cd == 2) return NULL;
                return lim_rec(s_div(num, den), h, depth + 1);
            }
            if ((cn == 2 && cd == 2) || ((cn == 1 || cn == -1) && (cd == 1 || cd == -1))) return lhopital(num, den, h, depth);
        }
        return NULL;
    }
    case E_MUL: {
        E **v = salloc(sizeof(E *) * e->n);
        int zero = 0, inf = 0, sign = 1, bad = 0, bnd = 0;
        for (uint32_t i = 0; i < e->n; i++) {
            v[i] = lim0(EK(e)[i], h, depth + 1);
            int c = classify(v[i]);
            if (c == 3) bad = 1;
            if (c == 2) zero = 1;
            if (c == 4) bnd = 1;
            if (c == 1 || c == -1) {
                inf = 1;
                sign *= c;
            }
        }
        if (bad) return NULL;
        /* a bounded oscillating factor: times 0 is 0 (squeeze), times a
         * finite value stays bounded, times inf is undecided (try the
         * expanded form) */
        if (bnd) {
            if (inf) {
                E *ex = expand(e);
                return expr_eq(ex, e) ? NULL : lim0(ex, h, depth + 1);
            }
            return zero ? K->c0 : g_bounded; /* squeeze */
        }
        if (!(zero && inf)) {
            if (zero) return K->c0;
            if (inf) {
                /* sign of finite factors */
                for (uint32_t i = 0; i < e->n; i++) {
                    int c = classify(v[i]);
                    if (c == 0) {
                        int sg = sign_of(v[i]);
                        if (!sg) return NULL;
                        if (sg < 0) sign = -sign;
                    }
                }
                return sign > 0 ? K->inf_ : s_neg(K->inf_);
            }
            return s_mul(e->n, v);
        }
        {
            int chg = 0;
            E *rw = ln_dom_rewrite(e, h, depth, &chg);
            if (chg) {
                E *r = lim0(expand(rw), h, depth + 1);
                if (r) return r;
            }
            /* distributing often separates the terms: h*(a/h + b) = a + h b */
            E *ex = expand(e);
            if (!expr_eq(ex, e) && ex->t == E_ADD) {
                E *r = lim0(ex, h, depth + 1);
                if (r && r != g_bounded) return r;
            }
        }
        /* quotient by a growing sum S: divide numerator and S by S's dominant term
         * e = rest * S^kk (kk < 0); for a term T of S with S/T -> lr finite
         * and nonzero, e = (rest T^kk) / (S/T)^(-kk), so the limit is
         * lim(rest T^kk) / lr^(-kk). */
        for (uint32_t i = 0; i < e->n && depth < 16; i++) {
            E *f = EK(e)[i];
            long kk;
            if (!(f->t == E_POW && K0(f)->t == E_ADD && num_is_small_int(K1(f), &kk) && kk < 0)) continue;
            E *S = K0(f);
            if (classify(lim0(S, h, depth + 1)) != 1) continue;
            for (uint32_t j = 0; j < S->n; j++) {
                char *m_ = smark();
                E *T = EK(S)[j];
                if (freeof(T, h)) continue;
                E *ratio = expand(s_div(S, T));
                E *lr = lim0(ratio, h, depth + 1);
                if (classify(lr) != 0 || is_zero(lr)) {
                    srelease(m_);
                    continue;
                }
                E **rest = salloc(sizeof(E *) * e->n);
                uint32_t nr = 0;
                for (uint32_t q = 0; q < e->n; q++)
                    if (q != i) rest[nr++] = EK(e)[q];
                E *num = expand(s_mul2(s_mul(nr, rest), s_pow(T, mk_int(kk))));
                E *ln_ = lim0(num, h, depth + 1);
                int cn = classify(ln_);
                if (cn == 3 || cn == 4) {
                    srelease(m_);
                    continue;
                }
                E *den = s_pow(lr, mk_int(-kk));
                if (cn == 1 || cn == -1) {
                    int sg = sign_of(den);
                    if (!sg) {
                        srelease(m_);
                        continue;
                    }
                    return cn * sg > 0 ? K->inf_ : s_neg(K->inf_);
                }
                return s_div(ln_, den);
            }
        }
        /* 0 * inf: try exp(sum of logs) when all factors are positive near 0+ */
        {
            /* constant factors aside, the rest must be positive near 0+
             * (checked only numerically at h = 1e-7); then
             * e = cst * exp(sum ln f_i) with ln_expand splitting powers */
            E **cf = salloc(sizeof(E *) * e->n), **vf = salloc(sizeof(E *) * e->n);
            uint32_t ncf = 0, nvf = 0;
            int allpos = 1;
            for (uint32_t i = 0; i < e->n; i++) {
                if (freeof(EK(e)[i], h)) {
                    cf[ncf++] = EK(e)[i];
                    continue;
                }
                vf[nvf++] = EK(e)[i];
                double fv;
                if (!eval_double_at(EK(e)[i], h->s, 1e-7, &fv) || fv < 0) allpos = 0;
            }
            if (allpos && nvf) {
                E **lg = salloc(sizeof(E *) * nvf);
                extern E *ln_expand(E *e);
                for (uint32_t i = 0; i < nvf; i++) lg[i] = ln_expand(s_ln(vf[i]));
                E *ls = lim0(s_add(nvf, lg), h, depth + 1);
                int c = classify(ls);
                E *cst = s_mul(ncf, cf);
                if (c == -1) return K->c0;
                if (c == 1) {
                    int sg = sign_of(cst);
                    if (sg) return sg > 0 ? K->inf_ : s_neg(K->inf_);
                } else if (c == 0 || c == 2)
                    return s_mul2(cst, s_exp(ls));
            }
        }
        /* 0 * inf: write as quotient zero_part / (1/inf_part) */
        E **zp = salloc(sizeof(E *) * e->n), **ip = salloc(sizeof(E *) * e->n);
        uint32_t nz = 0, ni = 0;
        for (uint32_t i = 0; i < e->n; i++) {
            int c = classify(v[i]);
            if (c == 1 || c == -1) ip[ni++] = EK(e)[i];
            else zp[nz++] = EK(e)[i];
        }
        E *num = s_mul(nz, zp), *den = s_inv(s_mul(ni, ip));
        char *mlh = smark();
        E *lh = lhopital(num, den, h, depth);
        if (lh) return lh;
        srelease(mlh);
        E *ex = expand(e);
        if (!expr_eq(ex, e)) return lim0(ex, h, depth + 1);
        return NULL;
    }
    case E_POW: {
        /* b^x: e^x by the limit of x; constant exponent; constant positive
         * base; otherwise exp(x ln b) */
        E *b = K0(e), *x = K1(e);
        if (ISYM(b, S_E)) {
            E *l = lim0(x, h, depth + 1);
            int c = classify(l);
            if (c == 1) return K->inf_;
            if (c == -1) return K->c0;
            if (c == 3 || c == 4) return NULL;
            return s_exp(l);
        }
        if (freeof(x, h)) {
            E *l = lim0(b, h, depth + 1);
            int c = classify(l);
            if (c == 3 || c == 4) return NULL;
            double xv;
            if (!to_double(x, &xv)) {
                int sx = sign_of(x);
                if (!sx) return NULL;
                xv = sx;
            }
            if (c == 1) return xv > 0 ? K->inf_ : K->c0;
            if (c == -1) {
                long kx;
                if (!num_is_small_int(x, &kx)) return NULL;
                if (kx < 0) return K->c0;
                return (kx & 1) ? s_neg(K->inf_) : K->inf_;
            }
            /* base -> 0: 0^positive = 0; for a negative exponent the side
             * of 0 is unknown here */
            if (c == 2) {
                if (xv > 0) return K->c0;
                /* 0^-k: sign of base */
                return NULL;
            }
            return s_pow(l, x);
        }
        if (freeof(b, h) && sign_of(b) > 0) {
            /* constant positive base: b^(lim x) */
            E *l = lim0(x, h, depth + 1);
            int c = classify(l);
            if (c == 3 || c == 4) return NULL;
            if (c == 1 || c == -1) {
                double bv;
                if (!to_double(b, &bv)) return NULL;
                if (bv == 1) return K->c1;
                return (bv > 1) == (c == 1) ? K->inf_ : K->c0;
            }
            return s_pow(b, l);
        }
        return lim_rec(s_exp(s_mul2(x, s_ln(b))), h, depth + 1);
    }
    case E_FN: {
        if (e->n != 1) return NULL;
        E *l = lim0(K0(e), h, depth + 1);
        int c = classify(l);
        /* |complex infinity| = +inf (abs(ln(-h)), ln(-h) = i pi + ln h) */
        if (c == 3 && l && e->s == F_ABS) return K->inf_;
        if (c == 3) return NULL;
        /* only these functions accept a bounded oscillating argument; the
         * result is again bounded (and oscillating) */
        if (c == 4 && e->s != F_SIN && e->s != F_COS && e->s != F_ATAN && e->s != F_TANH) return NULL;
        if (c == 4) return g_bounded;
        switch (e->s) {
        case F_LN:
            if (c == 1) {
                /* ln(D * (1 + rest/D)) = ln D + ln(...) when the sum has a dominant term D */
                E *u = K0(e);
                if (u->t == E_ADD && depth < 8)
                    for (uint32_t i = 0; i < u->n; i++) {
                        E *D = EK(u)[i];
                        E **o = salloc(sizeof(E *) * u->n);
                        uint32_t no = 0;
                        for (uint32_t j = 0; j < u->n; j++)
                            if (j != i) o[no++] = s_div(EK(u)[j], D);
                        E *ratio = s_add2(K->c1, s_add(no, o));
                        E *lr = lim0(ratio, h, depth + 1);
                        int cr = classify(lr);
                        if (cr != 0 || sign_of(lr) <= 0) continue;
                        extern E *ln_expand(E *e);
                        return lim0(s_add2(ln_expand(s_ln(D)), s_ln(ratio)), h, depth + 1);
                    }
                return K->inf_;
            }
            if (c == 2) return s_neg(K->inf_);
            break;
        case F_ATAN:
            if (c == 1) return s_frac_pi(K->chalf);
            if (c == -1) return s_frac_pi(mk_frac(-1, 2));
            break;
        case F_SINH: case F_ASINH:
            if (c == 1 || c == -1) return l;
            break;
        case F_COSH:
            if (c == 1 || c == -1) return K->inf_;
            break;
        case F_TANH:
            if (c == 1) return K->c1;
            if (c == -1) return K->cm1;
            break;
        case F_SQRT:
            if (c == 1) return K->inf_;
            break;
        case F_ERF:
            if (c == 1) return K->c1;
            if (c == -1) return K->cm1;
            break;
        case F_SI:
            if (c == 1) return s_frac_pi(K->chalf);
            if (c == -1) return s_frac_pi(mk_frac(-1, 2));
            break;
        case F_CI:
            if (c == 1) return K->c0;
            break;
        case F_EI:
            if (c == 1) return K->inf_;
            if (c == -1) return K->c0;
            if (c == 2) return s_neg(K->inf_);
            break;
        case F_ERFC:
            if (c == 1) return K->c0;
            if (c == -1) return K->c2;
            break;
        case F_SIN: case F_COS:
            if (c == 1 || c == -1 || c == 4) return g_bounded; /* oscillates, bounded */
            break;
        case F_ABS:
            if (c == 1 || c == -1) return K->inf_;
            break;
        }
        /* other functions: no known value at infinity; finite limits use
         * continuity of f at l */
        if (c == 1 || c == -1) return NULL;
        return s_fn1(e->s, l);
    }
    default: return NULL;
    }
}


/* ===================================================================
 * Gruntz algorithm for limits x -> +inf of exp-log expressions.
 *   mrv(e): set of most rapidly varying exponential-like subexpressions
 *   (e^u, b^u with variable exponent, or x itself); if x is among them the
 *   expression is moved up one level (x -> e^x).  One element is chosen as
 *   w = exp(-u') -> 0, every element is rewritten as A * w^(-c) with A of a
 *   lower class, and the leading term of the series in w decides the limit
 *   (recursing on the coefficient, which has a lower comparability class).
 *
 * Reference: D. Gruntz, "On Computing Limits in a Symbolic Manipulation
 * System", PhD thesis, ETH Zurich, 1996 (the MRV limit algorithm).
 * Key notions:
 *   - comparability: f, g -> inf are in the same class iff
 *     ln|f| / ln|g| tends to a finite nonzero limit; f is "faster" if the
 *     ratio tends to inf.  E.g. x < e^x < e^(x^2) < e^(e^x), but e^x and
 *     e^(2x) are in the same class.
 *   - mrv(e) is the set of subexpressions of e of the highest class; the
 *     algorithm only needs exponentials (and x) because every exp-log
 *     function is built from them.
 *   - rewriting: if w = e^(-u) and f = e^(lf) are in the same class,
 *     c = lim lf/u is a finite constant and f = e^(lf - c u) * w^(-c),
 *     where e^(lf - c u) belongs to a lower class.  After rewriting, e is
 *     a function of w (-> 0+) with coefficients of lower class, so its
 *     series in w exists and its leading coefficient's limit is a smaller
 *     problem.
 * Termination is guarded by the depth cap 14 and the call budget
 * g_gr_budget (2000 per outermost lim_side); exhausting either gives NULL.
 * The mrv set is a fixed-size array of at most 16 elements (failure if
 * more are needed).
 * =================================================================== */
static int g_gr_budget;
static E *gr_lim(E *e, E *x, int depth);
static E *try_direct(E *e, E *h);

/* A small set of mrv candidates (no duplicates under expr_eq). */
typedef struct {
    E *v[16];
    uint32_t n;
} mset;

/* is f exponential-like?  returns its logarithm
 * x itself (log ln x), e^u (log u) and b^u with u depending on x
 * (log u ln b) qualify; returns 1 and sets *lg, else 0. */
static int gr_log(E *f, E *x, E **lg) {
    if (expr_eq(f, x)) {
        *lg = s_ln(x);
        return 1;
    }
    if (f->t == E_POW && !freeof(K1(f), x)) {
        if (ISYM(K0(f), S_E)) *lg = K1(f);
        else *lg = s_mul2(K1(f), s_ln(K0(f)));
        return 1;
    }
    return 0;
}

/* l is +inf or -inf (non-NULL). */
static int is_inf_val(E *l) { return l && (is_pinf(l) || is_minf(l)); }

/* limit x -> +inf by direct evaluation, series in 1/x and the limit
 * calculus only (no Gruntz): used for comparisons against x itself
 * (comparing with x through gr_lim would ask for mrv sets containing x
 * again and recurse without progress).  Uses the internal symbol _hb;
 * NULL if undecided or bounded-oscillating. */
static E *lim_inf_basic(E *e, E *x) {
    E *h = mk_sym_name("_hb");
    E *f = subst(e, x, s_inv(h));
    E *r = try_direct(f, h);
    if (r) return r;
    E *saved = g_h;
    g_h = h;
    S *sr = ser_retry(f, 3);
    g_h = saved;
    if (sr) r = lead_limit(sr);
    if (r) return r;
    r = lim0(f, h, 4);
    return r == g_bounded ? NULL : r;
}

/* +1: f faster, -1: g faster, 0: same class, 2: failure
 * Compares the comparability classes of two exponential-like f, g via
 * lim ln f / ln g (inf: f faster, 0: g faster, finite nonzero: same
 * class).  Comparisons involving x itself use lim_inf_basic. */
static int gr_compare(E *f, E *g, E *x, int depth) {
    E *lf, *lg;
    if (!gr_log(f, x, &lf) || !gr_log(g, x, &lg)) return 2;
    E *ratio = rat_normal(s_div(lf, lg));
    E *r = (expr_eq(f, x) || expr_eq(g, x)) ? lim_inf_basic(ratio, x) : gr_lim(ratio, x, depth + 1);
    if (!r) return 2;
    if (is_inf_val(r)) return 1;
    if (is_zero(r)) return -1;
    if (!is_finite_val(r)) return 2;
    return 0;
}

/* 1 if f (structurally) belongs to s. */
static int mset_has(mset *s, E *f) {
    for (uint32_t i = 0; i < s->n; i++)
        if (expr_eq(s->v[i], f)) return 1;
    return 0;
}

/*
 * gr_max -- a := max(a, b) in the sense of Gruntz's mrv sets: the set of
 * the faster class, or the union if both are in the same class.  If the
 * sets share an element they are in the same class and are merged without
 * a comparison; otherwise representatives v[0] are compared.  Returns 0 on
 * failure (comparison undecided, or more than 16 elements).
 */
static int gr_max(mset *a, mset *b, E *x, int depth) {
    /* result in a */
    if (!b->n) return 1;
    if (!a->n) {
        *a = *b;
        return 1;
    }
    for (uint32_t i = 0; i < b->n; i++)
        if (mset_has(a, b->v[i])) goto merge;
    {
        int c = gr_compare(a->v[0], b->v[0], x, depth);
        if (c == 2) return 0;
        if (c == 1) return 1;
        if (c == -1) {
            *a = *b;
            return 1;
        }
    }
merge:
    for (uint32_t i = 0; i < b->n; i++)
        if (!mset_has(a, b->v[i])) {
            if (a->n >= 16) return 0;
            a->v[a->n++] = b->v[i];
        }
    return 1;
}

/*
 * gr_mrv -- compute mrv(e) (most rapidly varying subexpressions w.r.t.
 * x -> +inf) into *out; returns 0 if it cannot be determined.
 *   - free of x: empty set;  x: {x};
 *   - exponential-like f = e^u: if u -> +-inf then max({f}, mrv(u)),
 *     otherwise f is not "large" and mrv(f) = mrv(u);
 *   - sums/products: max over the children;  u^c (constant c): mrv(u);
 *   - ln(u): mrv(u);
 *   - other unary functions: allowed if the argument has a finite limit
 *     (continuity), or for the few functions with a known limit at
 *     infinity (atan, tanh, erf, Si, acot, abs, sqrt); then mrv(arg).
 * Anything else (multi-argument functions, lists, ...) fails.
 */
static int gr_mrv(E *e, E *x, mset *out, int depth) {
    stack_check();
    out->n = 0;
    if (freeof(e, x)) return 1;
    if (expr_eq(e, x)) {
        out->v[out->n++] = x;
        return 1;
    }
    if (--g_gr_budget < 0) return 0;
    E *lg;
    if (gr_log(e, x, &lg)) {
        mset inner;
        if (!gr_mrv(lg, x, &inner, depth)) return 0;
        E *l = gr_lim(lg, x, depth + 1);
        if (!l) return 0;
        if (is_inf_val(l)) {
            out->v[0] = e;
            out->n = 1;
            return gr_max(out, &inner, x, depth);
        }
        *out = inner;
        return 1;
    }
    switch (e->t) {
    case E_ADD:
    case E_MUL:
        for (uint32_t i = 0; i < e->n; i++) {
            mset t;
            if (!gr_mrv(EK(e)[i], x, &t, depth)) return 0;
            if (!gr_max(out, &t, x, depth)) return 0;
        }
        return 1;
    case E_POW: /* constant exponent */
        return gr_mrv(K0(e), x, out, depth);
    case E_FN:
        if (e->n != 1) return 0;
        if (e->s == F_LN) return gr_mrv(K0(e), x, out, depth);
        {
            /* other functions: argument must tend to a finite value, or
             * the function must have a known limit at infinity */
            E *l = gr_lim(K0(e), x, depth + 1);
            if (!l) return 0;
            if (is_inf_val(l)) {
                int id = e->s;
                if (!(id == F_ATAN || id == F_TANH || id == F_ERF || id == F_SI || id == F_ACOT || id == F_ABS ||
                      id == F_SQRT))
                    return 0;
            }
            return gr_mrv(K0(e), x, out, depth);
        }
    default: return 0;
    }
}

/* sign of e as x -> inf: +1, -1, or 0 if unknown
 * Tries sign_of (constants, assumptions), then the limit of e; if the limit
 * is 0 or undecided, falls back to sampling e at x = 40, 80, 160 and
 * accepts the sign only if all three samples agree (heuristic). */
static int gr_sign(E *e, E *x, int depth) {
    int s0 = sign_of(e);
    if (s0) return s0;
    E *l = gr_lim(e, x, depth + 1);
    if (l && is_pinf(l)) return 1;
    if (l && is_minf(l)) return -1;
    if (l && is_finite_val(l) && !is_zero(l)) return sign_of(l);
    /* numeric fallback at large x: all samples must agree */
    int sg = 0;
    double xs[3] = {40, 80, 160};
    for (int i = 0; i < 3; i++) {
        double v;
        if (!eval_double_at(e, x->s, xs[i], &v) || v == 0 || v != v) return 0;
        int t = v > 0 ? 1 : -1;
        if (sg && t != sg) return 0;
        sg = t;
    }
    return sg;
}

/* leading term c * W^q of e as W -> 0+ with real (possibly irrational)
 * exponents; handles sums, products, constant powers.  0 on failure.
 * *q is the exact exponent, *qv its numeric value used for comparisons.
 * In a sum, the terms with the smallest qv (within 1e-12 relative) are
 * combined; their exact exponents must be provably equal, and if their
 * coefficients cancel the function fails (the next order would be needed,
 * which this routine does not track). */
static int lt_real(E *e, E *W, E **c, E **q, double *qv) {
    stack_check();
    if (freeof(e, W)) {
        if (is_zero(e)) return 0;
        *c = e;
        *q = K->c0;
        *qv = 0;
        return 1;
    }
    if (expr_eq(e, W)) {
        *c = K->c1;
        *q = K->c1;
        *qv = 1;
        return 1;
    }
    switch (e->t) {
    case E_POW: {
        if (!freeof(K1(e), W) || !is_const_expr(K1(e))) return 0;
        E *bc, *bq;
        double bqv, p;
        if (!lt_real(K0(e), W, &bc, &bq, &bqv)) return 0;
        if (!to_double(K1(e), &p)) return 0;
        *c = s_pow(bc, K1(e));
        *q = s_mul2(bq, K1(e));
        *qv = bqv * p;
        return 1;
    }
    case E_MUL: {
        E *cc = K->c1, *qq = K->c0;
        double v = 0;
        for (uint32_t i = 0; i < e->n; i++) {
            E *ci, *qi;
            double vi;
            if (!lt_real(EK(e)[i], W, &ci, &qi, &vi)) return 0;
            cc = s_mul2(cc, ci);
            qq = s_add2(qq, qi);
            v += vi;
        }
        *c = cc;
        *q = qq;
        *qv = v;
        return 1;
    }
    case E_ADD: {
        E **cs = salloc(sizeof(E *) * e->n), **qs = salloc(sizeof(E *) * e->n);
        double *vs = salloc(sizeof(double) * e->n);
        double best = 1e300;
        for (uint32_t i = 0; i < e->n; i++) {
            if (!lt_real(EK(e)[i], W, &cs[i], &qs[i], &vs[i])) return 0;
            if (vs[i] < best) best = vs[i];
        }
        E *sum = K->c0, *qb = NULL;
        for (uint32_t i = 0; i < e->n; i++)
            if (fabs(vs[i] - best) < 1e-12 * (1 + fabs(best))) {
                if (!qb) qb = qs[i];
                else if (!is_zero(rat_normal(s_sub(qb, qs[i])))) return 0; /* numerically equal, not provably */
                sum = s_add2(sum, cs[i]);
            }
        sum = rat_normal(sum);
        if (is_zero(sum)) return 0; /* cancellation: needs the next order */
        *c = sum;
        *q = qb;
        *qv = best;
        return 1;
    }
    default: return 0;
    }
}

/* e^u built directly as a node marked canonical, bypassing s_exp: the
 * rewritten mrv factor A = e^(lf - c u) must stay a single opaque power so
 * the simplifier cannot split it or recombine it with w. */
static E *raw_exp(E *u) {
    E *r = enode(E_POW, 2);
    K0(r) = K->e_;
    K1(r) = u;
    r->f = F_SIMP;
    return r;
}

/*
 * gr_lim -- limit of e as x -> +inf by the Gruntz algorithm (see above).
 * Steps:
 *   1. trivial cases; compute Omega = mrv(e);
 *   2. if x is in Omega, substitute x -> e^x and recurse (upward move:
 *      afterwards all mrv elements are exponentials);
 *   3. pick u with Omega[0] = e^(+-u), u -> +inf, and a fresh symbol W
 *      standing for w = e^(-u) (name _w + letter by depth);
 *   4. rewrite every f = e^(lf) in Omega as A * W^(-c), c = lim lf/u
 *      (must be a finite constant), larger elements first so enclosing
 *      exponentials are replaced before their subexpressions;
 *   5. series of the rewritten e in W -> 0+ (ser_retry, ln W -> -u);
 *      leading exponent > 0: limit 0; = 0: recurse on the leading
 *      coefficient; < 0: +-inf by gr_sign of the coefficient;
 *   6. if no series exists (irrational exponents of W), use lt_real.
 * Returns the limit, +-inf, or NULL if undecided.  May throw (series
 * swell, memory, interrupt); gruntz() catches.  Shares g_h with the
 * series engine (saved/restored around ser_retry).
 */
static E *gr_lim(E *e, E *x, int depth) {
    stack_check();
    dbg("gr", e, depth);
    if (depth > 14 || --g_gr_budget < 0) return NULL;
    if (freeof(e, x)) return e;
    if (expr_eq(e, x)) return K->inf_;
    mset om;
    if (!gr_mrv(e, x, &om, depth) || !om.n) return NULL;
    if (mset_has(&om, x)) {
        /* move up one level: x -> exp(x) */
        E *e2 = subst(e, x, s_exp(x));
        return gr_lim(e2, x, depth + 1);
    }
    /* choose w = exp(-u'), u' -> +inf */
    E *u0;
    gr_log(om.v[0], x, &u0);
    E *l0 = gr_lim(u0, x, depth + 1);
    if (!is_inf_val(l0)) return NULL;
    E *up = is_pinf(l0) ? u0 : s_neg(u0);
    char nm[8] = "_w";
    nm[2] = (char)('a' + depth % 26);
    nm[3] = 0;
    E *W = mk_sym_name(nm);
    /* rewrite larger elements first */
    for (uint32_t i = 1; i < om.n; i++)
        for (uint32_t j = i; j > 0 && expr_size(om.v[j - 1]) < expr_size(om.v[j]); j--) {
            E *t = om.v[j]; om.v[j] = om.v[j - 1]; om.v[j - 1] = t;
        }
    E *rep[16];
    for (uint32_t i = 0; i < om.n; i++) {
        E *lf;
        gr_log(om.v[i], x, &lf);
        E *c = gr_lim(rat_normal(s_div(lf, up)), x, depth + 1);
        if (!c || !is_finite_val(c) || !is_const_expr(c)) return NULL;
        E *ae = rat_normal(s_sub(lf, s_mul2(c, up)));
        E *A = is_zero(ae) ? K->c1 : raw_exp(ae);
        rep[i] = s_mul2(A, s_pow(W, s_neg(c)));
    }
    E *eW = subst_many(e, om.n, om.v, rep);
    if (freeof(eW, W)) return gr_lim(eW, x, depth + 1);
    /* leading term in W -> 0+ */
    E *saved_h = g_h;
    g_h = W;
    S *ser_ = ser_retry(eW, 3);
    g_h = saved_h;
    if (!ser_ || ser_->n == 0) {
        /* irrational exponents: leading term only */
        E *lc, *lq;
        double lqv;
        if (!lt_real(eW, W, &lc, &lq, &lqv)) return NULL;
        if (lqv > 1e-12) return K->c0;
        if (lqv > -1e-12) return gr_lim(lc, x, depth + 1);
        int sg = gr_sign(lc, x, depth);
        if (!sg) return NULL;
        return sg > 0 ? K->inf_ : s_neg(K->inf_);
    }
    E *c0 = subst(ser_->c[0], g_L, s_neg(up)); /* ln w = -u' */
    if (ser_->val >= SR_INFVAL / 2) return K->c0;
    if (ser_->val > 0) return K->c0;
    if (ser_->val == 0) return gr_lim(c0, x, depth + 1);
    int sg = gr_sign(c0, x, depth);
    if (!sg) return NULL;
    return sg > 0 ? K->inf_ : s_neg(K->inf_);
}

/* limit of e as x -> +inf by Gruntz; NULL if it fails
 * Runs gr_lim (budget set by the outermost lim_side) under an error
 * handler (errors other than interrupts become NULL, including
 * out-of-memory: the caller releases the scratch), and rejects results
 * that are neither finite nor +-inf. */
static E *gruntz(E *e, E *x) {
    tryf tf;
    try_enter(&tf);
    E *volatile r = NULL;
    if (!setjmp(tf.jb)) {
        r = gr_lim(e, x, 0);
        try_leave(&tf);
    } else {
        try_leave(&tf);
        if (K->err == CAS_ERR_INTERRUPTED) rethrow();
        r = NULL;
    }
    if (r && !is_finite_val(r) && !is_inf_val(r)) r = NULL;
    return r;
}

/* for a real limit variable, ln(c^u) = u*ln(c) when c is a positive constant
 * (valid for real u; applied to the whole tree before substitution so the
 * series engine sees u ln c instead of ln of an exponential). */
static E *ln_pow_real(E *e, E *x) {
    stack_check();
    if (!(e->t == E_ADD || e->t == E_MUL || e->t == E_POW || e->t == E_FN) || freeof(e, x)) return e;
    if (IFN(e, F_LN) && e->n == 1 && K0(e)->t == E_POW && freeof(K0(K0(e)), x) && is_poslit(K0(K0(e))))
        return s_mul2(ln_pow_real(K1(K0(e)), x), s_ln(K0(K0(e))));
    E **k = salloc(sizeof(E *) * e->n);
    int ch = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        k[i] = ln_pow_real(EK(e)[i], x);
        ch |= k[i] != EK(e)[i];
    }
    return ch ? rebuild(e, k) : e;
}

/*
 * crosses_cut -- 1 if, for h -> 0+, the argument of a multivalued function in f
 * lies on that function's branch cut at h = 0 but leaves it for h > 0, as in
 * ln(-2 + i h), asin(3 + i h) or atan2(-h, -1).  Substituting h = 0 (or the
 * series at h = 0) then gives the principal value on the cut, which is the
 * limit from one side of the cut only, so lim_side1 checks its result
 * numerically and returns "undecided" rather than a wrong value.  Checked numerically (arguments with other symbols are
 * skipped).
 */
static int on_side_off(E *u, E *h, int imag_axis, double lo, int below) {
    double r0, i0, r1, i1, rel = 1e-12;
    if (!eval_complex_at(u, h->s, 1e-9, 0, &r1, &i1)) return 0;
    if (!eval_complex_at(u, h->s, 0, 0, &r0, &i0) || !isfinite(r0) || !isfinite(i0)) {
        /* u not defined at h = 0 (x sin(1/x)): estimate the end point at h = 1e-12 */
        if (!eval_complex_at(u, h->s, 1e-12, 0, &r0, &i0)) return 0;
        rel = 1e-6;
    }
    if (!isfinite(r0) || !isfinite(i0) || !isfinite(r1) || !isfinite(i1)) return 0;
    double tiny = rel * (1 + hypot(r0, i0));
    double on = imag_axis ? i0 : r0, off0 = imag_axis ? r0 : i0, off1 = imag_axis ? r1 : i1;
    if (fabs(off0) > tiny) return 0;                         /* not on the cut's line */
    if (below ? !(on < lo - tiny) : !(fabs(on) > lo + tiny)) return 0; /* not on the cut */
    return fabs(off1) > 1e-15 * (1 + hypot(r1, i1));         /* leaves it for h > 0 */
}

static int crosses_cut(E *f, E *h) {
    stack_check();
    if (freeof(f, h) || f->t == E_INT || f->t == E_RAT || f->t == E_FLT || f->t == E_SYM || f->t == E_STR) return 0;
    if (f->t == E_POW && !freeof(K0(f), h)) {
        long k;
        if (!num_is_small_int(K1(f), &k) && on_side_off(K0(f), h, 0, 0, 1)) return 1;
    } else if (f->t == E_FN && f->n == 1) {
        E *u = K0(f);
        switch (f->s) {
        case F_LN: if (on_side_off(u, h, 0, 0, 1)) return 1; break;
        case F_ASIN: case F_ACOS: case F_ATANH:
            if (on_side_off(u, h, 0, 1, 0)) return 1;
            break;
        case F_ACOSH: if (on_side_off(u, h, 0, 1, 1)) return 1; break;
        case F_ATAN: case F_ACOT: case F_ASINH:
            if (on_side_off(u, h, 1, 1, 0)) return 1;
            break;
        default: break;
        }
    } else if (f->t == E_FN && f->s == F_ATAN2 && f->n == 2) {
        /* atan2(y, x) jumps across y = 0, x < 0 */
        double yr, yi, xr, xi, y1, y1i;
        if (eval_complex_at(K0(f), h->s, 0, 0, &yr, &yi) && eval_complex_at(K1(f), h->s, 0, 0, &xr, &xi) &&
            eval_complex_at(K0(f), h->s, 1e-9, 0, &y1, &y1i) && fabs(yr) <= 1e-12 * (1 + fabs(xr)) && xr < 0 &&
            fabs(y1) > 1e-15)
            return 1;
    }
    for (uint32_t i = 0; i < f->n; i++)
        if (crosses_cut(EK(f)[i], h)) return 1;
    return 0;
}

/*
 * lim_side -- one-sided limit of e as x -> a from the right (side > 0) or
 * left (side < 0); a = +-inf is approached from the finite side.
 * Strategy (first success wins):
 *   1. x = a +- h (or +-1/h), h -> 0+: continuity, then series leading term;
 *   2. Gruntz on x = a +- 1/t, t -> +inf (skipped for discontinuous e;
 *      its scratch is released on failure);
 *   3. the heuristic limit calculus lim0.
 * Returns the limit, +-inf, undef, or NULL if undecided (the bounded
 * oscillation marker is mapped to NULL).  Resets g_bounded and the internal
 * symbols; interrupts are rethrown, other errors in each stage only make
 * that stage fail.
 */
static E *lim_side1(E *e, E *x, E *a, int side) {
    E *h = mk_sym_name("_hl");
    E *sub;
    if (ISYM(a, S_INF)) sub = s_inv(h);
    else if (is_minf(a)) sub = s_neg(s_inv(h));
    else sub = side > 0 ? s_add2(a, h) : s_sub(a, h);
    E *f;
    tryf tf;
    try_enter(&tf);
    if (!setjmp(tf.jb)) {
        f = subst(ln_pow_real(e, x), x, sub);
        try_leave(&tf);
    } else {
        try_leave(&tf);
        if (K->err == CAS_ERR_INTERRUPTED) rethrow();
        return NULL;
    }
    E *r = NULL;
    int cut = crosses_cut(f, h);
    setup_syms();
    g_bounded = mk_sym_name("_bounded");
    /* 1. continuity / series at the point */
    if (!has_discont(f)) r = try_direct(f, h);
    if (!r) {
        S *sr = NULL;
        tryf tf3;
        char *volatile ms = smark(); /* a failed expansion (out of memory too) gives its scratch back */
        try_enter(&tf3);
        if (!setjmp(tf3.jb)) {
            E *saved = g_h;
            g_h = h;
            sr = ser_retry(f, 3);
            g_h = saved;
            if (sr) r = lead_limit(sr);
            try_leave(&tf3);
        } else {
            try_leave(&tf3);
            if (K->err == CAS_ERR_INTERRUPTED) rethrow();
            srelease(ms);
            r = NULL;
        }
    }
    /* 2. Gruntz on x -> +inf (finite points: x = a +- 1/t) */
    if (!r && !has_discont(e)) {
        char *mg = smark();
        E *t = mk_sym_name("_gt");
        E *g;
        if (ISYM(a, S_INF)) g = subst(e, x, t);
        else if (is_minf(a)) g = subst(e, x, s_neg(t));
        else g = subst(e, x, side > 0 ? s_add2(a, s_inv(t)) : s_sub(a, s_inv(t)));
        setup_syms();
        tryf tfg;
        try_enter(&tfg);
        if (!setjmp(tfg.jb)) {
            r = gruntz(g, t);
            try_leave(&tfg);
        } else { /* out of memory: give the scratch back and try the limit calculus */
            try_leave(&tfg);
            if (K->err != CAS_ERR_MEMORY) rethrow();
            r = NULL;
        }
        if (!r) srelease(mg);
    }
    /* 3. heuristic limit calculus */
    if (!r) {
        tryf tf2;
        try_enter(&tf2);
        if (!setjmp(tf2.jb)) {
            r = lim0(f, h, 0);
            try_leave(&tf2);
        } else {
            try_leave(&tf2);
            if (K->err == CAS_ERR_INTERRUPTED || K->err == CAS_ERR_MEMORY) rethrow();
            r = NULL;
        }
        if (r == g_bounded) r = NULL;
    }
    /* on a branch cut the principal value is the limit from one side only:
     * keep r only if f(h) agrees with it numerically for small h > 0 */
    if (cut && r) {
        double rr, ri, fr, fi;
        if (!eval_complex(r, &rr, &ri) || !isfinite(rr) || !isfinite(ri)) return NULL;
        static const double hs[3] = {1e-10, 1.37e-11, 2.9e-12};
        for (int k = 0; k < 3; k++)
            if (!eval_complex_at(f, h->s, hs[k], 0, &fr, &fi) || hypot(fr - rr, fi - ri) > 1e-6 * (1 + hypot(rr, ri)))
                return NULL;
    }
    return r;
}

/* lim_side1 with the budgets g_lim_budget and g_gr_budget: filled by the
 * outermost call only, so nested limits (lead_limit -> limit_expr) draw on
 * the budget of the limit that started them instead of refilling it. */
static int g_lim_nest;
void series_reset_statics(void); /* cas.c eval_impl: state an error may have left set */
void series_reset_statics(void) { g_sdisp = NULL; g_prec = 0; g_lim_budget = 0; g_fail = 0; g_gr_budget = 0; g_lim_nest = 0; }
static E *lim_side(E *e, E *x, E *a, int side) {
    if (!g_lim_nest) {
        g_lim_budget = 3000;
        g_gr_budget = 2000;
    }
    g_lim_nest++;
    E *volatile r = NULL;
    char *volatile m = smark();
    tryf tf;
    try_enter(&tf);
    if (!setjmp(tf.jb)) {
        r = lim_side1(e, x, a, side);
        try_leave(&tf);
    } else {
        try_leave(&tf);
        g_lim_nest--;
        /* out of memory: undetermined, with the scratch given back (the caller may go on, e.g. a
         * definite integral to its numeric value) */
        if (K->err != CAS_ERR_MEMORY || g_lim_nest) rethrow();
        srelease(m);
        return NULL;
    }
    g_lim_nest--;
    return r;
}

/*
 * limit_expr -- public entry: limit of e as x -> a.
 *   dir: 0 two-sided, +1 from the right, -1 from the left (ignored for
 *        a = +-inf).
 * Lists are handled element-wise (NULL if any element fails).  A two-sided
 * limit returns the common value of both one-sided limits, undef if they
 * differ, and the right-hand limit alone if the left one cannot be
 * determined and e is not real left of a (e.g. sqrt(x) at 0).  Returns NULL
 * if undecided; the caller (cmds.c) turns that into "limit could not be
 * determined".
 */
/* 1 if e evaluates to non-real values (finite, imaginary part not negligible)
 * at the sample points a + side * d (fabs(a) + 1), d = 1e-3, 1e-6, 1e-9 */
static int nonreal_side(E *e, E *x, E *a, int side) {
    double av, re, im;
    if (x->t != E_SYM || !to_double(a, &av)) return 0;
    for (double d = 1e-3; d > 1e-10; d *= 1e-3)
        if (!eval_complex_at(e, x->s, av + side * d * (fabs(av) + 1), 0, &re, &im) || !isfinite(re) ||
            !isfinite(im) || fabs(im) <= 1e-9 * (1 + fabs(re)))
            return 0;
    return 1;
}

E *limit_expr(E *e, E *x, E *a, int dir) {
    stack_check();
    if (e->t == E_LIST) {
        E **v = salloc(sizeof(E *) * (e->n + 1));
        for (uint32_t i = 0; i < e->n; i++) {
            v[i] = limit_expr(EK(e)[i], x, a, dir);
            if (!v[i]) return NULL;
        }
        return s_list(e->n, v);
    }
    if (ISYM(a, S_INF) || is_minf(a)) return lim_side(e, x, a, 1);
    if (dir) return lim_side(e, x, a, dir);
    E *r1 = lim_side(e, x, a, 1);
    E *r2 = lim_side(e, x, a, -1);
    if (r1 && r2) {
        if (expr_eq(r1, r2)) return r1;
        E *d = rat_normal(s_sub(r1, r2));
        if (is_zero(d)) return r1;
        /* real-domain convention: a side where e is not real is ignored
         * (ln(x) at 0: -inf, the left side being complex infinity) */
        if (ISYM(r2, S_UNDEF) && nonreal_side(e, x, a, -1) && !nonreal_side(e, x, a, 1)) return r1;
        if (ISYM(r1, S_UNDEF) && nonreal_side(e, x, a, 1) && !nonreal_side(e, x, a, -1)) return r2;
        return K->undef_;
    }
    /* one-sided only (e.g. sqrt(x) at 0): accepted only when e is not real
     * just left of a; a left side that is real, or that cannot be evaluated
     * (exp(-1/x) sin(1/x) overflows), leaves the two-sided limit undecided */
    if (r1 && !r2) {
        if (!nonreal_side(e, x, a, -1)) return NULL;
        /* a finite r1 must also be the value approached from the left
         * (sqrt(x) at 0, but not ln(-2 + i x), whose left limit is ln 2 - i pi) */
        double rr, ri, fr, fi, av;
        if (eval_complex(r1, &rr, &ri) && isfinite(rr) && isfinite(ri) && to_double(a, &av) &&
            (!eval_complex_at(e, x->s, av - 1e-9 * (fabs(av) + 1), 0, &fr, &fi) ||
             hypot(fr - rr, fi - ri) > 1e-3 * (1 + hypot(rr, ri))))
            return NULL;
        return r1;
    }
    return NULL;
}
