/*
 * sum.c -- symbolic definite sums and products, Bernoulli numbers, zeta(2n).
 *
 * Entry points
 *   sum_expr(f, k, a, b)      sum_{k=a}^{b} f      (b may be the symbol inf)
 *   product_expr(f, k, a, b)  prod_{k=a}^{b} f
 *   bernoulli(n), zeta_exact(n)  exact B_n and zeta(n) for even n
 * Callers: cmds.c (commands sum / product; a NULL result means "not found"
 * and the command keeps the call unevaluated), simp.c (auto-simplification
 * of zeta(2m) and of zeta at negative integers via Bernoulli numbers).
 *
 * Strategy of sum_expr (first method that succeeds wins):
 *   1. small numeric ranges (< 5000 terms): add the terms explicitly;
 *   2. f free of k: f * (b - a + 1);  sums are split term by term;
 *   3. sum_{k=0}^{n} binomial(n,k) r^k P(k): apply the Euler operator
 *      P(x d/dx) to (1+x)^n and set x = r;
 *   4. exponential series sum_{k>=0} c^k / k! = exp(c);
 *   5. f = P(k) r^k (polynomial times geometric): indefinite sum F = Q(k) r^k
 *      with r Q(k+1) - Q(k) = P(k) by undetermined coefficients, then
 *      F(b+1) - F(a) (or the limit for b = inf).  This is the classical
 *      "summation by undetermined coefficients" special case of Gosper's
 *      algorithm (cf. Graham, Knuth & Patashnik "Concrete Mathematics" 2.6);
 *   6. b = inf, f = r^k R(k) with R having simple poles at integer shifts:
 *      logarithmic series sum r^j / j = -ln(1 - r);
 *   7. rational f: partial fractions; simple poles 1/(k+alpha) whose alphas
 *      differ by integers telescope (their coefficients must add up to 0,
 *      otherwise the sum is a harmonic-type divergence / not closed form);
 *      higher poles c/(k+alpha)^m with b = inf give zeta(m) minus finitely
 *      many terms (Hurwitz zeta at integer and half-integer shifts).
 *   There is no Gosper / Zeilberger algorithm, so general hypergeometric
 *   sums stay unevaluated.
 *
 * Products: explicit for small ranges; c^(b-a+1) for constant factors;
 * c^(sum of exponents) for c^g(k); termwise for products; and rational
 * factors lc*(k+alpha) with integer alpha become factorial ratios
 * (b+alpha)! / (a+alpha-1)!, which simplify_factorials then telescopes.
 *
 * Memory: all results are scratch expressions; only direct_sum compacts
 * scratch (skeep) because it is the one loop whose length depends on the
 * input.  Errors from the algebra routines (memory, interrupt, domain)
 * propagate via cas_throw; "cannot do it" is reported by returning NULL.
 */
#include "cas_int.h"
#include "upoly.h"
#include <string.h>
#include <math.h>

/* cold code: compiled for size (flash bank 2) */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

/* 1 if a and b are machine integers with b - a < 5000 (sum term by term);
 * stores them in *lo / *hi. */
static int small_range(E *a, E *b, long *lo, long *hi) {
    return num_is_small_int(a, lo) && num_is_small_int(b, hi) && *hi - *lo < 5000;
}

static int is_inf(E *e) { return ISYM(e, S_INF); }

/*
 * poly_geo -- recognise f = P(k) * r^k with P a polynomial in k.
 * Every factor base^(d*k + b0) with base free of k and d free of k
 * contributes base^d to the ratio r and the constant base^b0 to P; the
 * remaining factors, expanded, must form a polynomial in k.  On success
 * stores P (expanded) and r (1 when f is a plain polynomial) and returns 1;
 * returns 0 otherwise.  Example: 3 * k * 2^(k+1) -> P = 6k, r = 2.
 */
static int poly_geo(E *f, E *k, E **P, E **r) {
    E **fs = f->t == E_MUL ? EK(f) : &f;
    uint32_t n = f->t == E_MUL ? f->n : 1;
    E **pp = salloc(sizeof(E *) * (n + 1));
    uint32_t np = 0;
    E *rr = K->c1;
    for (uint32_t i = 0; i < n; i++) {
        E *t = fs[i];
        if (t->t == E_POW && freeof(K0(t), k) && !freeof(K1(t), k)) {
            /* base^(a k + b) */
            E *d = diff(K1(t), k);
            if (!freeof(d, k)) return 0;
            E *b0 = expand(s_sub(K1(t), s_mul2(d, k)));
            rr = s_mul2(rr, s_pow(K0(t), d));
            pp[np++] = s_pow(K0(t), b0);
            continue;
        }
        pp[np++] = t;
    }
    E *Pe = expand(s_mul(np, pp));
    long d;
    if (!poly_degree_in(Pe, k, &d)) return 0;
    *P = Pe;
    *r = rr;
    return 1;
}

/*
 * indef_poly_geo -- indefinite sum (antidifference) of P(k) r^k:
 * returns F with F(k+1) - F(k) = P(k) r^k, or NULL.
 *
 * Ansatz F = Q(k) r^k.  Then F(k+1) - F(k) = (r Q(k+1) - Q(k)) r^k, so Q
 * must satisfy the linear difference equation r Q(k+1) - Q(k) = P(k).
 *   - r != 1: deg Q = deg P (the leading coefficients give (r-1) q_d = p_d);
 *   - r == 1: deg Q = deg P + 1 and the constant q_0 is arbitrary, so it is
 *     fixed to 0 and removed from the unknowns.
 * The unknown coefficients are fresh symbols _q00, _q01, ...; comparing
 * coefficients of k^i gives a linear system solved exactly by linsolve.
 * (If r is a symbol that happens to equal 1 the system has no solution and
 * NULL results.)  deg P > 30 is refused to bound the system size.
 * Cost: one (d+2) x (d+2) exact linear solve.
 */
static E *indef_poly_geo(E *P, E *r, E *k) {
    long d;
    poly_degree_in(P, k, &d);
    if (d > 30) return NULL;
    int isone = is_one(r);
    long dq = isone ? d + 1 : d;
    E **cs = salloc(sizeof(E *) * (size_t)(dq + 2));
    char nm[8];
    for (long i = 0; i <= dq; i++) {
        nm[0] = '_';
        nm[1] = 'q';
        nm[2] = (char)('0' + i / 10);
        nm[3] = (char)('0' + i % 10);
        nm[4] = 0;
        cs[i] = mk_sym_name(nm);
    }
    E **t = salloc(sizeof(E *) * (size_t)(dq + 1));
    for (long i = 0; i <= dq; i++) t[i] = s_mul2(cs[i], s_pow(k, mk_int(i)));
    E *Q = s_add((uint32_t)(dq + 1), t);
    E *lhs = s_sub(s_mul2(r, subst(Q, k, s_add2(k, K->c1))), Q);
    E *eq = expand(s_sub(lhs, P));
    E **eqs = salloc(sizeof(E *) * (size_t)(dq + 2));
    long ne = 0;
    long de;
    if (!poly_degree_in(eq, k, &de)) return NULL;
    for (long i = 0; i <= de; i++) eqs[ne++] = poly_coeff(eq, k, i);
    long nunk = isone ? dq : dq + 1; /* for r=1 the constant term is free: drop cs[0] */
    E **vars = salloc(sizeof(E *) * (size_t)(nunk + 1));
    long nv = 0;
    for (long i = isone ? 1 : 0; i <= dq; i++) vars[nv++] = cs[i];
    if (isone)
        for (long i = 0; i < ne; i++) eqs[i] = subst(eqs[i], cs[0], K->c0);
    E *sol = linsolve(s_list((uint32_t)ne, eqs), s_list((uint32_t)nv, vars));
    if (!sol || sol->n != (uint32_t)nv) return NULL;
    E **xs = salloc(sizeof(E *) * (size_t)(nv + 1)), **vs = salloc(sizeof(E *) * (size_t)(nv + 1));
    for (uint32_t i = 0; i < sol->n; i++) {
        xs[i] = K0(EK(sol)[i]);
        vs[i] = K1(EK(sol)[i]);
    }
    E *Qs = subst_many(Q, sol->n, xs, vs);
    if (isone) Qs = subst(Qs, cs[0], K->c0);
    return s_mul2(Qs, s_pow(r, k));
}

/*
 * bernoulli -- exact Bernoulli number B_n (convention B_1 = -1/2), 0 <= n <=
 * 400; throws CAS_ERR_DOMAIN outside that range.  Odd n > 1 give 0.
 *
 * Akiyama-Tanigawa algorithm (Kaneko, "The Akiyama-Tanigawa algorithm for
 * Bernoulli numbers", J. Integer Sequences 3 (2000)): start a row with
 * a[m] = 1/(m+1) and repeatedly replace a[j-1] by j * (a[j-1] - a[j]); after
 * processing m = n the value a[0] is B_n with the B_1 = +1/2 convention,
 * which only differs at n = 1 (handled up front).  O(n^2) exact rational
 * operations; the numbers grow, hence the cap.
 */
E *bernoulli(long n) {
    if (n < 0 || n > 400) cas_throw(CAS_ERR_DOMAIN, "bernoulli: index out of range");
    if (n == 1) return K->cmhalf;
    if (n > 1 && (n & 1)) return K->c0;
    E **a = salloc(sizeof(E *) * (size_t)(n + 1));
    for (long m = 0; m <= n; m++) {
        a[m] = mk_frac(1, m + 1);
        for (long j = m; j >= 1; j--) a[j - 1] = num_mul(mk_int(j), num_sub(a[j - 1], a[j]));
    }
    return a[0];
}

/* zeta(2m) = (-1)^(m+1) B_2m (2 pi)^(2m) / (2 (2m)!)   (Euler; DLMF 25.6.2)
 * Returns the exact value c * pi^n for even n > 0, NULL for odd or
 * non-positive n (zeta(odd) has no known closed form and stays symbolic). */
E *zeta_exact(long n) {
    if (n <= 0 || (n & 1)) return NULL;
    long m = n / 2;
    E *b = bernoulli(n);
    E *c = num_div(num_mul(b, int_pow(mk_int(2), (uint32_t)n)), num_mul(K->c2, int_fact((uint32_t)n)));
    if (!(m & 1)) c = num_neg(c);
    return s_mul2(c, s_pow(K->pi_, mk_int(n)));
}

/* zeta(2m) */
static E *zeta_even(long m) { return zeta_exact(2 * m); }

/*
 * sum_rational -- sum_{k=a}^{b} f for f rational in k, via partial
 * fractions.  Returns the closed form or NULL
 * when the method does not apply.
 *
 * The partial fraction terms are split into
 *   simple poles  c_i / (k + alpha_i)   (numerator free of k, linear den.)
 *   other terms   c / (lc (k + alpha)^m), m >= 2   (only for b = inf)
 * Simple poles are grouped into classes whose alphas differ by integers.
 * Within a class, with base = the smallest alpha and s_i = alpha_i - base,
 *   sum_k c_i/(k+alpha_i) = c_i * sum_{t=a+base+s_i}^{b+base+s_i} 1/t,
 * so if sum_i c_i = 0 the common middle part cancels and only finitely many
 * boundary terms remain (telescoping):
 *   sum_i c_i [ -sum_{q<s_i} 1/(a+base+q) + sum_{q<s_i} 1/(b+base+1+q) ],
 * where the upper boundary terms vanish for b = inf.  If sum_i c_i != 0 the
 * sum is not expressible without harmonic numbers / digamma (NULL).
 * The other terms use zeta(m) (exact for even m, symbolic zeta(m) for odd)
 * minus the missing initial terms when a + alpha is a positive integer, or
 * the odd-integer series when a + alpha is a half-integer.
 *
 * Assumes no pole k = -alpha_i lies in [a, b] (sum_expr checks this).
 */
static E *sum_rational(E *f, E *k, E *a, E *b) {
    E *pf = partfrac(f, k);
    E **ts = pf->t == E_ADD ? EK(pf) : &pf;
    uint32_t n = pf->t == E_ADD ? pf->n : 1;
    /* collect simple-pole terms c/(k+alpha) */
    E **cc = salloc(sizeof(E *) * n), **al = salloc(sizeof(E *) * n);
    int *used = salloc(sizeof(int) * n);
    uint32_t np = 0;
    E **other = salloc(sizeof(E *) * (n + 1));
    uint32_t no = 0;
    for (uint32_t i = 0; i < n; i++) {
        E *t = ts[i];
        E *num, *den;
        numden(t, &num, &den);
        long dd;
        if (freeof(num, k) && poly_degree_in(den, k, &dd) && dd == 1) {
            E *lc = poly_coeff(den, k, 1);
            cc[np] = rat_normal(s_div(num, lc));
            al[np] = rat_normal(s_div(poly_coeff(den, k, 0), lc));
            used[np] = 0;
            np++;
        } else
            other[no++] = t;
    }
    E **res = salloc(sizeof(E *) * (n + 64));
    uint32_t nr = 0;
    /* group simple poles whose alphas differ by integers */
    for (uint32_t i = 0; i < np; i++) {
        if (used[i]) continue;
        E *gc = K->c0;
        uint32_t members[64];
        uint32_t nm = 0;
        for (uint32_t j = i; j < np && nm < 64; j++) {
            if (used[j]) continue;
            E *dlt = rat_normal(s_sub(al[j], al[i]));
            if (dlt->t != E_INT) continue;
            used[j] = 1;
            members[nm++] = j;
            gc = s_add2(gc, cc[j]);
        }
        gc = rat_normal(gc);
        /* gc != 0: harmonic-type part, not expressible in closed form.  For
         * b = inf a divergent sum was already detected by sum_expr (deg num
         * = deg den - 1); here the classes' gc add up to 0 and the sum
         * converges (to digamma values), e.g. 1/k - 2/(2k+1). */
        if (!is_zero(gc)) return NULL;
        /* telescoping: sum_i c_i/(k + al_i); shift by s_i = al_i - al_min */
        E *base = al[members[0]];
        for (uint32_t m = 1; m < nm; m++)
            if (num_cmp(rat_normal(s_sub(al[members[m]], base)), K->c0) < 0) base = al[members[m]];
        for (uint32_t m = 0; m < nm; m++) {
            uint32_t j = members[m];
            long s;
            num_is_small_int(rat_normal(s_sub(al[j], base)), &s);
            /* res has n + 64 slots: give up before the 2 s boundary terms
             * below would overflow it */
            if (s < 0 || nr + 2 * (unsigned long)s > n + 60) return NULL;
            /* c_j * (sum_{t=a+base+s}^{b+base+s} 1/t): telescoping parts */
            for (long q = 0; q < s; q++) {
                /* -c_j/(a+base+q)  [lower end]   +c_j/(b+base+1+q) [upper end] */
                res[nr++] = s_neg(s_div(cc[j], s_add(3, (E *[]){a, base, mk_int(q)})));
                if (!is_inf(b)) res[nr++] = s_div(cc[j], s_add(3, (E *[]){b, base, mk_int(q + 1)}));
            }
        }
    }
    /* remaining terms: c/(lc (k+al0)^m), m >= 2, only for b = inf.
     * sum_{k>=a} (k+al0)^-m = sum_{j>=start} j^-m with start = a + al0:
     *   start a positive integer:  zeta(m) - sum_{j<start} j^-m;
     *   start = m0/2, m0 odd:      2^m sum_{odd j>=m0} j^-m
     *                            = 2^m [(1 - 2^-m) zeta(m) - sum_{odd j<m0} j^-m]
     * (Hurwitz zeta at integer / half-integer parameters).  Shifts are
     * limited (start <= 1000, m0 <= 2001) to bound the correction sums. */
    for (uint32_t i = 0; i < no; i++) {
        E *t = other[i];
        if (!is_inf(b)) return NULL;
        E *num, *den;
        numden(t, &num, &den);
        long dd;
        if (!freeof(num, k) || !poly_degree_in(den, k, &dd)) return NULL;
        /* den = lc * (k + al)^dd ?  If so, al0 = (coefficient of k^(dd-1)) /
         * (dd * lc) by the binomial theorem; verify by expanding. */
        E *lc = poly_coeff(den, k, dd);
        E *al0 = rat_normal(s_div(poly_coeff(den, k, dd - 1), s_mul2(mk_int(dd), lc)));
        E *chk = expand(s_sub(den, s_mul2(lc, s_powi(s_add2(k, al0), dd))));
        if (!is_zero(chk) || dd < 2) return NULL;
        E *z = (dd & 1) ? s_fn1(F_ZETA, mk_int(dd)) : zeta_even(dd / 2);
        E *start = rat_normal(s_add2(a, al0));
        long st;
        if (start->t == E_RAT && is_int_val(K1(start), 2)) {
            /* half-integer shift: sum_{k} (k+al0)^-s = 2^s sum over odd m >= 2*start */
            E *twos = num_mul(start, K->c2);
            long m0;
            if (!num_is_small_int(twos, &m0) || m0 < 1 || m0 > 2001) return NULL;
            E *oddsum = s_mul2(s_sub(K->c1, s_pow(K->c2, mk_int(-dd))), z);
            for (long m = 1; m < m0; m += 2) oddsum = s_sub(oddsum, s_inv(s_powi(mk_int(m), dd)));
            res[nr++] = s_mul2(s_div(num, lc), s_mul2(s_powi(K->c2, dd), oddsum));
            continue;
        }
        if (!num_is_small_int(start, &st) || st < 1 || st > 1000) return NULL;
        E *corr = K->c0;
        for (long j = 1; j < st; j++) corr = s_add2(corr, s_inv(s_powi(mk_int(j), dd)));
        res[nr++] = s_mul2(s_div(num, lc), s_sub(z, corr));
    }
    return s_add(nr, res);
}

/*
 * direct_sum -- explicit sum (prod = 0) or product (prod = 1) of f(k) for
 * k = lo..hi (machine integers, hi >= lo, fewer than 5000 terms).
 * Every 64 terms the accumulator is compacted with skeep(mark, acc), which
 * frees the intermediate terms so memory stays bounded by the size of the
 * running result.  f and k were allocated before the mark and are not
 * affected.  Polls for interrupts every term.
 */
static E *direct_sum(E *f, E *k, long lo, long hi, int prod) {
    E *acc = prod ? K->c1 : K->c0;
    char *mark = smark();
    for (long i = lo; i <= hi; i++) {
        poll_intr();
        E *t = subst(f, k, mk_int(i));
        acc = prod ? s_mul2(acc, t) : s_add2(acc, t);
        if (((i - lo) & 63) == 63) acc = skeep(mark, acc);
    }
    return acc;
}

/*
 * sum_binomial -- binomial moment sums
 *     sum_{k=0}^{n} binomial(n,k) r^k P(k) = [P(theta) (1+x)^n]_{x=r},
 * theta = x d/dx (the Euler operator), because theta x^k = k x^k and
 * (1+x)^n = sum_k binomial(n,k) x^k.  Requires a = 0, a factor
 * binomial(n, k) with n equal to the upper bound b, factors c^k (multiplied
 * into r) and a polynomial remainder P of degree <= 20 (theta^j is applied
 * d times, each with a differentiation and an expansion).  Returns the
 * factored result or NULL if f does not have this shape.
 * Example: sum(binomial(n,k)*k, k, 0, n) = n 2^(n-1).
 */
static E *sum_binomial(E *f, E *k, E *a, E *b) {
    if (!is_zero(a)) return NULL;
    E **fs = f->t == E_MUL ? EK(f) : &f;
    uint32_t n = f->t == E_MUL ? f->n : 1;
    E *nn = NULL, *r = K->c1;
    E **rest = salloc(sizeof(E *) * (n + 1));
    uint32_t nr = 0;
    for (uint32_t i = 0; i < n; i++) {
        E *t = fs[i];
        if (IFN(t, F_BINOM) && t->n == 2 && expr_eq(K1(t), k) && freeof(K0(t), k) && !nn) nn = K0(t);
        else if (t->t == E_POW && freeof(K0(t), k) && expr_eq(K1(t), k)) r = s_mul2(r, K0(t));
        else rest[nr++] = t;
    }
    if (!nn || !expr_eq(nn, b)) return NULL;
    E *P = expand(s_mul(nr, rest));
    long d;
    if (!poly_degree_in(P, k, &d) || d > 20) return NULL;
    E *X = mk_sym_name("_bx");
    E *G = s_pow(s_add2(K->c1, X), nn);
    /* apply P(theta), theta = x d/dx: sum c_j theta^j G */
    E *acc = K->c0, *th = G;
    for (long j = 0; j <= d; j++) {
        E *c = poly_coeff(P, k, j);
        if (!is_zero(c)) acc = s_add2(acc, s_mul2(c, th));
        if (j < d) th = expand(s_mul2(X, diff(th, X)));
    }
    return factor_expr(rat_normal(subst(acc, X, r)));
}

/*
 * pole_in_range -- does the denominator den (polynomial in k) vanish at an
 * integer k in [a, b]?  Integer zeros come from the linear factors of den
 * over Q.  Returns 1 if such a pole certainly lies in the range, -1 if it
 * lies in it only for some values of a symbolic upper bound b (e.g.
 * 1/(k-3) from 1 to n), 0 otherwise or when a is not an integer.
 */
static int pole_in_range(E *den, E *k, E *a, E *b) {
    long lo, hi, r, d;
    if (!num_is_small_int(a, &lo)) return 0;
    E *cnt;
    E *fl = factor_list_in(den, k, &cnt);
    int res = 0;
    for (uint32_t i = 0; i < fl->n; i++) {
        E *g = K0(EK(fl)[i]);
        if (!poly_degree_in(g, k, &d) || d != 1) continue;
        E *root = rat_normal(s_neg(s_div(poly_coeff(g, k, 0), poly_coeff(g, k, 1))));
        if (!num_is_small_int(root, &r) || r < lo) continue;
        if (is_inf(b) || (num_is_small_int(b, &hi) && r <= hi)) return 1;
        if (!is_num(b)) res = -1;
    }
    return res;
}

/*
 * sum_expr -- sum_{k=a}^{b} f(k).
 *   f  summand (simplified), k the summation symbol, a / b the bounds;
 *      b may be inf (a may be any expression, e.g. a symbol).
 * Returns the closed form, +-inf for some detected divergences, or NULL if
 * no method applies (the caller then returns the sum unevaluated).  The
 * methods are tried in the order listed in the file header.
 * Recursive (sums are split termwise), hence stack_check.
 *
 * Splitting f = f1 + f2 termwise is only valid when the partial sums
 * converge: for b = inf a split result that is infinite is only used when
 * f as a whole cannot be summed, so sum(1/k - 1/(k+1), k, 1, inf) telescopes
 * to 1 while sum(1/k + 1/2^k, k, 1, inf) is still inf.
 */
static E *sum_whole(E *f, E *k, E *a, E *b);

E *sum_expr(E *f, E *k, E *a, E *b) {
    stack_check();
    long lo, hi;
    if (small_range(a, b, &lo, &hi)) {
        if (hi < lo) return K->c0;
        return direct_sum(f, k, lo, hi, 0);
    }
    if (is_zero(f)) return K->c0; /* also over infinite ranges (0 * inf) */
    if (freeof(f, k)) return s_mul2(f, s_add2(s_sub(b, a), K->c1));
    /* linearity: sum each term; if any term fails, retry f as a whole */
    E *split = NULL;
    if (f->t == E_ADD) {
        E **t = salloc(sizeof(E *) * f->n);
        uint32_t i;
        for (i = 0; i < f->n; i++) {
            t[i] = sum_expr(EK(f)[i], k, a, b);
            if (!t[i]) break;
        }
        if (i == f->n) {
            split = s_add(f->n, t);
            /* divergent pieces may cancel: retry f as a whole first */
            if (!is_inf(b) || (freeof_sym(split, S_INF) && freeof_sym(split, S_UNDEF))) return split;
        }
    }
    E *w = sum_whole(f, k, a, b);
    if (w) return w;
    /* a definite +-inf from the pieces stands (inf + finite); undef or a
     * mixture does not */
    if (split && (ISYM(split, S_INF) || expr_eq(split, s_neg(K->inf_)))) return split;
    return NULL;
}

/* sum_expr without the linearity split (methods 3-7 of the file header) */
static E *sum_whole(E *f, E *k, E *a, E *b) {
    {
        E *bs = sum_binomial(f, k, a, b);
        if (bs) return bs;
    }
    /* exponential series: (constants) * c^k / k! from 0 to inf = exp(c).
     * Only recognised when f is a product, so a bare 1/k! is not matched. */
    if (is_inf(b) && is_zero(a) && f->t == E_MUL) {
        E *c = NULL;
        int ok = 1, hasfact = 0;
        E **rest = salloc(sizeof(E *) * f->n);
        uint32_t nr = 0;
        for (uint32_t i = 0; i < f->n && ok; i++) {
            E *t = EK(f)[i];
            if (t->t == E_POW && IFN(K0(t), F_FACT) && expr_eq(K0(K0(t)), k) && is_int_val(K1(t), -1)) hasfact = 1;
            else if (t->t == E_POW && freeof(K0(t), k) && expr_eq(K1(t), k)) c = c ? s_mul2(c, K0(t)) : K0(t);
            else if (freeof(t, k)) rest[nr++] = t;
            else ok = 0;
        }
        if (ok && hasfact) return s_mul2(s_mul(nr, rest), s_exp(c ? c : K->c1));
    }
    /* polynomial * geometric: with the antidifference F,
     *   sum_{k=a}^{b} f = F(b+1) - F(a),
     * and for b = inf the limit of F(k) as k -> inf replaces F(b+1): it is 0
     * when |r| < 1 (so the sum is -F(a)); |r| >= 1 with r != 1 diverges or
     * oscillates (NULL); r = 1 (polynomial) diverges to +-inf. */
    E *P, *r;
    if (poly_geo(f, k, &P, &r) && !is_zero(P)) {
        E *F = indef_poly_geo(P, r, k);
        if (F) {
            if (is_inf(b)) {
                double re, im;
                if (!is_one(r) && !is_const_expr(r)) /* symbolic ratio: formal sum, assumes |r| < 1 */
                    return simplify_full(rat_normal(s_neg(subst(F, k, a))));
                if (!is_one(r) && is_const_expr(r) && eval_complex(r, &re, &im) && re * re + im * im < 1 - 1e-12)
                    return rat_normal(s_neg(subst(F, k, a)));
                if (!is_one(r) && is_const_expr(r) && eval_complex(r, &re, &im) && re * re + im * im >= 1)
                    return NULL;
                /* r == 1, or r numeric but not evaluable: ask the limit
                 * engine; only a plain +inf (divergence) or a finite limit
                 * is accepted */
                E *lim = limit_expr(F, k, K->inf_, 0);
                if (!lim) return NULL;
                if (!freeof_sym(lim, S_INF) || !freeof_sym(lim, S_UNDEF)) {
                    if (ISYM(lim, S_INF)) return lim;
                    return NULL;
                }
                return rat_normal(s_sub(lim, subst(F, k, a)));
            }
            /* finite b: telescoped difference; polynomial sums (r = 1) are
             * returned factored, e.g. n(n+1)/2 */
            E *res = s_sub(subst(F, k, s_add2(b, K->c1)), subst(F, k, a));
            return is_one(r) ? factor_expr(expand(res)) : simplify_full(res);
        }
    }
    /* r^k * R(k), R with simple poles at integer shifts, b = inf: logarithmic
     * series.  f is split into cst * r^k (all factors base^(d k + b0)) times
     * the rest R; partial fractions of R must give only terms c/(lc(k+al))
     * with integer al and start = a + al in [1, 1000].  Convergence needs
     * |r| <= 1, r != 1 (|r| = 1 converges conditionally for these terms);
     * a symbolic r is accepted formally (|r| < 1 assumed). */
    if (is_inf(b)) {
        E **fs = f->t == E_MUL ? EK(f) : &f;
        uint32_t nfs = f->t == E_MUL ? f->n : 1;
        E *r = K->c1, *cst = K->c1;
        E **rest = salloc(sizeof(E *) * (nfs + 1));
        uint32_t nr = 0;
        int ok = 1;
        for (uint32_t i = 0; i < nfs && ok; i++) {
            E *t = fs[i];
            if (t->t == E_POW && freeof(K0(t), k) && !freeof(K1(t), k)) {
                E *d = diff(K1(t), k);
                if (!freeof(d, k)) ok = 0;
                else {
                    r = s_mul2(r, s_pow(K0(t), d));
                    cst = s_mul2(cst, s_pow(K0(t), expand(s_sub(K1(t), s_mul2(d, k)))));
                }
            } else
                rest[nr++] = t;
        }
        double rre, rim;
        int conv = 0;
        if (ok && !is_one(r)) {
            if (!is_const_expr(r)) conv = 1; /* symbolic ratio: formal series, assumes |r| < 1 */
            else if (eval_complex(r, &rre, &rim) && rre * rre + rim * rim <= 1 + 1e-12 &&
                     !(fabs(rre - 1) < 1e-12 && fabs(rim) < 1e-12))
                conv = 1;
        }
        if (conv) {
            E *R = s_mul(nr, rest);
            E *pf = partfrac(R, k);
            E **ts = pf->t == E_ADD ? EK(pf) : &pf;
            uint32_t nt = pf->t == E_ADD ? pf->n : 1;
            E **out = salloc(sizeof(E *) * (nt + 1));
            uint32_t no = 0;
            int good = 1;
            for (uint32_t i = 0; i < nt && good; i++) {
                E *nu, *de;
                numden(ts[i], &nu, &de);
                long dd;
                if (!freeof(nu, k) || !poly_degree_in(de, k, &dd) || dd != 1) {
                    good = 0;
                    break;
                }
                E *lc = poly_coeff(de, k, 1);
                E *al = rat_normal(s_div(poly_coeff(de, k, 0), lc));
                long av, lo;
                E *start = rat_normal(s_add2(a, al));
                if (!num_is_small_int(al, &av) || !num_is_small_int(start, &lo) || lo < 1 || lo > 1000) {
                    good = 0;
                    break;
                }
                /* sum_{k>=a} r^k/(k+al) = r^-al * (-ln(1-r) - sum_{j=1}^{lo-1} r^j/j) */
                E *v = s_neg(s_ln(s_sub(K->c1, r)));
                for (long j = 1; j < lo; j++) v = s_sub(v, s_div(s_powi(r, j), mk_int(j)));
                out[no++] = s_mul(3, (E *[]){s_div(nu, lc), s_pow(r, s_neg(al)), v});
            }
            if (good) return simplify_full(s_mul2(cst, s_add(no, out)));
        }
    }
    /* rational in k.  A pole k = r in [a, b] makes the sum undefined (as for
     * an explicit sum).  For b = inf: deg num >= deg den means the terms do
     * not tend to 0 (NULL); deg num = deg den - 1 means harmonic-like
     * divergence to +-inf with the sign of the leading coefficient ratio. */
    E *num, *den;
    numden(f, &num, &den);
    long dn, dd;
    if (poly_degree_in(num, k, &dn) && poly_degree_in(den, k, &dd) && dd > 0) {
        int pole = pole_in_range(den, k, a, b);
        if (pole > 0) cas_throw(CAS_ERR_DOMAIN, "division by zero");
        if (pole < 0) return NULL;
        if (is_inf(b) && dn >= dd) return NULL;
        if (is_inf(b) && dn == dd - 1) {
            double lc;
            if (!to_double(s_div(poly_coeff(num, k, dn), poly_coeff(den, k, dd)), &lc) || lc == 0) return NULL;
            return lc > 0 ? K->inf_ : s_neg(K->inf_);
        }
        E *r2 = sum_rational(f, k, a, b);
        if (r2) return rat_normal(r2);
    }
    return NULL;
}

/*
 * simplify_factorials -- cancel factorial ratios with integer offsets:
 * (n+3)!/(n+1)! -> (n+2)(n+3).
 * Looks in the product e for a factor u!^p (p > 0 integer) and a factor
 * v!^-q (q > 0 integer) with d = u - v a small integer (|d| <= 200).  With
 * t = min(p, q), (u!/v!)^t is replaced by the explicit product
 * u (u-1) ... (v+1) (or the reciprocal of v (v-1) ... (u+1) when d < 0),
 * the exponents become p - t and -(q - t), and the search restarts on the
 * re-simplified product until no pair is left.  The result is put into
 * rational normal form and factored.  Non-products are returned unchanged.
 */
static E *simplify_factorials(E *e) {
    if (e->t != E_MUL) return e;
    E **fs = salloc(sizeof(E *) * e->n);
    uint32_t n = e->n;
    memcpy(fs, EK(e), sizeof(E *) * n);
    int changed = 1;
    while (changed) {
        changed = 0;
        for (uint32_t i = 0; i < n && !changed; i++) {
            E *fi = fs[i];
            E *bi = fi->t == E_POW ? K0(fi) : fi, *ei = fi->t == E_POW ? K1(fi) : K->c1;
            if (!IFN(bi, F_FACT) || !is_exactnum(ei)) continue;
            for (uint32_t j = 0; j < n && !changed; j++) {
                if (j == i) continue;
                E *fj = fs[j];
                E *bj = fj->t == E_POW ? K0(fj) : fj, *ej = fj->t == E_POW ? K1(fj) : K->c1;
                if (!IFN(bj, F_FACT) || !is_exactnum(ej)) continue;
                if (num_sgn(ei) <= 0 || num_sgn(ej) >= 0 || ei->t != E_INT || ej->t != E_INT) continue;
                E *amt = num_cmp(ei, num_neg(ej)) < 0 ? ei : num_neg(ej);
                E *d = rat_normal(s_sub(K0(bi), K0(bj)));
                long dv;
                if (!num_is_small_int(d, &dv) || dv < -200 || dv > 200) continue;
                /* (u)!/(u-dv)! */
                E *prod = K->c1;
                if (dv >= 0)
                    for (long q = 0; q < dv; q++) prod = s_mul2(prod, s_sub(K0(bi), mk_int(q)));
                else {
                    for (long q = 0; q < -dv; q++) prod = s_mul2(prod, s_sub(K0(bj), mk_int(q)));
                    prod = s_inv(prod);
                }
                fs[i] = s_mul2(s_pow(bi, num_sub(ei, amt)), s_pow(prod, amt));
                fs[j] = s_pow(bj, num_add(ej, amt));
                /* flatten back into the factor list */
                E *nm = s_mul(n, fs);
                if (nm->t != E_MUL) return factor_expr(rat_normal(nm));
                n = nm->n;
                fs = salloc(sizeof(E *) * n);
                memcpy(fs, EK(nm), sizeof(E *) * n);
                changed = 1;
            }
        }
    }
    return factor_expr(rat_normal(s_mul(n, fs)));
}

/*
 * product_expr -- prod_{k=a}^{b} f(k); returns the closed form or NULL.
 * Methods in order: explicit product for small numeric ranges; constant
 * factor c^(b-a+1); k itself from 1 gives b!; c^g(k) -> c^(sum g); a
 * product is handled factor by factor (recursively) and the factorials
 * recombined by simplify_factorials; finally a rational f is factored over
 * Q in k and every linear factor lc*(k+al) with integer al becomes
 *   prod_{k=a}^{b} lc (k+al) = lc^(b-a+1) (b+al)! / (a+al-1)!,
 * raised to its multiplicity (numerator factors multiply, denominator
 * factors divide).  Nonlinear or non-integer-shifted factors give NULL.
 * A numerator factor vanishing at an integer in [a, b] gives 0 (also for a
 * symbolic b, assumed large enough), a denominator one CAS_ERR_DOMAIN.
 */
E *product_expr(E *f, E *k, E *a, E *b) {
    stack_check();
    long lo, hi;
    if (small_range(a, b, &lo, &hi)) {
        if (hi < lo) return K->c1;
        return direct_sum(f, k, lo, hi, 1);
    }
    if (freeof(f, k)) return s_pow(f, s_add2(s_sub(b, a), K->c1));
    if (expr_eq(f, k) && is_one(a)) return s_fn1(F_FACT, b);
    if (f->t == E_POW && freeof(K0(f), k)) {
        E *s = sum_expr(K1(f), k, a, b);
        if (s) return s_pow(K0(f), s);
    }
    if (f->t == E_MUL) {
        E **t = salloc(sizeof(E *) * f->n);
        int ok = 1;
        for (uint32_t i = 0; i < f->n && ok; i++) {
            t[i] = product_expr(EK(f)[i], k, a, b);
            if (!t[i]) ok = 0;
        }
        if (ok) return simplify_factorials(s_mul(f->n, t));
    }
    /* rational: prod (k + a_i)^e_i with integer a_i -> factorials, telescoped */
    E *num, *den;
    numden(f, &num, &den);
    E *cn, *cd;
    E *fln = factor_list_in(num, k, &cn), *fld = factor_list_in(den, k, &cd);
    E *res = s_pow(s_div(cn, cd), s_add2(s_sub(b, a), K->c1));
    for (int side = 0; side < 2; side++) {
        E *fl = side ? fld : fln;
        for (uint32_t i = 0; i < fl->n; i++) {
            E *g = K0(EK(fl)[i]);
            long m;
            num_is_small_int(K1(EK(fl)[i]), &m);
            long dd;
            if (freeof(g, k)) {
                E *pc = s_pow(g, s_mul2(mk_int(m), s_add2(s_sub(b, a), K->c1)));
                res = side ? s_div(res, pc) : s_mul2(res, pc);
                continue;
            }
            if (!poly_degree_in(g, k, &dd) || dd != 1) return NULL;
            E *lc = poly_coeff(g, k, 1);
            E *al = rat_normal(s_div(poly_coeff(g, k, 0), lc));
            long av;
            if (!num_is_small_int(al, &av)) return NULL;
            /* a zero k = -al in [a, b] (for a symbolic b: once b >= -al)
             * makes the product 0, or undefined in the denominator */
            int z = pole_in_range(g, k, a, b);
            if (z > 0 && !side) return K->c0;
            if (z < 0 && !side) return NULL; /* 0 only once b reaches the zero: no single closed form */
            if (z > 0) cas_throw(CAS_ERR_DOMAIN, "division by zero");
            if (z) return NULL;
            /* prod_{k=a}^{b} lc (k + al) = lc^(b-a+1) (b+al)! / (a+al-1)! */
            E *pc = s_mul2(s_pow(lc, s_add2(s_sub(b, a), K->c1)),
                           s_div(s_fn1(F_FACT, s_add2(b, al)), s_fn1(F_FACT, s_sub(s_add2(a, al), K->c1))));
            pc = s_pow(pc, mk_int(m));
            res = side ? s_div(res, pc) : s_mul2(res, pc);
        }
    }
    return simplify_factorials(res);
}
