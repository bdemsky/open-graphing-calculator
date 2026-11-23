/*
 * inthyp.c -- antiderivatives in terms of uppergamma and hyp2f1 (the last strategy of the
 * casx integrator, for integrands that have no elementary antiderivative it can find):
 *   (al + be x)^q e^(b x + d)   (q not a nonnegative integer; sin, cos, sinh, cosh of linear
 *                                arguments through exponentials):
 *        y = al + be x, k = b/be:  int y^q e^(k y) dy = -y^(q+1) (-k y)^(-q-1) Gamma(q+1, -k y);
 *   x^m (a + b x^n)^p           (p not an integer, m != -1):
 *        x^(m+1)/(m+1) (a + b x^n)^p (1 + b x^n/a)^(-p) 2F1(-p, (m+1)/n; (m+1)/n + 1; -b x^n/a);
 *   (a + b x)^m (c + d x)^n     (m, n not integers):
 *        (a+bx)^(m+1)/(b (m+1)) (c+dx)^n (b (c+dx)/(b c - a d))^(-n)
 *          2F1(-n, m+1; m+2; -d (a+bx)/(b c - a d)).
 * The factors (u)^p (1 + ...)^(-p) are locally constant (a^p up to a branch).  (e x)^m is
 * written as G x^m with the locally constant G = (e x)^m x^(-m).  The caller checks the result.
 */
#include "intx.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

static int lincoef(XP, E *u, E *x, E **a, E **b) {
    long d;
    if (!poly_degree_in(u, x, &d) || d != 1) return 0;
    *a = poly_coeff(u, x, 0), *b = poly_coeff(u, x, 1);
    return freeof(*a, x) && freeof(*b, x);
}

static int nonint(XP, E *p) {
    long n;
    return !num_is_small_int(p, &n);
}

/* one term c y^q e^w */
static E *gam_term(XP, E *t, E *x) {
    E **fs = t->t == E_MUL ? EK(t) : &t;
    uint32_t n = t->t == E_MUL ? t->n : 1;
    E *c = K->c1, *y = NULL, *q = NULL, *w = NULL;
    for (uint32_t i = 0; i < n; i++) {
        E *g = fs[i], *a, *b;
        if (freeof(g, x)) c = s_mul2(c, g);
        else if (!w && g->t == E_POW && ISYM(K0(g), S_E) && lincoef(X, K1(g), x, &a, &b)) w = K1(g);
        else if (!w && IFN(g, F_EXP) && lincoef(X, K0(g), x, &a, &b)) w = K0(g);
        else if (g->t == E_POW && freeof(K1(g), x) && K0(g)->t == E_MUL && K0(g)->n == 2 &&
                 freeof(EK(K0(g))[0], x) && expr_eq(EK(K0(g))[1], x) && (!y || expr_eq(y, x))) {
            /* (c x)^q = G x^q, G = (c x)^q x^(-q) locally constant */
            c = s_mul2(c, s_mul2(g, s_pow(x, s_neg(K1(g)))));
            y = x, q = q ? s_add2(q, K1(g)) : K1(g);
        } else if (g->t == E_POW && freeof(K1(g), x) && lincoef(X, K0(g), x, &a, &b) && (!y || expr_eq(y, K0(g))))
            q = y ? s_add2(q, K1(g)) : K1(g), y = K0(g);
        else if (lincoef(X, g, x, &a, &b) && (!y || expr_eq(y, g)))
            q = y ? s_add2(q, K->c1) : K->c1, y = g;
        else return NULL;
    }
    if (!w) return NULL;
    long qi;
    if (!y || (num_is_small_int(q, &qi) && qi >= 0)) return ix_int(X, t, x); /* elementary */
    E *al, *be, *b0, *b1;
    lincoef(X, y, x, &al, &be);
    lincoef(X, w, x, &b0, &b1);
    E *k = s_div(b1, be), *q1 = s_add2(q, K->c1), *mky = s_neg(s_mul2(k, y));
    E *G = s_fn2(mk_sym_name("uppergamma")->s, q1, mky);
    E *pre = s_mul2(c, s_div(s_exp(s_sub(b0, s_div(s_mul2(b1, al), be))), be));
    return s_mul(4, (E *[]){s_neg(pre), s_pow(y, q1), s_pow(mky, s_neg(q1)), G});
}

static int has_tfn(XP, E *e) {
    if (IFN(e, F_SIN) || IFN(e, F_COS) || IFN(e, F_SINH) || IFN(e, F_COSH)) return 1;
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_tfn(X, EK(e)[i])) return 1;
    return 0;
}

static E *ix_gamma(XP, E *f, E *x) {
    E *g = f;
    if (has_tfn(X, f)) {
        E *a1[1] = {f};
        if (!(g = try_cmd(C_TRIG2EXP, 1, a1))) return NULL;
    }
    g = expand(g);
    E **ts = g->t == E_ADD ? EK(g) : &g;
    uint32_t n = g->t == E_ADD ? g->n : 1;
    if (n > 12) return NULL;
    E *r = K->c0;
    int any = 0;
    for (uint32_t i = 0; i < n; i++) {
        E *t = gam_term(X, ts[i], x);
        if (!t) return NULL;
        any |= ix_has_fn(X, t, mk_sym_name("uppergamma")->s);
        r = s_add2(r, t);
    }
    return any ? r : NULL;
}

/* B = a + b x^n: a, b, n */
static int binom(XP, E *B, E *x, E **a, E **b, E **n) {
    if (B->t != E_ADD) return 0;
    E *a0 = K->c0, *t = NULL;
    for (uint32_t i = 0; i < B->n; i++) {
        if (freeof(EK(B)[i], x)) a0 = s_add2(a0, EK(B)[i]);
        else if (!t) t = EK(B)[i];
        else return 0;
    }
    if (!t || is_zero(a0)) return 0;
    E *nn = rat_normal(s_div(s_mul2(x, diff(t, x)), t));
    if (!freeof(nn, x) || is_zero(nn)) return 0;
    E *bb = rat_normal(s_div(t, s_pow(x, nn)));
    if (!freeof(bb, x)) return 0;
    *a = a0, *b = bb, *n = nn;
    return 1;
}

static E *ix_binom(XP, E *f, E *x) {
    E **fs = f->t == E_MUL ? EK(f) : &f;
    uint32_t nf = f->t == E_MUL ? f->n : 1;
    E *c = K->c1, *m = K->c0, *B = NULL, *p = NULL, *B2 = NULL, *p2 = NULL;
    for (uint32_t i = 0; i < nf; i++) {
        E *g = fs[i], *a, *b, *nn;
        if (freeof(g, x)) c = s_mul2(c, g);
        else if (expr_eq(g, x)) m = s_add2(m, K->c1);
        else if (g->t == E_POW && expr_eq(K0(g), x) && freeof(K1(g), x)) m = s_add2(m, K1(g));
        else if (g->t == E_POW && K0(g)->t == E_MUL && freeof(K1(g), x) && K0(g)->n == 2 &&
                 (expr_eq(EK(K0(g))[1], x) || expr_eq(EK(K0(g))[0], x))) { /* (e x)^q = G x^q */
            c = s_mul2(c, s_mul2(g, s_pow(x, s_neg(K1(g)))));
            m = s_add2(m, K1(g));
        } else if (!B && g->t == E_POW && freeof(K1(g), x) && nonint(X, K1(g)) && binom(X, K0(g), x, &a, &b, &nn))
            B = K0(g), p = K1(g);
        else if (B && !B2 && g->t == E_POW && freeof(K1(g), x) && nonint(X, K1(g))) B2 = K0(g), p2 = K1(g);
        else return NULL;
    }
    if (!B) return NULL;
    E *a, *b, *nn;
    binom(X, B, x, &a, &b, &nn);
    if (B2) { /* (a + b x)^p (c + d x)^q, no x^m */
        E *a2, *b2;
        if (!is_zero(m) || !expr_eq(nn, K->c1) || !lincoef(X, B2, x, &a2, &b2)) return NULL;
        E *D = rat_normal(s_sub(s_mul2(b, a2), s_mul2(a, b2))); /* b c - a d */
        if (is_zero(D)) return NULL;
        E *p1 = s_add2(p, K->c1);
        E *v[4] = {s_neg(p2), p1, s_add2(p1, K->c1), s_neg(s_div(s_mul2(b2, B), D))};
        E *F = mk_fn(mk_sym_name("hyp2f1")->s, 4, v);
        F->f = F_SIMP;
        return s_mul(6, (E *[]){c, s_pow(B, p1), s_inv(s_mul2(b, p1)), s_pow(B2, p2),
                                s_pow(s_div(s_mul2(b, B2), D), s_neg(p2)), F});
    }
    E *m1 = s_add2(m, K->c1);
    if (is_zero(m1)) return NULL;
    E *s = rat_normal(s_div(m1, nn)), *z = s_neg(s_div(s_mul2(b, s_pow(x, nn)), a));
    E *v[4] = {s_neg(p), s, s_add2(s, K->c1), z};
    E *F = mk_fn(mk_sym_name("hyp2f1")->s, 4, v);
    F->f = F_SIMP;
    return s_mul(5, (E *[]){c, s_div(s_pow(x, m1), m1), s_pow(B, p), s_pow(s_sub(K->c1, z), s_neg(p)), F});
}

E *ix_hyp(XP, E *f, E *x) {
    const char *xn = sym_name(x->s);
    if (xn[0] == '_') return NULL; /* not for the inner integral of a substitution (_xt.., _t..) */
    char *mk = K->s_top;
    E *r = ix_gamma(X, f, x);
    if (r && ix_verify(X, f, r, x)) return r;
    K->s_top = mk;
    r = ix_binom(X, f, x);
    if (r && ix_verify(X, f, r, x)) return r;
    K->s_top = mk;
    return NULL;
}
