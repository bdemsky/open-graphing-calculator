/*
 * trig.c -- trigonometric rewriting: exponential form, linearisation
 * (products/powers -> multiple angles), expansion (sum and multiple angle
 * formulas), simplification heuristics, log expansion/collection.
 *
 * These are the user-level rewriting commands that the automatic
 * simplifier (simp.c) deliberately does not apply, because none of them
 * produces a canonical form on its own:
 *
 *   command     entry point       rewriting
 *   trig2exp    trig_to_exp       sin, cos, tan, cot, sec, csc, sinh, cosh,
 *                                 tanh -> complex exponentials (Euler)
 *   exp2trig    exp_to_trig       e^(a + i b) -> e^a (cos b + i sin b)
 *   tlin        trig_linearize    products and powers of sin/cos -> sums of
 *   tcollect    trig_collect      sin/cos of multiple angles
 *   texpand     trig_expand       sin(a+b), cos(n x), tan(a+b) -> functions
 *                                 of the single angles
 *   trigsimp    trig_simplify     heuristic: smallest of several rewritings
 *   lnexpand    ln_expand         ln(a b^n) -> ln a + n ln b,
 *                                 ln 12 -> 2 ln 2 + ln 3
 *   lncollect   ln_collect        a ln u + b ln v -> ln(u^a v^b)
 *
 * The commands are dispatched from cmds.c.  integ.c reuses the trig
 * rewritings for integrating trigonometric polynomials (linearisation) and
 * products of exponentials and trig functions (exponential form);
 * simplify.c (simplify_full) uses trig_simplify, trig_linearize and
 * ln_collect_pos as candidate transformations.
 *
 * Linearisation goes through the exponential form (the classic approach,
 * cf. Cohen, "Computer Algebra and Symbolic Computation: Mathematical
 * Methods", ch. 7 on trigonometric expansion and contraction): write
 * sin u = (e^(iu) - e^(-iu))/(2i), cos u = (e^(iu) + e^(-iu))/2, expand (the
 * automatic simplifier merges e^(iu) e^(iv) = e^(i(u+v)) and i^2 = -1), then
 * turn the exponentials back into sin/cos of the combined angles.  Expansion
 * uses the addition theorems directly.
 *
 * All functions work on simplified expressions, build their results with the
 * s_* constructors in scratch and do not release scratch themselves (the
 * caller's evaluation owns it).  Recursion is guarded by stack_check.
 * Most rewritings assume that symbols are real (e.g. which part of an
 * exponent is "imaginary" is decided syntactically by the presence of i).
 */
#include "cas_int.h"
#include <string.h>

/* Apply f to every child of e and rebuild (re-simplifying via rebuild) only
 * if some child changed; atoms are returned unchanged.  The bottom-up driver
 * used by all the recursive rewritings in this file. */
static E *map_kids(E *e, E *(*f)(E *)) {
    if (e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR) return e;
    E **k = salloc(sizeof(E *) * (e->n + 1));
    int ch = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        k[i] = f(EK(e)[i]);
        if (k[i] != EK(e)[i]) ch = 1;
    }
    return ch ? rebuild(e, k) : e;
}

/* 1 if e contains a circular trig function (sin cos tan cot sec csc)
 * anywhere; hyperbolic and inverse trig functions do not count. */
int has_trig(E *e) {
    stack_check();
    if (e->t == E_FN && (e->s == F_SIN || e->s == F_COS || e->s == F_TAN || e->s == F_COT || e->s == F_SEC ||
                         e->s == F_CSC))
        return 1;
    if (e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_trig(EK(e)[i])) return 1;
    return 0;
}

/* 1 if a trig function of e has an argument k u with an integer |k| > 12: expanding it
 * (sin 50x as a polynomial of degree 50 in sin x, cos x) is far too big to be simpler */
static int big_multiple(E *e) {
    stack_check();
    if (e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR) return 0;
    if (e->t == E_FN && (e->s == F_SIN || e->s == F_COS || e->s == F_TAN || e->s == F_COT || e->s == F_SEC ||
                         e->s == F_CSC) && e->n == 1) {
        E *u = K0(e);
        if (u->t == E_MUL && EK(u)[0]->t == E_INT) {
            long k;
            if (!num_is_small_int(EK(u)[0], &k) || k > 12 || k < -12) return 1;
        }
    }
    for (uint32_t i = 0; i < e->n; i++)
        if (big_multiple(EK(e)[i])) return 1;
    return 0;
}

/* e^(i u) */
static E *eix(E *u) { return s_exp(s_mul2(K->i_, u)); }

/*
 * trig2exp: rewrite circular and hyperbolic functions bottom-up into
 * exponentials.  With p = e^(iu), m = e^(-iu):
 *   sin u = (p - m)/(2i)       cos u = (p + m)/2   tan u = (p - m)/(i (p + m))
 *   cot u = i (p + m)/(p - m)  sec u = 2/(p + m)   csc u = 2i/(p - m)
 * and sinh, cosh, tanh through e^u, e^(-u).  Inverse functions are left
 * alone.
 */
E *trig_to_exp(E *e) {
    stack_check();
    e = map_kids(e, trig_to_exp);
    if (e->t != E_FN || e->n != 1) return e;
    E *u = K0(e);
    E *p = eix(u), *m = eix(s_neg(u));
    switch (e->s) {
    case F_SIN: return s_div(s_sub(p, m), s_mul2(K->c2, K->i_));
    case F_COS: return s_div(s_add2(p, m), K->c2);
    case F_TAN: return s_div(s_sub(p, m), s_mul2(K->i_, s_add2(p, m)));
    case F_COT: return s_div(s_mul2(K->i_, s_add2(p, m)), s_sub(p, m));
    case F_SEC: return s_div(K->c2, s_add2(p, m));
    case F_CSC: return s_div(s_mul2(K->c2, K->i_), s_sub(p, m));
    case F_SINH: return s_div(s_sub(s_exp(u), s_exp(s_neg(u))), K->c2);
    case F_COSH: return s_div(s_add2(s_exp(u), s_exp(s_neg(u))), K->c2);
    case F_TANH: return s_div(s_sub(s_exp(u), s_exp(s_neg(u))), s_add2(s_exp(u), s_exp(s_neg(u))));
    }
    return e;
}

/* Like trig_to_exp but only for sin and cos (for linearisation; tan etc.
 * are rewritten to sin/cos by the callers that want them included). */
static E *sincos_to_exp(E *e) {
    stack_check();
    e = map_kids(e, sincos_to_exp);
    if (e->t != E_FN || e->n != 1) return e;
    E *u = K0(e);
    switch (e->s) {
    case F_SIN: return s_div(s_sub(eix(u), eix(s_neg(u))), s_mul2(K->c2, K->i_));
    case F_COS: return s_div(s_add2(eix(u), eix(s_neg(u))), K->c2);
    }
    return e;
}

/*
 * exp2trig: e^(a + i b) -> e^a (cos b + i sin b), bottom-up.
 * The exponent is expanded into terms; a term t belongs to the imaginary
 * part when it contains i and -i*t (= t/i) is free of i, so i*x and
 * 3*i*pi/4 qualify.  All other terms form the "real" part a (no realness
 * check is made: symbols are taken as real).
 * Returns e unchanged when the exponent has no imaginary term.
 */
E *exp_to_trig(E *e) {
    stack_check();
    e = map_kids(e, exp_to_trig);
    if (e->t != E_POW || !ISYM(K0(e), S_E)) return e;
    E *x = expand(K1(e));
    E **re = salloc(sizeof(E *) * (x->t == E_ADD ? x->n : 1) + 8), **im = salloc(sizeof(E *) * (x->t == E_ADD ? x->n : 1) + 8);
    uint32_t nr = 0, ni = 0;
    uint32_t n = x->t == E_ADD ? x->n : 1;
    E **ts = x->t == E_ADD ? EK(x) : &x;
    for (uint32_t i = 0; i < n; i++) {
        E *t = ts[i];
        if (!freeof_sym(t, S_I)) {
            E *th = expand(s_mul2(t, s_neg(K->i_)));
            if (freeof_sym(th, S_I)) {
                im[ni++] = th;
                continue;
            }
        }
        re[nr++] = t;
    }
    if (!ni) return e;
    E *th = s_add(ni, im);
    E *c = s_add2(s_fn1(F_COS, th), s_mul2(K->i_, s_fn1(F_SIN, th)));
    return s_mul2(s_exp(s_add(nr, re)), c);
}

/*
 * tlin: linearise products and powers of sin/cos into sums of sin/cos of
 * multiple angles, e.g. sin(x)^2 -> 1/2 - cos(2x)/2.
 * Method: sin/cos -> exponentials, expand (products of exponentials merge
 * into single exponentials of summed angles), back to trig, expand again;
 * the conjugate pairs e^(ikx), e^(-ikx) then combine into real cos/sin
 * terms through the simplifier's parity rules (sin(-u) = -sin u).
 * Expressions without circular trig functions are returned unchanged.
 */
E *trig_linearize(E *e) {
    if (!has_trig(e)) return e;
    E *t = sincos_to_exp(e);
    t = expand(t);
    t = exp_to_trig(t);
    t = expand(t);
    return t;
}

/* tcollect: currently the same transformation as tlin. */
E *trig_collect(E *e) { return trig_linearize(e); }

/* expand sin(a+b), cos(a+b), sin(n u), cos(n u), tan(a+b) */
static E *texp_rec(E *e);

static E *sin_sum(E *u, int want_sin);

/*
 * sin(u) (want_sin = 1) or cos(u) (want_sin = 0), fully expanded:
 *   u = a + b + ...:  addition theorems
 *        sin(a+b) = sin a cos b + cos a sin b,
 *        cos(a+b) = cos a cos b - sin a sin b,
 *     with a = first term and b = sum of the remaining terms, recursively.
 *     Each level makes four recursive calls (sin and cos of a and of b), so
 *     a sum of n terms costs O(2^n) calls;
 *   u = k*v with an integer 2 <= k <= 64: multiple-angle formulas by the
 *     recurrence  sin(jv) = sin((j-1)v) cos v + cos((j-1)v) sin v,
 *                 cos(jv) = cos((j-1)v) cos v - sin((j-1)v) sin v,
 *     expanding at each step so the result is a polynomial in sin v, cos v;
 *   otherwise sin(u) / cos(u) unchanged.
 * Negative multiples are not expanded here; the simplifier already moves the
 * sign out by parity (sin(-2x) = -sin(2x)).
 */
static E *sin_sum(E *u, int want_sin) {
    stack_check();
    if (u->t == E_ADD) {
        E *a = EK(u)[0];
        E *b = u->n == 2 ? K1(u) : s_add(u->n - 1, EK(u) + 1);
        E *sa = sin_sum(a, 1), *ca = sin_sum(a, 0), *sb = sin_sum(b, 1), *cb = sin_sum(b, 0);
        if (want_sin) return s_add2(s_mul2(sa, cb), s_mul2(ca, sb));
        return s_sub(s_mul2(ca, cb), s_mul2(sa, sb));
    }
    long k;
    if (u->t == E_MUL && num_is_small_int(K0(u), &k) && k > 1 && k <= 64) {
        E *v = u->n == 2 ? K1(u) : s_mul(u->n - 1, EK(u) + 1);
        /* Chebyshev style recurrence: f(k v) from f((k-1) v) */
        E *s1 = s_fn1(F_SIN, v), *c1 = s_fn1(F_COS, v);
        E *sk = s1, *ck = c1;
        for (long i = 2; i <= k; i++) {
            E *ns = s_add2(s_mul2(sk, c1), s_mul2(ck, s1));
            E *nc = s_sub(s_mul2(ck, c1), s_mul2(sk, s1));
            sk = expand(ns);
            ck = expand(nc);
        }
        return want_sin ? sk : ck;
    }
    return s_fn1(want_sin ? F_SIN : F_COS, u);
}

/* texpand worker: bottom-up, replace sin/cos by sin_sum and tan(u) by
 * sin_sum(u,1)/sin_sum(u,0) when u is a sum or a numeric multiple. */
static E *texp_rec(E *e) {
    stack_check();
    e = map_kids(e, texp_rec);
    if (e->t != E_FN || e->n != 1) return e;
    E *u = K0(e);
    switch (e->s) {
    case F_SIN: return sin_sum(u, 1);
    case F_COS: return sin_sum(u, 0);
    case F_TAN:
        if (u->t == E_ADD || (u->t == E_MUL && is_num(K0(u)))) return s_div(sin_sum(u, 1), sin_sum(u, 0));
        return e;
    case F_LN: return e;
    }
    return e;
}

/* texpand: expand angle sums and integer multiples, then expand the
 * resulting polynomial in sin/cos. */
E *trig_expand(E *e) { return expand(texp_rec(e)); }

/* rewrite tan, cot, sec, csc into sin/cos (bottom-up); used by
 * trig_simplify and by integ.c */
E *trig_sincos(E *e) {
    stack_check();
    e = map_kids(e, trig_sincos);
    if (e->t != E_FN || e->n != 1) return e;
    E *u = K0(e);
    switch (e->s) {
    case F_TAN: return s_div(s_fn1(F_SIN, u), s_fn1(F_COS, u));
    case F_COT: return s_div(s_fn1(F_COS, u), s_fn1(F_SIN, u));
    case F_SEC: return s_inv(s_fn1(F_COS, u));
    case F_CSC: return s_inv(s_fn1(F_SIN, u));
    }
    return e;
}

/* replace sin(u)^2 by 1-cos(u)^2 (dir 0) or cos^2 by 1 - sin^2 (dir 1) */
static int g_dir;
/* Pythagorean substitution in direction g_dir (set by the caller): every
 * sin(u)^k (resp. cos(u)^k) with integer k >= 2 becomes
 * (1 - cos(u)^2)^(k div 2) * sin(u)^(k mod 2) (resp. with sin/cos swapped).
 * Followed by rat_normal this eliminates one of the two functions from even
 * powers, which is how identities like sin^2 + cos^2 = 1 are found. */
static E *pyth_rec(E *e) {
    stack_check();
    e = map_kids(e, pyth_rec);
    if (e->t == E_POW && K0(e)->t == E_FN && K0(e)->n == 1) {
        long k;
        int id = K0(e)->s;
        if (num_is_small_int(K1(e), &k) && k >= 2 && ((g_dir == 0 && id == F_SIN) || (g_dir == 1 && id == F_COS))) {
            E *u = K0(K0(e));
            E *other = s_fn1(id == F_SIN ? F_COS : F_SIN, u);
            E *base = s_sub(K->c1, s_powi(other, 2));
            E *r = s_powi(base, k / 2);
            if (k & 1) r = s_mul2(r, K0(e));
            return r;
        }
    }
    return e;
}

/* Size measure used to rank trig_simplify candidates: node count
 * (expr_size, capped). */
static size_t complexity(E *e) {
    size_t n = expr_size(e);
    return n;
}

/* sin(u)^k * cos(u)^-k -> tan(u)^k inside products (bottom-up).  Only the
 * first matching sin/cos pair of each product is replaced; the exponents
 * must cancel exactly (k may be symbolic or negative). */
static E *to_tan(E *e) {
    stack_check();
    e = map_kids(e, to_tan);
    if (e->t != E_MUL) return e;
    for (uint32_t i = 0; i < e->n; i++) {
        E *a = EK(e)[i];
        E *ab = a->t == E_POW ? K0(a) : a, *ae = a->t == E_POW ? K1(a) : K->c1;
        if (!IFN(ab, F_SIN) || ab->n != 1) continue;
        for (uint32_t j = 0; j < e->n; j++) {
            E *b = EK(e)[j];
            E *bb = b->t == E_POW ? K0(b) : b, *be = b->t == E_POW ? K1(b) : K->c1;
            if (!IFN(bb, F_COS) || bb->n != 1 || !expr_eq(K0(ab), K0(bb))) continue;
            if (!is_zero(s_add2(ae, be))) continue;
            /* replace both */
            E **k = salloc(sizeof(E *) * e->n);
            uint32_t n = 0;
            for (uint32_t q = 0; q < e->n; q++)
                if (q != i && q != j) k[n++] = EK(e)[q];
            k[n++] = s_pow(s_fn1(F_TAN, K0(ab)), ae);
            return s_mul(n, k);
        }
    }
    return e;
}

/*
 * trigsimp: heuristic trigonometric simplification.  Without circular trig
 * functions this is just rat_normal.  Otherwise it generates candidates
 *   1.   rat_normal of the sin/cos form,
 *   2-3. the same after the Pythagorean substitution in either direction,
 *   4.   the linearised sin/cos form,
 *   5-6. the expanded form (texpand) with either Pythagorean substitution,
 * plus the tan-contracted version of each (to_tan) when it differs, and
 * returns the one with the fewest nodes.  The input e is the initial "best"
 * and wins ties, so the result is never larger than the input.  Each
 * candidate costs a rat_normal (multivariate gcd) with sin(u), cos(u) as
 * kernels.  Errors from rat_normal (e.g. out of memory) propagate.
 */
E *trig_simplify(E *e) {
    if (!has_trig(e)) return rat_normal(e);
    E *best = e;
    size_t bs = complexity(e);
    E *cands[16];
    int nc = 0;
    E *sc = trig_sincos(e);
    cands[nc++] = rat_normal(sc);
    g_dir = 0;
    cands[nc++] = rat_normal(pyth_rec(sc));
    g_dir = 1;
    cands[nc++] = rat_normal(pyth_rec(sc));
    E *lin = trig_linearize(sc);
    cands[nc++] = rat_normal(lin);
    if (!big_multiple(e)) {
        E *te = rat_normal(trig_sincos(trig_expand(e)));
        g_dir = 0;
        cands[nc++] = rat_normal(pyth_rec(te));
        g_dir = 1;
        cands[nc++] = rat_normal(pyth_rec(te));
    }
    int n0 = nc;
    for (int i = 0; i < n0 && nc < 16; i++) {
        E *t = to_tan(cands[i]);
        if (!expr_eq(t, cands[i])) cands[nc++] = t;
    }
    for (int i = 0; i < nc; i++) {
        size_t s = complexity(cands[i]);
        if (s < bs) {
            bs = s;
            best = cands[i];
        }
    }
    return best;
}

/*
 * trig_pyth: the cheap trigonometric pass of autosimplify (simplify.c).
 * Only the Pythagorean candidates of trig_simplify (2-3 above: the sin/cos
 * form with sin^2 -> 1 - cos^2 or cos^2 -> 1 - sin^2, then rat_normal) and
 * their tan contractions; no linearisation and no angle expansion, so the
 * result keeps the user's angles (2 sin x cos x stays, sin(x)^2 + cos(x)^2
 * becomes 1, 1 + tan(x)^2 becomes 1/cos(x)^2).  The input wins ties, so the
 * result is never larger (expr_size).  Errors from rat_normal propagate.
 */
E *trig_pyth(E *e) {
    if (!has_trig(e)) return e;
    E *best = e;
    size_t bs = complexity(e);
    E *sc = trig_sincos(e);
    for (int dir = 0; dir < 2; dir++) {
        g_dir = dir;
        E *c = rat_normal(pyth_rec(sc));
        E *t = to_tan(c);
        E *cand[2] = {c, t};
        for (int i = 0; i < 2; i++) {
            size_t s = complexity(cand[i]);
            if (s < bs) {
                bs = s;
                best = cand[i];
            }
        }
    }
    return best;
}

/*
 * lnexpand worker, bottom-up:
 *   ln(a*b*...) -> ln a + ln b + ...   (each factor expanded recursively)
 *   ln(a^n)     -> n ln a              (any exponent n)
 *   ln(p/q)     -> ln p - ln q         (rational number; not factored)
 *   ln(N)       -> sum e_i ln p_i      (integer N = prod p_i^e_i, factored
 *                                       with int_factor; slow for huge N)
 * These identities hold for positive reals only (ln(x^2) = 2 ln x fails for
 * x < 0); the command applies them unconditionally, as an explicit user
 * request.  Logs of negative numbers never reach the integer case because
 * the simplifier rewrites ln(-a) = ln a + i pi.
 */
static E *lnexp_rec(E *e) {
    stack_check();
    e = map_kids(e, lnexp_rec);
    if (!IFN(e, F_LN)) return e;
    E *u = K0(e);
    if (u->t == E_MUL) {
        E **t = salloc(sizeof(E *) * u->n);
        for (uint32_t i = 0; i < u->n; i++) t[i] = lnexp_rec(s_ln(EK(u)[i]));
        return s_add(u->n, t);
    }
    if (u->t == E_POW) return s_mul2(K1(u), lnexp_rec(s_ln(K0(u))));
    if (u->t == E_RAT) return s_sub(s_ln(K0(u)), s_ln(K1(u)));
    if (u->t == E_INT && u->n) {
        E *fl = int_factor(u);
        if (fl->n > 1) {
            E **t = salloc(sizeof(E *) * fl->n);
            for (uint32_t i = 0; i < fl->n; i++) t[i] = s_mul2(K1(EK(fl)[i]), s_ln(K0(EK(fl)[i])));
            return s_add(fl->n, t);
        }
    }
    return e;
}

/* lnexpand command. */
E *ln_expand(E *e) { return lnexp_rec(e); }

/* a ln u + b ln v -> ln(u^a v^b) for rational a, b.  With pos_only, only
 * logarithms of positive constants are combined (branch-safe, used by
 * simplify). */
/* the pos_only flag of ln_collect; set temporarily by ln_collect_pos */
static int g_lnpos;
void trig_reset_statics(void); /* cas.c eval_impl: state an error may have left set */
void trig_reset_statics(void) { g_dir = 0; g_lnpos = 0; }

/*
 * lncollect: in every sum, combine the terms c*ln(u) with an exact rational
 * coefficient c into one logarithm ln(prod u^c); the other terms are
 * processed recursively.  A sum with fewer than two such terms is returned
 * unchanged (the recursively processed other terms are then discarded as
 * well).  Like lnexpand this is only valid for positive arguments unless
 * g_lnpos restricts it to logs of known-positive expressions (is_poslit).
 */
E *ln_collect(E *e) {
    stack_check();
    if (e->t != E_ADD) return map_kids(e, ln_collect);
    E **rest = salloc(sizeof(E *) * e->n), **args = salloc(sizeof(E *) * e->n);
    uint32_t nr = 0, na = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        E *t = EK(e)[i], *c, *r;
        split_coef(t, &c, &r);
        if (IFN(r, F_LN) && is_exactnum(c) && (!g_lnpos || is_poslit(K0(r))))
            args[na++] = s_pow(K0(r), c);
        else
            rest[nr++] = ln_collect(t);
    }
    if (na < 2) return e;
    rest[nr++] = s_ln(s_mul(na, args));
    return s_add(nr, rest);
}

/* Branch-safe ln_collect (only logs of positive quantities), used by
 * simplify_full.  Sets g_lnpos for the duration of the call; the old value
 * is restored also when ln_collect throws. */
E *ln_collect_pos(E *e) {
    int old = g_lnpos;
    g_lnpos = 1;
    E *volatile r = NULL;
    tryf tf;
    try_enter(&tf);
    if (!setjmp(tf.jb)) {
        r = ln_collect(e);
        try_leave(&tf);
    } else {
        try_leave(&tf);
        g_lnpos = old;
        rethrow();
    }
    g_lnpos = old;
    return r;
}
