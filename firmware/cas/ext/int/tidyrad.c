/*
 * tidyrad.c -- more cleanup of the casx integrator's antiderivatives (called by tidy.c, whose
 * size ranking and final check by differentiation apply to everything here):
 *   - ln(c u) -> ln(u) for a positive rational content c of u (ln(2 x + 2 sqrt(Q) + 4) ->
 *     ln(x + sqrt(Q) + 2)); equal logarithms then collect;
 *   - c ln(u) + c ln(v) -> c ln(u v) when u, v have irrational coefficients and u v expands to
 *     a smaller argument (ln(x^2 + sqrt(2) x + 1) + ln(x^2 - sqrt(2) x + 1) -> ln(x^4 + 1));
 *   - the algebraic terms (no function of x) with one square root kernel sqrt(Q), Q a
 *     polynomial (the Euler substitutions' t = x + sqrt(Q), (sqrt(Q) - sqrt(c))/x), are
 *     rationalised: with S = sqrt(Q), S^2 = Q, N(x, S)/D(x, S) = A(x) + B(x) S by the
 *     conjugate of the denominator;
 *   - a function term T whose derivative is k/sqrt(Q) for a constant k becomes k times the
 *     core's antiderivative of 1/sqrt(Q) (atan((sqrt(1 - x^2) - 1)/x) -> -asin(x)/2);
 *   - (a D + b)/D -> a + b/D: summands of a numerator that cancel the denominator are split
 *     off (the sec^3 antiderivative's logarithms);
 *   - exp(p + i q) -> exp(p) (cos q + i sin q) when the antiderivative of a real integrand has
 *     the imaginary unit only there and it cancels.
 */
#include "intx.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

static int is_node(E *e) { return e->t == E_ADD || e->t == E_MUL || e->t == E_POW || e->t == E_FN || e->t == E_LIST; }

static long sz(XP, E *e, long cap) {
    if (!is_node(e)) return 1;
    long n = 1;
    for (uint32_t i = 0; i < e->n && n <= cap; i++) n += sz(X, EK(e)[i], cap - n);
    return n;
}

/* ---- ln(c u) -> ln(u) ---- */

/* the numeric coefficient of a term (1 if none) */
static E *coef(E *t) {
    if (is_num(t)) return t;
    if (t->t == E_MUL && is_num(EK(t)[0])) return EK(t)[0];
    return NULL;
}

/* positive rational content of the sum s (gcd of the numerators / lcm of the denominators),
 * NULL if a coefficient is a float */
static E *content(XP, E *s) {
    E *g = K->c0, *l = K->c1;
    for (uint32_t i = 0; i < s->n; i++) {
        E *c = coef(EK(s)[i]);
        if (!c) c = K->c1;
        if (c->t == E_FLT) return NULL;
        E *nu = c->t == E_RAT ? K0(c) : c, *de = c->t == E_RAT ? K1(c) : K->c1;
        g = int_gcd(g, nu);
        E *gd = int_gcd(l, de);
        l = s_div(int_mul(l, de), gd);
    }
    if (is_zero(g)) return NULL;
    return s_div(num_sgn(g) < 0 ? s_neg(g) : g, l);
}

static E *div_terms(XP, E *s, E *c) {
    E **k = salloc(sizeof(E *) * s->n), *ci = s_inv(c);
    for (uint32_t i = 0; i < s->n; i++) k[i] = s_mul2(ci, EK(s)[i]);
    return s_add(s->n, k);
}

/* u without its positive constant factors, u itself when none */
static E *ln_arg(XP, E *u) {
    if (u->t == E_ADD) {
        E *c = content(X, u);
        return c && !expr_eq(c, K->c1) ? div_terms(X, u, c) : u;
    }
    if (u->t != E_MUL) return u;
    E **k = salloc(sizeof(E *) * u->n);
    int ch = 0;
    for (uint32_t i = 0; i < u->n; i++) {
        E *g = EK(u)[i];
        long p;
        k[i] = g;
        if (is_num(g) && g->t != E_FLT) { /* ln(-2 u) -> ln(u) (ln(-1) is constant too) */
            k[i] = K->c1, ch = 1;
        } else if (g->t == E_ADD || (g->t == E_POW && K0(g)->t == E_ADD && num_is_small_int(K1(g), &p))) {
            E *b = g->t == E_ADD ? g : K0(g), *c = content(X, b);
            if (c && !expr_eq(c, K->c1)) {
                b = div_terms(X, b, c);
                k[i] = g->t == E_ADD ? b : s_pow(b, K1(g)), ch = 1;
            }
        }
    }
    return ch ? s_mul(u->n, k) : u;
}

/* ---- c ln(u) + c ln(v) -> c ln(u v) ---- */

/* the ln factor of a term */
static E *ln_of(E *t) {
    if (IFN(t, F_LN)) return t;
    for (uint32_t i = 0; t->t == E_MUL && i < t->n; i++)
        if (IFN(EK(t)[i], F_LN)) return EK(t)[i];
    return NULL;
}

/* term = c ln(u), c free of x: c and u */
static int ln_term(XP, E *t, E *x, E **c, E **u) {
    E **fs = t->t == E_MUL ? EK(t) : &t;
    uint32_t m = t->t == E_MUL ? t->n : 1;
    E *cc = K->c1, *g = NULL;
    for (uint32_t i = 0; i < m; i++) {
        if (IFN(fs[i], F_LN) && !g && !freeof(fs[i], x)) g = fs[i];
        else if (freeof(fs[i], x)) cc = s_mul2(cc, fs[i]);
        else return 0;
    }
    if (!g) return 0;
    *c = cc, *u = K0(g);
    return 1;
}

/* the terms c ln(u) (c free of x: only there ln(k u) = ln(k) + ln(u) changes a constant):
 * ln(k u) -> ln(u) for a number k, ln(u^k) -> k ln(u) for an integer k */
static E *lnc(XP, E *e, E *x) {
    stack_check();
    E **ts = e->t == E_ADD ? EK(e) : &e;
    uint32_t n = e->t == E_ADD ? e->n : 1;
    E **k = salloc(sizeof(E *) * n), *c, *u;
    int ch = 0;
    for (uint32_t i = 0; i < n; i++) {
        k[i] = ts[i];
        if (ts[i]->t == E_MUL) { /* c (a ln(u) + b ln(v) + ...) */
            uint32_t j, ns = 0, js = 0;
            for (j = 0; j < ts[i]->n; j++)
                if (!freeof(EK(ts[i])[j], x)) ns++, js = j;
            if (ns == 1 && EK(ts[i])[js]->t == E_ADD) {
                E *g = lnc(X, EK(ts[i])[js], x);
                if (g != EK(ts[i])[js]) {
                    E **kk = salloc(sizeof(E *) * ts[i]->n);
                    for (j = 0; j < ts[i]->n; j++) kk[j] = j == js ? g : EK(ts[i])[j];
                    k[i] = s_mul(ts[i]->n, kk), ch = 1;
                }
                continue;
            }
        }
        if (!ln_term(X, ts[i], x, &c, &u)) continue;
        u = ln_arg(X, u);
        long pk;
        if (u->t == E_POW && num_is_small_int(K1(u), &pk) && pk >= -6 && pk <= 6) /* ln(u^k) -> k ln(u) */
            c = s_mul2(c, K1(u)), u = K0(u);
        if (u == K0(ln_of(ts[i])) || freeof(u, x)) continue;
        k[i] = s_mul2(c, s_fn1(F_LN, u)), ch = 1;
    }
    return ch ? s_add(n, k) : e;
}

/* e has a constant that is not a rational number (sqrt(2), ...) */
static int has_irr(XP, E *e, E *x) {
    if (e->t == E_POW && freeof(e, x) && !is_num(e)) return 1;
    if (e->t == E_SYM || !is_node(e)) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_irr(X, EK(e)[i], x)) return 1;
    return 0;
}

static E *ln_merge(XP, E *e, E *x) {
    if (e->t != E_ADD || e->n > 40) return e;
    E **t = salloc(sizeof(E *) * e->n), **c = salloc(sizeof(E *) * e->n), **u = salloc(sizeof(E *) * e->n);
    int ch = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        t[i] = EK(e)[i];
        if (!ln_term(X, t[i], x, &c[i], &u[i]) || sz(X, u[i], 300) > 200) u[i] = NULL;
    }
    for (uint32_t i = 0; i < e->n; i++)
        for (uint32_t j = i + 1; j < e->n && u[i]; j++) {
            if (!u[j]) continue;
            poll_intr();
            long s2 = sz(X, u[i], 1000) + sz(X, u[j], 1000);
            if (expr_eq(c[i], c[j])) { /* conjugate factors */
                if (!has_irr(X, u[i], x) || !has_irr(X, u[j], x)) continue;
                E *p = expand(s_mul2(u[i], u[j]));
                if (has_irr(X, p, x) || sz(X, p, 1000) >= s2) continue;
                u[i] = p, u[j] = NULL;
                t[i] = s_mul2(c[i], s_fn1(F_LN, p)), t[j] = K->c0;
                ch = 1;
            } else if (is_zero(s_add2(c[i], c[j]))) { /* ln(a w) - ln(b w) -> ln(a) - ln(b) */
                E *N, *D;
                X->numden(rat_normal(s_div(u[i], u[j])), &N, &D);
                N = ln_arg(X, N), D = ln_arg(X, D);
                if (freeof(N, x) || freeof(D, x) || sz(X, N, 1000) + sz(X, D, 1000) >= s2) continue;
                u[i] = N, u[j] = D;
                t[i] = s_mul2(c[i], s_fn1(F_LN, N)), t[j] = s_mul2(c[j], s_fn1(F_LN, D));
                ch = 1;
            }
        }
    return ch ? s_add(e->n, t) : e;
}

/* ---- rationalising the algebraic part ---- */

/* the bases Q[0..*nq) of the half-integer powers Q^(k/2) of e (polynomials in x, at most cap),
 * *bad when there are more or another radical or function of x occurs */
static void find_rads(XP, E *e, E *x, E **Q, int *nq, int cap, int *bad) {
    if (*bad || !is_node(e) || freeof(e, x)) return;
    stack_check();
    if (e->t == E_FN || e->t == E_LIST) {
        *bad = 1;
        return;
    }
    if (e->t == E_POW && !is_int(K1(e))) {
        long d;
        int i = 0;
        if (K1(e)->t != E_RAT || !num_is_small_int(K1(K1(e)), &d) || d != 2 || !ix_ratx(X, K0(e), x) ||
            !poly_degree_in(K0(e), x, &d) || d < 1) {
            *bad = 1;
            return;
        }
        while (i < *nq && !expr_eq(Q[i], K0(e))) i++;
        if (i == *nq) {
            if (*nq == cap) *bad = 1;
            else Q[(*nq)++] = K0(e);
        }
        return;
    }
    for (uint32_t i = 0; i < e->n; i++) find_rads(X, EK(e)[i], x, Q, nq, cap, bad);
}

static void find_rad(XP, E *e, E *x, E **Q, int *bad) {
    int nq = *Q != NULL;
    find_rads(X, e, x, Q, &nq, 1, bad);
}

/* Q^(k/2) -> S^k */
static E *put_s(XP, E *e, E *Q, E *S) {
    if (!is_node(e)) return e;
    if (e->t == E_POW && K1(e)->t == E_RAT && expr_eq(K0(e), Q)) return s_pow(S, K0(K1(e)));
    E **k = salloc(sizeof(E *) * e->n);
    int ch = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        k[i] = put_s(X, EK(e)[i], Q, S);
        ch |= k[i] != EK(e)[i];
    }
    return ch ? X->rebuild(e, k) : e;
}

/* p (a polynomial in S) with S^2 = Q: p0 + p1 S */
static void red_s(XP, E *p, E *S, E *Q, E **p0, E **p1) {
    long d;
    *p0 = *p1 = K->c0;
    if (!poly_degree_in(p, S, &d)) cas_throw(CAS_ERR_DOMAIN, "rad");
    for (long k = 0; k <= d; k++) {
        E *c = s_mul2(poly_coeff(p, S, k), s_powi(Q, k / 2));
        if (k & 1) *p1 = s_add2(*p1, c);
        else *p0 = s_add2(*p0, c);
    }
}

/* g rational in the symbol S (other symbols are coefficients) with S^2 = Q as A + B S */
int ix_kred(XP, E *g, E *S, E *Q, E **A, E **B) {
    E *N, *D, *n0, *n1, *d0, *d1;
    X->numden(rat_normal(g), &N, &D);
    N = expand(N), D = expand(D);
    red_s(X, N, S, Q, &n0, &n1);
    red_s(X, D, S, Q, &d0, &d1);
    E *den = expand(s_sub(s_mul2(d0, d0), s_mul(3, (E *[]){d1, d1, Q})));
    if (is_zero(den)) return 0;
    *A = rat_normal(s_div(s_sub(s_mul2(n0, d0), s_mul(3, (E *[]){n1, d1, Q})), den));
    *B = rat_normal(s_div(s_sub(s_mul2(n1, d0), s_mul2(n0, d1)), den));
    return 1;
}

/* e (rational in x and sqrt(Q)) as A + B sqrt(Q), NULL when that fails */
static E *rat_rad(XP, E *e, E *x, E *Q) {
    int fi = 0;
    E *S = ix_fresh(X, e, &fi), *A, *B;
    if (!ix_kred(X, put_s(X, e, Q, S), S, Q, &A, &B)) return NULL;
    long dg;
    if (poly_degree_in(A, x, &dg) && ix_ratx(X, A, x)) A = expand(A);
    /* B sqrt(Q), factor(B) sqrt(Q) ((x^2 + 1)^(3/2)/3), (B Q)/sqrt(Q) (-x/sqrt(1 - x^2)) */
    E *bs = s_mul2(B, s_sqrt(Q)), *bq = rat_normal(s_mul2(B, Q)), *bf = try_cmd(C_FACTOR, 1, &B);
    if (bf && sz(X, bf = s_mul2(bf, s_sqrt(Q)), 1000) < sz(X, bs, 1000)) bs = bf;
    if (sz(X, bq = s_mul2(bq, s_pow(Q, K->cmhalf)), 1000) < sz(X, bs, 1000)) bs = bq;
    bq = B;
    for (long k = 1; k <= 3; k++) /* c Q^k sqrt(Q) = c Q^(k + 1/2): -(4 - x^2)^(3/2)/3 */
        if (freeof(bq = rat_normal(s_div(bq, Q)), x)) {
            bs = s_mul2(bq, s_pow(Q, s_add2(mk_int(k), K->chalf)));
            break;
        }
    return s_add2(A, bs);
}

/* e (rational in x, sqrt(Q[0]), sqrt(Q[1])) as A0 + A1 sqrt(Q0) + (B0 + B1 sqrt(Q0)) sqrt(Q1) */
static E *rat_rad2(XP, E *e, E *x, E **Q) {
    int fi = 0;
    E *S0 = ix_fresh(X, e, &fi), *S1 = ix_fresh(X, e, &fi), *A, *B, *a0, *a1, *b0, *b1;
    E *g = put_s(X, put_s(X, e, Q[0], S0), Q[1], S1);
    if (!ix_kred(X, g, S1, Q[1], &A, &B) || !ix_kred(X, A, S0, Q[0], &a0, &a1) || !ix_kred(X, B, S0, Q[0], &b0, &b1))
        return NULL;
    E *s0 = s_sqrt(Q[0]), *s1 = s_sqrt(Q[1]);
    long dg;
    if (poly_degree_in(a0, x, &dg) && ix_ratx(X, a0, x)) a0 = expand(a0);
    return s_add(4, (E *[]){a0, s_mul2(a1, s0), s_mul2(b0, s1), s_mul(3, (E *[]){b1, s0, s1})});
}

/* the first function of x in e */
static E *first_fn(XP, E *e, E *x) {
    if (!is_node(e) || freeof(e, x)) return NULL;
    if (e->t == E_FN) return e;
    for (uint32_t i = 0; i < e->n; i++) {
        E *v = first_fn(X, EK(e)[i], x);
        if (v) return v;
    }
    return NULL;
}

static int alg_term(XP, E *t, E *x) {
    if (freeof(t, x)) return 1;
    if (t->t == E_FN || t->t == E_LIST) return 0;
    if (!is_node(t)) return 1;
    for (uint32_t i = 0; i < t->n; i++)
        if (!alg_term(X, EK(t)[i], x)) return 0;
    return 1;
}

/* t c (u1 + u2 + ...) with algebraic and other summands -> t c u1 + t c u2 + ... */
static E *distrib(XP, E *t, E *x) {
    if (t->t != E_MUL || alg_term(X, t, x)) return t;
    for (uint32_t i = 0; i < t->n; i++) {
        E *g = EK(t)[i];
        if (g->t != E_ADD || g->n > 20) continue;
        E *rest = K->c1, **k = salloc(sizeof(E *) * g->n);
        for (uint32_t j = 0; j < t->n; j++)
            if (j != i) rest = s_mul2(rest, EK(t)[j]);
        for (uint32_t j = 0; j < g->n; j++) k[j] = s_mul2(rest, EK(g)[j]);
        return s_add(g->n, k);
    }
    return t;
}

static E *rad_part(XP, E *e, E *x) {
    if (e->t == E_ADD && e->n <= 40) {
        E **k = salloc(sizeof(E *) * e->n);
        for (uint32_t i = 0; i < e->n; i++) k[i] = distrib(X, EK(e)[i], x);
        e = s_add(e->n, k);
    }
    E **ts = e->t == E_ADD ? EK(e) : &e;
    uint32_t n = e->t == E_ADD ? e->n : 1;
    E *alg = K->c0, *rest = K->c0;
    int bad = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (alg_term(X, ts[i], x)) alg = s_add2(alg, ts[i]);
        else rest = s_add2(rest, ts[i]);
    }
    E *Qs[2];
    int nq = 0;
    find_rads(X, alg, x, Qs, &nq, 2, &bad);
    if (bad || !nq || sz(X, alg, 800) > 600) return e;
    E *r = nq == 1 ? rat_rad(X, alg, x, Qs[0]) : rat_rad2(X, alg, x, Qs);
    return r ? s_add2(r, rest) : e;
}

/* the same with a function of x as the variable (sqrt(3 cos(x)^2 + 1) and cos(x)) */
static E *rad_kernel(XP, E *e, E *x) {
    E *v = first_fn(X, e, x);
    if (!v || IFN(v, F_LN) || IFN(v, F_ATAN) || IFN(v, F_ASIN) || IFN(v, F_ACOS)) return e;
    int fi = 0;
    E *y = ix_fresh(X, e, &fi), *g = subst(e, v, y);
    if (!freeof(g, x)) return e; /* x elsewhere too */
    E *r = rad_part(X, g, y);
    return r == g ? e : subst(r, y, v);
}

/* ---- T' = k / sqrt(Q) ---- */

/* the radicand of the first half-integer power of x in e */
static E *some_rad(XP, E *e, E *x) {
    if (!is_node(e) || freeof(e, x)) return NULL;
    if (e->t == E_POW && K1(e)->t == E_RAT && ix_ratx(X, K0(e), x)) {
        long d;
        if (num_is_small_int(K1(K1(e)), &d) && d == 2 && poly_degree_in(K0(e), x, &d) && d == 2) return K0(e);
    }
    for (uint32_t i = 0; i < e->n; i++) {
        E *q = some_rad(X, EK(e)[i], x);
        if (q) return q;
    }
    return NULL;
}

static E *fn_rad(XP, E *e, E *x) {
    if (e->t != E_ADD || e->n > 30) return e;
    E **t = salloc(sizeof(E *) * e->n), *J = NULL;
    int ch = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        t[i] = EK(e)[i];
        if (alg_term(X, t[i], x)) continue;
        E *Q = some_rad(X, t[i], x);
        if (!Q || sz(X, t[i], 200) > 120) continue;
        poll_intr();
        int bad = 0;
        E *q = NULL, *d = s_mul2(diff(t[i], x), s_sqrt(Q));
        find_rad(X, d, x, &q, &bad);
        if (bad || (q && !expr_eq(q, Q))) continue;
        E *k = q ? rat_rad(X, d, x, Q) : rat_normal(d);
        if (!k || !freeof(k, x)) continue;
        if (!J && !(J = ix_int(X, s_pow(Q, K->cmhalf), x))) return e;
        t[i] = s_mul2(k, J), ch = 1;
    }
    return ch ? s_add(e->n, t) : e;
}

/* ---- (a D + b)/D -> a + b/D ---- */

static int has_negpow(XP, E *e, E *b) {
    if (e->t == E_POW && expr_eq(K0(e), b) && is_num(K1(e)) && num_sgn(K1(e)) < 0) return 1;
    if (!is_node(e)) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_negpow(X, EK(e)[i], b)) return 1;
    return 0;
}

static E *split1(XP, E *e, E *x) {
    if (e->t != E_MUL || e->n > 20) return e;
    E *sum = NULL, *rest = K->c1;
    E **den = salloc(sizeof(E *) * e->n);
    uint32_t nd = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        E *g = EK(e)[i];
        if (g->t == E_ADD && !sum && !freeof(g, x)) sum = g;
        else {
            rest = s_mul2(rest, g);
            if (g->t == E_POW && is_num(K1(g)) && num_sgn(K1(g)) < 0 && !freeof(g, x)) den[nd++] = K0(g);
        }
    }
    if (!sum || !nd || sum->n > 40) return e;
    E *out = K->c0, *keep = K->c0;
    int ch = 0;
    for (uint32_t i = 0; i < sum->n; i++) {
        E *q = s_mul2(EK(sum)[i], rest);
        int free = 1;
        for (uint32_t j = 0; j < nd && free; j++) free = !has_negpow(X, q, den[j]);
        if (free) out = s_add2(out, q), ch = 1;
        else keep = s_add2(keep, EK(sum)[i]);
    }
    return ch ? s_add2(out, s_mul2(keep, rest)) : e;
}

static E *split(XP, E *e, E *x) {
    if (e->t != E_ADD) return split1(X, e, x);
    if (e->n > 40) return e;
    E **t = salloc(sizeof(E *) * e->n);
    int ch = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        t[i] = split1(X, EK(e)[i], x);
        ch |= t[i] != EK(e)[i];
    }
    return ch ? s_add(e->n, t) : e;
}

/* ---- exp(p + i q) -> exp(p) (cos q + i sin q) ---- */

static E *exp_re(XP, E *e, int *ch) {
    if (!is_node(e)) return e;
    stack_check();
    E **k = salloc(sizeof(E *) * e->n);
    int c0 = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        k[i] = exp_re(X, EK(e)[i], ch);
        c0 |= k[i] != EK(e)[i];
    }
    if (c0) e = X->rebuild(e, k);
    E *w = IFN(e, F_EXP) ? K0(e) : e->t == E_POW && ISYM(K0(e), S_E) ? K1(e) : NULL;
    if (!w || freeof(w, K->i_)) return e;
    w = expand(w);
    E *p = K->c0, *q = K->c0;
    E **ts = w->t == E_ADD ? EK(w) : &w;
    uint32_t n = w->t == E_ADD ? w->n : 1;
    for (uint32_t i = 0; i < n; i++) {
        if (freeof(ts[i], K->i_)) p = s_add2(p, ts[i]);
        else q = s_add2(q, ts[i]);
    }
    q = expand(s_div(q, K->i_));
    if (!freeof(q, K->i_)) return e;
    *ch = 1;
    return s_mul2(s_exp(p), s_add2(s_fn1(F_COS, q), s_mul2(K->i_, s_fn1(F_SIN, q))));
}

static E *real_exp(XP, E *e, E *f) {
    if (freeof(e, K->i_) || !freeof(f, K->i_)) return e;
    int ch = 0;
    E *r = exp_re(X, e, &ch);
    if (!ch) return e;
    r = expand(r);
    return freeof(r, K->i_) ? r : e;
}

/* ---- a e^u + b e^(-u) -> (a + b) cosh u + (a - b) sinh u (integrands with sinh, cosh, tanh) ---- */

static E *exp_of(E *e) { return IFN(e, F_EXP) ? K0(e) : e->t == E_POW && ISYM(K0(e), S_E) ? K1(e) : NULL; }

/* term = c e^w with c free of exponentials: c and w */
static int exp_term(XP, E *t, E *x, E **c, E **w) {
    E **fs = t->t == E_MUL ? EK(t) : &t;
    uint32_t m = t->t == E_MUL ? t->n : 1;
    E *cc = K->c1, *ww = NULL;
    for (uint32_t i = 0; i < m; i++) {
        E *v = exp_of(fs[i]);
        if (v && !ww && !freeof(v, x)) ww = v;
        else if (ix_has_fn(X, fs[i], F_EXP) || !freeof(fs[i], K->e_)) return 0;
        else cc = s_mul2(cc, fs[i]);
    }
    if (!ww) return 0;
    *c = cc, *w = ww;
    return 1;
}

static int has_hypf(XP, E *f) { return ix_has_fn(X, f, F_SINH) || ix_has_fn(X, f, F_COSH) || ix_has_fn(X, f, F_TANH); }

static E *exp_hyp(XP, E *e, E *x) {
    if (e->t != E_ADD || e->n > 40) return e;
    E **t = salloc(sizeof(E *) * e->n), **c = salloc(sizeof(E *) * e->n), **w = salloc(sizeof(E *) * e->n);
    int ch = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        t[i] = EK(e)[i];
        if (!exp_term(X, t[i], x, &c[i], &w[i])) w[i] = NULL;
    }
    for (uint32_t i = 0; i < e->n; i++)
        for (uint32_t j = i + 1; j < e->n && w[i]; j++) {
            if (!w[j] || !is_zero(expand(s_add2(w[i], w[j])))) continue;
            E *u = w[i];
            if (u->t == E_MUL && is_num(EK(u)[0]) && num_sgn(EK(u)[0]) < 0) { /* u = -v: swap */
                E *tc = c[i];
                c[i] = c[j], c[j] = tc, u = w[j];
            }
            t[i] = s_add2(s_mul2(s_add2(c[i], c[j]), s_fn1(F_COSH, u)), s_mul2(s_sub(c[i], c[j]), s_fn1(F_SINH, u)));
            t[j] = K->c0, w[i] = w[j] = NULL;
            ch = 1;
        }
    return ch ? s_add(e->n, t) : e;
}

/* ---- p ln(u) + q ln(u) -> (p + q) ln(u): terms grouped by their one function factor ---- */

static E *fn_key(XP, E *t, E *x, E **c) {
    E **fs = t->t == E_MUL ? EK(t) : &t;
    uint32_t m = t->t == E_MUL ? t->n : 1;
    E *k = NULL, *cc = K->c1;
    for (uint32_t i = 0; i < m; i++) {
        E *g = fs[i];
        E *h = g->t == E_POW && is_int(K1(g)) ? K0(g) : g;
        if (!freeof(g, x) && (IFN(h, F_LN) || IFN(h, F_ATAN) || IFN(h, F_ASIN) || IFN(h, F_ACOS) ||
                              IFN(h, F_ASINH) || IFN(h, F_ACOSH) || IFN(h, F_ATANH))) {
            if (k) return NULL;
            k = g;
        } else if (!alg_term(X, g, x) || !freeof(g, K->e_))
            return NULL;
        else
            cc = s_mul2(cc, g);
    }
    *c = cc;
    return k;
}

static E *grp(XP, E *e, E *x) {
    if (e->t != E_ADD || e->n > 40) return e;
    E **t = salloc(sizeof(E *) * e->n), **c = salloc(sizeof(E *) * e->n), **k = salloc(sizeof(E *) * e->n);
    int ch = 0;
    for (uint32_t i = 0; i < e->n; i++) t[i] = EK(e)[i], k[i] = fn_key(X, t[i], x, &c[i]);
    for (uint32_t i = 0; i < e->n; i++) {
        if (!k[i]) continue;
        E *s = c[i];
        int n = 0;
        for (uint32_t j = i + 1; j < e->n; j++)
            if (k[j] && expr_eq(k[i], k[j])) s = s_add2(s, c[j]), t[j] = K->c0, k[j] = NULL, n++;
        if (n) t[i] = s_mul2(s, k[i]), ch = 1;
    }
    return ch ? s_add(e->n, t) : e;
}

/* ---- denominators factored: 1/(x^8 + 8 x^7 + ...) -> 1/(x + 1)^8 ---- */

static E *fden(XP, E *e, E *x) {
    if (!is_node(e) || freeof(e, x)) return e;
    stack_check();
    E **k = salloc(sizeof(E *) * e->n);
    int ch = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        k[i] = fden(X, EK(e)[i], x);
        ch |= k[i] != EK(e)[i];
    }
    if (ch) e = X->rebuild(e, k);
    if (e->t == E_POW && K0(e)->t == E_ADD && is_int(K1(e)) && num_sgn(K1(e)) < 0) {
        long s0 = sz(X, K0(e), 200);
        if (s0 >= 120) return e;
        poll_intr();
        char *m = K->s_top;
        E *b = K0(e), *f = try_cmd(C_FACTOR, 1, &b);
        if (f && sz(X, f, s0) < s0) return s_pow(f, K1(e));
        K->s_top = m;
    }
    return e;
}

/* ---- sqrt(-x^2/4 + 1) -> sqrt(4 - x^2)/2: radicands without a square rational content ---- */

static E *radc(XP, E *e, E *x) {
    if (!is_node(e) || freeof(e, x)) return e;
    stack_check();
    E **k = salloc(sizeof(E *) * e->n);
    int ch = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        k[i] = radc(X, EK(e)[i], x);
        ch |= k[i] != EK(e)[i];
    }
    if (ch) e = X->rebuild(e, k);
    if (e->t == E_POW && K0(e)->t == E_ADD && K1(e)->t == E_RAT) {
        E *c = content(X, K0(e)), *r;
        if (c && !expr_eq(c, K->c1) && is_num(r = s_sqrt(c)))
            return s_mul2(s_pow(c, K1(e)), s_pow(div_terms(X, K0(e), c), K1(e)));
    }
    return e;
}

/* the passes: 0 radc, 1 lnc, 2 ln_merge, 3 rad_part, 4 fn_rad, 5 lnc, 6 split, 7 exp_hyp, 8 fden,
 * 10 rad_kernel (ix_tidy_rad), 9 grp (ix_tidy_fin) */
static E *pass_i(XP, int i, E *t, E *x) {
    switch (i) {
    case 0: return radc(X, t, x);
    case 1:
    case 5: return lnc(X, t, x);
    case 2: return ln_merge(X, t, x);
    case 3: return rad_part(X, t, x);
    case 4: return fn_rad(X, t, x);
    case 6: return split(X, t, x);
    case 7: return exp_hyp(X, t, x);
    case 8: return fden(X, t, x);
    case 10: return rad_kernel(X, t, x);
    default: return grp(X, t, x);
    }
}

/* a[0] pass index, a[1] t, a[2] x (protected: an error keeps t) */
static E *pass_p(XP, void **a) { return pass_i(X, (int)(intptr_t)a[0], a[1], a[2]); }

/* the logarithms and fractions once more, for the forms tidy.c's candidates bring */
E *ix_tidy_fin(XP, E *t, E *x) {
    long s = sz(X, t, 100000);
    for (int i = 0; i < 4; i++) { /* split, lnc, ln_merge, grp */
        char *m = K->s_top;
        void *a[3] = {(void *)(intptr_t)(i == 0 ? 6 : i == 1 ? 1 : i == 2 ? 2 : 9), t, x};
        E *u = X->protect(pass_p, a);
        long su = !u || u == t ? s : sz(X, u, s + 1);
        if (u && u != t && su <= s) t = u, s = su;
        else K->s_top = m;
    }
    return t;
}

/* exp(p + i q) when the integrand f is real (that one may make t larger), then the passes in
 * turn, each kept when it does not make t larger; *real set when t is preferred also when it
 * is not smaller than the antiderivative tidy.c started from (real, integral radicands) */
E *ix_tidy_rad(XP, E *f, E *t, E *x, int *real) {
    long s = sz(X, t, 100000);
    E *r = real_exp(X, t, f);
    if (r != t && sz(X, r, 3 * s) <= 3 * s) t = r, *real = 1, s = sz(X, t, 100000);
    for (int j = 0; j < 10; j++) {
        int i = j == 4 ? 10 : j < 4 ? j : j - 1; /* rad_kernel after rad_part */
        if (i == 7 && (!has_hypf(X, f) || ix_has_fn(X, f, F_EXP) || !freeof(f, K->e_))) continue;
        char *m = K->s_top;
        void *a[3] = {(void *)(intptr_t)i, t, x};
        E *u = X->protect(pass_p, a);
        long su = !u || u == t ? s : sz(X, u, s + 1);
        if (u && u != t && su <= s) t = u, s = su, *real |= i == 0;
        else K->s_top = m;
    }
    return t;
}
