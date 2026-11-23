/*
 * integ.c -- symbolic integration (indefinite and definite) and numeric quadrature.
 *
 * Entry points (declared in cas_int.h unless noted):
 *   integrate(f, x)          antiderivative of f with respect to the symbol x, or NULL when
 *                            nothing was found.  Called by the integrate/int command
 *                            (cmds.c), by dsolve (ode.c) and by integrate_def.
 *   integrate_def(f, x, a, b) definite integral: antiderivative evaluated at the endpoints
 *                            (one-sided limits where needed), cross-checked against numeric
 *                            quadrature, numeric fallback.  Used by cmds.c and by the
 *                            fourier_an/bn commands (xcmds.c).
 *   nint_double, nintegrate  (not in cas_int.h) double-precision adaptive Gauss-Kronrod
 *                            quadrature, also over infinite ranges; nintegrate is the nint
 *                            command (cmds.c).
 *
 * Search strategy.  There is no Risch decision procedure.  ir() is a recursive heuristic
 * driver that tries, cheapest first:
 *    1. f free of x                -> f*x
 *    2. table()                    elementary forms with a linear inner argument
 *    3. P(x) e^(g(x))              polynomial R with R' + g'R = P (int_exp_poly);
 *                                  x^k e^(c x^2) by a reduction formula (int_xk_gauss)
 *    4. linearity                  sums termwise, x-free factors pulled out
 *    5. rational functions         polynomial part, Hermite reduction (Mack's linear
 *                                  version), partial fractions over the factors over Q:
 *                                  ln / atan terms for degrees 1-2, quartics split into real
 *                                  quadratics via the resolvent cubic, Rothstein-Trager
 *                                  for other irreducible factors (int_rational)
 *    6. integration by parts       LIATE choice of u, tabular method (parts)
 *    7. derivative-divides         substitution u = candidate subexpression (deriv_divides)
 *    8. trigonometric integrands   linearisation, substitution, Weierstrass t = tan(u/2)
 *    9. sqrt(quadratic)            P/sqrt(Q) by undetermined coefficients, Euler substitutions
 *   10. expansion                  expand(f) and retry
 *   11. inverse substitutions      x = ln(T)/a, radicals of linear functions and of
 *                                  exponentials, x = T^m (inverse_subs)
 *   12. c^u                        rewritten as e^(u ln c)
 *   13. exp * trig, hyperbolics    rewritten with (complex) exponentials
 *   14. heurisch()                 parallel-Risch ansatz (heurisch.c), top levels only
 * Strategies call ir() recursively on the rewritten integrand.  Termination is ensured by
 * a depth limit (8) and a global call budget g_budget (400 ir() calls per integrate()).
 * Each recursion depth uses its own substitution variable _t<depth> (tsym), so nested
 * substitutions do not capture each other.  Failed strategies release their scratch
 * (TRY_STRAT: smark/srelease); results are left in scratch (no skeep).
 *
 * Correctness and conventions.  Every strategy applies an exact identity, so a returned
 * antiderivative is correct (the test suite verifies by differentiation; nothing is
 * re-checked at run time).  Results are "generic" antiderivatives in the usual CAS sense:
 * ln(u) instead of ln|u|, symbolic parameters treated as positive / generic (sqrt_pos,
 * atan branch of int_inv_quad), and possibly discontinuous forms (Weierstrass), which is
 * why integrate_def cross-checks numerically.
 *
 * Errors.  "Not found" is signalled by NULL.  Errors raised inside strategies (out of
 * memory, interrupt, recursion depth, and domain/unsupported errors thrown by called
 * modules such as solve, factor or heurisch's size limits) are not caught by ir() and
 * propagate with cas_throw/longjmp to the caller of integrate().
 *
 * Not reentrant: the call budget g_budget is a file-static global.
 *
 * References: M. Bronstein, "Symbolic Integration I", ch. 2 (Hermite reduction incl.
 * Mack's linear version, Rothstein-Trager); Geddes, Czapor, Labahn, "Algorithms for
 * Computer Algebra", ch. 11 (integration of rational functions); Abramowitz & Stegun ch. 5
 * and 7 / DLMF ch. 6-7 (Ei, Si, Ci, erf, erfi in the table); R. Piessens et al.,
 * "QUADPACK" (1983) for the Gauss-Kronrod 7/15 rule and the globally adaptive strategy.
 */
#include "cas_int.h"
#include "upoly.h"
#include "casx.h"
#include <math.h>
#include <string.h>

/* helpers from trig.c, solve.c, heurisch.c and poly.c */
int has_trig(E *e);
E *trig_sincos(E *e);
E *solve_expr(E *eqs, E *vars, int complex_mode);
E *trig_linearize(E *e);
E *trig_expand(E *e);
E *trig_to_exp(E *e);
E *exp_to_trig(E *e);

/* remaining ir() calls for the current integrate(); every ir() call consumes one */
static int g_budget;
static int g_xdepth; /* nesting of the casx integration slot (integrate -> slot -> integrate) */
static int g_split; /* int_rational: split irreducible factors with real_split */
E *heurisch(E *f, E *x);
E *subst_rad(E *e, E *x, E *T, E *Q);
static E *ir(E *f, E *x, int depth);

/*
 * tsym -- substitution variable for recursion depth `depth`: the symbol "_tNN" (two
 * decimal digits, so 0 <= depth <= 99).  Distinct depths get distinct symbols so that a
 * substitution nested inside another one does not clash with the outer variable;
 * int_euler uses depth+30 to stay clear of the names used by other strategies at the
 * same depth.  Side effect: interns the symbol in the symbol table.
 */
static E *tsym(int depth) {
    char nm[8] = "_t";
    nm[2] = (char)('0' + depth / 10);
    nm[3] = (char)('0' + depth % 10);
    nm[4] = 0;
    return mk_sym_name(nm);
}

/*
 * lin -- is u linear in x, u = a*x + b with a != 0 and a, b free of x?  Decided by
 * differentiation: a = du/dx must be nonzero and x-free, b = expand(u - a*x) must be
 * x-free.  Returns 1 and stores a, b (scratch); returns 0 leaving *a, *b untouched.
 * The "linear inner argument" test used by the table and most strategies.
 */
static int lin(E *u, E *x, E **a, E **b) {
    E *d = diff(u, x);
    if (is_zero(d) || !freeof(d, x)) return 0;
    E *bb = expand(s_sub(u, s_mul2(d, x)));
    if (!freeof(bb, x)) return 0;
    *a = d;
    *b = bb;
    return 1;
}

/*
 * quad -- is u a quadratic polynomial a*x^2 + b*x + c in x with x-free coefficients
 * (degree exactly 2, so a != 0)?  Returns 1 with the coefficients.  Note that the outputs
 * are also written when 0 is returned because a coefficient depends on x.
 */
static int quad(E *u, E *x, E **a, E **b, E **c) {
    long d;
    if (!poly_degree_in(u, x, &d) || d != 2) return 0;
    *a = poly_coeff(u, x, 2);
    *b = poly_coeff(u, x, 1);
    *c = poly_coeff(u, x, 0);
    return freeof(*a, x) && freeof(*b, x) && freeof(*c, x);
}

/*
 * is_rational_in -- syntactic test that f is a rational function of x: built from x and
 * x-free subexpressions (arbitrary constants, parameters, sqrt(2), ...) with +, * and
 * integer powers only; x may not occur inside a function or a non-integer power.
 * Gate for int_rational.
 */
static int is_rational_in(E *f, E *x) {
    stack_check();
    if (freeof(f, x)) return 1;
    if (expr_eq(f, x)) return 1;
    switch (f->t) {
    case E_ADD: case E_MUL:
        for (uint32_t i = 0; i < f->n; i++)
            if (!is_rational_in(EK(f)[i], x)) return 0;
        return 1;
    case E_POW: return K1(f)->t == E_INT && is_rational_in(K0(f), x);
    default: return 0;
    }
}

/*
 * is_poly_in -- syntactic test that f is a polynomial in x (sums, products and positive
 * small integer powers of x and x-free subexpressions).  Does not expand.
 */
static int is_poly_in(E *f, E *x) {
    stack_check();
    if (freeof(f, x)) return 1;
    if (expr_eq(f, x)) return 1;
    switch (f->t) {
    case E_ADD: case E_MUL:
        for (uint32_t i = 0; i < f->n; i++)
            if (!is_poly_in(EK(f)[i], x)) return 0;
        return 1;
    case E_POW: {
        long k;
        return num_is_small_int(K1(f), &k) && k > 0 && is_poly_in(K0(f), x);
    }
    default: return 0;
    }
}

/* -------------------------------------------------------------- table --- */
/*
 * table -- direct lookup of elementary antiderivatives; returns the antiderivative
 * (scratch) or NULL.  Never recurses into ir(), so it is cheap; parts() and table_c() also
 * use it as an "is this trivially integrable?" oracle.
 * Recognised forms (u = a*x + b linear with a != 0; results are divided by a = du/dx):
 *   1/ln(x)                       -> Ei(ln x)        (the logarithmic integral li(x))
 *   x                             -> x^2/2
 *   u^n, n free of x              -> u^(n+1)/((n+1) a), or ln(u)/a for n = -1
 *   c^u, c free of x              -> c^u/(a ln c), e^u/a for c = e
 *   e^(q(x)), q quadratic         -> erf/erfi after completing the square (the sign of the
 *                                    leading coefficient must be known)
 *   cos^-2, sin^-2, sec^2, csc^2, tan^2, cot^2, cosh^-2, sinh^-2, 1/sin, 1/cos, 1/tan of u
 *   g(u) for g in sin cos tan cot sec csc sinh cosh tanh ln asin acos atan acot asinh acosh
 *            atanh abs sign erf heaviside dirac
 *   sin(a x)/x -> Si(a x), cos(a x)/x -> Ci(a x), e^(a x)/x -> Ei(a x)  (no constant term)
 *   sec(u) tan(u) -> sec(u)/a,  csc(u) cot(u) -> -csc(u)/a
 * Logarithms are returned without absolute values (ln(u) rather than ln|u|).
 */
static E *table(E *f, E *x) {
    E *a, *b;
    if (f->t == E_POW && is_int_val(K1(f), -1) && IFN(K0(f), F_LN) && expr_eq(K0(K0(f)), x))
        return s_fn1(F_EI, K0(f)); /* li(x) = Ei(ln x) */
    if (expr_eq(f, x)) return s_mul2(K->chalf, s_powi(x, 2));
    if (f->t == E_POW) {
        E *B = K0(f), *N = K1(f);
        if (freeof(N, x) && lin(B, x, &a, &b)) {
            if (is_int_val(N, -1)) return s_div(s_ln(B), a);
            E *n1 = s_add2(N, K->c1);
            return s_div(s_pow(B, n1), s_mul2(n1, a));
        }
        if (freeof(B, x) && lin(N, x, &a, &b)) {
            if (ISYM(B, S_E)) return s_div(f, a);
            return s_div(f, s_mul2(a, s_ln(B)));
        }
        E *qa, *qb, *qc;
        if (ISYM(B, S_E) && quad(N, x, &qa, &qb, &qc)) {
            /* exp(qa x^2 + qb x + qc) = exp(qc - qb^2/(4qa)) exp(qa (x + qb/(2qa))^2) */
            int sa = sign_of(qa);
            if (sa != 0) {
                E *k = s_sqrt(sa < 0 ? s_neg(qa) : qa);
                E *sh = s_add2(x, s_div(qb, s_mul2(K->c2, qa)));
                E *cst = s_exp(rat_normal(s_sub(qc, s_div(s_powi(qb, 2), s_mul2(mk_int(4), qa)))));
                return s_mul2(s_mul2(cst, s_div(s_sqrt(K->pi_), s_mul2(K->c2, k))),
                              s_fn1(sa < 0 ? F_ERF : F_ERFI, s_mul2(k, sh)));
            }
        }
        long k;
        if (B->t == E_FN && B->n == 1 && num_is_small_int(N, &k) && lin(K0(B), x, &a, &b)) {
            E *u = K0(B);
            int id = B->s;
            if (k == -2 && id == F_COS) return s_div(s_fn1(F_TAN, u), a);
            if (k == -2 && id == F_SIN) return s_neg(s_div(s_fn1(F_COT, u), a));
            if (k == 2 && id == F_SEC) return s_div(s_fn1(F_TAN, u), a);
            if (k == 2 && id == F_CSC) return s_neg(s_div(s_fn1(F_COT, u), a));
            if (k == 2 && id == F_TAN) return s_sub(s_div(s_fn1(F_TAN, u), a), x);
            if (k == 2 && id == F_COT) return s_sub(s_neg(s_div(s_fn1(F_COT, u), a)), x);
            if (k == -2 && id == F_COSH) return s_div(s_fn1(F_TANH, u), a);
            if (k == -2 && id == F_SINH) return s_neg(s_div(s_inv(s_fn1(F_TANH, u)), a));
            if (k == -1 && id == F_SIN) return s_div(s_ln(s_fn1(F_TAN, s_mul2(K->chalf, u))), a);
            if (k == -1 && id == F_COS)
                return s_div(s_ln(s_add2(s_inv(s_fn1(F_COS, u)), s_fn1(F_TAN, u))), a);
            if (k == -1 && id == F_TAN) return s_div(s_ln(s_fn1(F_SIN, u)), a);
        }
        /* 1/sqrt(c - k x^2) and 1/sqrt(x^2 + c) are not table entries: they are handled
         * by int_sqrt_quad / int_inv_sqrt_quad */
        return NULL;
    }
    if (f->t == E_FN && f->n == 1 && lin(K0(f), x, &a, &b)) {
        E *u = K0(f), *r = NULL;
        switch (f->s) {
        case F_SIN: r = s_neg(s_fn1(F_COS, u)); break;
        case F_COS: r = s_fn1(F_SIN, u); break;
        case F_TAN: r = s_neg(s_ln(s_fn1(F_COS, u))); break;
        case F_COT: r = s_ln(s_fn1(F_SIN, u)); break;
        case F_SEC: r = s_ln(s_add2(s_fn1(F_SEC, u), s_fn1(F_TAN, u))); break;
        case F_CSC: r = s_neg(s_ln(s_add2(s_fn1(F_CSC, u), s_fn1(F_COT, u)))); break;
        case F_SINH: r = s_fn1(F_COSH, u); break;
        case F_COSH: r = s_fn1(F_SINH, u); break;
        case F_TANH: r = s_ln(s_fn1(F_COSH, u)); break;
        case F_LN: r = s_sub(s_mul2(u, s_ln(u)), u); break;
        case F_ASIN: r = s_add2(s_mul2(u, f), s_sqrt(s_sub(K->c1, s_powi(u, 2)))); break;
        case F_ACOS: r = s_sub(s_mul2(u, f), s_sqrt(s_sub(K->c1, s_powi(u, 2)))); break;
        case F_ATAN: r = s_sub(s_mul2(u, f), s_mul2(K->chalf, s_ln(s_add2(K->c1, s_powi(u, 2))))); break;
        case F_ACOT: r = s_add2(s_mul2(u, f), s_mul2(K->chalf, s_ln(s_add2(K->c1, s_powi(u, 2))))); break;
        case F_ASINH: r = s_sub(s_mul2(u, f), s_sqrt(s_add2(s_powi(u, 2), K->c1))); break;
        case F_ACOSH: r = s_sub(s_mul2(u, f), s_sqrt(s_sub(s_powi(u, 2), K->c1))); break;
        case F_ATANH: r = s_add2(s_mul2(u, f), s_mul2(K->chalf, s_ln(s_sub(K->c1, s_powi(u, 2))))); break;
        case F_ABS: r = s_mul2(K->chalf, s_mul2(u, f)); break;
        case F_SIGN: r = s_fn1(F_ABS, u); break;
        case F_ERF:
            r = s_add2(s_mul2(u, f), s_div(s_exp(s_neg(s_powi(u, 2))), s_sqrt(K->pi_)));
            break;
        case F_HEAVISIDE: r = s_mul2(u, f); break;
        case F_DIRAC: r = s_fn1(F_HEAVISIDE, u); break;
        }
        if (r) return s_div(r, a);
    }
    /* g(a x)/x for g = sin, cos, exp: Si, Ci, Ei */
    if (f->t == E_MUL && f->n == 2) {
        E *p = K0(f), *q = K1(f);
        if (p->t == E_POW && is_int_val(K1(p), -1) && expr_eq(K0(p), x)) {
            E *t = p; p = q; q = t;
        }
        if (q->t == E_POW && is_int_val(K1(q), -1) && expr_eq(K0(q), x)) {
            E *aa, *bb;
            if (p->t == E_FN && p->n == 1 && lin(K0(p), x, &aa, &bb) && is_zero(bb)) {
                if (p->s == F_SIN) return s_fn1(F_SI, K0(p));
                if (p->s == F_COS) return s_fn1(F_CI, K0(p));
                if (p->s == F_SINH) return NULL; /* Shi is not available */
            }
            if (p->t == E_POW && ISYM(K0(p), S_E) && lin(K1(p), x, &aa, &bb) && is_zero(bb)) return s_fn1(F_EI, K1(p));
        }
    }
    if (f->t == E_MUL && f->n == 2 && K0(f)->t == E_FN && K1(f)->t == E_FN && K0(f)->n == 1 && K1(f)->n == 1 &&
        expr_eq(K0(K0(f)), K0(K1(f))) && lin(K0(K0(f)), x, &a, &b)) {
        int p = K0(f)->s, q = K1(f)->s;
        E *u = K0(K0(f));
        if ((p == F_SEC && q == F_TAN) || (p == F_TAN && q == F_SEC)) return s_div(s_fn1(F_SEC, u), a);
        if ((p == F_CSC && q == F_COT) || (p == F_COT && q == F_CSC)) return s_neg(s_div(s_fn1(F_CSC, u), a));
    }
    return NULL;
}

/*
 * table_c -- table() after pulling the x-free factors out of a product:
 * table(c1*c2*g) = c1*c2*table(g).  NULL if the x-dependent part is not a table entry.
 */
static E *table_c(E *f, E *x) {
    if (f->t == E_MUL) {
        E **c = salloc(sizeof(E *) * f->n), **v = salloc(sizeof(E *) * f->n);
        uint32_t nc = 0, nv = 0;
        for (uint32_t i = 0; i < f->n; i++) {
            if (freeof(EK(f)[i], x)) c[nc++] = EK(f)[i];
            else v[nv++] = EK(f)[i];
        }
        if (nc && nv) {
            E *r = table(s_mul(nv, v), x);
            return r ? s_mul2(s_mul(nc, c), r) : NULL;
        }
    }
    return table(f, x);
}

/* ------------------------------------------------- rational functions --- */
/*
 * ext_euclid -- solve the polynomial Diophantine equation s*a + t*b = c over the
 * coefficient field with deg s < deg b (unique when it exists).  up_egcd gives
 * s0*a + t0*b = g (g = gcd, monic); this fails (returns 0) unless g | c.  With q = c/g,
 * s = s0*q and t = t0*q solve the equation; s is then reduced modulo b and the quotient
 * moved into t (s -= q2*b, t += q2*a).  This is the ExtendedEuclidean step of Hermite
 * reduction (Bronstein, Symbolic Integration I, ch. 1-2).
 */
static int ext_euclid(UP a, UP b, UP c, UP *s, UP *t) {
    UP s0, t0;
    UP g = up_egcd(a, b, &s0, &t0);
    UP q, r;
    up_divrem(c, g, &q, &r);
    if (r.d >= 0) return 0;
    UP ss = up_mul(s0, q), tt = up_mul(t0, q);
    if (ss.d >= b.d && b.d >= 0) {
        UP q2, r2;
        up_divrem(ss, b, &q2, &r2);
        ss = r2;
        tt = up_add(tt, up_mul(q2, a), 0);
    }
    *s = ss;
    *t = tt;
    return 1;
}

/*
 * int_poly -- termwise antiderivative of a univariate polynomial with x-free coefficients:
 * sum c_i x^(i+1)/(i+1).
 */
static E *int_poly(UP p, E *x) {
    E **t = salloc(sizeof(E *) * (size_t)(p.d + 2));
    uint32_t n = 0;
    for (long i = 0; i <= p.d; i++)
        if (!is_zero(p.c[i])) t[n++] = s_mul2(s_div(p.c[i], mk_int(i + 1)), s_pow(x, mk_int(i + 1)));
    return s_add(n, t);
}

/*
 * sqrt_pos -- square root of an x-free expression e that pulls even powers of symbolic
 * factors out of the root: sqrt(c * u^2) -> sqrt(c) * u for constant c > 0 (integration
 * constants are treated as positive parameters, as most CAS do for such formulas; the
 * result is wrong for negative u).  Only bases that are positive when their symbols are
 * (posish: a, a + b, not a - b) are pulled out.  e is factored with factor_expr; factors that are pure
 * constants (no free symbols) stay under the root.  If s_sqrt already simplifies e to
 * something that is neither a product nor a power, that is returned unchanged.
 */
/* posish -- e is positive when its symbols are: a symbol, a positive number, or a sum,
 * product or real power of posish parts (so a + b qualifies, a - b does not) */
static int posish(E *e) {
    stack_check();
    double v;
    if (e->t == E_SYM) return 1;
    if (is_num(e)) return to_double(e, &v) && v > 0;
    if (e->t == E_POW) return posish(K0(e)) && is_num(K1(e));
    if (e->t == E_ADD || e->t == E_MUL) {
        for (uint32_t i = 0; i < e->n; i++)
            if (!posish(EK(e)[i])) return 0;
        return 1;
    }
    return sign_of(e) > 0;
}

static E *sqrt_pos(E *e) {
    E *r = s_sqrt(e);
    if (r->t != E_MUL && r->t != E_POW) return r;
    E *f = factor_expr(e);
    E **fs = f->t == E_MUL ? EK(f) : &f;
    uint32_t n = f->t == E_MUL ? f->n : 1;
    E **out = salloc(sizeof(E *) * (n + 1));
    for (uint32_t i = 0; i < n; i++) {
        E *q = fs[i];
        long k;
        if (q->t == E_POW && num_is_small_int(K1(q), &k) && k % 2 == 0 && !is_const_expr(K0(q)) &&
            posish(K0(q)))
            out[i] = s_powi(K0(q), k / 2);
        else
            out[i] = s_sqrt(q);
    }
    return s_mul(n, out);
}

/*
 * int_inv_quad -- antiderivative of 1/(a x^2 + b x + c) with x-free coefficients.
 * With D = 4ac - b^2:
 *   D < 0 (numerically known):  (ln(2ax+b - s) - ln(2ax+b + s))/s,  s = sqrt(b^2 - 4ac)
 *   otherwise:                  2/sqrt(D) * atan((2ax + b)/sqrt(D))
 * The atan form is also used when the sign of D cannot be decided (symbolic parameters
 * are assumed to give an irreducible quadratic).  int_rational only passes squarefree
 * quadratics, so D = 0 is not expected.
 */
static E *int_inv_quad(E *a, E *b, E *c, E *x) {
    E *disc = rat_normal(s_sub(s_mul2(mk_int(4), s_mul2(a, c)), s_powi(b, 2)));
    E *lin2 = s_add2(s_mul2(s_mul2(K->c2, a), x), b);
    double dv;
    int neg = is_const_expr(disc) && to_double(disc, &dv) && dv < 0;
    if (!neg) {
        E *sq = sqrt_pos(disc);
        return s_mul2(s_div(K->c2, sq), s_fn1(F_ATAN, s_div(lin2, sq)));
    }
    E *sq = sqrt_pos(s_neg(disc));
    return s_div(s_sub(s_ln(s_sub(lin2, sq)), s_ln(s_add2(lin2, sq))), sq);
}

/*
 * int_log_part -- integral of A/f where f is squarefree and (normally) irreducible,
 * deg A < deg f: the logarithmic part of a rational integral.
 *   deg f = 1:  A0/f1 * ln(f)
 *   deg f = 2:  A = p x + q = p/(2a) * f' + (q - p b/(2a)): a log term plus the remainder
 *               times int_inv_quad
 *   3 <= deg f <= 8: Rothstein-Trager.  R(z) = res_x(A - z f', f); every root c of R gives
 *               the term c * ln(gcd(A - c f', f)).  Only roots in the coefficient field
 *               are handled: every non-constant factor of R must be linear in z, and the
 *               gcd degrees must add up to deg f (all residues accounted for).  Roots that
 *               need an algebraic extension (a RootSum) make the function give up.
 * Returns K->c0 for A = 0, the log/atan sum (scratch), or NULL.  The resultant is computed
 * by resultant_expr (determinant based), hence the degree cap.  Interns the symbol _z.
 */
static E *int_log_part(UP A, UP f, E *x) {
    if (A.d < 0) return K->c0;
    if (f.d == 1) return s_mul2(s_div(A.c[0], f.c[1]), s_ln(up_to(f, x)));
    if (f.d == 2) {
        E *a = f.c[2], *b = f.c[1], *c = f.c[0];
        E *p = A.d >= 1 ? A.c[1] : K->c0, *q = A.c[0];
        E *r = K->c0;
        if (!is_zero(p)) r = s_mul2(s_div(p, s_mul2(K->c2, a)), s_ln(up_to(f, x)));
        E *rest = rat_normal(s_sub(q, s_div(s_mul2(p, b), s_mul2(K->c2, a))));
        if (!is_zero(rest)) r = s_add2(r, s_mul2(rest, int_inv_quad(a, b, c, x)));
        return r;
    }
    UP fp = up_deriv(f);
    { /* A = c f': c ln f (a single residue; quartics like d x^4 + c with symbolic coefficients) */
        E *c = rat_normal(s_div(up_to(A, x), up_to(fp, x)));
        if (freeof(c, x)) return s_mul2(c, s_ln(up_to(f, x)));
    }
    /* Rothstein-Trager (moderate degrees only: resultant via determinant) */
    if (f.d > 8) return NULL;
    E *z = mk_sym_name("_z");
    E *Rz = resultant_expr(s_sub(up_to(A, x), s_mul2(z, up_to(fp, x))), up_to(f, x), x);
    Rz = expand(Rz);
    E *content;
    E *fl = factor_list_in(Rz, z, &content);
    E **terms = salloc(sizeof(E *) * (fl->n + 1));
    uint32_t nt = 0;
    long tot = 0;
    for (uint32_t i = 0; i < fl->n; i++) {
        E *fac = K0(EK(fl)[i]);
        long d;
        if (freeof(fac, z)) continue;
        if (!poly_degree_in(fac, z, &d)) return NULL;
        if (d != 1) return NULL;
        E *c = rat_normal(s_neg(s_div(poly_coeff(fac, z, 0), poly_coeff(fac, z, 1))));
        UP h;
        if (!up_from(expand(s_sub(up_to(A, x), s_mul2(c, up_to(fp, x)))), x, &h)) return NULL;
        UP g = up_gcd(h, f);
        if (g.d <= 0) return NULL;
        tot += g.d;
        terms[nt++] = s_mul2(c, s_ln(up_to(g, x)));
    }
    if (tot != f.d) return NULL;
    return s_add(nt, terms);
}

/*
 * real_quartic_split -- split an irreducible quartic q with exact rational coefficients
 * into two monic real quadratics, q = c4 * f1 * f2, whose coefficients may contain square
 * roots.  This lets e.g. 1/(x^4+1) integrate to ln/atan terms without algebraic
 * Rothstein-Trager.  Method (Ferrari/Descartes): make q monic and depress it with
 * x = y - a/4 to y^4 + P y^2 + Q y + R, and seek (y^2 + al y + be)(y^2 - al y + ga):
 *     be + ga = P + al^2,   al (ga - be) = Q,   be ga = R.
 * Eliminating be, ga gives the resolvent cubic in A = al^2
 *     A^3 + 2P A^2 + (P^2 - 4R) A - Q^2 = 0;
 * any positive real root gives al = sqrt(A), be, ga = (P + A -+ Q/al)/2.  For Q = 0
 * (biquadratic) be = ga = sqrt(R), al = sqrt(2 be - P), which needs R > 0 and
 * 2 sqrt(R) > P.  The cubic is solved with solve_expr; a rational root is preferred,
 * otherwise any exact positive root (float roots are rejected).  Returns 1 with f1, f2,
 * or 0 if there is no usable real root.  Interns the symbol _A.
 */
static int real_quartic_split(UP q, UP *f1, UP *f2) {
    for (int i = 0; i <= 4; i++)
        if (!is_exactnum(q.c[i])) return 0;
    E *a = num_div(q.c[3], q.c[4]), *b = num_div(q.c[2], q.c[4]), *c = num_div(q.c[1], q.c[4]),
      *d = num_div(q.c[0], q.c[4]);
    /* depressed: x = y - a/4 */
    E *a2 = num_mul(a, a);
    E *P = num_sub(b, num_mul(mk_frac(3, 8), a2));
    E *Q = num_add(num_sub(c, num_mul(K->chalf, num_mul(a, b))), num_mul(mk_frac(1, 8), num_mul(a2, a)));
    E *R = num_add(num_sub(d, num_mul(mk_frac(1, 4), num_mul(a, c))),
                   num_sub(num_mul(mk_frac(1, 16), num_mul(a2, b)), num_mul(mk_frac(3, 256), num_mul(a2, a2))));
    E *alpha = NULL, *beta = NULL, *gamma = NULL;
    if (num_is_zero(Q)) {
        /* biquadratic: (y^2 + al y + be)(y^2 - al y + be), be = sqrt(R), al^2 = 2 be - P */
        double rv;
        /* both conditions make be and al real */
        if (num_sgn(R) <= 0) return 0;
        rv = num_to_double(R);
        if (2 * sqrt(rv) - num_to_double(P) <= 0) return 0;
        beta = gamma = s_sqrt(R);
        alpha = s_sqrt(s_sub(s_mul2(K->c2, beta), P));
    } else {
        E *A = mk_sym_name("_A");
        E *res = s_add(4, (E *[]){s_powi(A, 3), s_mul2(s_mul2(K->c2, P), s_powi(A, 2)),
                                   s_mul2(num_sub(num_mul(P, P), num_mul(mk_int(4), R)), A), num_neg(num_mul(Q, Q))});
        E *roots = solve_expr(res, A, 0);
        E *Av = NULL;
        for (uint32_t i = 0; i < roots->n; i++) {
            E *r = EK(roots)[i];
            double v;
            if (has_float(r) || !to_double(r, &v) || v <= 0) continue;
            if (!Av || is_exactnum(r)) Av = r;
        }
        if (!Av) return 0;
        alpha = s_sqrt(Av);
        E *qa = s_div(Q, alpha);
        beta = s_mul2(K->chalf, s_sub(s_add2(P, Av), qa));
        gamma = s_mul2(K->chalf, s_add(3, (E *[]){P, Av, qa}));
    }
    /* back to x: y = x + a/4 */
    E *sh = num_mul(mk_frac(1, 4), a);
    UP g1 = up_alloc(2), g2 = up_alloc(2);
    /* (x+sh)^2 + al (x+sh) + be = x^2 + (2 sh + al) x + (sh^2 + al sh + be) */
    g1.c[2] = K->c1;
    g1.c[1] = rat_normal(s_add2(s_mul2(K->c2, sh), alpha));
    g1.c[0] = rat_normal(s_add(3, (E *[]){s_mul2(sh, sh), s_mul2(alpha, sh), beta}));
    g2.c[2] = K->c1;
    g2.c[1] = rat_normal(s_sub(s_mul2(K->c2, sh), alpha));
    g2.c[0] = rat_normal(s_add(3, (E *[]){s_mul2(sh, sh), s_neg(s_mul2(alpha, sh)), gamma}));
    *f1 = g1;
    *f2 = g2;
    return 1;
}

/*
 * real_split -- an irreducible q of degree 3..6 with rational coefficients whose
 * roots solve finds exactly (radicals, explicit i): the monic real factors
 * x - r (real roots) and x^2 - (r + r') x + r r' (conjugate pairs r, r' = r
 * with i -> -i, checked numerically to be a root and the conjugate) into
 * out[]; returns their number or 0.
 */
static int has_fn_node(E *e) {
    stack_check();
    if (e->t == E_FN && e->s != F_EQ) return 1;
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_LIST && e->t != E_FN) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_fn_node(EK(e)[i])) return 1;
    return 0;
}

static uint32_t real_split(UP q, E *x, UP *out) {
    for (long i = 0; i <= q.d; i++)
        if (!is_exactnum(q.c[i])) return 0;
    E *volatile rs = NULL, *I = mk_sym(S_I);
    tryf tf;
    try_enter(&tf);
    if (!setjmp(tf.jb)) {
        rs = solve_expr(up_to(q, x), x, 1);
        try_leave(&tf);
    } else {
        try_leave(&tf);
        if (K->err == CAS_ERR_INTERRUPTED) rethrow();
        return 0;
    }
    /* radicals only: trigonometric root forms (casus irreducibilis) would
     * give unwieldy logs and atans */
    if (rs->t != E_LIST || (long)rs->n != q.d || has_fn_node(rs)) return 0;
    uint32_t k = 0;
    long deg = 0;
    for (uint32_t i = 0; i < rs->n; i++) {
        E *r = EK(rs)[i];
        if (IFN(r, F_EQ)) r = K1(r);
        double re, im, cr, ci;
        if (has_float(r) || !eval_complex(r, &re, &im)) return 0;
        UP f;
        if (fabs(im) <= 1e-12 * (1 + fabs(re))) {
            if (!freeof(r, I)) return 0;
            f = up_alloc(1);
            f.c[0] = s_neg(r);
        } else {
            if (im < 0) continue;
            E *cj = subst(r, I, s_neg(I));
            if (!eval_complex(cj, &cr, &ci) || fabs(cr - re) + fabs(ci + im) > 1e-9 * (1 + fabs(re) + fabs(im)))
                return 0;
            f = up_alloc(2);
            f.c[1] = rat_normal(expand(s_neg(s_add2(r, cj))));
            f.c[0] = rat_normal(expand(s_mul2(r, cj)));
            if (!freeof(f.c[1], I) || !freeof(f.c[0], I)) return 0;
        }
        f.c[f.d] = K->c1;
        out[k++] = f;
        deg += f.d;
    }
    return deg == q.d ? k : 0;
}

/*
 * int_rational -- integrate a rational function f = N/D of x whose coefficients are x-free
 * (rationals or expressions in parameters/constants; the UP routines keep them in rational
 * normal form).  Steps:
 *   1. polynomial part: N = Q D + R, Q integrated termwise;
 *   2. Hermite reduction, Mack's linear version (Bronstein ch. 2): int R/D = g + int A/Ds
 *      with g rational and Ds the squarefree part of D, using only gcds and extended
 *      Euclid (no factorisation);
 *   3. A/Ds: polynomial part again, then the shortcut A = c Ds' -> c ln(Ds);
 *   4. factor Ds over Q (factor_list_in); quartic factors are split into real quadratics
 *      when possible; partial fractions A/Ds = sum A_i/f_i with
 *      A_i = A * (prod_{j != i} f_j)^(-1) mod f_i (extended Euclid); each A_i/f_i goes to
 *      int_log_part.
 * Returns the antiderivative (scratch) or NULL when f is not a ratio of polynomials in x or
 * a logarithmic part needs algebraic numbers.  Polls for interrupts in its loops; errors of
 * factor/solve propagate.
 */
static E *int_rational(E *f, E *x) {
    stack_check();
    E *num, *den;
    numden(f, &num, &den);
    UP N, D;
    if (!up_from(num, x, &N) || !up_from(den, x, &D)) return NULL;
    if (D.d < 0) return NULL;
    if (D.d == 0) return int_poly(up_scale(N, s_inv(D.c[0])), x);
    UP Q, R;
    up_divrem(N, D, &Q, &R);
    E *res = int_poly(Q, x);
    if (R.d < 0) return res;
    /* Hermite reduction (Mack's linear version) */
    /*
     * D = Ds * Dm with Dm = gcd(D, D') and Ds = D/Dm squarefree.  Invariant of the loop:
     *     int R/D = g + int A/(Ds * Dm).
     * Each round removes one multiplicity level: with Dm2 = gcd(Dm, Dm') and Dms = Dm/Dm2
     * (squarefree), solve B * (-Ds Dm'/Dm) + C * Dms = A (deg B < deg Dms); then
     *     int A/(Ds Dm) = B/Dm + int (C - B' Ds/Dms)/(Ds Dm2).
     * The divisions Ds Dm'/Dm and B' Ds/Dms are exact.  The loop stops when Dm is
     * constant, leaving int A/Ds.
     */
    UP A = R;
    E *g = K->c0;
    UP Dm = up_gcd(D, up_deriv(D));
    UP Ds;
    up_divrem(D, Dm, &Ds, NULL);
    while (Dm.d > 0) {
        poll_intr();
        UP Dm2 = up_gcd(Dm, up_deriv(Dm));
        UP Dms;
        up_divrem(Dm, Dm2, &Dms, NULL);
        UP t1;
        up_divrem(up_mul(Ds, up_deriv(Dm)), Dm, &t1, NULL);
        t1 = up_scale(t1, K->cm1);
        UP B, C;
        if (!ext_euclid(t1, Dms, A, &B, &C)) return NULL;
        UP t2;
        up_divrem(up_mul(up_deriv(B), Ds), Dms, &t2, NULL);
        A = up_add(C, t2, 1);
        g = s_add2(g, s_div(up_to(B, x), up_to(Dm, x)));
        Dm = Dm2;
    }
    res = s_add2(res, g);
    if (A.d < 0) return res;
    /* A / Ds with Ds squarefree */
    UP q2, r2;
    up_divrem(A, Ds, &q2, &r2);
    res = s_add2(res, int_poly(q2, x));
    A = r2;
    if (A.d < 0) return res;
    {
        /* A = c * Ds' ? */
        UP dD = up_deriv(Ds);
        if (dD.d == A.d && dD.d >= 0) {
            E *cc = rat_normal(s_div(A.c[A.d], dD.c[dD.d]));
            UP diffp = up_add(A, up_scale(dD, cc), 1);
            if (diffp.d < 0) return s_add2(res, s_mul2(cc, s_ln(up_to(Ds, x))));
        }
    }
    /*
     * Factor Ds = cst * prod fac[i]: constant factors and the leading coefficients of split
     * quartics go into cst (Ds is squarefree, so every multiplicity is 1).  fac has room
     * for two entries per factor because a quartic may be replaced by two quadratics.
     */
    E *content;
    E *fl = factor_list_in(up_to(Ds, x), x, &content);
    uint32_t nf = 0;
    UP *fac = salloc(sizeof(UP) * (2 * fl->n + 2));
    E *cst = content;
    for (uint32_t i = 0; i < fl->n; i++) {
        E *fe = K0(EK(fl)[i]);
        if (freeof(fe, x)) {
            cst = s_mul2(cst, s_pow(fe, K1(EK(fl)[i])));
            continue;
        }
        if (!up_from(fe, x, &fac[nf])) return NULL;
        UP q4 = fac[nf];
        if (q4.d == 4) {
            UP f1, f2;
            if (real_quartic_split(q4, &f1, &f2)) {
                cst = s_mul2(cst, q4.c[4]);
                fac[nf++] = f1;
                fac[nf++] = f2;
                continue;
            }
        }
        if (g_split == 1 && q4.d >= 3 && q4.d <= 6) {
            UP *nfac = salloc(sizeof(UP) * (2 * fl->n + q4.d + 2));
            memcpy(nfac, fac, sizeof(UP) * nf);
            uint32_t k = real_split(q4, x, nfac + nf);
            if (k) {
                cst = s_mul2(cst, q4.c[q4.d]);
                fac = nfac;
                nf += k;
                continue;
            }
        }
        nf++;
    }
    A = up_scale(A, s_inv(cst));
    /* partial fractions: A/prod f_j = sum A_i/f_i with A_i = A * Qi^(-1) mod f_i */
    for (uint32_t i = 0; i < nf; i++) {
        poll_intr();
        UP Qi = up_const(K->c1);
        for (uint32_t j = 0; j < nf; j++)
            if (j != i) Qi = up_mul(Qi, fac[j]);
        UP Ai;
        if (nf == 1)
            Ai = A;
        else {
            UP s, t;
            UP gg = up_egcd(Qi, fac[i], &s, &t);
            if (gg.d != 0) return NULL;
            up_divrem(up_mul(A, s), fac[i], NULL, &Ai);
        }
        E *lp = int_log_part(Ai, fac[i], x);
        int ex = !lp && g_split == 0 && fac[i].d >= 3 && fac[i].d <= 6;
        for (long j = 0; ex && j <= fac[i].d; j++) ex = is_exactnum(fac[i].c[j]);
        for (long j = 0; ex && j <= Ai.d; j++) ex = is_exactnum(Ai.c[j]);
        if (ex) { /* roots in radicals: split over the reals */
            tryf tf;
            try_enter(&tf);
            g_split = 1;
            if (!setjmp(tf.jb)) {
                lp = int_rational(s_div(up_to(Ai, x), up_to(fac[i], x)), x);
                try_leave(&tf);
            } else {
                try_leave(&tf);
                if (K->err == CAS_ERR_INTERRUPTED) rethrow();
                lp = NULL;
            }
            g_split = 0;
        }
        if (!lp) return NULL;
        res = s_add2(res, lp);
    }
    return res;
}

/* -------------------------------------------------- integration by parts --- */
/*
 * is_parts_g -- the "dv" factor of tabular integration by parts: c^(a x + b) with c free
 * of x, or sin/cos/sinh/cosh of a linear argument.  Their table antiderivatives have the
 * same shape, so they can be integrated any number of times.
 */
static int is_parts_g(E *g, E *x) {
    E *a, *b;
    if (g->t == E_POW && freeof(K0(g), x) && lin(K1(g), x, &a, &b)) return 1;
    if (g->t == E_FN && g->n == 1 &&
        (g->s == F_SIN || g->s == F_COS || g->s == F_SINH || g->s == F_COSH) && lin(K0(g), x, &a, &b))
        return 1;
    return 0;
}

/*
 * is_parts_u -- the "u" factor of integration by parts (LIATE rule: logarithms and inverse
 * functions first): ln, inverse trig/hyperbolic functions, erf/erfc/erfi, Si/Ci/Ei of an
 * x-dependent argument, or a positive integer power of ln/atan/asin/acos/asinh/atanh.
 * Their derivatives are algebraic or simpler, so int V u' is usually easier.
 */
static int is_parts_u(E *g, E *x) {
    if (g->t == E_FN && g->n == 1 &&
        (g->s == F_LN || g->s == F_ATAN || g->s == F_ASIN || g->s == F_ACOS || g->s == F_ASINH ||
         g->s == F_ACOSH || g->s == F_ATANH || g->s == F_ACOT || g->s == F_ERF || g->s == F_ERFC ||
         g->s == F_ERFI || g->s == F_SI || g->s == F_CI || g->s == F_EI))
        return !freeof(g, x);
    if (g->t == E_POW && K1(g)->t == E_INT && num_sgn(K1(g)) > 0 && K0(g)->t == E_FN && K0(g)->n == 1) {
        int id = K0(g)->s;
        return id == F_LN || id == F_ATAN || id == F_ASIN || id == F_ACOS || id == F_ASINH || id == F_ATANH;
    }
    return 0;
}

/*
 * int_xk_gauss -- x^k * e^(c x^2 + d), 1 <= k <= 40, by the reduction
 *     I_k = x^(k-1) e^(c x^2)/(2c) - (k-1)/(2c) I_(k-2),
 * which follows from d/dx[x^(k-1) e^(c x^2)] = (k-1) x^(k-2) e^(c x^2) + 2c x^k e^(c x^2).
 * Odd k ends at I_1 (elementary); even k reaches I_0, which table() gives as erf/erfi.
 * f must be exactly the product x^k * e^(quadratic without linear term).  Each step recurses
 * through ir() with depth+1, so the depth limit of ir() bounds the usable k in practice.
 * Returns NULL if f does not match or the recursion fails.
 */
static E *int_xk_gauss(E *f, E *x, int depth) {
    if (f->t != E_MUL || f->n != 2) return NULL;
    E *p = K0(f), *q = K1(f);
    if (!(q->t == E_POW && ISYM(K0(q), S_E))) {
        E *t = p; p = q; q = t;
    }
    if (!(q->t == E_POW && ISYM(K0(q), S_E))) return NULL;
    long k;
    if (expr_eq(p, x)) k = 1;
    else if (p->t == E_POW && expr_eq(K0(p), x) && num_is_small_int(K1(p), &k) && k >= 2 && k <= 40) {
    } else
        return NULL;
    E *qa, *qb, *qc;
    if (!quad(K1(q), x, &qa, &qb, &qc) || !is_zero(qb)) return NULL;
    E *two_c = s_mul2(K->c2, qa);
    E *t1 = s_div(s_mul2(s_pow(x, mk_int(k - 1)), q), two_c);
    if (k == 1) return t1;
    E *rest = ir(s_mul2(s_pow(x, mk_int(k - 2)), q), x, depth + 1);
    if (!rest) return NULL;
    return s_sub(t1, s_mul2(s_div(mk_int(k - 1), two_c), rest));
}

/*
 * parts -- integration by parts.  The factors of f are classified as: polynomial in x
 * (pp), the first LIATE factor u (is_parts_u), the first tabular factor g (is_parts_g),
 * and the rest.
 *   With u:  int u dv = u V - int V u', dv = all other factors.  Only tried when dv has a
 *            cheap antiderivative (x-free, polynomial, power of a linear function or a
 *            table entry) to keep the search small.  Covers x^n ln x, x^3 atan(x)^2, and
 *            ln x alone (dv = 1).
 *   Without: f = P(x) * g, g the product of the non-polynomial factors.  G = int g from the
 *            table, or for deg P <= 2 at shallow depth from a recursive ir() call.  If g is
 *            tabular, the tabular method gives the whole integral at once:
 *                int P g = sum_k (-1)^k P^(k) G_(k+1),  G_1 = G, G_(k+1) = int G_k,
 *            terminating when P^(k) = 0 (at most 60 terms).  Otherwise one step
 *            P G - int P' G.
 * depth > 6 fails (limits parts-within-parts chains).  Returns NULL on failure.
 */
static E *parts(E *f, E *x, int depth) {
    if (depth > 6) return NULL;
    if (f->t != E_MUL && !is_parts_u(f, x)) return NULL;
    E **fs = f->t == E_MUL ? EK(f) : &f;
    uint32_t n = f->t == E_MUL ? f->n : 1;
    E **pp = salloc(sizeof(E *) * (n + 1)), **rest = salloc(sizeof(E *) * (n + 1));
    uint32_t np = 0, nr = 0;
    E *g = NULL, *u = NULL;
    for (uint32_t i = 0; i < n; i++) {
        E *t = fs[i];
        if (is_poly_in(t, x)) pp[np++] = t;
        else if (!u && is_parts_u(t, x)) u = t;
        else if (!g && is_parts_g(t, x)) g = t;
        else rest[nr++] = t;
    }
    if (u) {
        /* u * dv: dv = everything else */
        for (uint32_t i = 0; i < np; i++) rest[nr++] = pp[i];
        if (g) rest[nr++] = g;
        E *dv = s_mul(nr, rest);
        if (!freeof(dv, x) && !is_poly_in(dv, x)) {
            /* only cheap antiderivatives of dv: power functions or table entries */
            E *a0, *b0;
            if (!(dv->t == E_POW && freeof(K1(dv), x) && lin(K0(dv), x, &a0, &b0)) && !table_c(dv, x)) return NULL;
        }
        E *V = ir(dv, x, depth + 1);
        if (!V) return NULL;
        E *rest_int = ir(expand(s_mul2(V, diff(u, x))), x, depth + 1);
        if (!rest_int) return NULL;
        return s_sub(s_mul2(u, V), rest_int);
    }
    if (nr) {
        /* P * (product of the non-polynomial factors) */
        if (g) rest[nr++] = g;
        g = s_mul(nr, rest);
    }
    if (!g) return NULL;
    E *P = s_mul(np, pp);
    if (freeof(P, x)) return NULL;
    E *G = table_c(g, x);
    if (!G) {
        long dP;
        if (!poly_degree_in(P, x, &dP) || dP > 2 || depth > 3) return NULL;
        G = ir(g, x, depth + 2);
        if (!G) return NULL;
    }
    if (is_parts_g(g, x)) {
        /* tabular: sum (-1)^k P^(k) G_{k+1} */
        E **t = salloc(sizeof(E *) * 64);
        uint32_t nt = 0;
        E *D = P, *Gk = G;
        int sg = 1;
        for (int k = 0; k < 60 && !is_zero(D); k++) {
            if (k) {
                Gk = table_c(Gk, x);
                if (!Gk) return NULL;
            }
            E *term = s_mul2(D, Gk);
            t[nt++] = sg > 0 ? term : s_neg(term);
            sg = -sg;
            D = diff(D, x);
        }
        if (!is_zero(D)) return NULL;
        return s_add(nt, t);
    }
    E *r = ir(expand(s_mul2(diff(P, x), G)), x, depth + 1);
    if (!r) return NULL;
    return s_sub(s_mul2(P, G), r);
}

/* ------------------------------------------------- derivative divides --- */
/* list of candidate substitution kernels: distinct (expr_eq) expressions, at most cap */
typedef struct {
    E **c;
    uint32_t n, cap;
} clist;

/* cl_add -- append u unless already present or the list is full (O(n) duplicate check) */
static void cl_add(clist *l, E *u) {
    for (uint32_t i = 0; i < l->n; i++)
        if (expr_eq(l->c[i], u)) return;
    if (l->n < l->cap) l->c[l->n++] = u;
}

/*
 * cands -- collect candidate substitutions u from e, outermost first: x-dependent function
 * applications and their non-trivial arguments, bases of powers with x-free exponents,
 * non-integer powers themselves (sqrt(x+1)), exponentials/powers with x-dependent exponents
 * together with the exponent, and for x^k the sub-powers x^d with d | k, 2 <= d <= 12 (so
 * x^8 offers x^4 and x^2).  x itself and x-free subexpressions are never candidates.
 */
static void cands(E *e, E *x, clist *l) {
    if (freeof(e, x) || expr_eq(e, x)) return;
    stack_check();
    switch (e->t) {
    case E_FN:
        cl_add(l, e);
        for (uint32_t i = 0; i < e->n; i++) {
            if (!freeof(EK(e)[i], x) && !expr_eq(EK(e)[i], x)) cl_add(l, EK(e)[i]);
            cands(EK(e)[i], x, l);
        }
        return;
    case E_POW:
        if (expr_eq(K0(e), x) && K1(e)->t == E_INT) {
            long k;
            if (num_is_small_int(K1(e), &k))
                for (long d = k - 1; d >= 2; d--)
                    if (k % d == 0 && d <= 12) cl_add(l, s_pow(x, mk_int(d)));
        }
        if (freeof(K1(e), x)) {
            if (!expr_eq(K0(e), x)) cl_add(l, K0(e));
            if (K1(e)->t != E_INT) cl_add(l, e);
        } else {
            cl_add(l, e);
            cl_add(l, K1(e));
        }
        cands(K0(e), x, l);
        cands(K1(e), x, l);
        return;
    case E_ADD:
    case E_MUL:
    case E_LIST:
        for (uint32_t i = 0; i < e->n; i++) cands(EK(e)[i], x, l);
        return;
    default: return;
    }
}

/*
 * monomial_plus -- is u = a x^n + b (n >= 2, no middle terms, a and b free of x)?
 * Lets try_sub express x through T = u as ((T-b)/a)^(1/n).
 */
static int monomial_plus(E *u, E *x, E **a, long *n, E **b) {
    long d;
    if (!poly_degree_in(u, x, &d) || d < 2) return 0;
    for (long k = 1; k < d; k++)
        if (!is_zero(poly_coeff(u, x, k))) return 0;
    *a = poly_coeff(u, x, d);
    *b = poly_coeff(u, x, 0);
    *n = d;
    return freeof(*a, x) && freeof(*b, x);
}

/* has_frac_pow_of -- does e contain base^(p/q) with a non-integer rational exponent? */
static int has_frac_pow_of(E *e, E *base) {
    stack_check();
    if (e->t == E_POW && K1(e)->t == E_RAT && expr_eq(K0(e), base)) return 1;
    if (e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_frac_pow_of(EK(e)[i], base)) return 1;
    return 0;
}

/*
 * subst_u -- replace every occurrence of u in e by T.  For an exponential u = e^g, any
 * other exponential e^h with h/g an exact rational k becomes T^k (so e^(2x) -> T^2 under
 * T = e^x).  Only the changed spine is rebuilt (and re-simplified by rebuild); unchanged
 * subtrees are shared.
 */
static E *subst_u(E *e, E *u, E *T) {
    stack_check();
    if (expr_eq(e, u)) return T;
    if (u->t == E_POW && ISYM(K0(u), S_E) && e->t == E_POW && ISYM(K0(e), S_E)) {
        E *k = rat_normal(s_div(K1(e), K1(u)));
        if (is_exactnum(k)) return s_pow(T, k);
    }
    if (e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR) return e;
    E **kk = salloc(sizeof(E *) * (e->n + 1));
    int ch = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        kk[i] = subst_u(EK(e)[i], u, T);
        if (kk[i] != EK(e)[i]) ch = 1;
    }
    return ch ? rebuild(e, kk) : e;
}

/*
 * try_sub -- derivative-divides substitution T = u(x): if f/u' can be written as q(u),
 * then int f dx = int q(T) dT with T = u substituted back.  Increasingly expensive
 * attempts to eliminate x from q:
 *   1. replace u by T in f/u';
 *   2. replace u by T in f first, then let rat_normal cancel the x-dependent part against
 *      u' (only for moderately sized f);
 *   3. rat_normal(f/u') first, then replace;
 *   4. for u = a x^n + b replace x by ((T-b)/a)^(1/n), accepted only if no fractional
 *      power of (T-b)/a survives.
 * The T-integral is done by ir() at depth+1.  The test qt == f with T == x rejects a
 * substitution that would just repeat the same call.  Returns NULL on failure.
 */
static E *try_sub(E *f, E *x, E *u, int depth) {
    E *du = diff(u, x);
    if (is_zero(du)) return NULL;
    E *T = tsym(depth);
    E *q = s_div(f, du);
    E *qt = subst_u(q, u, T);
    if (!freeof(qt, x)) {
        /* substitute first, then let the x-dependent cofactor cancel against u' */
        E *ft = subst_u(f, u, T);
        if (expr_size(ft) < 600) qt = rat_normal(s_div(ft, du));
    }
    if (!freeof(qt, x)) {
        q = rat_normal(q);
        qt = subst_u(q, u, T);
    }
    if (!freeof(qt, x)) {
        E *a, *b;
        long n;
        if (monomial_plus(u, x, &a, &n, &b)) {
            /* replace x^n by (T-b)/a (only if no radicals remain) */
            E *base = s_div(s_sub(T, b), a);
            E *xs = s_pow(base, mk_rat(K->c1, mk_int(n)));
            E *q0 = qt;
            qt = rat_normal(subst(q0, x, xs));
            if (has_frac_pow_of(qt, base)) return NULL;
            /* for even n, x = base^(1/n) holds only for x > 0: accept only if the other
             * branch x = -base^(1/n) gives the same q (else int sqrt(x^2) = x^2/2) */
            if (n % 2 == 0 && !is_zero(rat_normal(s_sub(qt, subst(q0, x, s_neg(xs)))))) return NULL;
        }
    }
    if (!freeof(qt, x)) return NULL;
    if (expr_eq(qt, f) && expr_eq(T, x)) return NULL;
    E *G = ir(qt, T, depth + 1);
    if (!G) return NULL;
    return subst(G, T, u);
}

/*
 * deriv_divides -- try_sub on every candidate from cands() (at most 48).  Non-linear
 * candidates come first because they carry the real substitutions (u = x^2+1 for
 * x/(x^2+1)^3); linear ones (u = a x + b, which always "divide") last.  Scratch of failed
 * attempts is released.  Returns the first success or NULL.
 */
static E *deriv_divides(E *f, E *x, int depth) {
    clist l;
    l.cap = 48;
    l.n = 0;
    l.c = salloc(sizeof(E *) * l.cap);
    cands(f, x, &l);
    for (uint32_t i = 0; i < l.n; i++) {
        char *m_ = smark();
        E *a, *b;
        if (lin(l.c[i], x, &a, &b)) continue;
        E *r = try_sub(f, x, l.c[i], depth);
        if (r) return r;
        srelease(m_);
    }
    /* linear substitutions last */
    for (uint32_t i = 0; i < l.n; i++) {
        char *m_ = smark();
        E *a, *b;
        if (!lin(l.c[i], x, &a, &b)) continue;
        E *r = try_sub(f, x, l.c[i], depth);
        if (r) return r;
        srelease(m_);
    }
    return NULL;
}

/* -------------------------------------------------- inverse substitution --- */
/*
 * exp_rates -- collect the rates a of all exponentials e^(a x + b) in e (at most 16 are
 * stored).  Sets *bad if some exponential has a non-linear exponent (then no T = e^(a x)
 * rationalises f).  *bad is only ever set, never cleared.
 */
static void exp_rates(E *e, E *x, E **rates, uint32_t *n, int *bad) {
    stack_check();
    if (freeof(e, x)) return;
    if (e->t == E_POW && ISYM(K0(e), S_E)) {
        E *a, *b;
        if (lin(K1(e), x, &a, &b)) {
            if (*n < 16) rates[(*n)++] = a;
            return;
        }
        *bad = 1;
        return;
    }
    if (e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR) return;
    for (uint32_t i = 0; i < e->n; i++) exp_rates(EK(e)[i], x, rates, n, bad);
}

/*
 * inv_sub -- inverse substitution x = phi(T): int f dx = int f(phi(T)) phi'(T) dT,
 * integrated by ir() at depth+1 and mapped back with T = back(x).  NULL if x survives
 * the substitution or the T-integral fails.
 */
static E *inv_sub(E *f, E *x, E *phi, E *dphi, E *back, E *T, int depth) {
    E *g = s_mul2(subst(f, x, phi), dphi);
    if (!freeof(g, x)) return NULL;
    g = rat_normal(g);
    E *G = ir(g, T, depth + 1);
    if (!G) return NULL;
    return subst(G, T, back);
}

/* frac_pows -- denominators q of the rational powers x^(p/q) of the bare variable x (max 16) */
static void frac_pows(E *e, E *x, E **dens, uint32_t *n) {
    stack_check();
    if (freeof(e, x)) return;
    if (e->t == E_POW && expr_eq(K0(e), x) && K1(e)->t == E_RAT) {
        if (*n < 16) dens[(*n)++] = K1(K1(e));
        return;
    }
    if (e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR) return;
    for (uint32_t i = 0; i < e->n; i++) frac_pows(EK(e)[i], x, dens, n);
}

/*
 * inverse_subs -- substitutions that rationalise the integrand, tried in order:
 *   1. exponentials: if every exponential is e^(a_i x + b_i), let a be the gcd of the rates
 *      (rational gcd if they are all numbers, else the first rate) and set T = e^(a x),
 *      x = ln(T)/a, dx = dT/(a T);
 *   2. a radical of a linear function of a single exponential, T = (A + B e^(c x))^(1/n),
 *      x = ln((T^n - A)/B)/c;
 *   3. a radical of a linear function, T = (a x + b)^(1/q), x = (T^q - b)/a; all powers
 *      of the base become powers of T (subst_rad);
 *   4. fractional powers of x: x = T^m, dx = m T^(m-1) dT, m = lcm of the denominators
 *      (m < 50).
 * The T-integrals are done by ir() at depth+1.  Returns the first success or NULL.
 * Interns the helper symbols _eu and _lb.
 */
static E *inverse_subs(E *f, E *x, int depth) {
    E *T = tsym(depth);
    /* exponentials */
    E *rates[16];
    uint32_t nr = 0;
    int bad = 0;
    exp_rates(f, x, rates, &nr, &bad);
    if (nr && !bad) {
        /* common rate: gcd of rational rates, else first */
        E *a = rates[0];
        int allnum = 1;
        for (uint32_t i = 0; i < nr; i++)
            if (!is_exactnum(rates[i])) allnum = 0;
        if (allnum) {
            E *g = rates[0];
            for (uint32_t i = 1; i < nr; i++) g = poly_gcd_expr(g, rates[i]);
            a = num_abs(g);
        }
        E *phi = s_div(s_ln(T), a), *dphi = s_div(K->c1, s_mul2(a, T));
        E *r = inv_sub(f, x, phi, dphi, s_exp(s_mul2(a, x)), T, depth);
        if (r) return r;
    }
    /* radical of a linear function of exp(c x): T = (a + b e^(c x))^(1/n) */
    /* the base must be linear in U = e^(c x): base = aa + bb U */
    {
        clist l;
        l.cap = 32;
        l.n = 0;
        l.c = salloc(sizeof(E *) * l.cap);
        cands(f, x, &l);
        for (uint32_t i = 0; i < l.n; i++) {
            E *k = l.c[i];
            if (k->t != E_POW || K1(k)->t != E_RAT) continue;
            E *base = K0(k);
            E *ex = NULL;
            E *cs[16];
            uint32_t nc = 0;
            bad = 0; /* (may still be set by the exponential stage above) */
            exp_rates(base, x, cs, &nc, &bad);
            if (nc != 1 || bad) continue;
            E *c = cs[0];
            ex = s_exp(s_mul2(c, x));
            E *U = mk_sym_name("_eu");
            E *lin_e = expand(subst(base, ex, U));
            long dd;
            if (!freeof(lin_e, x) || !poly_degree_in(lin_e, U, &dd) || dd != 1) continue;
            E *bb = poly_coeff(lin_e, U, 1), *aa = poly_coeff(lin_e, U, 0);
            E *nn = K1(K1(k));
            /* x = ln((T^n - a)/b)/c */
            E *phi = s_div(s_ln(s_div(s_sub(s_pow(T, nn), aa), bb)), c);
            E *r = inv_sub(f, x, phi, diff(phi, T), s_pow(base, mk_rat(K->c1, nn)), T, depth);
            if (r) return r;
        }
    }
    /* radicals of a linear function: T = (a x + b)^(1/q), x = (T^q - b)/a */
    {
        clist l;
        l.cap = 32;
        l.n = 0;
        l.c = salloc(sizeof(E *) * l.cap);
        cands(f, x, &l);
        for (uint32_t i = 0; i < l.n; i++) {
            E *k = l.c[i];
            E *aa, *bb;
            if (k->t != E_POW || K1(k)->t != E_RAT || expr_eq(K0(k), x) || !lin(K0(k), x, &aa, &bb)) continue;
            E *q = K1(K1(k));
            E *phi = s_div(s_sub(s_pow(T, q), bb), aa);
            /* every occurrence must be a power of the same linear base */
            E *g = s_mul2(subst_rad(subst(f, K0(k), mk_sym_name("_lb")), mk_sym_name("_lb"), T, q), diff(phi, T));
            g = subst(g, x, phi);
            if (!freeof(g, x)) continue;
            E *G = ir(rat_normal(g), T, depth + 1);
            if (G) return subst(G, T, s_pow(K0(k), s_inv(q)));
        }
    }
    /* fractional powers of x: x = T^m */
    E *dens[16];
    uint32_t nd = 0;
    frac_pows(f, x, dens, &nd);
    if (nd) {
        E *m = dens[0];
        for (uint32_t i = 1; i < nd; i++) m = int_lcm(m, dens[i]);
        long mv;
        if (num_is_small_int(m, &mv) && mv > 1 && mv < 50) {
            E *g = s_mul2(subst_rad(f, x, T, m), s_mul2(m, s_pow(T, mk_int(mv - 1))));
            if (freeof(g, x)) {
                E *G = ir(rat_normal(g), T, depth + 1);
                if (G) return subst(G, T, s_pow(x, mk_rat(K->c1, m)));
            }
        }
    }
    return NULL;
}

/* ---------------------------------------- P(x) * Q^(k/2), Q quadratic --- */
/*
 * find_sqrt_quad -- look for powers Q^(k/2) (k odd) of a quadratic Q in x.  Returns 1 if
 * there are some and they all share the same Q (stored in *Q), -1 if two different
 * quadratics occur, 0 if none.  *Q must be NULL on the first call.
 */
static int find_sqrt_quad(E *e, E *x, E **Q) {
    stack_check();
    if (freeof(e, x)) return 0;
    if (e->t == E_POW && K1(e)->t == E_RAT && is_int_val(K1(K1(e)), 2)) {
        E *a, *b, *c;
        if (quad(K0(e), x, &a, &b, &c)) {
            if (*Q && !expr_eq(*Q, K0(e))) return -1;
            *Q = K0(e);
            return 1;
        }
    }
    if (e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR) return 0;
    int found = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        int r = find_sqrt_quad(EK(e)[i], x, Q);
        if (r < 0) return -1;
        if (r) found = 1;
    }
    return found;
}

/*
 * int_inv_sqrt_quad -- antiderivative of 1/sqrt(Q), Q = a x^2 + b x + c:
 *   a < 0 (numerically known): -asin((2ax + b)/sqrt(b^2 - 4ac))/sqrt(-a)
 *   otherwise (a > 0 assumed): ln(2 sqrt(a) sqrt(Q) + 2ax + b)/sqrt(a),
 *                              or ln(sqrt(a) x + sqrt(Q))/sqrt(a) when b = 0
 * (constant factors inside ln are dropped as part of the integration constant).
 */
static E *int_inv_sqrt_quad(E *a, E *b, E *c, E *x) {
    E *Q = s_add(3, (E *[]){s_mul2(a, s_powi(x, 2)), s_mul2(b, x), c});
    double av;
    int aneg = is_const_expr(a) && to_double(a, &av) && av < 0;
    if (aneg) {
        E *disc = rat_normal(s_sub(s_powi(b, 2), s_mul2(mk_int(4), s_mul2(a, c))));
        E *arg = s_div(s_add2(s_mul2(s_mul2(K->c2, a), x), b), sqrt_pos(disc));
        return s_mul2(s_neg(s_inv(s_sqrt(s_neg(a)))), s_fn1(F_ASIN, arg));
    }
    E *sa = s_sqrt(a);
    if (is_zero(b)) return s_div(s_ln(s_add2(s_mul2(sa, x), s_sqrt(Q))), sa);
    return s_div(s_ln(s_add(3, (E *[]){s_mul2(s_mul2(K->c2, sa), s_sqrt(Q)), s_mul2(s_mul2(K->c2, a), x), b})), sa);
}

/*
 * int_sqrt_quad -- f = P(x)/sqrt(Q), Q quadratic, P a polynomial of degree n <= 12.  This
 * includes P*Q^(k/2) for odd k > 0 because f*sqrt(Q) is then still a polynomial.  Ansatz
 * by undetermined coefficients:
 *     int P/sqrt(Q) dx = R(x) sqrt(Q) + lambda * int dx/sqrt(Q),   deg R = n - 1;
 * differentiating and multiplying by sqrt(Q) gives P = R' Q + R Q'/2 + lambda, i.e. n+1
 * linear equations for the n coefficients of R and lambda (solved by linsolve).  Interns
 * the helper symbols _r00, _r01, ... and _lam.  NULL if f has another shape.
 */
static E *int_sqrt_quad(E *f, E *x) {
    E *Q = NULL;
    if (find_sqrt_quad(f, x, &Q) != 1) return NULL;
    E *a, *b, *c;
    quad(Q, x, &a, &b, &c);
    /* f = P / sqrt(Q) with P polynomial ? */
    E *P = rat_normal(s_mul2(f, s_sqrt(Q)));
    if (!is_poly_in(P, x)) return NULL;
    P = expand(P);
    long n;
    if (!poly_degree_in(P, x, &n)) return NULL;
    if (n > 12) return NULL;
    /* P = R' Q + R Q'/2 + lambda, deg R = n-1 */
    E *res;
    if (n == 0) {
        res = s_mul2(P, int_inv_sqrt_quad(a, b, c, x));
        return res;
    }
    E **rc = salloc(sizeof(E *) * (size_t)(n + 1));
    char nm[8];
    for (long i = 0; i < n; i++) {
        nm[0] = '_';
        nm[1] = 'r';
        nm[2] = (char)('0' + i / 10);
        nm[3] = (char)('0' + i % 10);
        nm[4] = 0;
        rc[i] = mk_sym_name(nm);
    }
    E *lam = mk_sym_name("_lam");
    E **rt = salloc(sizeof(E *) * (size_t)n);
    for (long i = 0; i < n; i++) rt[i] = s_mul2(rc[i], s_pow(x, mk_int(i)));
    E *R = s_add((uint32_t)n, rt);
    E *rhs = s_add(3, (E *[]){s_mul2(diff(R, x), Q), s_mul2(s_mul2(R, diff(Q, x)), K->chalf), lam});
    E *eq = expand(s_sub(rhs, P));
    E **eqs = salloc(sizeof(E *) * (size_t)(n + 1));
    for (long k = 0; k <= n; k++) eqs[k] = poly_coeff(eq, x, k);
    E **vars = salloc(sizeof(E *) * (size_t)(n + 1));
    for (long i = 0; i < n; i++) vars[i] = rc[i];
    vars[n] = lam;
    E *sol = linsolve(s_list((uint32_t)(n + 1), eqs), s_list((uint32_t)(n + 1), vars));
    if (!sol || sol->n != (uint32_t)(n + 1)) return NULL;
    E **xs = salloc(sizeof(E *) * (size_t)(n + 1)), **vs = salloc(sizeof(E *) * (size_t)(n + 1));
    for (uint32_t i = 0; i < sol->n; i++) {
        xs[i] = K0(EK(sol)[i]);
        vs[i] = K1(EK(sol)[i]);
    }
    E *Rv = subst_many(R, sol->n, xs, vs), *lv = subst_many(lam, sol->n, xs, vs);
    res = s_mul2(Rv, s_sqrt(Q));
    if (!is_zero(lv)) res = s_add2(res, s_mul2(lv, int_inv_sqrt_quad(a, b, c, x)));
    return res;
}

/* Euler substitutions for R(x, sqrt(a x^2 + b x + c)) */
/* replace_sqrtq -- replace every Q^(k/2) in e by S^k (S stands for sqrt(Q)) */
static E *replace_sqrtq(E *e, E *Q, E *S) {
    stack_check();
    if (e->t == E_POW && expr_eq(K0(e), Q) && K1(e)->t == E_RAT && is_int_val(K1(K1(e)), 2))
        return s_pow(S, K0(K1(e)));
    if (e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR) return e;
    E **k = salloc(sizeof(E *) * (e->n + 1));
    for (uint32_t i = 0; i < e->n; i++) k[i] = replace_sqrtq(EK(e)[i], Q, S);
    return rebuild(e, k);
}

/*
 * int_euler -- Euler substitutions, which make R(x, sqrt(Q)), Q = a x^2 + b x + c, rational
 * in a new variable t:
 *   a > 0:  sqrt(Q) = t - sqrt(a) x,  x = (t^2 - c)/(b + 2 sqrt(a) t),  t = sqrt(Q) + sqrt(a) x
 *   c > 0:  sqrt(Q) = x t + sqrt(c),  x = (2 sqrt(c) t - b)/(a - t^2),  t = (sqrt(Q) - sqrt(c))/x
 * The signs of a / c must be decidable numerically; the third substitution (real roots of
 * Q) is not implemented.  The transformed integrand must be rational in t and is passed
 * straight to int_rational.  Uses the variable tsym(depth+30).  NULL on failure.
 */
static E *int_euler(E *f, E *x, int depth) {
    E *Q = NULL;
    if (find_sqrt_quad(f, x, &Q) != 1) return NULL;
    E *a, *b, *c;
    quad(Q, x, &a, &b, &c);
    double av, cv;
    E *T = tsym(depth + 30);
    E *xt, *St, *back;
    if (is_const_expr(a) && to_double(a, &av) && av > 0) {
        E *sa = s_sqrt(a);
        /* sqrt(Q) = t - sa x  =>  x = (t^2 - c)/(b + 2 sa t) */
        xt = s_div(s_sub(s_powi(T, 2), c), s_add2(b, s_mul(3, (E *[]){K->c2, sa, T})));
        St = s_sub(T, s_mul2(sa, xt));
        back = s_add2(s_sqrt(Q), s_mul2(sa, x));
    } else if (is_const_expr(c) && to_double(c, &cv) && cv > 0) {
        E *sc = s_sqrt(c);
        /* sqrt(Q) = x t + sc  =>  x = (2 sc t - b)/(a - t^2) */
        xt = s_div(s_sub(s_mul(3, (E *[]){K->c2, sc, T}), b), s_sub(a, s_powi(T, 2)));
        St = s_add2(s_mul2(xt, T), sc);
        back = s_div(s_sub(s_sqrt(Q), sc), x);
    } else
        return NULL;
    E *g = replace_sqrtq(f, Q, St);
    g = subst(g, x, xt);
    if (!freeof(g, x)) return NULL;
    g = rat_normal(s_mul2(g, diff(xt, T)));
    if (!is_rational_in(g, T)) return NULL;
    E *G = int_rational(g, T);
    if (!G) return NULL;
    return subst(G, T, back);
}

/*
 * int_exp_poly -- integral of P(x) e^(g(x)) with P, g polynomials in x, deg g >= 1.
 * f must be a sum of terms (polynomial) * e^g with one common exponent g.  Since
 * (R e^g)' = (R' + g' R) e^g, the integral has this form iff R' + g' R = P has a
 * polynomial solution R, necessarily of degree deg P - (deg g - 1).  Its coefficients
 * (symbols _p00, _p01, ...) are found with linsolve; for deg g >= 2 the system is
 * overdetermined, so the solution is checked against every equation.  Examples:
 * x e^(x^2) -> e^(x^2)/2,  x^2 e^x -> (x^2 - 2x + 2) e^x.  Returns R e^g or NULL
 * (deg R <= 20).
 */
static E *int_exp_poly(E *f, E *x) {
    E **ts = f->t == E_ADD ? EK(f) : &f;
    uint32_t n = f->t == E_ADD ? f->n : 1;
    E *g = NULL;
    E **ps = salloc(sizeof(E *) * (n + 1));
    for (uint32_t i = 0; i < n; i++) {
        E *t = ts[i];
        E **fs = t->t == E_MUL ? EK(t) : &t;
        uint32_t m = t->t == E_MUL ? t->n : 1;
        E *gi = NULL;
        E **rest = salloc(sizeof(E *) * (m + 1));
        uint32_t nr = 0;
        for (uint32_t j = 0; j < m; j++) {
            E *q = fs[j];
            if (q->t == E_POW && ISYM(K0(q), S_E) && !freeof(K1(q), x) && !gi) gi = K1(q);
            else rest[nr++] = q;
        }
        if (!gi) return NULL;
        if (g && !expr_eq(gi, g)) return NULL;
        g = gi;
        ps[i] = s_mul(nr, rest);
    }
    long dg, dp;
    E *P = expand(s_add(n, ps));
    if (!poly_degree_in(g, x, &dg) || dg < 1 || !poly_degree_in(P, x, &dp)) return NULL;
    if (!is_poly_in(P, x) || !is_poly_in(g, x)) return NULL;
    long dr = dp - (dg - 1);
    if (dr < 0 || dr > 20) return NULL;
    E *gp = diff(g, x);
    char nm[8];
    E **cs = salloc(sizeof(E *) * (size_t)(dr + 1));
    E **rt = salloc(sizeof(E *) * (size_t)(dr + 1));
    for (long i = 0; i <= dr; i++) {
        nm[0] = '_'; nm[1] = 'p'; nm[2] = (char)('0' + i / 10); nm[3] = (char)('0' + i % 10); nm[4] = 0;
        cs[i] = mk_sym_name(nm);
        rt[i] = s_mul2(cs[i], s_pow(x, mk_int(i)));
    }
    E *R = s_add((uint32_t)(dr + 1), rt);
    E *eq = expand(s_sub(s_add2(diff(R, x), s_mul2(gp, R)), P));
    long de;
    if (!poly_degree_in(eq, x, &de)) return NULL;
    E **eqs = salloc(sizeof(E *) * (size_t)(de + 2));
    for (long i = 0; i <= de; i++) eqs[i] = poly_coeff(eq, x, i);
    E *sol = linsolve(s_list((uint32_t)(de + 1), eqs), s_list((uint32_t)(dr + 1), cs));
    if (!sol || sol->n != (uint32_t)(dr + 1)) return NULL;
    /* verify consistency: substituting must make every equation vanish */
    E **xs = salloc(sizeof(E *) * (size_t)(dr + 1)), **vs = salloc(sizeof(E *) * (size_t)(dr + 1));
    for (uint32_t i = 0; i < sol->n; i++) {
        xs[i] = K0(EK(sol)[i]);
        vs[i] = K1(EK(sol)[i]);
    }
    for (long i = 0; i <= de; i++)
        if (!is_zero(rat_normal(subst_many(eqs[i], sol->n, xs, vs)))) return NULL;
    E *Rv = subst_many(R, sol->n, xs, vs);
    return s_mul2(Rv, s_exp(g));
}

/* --------------------------------------------------------------- trig --- */
/*
 * trig_args_same -- do all trig functions (sin ... csc) in e have the same argument?  The
 * argument is stored in *u, which must start as NULL (it stays NULL if e has no trig).
 */
static int trig_args_same(E *e, E **u) {
    stack_check();
    if (e->t == E_FN && e->n == 1 &&
        (e->s == F_SIN || e->s == F_COS || e->s == F_TAN || e->s == F_COT || e->s == F_SEC || e->s == F_CSC)) {
        if (*u && !expr_eq(*u, K0(e))) return 0;
        *u = K0(e);
        return 1;
    }
    if (e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR) return 1;
    for (uint32_t i = 0; i < e->n; i++)
        if (!trig_args_same(EK(e)[i], u)) return 0;
    return 1;
}

/*
 * trig_poly -- permissive test that e is a polynomial in sin/cos (no negative powers of
 * anything containing trig).  Any function application is accepted as an atom (e.g.
 * ln(sin x)), so this is only a pre-check before trig_linearize.
 */
static int trig_poly(E *e) {
    stack_check();
    /* polynomial in sin/cos (no negative powers of trig) */
    if (e->t == E_POW && has_trig(K0(e))) return K1(e)->t == E_INT && num_sgn(K1(e)) > 0 && trig_poly(K0(e));
    if (e->t == E_ADD || e->t == E_MUL) {
        for (uint32_t i = 0; i < e->n; i++)
            if (!trig_poly(EK(e)[i])) return 0;
        return 1;
    }
    if (e->t == E_FN && (e->s == F_SIN || e->s == F_COS)) return 1;
    return !has_trig(e) || e->t == E_FN;
}

/*
 * int_trig -- trigonometric integrands.  g = f with tan/cot/sec/csc rewritten in sin/cos.
 *   1. g polynomial in sin/cos: linearise products and powers into sums of sin/cos of
 *      multiple angles (trig_linearize) and integrate the result;
 *   2. derivative-divides on the sin/cos form (e.g. u = cos x for sin^3 x);
 *   3. expand multiple angles (sin 2x -> 2 sin x cos x) and retry derivative-divides;
 *   4. Weierstrass substitution for rational functions of sin u, cos u with u = a x + b:
 *      T = tan(u/2), sin u = 2T/(1+T^2), cos u = (1-T^2)/(1+T^2), dx = 2 dT/(a (1+T^2));
 *      the rational T-integral goes to int_rational.
 * The Weierstrass form jumps at u = pi (mod 2 pi); integrate_def detects that through its
 * numeric cross-check.
 */
static E *int_trig(E *f, E *x, int depth) {
    E *g = trig_sincos(f);
    if (trig_poly(g)) {
        E *l = trig_linearize(g);
        if (!expr_eq(l, f)) {
            E *r = ir(l, x, depth + 1);
            if (r) return r;
        }
    }
    if (!expr_eq(g, f)) {
        E *r = deriv_divides(g, x, depth);
        if (r) return r;
    }
    {
        /* multiple angles: sin(2x) -> 2 sin x cos x, then substitute */
        E *te = trig_expand(f);
        if (!expr_eq(te, f) && expr_size(te) < 300) {
            E *r = deriv_divides(te, x, depth + 1);
            if (r) return r;
        }
    }
    /* Weierstrass: rational function of sin(u), cos(u) with u linear */
    E *u = NULL, *a, *b;
    if (trig_args_same(g, &u) && u && lin(u, x, &a, &b)) {
        E *T = tsym(depth);
        E *den = s_add2(K->c1, s_powi(T, 2));
        E *sx[2] = {s_fn1(F_SIN, u), s_fn1(F_COS, u)};
        E *sv[2] = {s_div(s_mul2(K->c2, T), den), s_div(s_sub(K->c1, s_powi(T, 2)), den)};
        E *h = subst_many(g, 2, sx, sv);
        if (!freeof(h, x)) return NULL;
        h = rat_normal(s_mul2(h, s_div(K->c2, s_mul2(a, den))));
        if (is_rational_in(h, T)) {
            E *G = int_rational(h, T);
            if (G) return subst(G, T, s_fn1(F_TAN, s_mul2(K->chalf, u)));
        }
    }
    return NULL;
}

/* exp * trig with complex exponentials */
/* has_exp -- does e contain an exponential e^(...)?  (identical to has_exp_any) */
static int has_exp(E *e) {
    stack_check();
    if (e->t == E_POW && ISYM(K0(e), S_E)) return 1;
    if (e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_exp(EK(e)[i])) return 1;
    return 0;
}

/*
 * int_exp_trig -- integrands mixing exponentials and trig functions (e^x sin x): rewrite
 * the trig functions as complex exponentials, expand, integrate the sum of exponentials,
 * then convert e^(a + i b) back to e^a (cos b + i sin b) and expand.
 */
static E *int_exp_trig(E *f, E *x, int depth) {
    if (!has_exp(f) || !has_trig(f)) return NULL;
    E *g = expand(trig_to_exp(f));
    E *r = ir(g, x, depth + 1);
    if (!r) return NULL;
    return expand(exp_to_trig(expand(r)));
}

/* has_hyp -- does e contain sinh, cosh or tanh? */
/* e contains piecewise (only int_heav may integrate it: an antiderivative must stay continuous) */
static int has_pw(E *e) {
    stack_check();
    if (e->t == E_FN && e->s == F_PIECEWISE) return 1;
    if (e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_pw(EK(e)[i])) return 1;
    return 0;
}

static int has_hyp(E *e) {
    stack_check();
    if (e->t == E_FN && (e->s == F_SINH || e->s == F_COSH || e->s == F_TANH)) return 1;
    if (e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_hyp(EK(e)[i])) return 1;
    return 0;
}

/* c^u with numeric base c != e and u depending on x */
static int has_numbase_pow(E *e, E *x) {
    stack_check();
    if (e->t == E_POW && is_num(K0(e)) && !freeof(K1(e), x)) return 1;
    if (e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_numbase_pow(EK(e)[i], x)) return 1;
    return 0;
}

/* numbase_to_exp -- rewrite every c^u (numeric c, u depending on x) as e^(u ln c) */
static E *numbase_to_exp(E *e, E *x) {
    stack_check();
    if (e->t == E_POW && is_num(K0(e)) && !freeof(K1(e), x))
        return s_exp(s_mul2(numbase_to_exp(K1(e), x), s_ln(K0(e))));
    if (e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR) return e;
    E **k = salloc(sizeof(E *) * (e->n + 1));
    for (uint32_t i = 0; i < e->n; i++) k[i] = numbase_to_exp(EK(e)[i], x);
    return rebuild(e, k);
}

/* has_exp_any -- does e contain an exponential e^(...)?  Gates the e^g strategies in ir() */
static int has_exp_any(E *e) {
    stack_check();
    if (e->t == E_POW && ISYM(K0(e), S_E)) return 1;
    if (e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_exp_any(EK(e)[i])) return 1;
    return 0;
}

/* ------------------------------------------------------------- driver --- */
/*
 * TRY_STRAT(call): run one strategy inside ir(); on success return its result from ir(),
 * otherwise release all scratch the attempt allocated and fall through to the next one.
 * Assigns ir()'s local variable r.
 */
#define TRY_STRAT(call)              \
    do {                             \
        char *m_ = smark();          \
        r = (call);                  \
        if (r) return r;             \
        srelease(m_);                \
    } while (0)

/*
 * heav_rw -- rewrite piecewise(x < a, e1, e2) (also <=, >, >=, a < x) as
 * e2 + (e1 - e2) H(a - x), sign(u) as 2 H(u) - 1 and abs(u) as u (2 H(u) - 1)
 * for u linear in x; int_heav then integrates g H(s x + b) continuously.
 */
static E *heav_rw(E *e, E *x) {
    stack_check();
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN) return e;
    E **k = salloc(sizeof(E *) * e->n), *s, *b;
    for (uint32_t i = 0; i < e->n; i++) k[i] = heav_rw(EK(e)[i], x);
    if (e->t == E_FN && e->n == 3 && e->s == F_PIECEWISE) {
        E *c = k[0];
        int op = c->t == E_FN && c->n == 2 ? c->s : 0;
        if ((op == F_LT || op == F_LE || op == F_GT || op == F_GE) && !freeof(c, x)) {
            /* u = rhs - lhs: the condition is u > 0 for < and <=, u < 0 for > and >= */
            E *u = s_sub(K1(c), K0(c));
            if (op == F_GT || op == F_GE) u = s_neg(u);
            if (lin(u, x, &s, &b)) return s_add2(k[2], s_mul2(s_sub(k[1], k[2]), s_fn1(F_HEAVISIDE, u)));
        }
    }
    if (e->t == E_FN && e->n == 1 && (e->s == F_SIGN || e->s == F_ABS) && lin(k[0], x, &s, &b)) {
        E *sg = s_sub(s_mul2(K->c2, s_fn1(F_HEAVISIDE, k[0])), K->c1);
        return e->s == F_SIGN ? sg : s_mul2(k[0], sg);
    }
    return rebuild(e, k);
}

static E *int_heav(E *f, E *x, int depth) {
    if (f->t != E_MUL) return NULL;
    E *h = NULL, **v = salloc(sizeof(E *) * f->n), *s, *b;
    uint32_t nv = 0;
    for (uint32_t i = 0; i < f->n; i++)
        if (!h && IFN(EK(f)[i], F_HEAVISIDE) && EK(f)[i]->n == 1 && lin(K0(EK(f)[i]), x, &s, &b)) h = K0(EK(f)[i]);
        else v[nv++] = EK(f)[i];
    double sv;
    if (!h || !to_double(s, &sv) || sv == 0) return NULL;
    E *G = ir(s_mul(nv, v), x, depth + 1);
    if (!G) return NULL;
    E *d = s_sub(G, subst(G, x, s_neg(s_div(b, s))));
    return sv > 0 ? s_mul2(s_fn1(F_HEAVISIDE, h), d) : s_sub(G, s_mul2(s_fn1(F_HEAVISIDE, s_neg(h)), d));
}

/*
 * ir -- recursive integration driver.  Returns an antiderivative of f with respect to x
 * (scratch) or NULL.
 *   depth  nesting level of rewrites and substitutions; depth > 8 fails.  It also selects
 *          the substitution variable tsym(depth) and gates expensive strategies (heurisch
 *          only at depth <= 1, exponential rewriting of c^u and hyperbolics below 6, ...).
 *   g_budget  remaining calls for this integrate(); each call consumes one, which cuts
 *          off the otherwise exponential search tree.
 * The strategies are listed in the file header.  Each is bracketed by smark/srelease so a
 * failed attempt leaves no garbage; errors thrown inside a strategy are not caught here
 * (ir_top turns them into "not found").
 */
static E *ir(E *f, E *x, int depth) {
    stack_check();
    if (--g_budget < 0 || depth > 8) return NULL;
    if (freeof(f, x)) return s_mul2(f, x);
    E *r = table(f, x);
    if (r) return r;
    if (has_exp_any(f)) TRY_STRAT(int_exp_poly(f, x));
    if (has_exp_any(f)) TRY_STRAT(int_xk_gauss(f, x, depth));
    /* linearity: every term must integrate, else fall through (the sum as a whole may
     * still succeed, e.g. after rewriting, by parts, or by heurisch) */
    if (f->t == E_ADD) {
        char *m0 = smark();
        E **t = salloc(sizeof(E *) * f->n);
        int ok = 1;
        for (uint32_t i = 0; i < f->n && ok; i++) {
            t[i] = ir(EK(f)[i], x, depth);
            if (!t[i]) ok = 0;
        }
        if (ok) return s_add(f->n, t);
        srelease(m0);
    }
    if (f->t == E_MUL) {
        E **c = salloc(sizeof(E *) * f->n), **v = salloc(sizeof(E *) * f->n);
        uint32_t nc = 0, nv = 0;
        for (uint32_t i = 0; i < f->n; i++) {
            if (freeof(EK(f)[i], x)) c[nc++] = EK(f)[i];
            else v[nv++] = EK(f)[i];
        }
        /* c * g with c free of x: the result is decided by g alone */
        if (nc) {
            E *g = ir(s_mul(nv, v), x, depth);
            return g ? s_mul2(s_mul(nc, c), g) : NULL;
        }
    }
    TRY_STRAT(int_heav(f, x, depth));
    if (depth < 6) {
        char *m_ = smark();
        E *g = heav_rw(f, x);
        if (!expr_eq(g, f)) {
            g = expand(g);
            r = ir(g, x, depth + 1);
            if (r) return r;
        }
        srelease(m_);
    }
    if (has_pw(f)) return NULL;
    if (is_rational_in(f, x)) {
        char *m_ = smark();
        g_split = depth ? -1 : 0; /* split over the reals only for the integrand itself */
        r = int_rational(f, x);
        g_split = 0;
        if (r && expr_size(r) > 30 && depth < 4) {
            /* a substitution may give a much simpler form (e.g. x/(x^4+1)) */
            E *r2 = deriv_divides(f, x, depth);
            if (r2 && expr_size(r2) < expr_size(r)) return r2;
        }
        if (r) return r;
        srelease(m_);
    }
    TRY_STRAT(parts(f, x, depth));
    TRY_STRAT(deriv_divides(f, x, depth));
    if (has_trig(f)) TRY_STRAT(int_trig(f, x, depth));
    TRY_STRAT(int_sqrt_quad(f, x));
    TRY_STRAT(int_euler(f, x, depth));
    /* expand products and powers (if not too large) and retry */
    {
        char *m_ = smark();
        E *ex = expr_size(f) < 400 ? expand(f) : f;
        if (!expr_eq(ex, f)) {
            r = ir(ex, x, depth + 1);
            if (r) return r;
        }
        srelease(m_);
    }
    TRY_STRAT(inverse_subs(f, x, depth));
    if (has_numbase_pow(f, x) && depth < 6) {
        char *m_ = smark();
        E *g = numbase_to_exp(f, x);
        if (!expr_eq(g, f)) {
            r = ir(g, x, depth + 1);
            if (r) return r;
        }
        srelease(m_);
    }
    TRY_STRAT(int_exp_trig(f, x, depth));
    if (has_hyp(f) && depth < 6) {
        char *m_ = smark();
        E *g = expand(trig_to_exp(f));
        if (!expr_eq(g, f)) {
            r = ir(g, x, depth + 1);
            if (r) return r;
        }
        srelease(m_);
    }
    if (depth <= 1) TRY_STRAT(heurisch(f, x));
    return NULL;
}

/* ir(f, x, 0) with a budget of `budget` calls; an error thrown by a strategy (exponent
 * too large, out of memory, ...) other than an interrupt means "not found" (NULL), and
 * the scratch of the attempt is released. */
static E *ir_top(E *f, E *x, int budget) {
    char *m = smark();
    g_budget = budget;
    g_split = 0;
    E *volatile r = NULL;
    tryf tf;
    try_enter(&tf);
    if (!setjmp(tf.jb)) {
        r = ir(f, x, 0);
        try_leave(&tf);
    } else {
        try_leave(&tf);
        if (K->err == CAS_ERR_INTERRUPTED) rethrow();
        srelease(m);
        r = NULL;
    }
    return r;
}

/*
 * fold_const_args -- function arguments that are secretly constant, e.g.
 * atan((x+1)/(3x+3)), would be treated as kernels with zero derivative; fold them first.
 * Each x-dependent sum/product/power argument of a function is replaced by its rational
 * normal form when that is free of x.  Recursion stops at depth 60; unchanged subtrees
 * are shared.
 */
static E *fold_const_args(E *e, E *x, int depth) {
    stack_check();
    if (depth > 60 || freeof(e, x)) return e;
    if (!(e->t == E_ADD || e->t == E_MUL || e->t == E_POW || e->t == E_FN)) return e;
    E **k = salloc(sizeof(E *) * e->n);
    int ch = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        E *a = fold_const_args(EK(e)[i], x, depth + 1);
        if (e->t == E_FN && !freeof(a, x) && (a->t == E_ADD || a->t == E_MUL || a->t == E_POW)) {
            E *n = rat_normal(a);
            if (freeof(n, x)) a = n;
        }
        k[i] = a;
        ch |= a != EK(e)[i];
    }
    return ch ? rebuild(e, k) : e;
}

/* The casx integrator (CASX_FN_INTEGRATE, cas/ext/int) after the core's own search, nested at
 * most 2 deep; an error in it other than an interrupt means "not found". */
static E *x_slot(E *f, E *x) {
    if (!CASX_SLOTS || g_xdepth >= 2) return NULL;
    char *m = smark();
    E *volatile r = NULL;
    E *v[2] = {f, x};
    tryf tf;
    g_xdepth++;
    try_enter(&tf);
    if (!setjmp(tf.jb)) {
        r = casx_eslot(CASX_FN_INTEGRATE, 2, v);
        try_leave(&tf);
    } else {
        try_leave(&tf);
        g_xdepth--;
        if (K->err == CAS_ERR_INTERRUPTED) rethrow();
        srelease(m);
        return NULL;
    }
    g_xdepth--;
    return r;
}

/*
 * integrate -- antiderivative of f with respect to the symbol x, or NULL if none was found.
 * Lists are integrated elementwise, equations side by side (NULL if any part fails).
 *   1. fold constant function arguments (fold_const_args; an error there other than an
 *      interrupt is ignored and f is used unchanged); x-free f gives f*x;
 *   2. ir() with a budget of 400 calls;
 *   3. if that fails and f is a sum, integrate the terms separately (budget 200 each) and
 *      return sum(done) + integrate(rest, x), where the inert integrate(rest, x) node is
 *      built raw with mk_fn and flagged F_SIMP so it is not simplified again.  NULL if no
 *      term could be integrated.
 * The result lives in scratch.  Side effects: resets the file-static g_budget (not
 * reentrant), may intern helper symbols (_t00, _z, ...).  Errors inside the search count
 * as "not found" (ir_top); only interrupts propagate.
 */
E *integrate(E *f, E *x) {
    stack_check();
    if (f->t != E_LIST && !IFN(f, F_EQ)) {
        tryf tf;
        try_enter(&tf);
        E *volatile g = f;
        if (!setjmp(tf.jb)) {
            g = fold_const_args(f, x, 0);
            try_leave(&tf);
        } else {
            try_leave(&tf);
            if (K->err == CAS_ERR_INTERRUPTED) rethrow();
        }
        f = g;
        if (freeof(f, x)) return s_mul2(f, x);
    }
    if (f->t == E_LIST) {
        E **v = salloc(sizeof(E *) * (f->n + 1));
        for (uint32_t i = 0; i < f->n; i++) {
            v[i] = integrate(EK(f)[i], x);
            if (!v[i]) return NULL;
        }
        return s_list(f->n, v);
    }
    if (IFN(f, F_EQ)) {
        E *a = integrate(K0(f), x), *b = integrate(K1(f), x);
        return a && b ? s_fn2(F_EQ, a, b) : NULL;
    }
    E *r = ir_top(f, x, 400);
    if (!r) r = x_slot(f, x);
    if (r && CASX_SLOTS && !g_xdepth) { /* the casx module tidies the antiderivative (cas/ext/int/tidy.c) */
        E *v[3] = {f, x, r}, *t;
        g_xdepth = 2;
        t = casx_eslot(CASX_FN_INTEGRATE, 3, v);
        g_xdepth = 0;
        if (t) r = t;
    }
    if (r || f->t != E_ADD) return r;
    /* partial result: integrate what we can, keep the rest inert */
    E **done = salloc(sizeof(E *) * (f->n + 1)), **left = salloc(sizeof(E *) * (f->n + 1));
    uint32_t nd = 0, nl = 0;
    for (uint32_t i = 0; i < f->n; i++) {
        E *t = ir_top(EK(f)[i], x, 200);
        if (!t) t = x_slot(EK(f)[i], x);
        if (t) done[nd++] = t;
        else left[nl++] = EK(f)[i];
    }
    if (!nd) return NULL;
    E *v[2] = {s_add(nl, left), x};
    E *inert = mk_fn(C_INTEGRATE, 2, v);
    inert->f = F_SIMP;
    done[nd++] = inert;
    return s_add(nd, done);
}

/* ------------------------------------------------------- numerics --- */
/*
 * Gauss-Kronrod 7/15 rule on [-1, 1] (QUADPACK's QK15).  gk_x: the non-negative Kronrod
 * nodes, gk_x[7] = 0 being the centre; the odd-indexed nodes gk_x[1], [3], [5] and the
 * centre are the 7-point Gauss nodes.  gk_wk: Kronrod weights for gk_x[0..7]; gk_wg: Gauss
 * weights for gk_x[1], [3], [5], [7].  |Kronrod - Gauss| serves as the error estimate.
 */
static const double gk_x[8] = {0.991455371120812639, 0.949107912342758525, 0.864864423359769073,
                               0.741531185599394440, 0.586087235467691130, 0.405845151377397167,
                               0.207784955007898468, 0.000000000000000000};
static const double gk_wk[8] = {0.022935322010529225, 0.063092092629978553, 0.104790010322250184,
                                0.140653259715525919, 0.169004726639267903, 0.190350578064785410,
                                0.204432940075298892, 0.209482141084727828};
static const double gk_wg[4] = {0.129484966168869693, 0.279705391489276668, 0.381830050505118945,
                                0.417959183673469388};

/* quadrature context: integrand f in the symbol with id var, range mapping (mode), and
 * a count of points where f could not be evaluated */
typedef struct {
    E *f;
    int var;
    int mode; /* 0 finite, 1 [a,inf), 2 (-inf,b], 3 (-inf,inf) */
    double a, b;
    int fails;
} qctx;

/*
 * qf -- integrand at quadrature parameter t, times the Jacobian of the range mapping:
 *   mode 0  finite:      x = t
 *   mode 1  [a, inf):    x = a + t/(1-t),  dx/dt = 1/(1-t)^2,          t in [0, 1)
 *   mode 2  (-inf, b]:   x = b - (1-t)/t,  dx/dt = 1/t^2,              t in (0, 1]
 *   mode 3  (-inf, inf): x = t/(1-t^2),    dx/dt = (1+t^2)/(1-t^2)^2,  t in (-1, 1)
 * Kronrod nodes are interior to each subinterval, so the singular ends of the maps are
 * never evaluated.  A point where f cannot be evaluated or is NaN/inf (v - v != 0)
 * contributes 0 and increments q->fails.
 */
static double qf(qctx *q, double t) {
    double x, w = 1, v;
    switch (q->mode) {
    case 1: x = q->a + t / (1 - t); w = 1 / ((1 - t) * (1 - t)); break;
    case 2: x = q->b - (1 - t) / t; w = 1 / (t * t); break;
    case 3: x = t / (1 - t * t); w = (1 + t * t) / ((1 - t * t) * (1 - t * t)); break;
    default: x = t;
    }
    if (!eval_double_at(q->f, q->var, x, &v) || v != v || v - v != 0) {
        q->fails++;
        return 0;
    }
    return v * w;
}

/*
 * gk15 -- 15-point Kronrod estimate of the integral of qf over [a, b] (15 evaluations);
 * *err = |Kronrod - Gauss(7)| scaled to the interval.
 */
static double gk15(qctx *q, double a, double b, double *err) {
    double c = (a + b) / 2, h = (b - a) / 2;
    double fc = qf(q, c);
    double rk = fc * gk_wk[7], rg = fc * gk_wg[3];
    for (int i = 0; i < 7; i++) {
        double dx = h * gk_x[i];
        double f1 = qf(q, c - dx), f2 = qf(q, c + dx);
        rk += gk_wk[i] * (f1 + f2);
        if (i & 1) rg += gk_wg[i / 2] * (f1 + f2);
    }
    *err = fabs((rk - rg) * h);
    return rk * h;
}

/*
 * adapt_global -- global adaptive Gauss-Kronrod (the QAG strategy of QUADPACK): keep all
 * subintervals with their estimates and repeatedly bisect the one with the largest error
 * until the summed error is <= tol*(1 + |total|) or < 1e-14 |total|.  Gives up early when
 * 599 subintervals are in use or the worst one is narrower than ~1e-13 relative (a
 * singularity or jump); *bad is then set if the error is still above 1e-6 relative.
 * Uses four scratch arrays of 600 doubles; each bisection rescans the list (O(n^2) total
 * for n <= 600 intervals).  Polls for interrupts.
 */
static double adapt_global(qctx *q, double lo, double hi, double tol, int *bad) {
    enum { MAXI = 600 };
    double *ia = salloc(sizeof(double) * MAXI), *ib = salloc(sizeof(double) * MAXI);
    double *iv = salloc(sizeof(double) * MAXI), *ie = salloc(sizeof(double) * MAXI);
    int ni = 0;
    if (q->mode == 0 && hi > lo) {
        /* finite range: start from a partition graded geometrically toward both ends
         * (widths (hi-lo) 2^-k, k <= 24), so that a narrow peak or singularity at an
         * endpoint, e.g. x^1000000 e^x on [0, 1], is not missed by the 15 interior nodes */
        double w = hi - lo, cut[2 * 24 + 2];
        int nc = 0;
        for (int k = 24; k >= 1; k--) cut[nc++] = lo + w * ldexp(1, -k);
        for (int k = 1; k <= 24; k++) cut[nc++] = hi - w * ldexp(1, -k);
        double prev = lo;
        for (int i = 0; i <= nc; i++) {
            double c = i < nc ? cut[i] : hi;
            if (!(c > prev)) continue;
            ia[ni] = prev;
            ib[ni] = c;
            iv[ni] = gk15(q, prev, c, &ie[ni]);
            ni++;
            prev = c;
        }
    }
    if (!ni) {
        ia[0] = lo;
        ib[0] = hi;
        iv[0] = gk15(q, lo, hi, &ie[0]);
        ni = 1;
    }
    for (int it = 0; it < 4000; it++) {
        double tot = 0, err = 0;
        int w = 0;
        for (int i = 0; i < ni; i++) {
            tot += iv[i];
            err += ie[i];
            if (ie[i] > ie[w]) w = i;
        }
        if (err <= tol * (1 + fabs(tot)) || err < 1e-14 * fabs(tot)) return tot;
        if (ni >= MAXI - 1 || ib[w] - ia[w] < 1e-13 * (1 + fabs(ia[w]))) {
            *bad = err > 1e-6 * (1 + fabs(tot));
            return tot;
        }
        poll_intr();
        double m = (ia[w] + ib[w]) / 2;
        ia[ni] = m;
        ib[ni] = ib[w];
        iv[ni] = gk15(q, m, ib[w], &ie[ni]);
        ib[w] = m;
        iv[w] = gk15(q, ia[w], m, &ie[w]);
        ni++;
    }
    *bad = 1;
    return 0;
}

/*
 * nint_double -- double-precision value of int_a^b f dx; x must be a symbol.  a and b are
 * numeric expressions, inf, or a negative term containing inf (-inf).  Supported ranges:
 * finite (either order), [a, inf), (-inf, b], (-inf, inf), and inf as the lower bound with
 * finite b (computed as -int_b^inf).  Relative tolerance 1e-11.  Returns 1 with *out, or 0
 * if a bound is not numeric, the range combination is unsupported, f failed to evaluate
 * at more than two points, or the error estimate stayed too large.  May throw on
 * interrupt / out of memory.  Used by integrate_def as an independent check.
 */
int nint_double(E *f, E *x, E *a, E *b, double *out) {
    qctx q;
    q.f = f;
    q.var = x->s;
    q.fails = 0;
    int ainf = 0, binf = 0;
    double av = 0, bv = 0;
    if (ISYM(a, S_INF)) ainf = 1;
    else if (is_negative_term(a) && !freeof_sym(a, S_INF)) ainf = -1;
    if (ISYM(b, S_INF)) binf = 1;
    else if (is_negative_term(b) && !freeof_sym(b, S_INF)) binf = -1;
    if (!ainf && !to_double(a, &av)) return 0;
    if (!binf && !to_double(b, &bv)) return 0;
    double lo, hi, sign = 1;
    if (!ainf && !binf) {
        q.mode = 0;
        lo = av;
        hi = bv;
    } else if (!ainf && binf == 1) {
        q.mode = 1;
        q.a = av;
        lo = 0;
        hi = 1;
    } else if (ainf == -1 && !binf) {
        q.mode = 2;
        q.b = bv;
        lo = 0;
        hi = 1;
    } else if (ainf == -1 && binf == 1) {
        q.mode = 3;
        lo = -1;
        hi = 1;
    } else if (ainf == 1 && !binf) {
        q.mode = 1;
        q.a = bv;
        lo = 0;
        hi = 1;
        sign = -1;
    } else
        return 0;
    int bad = 0;
    double r = adapt_global(&q, lo, hi, 1e-11, &bad);
    if (q.fails > 2 || bad) return 0;
    *out = sign * r;
    return 1;
}

/* nintegrate -- the nint(f, x, a, b) command: a float, or CAS_ERR_DOMAIN if x is not a
 * symbol or nint_double fails */
E *nintegrate(E *f, E *x, E *a, E *b) {
    double r;
    if (x->t != E_SYM || !nint_double(f, x, a, b, &r)) cas_throw(CAS_ERR_DOMAIN, "numeric integration failed");
    return mk_flt(r);
}

/*
 * eval_at -- value of the antiderivative F at x = v for integrate_def.  Direct
 * substitution is used when it succeeds without undef/inf and without introducing floats
 * (unless v is a float itself); otherwise, or if substitution throws, the one-sided limit
 * in direction dir (+1 from the right at the lower bound, -1 from the left at the upper
 * bound) via limit_expr, which may return NULL.  Interrupts are rethrown.
 */
/* fn_sing_at -- 1 if a function application in F is singular at x = v (numeric
 * but not finite there, like Ci(0)); substitution could cancel it away
 * (Ci(3x) - Ci(x) at 0 would give 0 instead of the limit ln 3) */
static int fn_sing_at(E *F, E *x, E *v) {
    stack_check();
    if (freeof(F, x) || F->t == E_INT || F->t == E_RAT || F->t == E_FLT || F->t == E_SYM || F->t == E_STR) return 0;
    for (uint32_t i = 0; i < F->n; i++)
        if (fn_sing_at(EK(F)[i], x, v)) return 1;
    if (F->t == E_FN) {
        E *w = subst(F, x, v);
        double re, im;
        if (!freeof_sym(w, S_INF) || !freeof_sym(w, S_UNDEF)) return 1;
        if (is_const_expr(w) && (!eval_complex(w, &re, &im) || !isfinite(re) || !isfinite(im))) return 1;
    }
    return 0;
}

static E *eval_at(E *F, E *x, E *v, int dir) {
    /* value of F at x=v, via limit if direct substitution fails */
    tryf tf;
    try_enter(&tf);
    E *volatile r = NULL;
    if (!setjmp(tf.jb)) {
        if (!ISYM(v, S_INF) && freeof_sym(v, S_INF) && !fn_sing_at(F, x, v)) {
            r = subst(F, x, v);
            if (!freeof_sym(r, S_UNDEF) || !freeof_sym(r, S_INF) || has_float(r) != has_float(v)) r = NULL;
        }
        try_leave(&tf);
    } else {
        try_leave(&tf);
        if (K->err == CAS_ERR_INTERRUPTED) rethrow();
        r = NULL;
    }
    if (r) return r;
    return limit_expr(F, x, v, dir);
}

/* Collect |w| for the sin/cos arguments w x + c of e (at most 8 distinct values in ws). */
static void trig_freqs(E *e, E *x, double *ws, int *n) {
    stack_check();
    if (freeof(e, x) || e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR)
        return;
    E *a, *b;
    double w;
    if (e->t == E_FN && (e->s == F_SIN || e->s == F_COS) && e->n == 1 && lin(K0(e), x, &a, &b) &&
        to_double(a, &w) && w != 0) {
        int dup = 0;
        for (int i = 0; i < *n; i++)
            if (fabs(ws[i] - fabs(w)) <= 1e-12 * fabs(w)) dup = 1;
        if (!dup && *n < 8) ws[(*n)++] = fabs(w);
    }
    for (uint32_t i = 0; i < e->n; i++) trig_freqs(EK(e)[i], x, ws, n);
}

/*
 * interior_poles -- real zeros of the denominator of f (tan, sec, ... rewritten in sin/cos)
 * strictly inside (a, b), sorted and deduplicated, at most cap of them; returns the count.
 * Zeros come from solve_expr.  If the denominator contains trig functions every solution
 * is also shifted by k*pi/w across (a, b) for each frequency w of a sin/cos argument
 * w x + c (w = 1 if there is none); a shifted point is kept only if the denominator is
 * numerically zero there relative to nearby values (the caller tests f numerically
 * anyway).  An error in numden/solve other than an interrupt yields 0 poles.
 */
static uint32_t interior_poles(E *f, E *x, double a, double b, double *out, uint32_t cap) {
    uint32_t n = 0;
    E *den;
    tryf tf;
    try_enter(&tf);
    if (!setjmp(tf.jb)) {
        E *num;
        numden(trig_sincos(f), &num, &den);
        try_leave(&tf);
    } else {
        try_leave(&tf);
        if (K->err == CAS_ERR_INTERRUPTED) rethrow();
        return 0;
    }
    if (freeof(den, x)) return 0;
    E *sols;
    try_enter(&tf);
    if (!setjmp(tf.jb)) {
        sols = solve_expr(den, x, 0);
        try_leave(&tf);
    } else {
        try_leave(&tf);
        if (K->err == CAS_ERR_INTERRUPTED) rethrow();
        return 0;
    }
    double ws[8];
    int nw = 0;
    if (has_trig(den)) {
        trig_freqs(den, x, ws, &nw);
        if (!nw) ws[nw++] = 1;
    }
    for (uint32_t i = 0; i < sols->n; i++) {
        double r;
        if (!to_double(EK(sols)[i], &r)) continue;
        /* w == nw: the solution itself; else shifts by k pi/ws[w] */
        for (int w = 0; w <= nw; w++) {
            double st = w < nw ? M_PI / ws[w] : 0, k0 = 0, k1 = 0;
            if (w < nw) {
                k0 = ceil((a - r) / st);
                k1 = fmin(floor((b - r) / st), k0 + 4000);
            }
            for (double k = k0; k <= k1; k++) {
                double v = r + k * st, d0, d1, d2;
                if (!(v > a + 1e-12 * (1 + fabs(a)) && v < b - 1e-12 * (1 + fabs(b)) && n < cap)) continue;
                if (w < nw &&
                    !(eval_double_at(den, x->s, v, &d0) && eval_double_at(den, x->s, v + st / 4, &d1) &&
                      eval_double_at(den, x->s, v - st / 3, &d2) && fabs(d0) <= 1e-6 * (fabs(d1) + fabs(d2))))
                    continue;
                int dup = 0;
                for (uint32_t j = 0; j < n; j++)
                    if (fabs(out[j] - v) < 1e-9) dup = 1;
                if (!dup) out[n++] = v;
            }
        }
    }
    for (uint32_t i = 1; i < n; i++)
        for (uint32_t j = i; j > 0 && out[j - 1] > out[j]; j--) {
            double t = out[j]; out[j] = out[j - 1]; out[j - 1] = t;
        }
    return n;
}

/* Up to cap distinct arguments u (containing x) of sign(u) nodes in e. */
static void sign_args(E *e, E *x, E **us, uint32_t *n, uint32_t cap) {
    stack_check();
    if (freeof(e, x) || e->t == E_SYM) return;
    if (IFN(e, F_SIGN) && e->n == 1) {
        for (uint32_t i = 0; i < *n; i++)
            if (expr_eq(us[i], K0(e))) return;
        if (*n < cap) us[(*n)++] = K0(e);
    }
    for (uint32_t i = 0; i < e->n; i++) sign_args(EK(e)[i], x, us, n, cap);
}

/*
 * sign_jumps -- r = F(b) - F(a) misses the jumps of sign(u) inside F (the
 * antiderivative of |x| cos x contains sign(x)): add F(c-) - F(c+) for every
 * zero c of u strictly between a and b (subtract it for a > b).  NULL when a
 * zero cannot be placed relative to a and b (sign_of unknown), a limit
 * fails, or solve throws; interrupts are rethrown.
 */
static E *sign_jumps(E *F, E *x, E *a, E *b, E *r) {
    E *us[4];
    uint32_t nu = 0;
    sign_args(F, x, us, &nu, 4);
    tryf tf;
    try_enter(&tf);
    E *volatile res = r;
    if (!setjmp(tf.jb)) {
        for (uint32_t i = 0; i < nu && res; i++) {
            E *cs = solve_expr(us[i], x, 0);
            for (uint32_t j = 0; j < cs->n && res; j++) {
                E *c = EK(cs)[j], *ca = s_sub(c, a), *bc = s_sub(b, c);
                int d1 = sign_of(ca), d2 = sign_of(bc);
                if (is_zero(ca) || is_zero(bc) || d1 * d2 < 0) continue;
                E *L = d1 ? limit_expr(F, x, c, -1) : NULL, *R = L ? limit_expr(F, x, c, 1) : NULL;
                res = R ? s_add2(res, s_mul2(mk_int(d1), s_sub(L, R))) : NULL;
            }
        }
        try_leave(&tf);
    } else {
        try_leave(&tf);
        if (K->err == CAS_ERR_INTERRUPTED) rethrow();
        res = NULL;
    }
    return res;
}

/*
 * sym_pole_check -- symbolic bounds a < b: for each zero s of the denominator
 * (from solve_expr) with s - a and b - s positive constants, as in
 * int 1/(x-c) dx from c-1 to c+1, f(s + t) is evaluated at t = +-1e-7 with
 * every other symbol set to a generic value, and a large or undefined value
 * throws the same divergence error as the numeric case.  Errors other than
 * interrupts make the check a no-op.
 */
static void sym_pole_check(E *f, E *x, E *a, E *b) {
    tryf tf;
    volatile int bad = 0;
    try_enter(&tf);
    if (!setjmp(tf.jb)) {
        E *num, *den;
        numden(f, &num, &den);
        E *sols = freeof(den, x) ? s_list(0, NULL) : solve_expr(den, x, 0);
        for (uint32_t i = 0; i < sols->n; i++) {
            E *s = EK(sols)[i], *sy[8];
            double d1, d2, h = 1e-7, v[2][2];
            if (!freeof(s, x) || !to_double(s_sub(s, a), &d1) || !to_double(s_sub(b, s), &d2) || d1 <= 0 ||
                d2 <= 0)
                continue;
            E *g = subst(f, x, s_add2(s, x));
            uint32_t ns = collect_syms(g, sy, 8);
            for (uint32_t j = 0; j < ns; j++)
                if (!expr_eq(sy[j], x)) g = subst(g, sy[j], mk_flt(0.5772 + 0.31 * j));
            for (int j = 0; j < 2; j++, h = -h)
                if (!eval_complex_at(g, x->s, h, 0, &v[j][0], &v[j][1]) || hypot(v[j][0], v[j][1]) > 1e5) bad = 1;
        }
        try_leave(&tf);
    } else {
        try_leave(&tf);
        if (K->err == CAS_ERR_INTERRUPTED) rethrow();
    }
    if (bad) cas_throw(CAS_ERR_DOMAIN, "integral diverges (pole inside the interval)");
}

/*
 * integrate_def -- definite integral int_a^b f dx.
 *   1. F = integrate(f, x); a partial result (containing an inert integrate) is dropped;
 *   2. numeric a < b: for every real pole of f inside (a, b), f must stay evaluable and
 *      below 1e5 in magnitude at distance ~1e-7, else CAS_ERR_DOMAIN "integral diverges"
 *      is thrown (removable and mild singularities pass);
 *   3. num = nint_double(...) when possible (errors other than interrupts are ignored);
 *   4. with F: r = F(b-) - F(a+) via eval_at, simplified with simplify_full.  r is accepted
 *      if no numeric value exists, if it agrees with num to 1e-6 relative, or if it is not
 *      a constant expression (parameters or symbolic bounds).  A constant r that disagrees
 *      means F is discontinuous on [a, b] (e.g. a Weierstrass tan(x/2) form) and is
 *      discarded;
 *   5. otherwise the float num, or as a last resort the inert integrate(f, x, a, b) node
 *      flagged F_SIMP.
 */
/*
 * param_mismatch -- for a result r with parameters: 1 if, at generic sample
 * values of the parameters (positive unless assumed negative, integers if
 * assumed integer), r differs from the numeric integral.  Catches
 * antiderivatives that are discontinuous on [a, b] for every parameter value,
 * e.g. the Weierstrass form of int 1/(1 + p cos x) over [0, 2 pi] giving 0.
 * Undecided (no numeric value) counts as no mismatch.
 */
static int param_mismatch(E *f, E *x, E *a, E *b, E *r) {
    E *sy[8], *vs[8];
    uint32_t n = collect_syms(s_list(4, (E *[]){f, a, b, r}), sy, 8), m = 0;
    if (n >= 8) return 0;
    for (uint32_t j = 0; j < n; j++) {
        if (expr_eq(sy[j], x)) continue;
        uint8_t fl = K->syms[sy[j]->s].assume;
        double v = (fl & A_INT) ? 2 + j : 0.5772 + 0.31 * j;
        sy[m] = sy[j];
        vs[m++] = mk_flt((fl & A_NEG) ? -v : v);
    }
    if (!m) return 0;
    int bad = 0;
    tryf tf;
    try_enter(&tf);
    if (!setjmp(tf.jb)) {
        double num, rr, ri;
        E *rv = evalf(subst_many(r, m, sy, vs));
        if (eval_complex(rv, &rr, &ri) &&
            nint_double(subst_many(f, m, sy, vs), x, subst_many(a, m, sy, vs), subst_many(b, m, sy, vs), &num) &&
            isfinite(num) && hypot(rr - num, ri) > 1e-5 * (1 + fabs(num)))
            bad = 1;
        try_leave(&tf);
    } else {
        try_leave(&tf);
        if (K->err == CAS_ERR_INTERRUPTED) rethrow();
    }
    return bad;
}

/* ------------------------------------------ definite: |u| and sign(u) by pieces --- */
/* sq_part -- product of f^(m div 2) over the factors f^m of the polynomial P */
static E *sq_part(E *P, E *x) {
    if (freeof(P, x)) return K->c1;
    E *ct, *fl = factor_list_in(P, x, &ct), *S = K->c1;
    for (uint32_t i = 0; i < fl->n; i++) {
        long m;
        if (num_is_small_int(K1(EK(fl)[i]), &m) && m >= 2) S = s_mul2(S, s_powi(K0(EK(fl)[i]), m / 2));
    }
    return S;
}

/* coef_fn -- T = B * fn(v) with B free of x: stores B and v */
static int coef_fn(E *T, int fn, E *x, E **B, E **v) {
    if (IFN(T, fn) && T->n == 1) return *B = K->c1, *v = K0(T), 1;
    if (T->t != E_MUL) return 0;
    E *c = NULL, **o = salloc(sizeof(E *) * T->n);
    uint32_t no = 0;
    for (uint32_t i = 0; i < T->n; i++)
        if (!c && IFN(EK(T)[i], fn) && EK(T)[i]->n == 1) c = EK(T)[i];
        else o[no++] = EK(T)[i];
    if (!c) return 0;
    *B = s_mul(no, o), *v = K0(c);
    return freeof(*B, x);
}

/*
 * rad_abs -- real-variable rewrite of square roots that hide an absolute value:
 * sqrt(R) = |S| sqrt(R/S^2) for the square part S of a rational R (valid for every
 * real x, also where R < 0: the principal root of c t for t > 0 is sqrt(c) sqrt(t)),
 * and the half-angle forms A - A cos v = 2A sin(v/2)^2, A + A cos v = 2A cos(v/2)^2.
 * Powers with exponent -1/2 the same way.
 */
static E *rad_abs(E *e, E *x) {
    stack_check();
    if (freeof(e, x) || (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN)) return e;
    E **k = salloc(sizeof(E *) * e->n);
    for (uint32_t i = 0; i < e->n; i++) k[i] = rad_abs(EK(e)[i], x);
    e = rebuild(e, k);
    if (e->t != E_POW || (!expr_eq(K1(e), K->chalf) && !expr_eq(K1(e), K->cmhalf))) return e;
    E *R = K0(e), *h2 = s_mul2(K->c2, K1(e)), *Rx = expand(R);
    if (Rx->t == E_ADD && Rx->n == 2)
        for (int j = 0; j < 2; j++) {
            E *A = EK(Rx)[j], *B, *v;
            if (!freeof(A, x) || !coef_fn(EK(Rx)[1 - j], F_COS, x, &B, &v)) continue;
            int fn = is_zero(s_add2(A, B)) ? F_SIN : is_zero(s_sub(A, B)) ? F_COS : 0;
            if (fn)
                return s_pow(s_mul2(s_sqrt(s_mul2(K->c2, A)), s_fn1(F_ABS, s_fn1(fn, s_div(v, K->c2)))), h2);
        }
    if (!is_rational_in(R, x)) return e;
    /* R = S^2 Nt/Dt: sqrt(R) = |S| sqrt(Nt Dt) / |Dt| (Dt^2 > 0) */
    E *N, *D;
    numden(rat_normal(R), &N, &D);
    E *sn = sq_part(N, x), *sd = sq_part(D, x), *S = s_div(sn, sd);
    E *Dt = rat_normal(s_div(D, s_powi(sd, 2))), *Nt = rat_normal(s_div(N, s_powi(sn, 2)));
    if (freeof(S, x) && freeof(Dt, x)) return e;
    return s_pow(s_div(s_mul2(s_fn1(F_ABS, S), s_sqrt(expand(s_mul2(Nt, Dt)))), s_fn1(F_ABS, Dt)), h2);
}

/* Up to cap distinct x-dependent arguments of abs / sign in e; -1 if more. */
static int abs_args(E *e, E *x, E **us, uint32_t *n, uint32_t cap) {
    stack_check();
    if (freeof(e, x) || (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN)) return 0;
    if (e->t == E_FN && (e->s == F_ABS || e->s == F_SIGN) && e->n == 1) {
        uint32_t i = 0;
        while (i < *n && !expr_eq(us[i], K0(e))) i++;
        if (i == *n) {
            if (*n == cap) return -1;
            us[(*n)++] = K0(e);
        }
    }
    for (uint32_t i = 0; i < e->n; i++)
        if (abs_args(EK(e)[i], x, us, n, cap) < 0) return -1;
    return 0;
}

/* trig_step -- every x-dependent node of P is +, *, ^ or a circular function of
 * c x + d with one |c| (numeric): *st = pi/|c| (the zeros repeat with it or a
 * multiple); 0 if P has other x-dependent functions */
static int trig_step(E *P, E *x, E **st) {
    stack_check();
    if (freeof(P, x) || expr_eq(P, x)) return 1;
    if (P->t == E_FN) {
        E *c, *d;
        double cv;
        if (P->n != 1 || P->s < F_SIN || P->s > F_CSC || !lin(K0(P), x, &c, &d) || !to_double(c, &cv)) return 0;
        E *s = s_div(K->pi_, s_fn1(F_ABS, c));
        if (*st && !is_zero(s_sub(*st, s))) return 0;
        *st = s;
        return 1;
    }
    if (P->t != E_ADD && P->t != E_MUL && P->t != E_POW) return 0;
    for (uint32_t i = 0; i < P->n; i++)
        if (!trig_step(EK(P)[i], x, st)) return 0;
    return 1;
}

/* zeros_in -- exact zeros (and poles) of u strictly inside (a, b) appended to pts / pv
 * (more points than the true zeros are harmless: they only split further); 0 if they
 * cannot be found exactly */
static int zeros_in(E *u, E *x, double a, double b, E **pts, double *pv, uint32_t *n, uint32_t cap) {
    E *nd[2];
    numden(u, &nd[0], &nd[1]);
    for (int q = 0; q < 2; q++) {
        E *P = nd[q], *st = NULL;
        if (freeof(P, x)) continue;
        if (!is_rational_in(P, x) && !trig_step(P, x, &st)) return 0;
        E *sols = solve_expr(P, x, 0);
        double stv = 0;
        if (st && !to_double(st, &stv)) return 0;
        for (uint32_t i = 0; i < sols->n; i++) {
            E *s = EK(sols)[i];
            double r, im;
            if (has_float(s) || !freeof(s, x) || !eval_complex(s, &r, &im)) return 0;
            if (fabs(im) > 1e-12 * (1 + fabs(r))) continue;
            double k0 = st ? ceil((a - r) / stv) : 0, k1 = st ? floor((b - r) / stv) : 0;
            for (double kk = k0; kk <= k1; kk++) {
                double v = r + kk * stv;
                if (!(v > a + 1e-12 * (1 + fabs(a)) && v < b - 1e-12 * (1 + fabs(b)))) continue;
                uint32_t j = 0;
                while (j < *n && fabs(pv[j] - v) > 1e-9 * (1 + fabs(v))) j++;
                if (j < *n) continue;
                if (*n == cap) return 0;
                pv[*n] = v;
                pts[(*n)++] = kk != 0 ? s_add2(s, s_mul2(mk_int((int64_t)kk), st)) : s;
            }
        }
    }
    return 1;
}

/* abs(u_i) -> sg_i u_i, sign(u_i) -> sg_i */
static E *abs_fix(E *e, E *x, E **us, int *sg, uint32_t n) {
    stack_check();
    if (freeof(e, x) || (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN)) return e;
    if (e->t == E_FN && (e->s == F_ABS || e->s == F_SIGN) && e->n == 1)
        for (uint32_t i = 0; i < n; i++)
            if (expr_eq(us[i], K0(e))) return e->s == F_SIGN ? mk_int(sg[i]) : s_mul2(mk_int(sg[i]), us[i]);
    E **k = salloc(sizeof(E *) * e->n);
    for (uint32_t i = 0; i < e->n; i++) k[i] = abs_fix(EK(e)[i], x, us, sg, n);
    return rebuild(e, k);
}

static int g_def_split; /* def_split recursion guard */
void integ_reset_statics(void); /* cas.c eval_impl: state an error may have left set */
void integ_reset_statics(void) { g_budget = 0; g_split = 0; g_def_split = 0; g_xdepth = 0; }

/*
 * def_split -- int_a^b f for numeric a < b when f contains |u| or sign(u) (also hidden in
 * a square root, rad_abs): the zeros of every u inside (a, b), found exactly (solve, and
 * shifts by pi/c for circular functions of c x + d), cut [a, b] into pieces on which each
 * u keeps the sign it has at five sample points; each piece is integrated without abs and
 * the sum is accepted only if it agrees with the numeric value num of the whole integral
 * (a missed zero would show there).  NULL otherwise.
 */
static E *def_split(E *f, E *x, E *a, E *b, double av, double bv, double num) {
    E *g = rad_abs(f, x), *us[6], *pts[34];
    double pv[34];
    uint32_t nu = 0, np = 0;
    if (abs_args(g, x, us, &nu, 6) < 0 || !nu) return NULL;
    for (uint32_t i = 0; i < nu; i++)
        if (!zeros_in(us[i], x, av, bv, pts, pv, &np, 32)) return NULL;
    for (uint32_t i = 1; i < np; i++) /* sort the cut points */
        for (uint32_t j = i; j > 0 && pv[j - 1] > pv[j]; j--) {
            double t = pv[j]; pv[j] = pv[j - 1]; pv[j - 1] = t;
            E *te = pts[j]; pts[j] = pts[j - 1]; pts[j - 1] = te;
        }
    E *sum = K->c0;
    for (uint32_t p = 0; p <= np; p++) {
        double lo = p ? pv[p - 1] : av, hi = p < np ? pv[p] : bv;
        int sg[6];
        for (uint32_t i = 0; i < nu; i++) {
            sg[i] = 0;
            for (int t = 1; t <= 9; t += 2) {
                double d;
                if (!eval_double_at(us[i], x->s, lo + (hi - lo) * t / 10, &d) || !isfinite(d) || d == 0) return NULL;
                int s = d > 0 ? 1 : -1;
                if (sg[i] && s != sg[i]) return NULL;
                sg[i] = s;
            }
        }
        E *r = integrate_def(abs_fix(g, x, us, sg, nu), x, p ? pts[p - 1] : a, p < np ? pts[p] : b);
        if (!freeof_sym(r, C_INTEGRATE) || has_float(r)) return NULL;
        sum = s_add2(sum, r);
    }
    sum = simplify_full(sum);
    double v;
    return to_double(sum, &v) && fabs(v - num) <= 1e-6 * (1 + fabs(num)) ? sum : NULL;
}

/* ---------------------------------------- definite: closed forms by shape --- */
static int is_neg_inf(E *e) { return expr_eq(e, s_neg(K->inf_)); }

/*
 * def_gauss -- int P(x) e^(-p x^2 + q x + r) over (-inf, inf) with p > 0 known (sign_of,
 * assumptions) and P a polynomial: x = y + q/(2p) completes the square, and
 * int y^(2k) e^(-p y^2) dy = Gamma(k + 1/2) / p^(k + 1/2) (odd powers give 0).  Over
 * (0, inf) when q = 0: int x^n e^(-p x^2) = Gamma((n+1)/2) / (2 p^((n+1)/2)).
 */
static E *def_gauss(E *f, E *x, E *a, E *b) {
    int half = is_zero(a);
    if (!ISYM(b, S_INF) || (!half && !is_neg_inf(a))) return NULL;
    E *Q = K->c0, *P = K->c1;
    uint32_t n = f->t == E_MUL ? f->n : 1;
    for (uint32_t i = 0; i < n; i++) {
        E *t = f->t == E_MUL ? EK(f)[i] : f;
        if (t->t == E_POW && ISYM(K0(t), S_E)) Q = s_add2(Q, K1(t));
        else P = s_mul2(P, t);
    }
    E *qa, *qb, *qc;
    long d;
    if (!quad(expand(Q), x, &qa, &qb, &qc) || !poly_degree_in(P, x, &d) || d > 20) return NULL;
    E *p = s_neg(qa);
    if (sign_of(p) <= 0 || (half && !is_zero(qb))) return NULL;
    E *m = s_div(qb, s_mul2(K->c2, p)), *Pn = expand(subst(P, x, s_add2(x, m))), *sum = K->c0;
    for (long k = 0; k <= d; k++) {
        if (!half && (k & 1)) continue;
        E *h = mk_frac(k + 1, 2), *t = s_div(s_mul2(poly_coeff(Pn, x, k), s_fn1(F_GAMMA, h)), s_pow(p, h));
        sum = s_add2(sum, half ? s_div(t, K->c2) : t);
    }
    return s_mul2(s_exp(s_add2(qc, s_div(s_powi(qb, 2), s_mul2(mk_int(4), p)))), sum);
}

/*
 * def_euler -- the Euler-gamma integrals over (0, 1): int (1/ln t + 1/(1 - t)) = gamma
 * (the two terms diverge alone, so they must come with equal coefficients) and
 * int ln(ln(1/t)) = -gamma; any other terms are integrated as usual and must give a
 * closed form.
 */
static E *def_euler(E *f, E *x, E *a, E *b) {
    if (!is_zero(a) || !expr_eq(b, K->c1) || f->t != E_ADD) return NULL;
    E *lnx = s_fn1(F_LN, x);
    E *pat[4] = {s_inv(lnx), s_inv(s_sub(K->c1, x)), s_fn1(F_LN, s_neg(lnx)), s_fn1(F_LN, s_fn1(F_LN, s_inv(x)))};
    E *c[3] = {K->c0, K->c0, K->c0}, **rest = salloc(sizeof(E *) * f->n);
    uint32_t nr = 0;
    for (uint32_t i = 0; i < f->n; i++) {
        int j = 0;
        E *q = NULL;
        for (; j < 4; j++)
            if (freeof(q = s_div(EK(f)[i], pat[j]), x)) break;
        if (j == 4) rest[nr++] = EK(f)[i];
        else c[j < 3 ? j : 2] = s_add2(c[j < 3 ? j : 2], q);
    }
    if (is_zero(c[0]) && is_zero(c[2])) return NULL;
    if (!is_zero(s_sub(c[0], c[1]))) return NULL;
    E *g = s_mul2(s_sub(c[0], c[2]), mk_sym(S_EULERG));
    if (!nr) return g;
    E *r = integrate_def(s_add(nr, rest), x, a, b);
    return freeof_sym(r, C_INTEGRATE) && !has_float(r) ? s_add2(g, r) : NULL;
}

/*
 * def_fourier -- int f over (-inf, inf) or (0, inf) for f a combination of sin(w x)/x and
 * cos(w x)/x (numeric w, x-free coefficients; exponentials e^(i w x) and products of
 * circular functions are first rewritten so): Dirichlet's integral
 * int_0^inf sin(w x)/x = pi/2 sign(w), and for the cosines, whose coefficients must sum to
 * 0 (else the integral diverges at 0), Frullani's int_0^inf sum c_j cos(w_j x)/x =
 * -sum c_j ln|w_j|; over (-inf, inf) the sines count twice and the cosines (odd) not at all.
 */
static E *def_fourier(E *f, E *x, E *a, E *b) {
    int half = is_zero(a);
    if (!ISYM(b, S_INF) || (!half && !is_neg_inf(a)) || (!has_trig(f) && !has_exp(f))) return NULL;
    E *g = expand(trig_linearize(expand(exp_to_trig(f))));
    uint32_t n = g->t == E_ADD ? g->n : 1;
    E *sum = K->c0, *csum = K->c0;
    for (uint32_t i = 0; i < n; i++) {
        E *t = s_mul2(g->t == E_ADD ? EK(g)[i] : g, x), *c, *v, *w, *d;
        int fn = coef_fn(t, F_SIN, x, &c, &v) ? F_SIN : coef_fn(t, F_COS, x, &c, &v) ? F_COS : 0;
        double wv;
        if (!fn || !lin(v, x, &w, &d) || !is_zero(d) || !to_double(w, &wv) || wv == 0) return NULL;
        if (fn == F_SIN) sum = s_add2(sum, s_mul2(s_mul2(c, mk_int(wv > 0 ? 1 : -1)), half ? s_div(K->pi_, K->c2) : K->pi_));
        else {
            csum = s_add2(csum, c);
            if (half) sum = s_sub(sum, s_mul2(c, s_fn1(F_LN, s_fn1(F_ABS, w))));
        }
    }
    return is_zero(csum) ? sum : NULL;
}

/*
 * def_residue -- int R(x) T(w x) over (-inf, inf) for R = N/D rational (deg D > deg N,
 * D squarefree with no real root) and T = cos, sin or e^(i .) with a real w != 0 of
 * known sign: by Jordan's lemma int R e^(i w x) = 2 pi i sum Res over the upper
 * half-plane (w > 0), -2 pi i sum over the lower one (w < 0), Res = N(p)/D'(p) e^(i w p);
 * cos and sin are the even and odd combinations of w and -w.  The half-plane of each
 * root is decided by the sign of its imaginary part (assumptions for parameters).
 */
static E *res_sum(E *N, E *D1, E *x, E *w, E **roots, int *up, uint32_t n) {
    int sw = sign_of(w);
    E *sum = K->c0;
    for (uint32_t i = 0; i < n; i++)
        if ((sw > 0) == (up[i] > 0))
            sum = s_add2(sum, s_mul2(s_div(subst(N, x, roots[i]), subst(D1, x, roots[i])),
                                     s_exp(s_mul(3, (E *[]){K->i_, w, roots[i]}))));
    return s_mul(3, (E *[]){mk_int(sw > 0 ? 2 : -2), K->pi_, s_mul2(K->i_, sum)});
}

static E *def_residue(E *f, E *x, E *a, E *b) {
    if (!ISYM(b, S_INF) || !is_neg_inf(a) || f->t != E_MUL) return NULL;
    E *T = NULL, **o = salloc(sizeof(E *) * f->n);
    uint32_t no = 0;
    for (uint32_t i = 0; i < f->n; i++) {
        E *t = EK(f)[i];
        if (!T && !freeof(t, x) && ((t->t == E_FN && (t->s == F_COS || t->s == F_SIN) && t->n == 1) ||
                                    (t->t == E_POW && ISYM(K0(t), S_E))))
            T = t;
        else
            o[no++] = t;
    }
    if (!T) return NULL;
    E *R = s_mul(no, o), *arg = T->t == E_POW ? K1(T) : K0(T), *w, *c0;
    if (!is_rational_in(R, x) || !lin(arg, x, &w, &c0) || !is_zero(c0)) return NULL;
    if (T->t == E_POW) { /* e^(i w x) */
        w = s_div(w, K->i_);
        if (!freeof_sym(w, S_I)) return NULL;
    }
    if (!freeof_sym(w, S_I) || !sign_of(w)) return NULL;
    E *N, *D;
    numden(rat_normal(R), &N, &D);
    long dn, dd;
    if (!poly_degree_in(N, x, &dn) || !poly_degree_in(D, x, &dd) || dd < dn + 1 || dd > 8) return NULL;
    E *D1 = diff(D, x), *roots = solve_expr(D, x, 1);
    if (roots->n != (uint32_t)dd) return NULL;
    int *up = salloc(sizeof(int) * (size_t)dd);
    for (uint32_t i = 0; i < roots->n; i++) {
        E *p = EK(roots)[i];
        if (!freeof(p, x) || has_float(p) || is_zero(subst(D1, x, p))) return NULL; /* repeated root */
        E *im = expand(s_div(s_sub(p, subst(p, K->i_, s_neg(K->i_))), s_mul2(K->c2, K->i_)));
        if (!(up[i] = sign_of(im))) return NULL; /* real root or undecided half-plane */
    }
    E *Ip = res_sum(N, D1, x, w, EK(roots), up, roots->n);
    if (T->t == E_POW) return Ip;
    E *Im = res_sum(N, D1, x, s_neg(w), EK(roots), up, roots->n);
    return T->s == F_COS ? s_div(s_add2(Ip, Im), K->c2) : s_div(s_sub(Ip, Im), s_mul2(K->c2, K->i_));
}

static E *def_special(E *f, E *x, E *a, E *b) {
    tryf tf;
    try_enter(&tf);
    E *volatile r = NULL;
    if (!setjmp(tf.jb)) {
        r = def_gauss(f, x, a, b);
        if (!r) r = def_euler(f, x, a, b);
        if (!r) r = def_fourier(f, x, a, b);
        if (!r) r = def_residue(f, x, a, b);
        if (r) r = simplify_full(r);
        try_leave(&tf);
    } else {
        try_leave(&tf);
        if (K->err == CAS_ERR_INTERRUPTED) rethrow();
        r = NULL;
    }
    return r;
}

/*
 * eval_by_sign -- F(b-) - F(a+) when the limits decide only with the sign of a parameter p
 * (atan(y/p) at y = inf): with R+ for p > 0 and R- for p < 0 (assumed in turn), the value
 * for real p != 0 is (R+ + R-)/2 + sign(p) (R+ - R-)/2.  NULL if no parameter does it.
 */
static E *eval_by_sign(E *F, E *x, E *a, E *b) {
    E *sy[8];
    uint32_t ns = collect_syms(F, sy, 8);
    for (uint32_t i = 0; i < ns; i++) {
        if (expr_eq(sy[i], x) || sy[i]->s < S_X || K->syms[sy[i]->s].assume) continue;
        E *R[2] = {NULL, NULL};
        for (int s = 0; s < 2; s++) {
            tryf tf;
            K->syms[sy[i]->s].assume = s ? A_NEG : A_POS;
            try_enter(&tf);
            if (!setjmp(tf.jb)) {
                E *fb = eval_at(F, x, b, -1), *fa = fb ? eval_at(F, x, a, 1) : NULL;
                R[s] = fa ? simplify_full(s_sub(fb, fa)) : NULL;
                try_leave(&tf);
            } else {
                try_leave(&tf);
                K->syms[sy[i]->s].assume = 0;
                if (K->err == CAS_ERR_INTERRUPTED) rethrow();
            }
            K->syms[sy[i]->s].assume = 0;
        }
        if (R[0] && R[1] && freeof_sym(R[0], S_UNDEF) && freeof_sym(R[1], S_UNDEF) && freeof_sym(R[0], S_INF) &&
            freeof_sym(R[1], S_INF))
            return s_add2(s_div(s_add2(R[0], R[1]), K->c2),
                          s_mul2(s_fn1(F_SIGN, sy[i]), s_div(s_sub(R[0], R[1]), K->c2)));
    }
    return NULL;
}

E *integrate_def(E *f, E *x, E *a, E *b) {
    if (expr_eq(a, b)) return K->c0; /* also int_inf^inf */
    E *F = integrate(f, x);
    if (F && !freeof_sym(F, C_INTEGRATE)) F = NULL; /* partial antiderivative */
    {
        double av, bv, poles[16];
        if (to_double(a, &av) && to_double(b, &bv) && av < bv) {
            uint32_t np = interior_poles(f, x, av, bv, poles, 16);
            for (uint32_t i = 0; i < np; i++) {
                /* integrand must stay finite near the point, else divergent; measured
                 * in modulus, since a principal-branch power such as x^(-1/3) is
                 * complex (but finite) left of 0 */
                double lr, li, rr, ri;
                double h = 1e-7 * (1 + fabs(poles[i]));
                int okl = eval_complex_at(f, x->s, poles[i] - h, 0, &lr, &li),
                    okr = eval_complex_at(f, x->s, poles[i] + h, 0, &rr, &ri);
                if (!okl || !okr || hypot(lr, li) > 1e5 || hypot(rr, ri) > 1e5)
                    cas_throw(CAS_ERR_DOMAIN, "integral diverges (pole inside the interval)");
            }
        } else if (!is_const_expr(a) || !is_const_expr(b))
            sym_pole_check(f, x, a, b);
    }
    double num;
    int have_num = 0;
    {
        tryf tf;
        try_enter(&tf);
        if (!setjmp(tf.jb)) {
            have_num = nint_double(f, x, a, b, &num);
            try_leave(&tf);
        } else {
            try_leave(&tf);
            if (K->err == CAS_ERR_INTERRUPTED) rethrow();
            have_num = 0;
        }
    }
    if (F) {
        tryf tf;
        try_enter(&tf);
        E *volatile r = NULL;
        if (!setjmp(tf.jb)) {
            E *volatile fb = NULL, *volatile fa = NULL;
            tryf t2;
            try_enter(&t2);
            if (!setjmp(t2.jb)) {
                fb = eval_at(F, x, b, -1), fa = eval_at(F, x, a, 1);
                try_leave(&t2);
            } else {
                try_leave(&t2);
                if (K->err == CAS_ERR_INTERRUPTED) rethrow();
                fb = fa = NULL;
            }
            if (fb && fa) r = sign_jumps(F, x, a, b, s_sub(fb, fa));
            else r = eval_by_sign(F, x, a, b);
            try_leave(&tf);
        } else {
            try_leave(&tf);
            if (K->err == CAS_ERR_INTERRUPTED) rethrow();
            r = NULL;
        }
        if (r) {
            E *rs = simplify_full(r);
            double v;
            if (!have_num) {
                if (param_mismatch(f, x, a, b, rs)) goto inert;
                return rs;
            }
            if (to_double(rs, &v) && fabs(v - num) <= 1e-6 * (1 + fabs(num))) return rs;
            /* symbolic bounds/parameters (but an undef/inf r loses to num) */
            if (!is_const_expr(rs) && freeof_sym(rs, S_UNDEF) && freeof_sym(rs, S_INF) &&
                !param_mismatch(f, x, a, b, rs))
                return rs;
            /* disagreement: antiderivative discontinuous on the interval */
        }
    }
    {
        E *sp = def_special(f, x, a, b);
        double v;
        if (sp && (have_num ? to_double(sp, &v) && fabs(v - num) <= 1e-6 * (1 + fabs(num))
                            : !param_mismatch(f, x, a, b, sp)))
            return sp;
    }
    if (have_num) {
        double av, bv;
        if (!g_def_split && to_double(a, &av) && to_double(b, &bv) && av < bv) {
            g_def_split = 1;
            tryf tf;
            try_enter(&tf);
            E *volatile r = NULL;
            if (!setjmp(tf.jb)) {
                r = def_split(f, x, a, b, av, bv, num);
                try_leave(&tf);
            } else {
                try_leave(&tf);
                g_def_split = 0;
                if (K->err == CAS_ERR_INTERRUPTED) rethrow();
            }
            g_def_split = 0;
            if (r) return r;
        }
        return mk_flt(num);
    }
inert:;
    E *v[4] = {f, x, a, b};
    E *e = mk_fn(C_INTEGRATE, 4, v);
    e->f = F_SIMP;
    return e;
}
