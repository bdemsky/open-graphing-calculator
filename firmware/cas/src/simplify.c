/*
 * simplify.c -- the user-level simplify(): try several rewritings
 * (rational normal form, factored form, trigonometric and logarithmic
 * rewritings) and keep the smallest result.
 *
 * Role in the CAS
 * ---------------
 * Automatic simplification (simp.c) only produces a *canonical* form; it
 * never chooses between mathematically equivalent shapes such as
 * (x^2-1)/(x-1) versus x+1 or sin(x)^2+cos(x)^2 versus 1.  simplify_full()
 * is the "make it look nice" pass behind the simplify command (cmds.c) and
 * is also used to tidy results of implicitdiff (cmds.c), integration
 * (integ.c), summation (sum.c), ODE solutions (ode.c), solve (solve.c),
 * Gram-Schmidt / Cholesky entries (linalg.c) and pade (xcmds.c).
 *
 * Strategy
 * --------
 * There is no single normal form for elementary expressions, so the module
 * is a heuristic "generate and pick" search (in the spirit of the
 * simplification discussion in Cohen, "Computer Algebra and Symbolic
 * Computation"):
 *   1. denest square roots sqrt(a + b sqrt(c)) with rational a, b, c;
 *   2. candidates: rational normal form (poly.c rat_normal), trig_simplify
 *      (trig.c) of the input and of its normal form, the factored normal
 *      form (factor.c), ln-collection of the current best (only logs of
 *      known-positive arguments are merged, so no branch issues), denominator
 *      rationalisation, and rewriting sinh/cosh/tanh through exponentials;
 *   3. keep the candidate with the smallest cost() (a weighted node count).
 * Each rewriting runs under try_(): if it raises an ordinary error (domain,
 * unsupported, ...) the candidate is simply dropped; only interrupts and
 * out-of-memory propagate.
 *
 * simplify_auto (autosimplify command) is the cheap subset the calculator
 * runs on every displayed home/RPN result (MODE SIMPLIFY AUTO): rational
 * normal form (combines like terms across products and cancels common
 * factors), the Pythagorean identities and the Euler pass only, with size
 * guards; see the comment above it.  simplify_full also tries the Euler
 * pass.
 *
 * Memory: everything is scratch; rejected candidates are not released.
 * The only globals are the Euler pass's matched keys (eu_k, set per call).
 */
#include "casx.h"
#include <string.h>

/* rewriting passes implemented in trig.c */
int has_trig(E *e);
E *trig_simplify(E *e);
E *ln_collect_pos(E *e);
E *trig_to_exp(E *e);
E *exp_to_trig(E *e);
E *trig_pyth(E *e);

/* 1 for leaves (numbers, symbols, strings): nodes without children */
static int is_leaf(E *e) {
    return e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR;
}

/* 1 if e contains sinh, cosh or tanh anywhere (recursive tree walk). */
static int has_hyp_fn(E *e) {
    stack_check();
    if (e->t == E_FN && (e->s == F_SINH || e->s == F_COSH || e->s == F_TANH)) return 1;
    if (is_leaf(e)) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_hyp_fn(EK(e)[i])) return 1;
    return 0;
}

/* Hyperbolic candidate: sinh/cosh/tanh (and trig) -> exponentials, expand,
 * then rational normal form; e.g. cosh(x)^2 - sinh(x)^2 -> 1. */
static E *hyp_via_exp(E *e) { return rat_normal(expand(trig_to_exp(e))); }

/*
 * cost -- size measure used to rank candidate forms (smaller is simpler).
 * One per node, +2 per function application, +1 per power with a
 * non-numeric exponent, and big integers (more than 2 limbs, i.e. > 64
 * bits) add half their limb count, so huge coefficients count against a
 * form.  Linear in the tree size; recursion guarded by stack_check().
 */
static size_t cost(E *e) {
    stack_check();
    /* node count, with a small penalty for functions and powers */
    size_t n = 1;
    if (e->t == E_FN) n += 2;
    if (e->t == E_POW && !is_num(K1(e))) n += 1;
    if (e->t == E_INT && e->n > 2) n += e->n / 2;
    if (!is_leaf(e))
        for (uint32_t i = 0; i < e->n; i++) n += cost(EK(e)[i]);
    return n;
}

/* Return cand if it exists (non-NULL) and is strictly cheaper than best;
 * ties keep the earlier candidate. */
static E *pick(E *best, E *cand) {
    if (!cand) return best;
    return cost(cand) < cost(best) ? cand : best;
}

/*
 * try_ -- run the rewriting f(e) under a local error handler.
 * Returns f's result, or NULL if f raised an error (cas_throw/longjmp)
 * other than CAS_ERR_INTERRUPTED / CAS_ERR_MEMORY, which are rethrown to
 * the caller's handler.  r is volatile because it is assigned between
 * setjmp and a possible longjmp.  Scratch allocated by a failed f is not
 * released.
 */
static E *try_(E *(*f)(E *), E *e) {
    tryf tf;
    try_enter(&tf);
    E *volatile r = NULL;
    if (!setjmp(tf.jb)) {
        r = f(e);
        try_leave(&tf);
    } else {
        try_leave(&tf);
        if (K->err == CAS_ERR_INTERRUPTED || K->err == CAS_ERR_MEMORY) rethrow();
        r = NULL;
    }
    return r;
}

static E *euler_pass(E *e);

/* First (pre-order) subexpression of the form u^(1/2), i.e. sqrt(u), or
 * NULL.  Note that only the exponent exactly 1/2 matches (not -1/2, 3/2). */
static E *find_sqrt(E *e) {
    stack_check();
    if (e->t == E_POW && K1(e)->t == E_RAT && is_int_val(K1(K1(e)), 2) && is_int_val(K0(K1(e)), 1)) return e;
    if (is_leaf(e)) return NULL;
    for (uint32_t i = 0; i < e->n; i++) {
        E *r = find_sqrt(EK(e)[i]);
        if (r) return r;
    }
    return NULL;
}

/*
 * rationalize -- multiply by conjugates to clear square roots from the
 * denominator.  With e = n/d and k = sqrt(u) occurring in d, if d is
 * linear in k, d = a + b k, then
 *     n/d = n (a - b k) / (a^2 - b^2 u)
 * and expanding the new denominator eliminates k (k^2 auto-simplifies to
 * u).  Repeated for at most 4 radicals; stops at the first square root in
 * which d is not linear.  Returns the rational normal form of the result;
 * errors (e.g. from expand) propagate to the caller's try_().
 */
static E *rationalize(E *e) {
    E *n, *d;
    numden(e, &n, &d);
    for (int it = 0; it < 4; it++) {
        E *k = find_sqrt(d);
        if (!k) break;
        long deg;
        if (!poly_degree_in(d, k, &deg) || deg != 1) break;
        E *a = poly_coeff(d, k, 0), *b = poly_coeff(d, k, 1);
        E *conj = s_sub(a, s_mul2(b, k));
        n = expand(s_mul2(n, conj));
        d = expand(s_mul2(d, conj));
    }
    return rat_normal(s_div(n, d));
}

/* sqrt(a + b sqrt(c)) = sqrt((a+d)/2) + sign(b) sqrt((a-d)/2) when
 * a^2 - b^2 c = d^2 is a rational square (a, b, c rational)
 *
 * (Classical identity: square the right-hand side.)  base is the radicand
 * a + b*c^(1/2); in canonical order the number a is the first operand of
 * the two-term sum.  Both (a+d)/2 and (a-d)/2 must be >= 0 so the real
 * square roots are valid; then (a+d)/2 >= (a-d)/2, so for b < 0 the
 * difference s1 - s2 is still >= 0, as a principal square root must be.  Returns the denested sqrt (not the radicand), or NULL if the
 * pattern does not match or d is irrational. */
static E *denest1(E *base) {
    if (base->t != E_ADD || base->n != 2) return NULL;
    E *a = EK(base)[0], *t = EK(base)[1];
    if (!is_exactnum(a)) return NULL;
    E *b = K->c1, *r = t;
    if (t->t == E_MUL && t->n == 2 && is_exactnum(K0(t))) {
        b = K0(t);
        r = K1(t);
    }
    if (!(r->t == E_POW && is_exactnum(K0(r)) && K1(r)->t == E_RAT && is_int_val(K0(K1(r)), 1) &&
          is_int_val(K1(K1(r)), 2)))
        return NULL;
    E *c = K0(r);
    E *d2 = num_sub(num_mul(a, a), num_mul(num_mul(b, b), c));
    if (num_sgn(d2) < 0) return NULL;
    E *d = s_sqrt(d2);
    if (!is_exactnum(d)) return NULL;
    E *p1 = num_div(num_add(a, d), K->c2), *p2 = num_div(num_sub(a, d), K->c2);
    if (num_sgn(p1) < 0 || num_sgn(p2) < 0) return NULL;
    E *s1 = s_sqrt(p1), *s2 = s_sqrt(p2);
    return num_sgn(b) > 0 ? s_add2(s1, s2) : s_sub(s1, s2);
}

/*
 * denest -- bottom-up rewrite of every u^(p/2) whose radicand u denests
 * with denest1(): u^(p/2) = (sqrt u)^p is replaced by expand(dn^p) for
 * |p| < 20.  Unchanged subtrees are shared (rebuild only when a child
 * changed).  Returns e itself when nothing applies.
 */
static E *denest(E *e) {
    if (is_leaf(e)) return e;
    stack_check();
    E **k = salloc(sizeof(E *) * (e->n + 1));
    int ch = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        k[i] = denest(EK(e)[i]);
        if (k[i] != EK(e)[i]) ch = 1;
    }
    E *r = ch ? rebuild(e, k) : e;
    if (r->t == E_POW && K1(r)->t == E_RAT && is_int_val(K1(K1(r)), 2)) {
        E *dn = denest1(K0(r));
        if (dn) {
            long p;
            if (num_is_small_int(K0(K1(r)), &p) && p > -20 && p < 20) return expand(s_powi(dn, p));
        }
    }
    return r;
}

/* 1 if e contains u^(p/2) with a sum u as radicand (a denesting
 * candidate); cheap pre-test so denest() only runs when useful. */
static int has_nested_sqrt(E *e) {
    stack_check();
    if (e->t == E_POW && K1(e)->t == E_RAT && is_int_val(K1(K1(e)), 2) && K0(e)->t == E_ADD) return 1;
    if (is_leaf(e)) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_nested_sqrt(EK(e)[i])) return 1;
    return 0;
}

/*
 * simplify_full -- user-level simplify (see the file header).
 *   e: canonical expression.  Lists are simplified element-wise and
 *      equations side by side; atoms are returned unchanged.
 * Returns the cheapest candidate according to cost(); never NULL (worst
 * case e itself, possibly denested).  Candidate passes that fail are
 * skipped; interrupts and out-of-memory propagate via cas_throw/longjmp.
 * Complexity is dominated by rat_normal/factor_expr/trig_simplify, i.e.
 * polynomial gcd and factorisation on the numerator and denominator.
 */
static E *hook_pre(E *e) { return casx_hook(CASX_HOOK_SIMPLIFY_PRE, e); }
static E *hook_cand(E *e) { return casx_hook(CASX_HOOK_SIMPLIFY_CAND, e); }

E *simplify_full(E *e) {
    stack_check();
    if (e->t == E_LIST) {
        E **a = salloc(sizeof(E *) * (e->n + 1));
        for (uint32_t i = 0; i < e->n; i++) a[i] = simplify_full(EK(e)[i]);
        return s_list(e->n, a);
    }
    if (e->t == E_FN && e->s == F_EQ && e->n == 2) return s_fn2(F_EQ, simplify_full(K0(e)), simplify_full(K1(e)));
    if (is_leaf(e)) return e;
    /* denesting is applied unconditionally (not cost-ranked): the denested
     * form becomes the starting point for all other candidates */
    if (has_nested_sqrt(e)) {
        E *d = try_(denest, e);
        if (d && !expr_eq(d, e)) e = d;
    }
    /* extension rewrites (casx.c: the advanced module's radical denesting, ...) */
    {
        E *h = try_(hook_pre, e);
        if (h) e = h;
    }
    /* n = rational normal form; it is also the input of the trig and
     * factorisation candidates below */
    E *best = e;
    E *n = try_(rat_normal, e);
    best = pick(best, n);
    if (has_trig(e)) {
        best = pick(best, try_(trig_simplify, e));
        if (n) best = pick(best, try_(trig_simplify, n));
    }
    if (n && !is_num(n)) {
        E *f = try_(factor_expr, n);
        best = pick(best, f);
    }
    best = pick(best, try_(ln_collect_pos, best));
    /* a rationalised denominator is preferred even if slightly larger
     * (cost up to best + 2) */
    if (find_sqrt(e)) {
        E *r = try_(rationalize, best);
        if (r && cost(r) <= cost(best) + 2) best = r;
    }
    if (has_hyp_fn(e)) best = pick(best, try_(hyp_via_exp, e));
    best = pick(best, try_(hook_cand, e));
    return pick(best, try_(euler_pass, e));
}

/* ------------------------------------------------------ autosimplify --- */
/* Size guard of simplify_auto: larger inputs (cost) are returned as they
 * are, so the pass stays cheap on the calculator. */
#define AUTO_MAX_COST 600
/* Largest |k| of a power (sum)^k that simplify_auto expands through
 * rat_normal: (x+y+z)^30 would be 496 terms only to be rejected. */
#define AUTO_MAX_POW 12

/* 1 if the cheap pass must leave e alone: floats (rat_normal over
 * approximate coefficients is not exact), unit and hidden symbols (_m,
 * names starting with '_'), uncertain values pm(v, s), strings, and powers
 * of sums with an exponent beyond AUTO_MAX_POW. */
static int auto_skip(E *e) {
    stack_check();
    switch (e->t) {
    case E_FLT: case E_STR: return 1;
    case E_INT: case E_RAT: return 0;
    case E_SYM: return sym_name(e->s)[0] == '_';
    default: break;
    }
    if (e->t == E_FN && e->s >= S__COUNT && !strcmp(sym_name(e->s), "pm")) return 1;
    if (e->t == E_POW && K0(e)->t == E_ADD) {
        long k;
        if (!num_is_small_int(K1(e), &k) || k > AUTO_MAX_POW || k < -AUTO_MAX_POW) return 1;
    }
    for (uint32_t i = 0; i < e->n; i++)
        if (auto_skip(EK(e)[i])) return 1;
    return 0;
}

/* 1 if e contains a symbol or a function call, i.e. something the
 * rewritings can act on (numbers are already in canonical form). */
static int auto_has_var(E *e) {
    stack_check();
    if (e->t == E_SYM || e->t == E_FN) return 1;
    if (is_leaf(e)) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (auto_has_var(EK(e)[i])) return 1;
    return 0;
}

/* Syntactic size of the denominator of e: the cost of the bases of the
 * factors with a negative numeric exponent (x/(x-1): cost(x-1)); 0 for no
 * denominator.  Cheap (no rational conversion, unlike numden). */
static size_t den_cost(E *e) {
    stack_check();
    if (e->t == E_POW && is_num(K1(e)) && num_sgn(K1(e)) < 0) return cost(K0(e));
    if (e->t != E_MUL) return 0;
    size_t c = 0;
    for (uint32_t i = 0; i < e->n; i++) c += den_cost(EK(e)[i]);
    return c;
}

/* 1 if e is a sum of which at least one term has a denominator: rational
 * normalisation then combines fractions (1/(x-1)-1/(x+1)). */
static int sum_of_fractions(E *e) {
    if (e->t != E_ADD) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (den_cost(EK(e)[i])) return 1;
    return 0;
}

/* Number of occurrences of circular trig functions in e (with
 * multiplicity, so cos(x)^2+sin(x)^2 has 2). */
static size_t trig_count(E *e) {
    stack_check();
    size_t c = e->t == E_FN && (e->s == F_SIN || e->s == F_COS || e->s == F_TAN || e->s == F_COT ||
                                e->s == F_SEC || e->s == F_CSC);
    if (!is_leaf(e))
        for (uint32_t i = 0; i < e->n; i++) c += trig_count(EK(e)[i]);
    return c;
}

/* ------------------------------------------------------- Euler pass --- */
/* Euler pass (simplify_auto, simplify_full): e^(i u) next to e^(-i u) or to a
 * trig function of u (and e^u next to e^-u or to sinh/cosh/tanh of u) is
 * rewritten exp -> trig (exp_to_trig; e^u -> cosh u + sinh u) or
 * trig -> exp (trig_to_exp), each then tidied by rat_normal and trig_pyth.
 * Only the exponentials and functions of such matched arguments ("keys",
 * eu_key) are converted: a lone e^(i x) or cos(x) + i sin(x) keeps its form.
 * Family 1: circular (key u of e^(i u); none in degree mode, where the new
 * cos(u) would print as degrees), family 2: hyperbolic (key u of e^u). */
#define EU_MAX 16
static E *eu_k[EU_MAX];     /* matched keys */
static int eu_f[EU_MAX], eu_n, eu_dir; /* their families; count; 0 exp->trig, 1 trig->exp */

/* family of a function node: 1 circular, 2 hyperbolic, 0 other */
static int eu_fam(E *e) {
    if (e->t != E_FN || e->n != 1) return 0;
    if (e->s == F_SINH || e->s == F_COSH || e->s == F_TANH) return 2;
    return e->s == F_SIN || e->s == F_COS || e->s == F_TAN || e->s == F_COT || e->s == F_SEC || e->s == F_CSC;
}

/* key of the exponent u of e^u (family in *f), or NULL: u = i*v with v free
 * of i (circular), or u free of i with a variable (hyperbolic). */
static E *eu_key(E *u, int *f) {
    if (freeof_sym(u, S_I)) return *f = 2, auto_has_var(u) ? u : NULL;
    if (K->set.angle == CAS_ANGLE_DEG) return NULL;
    E *v = expand(s_mul2(u, s_neg(K->i_)));
    return *f = 1, freeof_sym(v, S_I) ? v : NULL;
}

/* 1 if a is +-k for a key k of family f in list ks[0..n) */
static int eu_in(E *a, int f, E **ks, int *fs, int n) {
    for (int j = 0; j < n; j++)
        if (fs[j] == f && (expr_eq(a, ks[j]) || is_zero(s_add2(a, ks[j])))) return 1;
    return 0;
}

/* collect exponent keys (x) and function arguments (t) of e */
static void eu_scan(E *e, E **x, int *xf, int *nx, E **t, int *tf, int *nt) {
    stack_check();
    if (is_leaf(e)) return;
    int f;
    if (e->t == E_POW && ISYM(K0(e), S_E)) {
        E *k = eu_key(K1(e), &f);
        if (k && *nx < EU_MAX) x[*nx] = k, xf[(*nx)++] = f;
    } else if ((f = eu_fam(e)) && *nt < EU_MAX && (f == 2 || K->set.angle != CAS_ANGLE_DEG))
        t[*nt] = K0(e), tf[(*nt)++] = f;
    for (uint32_t i = 0; i < e->n; i++) eu_scan(EK(e)[i], x, xf, nx, t, tf, nt);
}

/* rewrite the exponentials (eu_dir 0) or functions (1) of matched keys */
static E *eu_rw(E *e) {
    stack_check();
    if (is_leaf(e)) return e;
    E **k = salloc(sizeof(E *) * (e->n + 1));
    int ch = 0, f;
    for (uint32_t i = 0; i < e->n; i++) ch |= (k[i] = eu_rw(EK(e)[i])) != EK(e)[i];
    if (ch) e = rebuild(e, k);
    if (!eu_dir && e->t == E_POW && ISYM(K0(e), S_E)) {
        E *u = K1(e), *key = eu_key(u, &f);
        if (key && eu_in(key, f, eu_k, eu_f, eu_n))
            return f == 1 ? exp_to_trig(e) : s_add2(s_fn1(F_COSH, u), s_fn1(F_SINH, u));
    }
    if (eu_dir && (f = eu_fam(e)) && eu_in(K0(e), f, eu_k, eu_f, eu_n)) return trig_to_exp(e);
    return e;
}

/* one direction of the pass, then rat_normal / trig_pyth when smaller */
static E *eu_cand(E *e) {
    E *c = eu_rw(e), *r = rat_normal(c);
    if (cost(r) < cost(c)) c = r;
    return has_trig(c) ? trig_pyth(c) : c;
}

/* Euler pass: best of e and both directions, strictly cheaper only; a
 * result with i is refused when e has none. */
static E *euler_pass(E *e) {
    E *x[EU_MAX], *t[EU_MAX];
    int xf[EU_MAX], tf[EU_MAX], nx = 0, nt = 0;
    eu_scan(e, x, xf, &nx, t, tf, &nt);
    eu_n = 0;
    for (int j = 0; j < nx; j++) {
        int m = eu_in(x[j], xf[j], t, tf, nt);
        for (int l = 0; l < nx && !m; l++) m = xf[l] == xf[j] && is_zero(s_add2(x[j], x[l]));
        if (m) eu_k[eu_n] = x[j], eu_f[eu_n++] = xf[j];
    }
    E *best = e;
    for (eu_dir = 0; eu_n && eu_dir < 2; eu_dir++) {
        E *c = try_(eu_cand, e);
        if (c && cost(c) < cost(best) && (freeof_sym(c, S_I) || !freeof_sym(e, S_I))) best = c;
    }
    return best;
}

/*
 * simplify_auto -- the cheap "auto" simplification of displayed results
 * (autosimplify command; the calculator applies it to home/RPN results in
 * MODE SIMPLIFY AUTO, never inside graphing, the solver or compiled code).
 * It must not reshape what the user wrote or asked for ((x-1)*(x+1) is
 * cheaper expanded, but stays), so each candidate has its own acceptance
 * rule on top of being strictly cheaper (cost) than e:
 *   - the rational normal form n (combines like terms across products,
 *     cancels common factors, combines fractions) is taken if it is at most
 *     half the cost of e (x*(x+1)-x^2 -> x, (a+b)^2-(a-b)^2 -> 4*a*b), or
 *     its denominator is smaller (a common factor cancelled: (x^2-1)/(x-1)
 *     -> x+1), or e is a sum of fractions (1/(x-1)-1/(x+1) -> 2/(x^2-1));
 *   - the Pythagorean pass trig_pyth (of e and of n) is taken if it has
 *     fewer trig function occurrences than e (sin(x)^2+cos(x)^2 -> 1,
 *     cos(x)^4-sin(x)^4 -> 2*cos(x)^2-1, sin(x)^2/(1-cos(x)) -> cos(x)+1),
 *     so a factored trig polynomial is not expanded.
 *   - the Euler pass euler_pass (exp <-> trig between matched
 *     exponentials and functions of one argument) if strictly cheaper:
 *     (e^(ix)+e^(-ix))/2 -> cos(x), (e^x+e^-x)/2 -> cosh(x); a lone
 *     e^(ix) or cos(x)+i sin(x) has no match and stays.
 * No factoring, no expansion of angles, no logarithm rewriting.  Lists and equations are processed element-wise.  Inputs that
 * auto_skip rejects, have no symbols, or cost more than AUTO_MAX_COST come
 * back unchanged, as do candidates that raise an ordinary error.
 * Interrupts (the calculator's time budget) and out-of-memory propagate.
 */
E *simplify_auto(E *e) {
    stack_check();
    if (e->t == E_LIST) {
        E **a = salloc(sizeof(E *) * (e->n + 1));
        for (uint32_t i = 0; i < e->n; i++) a[i] = simplify_auto(EK(e)[i]);
        return s_list(e->n, a);
    }
    if (e->t == E_FN && e->s == F_EQ && e->n == 2) return s_fn2(F_EQ, simplify_auto(K0(e)), simplify_auto(K1(e)));
    if (is_leaf(e) || !auto_has_var(e) || auto_skip(e)) return e;
    size_t ce = cost(e);
    if (ce > AUTO_MAX_COST) return e;
    E *best = e;
    E *n = try_(rat_normal, e);
    if (n) {
        size_t cn = cost(n);
        if (cn < ce && (2 * cn <= ce || den_cost(n) < den_cost(e) || sum_of_fractions(e))) best = n;
    }
    if (has_trig(e)) {
        size_t te = trig_count(e);
        E *c[2] = {try_(trig_pyth, e), n && n != e ? try_(trig_pyth, n) : NULL};
        for (int i = 0; i < 2; i++)
            if (c[i] && trig_count(c[i]) < te && cost(c[i]) < cost(best)) best = c[i];
    }
    return pick(best, try_(euler_pass, e));
}

/* forms.c (alternate forms): the cost measure and the rationalised
 * denominator, for ranking and as one of its rewritings. */
size_t simplify_cost(E *e) { return cost(e); }
E *simplify_rationalize(E *e) { return rationalize(e); }
