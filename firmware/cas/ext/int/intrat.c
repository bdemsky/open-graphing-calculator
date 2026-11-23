/*
 * intrat.c -- rational integrands with symbolic coefficients whose denominator has a factor
 * the core cannot split over Q(parameters):
 *   binomial   a x^k + b (k >= 2, b/a not a number): x = rho u with rho^k = +-b/a turns it into
 *              (b)(u^k +- 1), whose factors over Q the core finds;
 *   trinomial  a x^(2k) + b x^k + c (squarefree factor): with the roots s1, s2 of a s^2 + b s + c
 *              (sqrt of the discriminant),  1/(a x^(2k) + b x^k + c) =
 *              (1/(x^k - s1) - 1/(x^k - s2)) / (a (s1 - s2)),  and each part again.
 */
#include "intx.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

/* f is a polynomial in x with exactly the powers 0 and k (k >= 2), or 0, k, 2k: kind 1 / 2 */
static int shape(XP, E *f, E *x, long *k, E **c) {
    long d;
    if (!poly_degree_in(f, x, &d) || d < 2 || d > 64) return 0;
    long nz = 0, pw[3] = {0, 0, 0};
    for (long j = 0; j <= d; j++) {
        E *cj = poly_coeff(f, x, j);
        if (is_zero(cj)) continue;
        if (nz == 3) return 0;
        c[nz] = cj, pw[nz++] = j;
    }
    if (pw[0] != 0) return 0;
    if (nz == 2) {
        *k = pw[1];
        return 1;
    }
    if (nz == 3 && pw[2] == 2 * pw[1]) {
        *k = pw[1];
        return 2;
    }
    return 0;
}

/* e "looks negative": a negative number, or a product / sum led by one */
static int looks_neg(XP, E *e) {
    if (is_num(e)) return num_sgn(e) < 0;
    if (e->t == E_MUL || e->t == E_ADD) return looks_neg(X, EK(e)[0]);
    return 0;
}

E *ix_ratbin(XP, E *f, E *x) {
    if (!ix_ratx(X, f, x)) return NULL;
    E *N, *D;
    X->numden(rat_normal(f), &N, &D);
    if (freeof(D, x)) return NULL;
    E *cont, *L = X->factor_list_in(D, x, &cont);
    if (!L || L->t != E_LIST) return NULL;
    for (uint32_t i = 0; i < L->n; i++) {
        E *p = EK(L)[i], *fb = EK(p)[0], *c[3];
        long k, m;
        int kind = shape(X, fb, x, &k, c);
        if (!kind || !num_is_small_int(EK(p)[1], &m)) continue;
        if (kind == 1) {
            E *r = rat_normal(s_div(c[0], c[1]));
            if (is_num(r)) continue;
            int neg = looks_neg(X, r);
            E *rho = s_pow(neg ? rat_normal(s_neg(r)) : r, mk_frac(1, k));
            int fi = 0;
            E *u = ix_fresh(X, f, &fi);
            E *g = rat_normal(s_mul2(subst(f, x, s_mul2(rho, u)), rho));
            E *G = ix_int(X, g, u);
            if (G) return subst(G, u, s_div(x, rho));
            continue;
        }
        if (m != 1) continue;
        /* trinomial: a s^2 + b s + c, s = x^k */
        E *a = c[2], *b = c[1], *c0 = c[0];
        E *disc = rat_normal(s_sub(s_mul2(b, b), s_mul(3, (E *[]){mk_int(4), a, c0})));
        if (is_zero(disc)) continue;
        E *sq = s_sqrt(disc);
        E *s1 = s_div(s_add2(s_neg(b), sq), s_mul2(K->c2, a)), *s2 = s_div(s_sub(s_neg(b), sq), s_mul2(K->c2, a));
        E *xk = s_powi(x, k);
        E *rest = rat_normal(s_div(f, s_inv(fb))); /* f * fb */
        E *g1 = s_div(rest, s_mul(3, (E *[]){a, s_sub(s1, s2), s_sub(xk, s1)}));
        E *g2 = s_div(rest, s_mul(3, (E *[]){a, s_sub(s1, s2), s_sub(xk, s2)}));
        E *G1 = ix_int(X, g1, x);
        IXD("ratbin g1", g1);
        IXD("ratbin G1", G1);
        if (!G1) G1 = ix_ratbin(X, g1, x);
        if (!G1) continue;
        E *G2 = ix_int(X, g2, x);
        IXD("ratbin g2", g2);
        IXD("ratbin G2", G2);
        if (!G2) G2 = ix_ratbin(X, g2, x);
        if (!G2) continue;
        return s_sub(G1, G2);
    }
    return NULL;
}
