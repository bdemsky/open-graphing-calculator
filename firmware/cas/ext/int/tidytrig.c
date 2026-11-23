/*
 * tidytrig.c -- the trigonometric part of an antiderivative in a textbook form (called by
 * tidy.c, which keeps it only when it is smaller and checks it by differentiation).
 *
 * The terms that are rational functions of the trigonometric functions of one angle w (and
 * nothing else of x) are written with S = sin w, C = cos w as R(S, C).  When w = u/2 and R has
 * even degree (R(-S, -C) = R(S, C)), R is first a rational function of T = tan w and then, with
 * T = sin u/(1 + cos u), a rational function of sin u, cos u (the Weierstrass substitution's
 * results, (8 cos(x/2)^3 sin(x/2)/3 - ...)/(2 cos(x/2)^2 - 1)^5, become polynomials in tan x).
 * The candidates for R(S, C):
 *   - A(C) + B(C) S with S^2 = 1 - C^2 (powers of the cosine: sec^6 tan^3 -> 1/(8 cos^8) - ...);
 *   - A(S) + B(S) C with C^2 = 1 - S^2;
 *   - R_T(tan w) when R has even degree (sec^4 tan^2 -> tan^3/3 + tan^5/5);
 * each rational function of one variable expanded or split into partial fractions, whichever
 * is smaller; the smallest candidate is returned.
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

static int is_trig(E *e) {
    return e->t == E_FN && (e->s == F_SIN || e->s == F_COS || e->s == F_TAN || e->s == F_SEC || e->s == F_CSC ||
                            e->s == F_COT);
}

/* the one argument w of the trigonometric functions of e; *bad when there are two, another
 * function of x or x outside them */
static void trig_arg(XP, E *e, E *x, E **w, int *bad) {
    if (*bad || freeof(e, x)) return;
    if (is_trig(e)) {
        if (!*w) *w = K0(e);
        else if (!expr_eq(*w, K0(e))) *bad = 1;
        return;
    }
    if (e->t == E_FN || e->t == E_LIST || !is_node(e) || (e->t == E_POW && !is_int(K1(e)))) {
        *bad = 1;
        return;
    }
    stack_check();
    for (uint32_t i = 0; i < e->n; i++) trig_arg(X, EK(e)[i], x, w, bad);
}

/* sin w -> S, cos w -> C, tan w -> S/C, ... */
static E *to_sc(XP, E *e, E *S, E *C) {
    if (is_trig(e)) {
        switch (e->s) {
        case F_SIN: return S;
        case F_COS: return C;
        case F_TAN: return s_div(S, C);
        case F_SEC: return s_inv(C);
        case F_CSC: return s_inv(S);
        default: return s_div(C, S);
        }
    }
    if (!is_node(e)) return e;
    E **k = salloc(sizeof(E *) * e->n);
    for (uint32_t i = 0; i < e->n; i++) k[i] = to_sc(X, EK(e)[i], S, C);
    return X->rebuild(e, k);
}

/* r (rational in y): the smaller of its expansion and its partial fractions */
static E *best_rat(XP, E *r, E *y) {
    E *v[2] = {r, y}, *a = try_cmd(C_EXPAND, 1, &r), *p = try_cmd(C_PARTFRAC, 2, v);
    E *b = a ? a : r;
    if (p && sz(X, p, 2000) < sz(X, b, 2000)) b = p;
    return b;
}

/* no fractional power of anything with y */
static int no_rad(XP, E *e, E *y) {
    if (!is_node(e) || freeof(e, y)) return 1;
    if (e->t == E_POW && !is_int(K1(e))) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (!no_rad(X, EK(e)[i], y)) return 0;
    return 1;
}

/* R(S, C) as a rational function of T = tan w (S = T C, C^2 = 1/(1 + T^2)), NULL when R has
 * odd degree */
static E *to_tan(XP, E *R, E *S, E *C, E *T) {
    E *r = rat_normal(subst(R, S, s_mul2(T, C)));
    r = rat_normal(subst(r, C, s_pow(s_add2(K->c1, s_mul2(T, T)), K->cmhalf)));
    return no_rad(X, r, T) && freeof(r, C) ? r : NULL;
}

static E *keep_min(XP, E *best, E *c) {
    if (c && (!best || sz(X, c, 4000) < sz(X, best, 4000))) return c;
    return best;
}

/* candidate a[4] (0 cosine, 1 sine, 2 tangent, 3 one fraction) of R = a[0] in S = a[1], C = a[2], T = a[3] */
static E *form_k(XP, void **a) {
    E *R = a[0], *S = a[1], *C = a[2], *T = a[3], *A, *B;
    int k = (int)(intptr_t)a[4];
    if (k == 2) { /* R reduced to A(C) + B(C) S first */
        if (!ix_kred(X, R, S, s_sub(K->c1, s_mul2(C, C)), &A, &B)) return NULL;
        E *r = to_tan(X, s_add2(A, s_mul2(B, S)), S, C, T);
        return r ? best_rat(X, r, T) : NULL;
    }
    if (k == 1) {
        E *t = S;
        S = C, C = t;
    }
    if (!ix_kred(X, R, S, s_sub(K->c1, s_mul2(C, C)), &A, &B)) return NULL;
    A = best_rat(X, A, C);
    if (k == 3) { /* one fraction, without the constant of A: (1 + sin x)/cos x */
        if (is_zero(B)) return NULL;
        if (A->t == E_ADD) {
            E *a0 = K->c0;
            for (uint32_t i = 0; i < A->n; i++)
                if (!freeof(EK(A)[i], C)) a0 = s_add2(a0, EK(A)[i]);
            A = a0;
        }
        return rat_normal(s_add2(A, s_mul2(B, S)));
    }
    return s_add2(A, s_mul2(best_rat(X, B, C), S));
}

/* rest + the smallest form of R(S, C) at the angle w (see the top) */
static E *forms(XP, E *R, E *S, E *C, E *T, E *w, int top, E *rest) {
    E *best = NULL, *xs[3] = {S, C, T}, *vs[3] = {s_fn1(F_SIN, w), s_fn1(F_COS, w), s_fn1(F_TAN, w)};
    for (int k = 0; k < 3 + top; k++) {
        void *a[5] = {R, S, C, T, (void *)(intptr_t)k};
        char *m = K->s_top;
        E *c = X->protect(form_k, a);
        if (c) best = keep_min(X, best, s_add2(rest, c)); /* with the other terms (constants) */
        else K->s_top = m; /* out of memory: the scratch it used is free again */
    }
    return best ? subst_many(best, 3, xs, vs) : NULL;
}

static E *no_const(XP, E *t, E *x) {
    if (t->t != E_ADD) return t;
    E *s = K->c0;
    for (uint32_t i = 0; i < t->n; i++)
        if (!freeof(EK(t)[i], x)) s = s_add2(s, EK(t)[i]);
    return is_zero(s) ? t : s;
}

/* the trigonometric functions of e are tangents */
static int only_tan(XP, E *e, E *x) {
    if (is_trig(e)) return e->s == F_TAN;
    if (!is_node(e) || freeof(e, x)) return 1;
    for (uint32_t i = 0; i < e->n; i++)
        if (!only_tan(X, EK(e)[i], x)) return 0;
    return 1;
}

/* a trigonometric function of w in e */
static int has_arg(XP, E *e, E *w) {
    if (is_trig(e) && expr_eq(K0(e), w)) return 1;
    if (!is_node(e)) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_arg(X, EK(e)[i], w)) return 1;
    return 0;
}

/* hyperbolic functions as trigonometric ones of i w (back: the reverse; *n counts them) */
static E *hyp_sw(XP, E *e, int back, int *n) {
    if (!is_node(e)) return e;
    E **k = salloc(sizeof(E *) * e->n);
    int ch = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        k[i] = hyp_sw(X, EK(e)[i], back, n);
        ch |= k[i] != EK(e)[i];
    }
    if (ch) e = X->rebuild(e, k);
    if (e->t != E_FN) return e;
    E *i_ = K->i_, *a = K0(e);
    if (!back) {
        E *ia = s_mul2(i_, a);
        if (e->s == F_SINH) return (*n)++, s_mul2(s_neg(i_), s_fn1(F_SIN, ia));
        if (e->s == F_COSH) return (*n)++, s_fn1(F_COS, ia);
        if (e->s == F_TANH) return (*n)++, s_mul2(s_neg(i_), s_fn1(F_TAN, ia));
        if (is_trig(e) || ix_has_fn(X, e, F_EXP)) *n = -1000; /* trig and hyperbolic: left alone */
        return e;
    }
    if (!is_trig(e) || freeof(a, i_)) return e;
    a = s_div(a, i_);
    (*n)++;
    switch (e->s) {
    case F_SIN: return s_mul2(i_, s_fn1(F_SINH, a));
    case F_COS: return s_fn1(F_COSH, a);
    case F_TAN: return s_mul2(i_, s_fn1(F_TANH, a));
    case F_SEC: return s_inv(s_fn1(F_COSH, a));
    case F_CSC: return s_inv(s_mul2(i_, s_fn1(F_SINH, a)));
    default: return s_inv(s_mul2(i_, s_fn1(F_TANH, a)));
    }
}

E *ix_tidy_trig(XP, E *t, E *x, E *f, int *pref) {
    int nh = 0;
    if (freeof(t, K->i_)) { /* sinh, cosh, tanh through sin(i w), cos(i w), tan(i w) */
        E *h = hyp_sw(X, t, 0, &nh);
        if (nh > 0) {
            E *r = ix_tidy_trig(X, h, x, f, pref);
            if (r == h) return t;
            nh = 0;
            r = hyp_sw(X, r, 1, &nh);
            if (!freeof(r, K->i_) && !freeof(r = rat_normal(r), K->i_)) return t;
            return sz(X, r, 4000) <= sz(X, t, 4000) ? r : t;
        }
    }
    int top = pref != NULL; /* an antiderivative (constant terms dropped), not a logarithm's argument */
    int hp0 = 0;
    if (!top) pref = &hp0;
    E **ts = t->t == E_ADD ? EK(t) : &t;
    uint32_t n = t->t == E_ADD ? t->n : 1;
    E *w = NULL, *G = K->c0, *rest = K->c0;
    for (uint32_t i = 0; i < n; i++) {
        E *w0 = w;
        int bad = 0;
        trig_arg(X, ts[i], x, &w, &bad);
        if (bad || !w || freeof(ts[i], x)) {
            w = w0;
            rest = s_add2(rest, ts[i]);
        } else
            G = s_add2(G, ts[i]);
    }
    if (!w || is_zero(G) || sz(X, G, 1000) > 800) return t;
    int fi = 0;
    E *S = ix_fresh(X, t, &fi), *C = ix_fresh(X, t, &fi), *T = ix_fresh(X, t, &fi);
    E *best = NULL, *half = NULL, *R = NULL, *r = NULL, *w2 = expand(s_mul2(K->c2, w));
    if (only_tan(X, G, x)) /* a rational function of tan w (the Weierstrass substitution's results) */
        r = rat_normal(subst(G, s_fn1(F_TAN, w), T));
    else if (!freeof(R = rat_normal(to_sc(X, G, S, C)), x))
        return t;
    if (sz(X, w2, 100) < sz(X, w, 100)) { /* w = u/2: tan w = sin u/(1 + cos u) */
        E *A, *B, *cp = s_add2(K->c1, C); /* T^2 = (1 - cos u)/(1 + cos u) */
        if (!r) r = to_tan(X, R, S, C, T);
        if (r && ix_kred(X, r, T, s_div(s_sub(K->c1, C), cp), &A, &B))
            half = forms(X, s_add2(A, s_div(s_mul2(B, S), cp)), S, C, T, w2, top, rest);
    }
    if (!half || sz(X, G, 200) < 100) { /* the forms at the angle w itself (-1/tan(x/2)) */
        if (!R) R = rat_normal(to_sc(X, G, S, C));
        best = forms(X, R, S, C, T, w, top, rest);
    }
    if (top && best) best = no_const(X, best, x);
    if (top && half) half = no_const(X, half, x);
    long s0 = sz(X, t, 4000), sb = best ? sz(X, best, 4000) : s0;
    if (half) { /* the angle of the integrand rather than a half angle it does not have, unless larger */
        long sh = sz(X, half, 4000);
        if (sh < sb || (!has_arg(X, f, w) && sh <= (sb < s0 ? sb : s0))) {
            *pref |= sh >= s0;
            return half;
        }
    }
    return sb < s0 ? best : t;
}

/* ---- c1 ln(u1) + c2 ln(u2) + ... (c_i rational multiples of one c) -> c ln(u1^k1 u2^k2 ...) when
 * the argument then simplifies (2 ln(tan(x/2) + 1) - ln(tan(x/2)^2 + 1) -> ln(sin(x) + 1); also one
 * logarithm: ln(tan(x)^2 + 1) -> ln(1/cos(x)^2)) ---- */
E *ix_tidy_lncomb(XP, E *t, E *x, E *f) {
    if (t->t != E_ADD || t->n > 40) return t;
    E *c0 = NULL, *P = K->c1, *rest = K->c0, *L = K->c1, *sum = K->c0;
    E **c = salloc(sizeof(E *) * t->n), **u = salloc(sizeof(E *) * t->n);
    int nl = 0;
    for (uint32_t i = 0; i < t->n; i++) {
        E *g = EK(t)[i], *ln = NULL, *cc = K->c1;
        for (uint32_t j = 0; g->t == E_MUL && j < g->n; j++)
            if (IFN(EK(g)[j], F_LN) && !ln) ln = EK(g)[j];
            else cc = s_mul2(cc, EK(g)[j]);
        if (IFN(g, F_LN)) ln = g;
        E *q = ln && freeof(cc, x) ? (c0 ? rat_normal(s_div(cc, c0)) : K->c1) : NULL;
        if (!q || (q->t != E_INT && q->t != E_RAT)) {
            rest = s_add2(rest, g);
            continue;
        }
        if (!c0) c0 = cc;
        c[nl] = q, u[nl++] = K0(ln), sum = s_add2(sum, g);
        if (q->t == E_RAT) L = s_div(int_mul(L, K1(q)), int_gcd(L, K1(q)));
    }
    if (!nl) return t;
    for (int i = 0; i < nl; i++) {
        long k;
        if (!num_is_small_int(s_mul2(c[i], L), &k) || k > 6 || k < -6) return t;
        P = s_mul2(P, s_powi(u[i], k));
    }
    /* the argument simplified: by the trigonometric forms, through exponentials (hyperbolic
     * functions: (tanh(x) + 1)/(tanh(x) - 1) = -e^(2x)) or by cancellation */
    E *Pn = rat_normal(P), *Pt = ix_tidy_trig(X, Pn, x, f, NULL), *Ph = try_cmd(C_TRIG2EXP, 1, &Pn);
    long su = 0, sp = sz(X, Pn, 4000);
    if (Ph && sz(X, Ph = rat_normal(Ph), 4000) < sz(X, Pt, 4000)) Pt = Ph;
    for (int i = 0; i < nl; i++) su += sz(X, u[i], 4000);
    long st = sz(X, Pt, 4000); /* a sum that became a product splits into logarithms of its factors */
    if (st >= sp && (nl < 2 || sp >= su) && !(st == sp && Pn->t == E_ADD && Pt->t != E_ADD)) return t;
    E *cl = s_div(c0, L); /* c ln(k u^j) = c j ln(u) + constant */
    long j;
    if (Pt->t == E_MUL && Pt->n == 2 && is_num(EK(Pt)[0])) Pt = EK(Pt)[1];
    if (Pt->t == E_POW && num_is_small_int(K1(Pt), &j)) cl = s_mul2(cl, K1(Pt)), Pt = K0(Pt);
    E *r = s_add2(rest, s_mul2(cl, s_fn1(F_LN, Pt)));
    return sz(X, r, 4000) < sz(X, t, 4000) ? r : t;
}
