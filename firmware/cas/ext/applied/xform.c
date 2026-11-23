/*
 * xform.c -- continuous Fourier and z transforms of the casx module (applied.h).
 *
 * fourier(f, x, w) = 1/sqrt(2 pi) int_{-inf}^{inf} f(x) e^(-i w x) dx  (unitary, angular frequency)
 * ifourier(F, w, x) = 1/sqrt(2 pi) int F(w) e^(i w x) dw = fourier(F, w, -x)
 * Rules, tried in order (G is the transform of g):
 *   constant c                  c sqrt(2 pi) dirac(w)
 *   sums, constant factors      linearity
 *   dirac(a x + b) g(x)         g(x0) e^(-i w x0)/(|a| sqrt(2 pi)), x0 = -b/a
 *   g(a x + b) (every abs, sign, heaviside argument the same a x + b)
 *                               e^(i w b/a) G(w/a)/|a|
 *   e^(i k x + c) g, cos(k x + c) g, sin(k x + c) g    modulation: G(w - k), G(w + k)
 *   x^k g (g not constant)      i^k d^k/dw^k G
 *   heaviside(x)                sqrt(pi/2) dirac(w) - i/(sqrt(2 pi) w); sign(x): -i sqrt(2/pi)/w
 *   heaviside(x) g, g decaying  L[g](i w)/sqrt(2 pi) (the Laplace transform, cas/ext/adv/lapl.c)
 *   P(x) e^(-a x^2 + b x + c)   (i d/dw)^k of sqrt(pi/a) e^(c + (b - i w)^2/(4 a)) for x^k (a > 0)
 *   N(x)/D(x), deg N < deg D, poles off the real axis found exactly: residues,
 *                               w > 0: -2 pi i sum_{Im p < 0} Res(f e^(-i w x)), w < 0: 2 pi i sum_{Im p > 0},
 *                               written with abs(w) / sign(w) when the halves match
 *   f decaying at +-inf (|f(+-30)|, |f(+-60)| < 1e-6): L[f(x)](i w) + L[f(-x)](-i w) with abs,
 *                               sign and heaviside of x resolved on x > 0
 * Afterwards dirac(w - k) h(w) -> dirac(w - k) h(k), and simplify.  No rule: "transform not found".
 *
 * ztrans(f, n, z) = sum_{n >= 0} f(n) z^(-n) (unilateral):
 *   constant c -> c z/(z - 1); linearity; q^n g(n) -> G(z/q) (q^n = b^(a n + c), e^(a n + c));
 *   n^k g(n) -> (-z d/dz)^k G(z); cos, sin(a n + b) -> z (z - cos a)/(z^2 - 2 z cos a + 1),
 *   z sin a/(...) by the addition theorem; cosh, sinh through exponentials; 1/n! -> e^(1/z);
 *   binomial(n, k) -> z/(z - 1)^(k + 1); otherwise the sum itself (the core's sum).
 * iztrans(F, z, n): F rational in z with deg num <= deg den: the partial fractions of F(z)/z from
 *   its poles p (found exactly, with multiplicities): z/(z - p)^j -> binomial(n, j - 1) p^(n - j + 1)
 *   (p != 0), z/z^j -> the unit impulse at n = j - 1, written piecewise(n = j - 1, 1, 0); for a real F
 *   a pair of complex poles r e^(+-i t) gives 2 r^n (Re(A) cos(n t) - Im(A) sin(n t)).
 */
#include "applied.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

E *casx_laplace(XP, E *f, E *t, E *s); /* cas/ext/adv/lapl.c */

static E *simpl(XP, E *e) { return eval_cmd(C_SIMPLIFY, 1, &e); }

/* u = a x + b with a != 0, a and b free of x */
static int lin(XP, E *u, E *x, E **a, E **b) {
    long d;
    if (!poly_degree_in(u, x, &d) || d != 1) return 0;
    *a = poly_coeff(u, x, 1), *b = poly_coeff(u, x, 0);
    return !is_zero(*a) && freeof(*a, x) && freeof(*b, x);
}

static E *r2p(XP) { return s_sqrt(s_mul2(K->c2, K->pi_)); }

/* the factors of a product (or e itself) */
static E **facs(XP, E *e, uint32_t *n) {
    if (e->t == E_MUL) {
        *n = e->n;
        return EK(e);
    }
    E **v = salloc(sizeof(E *));
    v[0] = e, *n = 1;
    return v;
}

static E *prod_but(XP, E **f, uint32_t n, uint32_t skip) {
    E **v = salloc(sizeof(E *) * (n + 1));
    uint32_t k = 0;
    for (uint32_t i = 0; i < n; i++)
        if (i != skip) v[k++] = f[i];
    return k ? s_mul(k, v) : K->c1;
}

/* |f| < 1e-3 at x = +-30, +-60 (one side: sides = 1 for x > 0 only); with parameters the
 * limits at +-inf must be 0 (the core's limit, which knows assume(a > 0)) */
static int decays(XP, E *f, E *x, int sides) {
    static const int16_t pts[4] = {30, 60, -30, -60};
    for (int i = 0; i < 2 * sides; i++) {
        double re, im;
        E *v = subst(f, x, mk_int(pts[i]));
        if (!X->eval_complex(v, &re, &im)) {
            if (is_const_expr(v)) return 0;
            E *a[3] = {f, x, i < 2 ? K->inf_ : s_neg(K->inf_)}, *l = try_cmd(C_LIMIT, 3, a);
            if (!l || !is_zero(l)) return 0;
            i |= 1;
            continue;
        }
        if (!(re * re + im * im < 1e-6)) return 0;
    }
    return 1;
}

/* f on x > 0 (neg: f(-x) on x > 0): abs, sign, heaviside of x resolved */
static E *half(XP, E *e, E *x, int neg) {
    stack_check();
    if (freeof(e, x)) return e;
    if (expr_eq(e, x)) return neg ? s_neg(x) : x;
    if (IFN(e, F_ABS) && expr_eq(K0(e), x)) return x;
    if (IFN(e, F_SIGN) && expr_eq(K0(e), x)) return neg ? K->cm1 : K->c1;
    if (IFN(e, F_HEAVISIDE) && expr_eq(K0(e), x)) return neg ? K->c0 : K->c1;
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN) return e;
    E **k = salloc(sizeof(E *) * e->n);
    for (uint32_t i = 0; i < e->n; i++) k[i] = half(X, EK(e)[i], x, neg);
    E *r = X->rebuild(e, k), *a, *b;
    /* heaviside(c - a x), a > 0 (a window): 1 - heaviside(a x - c), which the Laplace rules know */
    if (IFN(r, F_HEAVISIDE) && r->n == 1 && lin(X, K0(r), x, &a, &b) && X->sign_of(a) < 0)
        return s_sub(K->c1, s_fn1(F_HEAVISIDE, s_neg(K0(r))));
    return r;
}

static E *lap_p(XP, void **a) { return casx_laplace(X, (E *)a[0], (E *)a[1], (E *)a[2]); }
/* the Laplace transform at s, NULL if not found */
static E *lap(XP, E *f, E *x, E *s) {
    void *a[3] = {f, x, s};
    return X->protect(lap_p, a);
}

/* the arguments of abs, sign, heaviside, dirac containing x: 1 if all equal (*u), 0 if none,
 * -1 if several differ */
static int args_of(XP, E *e, E *x, E **u) {
    stack_check();
    if (freeof(e, x) || (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN)) return 0;
    if (e->t == E_FN && e->n == 1 && (e->s == F_ABS || e->s == F_SIGN || e->s == F_HEAVISIDE || e->s == F_DIRAC)) {
        if (*u && !expr_eq(*u, K0(e))) return -1;
        *u = K0(e);
        return 1;
    }
    int r = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        int t = args_of(X, EK(e)[i], x, u);
        if (t < 0) return -1;
        r |= t;
    }
    return r;
}

static E *ft(XP, E *f, E *x, E *w, int dep);

/* P(x) e^(q(x)), q quadratic with a negative leading coefficient, summed over the terms */
static E *gauss(XP, E *f, E *x, E *w) {
    E *g = expand(f), *res = K->c0, **ts = g->t == E_ADD ? EK(g) : &g;
    uint32_t nt = g->t == E_ADD ? g->n : 1;
    for (uint32_t i = 0; i < nt; i++) {
        uint32_t m;
        E **fs = facs(X, ts[i], &m), *P = K->c1, *q = NULL;
        for (uint32_t j = 0; j < m; j++) {
            E *h = fs[j];
            if (!q && h->t == E_POW && ISYM(K0(h), S_E) && !freeof(K1(h), x)) q = K1(h);
            else if (!q && IFN(h, F_EXP) && !freeof(K0(h), x)) q = K0(h);
            else P = s_mul2(P, h);
        }
        long d, kp;
        if (!q || !poly_degree_in(q, x, &d) || d != 2 || !poly_degree_in(P, x, &kp) || kp > 8) return NULL;
        E *a = s_neg(poly_coeff(q, x, 2)), *b = poly_coeff(q, x, 1), *c = poly_coeff(q, x, 0);
        if (X->sign_of(a) <= 0) return NULL;
        E *bz = s_sub(b, s_mul2(K->i_, w));
        E *G = s_mul2(s_sqrt(s_div(K->pi_, a)), s_exp(s_add2(c, s_div(s_mul2(bz, bz), s_mul2(mk_int(4), a)))));
        for (long k = 0; k <= kp; k++) {
            E *ck = poly_coeff(P, x, k);
            if (!is_zero(ck)) res = s_add2(res, s_mul2(ck, G));
            G = s_mul2(K->i_, diff(G, w));
        }
    }
    return s_div(res, r2p(X));
}

/* the polynomial c[0..d] divided by (x - p) m times (synthetic division; the remainder is 0) */
static void sdiv(XP, E **c, long *d, E *p, long m) {
    for (long t = 0; t < m; t++) {
        E *acc = c[*d];
        for (long k = *d - 1; k >= 0; k--) {
            E *ck = c[k];
            c[k] = acc;
            acc = s_add2(ck, s_mul2(p, acc));
        }
        c[*d] = K->c0, --*d;
    }
}

/* the coefficients of the polynomial D in x (degree dd) */
static E **coefs(XP, E *D, E *x, long dd) {
    E **c = salloc(sizeof(E *) * (size_t)(dd + 1));
    for (long k = 0; k <= dd; k++) c[k] = poly_coeff(D, x, k);
    return c;
}

static E *poly_of(XP, E **c, long d, E *x) {
    E **t = salloc(sizeof(E *) * (size_t)(d + 1));
    for (long k = 0; k <= d; k++) t[k] = s_mul2(c[k], s_powi(x, k));
    return s_add((uint32_t)(d + 1), t);
}

/* the residue of h(x)/(x - p)^m at p: d^(m-1)/dx^(m-1) h / (m-1)! at p */
static E *resid(XP, E *h, E *x, E *p, long m) {
    E *fac = K->c1;
    for (long j = 1; j < m; j++) h = diff(h, x), fac = s_mul2(fac, mk_int(j));
    return s_div(subst(h, x, p), fac);
}

static E *ft_rat(XP, E *f, E *x, E *w) {
    E *N, *D;
    long dn, dd;
    X->numden(f, &N, &D);
    if (!poly_degree_in(N, x, &dn) || !poly_degree_in(D, x, &dd) || dd < 1 || dn >= dd || dd > 8) return NULL;
    /* centre the poles (c = their mean, Vieta): f(x) = g(x - c), F = e^(-i w c) G */
    E *c0 = rat_normal(s_neg(s_div(poly_coeff(D, x, dd - 1), s_mul2(mk_int(dd), poly_coeff(D, x, dd)))));
    if (!is_zero(c0)) {
        if (!freeof(c0, K->i_)) return NULL;
        E *T = mk_sym_name("_fc"), *g = rat_normal(subst(f, x, s_add2(T, c0))), *G = ft_rat(X, subst(g, T, x), x, w);
        return G ? s_mul2(s_exp(s_neg(s_mul(3, (E *[]){K->i_, w, c0}))), G) : NULL;
    }
    E **lam = salloc(sizeof(E *) * (size_t)dd);
    long *mult = salloc(sizeof(long) * (size_t)dd);
    int nr = roots_mult(D, x, (uint32_t)dd, lam, mult);
    if (nr < 0) return NULL;
    E *A = K->c0, *B = K->c0, *tpi = s_mul(3, (E *[]){K->c2, K->pi_, K->i_});
    for (int r = 0; r < nr; r++) {
        double re, im;
        E *p = lam[r];
        if (!X->eval_complex(p, &re, &im) || AFABS(im) < 1e-12) return NULL;
        long d = dd;
        E **c = coefs(X, D, x, dd);
        sdiv(X, c, &d, p, mult[r]);
        E *h = s_div(s_mul2(N, s_exp(s_neg(s_mul(3, (E *[]){K->i_, w, x})))), poly_of(X, c, d, x));
        E *rs = resid(X, h, x, p, mult[r]);
        if (im < 0) A = s_sub(A, s_mul2(tpi, rs));
        else B = s_add2(B, s_mul2(tpi, rs));
    }
    A = simpl(X, A), B = simpl(X, B);
    E *Am = subst(A, w, s_neg(w)), *aw = s_fn1(F_ABS, w), *res;
    if (is_zero(simpl(X, s_sub(B, Am)))) res = subst(A, w, aw);
    else if (is_zero(simpl(X, s_add2(B, Am)))) res = s_mul2(s_fn1(F_SIGN, w), subst(A, w, aw));
    else res = s_add2(s_mul2(s_fn1(F_HEAVISIDE, w), A), s_mul2(s_fn1(F_HEAVISIDE, s_neg(w)), B));
    return s_div(res, r2p(X));
}

/* the whole-expression methods */
static E *ft_whole(XP, E *f, E *x, E *w) {
    E *r = gauss(X, f, x, w);
    if (r) return r;
    if ((r = ft_rat(X, f, x, w))) return r;
    if (!decays(X, f, x, 2)) return NULL;
    E *s = mk_sym_name("_fs"), *iw = s_mul2(K->i_, w);
    E *Lp = lap(X, half(X, f, x, 0), x, s), *Lm = Lp ? lap(X, half(X, f, x, 1), x, s) : NULL;
    if (!Lm) return NULL;
    return s_div(s_add2(subst(Lp, s, iw), subst(Lm, s, s_neg(iw))), r2p(X));
}

static E *ft(XP, E *f, E *x, E *w, int dep) {
    stack_check();
    if (dep > 12) return NULL;
    poll_intr();
    if (freeof(f, x)) return s_mul(3, (E *[]){f, r2p(X), s_fn1(F_DIRAC, w)});
    if (f->t == E_ADD) {
        E **v = salloc(sizeof(E *) * f->n);
        uint32_t i;
        for (i = 0; i < f->n; i++)
            if (!(v[i] = ft(X, EK(f)[i], x, w, dep + 1))) break;
        if (i == f->n) return s_add(f->n, v);
        return ft_whole(X, f, x, w);
    }
    uint32_t nf;
    E **fs = facs(X, f, &nf), *a, *b, *u = NULL, *r;
    /* constant factors */
    if (nf > 1) {
        E **cv = salloc(sizeof(E *) * nf), **rv = salloc(sizeof(E *) * nf);
        uint32_t nc = 0, nr = 0;
        for (uint32_t i = 0; i < nf; i++) {
            if (freeof(fs[i], x)) cv[nc++] = fs[i];
            else rv[nr++] = fs[i];
        }
        if (nc) return (r = ft(X, s_mul(nr, rv), x, w, dep + 1)) ? s_mul2(s_mul(nc, cv), r) : NULL;
    }
    /* dirac(a x + b) g(x) */
    for (uint32_t i = 0; i < nf; i++)
        if (IFN(fs[i], F_DIRAC) && fs[i]->n == 1 && lin(X, K0(fs[i]), x, &a, &b)) {
            E *x0 = s_neg(s_div(b, a)), *g = subst(prod_but(X, fs, nf, i), x, x0);
            return s_div(s_mul2(g, s_exp(s_neg(s_mul(3, (E *[]){K->i_, w, x0})))), s_mul2(s_fn1(F_ABS, a), r2p(X)));
        }
    /* g(a x + b): a shift and a scaling */
    if (args_of(X, f, x, &u) == 1 && !expr_eq(u, x) && lin(X, u, x, &a, &b)) {
        E *T = mk_sym_name("_ft"), *g = subst(f, x, s_div(s_sub(T, b), a));
        if ((r = ft(X, g, T, s_div(w, a), dep + 1)))
            return s_div(s_mul2(s_exp(s_div(s_mul(3, (E *[]){K->i_, w, b}), a)), r), s_fn1(F_ABS, a));
        return NULL;
    }
    /* modulation */
    for (uint32_t i = 0; i < nf; i++) {
        E *h = fs[i], *arg = NULL;
        int kind = 0; /* 1 exp, 2 cos, 3 sin */
        if (IFN(h, F_EXP)) arg = K0(h), kind = 1;
        else if (h->t == E_POW && ISYM(K0(h), S_E)) arg = K1(h), kind = 1;
        else if (IFN(h, F_COS) || IFN(h, F_SIN)) arg = K0(h), kind = IFN(h, F_COS) ? 2 : 3;
        if (!kind || !lin(X, arg, x, &a, &b)) continue;
        E *k = a;
        if (kind == 1) {
            k = s_div(a, K->i_);
            if (!freeof(k, K->i_) || has_float(k)) continue;
        }
        E *Wp = mk_sym_name("_fw"), *G = ft(X, prod_but(X, fs, nf, i), x, Wp, dep + 1);
        if (!G) return NULL;
        E *Gm = subst(G, Wp, s_sub(w, k));
        if (kind == 1) return s_mul2(s_exp(b), Gm);
        E *Gp = subst(G, Wp, s_add2(w, k)), *eb = s_exp(s_mul2(K->i_, b)), *emb = s_exp(s_neg(s_mul2(K->i_, b)));
        if (kind == 2) return s_mul2(K->chalf, s_add2(s_mul2(eb, Gm), s_mul2(emb, Gp)));
        return s_div(s_sub(s_mul2(eb, Gm), s_mul2(emb, Gp)), s_mul2(K->c2, K->i_));
    }
    /* x^k g */
    for (uint32_t i = 0; i < nf && nf > 1; i++) {
        long k = 0;
        if (expr_eq(fs[i], x)) k = 1;
        else if (fs[i]->t == E_POW && expr_eq(K0(fs[i]), x) && num_is_small_int(K1(fs[i]), &k) && (k < 1 || k > 8)) k = 0;
        if (!k) continue;
        E *G = ft(X, prod_but(X, fs, nf, i), x, w, dep + 1);
        if (!G) break;
        for (long j = 0; j < k; j++) G = s_mul2(K->i_, diff(G, w));
        return G;
    }
    /* heaviside(x), sign(x) alone; heaviside(x) g with g decaying */
    for (uint32_t i = 0; i < nf; i++) {
        if (fs[i]->t != E_FN || fs[i]->n != 1 || !expr_eq(K0(fs[i]), x)) continue;
        if (nf == 1 && fs[i]->s == F_HEAVISIDE)
            return s_sub(s_mul2(s_sqrt(s_div(K->pi_, K->c2)), s_fn1(F_DIRAC, w)), s_div(K->i_, s_mul2(r2p(X), w)));
        if (nf == 1 && fs[i]->s == F_SIGN) return s_neg(s_div(s_mul2(K->i_, s_sqrt(s_div(K->c2, K->pi_))), w));
        if (fs[i]->s == F_HEAVISIDE) {
            E *g = prod_but(X, fs, nf, i), *s = mk_sym_name("_fs");
            if (!decays(X, g, x, 1) || !(r = lap(X, g, x, s))) break;
            return s_div(subst(r, s, s_mul2(K->i_, w)), r2p(X));
        }
    }
    return ft_whole(X, f, x, w);
}

/* dirac(a w + b) h(w) -> dirac(a w + b) h(-b/a) in every term */
static E *sift(XP, E *e, E *w) {
    E *ex = expand(e);
    uint32_t nt = ex->t == E_ADD ? ex->n : 1;
    E **t = salloc(sizeof(E *) * nt);
    for (uint32_t i = 0; i < nt; i++) {
        E *term = ex->t == E_ADD ? EK(ex)[i] : ex, *a, *b;
        uint32_t nf;
        E **fs = facs(X, term, &nf);
        t[i] = term;
        for (uint32_t j = 0; j < nf; j++)
            if (IFN(fs[j], F_DIRAC) && fs[j]->n == 1 && lin(X, K0(fs[j]), w, &a, &b)) {
                t[i] = s_mul2(fs[j], subst(prod_but(X, fs, nf, j), w, s_neg(s_div(b, a))));
                break;
            }
    }
    return s_add(nt, t);
}

static int has_dirac(XP, E *e) {
    stack_check();
    if (IFN(e, F_DIRAC)) return 1;
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN && e->t != E_LIST) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_dirac(X, EK(e)[i])) return 1;
    return 0;
}

/* the one free symbol of e other than x (no value, no assumption, not w); 0 if several */
static int one_param(XP, E *e, E *x, E *w, E **p) {
    if (e->t == E_SYM) {
        if (expr_eq(e, x) || expr_eq(e, w) || is_const_expr(e)) return 1;
        if (*p && !expr_eq(*p, e)) return 0;
        *p = e;
        return 1;
    }
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN) return 1;
    for (uint32_t i = 0; i < e->n; i++)
        if (!one_param(X, EK(e)[i], x, w, p)) return 0;
    return 1;
}

static E *ft_p(XP, void **a) { return ft(X, (E *)a[0], (E *)a[1], (E *)a[2], 0); }
/* the temporary assumption on a[3] (the parameter) cleared, also when ft_p was interrupted */
static void ft_unassume(XP, void **a) { K->syms[((E *)a[3])->s].assume = 0; }

/* no transform for a parameter p of unknown sign: when f decays for p = 1, 4, 9 times sg (+-1)
 * but not for p = 0 or -sg, the transform found with the assumption sg*p > 0 (set only for the
 * computation) and the condition: when(sg*p>0, F, undef); NULL otherwise */
static E *ft_cond(XP, E *f, E *x, E *W, E **cond) {
    E *p = NULL;
    if (!one_param(X, f, x, W, &p) || !p) return NULL;
    symrec *sr = &K->syms[p->s];
    if (sr->val || sr->fdef || sr->assume) return NULL;
    for (int sg = 1; sg >= -1; sg -= 2) {
        int ok = !decays(X, subst(f, p, K->c0), x, 2) && !decays(X, subst(f, p, mk_int(-sg)), x, 2);
        for (int k = 1; k <= 3 && ok; k++) ok = decays(X, subst(f, p, mk_int(sg * k * k)), x, 2);
        if (!ok) continue;
        sr->assume = sg > 0 ? (A_POS | A_NONNEG) : A_NEG;
        void *a[4] = {f, x, W, p};
        E *r = X->protect_undo(ft_p, ft_unassume, a);
        if (r) *cond = s_fn2(sg > 0 ? F_GT : F_LT, p, K->c0);
        return r;
    }
    return NULL;
}

E *ap_fourier(XP, E *f, E *x, E *w, int inv) {
    if (x->t != E_SYM) cas_throw(CAS_ERR_DOMAIN, inv ? "ifourier: expected a variable" : "fourier: expected a variable");
    E *W = mk_sym_name("_fw0"), *cond = NULL;
    E *r = ft(X, f, x, W, 0);
    if (!r) r = ft_cond(X, f, x, W, &cond);
    if (!r) cas_throw(CAS_ERR_UNSUPPORTED, inv ? "ifourier: transform not found" : "fourier: transform not found");
    r = subst(r, W, inv ? s_neg(w) : w);
    if (!has_dirac(X, r)) r = simpl(X, r);
    else {
        /* the impulses simplified apart from the rest (simplify would put them over the common
         * denominator again) */
        r = sift(X, r, w);
        uint32_t nt = r->t == E_ADD ? r->n : 1, k = 0;
        E **t = salloc(sizeof(E *) * (nt + 1)), *rest = K->c0;
        for (uint32_t i = 0; i < nt; i++) {
            E *u = r->t == E_ADD ? EK(r)[i] : r;
            if (has_dirac(X, u)) t[k++] = simpl(X, u);
            else rest = s_add2(rest, u);
        }
        t[k++] = simpl(X, rest);
        r = s_add(k, t);
    }
    if (!cond) return r;
    E *v[3] = {cond, r, K->undef_};
    return mk_fn(C_WHEN, 3, v);
}

static E *zt(XP, E *f, E *n, E *z, int dep);

static E *zt_sum(XP, E *f, E *n, E *z) {
    E *a[4] = {s_mul2(f, s_pow(z, s_neg(n))), n, K->c0, K->inf_};
    E *r = try_cmd(C_SUM, 4, a);
    if (!r || !freeof(r, n) || has_dirac(X, r)) return NULL;
    return r;
}

static E *zt(XP, E *f, E *n, E *z, int dep) {
    stack_check();
    poll_intr();
    if (dep > 16) return NULL;
    if (freeof(f, n)) return s_div(s_mul2(f, z), s_sub(z, K->c1));
    if (f->t == E_ADD) {
        E **v = salloc(sizeof(E *) * f->n);
        for (uint32_t i = 0; i < f->n; i++)
            if (!(v[i] = zt(X, EK(f)[i], n, z, dep + 1))) return zt_sum(X, f, n, z);
        return s_add(f->n, v);
    }
    uint32_t nf;
    E **fs = facs(X, f, &nf), *a, *b, *r;
    if (nf > 1) {
        E **cv = salloc(sizeof(E *) * nf), **rv = salloc(sizeof(E *) * nf);
        uint32_t nc = 0, nr = 0;
        for (uint32_t i = 0; i < nf; i++) {
            if (freeof(fs[i], n)) cv[nc++] = fs[i];
            else rv[nr++] = fs[i];
        }
        if (nc) return (r = zt(X, s_mul(nr, rv), n, z, dep + 1)) ? s_mul2(s_mul(nc, cv), r) : NULL;
    }
    /* q^n g(n) -> G(z/q) */
    for (uint32_t i = 0; i < nf; i++) {
        E *h = fs[i], *base = NULL, *ex = NULL;
        if (h->t == E_POW && freeof(K0(h), n)) base = K0(h), ex = K1(h);
        else if (IFN(h, F_EXP)) base = K->e_, ex = K0(h);
        if (!base || !lin(X, ex, n, &a, &b)) continue;
        E *q = s_pow(base, a), *Zp = mk_sym_name("_zq"), *G = zt(X, prod_but(X, fs, nf, i), n, Zp, dep + 1);
        if (!G) return zt_sum(X, f, n, z);
        return s_mul2(s_pow(base, b), subst(G, Zp, s_div(z, q)));
    }
    /* n^k g -> (-z d/dz)^k G */
    for (uint32_t i = 0; i < nf; i++) {
        long k = 0;
        if (expr_eq(fs[i], n)) k = 1;
        else if (fs[i]->t == E_POW && expr_eq(K0(fs[i]), n) && num_is_small_int(K1(fs[i]), &k) && (k < 1 || k > 10)) k = 0;
        if (!k) continue;
        E *G = zt(X, prod_but(X, fs, nf, i), n, z, dep + 1);
        if (!G) return zt_sum(X, f, n, z);
        for (long j = 0; j < k; j++) G = rat_normal(s_neg(s_mul2(z, diff(G, z))));
        return G;
    }
    if (nf == 1) {
        E *h = f;
        if ((IFN(h, F_COS) || IFN(h, F_SIN)) && lin(X, K0(h), n, &a, &b)) {
            E *ca = s_fn1(F_COS, a), *sa = s_fn1(F_SIN, a);
            E *den = s_add(3, (E *[]){s_mul2(z, z), s_mul(3, (E *[]){mk_int(-2), z, ca}), K->c1});
            E *Zc = s_div(s_mul2(z, s_sub(z, ca)), den), *Zs = s_div(s_mul2(z, sa), den);
            E *cb = s_fn1(F_COS, b), *sb = s_fn1(F_SIN, b);
            if (IFN(h, F_COS)) return s_sub(s_mul2(cb, Zc), s_mul2(sb, Zs)); /* cos(an+b) = cos b cos an - sin b sin an */
            return s_add2(s_mul2(sb, Zc), s_mul2(cb, Zs));                   /* sin(an+b) = sin b cos an + cos b sin an */
        }
        if ((IFN(h, F_COSH) || IFN(h, F_SINH)) && lin(X, K0(h), n, &a, &b)) {
            E *ep = s_exp(K0(h)), *em = s_exp(s_neg(K0(h)));
            return zt(X, s_mul2(K->chalf, IFN(h, F_COSH) ? s_add2(ep, em) : s_sub(ep, em)), n, z, dep + 1);
        }
        long k;
        if (IFN(h, F_BINOM) && h->n == 2 && expr_eq(K0(h), n) && num_is_small_int(K1(h), &k) && k >= 0 && k <= 50)
            return s_div(z, s_powi(s_sub(z, K->c1), k + 1));
        if (h->t == E_POW && IFN(K0(h), F_FACT) && expr_eq(K0(K0(h)), n) && num_is_small_int(K1(h), &k) && k == -1)
            return s_exp(s_inv(z));
    }
    return zt_sum(X, f, n, z);
}

E *ap_ztrans(XP, E *f, E *n, E *z) {
    if (n->t != E_SYM) cas_throw(CAS_ERR_DOMAIN, "ztrans: expected a variable");
    E *Z = mk_sym_name("_zz"), *r = zt(X, f, n, Z, 0);
    if (!r) cas_throw(CAS_ERR_UNSUPPORTED, "ztrans: transform not found");
    r = simpl(X, rat_normal(r));
    E *N, *D;
    long d;
    X->numden(r, &N, &D);
    if (poly_degree_in(D, Z, &d) && d > 0 && X->sign_of(poly_coeff(D, Z, d)) < 0) /* a positive leading coefficient */
        r = s_div(s_neg(N), s_neg(D));
    return subst(r, Z, z);
}

E *ap_iztrans(XP, E *F, E *z, E *n) {
    if (z->t != E_SYM) cas_throw(CAS_ERR_DOMAIN, "iztrans: expected a variable");
    E *G = rat_normal(s_div(F, z)), *N, *D;
    long dn, dd;
    X->numden(G, &N, &D);
    if (!poly_degree_in(N, z, &dn) || !poly_degree_in(D, z, &dd) || dd < 1)
        cas_throw(CAS_ERR_UNSUPPORTED, "iztrans: F must be a rational function of z");
    if (dn >= dd) cas_throw(CAS_ERR_DOMAIN, "iztrans: F must be proper (degree of numerator <= degree of denominator)");
    if (dd > 12) cas_throw(CAS_ERR_UNSUPPORTED, "iztrans: denominator degree too large");
    E **lam = salloc(sizeof(E *) * (size_t)dd);
    long *mult = salloc(sizeof(long) * (size_t)dd);
    int nr = roots_mult(D, z, (uint32_t)dd, lam, mult);
    if (nr < 0) cas_throw(CAS_ERR_UNSUPPORTED, "iztrans: the poles were not found exactly");
    int real = freeof(F, K->i_);
    E **out = salloc(sizeof(E *) * (size_t)(nr + 1));
    uint32_t no = 0;
    for (int r = 0; r < nr; r++) {
        E *p = lam[r];
        long m = mult[r];
        double re = 0, im = 0;
        int num = X->eval_complex(p, &re, &im);
        int pair = 0;
        if (real && num && AFABS(im) > 1e-12) { /* a complex pole: the pair from the one in the upper half */
            if (im < 0) {
                int found = 0;
                for (int q = 0; q < nr; q++) {
                    double r2, i2;
                    if (q != r && mult[q] == m && X->eval_complex(lam[q], &r2, &i2) && AFABS(r2 - re) < 1e-9 * (1 + AFABS(re)) &&
                        AFABS(i2 + im) < 1e-9 * (1 + AFABS(im)))
                        found = 1;
                }
                if (found) continue;
            } else {
                for (int q = 0; q < nr; q++) {
                    double r2, i2;
                    if (q != r && mult[q] == m && X->eval_complex(lam[q], &r2, &i2) && AFABS(r2 - re) < 1e-9 * (1 + AFABS(re)) &&
                        AFABS(i2 + im) < 1e-9 * (1 + AFABS(im)))
                        pair = 1;
                }
            }
        }
        long d = dd;
        E **c = coefs(X, D, z, dd);
        sdiv(X, c, &d, p, m);
        E *h = s_div(N, poly_of(X, c, d, z)), *A = K->c0, *fac = K->c1;
        int zero = is_zero(p);
        for (long t = 0; t < m; t++) { /* coefficient of 1/(z - p)^(m - t): h^(t)(p)/t! */
            if (t) h = diff(h, z), fac = s_mul2(fac, mk_int(t));
            E *cj = rat_normal(s_div(subst(h, z, p), fac));
            long j = m - t;
            if (is_zero(cj)) continue;
            if (zero) { /* z^(1-j): the impulse at n = j - 1 */
                E *pw[3] = {s_fn2(F_EQ, n, mk_int(j - 1)), K->c1, K->c0};
                out[no++] = s_mul2(cj, mk_fn(F_PIECEWISE, 3, pw));
                continue;
            }
            E *bin = K->c1; /* binomial(n, j-1) as a polynomial */
            for (long q = 0; q < j - 1; q++) bin = s_mul2(bin, s_div(s_sub(n, mk_int(q)), mk_int(q + 1)));
            A = s_add2(A, s_mul(3, (E *[]){cj, expand(bin), s_powi(p, 1 - j)}));
        }
        if (zero || is_zero(A)) continue;
        if (!pair) {
            out[no++] = s_mul2(A, s_pow(p, n));
            continue;
        }
        A = expand(A);
        long da;
        if (!poly_degree_in(A, n, &da)) da = -1;
        E *ra = K->c0, *ia = K->c0;
        for (long k = 0; k <= da; k++) {
            E *ck = poly_coeff(A, n, k), *nk = s_powi(n, k);
            ra = s_add2(ra, s_mul2(simpl(X, s_fn1(F_RE, ck)), nk));
            ia = s_add2(ia, s_mul2(simpl(X, s_fn1(F_IM, ck)), nk));
        }
        E *pr = simpl(X, s_fn1(F_RE, p)), *pi = simpl(X, s_fn1(F_IM, p));
        E *rr = simpl(X, s_sqrt(s_add2(s_mul2(pr, pr), s_mul2(pi, pi)))), *th = simpl(X, s_fn1(F_ARG, p)), *nt = s_mul2(n, th);
        out[no++] = s_mul(3, (E *[]){K->c2, s_pow(rr, n), s_sub(s_mul2(ra, s_fn1(F_COS, nt)), s_mul2(ia, s_fn1(F_SIN, nt)))});
    }
    return simpl(X, s_add(no, out));
}
