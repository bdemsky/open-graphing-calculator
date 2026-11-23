/*
 * intalg.c -- elliptic integrals of algebraic integrands: R(x) times square roots of
 * polynomials (or rational functions) whose product P is a cubic or a quartic, in Legendre's
 * form F(phi|m), E(phi|m), Pi(n; phi|m) (ellip.c).
 *
 * The integrand is written R0/Q0 with Q0^2 = P0 squarefree of degree 3 or 4 (Q0 a product of
 * the integrand's own square roots, so no branch is changed).  A substitution sin(phi)^2 = w(x)
 * with parameter m is chosen from the roots of P0 such that phi'/sqrt(1 - m w) = kappa/Q0 with
 * kappa locally constant (kappa^2 is checked to be free of x):
 *   P0 even, u = x^2 roots u1, u2 real: a positive root u1: phi = asin(x/sqrt(u1)), m = u1/u2;
 *     both negative: phi = atan(x/sqrt(-u1)), m = 1 - u1/u2;  complex: phi = 2 atan(q x),
 *     q^4 = c4/c0, m = 1/2 - c2/(4 sqrt(c0 c4));
 *   three or four real roots e1 < e2 < e3 (< e4): w = (x - e1)/(e2 - e1), m = (e2-e1)/(e3-e1);
 *     w = l (x - e1)/(x - e4), l = (e2-e4)/(e2-e1), m = (e3-e4)(e2-e1)/((e2-e4)(e3-e1));
 *   a cubic c (x - e)(x^2 + p x + q) with complex roots (Byrd & Friedman 239, 240):
 *     A^2 = e^2 + p e + q, phi = acos((A -+ (x - e))/(A +- (x - e))), m = (A +- (e + p/2))/(2 A).
 * Then R0 = (T Q0)' Q0 + al kappa + be kappa (1 - m w) + sum ga_j kappa/(1 - n_j w) with T = A/D
 * rational (D from the poles of R0, A of bounded degree) and one Pi term per simple pole x_j
 * (n_j = 1/w(x_j)) is a linear system for the coefficients of A and al, be, ga_j; the
 * antiderivative is T Q0 + (al F + be E + sum ga_j Pi(n_j))/kappa.  For an even P0 the odd part
 * of R0 goes to the core (u = x^2).  Square roots in 1/kappa whose radicands differ by a
 * positive numeric factor are cancelled.  The result is checked by differentiation.
 */
#include "intx.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

#define NPI 6

typedef struct {
    E *phi, *w, *m;
    E *x1, *x2; /* two points of the one interval where the integrand is real (then 1/kappa is
                 * constant there and is written as a number), or NULL */
} ecand;

/* the complex value of e with the other symbols at sample values; 0 if not numeric */
static int sval(XP, E *e, E *x, double *re, double *im) {
    E *vs[17], *vals[17];
    uint32_t nv = 0;
    ix_syms(X, e, x, vs, &nv, 16);
    static const int16_t pv[8] = {731, 1172, 433, 1530, 917, 288, 1364, 615};
    for (uint32_t i = 0; i < nv; i++) vals[i] = mk_flt(pv[i % 8] / 1000.0 + 0.001 * i);
    char *pm = K->s_top;
    E *v = nv ? subst_many(e, nv, vs, vals) : e;
    int ok = X->eval_complex(v, re, im);
    K->s_top = pm;
    return ok;
}

static int sreal(XP, E *e, E *x, double *v) {
    double im;
    if (!sval(X, e, x, v, &im)) return 0;
    return __builtin_fabs(im) <= 1e-9 * (1 + __builtin_fabs(*v));
}

/* ---- the integrand as R * Q with Q a product of square roots ---- */

static int split(XP, E *f, E *x, E **R, E **Q, E **P) {
    E **fs = f->t == E_MUL ? EK(f) : &f;
    uint32_t n = f->t == E_MUL ? f->n : 1;
    E *r = K->c1, *q = K->c1, *p = K->c1;
    int nr = 0;
    for (uint32_t i = 0; i < n; i++) {
        E *g = fs[i];
        if (freeof(g, x) || ix_ratx(X, g, x)) {
            r = s_mul2(r, g);
            continue;
        }
        if (g->t == E_POW && is_rat(K1(g))) {
            long d;
            if (!num_is_small_int(K1(K1(g)), &d) || d != 2 || !ix_ratx(X, K0(g), x)) return 0;
            E *B = K0(g), *h = s_sub(K1(g), K->chalf);
            r = s_mul2(r, s_pow(B, h));
            q = s_mul2(q, s_pow(B, K->chalf));
            p = s_mul2(p, B);
            nr++;
            continue;
        }
        return 0;
    }
    if (!nr) return 0;
    *R = r, *Q = q, *P = p;
    return 1;
}

/* ---- linear algebra ---- */

static long esize(XP, E *e, long cap) {
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN) return 1;
    long n = 1;
    for (uint32_t i = 0; i < e->n && n <= cap; i++) n += esize(X, EK(e)[i], cap - n);
    return n;
}

/* the single numeric square root of e (sqrt(n), n an integer), NULL if none or several */
static void sqrt_find(XP, E *e, E **r, int *bad) {
    if (*bad || (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN)) return;
    if (e->t == E_POW && is_int(K0(e)) && expr_eq(K1(e), K->chalf)) {
        if (!*r) *r = e;
        else if (!expr_eq(*r, e)) *bad = 1;
        return;
    }
    for (uint32_t i = 0; i < e->n; i++) sqrt_find(X, EK(e)[i], r, bad);
}

/* rational normal form, denominators free of the square root rho (rho^2 numeric) */
static E *enorm(XP, E *e, E *rho) {
    e = rat_normal(e);
    if (!rho || freeof(e, rho)) return e;
    E *nu, *de;
    X->numden(e, &nu, &de);
    de = expand(de);
    if (freeof(de, rho)) return e;
    E *d0 = poly_coeff(de, rho, 0), *d1 = poly_coeff(de, rho, 1);
    E *n2 = s_mul2(K0(rho), K->c1);
    E *cj = s_sub(d0, s_mul2(d1, rho));
    return rat_normal(s_div(expand(s_mul2(nu, cj)), expand(s_sub(s_mul2(d0, d0), s_mul(3, (E *[]){n2, d1, d1})))));
}

/* solve the linear equations eqs (= 0) for vs (Gauss-Jordan, smallest pivots, entries in
 * rational normal form); values (free unknowns 0) into out */
int ix_lsolve(XP, uint32_t ne, E **eqs, uint32_t nv, E **vs, E **out) {
    if (ne > 60 || nv > 30) return 0;
    E *rho = NULL;
    int bad = 0;
    for (uint32_t i = 0; i < ne; i++) sqrt_find(X, eqs[i], &rho, &bad);
    if (bad) rho = NULL;
    uint32_t w = nv + 1;
    E **M = salloc(sizeof(E *) * ne * w), **z = salloc(sizeof(E *) * nv);
    for (uint32_t j = 0; j < nv; j++) z[j] = K->c0;
    for (uint32_t i = 0; i < ne; i++) {
        E *q = expand(eqs[i]);
        for (uint32_t j = 0; j < nv; j++) M[i * w + j] = enorm(X, diff(q, vs[j]), rho);
        M[i * w + nv] = enorm(X, s_neg(subst_many(q, nv, vs, z)), rho);
    }
    int *piv = salloc(sizeof(int) * nv);
    for (uint32_t j = 0; j < nv; j++) piv[j] = -1;
    uint32_t row = 0;
    for (uint32_t col = 0; col < nv && row < ne; col++) {
        poll_intr();
        long best = -1;
        uint32_t bi = 0;
        for (uint32_t i = row; i < ne; i++) {
            E *a = M[i * w + col];
            if (is_zero(a)) continue;
            long sz = esize(X, a, 400);
            if (best < 0 || sz < best) best = sz, bi = i;
        }
        if (best < 0) continue;
        for (uint32_t j = 0; j < w; j++) {
            E *t = M[row * w + j];
            M[row * w + j] = M[bi * w + j], M[bi * w + j] = t;
        }
        E *ip = s_inv(M[row * w + col]);
        for (uint32_t j = 0; j < w; j++) M[row * w + j] = enorm(X, s_mul2(M[row * w + j], ip), rho);
        for (uint32_t i = 0; i < ne; i++) {
            E *fc = M[i * w + col];
            if (i == row || is_zero(fc)) continue;
            for (uint32_t j = 0; j < w; j++)
                if (!is_zero(M[row * w + j])) M[i * w + j] = enorm(X, s_sub(M[i * w + j], s_mul2(fc, M[row * w + j])), rho);
        }
        piv[col] = (int)row++;
    }
    for (uint32_t i = row; i < ne; i++)
        if (!is_zero(M[i * w + nv])) return 0;
    for (uint32_t j = 0; j < nv; j++) out[j] = piv[j] < 0 ? K->c0 : M[(uint32_t)piv[j] * w + nv];
    /* free unknowns are 0: the pivot rows' other entries drop out */
    return 1;
}

/* the coefficients of the numerator of e (in x) as equations; 0 if not polynomial */
static int coeff_eqs(XP, E *e, E *x, E **eqs, uint32_t *ne, uint32_t cap) {
    E *num, *den;
    X->numden(rat_normal(e), &num, &den);
    num = expand(num);
    long d;
    if (!poly_degree_in(num, x, &d) || d < 0) return is_zero(num);
    if ((uint32_t)d + 1 > cap) return 0;
    for (long k = 0; k <= d; k++) {
        E *c = poly_coeff(num, x, k);
        if (!is_zero(c)) eqs[(*ne)++] = c;
    }
    return 1;
}

/* ---- 1/kappa: square roots simplified for real x ---- */

/* g (a polynomial in x with numeric coefficients) is > 0 for every real x: 1, else 0
 * (conservative: constants, quadratics, quartics even in x) */
static int pdpoly(XP, E *g, E *x) {
    long d;
    double c0, c1, c2;
    if (!poly_degree_in(g, x, &d)) return 0;
    if (d == 0) return is_num(g) && to_double(g, &c0) && c0 > 0;
    E *a = poly_coeff(g, x, d);
    if (!is_num(a) || !to_double(a, &c2) || !(c2 > 0)) return 0;
    if (d == 2 || (d == 4 && is_zero(poly_coeff(g, x, 3)) && is_zero(poly_coeff(g, x, 1)))) {
        long h = d / 2;
        E *b = poly_coeff(g, x, h), *c = poly_coeff(g, x, 0);
        if (!is_num(b) || !is_num(c) || !to_double(b, &c1) || !to_double(c, &c0)) return 0;
        if (c1 * c1 - 4 * c2 * c0 < 0) return 1;
        return d == 4 && c0 > 0 && c1 >= 0; /* both roots in u = x^2 negative */
    }
    return 0;
}

/* r rational in x is >= 0 for real x: every factor of numerator and denominator positive
 * definite or of even multiplicity; *out: r with the positive definite squares pulled out
 * (sqrt(r) = *pull * sqrt(*in)) */
static int nonneg(XP, E *r, E *x, E **pull, E **in) {
    E *nd[2];
    X->numden(rat_normal(r), &nd[0], &nd[1]);
    int ok = 1;
    *pull = K->c1, *in = K->c1;
    for (int s = 0; s < 2; s++) {
        E *ct, *fl = X->factor_list_in(expand(nd[s]), x, &ct);
        E *pin = ct, *pp = K->c1;
        double cv;
        if (!is_num(ct) || !to_double(ct, &cv) || !(cv > 0)) ok = 0;
        for (uint32_t i = 0; i < fl->n; i++) {
            E *g = K0(EK(fl)[i]);
            long k;
            num_is_small_int(K1(EK(fl)[i]), &k);
            int pd = pdpoly(X, g, x);
            if (!pd && (k & 1)) ok = 0;
            if (k >= 2 && (pd || !((k / 2) & 1))) pp = s_mul2(pp, s_powi(g, k / 2)), k &= 1;
            else if (k >= 4) pp = s_mul2(pp, s_powi(g, (k / 4) * 2)), k -= (k / 4) * 4;
            pin = s_mul2(pin, s_powi(g, k));
        }
        if (s) *pull = s_div(*pull, pp), *in = s_div(*in, pin);
        else *pull = s_mul2(*pull, pp), *in = s_mul2(*in, pin);
    }
    return ok;
}

static E *radpair(XP, E *e, E *x) {
    E **fs = e->t == E_MUL ? EK(e) : &e;
    uint32_t n = e->t == E_MUL ? e->n : 1;
    E **f = salloc(sizeof(E *) * (n + 2));
    int *sg = salloc(sizeof(int) * (n + 2));
    E *rest = K->c1;
    uint32_t nf = 0;
    for (uint32_t i = 0; i < n; i++) {
        E *g = fs[i];
        if (g->t == E_POW && !freeof(K0(g), x) && is_rat(K1(g)) && expr_eq(K1(K1(g)), K->c2)) {
            E *p = K1(g);
            int s = num_sgn(p) > 0 ? 1 : -1;
            rest = s_mul2(rest, s_pow(K0(g), s_sub(p, s > 0 ? K->chalf : K->cmhalf)));
            f[nf] = K0(g), sg[nf++] = s;
        } else
            rest = s_mul2(rest, g);
    }
    /* squares out, then nonnegative radicands merge into another radical (sqrt(u) sqrt(v) =
     * sqrt(u v) for u >= 0) */
    E *W = K->c1;
    int nw = 0;
    for (uint32_t i = 0; i < nf; i++) {
        E *pl, *in;
        if (!ix_ratx(X, f[i], x)) continue;
        int nn = nonneg(X, f[i], x, &pl, &in);
        rest = s_mul2(rest, sg[i] > 0 ? pl : s_inv(pl));
        f[i] = in;
        if (nn) {
            W = sg[i] > 0 ? s_mul2(W, f[i]) : s_div(W, f[i]);
            f[i] = NULL, nw++;
        }
    }
    if (nw) {
        uint32_t i = 0;
        while (i < nf && !f[i]) i++;
        if (i < nf) f[i] = rat_normal(sg[i] > 0 ? s_mul2(f[i], W) : s_div(f[i], W));
        else f[0] = rat_normal(W), sg[0] = 1;
        for (uint32_t i = 0; i < nf; i++) {
            E *pl, *in;
            if (!f[i] || !ix_ratx(X, f[i], x)) continue;
            nonneg(X, f[i], x, &pl, &in);
            rest = s_mul2(rest, sg[i] > 0 ? pl : s_inv(pl));
            f[i] = in;
        }
    }
    for (uint32_t i = 0; i < nf; i++) {
        if (!f[i] || sg[i] != 1) continue;
        for (uint32_t j = 0; j < nf; j++) {
            if (!f[j] || sg[j] != -1) continue;
            E *c = rat_normal(s_div(f[i], f[j]));
            double v;
            if (!freeof(c, x) || !is_num(c) || !to_double(c, &v) || !(v > 0)) continue;
            rest = s_mul2(rest, s_sqrt(c));
            f[i] = f[j] = NULL;
            break;
        }
    }
    rest = rat_normal(rest);
    for (uint32_t i = 0; i < nf; i++)
        if (f[i]) rest = s_mul2(rest, s_pow(f[i], sg[i] > 0 ? K->chalf : K->cmhalf));
    return rest;
}

/* ---- the reduction for one substitution ---- */

/* e does not depend on x (numerically, at three points, when rat_normal leaves radicals) */
static int xconst(XP, E *e, E *x) {
    if (freeof(e, x)) return 1;
    double r0 = 0, i0 = 0;
    static const int16_t xs[3] = {371, 1213, 2297};
    for (int j = 0; j < 3; j++) {
        double re, im;
        if (!sval(X, subst(e, x, mk_flt(xs[j] / 1000.0)), x, &re, &im)) return 0;
        if (!j) r0 = re, i0 = im;
        else if (__builtin_fabs(re - r0) + __builtin_fabs(im - i0) > 1e-10 * (1 + __builtin_fabs(r0) + __builtin_fabs(i0))) return 0;
    }
    return 1;
}

static E *fn2(XP, const char *nm, E *a, E *b) { return s_fn2(mk_sym_name(nm)->s, a, b); }

static E *ell_pi(XP, E *n, E *phi, E *m) {
    E *v[3] = {n, phi, m};
    E *P = mk_fn(mk_sym_name("elliptic_pi")->s, 3, v);
    P->f = F_SIMP;
    return P;
}

/* ---- radical constants as a symbol: rho = B^(1/k) for the linear algebra ---- */

static void rk_find(XP, E *e, E *x, E **B, long *k) {
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN) return;
    stack_check();
    if (e->t == E_POW && freeof(e, x) && is_rat(K1(e)) && !is_num(K0(e))) {
        long d;
        if (num_is_small_int(K1(K1(e)), &d) && d > 1 && d <= 12 && (!*B || expr_eq(*B, K0(e)))) {
            if (!*B) *B = K0(e), *k = d;
            else if (*k % d) *k = *k * d; /* not lcm, but a multiple */
            return;
        }
    }
    for (uint32_t i = 0; i < e->n; i++) rk_find(X, EK(e)[i], x, B, k);
}

static E *rk_sub(XP, E *e, E *B, long k, E *rho) {
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN && e->t != E_LIST) return e;
    stack_check();
    if (e->t == E_POW && expr_eq(K0(e), B) && is_rat(K1(e))) {
        E *pk = s_mul2(K1(e), mk_int(k));
        long v;
        if (num_is_small_int(pk, &v)) return s_powi(rho, v);
    }
    E **kk = salloc(sizeof(E *) * e->n);
    for (uint32_t i = 0; i < e->n; i++) kk[i] = rk_sub(X, EK(e)[i], B, k, rho);
    return X->rebuild(e, kk);
}

/* a parameter y of B with rho^k = B solved for y: *y, *yv; 0 if none */
static int rk_elim(XP, E *B, long k, E *rho, E *x, E **y, E **yv) {
    E *vs[8];
    uint32_t nv = 0;
    ix_syms(X, B, x, vs, &nv, 8);
    E *nu, *de, *rk = s_powi(rho, k);
    X->numden(rat_normal(B), &nu, &de);
    nu = expand(nu), de = expand(de);
    for (uint32_t i = 0; i < nv; i++) {
        long d1, d2;
        if (!poly_degree_in(nu, vs[i], &d1) || !poly_degree_in(de, vs[i], &d2)) continue;
        if (d1 == 1 && d2 == 0) { /* nu1 y + nu0 = rk de */
            *y = vs[i], *yv = rat_normal(s_div(s_sub(s_mul2(rk, de), poly_coeff(nu, vs[i], 0)), poly_coeff(nu, vs[i], 1)));
            return 1;
        }
        if (d1 == 0 && d2 == 1) { /* rk (de1 y + de0) = nu */
            *y = vs[i], *yv = rat_normal(s_div(s_sub(s_div(nu, rk), poly_coeff(de, vs[i], 0)), poly_coeff(de, vs[i], 1)));
            return 1;
        }
    }
    return 0;
}

static E *solve_cand(XP, E *f, E *R, E *P0, E *Q0, ecand *c, E *x, int *fi) {
    E *dphi = diff(c->phi, x);
    E *omw = s_sub(K->c1, s_mul2(c->m, c->w));
    E *k2 = rat_normal(s_div(s_mul2(s_mul2(dphi, dphi), P0), omw));
    IXD("alg k2", k2);
    if (is_zero(k2) || !xconst(X, k2, x)) { IXD("k2 not const", k2); return NULL; }
    poll_intr();
    E *Q0o = Q0, *wo = c->w, *mo = c->m, *rho = NULL, *rv = NULL;
    {
        E *B = NULL, *y, *yv;
        long k = 0;
        rk_find(X, c->w, x, &B, &k);
        rk_find(X, c->m, x, &B, &k);
        if (B) {
            int i0 = 90;
            rho = ix_fresh(X, f, &i0);
            if (rk_elim(X, B, k, rho, x, &y, &yv)) {
                rv = s_pow(B, mk_frac(1, k));
                c->w = rat_normal(subst(rk_sub(X, c->w, B, k, rho), y, yv));
                c->m = rat_normal(subst(rk_sub(X, c->m, B, k, rho), y, yv));
                R = subst(R, y, yv), P0 = expand(subst(P0, y, yv));
            } else
                rho = NULL;
        }
    }
    E *omwo = omw;
    omw = s_sub(K->c1, s_mul2(c->m, c->w));
    E *num, *den;
    X->numden(rat_normal(R), &num, &den);
    num = expand(num), den = expand(den);
    long dn, dd, dp;
    if (!poly_degree_in(num, x, &dn) || !poly_degree_in(den, x, &dd) || !poly_degree_in(P0, x, &dp)) return NULL;
    E *D = K->c1, *ns[NPI];
    int np = 0;
    long dD = 0;
    if (dd > 0) {
        E *ct, *fl = X->factor_list_in(den, x, &ct);
        for (uint32_t i = 0; i < fl->n; i++) {
            E *g = K0(EK(fl)[i]);
            long k, dg;
            num_is_small_int(K1(EK(fl)[i]), &k);
            if (!poly_degree_in(g, x, &dg) || dg < 1) continue;
            E *a2[2] = {P0, g}, *rm = try_cmd(C_REM, 2, a2);
            int onp = rm && is_zero(rat_normal(rm));
            long e = onp ? k : k - 1;
            if (e > 0) D = s_mul2(D, s_powi(g, e)), dD += e * dg;
            if (onp) continue;
            /* a pole off the roots of P0: Pi terms */
            E *rts[2];
            int nr = 0;
            if (dg == 1) rts[nr++] = rat_normal(s_neg(s_div(poly_coeff(g, x, 0), poly_coeff(g, x, 1))));
            else if (dg == 2) {
                E *a = poly_coeff(g, x, 2), *b = poly_coeff(g, x, 1), *cc = poly_coeff(g, x, 0);
                E *sq = s_sqrt(s_sub(s_mul2(b, b), s_mul(3, (E *[]){mk_int(4), a, cc})));
                rts[nr++] = s_div(s_add2(s_neg(b), sq), s_mul2(K->c2, a));
                rts[nr++] = s_div(s_sub(s_neg(b), sq), s_mul2(K->c2, a));
            } else
                return NULL;
            for (int j = 0; j < nr; j++) {
                E *wv = rat_normal(subst(c->w, x, rts[j]));
                if (is_zero(wv) || np == NPI) return NULL;
                E *nn = rat_normal(s_inv(wv));
                int dup = 0;
                for (int t = 0; t < np; t++) dup |= is_zero(rat_normal(s_sub(ns[t], nn)));
                if (!dup) ns[np++] = nn;
            }
        }
    }
    /* a quartic: the points at infinity give a third-kind term when w(inf) is finite */
    if (dp == 4 && np < NPI) {
        E *wn, *wd;
        long a1, a2;
        X->numden(rat_normal(c->w), &wn, &wd);
        wn = expand(wn), wd = expand(wd);
        if (poly_degree_in(wn, x, &a1) && poly_degree_in(wd, x, &a2) && a1 == a2 && a1 > 0) {
            E *wi = rat_normal(s_div(poly_coeff(wn, x, a1), poly_coeff(wd, x, a2)));
            if (!is_zero(wi) && !is_zero(rat_normal(s_sub(wi, K->c1)))) {
                E *nn = rat_normal(s_inv(wi));
                int dup = 0;
                for (int t = 0; t < np; t++) dup |= is_zero(rat_normal(s_sub(ns[t], nn)));
                if (!dup) ns[np++] = nn;
            }
        }
    }
    /* the denominator of w (poles of E') once */
    {
        E *wn, *wd, *ct;
        X->numden(rat_normal(c->w), &wn, &wd);
        long dw;
        if (poly_degree_in(wd, x, &dw) && dw > 0) {
            E *fl = X->factor_list_in(expand(wd), x, &ct);
            for (uint32_t i = 0; i < fl->n; i++) {
                E *g = K0(EK(fl)[i]);
                E *a2[2] = {D, g}, *rm = try_cmd(C_REM, 2, a2);
                long dg;
                if (rm && is_zero(rat_normal(rm))) continue;
                if (!poly_degree_in(g, x, &dg)) return NULL;
                D = s_mul2(D, g), dD += dg;
            }
        }
    }
    long dA = dD + (dn - dd - dp / 2 > 0 ? dn - dd - dp / 2 : 0) + 2;
    if (dA > 10) return NULL;
    uint32_t nu = (uint32_t)dA + 1 + 2 + (uint32_t)np;
    E **u = salloc(sizeof(E *) * nu), **val = salloc(sizeof(E *) * nu);
    for (uint32_t i = 0; i < nu; i++) u[i] = ix_fresh(X, f, fi);
    E *A = K->c0;
    for (long i = 0; i <= dA; i++) A = s_add2(A, s_mul2(u[i], s_powi(x, i)));
    E *al = u[dA + 1], *be = u[dA + 2];
    E *T = s_div(A, D);
    E *id = s_add(3, (E *[]){s_mul2(diff(T, x), P0), s_mul(3, (E *[]){K->chalf, T, diff(P0, x)}), al});
    id = s_add2(id, s_mul2(be, omw));
    for (int j = 0; j < np; j++) id = s_add2(id, s_div(u[dA + 3 + j], s_sub(K->c1, s_mul2(ns[j], c->w))));
    id = s_sub(id, R);
    uint32_t cap = 64, ne = 0;
    E **eqs = salloc(sizeof(E *) * cap);
    if (!coeff_eqs(X, id, x, eqs, &ne, cap)) return NULL;
    poll_intr();
    IXD("alg id", id);
    if (!ix_lsolve(X, ne, eqs, nu, u, val)) return NULL;
    IXD("alg solved", T);
    /* the result */
    E *Ts = rat_normal(subst_many(T, nu, u, val));
    if (rho) {
        Ts = subst(Ts, rho, rv);
        for (uint32_t i = 0; i < nu; i++) val[i] = subst(val[i], rho, rv);
        for (int j = 0; j < np; j++) ns[j] = subst(ns[j], rho, rv);
        c->w = wo, c->m = mo;
    }
    Q0 = Q0o, omw = omwo;
    E *ik = s_div(s_sqrt(omw), s_mul2(dphi, Q0));
    if (c->x1) { /* one interval: the constant 1/kappa */
        E *kc = expand(rat_normal(subst(k2, x, c->x1))), *g = s_pow(kc, K->cmhalf);
        double a, b, a2, b2;
        int same = sval(X, g, x, &a2, &b2) && __builtin_fabs(b2) < 1e-12 * __builtin_fabs(a2), sg = 0;
        for (int j = 0; j < 2 && same; j++) {
            same = sval(X, subst(ik, x, j ? c->x2 : c->x1), x, &a, &b) && __builtin_fabs(b) < 1e-9 * __builtin_fabs(a) &&
                   __builtin_fabs(__builtin_fabs(a) - __builtin_fabs(a2)) < 1e-9 * __builtin_fabs(a2);
            int s1 = (a > 0) == (a2 > 0) ? 1 : -1;
            if (same && sg && s1 != sg) same = 0;
            sg = s1;
        }
        if (same && sg < 0) g = s_neg(g);
        ik = same ? g : radpair(X, ik, x);
    } else
        ik = radpair(X, ik, x);
    E *ell = s_add2(s_mul2(val[dA + 1], fn2(X, "elliptic_f", c->phi, c->m)),
                    s_mul2(val[dA + 2], fn2(X, "elliptic_e", c->phi, c->m)));
    for (int j = 0; j < np; j++) ell = s_add2(ell, s_mul2(val[dA + 3 + j], ell_pi(X, ns[j], c->phi, c->m)));
    if (is_zero(ell)) return NULL; /* elementary: the core's business */
    return s_add2(s_mul2(Ts, Q0), s_mul2(ik, ell));
}

/* ---- substitutions from the roots ---- */

static int add_cand(ecand *cs, int *nc, E *phi, E *w, E *m) {
    if (*nc >= 4) return 0;
    cs[*nc].phi = phi, cs[*nc].w = w, cs[*nc].m = m, cs[*nc].x1 = cs[*nc].x2 = NULL;
    (*nc)++;
    return 1;
}

static void even_cands(XP, E *P0, E *x, ecand *cs, int *nc) {
    E *c4 = poly_coeff(P0, x, 4), *c2 = poly_coeff(P0, x, 2), *c0 = poly_coeff(P0, x, 0);
    if (is_zero(c0) || is_zero(c4)) return;
    E *u[2] = {NULL, NULL};
    E *ct, *fl = X->factor_list_in(P0, x, &ct);
    if (fl->n == 2) {
        for (int i = 0; i < 2; i++) {
            E *g = K0(EK(fl)[i]);
            long dg;
            if (!poly_degree_in(g, x, &dg) || dg != 2 || !is_zero(poly_coeff(g, x, 1))) break;
            u[i] = rat_normal(s_neg(s_div(poly_coeff(g, x, 0), poly_coeff(g, x, 2))));
        }
    } else if (fl->n == 1 && (expr_eq(K1(EK(fl)[0]), K->c2))) {
        E *g = K0(EK(fl)[0]); /* (p + q x^2)^2: not squarefree */
        (void)g;
        return;
    }
    if (!u[0] || !u[1]) {
        E *dl = s_sub(s_mul2(c2, c2), s_mul(3, (E *[]){mk_int(4), c4, c0}));
        double dv;
        if (!sreal(X, dl, x, &dv)) return;
        if (dv < 0) { /* complex roots: phi = 2 atan(q x) */
            E *q = s_pow(s_div(c4, c0), mk_frac(1, 4));
            E *m = s_sub(K->chalf, s_div(c2, s_mul2(mk_int(4), s_sqrt(s_mul2(c0, c4)))));
            E *t = s_mul2(q, x), *t2 = s_add2(K->c1, s_mul2(t, t));
            if (add_cand(cs, nc, s_mul2(K->c2, s_fn1(F_ATAN, t)), s_div(s_mul(3, (E *[]){mk_int(4), t, t}), s_mul2(t2, t2)), m))
                cs[*nc - 1].x1 = K->c0, cs[*nc - 1].x2 = K->c1; /* no real root: one interval */
            return;
        }
        if (is_zero(c2)) {
            E *sq = s_sqrt(s_neg(s_div(c0, c4)));
            u[0] = s_neg(sq), u[1] = sq;
        } else {
            E *sq = s_sqrt(dl);
            u[0] = s_div(s_sub(s_neg(c2), sq), s_mul2(K->c2, c4));
            u[1] = s_div(s_add2(s_neg(c2), sq), s_mul2(K->c2, c4));
        }
    }
    double v0, v1;
    if (!sreal(X, u[0], x, &v0) || !sreal(X, u[1], x, &v1)) return;
    if (v0 == 0 || v1 == 0) return;
    int pos0 = v0 > 0, pos1 = v1 > 0;
    double lv;
    if ((pos0 != pos1) && sreal(X, c4, x, &lv) && lv > 0) { /* real for x^2 > u_j: phi = acos(sqrt(u_j)/x) */
        E *uj = pos0 ? u[0] : u[1], *uo = pos0 ? u[1] : u[0];
        E *w = s_sub(K->c1, s_div(uj, s_mul2(x, x)));
        add_cand(cs, nc, s_fn1(F_ACOS, s_div(s_sqrt(uj), x)), w, rat_normal(s_div(s_neg(uo), s_sub(uj, uo))));
    }
    if (pos0 || pos1) {
        /* asin on the smaller positive root */
        int j = pos0 && pos1 ? (v0 < v1 ? 0 : 1) : (pos0 ? 0 : 1);
        E *uj = u[j], *uo = u[1 - j];
        E *s = s_pow(uj, K->cmhalf);
        add_cand(cs, nc, s_fn1(F_ASIN, s_mul2(s, x)), s_div(s_mul2(x, x), uj), rat_normal(s_div(uj, uo)));
        if (pos0 && pos1) { /* also the other root (m > 1 there) */
            s = s_pow(uo, K->cmhalf);
            add_cand(cs, nc, s_fn1(F_ASIN, s_mul2(s, x)), s_div(s_mul2(x, x), uo), rat_normal(s_div(uo, uj)));
        }
        return;
    }
    int j = -v0 < -v1 ? 0 : 1;
    E *uj = u[j], *uo = u[1 - j];
    E *s = s_pow(s_neg(uj), K->cmhalf), *t = s_mul2(s, x);
    E *t2 = s_mul2(t, t);
    if (add_cand(cs, nc, s_fn1(F_ATAN, t), s_div(t2, s_add2(K->c1, t2)), rat_normal(s_sub(K->c1, s_div(uj, uo)))))
        cs[*nc - 1].x1 = K->c0, cs[*nc - 1].x2 = K->c1; /* no real root: one interval */
}

/* the roots of P0 from its factors: linear factors, quadratics by the formula, binomial
 * cubics a + b x^3; *cplx: a complex pair (cubic only: e real, quadratic in qd) */
static int roots(XP, E *P0, E *x, E **e, int *ne, E **qd, E **lc) {
    E *ct, *fl = X->factor_list_in(P0, x, &ct);
    *ne = 0, *qd = NULL;
    *lc = ct;
    for (uint32_t i = 0; i < fl->n; i++) {
        E *g = K0(EK(fl)[i]);
        long dg, k;
        num_is_small_int(K1(EK(fl)[i]), &k);
        if (k != 1 || !poly_degree_in(g, x, &dg)) return 0;
        E *l = poly_coeff(g, x, dg);
        *lc = s_mul2(*lc, l);
        if (dg == 0) continue;
        if (dg == 1) {
            e[(*ne)++] = rat_normal(s_neg(s_div(poly_coeff(g, x, 0), l)));
        } else if (dg == 2) {
            E *b = s_div(poly_coeff(g, x, 1), l), *c = s_div(poly_coeff(g, x, 0), l);
            E *dl = s_sub(s_mul2(b, b), s_mul2(mk_int(4), c));
            double dv;
            if (!sreal(X, dl, x, &dv)) return 0;
            if (dv <= 0) {
                if (*qd) return 0;
                *qd = expand(s_div(g, l));
                continue;
            }
            E *sq = s_sqrt(dl);
            e[(*ne)++] = s_div(s_sub(s_neg(b), sq), K->c2);
            e[(*ne)++] = s_div(s_add2(s_neg(b), sq), K->c2);
        } else if (dg == 3 && is_zero(poly_coeff(g, x, 1)) && is_zero(poly_coeff(g, x, 2))) {
            E *r = s_pow(s_div(poly_coeff(g, x, 0), l), mk_frac(1, 3));
            e[(*ne)++] = s_neg(r);
            if (*qd) return 0;
            *qd = s_add(3, (E *[]){s_mul2(x, x), s_neg(s_mul2(r, x)), s_mul2(r, r)});
        } else
            return 0;
    }
    return 1;
}

/* every radicand of f is positive at x = xm (f a product) */
static int radpos(XP, E *f, E *x, double xm) {
    E **fs = f->t == E_MUL ? EK(f) : &f;
    uint32_t n = f->t == E_MUL ? f->n : 1;
    for (uint32_t i = 0; i < n; i++) {
        double v;
        if (fs[i]->t == E_POW && !freeof(K0(fs[i]), x) && !is_int(K1(fs[i])) &&
            (!sreal(X, subst(K0(fs[i]), x, mk_flt(xm)), x, &v) || !(v > 0)))
            return 0;
    }
    return 1;
}

static void root_cands(XP, E *f, E *P0, E *x, long dp, ecand *cs, int *nc) {
    E *e[4], *qd, *lc;
    int ne;
    if (!roots(X, P0, x, e, &ne, &qd, &lc)) return;
    if (qd && dp == 3 && ne == 1) { /* Byrd & Friedman 239 / 240 */
        double lv;
        if (!sreal(X, lc, x, &lv) || lv == 0) return;
        E *p = poly_coeff(qd, x, 1), *q = poly_coeff(qd, x, 0), *e0 = e[0];
        E *A2 = rat_normal(s_add(3, (E *[]){s_mul2(e0, e0), s_mul2(p, e0), q})), *A = NULL;
        E *ta = is_zero(e0) ? NULL : rat_normal(s_div(A2, s_mul2(e0, e0)));
        double tv, ev;
        if (ta && is_num(ta) && to_double(ta, &tv) && tv > 0 && sreal(X, e0, x, &ev)) /* A = sqrt(t) |e| */
            A = s_mul2(s_sqrt(ta), ev < 0 ? s_neg(e0) : e0);
        if (!A) A = s_sqrt(A2);
        E *b1 = s_neg(s_div(p, K->c2));
        E *t = lv > 0 ? s_sub(x, e0) : s_sub(e0, x);
        E *mu = s_div(s_sub(A, t), s_add2(A, t));
        E *m = lv > 0 ? s_div(s_add2(A, s_sub(b1, e0)), s_mul2(K->c2, A)) : s_div(s_sub(A, s_sub(b1, e0)), s_mul2(K->c2, A));
        if (add_cand(cs, nc, s_fn1(F_ACOS, mu), s_sub(K->c1, s_mul2(mu, mu)), expand(rat_normal(m)))) {
            cs[*nc - 1].x1 = s_add2(e0, lv > 0 ? K->c1 : K->cm1);
            cs[*nc - 1].x2 = s_add2(e0, mk_int(lv > 0 ? 3 : -3));
        }
        return;
    }
    if (qd || ne != dp) return;
    double v[4];
    for (int i = 0; i < ne; i++)
        if (!sreal(X, e[i], x, &v[i])) return;
    for (int i = 0; i < ne; i++) /* ascending */
        for (int j = i + 1; j < ne; j++)
            if (v[j] < v[i]) {
                double tv = v[i]; v[i] = v[j]; v[j] = tv;
                E *te = e[i]; e[i] = e[j]; e[j] = te;
            }
    /* the interval (r_i, r_(i+1)) (cyclic, a cubic's fourth root is infinity) where f is real
     * goes to w in (0, 1): w(r_i) = 0, w(r_(i+1)) = 1, w(r_(i+2)) = 1/m, w(r_(i+3)) = inf */
    E *r[4] = {e[0], e[1], e[2], ne == 4 ? e[3] : NULL};
    for (int i = 0; i < 4; i++) {
        int ib = (i + 1) & 3;
        double xm = !r[i] ? v[0] - 1 : !r[ib] || ib == 0 ? v[i] + 1 : (v[i] + v[ib]) / 2;
        if (!radpos(X, f, x, xm) && !(ib == 0 && r[i] && radpos(X, f, x, v[0] - 1))) continue; /* (r3, r0) through inf */
        E *a = r[i], *b = r[ib], *cc = r[(i + 3) & 3], *d = r[(i + 2) & 3];
        E *w;
        if (!a) w = s_div(s_sub(b, cc), s_sub(x, cc));
        else if (!b) w = s_div(s_sub(x, a), s_sub(x, cc));
        else if (!cc) w = s_div(s_sub(x, a), s_sub(b, a));
        else w = s_div(s_mul2(s_sub(x, a), s_sub(b, cc)), s_mul2(s_sub(x, cc), s_sub(b, a)));
        E *wd = d ? subst(w, x, d) : s_div(s_sub(b, cc), s_sub(b, a));
        E *m = rat_normal(s_inv(wd));
        add_cand(cs, nc, s_fn1(F_ASIN, s_sqrt(w)), w, m);
        return;
    }
}

/* ---- entry ---- */

/* R/sqrt(P) = (T sqrt(P))' + R'/sqrt(P) with T a Laurent polynomial: the polynomial part of R
 * lowered below degree dp - 1, and (when R's denominator is a power of x and P(0) != 0) the
 * powers x^(-k), k >= 2, removed; returns T, *R becomes R' */
static E *reduce(XP, E **Rp, E *P0, E *x, long dp) {
    E *R = rat_normal(*Rp), *nu, *de, *T = K->c0;
    X->numden(R, &nu, &de);
    nu = expand(nu), de = expand(de);
    long dn, dd;
    if (!poly_degree_in(nu, x, &dn) || !poly_degree_in(de, x, &dd)) return T;
    E *lcP = poly_coeff(P0, x, dp), *P0d = diff(P0, x), *p0 = poly_coeff(P0, x, 0);
    int lau = dd >= 0 && is_zero(expand(s_sub(de, s_mul2(poly_coeff(de, x, dd), s_powi(x, dd)))));
    E *a2[2] = {nu, de}, *qr = NULL;
    E *Q = K->c0, *Rr = R;
    if (lau) Q = expand(R), Rr = K->c0; /* a Laurent polynomial */
    else if (dn >= dd && (qr = try_cmd(C_DIVIDE, 2, a2)) && qr->t == E_LIST && qr->n == 2) {
        Q = expand(K0(qr)), Rr = s_div(K1(qr), de);
    } else
        return T;
    for (int it = 0; it < 40; it++) { /* top terms */
        long n;
        E *top = NULL;
        E *qn, *qd;
        X->numden(rat_normal(Q), &qn, &qd);
        if (!poly_degree_in(expand(qn), x, &n)) break;
        long dq;
        poly_degree_in(qd, x, &dq);
        n -= dq;
        if (n < dp - 1) break;
        top = rat_normal(s_div(poly_coeff(expand(qn), x, n + dq), poly_coeff(expand(qd), x, dq)));
        long j = n - dp + 1;
        E *cf = s_div(top, s_mul2(s_add2(mk_int(j), mk_frac(dp, 2)), lcP));
        T = s_add2(T, s_mul2(cf, s_powi(x, j)));
        Q = expand(s_sub(Q, s_mul2(cf, s_add2(s_mul(3, (E *[]){mk_int(j), s_powi(x, j - 1), P0}), s_mul(3, (E *[]){K->chalf, s_powi(x, j), P0d})))));
    }
    if (lau && !is_zero(p0)) {
        for (int it = 0; it < 40; it++) { /* bottom terms x^i, i <= -2 */
            E *Qx = rat_normal(Q), *qn, *qd;
            X->numden(Qx, &qn, &qd);
            qn = expand(qn), qd = expand(qd);
            long k;
            if (!poly_degree_in(qd, x, &k) || k < 2) break;
            E *lo = NULL;
            long l = 0;
            for (l = 0; l <= k; l++)
                if (!is_zero(lo = poly_coeff(qn, x, l))) break;
            if (!lo || is_zero(lo) || l > k - 2) break;
            long i0 = l - k, j = i0 + 1;
            E *cf = s_div(s_div(lo, poly_coeff(qd, x, k)), s_mul2(mk_int(j), p0));
            T = s_add2(T, s_mul2(cf, s_powi(x, j)));
            Q = expand(s_sub(Q, s_mul2(cf, s_add2(s_mul(3, (E *[]){mk_int(j), s_powi(x, j - 1), P0}), s_mul(3, (E *[]){K->chalf, s_powi(x, j), P0d})))));
        }
    }
    *Rp = rat_normal(s_add2(Q, Rr));
    return T;
}

static E *alg0(XP, E *f, E *x) {
    E *R, *Q, *P;
    if (!split(X, f, x, &R, &Q, &P)) return NULL;
    E *N, *Dn;
    X->numden(rat_normal(P), &N, &Dn);
    E *Q1 = s_mul2(Q, Dn), *P1 = expand(s_mul2(N, Dn));
    E *R1 = s_mul2(R, N);
    /* squarefree part */
    E *ct, *fl = X->factor_list_in(P1, x, &ct);
    E *P0 = ct, *S = K->c1;
    for (uint32_t i = 0; i < fl->n; i++) {
        E *g = K0(EK(fl)[i]);
        long k;
        if (!num_is_small_int(K1(EK(fl)[i]), &k)) return NULL;
        if (k & 1) P0 = s_mul2(P0, g);
        if (k > 1) S = s_mul2(S, s_powi(g, k / 2));
    }
    P0 = expand(P0);
    long dp;
    if (!poly_degree_in(P0, x, &dp) || dp < 3 || dp > 4) return NULL;
    E *Q0 = s_div(Q1, S), *R0 = rat_normal(s_div(R1, S));
    ecand cs[4];
    int nc = 0;
    int even = dp == 4 && is_zero(expand(s_sub(subst(P0, x, s_neg(x)), P0)));
    if (even) even_cands(X, P0, x, cs, &nc);
    else root_cands(X, f, P0, x, dp, cs, &nc);
    if (!nc) return NULL;
    E *Re = R0, *odd = NULL;
    if (even) {
        E *Rm = subst(R0, x, s_neg(x));
        Re = rat_normal(s_div(s_add2(R0, Rm), K->c2));
        E *Ro = rat_normal(s_div(s_sub(R0, Rm), K->c2));
        if (!is_zero(Ro)) {
            odd = ix_int(X, s_div(Ro, Q0), x);
            if (!odd) return NULL;
        }
        if (is_zero(Re)) return NULL;
    }
    E *Tp = reduce(X, &Re, P0, x, dp);
    int fi = 60;
    for (int i = 0; i < nc; i++) {
        char *mk = K->s_top;
        E *r = solve_cand(X, f, Re, P0, Q0, &cs[i], x, &fi);
        if (r) {
            if (!is_zero(Tp)) r = s_add2(r, s_mul2(Tp, Q0));
            if (odd) r = s_add2(r, odd);
            if (ix_verify(X, f, r, x)) return r;
        }
        K->s_top = mk;
    }
    return NULL;
}

/* (c x)^(k/2) factors: t = sqrt(c x) (exact: x = t^2/c, dx = 2 t/c dt) makes an odd P even */
static E *alg_sqx(XP, E *f, E *x) {
    if (f->t != E_MUL) return NULL;
    for (uint32_t i = 0; i < f->n; i++) {
        E *g = EK(f)[i], *B, *c;
        long d;
        if (g->t != E_POW || !is_rat(K1(g)) || !expr_eq(K1(K1(g)), K->c2)) continue;
        B = K0(g);
        if (!poly_degree_in(B, x, &d) || d != 1 || !is_zero(poly_coeff(B, x, 0))) continue;
        c = poly_coeff(B, x, 1);
        int fi = 80;
        E *t = ix_fresh(X, f, &fi);
        E **k = salloc(sizeof(E *) * f->n);
        for (uint32_t j = 0; j < f->n; j++) k[j] = j == i ? s_pow(t, s_mul2(K->c2, K1(g))) : subst(EK(f)[j], x, s_div(s_mul2(t, t), c));
        E *h = s_mul2(s_mul(f->n, k), s_div(s_mul2(K->c2, t), c));
        E *r = alg0(X, h, t);
        if (!r) return NULL;
        r = subst(r, t, s_sqrt(B));
        return ix_verify(X, f, r, x) ? r : NULL;
    }
    return NULL;
}

static E *algell_p(XP, void **a) {
    E *f = a[0], *x = a[1];
    char *mk = K->s_top;
    E *r = alg0(X, f, x);
    if (r) return r;
    K->s_top = mk;
    if ((r = alg_sqx(X, f, x))) return r;
    K->s_top = mk;
    return NULL;
}

/* ---- trigonometric integrands: C = cos u (sin u, cosh u) ---- */

typedef struct {
    E *x, *u, *C, *S;
    int fam; /* 0 cos, 1 sin, 2 cosh */
    int bad;
} tctx;

static void t_findu(XP, tctx *c, E *e) {
    if (c->bad || freeof(e, c->x) || (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN)) return;
    stack_check();
    if (e->t == E_FN) {
        int t = IFN(e, F_SIN) || IFN(e, F_COS) || IFN(e, F_TAN) || IFN(e, F_COT) || IFN(e, F_SEC) || IFN(e, F_CSC);
        int h = IFN(e, F_SINH) || IFN(e, F_COSH) || IFN(e, F_TANH);
        if (t || h) {
            if (h && c->fam != 2) c->bad = 1;
            if (t && c->fam == 2) c->bad = 1;
            if (!c->u) c->u = K0(e);
            else if (!expr_eq(c->u, K0(e))) c->bad = 1;
            return;
        }
        if (e->s < S__FIRSTCMD && e->s != F_SQRT) {
            c->bad = 1;
            return;
        }
    }
    for (uint32_t i = 0; i < e->n; i++) t_findu(X, c, EK(e)[i]);
}

static E *t_conv(XP, tctx *c, E *e) {
    if (freeof(e, c->x) || (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN)) return e;
    stack_check();
    if (e->t == E_FN && e->n == 1 && expr_eq(K0(e), c->u)) {
        E *C = c->C, *S = c->S;
        if (c->fam == 1) { /* sin u is the variable */
            switch (e->s) {
            case F_SIN: return C;
            case F_COS: return S;
            case F_TAN: return s_div(C, S);
            case F_COT: return s_div(S, C);
            case F_SEC: return s_inv(S);
            case F_CSC: return s_inv(C);
            default: break;
            }
        } else {
            switch (e->s) {
            case F_SIN: case F_SINH: return S;
            case F_COS: case F_COSH: return C;
            case F_TAN: case F_TANH: return s_div(S, C);
            case F_COT: return s_div(C, S);
            case F_SEC: return s_inv(C);
            case F_CSC: return s_inv(S);
            default: break;
            }
        }
    }
    E **k = salloc(sizeof(E *) * e->n);
    for (uint32_t i = 0; i < e->n; i++) k[i] = t_conv(X, c, EK(e)[i]);
    return X->rebuild(e, k);
}

static E *trig_fam(XP, E *f, E *x, int fam) {
    tctx c0, *c = &c0;
    c->x = x, c->u = NULL, c->fam = fam, c->bad = 0;
    t_findu(X, c, f);
    if (c->bad || !c->u) return NULL;
    E *du = diff(c->u, x);
    long dg;
    if (!poly_degree_in(c->u, x, &dg) || dg != 1 || !freeof(du, x)) return NULL;
    int fi = 70;
    c->C = ix_fresh(X, f, &fi), c->S = ix_fresh(X, f, &fi);
    E *g = t_conv(X, c, f);
    if (!freeof(g, x)) return NULL;
    /* g(C, S) = ge(C) + S go(C), S^2 = 1 - C^2 (cosh: C^2 - 1) */
    E *s2 = fam == 2 ? s_sub(s_mul2(c->C, c->C), K->c1) : s_sub(K->c1, s_mul2(c->C, c->C));
    E *gm = subst(g, c->S, s_neg(c->S));
    E *ge = subst(s_div(s_add2(g, gm), K->c2), c->S, s_sqrt(s2));
    E *go = subst(s_div(s_div(s_sub(g, gm), K->c2), c->S), c->S, s_sqrt(s2));
    ge = rat_normal(ge), go = rat_normal(go);
    if (!freeof(ge, c->S) || !freeof(go, c->S)) return NULL;
    E *cu = s_fn1(fam == 2 ? F_COSH : fam ? F_SIN : F_COS, c->u);
    E *su = s_fn1(fam == 2 ? F_SINH : fam ? F_COS : F_SIN, c->u);
    /* dC = s du, s = -sin u (cos), cos u (sin), sinh u (cosh); s = +-sg Sh with
     * Sh = sqrt(1 - C) sqrt(1 + C) and sg = s/Sh locally constant */
    E *Sh = fam == 2 ? s_mul2(s_sqrt(s_sub(c->C, K->c1)), s_sqrt(s_add2(c->C, K->c1)))
                     : s_mul2(s_sqrt(s_sub(K->c1, c->C)), s_sqrt(s_add2(K->c1, c->C)));
    E *res = K->c0;
    if (!is_zero(ge)) {
        E *H = alg0(X, s_div(ge, Sh), c->C);
        if (!H) return NULL;
        E *sg = s_div(fam == 0 ? s_neg(su) : su, subst(Sh, c->C, cu));
        res = s_mul2(sg, subst(H, c->C, cu));
    }
    if (!is_zero(go)) { /* S go(C) du = go(C) dC (cos: -) */
        E *H = ix_int(X, go, c->C);
        if (!H) return NULL;
        H = subst(H, c->C, cu);
        res = s_add2(res, fam == 0 ? s_neg(H) : H);
    }
    res = s_div(res, du);
    return ix_verify(X, f, res, x) ? res : NULL;
}

static E *trigalg_p(XP, void **a) {
    E *f = a[0], *x = a[1];
    char *mk = K->s_top;
    for (int fam = 0; fam < 3; fam++) {
        E *r = trig_fam(X, f, x, fam);
        if (r) return r;
        K->s_top = mk;
    }
    return NULL;
}

/* errors inside (a failed factorisation, memory) mean "not found" */
E *ix_algell(XP, E *f, E *x) {
    void *a[2] = {f, x};
    char *mk = K->s_top;
    E *r = X->protect(algell_p, a);
    if (!r) K->s_top = mk;
    return r;
}

E *ix_trigalg(XP, E *f, E *x) {
    void *a[2] = {f, x};
    char *mk = K->s_top;
    E *r = X->protect(trigalg_p, a);
    if (!r) K->s_top = mk;
    return r;
}

/* ---- sin(u)^p cos(u)^q with p or q not an integer: Gauss's hypergeometric function ----
 * int s^p (1 - s^2)^((q-1)/2) ds = s^(p+1)/(p+1) 2F1((1-q)/2, (p+1)/2; (p+3)/2; s^2), s = sin u,
 * times the locally constant cos(u)^(q+1) (cos(u)^2)^(-(q+1)/2); the integrand's own powers
 * (tan, sec, (a sin u)^p, ...) differ from sin^p cos^q by a locally constant factor G
 * (hyperbolic: s = sinh u, 2F1(...; -s^2)). */
static E *th_part(XP, E *e, E *x, E **u, int *hyp, E **p, E **q) {
    E *b = e, *k = K->c1;
    if (e->t == E_POW) b = K0(e), k = K1(e);
    if (b->t != E_FN || b->n != 1 || freeof(b, x) || !is_num(k)) return NULL;
    if (*u && !expr_eq(*u, K0(b))) return NULL;
    int h = IFN(b, F_SINH) || IFN(b, F_COSH) || IFN(b, F_TANH);
    if (*u && h != *hyp) return NULL;
    E *dp = K->c0, *dq = K->c0;
    switch (b->s) {
    case F_SIN: case F_SINH: dp = k; break;
    case F_COS: case F_COSH: dq = k; break;
    case F_TAN: case F_TANH: dp = k, dq = s_neg(k); break;
    case F_COT: dp = s_neg(k), dq = k; break;
    case F_SEC: dq = s_neg(k); break;
    case F_CSC: dp = s_neg(k); break;
    default: return NULL;
    }
    *u = K0(b), *hyp = h;
    *p = s_add2(*p, dp), *q = s_add2(*q, dq);
    return e;
}

static E *trighyp_p(XP, void **a) {
    E *f = a[0], *x = a[1];
    E **fs = f->t == E_MUL ? EK(f) : &f;
    uint32_t n = f->t == E_MUL ? f->n : 1;
    E *u = NULL, *p = K->c0, *q = K->c0, *cst = K->c1;
    int hyp = 0, nonint = 0;
    for (uint32_t i = 0; i < n; i++) {
        E *g = fs[i];
        if (freeof(g, x)) {
            cst = s_mul2(cst, g);
            continue;
        }
        E *b = g->t == E_POW && K0(g)->t == E_MUL ? K0(g) : NULL; /* (a sin u)^k */
        if (b) {
            for (uint32_t j = 0; j < b->n; j++) {
                E *t = EK(b)[j];
                if (freeof(t, x)) continue;
                if (!th_part(X, s_pow(t, K1(g)), x, &u, &hyp, &p, &q)) return NULL;
            }
        } else if (!th_part(X, g, x, &u, &hyp, &p, &q))
            return NULL;
        if (g->t == E_POW && !is_int(K1(g))) nonint = 1;
    }
    if (!u || !nonint || !is_num(p) || !is_num(q) || (is_int(p) && is_int(q))) return NULL;
    E *du = diff(u, x);
    long dg;
    if (!poly_degree_in(u, x, &dg) || dg != 1 || !freeof(du, x)) return NULL;
    E *S = s_fn1(hyp ? F_SINH : F_SIN, u), *C = s_fn1(hyp ? F_COSH : F_COS, u);
    E *base = s_mul2(s_pow(S, p), s_pow(C, q));
    E *G = s_div(s_div(f, cst), base);
    E *F;
    E *p1 = s_add2(p, K->c1), *q1 = s_add2(q, K->c1);
    int h2 = mk_sym_name("hyp2f1")->s;
    if (!is_zero(p1)) {
        E *v[4] = {s_div(s_sub(K->c1, q), K->c2), s_div(p1, K->c2), s_div(s_add2(p, mk_int(3)), K->c2),
                   hyp ? s_neg(s_mul2(S, S)) : s_mul2(S, S)};
        F = s_div(s_mul(3, (E *[]){s_pow(S, p1), s_pow(C, q1), s_pow(s_mul2(C, C), s_neg(s_div(q1, K->c2)))}), p1);
        E *H = mk_fn(h2, 4, v);
        H->f = F_SIMP;
        F = s_mul2(F, H);
    } else if (!is_zero(q1) && !hyp) { /* the cosine version */
        E *v[4] = {s_div(s_sub(K->c1, p), K->c2), s_div(q1, K->c2), s_div(s_add2(q, mk_int(3)), K->c2), s_mul2(C, C)};
        F = s_neg(s_div(s_mul(3, (E *[]){s_pow(C, q1), s_pow(S, p1), s_pow(s_mul2(S, S), s_neg(s_div(p1, K->c2)))}), q1));
        E *H = mk_fn(h2, 4, v);
        H->f = F_SIMP;
        F = s_mul2(F, H);
    } else
        return NULL;
    E *r = s_div(s_mul(3, (E *[]){cst, G, F}), du);
    return ix_verify(X, f, r, x) ? r : NULL;
}

E *ix_trighyp(XP, E *f, E *x) {
    void *a[2] = {f, x};
    char *mk = K->s_top;
    E *r = X->protect(trighyp_p, a);
    if (!r) K->s_top = mk;
    return r;
}

/* ---- polylogarithms of symbolic order and products of logarithms ----
 *   polylog(s, a x^q)/x            -> polylog(s+1, a x^q)/q
 *   x^m polylog(s, a e^(b x))      -> sum_j (-1)^j m!/(m-j)! x^(m-j) polylog(s+1+j, a e^(b x))/b^(j+1)
 *   ln(u) ln(v) (u, v linear)      -> by parts: U ln(v) - int U v'/v with U = int ln(u)
 * (s any order, m a nonnegative integer; checked by differentiation) */
static int is_pl(XP, E *e) {
    const char *nm = e->t == E_FN ? sym_name(e->s) : "";
    return e->t == E_FN && e->n == 2 && nm[0] == 'p' && nm[1] == 'o' && nm[2] == 'l' && nm[3] == 'y' && nm[4] == 'l' &&
           nm[5] == 'o' && nm[6] == 'g' && !nm[7];
}

static E *mkpl(XP, E *s, E *z) {
    E *v[2] = {s, z};
    return X->rebuild(mk_fn(mk_sym_name("polylog")->s, 2, v), v);
}

static E *plog_p(XP, void **aa) {
    E *f = aa[0], *x = aa[1];
    E **fs = f->t == E_MUL ? EK(f) : &f;
    uint32_t n = f->t == E_MUL ? f->n : 1;
    E *pl = NULL, *rest = K->c1, *L1 = NULL, *L2 = NULL;
    for (uint32_t i = 0; i < n; i++) {
        E *g = fs[i];
        if (is_pl(X, g) && !pl && freeof(K0(g), x)) pl = g;
        else if (IFN(g, F_LN) && !L1) L1 = g;
        else if (IFN(g, F_LN) && !L2) L2 = g;
        else if (g->t == E_POW && IFN(K0(g), F_LN) && expr_eq(K1(g), K->c2) && !L1) L1 = L2 = K0(g);
        else rest = s_mul2(rest, g);
    }
    E *r = NULL;
    if (pl && !L1) {
        E *s = K0(pl), *z = K1(pl), *dz = diff(z, x);
        E *q = rat_normal(s_div(s_mul2(x, dz), z));
        E *c = rat_normal(s_mul2(rest, x));
        long m;
        if (freeof(q, x) && !is_zero(q) && freeof(c, x)) { /* z = a x^q, rest = c/x */
            r = s_div(s_mul2(c, mkpl(X, s_add2(s, K->c1), z)), q);
        } else if (!is_zero(z) && freeof(rat_normal(s_div(dz, z)), x) && poly_degree_in(rest, x, &m) && m >= 0 && m <= 8 &&
                   ix_ratx(X, rest, x)) {
            E *b = rat_normal(s_div(dz, z)); /* z = a e^(b x), rest a polynomial */
            E *P = rest;
            r = K->c0;
            E *sign = K->c1, *bp = b;
            for (long j = 0; j <= m; j++) {
                r = s_add2(r, s_div(s_mul(3, (E *[]){sign, P, mkpl(X, s_add(3, (E *[]){s, K->c1, mk_int(j)}), z)}), bp));
                P = diff(P, x), sign = s_neg(sign), bp = s_mul2(bp, b);
            }
        }
    } else if (L1 && L2 && !pl && freeof(rest, x)) {
        for (int t = 0; t < 2 && !r; t++) {
            E *A = t ? L2 : L1, *B = t ? L1 : L2;
            long d;
            if (!poly_degree_in(K0(A), x, &d) || d != 1) continue;
            E *U = ix_int(X, A, x);
            if (!U) continue;
            E *I = ix_int(X, rat_normal(s_mul2(U, diff(B, x))), x);
            if (I) r = s_mul2(rest, s_sub(s_mul2(U, B), I));
        }
    }
    return r && ix_verify(X, f, r, x) ? r : NULL;
}

E *ix_plog2(XP, E *f, E *x) {
    void *a[2] = {f, x};
    char *mk = K->s_top;
    E *r = X->protect(plog_p, a);
    if (!r) K->s_top = mk;
    return r;
}
