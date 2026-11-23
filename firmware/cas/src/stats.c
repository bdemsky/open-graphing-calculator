/*
 * stats.c -- probability distribution commands (see stats.h):
 *
 *   normalpdf(x[,mu,sigma])      normalcdf(lo,hi[,mu,sigma])   invNorm(p[,mu,sigma])
 *   tpdf(x,df)  tcdf(lo,hi,df)  invT(p,df)
 *   chi2pdf(x,df)  chi2cdf(lo,hi,df)  invChi2(p,df)
 *   Fpdf(x,d1,d2)  Fcdf(lo,hi,d1,d2)  invF(p,d1,d2)
 *   binompdf(n,p[,x])  binomcdf(n,p[,x])  poissonpdf(mu,x)  poissoncdf(mu,x)
 *   geometpdf(p,x)  geometcdf(p,x)
 *
 * The usual calculator argument order and defaults. Results are floats (also in exact mode),
 * except binomial/geometric probabilities of exact rational arguments in
 * exact mode, which are exact rationals (binompdf(3,1/2,1) = 3/8). A list
 * argument maps the command over its elements; binompdf(n,p) / binomcdf(n,p)
 * give the list for x = 0..n. Symbolic arguments leave the call unevaluated
 * (so Y1=normalpdf(x) graphs: numeric.c compiles it with stats_num).
 *
 * Data flow: eval_rec (eval.c) evaluates the arguments of a command call and
 * passes them to eval_cmd (cmds.c), which forwards every id in the
 * STATS_SYMS range to stats_cmd below.  stats_cmd handles the CAS-level
 * concerns (arity, list mapping, exact rational results, symbolic arguments,
 * turning NAN into a domain error) and then calls stats_num, which is the
 * only place that knows the argument order and defaults of each command and
 * maps it onto the double-precision library in statdist.c (cas_stat.h).
 * The numeric evaluators in numeric.c (complex evaluation and the cas_compile
 * bytecode used for graphing) call stats_num directly, bypassing the CAS.
 *
 * Memory: results and intermediate lists are allocated in scratch; nothing
 * here touches the persistent heap or global state other than reading
 * K->set.exact.
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os") /* flash budget: size over speed (not time-critical) */
#endif
#include "cas_int.h"
#include <math.h>
#include "../include/cas_stat.h"

/* {min, max} number of arguments of each command, indexed by
 * id - C_NORMALPDF (same order as STATS_SYMS in stats.h).  The binomial rows
 * allow 2 arguments (the whole distribution as a list, see stats_cmd). */
static const uint8_t arity[][2] = {
    {1, 3}, {2, 4}, {1, 3}, /* normal */
    {2, 2}, {3, 3}, {2, 2}, /* t */
    {2, 2}, {3, 3}, {2, 2}, /* chi2 */
    {3, 3}, {4, 4}, {3, 3}, /* F */
    {2, 3}, {2, 3}, {2, 2}, {2, 2}, {2, 2}, {2, 2},
};

/* Numeric value of distribution command `id` applied to n real arguments
 * x[0..n-1]: fills in the calculator defaults (mu = 0, sigma = 1 for the
 * normal) and calls the matching sd_* function.  Returns NAN for an unknown
 * id, a bad argument count, binompdf/binomcdf with only (n, p) (the list form
 * has no scalar value), or arguments outside the distribution's domain.
 * Pure function: no allocation, no K access, never throws, so it is safe to
 * call from the compiled bytecode evaluator (cas_fn_eval). */
double stats_num(int id, int n, const double *x)
{
    if (!STATS_IS_CMD(id) || n < arity[id - C_NORMALPDF][0] || n > arity[id - C_NORMALPDF][1])
        return NAN;
    double mu = 0, s = 1;
    switch (id) {
    case C_NORMALPDF: case C_INVNORM:
        if (n > 1) mu = x[1];
        if (n > 2) s = x[2];
        return id == C_NORMALPDF ? sd_normpdf(x[0], mu, s) : sd_invnorm(x[0], mu, s);
    case C_NORMALCDF:
        if (n > 2) mu = x[2];
        if (n > 3) s = x[3];
        return sd_normcdf(x[0], x[1], mu, s);
    case C_TPDF: return sd_tpdf(x[0], x[1]);
    case C_TCDF: return sd_tcdf(x[0], x[1], x[2]);
    case C_INVT: return sd_invt(x[0], x[1]);
    case C_CHI2PDF: return sd_chi2pdf(x[0], x[1]);
    case C_CHI2CDF: return sd_chi2cdf(x[0], x[1], x[2]);
    case C_INVCHI2: return sd_invchi2(x[0], x[1]);
    case C_FPDF: return sd_fpdf(x[0], x[1], x[2]);
    case C_FCDF: return sd_fcdf(x[0], x[1], x[2], x[3]);
    case C_INVF: return sd_invf(x[0], x[1], x[2]);
    case C_BINOMPDF: return n == 3 ? sd_binompdf(x[0], x[1], x[2]) : NAN;
    case C_BINOMCDF: return n == 3 ? sd_binomcdf(x[0], x[1], x[2]) : NAN;
    case C_POISSONPDF: return sd_poissonpdf(x[0], x[1]);
    case C_POISSONCDF: return sd_poissoncdf(x[0], x[1]);
    case C_GEOMETPDF: return sd_geometpdf(x[0], x[1]);
    case C_GEOMETCDF: return sd_geometcdf(x[0], x[1]);
    }
    return NAN;
}

/* 1 for exact rational numbers (E_INT or E_RAT). */
static int is_rational(E *e) { return e->t == E_INT || e->t == E_RAT; }

/* Exact binomial / geometric probabilities (exact mode, all arguments exact
 * rationals), e.g. binompdf(3,1/2,1) = 3/8, geometcdf(1/3,2) = 5/9.
 * a[] holds (n, p, x) for binomial and (p, x) for geometric.
 * Returns NULL ("not applicable", the caller falls back to floats) when p is
 * not in [0,1], x is not a machine-size integer, n is not an integer in
 * [0, 2000] (bounds the cost of exact binomial sums), or p = 0 for geometric.
 * The cdf of the binomial is summed term by term with the coefficient
 * updated incrementally, C(n,k) = C(n,k-1) (n-k+1) / k, polling for
 * interrupts; O(x) exact rational operations.  Result in scratch. */
static E *exact_discrete(int id, E **a)
{
    E *p = id == C_BINOMPDF || id == C_BINOMCDF ? a[1] : a[0];
    E *x = id == C_BINOMPDF || id == C_BINOMCDF ? a[2] : a[1];
    long nn = 0, xx;
    if (!is_rational(p) || num_sgn(p) < 0 || num_cmp(p, K->c1) > 0 || !num_is_small_int(x, &xx))
        return NULL;
    if ((id == C_BINOMPDF || id == C_BINOMCDF) && (!num_is_small_int(a[0], &nn) || nn < 0 || nn > 2000))
        return NULL;
    E *q = s_sub(K->c1, p);
    switch (id) {
    case C_GEOMETPDF:
        if (num_sgn(p) == 0) return NULL;
        return xx < 1 ? K->c0 : s_mul2(p, s_powi(q, xx - 1));
    case C_GEOMETCDF:
        if (num_sgn(p) == 0) return NULL;
        return xx < 1 ? K->c0 : s_sub(K->c1, s_powi(q, xx));
    case C_BINOMPDF:
        if (xx < 0 || xx > nn) return K->c0;
        return s_mul2(s_fn2(F_BINOM, mk_int(nn), mk_int(xx)), s_mul2(s_powi(p, xx), s_powi(q, nn - xx)));
    default: { /* C_BINOMCDF */
        if (xx < 0) return K->c0;
        if (xx >= nn) return K->c1;
        E *sum = K->c0, *c = K->c1; /* C(n,k) */
        for (long k = 0; k <= xx; k++) {
            poll_intr();
            if (k) c = s_div(s_mul2(c, mk_int(nn - k + 1)), mk_int(k));
            sum = s_add2(sum, s_mul2(c, s_mul2(s_powi(p, k), s_powi(q, nn - k))));
        }
        return sum;
    }
    }
}

/* Evaluate a distribution command (see stats.h for the contract).  Order
 * of the cases:
 *  1. arity check (CAS_ERR_DOMAIN);
 *  2. binompdf(n,p) / binomcdf(n,p): the list over x = 0..n (n <= 10000),
 *     each element by a recursive 3-argument call; a symbolic n leaves the
 *     call unevaluated, a numeric non-integer (or too large) n is an error;
 *  3. list arguments: map element-wise (all lists must have equal length,
 *     scalars are broadcast), by recursion;
 *  4. exact mode with all-rational arguments: exact_discrete for the
 *     binomial and geometric commands (the Poisson ones involve e^-mu);
 *  5. otherwise convert to doubles: inf / -inf become infinite bounds, other
 *     non-real constants are errors, and any argument that still contains a
 *     variable makes the whole call stay unevaluated (marked F_SIMP so the
 *     simplifier leaves it alone; numeric.c can then compile it for graphs);
 *  6. stats_num, with NAN reported as a domain error.
 * Results are allocated in scratch. */
E *stats_cmd(int id, uint32_t n, E **a)
{
    stack_check();
    if (n < arity[id - C_NORMALPDF][0] || n > arity[id - C_NORMALPDF][1])
        cas_throw(CAS_ERR_DOMAIN, "wrong number of arguments");
    if ((id == C_BINOMPDF || id == C_BINOMCDF) && n == 2) {
        /* list for x = 0..n */
        long nn;
        if (!num_is_small_int(a[0], &nn) || nn < 0 || nn > 10000) {
            double d;
            if (to_double(a[0], &d)) cas_throw(CAS_ERR_DOMAIN, "expected an integer number of trials");
            return s_fn(id, n, a);
        }
        E **v = salloc(sizeof(E *) * (size_t)(nn + 2));
        for (long k = 0; k <= nn; k++) {
            E *b[3] = {a[0], a[1], mk_int(k)};
            v[k] = stats_cmd(id, 3, b);
        }
        return s_list((uint32_t)nn + 1, v);
    }
    /* lists: map over the elements */
    uint32_t len = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (a[i]->t != E_LIST) continue;
        if (len && a[i]->n != len) cas_throw(CAS_ERR_DOMAIN, "list sizes do not match");
        len = a[i]->n;
    }
    if (len) {
        E **v = salloc(sizeof(E *) * (len + 1));
        for (uint32_t k = 0; k < len; k++) {
            E *b[4];
            for (uint32_t i = 0; i < n; i++) b[i] = a[i]->t == E_LIST ? EK(a[i])[k] : a[i];
            v[k] = stats_cmd(id, n, b);
        }
        return s_list(len, v);
    }
    if (K->set.exact && id >= C_BINOMPDF && id != C_POISSONPDF && id != C_POISSONCDF) {
        int all = 1;
        for (uint32_t i = 0; i < n; i++) all &= is_rational(a[i]);
        E *r = all ? exact_discrete(id, a) : NULL;
        if (r) return r;
    }
    double x[4];
    for (uint32_t i = 0; i < n; i++) {
        if (!to_double(a[i], &x[i])) {
            /* inf / -inf bounds */
            if (ISYM(a[i], S_INF)) x[i] = INFINITY;
            else if (ISYM(s_neg(a[i]), S_INF)) x[i] = -INFINITY;
            else if (is_const_expr(a[i])) cas_throw(CAS_ERR_DOMAIN, "expected a real number");
            else {
                E *e = mk_fn(id, n, a); /* symbolic: keep unevaluated */
                e->f = F_SIMP;
                return e;
            }
        }
    }
    double r = stats_num(id, (int)n, x);
    if (r != r) cas_throw(CAS_ERR_DOMAIN, "argument outside the domain of the distribution");
    return mk_flt(r);
}
