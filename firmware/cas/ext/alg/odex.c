/*
 * odex.c -- ordinary differential equations the core's dsolve (ode.c) does not solve, through
 * the algorithm slot CASX_FN_DSOLVE (casx.h).
 *
 * The slot gets the equation as F(x, _y0, .., _ym) = 0 (_yi the i-th derivative).
 *   mode 0 (no core method applied):
 *     - Riccati y' = a y^2 + b y + c: a particular solution y_p of the forms A x^k
 *       (k = -2..2), A x + B, A + B/x (coefficients by solve), then y = y_p + 1/v with the
 *       linear v' = -(2 a y_p + b) v - a; otherwise y = -u'/(a u) with
 *       u'' - (b + a'/a) u' + a c u = 0 given to dsolve again;
 *     - Bessel x^2 y'' + x y' + (b x^2 - nu^2) y = 0: c1 J_nu(sqrt(b) x) + c2 Y_nu(sqrt(b) x)
 *       (b < 0: I_nu, K_nu).
 *     Every solution is checked by substitution at sample points.
 *   mode 1 [.., x0, N]: a linear equation as a power series in t = x - x0 below t^N:
 *     an ordinary point (p_m(x0) != 0): y = sum a_j t^j, a_j = c_(j+1) for j < m, the others
 *     from the coefficients of t^0 .. t^(N-1-m) of the residual (linear algebra);
 *     a regular singular point of t^2 y'' + t p y' + q y = 0 (homogeneous): Frobenius,
 *     y1 = t^r1 sum a_j t^j, y2 = k y1 ln t + t^r2 sum b_j t^j (r1, r2 the roots of the
 *     indicial equation r(r-1) + p(0) r + q(0) = 0, real part of r1 >= that of r2).
 *   mode 2 [sol, ..]: a solution linear in c1, c2, ... rewritten as sum c_i y_i + y_p with
 *     every part expanded and simplified (equal at sample points, else unchanged).
 */
#include "casx_mod.h"
#include "../int/intx.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

static E *ysym(XP, long k) {
    char nm[4] = {'_', 'y', (char)('0' + k), 0};
    return mk_sym_name(nm);
}

static E *csym(XP, long k) {
    char nm[3] = {'c', (char)('0' + k), 0};
    return mk_sym_name(nm);
}

static E *osimp(XP, E *e) { return eval_cmd(C_SIMPLIFY, 1, &e); }

static E *cmd2(XP, int id, E *a, E *b) {
    E *v[2] = {a, b};
    return try_cmd(id, 2, v);
}


/* F(x, sol, sol', ..) = 0 at sample points (the constants c_i and other symbols set too) */
static int sol_ok(XP, E *F, E *sol, E *x, long m) {
    E *ks[12], *vs[12];
    uint32_t n = 0;
    E *d = sol;
    for (long i = 0; i <= m && n < 10; i++) {
        ks[n] = ysym(X, i), vs[n++] = d;
        if (i < m) d = diff(d, x);
    }
    E *R = subst_many(F, n, ks, vs);
    E *sy[17], *sv[17];
    uint32_t ns = 0;
    ix_syms(X, R, x, sy, &ns, 15);
    static const int16_t pv[8] = {731, 1172, 433, 1530, 917, 288, 1364, 615};
    static const int16_t xv[4] = {317, 1243, 689, 1681};
    int good = 0;
    for (int j = 0; j < 4; j++) {
        uint32_t q = ns;
        for (uint32_t i = 0; i < ns; i++) sv[i] = mk_flt(pv[(i + j) % 8] / 1000.0);
        sy[q] = x, sv[q++] = mk_flt(xv[j] / 1000.0);
        char *pm = K->s_top;
        double re, im, fr, fi;
        E *Fv = subst_many(R, q, sy, sv);
        int ok = X->eval_complex(Fv, &re, &im);
        /* the scale: the first term of F alone */
        E *sc = subst_many(subst_many(F->t == E_ADD ? EK(F)[0] : F, n, ks, vs), q, sy, sv);
        int ok2 = X->eval_complex(sc, &fr, &fi);
        K->s_top = pm;
        if (!ok) continue;
        double s = 1 + (ok2 ? __builtin_fabs(fr) + __builtin_fabs(fi) : 0);
        if (__builtin_fabs(re) + __builtin_fabs(im) > 1e-8 * s) return 0;
        good++;
    }
    return good >= 3;
}

/* ---- mode 2: sum c_i y_i + y_p ---- */

static E *normalise(XP, E *sol, E *x, long m) {
    if (sol->t == E_LIST || IFN(sol, F_EQ) || !freeof(sol, ysym(X, 0))) return NULL;
    E *cs[10], *z[10];
    uint32_t nc = 0;
    for (long i = 1; i <= 9; i++)
        if (!freeof(sol, csym(X, i))) cs[nc] = csym(X, i), z[nc++] = K->c0;
    if (!nc) return NULL;
    E *ex = expand(sol), *r = K->c0;
    for (uint32_t i = 0; i < nc; i++) {
        E *d = diff(ex, cs[i]);
        for (uint32_t j = 0; j < nc; j++)
            if (!freeof(d, cs[j])) return NULL; /* not linear in the constants */
        r = s_add2(r, s_mul2(cs[i], osimp(X, d)));
    }
    E *p = subst_many(ex, nc, cs, z);
    if (!is_zero(p)) r = s_add2(r, osimp(X, p));
    /* the same function: at sample points */
    E *df = s_sub(r, sol), *sy[17], *sv[17];
    uint32_t ns = 0;
    ix_syms(X, df, x, sy, &ns, 15);
    for (int j = 0; j < 3; j++) {
        uint32_t q = ns;
        for (uint32_t i = 0; i < ns; i++) sv[i] = mk_flt(0.37 + 0.29 * i + 0.11 * j);
        sy[q] = x, sv[q++] = mk_flt(0.41 + 0.53 * j);
        double re, im, a, b;
        char *pm = K->s_top;
        int ok = X->eval_complex(subst_many(df, q, sy, sv), &re, &im) && X->eval_complex(subst_many(sol, q, sy, sv), &a, &b);
        K->s_top = pm;
        if (!ok || __builtin_fabs(re) + __builtin_fabs(im) > 1e-9 * (1 + __builtin_fabs(a) + __builtin_fabs(b))) return NULL;
    }
    return r;
}

/* ---- mode 0: Riccati ---- */

/* y_p of the form tpl (unknowns u[0..nu)) with R(y_p) = 0: the solved form, else NULL */
static E *rc_try(XP, E *F, E *x, E *tpl, uint32_t nu, E **u) {
    E *ks[2] = {ysym(X, 0), ysym(X, 1)}, *vs[2] = {tpl, diff(tpl, x)};
    E *R, *nm, *dn;
    X->numden(rat_normal(subst_many(F, 2, ks, vs)), &nm, &dn);
    nm = expand(nm);
    long d;
    if (!poly_degree_in(nm, x, &d) || d > 12) return NULL;
    E *eqs[14];
    uint32_t ne = 0;
    for (long i = 0; i <= d; i++) {
        E *c = poly_coeff(nm, x, i);
        if (!is_zero(c)) eqs[ne++] = c;
    }
    if (!ne) return NULL;
    E *sl = cmd2(X, C_SOLVE, s_list(ne, eqs), s_list(nu, u));
    if (!sl || sl->t != E_LIST || !sl->n) return NULL;
    /* a list of solutions: [u=..] or [[u=.., v=..], ..]; take the first real one */
    for (uint32_t i = 0; i < sl->n; i++) {
        E *s1 = EK(sl)[i], **eq = s1->t == E_LIST ? EK(s1) : &s1;
        uint32_t n = s1->t == E_LIST ? s1->n : 1;
        R = tpl;
        for (uint32_t j = 0; j < n; j++)
            if (IFN(eq[j], F_EQ)) R = subst(R, K0(eq[j]), K1(eq[j]));
        int free = 1;
        for (uint32_t j = 0; j < nu; j++) free &= freeof(R, u[j]);
        if (free && freeof(R, K->i_) && !is_zero(R)) return R;
    }
    return NULL;
}

static E *riccati(XP, E *F, E *x) {
    E *y0 = ysym(X, 0), *y1 = ysym(X, 1);
    long d1, d0;
    if (!poly_degree_in(F, y1, &d1) || d1 != 1 || !poly_degree_in(F, y0, &d0) || d0 != 2) return NULL;
    E *cy1 = poly_coeff(F, y1, 1);
    if (!freeof(cy1, y0)) return NULL;
    E *g = rat_normal(s_neg(s_div(poly_coeff(F, y1, 0), cy1))); /* y' = g */
    E *a = rat_normal(poly_coeff(g, y0, 2)), *b = rat_normal(poly_coeff(g, y0, 1)), *c = rat_normal(poly_coeff(g, y0, 0));
    if (!freeof(a, y0) || !freeof(b, y0) || !freeof(c, y0) || is_zero(a) || is_zero(c)) return NULL;
    E *Fr = s_sub(y1, s_add(3, (E *[]){s_mul2(a, s_mul2(y0, y0)), s_mul2(b, y0), c}));
    int fi = 30;
    E *A = ix_fresh(X, F, &fi), *B = ix_fresh(X, F, &fi), *u2[2] = {A, B};
    E *yp = NULL;
    for (long k = -2; k <= 2 && !yp; k++) yp = rc_try(X, Fr, x, s_mul2(A, s_powi(x, k)), 1, u2);
    if (!yp) yp = rc_try(X, Fr, x, s_add2(s_mul2(A, x), B), 2, u2);
    if (!yp) yp = rc_try(X, Fr, x, s_add2(A, s_div(B, x)), 2, u2);
    E *c1 = csym(X, 1);
    if (yp) {
        /* v' = -(2 a y_p + b) v - a: v = e^(-P) (c1 - int a e^P), P = int (2 a y_p + b) */
        E *P = ix_int(X, rat_normal(s_add2(s_mul(3, (E *[]){K->c2, a, yp}), b)), x);
        if (P) {
            E *eP = osimp(X, s_exp(P));
            E *I = ix_int(X, osimp(X, s_mul2(a, eP)), x);
            if (I) {
                E *v = s_div(s_sub(c1, I), eP);
                E *sol = osimp(X, s_add2(yp, s_inv(v)));
                if (sol_ok(X, Fr, sol, x, 1)) return sol;
            }
        }
    }
    /* y = -u'/(a u): u'' - (b + a'/a) u' + a c u = 0 */
    E *u = mk_sym_name("_u"), *ux = mk_fn(u->s, 1, &x);
    E *d1u = mk_fn(C_DIFF, 2, (E *[]){ux, x}), *d2u = mk_fn(C_DIFF, 2, (E *[]){d1u, x});
    E *q1 = rat_normal(s_add2(b, s_div(diff(a, x), a))), *q0 = rat_normal(s_mul2(a, c));
    E *eq = s_add(3, (E *[]){d2u, s_neg(s_mul2(q1, d1u)), s_mul2(q0, ux)});
    E *v3[3] = {eq, u, x};
    E *us = try_cmd(C_ODESOLVE, 3, v3);
    if (!us || us->t == E_LIST || IFN(us, F_EQ) || ix_has_fn(X, us, C_ODESOLVE)) return NULL;
    E *c2 = csym(X, 2);
    if (freeof(us, c1) || freeof(us, c2)) return NULL;
    us = subst(subst(us, c1, K->c1), c2, c1); /* only c2/c1 matters */
    E *sol = osimp(X, s_neg(s_div(diff(us, x), s_mul2(a, us))));
    return sol_ok(X, Fr, sol, x, 1) ? sol : NULL;
}

/* x^2 y'' + x y' + (b x^2 - nu^2) y = 0 */
static E *bessel(XP, E *F, E *x) {
    E *p2 = poly_coeff(F, ysym(X, 2), 1), *p1 = diff(F, ysym(X, 1)), *p0 = diff(F, ysym(X, 0));
    for (int i = 0; i <= 2; i++)
        if (!freeof(i == 0 ? p0 : i == 1 ? p1 : p2, ysym(X, i))) return NULL;
    E *r = subst_many(F, 3, (E *[]){ysym(X, 0), ysym(X, 1), ysym(X, 2)}, (E *[]){K->c0, K->c0, K->c0});
    if (!is_zero(rat_normal(r)) || is_zero(p2)) return NULL;
    E *q1 = rat_normal(s_div(s_mul2(x, p1), p2)), *q0 = rat_normal(s_div(s_mul2(s_mul2(x, x), p0), p2));
    long d;
    if (!expr_eq(q1, K->c1) || !poly_degree_in(q0, x, &d) || d != 2 || !is_zero(poly_coeff(q0, x, 1))) return NULL;
    E *b = poly_coeff(q0, x, 2), *nu2 = s_neg(poly_coeff(q0, x, 0));
    double bv, nv;
    if (!to_double(b, &bv) || !to_double(nu2, &nv) || nv < 0 || bv == 0) return NULL;
    E *nu = s_sqrt(nu2);
    E *z = s_mul2(s_sqrt(bv > 0 ? b : s_neg(b)), x);
    E *f1 = mk_fn(mk_sym_name(bv > 0 ? "besselj" : "besseli")->s, 2, (E *[]){nu, z});
    E *f2 = mk_fn(mk_sym_name(bv > 0 ? "bessely" : "besselk")->s, 2, (E *[]){nu, z});
    E *sol = s_add2(s_mul2(csym(X, 1), X->rebuild(f1, EK(f1))), s_mul2(csym(X, 2), X->rebuild(f2, EK(f2))));
    return sol_ok(X, F, sol, x, 2) ? sol : NULL;
}

/* ---- mode 1: power series ---- */

/* the Taylor polynomial of e in t at 0 below t^N */
static E *tser(XP, E *e, E *t, long N) {
    if (freeof(e, t)) return e;
    E *v[4] = {e, t, K->c0, mk_int(N)};
    E *s = try_cmd(C_SERIES, 4, v);
    long d;
    if (!s || !poly_degree_in(expand(s), t, &d)) return NULL;
    return expand(s);
}

static E *trunc_t(XP, E *p, E *t, long N) {
    long d;
    p = expand(p);
    if (!poly_degree_in(p, t, &d)) return NULL;
    E *r = K->c0;
    for (long i = 0; i <= d && i < N; i++) r = s_add2(r, s_mul2(poly_coeff(p, t, i), s_powi(t, i)));
    return r;
}

/* the coefficients of t^0 .. t^(hi) of p as equations */
static uint32_t coeffs(XP, E *p, E *t, long hi, E **eqs) {
    uint32_t n = 0;
    p = expand(p);
    for (long i = 0; i <= hi; i++) {
        E *c = poly_coeff(p, t, i);
        if (!is_zero(c)) eqs[n++] = c;
    }
    return n;
}

static E *series_sol(XP, E *F, E *x, long m, E *x0, long N) {
    if (N < m + 1 || N > 24 || m > 6) return NULL;
    E *p[8], *ys[8], *zs[8];
    for (long i = 0; i <= m; i++) ys[i] = ysym(X, i), zs[i] = K->c0;
    for (long i = 0; i <= m; i++) {
        p[i] = diff(F, ys[i]);
        for (long j = 0; j <= m; j++)
            if (!freeof(p[i], ys[j])) return NULL; /* not linear */
    }
    E *r = subst_many(F, (uint32_t)m + 1, ys, zs);
    int fi = 30;
    E *t = ix_fresh(X, F, &fi);
    E *xt = s_add2(x0, t);
    E *pt[8];
    for (long i = 0; i <= m; i++) pt[i] = rat_normal(subst(p[i], x, xt));
    E *rt = rat_normal(subst(r, x, xt));
    E *pm0 = rat_normal(subst(pt[m], t, K->c0));
    E *T = s_sub(x, x0);
    if (!is_zero(pm0)) { /* an ordinary point */
        E *ser[8], *rs = tser(X, rt, t, N);
        if (!rs) return NULL;
        for (long i = 0; i <= m; i++)
            if (!(ser[i] = tser(X, pt[i], t, N))) return NULL;
        uint32_t nu = (uint32_t)(N - m);
        E **u = salloc(sizeof(E *) * nu), **val = salloc(sizeof(E *) * nu), *Y = K->c0;
        for (long j = 0; j < N; j++) {
            E *aj = j < m ? csym(X, j + 1) : (u[j - m] = ix_fresh(X, F, &fi));
            Y = s_add2(Y, s_mul2(aj, s_powi(t, j)));
        }
        E *res = rs, *d = Y;
        for (long i = 0; i <= m; i++) {
            res = s_add2(res, s_mul2(ser[i], d));
            d = diff(d, t);
        }
        E **eqs = salloc(sizeof(E *) * (size_t)(N + 1));
        uint32_t ne = coeffs(X, res, t, N - 1 - m, eqs);
        if (!ix_lsolve(X, ne, eqs, nu, u, val)) return NULL;
        E *y = expand(subst_many(Y, nu, u, val));
        /* sum c_i y_i + y_p */
        E *out = K->c0;
        for (long i = 1; i <= m; i++) out = s_add2(out, s_mul2(csym(X, i), subst(diff(y, csym(X, i)), t, T)));
        E *cz[8];
        for (long i = 0; i < m; i++) cz[i] = K->c0;
        E *cs8[8];
        for (long i = 0; i < m; i++) cs8[i] = csym(X, i + 1);
        E *yp = subst_many(y, (uint32_t)m, cs8, cz);
        if (!is_zero(yp)) out = s_add2(out, subst(yp, t, T));
        return out;
    }
    /* a regular singular point: m = 2, homogeneous */
    if (m != 2 || !is_zero(rt)) return NULL;
    E *ps = tser(X, rat_normal(s_div(s_mul2(t, pt[1]), pt[2])), t, N), *qs = tser(X, rat_normal(s_div(s_mul2(s_mul2(t, t), pt[0]), pt[2])), t, N);
    if (!ps || !qs) return NULL;
    E *p0 = poly_coeff(ps, t, 0), *q0 = poly_coeff(qs, t, 0);
    if (!is_num(p0) || !is_num(q0)) return NULL;
    /* r^2 + (p0 - 1) r + q0 = 0 */
    E *dl = s_sub(s_mul2(s_sub(p0, K->c1), s_sub(p0, K->c1)), s_mul2(mk_int(4), q0));
    double dv;
    if (!to_double(dl, &dv) || dv < 0) return NULL;
    E *sq = s_sqrt(dl), *r1 = s_div(s_add2(s_sub(K->c1, p0), sq), K->c2), *r2 = s_div(s_sub(s_sub(K->c1, p0), sq), K->c2);
    E *sdiff = rat_normal(s_sub(r1, r2));
    long sint = -1;
    if (!num_is_small_int(sdiff, &sint)) sint = -1;
    /* M_r[Y] = t^2 Y'' + 2 r t Y' + r (r-1) Y + p (t Y' + r Y) + q Y */
#define MOP(Yv, rv) s_add(5, (E *[]){s_mul2(s_mul2(t, t), diff(diff(Yv, t), t)), s_mul(3, (E *[]){K->c2, rv, s_mul2(t, diff(Yv, t))}), \
                                     s_mul(3, (E *[]){rv, s_sub(rv, K->c1), Yv}), s_mul2(ps, s_add2(s_mul2(t, diff(Yv, t)), s_mul2(rv, Yv))), s_mul2(qs, Yv)})
    uint32_t nu = (uint32_t)N;
    E **u = salloc(sizeof(E *) * (nu + 1)), **val = salloc(sizeof(E *) * (nu + 1)), *A = K->c1;
    for (long j = 1; j < N; j++) A = s_add2(A, s_mul2(u[j - 1] = ix_fresh(X, F, &fi), s_powi(t, j)));
    E **eqs = salloc(sizeof(E *) * (size_t)(N + 2));
    uint32_t ne = coeffs(X, MOP(A, r1), t, N - 1, eqs);
    if (!ix_lsolve(X, ne, eqs, nu - 1, u, val)) return NULL;
    A = expand(subst_many(A, nu - 1, u, val));
    E *y1 = s_mul2(s_pow(T, r1), subst(A, t, T)), *y2;
    if (sint < 0) { /* r1 - r2 not an integer */
        E *B = K->c1;
        for (long j = 1; j < N; j++) B = s_add2(B, s_mul2(u[j - 1] = ix_fresh(X, F, &fi), s_powi(t, j)));
        ne = coeffs(X, MOP(B, r2), t, N - 1, eqs);
        if (!ix_lsolve(X, ne, eqs, nu - 1, u, val)) return NULL;
        B = expand(subst_many(B, nu - 1, u, val));
        y2 = s_mul2(s_pow(T, r2), subst(B, t, T));
    } else { /* y2 = k y1 ln t + t^r2 B */
        E *B = sint ? K->c1 : K->c0, *kap = sint ? ix_fresh(X, F, &fi) : K->c1;
        uint32_t nb = 0;
        for (long j = sint ? 1 : 1; j < N; j++) B = s_add2(B, s_mul2(u[nb++] = ix_fresh(X, F, &fi), s_powi(t, j)));
        if (sint) u[nb++] = kap;
        E *L = s_mul2(s_powi(t, sint), s_add(3, (E *[]){s_mul2(K->c2, s_mul2(t, diff(A, t))), s_mul(3, (E *[]){K->c2, r1, A}), s_mul2(s_sub(ps, K->c1), A)}));
        E *res = s_add2(MOP(B, r2), s_mul2(kap, L));
        ne = coeffs(X, res, t, N - 1, eqs);
        if (!ix_lsolve(X, ne, eqs, nb, u, val)) return NULL;
        B = expand(subst_many(B, nb, u, val));
        E *kv = sint ? val[nb - 1] : K->c1;
        y2 = s_add2(s_mul(3, (E *[]){kv, y1, s_fn1(F_LN, T)}), s_mul2(s_pow(T, r2), subst(trunc_t(X, B, t, N), t, T)));
    }
#undef MOP
    return s_add2(s_mul2(csym(X, 1), y1), s_mul2(csym(X, 2), y2));
}

static E *dslot(XP, uint32_t na, E **A) {
    long m, mode;
    if (na < 4 || !num_is_small_int(A[2], &m) || !num_is_small_int(A[3], &mode)) return NULL;
    E *F = A[0], *x = A[1];
    if (mode == 2) return normalise(X, F, x, m);
    if (mode == 1) {
        long N;
        if (na < 6 || !num_is_small_int(A[5], &N)) return NULL;
        return series_sol(X, F, x, m, A[4], N);
    }
    if (m == 1) return riccati(X, F, x);
    if (m == 2) return bessel(X, F, x);
    return NULL;
}

int casx_odex_register(XP) { return X->reg_fn(CASX_FN_DSOLVE, (void *)dslot); }
