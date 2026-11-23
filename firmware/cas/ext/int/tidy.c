/*
 * tidy.c -- cleanup of the casx integrator's antiderivatives (after their check):
 *   - additive terms free of x are dropped (an antiderivative is defined up to a constant);
 *   - atan(tan(u)) -> u (the same derivative, and continuous where the other jumps);
 *   - arguments of functions and bases of fractional or negative powers become the smallest of
 *     the expression, its expansion, its rational normal form and, for polynomials in sin u and
 *     cos u, the forms with sin^2 = 1 - cos^2 or cos^2 = 1 - sin^2 (so the back substitution's
 *     A^2 + B^2 (1 - cos(x)^2) becomes A^2 + B^2 sin(x)^2, c - a d/b + d (b x^4 + a)/b becomes
 *     c + d x^4);
 *   - the whole expression: itself or its expansion, whichever is smaller.
 * Sizes are node counts.  The result is checked by differentiation once more; on a mismatch
 * (or when nothing got smaller) the antiderivative comes back unchanged.
 */
#include "intx.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

static int is_node(E *e) { return e->t == E_ADD || e->t == E_MUL || e->t == E_POW || e->t == E_FN || e->t == E_LIST; }

/* expand / normal that give NULL instead of throwing (out of memory, a division by zero) */
static E *tcmd(XP, int id, E *e) {
    char *m = K->s_top;
    E *r = try_cmd(id, 1, &e);
    if (!r) K->s_top = m; /* out of memory: the scratch it used is free again */
    return r;
}
static E *texp(XP, E *e) { return tcmd(X, C_EXPAND, e); }
static E *tnorm(XP, E *e) { return tcmd(X, C_NORMAL, e); }

static long sz(XP, E *e, long cap) {
    if (!is_node(e)) return 1;
    long n = 1;
    for (uint32_t i = 0; i < e->n && n <= cap; i++) n += sz(X, EK(e)[i], cap - n);
    return n;
}

/* sin(u)^(2k) -> (1 - cos(u)^2)^k (to_cos), or cos(u)^(2k) -> (1 - sin(u)^2)^k; *ch set */
static E *trig_sq(XP, E *e, int to_cos, int *ch) {
    if (!is_node(e)) return e;
    stack_check();
    E **k = salloc(sizeof(E *) * e->n);
    int c0 = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        k[i] = trig_sq(X, EK(e)[i], to_cos, ch);
        c0 |= k[i] != EK(e)[i];
    }
    if (c0) e = X->rebuild(e, k);
    long n;
    if (e->t == E_POW && IFN(K0(e), to_cos ? F_SIN : F_COS) && num_is_small_int(K1(e), &n) && n >= 2 && !(n & 1)) {
        *ch = 1;
        E *o = s_fn1(to_cos ? F_COS : F_SIN, K0(K0(e)));
        return s_powi(s_sub(K->c1, s_mul2(o, o)), n / 2);
    }
    return e;
}

static int has_sc(XP, E *e) {
    if (IFN(e, F_SIN) || IFN(e, F_COS)) return 1;
    if (!is_node(e)) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_sc(X, EK(e)[i])) return 1;
    return 0;
}

static E *best_arg(XP, E *a, E *x) {
    if (!is_node(a) || freeof(a, x) || (a->t != E_ADD && a->t != E_MUL)) return a;
    long s0 = sz(X, a, 400);
    if (s0 > 300) return a;
    E *best = a, *c[4];
    long bs = s0;
    int nc = 0, ch = 0;
    c[nc++] = texp(X, a);
    c[nc++] = tnorm(X, a);
    if (has_sc(X, a)) {
        E *t = trig_sq(X, a, 1, &ch);
        if (ch) c[nc++] = texp(X, t);
        ch = 0;
        t = trig_sq(X, a, 0, &ch);
        if (ch) c[nc++] = texp(X, t);
    }
    for (int i = 0; i < nc; i++) {
        if (!c[i]) continue;
        long s = sz(X, c[i], bs);
        if (s < bs) best = c[i], bs = s;
    }
    return best;
}

static E *targ(XP, E *e, E *x) {
    if (!is_node(e) || freeof(e, x)) return e;
    stack_check();
    poll_intr();
    E **k = salloc(sizeof(E *) * e->n);
    int ch = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        k[i] = targ(X, EK(e)[i], x);
        if (e->t == E_FN || (e->t == E_POW && i == 0)) {
            long n;
            if (e->t == E_FN || !num_is_small_int(K1(e), &n) || n < 0) k[i] = best_arg(X, k[i], x);
        }
        ch |= k[i] != EK(e)[i];
    }
    if (ch) e = X->rebuild(e, k);
    if (IFN(e, F_ATAN) && IFN(K0(e), F_TAN)) return K0(K0(e)); /* atan(tan u) */
    if (e->t == E_MUL) { /* c (t1 + t2 + ...) R: c distributed into the sum when that is smaller */
        E *c = K->c1, *rest = K->c1, *sum = NULL;
        for (uint32_t i = 0; i < e->n; i++) {
            E *g = EK(e)[i];
            if (freeof(g, x)) c = s_mul2(c, g);
            else if (g->t == E_ADD && !sum) sum = g;
            else rest = s_mul2(rest, g);
        }
        if (sum && !expr_eq(c, K->c1) && !expr_eq(c, K->cm1) && sum->n <= 40) {
            E **ts = salloc(sizeof(E *) * sum->n);
            for (uint32_t i = 0; i < sum->n; i++) ts[i] = s_mul2(c, EK(sum)[i]);
            E *d = s_mul2(s_add(sum->n, ts), rest);
            if (sz(X, d, 100000) < sz(X, e, 100000)) return d;
        }
    }
    return e;
}

/* tan(u/2) -> sin(u)/(1 + cos(u)) outside atan(...) (Weierstrass results); *ch set */
static E *half_tan(XP, E *e, int *ch) {
    if (!is_node(e) || IFN(e, F_ATAN)) return e;
    stack_check();
    E **k = salloc(sizeof(E *) * e->n);
    int c0 = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        k[i] = half_tan(X, EK(e)[i], ch);
        c0 |= k[i] != EK(e)[i];
    }
    if (c0) e = X->rebuild(e, k);
    if (IFN(e, F_TAN)) {
        E *u = s_mul2(K->c2, K0(e));
        *ch = 1;
        return s_div(s_fn1(F_SIN, u), s_add2(K->c1, s_fn1(F_COS, u)));
    }
    return e;
}

static int has_trig(XP, E *e) {
    if (IFN(e, F_SIN) || IFN(e, F_COS) || IFN(e, F_TAN) || IFN(e, F_COT) || IFN(e, F_SEC) || IFN(e, F_CSC))
        return 1;
    if (!is_node(e)) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_trig(X, EK(e)[i])) return 1;
    return 0;
}

/* trigsimp of e with tan(u/2) written with sin u, cos u (protected: a[0] = e) */
static E *cand_half(XP, void **a) {
    int ch = 0;
    E *h = half_tan(X, a[0], &ch);
    return ch ? tcmd(X, C_TRIGSIMP, h) : NULL;
}

/* x outside the arguments of sin and cos */
static int x_out(XP, E *e, E *x) {
    if (IFN(e, F_SIN) || IFN(e, F_COS)) return 0;
    if (expr_eq(e, x)) return 1;
    if (!is_node(e)) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (x_out(X, EK(e)[i], x)) return 1;
    return 0;
}

static E *no_const(XP, E *t, E *x) {
    if (t->t != E_ADD) return t;
    E *s = K->c0;
    for (uint32_t i = 0; i < t->n; i++)
        if (!freeof(EK(t)[i], x)) s = s_add2(s, EK(t)[i]);
    return is_zero(s) ? t : s;
}

/* a sum of sines and cosines of multiples of u as a polynomial in cos u or in sin u
 * (cos(3x)/12 - 3 cos(x)/4 -> cos(x)^3/3 - cos(x)), the smaller; NULL if none */
static E *cand_pow(XP, void **a) {
    E *t = a[0], *x = a[1], *best = NULL, *u = tcmd(X, C_TEXPAND, t);
    if (!u || expr_eq(u, t)) return NULL;
    for (int to_cos = 0; to_cos < 2; to_cos++) {
        int ch = 0;
        E *v = texp(X, trig_sq(X, u, to_cos, &ch));
        if (v && (v = no_const(X, v, x)) && (!best || sz(X, v, 1000) < sz(X, best, 1000))) best = v;
    }
    return best;
}

/* fractions and multiple angles (sin(3 x)) in e: at the same size, the form with fewer is
 * preferred (cos(x)^3/3 - cos(x), sin(x)^4/4 - sin(x)^6/6) */
static long nfrac(XP, E *e) {
    if (e->t == E_RAT) return 1;
    if (!is_node(e)) return 0;
    long n = (IFN(e, F_SIN) || IFN(e, F_COS)) && K0(e)->t == E_MUL && EK(K0(e))[0]->t == E_INT;
    for (uint32_t i = 0; i < e->n; i++) n += nfrac(X, EK(e)[i]);
    return n;
}

static E *cand_trig(XP, void **a) { return ix_tidy_trig(X, a[0], a[1], a[2], a[3]); }
static E *cand_lncomb(XP, void **a) { return ix_tidy_lncomb(X, a[0], a[1], a[2]); }

static E *tidy0(XP, void **a) {
    E *f = a[0], *r = a[1], *x = a[2];
    long s0 = (long)(intptr_t)a[3];
    int pref = 0; /* t (real) is preferred to r also when it is larger */
    E *t = no_const(X, targ(X, r, x), x); /* constant terms */
    long s1 = sz(X, t, s0 + 1);
    if (s1 < 1500) {
        E *c[3];
        int nc = 0;
        c[nc++] = texp(X, t);
        if (s1 < 400 && has_trig(X, t)) {
            void *ha[1] = {t};
            char *m = K->s_top;
            if (!(c[nc++] = X->protect(cand_half, ha))) K->s_top = m;
            if (s1 < 300) c[nc++] = tcmd(X, C_TRIGSIMP, t);
        }
        E *b = t;
        for (int i = 0; i < nc; i++) {
            long si;
            if (c[i] && (si = sz(X, c[i], s1)) < s1) b = c[i], s1 = si;
        }
        t = b;
        if (s1 < 1000 && has_trig(X, t)) { /* powers of sin, cos, tan */
            int hp = 0;
            void *ta[4] = {t, x, f, &hp};
            char *m = K->s_top;
            if ((b = X->protect(cand_trig, ta)) && b != t && (hp || sz(X, b, s1) < s1))
                t = b, s1 = sz(X, b, 4 * s1), pref |= hp;
            else
                K->s_top = m;
        }
    }
    t = no_const(X, ix_tidy_rad(X, f, t, x, &pref), x);
    s1 = sz(X, t, 100000);
    if (t->t == E_ADD && s1 < 200 && has_sc(X, t) && !x_out(X, t, x)) { /* powers, also at the same size */
        void *pa[2] = {t, x};
        char *m = K->s_top;
        E *p = X->protect(cand_pow, pa);
        if (p && sz(X, p, s1) <= s1) t = p, s1 = sz(X, p, s1);
        else K->s_top = m;
    }
    t = no_const(X, ix_tidy_fin(X, t, x), x);
    {
        void *la[3] = {t, x, f};
        char *m = K->s_top;
        E *u = X->protect(cand_lncomb, la);
        if (u && u != t) t = no_const(X, ix_tidy_fin(X, u, x), x);
        else K->s_top = m;
    }
    if (t == r || sz(X, t, s0 + 1) > s0 + (pref ? 2 * s0 : 0) ||
        (!pref && sz(X, t, s0) == s0 && nfrac(X, t) >= nfrac(X, r)) || !ix_verify_safe(X, f, t, x))
        return NULL;
    return t;
}

E *ix_tidy(XP, E *f, E *r, E *x) {
    const char *xn = sym_name(x->s);
    if (X->size < sizeof(casx_api_t)) return r; /* an older core: no X->protect */
    if (xn[0] == '_' && xn[1] == 'x' && xn[2] == 't') return r; /* an inner integral of a substitution */
    long s0 = sz(X, r, 5000);
    if (s0 > 4000) return r;
    char *m = K->s_top;
    void *a[4] = {f, r, x, (void *)(intptr_t)s0};
    E *t = X->protect(tidy0, a);
    if (!t) {
        K->s_top = m;
        return r;
    }
    return t;
}

/* ---- simplify: c b^u -> b^(u + k) when the number c is b^k (2*2^n -> 2^(n+1)) ---- */
static E *pmerge(XP, E *e, int *ch) {
    if (!is_node(e)) return e;
    stack_check();
    E **k = salloc(sizeof(E *) * e->n);
    int c0 = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        k[i] = pmerge(X, EK(e)[i], ch);
        c0 |= k[i] != EK(e)[i];
    }
    if (c0) e = X->rebuild(e, k);
    if (e->t != E_MUL || e->n < 2 || (EK(e)[0]->t != E_INT && EK(e)[0]->t != E_RAT)) return e;
    E *c = EK(e)[0];
    for (uint32_t i = 1; i < e->n; i++) {
        E *p = EK(e)[i];
        long b;
        if (p->t != E_POW || !num_is_small_int(K0(p), &b) || b < 2 || is_num(K1(p))) continue;
        /* c = b^kk (or 1/b^kk) for an integer kk >= 1 */
        E *cc = c->t == E_RAT && expr_eq(K0(c), K->c1) ? K1(c) : c;
        int inv = cc != c;
        long kk = 0;
        E *t = cc;
        while (kk < 64) {
            E *q = rat_normal(s_div(t, mk_int(b)));
            if (q->t != E_INT) break;
            t = q, kk++;
            if (expr_eq(t, K->c1)) break;
        }
        if (!kk || !expr_eq(t, K->c1)) continue;
        E *rest = K->c1;
        for (uint32_t j = 1; j < e->n; j++)
            if (j != i) rest = s_mul2(rest, EK(e)[j]);
        *ch = 1;
        return s_mul2(rest, s_pow(K0(p), s_add2(K1(p), mk_int(inv ? -kk : kk))));
    }
    return e;
}

static E *simp_pmerge(XP, E *e) {
    int ch = 0;
    E *r = pmerge(X, e, &ch);
    return ch ? r : NULL;
}

int casx_tidy_register(XP) { return X->reg_hook(CASX_HOOK_SIMPLIFY_PRE, simp_pmerge); }
