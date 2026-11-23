/*
 * intsf.c -- antiderivatives in terms of Ei, Si, Ci, erf and erfi:
 *   linear arguments  R(x) F(a + b x), F in {exp, sin, cos, sinh, cosh}, R rational with linear
 *                     factors in the denominator: partial fractions, then for each term
 *                     u = c + d x,  F(al + be u)/u^m: m = 1 by the addition theorems
 *                       exp -> e^al Ei(be u),  sin -> sin(al) Ci(be u) + cos(al) Si(be u),
 *                       cos -> cos(al) Ci(be u) - sin(al) Si(be u),
 *                       sinh, cosh -> (Ei(be u) -+ Ei(-be u))/2 with cosh / sinh(al),
 *                     m >= 2 by parts:  -F/((m-1) u^(m-1)) + be/(m-1) int F'/u^(m-1);
 *   Gaussians         P(x) e^(c x^2 + b x + a) (also sin, cos, sinh, cosh of a quadratic, via
 *                     exponentials):  int e^q = e^(a - b^2/(4c)) sqrt(pi)/(2 s) erf(s (x + b/(2c)))
 *                     with s = sqrt(-c) (erfi and s = sqrt(c) unless c looks negative), and
 *                     I_m = x^(m-1) e^q/(2c) - (m-1)/(2c) I_(m-2) - b/(2c) I_(m-1);
 *   logarithms        R(x, x^r, ln(c x^n)): t = ln x, x = e^t, ln(c x^n) = K + n t with the
 *                     locally constant K = ln(c x^n) - n ln x; then the linear-argument case
 *                     (x^m/ln x -> Ei((m+1) ln x), ...).
 *   symbolic bases    f^u (f a symbol, u depending on x) is e^(u ln f) first.
 */
#include "intx.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

static int lin_coef(XP, E *u, E *x, E **a, E **b) {
    long d;
    if (!poly_degree_in(u, x, &d) || d != 1) return 0;
    *b = poly_coeff(u, x, 1), *a = poly_coeff(u, x, 0);
    return 1;
}

static int is_tfn(E *e) {
    return IFN(e, F_SIN) || IFN(e, F_COS) || IFN(e, F_SINH) || IFN(e, F_COSH) || IFN(e, F_EXP);
}

/* F(arg) for F given as a function id (F_EXP for e^arg) */
static E *mkF(XP, int F, E *arg) { return F == F_EXP ? s_exp(arg) : s_fn1(F, arg); }

/* int F(al + be u) / u^m du */
static E *jm(XP, int F, E *al, E *be, E *u, long m, int depth) {
    if (depth > 12) return NULL;
    E *bu = s_mul2(be, u);
    if (m == 1) {
        E *ei = s_fn1(F_EI, bu), *ein = s_fn1(F_EI, s_neg(bu));
        switch (F) {
        case F_EXP: return s_mul2(s_exp(al), ei);
        case F_SIN:
            return s_add2(s_mul2(s_fn1(F_SIN, al), s_fn1(F_CI, bu)), s_mul2(s_fn1(F_COS, al), s_fn1(F_SI, bu)));
        case F_COS:
            return s_sub(s_mul2(s_fn1(F_COS, al), s_fn1(F_CI, bu)), s_mul2(s_fn1(F_SIN, al), s_fn1(F_SI, bu)));
        case F_SINH: /* sinh al cosh(bu) + cosh al sinh(bu) */
            return s_div(s_add2(s_mul2(s_fn1(F_SINH, al), s_add2(ei, ein)), s_mul2(s_fn1(F_COSH, al), s_sub(ei, ein))), K->c2);
        case F_COSH:
            return s_div(s_add2(s_mul2(s_fn1(F_COSH, al), s_add2(ei, ein)), s_mul2(s_fn1(F_SINH, al), s_sub(ei, ein))), K->c2);
        }
        return NULL;
    }
    if (m <= 0) return NULL;
    int dF = F == F_SIN ? F_COS : F == F_COS ? F_SIN : F == F_SINH ? F_COSH : F == F_COSH ? F_SINH : F_EXP;
    E *rest = jm(X, dF, al, be, u, m - 1, depth + 1);
    if (!rest) return NULL;
    if (F == F_COS) rest = s_neg(rest);
    E *m1 = mk_int(m - 1);
    return s_add2(s_neg(s_div(mkF(X, F, s_add2(al, bu)), s_mul2(m1, s_powi(u, m - 1)))), s_mul2(s_div(be, m1), rest));
}

/* f = R(x) F(arg), arg linear: the pieces, else 0 */
static int lin_split(XP, E *f, E *x, E **R, int *F, E **arg) {
    E **fs = f->t == E_MUL ? EK(f) : &f;
    uint32_t n = f->t == E_MUL ? f->n : 1;
    E *r = K->c1, *g = NULL;
    for (uint32_t i = 0; i < n; i++) {
        E *q = fs[i];
        if (freeof(q, x)) r = s_mul2(r, q);
        else if (!g && (is_tfn(q) || (q->t == E_POW && ISYM(K0(q), S_E)))) g = q;
        else if (ix_ratx(X, q, x)) r = s_mul2(r, q);
        else return 0;
    }
    if (!g) return 0;
    if (g->t == E_POW) *F = F_EXP, *arg = K1(g);
    else *F = g->s, *arg = K0(g);
    *R = r;
    return 1;
}

static E *ix_linfn(XP, E *f, E *x) {
    E *R, *arg, *a0, *b0;
    int F;
    if (!lin_split(X, f, x, &R, &F, &arg) || !lin_coef(X, arg, x, &a0, &b0)) return NULL;
    E *N, *D;
    X->numden(rat_normal(R), &N, &D);
    if (freeof(D, x)) return NULL; /* polynomial: the core's */
    E *pa[2] = {R, x};
    E *pf = try_cmd(C_PARTFRAC, 2, pa);
    if (!pf) return NULL;
    E **ts = pf->t == E_ADD ? EK(pf) : &pf;
    uint32_t nt = pf->t == E_ADD ? pf->n : 1;
    E *res = K->c0, *poly = K->c0;
    for (uint32_t i = 0; i < nt; i++) {
        E *tn, *td;
        X->numden(ts[i], &tn, &td);
        if (freeof(td, x)) {
            poly = s_add2(poly, ts[i]);
            continue;
        }
        if (!freeof(tn, x)) return NULL;
        E *cont, *L = X->factor_list_in(td, x, &cont);
        if (!L || L->t != E_LIST) return NULL;
        E *lf = NULL;
        long m = 0;
        E *cc = s_div(tn, cont);
        for (uint32_t j = 0; j < L->n; j++) {
            E *p = EK(L)[j], *fj = EK(p)[0];
            long mj;
            if (!num_is_small_int(EK(p)[1], &mj)) return NULL;
            if (freeof(fj, x)) {
                cc = s_div(cc, s_powi(fj, mj));
                continue;
            }
            if (lf) return NULL;
            lf = fj, m = mj;
        }
        E *c0, *d0;
        if (!lf || !lin_coef(X, lf, x, &c0, &d0)) return NULL;
        /* u = c0 + d0 x: a0 + b0 x = al + be u */
        int fi = 0;
        E *u = ix_fresh(X, f, &fi);
        E *be = rat_normal(s_div(b0, d0)), *al = rat_normal(s_sub(a0, s_mul2(be, c0)));
        E *J = jm(X, F, al, be, u, m, 0);
        if (!J) return NULL;
        res = s_add2(res, s_mul2(s_div(cc, d0), subst(J, u, lf)));
    }
    if (!is_zero(poly)) {
        E *P = ix_int(X, s_mul2(poly, mkF(X, F, arg)), x);
        if (!P) return NULL;
        res = s_add2(res, P);
    }
    return res;
}

/* ---- Gaussians ---- */

static int quad_coef(XP, E *u, E *x, E **a, E **b, E **c) {
    long d;
    if (!poly_degree_in(u, x, &d) || d != 2) return 0;
    *c = poly_coeff(u, x, 2), *b = poly_coeff(u, x, 1), *a = poly_coeff(u, x, 0);
    return 1;
}

static int looks_neg(XP, E *e) {
    if (is_num(e)) return num_sgn(e) < 0;
    if (e->t == E_MUL || e->t == E_ADD) return looks_neg(X, EK(e)[0]);
    return 0;
}

/* int x^m e^(c x^2 + b x + a) */
static E *gauss_m(XP, long m, E *a, E *b, E *c, E *x) {
    if (m == 0) {
        E *h = s_div(b, s_mul2(K->c2, c)), *k0 = rat_normal(s_sub(a, s_div(s_mul2(b, b), s_mul2(mk_int(4), c))));
        int neg = looks_neg(X, c);
        E *s = s_sqrt(neg ? s_neg(c) : c);
        E *er = s_fn1(neg ? F_ERF : F_ERFI, s_mul2(s, s_add2(x, h)));
        return s_mul(4, (E *[]){s_exp(k0), s_sqrt(K->pi_), s_inv(s_mul2(K->c2, s)), er});
    }
    E *q = s_add(3, (E *[]){a, s_mul2(b, x), s_mul2(c, s_mul2(x, x))});
    E *c2 = s_mul2(K->c2, c);
    E *r = s_div(s_mul2(s_powi(x, m - 1), s_exp(q)), c2);
    E *i1 = gauss_m(X, m - 1, a, b, c, x);
    r = s_sub(r, s_mul2(s_div(b, c2), i1));
    if (m >= 2) r = s_sub(r, s_mul2(s_div(mk_int(m - 1), c2), gauss_m(X, m - 2, a, b, c, x)));
    return r;
}

/* P(x) e^q, P polynomial of degree <= 12, q quadratic */
static E *gauss_term(XP, E *f, E *x) {
    E **fs = f->t == E_MUL ? EK(f) : &f;
    uint32_t n = f->t == E_MUL ? f->n : 1;
    E *P = K->c1, *q = NULL;
    for (uint32_t i = 0; i < n; i++) {
        E *e = fs[i];
        if (!q && e->t == E_POW && ISYM(K0(e), S_E) && !freeof(K1(e), x)) q = K1(e);
        else if (!q && IFN(e, F_EXP) && !freeof(K0(e), x)) q = K0(e);
        else P = s_mul2(P, e);
    }
    E *a, *b, *c;
    long d;
    if (!q || !quad_coef(X, expand(q), x, &a, &b, &c)) return NULL;
    P = expand(P);
    if (!poly_degree_in(P, x, &d) || d > 12) return NULL;
    E *r = K->c0;
    for (long j = 0; j <= d; j++) {
        E *cj = poly_coeff(P, x, j);
        if (!is_zero(cj)) r = s_add2(r, s_mul2(cj, gauss_m(X, j, a, b, c, x)));
    }
    return r;
}

static int has_quad_tfn(XP, E *e, E *x) {
    if (freeof(e, x)) return 0;
    if ((IFN(e, F_SIN) || IFN(e, F_COS) || IFN(e, F_SINH) || IFN(e, F_COSH))) {
        long d;
        return poly_degree_in(K0(e), x, &d) && d == 2;
    }
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_quad_tfn(X, EK(e)[i], x)) return 1;
    return 0;
}

/* ---- Fresnel integrals: P(x) sin(q), P(x) cos(q) with a real quadratic q ----
 * q = c (x + h)^2 + k0, t = x + h:  int sin(c t^2) = S(s t)/s, int cos(c t^2) = C(s t)/s with
 * s = sqrt(2 c/pi) (c < 0: sin q = -sin(-q), cos q = cos(-q));
 *   int t^j sin th = -t^(j-1) cos th/(2c) + (j-1)/(2c) int t^(j-2) cos th,
 *   int t^j cos th =  t^(j-1) sin th/(2c) - (j-1)/(2c) int t^(j-2) sin th,  th = c t^2 + k0 */
/* a square root of e up to sign: squares pulled out (f^(2k) -> f^k) */
static E *fsqrt(XP, E *e) {
    E **fs = e->t == E_MUL ? EK(e) : &e;
    uint32_t n = e->t == E_MUL ? e->n : 1;
    E *r = K->c1;
    for (uint32_t i = 0; i < n; i++) {
        long k;
        E *f = fs[i];
        if (f->t == E_POW && num_is_small_int(K1(f), &k) && !(k & 1)) r = s_mul2(r, s_powi(K0(f), k / 2));
        else r = s_mul2(r, s_sqrt(f));
    }
    return r;
}

static E *fres_m(XP, int cs, long j, E *c, E *k0, E *t) {
    E *th = s_add2(s_mul2(c, s_mul2(t, t)), k0), *c2 = s_mul2(K->c2, c);
    if (j == -1) { /* int sin(th)/t = (sin k0 Ci(c t^2) + cos k0 Si(c t^2))/2, cos: (cos k0 Ci - sin k0 Si)/2 */
        E *ct = s_mul2(c, s_mul2(t, t)), *ci = s_fn1(F_CI, ct), *si = s_fn1(F_SI, ct);
        E *ck = s_fn1(F_COS, k0), *sk = s_fn1(F_SIN, k0);
        return s_div(cs ? s_sub(s_mul2(ck, ci), s_mul2(sk, si)) : s_add2(s_mul2(sk, ci), s_mul2(ck, si)), K->c2);
    }
    if (j < -1) { /* by parts upwards: int t^j sin th = t^(j+1) sin th/(j+1) - 2c/(j+1) int t^(j+2) cos th */
        E *r = s_div(s_mul2(s_powi(t, j + 1), s_fn1(cs ? F_COS : F_SIN, th)), mk_int(j + 1));
        E *rest = s_mul2(s_div(c2, mk_int(j + 1)), fres_m(X, !cs, j + 2, c, k0, t));
        return cs ? s_add2(r, rest) : s_sub(r, rest);
    }
    if (j == 0) {
        E *s = fsqrt(X, s_div(c2, K->pi_)); /* S(s t)/s is even in s */
        E *S0 = s_div(s_fn1(mk_sym_name("fresnels")->s, s_mul2(s, t)), s);
        E *C0 = s_div(s_fn1(mk_sym_name("fresnelc")->s, s_mul2(s, t)), s);
        E *ck = s_fn1(F_COS, k0), *sk = s_fn1(F_SIN, k0);
        return cs ? s_sub(s_mul2(ck, C0), s_mul2(sk, S0)) : s_add2(s_mul2(ck, S0), s_mul2(sk, C0));
    }
    E *r = s_div(s_mul2(s_powi(t, j - 1), s_fn1(cs ? F_SIN : F_COS, th)), c2);
    if (!cs) r = s_neg(r);
    if (j >= 2) {
        E *rest = s_mul2(s_div(mk_int(j - 1), c2), fres_m(X, !cs, j - 2, c, k0, t));
        r = cs ? s_sub(r, rest) : s_add2(r, rest);
    }
    return r;
}

static E *fres_term(XP, E *f, E *x, E *t) {
    E **fs = f->t == E_MUL ? EK(f) : &f;
    uint32_t n = f->t == E_MUL ? f->n : 1;
    E *P = K->c1, *q = NULL;
    int cs = 0;
    for (uint32_t i = 0; i < n; i++) {
        E *e = fs[i];
        if (!q && (IFN(e, F_SIN) || IFN(e, F_COS)) && !freeof(e, x)) q = K0(e), cs = IFN(e, F_COS);
        else P = s_mul2(P, e);
    }
    E *a, *b, *c;
    long d;
    if (!q && poly_degree_in(P, x, &d) && d <= 12) { /* a polynomial term (after tlin) */
        E *r = K->c0;
        for (long j = 0; j <= d; j++) r = s_add2(r, s_div(s_mul2(poly_coeff(P, x, j), s_powi(x, j + 1)), mk_int(j + 1)));
        return r;
    }
    if (!q || !quad_coef(X, expand(q), x, &a, &b, &c) || !freeof(P, t)) return NULL;
    if (!freeof(a, K->i_) || !freeof(b, K->i_) || !freeof(c, K->i_)) return NULL;
    E *h = s_div(b, s_mul2(K->c2, c)), *k0 = rat_normal(s_sub(a, s_div(s_mul2(b, b), s_mul2(mk_int(4), c))));
    E *sg = K->c1;
    if (looks_neg(X, c)) {
        c = s_neg(c), k0 = s_neg(k0);
        if (!cs) sg = K->cm1;
    }
    /* P a polynomial in t (a Laurent polynomial when h = 0) */
    long lo = is_zero(h) ? 12 : 0;
    P = expand(s_mul2(subst(P, x, s_sub(t, h)), s_powi(t, lo)));
    if (!poly_degree_in(P, t, &d) || d > 12 + lo) return NULL;
    E *r = K->c0;
    for (long j = 0; j <= d; j++) {
        E *cj = poly_coeff(P, t, j);
        if (!is_zero(cj)) r = s_add2(r, s_mul2(cj, fres_m(X, cs, j - lo, c, k0, t)));
    }
    return s_mul2(sg, subst(r, t, s_add2(x, h)));
}

static int has_hyp_quad(XP, E *e, E *x) { /* sinh, cosh or e^ of a quadratic */
    if (freeof(e, x)) return 0;
    long d;
    if ((IFN(e, F_SINH) || IFN(e, F_COSH) || IFN(e, F_EXP)) && poly_degree_in(K0(e), x, &d) && d == 2) return 1;
    if (e->t == E_POW && ISYM(K0(e), S_E) && poly_degree_in(K1(e), x, &d) && d == 2) return 1;
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_hyp_quad(X, EK(e)[i], x)) return 1;
    return 0;
}

static E *ix_fresnel(XP, E *f, E *x) {
    E *a1[1] = {f}, *g = try_cmd(C_TLIN, 1, a1); /* products of sines and cosines */
    g = expand(g ? g : f);
    E **ts = g->t == E_ADD ? EK(g) : &g;
    uint32_t n = g->t == E_ADD ? g->n : 1;
    if (n > 24) return NULL;
    int fi = 0;
    E *t = ix_fresh(X, f, &fi), *r = K->c0;
    for (uint32_t i = 0; i < n; i++) {
        E *s = fres_term(X, ts[i], x, t);
        if (!s) return NULL;
        r = s_add2(r, s);
    }
    return r;
}

static E *ix_gauss(XP, E *f, E *x) {
    E *g = f;
    if (has_quad_tfn(X, f, x) && !has_hyp_quad(X, f, x)) {
        char *m = K->s_top;
        E *r = ix_fresnel(X, f, x);
        if (r) return r;
        K->s_top = m;
    }
    if (has_quad_tfn(X, f, x)) {
        E *a1[1] = {f};
        g = try_cmd(C_TRIG2EXP, 1, a1);
        if (!g) return NULL;
    }
    g = expand(g);
    E **ts = g->t == E_ADD ? EK(g) : &g;
    uint32_t n = g->t == E_ADD ? g->n : 1;
    if (n > 24) return NULL;
    E *r = K->c0;
    for (uint32_t i = 0; i < n; i++) {
        E *t = gauss_term(X, ts[i], x);
        if (!t) return NULL;
        r = s_add2(r, t);
    }
    return r;
}

/* ---- logarithms: t = ln x ---- */

typedef struct {
    E *t, *ks[6], *kv[6];
    uint32_t nk;
    int bad;
} lctx;

static E *lnsub(XP, lctx *L, E *e, E *x, int *fi, E *f) {
    if (L->bad || freeof(e, x)) return e;
    stack_check();
    if (expr_eq(e, x)) return s_exp(L->t);
    if (IFN(e, F_LN)) {
        E *B = K0(e);
        E *n = rat_normal(s_div(s_mul2(x, diff(B, x)), B));
        if (!freeof(n, x)) {
            L->bad = 1;
            return e;
        }
        E *kv = s_sub(e, s_mul2(n, s_fn1(F_LN, x)));
        if (is_zero(kv)) return s_mul2(n, L->t);
        if (L->nk == 6) {
            L->bad = 1;
            return e;
        }
        L->ks[L->nk] = ix_fresh(X, f, fi), L->kv[L->nk] = kv;
        return s_add2(L->ks[L->nk++], s_mul2(n, L->t));
    }
    if (e->t == E_POW && expr_eq(K0(e), x) && freeof(K1(e), x)) return s_exp(s_mul2(K1(e), L->t));
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW) {
        L->bad = 1;
        return e;
    }
    E **k = salloc(sizeof(E *) * e->n);
    for (uint32_t i = 0; i < e->n; i++) k[i] = lnsub(X, L, EK(e)[i], x, fi, f);
    return X->rebuild(e, k);
}

static E *ix_lnsub(XP, E *f, E *x) {
    if (!ix_has_fn(X, f, F_LN)) return NULL;
    lctx *L = salloc(sizeof(lctx));
    int fi = 0;
    L->nk = 0, L->bad = 0;
    L->t = ix_fresh(X, f, &fi);
    E *g = lnsub(X, L, f, x, &fi, f);
    if (L->bad || !freeof(g, x)) return NULL;
    g = s_mul2(g, s_exp(L->t));
    E *G = ix_int(X, g, L->t);
    if (!G) G = ix_linfn(X, g, L->t);
    if (!G) return NULL;
    G = subst(G, L->t, s_fn1(F_LN, x));
    for (uint32_t i = 0; i < L->nk; i++) G = subst(G, L->ks[i], L->kv[i]);
    return G;
}

/* ---- R(x) ln(a + b x): dilogarithms ----
 * partial fractions of R; a term C/(c + d x) gives
 *   (C/d) (ln(L) ln(b (c + d x)/D) + polylog(2, -d L/D)),  L = a + b x, D = b c - a d,
 * the polynomial part and the higher powers of (c + d x) go to the core (by parts) */
static E *ix_lnlin(XP, E *f, E *x) {
    E **fs = f->t == E_MUL ? EK(f) : &f;
    uint32_t n = f->t == E_MUL ? f->n : 1;
    E *R = K->c1, *lg = NULL;
    for (uint32_t i = 0; i < n; i++) {
        E *q = fs[i];
        if (!lg && IFN(q, F_LN) && !freeof(q, x)) lg = q;
        else if (ix_ratx(X, q, x)) R = s_mul2(R, q);
        else return NULL;
    }
    E *a, *b;
    if (!lg || !lin_coef(X, K0(lg), x, &a, &b)) return NULL;
    E *N, *Dn;
    X->numden(rat_normal(R), &N, &Dn);
    if (freeof(Dn, x)) return NULL;
    E *pa[2] = {R, x};
    E *pf = try_cmd(C_PARTFRAC, 2, pa);
    if (!pf) return NULL;
    E **ts = pf->t == E_ADD ? EK(pf) : &pf;
    uint32_t nt = pf->t == E_ADD ? pf->n : 1;
    E *res = K->c0, *L = K0(lg);
    int used = 0;
    for (uint32_t i = 0; i < nt; i++) {
        E *tn, *td, *c0, *d0;
        long m;
        X->numden(ts[i], &tn, &td);
        if (freeof(tn, x) && poly_degree_in(td, x, &m) && m == 1 && lin_coef(X, td, x, &c0, &d0)) {
            E *D = rat_normal(s_sub(s_mul2(b, c0), s_mul2(a, d0)));
            if (!is_zero(D) && is_zero(c0) && !is_zero(a)) { /* C/(d x): ln(a) ln(x) - Li2(-b x/a) */
                E *v[2] = {mk_int(2), s_neg(s_div(s_mul2(b, x), a))};
                E *li = mk_fn(mk_sym_name("polylog")->s, 2, v);
                res = s_add2(res, s_mul2(s_div(tn, d0), s_sub(s_mul2(s_fn1(F_LN, a), s_fn1(F_LN, x)), li)));
                used = 1;
                continue;
            }
            if (!is_zero(D)) {
                E *t1 = s_mul2(lg, s_fn1(F_LN, s_div(s_mul2(b, td), D)));
                E *v[2] = {mk_int(2), s_neg(s_div(s_mul2(d0, L), D))};
                E *li = mk_fn(mk_sym_name("polylog")->s, 2, v);
                res = s_add2(res, s_mul2(s_div(tn, d0), s_add2(t1, li)));
                used = 1;
                continue;
            }
        }
        E *P = ix_int(X, s_mul2(ts[i], lg), x);
        if (!P) return NULL;
        res = s_add2(res, P);
    }
    return used ? res : NULL;
}

/* f^u with a symbolic base f -> e^(u ln f) */
static E *symbase(XP, E *e, E *x, int *ch) {
    if (freeof(e, x) || (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN)) return e;
    stack_check();
    E **k = salloc(sizeof(E *) * e->n);
    int c0 = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        k[i] = symbase(X, EK(e)[i], x, ch);
        c0 |= k[i] != EK(e)[i];
    }
    if (c0) e = X->rebuild(e, k);
    long dg;
    if (e->t == E_POW && freeof(K0(e), x) && !ISYM(K0(e), S_E) && !freeof(K1(e), x) &&
        poly_degree_in(K1(e), x, &dg) && dg <= 2) {
        *ch = 1;
        return s_exp(s_mul2(K1(e), s_fn1(F_LN, K0(e))));
    }
    return e;
}

/* ---- exponentials e^(c_i x) with commensurable rates: u = e^(r x / q) ---- */

typedef struct {
    E *r;     /* first rate */
    long q;   /* common denominator of the ratios */
    int bad, pass;
    E *u;
} ectx;

static long lcm2(long a, long b) {
    long x = a, y = b;
    while (y) {
        long t = x % y;
        x = y, y = t;
    }
    return a / x * b;
}

/* the rate c of e^w, w = c x (+ x-free part split off as a factor), or NULL */
static E *esub(XP, ectx *C, E *e, E *x) {
    if (C->bad || freeof(e, x)) return e;
    stack_check();
    E *w = NULL;
    if (e->t == E_POW && !freeof(K1(e), x)) {
        if (!freeof(K0(e), x)) {
            C->bad = 1;
            return e;
        }
        w = ISYM(K0(e), S_E) ? K1(e) : s_mul2(K1(e), s_fn1(F_LN, K0(e)));
    } else if (IFN(e, F_EXP))
        w = K0(e);
    if (w) {
        E *a, *c;
        long d;
        w = expand(w);
        if (!poly_degree_in(w, x, &d) || d != 1) {
            C->bad = 1;
            return e;
        }
        c = poly_coeff(w, x, 1), a = poly_coeff(w, x, 0);
        if (!C->r) C->r = c;
        E *ra = rat_normal(s_div(c, C->r));
        if (!is_exactnum(ra)) {
            C->bad = 1;
            return e;
        }
        if (C->pass == 0) {
            long den;
            if (is_rat(ra)) {
                if (!num_is_small_int(K1(ra), &den) || den > 12) C->bad = 1;
                else C->q = lcm2(C->q, den);
            }
            return e;
        }
        E *pw = s_mul2(ra, mk_int(C->q));
        return s_mul2(s_exp(a), s_pow(C->u, pw));
    }
    if (expr_eq(e, x) || (e->t != E_ADD && e->t != E_MUL && e->t != E_POW)) {
        C->bad = 1;
        return e;
    }
    E **k = salloc(sizeof(E *) * e->n);
    for (uint32_t i = 0; i < e->n; i++) k[i] = esub(X, C, EK(e)[i], x);
    return X->rebuild(e, k);
}

static E *ix_expsub(XP, E *f, E *x) {
    ectx C = {NULL, 1, 0, 0, NULL};
    esub(X, &C, f, x);
    if (C.bad || !C.r) return NULL;
    int fi = 0;
    C.u = ix_fresh(X, f, &fi), C.pass = 1;
    E *g = esub(X, &C, f, x);
    if (C.bad || !freeof(g, x)) return NULL;
    E *rq = s_div(C.r, mk_int(C.q));
    g = s_div(g, s_mul2(rq, C.u));
    E *G = ix_int(X, g, C.u);
    if (!G) return NULL;
    return subst(G, C.u, s_exp(s_mul2(rq, x)));
}

E *ix_special(XP, E *f, E *x) {
    int ch = 0;
    if (f->t == E_ADD && f->n <= 40) { /* termwise (the terms the core cannot do come here) */
        E **t = salloc(sizeof(E *) * f->n);
        for (uint32_t i = 0; i < f->n; i++) {
            E *q = EK(f)[i];
            t[i] = ix_int(X, q, x);
            if (!t[i]) t[i] = ix_special(X, q, x);
            if (!t[i]) return NULL;
        }
        return s_add(f->n, t);
    }
    E *g = symbase(X, f, x, &ch), *r;
    if (ch) {
        g = expand(g);
        if ((r = ix_int(X, g, x))) return r;
    }
    if ((r = ix_linfn(X, g, x))) return r;
    if ((r = ix_gauss(X, g, x))) return r;
    if ((r = ix_lnlin(X, g, x))) return r;
    if ((r = ix_lnsub(X, g, x))) return r;
    if ((r = ix_expsub(X, f, x))) return r;
    return NULL;
}
