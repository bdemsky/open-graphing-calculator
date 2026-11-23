/*
 * diff.c -- symbolic differentiation (sum/product/power/chain rules and a
 * table of elementary derivatives).  Derivatives of unknown functions stay
 * as diff(f(u), x) nodes.
 *
 * Role in the CAS
 * ---------------
 * diff() is the classic recursive derivative operator on the expression
 * tree (cf. Cohen, "Computer Algebra and Symbolic Computation: Elementary
 * Algorithms", the Derivative procedure).  It is a leaf service used by many
 * modules:
 *   - cmds.c: the diff / tangent / grad / hessian / jacobian / divergence /
 *     curl / implicitdiff commands and f'(a) notation;
 *   - integ.c, heurisch.c: derivative-divides substitution, integration by
 *     parts, verifying ansatz candidates;
 *   - series.c: Taylor coefficients of functions without a built-in
 *     expansion, and l'Hopital's rule in the limit calculus;
 *   - ode.c, solve.c, sum.c, linalg.c, xcmds.c: assorted uses.
 *
 * Design
 * ------
 *  - The input must already be in canonical (auto-simplified) form; every
 *    result is rebuilt through the s_* constructors (s_add, s_mul2, s_pow,
 *    ...), so the output is again canonical: like terms are merged and
 *    zero summands vanish without a separate simplification pass.
 *  - x is any expression, not only a symbol: "freeof(e, x)" and
 *    "expr_eq(e, x)" are the base cases, so differentiating with respect to
 *    a kernel such as sin(t) works in the obvious structural sense.
 *  - Products use the n-ary Leibniz rule (one term per factor that depends
 *    on x); powers distinguish the constant-exponent, constant-base and
 *    general u^v cases; unary functions use a derivative table times the
 *    chain-rule factor du/dx.
 *  - Functions without a table entry (user functions, gamma, zeta,
 *    re/im/conj, multi-argument functions other than atan2 and two-argument
 *    log) produce the inert node diff(f(...), x), flagged F_SIMP so that
 *    simp() treats it as already canonical and leaves it untouched.
 *  - Piecewise-constant functions (sign, floor, ceil, round) differentiate
 *    to 0 and frac to 1: these are the derivatives almost everywhere; the
 *    Dirac deltas at the jumps are deliberately not produced (only
 *    heaviside -> dirac is).
 *
 * Memory/errors: all nodes are allocated in scratch; nothing is persistent.
 * Recursion depth is guarded with stack_check() (raises CAS_ERR_MEMORY via
 * cas_throw/longjmp on deep inputs).  The only other error is
 * CAS_ERR_UNSUPPORTED for node kinds that cannot be differentiated.
 */
#include "cas_int.h"
#include "casx.h"

E *rebuild(E *e, E **k);

static E *dfn(E *e, E *x);

/*
 * diff -- d e / d x.
 *   e: canonical expression (a list is differentiated element-wise; an
 *      equation a=b becomes a'=b' via dfn).
 *   x: the differentiation "variable" (usually a symbol, compared
 *      structurally, so any kernel works).
 * Returns a canonical expression in scratch memory.  Never returns NULL.
 * Errors: stack_check() may throw CAS_ERR_MEMORY on very deep trees;
 * CAS_ERR_UNSUPPORTED for node kinds with no derivative (unreachable for
 * well-formed canonical input, since numbers/symbols/strings are caught by
 * the freeof test).  Complexity is linear in the tree size per
 * differentiation level, but products of n factors create n terms, so
 * repeated differentiation (diffn) can grow the result quickly.
 */
E *diff(E *e, E *x) {
    stack_check();
    /* Base cases (a list, even one free of x, is differentiated element-wise
     * below, giving a list of zeros). */
    if (e->t != E_LIST && freeof(e, x)) return K->c0;
    if (expr_eq(e, x)) return K->c1;
    switch (e->t) {
    case E_ADD: {
        /* linearity: (sum u_i)' = sum u_i' */
        E **t = salloc(sizeof(E *) * e->n);
        for (uint32_t i = 0; i < e->n; i++) t[i] = diff(EK(e)[i], x);
        return s_add(e->n, t);
    }
    case E_MUL: {
        /* n-ary Leibniz rule: (prod u_j)' = sum_i u_i' * prod_{j != i} u_j.
         * Factors with zero derivative contribute no term; each term is
         * a fresh n-factor product in which factor i is replaced by u_i'. */
        E **t = salloc(sizeof(E *) * e->n);
        uint32_t nt = 0;
        for (uint32_t i = 0; i < e->n; i++) {
            E *d = diff(EK(e)[i], x);
            if (is_zero(d)) continue;
            E **f = salloc(sizeof(E *) * e->n);
            for (uint32_t j = 0; j < e->n; j++) f[j] = j == i ? d : EK(e)[j];
            t[nt++] = s_mul(e->n, f);
        }
        return s_add(nt, t);
    }
    case E_POW: {
        E *b = K0(e), *n = K1(e);
        /* b^n with constant n:  n b^(n-1) b'
         * b^n with constant b:  b^n ln(b) n'
         * general u^v = exp(v ln u):  u^v (v' ln u + v u'/u) */
        if (freeof(n, x)) return s_mul2(s_mul2(n, s_pow(b, s_sub(n, K->c1))), diff(b, x));
        if (freeof(b, x)) return s_mul2(s_mul2(e, s_ln(b)), diff(n, x));
        E *t1 = s_mul2(diff(n, x), s_ln(b));
        E *t2 = s_mul2(n, s_div(diff(b, x), b));
        return s_mul2(e, s_add2(t1, t2));
    }
    case E_LIST: {
        /* element-wise (vectors, matrices as lists of rows) */
        E **t = salloc(sizeof(E *) * (e->n + 1));
        for (uint32_t i = 0; i < e->n; i++) t[i] = diff(EK(e)[i], x);
        return s_list(e->n, t);
    }
    case E_FN: return dfn(e, x);
    default: break;
    }
    cas_throw(CAS_ERR_UNSUPPORTED, "cannot differentiate");
}

/* Inert derivative node diff(e, x), marked canonical so simp() keeps it. */
static E *inert_diff(E *e, E *x) {
    E *a[2] = {e, x};
    E *r = mk_fn(C_DIFF, 2, a);
    r->f = F_SIMP;
    return r;
}

/* u^2 (canonical). */
static E *sq(E *u) { return s_powi(u, 2); }

/*
 * dfn -- derivative of a function application e = f(args) (E_FN node).
 * Handles first the special shapes:
 *   - equations a = b              -> a' = b'
 *   - atan2(y, x)                  -> (x y' - y x') / (x^2 + y^2)
 *   - two-argument ln/log(u, b)    -> rewritten as ln(u)/ln(b) and
 *                                     differentiated (b may depend on x)
 *   - any other multi-argument fn  -> inert diff(f(...), x)
 * then unary functions by the table below times the chain-rule factor u'.
 * Returns 0 if u' = 0 (the argument is constant in x), the inert node for
 * functions without a table entry.  Table formulas are the standard ones
 * (Abramowitz & Stegun / DLMF ch. 4 for the elementary functions, DLMF
 * 7.10 for erf/erfc/erfi, the integral definitions in DLMF 6.2 for
 * Si/Ci/Ei).
 * Error behaviour as for diff().
 */
static E *dfn(E *e, E *x) {
    int id = e->s;
    if (id == F_EQ && e->n == 2) return s_fn2(F_EQ, diff(K0(e), x), diff(K1(e), x));
    if (e->n == 2 && id == F_ATAN2) {
        E *y = K0(e), *xx = K1(e);
        E *num = s_sub(s_mul2(xx, diff(y, x)), s_mul2(y, diff(xx, x)));
        return s_div(num, s_add2(sq(xx), sq(y)));
    }
    if (e->n == 2 && (id == F_LN || id == F_LOG)) {
        E *v = s_div(s_ln(K0(e)), s_ln(K1(e)));
        return diff(v, x);
    }
    if (id == F_PIECEWISE && e->n >= 2) {
        /* piecewise(c1, v1, ..., [else]): the pieces' derivatives, almost everywhere (pieces on
         * an equation c = d hold on a null set and are dropped) */
        E **k = salloc(sizeof(E *) * e->n);
        uint32_t m = 0;
        for (uint32_t i = 0; i < e->n; i++) {
            if (!(i & 1) && i + 1 < e->n) {
                if (IFN(EK(e)[i], F_EQ)) {
                    i++;
                    continue;
                }
                k[m++] = EK(e)[i];
            } else
                k[m++] = diff(EK(e)[i], x);
        }
        if (m == 1) return k[0];
        if (!m) return inert_diff(e, x);
        E *r = mk_fn(F_PIECEWISE, m, k);
        return simp(r);
    }
    if (CASX_SLOTS && id >= S__COUNT) { /* functions of the casx module (cas/ext/int/fnx.c) */
        E *v[2] = {e, x}, *r = casx_eslot(CASX_FN_DIFF, 2, v);
        if (r) return r;
    }
    /* Leibniz: d/dx int(F(y), y, a, b) = F(b) b' - F(a) a' (F free of x) */
    if ((id == C_INTEGRATE || id == C_INT) && e->n == 4 && K1(e)->t == E_SYM && freeof(K0(e), x) &&
        !expr_eq(K1(e), x)) {
        E *F = K0(e), *y = K1(e), *a = EK(e)[2], *b = EK(e)[3];
        return s_sub(s_mul2(subst(F, y, b), diff(b, x)), s_mul2(subst(F, y, a), diff(a, x)));
    }
    /* f^(k)(u) of an unknown f (derivative(f, k, u), printed f'(u)): f^(k+1)(u) u' */
    if (id == F_DERIV && e->n == 3 && K0(e)->t == E_SYM && K1(e)->t == E_INT) {
        E *du = diff(EK(e)[2], x);
        if (is_zero(du)) return K->c0;
        E *v[3] = {K0(e), s_add2(K1(e), K->c1), EK(e)[2]};
        return s_mul2(s_fn(F_DERIV, 3, v), du);
    }
    /* nested inert derivatives and all other multi-argument functions stay
     * unevaluated */
    if (e->n != 1) return inert_diff(e, x);
    E *u = K0(e);
    E *du = diff(u, x);
    if (is_zero(du)) return K->c0;
    /* d = f'(u); the result is d * u' (chain rule).  d == NULL means "no
     * table entry": return the inert derivative instead. */
    E *d;
    switch (id) {
    case F_SIN: d = s_fn1(F_COS, u); break;
    case F_COS: d = s_neg(s_fn1(F_SIN, u)); break;
    case F_TAN: d = s_add2(K->c1, sq(s_fn1(F_TAN, u))); break;
    case F_COT: d = s_neg(s_add2(K->c1, sq(s_fn1(F_COT, u)))); break;
    case F_SEC: d = s_mul2(s_fn1(F_SEC, u), s_fn1(F_TAN, u)); break;
    case F_CSC: d = s_neg(s_mul2(s_fn1(F_CSC, u), s_fn1(F_COT, u))); break;
    case F_ASIN: d = s_pow(s_sub(K->c1, sq(u)), K->cmhalf); break;
    case F_ACOS: d = s_neg(s_pow(s_sub(K->c1, sq(u)), K->cmhalf)); break;
    case F_ATAN: d = s_inv(s_add2(K->c1, sq(u))); break;
    case F_ACOT: d = s_neg(s_inv(s_add2(K->c1, sq(u)))); break;
    /* asec/acsc: 1/(u^2 sqrt(1 - 1/u^2)), which equals 1/(|u| sqrt(u^2 - 1))
     * for real |u| > 1 but needs no abs(). */
    case F_ASEC: d = s_inv(s_mul2(sq(u), s_sqrt(s_sub(K->c1, s_inv(sq(u)))))); break;
    case F_ACSC: d = s_neg(s_inv(s_mul2(sq(u), s_sqrt(s_sub(K->c1, s_inv(sq(u))))))); break;
    case F_SINH: d = s_fn1(F_COSH, u); break;
    case F_COSH: d = s_fn1(F_SINH, u); break;
    case F_TANH: d = s_sub(K->c1, sq(s_fn1(F_TANH, u))); break;
    case F_ASINH: d = s_pow(s_add2(sq(u), K->c1), K->cmhalf); break;
    case F_ACOSH: d = s_pow(s_sub(sq(u), K->c1), K->cmhalf); break;
    case F_ATANH: d = s_inv(s_sub(K->c1, sq(u))); break;
    case F_LN: d = s_inv(u); break;
    /* |u|' = sign(u) (real u, u != 0) */
    case F_ABS: d = s_fn1(F_SIGN, u); break;
    /* step functions: derivative almost everywhere (no Dirac terms) */
    case F_SIGN: case F_FLOOR: case F_CEIL: case F_ROUND: d = K->c0; break;
    case F_FRAC: d = K->c1; break;
    case F_ERF: d = s_mul2(s_div(K->c2, s_sqrt(K->pi_)), s_exp(s_neg(sq(u)))); break;
    case F_HEAVISIDE: d = s_fn1(F_DIRAC, u); break;
    case F_SI: d = s_div(s_fn1(F_SIN, u), u); break;
    case F_CI: d = s_div(s_fn1(F_COS, u), u); break;
    case F_EI: d = s_div(s_exp(u), u); break;
    case F_ERFC: d = s_neg(s_mul2(s_div(K->c2, s_sqrt(K->pi_)), s_exp(s_neg(sq(u))))); break;
    case F_ERFI: d = s_mul2(s_div(K->c2, s_sqrt(K->pi_)), s_exp(sq(u))); break;
    case F_SQRT: d = s_mul2(K->chalf, s_pow(u, K->cmhalf)); break;
    case F_EXP: d = s_exp(u); break;
    /* everything else, including re/im/conj (not complex-differentiable):
     * keep inert */
    default:
        /* an unknown function of an expression u != x: the chain rule f'(u) u' */
        d = NULL;
        if (id >= S__COUNT && !expr_eq(u, x)) {
            E *v[3] = {mk_sym(id), K->c1, u};
            d = s_fn(F_DERIV, 3, v);
        }
        break;
    }
    if (!d) return inert_diff(e, x);
    return s_mul2(d, du);
}

/*
 * diffn -- n-th derivative d^n e / d x^n by repeated diff(); stops early
 * once the result is 0.  n <= 0 returns e unchanged.  Same memory and
 * error behaviour as diff().
 */
E *diffn(E *e, E *x, long n) {
    for (long i = 0; i < n; i++) {
        e = diff(e, x);
        if (is_zero(e)) break;
    }
    return e;
}
