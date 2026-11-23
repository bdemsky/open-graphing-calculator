/*
 * sums.c -- sums, products and limits the core does not find (algorithm slots CASX_FN_SUM,
 * CASX_FN_PRODUCT and CASX_FN_LIMIT, casx.h; tried after the core's own methods).
 *
 * Sums sum(f, k, a, b):
 *   - lower bound a = 1..5: the sum from 0 minus the first terms (when the sum from 0 is
 *     known: sum k^2 binomial(n, k) from 1);
 *   - binomial(n, k) R(k) over k = 0..n with R rational: R = Q(k) + c/(k+1) by partial
 *     fractions, the Q part by the core (Euler operator on (1+x)^n) and
 *     sum binomial(n, k)/(k+1) = (2^(n+1) - 1)/(n+1) (absorption: binomial(n+1, k+1)/(n+1));
 *   - trigonometric terms in k: through exponentials (trig2exp), the geometric sums of the
 *     core, and back (exp2trig, simplify).
 * Products product(f, k, a, b) of a rational f whose factors over Q are linear:
 *   prod_{k=a}^{b} (p k + q) = p^(b-a+1) Gamma(b + 1 + q/p) / Gamma(a + q/p).
 * Every closed form for a finite range is checked against the terms added (multiplied) up for
 * two values of the upper bound, with numbers for the other symbols; a mismatch leaves the
 * call unevaluated.
 * Limits at +inf of expressions with gamma(u) or u! (u -> inf): Stirling's series
 *   gamma(u) = sqrt(2 pi) u^(u - 1/2) e^(-u) exp(1/(12 u) - 1/(360 u^3) + ...)
 * truncated after one and after two terms; the limit is accepted only when both agree.
 */
#include "casx_mod.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

static int has_fn(XP, E *e, int id) {
    stack_check();
    if (e->t == E_FN && e->s == id) return 1;
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN && e->t != E_LIST) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_fn(X, EK(e)[i], id)) return 1;
    return 0;
}

static int unev(XP, E *e) {
    return !e || has_fn(X, e, C_SUM) || has_fn(X, e, C_PRODUCT) || has_fn(X, e, C_LIMIT) || has_fn(X, e, C_INTEGRATE) ||
           !freeof(e, mk_sym(S_UNDEF));
}

static E *ssimp(XP, E *e) { return eval_cmd(C_SIMPLIFY, 1, &e); }

static E *cmd4(XP, int id, E *a, E *b, E *c, E *d) {
    E *v[4] = {a, b, c, d};
    E *r = try_cmd(id, 4, v);
    return unev(X, r) ? NULL : r;
}

static void syms(XP, E *e, E **out, uint32_t *n) {
    stack_check();
    if (e->t == E_SYM) {
        if (is_const_expr(e)) return;
        for (uint32_t i = 0; i < *n; i++)
            if (expr_eq(out[i], e)) return;
        if (*n < 8) out[(*n)++] = e;
        return;
    }
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN && e->t != E_LIST) return;
    for (uint32_t i = 0; i < e->n; i++) syms(X, EK(e)[i], out, n);
}

/* S agrees with the explicit sum / product for the upper bound's symbols at 3 and 5 */
static int check(XP, int prod, E *f, E *k, E *a, E *b, E *S) {
    E *bs[8], *os[8], *val[16], *var[16];
    uint32_t nb = 0, no = 0;
    syms(X, b, bs, &nb);
    if (!nb) return 1;
    syms(X, f, os, &no);
    syms(X, S, os, &no);
    for (int m = 3; m <= 5; m += 2) {
        uint32_t nv = 0;
        for (uint32_t i = 0; i < nb; i++) var[nv] = bs[i], val[nv++] = mk_int(m);
        for (uint32_t i = 0; i < no; i++) {
            int inb = expr_eq(os[i], k);
            for (uint32_t j = 0; j < nb; j++) inb |= expr_eq(os[i], bs[j]);
            if (!inb) var[nv] = os[i], val[nv++] = mk_frac(7 + 2 * (long)i, 11);
        }
        E *bb = subst_many(b, nv, var, val), *ab = subst_many(a, nv, var, val);
        long lo, hi;
        if (!num_is_small_int(ab, &lo) || !num_is_small_int(bb, &hi) || hi < lo || hi - lo > 200) return 0;
        E *fe = subst_many(f, nv, var, val), *d = cmd4(X, prod ? C_PRODUCT : C_SUM, fe, k, ab, bb);
        double r1, i1, r2, i2;
        if (!d) return 0;
        if (!X->eval_complex(evalf_digits(d, 20), &r1, &i1)) { /* unknown functions: exactly */
            if (!is_zero(rat_normal(expand(s_sub(d, subst_many(S, nv, var, val)))))) return 0;
            continue;
        }
        if (!X->eval_complex(evalf_digits(subst_many(S, nv, var, val), 20), &r2, &i2)) return 0;
        double sc = 1 + __builtin_fabs(r1) + __builtin_fabs(i1);
        if (__builtin_fabs(r1 - r2) + __builtin_fabs(i1 - i2) > 1e-9 * sc) return 0;
    }
    return 1;
}

/* the binomial(N, k) factor of f */
static E *binom_of(XP, E *f, E *k) {
    E **fs = f->t == E_MUL ? EK(f) : &f;
    uint32_t m = f->t == E_MUL ? f->n : 1;
    for (uint32_t i = 0; i < m; i++)
        if (IFN(fs[i], F_BINOM) && fs[i]->n == 2 && expr_eq(K1(fs[i]), k) && freeof(K0(fs[i]), k)) return fs[i];
    return NULL;
}

static E *sum_binom(XP, E *f, E *k, E *b) {
    E *bn = binom_of(X, f, k);
    if (!bn || !expr_eq(K0(bn), b)) return NULL;
    E *h = rat_normal(s_div(f, bn)), *n = b;
    E *pa[2] = {h, k};
    E *pf = try_cmd(C_PARTFRAC, 2, pa);
    if (!pf) return NULL;
    E **ts = pf->t == E_ADD ? EK(pf) : &pf, *poly = K->c0, *c = K->c0;
    uint32_t nt = pf->t == E_ADD ? pf->n : 1;
    for (uint32_t i = 0; i < nt; i++) {
        long d;
        E *num, *den;
        X->numden(ts[i], &num, &den);
        if (freeof(den, k)) poly = s_add2(poly, ts[i]);
        else if (poly_degree_in(den, k, &d) && d == 1 && freeof(num, k) &&
                 is_zero(rat_normal(s_sub(s_div(den, poly_coeff(den, k, 1)), s_add2(k, K->c1)))))
            c = s_add2(c, s_div(num, poly_coeff(den, k, 1)));
        else
            return NULL;
    }
    if (is_zero(c)) return NULL; /* nothing the core could not do */
    E *r = s_div(s_sub(s_pow(K->c2, s_add2(n, K->c1)), K->c1), s_add2(n, K->c1));
    r = s_mul2(c, r);
    if (!is_zero(poly)) {
        E *S = cmd4(X, C_SUM, s_mul2(bn, poly), k, K->c0, n);
        if (!S) return NULL;
        r = s_add2(r, S);
    }
    return r;
}

static int has_trig_in(XP, E *e, E *k) {
    stack_check();
    if ((IFN(e, F_SIN) || IFN(e, F_COS)) && !freeof(e, k)) return 1;
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_trig_in(X, EK(e)[i], k)) return 1;
    return 0;
}

/* sum_{k=a}^{b} c sin(al k + be) = c sin(al N/2) sin(al (a+b)/2 + be) / sin(al/2), N = b - a + 1,
 * and the same with cos(al (a+b)/2 + be) for cos; sums termwise; NULL if a term is not of
 * this form */
static E *sum_trig(XP, E *f, E *k, E *a, E *b) {
    f = expand(f);
    if (f->t == E_ADD) {
        E **t = salloc(sizeof(E *) * f->n);
        for (uint32_t i = 0; i < f->n; i++) {
            t[i] = freeof(EK(f)[i], k) ? s_mul2(EK(f)[i], s_add2(s_sub(b, a), K->c1)) : sum_trig(X, EK(f)[i], k, a, b);
            if (!t[i]) return NULL;
        }
        return s_add(f->n, t);
    }
    E **fs = f->t == E_MUL ? EK(f) : &f, *c = K->c1, *tr = NULL;
    uint32_t m = f->t == E_MUL ? f->n : 1;
    for (uint32_t i = 0; i < m; i++) {
        if (freeof(fs[i], k)) c = s_mul2(c, fs[i]);
        else if (!tr && (IFN(fs[i], F_SIN) || IFN(fs[i], F_COS))) tr = fs[i];
        else return NULL;
    }
    if (!tr) return NULL;
    E *al = rat_normal(diff(K0(tr), k));
    if (!freeof(al, k) || is_zero(al)) return NULL;
    E *be = expand(s_sub(K0(tr), s_mul2(al, k))), *N = s_add2(s_sub(b, a), K->c1);
    if (!freeof(be, k)) return NULL;
    E *mid = s_add2(s_mul2(s_div(al, K->c2), s_add2(a, b)), be);
    E *r = s_div(s_mul2(s_fn1(F_SIN, s_mul2(s_div(al, K->c2), N)), s_fn1(tr->s, mid)), s_fn1(F_SIN, s_div(al, K->c2)));
    return s_mul2(c, r);
}

/* telescoping: f = h(k+1) - h(k) for h = -(a term of f, or the sum of the terms with one sign)
 * gives h(b+1) - h(a) */
static E *sum_tele(XP, E *f, E *k, E *a, E *b) {
    if (f->t != E_ADD || f->n > 8) return NULL;
    for (uint32_t i = 0; i < f->n; i++) {
        E *h = s_neg(EK(f)[i]);
        E *d = s_sub(f, s_sub(subst(h, k, s_add2(k, K->c1)), h));
        if (is_zero(d) || is_zero(rat_normal(expand(d)))) return s_sub(subst(h, k, s_add2(b, K->c1)), subst(h, k, a));
    }
    return NULL;
}

E *hx_sum(XP, E *t, E *k, E *lo, E *hi); /* hyper.c */

static E *sum_slot(XP, uint32_t na, E **A) {
    E *f = A[0], *k = A[1], *a = A[2], *b = A[3], *r = NULL;
    long lo;
    if (ISYM(b, S_INF)) return NULL;
    if (num_is_small_int(a, &lo) && lo >= 1 && lo <= 5) { /* from 0, minus the first terms */
        E *S0 = cmd4(X, C_SUM, f, k, K->c0, b);
        if (S0) {
            r = S0;
            for (long j = 0; j < lo && r; j++) {
                E *sv[3] = {f, k, mk_int(j)};
                E *t = try_cmd(C_SUBST, 3, sv);
                r = t && !unev(X, t) ? s_sub(r, t) : NULL;
            }
        }
    }
    if (!r && num_is_small_int(a, &lo) && lo == 0) r = sum_binom(X, f, k, b);
    if (!r && has_trig_in(X, f, k)) r = sum_trig(X, f, k, a, b);
    if (!r && !ISYM(b, S_INF)) r = sum_tele(X, f, k, a, b);
    if (!r && !ISYM(b, S_INF)) r = hx_sum(X, f, k, a, b); /* hyper.c: Gosper, Zeilberger */
    if (!r || unev(X, r)) return NULL;
    r = ssimp(X, r);
    return check(X, 0, f, k, a, b, r) ? r : NULL;
}

static E *prod_slot(XP, uint32_t na, E **A) {
    E *f = rat_normal(A[0]), *k = A[1], *a = A[2], *b = A[3];
    if (ISYM(b, S_INF) || ISYM(a, S_INF)) return NULL;
    E *num, *den, *len = s_add2(s_sub(b, a), K->c1), *r = K->c1;
    X->numden(f, &num, &den);
    long d;
    if (!poly_degree_in(num, k, &d) || !poly_degree_in(den, k, &d)) return NULL;
    for (int s = 0; s < 2; s++) {
        E *content, *fl = X->factor_list_in(s ? den : num, k, &content);
        E *part = s_pow(content, len);
        for (uint32_t j = 0; j < fl->n; j++) {
            E *g = K0(EK(fl)[j]), *m = K1(EK(fl)[j]);
            if (freeof(g, k)) {
                part = s_mul2(part, s_pow(g, s_mul2(m, len)));
                continue;
            }
            if (!poly_degree_in(g, k, &d) || d != 1) return NULL;
            E *p = poly_coeff(g, k, 1), *q = s_div(poly_coeff(g, k, 0), p), *st = s_add2(a, q);
            double sv;
            if (!to_double(evalf_digits(st, 15), &sv) || !(sv > 0)) return NULL; /* a zero or pole in the range */
            E *g2 = s_div(s_fn1(F_GAMMA, s_add2(s_add2(b, K->c1), q)), s_fn1(F_GAMMA, st));
            part = s_mul2(part, s_pow(s_mul2(s_pow(p, len), g2), m));
        }
        r = s ? s_div(r, part) : s_mul2(r, part);
    }
    r = ssimp(X, r);
    if (unev(X, r)) return NULL;
    return check(X, 1, A[0], k, a, b, r) ? r : NULL;
}

/* ------------------------------------------------------------ limits --- */
/* e with gamma(u) and u! (u depending on x) replaced by Stirling's series with `terms` terms */
static E *stirling(XP, E *e, E *x, int terms, int *found) {
    stack_check();
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN && e->t != E_LIST) return e;
    E **k = salloc(sizeof(E *) * e->n);
    for (uint32_t i = 0; i < e->n; i++) k[i] = stirling(X, EK(e)[i], x, terms, found);
    e = X->rebuild(e, k);
    if ((IFN(e, F_GAMMA) || IFN(e, F_FACT)) && e->n == 1 && !freeof(K0(e), x)) {
        E *u = IFN(e, F_FACT) ? s_add2(K0(e), K->c1) : K0(e);
        E *c = s_div(K->c1, s_mul2(mk_int(12), u));
        if (terms > 1) c = s_sub(c, s_div(K->c1, s_mul2(mk_int(360), s_powi(u, 3))));
        *found = 1;
        return s_mul(4, (E *[]){s_sqrt(s_mul2(K->c2, K->pi_)), s_pow(u, s_sub(u, K->chalf)), s_exp(s_neg(u)), s_exp(c)});
    }
    return e;
}

static E *lim(XP, E *e, E *x, E *pt) {
    E *v[3] = {e, x, pt};
    E *l = try_cmd(C_LIMIT, 3, v);
    return l && !unev(X, l) ? l : NULL;
}

/* ln e for a product of positive factors, with ln gamma(u) by Stirling's series (`terms`
 * terms); NULL if a factor is not known to be positive for large x */
static E *ln_of(XP, E *e, E *x, int terms, int *found) {
    stack_check();
    if (freeof(e, x)) {
        double v;
        if (!to_double(evalf_digits(e, 15), &v) || !(v > 0)) return NULL;
        return s_fn1(F_LN, e);
    }
    if (e->t == E_MUL) {
        E **t = salloc(sizeof(E *) * e->n);
        for (uint32_t i = 0; i < e->n; i++)
            if (!(t[i] = ln_of(X, EK(e)[i], x, terms, found))) return NULL;
        return s_add(e->n, t);
    }
    if (e->t == E_POW) {
        if (ISYM(K0(e), S_E)) return K1(e);
        E *l = ln_of(X, K0(e), x, terms, found);
        return l ? s_mul2(K1(e), l) : NULL;
    }
    if ((IFN(e, F_GAMMA) || IFN(e, F_FACT)) && e->n == 1) {
        E *u = IFN(e, F_FACT) ? s_add2(K0(e), K->c1) : K0(e), *pt = K->inf_;
        E *lu = lim(X, u, x, pt);
        if (!lu || !ISYM(lu, S_INF)) return NULL;
        *found = 1;
        E *c = s_div(K->c1, s_mul2(mk_int(12), u));
        if (terms > 1) c = s_sub(c, s_div(K->c1, s_mul2(mk_int(360), s_powi(u, 3))));
        return s_add(4, (E *[]){s_mul2(s_sub(u, K->chalf), s_fn1(F_LN, u)), s_neg(u),
                                 s_mul2(K->chalf, s_fn1(F_LN, s_mul2(K->c2, K->pi_))), c});
    }
    E *l = lim(X, e, x, K->inf_);
    double v;
    if (!l || !(ISYM(l, S_INF) || (to_double(evalf_digits(l, 15), &v) && v > 0))) return NULL;
    return s_fn1(F_LN, e);
}

/* a numerator with a difference of exponentials, e^u (e^(v-u) - ...)/D with e^u taken out of
 * every term (Wester T5: the cancellation e^v - e^u is then explicit) */
static E *exp_out(XP, E *e, E *x, E *pt) {
    E *num, *den;
    if (e->t == E_MUL) { /* already e^u * (sum) / D (this rewrite, one level down): stop */
        int ex = 0, sm = 0;
        for (uint32_t i = 0; i < e->n; i++) {
            ex |= EK(e)[i]->t == E_POW && ISYM(K0(EK(e)[i]), S_E);
            sm |= EK(e)[i]->t == E_ADD;
        }
        if (ex && sm) return NULL;
    }
    X->numden(e, &num, &den);
    num = expand(num);
    if (num->t != E_ADD || num->n > 6) return NULL;
    for (uint32_t i = 0; i < num->n; i++) {
        E *t = EK(num)[i], **fs = t->t == E_MUL ? EK(t) : &t;
        uint32_t m = t->t == E_MUL ? t->n : 1;
        for (uint32_t j = 0; j < m; j++) {
            if (!(fs[j]->t == E_POW && ISYM(K0(fs[j]), S_E) && !freeof(K1(fs[j]), x))) continue;
            E *u = K1(fs[j]), **q = salloc(sizeof(E *) * num->n);
            for (uint32_t r = 0; r < num->n; r++) q[r] = rat_normal(s_mul2(EK(num)[r], s_exp(s_neg(u))));
            E *g = s_div(s_mul2(s_exp(u), s_add(num->n, q)), den), *l = lim(X, g, x, pt);
            if (l) return l;
        }
    }
    return NULL;
}

static E *lim_slot(XP, uint32_t na, E **A) {
    E *e = A[0], *x = A[1], *pt = A[2];
    if (!ISYM(pt, S_INF) || x->t != E_SYM) return NULL;
    int found = 0;
    E *e1 = stirling(X, e, x, 1, &found), *e2 = stirling(X, e, x, 2, &found);
    if (!found) return exp_out(X, e, x, pt);
    E *l1 = lim(X, e1, x, pt), *l2 = l1 ? lim(X, e2, x, pt) : NULL;
    if (l1 && l2 && is_zero(ssimp(X, s_sub(l1, l2)))) return l1;
    /* through the logarithm: e = exp(ln e) */
    int f2 = 0;
    E *g1 = ln_of(X, e, x, 1, &f2), *g2 = g1 ? ln_of(X, e, x, 2, &f2) : NULL;
    if (!g1 || !g2 || !f2) return NULL;
    l1 = lim(X, expand(g1), x, pt), l2 = l1 ? lim(X, expand(g2), x, pt) : NULL;
    if (!l1 || !l2) return NULL;
    if (ISYM(l1, S_INF) && ISYM(l2, S_INF)) return K->inf_;
    if (!is_zero(ssimp(X, s_sub(l1, l2)))) return NULL;
    E *nl = s_neg(K->inf_);
    if (expr_eq(l1, nl)) return K->c0;
    return ssimp(X, s_exp(l1));
}

int casx_sums_register(XP) {
    return X->reg_fn(CASX_FN_SUM, (void *)sum_slot) | X->reg_fn(CASX_FN_PRODUCT, (void *)prod_slot) |
           X->reg_fn(CASX_FN_LIMIT, (void *)lim_slot);
}
