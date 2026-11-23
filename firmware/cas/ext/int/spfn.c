/*
 * spfn.c -- special-function and analysis commands of the casx module:
 *   chebyshevt(n, x), chebyshevu(n, x), legendre(n, x), hermite(n, x), laguerre(n, x)
 *       the classical orthogonal polynomials for an integer n >= 0 (0 <= n <= 4000), from the
 *       explicit coefficient sums (one pass, no recurrence):
 *         T_n = n/2 sum_k (-1)^k (n-k-1)!/(k! (n-2k)!) (2x)^(n-2k),  U_n, P_n (Legendre),
 *         H_n (physicists'), L_n (Laguerre);  a symbolic n at x = 1, -1 (and T_n(0)) gives the
 *       closed values (T_n(-1) = (-1)^n, ...), otherwise the call stays unevaluated;
 *   legendrep(n, m, x)   associated Legendre P_n^m = (-1)^m (1 - x^2)^(m/2) d^m/dx^m P_n;
 *   digamma(x)   x a rational p/q (q <= 12): Gauss's digamma theorem
 *         psi(p/q) = -gamma - ln(2q) - pi/2 cot(pi p/q) + 2 sum_k cos(2 pi k p/q) ln sin(pi k/q)
 *       for 0 < p/q < 1, shifted by psi(x + 1) = psi(x) + 1/x; a float: the asymptotic series;
 *   residue(f, z, a)   the pole order m <= 12 from the limits of (z - a)^m f, then
 *       lim d^(m-1)/dz^(m-1) ((z - a)^m f)/(m-1)!;
 *   minimize(f, x), maximize(f, x)   the least / greatest value of f over the reals: the values
 *       at the real critical points and the limits at -inf and inf;
 *   cartprod(A, B, ...)   the Cartesian product of lists.
 */
#include "intx.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

enum { S_CHT, S_CHU, S_LEG, S_HER, S_LAG, S_LEGP, S_DIG, S_RES, S_MIN, S_MAX, S_CART };
static const casx_cmd_t stab[] = {
    {"chebyshevt", 2, 2}, {"chebyshevu", 2, 2}, {"legendre", 2, 2}, {"hermite", 2, 2},
    {"laguerre", 2, 2},   {"legendrep", 3, 3},  {"digamma", 1, 1},  {"residue", 3, 3},
    {"minimize", 2, 2},   {"maximize", 2, 2},   {"cartprod", 2, 8},
};

/* the polynomial of kind op and degree n in x */
static E *opoly(XP, int op, long n, E *x) {
    E **t = salloc(sizeof(E *) * (size_t)(n + 2));
    uint32_t nt = 0;
    if (op == S_LAG) { /* sum (-1)^k C(n,k) x^k / k! */
        E *a = K->c1;
        for (long k = 0; k <= n; k++) {
            poll_intr();
            t[nt++] = s_mul2(a, s_powi(x, k));
            a = s_mul2(a, mk_frac(-(n - k), (k + 1) * (k + 1)));
        }
        return s_add(nt, t);
    }
    if (n == 0) return K->c1;
    E *c;
    if (op == S_CHT) c = s_powi(K->c2, n - 1);
    else if (op == S_CHU || op == S_HER) c = s_powi(K->c2, n);
    else { /* (2n)!/(2^n n!^2) */
        c = K->c1;
        for (long k = 1; k <= n; k++) c = s_mul2(c, mk_frac(2 * k - 1, k));
    }
    for (long k = 0; 2 * k <= n; k++) {
        poll_intr();
        t[nt++] = s_mul2(c, s_powi(x, n - 2 * k));
        long a = (n - 2 * k) * (n - 2 * k - 1), b;
        switch (op) {
        case S_CHT: b = 4 * (k + 1) * (n - k - 1); break;
        case S_CHU: b = 4 * (k + 1) * (n - k); break;
        case S_LEG: b = 2 * (k + 1) * (2 * n - 2 * k - 1); break;
        default: b = 4 * (k + 1); break;
        }
        if (!a) break;
        c = s_mul2(c, mk_frac(-a, b));
    }
    return s_add(nt, t);
}

/* symbolic degree: the values at x = 1, -1 (and T_n(0)) */
static E *opoly_sym(XP, int op, E *n, E *x) {
    long v;
    if (!num_is_small_int(x, &v) || v < -1 || v > 1) return NULL;
    E *sg = v == -1 ? s_pow(K->cm1, n) : K->c1;
    switch (op) {
    case S_CHT: return v ? sg : s_fn1(F_COS, s_mul2(n, s_div(K->pi_, K->c2)));
    case S_CHU: return v ? s_mul2(sg, s_add2(n, K->c1)) : NULL;
    case S_LEG: return v ? sg : NULL;
    }
    return NULL;
}

static E *digamma_q(XP, E *r) {
    E *p = K0(r), *q = K1(r);
    long pp, qq;
    if (!num_is_small_int(p, &pp) || !num_is_small_int(q, &qq) || qq > 12) return NULL;
    /* r = m + p'/q with 0 < p'/q < 1 */
    long m = pp >= 0 ? pp / qq : -((-pp + qq - 1) / qq), p1 = pp - m * qq;
    if (m > 1000 || m < -1000) return NULL;
    E *pq = mk_frac(p1, qq), *pi = K->pi_;
    E *v = s_add(3, (E *[]){s_neg(mk_sym(S_EULERG)), s_neg(s_fn1(F_LN, mk_int(2 * qq))),
                             s_neg(s_mul2(s_div(pi, K->c2), s_fn1(F_COT, s_mul2(pi, pq))))});
    for (long k = 1; 2 * k <= qq - 1; k++)
        v = s_add2(v, s_mul(3, (E *[]){K->c2, s_fn1(F_COS, s_mul2(s_mul2(K->c2, pi), mk_frac(k * p1, qq))),
                                        s_fn1(F_LN, s_fn1(F_SIN, s_mul2(pi, mk_frac(k, qq))))}));
    for (long j = 0; j < m; j++) v = s_add2(v, s_inv(s_add2(pq, mk_int(j))));
    for (long j = -1; j >= m; j--) v = s_sub(v, s_inv(s_add2(pq, mk_int(j))));
    return v;
}

static E *digamma(XP, E *x) {
    long n;
    if (num_is_small_int(x, &n)) {
        if (n <= 0 || n > 100000) return NULL;
        E *h = s_neg(mk_sym(S_EULERG));
        for (long k = 1; k < n; k++) h = s_add2(h, mk_frac(1, k));
        return h;
    }
    if (is_rat(x)) return digamma_q(X, x);
    double v;
    if (x->t == E_FLT && to_double(x, &v) && v > 0 && v < 1e15) {
        double s = 0;
        while (v < 12) s -= 1 / v, v += 1;
        double i2 = 1 / (v * v);
        /* ln v - 1/(2v) - sum B_2k/(2k v^2k) */
        double lv;
        if (!to_double(s_fn1(F_LN, mk_flt(v)), &lv)) return NULL;
        s += lv - 0.5 / v -
             i2 * (1.0 / 12 - i2 * (1.0 / 120 - i2 * (1.0 / 252 - i2 * (1.0 / 240 - i2 * (1.0 / 132)))));
        return mk_flt(s);
    }
    return NULL;
}

static E *lim_dir(XP, E *f, E *z, E *a, int dir) {
    E *v[4] = {f, z, a, dir < 0 ? K->cm1 : K->c1};
    E *r = try_cmd(C_LIMIT, dir ? 4 : 3, v);
    if (!r || ix_has_fn(X, r, C_LIMIT) || !freeof(r, mk_sym(S_UNDEF)) || !freeof(r, z)) return NULL;
    return r;
}

static E *lim(XP, E *f, E *z, E *a) { return lim_dir(X, f, z, a, 0); }

static int is_infin(XP, E *e) { return !freeof(e, K->inf_); }

static E *residue(XP, E *f, E *z, E *a) {
    for (long m = 0; m <= 12; m++) {
        E *g = m ? rat_normal(s_mul2(s_powi(s_sub(z, a), m), f)) : f;
        E *L = lim(X, g, z, a);
        if (!L || is_infin(X, L)) continue;
        if (!m) return K->c0;
        E *d = g;
        for (long j = 1; j < m; j++) d = diff(d, z);
        E *R = lim(X, rat_normal(d), z, a);
        if (!R || is_infin(X, R)) return NULL;
        E *fa = K->c1;
        for (long j = 2; j < m; j++) fa = s_mul2(fa, mk_int(j));
        return s_div(R, fa);
    }
    return NULL;
}

/* f real and finite at x = p (a number) */
static int real_at(XP, E *f, E *x, double p) {
    double re, im;
    if (!X->eval_complex(evalf_digits(subst(f, x, mk_flt(p)), 15), &re, &im)) return 0;
    return re - re == 0 && __builtin_fabs(im) <= 1e-9 * (1 + __builtin_fabs(re));
}

/* the real roots of the arguments of ln, log and fractional powers in f that depend on x:
 * the possible ends of the real domain (ln(x) -> 0) */
static void dom_ends(XP, E *f, E *x, E **pts, uint32_t *np) {
    if (freeof(f, x) || *np >= 16) return;
    E *u = NULL;
    if ((IFN(f, F_LN) || IFN(f, F_LOG) || IFN(f, F_LOG10) || IFN(f, F_LOG2) || IFN(f, F_SQRT)) && f->n >= 1) u = K0(f);
    else if (f->t == E_POW && K1(f)->t == E_RAT) u = K0(f);
    if (u && !freeof(u, x)) {
        E *sol = X->solve_expr(s_fn2(F_EQ, u, K->c0), x, 0);
        if (sol && sol->t == E_LIST)
            for (uint32_t i = 0; i < sol->n && *np < 16; i++) {
                E *s = EK(sol)[i];
                if (IFN(s, F_EQ)) s = K1(s);
                if (is_const_expr(s)) pts[(*np)++] = s;
            }
    }
    if (f->t == E_ADD || f->t == E_MUL || f->t == E_POW || f->t == E_FN)
        for (uint32_t i = 0; i < f->n; i++) dom_ends(X, EK(f)[i], x, pts, np);
}

/* fold the candidate value L into best (sgn: 1 least, -1 greatest); 1 = keep going, 0 = fail,
 * 2 = unbounded (L is the answer) */
static int ext_take(XP, E *L, int sgn, E **best, double *bv) {
    if (!L) return 0;
    if (is_infin(X, L)) {
        int si = expr_eq(L, K->inf_) ? 1 : expr_eq(L, s_neg(K->inf_)) ? -1 : 0;
        if (!si) return 0;
        return si * sgn < 0 ? 2 : 1;
    }
    double v;
    if (!to_double(evalf_digits(L, 20), &v)) return 0;
    if (!*best || sgn * v < sgn * *bv) *best = L, *bv = v;
    return 1;
}

/* least (sgn = 1) or greatest (-1) value of f over its real domain: the critical points, the
 * limits at -inf and inf (where f is real out there) and the one-sided limits at the ends of the
 * domain coming from ln / roots (x*ln(x): domain x > 0, least value -1/e at x = 1/e) */
static E *extremum(XP, E *f, E *x, int sgn) {
    E *d = diff(f, x);
    E *eq = s_fn2(F_EQ, d, K->c0);
    E *sol = X->solve_expr(eq, x, 0);
    if (!sol || sol->t != E_LIST) return NULL;
    E *best = NULL, *pts[16];
    double bv = 0;
    uint32_t np = 0;
    int r;
    dom_ends(X, f, x, pts, &np);
    for (int s = -1; s <= 1; s += 2) { /* the limits at -inf and inf */
        if (np && !real_at(X, f, x, s * 1e6)) continue;
        if ((r = ext_take(X, lim(X, f, x, s < 0 ? s_neg(K->inf_) : K->inf_), sgn, &best, &bv)) != 1)
            return r == 2 ? (sgn > 0 ? s_neg(K->inf_) : K->inf_) : NULL;
    }
    for (uint32_t i = 0; i < np; i++) {
        double p;
        if (!to_double(evalf_digits(pts[i], 20), &p)) continue;
        for (int s = -1; s <= 1; s += 2) {
            double h = 1e-7 * (1 + __builtin_fabs(p));
            if (!real_at(X, f, x, p + s * h)) continue;
            E *L = lim_dir(X, f, x, pts[i], s);
            if ((r = ext_take(X, L, sgn, &best, &bv)) != 1)
                return r == 2 ? (sgn > 0 ? s_neg(K->inf_) : K->inf_) : NULL;
        }
    }
    for (uint32_t i = 0; i < sol->n; i++) {
        E *s = EK(sol)[i];
        if (IFN(s, F_EQ)) s = K1(s);
        double re, im;
        if (!X->eval_complex(evalf_digits(s, 20), &re, &im)) return NULL;
        if (__builtin_fabs(im) > 1e-12 * (1 + __builtin_fabs(re))) continue;
        if (np && !real_at(X, f, x, re)) continue;
        E *v = subst(f, x, s);
        E *sv = eval_cmd(C_SIMPLIFY, 1, &v);
        if (ext_take(X, sv, sgn, &best, &bv) != 1) return NULL;
    }
    return best;
}

static E *cartprod(XP, uint32_t n, E **a) {
    uint64_t tot = 1;
    for (uint32_t i = 0; i < n; i++) {
        if (a[i]->t != E_LIST) return NULL;
        tot *= a[i]->n;
        if (tot > 20000) cas_throw(CAS_ERR_DOMAIN, "cartprod: too many elements");
    }
    E **out = salloc(sizeof(E *) * (size_t)(tot + 1));
    uint32_t *ix = salloc(sizeof(uint32_t) * n);
    for (uint32_t i = 0; i < n; i++) ix[i] = 0;
    for (uint64_t k = 0; k < tot; k++) {
        E **t = salloc(sizeof(E *) * n);
        for (uint32_t i = 0; i < n; i++) t[i] = EK(a[i])[ix[i]];
        out[k] = s_list(n, t);
        for (int i = (int)n - 1; i >= 0; i--) {
            if (++ix[i] < a[i]->n) break;
            ix[i] = 0;
        }
    }
    return s_list((uint32_t)tot, out);
}

static E *scmd(XP, int op, uint32_t n, E **a) {
    long nn, mm;
    switch (op) {
    case S_CHT: case S_CHU: case S_LEG: case S_HER: case S_LAG:
        if (num_is_small_int(a[0], &nn)) {
            if (nn < 0) cas_throw(CAS_ERR_DOMAIN, "degree must be >= 0");
            if (nn > 4000) cas_throw(CAS_ERR_DOMAIN, "degree too large (at most 4000)");
            return opoly(X, op, nn, a[1]);
        }
        return opoly_sym(X, op, a[0], a[1]);
    case S_LEGP: {
        if (!num_is_small_int(a[0], &nn) || !num_is_small_int(a[1], &mm)) return NULL;
        if (nn < 0 || mm < 0 || nn > 4000) cas_throw(CAS_ERR_DOMAIN, "legendrep: 0 <= m, 0 <= n <= 4000");
        if (mm > nn) return K->c0;
        int fi = 0;
        E *t = ix_fresh(X, a[2], &fi), *p = opoly(X, S_LEG, nn, t);
        for (long j = 0; j < mm; j++) p = diff(p, t);
        E *r = s_mul2(s_pow(s_sub(K->c1, s_mul2(t, t)), mk_frac(mm, 2)), p);
        if (mm & 1) r = s_neg(r);
        return subst(r, t, a[2]);
    }
    case S_DIG: return digamma(X, a[0]);
    case S_RES:
        if (a[1]->t != E_SYM) cas_throw(CAS_ERR_DOMAIN, "residue: expected a variable");
        return residue(X, a[0], a[1], a[2]);
    case S_MIN: case S_MAX:
        if (a[1]->t != E_SYM) cas_throw(CAS_ERR_DOMAIN, "expected a variable");
        return extremum(X, a[0], a[1], op == S_MIN ? 1 : -1);
    case S_CART: return cartprod(X, n, a);
    }
    return NULL;
}

int casx_spfn_register(XP) { return X->reg("casx", stab, (int)(sizeof stab / sizeof stab[0]), scmd); }
