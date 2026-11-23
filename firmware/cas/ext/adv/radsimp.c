/*
 * radsimp.c -- radical and algebraic simplification of the advanced CAS
 * extension (cas/src/casx.h), hooked into simplify (simplify.c simplify_full):
 *
 * rewrites applied first (CASX_HOOK_SIMPLIFY_PRE: each is an identity that
 * makes the expression simpler, so the result becomes the starting point):
 *  - n-th roots of a + b sqrt(d) (a, b rational, d a positive integer,
 *    n <= 12) that are c + e sqrt(d) with c, e rational: c and e come from
 *    the real roots r = (a + b sqrt d)^(1/n), r' = (a - b sqrt d)^(1/n)
 *    (c = (r + r')/2, e = (r - r')/(2 sqrt d)), rounded to halves and then
 *    checked exactly ((c + e sqrt d)^n expands to a + b sqrt d) [Wester C19-C21];
 *  - sqrt(z) sqrt(conj z) = |z| for a non-real constant z (the principal root
 *    of conj z is the conjugate of the root of z) [C18];
 *  - P^c with P = k Q^2, Q = a + b sqrt(v) (a > 0, b >= 0 rational, k > 0):
 *    P^c = (sqrt(k) Q)^(2c), valid for every complex v because the principal
 *    root has a nonnegative real part, so Re Q > 0 and ln(Q^2) = 2 ln Q
 *    [L7, L8];
 *  - sqrt((A + A cos t)) = sqrt(2A) |cos(t/2)| and sqrt(A - A cos t) =
 *    sqrt(2A) |sin(t/2)| for a real constant t, where the sign of the
 *    constant decides |.| [I2];
 * candidates ranked by simplify's cost (CASX_HOOK_SIMPLIFY_CAND):
 *  - exponentials e^(k_i u) with commensurate exponents become powers of
 *    T = e^u, the rational function in T is normalised (so common factors
 *    cancel) and T is put back [H15];
 *  - a numerator factor x - r (r algebraic) that is a root of the
 *    denominator is cancelled by exact division [H21].
 */
#include "casx_mod.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

#define rebuild X->rebuild
#define numden X->numden
#define factor_list_in X->factor_list_in
#define sign_of X->sign_of
#define eval_complex X->eval_complex

static int rs_compound(E *e) { return e->t == E_ADD || e->t == E_MUL || e->t == E_POW || e->t == E_FN || e->t == E_LIST; }

static int rs_rat(E *e) { return e->t == E_INT || e->t == E_RAT; }

/* sqrt(d) with d a positive integer: *d */
static int sqrt_int(XP, E *e, long *d) {
    return e->t == E_POW && expr_eq(K1(e), K->chalf) && K0(e)->t == E_INT && num_is_small_int(K0(e), d) && *d > 1;
}

/* B = a + b sqrt(d) */
static int quad_surd(XP, E *B, E **a, E **b, long *d) {
    if (B->t != E_ADD || B->n != 2 || !rs_rat(EK(B)[0])) return 0;
    E *t = EK(B)[1];
    *a = EK(B)[0];
    if (sqrt_int(X, t, d)) return *b = K->c1, 1;
    if (t->t == E_MUL && t->n == 2 && rs_rat(EK(t)[0]) && sqrt_int(X, EK(t)[1], d)) return *b = EK(t)[0], 1;
    return 0;
}

static double r_floor(double v) { return __builtin_floor(v); }

/* c + e sqrt(d) with (c + e sqrt d)^n = B, or NULL */
static E *root_surd(XP, E *B, long n) {
    E *a, *b;
    long d;
    double av, bv, s, p, q, r, rc;
    if (!quad_surd(X, B, &a, &b, &d) || !to_double(a, &av) || !to_double(b, &bv)) return NULL;
    s = X->sqrt((double)d);
    p = av + bv * s, q = av - bv * s;
    if (p <= 0 || (q < 0 && !(n & 1))) return NULL;
    r = X->pow(p, 1.0 / (double)n);
    rc = q < 0 ? -X->pow(-q, 1.0 / (double)n) : X->pow(q, 1.0 / (double)n);
    double c = (r + rc) / 2, e = (r - rc) / (2 * s);
    for (int den = 1; den <= 2; den++) {
        double cr = r_floor(c * den + 0.5), er = r_floor(e * den + 0.5);
        if (__builtin_fabs(cr - c * den) > 1e-6 || __builtin_fabs(er - e * den) > 1e-6 || __builtin_fabs(cr) > 1e9 ||
            __builtin_fabs(er) > 1e9)
            continue;
        E *Q = s_add2(mk_frac((int64_t)cr, den), s_mul2(mk_frac((int64_t)er, den), s_sqrt(mk_int(d))));
        if (is_zero(expand(s_sub(s_powi(Q, n), B)))) return Q;
    }
    return NULL;
}

/* P = k Q^2 with Q = a + b sqrt(v): (sqrt(k) Q), else NULL */
static E *square_root_of(XP, E *P) {
    if (P->t != E_ADD) return NULL;
    E *u = NULL;
    for (uint32_t i = 0; i < P->n && !u; i++) { /* the first sqrt(v) with a symbolic v */
        E *t = EK(P)[i];
        if (t->t == E_MUL)
            for (uint32_t j = 0; j < t->n && !u; j++)
                if (EK(t)[j]->t == E_POW && expr_eq(K1(EK(t)[j]), K->chalf) && !is_const_expr(K0(EK(t)[j]))) u = EK(t)[j];
        if (t->t == E_POW && expr_eq(K1(t), K->chalf) && !is_const_expr(K0(t))) u = t;
    }
    if (!u) return NULL;
    E *s = mk_sym_name("_rs"), *v = K0(u);
    E *Ps = expand(subst(subst(P, u, s), v, s_powi(s, 2)));
    long dg;
    if (!freeof(Ps, v) || !poly_degree_in(Ps, s, &dg) || dg != 2) return NULL;
    E *a2 = poly_coeff(Ps, s, 2), *a1 = poly_coeff(Ps, s, 1), *a0 = poly_coeff(Ps, s, 0);
    if (!rs_rat(a2) || !rs_rat(a1) || !rs_rat(a0) || sign_of(a2) <= 0 || sign_of(a0) <= 0) return NULL;
    /* k Q^2 with Q = s + m: a1 = 2 k m, a0 = k m^2 */
    E *m = s_div(a1, s_mul2(K->c2, a2));
    /* sqrt(k) (sqrt(v) + m) with m > 0: its real part is >= m > 0 */
    if (!is_zero(s_sub(a0, s_mul2(a2, s_powi(m, 2)))) || sign_of(m) <= 0) return NULL;
    return s_mul2(s_sqrt(a2), s_add2(u, m));
}

/* sqrt(A + B cos t), t a real constant, B = +-A: sqrt(2A) |cos(t/2)| / |sin(t/2)| */
static E *half_angle(XP, E *R) {
    if (!is_const_expr(R)) return NULL;
    E *Rx = expand(R);
    if (Rx->t != E_ADD || Rx->n != 2) return NULL;
    for (int j = 0; j < 2; j++) {
        E *A = EK(Rx)[j], *T = EK(Rx)[1 - j], *B = K->c1, *c = T;
        if (T->t == E_MUL && T->n == 2 && rs_rat(EK(T)[0])) B = EK(T)[0], c = EK(T)[1];
        if (!rs_rat(A) || !IFN(c, F_COS) || c->n != 1) continue;
        double re, im;
        if (!eval_complex(K0(c), &re, &im) || im != 0) return NULL;
        int fn = is_zero(s_add2(A, B)) ? F_SIN : is_zero(s_sub(A, B)) ? F_COS : 0;
        if (fn) return s_mul2(s_sqrt(s_mul2(K->c2, A)), s_fn1(F_ABS, s_fn1(fn, s_div(K0(c), K->c2))));
    }
    return NULL;
}

/* sqrt(z) sqrt(conj z) -> sqrt(z conj z) in a product */
static E *conj_pairs(XP, E *e) {
    E **f = salloc(sizeof(E *) * e->n);
    uint32_t n = e->n, changed = 0;
    for (uint32_t i = 0; i < n; i++) f[i] = EK(e)[i];
    for (uint32_t i = 0; i < n; i++) {
        E *p = f[i];
        if (!p || p->t != E_POW || !expr_eq(K1(p), K->chalf) || !is_const_expr(K0(p)) || freeof(K0(p), K->i_)) continue;
        E *cj = expand(subst(K0(p), K->i_, s_neg(K->i_)));
        for (uint32_t j = i + 1; j < n; j++) {
            E *q = f[j];
            if (!q || q->t != E_POW || !expr_eq(K1(q), K->chalf) || !is_zero(expand(s_sub(K0(q), cj)))) continue;
            double re, im;
            if (!eval_complex(K0(p), &re, &im) || im == 0) continue;
            f[i] = s_sqrt(expand(s_mul2(K0(p), K0(q))));
            f[j] = NULL;
            changed = 1;
            break;
        }
    }
    if (!changed) return e;
    uint32_t m = 0;
    for (uint32_t i = 0; i < n; i++)
        if (f[i]) f[m++] = f[i];
    return s_mul(m, f);
}

static E *pre(XP, E *e) {
    stack_check();
    if (!rs_compound(e)) return e;
    E **k = salloc(sizeof(E *) * (e->n + 1));
    int ch = 0;
    for (uint32_t i = 0; i < e->n; i++) ch |= (k[i] = pre(X, EK(e)[i])) != EK(e)[i];
    if (ch) e = rebuild(e, k);
    if (e->t == E_MUL) return conj_pairs(X, e);
    if (e->t != E_POW || K1(e)->t == E_INT) return e;
    E *B = K0(e), *q = K1(e), *r = square_root_of(X, B);
    if (r) return s_pow(r, s_mul2(K->c2, q));
    long num, den;
    if (q->t != E_RAT || !num_is_small_int(K0(q), &num) || !num_is_small_int(K1(q), &den) || den < 2 || den > 12 ||
        num < -40 || num > 40)
        return e;
    if ((r = root_surd(X, B, den))) return s_powi(r, num);
    if (den == 2 && (r = half_angle(X, B))) return s_powi(r, num);
    return e;
}

/* ---- candidates ---- */
static void exp_args(XP, E *e, E **as, uint32_t *n, uint32_t cap) {
    stack_check();
    if (!rs_compound(e)) return;
    if (e->t == E_POW && ISYM(K0(e), S_E)) {
        for (uint32_t i = 0; i < *n; i++)
            if (expr_eq(as[i], K1(e))) return;
        if (*n < cap) as[(*n)++] = K1(e);
        else *n = cap + 1;
        return;
    }
    for (uint32_t i = 0; i < e->n; i++) exp_args(X, EK(e)[i], as, n, cap);
}

static E *exp_to_t(XP, E *e, E *base, E *T) {
    stack_check();
    if (!rs_compound(e)) return e;
    if (e->t == E_POW && ISYM(K0(e), S_E)) {
        E *q = s_div(K1(e), base);
        long k;
        if (num_is_small_int(q, &k)) return s_powi(T, k);
    }
    E **k = salloc(sizeof(E *) * (e->n + 1));
    for (uint32_t i = 0; i < e->n; i++) k[i] = exp_to_t(X, EK(e)[i], base, T);
    return rebuild(e, k);
}

static E *exp_cand(XP, E *e) {
    E *as[8];
    uint32_t n = 0;
    exp_args(X, e, as, &n, 8);
    if (n < 2 || n > 8) return NULL;
    long L = 1;
    for (uint32_t i = 1; i < n; i++) {
        E *q = s_div(as[i], as[0]);
        long a, b;
        if (q->t == E_INT) continue;
        if (q->t != E_RAT || !num_is_small_int(K0(q), &a) || !num_is_small_int(K1(q), &b) || b > 12) return NULL;
        long g = L, h = b;
        while (h) { long t = g % h; g = h; h = t; }
        L = L / g * b;
        if (L > 24) return NULL;
    }
    E *base = s_div(as[0], mk_int(L)), *T = mk_sym_name("_rT");
    E *r = rat_normal(exp_to_t(X, e, base, T));
    if (!freeof(r, K->e_) && !freeof(r, T)) {
        /* exponentials left that are not powers of T: no gain */
        E *as2[8];
        uint32_t n2 = 0;
        exp_args(X, r, as2, &n2, 8);
        if (n2) return NULL;
    }
    return subst(r, T, s_exp(base));
}

static E *alg_cancel(XP, E *e) {
    E *N, *D;
    numden(e, &N, &D);
    E *x = default_var(e);
    long dd, dn;
    if (!x || freeof(D, x) || freeof(N, x) || !poly_degree_in(D, x, &dd) || dd < 2 || dd > 8 ||
        !poly_degree_in(N, x, &dn) || dn > 24 || is_const_expr(e))
        return NULL;
    /* a common root r of N and D that is irrational (rational ones cancel already) */
    E *rs = X->solve_expr(D, x, 1);
    for (uint32_t i = 0; i < rs->n; i++) {
        E *r = EK(rs)[i];
        if (!is_const_expr(r) || has_float(r) || rs_rat(r) || !is_zero(expand(subst(N, x, r)))) continue;
        E *f = s_sub(x, r), *qn[3] = {N, f, x}, *qd[3] = {D, f, x};
        E *Qn = eval_cmd(C_QUO, 3, qn), *Qd = eval_cmd(C_QUO, 3, qd);
        if (!is_zero(expand(s_sub(s_mul2(Qn, f), N))) || !is_zero(expand(s_sub(s_mul2(Qd, f), D)))) continue;
        return s_div(Qn, Qd);
    }
    return NULL;
}

static E *simp_pre(XP, E *e) {
    E *r = pre(X, e);
    return r == e ? NULL : r;
}

static E *simp_cand(XP, E *e) {
    E *r = exp_cand(X, e);
    if (!r) r = alg_cancel(X, e);
    return r;
}

int casx_rad_register(XP) {
    return X->reg_hook(CASX_HOOK_SIMPLIFY_PRE, simp_pre) | X->reg_hook(CASX_HOOK_SIMPLIFY_CAND, simp_cand);
}
