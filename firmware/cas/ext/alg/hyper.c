/*
 * hyper.c -- hypergeometric summation for the sum slot (sums.c): Gosper's algorithm for
 * indefinite sums and Zeilberger's creative telescoping for definite sums over k = 0..n.
 * (Petkovsek, Wilf, Zeilberger, "A = B", chapters 5 and 6.)
 *
 * Term ratios.  t(k+1)/t(k) is formed factor by factor: c^(a k + b) -> c^a, (a k + b)! and
 * gamma(a k + b) (a an integer) -> rising products, binomial(A, B) through factorials,
 * rational functions R(k+1)/R(k); anything else is not hypergeometric (NULL).
 *
 * Gosper.  r = t(k+1)/t(k) = a(k)/b(k) c(k+1)/c(k) with gcd(a(k), b(k+h)) = 1 for every
 * h >= 0 (a factor p of a with p(k) ~ q(k+h) for a factor q of b moves into c as
 * p(k-1)...p(k-h)); a polynomial solution x of a(k) x(k+1) - b(k-1) x(k) = c(k) (degree
 * bound from the leading terms, coefficients by linear algebra) gives the antidifference
 * z(k) = b(k-1) x(k)/c(k) t(k), z(k+1) - z(k) = t(k), so sum_{k=lo}^{hi} t = z(hi+1) - z(lo).
 *
 * Zeilberger.  For F(n, k) hypergeometric in both, s_0(n) F(n,k) + ... + s_J(n) F(n+J,k)
 * = G(n,k+1) - G(n,k) is found by Gosper's equation with the s_j as extra unknowns
 * (J = 1, 2); summing over k (natural boundaries) gives sum_j s_j(n) S(n+j) = 0.  Order 1:
 * S(n) = S(0) prod_{j=0}^{n-1} -s_0(j)/s_1(j) (the core's product, gamma functions rewritten
 * as factorials and binomials); order 2 with s_1 = 0 and S(1) = 0: the even terms only.
 * The caller (sums.c) checks every closed form against the explicit sums.
 */
#include "casx_mod.h"
#include "../int/intx.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

/* e = a k + b with a, b free of k: a (NULL if not) */
static E *lincoef(XP, E *e, E *k, E **b) {
    long d;
    if (!poly_degree_in(e, k, &d) || d > 1) return NULL;
    E *a = d == 1 ? poly_coeff(e, k, 1) : K->c0;
    *b = poly_coeff(e, k, 0);
    return freeof(a, k) && freeof(*b, k) ? a : NULL;
}

/* gamma(L(k+1))/gamma(L(k)), L = a k + b with integer a */
static E *gratio(XP, E *L, E *k) {
    E *b, *a = lincoef(X, L, k, &b);
    long ai;
    if (!a || !num_is_small_int(a, &ai) || ai < -8 || ai > 8) return NULL;
    E *r = K->c1;
    if (ai > 0)
        for (long j = 0; j < ai; j++) r = s_mul2(r, s_add2(L, mk_int(j)));
    else
        for (long j = 1; j <= -ai; j++) r = s_div(r, s_sub(L, mk_int(j)));
    return r;
}

/* t(k+1)/t(k) as a rational function of k, NULL if t is not hypergeometric in k */
static E *hratio(XP, E *t, E *k) {
    if (freeof(t, k)) return K->c1;
    stack_check();
    if (t->t == E_MUL) {
        E *r = K->c1;
        for (uint32_t i = 0; i < t->n; i++) {
            E *q = hratio(X, EK(t)[i], k);
            if (!q) return NULL;
            r = s_mul2(r, q);
        }
        return r;
    }
    if (ix_ratx(X, t, k)) return s_div(subst(t, k, s_add2(k, K->c1)), t);
    if (t->t == E_POW) {
        E *B = K0(t), *e = K1(t);
        if (freeof(B, k)) {
            E *d = rat_normal(s_sub(subst(e, k, s_add2(k, K->c1)), e));
            return freeof(d, k) ? s_pow(B, d) : NULL;
        }
        long m;
        if (!num_is_small_int(e, &m)) return NULL;
        E *q = hratio(X, B, k);
        return q ? s_powi(q, m) : NULL;
    }
    if (IFN(t, F_FACT) && t->n == 1) return gratio(X, s_add2(K0(t), K->c1), k);
    if (IFN(t, F_GAMMA) && t->n == 1) return gratio(X, K0(t), k);
    if (IFN(t, F_BINOM) && t->n == 2) {
        E *A = K0(t), *B = K1(t);
        E *g1 = gratio(X, s_add2(A, K->c1), k), *g2 = gratio(X, s_add2(B, K->c1), k),
          *g3 = gratio(X, s_add2(s_sub(A, B), K->c1), k);
        return g1 && g2 && g3 ? s_div(g1, s_mul2(g2, g3)) : NULL;
    }
    return NULL;
}

/* ---- Gosper's algorithm ---- */

typedef struct {
    E *a, *b, *c; /* polynomials in k */
} gform;

/* p(k) = const q(k+h) for an integer h >= 0: h, else -1 */
static long shift_of(XP, E *p, E *q, E *k) {
    long dp, dq;
    if (!poly_degree_in(p, k, &dp) || !poly_degree_in(q, k, &dq) || dp != dq || dp < 1) return -1;
    E *h = rat_normal(s_div(s_sub(s_div(poly_coeff(p, k, dp - 1), poly_coeff(p, k, dp)),
                                  s_div(poly_coeff(q, k, dq - 1), poly_coeff(q, k, dq))),
                            mk_int(dp)));
    long hv;
    if (!num_is_small_int(h, &hv) || hv < 0 || hv > 60) return -1;
    E *d = rat_normal(s_sub(s_mul2(poly_coeff(q, k, dq), p), s_mul2(poly_coeff(p, k, dp), subst(q, k, s_add2(k, mk_int(hv))))));
    return is_zero(d) ? hv : -1;
}

static int gosper_form(XP, E *r, E *k, gform *g) {
    E *nu, *de;
    X->numden(rat_normal(r), &nu, &de);
    g->a = expand(nu), g->b = expand(de), g->c = K->c1;
    for (int it = 0; it < 24; it++) {
        poll_intr();
        E *ca, *cb, *fa = X->factor_list_in(g->a, k, &ca), *fb = X->factor_list_in(g->b, k, &cb);
        int moved = 0;
        for (uint32_t i = 0; i < fa->n && !moved; i++)
            for (uint32_t j = 0; j < fb->n && !moved; j++) {
                E *p = K0(EK(fa)[i]), *q = K0(EK(fb)[j]);
                long h = shift_of(X, p, q, k);
                if (h < 0) continue;
                /* p(k) | a, p(k-h) ~ q | b */
                E *ph = subst(p, k, s_sub(k, mk_int(h)));
                g->a = expand(rat_normal(s_div(g->a, p)));
                g->b = expand(rat_normal(s_div(g->b, ph)));
                for (long m = 1; m <= h; m++) g->c = s_mul2(g->c, subst(p, k, s_sub(k, mk_int(m))));
                moved = 1;
            }
        if (!moved) {
            g->c = expand(g->c);
            return 1;
        }
    }
    return 0;
}

/* the degree bound for x in a(k) x(k+1) - b(k-1) x(k) = c(k), -1 if none */
static long xdeg(XP, E *A, E *B, long dc, E *k) {
    long da, db;
    if (!poly_degree_in(A, k, &da) || !poly_degree_in(B, k, &db)) return -1;
    long d = da > db ? da : db;
    E *la = poly_coeff(A, k, d), *lb = poly_coeff(B, k, d);
    if (da != db || !is_zero(rat_normal(s_sub(la, lb)))) return dc - d;
    long D = dc - d + 1;
    E *t = rat_normal(s_div(s_sub(poly_coeff(B, k, d - 1), poly_coeff(A, k, d - 1)), la));
    long tv;
    if (d >= 1 && num_is_small_int(t, &tv) && tv > D) D = tv;
    return D;
}

/* solve a(k) x(k+1) - b(k-1) x(k) = c(k) + sum_j u_j c_j(k) (unknowns u[0..nu)) for a
 * polynomial x: x into *xo, the u_j into uo; 0 if none */
static int gosper_eq(XP, gform *g, E *k, uint32_t nu, E **u, E **uo, E **xo, E *f, long dc) {
    E *B1 = expand(subst(g->b, k, s_sub(k, K->c1)));
    long D = xdeg(X, g->a, B1, dc, k);
    if (D < 0 || D > 40) return 0;
    uint32_t nx = (uint32_t)D + 1, nv = nx + nu;
    E **v = salloc(sizeof(E *) * nv), **val = salloc(sizeof(E *) * nv);
    int fi = 40;
    E *x = K->c0;
    for (uint32_t i = 0; i < nx; i++) {
        v[i] = ix_fresh(X, f, &fi);
        x = s_add2(x, s_mul2(v[i], s_powi(k, (long)i)));
    }
    for (uint32_t j = 0; j < nu; j++) v[nx + j] = u[j];
    E *id = expand(s_sub(s_sub(s_mul2(g->a, subst(x, k, s_add2(k, K->c1))), s_mul2(B1, x)), g->c));
    long dd;
    if (!poly_degree_in(id, k, &dd)) return 0;
    E **eqs = salloc(sizeof(E *) * (size_t)(dd + 2));
    uint32_t ne = 0;
    for (long i = 0; i <= dd; i++) {
        E *c = poly_coeff(id, k, i);
        if (!is_zero(c)) eqs[ne++] = c;
    }
    if (!ix_lsolve(X, ne, eqs, nv, v, val)) return 0;
    *xo = rat_normal(subst_many(x, nv, v, val));
    for (uint32_t j = 0; j < nu; j++) uo[j] = val[nx + j];
    return 1;
}

/* the antidifference z of t (z(k+1) - z(k) = t(k)), NULL if t is not Gosper-summable */
static E *gosper(XP, E *t, E *k) {
    E *r = hratio(X, t, k);
    if (!r) return NULL;
    r = rat_normal(r);
    if (!ix_ratx(X, r, k)) return NULL;
    gform g;
    if (!gosper_form(X, r, k, &g)) return NULL;
    long dc;
    if (!poly_degree_in(g.c, k, &dc)) return NULL;
    E *x;
    if (!gosper_eq(X, &g, k, 0, NULL, NULL, &x, t, dc) || is_zero(x)) return NULL;
    E *B1 = subst(g.b, k, s_sub(k, K->c1));
    return s_mul2(rat_normal(s_div(s_mul2(B1, x), g.c)), t);
}

/* ---- Zeilberger ---- */

/* the gamma functions of e with argument m + 1/2 as (2m)! sqrt(pi)/(4^m m!), then
 * (A)!/((B)! (A-B)!) as binomial(A, B) where it fits */
static E *gam_half(XP, E *e) {
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN) return e;
    stack_check();
    E **kk = salloc(sizeof(E *) * e->n);
    for (uint32_t i = 0; i < e->n; i++) kk[i] = gam_half(X, EK(e)[i]);
    e = X->rebuild(e, kk);
    if (IFN(e, F_GAMMA)) {
        E *m = s_sub(K0(e), K->chalf);
        if (!is_num(K0(e)) && K0(e)->t == E_ADD) {
            /* the numeric part of the argument is a half-integer */
            E *c = NULL;
            for (uint32_t i = 0; i < K0(e)->n; i++)
                if (is_num(EK(K0(e))[i])) c = EK(K0(e))[i];
            if (c && is_rat(c) && expr_eq(K1(c), K->c2)) {
                E *two = s_mul2(K->c2, m);
                return s_div(s_mul2(s_fn1(F_FACT, two), s_sqrt(K->pi_)), s_mul2(s_pow(K->c2, two), s_fn1(F_FACT, m)));
            }
        }
    }
    if (IFN(e, F_GAMMA)) return s_fn1(F_FACT, s_sub(K0(e), K->c1));
    return e;
}

/* e with its symbols set to positive sample values */
static int sample(XP, E *e, double *v) {
    E *vs[9], *vals[9];
    uint32_t n = 0;
    ix_syms(X, e, mk_sym_name("_none"), vs, &n, 8);
    for (uint32_t i = 0; i < n; i++) vals[i] = mk_flt(1.3 + 0.71 * i);
    double im;
    return X->eval_complex(n ? subst_many(e, n, vs, vals) : e, v, &im);
}

/* the factorials of a product with exponents: c (A)!/((B)! (C)!) with A = B + C -> binomial */
static E *to_binom(XP, E *e) {
    if (e->t != E_MUL) return e;
    E *num = NULL, *d1 = NULL, *d2 = NULL, *rest = K->c1;
    for (uint32_t i = 0; i < e->n; i++) {
        E *q = EK(e)[i];
        if (IFN(q, F_FACT) && !num) num = q;
        else if (q->t == E_POW && IFN(K0(q), F_FACT) && expr_eq(K1(q), K->cm1) && !d1) d1 = K0(q);
        else if (q->t == E_POW && IFN(K0(q), F_FACT) && expr_eq(K1(q), K->cm1) && !d2) d2 = K0(q);
        else if (q->t == E_POW && IFN(K0(q), F_FACT) && expr_eq(K1(q), mk_int(-2)) && !d1 && !d2) d1 = d2 = K0(q);
        else rest = s_mul2(rest, q);
    }
    if (!num || !d1 || !d2 || !is_zero(rat_normal(s_sub(K0(num), s_add2(K0(d1), K0(d2)))))) return e;
    /* the lower argument that is nonnegative for positive symbols; a negative upper one
     * by upper negation binomial(A, B) = (-1)^B binomial(B - A - 1, B) */
    E *A = K0(num), *B = K0(d1);
    double va, vb;
    if (sample(X, B, &vb) && vb < 0) B = K0(d2);
    if (sample(X, A, &va) && va < 0 && sample(X, B, &vb) && vb >= 0)
        return s_mul2(s_div(rest, s_pow(K->cm1, B)), s_fn2(F_BINOM, s_sub(s_sub(B, A), K->c1), B)); /* (-1)^B = (-1)^-B */
    return s_mul2(rest, s_fn2(F_BINOM, A, B));
}

/* A!/B! with A - B a small integer as a product; binomial(A, B) with B - A a positive integer
 * is 0 */
static E *fratio(XP, E *e) {
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN) return e;
    stack_check();
    E **kk = salloc(sizeof(E *) * e->n);
    for (uint32_t i = 0; i < e->n; i++) kk[i] = fratio(X, EK(e)[i]);
    e = X->rebuild(e, kk);
    if (IFN(e, F_BINOM) && e->n == 2) {
        long d;
        if (num_is_small_int(rat_normal(s_sub(K1(e), K0(e))), &d) && d > 0) return K->c0;
    }
    if (e->t != E_MUL) return e;
    E **f = salloc(sizeof(E *) * e->n);
    uint32_t n = e->n;
    for (uint32_t i = 0; i < n; i++) f[i] = EK(e)[i];
    for (uint32_t i = 0; i < n; i++) {
        if (!f[i] || !IFN(f[i], F_FACT)) continue;
        for (uint32_t j = 0; j < n; j++) {
            if (!f[j] || f[j]->t != E_POW || !IFN(K0(f[j]), F_FACT) || !expr_eq(K1(f[j]), K->cm1)) continue;
            E *A = K0(f[i]), *B = K0(K0(f[j]));
            long d;
            if (!num_is_small_int(rat_normal(s_sub(A, B)), &d) || d < -12 || d > 12) continue;
            E *r = K->c1;
            for (long t = 1; t <= d; t++) r = s_mul2(r, s_add2(B, mk_int(t)));
            for (long t = 1; t <= -d; t++) r = s_div(r, s_add2(A, mk_int(t)));
            f[i] = r, f[j] = NULL;
            break;
        }
    }
    E *r = K->c1;
    for (uint32_t i = 0; i < n; i++)
        if (f[i]) r = s_mul2(r, f[i]);
    return r;
}

static E *nice(XP, E *e) {
    e = fratio(X, gam_half(X, e));
    E *s = eval_cmd(C_SIMPLIFY, 1, &e);
    return to_binom(X, s);
}

/* prod_{j=0}^{N-1} rho(j) for rho rational with linear factors over Q(params):
 * (p j + q) -> p^N gamma(N + q/p)/gamma(q/p); NULL if a factor is not linear */
static E *rprod(XP, E *rho, E *j, E *N) {
    E *nd[2];
    X->numden(rat_normal(rho), &nd[0], &nd[1]);
    E *r = K->c1;
    for (int s = 0; s < 2; s++) {
        E *ct, *fl = X->factor_list_in(expand(nd[s]), j, &ct);
        E *part = is_num(ct) && num_sgn(ct) < 0 ? s_mul2(s_pow(K->cm1, N), s_pow(s_neg(ct), N)) : s_pow(ct, N);
        for (uint32_t i = 0; i < fl->n; i++) {
            E *g = K0(EK(fl)[i]), *b;
            long m;
            num_is_small_int(K1(EK(fl)[i]), &m);
            E *p = lincoef(X, g, j, &b);
            if (!p || is_zero(p)) {
                if (!freeof(g, j)) return NULL;
                part = s_mul2(part, s_pow(g, s_mul2(mk_int(m), N)));
                continue;
            }
            E *q = s_div(b, p);
            if (is_zero(q)) q = NULL; /* p j: gamma(N)/gamma(0) -> 0 */
            if (!q) return NULL;
            E *gr = s_div(s_fn1(F_GAMMA, s_add2(N, q)), s_fn1(F_GAMMA, q));
            part = s_mul2(part, s_powi(s_mul2(s_pow(p, N), gr), m));
        }
        r = s ? s_div(r, part) : s_mul2(r, part);
    }
    return r;
}

static E *sum_at(XP, E *F, E *k, E *n, long nv) {
    E *v[4] = {subst(F, n, mk_int(nv)), k, K->c0, mk_int(nv)};
    return try_cmd(C_SUM, 4, v);
}

/* sum_{k=0}^{n} F(n, k) by creative telescoping, NULL if not found */
static E *zeil(XP, E *F, E *k, E *n) {
    E *rk = hratio(X, F, k), *rn = hratio(X, F, n);
    if (!rk || !rn) return NULL;
    rk = rat_normal(rk), rn = rat_normal(rn);
    if (!ix_ratx(X, rk, k) || !ix_ratx(X, rn, k) || !ix_ratx(X, rn, n)) return NULL;
    for (int J = 1; J <= 2; J++) {
        poll_intr();
        /* F(n+j,k)/F(n,k) = N_j/D_j */
        E *Nj[3], *Dj[3], *ratio = K->c1, *L = K->c1;
        for (int j = 0; j <= J; j++) {
            if (j) ratio = rat_normal(s_mul2(ratio, subst(rn, n, s_add2(n, mk_int(j - 1)))));
            X->numden(ratio, &Nj[j], &Dj[j]);
            E *ga[2] = {L, Dj[j]}, *gd = try_cmd(C_GCD, 2, ga);
            if (!gd) return NULL;
            L = rat_normal(s_div(s_mul2(L, Dj[j]), gd));
        }
        if (!ix_ratx(X, L, k)) return NULL;
        /* P(k) = sum_j s_j N_j L/D_j, t = P Fbar, Fbar = F/L */
        int fi = 60;
        E *sj[3], *P = K->c0;
        for (int j = 0; j <= J; j++) {
            sj[j] = ix_fresh(X, F, &fi);
            P = s_add2(P, s_mul2(sj[j], rat_normal(s_div(s_mul2(Nj[j], L), Dj[j]))));
        }
        P = expand(P);
        E *rb = rat_normal(s_div(s_mul2(rk, L), subst(L, k, s_add2(k, K->c1))));
        gform g;
        if (!gosper_form(X, rb, k, &g)) return NULL;
        g.c = expand(s_mul2(g.c, P));
        long dc;
        if (!poly_degree_in(g.c, k, &dc)) return NULL;
        /* normalise s_J = 1 (else s_0 = 1) */
        for (int nz = 0; nz < 2; nz++) {
            int fix = nz ? 0 : J;
            gform g2 = g;
            g2.c = expand(subst(g.c, sj[fix], K->c1));
            E *us[3], *uo[3], *x;
            uint32_t nu = 0;
            for (int j = 0; j <= J; j++)
                if (j != fix) us[nu++] = sj[j];
            if (!gosper_eq(X, &g2, k, nu, us, uo, &x, F, dc)) continue;
            E *s[3];
            uint32_t q = 0;
            for (int j = 0; j <= J; j++) s[j] = j == fix ? K->c1 : uo[q++];
            /* the recurrence sum_j s_j(n) S(n+j) = 0 */
            E *S0 = sum_at(X, F, k, n, 0);
            if (!S0) return NULL;
            if (J == 1) {
                if (is_zero(s[1]) || is_zero(s[0])) continue;
                E *rho = rat_normal(s_neg(s_div(s[0], s[1])));
                int jj = 60;
                E *j = ix_fresh(X, F, &jj);
                for (long n0 = 0; n0 <= 2; n0++) { /* S(n) = S(n0) prod_{j=n0}^{n-1} rho(j) */
                    E *Sn0 = n0 ? sum_at(X, F, k, n, n0) : S0;
                    E *pr = Sn0 ? rprod(X, subst(rho, n, s_add2(j, mk_int(n0))), j, s_sub(n, mk_int(n0))) : NULL;
                    if (pr) return nice(X, s_mul2(Sn0, pr));
                }
                return NULL;
            }
            /* J = 2 with s_1 = 0: S(n+2) = rho(n) S(n); odd terms vanish */
            if (!is_zero(rat_normal(s[1])) || is_zero(s[2])) return NULL;
            E *S1 = sum_at(X, F, k, n, 1);
            if (!S1 || !is_zero(S1)) return NULL;
            E *rho = rat_normal(s_neg(s_div(s[0], s[2])));
            int jj = 60;
            E *j = ix_fresh(X, F, &jj);
            E *pr = rprod(X, subst(rho, n, s_mul2(K->c2, j)), j, s_div(n, K->c2));
            if (!pr) return NULL;
            E *ev = s_div(s_add2(K->c1, s_pow(K->cm1, n)), K->c2);
            return s_mul2(ev, nice(X, s_mul2(S0, pr)));
        }
    }
    return NULL;
}

/* sum_{k=lo}^{hi} t: Gosper, then (lo = 0, hi = n a symbol of t) Zeilberger; NULL if none */
E *hx_sum(XP, E *t, E *k, E *lo, E *hi) {
    if (ISYM(hi, S_INF) || ISYM(lo, S_INF)) return NULL;
    E *z = gosper(X, t, k);
    if (z) {
        E *r = s_sub(subst(z, k, s_add2(hi, K->c1)), subst(z, k, lo));
        return nice(X, r);
    }
    if (hi->t == E_SYM && is_zero(lo) && !freeof(t, hi)) return zeil(X, t, k, hi);
    return NULL;
}
