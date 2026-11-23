/*
 * lp.c -- linear programming and constrained extrema of the casx module (applied.h).
 *
 *   linprog(c, A, b [, Aeq, beq])   minimise c.x subject to A x <= b, Aeq x = beq, x >= 0 (vectors
 *                                   and matrices; [] for no rows): [value, [x_1, ..., x_n]]
 *   linprog(f, constraints, vars)   minimise the linear f subject to linear relations (<=, >=, =;
 *                                   < and > are taken as <= and >=); a variable is >= 0 only if a
 *                                   constraint `v >= 0` says so, otherwise free:
 *                                   [value, [v_1 = .., ...]]
 *   minimize(f, constraints, vars), maximize(...)   the same for a linear problem; otherwise, with
 *                                   equality constraints only (or none), Lagrange multipliers:
 *                                   the real solutions of grad f = sum l_i grad g_i, g_i = 0
 *                                   (solve), the least / greatest value of f among them.
 *
 * The simplex method runs in exact rational arithmetic (floats in the input are made exact
 * first and the answer is given as floats): two phases (an artificial variable per row; phase 1
 * minimises their sum, artificials left in the basis at zero are pivoted out or their redundant
 * rows dropped), Bland's rule for the entering (lowest index with negative reduced cost) and the
 * leaving variable (least ratio, ties to the lowest basic index), so it cannot cycle.  Free
 * variables are split into two non-negative ones.  No point: [value, []] with the infimum
 * (supremum for maximize) of the objective over the feasible set, -inf (inf) when it is
 * unbounded and inf (-inf) when the set is empty.
 * At most 60 rows and 120 columns after splitting.
 */
#include "applied.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

typedef struct {
    int m, nx;    /* rows, structural columns */
    E **A, **b;   /* m x nx coefficients, m right-hand sides */
    int8_t *rel;  /* -1 <=, 0 =, 1 >= */
    E **c;        /* nx objective coefficients */
} lp;

#define T_(i, j) T[(i) * (N + 1) + (j)]

static int sgn(XP, E *e) { return num_sgn(e); }

/* 1 optimal (x, *val), 0 infeasible, -1 unbounded */
static int simplex(XP, const lp *P, E **x, E **val) {
    int m = P->m, nx = P->nx, ns = 0;
    for (int i = 0; i < m; i++) ns += P->rel[i] != 0;
    int as = nx + ns, N = as + m;
    if (m > 60 || N > 300) cas_throw(CAS_ERR_DOMAIN, "linprog: problem too large");
    E **T = salloc(sizeof(E *) * (size_t)(m + 1) * (size_t)(N + 1));
    int *bas = salloc(sizeof(int) * (size_t)(m + 1));
    uint8_t *dead = salloc((size_t)m + 1);
    for (int i = 0; i <= m; i++)
        for (int j = 0; j <= N; j++) T_(i, j) = K->c0;
    for (int i = 0, s = nx; i < m; i++) {
        int neg = sgn(X, P->b[i]) < 0, r = neg ? -P->rel[i] : P->rel[i];
        for (int j = 0; j < nx; j++) T_(i, j) = neg ? s_neg(P->A[i * nx + j]) : P->A[i * nx + j];
        T_(i, N) = neg ? s_neg(P->b[i]) : P->b[i];
        if (r) T_(i, s++) = r < 0 ? K->c1 : K->cm1;
        T_(i, as + i) = K->c1;
        bas[i] = as + i, dead[i] = 0;
    }
    for (int phase = 1; phase <= 2; phase++) {
        int lim = phase == 1 ? N : as; /* columns that may enter */
        /* objective row: reduced costs of the phase's costs */
        for (int j = 0; j <= N; j++) {
            E *cj = phase == 1 ? (j >= as && j < N ? K->c1 : K->c0) : (j < nx ? P->c[j] : K->c0);
            for (int i = 0; i < m; i++) {
                if (dead[i]) continue;
                int bj = bas[i];
                E *cb = phase == 1 ? (bj >= as ? K->c1 : K->c0) : (bj < nx ? P->c[bj] : K->c0);
                if (!num_is_zero(cb)) cj = s_sub(cj, s_mul2(cb, T_(i, j)));
            }
            T_(m, j) = cj;
        }
        for (long it = 0;; it++) {
            poll_intr();
            if (it > 20000) cas_throw(CAS_ERR_DOMAIN, "linprog: too many iterations");
            int e = -1, l = -1;
            for (int j = 0; j < lim && e < 0; j++)
                if (sgn(X, T_(m, j)) < 0) e = j;
            if (e < 0) break;
            E *best = NULL;
            for (int i = 0; i < m; i++) {
                if (dead[i] || sgn(X, T_(i, e)) <= 0) continue;
                E *r = s_div(T_(i, N), T_(i, e));
                int c = best ? num_cmp(r, best) : -1;
                if (c < 0 || (c == 0 && bas[i] < bas[l])) best = r, l = i;
            }
            if (l < 0) return -1;
            E *pv = T_(l, e);
            for (int j = 0; j <= N; j++) T_(l, j) = s_div(T_(l, j), pv);
            for (int i = 0; i <= m; i++) {
                if (i == l || num_is_zero(T_(i, e))) continue;
                E *f = T_(i, e);
                for (int j = 0; j <= N; j++)
                    if (!num_is_zero(T_(l, j))) T_(i, j) = s_sub(T_(i, j), s_mul2(f, T_(l, j)));
            }
            bas[l] = e;
        }
        if (phase == 1) {
            if (sgn(X, T_(m, N)) != 0) return 0; /* sum of artificials > 0 */
            for (int i = 0; i < m; i++) {
                if (bas[i] < as) continue;
                int e = -1;
                for (int j = 0; j < as && e < 0; j++)
                    if (!num_is_zero(T_(i, j))) e = j;
                if (e < 0) {
                    dead[i] = 1;
                    continue;
                }
                E *pv = T_(i, e);
                for (int j = 0; j <= N; j++) T_(i, j) = s_div(T_(i, j), pv);
                for (int k = 0; k <= m; k++) {
                    if (k == i || num_is_zero(T_(k, e))) continue;
                    E *f = T_(k, e);
                    for (int j = 0; j <= N; j++) T_(k, j) = s_sub(T_(k, j), s_mul2(f, T_(i, j)));
                }
                bas[i] = e;
            }
        }
    }
    for (int j = 0; j < nx; j++) x[j] = K->c0;
    for (int i = 0; i < m; i++)
        if (!dead[i] && bas[i] < nx) x[bas[i]] = T_(i, N);
    *val = s_neg(T_(m, N));
    return 1;
}

static E *num_exact(XP, E *e) {
    if (!has_float(e)) return e;
    return eval_cmd(C_EXACT, 1, &e);
}

static E *must_num(XP, E *e) {
    if (e->t != E_INT && e->t != E_RAT) cas_throw(CAS_ERR_DOMAIN, "linprog: numeric coefficients expected");
    return e;
}

static E *aslist(XP, E *e) { return e->t == E_LIST ? e : s_list(1, &e); }

/* the linear form e in vars: coefficients into co[0..nv), the constant returned; NULL if e is
 * not linear with numeric coefficients */
static E *linear(XP, E *e, E *vars, E **co) {
    e = expand(e);
    E *rest = e;
    for (uint32_t j = 0; j < vars->n; j++) {
        long d;
        E *v = EK(vars)[j];
        if (!poly_degree_in(e, v, &d) || d > 1) return NULL;
        co[j] = poly_coeff(e, v, 1);
        if (co[j]->t != E_INT && co[j]->t != E_RAT) return NULL;
        rest = s_sub(rest, s_mul2(co[j], v));
    }
    rest = expand(rest);
    return rest->t == E_INT || rest->t == E_RAT ? rest : NULL;
}

static int rel_of(E *c) {
    if (c->t != E_FN || c->n != 2) return 2;
    switch (c->s) {
    case F_LE: case F_LT: return -1;
    case F_GE: case F_GT: return 1;
    case F_EQ: return 0;
    }
    return 2;
}

/* symbolic LP: NULL when not linear (f or a constraint) */
static E *lp_sym(XP, E *f, E *cons, E *vars, int maxi) {
    uint32_t nv = vars->n, nc = cons->n;
    for (uint32_t j = 0; j < nv; j++)
        if (EK(vars)[j]->t != E_SYM) cas_throw(CAS_ERR_DOMAIN, "expected variables");
    int fl = has_float(f) || has_float(cons);
    f = num_exact(X, f), cons = num_exact(X, cons);
    E **co = salloc(sizeof(E *) * nv);
    E *f0 = linear(X, f, vars, co);
    if (!f0) return NULL;
    /* nonneg[j]: a constraint v_j >= 0 (or 0 <= v_j) */
    uint8_t *nn = salloc(nv + 1), *skip = salloc(nc + 1);
    E **rows = salloc(sizeof(E *) * nc * nv), **rhs = salloc(sizeof(E *) * nc);
    int8_t *rl = salloc(nc + 1);
    for (uint32_t j = 0; j < nv; j++) nn[j] = 0;
    for (uint32_t i = 0; i < nc; i++) {
        E *c = EK(cons)[i];
        int r = rel_of(c);
        if (r == 2) cas_throw(CAS_ERR_DOMAIN, "expected relations as constraints");
        E *cst = linear(X, s_sub(K0(c), K1(c)), vars, rows + i * nv);
        if (!cst) return NULL;
        rhs[i] = s_neg(cst), rl[i] = (int8_t)r, skip[i] = 0;
        /* a bound v >= 0 */
        int one = -1, cnt = 0;
        for (uint32_t j = 0; j < nv; j++)
            if (!num_is_zero(rows[i * nv + j])) one = (int)j, cnt++;
        if (cnt == 1 && num_is_zero(rhs[i]) && r != 0 && sgn(X, rows[i * nv + one]) == r) nn[one] = 1, skip[i] = 1;
    }
    /* columns: v_j, or v_j+ and v_j- */
    int nx = 0, *col = salloc(sizeof(int) * (nv + 1));
    for (uint32_t j = 0; j < nv; j++) col[j] = nx, nx += nn[j] ? 1 : 2;
    int m = 0;
    for (uint32_t i = 0; i < nc; i++) m += !skip[i];
    lp P = {m, nx, salloc(sizeof(E *) * (size_t)(m * nx + 1)), salloc(sizeof(E *) * (size_t)(m + 1)), salloc((size_t)m + 1),
            salloc(sizeof(E *) * (size_t)nx)};
    for (uint32_t j = 0; j < nv; j++) {
        E *cj = maxi ? s_neg(co[j]) : co[j];
        P.c[col[j]] = cj;
        if (!nn[j]) P.c[col[j] + 1] = s_neg(cj);
    }
    for (uint32_t i = 0, r = 0; i < nc; i++) {
        if (skip[i]) continue;
        for (uint32_t j = 0; j < nv; j++) {
            P.A[r * nx + col[j]] = rows[i * nv + j];
            if (!nn[j]) P.A[r * nx + col[j] + 1] = s_neg(rows[i * nv + j]);
        }
        P.b[r] = rhs[i], P.rel[r] = rl[i], r++;
    }
    E **x = salloc(sizeof(E *) * (size_t)(nx + 1)), *val;
    int st = simplex(X, &P, x, &val);
    if (st <= 0) { /* unbounded: -inf (inf); infeasible: the infimum of the empty set, inf (-inf) */
        E *r[2] = {(st < 0) != !maxi ? K->inf_ : s_neg(K->inf_), s_list(0, NULL)};
        return s_list(2, r);
    }
    val = s_add2(maxi ? s_neg(val) : val, f0);
    E **pt = salloc(sizeof(E *) * nv);
    for (uint32_t j = 0; j < nv; j++) {
        E *v = nn[j] ? x[col[j]] : s_sub(x[col[j]], x[col[j] + 1]);
        pt[j] = s_fn2(F_EQ, EK(vars)[j], fl ? eval_cmd(C_EVALF, 1, &v) : v);
    }
    E *r[2] = {fl ? eval_cmd(C_EVALF, 1, &val) : val, s_list(nv, pt)};
    return s_list(2, r);
}

/* linprog(c, A, b [, Aeq, beq]) */
static E *lp_mat(XP, uint32_t na, E **a) {
    int fl = 0;
    for (uint32_t i = 0; i < na; i++) fl |= has_float(a[i]), a[i] = num_exact(X, a[i]);
    E *c = a[0];
    uint32_t n = c->n, m1 = 0, m2 = 0, r, cc;
    if (c->t != E_LIST || !n) cas_throw(CAS_ERR_DOMAIN, "linprog: c must be a vector");
    for (int k = 0; k < 2; k++) {
        E *A = a[1 + 2 * k], *b = 2 + 2 * k < (int)na ? a[2 + 2 * k] : NULL;
        if (1 + 2 * k >= (int)na) break;
        if (!b) cas_throw(CAS_ERR_DOMAIN, "linprog: Aeq needs beq");
        if (A->t == E_LIST && A->n == 0) r = 0;
        else if (!is_matrix(A, &r, &cc) || cc != n) cas_throw(CAS_ERR_DOMAIN, "linprog: A must have one column per entry of c");
        if (b->t != E_LIST || b->n != r) cas_throw(CAS_ERR_DOMAIN, "linprog: b must have one entry per row of A");
        if (k) m2 = r;
        else m1 = r;
    }
    int m = (int)(m1 + m2);
    lp P = {m, (int)n, salloc(sizeof(E *) * (size_t)(m * (int)n + 1)), salloc(sizeof(E *) * (size_t)(m + 1)),
            salloc((size_t)m + 1), salloc(sizeof(E *) * n)};
    for (uint32_t j = 0; j < n; j++) P.c[j] = must_num(X, EK(c)[j]);
    for (int i = 0; i < m; i++) {
        int eq = i >= (int)m1;
        E *A = a[eq ? 3 : 1], *b = a[eq ? 4 : 2];
        uint32_t ri = eq ? (uint32_t)i - m1 : (uint32_t)i;
        for (uint32_t j = 0; j < n; j++) P.A[i * (int)n + (int)j] = must_num(X, EK(EK(A)[ri])[j]);
        P.b[i] = must_num(X, EK(b)[ri]), P.rel[i] = eq ? 0 : -1;
    }
    E **x = salloc(sizeof(E *) * (n + 1)), *val;
    int st = simplex(X, &P, x, &val);
    if (st <= 0) {
        E *rr[2] = {st < 0 ? s_neg(K->inf_) : K->inf_, s_list(0, NULL)};
        return s_list(2, rr);
    }
    E *rr[2] = {val, s_list(n, x)};
    E *res = s_list(2, rr);
    return fl ? eval_cmd(C_EVALF, 1, &res) : res;
}

E *ap_linprog(XP, uint32_t n, E **a) {
    if (a[0]->t == E_LIST) return lp_mat(X, n, a);
    if (n != 3) cas_throw(CAS_ERR_DOMAIN, "linprog(f, constraints, vars) or linprog(c, A, b [, Aeq, beq])");
    E *r = lp_sym(X, a[0], aslist(X, a[1]), aslist(X, a[2]), 0);
    if (!r) cas_throw(CAS_ERR_DOMAIN, "linprog: the objective and the constraints must be linear");
    return r;
}

static double dval(XP, E *e, int *ok) {
    double re, im;
    *ok = is_const_expr(e) && X->eval_complex(e, &re, &im) && AFABS(im) <= 1e-12 * (1 + AFABS(re));
    return *ok ? re : 0;
}

/* Lagrange multipliers for equality constraints (or none) */
static E *lagr(XP, E *f, E *cons, E *vars, int maxi) {
    uint32_t nv = vars->n, ng = cons->n, nu = nv + ng;
    E **g = salloc(sizeof(E *) * (ng + 1)), **lam = salloc(sizeof(E *) * (ng + 1));
    E **eqs = salloc(sizeof(E *) * nu), **un = salloc(sizeof(E *) * nu);
    char nm[6] = {'_', 'l', 'm', '0', 0, 0};
    for (uint32_t i = 0; i < ng; i++) {
        E *c = EK(cons)[i];
        if (rel_of(c) != 0) cas_throw(CAS_ERR_UNSUPPORTED, "minimize: nonlinear problems need equality constraints");
        g[i] = s_sub(K0(c), K1(c));
        nm[3] = (char)('0' + i % 10), nm[4] = i >= 10 ? (char)('0' + i / 10) : 0;
        lam[i] = mk_sym_name(nm);
    }
    for (uint32_t j = 0; j < nv; j++) {
        E *v = EK(vars)[j], *d = diff(f, v);
        for (uint32_t i = 0; i < ng; i++) d = s_sub(d, s_mul2(lam[i], diff(g[i], v)));
        eqs[j] = s_fn2(F_EQ, d, K->c0), un[j] = v;
    }
    for (uint32_t i = 0; i < ng; i++) eqs[nv + i] = s_fn2(F_EQ, g[i], K->c0), un[nv + i] = lam[i];
    E *sol = X->solve_expr(s_list(nu, eqs), s_list(nu, un), 0);
    if (!sol || sol->t != E_LIST) cas_throw(CAS_ERR_UNSUPPORTED, "minimize: could not solve the conditions");
    E *bv = NULL, *bp = NULL;
    double bd = 0;
    E **xs = salloc(sizeof(E *) * nu), **vs = salloc(sizeof(E *) * nu);
    for (uint32_t s = 0; s < sol->n; s++) {
        E *so = EK(sol)[s];
        if (so->t != E_LIST) so = s_list(1, &so);
        uint32_t k = 0;
        for (uint32_t i = 0; i < so->n; i++)
            if (IFN(EK(so)[i], F_EQ)) xs[k] = K0(EK(so)[i]), vs[k] = K1(EK(so)[i]), k++;
        E **pt = salloc(sizeof(E *) * nv);
        int ok = 1;
        for (uint32_t j = 0; j < nv && ok; j++) {
            E *v = NULL;
            for (uint32_t i = 0; i < k; i++)
                if (expr_eq(xs[i], EK(vars)[j])) v = vs[i];
            if (!v) ok = 0;
            else {
                dval(X, v, &ok);
                pt[j] = s_fn2(F_EQ, EK(vars)[j], v);
            }
        }
        if (!ok) continue;
        E *fv = subst_many(f, k, xs, vs);
        fv = eval_cmd(C_SIMPLIFY, 1, &fv);
        double d = dval(X, fv, &ok);
        if (!ok) continue;
        if (!bv || (maxi ? d > bd : d < bd)) bv = fv, bd = d, bp = s_list(nv, pt);
    }
    if (!bv) cas_throw(CAS_ERR_UNSUPPORTED, "minimize: no real critical point found");
    E *r[2] = {bv, bp};
    return s_list(2, r);
}

E *ap_extremum(XP, E *f, E *cons, E *vars, int maxi) {
    cons = aslist(X, cons), vars = aslist(X, vars);
    for (uint32_t j = 0; j < vars->n; j++)
        if (EK(vars)[j]->t != E_SYM) cas_throw(CAS_ERR_DOMAIN, "expected variables");
    if (vars->n == 0 || vars->n > 10) cas_throw(CAS_ERR_DOMAIN, "1 to 10 variables");
    E *r = lp_sym(X, f, cons, vars, maxi);
    return r ? r : lagr(X, f, cons, vars, maxi);
}
