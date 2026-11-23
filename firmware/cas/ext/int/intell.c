/*
 * intell.c -- elliptic integrals of trigonometric (and cosh) integrands: R(cos u, sin u) times
 * half-integer powers of one linear form L = cos u + r (or sin u + r, cosh u + r), u = c + d x.
 *
 * Method: sin u, cos u -> S, C; every radical B^p (p = k/2) has B = beta C-form, i.e. its
 * numerator and denominator are constants times powers of L; it becomes G L^(kp) with the
 * locally constant G = B^p / L^(kp) (its derivative is 0, so it is kept as a factor).  Even
 * powers of S become polynomials in C (S^2 = 1 - C^2, cosh: C^2 - 1), C = L - r, and the
 * integrand is then sum c_j L^(j - 1/2).  With J_n = int L^n du:
 *   n J_n = S L^(n-1) + (2n - 1) r J_(n-1) + (n - 1)(1 - r^2) J_(n-2)
 * (S = sin u; for the sine family -cos u; for cosh, sinh u), upwards from and downwards to
 *   J_(1/2)  = 2 K E(u/2 | m),  J_(-1/2) = 2/K F(u/2 | m),  m = 2/(1 + r),  K = sqrt(L)/sqrt(L/(1+r))
 * (sine: u - pi/2 for u; cosh: -2 i K E(i u/2 | m) and -2 i/K F(i u/2 | m)).
 * The result is checked by differentiation by the caller.
 */
#include "intx.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

#define NG 8

typedef struct {
    E *x, *u, *C, *S, *L, *r;
    int hyp, sinf; /* cosh family; sine family */
    E *g[NG], *gv[NG], *gr[NG]; /* the constants G, their values and their radicals */
    int ng, bad;
} ectx;

static int is_node(E *e) { return e->t == E_ADD || e->t == E_MUL || e->t == E_POW || e->t == E_FN || e->t == E_LIST; }

/* the common argument of the trigonometric (or hyperbolic) functions in e */
static void find_u(XP, ectx *c, E *e) {
    if (c->bad || !is_node(e) || freeof(e, c->x)) return;
    stack_check();
    if (e->t == E_FN) {
        int t = IFN(e, F_SIN) || IFN(e, F_COS) || IFN(e, F_TAN) || IFN(e, F_COT) || IFN(e, F_SEC) || IFN(e, F_CSC);
        int h = IFN(e, F_SINH) || IFN(e, F_COSH) || IFN(e, F_TANH);
        if (t || h) {
            if (!c->u) c->u = K0(e), c->hyp = h;
            else if (!expr_eq(c->u, K0(e)) || c->hyp != h) c->bad = 1;
            return;
        }
        if (e->s < S__FIRSTCMD && e->s != F_SQRT) { /* another function of x */
            c->bad = 1;
            return;
        }
    }
    for (uint32_t i = 0; i < e->n; i++) find_u(X, c, EK(e)[i]);
}

/* sin u, cos u, ... -> S, C */
static E *tosc(XP, ectx *c, E *e) {
    if (!is_node(e) || freeof(e, c->x)) return e;
    stack_check();
    if (e->t == E_FN && expr_eq(K0(e), c->u) && e->n == 1) {
        E *S = c->sinf ? c->C : c->S, *C = c->sinf ? c->S : c->C; /* sine family: sin u is the C */
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
    E **k = salloc(sizeof(E *) * e->n);
    for (uint32_t i = 0; i < e->n; i++) k[i] = tosc(X, c, EK(e)[i]);
    return X->rebuild(e, k);
}

/* P (a polynomial in C) = lc (C + r)^k: k (and r, lc), else -1 */
static long lin_pow(XP, E *P, E *C, E **r, E **lc) {
    long d;
    if (!poly_degree_in(P, C, &d) || d > 12) return -1;
    *lc = poly_coeff(P, C, d);
    if (d == 0) return 0;
    *r = rat_normal(s_div(poly_coeff(P, C, d - 1), s_mul2(mk_int(d), *lc)));
    if (!is_zero(expand(s_sub(P, s_mul2(*lc, s_powi(s_add2(C, *r), d)))))) return -1;
    return d;
}

/* radicals B^p (p not an integer) -> G L^(k p) */
static E *rads(XP, ectx *c, E *e) {
    if (c->bad || !is_node(e) || (freeof(e, c->C) && freeof(e, c->S))) return e;
    stack_check();
    E **k = salloc(sizeof(E *) * e->n);
    int ch = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        k[i] = rads(X, c, EK(e)[i]);
        ch |= k[i] != EK(e)[i];
    }
    if (ch) e = X->rebuild(e, k);
    long pi;
    if (e->t != E_POW || num_is_small_int(K1(e), &pi)) return e;
    E *B = K0(e), *p = K1(e), *nB, *dB, *r1 = NULL, *r2 = NULL, *l1, *l2;
    if (!is_num(p) || !freeof(B, c->S)) { /* radicals of sin u with cos u: the other family */
        c->bad = 1;
        return e;
    }
    X->numden(B, &nB, &dB);
    long kn = lin_pow(X, expand(nB), c->C, &r1, &l1), kd = lin_pow(X, expand(dB), c->C, &r2, &l2);
    if (kn < 0 || kd < 0 || (kn && kd && !expr_eq(r1, r2))) {
        c->bad = 1;
        return e;
    }
    E *r = kn ? r1 : r2;
    if (r) {
        if (!c->r) c->r = r;
        else if (!expr_eq(c->r, r)) {
            c->bad = 1;
            return e;
        }
    }
    E *kp = s_mul2(mk_int(kn - kd), p);
    for (int i = 0; i < c->ng; i++) /* the same radical again */
        if (expr_eq(c->gr[i], e)) return s_mul2(c->g[i], s_pow(c->L, kp));
    if (c->ng == NG) {
        c->bad = 1;
        return e;
    }
    /* G = B^p / L^(kp), in x */
    E *cu = s_fn1(c->hyp ? F_COSH : (c->sinf ? F_SIN : F_COS), c->u), *su = s_fn1(c->hyp ? F_SINH : (c->sinf ? F_COS : F_SIN), c->u);
    E *Bx = subst(subst(B, c->C, cu), c->S, su);
    E *Lx = r ? s_add2(cu, r) : cu;
    int fi = 30 + c->ng;
    E *g = ix_fresh(X, e, &fi);
    c->g[c->ng] = g, c->gr[c->ng] = e;
    c->gv[c->ng++] = s_div(s_pow(Bx, p), s_pow(Lx, kp));
    return s_mul2(g, s_pow(c->L, kp));
}

/* Q_k = int L^(-1/2) (L - rho)^(-k) du.  Q_1 = pre/((1 + r - rho) K) Pi(2/(1 + r - rho); ph | m);
 * for k >= 2, with T = L - rho, C = L - r:
 *   d/du (Sx L^(1/2) T^(1-k)) = L^(-1/2) T^(-k) W(T),
 *   W = C L T - (1 - C^2) T/2 + (k - 1)(1 - C^2) L = w_0 + w_1 T + w_2 T^2 + w_3 T^3,
 * so Q_k = (Sx L^(1/2) T^(1-k) - sum_(j>=1) w_j I_(j-k))/w_0 with I_m = Q_(-m) (m < 0) or
 * int L^(-1/2) (L - rho)^m du = sum_i C(m,i) (-rho)^(m-i) J_(i-1/2) (m >= 0). */
static E *ell_q(XP, ectx *c, E *rho, long k, E **J, long lo, long hi, E *ph, E *m, E *pre, E *Kf, E *Sx, E *Lx) {
    E *one_r = s_add2(K->c1, c->r);
    if (k == 1) {
        E *w = rat_normal(s_sub(one_r, rho));
        if (is_zero(w)) return NULL;
        E *v[3] = {rat_normal(s_div(K->c2, w)), ph, m};
        E *P = mk_fn(mk_sym_name("elliptic_pi")->s, 3, v);
        P->f = F_SIMP;
        return s_div(s_mul2(pre, P), s_mul2(w, Kf));
    }
    poll_intr();
    int fi = 50;
    E *T = ix_fresh(X, c->L, &fi);
    E *Lt = s_add2(T, rho), *Ct = s_add2(T, s_sub(rho, c->r)), *om = s_sub(K->c1, s_mul2(Ct, Ct));
    E *W = expand(s_add(3, (E *[]){s_mul(3, (E *[]){Ct, Lt, T}), s_neg(s_mul2(K->chalf, s_mul2(om, T))),
                                   s_mul2(mk_int(k - 1), s_mul2(om, Lt))}));
    E *w0 = rat_normal(poly_coeff(W, T, 0));
    if (is_zero(w0)) return NULL;
    E *r = s_mul(3, (E *[]){Sx, s_sqrt(Lx), s_powi(s_sub(Lx, rho), 1 - k)});
    for (long j = 1; j <= 3; j++) {
        E *wj = poly_coeff(W, T, j);
        if (is_zero(wj)) continue;
        long mm = j - k;
        E *I;
        if (mm < 0) I = ell_q(X, c, rho, -mm, J, lo, hi, ph, m, pre, Kf, Sx, Lx);
        else {
            I = K->c0;
            E *bi = K->c1;
            for (long i = 0; i <= mm; i++) { /* C(mm, i) (-rho)^(mm-i) J_(i-1/2) */
                long jj = 2 * i - 1;
                if (jj > hi || jj < lo) return NULL;
                I = s_add2(I, s_mul(3, (E *[]){bi, s_powi(s_neg(rho), mm - i), J[(jj - lo) / 2]}));
                bi = s_div(s_mul2(bi, mk_int(mm - i)), mk_int(i + 1));
            }
        }
        if (!I) return NULL;
        r = s_sub(r, s_mul2(wj, I));
    }
    return s_div(r, w0);
}

/* int h(L) L^(-1/2) du for h rational in L: partial fractions, c L^k -> J_(k-1/2) (recurrence
 * from J_(1/2), J_(-1/2)), c/(L - rho) -> Pi */
static E *ell_even(XP, ectx *c, E *h) {
    E *L = c->L, *r = c->r;
    E *a1[2] = {h, L}, *pf = try_cmd(C_PARTFRAC, 2, a1);
    if (!pf) return NULL;
    pf = expand(pf);
    E **ts = pf->t == E_ADD ? EK(pf) : &pf;
    uint32_t nt = pf->t == E_ADD ? pf->n : 1;
    if (nt > 40) return NULL;
    /* classify the terms: c L^k (k = kp[i]) or c/(L - rho) */
    long *kp = salloc(sizeof(long) * nt);
    E **cf = salloc(sizeof(E *) * nt), **rho = salloc(sizeof(E *) * nt);
    long lo = -1, hi = 1;
    for (uint32_t i = 0; i < nt; i++) {
        E *t = ts[i], *nu, *de;
        X->numden(t, &nu, &de);
        long dn, dd;
        nu = expand(nu), de = expand(de);
        if (!poly_degree_in(nu, L, &dn) || !poly_degree_in(de, L, &dd)) return NULL;
        rho[i] = NULL;
        if (dn == 0 && dd >= 1 && dd <= 6 && !is_zero(poly_coeff(de, L, 0))) { /* c/(L - rho)^k */
            E *l1 = poly_coeff(de, L, dd);
            E *rh = rat_normal(s_neg(s_div(poly_coeff(de, L, dd - 1), s_mul2(mk_int(dd), l1))));
            if (is_zero(expand(s_sub(de, s_mul2(l1, s_powi(s_sub(L, rh), dd)))))) {
                rho[i] = rh, cf[i] = s_div(nu, l1), kp[i] = dd;
                if (!freeof(cf[i], L) || !freeof(rh, L)) return NULL;
                if (dd > 1 && hi < 3) hi = 3; /* the reduction of Q_k uses J_(3/2) */
                continue;
            }
        }
        if (dn == 0 && dd >= 0 && is_zero(expand(s_sub(de, s_mul2(poly_coeff(de, L, dd), s_powi(L, dd)))))) {
            cf[i] = s_div(nu, poly_coeff(de, L, dd)), kp[i] = -dd;
        } else if (dd == 0 && dn >= 0 && is_zero(expand(s_sub(nu, s_mul2(poly_coeff(nu, L, dn), s_powi(L, dn)))))) {
            cf[i] = s_div(poly_coeff(nu, L, dn), de), kp[i] = dn;
        } else
            return NULL;
        if (!freeof(cf[i], L)) return NULL;
        long j = 2 * kp[i] - 1;
        if (j < lo) lo = j;
        if (j > hi) hi = j;
    }
    if (hi > 49 || lo < -49) return NULL;
    long N = (hi - lo) / 2 + 1;
    E **J = salloc(sizeof(E *) * (size_t)N);
#define JI(j) J[((j) - lo) / 2]
    E *one_r = s_add2(K->c1, r);
    if (is_zero(one_r)) return NULL;
    E *m = rat_normal(s_div(K->c2, one_r));
    E *cu = s_fn1(c->hyp ? F_COSH : (c->sinf ? F_SIN : F_COS), c->u), *Lx = s_add2(cu, r);
    E *Kf = s_div(s_sqrt(Lx), s_sqrt(s_div(Lx, one_r)));
    E *ph;
    if (c->hyp) ph = s_mul2(K->i_, s_div(c->u, K->c2));
    else if (c->sinf) ph = s_div(s_sub(c->u, s_div(K->pi_, K->c2)), K->c2);
    else ph = s_div(c->u, K->c2);
    E *pre = c->hyp ? s_mul2(mk_int(-2), K->i_) : K->c2;
    JI(1) = s_mul(3, (E *[]){pre, Kf, s_fn2(mk_sym_name("elliptic_e")->s, ph, m)});
    JI(-1) = s_div(s_mul2(pre, s_fn2(mk_sym_name("elliptic_f")->s, ph, m)), Kf);
    /* boundary term T_n = S L^(n-1) (sine family: -cos u; cosh: sinh u) */
    E *Sx = c->hyp ? s_fn1(F_SINH, c->u) : c->sinf ? s_neg(s_fn1(F_COS, c->u)) : s_fn1(F_SIN, c->u);
    E *q = s_sub(K->c1, s_mul2(r, r));
    for (long j = 3; j <= hi; j += 2) { /* n = j/2: n J_n = T_n + (2n-1) r J_(n-1) + (n-1) q J_(n-2) */
        poll_intr();
        E *n = mk_frac(j, 2), *n1 = s_sub(n, K->c1);
        E *t = s_mul2(Sx, s_pow(Lx, n1));
        t = s_add2(t, s_mul2(s_mul2(s_sub(s_mul2(K->c2, n), K->c1), r), JI(j - 2)));
        t = s_add2(t, s_mul2(s_mul2(n1, q), JI(j - 4)));
        JI(j) = s_div(t, n);
    }
    if (lo < -1 && is_zero(rat_normal(q))) return NULL;
    for (long j = -3; j >= lo; j -= 2) { /* from n' = n + 2 */
        poll_intr();
        E *np = mk_frac(j + 4, 2), *np1 = s_sub(np, K->c1);
        E *t = s_mul2(np, JI(j + 4));
        t = s_sub(t, s_mul2(Sx, s_pow(Lx, np1)));
        t = s_sub(t, s_mul2(s_mul2(s_sub(s_mul2(K->c2, np), K->c1), r), JI(j + 2)));
        JI(j) = s_div(t, s_mul2(np1, q));
    }
    E *res = K->c0;
    for (uint32_t i = 0; i < nt; i++) {
        if (rho[i]) {
            E *Q = ell_q(X, c, rho[i], kp[i], J, lo, hi, ph, m, pre, Kf, Sx, Lx);
            if (!Q) return NULL;
            res = s_add2(res, s_mul2(cf[i], Q));
        } else
            res = s_add2(res, s_mul2(cf[i], JI(2 * kp[i] - 1)));
    }
#undef JI
    return res;
}

static E *ell_int(XP, ectx *c, E *f) {
    E *x = c->x;
    E *dd = diff(c->u, x);
    long dg;
    if (!poly_degree_in(c->u, x, &dg) || dg != 1 || !freeof(dd, x)) return NULL;
    E *f1 = tosc(X, c, f);
    if (!freeof(f1, x)) return NULL;
    E *cu = s_fn1(c->hyp ? F_COSH : (c->sinf ? F_SIN : F_COS), c->u);
    E *su = s_fn1(c->hyp ? F_SINH : (c->sinf ? F_COS : F_SIN), c->u);
    /* the parts even and odd in S */
    E *f1n = subst(f1, c->S, s_neg(c->S));
    E *fe = s_div(s_add2(f1, f1n), K->c2), *fo = s_div(s_sub(f1, f1n), K->c2);
    E *s2 = c->hyp ? s_sub(s_mul2(c->C, c->C), K->c1) : s_sub(K->c1, s_mul2(c->C, c->C));
    /* res: the antiderivative; fg, rg: integrand and antiderivative with the constants G kept as
     * symbols, for the check (the G' terms of the substituted result are 0 only numerically, and
     * elliptic_pi has no value where its arguments cross a branch cut) */
    E *res = K->c0, *rg = K->c0, *fg = K->c0;
    c->ng = 0, c->r = NULL;
    if (!is_zero(fe) && !is_zero(rat_normal(fe))) {
        E *f2 = rads(X, c, fe);
        if (c->bad || !c->ng) return NULL;
        if (!c->r) c->r = K->c0;
        /* S^2 -> 1 - C^2 (cosh: C^2 - 1), then C = L - r */
        E *f3 = subst(f2, c->S, s_sqrt(s2));
        f3 = subst(f3, c->C, s_sub(c->L, c->r));
        E *h = rat_normal(s_mul2(f3, s_sqrt(c->L)));
        E *re = ell_even(X, c, h);
        if (!re) return NULL;
        rg = s_div(re, dd);
        E *ks[3] = {c->C, c->S, c->L}, *vs[3] = {cu, su, s_add2(cu, c->r)};
        fg = subst_many(f2, 3, ks, vs);
        for (int i = 0; i < c->ng; i++) re = subst(re, c->g[i], c->gv[i]);
        res = s_div(re, dd);
    }
    if (!is_zero(fo) && !is_zero(rat_normal(fo))) {
        /* S h(C): t = C (cos: dt = -S du; sine family, cosh: dt = S du) */
        E *h = subst(s_div(fo, c->S), c->S, s_sqrt(s2));
        int fi = 40;
        E *t = ix_fresh(X, f, &fi);
        E *H = ix_int(X, subst(h, c->C, t), t);
        if (!H) return NULL;
        H = subst(H, t, cu);
        H = s_div(c->hyp || c->sinf ? H : s_neg(H), dd);
        res = s_add2(res, H), rg = s_add2(rg, H);
        E *ks[2] = {c->C, c->S}, *vs[2] = {cu, su};
        fg = s_add2(fg, subst_many(fo, 2, ks, vs));
    }
    if (!freeof(res, c->C) || !freeof(res, c->S) || !freeof(res, c->L)) return NULL;
    if (!ix_verify(X, fg, rg, x)) return NULL;
    return res;
}

static void ec_init(ectx *c, E *x, int sinf) {
    c->x = x, c->u = c->C = c->S = c->L = c->r = NULL;
    c->hyp = 0, c->sinf = sinf, c->ng = 0, c->bad = 0;
}

/* f a function of cos(u)^2 and sin(u)^2 only: in cos(2 u) (cosh: cosh(2 u)), else NULL */
static E *dbl(XP, ectx *c, E *f) {
    E *f1 = tosc(X, c, f);
    if (!freeof(f1, c->x)) return NULL;
    E *mc = subst(f1, c->C, s_neg(c->C)), *ms = subst(f1, c->S, s_neg(c->S));
    if (!is_zero(rat_normal(s_sub(f1, mc))) || !is_zero(rat_normal(s_sub(f1, ms)))) return NULL;
    E *u2 = s_mul2(K->c2, c->u), *c2 = s_fn1(c->hyp ? F_COSH : F_COS, u2);
    E *ks[2] = {c->C, c->S};
    E *vs[2] = {s_sqrt(s_div(s_add2(K->c1, c2), K->c2)),
                s_sqrt(s_div(c->hyp ? s_sub(c2, K->c1) : s_sub(K->c1, c2), K->c2))};
    E *g = subst_many(f1, 2, ks, vs);
    return freeof(g, c->C) && freeof(g, c->S) ? g : NULL;
}

static E *ell_try(XP, E *f, E *x, int sinf) {
    ectx c0, *c = &c0;
    ec_init(c, x, sinf);
    find_u(X, c, f);
    if (c->bad || !c->u) return NULL;
    if (sinf && c->hyp) return NULL;
    int fi = 20;
    c->C = ix_fresh(X, f, &fi), c->S = ix_fresh(X, f, &fi), c->L = ix_fresh(X, f, &fi);
    return ell_int(X, c, f);
}

E *ix_ellip(XP, E *f, E *x) {
    char *mk = K->s_top;
    E *r = ell_try(X, f, x, 0);
    if (r) return r;
    K->s_top = mk;
    /* the sine family: the roles of sin and cos exchanged */
    if ((r = ell_try(X, f, x, 1))) return r;
    K->s_top = mk;
    /* radicals of a + b sin(u)^2: in cos(2 u) */
    ectx c0, *c = &c0;
    ec_init(c, x, 0);
    find_u(X, c, f);
    if (c->bad || !c->u) return NULL;
    int fi = 20;
    c->C = ix_fresh(X, f, &fi), c->S = ix_fresh(X, f, &fi), c->L = ix_fresh(X, f, &fi);
    E *g = dbl(X, c, f);
    if (!g) return NULL;
    r = ell_try(X, g, x, 0);
    if (!r) K->s_top = mk;
    return r;
}
