/*
 * intsfn.c -- integrals of the special functions themselves (the casx functions of fnx.c and
 * sfn.c, and the core's Si, Ci, Ei, erf, erfc, erfi):
 *
 *   by parts       P(x) F(u)^k:  Q F^k - int Q k F^(k-1) F'(u) u'  with Q = int P (elementary),
 *                  the new integrand split into terms, each integrated the same way (at most
 *                  7 levels) or by the core.  F(u) = expint(n, a + b x) with a polynomial P is
 *                  integrated instead (int E_n(u) = -E_(n+1)(u)/b, P differentiated), and so is
 *                  polylog(s, c x^q) times G(x)/x with G free of x outside logarithms
 *                  (int Li_s(c x^q)/x = Li_(s+1)(c x^q)/q, G differentiated);
 *   lambertw       R(x, W(a + b x)) and R(x, W(b x^n)): u = W, x = (u e^u - a)/b
 *                  (dx = (1 + u) e^u/b du), or x^n = u e^u/b (dx = x (1 + u)/(n u) du); back
 *                  substituted with e^(k W(L)) = (L/W(L))^k.
 */
#include "intx.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

int fx_which(XP, E *e); /* fnx.c: polylog, Bessel functions */

enum { K_NONE, K_DIFF, K_EXPINT, K_POLYLOG };

/* the kind of special function e is (how it is integrated by parts) */
static int sp_kind(XP, E *e) {
    if (e->t != E_FN) return K_NONE;
    if (e->s == F_SI || e->s == F_CI || e->s == F_EI || e->s == F_ERF || e->s == F_ERFC || e->s == F_ERFI)
        return K_DIFF;
    if (e->s < S__COUNT) return K_NONE;
    int w = sf_which(X, e);
    long n;
    if (w == SF_EXPINT) return num_is_small_int(K0(e), &n) ? K_EXPINT : K_NONE;
    if (w >= 0 && w != SF_LAMBERTW) return K_DIFF;
    if (hy_which(X, e) == 0) return K_DIFF; /* uppergamma */
    if (fx_which(X, e) == 0 && e->n == 2 && num_is_small_int(K0(e), &n) && n >= 1 && n <= 12) return K_POLYLOG;
    return K_NONE;
}

/* the special-function calls of e: the first one in *F (NULL if none); 2 if another one has a
 * different argument list, else 1 */
static int find_sp(XP, E *e, E *x, E **F) {
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN && e->t != E_LIST) return 0;
    stack_check();
    if (sp_kind(X, e) != K_NONE && !freeof(e, x)) {
        if (!*F) *F = e;
        else if (!expr_eq(*F, e)) return 2;
        for (uint32_t i = 0; i < e->n; i++) /* nested: F(F(x)) */
            if (find_sp(X, EK(e)[i], x, F) == 2) return 2;
        return 1;
    }
    int r = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        int s = find_sp(X, EK(e)[i], x, F);
        if (s == 2) return 2;
        r |= s;
    }
    return r;
}

/* f = c * F^k * P with P free of F: k and P, else 0 */
static int split_pow(XP, E *f, E *F, long *k, E **P) {
    E **fs = f->t == E_MUL ? EK(f) : &f;
    uint32_t m = f->t == E_MUL ? f->n : 1;
    long kk = 0;
    E *p = K->c1;
    for (uint32_t i = 0; i < m; i++) {
        E *g = fs[i];
        long e;
        if (expr_eq(g, F)) kk += 1;
        else if (g->t == E_POW && expr_eq(K0(g), F) && num_is_small_int(K1(g), &e) && e >= 1) kk += e;
        else if (!freeof(g, F)) return 0;
        else p = s_mul2(p, g);
    }
    if (kk < 1 || kk > 6) return 0;
    *k = kk, *P = p;
    return 1;
}

/* ln(...) calls of x in e replaced by fresh symbols: then free of x? */
static int only_logs(XP, E *e, E *x) {
    if (freeof(e, x)) return 1;
    if (IFN(e, F_LN)) return 1;
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW) return 0;
    stack_check();
    for (uint32_t i = 0; i < e->n; i++)
        if (!only_logs(X, EK(e)[i], x)) return 0;
    return 1;
}

static E *ibp(XP, E *f, E *x, int depth);

/* int g dx, g a sum of terms: each by parts (when it has a special function) or by the core */
static E *int_terms(XP, E *g, E *x, int depth) {
    g = expand(g);
    E **ts = g->t == E_ADD ? EK(g) : &g;
    uint32_t m = g->t == E_ADD ? g->n : 1;
    if (m > 16) return NULL;
    E *r = K->c0;
    for (uint32_t i = 0; i < m; i++) {
        poll_intr();
        E *F = NULL, *t;
        if (freeof(ts[i], x)) t = s_mul2(ts[i], x);
        else if (find_sp(X, ts[i], x, &F)) t = ibp(X, ts[i], x, depth + 1);
        else t = ix_int(X, ts[i], x);
        if (!t) return NULL;
        r = s_add2(r, t);
    }
    return r;
}

static E *ibp(XP, E *f, E *x, int depth) {
    if (depth > 7) return NULL;
    E *F = NULL, *P, *Q;
    long k, s;
    if (find_sp(X, f, x, &F) != 1 || !split_pow(X, f, F, &k, &P)) return NULL;
    int kind = sp_kind(X, F);
    E *u = EK(F)[F->n - 1];
    if (kind == K_EXPINT && k == 1) { /* E_n(a + b x) P(x): int E_n = -E_(n+1)/b */
        long d;
        E *b;
        if (poly_degree_in(u, x, &d) && d == 1 && poly_degree_in(P, x, &d) && d >= 0 && d <= 12 &&
            freeof(b = poly_coeff(u, x, 1), x)) {
            E *H = s_neg(s_div(s_fn2(F->s, s_add2(K0(F), K->c1), u), b));
            E *rest = is_zero(diff(P, x)) ? K->c0 : int_terms(X, s_mul2(diff(P, x), H), x, depth);
            if (rest) return s_sub(s_mul2(P, H), rest);
        }
    }
    if (kind == K_POLYLOG && k == 1 && num_is_small_int(K0(F), &s)) { /* G/x Li_s(c x^q) */
        E *G = s_mul2(P, x), *q = NULL;
        if (only_logs(X, G, x)) { /* u = c x^q */
            E *lu = rat_normal(s_mul2(x, s_div(diff(u, x), u)));
            if (freeof(lu, x) && !is_zero(lu)) q = lu;
        }
        if (q) {
            E *H = s_div(s_fn2(F->s, mk_int(s + 1), u), q);
            E *dG = diff(G, x);
            E *rest = is_zero(dG) ? K->c0 : int_terms(X, s_mul2(dG, H), x, depth);
            if (rest) return s_sub(s_mul2(G, H), rest);
            return NULL;
        }
    }
    /* differentiate F^k: Q = int P */
    Q = freeof(P, x) ? s_mul2(P, x) : ix_int(X, P, x);
    if (!Q) return NULL;
    E *g = s_mul2(s_mul2(Q, mk_int(k)), s_mul2(k > 1 ? s_powi(F, k - 1) : K->c1, diff(F, x)));
    E *rest = int_terms(X, g, x, depth);
    if (!rest) return NULL;
    return s_sub(s_mul2(Q, s_powi(F, k)), rest);
}

/* ---- lambertw ---- */

static E *find_w(XP, E *e, E *x) {
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN && e->t != E_LIST) return NULL;
    if (e->t == E_FN && sf_which(X, e) == SF_LAMBERTW && !freeof(e, x)) return e;
    for (uint32_t i = 0; i < e->n; i++) {
        E *r = find_w(X, EK(e)[i], x);
        if (r) return r;
    }
    return NULL;
}

/* e^(k W) -> (L/W)^k for integers k (the exponent a sum: each term) */
static E *wback(XP, E *e, E *W) {
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN && e->t != E_LIST) return e;
    stack_check();
    E **k = salloc(sizeof(E *) * e->n);
    int ch = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        k[i] = wback(X, EK(e)[i], W);
        ch |= k[i] != EK(e)[i];
    }
    if (ch) e = X->rebuild(e, k);
    E *w = e->t == E_POW && ISYM(K0(e), S_E) ? K1(e) : IFN(e, F_EXP) ? K0(e) : NULL;
    if (!w) return e;
    E *c = rat_normal(s_div(w, W));
    long n;
    if (freeof(c, W) && num_is_small_int(c, &n) && n != 0 && n >= -12 && n <= 12)
        return s_powi(s_div(K0(W), W), n);
    if (w->t == E_ADD) { /* e^(a + k W) = e^a (L/W)^k */
        E *rest = K->c0, *kw = NULL;
        for (uint32_t i = 0; i < w->n; i++) {
            E *t = EK(w)[i], *ct = rat_normal(s_div(t, W));
            if (!kw && freeof(ct, W) && num_is_small_int(ct, &n) && n >= -12 && n <= 12) kw = t;
            else rest = s_add2(rest, t);
        }
        if (kw) {
            num_is_small_int(rat_normal(s_div(kw, W)), &n);
            return s_mul2(s_exp(rest), s_powi(s_div(K0(W), W), n));
        }
    }
    return e;
}

/* (c u)^p -> c^p u^p in e (u = W(L) > 0 where the integrand is checked) */
static E *udist(XP, E *e, E *u) {
    if (freeof(e, u) || (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN)) return e;
    stack_check();
    E **k = salloc(sizeof(E *) * e->n);
    int ch = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        k[i] = udist(X, EK(e)[i], u);
        ch |= k[i] != EK(e)[i];
    }
    if (ch) e = X->rebuild(e, k);
    if (e->t == E_POW && K0(e)->t == E_MUL) {
        E *b = K0(e), *r = K->c1;
        for (uint32_t i = 0; i < b->n; i++) r = s_mul2(r, s_pow(EK(b)[i], K1(e)));
        return r;
    }
    return e;
}

static E *ix_lambertw(XP, E *f, E *x) {
    E *W = find_w(X, f, x);
    if (!W) return NULL;
    E *L = K0(W), *a, *b, *n = NULL;
    long d;
    if (poly_degree_in(L, x, &d) && d == 1) {
        a = poly_coeff(L, x, 0), b = poly_coeff(L, x, 1);
    } else { /* b x^n */
        E *dl = rat_normal(s_mul2(x, s_div(diff(L, x), L)));
        if (!freeof(dl, x) || is_zero(dl)) return NULL;
        n = dl, a = K->c0, b = rat_normal(s_div(L, s_pow(x, n)));
        if (!freeof(b, x)) return NULL;
    }
    if (!freeof(b, x) || !freeof(a, x)) return NULL;
    int fi = 0;
    E *u = ix_fresh(X, f, &fi);
    E *f1 = udist(X, subst(f, W, u), u);
    if (find_w(X, f1, x)) return NULL; /* another W */
    E *eu = s_exp(u), *g;
    if (!n) { /* x = (u e^u - a)/b, dx = (1 + u) e^u/b du */
        E *xs = s_div(s_sub(s_mul2(u, eu), a), b);
        g = s_mul2(subst(f1, x, xs), s_div(s_mul2(s_add2(K->c1, u), eu), b));
    } else { /* x = (u e^u/b)^(1/n): x^m -> u^(m/n) e^(m u/n) b^(-m/n); dx = x (1 + u)/(n u) du */
        E *in = s_inv(n), *xs = s_mul2(s_mul2(s_pow(u, in), s_exp(s_mul2(in, u))), s_pow(b, s_neg(in)));
        g = s_mul2(subst(s_mul2(f1, x), x, xs), s_div(s_add2(K->c1, u), s_mul2(n, u)));
        g = expand(g);
    }
    if (!freeof(g, x)) return NULL;
    E *r = ix_int(X, g, u);
    if (!r) return NULL;
    return wback(X, subst(r, u, W), W);
}

E *ix_ibp(XP, E *f, E *x) { return ibp(X, f, x, 1); } /* intpl.c (not checked) */

E *ix_sfn(XP, E *f, E *x) {
    char *m = K->s_top;
    E *r = ix_lambertw(X, f, x);
    if (r && ix_verify(X, f, r, x)) return r;
    K->s_top = m;
    E *F = NULL;
    if (find_sp(X, f, x, &F) != 1) return NULL;
    r = ibp(X, f, x, 0);
    if (r && ix_verify(X, f, r, x)) return r;
    K->s_top = m;
    return NULL;
}
