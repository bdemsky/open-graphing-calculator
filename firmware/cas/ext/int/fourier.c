/*
 * fourier.c -- fourier(f, x, z) of the casx module (cas/ext/int: not in the core tier's flash).
 */
#include "casx_mod.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

E *casx_laplace(XP, E *f, E *t, E *s); /* cas/ext/adv/lapl.c */

static E *simpl(XP, E *e) { return eval_cmd(C_SIMPLIFY, 1, &e); }

/* ---- fourier(f, x, z) = 1/sqrt(2 pi) int f(x) e^(-i z x) dx (unitary convention) ----
 *   f free of x: f sqrt(2 pi) dirac(z);
 *   P(x) e^(-a x^2 + b x + c): (i d/dz)^k of sqrt(pi/a) e^(c + (b - i z)^2/(4 a)) for x^k;
 *   otherwise f decaying at both ends (|f(+-30)|, |f(+-60)| small): the two half-line Laplace
 *   transforms, L[f(x)](i z) + L[f(-x)](-i z), with abs(x) -> x and sign(x) -> 1 on x > 0. */
static E *half(XP, E *e, E *x, int neg) {
    if (freeof(e, x)) return e;
    if (IFN(e, F_ABS) && expr_eq(K0(e), x)) return x;
    if (IFN(e, F_SIGN) && expr_eq(K0(e), x)) return K->c1;
    if (expr_eq(e, x)) return neg ? s_neg(x) : x;
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN) return e;
    E **k = salloc(sizeof(E *) * e->n);
    for (uint32_t i = 0; i < e->n; i++) k[i] = half(X, EK(e)[i], x, neg);
    return X->rebuild(e, k);
}

static int decays(XP, E *f, E *x) {
    const int16_t pts[4] = {30, -30, 60, -60};
    for (int i = 0; i < 4; i++) {
        double re, im;
        E *v = subst(f, x, mk_int(pts[i]));
        if (!X->eval_complex(v, &re, &im)) return 0;
        if (!(re * re + im * im < 1e-12)) return 0;
    }
    return 1;
}

static E *fourier(XP, E *f, E *x, E *z) {
    if (x->t != E_SYM) cas_throw(CAS_ERR_DOMAIN, "fourier: expected a variable");
    E *r2p = s_sqrt(s_mul2(K->c2, K->pi_));
    if (freeof(f, x)) return s_mul(3, (E *[]){f, r2p, s_fn1(F_DIRAC, z)});
    /* Gaussian */
    E *g = expand(f);
    E **ts = g->t == E_ADD ? EK(g) : &g;
    uint32_t nt = g->t == E_ADD ? g->n : 1;
    E *res = K->c0;
    int gauss = 1;
    for (uint32_t i = 0; i < nt && gauss; i++) {
        E *t = ts[i], *P = K->c1, *q = NULL;
        E **fs = t->t == E_MUL ? EK(t) : &t;
        uint32_t m = t->t == E_MUL ? t->n : 1;
        for (uint32_t j = 0; j < m; j++) {
            E *h = fs[j];
            if (!q && h->t == E_POW && ISYM(K0(h), S_E) && !freeof(K1(h), x)) q = K1(h);
            else if (!q && IFN(h, F_EXP) && !freeof(K0(h), x)) q = K0(h);
            else P = s_mul2(P, h);
        }
        long d, kp;
        if (!q || !poly_degree_in(q, x, &d) || d != 2 || !poly_degree_in(P, x, &kp) || kp > 8) {
            gauss = 0;
            break;
        }
        E *a = s_neg(poly_coeff(q, x, 2)), *b = poly_coeff(q, x, 1), *c = poly_coeff(q, x, 0);
        if (X->sign_of(a) <= 0) {
            gauss = 0;
            break;
        }
        E *bz = s_sub(b, s_mul2(K->i_, z));
        E *G = s_mul2(s_sqrt(s_div(K->pi_, a)), s_exp(s_add2(c, s_div(s_mul2(bz, bz), s_mul2(mk_int(4), a)))));
        for (long k = 0; k <= kp; k++) {
            E *ck = poly_coeff(P, x, k);
            if (!is_zero(ck)) res = s_add2(res, s_mul2(ck, G));
            G = s_mul2(K->i_, diff(G, z));
        }
    }
    if (!gauss) {
        if (!decays(X, f, x)) cas_throw(CAS_ERR_UNSUPPORTED, "fourier: transform not found");
        E *s = mk_sym_name("_fs");
        E *Lp = casx_laplace(X, half(X, f, x, 0), x, s), *Lm = casx_laplace(X, half(X, f, x, 1), x, s);
        res = s_add2(subst(Lp, s, s_mul2(K->i_, z)), subst(Lm, s, s_neg(s_mul2(K->i_, z))));
    }
    return simpl(X, s_div(res, r2p));
}

static const casx_cmd_t ftab[] = {{"fourier", 3, 3}};

static E *fcmd(XP, int op, uint32_t n, E **a) { return fourier(X, a[0], a[1], a[2]); }

int casx_fourier_register(XP) { return X->reg("casx", ftab, 1, fcmd); }
