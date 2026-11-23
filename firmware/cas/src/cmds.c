/*
 * cmds.c -- the user-visible commands (factor, diff, solve, det, ...): argument
 * checking, defaults, and dispatch to the algorithm modules.
 *
 * Commands are the builtin symbols with ids in (S__FIRSTCMD, S__COUNT) (see
 * BUILTIN_SYMS in cas_int.h).  eval_fn (eval.c) evaluates a command's
 * arguments and calls eval_cmd(id, n, args) here.  Arguments therefore arrive
 * evaluated and simplified; for commands that bind variables (diff(f, x),
 * sum(f, k, a, b), solve(eq, x), ...) they were evaluated with those
 * variables' stored values hidden, so a variable argument is a bare symbol
 * even if the user assigned it.  A few commands never get here: dsolve takes
 * its arguments raw (eval.c -> ode.c), assume/unassume/about are evaluated
 * with names hidden, purge/when/ifte are special forms in eval.c, and
 * commands without a builtin id are dispatched by name in xcmds.c.
 *
 * Most cases are thin: check the argument count (nargs), pick a default
 * variable (default_var: x if present, else the first symbol) and call the
 * module that does the real work:
 *   calculus      diff.c, integ.c, series.c (series and limits), sum.c
 *   algebra       poly.c, factor.c, upoly.c, simplify.c, trig.c
 *   solving       solve.c, gbasis.c (Groebner bases)
 *   integers      big.c (bignums, primality, integer factorisation)
 *   linear alg.   linalg.c (Bareiss det, Gauss-Jordan rref/inverse, ...)
 *   numeric       numeric.c, bf.c (evalf, arbitrary precision), quadrature
 *   ODE/transform ode.c (laplace, ilaplace, dsolve)
 *   statistics    stats.c (STATS_IS_CMD range)
 * The small algorithms implemented in this file itself are: list statistics,
 * sorting, divisor enumeration, Euler's totient, Jacobian/Hessian/curl,
 * Lagrange interpolation, the Chinese remainder theorem for two moduli,
 * splitting quadratic factors over C (cfactor), and an adaptive
 * Runge-Kutta-Fehlberg 4(5) integrator for odesolve.
 *
 * Conventions: results are allocated in scratch and built with the s_*
 * (simplifying) constructors; deliberately non-canonical displays (an
 * unevaluated command, a proper fraction q + r/d, cfactor's ordered
 * product) are built raw and flagged F_SIMP so that the simplifier leaves
 * them alone.  Bad arguments raise CAS_ERR_DOMAIN via cas_throw; problems the
 * engine cannot solve either return the command unevaluated (integrate,
 * sum) or raise CAS_ERR_UNSUPPORTED (limit, series).
 */
#include "cas_int.h"
#include "casx.h" /* casx_dispatch: grad/divergence/curl with coordinates */
#include <math.h>
#include <string.h>

/* Entry points of other modules that are not declared in cas_int.h
 * (trig.c, integ.c, upoly.c, solve.c, ode.c). */
E *trig_expand(E *e);
E *trig_linearize(E *e);
E *trig_collect(E *e);
E *trig_to_exp(E *e);
E *exp_to_trig(E *e);
E *trig_simplify(E *e);
E *ln_expand(E *e);
E *ln_collect(E *e);
E *nintegrate(E *f, E *x, E *a, E *b);
E *poly_roots_numeric(E *p, E *x);
E *dsolve_expr(E *eq, E *y, E *x);
E *exact_expr(E *e);
E *proot_expr(E *p, E *x);
E *roots_expr(E *p, E *x);

/* Throw CAS_ERR_DOMAIN unless lo <= n <= hi. */
static void nargs(uint32_t n, uint32_t lo, uint32_t hi) {
    if (n < lo || n > hi) cas_throw(CAS_ERR_DOMAIN, "wrong number of arguments");
}

/* The variable argument at position i: it must be a symbol if present
 * (CAS_ERR_DOMAIN otherwise); if absent, the default variable of e. */
static E *var_arg(uint32_t n, E **a, uint32_t i, E *e) {
    if (n > i) {
        if (a[i]->t != E_SYM) cas_throw(CAS_ERR_DOMAIN, "expected a variable name");
        return a[i];
    }
    return default_var(e);
}

/* The argument as a machine integer; throws unless it is a small integer. */
static long int_arg(E *e) {
    long v;
    if (!num_is_small_int(e, &v)) cas_throw(CAS_ERR_DOMAIN, "expected an integer");
    return v;
}

/* Return e, throwing unless it is an exact integer (bignums allowed). */
static E *int_only(E *e) {
    if (e->t != E_INT) cas_throw(CAS_ERR_DOMAIN, "expected an integer");
    return e;
}

/* The symbol true or false. */
static E *bool_e(int b) { return mk_sym(b ? S_TRUE : S_FALSE); }

/* A command call returned unevaluated, e.g. integrate(f, x) when no
 * antiderivative was found.  Marked F_SIMP so the simplifier does not try to
 * re-run it; printing it gives back the input. */
static E *inert(int id, uint32_t n, E **a) {
    E *e = mk_fn(id, n, a);
    e->f = F_SIMP;
    return e;
}

/* 1 if the solutions sol of p(x) = 0 are fewer than the distinct roots of p (the degree of its
 * squarefree part): solve could not express some (a general quintic), so a list of eigenvalues
 * would be incomplete */
static int roots_missing(E *p, E *x, E *sol) {
    long d, dg;
    if (!poly_degree_in(p, x, &d) || !poly_degree_in(poly_gcd_expr(p, diff(p, x)), x, &dg)) return 0;
    return sol->t == E_LIST && (long)sol->n < d - dg;
}

/* e itself if it is a list, else the one-element list [e]. */
static E *as_list(E *e) { return e->t == E_LIST ? e : s_list(1, &e); }

/* The variables of grad, divergence, curl and laplacian called with only
 * the field (∇f, parse.c): [x], [x, y] or [x, y, z] -- one per component of
 * a vector field (1 to 3), for a scalar up to the last of x, y, z in it. */
E *nabla_vars(E *f) {
    E *v[3] = {mk_sym(S_X), mk_sym(sym_intern("y", 1)), mk_sym(sym_intern("z", 1))};
    uint32_t n = !freeof(f, v[2]) ? 3 : !freeof(f, v[1]) ? 2 : 1;
    if (f->t == E_LIST) n = f->n;
    if (n < 1 || n > 3) cas_throw(CAS_ERR_DOMAIN, "nabla: give the variables");
    return s_list(n, v);
}

/* Parse the "variable and point" arguments of limit/series/tangent starting
 * at position i.  Two spellings are accepted:
 *   cmd(f, x = a, ...)   -> *x = x, *pt = a, *next = i + 1
 *   cmd(f, x, a, ...)    -> *x = x, *pt = a, *next = i + 2
 * If the point is missing it defaults to 0; if the variable is missing it is
 * default_var(e).  *next is the index of the first argument after them. */
static void var_point(uint32_t n, E **a, uint32_t i, E *e, E **x, E **pt, uint32_t *next) {
    if (n > i && IFN(a[i], F_EQ) && K0(a[i])->t == E_SYM) {
        *x = K0(a[i]);
        *pt = K1(a[i]);
        *next = i + 1;
        return;
    }
    *x = var_arg(n, a, i, e);
    if (n > i + 1) {
        *pt = a[i + 1];
        *next = i + 2;
    } else {
        *pt = K->c0;
        *next = i + 2;
    }
}

/* mean / median / variance / stddev of a nonempty list (exact when the
 * entries are exact).  The variance is the population variance
 * (1/n) * sum (x_i - mean)^2, normalised with rat_normal; stddev is its
 * square root.  The median sorts a copy by numeric value (insertion sort,
 * O(n^2)) and needs every entry to evaluate to a double; for even n it is
 * the mean of the two middle entries. */
static E *list_stat(int id, E *l) {
    if (l->t != E_LIST || l->n == 0) cas_throw(CAS_ERR_DOMAIN, "expected a nonempty list");
    uint32_t n = l->n;
    E *sum = s_add(n, EK(l));
    E *mean = s_div(sum, mk_int(n));
    if (id == C_MEAN) return mean;
    if (id == C_MEDIAN) {
        E **v = salloc(sizeof(E *) * n);
        memcpy(v, EK(l), sizeof(E *) * n);
        for (uint32_t i = 1; i < n; i++)
            for (uint32_t j = i; j > 0; j--) {
                double a, b;
                if (!to_double(v[j - 1], &a) || !to_double(v[j], &b)) cas_throw(CAS_ERR_DOMAIN, "median: non-numeric");
                if (a <= b) break;
                E *t = v[j]; v[j] = v[j - 1]; v[j - 1] = t;
            }
        if (n & 1) return v[n / 2];
        return s_div(s_add2(v[n / 2 - 1], v[n / 2]), K->c2);
    }
    E **d = salloc(sizeof(E *) * n);
    for (uint32_t i = 0; i < n; i++) d[i] = s_powi(s_sub(EK(l)[i], mean), 2);
    E *var = rat_normal(s_div(s_add(n, d), mk_int(n)));
    if (id == C_VARIANCE) return var;
    return s_sqrt(var);
}

/* Numeric value of e as a double, if it has one (constant expression). */
static int num_val(E *e, double *v) { return to_double(e, v); }

/* sort(list): ascending.  Pairs of entries with numeric values are compared
 * numerically, anything else by the canonical expression order, so mixed
 * lists get a deterministic (though not mathematically meaningful) order.
 * Stable insertion sort, O(n^2). */
static E *sort_list(E *l) {
    if (l->t != E_LIST) cas_throw(CAS_ERR_DOMAIN, "expected a list");
    E **v = salloc(sizeof(E *) * (l->n + 1));
    memcpy(v, EK(l), sizeof(E *) * l->n);
    for (uint32_t i = 1; i < l->n; i++)
        for (uint32_t j = i; j > 0; j--) {
            double a, b;
            int c;
            if (num_val(v[j - 1], &a) && num_val(v[j], &b)) c = a > b;
            else c = expr_cmp(v[j - 1], v[j]) > 0;
            if (!c) break;
            E *t = v[j]; v[j] = v[j - 1]; v[j - 1] = t;
        }
    return s_list(l->n, v);
}

/* seq(f, k, lo, hi [, step]) / makelist: the list f(lo), f(lo+step), ...,
 * up to hi.  Bounds and step must have numeric values; the element count is
 * floor((hi-lo)/step + 1e-9) + 1 (the epsilon keeps decimal steps such as
 * 0.1 from losing the last element to rounding), capped at 100000.  The
 * running value lo + i*step is accumulated with s_add2 (exactly, for exact
 * bounds and step), and each element is subst(f, k, value), re-simplified. */
static E *seq_cmd(uint32_t n, E **a) {
    nargs(n, 4, 5);
    E *f = a[0], *k = a[1];
    if (k->t != E_SYM) cas_throw(CAS_ERR_DOMAIN, "seq: expected a variable");
    E *lo = a[2], *hi = a[3], *st = n > 4 ? a[4] : K->c1;
    double dl, dh, ds;
    if (!to_double(lo, &dl) || !to_double(hi, &dh) || !to_double(st, &ds) || ds == 0)
        cas_throw(CAS_ERR_DOMAIN, "seq: bounds must be numeric");
    double cnt = floor((dh - dl) / ds + 1e-9) + 1;
    if (cnt < 0) cnt = 0;
    if (cnt > 100000) cas_throw(CAS_ERR_MEMORY, "seq: too many elements");
    uint32_t m = (uint32_t)cnt;
    E **v = salloc(sizeof(E *) * (m + 1));
    E *cur = lo;
    for (uint32_t i = 0; i < m; i++) {
        v[i] = subst(f, k, cur);
        cur = s_add2(cur, st);
    }
    return s_list(m, v);
}

/* subst(e, x, v), subst(e, x = v) or subst(e, [x = v, y = w]) (simultaneous
 * substitution); the old side may be any expression subst understands. */
static E *subst_cmd(uint32_t n, E **a) {
    nargs(n, 2, 3);
    E *e = a[0];
    if (n == 3) return subst(e, a[1], a[2]);
    E *eqs = as_list(a[1]);
    E **xs = salloc(sizeof(E *) * (eqs->n + 1)), **vs = salloc(sizeof(E *) * (eqs->n + 1));
    for (uint32_t i = 0; i < eqs->n; i++) {
        E *q = EK(eqs)[i];
        if (!IFN(q, F_EQ)) cas_throw(CAS_ERR_DOMAIN, "subst: expected var=value");
        xs[i] = K0(q);
        vs[i] = K1(q);
    }
    return subst_many(e, eqs->n, xs, vs);
}

/* Cross product of two 3-vectors. */
static E *cross(E *a, E *b) {
    if (!is_vector(a) || !is_vector(b) || a->n != 3 || b->n != 3) cas_throw(CAS_ERR_DOMAIN, "cross: need 3-vectors");
    E **x = EK(a), **y = EK(b);
    E *v[3] = {s_sub(s_mul2(x[1], y[2]), s_mul2(x[2], y[1])), s_sub(s_mul2(x[2], y[0]), s_mul2(x[0], y[2])),
               s_sub(s_mul2(x[0], y[1]), s_mul2(x[1], y[0]))};
    return s_list(3, v);
}

/* norm: |a| for a scalar, the Frobenius norm sqrt(sum |a_ij|^2) for a
 * matrix, the Euclidean norm for a vector.  For real constant entries the
 * abs() is dropped (x^2 instead of abs(x)^2) to give a simpler result. */
/* norm(A, p): p = inf (max row sum / max |x_i|), 1 (max column sum / sum
 * |x_i|), or a number p for vectors; NULL for p = 2 (norm_cmd) */
/* the numeric linear algebra slot (casx.h CASX_FN_NUMLA): k = 0 eigenvals, 1 eigenvects, 2 rank */
static E *numla(E *A, long k) {
    E *v[2] = {A, mk_int(k)};
    return casx_eslot(CASX_FN_NUMLA, 2, v);
}

static E *norm_p(E *a, E *p) {
    uint32_t r, c, m = 0;
    int inf = ISYM(p, S_INF), mat = is_matrix(a, &r, &c);
    if (!inf && !is_one(p) && (mat || !is_num(p) || is_int_val(p, 2))) return NULL;
    uint32_t outer = mat ? (inf ? r : c) : (inf ? a->n : 1), inner = mat ? (inf ? c : r) : (inf ? 1 : a->n);
    E **mx = salloc(sizeof(E *) * outer), **t = salloc(sizeof(E *) * inner);
    for (uint32_t o = 0; o < outer; o++) {
        for (uint32_t k = 0; k < inner; k++) {
            E *x = mat ? EK(EK(a)[inf ? o : k])[inf ? k : o] : EK(a)[inf ? o : k];
            t[k] = mat || inf ? s_fn1(F_ABS, x) : s_pow(s_fn1(F_ABS, x), p);
        }
        mx[m++] = s_add(inner, t);
    }
    if (!mat && !inf) return s_pow(mx[0], s_inv(p));
    return m == 1 ? mx[0] : s_fn(F_MAX, m, mx);
}

static E *norm_cmd(E *a) {
    if (a->t != E_LIST) return s_fn1(F_ABS, a);
    uint32_t r, c;
    if (is_matrix(a, &r, &c)) {
        /* Frobenius norm */
        E **t = salloc(sizeof(E *) * r * c);
        uint32_t k = 0;
        for (uint32_t i = 0; i < r; i++)
            for (uint32_t j = 0; j < c; j++) t[k++] = s_powi(s_fn1(F_ABS, EK(EK(a)[i])[j]), 2);
        return s_sqrt(s_add(k, t));
    }
    E **t = salloc(sizeof(E *) * a->n);
    for (uint32_t i = 0; i < a->n; i++) {
        E *x = EK(a)[i];
        t[i] = freeof_sym(x, S_I) && is_const_expr(x) ? s_powi(x, 2) : s_powi(s_fn1(F_ABS, x), 2);
    }
    return s_sqrt(s_add(a->n, t));
}

/* matrix(r, c [, f]): an r x c matrix (r*c <= 10000).  Without f it is the
 * zero matrix; if f names a user function, entry (i, j) is f(i, j) with
 * 1-based indices (evaluated through eval); any other f is used as a
 * constant fill value. */
static E *matrix_cmd(uint32_t n, E **a) {
    nargs(n, 2, 3);
    long r = int_arg(a[0]), c = int_arg(a[1]);
    if (r < 1 || c < 1 || r * c > 10000) cas_throw(CAS_ERR_DOMAIN, "bad matrix size");
    E **rows = salloc(sizeof(E *) * r);
    for (long i = 0; i < r; i++) {
        E **v = salloc(sizeof(E *) * c);
        for (long j = 0; j < c; j++) {
            if (n == 3) {
                E *f = a[2];
                if (f->t == E_SYM && K->syms[f->s].fdef) {
                    E *args[2] = {mk_int(i + 1), mk_int(j + 1)};
                    v[j] = eval(mk_fn(f->s, 2, args));
                } else
                    v[j] = f;
            } else
                v[j] = K->c0;
        }
        rows[i] = s_list((uint32_t)c, v);
    }
    return s_list((uint32_t)r, rows);
}

/* First power sym^e with a non-numeric exponent in e, or NULL. */
static E *find_symexp(E *e) {
    stack_check();
    if (e->t == E_POW && K0(e)->t == E_SYM && !is_const_expr(K0(e)) && !is_num(K1(e))) return e;
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW) return NULL;
    for (uint32_t i = 0; i < e->n; i++) {
        E *r = find_symexp(EK(e)[i]);
        if (r) return r;
    }
    return NULL;
}

/* e with x^(c+s) -> x^c T for integers c >= 0; NULL if some x^(...) does not fit. */
static E *symexp_sub(E *e, E *x, E *s, E *T) {
    stack_check();
    if (e->t == E_POW && expr_eq(K0(e), x) && !is_num(K1(e))) {
        E *c = s_sub(K1(e), s);
        return c->t == E_INT && int_sgn(c) >= 0 ? s_mul2(s_pow(x, c), T) : NULL;
    }
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW) return e;
    E **k = salloc(sizeof(E *) * e->n);
    for (uint32_t i = 0; i < e->n; i++)
        if (!(k[i] = symexp_sub(EK(e)[i], x, s, T))) return NULL;
    return rebuild(e, k);
}

/* gcd / lcm of a and b containing powers x^(c+s) with a symbolic s: with
 * T = x^s each must be T^p A0 (A0 polynomial in x).  gcd = T^pmin gcd(A0, B0)
 * when the p agree or the side with the smaller p has A0(0) != 0 (T cannot
 * share factors with it); lcm needs equal p.  NULL otherwise (unevaluated):
 * gcd(2x^(n+4) - x^(n+2), 4x^(n+1) + 3x^n) = x^n. */
static E *symexp_gcd(E *a, E *b, int lcm) {
    E *p = find_symexp(a), *T = mk_sym_name("_T"), *ab[2] = {a, b}, *a0[2];
    if (!p) p = find_symexp(b);
    E *x = K0(p), *s = K1(p);
    if (s->t == E_ADD && K0(s)->t == E_INT) s = s_sub(s, K0(s));
    long d[2];
    for (int i = 0; i < 2; i++) {
        E *t = symexp_sub(ab[i], x, s, T);
        if (!t || find_symexp(t = expand(t)) || !poly_degree_in(t, T, &d[i])) return NULL;
        a0[i] = expand(s_div(t, s_powi(T, d[i])));
        if (!freeof(a0[i], T)) return NULL;
    }
    int lo = d[1] < d[0];
    if (d[0] != d[1] && (lcm || is_zero(expand(subst(a0[lo], x, K->c0))))) return NULL;
    E *g = lcm ? poly_lcm_expr(a0[0], a0[1]) : poly_gcd_expr(a0[0], a0[1]);
    return s_mul2(g, s_pow(x, s_mul2(s, mk_int(d[lo]))));
}

/* gcd / lcm of n polynomials (or integers), folded pairwise; a single list
 * argument is expanded.  gcd() = 0 and lcm() = 1 for no arguments. */
static E *gcd_many(uint32_t n, E **a, int lcm) {
    stack_check();
    if (n == 1 && a[0]->t == E_LIST) return gcd_many(a[0]->n, EK(a[0]), lcm);
    if (n == 0) return lcm ? K->c1 : K->c0;
    E *g = a[0];
    for (uint32_t i = 1; i < n; i++) {
        if (find_symexp(g) || find_symexp(a[i])) {
            if (!(g = symexp_gcd(g, a[i], lcm))) return inert(lcm ? C_LCM : C_GCD, n, a);
        } else
            g = lcm ? poly_lcm_expr(g, a[i]) : poly_gcd_expr(g, a[i]);
    }
    return g;
}

/* All positive divisors of the integer n, ascending.  From n = prod p_i^m_i
 * (int_factor) the divisors are generated multiplicatively: start with [1]
 * and for each prime append d*p^k for k = 1..m_i to the divisors found so
 * far.  The count prod (m_i + 1) is limited to 100000.  The final sort is an
 * insertion sort. */
static E *divisors(E *n) {
    n = int_abs(int_only(n));
    if (int_is_zero(n)) cas_throw(CAS_ERR_DOMAIN, "divisors of 0");
    E *fl = int_factor(n);
    uint32_t cnt = 1;
    for (uint32_t i = 0; i < fl->n; i++) {
        long m = int_arg(K1(EK(fl)[i]));
        cnt *= (uint32_t)(m + 1);
        if (cnt > 100000) cas_throw(CAS_ERR_MEMORY, "too many divisors");
    }
    E **d = salloc(sizeof(E *) * cnt);
    uint32_t nd = 1;
    d[0] = K->c1;
    for (uint32_t i = 0; i < fl->n; i++) {
        E *p = K0(EK(fl)[i]);
        long m = int_arg(K1(EK(fl)[i]));
        uint32_t base = nd;
        E *pk = K->c1;
        for (long k = 1; k <= m; k++) {
            pk = int_mul(pk, p);
            for (uint32_t j = 0; j < base; j++) d[nd++] = int_mul(d[j], pk);
        }
    }
    for (uint32_t i = 1; i < nd; i++)
        for (uint32_t j = i; j > 0 && int_cmp(d[j - 1], d[j]) > 0; j--) {
            E *t = d[j]; d[j] = d[j - 1]; d[j - 1] = t;
        }
    return s_list(nd, d);
}

/* Euler's totient phi(n) = n * prod over primes p | n of (1 - 1/p), computed
 * exactly as n / p * (p - 1) for each distinct prime.  phi(0) = 0, and the
 * sign of n is ignored. */
static E *euler_phi(E *n) {
    n = int_abs(int_only(n));
    if (int_is_zero(n)) return K->c0;
    E *fl = int_factor(n), *r = n;
    for (uint32_t i = 0; i < fl->n; i++) {
        E *p = K0(EK(fl)[i]);
        r = int_mul(int_divexact(r, p), int_sub(p, K->c1));
    }
    return r;
}

/* Jacobian matrix J_ij = d f_i / d v_j of a list of functions (a scalar
 * counts as a one-element list, giving the gradient as a 1 x n matrix). */
static E *jacobian(E *fs, E *vars) {
    fs = as_list(fs);
    vars = as_list(vars);
    E **rows = salloc(sizeof(E *) * fs->n);
    for (uint32_t i = 0; i < fs->n; i++) {
        E **r = salloc(sizeof(E *) * vars->n);
        for (uint32_t j = 0; j < vars->n; j++) r[j] = diff(EK(fs)[i], EK(vars)[j]);
        rows[i] = s_list(vars->n, r);
    }
    return s_list(fs->n, rows);
}

/* lagrange(xs, ys [, x]): the interpolating polynomial through the points
 * (xs_i, ys_i) in Lagrange form,
 *     sum_i ys_i * prod_{j != i} (x - xs_j) / (xs_i - xs_j),
 * expanded.  O(n^2) symbolic products; the xs must be distinct (a repeated
 * abscissa makes a denominator zero). */
static E *lagrange(E *xs, E *ys, E *x) {
    if (xs->t != E_LIST || ys->t != E_LIST || xs->n != ys->n) cas_throw(CAS_ERR_DOMAIN, "lagrange: need two lists");
    uint32_t n = xs->n;
    E **t = salloc(sizeof(E *) * n);
    for (uint32_t i = 0; i < n; i++) {
        E *term = EK(ys)[i];
        for (uint32_t j = 0; j < n; j++) {
            if (j == i) continue;
            term = s_mul2(term, s_div(s_sub(x, EK(xs)[j]), s_sub(EK(xs)[i], EK(xs)[j])));
        }
        t[i] = term;
    }
    return expand(s_add(n, t));
}

/* resultant(a, b, x): determinant of the Sylvester matrix (upoly.c,
 * total degree <= 60). */
static E *resultant_cmd(E *a, E *b, E *x) { return resultant_expr(a, b, x); }

/* type(e): a string naming the node type ("integer", "rational", "real",
 * "identifier", "matrix", "vector", "string", "equation", "function", or
 * "expression" for sums, products and powers). */
static E *type_name(E *e) {
    const char *s = "expression";
    switch (e->t) {
    case E_INT: s = "integer"; break;
    case E_RAT: s = "rational"; break;
    case E_FLT: s = "real"; break;
    case E_SYM: s = "identifier"; break;
    case E_LIST: s = is_matrix(e, NULL, NULL) ? "matrix" : "vector"; break;
    case E_STR: s = "string"; break;
    case E_FN: s = IFN(e, F_EQ) ? "equation" : "function"; break;
    }
    return mk_str(s, strlen(s));
}

/* Unused placeholder (lnexpand is implemented by ln_expand in trig.c); kept
 * referenced at the end of eval_cmd to silence the unused-function warning. */
static E *lnexpand_rec(E *e);

/* odesolve: numeric solution y(x1) of the scalar initial value problem
 * y' = f(x, y), y(x0) = y0, by the adaptive Runge-Kutta-Fehlberg 4(5) method
 * (Fehlberg 1969; see e.g. Burden & Faires, "Numerical Analysis", or Hairer,
 * Norsett & Wanner, "Solving ODEs I", sec. II.4).
 *
 * Each step evaluates f at six stages with the Butcher tableau A / C below
 * and forms a 4th-order solution y4 and a 5th-order solution y5 from the same
 * stages; |y5 - y4| estimates the local error.  A step is accepted if the
 * error is below tol = 1e-12 * (1 + |y|) (mixed absolute/relative), and the
 * 5th-order value is kept ("local extrapolation").  The next step size is
 * h * 0.84 * (tol / err)^(1/4), clamped to [h/10, 4h]; 0.84 is the usual
 * safety factor.  A step that fails although |h| < 1e-12 (1 + |x|) means a
 * singularity (e.g. y' = y^2 blowing up): CAS_ERR_DOMAIN.  The first
 * trial step is (x1 - x0) / 100 and the last step is shortened to land
 * exactly on x1; integration in the negative direction works the same way.
 *
 * f is converted to floating point once (evalf) and then evaluated by
 * substituting double values for x and y at every stage, which allocates in
 * scratch; the loop state is all doubles, so that scratch is released after
 * every step.
 * Errors: non-numeric initial values or f, NaN / overflow of the solution
 * (CAS_ERR_DOMAIN); interrupts are polled each step.  At most 200000 steps
 * are taken; if x1 is not reached by then CAS_ERR_UNSUPPORTED is raised. */
static E *odesolve_num(E *f, E *x, E *y, E *x0e, E *y0e, E *x1e) {
    double x0, y0, x1;
    if (!to_double(x0e, &x0) || !to_double(y0e, &y0) || !to_double(x1e, &x1))
        cas_throw(CAS_ERR_DOMAIN, "odesolve: numeric initial values expected");
    E *fe = evalf(f);
    E *X = x, *Y = y;
    double h = (x1 - x0) / 100, xc = x0, yc = y0;
    if (h == 0) return mk_flt(y0);
    char *mark = smark();
    for (int it = 0; it < 200000 && (h > 0 ? xc < x1 : xc > x1); it++) {
        poll_intr();
        if ((h > 0 && xc + h > x1) || (h < 0 && xc + h < x1)) h = x1 - xc;
        double k[6];
        static const double A[6][5] = {{0}, {0.25}, {3.0 / 32, 9.0 / 32},
                                       {1932.0 / 2197, -7200.0 / 2197, 7296.0 / 2197},
                                       {439.0 / 216, -8, 3680.0 / 513, -845.0 / 4104},
                                       {-8.0 / 27, 2, -3544.0 / 2565, 1859.0 / 4104, -11.0 / 40}};
        static const double C[6] = {0, 0.25, 3.0 / 8, 12.0 / 13, 1, 0.5};
        int bad = 0;
        for (int i = 0; i < 6 && !bad; i++) {
            double yy = yc;
            for (int j = 0; j < i; j++) yy += h * A[i][j] * k[j];
            E *xs[2] = {X, Y};
            E *vv[2] = {mk_flt(xc + C[i] * h), mk_flt(yy)};
            double v;
            if (!to_double(subst_many(fe, 2, xs, vv), &v)) bad = 1;
            k[i] = v;
        }
        if (bad) cas_throw(CAS_ERR_DOMAIN, "odesolve: f is not numeric along the path");
        double y4 = yc + h * (25.0 / 216 * k[0] + 1408.0 / 2565 * k[2] + 2197.0 / 4104 * k[3] - 0.2 * k[4]);
        double y5 = yc + h * (16.0 / 135 * k[0] + 6656.0 / 12825 * k[2] + 28561.0 / 56430 * k[3] - 9.0 / 50 * k[4] +
                              2.0 / 55 * k[5]);
        double err = fabs(y5 - y4), tol = 1e-12 * (1 + fabs(y5));
        if (err <= tol) {
            xc += h;
            yc = y5;
        } else if (!(fabs(h) >= 1e-12 * (1 + fabs(xc)))) {
            /* the step cannot be shrunk any further: y (or f) is singular */
            cas_throw(CAS_ERR_DOMAIN, "odesolve: singularity or blow-up in the solution");
        }
        double sc = err > 0 ? 0.84 * pow(tol / err, 0.25) : 2;
        if (sc < 0.1) sc = 0.1;
        if (sc > 4) sc = 4;
        h *= sc;
        if (!(yc == yc) || fabs(yc) > 1e300) cas_throw(CAS_ERR_DOMAIN, "odesolve: solution blows up");
        srelease(mark);
    }
    if (h > 0 ? xc < x1 : xc > x1) cas_throw(CAS_ERR_UNSUPPORTED, "odesolve: too many steps");
    return mk_flt(yc);
}

/* cfactor: factor over Q (factor_expr), then split every quadratic factor
 * a*x^2 + b*x + c with coefficients free of x into linear factors over C
 * with the quadratic formula:
 *     a*x^2 + b*x + c = a * (x - r1) * (x - r2),  r1,2 = (-b +- sqrt(D)) / (2a),
 * where D = b^2 - 4ac (sqrt of a negative D simplifies to i*sqrt(-D)).  The
 * variable of each factor is chosen with default_var.  A repeated factor
 * (q)^e becomes a^e * (x - r1)^e * (x - r2)^e.  The resulting product is assembled raw (not
 * re-simplified, flagged F_SIMP) so the factors stay in the order produced. */
static E *cfactor_expr(E *e) {
    E *f = factor_expr(e);
    E **fs = f->t == E_MUL ? EK(f) : &f;
    uint32_t n = f->t == E_MUL ? f->n : 1;
    E **out = salloc(sizeof(E *) * (2 * n + 2));
    uint32_t no = 0;
    for (uint32_t i = 0; i < n; i++) {
        E *fi = fs[i];
        E *b = fi->t == E_POW ? K0(fi) : fi, *ex = fi->t == E_POW ? K1(fi) : K->c1;
        E *x = default_var(b);
        long d;
        if (b->t == E_ADD && poly_degree_in(b, x, &d) && d == 2) {
            E *qa = poly_coeff(b, x, 2), *qb = poly_coeff(b, x, 1), *qc = poly_coeff(b, x, 0);
            if (freeof(qa, x) && freeof(qb, x) && freeof(qc, x)) {
                E *disc = rat_normal(s_sub(s_powi(qb, 2), s_mul2(mk_int(4), s_mul2(qa, qc))));
                E *sd = s_sqrt(disc);
                E *r1 = s_div(s_add2(s_neg(qb), sd), s_mul2(K->c2, qa)), *r2 = s_div(s_sub(s_neg(qb), sd), s_mul2(K->c2, qa));
                E *l1 = s_sub(x, r1), *l2 = s_sub(x, r2);
                if (!is_one(qa)) out[no++] = s_pow(qa, ex);
                E *p1 = enode(E_POW, 2), *p2 = enode(E_POW, 2);
                K0(p1) = l1; K1(p1) = ex; p1->f = F_SIMP;
                K0(p2) = l2; K1(p2) = ex; p2->f = F_SIMP;
                out[no++] = is_one(ex) ? l1 : p1;
                out[no++] = is_one(ex) ? l2 : p2;
                continue;
            }
        }
        out[no++] = fi;
    }
    if (no == 1) return out[0];
    E *r = enode(E_MUL, no);
    for (uint32_t i = 0; i < no; i++) EK(r)[i] = out[i];
    r->f = F_SIMP;
    return r;
}

/* Execute command `id` on the evaluated arguments a[0..n).  Called by
 * eval_fn (eval.c); also the target of a few internal re-evaluations.
 * Returns the result in scratch.  Throws CAS_ERR_DOMAIN for bad arguments,
 * CAS_ERR_UNSUPPORTED for "could not be determined" results of limit and
 * series and for ids without a case below, and whatever the called
 * algorithms throw (memory, interrupt).  Side effects: assume/unassume
 * change symbol assumption flags, restart clears all user variables and
 * functions.  Ids not handled here but listed as commands are statistics
 * (stats.c) or a few extension commands forwarded to xcmds.c. */
E *eval_cmd(int id, uint32_t n, E **a) {
    E *x, *pt, *nv[2];
    uint32_t nx;
    if (STATS_IS_CMD(id)) return stats_cmd(id, n, a); /* stats.c */
    if (n == 1 && (id == C_GRAD || id == C_DIVERGENCE || id == C_CURL)) { /* ∇f */
        nv[0] = a[0], nv[1] = nabla_vars(a[0]);
        a = nv, n = 2;
    }
    switch (id) {
    /* ------------------------------------------------ calculus */
    /* diff(f) in the default variable; diff(f, [x, y]) = gradient list;
     * diff(f, x, k) = k-th derivative; diff(f, x, y, ...) = mixed partials */
    case C_DIFF: {
        nargs(n, 1, 16);
        E *f = a[0];
        if (n == 1) return diff(f, default_var(f));
        if (a[1]->t == E_LIST && n == 2) {
            /* gradient */
            E **g = salloc(sizeof(E *) * a[1]->n);
            for (uint32_t i = 0; i < a[1]->n; i++) g[i] = diff(f, EK(a[1])[i]);
            return s_list(a[1]->n, g);
        }
        if (n == 3 && a[1]->t == E_SYM && a[2]->t == E_INT) {
            long k = int_arg(a[2]);
            if (k < 0 || k > 1000) cas_throw(CAS_ERR_DOMAIN, "bad derivative order");
            return diffn(f, a[1], k);
        }
        /* symbolic order k (assumed integer): d^k/dx^k x^k = k!, else unevaluated */
        if (n == 3 && a[1]->t == E_SYM && a[2]->t == E_SYM && is_int_assumed(a[2])) {
            if (f->t == E_POW && expr_eq(K0(f), a[1]) && expr_eq(K1(f), a[2])) return s_fn1(F_FACT, a[2]);
            return inert(C_DIFF, 3, a);
        }
        for (uint32_t i = 1; i < n; i++) {
            if (a[i]->t != E_SYM) cas_throw(CAS_ERR_DOMAIN, "diff: expected variables");
            f = diff(f, a[i]);
        }
        return f;
    }
    /* integrate(f [, x]) indefinite (unevaluated if no antiderivative is
     * found); integrate(f, x, b) = from 0 to b; integrate(f, x, a, b) */
    case C_INTEGRATE:
    case C_INT: {
        nargs(n, 1, 4);
        E *f = a[0];
        x = var_arg(n, a, 1, f);
        if (n == 4 || n == 3) {
            E *lo = n == 4 ? a[2] : K->c0, *hi = n == 4 ? a[3] : a[2];
            return integrate_def(f, x, lo, hi);
        }
        E *r = integrate(f, x);
        if (r) return r;
        E *v[2] = {f, x};
        return inert(C_INTEGRATE, 2, v);
    }
    /* nint(f, x, a, b): numeric quadrature (adaptive Gauss-Kronrod in
     * integ.c, despite the historical id name) */
    case C_ROMBERG: {
        nargs(n, 4, 4);
        return nintegrate(a[0], a[1], a[2], a[3]);
    }
    /* nderiv(f, x, x0): central difference (f(x0+h) - f(x0-h)) / 2h with
     * h = 1e-5 * (|x0| + 1).  The truncation error is O(h^2) and the
     * rounding error O(eps/h), so h ~ eps^(1/3) ~ 1e-5 balances the two and
     * gives roughly 10 correct digits for smooth f. */
    case C_NDIFF: {
        nargs(n, 3, 3);
        double h = 1e-5, x0;
        if (!to_double(a[2], &x0)) cas_throw(CAS_ERR_DOMAIN, "nderiv: point must be numeric");
        h = 1e-5 * (fabs(x0) + 1);
        E *f1 = evalf(subst(a[0], a[1], mk_flt(x0 + h))), *f0 = evalf(subst(a[0], a[1], mk_flt(x0 - h)));
        double v1, v0;
        if (!to_double(f1, &v1) || !to_double(f0, &v0)) cas_throw(CAS_ERR_DOMAIN, "nderiv: not numeric");
        return mk_flt((v1 - v0) / (2 * h));
    }
    /* limit(f, x = a [, dir]) or limit(f, x, a [, dir]); dir > 0 from the
     * right, < 0 from the left, absent/0 two-sided (series.c: direct
     * evaluation, series, Gruntz's algorithm, then a limit calculus) */
    case C_LIMIT: {
        nargs(n, 2, 4);
        var_point(n, a, 1, a[0], &x, &pt, &nx);
        int dir = 0;
        if (n > nx) {
            double d;
            if (!to_double(a[nx], &d)) cas_throw(CAS_ERR_DOMAIN, "limit: direction must be +1 or -1");
            dir = d > 0 ? 1 : d < 0 ? -1 : 0;
        }
        E *r = limit_expr(a[0], x, pt, dir);
        if (!r && CASX_SLOTS) { /* the extension's methods (cas/ext/alg/sums.c: Stirling) */
            E *v[4] = {a[0], x, pt, mk_int(dir)};
            r = casx_eslot(CASX_FN_LIMIT, 4, v);
        }
        if (r) return r;
        cas_throw(CAS_ERR_UNSUPPORTED, "limit could not be determined");
    }
    /* series(f, x = a, n) / taylor: truncated expansion to order n
     * (default 6, at most 200) about a (default 0) */
    case C_SERIES:
    case C_TAYLOR: {
        nargs(n, 1, 4);
        var_point(n, a, 1, a[0], &x, &pt, &nx);
        long ord = 6;
        if (n > nx) ord = int_arg(a[nx]);
        if (ord < 0 || ord > 200) cas_throw(CAS_ERR_DOMAIN, "bad series order");
        E *r = series_expr(a[0], x, pt, ord);
        if (r) return r;
        cas_throw(CAS_ERR_UNSUPPORTED, "series expansion failed");
    }
    /* sum(list) / product(list) of the elements; sum(f, k, a, b) symbolic
     * (sum.c), returned unevaluated when no closed form is found */
    case C_SUM:
    case C_PRODUCT: {
        if (n == 1 && a[0]->t == E_LIST) return id == C_SUM ? s_add(a[0]->n, EK(a[0])) : s_mul(a[0]->n, EK(a[0]));
        nargs(n, 4, 4);
        if (a[1]->t != E_SYM) cas_throw(CAS_ERR_DOMAIN, "sum: expected a variable");
        E *r = id == C_SUM ? sum_expr(a[0], a[1], a[2], a[3]) : product_expr(a[0], a[1], a[2], a[3]);
        if (!r && CASX_SLOTS) r = casx_eslot(id == C_SUM ? CASX_FN_SUM : CASX_FN_PRODUCT, 4, a); /* cas/ext/alg/sums.c */
        if (r) return r;
        return inert(id, n, a);
    }
    /* ------------------------------------------------ solving */
    /* solve(eq [, vars]) over the reals, csolve over C.  Without vars: a
     * system (list) with several symbols is solved for all of them (up to
     * 16, alphabetical), a single equation for its default variable. */
    case C_SOLVE:
    case C_CSOLVE: {
        nargs(n, 1, id == C_SOLVE ? 4 : 2); /* solve(eq, x, a, b): solutions in [a, b] (cas/ext/alg) */
        if (n == 3) nargs(n, 1, 2);
        E *vars = n > 1 ? a[1] : NULL;
        if (!vars) {
            E *v[16];
            uint32_t nv = collect_syms(a[0], v, 16);
            if (nv == 0) cas_throw(CAS_ERR_DOMAIN, "solve: no variable");
            if (a[0]->t == E_LIST && nv > 1) vars = s_list(nv, v);
            else vars = default_var(a[0]);
        }
        if (CASX_SLOTS && id == C_SOLVE) {
            E *v[4] = {a[0], vars, n > 2 ? a[2] : NULL, n > 2 ? a[3] : NULL}, *r = casx_eslot(CASX_FN_SOLVE, n > 2 ? 4 : 2, v);
            if (r) return r;
        }
        if (n > 2) cas_throw(CAS_ERR_UNSUPPORTED, "solve on an interval needs the advanced CAS module");
        return solve_expr(a[0], vars, id == C_CSOLVE);
    }
    /* fsolve(eq, x [, guess]) or fsolve(eq, x = guess): numeric root
     * (solve.c; systems with lists of equations and unknowns) */
    case C_FSOLVE:
    case C_NSOLVE: {
        nargs(n, 1, 3);
        E *v = n > 1 ? a[1] : default_var(a[0]);
        E *g = n > 2 ? a[2] : NULL;
        if (IFN(v, F_EQ)) {
            g = K1(v);
            v = K0(v);
        }
        return fsolve_expr(a[0], v, g);
    }
    case C_LINSOLVE: {
        nargs(n, 2, 2);
        E *r = linsolve(as_list(a[0]), as_list(a[1]));
        if (!r) cas_throw(CAS_ERR_DOMAIN, "linsolve: system is not linear");
        return r;
    }
    /* ------------------------------------------------ algebra
     * (a second argument of factor / cfactor / sqrfree is accepted for
     * compatibility and ignored) */
    case C_SIMPLIFY: nargs(n, 1, 1); return simplify_full(a[0]);
    case C_EXPAND: nargs(n, 1, 1); return expand(a[0]);
    case C_FACTOR: nargs(n, 1, 2); return factor_expr(a[0]);
    case C_CFACTOR: nargs(n, 1, 2); return cfactor_expr(a[0]);
    case C_LU: nargs(n, 1, 1); return mat_lu(a[0]);
    case C_QR: nargs(n, 1, 1); return mat_qr(a[0]);
    case C_CHOLESKY: nargs(n, 1, 1); return mat_cholesky(a[0]);
    case C_SQRFREE: nargs(n, 1, 2); return sqrfree_expr(a[0]);
    /* factors(p): list of [factor, multiplicity] pairs; the numeric content
     * returned by factor_list is not included in the list */
    case C_FACTORS: {
        nargs(n, 1, 1);
        E *c;
        E *l = factor_list(a[0], &c);
        return l;
    }
    /* collect / partfrac default to default_var when no variable is given;
     * normal, ratsimp and together are all the rational normal form
     * (numerator and denominator with their gcd cancelled, poly.c) */
    case C_COLLECT: nargs(n, 1, 2); return collect_expr(a[0], var_arg(n, a, 1, a[0]));
    case C_NORMAL: case C_RATSIMP: case C_TOGETHER: nargs(n, 1, 1); return rat_normal(a[0]);
    case C_PARTFRAC: case C_APART: nargs(n, 1, 2); return partfrac(a[0], var_arg(n, a, 1, a[0]));
    case C_SUBST: case C_SUBS: return subst_cmd(n, a);
    /* approx(e [, digits]): floating-point value; with explicit digits the
     * constant parts are evaluated with arbitrary precision (bf.c).  At top
     * level ext_post (xcmds.c) applies the digit count once more. */
    case C_APPROX: case C_EVALF: case C_FLOATS: {
        nargs(n, 1, 2);
        if (n == 2) {
            long d = int_arg(a[1]);
            if (d < 1 || d > 5000) cas_throw(CAS_ERR_DOMAIN, "digits must be 1..5000");
            return evalf_digits(a[0], (int)d);
        }
        return evalf_digits(a[0], K->set.digits);
    }
    case C_EXACT: nargs(n, 1, 1); return exact_expr(a[0]);
    case C_NUMER: nargs(n, 1, 1); return numer_of(a[0]);
    case C_DENOM: nargs(n, 1, 1); return denom_of(a[0]);
    /* quo / rem / divide: for two integers, floor division (remainder has
     * the sign of the divisor; contrast iquo/irem, which truncate);
     * otherwise polynomial division in x, divide returning [q, r] */
    case C_QUO: case C_REM: case C_DIVIDE: {
        nargs(n, 2, 3);
        if (a[0]->t == E_INT && a[1]->t == E_INT) {
            E *q, *r;
            int_fdivmod(a[0], a[1], &q, &r);
            if (id == C_QUO) return q;
            if (id == C_REM) return r;
            E *v[2] = {q, r};
            return s_list(2, v);
        }
        x = n > 2 ? a[2] : default_var(s_add2(a[0], a[1]));
        E *r;
        E *q = poly_quo_rem(a[0], a[1], x, &r);
        if (id == C_QUO) return q;
        if (id == C_REM) return r;
        E *v[2] = {q, r};
        return s_list(2, v);
    }
    case C_GCD: return gcd_many(n, a, 0);
    case C_LCM: return gcd_many(n, a, 1);
    case C_DEGREE: {
        nargs(n, 1, 2);
        long d;
        x = var_arg(n, a, 1, a[0]);
        if (!poly_degree_in(a[0], x, &d)) cas_throw(CAS_ERR_DOMAIN, "degree: not a polynomial");
        return mk_int(d);
    }
    /* coeff(p, k) or coeff(p, x, k): coefficient of x^k (poly.c, which
     * throws "not a polynomial" when p is not polynomial in x) */
    case C_COEFF: {
        nargs(n, 2, 3);
        if (n == 2) return poly_coeff(a[0], default_var(a[0]), int_arg(a[1]));
        return poly_coeff(a[0], a[1], int_arg(a[2]));
    }
    /* coeffs: all coefficients, highest degree first (so lcoeff is the
     * first entry; the list is never empty, the zero polynomial gives [0]) */
    case C_COEFFS: nargs(n, 1, 2); return poly_coeff_list(a[0], var_arg(n, a, 1, a[0]), NULL);
    case C_LCOEFF: {
        nargs(n, 1, 2);
        E *l = poly_coeff_list(a[0], var_arg(n, a, 1, a[0]), NULL);
        return EK(l)[0];
    }
    /* content = gcd of the coefficients in x (coefficients may themselves
     * be polynomials in other variables); primpart = p / content */
    case C_CONTENT: case C_PRIMPART: {
        nargs(n, 1, 2);
        x = var_arg(n, a, 1, a[0]);
        E *l = poly_coeff_list(a[0], x, NULL);
        E *g = gcd_many(l->n, EK(l), 0);
        if (id == C_CONTENT) return g;
        return rat_normal(s_div(a[0], g));
    }
    case C_ROOTS: nargs(n, 1, 2); return roots_expr(a[0], var_arg(n, a, 1, a[0]));
    case C_PROOT: nargs(n, 1, 2); return proot_expr(a[0], n > 1 ? a[1] : NULL);
    case C_RESULTANT: nargs(n, 2, 3); return resultant_cmd(a[0], a[1], n > 2 ? a[2] : default_var(s_add2(a[0], a[1])));
    /* discriminant of p of degree d with leading coefficient lc:
     *     disc(p) = (-1)^(d(d-1)/2) * res(p, p') / lc
     * (e.g. b^2 - 4ac for a quadratic) */
    case C_DISCRIM: {
        nargs(n, 1, 2);
        x = var_arg(n, a, 1, a[0]);
        long d;
        if (!poly_degree_in(a[0], x, &d) || d < 1) cas_throw(CAS_ERR_DOMAIN, "discriminant: need a polynomial");
        E *lc = poly_coeff(a[0], x, d);
        E *r = resultant_expr(a[0], diff(a[0], x), x);
        E *sg = ((d * (d - 1) / 2) & 1) ? K->cm1 : K->c1;
        return rat_normal(s_mul2(sg, s_div(r, lc)));
    }
    /* horner(p, v [, x]): the value of p at x = v (named after Horner's
     * scheme; implemented as substitution + simplification) */
    case C_HORNER: {
        nargs(n, 2, 3);
        x = n > 2 ? a[2] : default_var(a[0]);
        return subst(a[0], x, a[1]);
    }
    /* trig / exp / log rewriting (trig.c; expexpand in xcmds.c); powexpand
     * is plain expand */
    case C_TEXPAND: nargs(n, 1, 1); return trig_expand(a[0]);
    case C_TLIN: nargs(n, 1, 1); return trig_linearize(a[0]);
    case C_TCOLLECT: nargs(n, 1, 1); return trig_collect(a[0]);
    case C_TRIGSIMP: nargs(n, 1, 1); return trig_simplify(a[0]);
    case C_TRIG2EXP: nargs(n, 1, 1); return trig_to_exp(a[0]);
    case C_EXPLN: nargs(n, 1, 1); return exp_to_trig(a[0]);
    case C_LNEXPAND: nargs(n, 1, 1); return ln_expand(a[0]);
    case C_LNCOLLECT: nargs(n, 1, 1); return ln_collect(a[0]);
    case C_EXPEXPAND: { extern E *exp_expand(E *e); nargs(n, 1, 1); return exp_expand(a[0]); }
    case C_POWEXPAND: nargs(n, 1, 1); return expand(a[0]);
    /* ------------------------------------------------ integers */
    /* ------------------------------------------------ integers
     * ifactor goes through factor_expr, whose integer case factors with
     * trial division + Pollard-Brent rho (big.c) and prints p^k products;
     * isprime is Miller-Rabin (deterministic below 3.3e24). */
    case C_IFACTOR: nargs(n, 1, 1); return factor_expr(int_only(a[0]));
    case C_ISPRIME: nargs(n, 1, 1); return bool_e(int_isprime(int_only(a[0])));
    case C_NEXTPRIME: nargs(n, 1, 1); return int_nextprime(int_only(a[0]));
    /* prevprime(n): largest prime < n, by downward search with the
     * Miller-Rabin test (big.c); error if n <= 2 */
    case C_PREVPRIME: {
        nargs(n, 1, 1);
        E *c = int_sub(int_only(a[0]), K->c1);
        if (int_cmp(c, K->c2) < 0) cas_throw(CAS_ERR_DOMAIN, "no smaller prime");
        while (!int_isprime(c)) c = int_sub(c, K->c1);
        return c;
    }
    case C_IQUO: case C_IREM: {
        nargs(n, 2, 2);
        E *q, *r;
        int_tdivmod(int_only(a[0]), int_only(a[1]), &q, &r);
        return id == C_IQUO ? q : r;
    }
    case C_DIVISORS: nargs(n, 1, 1); return divisors(a[0]);
    case C_EULER: nargs(n, 1, 1); return euler_phi(a[0]);
    /* perm(n, k) = n! / (n-k)! = n (n-1) ... (n-k+1); 0 if k > n or k < 0 */
    case C_PERM: case C_NPR: {
        nargs(n, 2, 2);
        E *nn = int_only(a[0]), *k = int_only(a[1]);
        if (int_cmp(k, nn) > 0 || k->s) return K->c0;
        long kv = int_arg(k);
        E *r = K->c1;
        for (long i = 0; i < kv; i++) r = int_mul(r, int_sub(nn, mk_int(i)));
        return r;
    }
    /* comb(n, k) is the builtin function binomial(n, k) (auto-simplified) */
    case C_COMB: case C_NCR: nargs(n, 2, 2); return s_fn2(F_BINOM, a[0], a[1]);
    /* iegcd(a, b) = [s, t, g] with s*a + t*b = g = gcd(a, b) */
    case C_IEGCD: {
        nargs(n, 2, 2);
        E *s, *t;
        E *g = int_egcd(int_only(a[0]), int_only(a[1]), &s, &t);
        E *v[3] = {s, t, g};
        return s_list(3, v);
    }
    /* invmod(a, m): s from s*a + t*m = 1, reduced mod m (floor mod, so the
     * result has the sign of m) */
    case C_INVMOD: {
        nargs(n, 2, 2);
        E *s, *t;
        E *g = int_egcd(int_only(a[0]), int_only(a[1]), &s, &t);
        if (!int_is_one(g)) cas_throw(CAS_ERR_DOMAIN, "not invertible");
        return int_mod(s, a[1]);
    }
    /* powmod(a, e, m) = a^e mod m by square-and-multiply on bignums */
    case C_POWMOD: {
        nargs(n, 3, 3);
        if (int_sgn(int_only(a[1])) < 0) cas_throw(CAS_ERR_DOMAIN, "powmod: negative exponent");
        if (int_sgn(int_only(a[2])) <= 0) cas_throw(CAS_ERR_DOMAIN, "powmod: bad modulus");
        return int_powmod(int_only(a[0]), a[1], a[2]);
    }
    case C_CHINREM: {
        /* ichinrem([a1,m1],[a2,m2]): x with x = a1 mod m1, x = a2 mod m2, as
         * [x, l] with l = lcm(m1, m2) and 0 <= x < l (for positive moduli).
         * Chinese remainder theorem for possibly non-coprime moduli: with
         * s*m1 + t*m2 = g = gcd(m1, m2), a solution exists iff g | a2 - a1,
         * and then x = a1 + ((a2 - a1)/g) * s * m1, since s*m1 = g - t*m2
         * makes x = a2 mod m2 while clearly x = a1 mod m1. */
        nargs(n, 2, 2);
        if (a[0]->t != E_LIST || a[1]->t != E_LIST || a[0]->n != 2 || a[1]->n != 2)
            cas_throw(CAS_ERR_DOMAIN, "ichinrem([a,m],[b,n])");
        E *a1 = int_only(EK(a[0])[0]), *m1 = int_only(EK(a[0])[1]), *a2 = int_only(EK(a[1])[0]),
          *m2 = int_only(EK(a[1])[1]);
        E *s, *t;
        E *g = int_egcd(m1, m2, &s, &t);
        E *diffv = int_sub(a2, a1);
        E *q, *r;
        int_tdivmod(diffv, g, &q, &r);
        if (!int_is_zero(r)) cas_throw(CAS_ERR_DOMAIN, "no solution");
        E *l = int_divexact(int_mul(m1, m2), g);
        E *x0 = int_mod(int_add(a1, int_mul(int_mul(q, s), m1)), l);
        E *v[2] = {x0, l};
        return s_list(2, v);
    }
    /* ------------------------------------------------ linear algebra */
    case C_DET: nargs(n, 1, 1); return mat_det(a[0]);
    /* inv: matrix inverse for lists, 1/x for anything else */
    case C_INV: nargs(n, 1, 1);
        if (a[0]->t == E_LIST) return mat_inv(a[0]);
        return s_inv(a[0]);
    case C_TRANSPOSE: case C_TRN: nargs(n, 1, 1); return mat_transpose(a[0]);
    case C_RREF: nargs(n, 1, 1); return mat_rref(a[0], NULL);
    /* rank = number of pivots found by the Gauss-Jordan elimination */
    case C_RANK: {
        nargs(n, 1, 2);
        if (n == 2) return casx_dispatch("rank", n, a); /* rank(A, tol): cas/ext/applied */
        E *x = CASX_SLOTS && has_float(a[0]) ? numla(a[0], 2) : NULL;
        if (x) return x;
        long r;
        mat_rref(a[0], &r);
        return mk_int(r);
    }
    case C_TRACE: {
        nargs(n, 1, 1);
        uint32_t r, c;
        if (!is_matrix(a[0], &r, &c) || r != c) cas_throw(CAS_ERR_DOMAIN, "trace: need a square matrix");
        E **t = salloc(sizeof(E *) * r);
        for (uint32_t i = 0; i < r; i++) t[i] = EK(EK(a[0])[i])[i];
        return s_add(r, t);
    }
    case C_IDENTITY: case C_IDN: nargs(n, 1, 1); return mat_identity(int_arg(a[0]));
    /* zeros(n [, m]): n x m (default n x n) zero matrix */
    case C_ZEROS: {
        nargs(n, 1, 2);
        E *v[2] = {a[0], n > 1 ? a[1] : a[0]};
        return matrix_cmd(2, v);
    }
    case C_MATRIX: return matrix_cmd(n, a);
    /* charpoly(A [, x]) = det(x I - A) (linalg.c), in x by default */
    case C_CHARPOLY: nargs(n, 1, 2); return mat_charpoly(a[0], n > 1 ? a[1] : mk_sym(S_X));
    /* eigenvalues: roots of the characteristic polynomial in a private
     * variable _lambda, solved over C (exact where solve can, numeric for
     * high degree); eigenvects pairs each eigenvalue with a basis of
     * ker(A - lambda I).  A repeated root is reported once per distinct
     * solution returned by solve. */
    case C_EIGENVALS: {
        nargs(n, 1, 1);
        /* floats: exact charpoly of exact(A), roots evaluated afterwards (a
         * float charpoly degenerates into rounding noise and undef) */
        E *r = CASX_SLOTS ? (has_float(a[0]) ? numla(a[0], 0) : casx_eslot(CASX_FN_EIGENVALS, 1, a)) : NULL; /* cas/ext/alg/eigblk.c */
        if (r) return r;
        int fl = has_float(a[0]);
        E *lam = mk_sym_name("_lambda");
        E *cp = mat_charpoly(fl ? exact_expr(a[0]) : a[0], lam);
        E *sol = solve_expr(cp, lam, 1);
        if (roots_missing(cp, lam, sol)) return inert(C_EIGENVALS, 1, a);
        E **v = salloc(sizeof(E *) * (sol->n + 1));
        for (uint32_t i = 0; i < sol->n; i++) {
            v[i] = IFN(EK(sol)[i], F_EQ) ? K1(EK(sol)[i]) : EK(sol)[i];
            if (fl) v[i] = evalf(v[i]);
        }
        return s_list(sol->n, v);
    }
    case C_EIGENVECTS: {
        nargs(n, 1, 1);
        uint32_t r, c;
        if (!is_matrix(a[0], &r, &c) || r != c) cas_throw(CAS_ERR_DOMAIN, "need a square matrix");
        E *nv = CASX_SLOTS && has_float(a[0]) ? numla(a[0], 1) : NULL; /* cas/ext/applied */
        if (nv) return nv;
        E *lam = mk_sym_name("_lambda");
        E *cpe = mat_charpoly(a[0], lam), *sol = solve_expr(cpe, lam, 1);
        if (CASX_SLOTS && has_float(sol) && (nv = numla(a[0], 1))) return nv; /* numeric roots */
        if (roots_missing(cpe, lam, sol)) return inert(C_EIGENVECTS, 1, a);
        E **out = salloc(sizeof(E *) * (sol->n + 1));
        for (uint32_t i = 0; i < sol->n; i++) {
            E *ev = IFN(EK(sol)[i], F_EQ) ? K1(EK(sol)[i]) : EK(sol)[i];
            E *shifted = s_sub(a[0], s_mul2(ev, mat_identity(r)));
            E *v[2] = {ev, mat_kernel(shifted)};
            /* no kernel found for an eigenvalue (radicals not recognised as roots): numeric */
            if (CASX_SLOTS && !v[1]->n && (nv = numla(a[0], 1))) return nv;
            out[i] = s_list(2, v);
        }
        return s_list(sol->n, out);
    }
    case C_KER: nargs(n, 1, 1); return mat_kernel(a[0]);
    /* dot(u, v): sum u_i v_i (no complex conjugation) */
    case C_DOT: nargs(n, 2, 2);
        if (!is_vector(a[0]) || !is_vector(a[1])) cas_throw(CAS_ERR_DOMAIN, "dot: need vectors");
        return mat_mul_lists(a[0], a[1]);
    case C_CROSS: nargs(n, 2, 2); return cross(a[0], a[1]);
    case C_NORM: {
        nargs(n, 1, 2);
        uint32_t nr, nc;
        if (n == 2 && is_int_val(a[1], 2) && is_matrix(a[0], &nr, &nc)) return casx_dispatch("norm", n, a); /* spectral */
        E *r = n == 2 && a[0]->t == E_LIST ? norm_p(a[0], a[1]) : NULL;
        if (n == 2 && !r && !is_int_val(a[1], 2)) cas_throw(CAS_ERR_DOMAIN, "norm: p must be 1, 2, inf or a number");
        return r ? r : norm_cmd(a[0]);
    }
    /* adjoint (adjugate) = det(A) * A^-1; only defined this way for
     * invertible A (mat_inv throws for a singular matrix) */
    case C_ADJOINT: {
        nargs(n, 1, 1);
        return rat_normal(s_mul2(mat_det(a[0]), mat_inv(a[0])));
    }
    /* size / length: number of elements of a list (rows of a matrix) or
     * characters of a string; 1 for anything else */
    case C_SIZE: case C_LENGTH: {
        nargs(n, 1, 1);
        if (a[0]->t == E_LIST) return mk_int(a[0]->n);
        if (a[0]->t == E_STR) return mk_int(a[0]->n);
        return K->c1;
    }
    /* hessian(f, vars) = jacobian(grad f): H_ij = d^2 f / dv_i dv_j */
    case C_HESSIAN: {
        nargs(n, 2, 2);
        E *g = jacobian(a[0], a[1]); /* 1 x n: the gradient */
        return jacobian(EK(g)[0], a[1]);
    }
    case C_GRAD: {
        if (n == 3) return casx_dispatch("grad", n, a); /* coordinates: cas/ext/sci/veccalc.c */
        nargs(n, 2, 2);
        E *v = as_list(a[1]);
        E **g = salloc(sizeof(E *) * v->n);
        for (uint32_t i = 0; i < v->n; i++) g[i] = diff(a[0], EK(v)[i]);
        return s_list(v->n, g);
    }
    case C_JACOBIAN: nargs(n, 2, 2); return jacobian(a[0], a[1]);
    /* divergence(F, vars) = sum dF_i/dv_i; curl(F, [x, y, z]) =
     * (dFz/dy - dFy/dz, dFx/dz - dFz/dx, dFy/dx - dFx/dy) */
    case C_DIVERGENCE: {
        if (n == 3) return casx_dispatch("divergence", n, a);
        nargs(n, 2, 2);
        E *f = as_list(a[0]), *v = as_list(a[1]);
        if (f->n != v->n) cas_throw(CAS_ERR_DOMAIN, "divergence: size mismatch");
        E **t = salloc(sizeof(E *) * f->n);
        for (uint32_t i = 0; i < f->n; i++) t[i] = diff(EK(f)[i], EK(v)[i]);
        return s_add(f->n, t);
    }
    case C_CURL: {
        nargs(n, 2, 3);
        E *f = as_list(a[0]), *v = as_list(a[1]);
        if (n == 3 || f->n == 2) return casx_dispatch("curl", n, a);
        if (f->n != 3 || v->n != 3) cas_throw(CAS_ERR_DOMAIN, "curl: need 3 components");
        E **F = EK(f), **V = EK(v);
        E *r[3] = {s_sub(diff(F[2], V[1]), diff(F[1], V[2])), s_sub(diff(F[0], V[2]), diff(F[2], V[0])),
                   s_sub(diff(F[1], V[0]), diff(F[0], V[1]))};
        return s_list(3, r);
    }
    case C_INTERP: nargs(n, 2, 3); return lagrange(a[0], a[1], n > 2 ? a[2] : mk_sym(S_X));
    /* ------------------------------------------------ assumptions
     * Assumptions are A_* flag bits on the symbol record (cas_int.h), read by
     * the sign analysis in simp.c.  A sign assumption replaces the previous
     * sign facts but keeps A_INT. */
    case C_ASSUME: {
        /* assume(x>0), assume(x>=0), assume(x<0), assume(x,integer), assume(x,positive) */
        nargs(n, 1, 2);
        E *c = a[0];
        if (n == 2 && c->t == E_SYM && a[1]->t == E_SYM) {
            const char *w = sym_name(a[1]->s);
            uint8_t *f = &K->syms[c->s].assume;
            if (!strcmp(w, "integer")) *f |= A_INT;
            else if (!strcmp(w, "positive")) *f = (uint8_t)((*f & A_INT) | A_POS | A_NONNEG);
            else if (!strcmp(w, "negative")) *f = (uint8_t)((*f & A_INT) | A_NEG);
            else if (!strcmp(w, "nonnegative")) *f = (uint8_t)((*f & A_INT) | A_NONNEG);
            else if (!strcmp(w, "real")) { /* variables are real by default */ }
            else cas_throw(CAS_ERR_DOMAIN, "assume: integer, positive, negative, nonnegative or real");
            return c;
        }
        if (ISYM(c, S_TRUE)) return c; /* already known */
        if (IFN(c, F_AND) && assume_rel(c)) return c;
        if (c->t != E_FN || c->n != 2) cas_throw(CAS_ERR_DOMAIN, "assume: expected a relation like x>0");
        E *l = K0(c), *r = K1(c);
        int id = c->s;
        /* normalise 0 < x to x > 0 by swapping sides and mirroring the op */
        if (l->t != E_SYM && r->t == E_SYM) {
            E *t = l; l = r; r = t;
            id = id == F_GT ? F_LT : id == F_LT ? F_GT : id == F_GE ? F_LE : id == F_LE ? F_GE : id;
        }
        if (l->t != E_SYM || !is_zero(r)) {
            /* relations between expressions, intervals, |u| < r, conjunctions (assume.c) */
            if (assume_rel(c)) return c;
            cas_throw(CAS_ERR_DOMAIN, "assume: expected a real relation such as x>0, x>y or -1<x and x<1");
        }
        uint8_t *f = &K->syms[l->s].assume;
        *f &= A_INT;
        if (id == F_GT) *f |= A_POS | A_NONNEG;
        else if (id == F_GE) *f |= A_NONNEG;
        else if (id == F_LT) *f |= A_NEG;
        else cas_throw(CAS_ERR_DOMAIN, "assume: use >, >= or <");
        return c;
    }
    case C_UNASSUME: {
        for (uint32_t i = 0; i < n; i++)
            if (a[i]->t == E_SYM) K->syms[a[i]->s].assume = 0, facts_forget(a[i]->s);
        return mk_sym(S_TRUE);
    }
    case C_ABOUT: {
        nargs(n, 1, 1);
        if (a[0]->t != E_SYM) return a[0];
        uint8_t f = K->syms[a[0]->s].assume;
        E *v[2];
        uint32_t k = 0;
        if (f & A_POS) v[k++] = s_fn2(F_GT, a[0], K->c0);
        else if (f & A_NEG) v[k++] = s_fn2(F_LT, a[0], K->c0);
        else if (f & A_NONNEG) v[k++] = s_fn2(F_GE, a[0], K->c0);
        if (f & A_INT) v[k++] = mk_sym_name("integer");
        E *fa = facts_about(a[0]->s);
        if (fa) {
            E **w = salloc(sizeof(E *) * (k + fa->n + 1));
            for (uint32_t i = 0; i < k; i++) w[i] = v[i];
            for (uint32_t i = 0; i < fa->n; i++) w[k + i] = EK(fa)[i];
            return s_list(k + fa->n, w);
        }
        if (!k) return mk_sym_name("real");
        return k == 1 ? v[0] : s_list(k, v);
    }
    case C_IMPLICITDIFF: {
        /* implicitdiff(F(x,y)=0, y, x) = -F_x/F_y */
        nargs(n, 3, 3);
        E *F = IFN(a[0], F_EQ) ? s_sub(K0(a[0]), K1(a[0])) : a[0];
        if (a[1]->t != E_SYM || a[2]->t != E_SYM) cas_throw(CAS_ERR_DOMAIN, "implicitdiff(eq, y, x)");
        return simplify_full(s_neg(s_div(diff(F, a[2]), diff(F, a[1]))));
    }
    case C_ODESOLVE_N: {
        /* odesolve(f(x,y), [x,y], [x0,y0], x1): numeric y(x1) by adaptive RK45 */
        nargs(n, 4, 4);
        E *f = a[0], *vs = a[1], *iv = a[2];
        if (vs->t != E_LIST || vs->n != 2 || iv->t != E_LIST || iv->n != 2 || EK(vs)[0]->t != E_SYM ||
            EK(vs)[1]->t != E_SYM)
            cas_throw(CAS_ERR_DOMAIN, "odesolve(f(x,y), [x,y], [x0,y0], x1)");
        if (IFN(f, F_EQ)) f = K1(f);
        return odesolve_num(f, EK(vs)[0], EK(vs)[1], EK(iv)[0], EK(iv)[1], a[3]);
    }
    /* propfrac(17/5) = 3 + 2/5 (truncating division, built raw so the sum is
     * not folded back into 17/5); propfrac(p/q, x) = quo + rem/q in x */
    case C_PROPFRAC: {
        nargs(n, 1, 2);
        E *v = a[0];
        if (v->t == E_RAT) {
            E *q, *r;
            int_tdivmod(K0(v), K1(v), &q, &r);
            E *pr = mk_rat(r, K1(v));
            E *e2 = enode(E_ADD, 2);
            K0(e2) = q;
            K1(e2) = pr;
            e2->f = F_SIMP;
            return is_zero(q) ? pr : e2;
        }
        E *x = n > 1 ? a[1] : default_var(v);
        E *num, *den;
        numden(v, &num, &den);
        E *r;
        E *q = poly_quo_rem(num, den, x, &r);
        return s_add2(q, s_div(r, den));
    }
    /* gbasis(polys, vars [, order]): reduced Groebner basis (Buchberger with
     * Gebauer-Moeller criteria, gbasis.c); order plex/lex (0, default) or
     * tdeg/grevlex (1) */
    case C_GBASIS: {
        nargs(n, 2, 3);
        E *ps = as_list(a[0]), *vs = as_list(a[1]);
        for (uint32_t i = 0; i < vs->n; i++)
            if (EK(vs)[i]->t != E_SYM) cas_throw(CAS_ERR_DOMAIN, "gbasis: expected variables");
        int order = 0;
        if (n > 2) {
            E *o = a[2];
            const char *nm = o->t == E_SYM ? sym_name(o->s) : o->t == E_STR ? ESTR(o) : "";
            if (!strcmp(nm, "tdeg") || !strcmp(nm, "grevlex")) order = 1;
            else if (strcmp(nm, "plex") && strcmp(nm, "lex")) cas_throw(CAS_ERR_DOMAIN, "gbasis: order plex or tdeg");
        }
        E *g = groebner(ps, vs, order);
        if (!g) cas_throw(CAS_ERR_DOMAIN, "gbasis: polynomials with rational coefficients expected");
        return g;
    }
    /* ------------------------------------------------ lists / misc */
    case C_SEQ: case C_MAKELIST: return seq_cmd(n, a);
    /* lhs/rhs: sides of an equation or any binary relation/function; first
     * and last element of a list; otherwise the argument itself */
    case C_LHS: case C_LEFT:
        nargs(n, 1, 1);
        if (a[0]->t == E_FN && a[0]->n == 2) return K0(a[0]);
        if (a[0]->t == E_LIST && a[0]->n) return EK(a[0])[0];
        return a[0];
    case C_RHS: case C_RIGHT:
        nargs(n, 1, 1);
        if (a[0]->t == E_FN && a[0]->n == 2) return K1(a[0]);
        if (a[0]->t == E_LIST && a[0]->n) return EK(a[0])[a[0]->n - 1];
        return a[0];
    /* has(e, x): true if x occurs in e (the id says "freeof", the result is
     * its negation) */
    case C_FREEOF: nargs(n, 2, 2); return bool_e(!freeof(a[0], a[1]));
    case C_TYPE: nargs(n, 1, 1); return type_name(a[0]);
    case C_MEAN: case C_MEDIAN: case C_VARIANCE: case C_STDDEV: nargs(n, 1, 1); return list_stat(id, a[0]);
    case C_SORT: nargs(n, 1, 1); return sort_list(a[0]);
    case C_REVERSE: {
        nargs(n, 1, 1);
        if (a[0]->t != E_LIST) return a[0];
        E **v = salloc(sizeof(E *) * (a[0]->n + 1));
        for (uint32_t i = 0; i < a[0]->n; i++) v[i] = EK(a[0])[a[0]->n - 1 - i];
        return s_list(a[0]->n, v);
    }
    /* concat(a, b, ...): flatten one level -- list arguments contribute their
     * elements, other arguments themselves */
    case C_CONCAT: {
        uint32_t tot = 0;
        for (uint32_t i = 0; i < n; i++) tot += a[i]->t == E_LIST ? a[i]->n : 1;
        E **v = salloc(sizeof(E *) * (tot + 1));
        uint32_t k = 0;
        for (uint32_t i = 0; i < n; i++) {
            if (a[i]->t == E_LIST)
                for (uint32_t j = 0; j < a[i]->n; j++) v[k++] = EK(a[i])[j];
            else
                v[k++] = a[i];
        }
        return s_list(k, v);
    }
    /* map(list, f) / apply(f, list): [f(l1), f(l2), ...] where f is a
     * function name (user or builtin); each call goes through eval */
    case C_MAP: case C_APPLY: {
        nargs(n, 2, 2);
        E *l = a[0], *f = a[1];
        if (id == C_APPLY) {
            l = a[1];
            f = a[0];
        }
        if (l->t != E_LIST) cas_throw(CAS_ERR_DOMAIN, "map: expected a list");
        if (f->t != E_SYM) cas_throw(CAS_ERR_DOMAIN, "map: expected a function name");
        E **v = salloc(sizeof(E *) * (l->n + 1));
        for (uint32_t i = 0; i < l->n; i++) v[i] = eval(mk_fn(f->s, 1, &EK(l)[i]));
        return s_list(l->n, v);
    }
    case C_SIGNUM: nargs(n, 1, 1); return s_fn1(F_SIGN, a[0]);
    /* tangent(f, x = a): the tangent line f(a) + f'(a) (x - a), expanded */
    case C_TANGENT: {
        nargs(n, 2, 3);
        var_point(n, a, 1, a[0], &x, &pt, &nx);
        E *f0 = subst(a[0], x, pt), *d = subst(diff(a[0], x), x, pt);
        return expand(s_add2(f0, s_mul2(d, s_sub(x, pt))));
    }
    /* restart: forget all user variables and functions (and x's value).
     * Assumptions are kept.  Heap blocks are freed via hfree_expr (deferred
     * to the end of the call). */
    case C_RESTART: {
        for (uint32_t i = S__COUNT; i < K->nsyms; i++) {
            symrec *r = &K->syms[i];
            if (r->val) hfree_expr(r->val);
            if (r->fdef) hfree_expr(r->fdef);
            r->val = r->fdef = NULL;
        }
        if (K->syms[S_X].val) hfree_expr(K->syms[S_X].val);
        K->syms[S_X].val = NULL;
        return mk_sym(S_TRUE);
    }
    case C_LAPLACE: {
        nargs(n, 1, 3);
        E *v[3] = {a[0], n > 1 ? a[1] : mk_sym_name("t"), n > 2 ? a[2] : mk_sym_name("s")};
        return casx_dispatch("laplace", 3, v); /* cas/ext/adv/lapl.c */
    }
    case C_ILAPLACE: {
        nargs(n, 1, 3);
        E *v[3] = {a[0], n > 1 ? a[1] : mk_sym_name("s"), n > 2 ? a[2] : mk_sym_name("t")};
        return casx_dispatch("ilaplace", 3, v);
    }
    /* Not reached from the evaluator (eval_fn sends dsolve/desolve to
     * dsolve_raw with unevaluated arguments); kept for direct callers. */
    case C_DESOLVE: case C_ODESOLVE: {
        nargs(n, 1, 3);
        E *y = n > 1 ? a[1] : mk_sym_name("y");
        E *xx = n > 2 ? a[2] : mk_sym(S_X);
        return dsolve_expr(a[0], y, xx);
    }
    case C_DIGITS: case C_PADE: case C_FOURIERA: case C_RANDPOLY: case C_EGCD: {
        extern E *xcmd_builtin(int id, uint32_t n, E **a);
        return xcmd_builtin(id, n, a);
    }
    default: break;
    }
    (void)lnexpand_rec;
    cas_throw(CAS_ERR_UNSUPPORTED, "command not implemented");
}

static E *lnexpand_rec(E *e) { return e; }
