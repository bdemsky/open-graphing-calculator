/*
 * intinv.c -- integrands with one inverse trigonometric or hyperbolic function of a linear
 * argument, F(w), w = c x + d, F in {asin, acos, atan, asinh, acosh, atanh}, the rest algebraic
 * in x:  u = F(w),  x = (h(u) - d)/c with h = F^-1,  dx = h'(u)/c du.
 * The radicals must be powers of the matching quadratic Q up to a factor free of x, and are
 * then exact functions of u on the range of F:
 *   asin   Q = 1 - w^2,  Q^p = cos(u)^(2p)        acos   Q = 1 - w^2,  Q^p = sin(u)^(2p)
 *   atan   Q = 1 + w^2,  Q^p = cos(u)^(-2p)       asinh  Q = 1 + w^2,  Q^p = cosh(u)^(2p)
 *   atanh  Q = 1 - w^2,  Q^p = cosh(u)^(-2p)      acosh  Q = w^2 - 1,  Q^p = sinh(u)^(2p),
 *          (w - 1)^p = 2^p sinh(u/2)^(2p), (w + 1)^p = 2^p cosh(u/2)^(2p)
 * (B^p = K Q^p with the locally constant K = B^p / Q^p, kept as a constant).  The integral in
 * u (polynomials and powers of u times trigonometric / hyperbolic polynomials) goes to the core
 * and to intsf.c; then u = F(w).
 */
#include "intx.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

static int is_inv(E *e) {
    return IFN(e, F_ASIN) || IFN(e, F_ACOS) || IFN(e, F_ATAN) || IFN(e, F_ASINH) || IFN(e, F_ACOSH) ||
           IFN(e, F_ATANH);
}

/* the single inverse function node of e in x (NULL: none; *bad: two different) */
static void find_inv(XP, E *e, E *x, E **g, int *bad) {
    if (*bad || freeof(e, x)) return;
    if (is_inv(e)) {
        if (*g && !expr_eq(*g, e)) *bad = 1;
        else *g = e;
        return;
    }
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN) return;
    stack_check();
    for (uint32_t i = 0; i < e->n; i++) find_inv(X, EK(e)[i], x, g, bad);
}

typedef struct {
    int F;
    E *x, *w, *u, *g, *xu, *f0;
    E *ks[8], *kv[8];
    uint32_t nk;
    int fi, bad;
} ictx;

/* Q^p in u for the quadratic of F (which = 0), or w - 1 / w + 1 (which = 1, 2; acosh) */
static E *qpow(XP, ictx *C, int which, E *p) {
    E *u = C->u, *p2 = s_mul2(K->c2, p);
    switch (C->F) {
    case F_ASIN: return s_pow(s_fn1(F_COS, u), p2);
    case F_ACOS: return s_pow(s_fn1(F_SIN, u), p2);
    case F_ATAN: return s_pow(s_fn1(F_COS, u), s_neg(p2));
    case F_ASINH: return s_pow(s_fn1(F_COSH, u), p2);
    case F_ATANH: return s_pow(s_fn1(F_COSH, u), s_neg(p2));
    case F_ACOSH:
        if (!which) return s_pow(s_fn1(F_SINH, u), p2);
        return s_mul2(s_pow(K->c2, p), s_pow(s_fn1(which == 1 ? F_SINH : F_COSH, s_div(u, K->c2)), p2));
    }
    return NULL;
}

static E *isub(XP, ictx *C, E *e) {
    E *x = C->x;
    if (C->bad || freeof(e, x)) return e;
    stack_check();
    if (expr_eq(e, C->g)) return C->u;
    if (expr_eq(e, x)) return C->xu;
    if (e->t == E_POW && !is_int(K1(e)) && freeof(K1(e), x)) {
        E *B = K0(e), *p = K1(e), *w = C->w;
        if (!ix_ratx(X, B, x)) {
            C->bad = 1;
            return e;
        }
        E *Q[3] = {C->F == F_ATAN || C->F == F_ASINH ? s_add2(K->c1, s_mul2(w, w))
                   : C->F == F_ACOSH                ? s_sub(s_mul2(w, w), K->c1)
                                                    : s_sub(K->c1, s_mul2(w, w)),
                   s_sub(w, K->c1), s_add2(w, K->c1)};
        for (int j = 0; j < (C->F == F_ACOSH ? 3 : 1); j++) {
            E *r = rat_normal(s_div(B, Q[j]));
            if (!freeof(r, x)) continue;
            E *kv = s_div(e, s_pow(Q[j], p)), *ks = NULL;
            if (!expr_eq(kv, K->c1)) {
                if (C->nk == 8) break;
                ks = ix_fresh(X, C->f0, &C->fi);
                C->ks[C->nk] = ks, C->kv[C->nk++] = kv;
            }
            E *v = qpow(X, C, j, p);
            return ks ? s_mul2(ks, v) : v;
        }
        C->bad = 1;
        return e;
    }
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW) {
        C->bad = 1;
        return e;
    }
    if (e->t == E_POW && is_int(K1(e)) && num_sgn(K1(e)) < 0 && !freeof(K0(e), x) && !expr_eq(K0(e), x) &&
        freeof(K0(e), C->g)) {
        /* a denominator: only x^k, w -+ 1 and the quadratic Q of F (others make long searches) */
        E *B = K0(e), *w = C->w, *ok[3] = {s_sub(K->c1, s_mul2(w, w)), s_sub(w, K->c1), s_add2(w, K->c1)};
        if (C->F == F_ATAN || C->F == F_ASINH) ok[0] = s_add2(K->c1, s_mul2(w, w));
        int good = 0;
        for (int j = 0; j < 3 && !good; j++) good = freeof(rat_normal(s_div(B, ok[j])), x);
        if (!good) {
            E *n0, *d0;
            X->numden(rat_normal(B), &n0, &d0);
            E *cn, *L = X->factor_list_in(n0, x, &cn);
            good = L && L->t == E_LIST && L->n == 1 && expr_eq(EK(EK(L)[0])[0], x);
        }
        if (!good) {
            C->bad = 1;
            return e;
        }
    }
    E **k = salloc(sizeof(E *) * e->n);
    for (uint32_t i = 0; i < e->n; i++) k[i] = isub(X, C, EK(e)[i]);
    return X->rebuild(e, k);
}

E *ix_invsub(XP, E *f, E *x) {
    E *g = NULL;
    int bad = 0;
    find_inv(X, f, x, &g, &bad);
    if (!g || bad) return NULL;
    long dg;
    E *w = K0(g);
    if (!poly_degree_in(w, x, &dg) || dg != 1) return NULL;
    ictx *C = salloc(sizeof(ictx));
    C->F = g->s, C->x = x, C->w = w, C->g = g, C->f0 = f, C->nk = 0, C->fi = 0, C->bad = 0;
    C->u = ix_fresh(X, f, &C->fi);
    E *c = poly_coeff(w, x, 1), *d = poly_coeff(w, x, 0);
    int h = C->F == F_ASIN ? F_SIN : C->F == F_ACOS ? F_COS : C->F == F_ATAN ? F_TAN
          : C->F == F_ASINH ? F_SINH : C->F == F_ACOSH ? F_COSH : F_TANH;
    E *hu = s_fn1(h, C->u);
    C->xu = s_div(s_sub(hu, d), c);
    E *gu = isub(X, C, f);
    if (C->bad || !freeof(gu, x)) return NULL;
    E *dh = diff(hu, C->u);
    gu = s_mul2(gu, s_div(dh, c));
    E *G = ix_int(X, gu, C->u);
    if (!G) {
        E *a1[1] = {gu};
        E *gl = try_cmd(C_TLIN, 1, a1);
        if (gl) G = ix_int(X, expand(gl), C->u);
        if (!G && gl) G = ix_special(X, expand(gl), C->u);
        if (!G && h != F_SIN && h != F_COS && h != F_TAN) {
            E *ge = try_cmd(C_TRIG2EXP, 1, a1);
            if (ge) G = ix_special(X, expand(ge), C->u);
        }
    }
    IXD("invsub gu", gu);
    IXD("invsub G", G);
    if (!G) return NULL;
    G = subst(G, C->u, g);
    for (uint32_t i = 0; i < C->nk; i++) G = subst(G, C->ks[i], C->kv[i]);
    return G;
}
