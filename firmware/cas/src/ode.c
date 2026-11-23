/*
 * ode.c -- ordinary differential equations and Laplace transforms.
 *
 * dsolve(ode [, y, x])  or dsolve([ode, y(a)=b, y'(a)=c, ...], y, x)
 *   order 1: y' = g(x) (quadrature), linear (integrating factor),
 *            separable, Bernoulli, homogeneous (g depends on y/x only),
 *            exact (M dx + N dy with M_y = N_x);
 *   order n: linear with constant coefficients (characteristic roots,
 *            real basis for complex pairs); right-hand side for order 2 by
 *            variation of parameters;
 *   orders 2..4: homogeneous Euler-Cauchy equations sum a_k x^k y^(k) = 0.
 *   Initial conditions determine the constants c1, c2, ...
 *   The extension (slot CASX_FN_DSOLVE, cas/ext/alg/odex.c) is asked when no method applies
 *   (Riccati, Bessel), for dsolve(eq, y, x, x0, n) (power series), and to normalise the
 *   general solution as sum c_i y_i + y_p.
 * laplace(f, t, s) / ilaplace(F, s, t): rule/table based (linearity, shifts,
 *   multiplication by t^n, partial fractions for the inverse).
 *
 * Data flow.  dsolve is special in eval.c: it receives its arguments
 * *unevaluated* (dsolve_raw), because y(x), y' and y(a)=b must not be
 * evaluated as function calls.  The pipeline is
 *   raw equation --raw_prep--> bare y replaced by y(x)
 *     --eval_hiding (y, x hidden)--> evaluated tree, derivatives appear as
 *       unevaluated diff(y(x), x) / diff(diff(y(x), x), x) nodes
 *     --ymap--> form in which y, y', y'', ... are the plain symbols
 *       _y0, _y1, _y2, ...  (so the whole ODE is an ordinary expression
 *       F(x, _y0, ..., _ym) = 0 that poly_coeff, diff, integrate, solve and
 *       rat_normal understand)
 *     --ode1 / ode_const_coef / ode_euler--> general solution containing the
 *       arbitrary constants c1, c2, ... (ordinary symbols)
 *     --linsolve on the initial conditions--> particular solution.
 * The evaluated command form (cmds.c, C_DESOLVE / C_ODESOLVE in eval_cmd)
 * goes through dsolve_expr.  laplace / ilaplace are ordinary commands
 * (cmds.c) that call laplace_expr and ilaplace_expr with evaluated arguments.
 *
 * Methods are the standard textbook ones (e.g. Boyce & DiPrima, "Elementary
 * Differential Equations"; Zwillinger, "Handbook of Differential
 * Equations").  Every candidate solution is produced constructively from
 * integrate() and solve(), so a method "fails" (returns NULL and the next
 * one is tried) when a needed integral or equation cannot be done.
 *
 * Memory and errors: everything is built in scratch.  Calls to solve_expr
 * that may throw are wrapped in local tryf handlers so that a failed solve
 * only means "fall back to an implicit solution"; interrupts are always
 * re-thrown.  Unsupported equations end in cas_throw(CAS_ERR_UNSUPPORTED).
 */
#include "cas_int.h"
#include "upoly.h"
#include "casx.h"
#include <string.h>

/* cold code: compiled for size (flash bank 2) */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

E *eval_hiding(E *e, int nvars, int *vars);
uint32_t collect_syms(E *e, E **out, uint32_t cap);

/* The symbol _y<k> standing for the k-th derivative of the unknown function
 * (k = 0..9; ymap limits the order to 9 so the name has one digit). */
static E *ysym(long k) {
    char nm[8] = "_y";
    nm[2] = (char)('0' + k);
    nm[3] = 0;
    return mk_sym_name(nm);
}

/* The k-th arbitrary constant c<k> (k = 1..9) of a general solution.  These
 * are ordinary symbols, so they look like (and collide with) user names c1,
 * c2, ... */
static E *csym(long k) {
    char nm[8] = "c";
    nm[1] = (char)('0' + k);
    nm[2] = 0;
    return mk_sym_name(nm);
}

/* Prepare the *raw* (unevaluated) equation tree: every bare occurrence of
 * the unknown name y becomes the call y(x), and diff(.., x) with the
 * parser's default variable x (the prime notation y' parses as diff(y, x))
 * is redirected to the actual independent variable.  Existing calls y(..)
 * and derivative(f, k, a) nodes (f'(a) notation) are left alone.  Returns a
 * fresh raw (unsimplified) copy in scratch; leaves are shared. */
static E *raw_prep(E *e, int yid, E *x) {
    if (e->t == E_SYM && e->s == yid) return mk_fn1(yid, x);
    if (e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR) return e;
    if (e->t == E_FN && e->s == F_DERIV) return e;
    if (e->t == E_FN && e->s == yid) return e;
    stack_check();
    E *c = enode(e->t, e->n);
    c->s = e->s;
    for (uint32_t i = 0; i < e->n; i++) EK(c)[i] = raw_prep(EK(e)[i], yid, x);
    if (e->t == E_FN && e->s == C_DIFF && e->n == 2 && ISYM(EK(e)[1], S_X)) EK(c)[1] = x;
    return c;
}

/* Map the *evaluated* tree to the symbolic-variable form: y(x) -> _y0 and a
 * nest of d unevaluated diff(.., ..) calls around y(..) -> _y<d>.  *maxord
 * is raised to the highest derivative order seen.  Throws
 * CAS_ERR_UNSUPPORTED for order > 9 (symbol names are single digits).
 * Other occurrences of y (e.g. y(0) or y(2x)) are kept unchanged, and
 * dsolve_raw rejects the equation afterwards.  The result is rebuilt and
 * re-simplified (rebuild) in scratch. */
static E *ymap(E *e, int yid, E *x, long *maxord) {
    stack_check();
    if (e->t == E_FN && e->s == yid && e->n == 1 && expr_eq(K0(e), x)) return ysym(0);
    if (e->t == E_FN && e->s == C_DIFF && e->n == 2) {
        long d = 1;
        E *in = K0(e);
        while (in->t == E_FN && in->s == C_DIFF && in->n == 2) {
            d++;
            in = K0(in);
        }
        if (in->t == E_FN && in->s == yid) {
            if (d > 9) cas_throw(CAS_ERR_UNSUPPORTED, "dsolve: order too high");
            if (d > *maxord) *maxord = d;
            return ysym(d);
        }
    }
    if (e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR) return e;
    E **k = salloc(sizeof(E *) * (e->n + 1));
    for (uint32_t i = 0; i < e->n; i++) k[i] = ymap(EK(e)[i], yid, x, maxord);
    return rebuild(e, k);
}

/* 1 if e contains none of _y0 .. _y<m>. */
static int free_of_ys(E *e, long m) {
    for (long k = 0; k <= m; k++)
        if (!freeof(e, ysym(k))) return 0;
    return 1;
}

/* First-order equation F(x, _y0, _y1) = 0, where F must be linear in
 * _y1 = y' (degree 1 as a polynomial in _y1; F is the numerator of the
 * normalised ODE).  Writing it as y' = g(x, y), the methods tried in order:
 *   1. quadrature   g = g(x):            y = int g dx + c1
 *   2. linear       g = a(x) y + b(x):   y = e^A (int b e^-A dx + c1),
 *                   A = int a dx (integrating factor e^-A)
 *   3. separable    g = p(x) q(y) (read off the factorisation of g):
 *                   int dy/q = int p dx + c1, solved for y if possible
 *   4. Bernoulli    g = a y + b y^m (m constant, m != 0, 1):
 *                   v = y^(1-m) satisfies the linear v' = (1-m)(a v + b)
 *   5. homogeneous  g(x, v x) independent of x: with y = v x,
 *                   int dv/(G(v) - v) = ln x + c1
 *   6. exact        F = M + N y' with dM/dy = dN/dx: potential
 *                   P(x, y) = int M dx + int (N - dP/dy) dy, P = c1
 * Returns the general solution as an expression in x and c1, a list of
 * branches when solve finds several, or an implicit equation lhs = rhs
 * (which may still contain _y0; dsolve_raw substitutes y back) when y cannot
 * be isolated.  Returns NULL when no method applies (the caller then tries
 * the higher-order solvers).  Scratch allocation; solve failures are caught
 * locally, interrupts re-thrown.  Note that dividing by q(y) in the
 * separable case loses equilibrium solutions q(y) = 0, as usual. */
static E *ode1(E *F, E *x) {
    E *Y0 = ysym(0), *Y1 = ysym(1);
    long d;
    if (!poly_degree_in(F, Y1, &d) || d != 1) return NULL;
    /* F = F0 + F1 y'  ->  y' = g = -F0 / F1 */
    E *g = rat_normal(s_neg(s_div(poly_coeff(F, Y1, 0), poly_coeff(F, Y1, 1))));
    E *C = csym(1);
    if (freeof(g, Y0)) {
        E *G = integrate(g, x);
        return G ? s_add2(G, C) : NULL;
    }
    /* linear: g = a Y + b */
    E *a = rat_normal(diff(g, Y0));
    if (freeof(a, Y0)) {
        E *b = rat_normal(s_sub(g, s_mul2(a, Y0)));
        E *A = integrate(a, x);
        if (!A) return NULL;
        E *ib = is_zero(b) ? K->c0 : integrate(simplify_full(s_mul2(b, s_exp(s_neg(A)))), x);
        if (!ib) return NULL;
        return simplify_full(s_mul2(s_exp(A), s_add2(ib, C)));
    }
    /* separable: g = p(x) q(Y).  Factors free of y (including constants) go
     * to p, factors free of x to q; a factor with both fails the test. */
    E *fg = factor_expr(g);
    E **fs = fg->t == E_MUL ? EK(fg) : &fg;
    uint32_t n = fg->t == E_MUL ? fg->n : 1;
    E **px = salloc(sizeof(E *) * (n + 1)), **qy = salloc(sizeof(E *) * (n + 1));
    uint32_t np = 0, nq = 0;
    int sep = 1;
    for (uint32_t i = 0; i < n; i++) {
        if (freeof(fs[i], Y0)) px[np++] = fs[i];
        else if (freeof(fs[i], x)) qy[nq++] = fs[i];
        else sep = 0;
    }
    if (sep) {
        E *P = s_mul(np, px), *Q = s_mul(nq, qy);
        E *GY = integrate(s_inv(Q), Y0), *GX = integrate(P, x);
        if (!GY || !GX) return NULL;
        E *eq = s_sub(GY, s_add2(GX, C));
        tryf tf;
        try_enter(&tf);
        E *volatile sol = NULL;
        if (!setjmp(tf.jb)) {
            tryf t2;
            try_enter(&t2);
            if (!setjmp(t2.jb)) {
                sol = solve_expr(eq, Y0, 0);
                try_leave(&t2);
            } else {
                /* "cannot solve": retry below (an interrupt goes to the outer handler) */
                try_leave(&t2);
                if (K->err == CAS_ERR_INTERRUPTED) rethrow();
            }
            if (!sol || sol->n == 0) {
                /* combine logarithms (the constant absorbs branch differences) */
                extern E *ln_collect(E *e);
                sol = solve_expr(s_sub(ln_collect(GY), s_add2(GX, C)), Y0, 0);
            }
            try_leave(&tf);
        } else {
            try_leave(&tf);
            if (K->err == CAS_ERR_INTERRUPTED) rethrow();
            sol = NULL;
        }
        if (sol && sol->n == 1) return EK(sol)[0];
        if (sol && sol->n > 1) return sol;
        /* implicit */
        return s_fn2(F_EQ, GY, s_add2(GX, C));
    }
    /* Bernoulli: g = a(x) Y + b(x) Y^m */
    E *ge = expand(g);
    if (ge->t == E_ADD && ge->n == 2) {
        for (int pass = 0; pass < 2; pass++) {
            E *t1 = EK(ge)[pass], *t2 = EK(ge)[1 - pass];
            E *aa = rat_normal(s_div(t1, Y0));
            if (!freeof(aa, Y0)) continue;
            /* t2 = b Y^m: the exponent is recovered as m = Y t2' / t2 (the
             * logarithmic derivative), which must be a constant */
            E *ratio = rat_normal(s_div(s_mul2(Y0, diff(t2, Y0)), t2));
            if (!freeof(ratio, Y0) || !freeof(ratio, x)) continue;
            E *m = ratio;
            if (is_one(m) || is_zero(m)) continue;
            E *bb = rat_normal(s_div(t2, s_pow(Y0, m)));
            if (!freeof(bb, Y0)) continue;
            /* v = Y^(1-m): v' = (1-m)(a v + b) */
            E *om = s_sub(K->c1, m);
            E *A = integrate(s_mul2(om, aa), x);
            if (!A) return NULL;
            E *ib = integrate(simplify_full(s_mul(3, (E *[]){om, bb, s_exp(s_neg(A))})), x);
            if (!ib) return NULL;
            E *v = s_mul2(s_exp(A), s_add2(ib, C));
            return simplify_full(s_pow(v, s_inv(om)));
        }
    }
    /* homogeneous: g(x, y) = G(y/x); substitute y = v x */
    {
        E *V = mk_sym_name("_ov");
        E *gv = rat_normal(subst(g, Y0, s_mul2(V, x)));
        if (freeof(gv, x)) {
            /* v' x + v = G(v)  ->  dv/(G(v) - v) = dx/x */
            E *den = rat_normal(s_sub(gv, V));
            if (!is_zero(den)) {
                E *Gv = integrate(s_inv(den), V);
                if (Gv && freeof_sym(Gv, C_INTEGRATE)) {
                    E *eq = s_sub(Gv, s_add2(s_ln(x), C));
                    E *sol = NULL;
                    tryf tf;
                    try_enter(&tf);
                    if (!setjmp(tf.jb)) {
                        sol = solve_expr(eq, V, 0);
                        try_leave(&tf);
                    } else {
                        try_leave(&tf);
                        if (K->err == CAS_ERR_INTERRUPTED) rethrow();
                        sol = NULL;
                    }
                    if (sol && sol->n == 1) return simplify_full(s_mul2(x, EK(sol)[0]));
                    return s_fn2(F_EQ, subst(Gv, V, s_div(Y0, x)), s_add2(s_ln(x), C));
                }
            }
        }
    }
    /* exact: M + N y' = 0 with dM/dy = dN/dx.  P = int M dx; the rest
     * N - dP/dy must then depend on y only, Q = its y-integral; the solution
     * is the level set P + Q = c1. */
    {
        E *Nn = poly_coeff(F, Y1, 1), *Mm = poly_coeff(F, Y1, 0);
        E *chk = rat_normal(s_sub(diff(Mm, Y0), diff(Nn, x)));
        if (is_zero(chk)) {
            E *P = integrate(Mm, x);
            if (P && freeof_sym(P, C_INTEGRATE)) {
                E *rest = rat_normal(s_sub(Nn, diff(P, Y0)));
                if (freeof(rest, x)) {
                    E *Q = integrate(rest, Y0);
                    if (Q && freeof_sym(Q, C_INTEGRATE)) {
                        E *pot = s_add2(P, Q);
                        E *sol = NULL;
                        tryf tf;
                        try_enter(&tf);
                        if (!setjmp(tf.jb)) {
                            sol = solve_expr(s_sub(pot, C), Y0, 0);
                            try_leave(&tf);
                        } else {
                            try_leave(&tf);
                            if (K->err == CAS_ERR_INTERRUPTED) rethrow();
                            sol = NULL;
                        }
                        if (sol && sol->n == 1) return EK(sol)[0];
                        return s_fn2(F_EQ, pot, C);
                    }
                }
            }
        }
    }
    return NULL;
}

/* Euler-Cauchy equation sum_{k=0..m} a_k x^k y^(k) = 0 (homogeneous only,
 * 2 <= m <= 4, a_k constants).  The trial solution y = x^r gives the
 * indicial (characteristic) polynomial
 *   sum_k a_k r (r-1) ... (r-k+1) = 0,
 * and each root r of multiplicity mu contributes x^r ln(x)^j, j < mu; a
 * complex pair alpha +- i beta contributes the real functions
 *   x^alpha ln(x)^j cos(beta ln x),  x^alpha ln(x)^j sin(beta ln x)
 * (only the root with positive imaginary part is used; its conjugate is
 * skipped).  Equivalent to the constant-coefficient case under x = e^u.
 * Returns c1 y_1 + ... + cm y_m, or NULL when the equation does not have
 * this shape or the basis cannot be completed (e.g. roots not found). */
static E *ode_euler(E *F, E *x, long m) {
    if (m < 2 || m > 4) return NULL;
    E **a = salloc(sizeof(E *) * (size_t)(m + 1));
    E *rest = F;
    for (long k = 0; k <= m; k++) {
        E *Yk = ysym(k);
        long d;
        if (!poly_degree_in(F, Yk, &d) || d > 1) return NULL;
        E *c = rat_normal(poly_coeff(F, Yk, 1));
        /* c must be a_k * x^k with a_k constant; `rest` accumulates F with
         * all _y's set to 0, which must vanish (no right-hand side) */
        E *ak = rat_normal(s_div(c, s_pow(x, mk_int(k))));
        if (!freeof(ak, x)) return NULL;
        a[k] = ak;
        rest = subst(rest, Yk, K->c0);
    }
    if (!is_zero(rat_normal(rest))) return NULL;
    /* characteristic polynomial in r: sum a_k r(r-1)...(r-k+1) */
    E *r = mk_sym_name("_er");
    E *cp = K->c0;
    for (long k = 0; k <= m; k++) {
        E *fall = K->c1;
        for (long j = 0; j < k; j++) fall = s_mul2(fall, s_sub(r, mk_int(j)));
        cp = s_add2(cp, s_mul2(a[k], fall));
    }
    E *rl = roots_expr(expand(cp), r);
    E **basis = salloc(sizeof(E *) * (size_t)(m + 2));
    long nb = 0;
    for (uint32_t i = 0; i < rl->n && nb < m; i++) {
        E *root = K0(EK(rl)[i]);
        long mult;
        num_is_small_int(K1(EK(rl)[i]), &mult);
        E *re = rat_normal(s_fn1(F_RE, root)), *im = rat_normal(s_fn1(F_IM, root));
        if (is_zero(im)) {
            if (nb + mult > m) return NULL;
            for (long j = 0; j < mult; j++) basis[nb++] = s_mul2(s_pow(x, re), s_powi(s_ln(x), j));
            continue;
        }
        double iv;
        if (to_double(im, &iv) && iv < 0) continue;
        /* both roots of a pair are used when the imaginary part is not
         * numeric; more basis functions than m means failure, not overflow */
        if (nb + 2 * mult > m) return NULL;
        for (long j = 0; j < mult; j++) {
            E *xr = s_mul2(s_pow(x, re), s_powi(s_ln(x), j));
            basis[nb++] = s_mul2(xr, s_fn1(F_COS, s_mul2(im, s_ln(x))));
            basis[nb++] = s_mul2(xr, s_fn1(F_SIN, s_mul2(im, s_ln(x))));
        }
    }
    if (nb != m) return NULL;
    E **t = salloc(sizeof(E *) * (size_t)m);
    for (long k = 0; k < m; k++) t[k] = s_mul2(csym(k + 1), basis[k]);
    return s_add((uint32_t)m, t);
}

/* Linear equation with constant coefficients of order m:
 *   F = sum_{k=0..m} a_k y^(k) - r(x),  a_k free of x and of all _y's.
 * Homogeneous part: characteristic polynomial sum a_k lambda^k, roots (with
 * multiplicities, possibly radicals or floats) from roots_expr; a real root
 * lambda of multiplicity mu gives x^j e^(lambda x), a complex pair
 * alpha +- i beta gives x^j e^(alpha x) cos(beta x) and x^j e^(alpha x)
 * sin(beta x), j < mu (a real fundamental system).  The general solution is
 * y_h = c1 y_1 + ... + cm y_m.
 * Right-hand side: only for m = 2, by variation of parameters,
 *   y_p = -y_1 int(y_2 f / W) + y_2 int(y_1 f / W),
 *   W = y_1 y_2' - y_1' y_2 (Wronskian),  f = r / a_2.
 * (For m = 1 with r != 0 this returns NULL: such equations are linear first
 * order and were already offered to ode1.)  Returns NULL when the shape does
 * not match, the basis is incomplete, or an integral is not found. */
static E *ode_const_coef(E *F, E *x, long m) {
    E **a = salloc(sizeof(E *) * (size_t)(m + 1));
    E *rest = F;
    for (long k = 0; k <= m; k++) {
        E *Yk = ysym(k);
        long d;
        if (!poly_degree_in(F, Yk, &d) || d > 1) return NULL;
        a[k] = rat_normal(poly_coeff(F, Yk, 1));
        if (!freeof(a[k], x) || !free_of_ys(a[k], m)) return NULL;
        rest = subst(rest, Yk, K->c0);
    }
    E *r = rat_normal(s_neg(rest)); /* sum a_k y^(k) = r */
    if (!free_of_ys(r, m)) return NULL;
    /* characteristic polynomial */
    E *lam = mk_sym_name("_lam");
    E **t = salloc(sizeof(E *) * (size_t)(m + 1));
    for (long k = 0; k <= m; k++) t[k] = s_mul2(a[k], s_pow(lam, mk_int(k)));
    E *cp = s_add((uint32_t)(m + 1), t);
    E *rl = roots_expr(cp, lam);
    E **basis = salloc(sizeof(E *) * (size_t)(m + 2));
    long nb = 0;
    /* roots come as [root, mult]; build real basis */
    for (uint32_t i = 0; i < rl->n && nb < m; i++) {
        E *root = K0(EK(rl)[i]);
        long mult;
        num_is_small_int(K1(EK(rl)[i]), &mult);
        E *re = rat_normal(s_fn1(F_RE, root)), *im = rat_normal(s_fn1(F_IM, root));
        if (is_zero(im)) {
            if (nb + mult > m) return NULL;
            for (long j = 0; j < mult; j++) basis[nb++] = s_mul2(s_pow(x, mk_int(j)), s_exp(s_mul2(re, x)));
            continue;
        }
        double iv;
        if (to_double(im, &iv) && iv < 0) continue; /* conjugate handled with the positive one */
        if (nb + 2 * mult > m) return NULL; /* see ode_euler */
        for (long j = 0; j < mult; j++) {
            E *xe = s_mul2(s_pow(x, mk_int(j)), s_exp(s_mul2(re, x)));
            basis[nb++] = s_mul2(xe, s_fn1(F_COS, s_mul2(im, x)));
            basis[nb++] = s_mul2(xe, s_fn1(F_SIN, s_mul2(im, x)));
        }
    }
    if (nb != m) return NULL;
    E **terms = salloc(sizeof(E *) * (size_t)(m + 2));
    for (long k = 0; k < m; k++) terms[k] = s_mul2(csym(k + 1), basis[k]);
    E *yh = s_add((uint32_t)m, terms);
    if (is_zero(r)) return yh;
    if (m != 2 && m != 1) return NULL;
    if (m == 1) {
        /* y' + p y = q handled as linear first order */
        return NULL;
    }
    /* variation of parameters */
    E *y1 = basis[0], *y2 = basis[1];
    E *W = simplify_full(s_sub(s_mul2(y1, diff(y2, x)), s_mul2(diff(y1, x), y2)));
    E *f = s_div(r, a[2]);
    E *I1 = integrate(simplify_full(s_div(s_mul2(y2, f), W)), x);
    E *I2 = integrate(simplify_full(s_div(s_mul2(y1, f), W)), x);
    if (!I1 || !I2 || !freeof_sym(I1, C_INTEGRATE) || !freeof_sym(I2, C_INTEGRATE)) return NULL;
    E *yp = simplify_full(s_add2(s_neg(s_mul2(y1, I1)), s_mul2(y2, I2)));
    return s_add2(yh, yp);
}

/* dsolve / desolve entry point with *unevaluated* arguments (called from
 * eval.c before argument evaluation): raw[0] is the ODE, or a list
 * [ode, cond1, cond2, ...]; raw[1], raw[2] are the names of the unknown
 * function (default y) and the independent variable (default x), accepted
 * in either order.  Steps:
 *  1. bring the equation into the _y0.._ym form (raw_prep, eval_hiding with
 *     y and x hidden so stored values do not interfere, ymap), take
 *     lhs - rhs and the numerator of its rational normal form;
 *  2. general solution: ode1 (order 1), then ode_const_coef, then ode_euler;
 *  3. conditions y(a) = b / y'(a) = b / y''(a) = b (the parser's
 *     derivative(y, k, a) nodes): each gives the linear equation
 *     (d^k/dx^k of the general solution)(a) = b in c1..cm, solved with
 *     linsolve, and the constants are substituted.
 * Throws CAS_ERR_DOMAIN for malformed input or unsatisfiable conditions and
 * CAS_ERR_UNSUPPORTED when no method applies or conditions are given for an
 * implicit / multi-branch solution.  Result in scratch. */
/* the names written with primes in e (y' parses to diff(y, x)), distinct, into ids[0..*k) (at most 8) */
static void primed_names(E *e, int *ids, int *k) {
    stack_check();
    if (IFN(e, C_DIFF) && e->n == 2 && K0(e)->t == E_SYM) {
        for (int i = 0; i < *k; i++)
            if (ids[i] == K0(e)->s) return;
        if (*k < 8) ids[(*k)++] = K0(e)->s;
        return;
    }
    if (e->t == E_INT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR) return;
    for (uint32_t i = 0; i < e->n; i++) primed_names(EK(e)[i], ids, k);
}

E *dsolve_raw(uint32_t n, E **raw) {
    if (n < 1) cas_throw(CAS_ERR_DOMAIN, "dsolve: missing equation");
    if (n == 1 && raw[0]->t == E_LIST) {
        /* dsolve([x'=y, y'=-x]): a system in the names written with primes; the
         * variable is t when x is one of them, else x */
        int ids[8], k = 0;
        primed_names(raw[0], ids, &k);
        if (k >= 2) {
            extern E *odesys(E *eqs, E *names, E *x); /* matfn.c */
            E **v = salloc(sizeof(E *) * (size_t)k);
            int has_x = 0;
            for (int i = 0; i < k; i++) {
                v[i] = mk_sym(ids[i]);
                has_x |= ids[i] == S_X;
            }
            return odesys(raw[0], mk_list(k, v), has_x ? mk_sym_name("t") : mk_sym(S_X));
        }
    }
    if (n > 1 && raw[1]->t == E_LIST) {
        extern E *odesys(E *eqs, E *names, E *x); /* matfn.c */
        return odesys(raw[0], raw[1], n > 2 ? raw[2] : mk_sym(S_X));
    }
    E *y = n > 1 ? raw[1] : mk_sym_name("y");
    E *x = n > 2 ? raw[2] : mk_sym(S_X);
    if (y->t != E_SYM || x->t != E_SYM) cas_throw(CAS_ERR_DOMAIN, "dsolve: expected names y, x");
    /* accept dsolve(eq, x, y) order too: y is the one used as a function.
     * Swap when the presumed y does not occur in the equation, or when the
     * presumed y is the default name x and the presumed x does occur. */
    {
        E *probe = raw[0]->t == E_LIST && raw[0]->n ? EK(raw[0])[0] : raw[0];
        if (freeof_sym(probe, y->s) || (!freeof_sym(probe, x->s) && n > 2 && x->s != S_X && y->s == S_X)) {
            E *t = x;
            x = y;
            y = t;
        }
    }
    int hide[2] = {y->s, x->s};
    E *odes = raw[0]->t == E_LIST ? raw[0] : NULL;
    E *ode = odes ? (odes->n ? EK(odes)[0] : NULL) : raw[0];
    if (!ode) cas_throw(CAS_ERR_DOMAIN, "dsolve: missing equation");
    E *ev = eval_hiding(raw_prep(ode, y->s, x), 2, hide);
    if (IFN(ev, F_EQ)) ev = s_sub(K0(ev), K1(ev));
    long m = 0;
    E *F = ymap(ev, y->s, x, &m);
    if (m == 0) cas_throw(CAS_ERR_DOMAIN, "dsolve: no derivative of the unknown function");
    if (!freeof_sym(F, y->s)) cas_throw(CAS_ERR_UNSUPPORTED, "dsolve: unsupported form");
    F = rat_normal(F);
    E *num, *den;
    numden(F, &num, &den);
    F = num;
    E *gen = NULL, *sv[6] = {F, x, mk_int(m), K->c0, NULL, NULL};
    if (n > 4 && CASX_SLOTS) { /* dsolve(eq, y, x, x0, order): a power series (cas/ext/alg/odex.c) */
        sv[3] = K->c1, sv[4] = eval_hiding(raw[3], 2, hide), sv[5] = eval_hiding(raw[4], 2, hide);
        gen = casx_eslot(CASX_FN_DSOLVE, 6, sv);
        if (!gen) cas_throw(CAS_ERR_UNSUPPORTED, "dsolve: no series solution");
    }
    if (!gen && m == 1) gen = ode1(F, x);
    if (!gen) gen = ode_const_coef(F, x, m);
    if (!gen) gen = ode_euler(F, x, m);
    if (!gen && CASX_SLOTS) gen = casx_eslot(CASX_FN_DSOLVE, 4, sv); /* Riccati, ... */
    if (!gen) cas_throw(CAS_ERR_UNSUPPORTED, "dsolve: cannot solve this equation");
    if (CASX_SLOTS && n <= 4) { /* the general solution as sum c_i y_i + y_p */
        E *nv[4] = {gen, x, mk_int(m), mk_int(2)}, *g2 = casx_eslot(CASX_FN_DSOLVE, 4, nv);
        if (g2) gen = g2;
    }
    if (!freeof(gen, ysym(0))) gen = subst(gen, ysym(0), y); /* implicit solution: show y */
    if (!odes || odes->n < 2) return gen;
    if (gen->t == E_LIST || IFN(gen, F_EQ)) cas_throw(CAS_ERR_UNSUPPORTED, "dsolve: conditions on implicit solutions");
    /* conditions */
    uint32_t nc = odes->n - 1;
    E **eqs = salloc(sizeof(E *) * nc), **cs = salloc(sizeof(E *) * (size_t)(m + 1));
    for (long k = 0; k < m; k++) cs[k] = csym(k + 1);
    for (uint32_t i = 0; i < nc; i++) {
        E *c = eval_hiding(EK(odes)[i + 1], 2, hide);
        if (!IFN(c, F_EQ)) cas_throw(CAS_ERR_DOMAIN, "dsolve: conditions must be equations");
        E *l = K0(c);
        long ord = 0;
        E *pt;
        if (l->t == E_FN && l->s == y->s && l->n == 1) pt = K0(l);
        else if (IFN(l, F_DERIV) && l->n == 3 && ISYM(K0(l), y->s)) {
            num_is_small_int(K1(l), &ord);
            pt = EK(l)[2];
        } else
            cas_throw(CAS_ERR_DOMAIN, "dsolve: conditions look like y(a)=b or y'(a)=b");
        eqs[i] = s_sub(subst(diffn(gen, x, ord), x, pt), K1(c));
    }
    E *sol = linsolve(s_list(nc, eqs), s_list((uint32_t)m, cs));
    if ((!sol || sol->n == 0) && m == 1 && nc == 1) { /* c1 not linear (Riccati): solve */
        E *v = solve_expr(eqs[0], cs[0], 0);
        if (v && v->t == E_LIST && v->n) {
            E *q = EK(v)[0];
            sol = s_list(1, (E *[]){IFN(q, F_EQ) ? q : s_fn2(F_EQ, cs[0], q)});
        }
    }
    if (!sol || sol->n == 0) cas_throw(CAS_ERR_DOMAIN, "dsolve: conditions cannot be satisfied");
    E **xs = salloc(sizeof(E *) * sol->n), **vs = salloc(sizeof(E *) * sol->n);
    for (uint32_t i = 0; i < sol->n; i++) {
        xs[i] = K0(EK(sol)[i]);
        vs[i] = K1(EK(sol)[i]);
    }
    return simplify_full(subst_many(gen, sol->n, xs, vs));
}

/* dsolve with already-evaluated arguments (cmds.c, C_DESOLVE / C_ODESOLVE
 * through eval_cmd); same contract as dsolve_raw. */
E *dsolve_expr(E *eq, E *y, E *x) {
    E *raw[3] = {eq, y, x};
    return dsolve_raw(3, raw);
}

/* The Laplace transforms (laplace, ilaplace) live in the advanced CAS extension since CAS
 * round 3 (cas/ext/adv/lapl.c): cmds.c forwards the commands. */
