/*
 * lapl.c -- Laplace transforms of the advanced CAS extension (casx.h): laplace and
 * ilaplace, moved out of the core (ode.c) in CAS round 3 to make room in flash bank 2.
 * The core's commands forward to these with their default variables (cmds.c).
 *
 * laplace(f, t, s): rules on a sum of products after linearising trig powers:
 *   constant c -> c/s; sums termwise; constant factors out; e^(a t + b) g(t) -> e^b G(s - a);
 *   t^k g(t) -> (-1)^k d^k/ds^k G(s); t^n -> Gamma(n+1)/s^(n+1); e^(a t + b) -> e^b/(s - a);
 *   sin, cos, sinh, cosh of a t; heaviside(a t), dirac(a t); heaviside(t - c) -> e^(-c s)/s;
 *   sin / cos(a t + b) by the addition theorem; sinh / cosh through exponentials.
 * ilaplace(F, s, t): terms e^(-c s) R_c(s) shifted (heaviside(t - c)); partial fractions of
 *   R_c; constant -> dirac; c/(p s + q)^k; (A s + B)/(p s^2 + q s + w)^k with k <= 12
 *   (complete the square; powers by the recurrence of il_quad_pow).
 * No rule: the error "transform not found" / "unsupported denominator".
 */
#include "casx_mod.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

static E *lap_rec(XP, E *f, E *t, E *s, int depth);

static E *simpl(XP, E *e) { return eval_cmd(C_SIMPLIFY, 1, &e); }

static int lin_in(XP, E *u, E *t, E **a, E **b) {
    E *d = diff(u, t);
    if (!freeof(d, t)) return 0;
    *a = d;
    *b = expand(s_sub(u, s_mul2(d, t)));
    return freeof(*b, t);
}

static E *lap_rec(XP, E *f, E *t, E *s, int depth) {
    stack_check();
    if (depth > 12) return NULL;
    if (freeof(f, t)) return s_div(f, s);
    if (f->t == E_ADD) {
        E **v = salloc(sizeof(E *) * f->n);
        for (uint32_t i = 0; i < f->n; i++) {
            v[i] = lap_rec(X, EK(f)[i], t, s, depth + 1);
            if (!v[i]) return NULL;
        }
        return s_add(f->n, v);
    }
    E *a, *b;
    if (f->t == E_MUL) {
        E **c = salloc(sizeof(E *) * f->n), **r = salloc(sizeof(E *) * f->n);
        uint32_t nc = 0, nr = 0;
        E *expo = NULL, *tpow = NULL;
        for (uint32_t i = 0; i < f->n; i++) {
            E *q = EK(f)[i];
            if (freeof(q, t)) c[nc++] = q;
            else if (!expo && q->t == E_POW && ISYM(K0(q), S_E) && lin_in(X, K1(q), t, &a, &b)) expo = q;
            else if (!tpow && (expr_eq(q, t) || (q->t == E_POW && expr_eq(K0(q), t) && K1(q)->t == E_INT)))
                tpow = q;
            else r[nr++] = q;
        }
        if (nc) {
            E **nf = salloc(sizeof(E *) * f->n);
            uint32_t nn = 0;
            for (uint32_t i = 0; i < f->n; i++)
                if (!freeof(EK(f)[i], t)) nf[nn++] = EK(f)[i];
            E *g = lap_rec(X, s_mul(nn, nf), t, s, depth + 1);
            return g ? s_mul2(s_mul(nc, c), g) : NULL;
        }
        if (expo) {
            lin_in(X, K1(expo), t, &a, &b);
            E *rest = s_mul2(nr ? s_mul(nr, r) : K->c1, tpow ? tpow : K->c1);
            E *G = lap_rec(X, rest, t, s, depth + 1);
            if (!G) return NULL;
            return s_mul2(s_exp(b), subst(G, s, s_sub(s, a)));
        }
        if (tpow && nr) {
            long k = 1;
            if (tpow->t == E_POW) num_is_small_int(K1(tpow), &k);
            if (k < 0) return NULL;
            E *G = lap_rec(X, s_mul(nr, r), t, s, depth + 1);
            if (!G) return NULL;
            for (long j = 0; j < k; j++) G = diff(G, s);
            return (k & 1) ? s_neg(G) : G;
        }
        return NULL;
    }
    if (expr_eq(f, t)) return s_inv(s_powi(s, 2));
    if (f->t == E_POW) {
        if (expr_eq(K0(f), t) && is_num(K1(f)) && num_sgn(K1(f)) > 0) {
            E *n1 = s_add2(K1(f), K->c1);
            return s_div(s_fn1(F_GAMMA, n1), s_pow(s, n1));
        }
        if (ISYM(K0(f), S_E) && lin_in(X, K1(f), t, &a, &b)) return s_div(s_exp(b), s_sub(s, a));
        return NULL;
    }
    if (f->t == E_FN && f->n == 1 && lin_in(X, K0(f), t, &a, &b) && is_zero(b)) {
        E *a2 = s_powi(a, 2), *s2 = s_powi(s, 2);
        switch (f->s) {
        case F_SIN: return s_div(a, s_add2(s2, a2));
        case F_COS: return s_div(s, s_add2(s2, a2));
        case F_SINH: return s_div(a, s_sub(s2, a2));
        case F_COSH: return s_div(s, s_sub(s2, a2));
        case F_HEAVISIDE: {
            double av;
            return to_double(a, &av) && av < 0 ? K->c0 : s_inv(s);
        }
        case F_DIRAC: return s_inv(s_fn1(F_ABS, a));
        }
    }
    if (f->t == E_FN && f->n == 1 && f->s == F_HEAVISIDE && lin_in(X, K0(f), t, &a, &b) && expr_eq(a, K->c1)) {
        double bv;
        if (to_double(b, &bv) && bv >= 0) return s_inv(s);
        return s_div(s_exp(s_mul2(b, s)), s);
    }
    if (f->t == E_FN && f->n == 1 && (f->s == F_SIN || f->s == F_COS) && lin_in(X, K0(f), t, &a, &b)) {
        E *at = s_mul2(a, t);
        E *g = f->s == F_SIN ? s_add2(s_mul2(s_fn1(F_SIN, at), s_fn1(F_COS, b)), s_mul2(s_fn1(F_COS, at), s_fn1(F_SIN, b)))
                             : s_sub(s_mul2(s_fn1(F_COS, at), s_fn1(F_COS, b)), s_mul2(s_fn1(F_SIN, at), s_fn1(F_SIN, b)));
        return lap_rec(X, g, t, s, depth + 1);
    }
    return NULL;
}

static E *hyp_exp(XP, E *e, E *t) {
    stack_check();
    if (freeof(e, t) || (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN)) return e;
    E **k = salloc(sizeof(E *) * e->n);
    for (uint32_t i = 0; i < e->n; i++) k[i] = hyp_exp(X, EK(e)[i], t);
    e = X->rebuild(e, k);
    if (e->t == E_FN && e->n == 1 && (e->s == F_SINH || e->s == F_COSH)) {
        E *p = s_exp(K0(e)), *m = s_exp(s_neg(K0(e)));
        return s_div(e->s == F_SINH ? s_sub(p, m) : s_add2(p, m), K->c2);
    }
    return e;
}

static E *laplace(XP, E *f, E *t, E *s) {
    if (t->t != E_SYM || s->t != E_SYM) cas_throw(CAS_ERR_DOMAIN, "laplace: expected variable names");
    E *r = lap_rec(X, expand(eval_cmd(C_TLIN, 1, &f)), t, s, 0);
    if (!r) r = lap_rec(X, expand(hyp_exp(X, f, t)), t, s, 0);
    if (!r) cas_throw(CAS_ERR_UNSUPPORTED, "laplace: transform not found");
    return simpl(X, r);
}

static void il_quad_pow(XP, E *w2, long k, E *t, E **g1, E **gs) {
    E *w = s_sqrt(w2), *a = s_div(s_fn1(F_SIN, s_mul2(w, t)), w), *b = s_fn1(F_COS, s_mul2(w, t));
    for (long j = 1; j < k; j++) {
        E *na = s_div(s_sub(s_mul2(mk_int(2 * j - 1), a), s_mul2(t, b)), s_mul2(mk_int(2 * j), w2));
        E *nb = s_mul2(s_div(t, mk_int(2 * j)), a);
        a = na, b = nb;
    }
    *g1 = a, *gs = b;
}

static E *exp_shift(XP, E *term, E *s, E **rest) {
    E **fs = term->t == E_MUL ? EK(term) : &term;
    uint32_t m = term->t == E_MUL ? term->n : 1;
    for (uint32_t j = 0; j < m; j++)
        if (fs[j]->t == E_POW && ISYM(K0(fs[j]), S_E)) {
            E *a, *b;
            if (lin_in(X, K1(fs[j]), s, &a, &b) && is_zero(b) && !is_zero(a)) {
                *rest = s_div(term, fs[j]);
                return s_neg(a);
            }
        }
    return NULL;
}

static E *up_e(XP, UP u, E *s) {
    E **t = salloc(sizeof(E *) * (size_t)(u.d + 2));
    for (long i = 0; i <= u.d; i++) t[i] = s_mul2(u.c[i], s_powi(s, i));
    return s_add((uint32_t)(u.d + 1), t);
}

static E *ilaplace(XP, E *F, E *s, E *t) {
    if (t->t != E_SYM || s->t != E_SYM) cas_throw(CAS_ERR_DOMAIN, "ilaplace: expected variable names");
    {
        E *Fx = expand(F), *rest;
        uint32_t n = Fx->t == E_ADD ? Fx->n : 1, ng = 0;
        E **terms = Fx->t == E_ADD ? EK(Fx) : &Fx, **cs = salloc(sizeof(E *) * (n + 1)), **Rs = salloc(sizeof(E *) * (n + 1));
        int any = 0;
        for (uint32_t i = 0; i < n; i++) {
            E *c = exp_shift(X, terms[i], s, &rest);
            if (!c) c = K->c0, rest = terms[i];
            else any = 1;
            uint32_t g = 0;
            while (g < ng && !is_zero(s_sub(cs[g], c))) g++;
            if (g == ng) cs[ng] = c, Rs[ng++] = rest;
            else Rs[g] = s_add2(Rs[g], rest);
        }
        if (any) {
            E **out = salloc(sizeof(E *) * (ng + 1));
            for (uint32_t g = 0; g < ng; g++) {
                E *r = ilaplace(X, rat_normal(Rs[g]), s, t);
                out[g] = is_zero(cs[g]) ? r : s_mul2(subst(r, t, s_sub(t, cs[g])), s_fn1(F_HEAVISIDE, s_sub(t, cs[g])));
            }
            return simpl(X, s_add(ng, out));
        }
    }
    E *pa[2] = {F, s};
    E *pf = eval_cmd(C_PARTFRAC, 2, pa);
    E **ts = pf->t == E_ADD ? EK(pf) : &pf;
    uint32_t n = pf->t == E_ADD ? pf->n : 1;
    E **out = salloc(sizeof(E *) * (n + 1));
    for (uint32_t i = 0; i < n; i++) {
        E *term = ts[i], *shift = NULL;
        {
            E **fs = term->t == E_MUL ? EK(term) : &term;
            uint32_t m = term->t == E_MUL ? term->n : 1;
            for (uint32_t j = 0; j < m; j++)
                if (fs[j]->t == E_POW && ISYM(K0(fs[j]), S_E)) {
                    E *a, *b;
                    if (lin_in(X, K1(fs[j]), s, &a, &b) && is_zero(b)) {
                        shift = s_neg(a);
                        term = s_div(term, fs[j]);
                        break;
                    }
                }
        }
        E *num, *den;
        X->numden(term, &num, &den);
        UP N, D;
        if (!up_from(num, s, &N) || !up_from(den, s, &D)) cas_throw(CAS_ERR_UNSUPPORTED, "ilaplace: not rational");
        E *r = NULL, *content;
        E *fl = X->factor_list_in(den, s, &content), *f0 = NULL;
        long k = 0;
        uint32_t nfac = 0;
        for (uint32_t j = 0; j < fl->n; j++) {
            if (freeof(K0(EK(fl)[j]), s)) {
                content = s_mul2(content, s_pow(K0(EK(fl)[j]), K1(EK(fl)[j])));
                continue;
            }
            f0 = K0(EK(fl)[j]);
            num_is_small_int(K1(EK(fl)[j]), &k);
            nfac++;
        }
        if (nfac == 0) {
            if (N.d == 0) {
                r = s_mul2(s_div(up_e(X, N, s), content), s_fn1(F_DIRAC, t));
                out[i] = shift ? subst(r, t, s_sub(t, shift)) : r;
                continue;
            }
            cas_throw(CAS_ERR_UNSUPPORTED, "ilaplace: improper rational function");
        }
        if (nfac != 1) cas_throw(CAS_ERR_UNSUPPORTED, "ilaplace: unsupported denominator");
        UP Fq;
        up_from(f0, s, &Fq);
        E *cN = s_div(up_e(X, N, s), content);
        if (Fq.d == 1) {
            E *p = Fq.c[1], *a = rat_normal(s_neg(s_div(Fq.c[0], p)));
            if (N.d > 0) cas_throw(CAS_ERR_UNSUPPORTED, "ilaplace: unexpected numerator");
            E *c = s_div(cN, s_powi(p, k));
            r = s_mul(3, (E *[]){s_div(c, s_fn1(F_FACT, mk_int(k - 1))), s_pow(t, mk_int(k - 1)), s_exp(s_mul2(a, t))});
        } else if (Fq.d == 2 && k == 1) {
            E *p = Fq.c[2];
            E *h = s_div(Fq.c[1], s_mul2(K->c2, p));
            E *om2 = rat_normal(s_sub(s_div(Fq.c[0], p), s_powi(h, 2)));
            UP Nn;
            up_from(cN, s, &Nn);
            E *A = Nn.d >= 1 ? s_div(Nn.c[1], p) : K->c0, *B = s_div(Nn.c[0], p);
            E *om = s_sqrt(om2), *ex = s_exp(s_mul2(s_neg(h), t));
            E *cosp = s_mul2(A, s_fn1(F_COS, s_mul2(om, t)));
            E *sinp = s_mul2(s_div(s_sub(B, s_mul2(A, h)), om), s_fn1(F_SIN, s_mul2(om, t)));
            r = s_mul2(ex, s_add2(cosp, sinp));
        } else if (Fq.d == 2 && k >= 2 && k <= 12) {
            E *p = Fq.c[2];
            E *h = s_div(Fq.c[1], s_mul2(K->c2, p));
            E *om2 = rat_normal(s_sub(s_div(Fq.c[0], p), s_powi(h, 2)));
            UP Nn;
            up_from(cN, s, &Nn);
            if (Nn.d > 1) cas_throw(CAS_ERR_UNSUPPORTED, "ilaplace: unexpected numerator");
            E *pk = s_powi(p, k), *A = Nn.d >= 1 ? s_div(Nn.c[1], pk) : K->c0, *B = Nn.d >= 0 ? s_div(Nn.c[0], pk) : K->c0;
            E *g1, *gs;
            il_quad_pow(X, om2, k, t, &g1, &gs);
            r = s_mul2(s_exp(s_mul2(s_neg(h), t)), s_add2(s_mul2(A, gs), s_mul2(s_sub(B, s_mul2(A, h)), g1)));
        } else
            cas_throw(CAS_ERR_UNSUPPORTED, "ilaplace: unsupported denominator");
        if (shift) r = s_mul2(subst(r, t, s_sub(t, shift)), s_fn1(F_HEAVISIDE, s_sub(t, shift)));
        out[i] = r;
    }
    return simpl(X, s_add(n, out));
}

/* for fourier (cas/ext/int/fourier.c) */
E *casx_laplace(XP, E *f, E *t, E *s) { return laplace(X, f, t, s); }

enum { L_LAPLACE, L_ILAPLACE };
static const casx_cmd_t ltab[] = {{"laplace", 3, 3}, {"ilaplace", 3, 3}};

static E *lcmd(XP, int op, uint32_t n, E **a) {
    return op == L_LAPLACE ? laplace(X, a[0], a[1], a[2]) : ilaplace(X, a[0], a[1], a[2]);
}

int casx_lap_register(XP) { return X->reg("casx", ltab, (int)(sizeof ltab / sizeof ltab[0]), lcmd); }
