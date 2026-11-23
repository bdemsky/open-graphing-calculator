/*
 * inttrig.c -- R(sin w, cos w) (also tan, cot, sec, csc; and the hyperbolic functions) with
 * w = a x + b, R algebraic (radicals included): the classical substitutions that make it an
 * algebraic integrand in u, chosen by the symmetries of R (S = sin w, C = cos w):
 *   R(S, -C) = -R(S, C):   u = sin w,  R = C h(S, C^2),  dx = du/(a C),  C^2 = 1 - u^2
 *   R(-S, C) = -R(S, C):   u = cos w,  dx = -du/(a S),  S^2 = 1 - u^2
 *   R(-S, -C) = R(S, C):   u = tan w,  S^2 = u^2/(1+u^2), C^2 = 1/(1+u^2), S C = u/(1+u^2),
 *                          dx = du/(a (1 + u^2))
 *   otherwise:             u = sin w with C = K sqrt(1 - u^2), dx = du/(a K sqrt(1 - u^2)), and
 *                          u = cos w likewise, where K = cos w / sqrt(1 - sin(w)^2) = +-1 is
 *                          locally constant (a symbol while integrating, put back after), so
 *                          the result holds on every branch (radicals of a + a sin w, ...)
 * and the same with sinh, cosh (u = sinh w: C^2 = 1 + u^2; u = cosh w: S^2 = u^2 - 1;
 * u = tanh w: S^2 = u^2/(1-u^2), C^2 = 1/(1-u^2)).  Each R is rewritten in S and C, the
 * symmetry tested, and S, C replaced by the matching functions of u; the integral in u goes to
 * the core and this module (radicals: intrad.c), and u is put back.
 */
#include "intx.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

typedef struct {
    E *w, *x, *S, *C;
    int hyp, bad; /* -1 unknown, 0 trigonometric, 1 hyperbolic */
} tctx;

static int is_trig(E *e) {
    return IFN(e, F_SIN) || IFN(e, F_COS) || IFN(e, F_TAN) || IFN(e, F_COT) || IFN(e, F_SEC) || IFN(e, F_CSC);
}
static int is_hyp(E *e) { return IFN(e, F_SINH) || IFN(e, F_COSH) || IFN(e, F_TANH); }

/* e in S and C (all trig of the same argument w; nothing else depends on x) */
static E *to_sc(XP, tctx *T, E *e) {
    E *x = T->x;
    if (T->bad || freeof(e, x)) return e;
    stack_check();
    int tr = is_trig(e), hy = is_hyp(e);
    if (tr || hy) {
        if (T->hyp < 0) T->hyp = hy;
        if (T->hyp != hy || (T->w && !expr_eq(T->w, K0(e)))) {
            T->bad = 1;
            return e;
        }
        T->w = K0(e);
        switch (e->s) {
        case F_SIN: case F_SINH: return T->S;
        case F_COS: case F_COSH: return T->C;
        case F_TAN: case F_TANH: return s_div(T->S, T->C);
        case F_COT: return s_div(T->C, T->S);
        case F_SEC: return s_inv(T->C);
        case F_CSC: return s_inv(T->S);
        }
    }
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW) {
        T->bad = 1;
        return e;
    }
    E **k = salloc(sizeof(E *) * e->n);
    for (uint32_t i = 0; i < e->n; i++) k[i] = to_sc(X, T, EK(e)[i]);
    return X->rebuild(e, k);
}

static int same(XP, E *a, E *b) { return expr_eq(a, b) || is_zero(rat_normal(s_sub(a, b))); }

/* h(S, C) with S, C replaced; NULL if the square root r is left over */
static E *put(XP, E *h, E *S, E *C, E *sv, E *cv, E *r) {
    E *vs[2] = {S, C}, *vv[2] = {sv, cv};
    E *g = subst_many(h, 2, vs, vv);
    if (r && !freeof(g, r)) {
        g = rat_normal(g);
        if (!freeof(g, r)) return NULL;
    }
    return g;
}

E *ix_trigsub(XP, E *f, E *x) {
    tctx T;
    int fi = 0;
    T.x = x, T.w = NULL, T.hyp = -1, T.bad = 0;
    T.S = ix_fresh(X, f, &fi), T.C = ix_fresh(X, f, &fi);
    E *g = to_sc(X, &T, f);
    long d;
    if (T.bad || !T.w || !freeof(g, x) || !poly_degree_in(T.w, x, &d) || d != 1) return NULL;
    E *a = poly_coeff(T.w, x, 1), *u = ix_fresh(X, f, &fi), *S = T.S, *C = T.C;
    E *gSm = subst(g, S, s_neg(S)), *gCm = subst(g, C, s_neg(C)), *gm = subst(gSm, C, s_neg(C));
    E *one = K->c1, *u2 = s_mul2(u, u);
    for (int kind = 0; kind < 5; kind++) {
        E *h = NULL, *back = NULL, *kv = NULL, *ks = NULL;
        char *m = K->s_top;
        if (kind == 0 && same(X, gCm, s_neg(g))) { /* u = sin w (sinh w) */
            E *r = s_sqrt(T.hyp ? s_add2(one, u2) : s_sub(one, u2));
            h = put(X, s_div(g, s_mul2(a, C)), S, C, u, r, r);
            back = s_fn1(T.hyp ? F_SINH : F_SIN, T.w);
        } else if (kind == 1 && same(X, gSm, s_neg(g))) { /* u = cos w (cosh w) */
            E *r = s_sqrt(T.hyp ? s_sub(u2, one) : s_sub(one, u2));
            h = put(X, s_div(g, s_mul2(a, S)), S, C, r, u, r);
            if (h && !T.hyp) h = s_neg(h);
            back = s_fn1(T.hyp ? F_COSH : F_COS, T.w);
        } else if (kind == 2 && same(X, gm, g)) { /* u = tan w (tanh w) */
            E *q = T.hyp ? s_sub(one, u2) : s_add2(one, u2), *r = s_sqrt(q);
            h = put(X, s_div(g, s_mul2(a, q)), S, C, s_div(u, r), s_inv(r), r);
            back = s_fn1(T.hyp ? F_TANH : F_TAN, T.w);
        } else if (kind >= 3 && !(T.hyp && kind == 3)) {
            /* no symmetry: u = sin w (cos w) all the same, with C = K sqrt(1 - u^2) (S = ...) and
             * the locally constant K = cos w / sqrt(1 - sin(w)^2) = +-1 kept as a symbol */
            int sn = kind == 3;
            E *fn = s_fn1(sn ? (T.hyp ? F_SINH : F_SIN) : (T.hyp ? F_COSH : F_COS), T.w);
            E *other = s_fn1(sn ? (T.hyp ? F_COSH : F_COS) : (T.hyp ? F_SINH : F_SIN), T.w);
            E *q = T.hyp ? (sn ? s_add2(one, u2) : s_sub(u2, one)) : s_sub(one, u2), *r = s_sqrt(q);
            ks = ix_fresh(X, f, &fi);
            kv = s_div(other, s_sqrt(subst(q, u, fn)));
            E *Kr = s_mul2(ks, r);
            h = sn ? put(X, s_div(g, s_mul2(a, Kr)), S, C, u, Kr, NULL)
                   : put(X, s_div(g, s_mul2(a, Kr)), S, C, Kr, u, NULL);
            if (h && !sn && !T.hyp) h = s_neg(h);
            back = fn;
        }
        if (!h) {
            K->s_top = m;
            continue;
        }
        IXD("trigsub h", h);
        E *G = ix_int(X, h, u);
        if (!G && ks) G = ix_radsub(X, h, u);
        IXD("trigsub G", G);
        if (G) {
            G = subst(G, u, back);
            return ks ? subst(G, ks, kv) : G;
        }
        K->s_top = m;
    }
    return NULL;
}
