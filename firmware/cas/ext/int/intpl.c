/*
 * intpl.c -- polylogarithms for powers of logarithms times rational functions:
 *   int ln(t)^j R(t) dt, R rational in t with linear (and quadratic, split at their roots)
 *   factors in the denominator: partial fractions, then
 *     t^k:          the core (ln(t)^j t^k is elementary; k = -1 gives ln(t)^(j+1)/(j+1));
 *     1/(t - rho):  I_j = ln(t)^j ln(1 - t/rho)
 *                         + sum_(i<j) (-1)^i j!/(j-1-i)! ln(t)^(j-1-i) Li_(2+i)(t/rho);
 *     1/(t - rho)^m (m >= 2): by parts, -ln(t)^j/((m-1)(t-rho)^(m-1))
 *                         + j/(m-1) int ln(t)^(j-1)/(t (t-rho)^(m-1)).
 * Front ends:
 *   P(x) R(e^(c_i x)) with commensurable rates: t = e^(r x), x = ln(t)/r (P polynomial);
 *   R(x) ln(c (a + b x)^n)^k: t = a + b x, ln(c t^n) = n ln t + K (K locally constant).
 * The caller checks the result by differentiation.
 */
#include "intx.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

typedef struct {
    E *t, *L; /* t and the symbol standing for ln t */
    int depth;
} pctx;

static E *lnrat(XP, pctx *P, E *R, long j);

/* int ln(t)^j / (t - rho) */
static E *Ij(XP, pctx *P, long j, E *rho) {
    E *t = P->t, *L = P->L;
    if (is_zero(rho)) return s_div(s_powi(L, j + 1), mk_int(j + 1));
    E *z = s_div(t, rho), *r = s_mul2(s_powi(L, j), s_fn1(F_LN, s_sub(K->c1, z)));
    E *pl = mk_sym_name("polylog");
    for (long i = 0; i < j; i++) {
        long fac = 1;
        for (long q = j - i; q <= j; q++) fac *= q; /* j!/(j-1-i)! = j (j-1) ... (j-i) */
        E *term = s_mul(3, (E *[]){mk_int((i & 1) ? -fac : fac), s_powi(L, j - 1 - i), s_fn2(pl->s, mk_int(2 + i), z)});
        r = s_add2(r, term);
    }
    return r;
}

/* int ln(t)^j / (t - rho)^m */
static E *Ijm(XP, pctx *P, long j, E *rho, long m) {
    if (m == 1) return Ij(X, P, j, rho);
    E *t = P->t;
    E *r = s_neg(s_div(s_powi(P->L, j), s_mul2(mk_int(m - 1), s_powi(s_sub(t, rho), m - 1))));
    if (j == 0) return r;
    E *rest = lnrat(X, P, s_inv(s_mul2(t, s_powi(s_sub(t, rho), m - 1))), j - 1);
    if (!rest) return NULL;
    return s_add2(r, s_mul2(s_div(mk_int(j), mk_int(m - 1)), rest));
}

/* a square root of e up to sign (squares pulled out): the two roots of a quadratic only swap */
static E *fsq(XP, E *e) {
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

/* the term nu/de of a partial-fraction expansion: c t^k terms are returned as NULL with *pow set */
static E *pf_term(XP, pctx *P, E *term, long j, int *ok) {
    E *t = P->t, *nu, *de;
    X->numden(term, &nu, &de);
    nu = expand(nu), de = expand(de);
    long dn, dd;
    *ok = 0;
    if (!poly_degree_in(nu, t, &dn) || !poly_degree_in(de, t, &dd) || dn > 30 || dd > 30) return NULL;
    E *ld = poly_coeff(de, t, dd);
    if (is_zero(expand(s_sub(de, s_mul2(ld, s_powi(t, dd)))))) { /* a Laurent polynomial */
        E *r = K->c0;
        for (long i = 0; i <= dn; i++) {
            E *ci = poly_coeff(nu, t, i);
            if (is_zero(ci)) continue;
            long k = i - dd;
            E *s;
            if (k == -1) s = s_div(s_powi(P->L, j + 1), mk_int(j + 1));
            else { /* the core: ln(t)^j t^k */
                E *g = ix_int(X, s_mul2(s_powi(s_fn1(F_LN, t), j), s_powi(t, k)), t);
                if (!g) return NULL;
                s = subst(g, s_fn1(F_LN, t), P->L);
            }
            r = s_add2(r, s_mul2(s_div(ci, ld), s));
        }
        *ok = 1;
        return r;
    }
    if (dn == 0 && dd >= 1) { /* c/(t - rho)^m */
        E *rho = rat_normal(s_neg(s_div(poly_coeff(de, t, dd - 1), s_mul2(mk_int(dd), ld))));
        if (is_zero(expand(s_sub(de, s_mul2(ld, s_powi(s_sub(t, rho), dd)))))) {
            E *s = Ijm(X, P, j, rho, dd);
            if (!s) return NULL;
            *ok = 1;
            return s_mul2(s_div(nu, ld), s);
        }
    }
    if (dd == 4 && dn < 4 && is_zero(poly_coeff(de, t, 3)) && is_zero(poly_coeff(de, t, 1))) {
        /* a t^4 + b t^2 + c: the roots +-sqrt(s), s = (-b +- sqrt(b^2 - 4 a c))/(2 a); simple
         * poles: sum nu(r)/de'(r) I_j(r) */
        E *a = ld, *b = poly_coeff(de, t, 2), *c = poly_coeff(de, t, 0);
        E *sq = fsq(X, factor_expr(s_sub(s_mul2(b, b), s_mul(3, (E *[]){mk_int(4), a, c}))));
        if (is_zero(sq) || is_zero(c)) return NULL;
        E *dde = diff(de, t), *r = K->c0;
        for (int k = 0; k < 4; k++) {
            E *sv = s_div(k < 2 ? s_add2(s_neg(b), sq) : s_sub(s_neg(b), sq), s_mul2(K->c2, a));
            E *rt = s_sqrt(sv);
            if (k & 1) rt = s_neg(rt);
            E *ck = s_div(subst(nu, t, rt), subst(dde, t, rt));
            r = s_add2(r, s_mul2(ck, Ij(X, P, j, rt)));
        }
        *ok = 1;
        return r;
    }
    if (dd == 2 && dn <= 1) { /* (A t + B)/(a t^2 + b t + c) at the roots */
        E *a = ld, *b = poly_coeff(de, t, 1), *c = poly_coeff(de, t, 0);
        E *sq = fsq(X, factor_expr(s_sub(s_mul2(b, b), s_mul(3, (E *[]){mk_int(4), a, c}))));
        if (is_zero(sq)) return NULL;
        E *r1 = s_div(s_add2(s_neg(b), sq), s_mul2(K->c2, a)), *r2 = s_div(s_sub(s_neg(b), sq), s_mul2(K->c2, a));
        E *A = poly_coeff(nu, t, 1), *B = poly_coeff(nu, t, 0);
        /* (A t + B)/(a (t-r1)(t-r2)) = c1/(t-r1) + c2/(t-r2), c1 = (A r1 + B)/(a (r1 - r2)) */
        E *den = s_mul2(a, s_sub(r1, r2));
        E *c1 = s_div(s_add2(s_mul2(A, r1), B), den), *c2 = s_neg(s_div(s_add2(s_mul2(A, r2), B), den));
        E *s1 = Ij(X, P, j, r1), *s2 = Ij(X, P, j, r2);
        *ok = 1;
        return s_add2(s_mul2(c1, s1), s_mul2(c2, s2));
    }
    return NULL;
}

/* int ln(t)^j R(t) dt, ln t written as P->L */
static E *lnrat(XP, pctx *P, E *R, long j) {
    if (++P->depth > 40) return NULL;
    poll_intr();
    E *a1[2] = {R, P->t}, *pf = try_cmd(C_PARTFRAC, 2, a1);
    if (!pf) return NULL;
    pf = expand(pf);
    E **ts = pf->t == E_ADD ? EK(pf) : &pf;
    uint32_t n = pf->t == E_ADD ? pf->n : 1;
    if (n > 40) return NULL;
    E *r = K->c0;
    for (uint32_t i = 0; i < n; i++) {
        int ok;
        E *s = pf_term(X, P, ts[i], j, &ok);
        if (!ok || !s) return NULL;
        r = s_add2(r, s);
    }
    return r;
}

/* int sum_j L^j R_j(t) dt for g a polynomial in the symbol L with coefficients rational in t */
static E *lnpoly(XP, pctx *P, E *g) {
    g = expand(g);
    long d;
    if (!poly_degree_in(g, P->L, &d) || d > 8) return NULL;
    E *r = K->c0;
    int any = 0;
    for (long j = 0; j <= d; j++) {
        E *cj = poly_coeff(g, P->L, j);
        if (is_zero(cj)) continue;
        if (!freeof(cj, P->L)) return NULL;
        E *s = lnrat(X, P, rat_normal(cj), j);
        if (!s) return NULL;
        any |= j > 0;
        r = s_add2(r, s);
    }
    return any ? r : NULL;
}

/* ---- front end 1: P(x) R(e^(c x)) ---- */

typedef struct {
    E *r, *u, *Lx; /* first rate, t, the symbol for x */
    long q;
    int bad, pass;
} exctx;

static long lcm2(long a, long b) {
    long x = a, y = b;
    while (y) {
        long t = x % y;
        x = y, y = t;
    }
    return a / x * b;
}

static E *esub(XP, exctx *C, E *e, E *x) {
    if (C->bad || freeof(e, x)) return e;
    stack_check();
    E *w = NULL;
    if (expr_eq(e, x)) return C->pass ? C->Lx : e;
    if (e->t == E_POW && !freeof(K1(e), x)) {
        if (!freeof(K0(e), x)) {
            C->bad = 1;
            return e;
        }
        w = ISYM(K0(e), S_E) ? K1(e) : s_mul2(K1(e), s_fn1(F_LN, K0(e)));
    } else if (IFN(e, F_EXP))
        w = K0(e);
    if (w) {
        long d;
        w = expand(w);
        if (!poly_degree_in(w, x, &d) || d != 1) {
            C->bad = 1;
            return e;
        }
        E *c = poly_coeff(w, x, 1), *a = poly_coeff(w, x, 0);
        if (!C->r) C->r = c;
        E *ra = rat_normal(s_div(c, C->r));
        long den = 1;
        if (!(ra->t == E_INT || ra->t == E_RAT)) {
            C->bad = 1;
            return e;
        }
        if (ra->t == E_RAT && (!num_is_small_int(K1(ra), &den) || den > 12)) {
            C->bad = 1;
            return e;
        }
        if (!C->pass) {
            C->q = lcm2(C->q, den);
            return e;
        }
        return s_mul2(s_exp(a), s_pow(C->u, s_mul2(ra, mk_int(C->q))));
    }
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW) {
        C->bad = 1;
        return e;
    }
    E **k = salloc(sizeof(E *) * e->n);
    for (uint32_t i = 0; i < e->n; i++) k[i] = esub(X, C, EK(e)[i], x);
    return X->rebuild(e, k);
}

static int has_trig(XP, E *e) {
    if (IFN(e, F_SIN) || IFN(e, F_COS) || IFN(e, F_TAN) || IFN(e, F_SEC) || IFN(e, F_CSC) || IFN(e, F_COT) ||
        IFN(e, F_SINH) || IFN(e, F_COSH) || IFN(e, F_TANH))
        return 1;
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_trig(X, EK(e)[i])) return 1;
    return 0;
}

static E *pl_exp(XP, E *f, E *x) {
    E *g = f;
    if (has_trig(X, f)) {
        E *a1[1] = {f};
        if (!(g = try_cmd(C_TRIG2EXP, 1, a1))) return NULL;
    }
    exctx C = {NULL, NULL, NULL, 1, 0, 0};
    esub(X, &C, g, x);
    if (C.bad || !C.r) return NULL;
    int fi = 60;
    pctx P;
    P.t = C.u = ix_fresh(X, f, &fi), P.L = ix_fresh(X, f, &fi), P.depth = 0;
    E *rq = s_div(C.r, mk_int(C.q)); /* t = e^(rq x), x = L/rq */
    C.Lx = s_div(P.L, rq), C.pass = 1;
    E *h = esub(X, &C, g, x);
    if (C.bad || !freeof(h, x)) return NULL;
    h = s_div(h, s_mul2(rq, P.t));
    E *r = lnpoly(X, &P, h);
    if (!r) return NULL;
    E *ks[2] = {P.t, P.L}, *vs[2] = {s_exp(s_mul2(rq, x)), s_mul2(rq, x)};
    return subst_many(r, 2, ks, vs);
}

/* ---- front end 2: R(x) ln(c (a + b x)^n)^k ---- */

typedef struct {
    E *lin, *t, *L, *x;
    E *Ks[4], *Kv[4];
    int nk, bad;
} lnctx;

static E *lsub(XP, lnctx *C, E *e) {
    if (C->bad || freeof(e, C->x)) return e;
    stack_check();
    if (IFN(e, F_LN)) {
        /* u = c prod lin_i^(ex_i), the lin_i proportional: ln u = n ln(lin) + K */
        E *u = K0(e), *lin = NULL, *n = K->c0;
        E **fs = u->t == E_MUL ? EK(u) : &u;
        uint32_t nf = u->t == E_MUL ? u->n : 1;
        for (uint32_t i = 0; i < nf; i++) {
            E *v = fs[i], *base = v, *ex = K->c1;
            if (freeof(v, C->x)) continue;
            if (v->t == E_POW && freeof(K1(v), C->x)) ex = K1(v), base = K0(v);
            long d;
            if (!poly_degree_in(base, C->x, &d) || d != 1) { C->bad = 1; return e; }
            if (lin && !freeof(rat_normal(s_div(base, lin)), C->x)) { C->bad = 1; return e; }
            if (!lin) lin = base;
            n = s_add2(n, ex);
        }
        if (!lin) return e;
        if (!C->lin) C->lin = lin;
        else if (!expr_eq(C->lin, lin)) { C->bad = 1; return e; }
        /* ln u = n ln(lin) + K, K = ln u - n ln(lin) locally constant */
        if (C->nk == 4) { C->bad = 1; return e; }
        int fi = 70 + C->nk;
        E *k = ix_fresh(X, e, &fi);
        C->Ks[C->nk] = k, C->Kv[C->nk++] = s_sub(e, s_mul2(n, s_fn1(F_LN, lin)));
        return s_add2(s_mul2(n, C->L), k);
    }
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW) {
        if (e->t == E_FN) C->bad = 1;
        return e;
    }
    E **kk = salloc(sizeof(E *) * e->n);
    for (uint32_t i = 0; i < e->n; i++) kk[i] = lsub(X, C, EK(e)[i]);
    return X->rebuild(e, kk);
}

static E *pl_log(XP, E *f, E *x) {
    lnctx C;
    C.lin = NULL, C.nk = 0, C.bad = 0, C.x = x;
    int fi = 60;
    C.t = ix_fresh(X, f, &fi), C.L = ix_fresh(X, f, &fi);
    E *g = lsub(X, &C, f);
    if (C.bad || !C.lin || !C.nk) return NULL;
    E *a = poly_coeff(C.lin, x, 0), *b = poly_coeff(C.lin, x, 1);
    g = subst(g, x, s_div(s_sub(C.t, a), b)); /* x = (t - a)/b, dx = dt/b */
    if (!freeof(g, x)) return NULL;
    g = s_div(g, b);
    pctx P;
    P.t = C.t, P.L = C.L, P.depth = 0;
    E *r = lnpoly(X, &P, g);
    if (!r) return NULL;
    E *ks[2] = {P.t, P.L}, *vs[2] = {C.lin, s_fn1(F_LN, C.lin)};
    r = subst_many(r, 2, ks, vs);
    for (int i = 0; i < C.nk; i++) r = subst(r, C.Ks[i], C.Kv[i]);
    return r;
}

/* ---- front end 3: G ln(a + b x^q)/x, G a polynomial in logarithms of monomials ----
 * ln(a + b x^q) = K + ln(1 - z), z = -b x^q/a, K locally constant;
 * int G ln(1 - z)/x = -G Li_2(z)/q + 1/q int (x G') Li_2(z)/x  (the by-parts recursion) */
static int mono_logs(XP, E *e, E *x) { /* e depends on x only through ln(c x^n) */
    if (freeof(e, x)) return 1;
    if (IFN(e, F_LN)) return freeof(rat_normal(s_div(s_mul2(x, diff(K0(e), x)), K0(e))), x);
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (!mono_logs(X, EK(e)[i], x)) return 0;
    return 1;
}

static E *pl_lnbin(XP, E *f, E *x) {
    E *g = s_mul2(f, x);
    E **fs = g->t == E_MUL ? EK(g) : &g;
    uint32_t n = g->t == E_MUL ? g->n : 1;
    E *lnB = NULL, *G = K->c1;
    for (uint32_t i = 0; i < n; i++) {
        E *h = fs[i];
        if (!lnB && IFN(h, F_LN) && !mono_logs(X, h, x)) lnB = h;
        else G = s_mul2(G, h);
    }
    if (!lnB || !mono_logs(X, G, x)) return NULL;
    E *B = K0(lnB), *a = K->c0, *t = NULL;
    if (B->t != E_ADD) return NULL;
    for (uint32_t i = 0; i < B->n; i++) {
        if (freeof(EK(B)[i], x)) a = s_add2(a, EK(B)[i]);
        else if (!t) t = EK(B)[i];
        else return NULL;
    }
    if (!t || is_zero(a)) return NULL;
    E *q = rat_normal(s_div(s_mul2(x, diff(t, x)), t));
    if (!freeof(q, x) || is_zero(q)) return NULL;
    E *z = rat_normal(s_neg(s_div(t, a)));
    E *Kc = s_sub(lnB, s_fn1(F_LN, s_sub(K->c1, z)));
    E *I0 = ix_int(X, s_div(G, x), x); /* K int G/x */
    if (!I0) return NULL;
    E *li2 = s_fn2(mk_sym_name("polylog")->s, K->c2, z);
    E *r = s_add2(s_mul2(Kc, I0), s_neg(s_div(s_mul2(G, li2), q)));
    E *xg = rat_normal(s_mul2(x, diff(G, x)));
    if (!is_zero(xg)) {
        E *rest = ix_ibp(X, s_div(s_mul2(xg, li2), x), x);
        if (!rest) return NULL;
        r = s_add2(r, s_div(rest, q));
    }
    return r;
}

E *ix_polylog(XP, E *f, E *x) {
    char *mk = K->s_top;
    E *r = pl_exp(X, f, x);
    if (r && ix_verify(X, f, r, x)) return r;
    K->s_top = mk;
    r = pl_log(X, f, x);
    if (r && ix_verify(X, f, r, x)) return r;
    K->s_top = mk;
    r = pl_lnbin(X, f, x);
    if (r && ix_verify(X, f, r, x)) return r;
    K->s_top = mk;
    return NULL;
}
