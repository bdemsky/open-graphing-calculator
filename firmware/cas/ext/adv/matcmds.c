/*
 * matcmds.c -- matrix functions and structure commands of the advanced CAS
 * extension (cas/src/casx.h): expm, sinm, cosm, sqrtm (through the core's
 * f(A) by Hermite interpolation on the spectrum, matfn.c mat_fun), the
 * Jordan form, the minimal polynomial, rsolve (linear recurrences with
 * constant coefficients), wronskian and wronskianmat.
 *
 * Jordan form: for an eigenvalue z of multiplicity m, r_k = rank (A - zI)^k;
 * the number of blocks of size >= k is r_{k-1} - r_k.  Repeated eigenvalues
 * with radicals or i are refused (rank decisions need exact zero tests).
 * Minimal polynomial: the first linear dependence among vec(I), vec(A),
 * vec(A^2), ... (Krylov sequence of A in the space of matrices).
 */
#include "casx_mod.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

#define AT(A, i, j) EK(EK(A)[i])[j]

static int sqmat(XP, E *A, uint32_t *n) {
    uint32_t c;
    return is_matrix(A, n, &c) && *n == c && *n > 0;
}

/* eigenvalues of A with algebraic multiplicities (matfn.c roots_mult) */
static int eig_mult(XP, E *A, uint32_t n, E **lam, long *mult) {
    E *L = mk_sym_name("_lambda");
    return roots_mult(mat_charpoly(A, L), L, n, lam, mult);
}

static int has_rad(XP, E *e) {
    stack_check();
    if (ISYM(e, S_I)) return 1;
    if (e->t == E_POW && K1(e)->t != E_INT) return 1;
    if (e->t == E_SYM || is_num(e)) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_rad(X, EK(e)[i])) return 1;
    return 0;
}

static long rank_of(XP, E *M) {
    long r;
    mat_rref(M, &r);
    return r;
}

static E *jordan(XP, E *A) {
    uint32_t n;
    if (!sqmat(X, A, &n) || has_float(A)) return NULL;
    E **lam = salloc(sizeof(E *) * n);
    long *mult = salloc(sizeof(long) * n), *r = salloc(sizeof(long) * (n + 2));
    int k = eig_mult(X, A, n, lam, mult);
    if (k < 0) return NULL;
    E ***J = salloc(sizeof(E **) * n);
    for (uint32_t i = 0; i < n; i++) {
        J[i] = salloc(sizeof(E *) * n);
        for (uint32_t j = 0; j < n; j++) J[i][j] = K->c0;
    }
    uint32_t pos = 0;
    E *I = mat_identity(n);
    for (int g = 0; g < k; g++) {
        long m = mult[g];
        if (m > 1 && has_rad(X, lam[g])) return NULL;
        E *B = s_sub(A, s_mul2(lam[g], I)), *P = I;
        r[0] = n;
        for (long j = 1; j <= m + 1; j++) {
            P = mat_mul_lists(P, B);
            r[j] = m == 1 ? (long)n - 1 : rank_of(X, P);
        }
        long used = 0;
        for (long s = m; s >= 1; s--) {
            long cnt = (r[s - 1] - r[s]) - (r[s] - r[s + 1]);
            for (long b = 0; b < cnt; b++) {
                for (long t = 0; t < s; t++) {
                    J[pos + t][pos + t] = lam[g];
                    if (t + 1 < s) J[pos + t][pos + t + 1] = K->c1;
                }
                pos += (uint32_t)s;
                used += s;
            }
        }
        if (used != m) return NULL;
    }
    E **rows = salloc(sizeof(E *) * n);
    for (uint32_t i = 0; i < n; i++) rows[i] = s_list(n, J[i]);
    return s_list(n, rows);
}

/* vec(A^k) as column k of a matrix; the first k with a dependency gives
 * the minimal polynomial */
static E *minpoly(XP, E *A, E *x) {
    uint32_t n;
    if (!sqmat(X, A, &n)) return NULL;
    uint32_t nn = n * n;
    E **pw = salloc(sizeof(E *) * (n + 1));
    pw[0] = mat_identity(n);
    for (uint32_t k = 1; k <= n; k++) {
        pw[k] = mat_mul_lists(pw[k - 1], A);
        E **rows = salloc(sizeof(E *) * nn);
        for (uint32_t i = 0; i < nn; i++) {
            E **v = salloc(sizeof(E *) * (k + 1));
            for (uint32_t j = 0; j <= k; j++) v[j] = AT(pw[j], i / n, i % n);
            rows[i] = s_list(k + 1, v);
        }
        E *ker = mat_kernel(s_list(nn, rows));
        if (ker->n) {
            E *v = EK(ker)[0], **t = salloc(sizeof(E *) * (k + 1));
            for (uint32_t j = 0; j <= k; j++) t[j] = s_mul2(s_div(EK(v)[j], EK(v)[k]), s_powi(x, j));
            return factor_expr(rat_normal(s_add(k + 1, t)));
        }
    }
    return NULL;
}

static E *cvar(XP, char c, uint32_t i) {
    char nm[4] = {c, (char)('1' + i), 0};
    return mk_sym_name(nm);
}

/* solve the linear equations eqs[0..ne) for cs[0..nc) and substitute into
 * the list sol; throws if they cannot be satisfied */
static int free_all(XP, E *e, uint32_t k, E **a, E **b) {
    for (uint32_t i = 0; i < k; i++)
        if (!freeof(e, a[i]) || (b && !freeof(e, b[i]))) return 0;
    return 1;
}

/*
 * dsolve([u'=..., v'=..., u(0)=a, ...], [u, v], t) for first-order linear
 * systems with constant coefficients B X' + C X + g(t) = 0 (ode.c hands the
 * raw arguments over): X' = A X + G with A = -B^-1 C, so
 *   X = Phi(t) (c + int Phi(-t) G(t) dt),  Phi(t) = expm(A t)
 * (variation of constants).  Conditions u(t0) = a fix the constants c1..ck.
 */
/* -------------------------------------------------------- recurrences --- */
/* f(n + s) nodes of e: shifts into sh[], nodes into nd[] */
static int shifts(XP, E *e, int f, E *n, E **nd, long *sh, int *cnt) {
    stack_check();
    if (e->t == E_FN && e->s == f) {
        long s;
        if (e->n != 1 || !num_is_small_int(rat_normal(s_sub(K0(e), n)), &s)) return 0;
        for (int i = 0; i < *cnt; i++)
            if (expr_eq(nd[i], e)) return 1;
        if (*cnt == 16) return 0;
        nd[*cnt] = e, sh[(*cnt)++] = s;
        return 1;
    }
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN) return 1;
    for (uint32_t i = 0; i < e->n; i++)
        if (!shifts(X, EK(e)[i], f, n, nd, sh, cnt)) return 0;
    return 1;
}

static E *find_base(XP, E *e, E *n) {
    stack_check();
    if (e->t == E_POW && expr_eq(K1(e), n) && freeof(K0(e), n)) return K0(e);
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN) return NULL;
    for (uint32_t i = 0; i < e->n; i++) {
        E *b = find_base(X, EK(e)[i], n);
        if (b) return b;
    }
    return NULL;
}

/*
 * rsolve(X, eq, f(n) [, [f(n0)=a, ...]]): linear recurrence with constant
 * coefficients sum a_j f(n+j) = h(n), h a polynomial times b^n.  The
 * homogeneous part is sum c n^m r^n over the roots r of sum a_j r^j (m below
 * the multiplicity); a particular solution n^s b^n P(n) (s = multiplicity
 * of b as a root, deg P = deg h) by undetermined coefficients.
 */
static E *rsolve(XP, uint32_t na, E **a) {
    E *fn = a[1], *e = a[0], *nd[16], *rs[16], *L = mk_sym_name("_lambda");
    long sh[16], smin = 1L << 30, smax = -smin;
    int cnt = 0;
    if (fn->t != E_FN || fn->n != 1 || K0(fn)->t != E_SYM) return NULL;
    E *n = K0(fn);
    if (IFN(e, F_EQ)) e = s_sub(K0(e), K1(e));
    if (!shifts(X, e, fn->s, n, nd, sh, &cnt) || !cnt) return NULL;
    for (int i = 0; i < cnt; i++) smin = sh[i] < smin ? sh[i] : smin, smax = sh[i] > smax ? sh[i] : smax;
    long d = smax - smin;
    if (d < 1 || d > 8) return NULL;
    for (long j = 0; j <= d; j++) rs[j] = cvar(X, '_', (uint32_t)j);
    for (int i = 0; i < cnt; i++) e = subst(e, nd[i], rs[sh[i] - smin]);
    e = expand(subst(e, n, s_sub(n, mk_int(smin))));
    E **co = salloc(sizeof(E *) * (d + 1)), **pt = salloc(sizeof(E *) * (d + 1)), *zs[16];
    for (long j = 0; j <= d; j++) {
        co[j] = diff(e, rs[j]), zs[j] = K->c0;
        if (!free_all(X, co[j], (uint32_t)d + 1, rs, NULL) || !freeof(co[j], n)) return NULL;
        pt[j] = s_mul2(co[j], s_powi(L, j));
    }
    E *h = rat_normal(s_neg(subst_many(e, (uint32_t)d + 1, rs, zs)));
    E **lam = salloc(sizeof(E *) * d), **cs = salloc(sizeof(E *) * d), **t = salloc(sizeof(E *) * d);
    long *mult = salloc(sizeof(long) * d);
    int k = roots_mult(expand(s_add((uint32_t)d + 1, pt)), L, (uint32_t)d, lam, mult);
    if (k < 0) return NULL;
    uint32_t m = 0;
    for (int g = 0; g < k; g++)
        for (long j = 0; j < mult[g]; j++, m++) {
            cs[m] = cvar(X, 'c', m);
            t[m] = s_mul(3, (E *[]){cs[m], s_powi(n, j), s_pow(lam[g], n)});
        }
    E *sol = s_add(m, t);
    if (!is_zero(h)) {
        E *b = find_base(X, h, n);
        if (!b) b = K->c1;
        E *q = rat_normal(s_div(h, s_pow(b, n)));
        long p, s = 0;
        if (!poly_degree_in(q, n, &p) || p > 6) return NULL;
        for (int g = 0; g < k; g++)
            if (is_zero(rat_normal(s_sub(lam[g], b)))) s = mult[g];
        E **u = salloc(sizeof(E *) * (p + 1)), **ut = salloc(sizeof(E *) * (p + 1)), **ct = salloc(sizeof(E *) * (d + 2));
        for (long i = 0; i <= p; i++) u[i] = cvar(X, 'u', (uint32_t)i), ut[i] = s_mul2(u[i], s_powi(n, i + s));
        E *P = s_add((uint32_t)p + 1, ut);
        for (long j = 0; j <= d; j++) ct[j] = s_mul(3, (E *[]){co[j], s_powi(b, j), subst(P, n, s_add2(n, mk_int(j)))});
        ct[d + 1] = s_neg(q);
        E *res = expand(s_add((uint32_t)d + 2, ct)), **eq = salloc(sizeof(E *) * (p + s + d + 2));
        long dr;
        if (!poly_degree_in(res, n, &dr)) return NULL;
        for (long i = 0; i <= dr; i++) eq[i] = poly_coeff(res, n, i);
        E *yp = lin_fit(s_mul2(P, s_pow(b, n)), (uint32_t)dr + 1, eq, (uint32_t)p + 1, u);
        if (!free_all(X, yp, (uint32_t)p + 1, u, NULL)) return NULL;
        sol = s_add2(sol, yp);
    }
    if (na > 2) {
        E *cl = a[2]->t == E_LIST ? a[2] : s_list(1, &a[2]), **fe = salloc(sizeof(E *) * (cl->n + 1));
        for (uint32_t i = 0; i < cl->n; i++) {
            E *c = EK(cl)[i];
            if (!IFN(c, F_EQ) || K0(c)->t != E_FN || K0(c)->s != fn->s || K0(c)->n != 1)
                cas_throw(CAS_ERR_DOMAIN, "rsolve: conditions look like f(0)=a");
            fe[i] = s_sub(subst(sol, n, K0(K0(c))), K1(c));
        }
        sol = lin_fit(sol, cl->n, fe, m, cs);
    }
    return rat_normal(sol);
}

/* in the order of the M_* ops (the handler gets the index) */
enum { M_EXPM, M_SINM, M_COSM, M_SQRTM, M_JORDAN, M_MINPOLY, M_RSOLVE, M_WRONSKIAN, M_WMATRIX };
static const casx_cmd_t mtab[] = {
    {"expm", 1, 1}, {"sinm", 1, 1}, {"cosm", 1, 1}, {"sqrtm", 1, 1}, {"jordan", 1, 1}, {"minpoly", 1, 2}, {"rsolve", 2, 3},
    {"wronskian", 2, 2}, {"wronskianmat", 2, 2},
};

/* wronskian([f1, .., fn], x) = det [f_j^(i)] (i = 0..n-1), simplified */
static E *wronskian(XP, E *L, E *x, int mat) {
    if (L->t != E_LIST || !L->n || L->n > 10 || x->t != E_SYM) cas_throw(CAS_ERR_DOMAIN, "wronskian: expected [f1, ..., fn], x");
    uint32_t n = L->n;
    E **rows = salloc(sizeof(E *) * n), **cur = salloc(sizeof(E *) * n);
    for (uint32_t j = 0; j < n; j++) cur[j] = EK(L)[j];
    for (uint32_t i = 0; i < n; i++) {
        rows[i] = s_list(n, cur);
        E **nx = salloc(sizeof(E *) * n);
        for (uint32_t j = 0; j < n; j++) nx[j] = diff(cur[j], x);
        cur = nx;
    }
    E *M = s_list(n, rows);
    if (mat) return M;
    E *d = eval_cmd(C_DET, 1, &M);
    return eval_cmd(C_SIMPLIFY, 1, &d);
}

static E *mcmd(XP, int op, uint32_t n, E **a) {
    E *Y = mk_sym_name("_X");
    switch (op) {
    case M_EXPM: return mat_fun(a[0], s_exp(Y), Y);
    case M_SINM: return mat_fun(a[0], s_fn1(F_SIN, Y), Y);
    case M_COSM: return mat_fun(a[0], s_fn1(F_COS, Y), Y);
    case M_SQRTM: return mat_rpow(a[0], K->chalf);
    case M_JORDAN: return jordan(X, a[0]);
    case M_RSOLVE: return rsolve(X, n, a);
    case M_WRONSKIAN: return wronskian(X, a[0], a[1], 0);
    case M_WMATRIX: return wronskian(X, a[0], a[1], 1);
    case M_MINPOLY: return minpoly(X, a[0], n > 1 ? a[1] : mk_sym(S_X));
    }
    return NULL;
}

int casx_mat_register(XP) { return X->reg("casx", mtab, (int)(sizeof mtab / sizeof mtab[0]), mcmd); }
