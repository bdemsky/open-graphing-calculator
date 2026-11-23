/*
 * matfn.c -- matrix functions f(A) (mat_fun: M^(p/q) in simp.c, and expm,
 * sinm, cosm, sqrtm of the advanced extension, cas/ext/adv/matcmds.c),
 * first-order linear ODE systems (odesys), structure commands (diag, minor,
 * submatrix, blockdiag, getElem / L[i]) and the name dispatch of the
 * commands added with them (gap_cmd, called from xcmds.c ext_cmd for names
 * it does not know; extension commands last, casx.c).
 *
 * f(A) for a square exact matrix: with the eigenvalues z_0..z_{n-1} of A
 * (each repeated by its algebraic multiplicity, equal ones adjacent),
 *     f(A) = sum_j f[z_0..z_j] (A - z_0 I) ... (A - z_{j-1} I),
 * the Newton form of the Hermite interpolant of f on the spectrum
 * (Higham, "Functions of Matrices", 1.2.2); a confluent divided difference
 * over k+1 equal points is f^(k)(z)/k!.  Since the interpolant agrees with
 * f to the order of each eigenvalue's multiplicity (>= its index), the sum
 * equals f(A) exactly.  Eigenvalues come from the factored characteristic
 * polynomial; if any is not found exactly the command stays unevaluated.
 */
#include "casx.h"
#include <string.h>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

E *exp_to_trig(E *e);
E *eval_hiding(E *e, int nvars, int *vars);

static long gint(E *e) {
    long v;
    if (!num_is_small_int(e, &v)) cas_throw(CAS_ERR_DOMAIN, "expected an integer");
    return v;
}

static int sqmat(E *A, uint32_t *n) {
    uint32_t c;
    return is_matrix(A, n, &c) && *n == c && *n > 0;
}

#define AT(A, i, j) EK(EK(A)[i])[j]

/* roots of the polynomial p of degree n in L with multiplicities; returns
 * the number of distinct ones or -1 if they are not all found exactly */
int roots_mult(E *p, E *L, uint32_t n, E **lam, long *mult) {
    E *content, *fl = factor_list_in(p, L, &content);
    int k = 0;
    long tot = 0;
    for (uint32_t i = 0; i < fl->n; i++) {
        E *f = K0(EK(fl)[i]);
        long d, m = gint(K1(EK(fl)[i]));
        if (!poly_degree_in(f, L, &d)) return -1;
        if (d == 0) continue;
        E *sol = solve_expr(f, L, 1);
        if (sol->t != E_LIST || (long)sol->n != d) return -1;
        for (uint32_t j = 0; j < sol->n; j++) {
            E *v = IFN(EK(sol)[j], F_EQ) ? K1(EK(sol)[j]) : EK(sol)[j];
            if (has_float(v) || !freeof(v, L)) return -1;
            lam[k] = v;
            mult[k++] = m;
            tot += m;
        }
    }
    return tot == (long)n ? k : -1;
}

/* eigenvalues of A with algebraic multiplicities (roots_mult) */
static int eig_mult(E *A, uint32_t n, E **lam, long *mult) {
    E *L = mk_sym_name("_lambda");
    return roots_mult(mat_charpoly(A, L), L, n, lam, mult);
}

/* rational normal form; complex exponentials -> cos, sin and i out of the
 * denominator (num/den = num den'/(den den'), den' = den with i -> -i) */
static E *tidy(E *e) {
    e = rat_normal(e);
    if (freeof_sym(e, S_I)) return e;
    E *num, *den, *I = mk_sym(S_I);
    numden(rat_normal(expand(exp_to_trig(e))), &num, &den);
    if (!freeof(den, I)) {
        E *cj = subst(den, I, s_neg(I)), *d2 = expand(s_mul2(den, cj));
        if (freeof(d2, I) && !is_zero(d2)) num = s_mul2(num, cj), den = d2;
    }
    return rat_normal(s_div(expand(num), den));
}

static E *mat_tidy(E *M) {
    E **r = salloc(sizeof(E *) * M->n);
    for (uint32_t i = 0; i < M->n; i++) {
        E *row = EK(M)[i], **v = salloc(sizeof(E *) * row->n);
        for (uint32_t j = 0; j < row->n; j++) v[j] = tidy(EK(row)[j]);
        r[i] = s_list(row->n, v);
    }
    return s_list(M->n, r);
}

/* f(A) for f = fx(X); NULL if the eigenvalues are not exact */
E *mat_fun(E *A, E *fx, E *X) {
    uint32_t n;
    if (!sqmat(A, &n) || has_float(A)) return NULL;
    E **lam = salloc(sizeof(E *) * n), **z = salloc(sizeof(E *) * n), **c = salloc(sizeof(E *) * n);
    long *mult = salloc(sizeof(long) * n);
    int *grp = salloc(sizeof(int) * n);
    int k = eig_mult(A, n, lam, mult);
    if (k < 0) return NULL;
    uint32_t m = 0;
    for (int g = 0; g < k; g++)
        for (long j = 0; j < mult[g]; j++) z[m] = lam[g], grp[m++] = g;
    for (uint32_t i = 0; i < n; i++) c[i] = tidy(subst(fx, X, z[i]));
    E *fd = fx, *fact = K->c1;
    for (uint32_t j = 1; j < n; j++) {
        fd = diff(fd, X);
        fact = s_mul2(fact, mk_int(j));
        for (uint32_t i = n - 1; i >= j; i--)
            c[i] = tidy(grp[i] == grp[i - j] ? s_div(subst(fd, X, z[i]), fact)
                                             : s_div(s_sub(c[i], c[i - 1]), s_sub(z[i], z[i - j])));
    }
    E *I = mat_identity(n), *P = I, *R = s_mul2(c[0], I);
    for (uint32_t j = 1; j < n; j++) {
        P = mat_mul_lists(P, s_sub(A, s_mul2(z[j - 1], I)));
        R = s_add2(R, s_mul2(c[j], P));
    }
    return mat_tidy(R);
}

/* matrix power A^e for a non-integer exponent (simp.c s_pow); NULL: keep */
E *mat_rpow(E *A, E *e) {
    E *X = mk_sym_name("_X");
    tryf tf;
    try_enter(&tf);
    if (setjmp(tf.jb)) {
        try_leave(&tf);
        if (K->err == CAS_ERR_INTERRUPTED) rethrow();
        return NULL;
    }
    E *r = mat_fun(A, s_pow(X, e), X);
    try_leave(&tf);
    return r;
}

static uint32_t idx(E *e, uint32_t n) {
    long i = gint(e);
    if (i < 0) i += (long)n + 1;
    if (i < 1 || i > (long)n) cas_throw(CAS_ERR_DOMAIN, "index out of range");
    return (uint32_t)(i - 1);
}

/* rows r0..r1, columns c0..c1 (0-based, inclusive), skipping row sr / column sc */
static E *subm(E *M, uint32_t r0, uint32_t r1, uint32_t c0, uint32_t c1, long sr, long sc) {
    E **rows = salloc(sizeof(E *) * (r1 - r0 + 1));
    uint32_t nr = 0;
    for (uint32_t i = r0; i <= r1; i++) {
        if ((long)i == sr) continue;
        E **v = salloc(sizeof(E *) * (c1 - c0 + 1));
        uint32_t nc = 0;
        for (uint32_t j = c0; j <= c1; j++)
            if ((long)j != sc) v[nc++] = AT(M, i, j);
        rows[nr++] = s_list(nc, v);
    }
    return s_list(nr, rows);
}

/* ------------------------------------------------- linear systems --- */
static E *cvar(char c, uint32_t i) {
    char nm[4] = {c, (char)('1' + i), 0};
    return mk_sym_name(nm);
}

/* solve the linear equations eqs[0..ne) for cs[0..nc) and substitute into
 * the list sol; throws if they cannot be satisfied */
E *lin_fit(E *sol, uint32_t ne, E **eqs, uint32_t nc, E **cs) {
    if (!ne) return sol;
    E *r = linsolve(s_list(ne, eqs), s_list(nc, cs));
    if (!r || !r->n) cas_throw(CAS_ERR_DOMAIN, "conditions cannot be satisfied");
    E **xs = salloc(sizeof(E *) * r->n), **vs = salloc(sizeof(E *) * r->n);
    for (uint32_t i = 0; i < r->n; i++) xs[i] = K0(EK(r)[i]), vs[i] = K1(EK(r)[i]);
    return subst_many(sol, r->n, xs, vs);
}

/* u', diff(u(t),t) -> ds[i]; u(t) -> u (raw tree) */
static E *sys_map(E *e, uint32_t k, E **nm, E **ds) {
    stack_check();
    for (uint32_t i = 0; i < k; i++) {
        if (IFN(e, C_DIFF) && e->n && (ISYM(K0(e), nm[i]->s) || (K0(e)->t == E_FN && K0(e)->s == nm[i]->s)))
            return ds[i];
        if (e->t == E_FN && e->n == 1 && e->s == nm[i]->s) return nm[i];
    }
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN && e->t != E_LIST) return e;
    E **kk = salloc(sizeof(E *) * e->n);
    for (uint32_t i = 0; i < e->n; i++) kk[i] = sys_map(EK(e)[i], k, nm, ds);
    return rebuild(e, kk);
}

static int has_diff(E *e) {
    stack_check();
    if (IFN(e, C_DIFF)) return 1;
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN && e->t != E_LIST) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_diff(EK(e)[i])) return 1;
    return 0;
}

static int free_all(E *e, uint32_t k, E **a, E **b) {
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
E *odesys(E *eqs, E *names, E *x) {
    uint32_t k = names->n, ne = 0, nc = 0;
    if (eqs->t != E_LIST || k < 1 || k > 8 || x->t != E_SYM) cas_throw(CAS_ERR_DOMAIN, "dsolve: expected [eqs], [names], var");
    E **nm = EK(names), **ds = salloc(sizeof(E *) * k), **cs = salloc(sizeof(E *) * k);
    int *hide = salloc(sizeof(int) * (k + 1));
    for (uint32_t i = 0; i < k; i++) {
        if (nm[i]->t != E_SYM) cas_throw(CAS_ERR_DOMAIN, "dsolve: expected names");
        ds[i] = cvar('_', i), cs[i] = cvar('c', i), hide[i] = nm[i]->s;
    }
    hide[k] = x->s;
    E **B = salloc(sizeof(E *) * k), **C = salloc(sizeof(E *) * k), **g = salloc(sizeof(E *) * k);
    E **ce = salloc(sizeof(E *) * eqs->n), **cl = salloc(sizeof(E *) * eqs->n);
    for (uint32_t q = 0; q < eqs->n; q++) {
        E *r = EK(eqs)[q];
        if (!IFN(r, F_EQ)) cas_throw(CAS_ERR_DOMAIN, "dsolve: expected equations");
        if (!has_diff(r)) { /* condition u(t0) = a */
            uint32_t i = 0;
            while (i < k && !(K0(r)->t == E_FN && K0(r)->s == nm[i]->s && K0(r)->n == 1)) i++;
            if (i == k) cas_throw(CAS_ERR_DOMAIN, "dsolve: conditions look like u(a)=b");
            cl[nc] = mk_int(i);
            ce[nc++] = eval_hiding(mk_fn2(F_EQ, K0(K0(r)), K1(r)), (int)k + 1, hide);
            continue;
        }
        if (ne == k) cas_throw(CAS_ERR_DOMAIN, "dsolve: more equations than unknowns");
        E *e = sys_map(mk_add2(K0(r), mk_mul2(mk_int(-1), K1(r))), k, nm, ds);
        if (has_diff(e)) cas_throw(CAS_ERR_UNSUPPORTED, "dsolve: only first-order systems");
        e = expand(eval_hiding(e, (int)k + 1, hide));
        E **br = salloc(sizeof(E *) * k), **cr = salloc(sizeof(E *) * k);
        for (uint32_t j = 0; j < k; j++) {
            br[j] = diff(e, ds[j]), cr[j] = diff(e, nm[j]);
            if (!free_all(br[j], k, nm, ds) || !free_all(cr[j], k, nm, ds) || !freeof(br[j], x) || !freeof(cr[j], x))
                cas_throw(CAS_ERR_UNSUPPORTED, "dsolve: only linear systems with constant coefficients");
        }
        E *zs[16];
        for (uint32_t j = 0; j < k; j++) zs[j] = zs[k + j] = K->c0;
        E *xs[16];
        memcpy(xs, ds, sizeof(E *) * k);
        memcpy(xs + k, nm, sizeof(E *) * k);
        g[ne] = rat_normal(subst_many(e, 2 * k, xs, zs));
        B[ne] = s_list(k, br), C[ne++] = s_list(k, cr);
    }
    if (ne != k) cas_throw(CAS_ERR_DOMAIN, "dsolve: need one equation per unknown");
    E *Bi = mat_inv(s_list(k, B)), *A = s_neg(mat_mul_lists(Bi, s_list(k, C)));
    E *G = s_neg(mat_mul_lists(Bi, s_list(k, g))), *X = mk_sym_name("_X");
    E *Phi = mat_fun(s_mul2(A, x), s_exp(X), X);
    if (!Phi) cas_throw(CAS_ERR_UNSUPPORTED, "dsolve: eigenvalues not found exactly");
    E *v = s_list(k, cs);
    int inh = 0;
    for (uint32_t i = 0; i < k; i++) inh |= !is_zero(EK(G)[i]);
    if (inh) {
        E *w = mat_mul_lists(mat_fun(s_mul2(A, s_neg(x)), s_exp(X), X), G), **iv = salloc(sizeof(E *) * k);
        for (uint32_t i = 0; i < k; i++) {
            iv[i] = integrate(rat_normal(EK(w)[i]), x);
            if (!iv[i]) cas_throw(CAS_ERR_UNSUPPORTED, "dsolve: cannot integrate the forcing term");
        }
        v = s_add2(v, s_list(k, iv));
    }
    E *sol = mat_mul_lists(Phi, v);
    E **fe = salloc(sizeof(E *) * (nc + 1));
    for (uint32_t q = 0; q < nc; q++) {
        long i = gint(cl[q]);
        fe[q] = s_sub(subst(EK(sol)[i], x, K0(ce[q])), K1(ce[q]));
    }
    sol = lin_fit(sol, nc, fe, k, cs);
    E **out = salloc(sizeof(E *) * k);
    for (uint32_t i = 0; i < k; i++) out[i] = s_fn2(F_EQ, nm[i], simplify_full(tidy(EK(sol)[i])));
    return s_list(k, out);
}

enum { G_DIAG, G_MINOR, G_SUBMAT, G_GETELEM, G_BLOCKDIAG };

static const struct {
    const char *name;
    uint8_t op, lo, hi;
} gtab[] = {
    {"diag", G_DIAG, 1, 2},       {"minor", G_MINOR, 3, 3},   {"submatrix", G_SUBMAT, 5, 5},
    {"getElem", G_GETELEM, 2, 3}, {"blockdiag", G_BLOCKDIAG, 1, 32},
};

static E *gcmd(int op, uint32_t n, E **a) {
    uint32_t r, c;
    switch (op) {
    case G_DIAG: {
        long k = n > 1 ? gint(a[1]) : 0;
        if (is_matrix(a[0], &r, &c)) {
            E **v = salloc(sizeof(E *) * (r + c));
            uint32_t m = 0;
            for (long i = 0; i < (long)r; i++)
                if (i + k >= 0 && i + k < (long)c) v[m++] = AT(a[0], i, i + k);
            return s_list(m, v);
        }
        if (a[0]->t != E_LIST || n > 1) return NULL;
        r = a[0]->n;
        E **rows = salloc(sizeof(E *) * r);
        for (uint32_t i = 0; i < r; i++) {
            E **v = salloc(sizeof(E *) * r);
            for (uint32_t j = 0; j < r; j++) v[j] = i == j ? EK(a[0])[i] : K->c0;
            rows[i] = s_list(r, v);
        }
        return s_list(r, rows);
    }
    case G_MINOR:
        if (!is_matrix(a[0], &r, &c) || r < 2 || c < 2) return NULL;
        return subm(a[0], 0, r - 1, 0, c - 1, idx(a[1], r), idx(a[2], c));
    case G_SUBMAT: {
        if (!is_matrix(a[0], &r, &c)) return NULL;
        uint32_t r0 = idx(a[1], r), r1 = idx(a[2], r), c0 = idx(a[3], c), c1 = idx(a[4], c);
        if (r1 < r0 || c1 < c0) cas_throw(CAS_ERR_DOMAIN, "submatrix: empty range");
        return subm(a[0], r0, r1, c0, c1, -1, -1);
    }
    case G_BLOCKDIAG: {
        uint32_t N = 0, C = 0, *br = salloc(sizeof(uint32_t) * n), *bc = salloc(sizeof(uint32_t) * n);
        for (uint32_t k = 0; k < n; k++) {
            if (a[k]->t == E_LIST && !is_matrix(a[k], &br[k], &bc[k])) return NULL;
            if (a[k]->t != E_LIST) br[k] = bc[k] = 1;
            N += br[k], C += bc[k];
        }
        if ((uint64_t)N * C > 10000) cas_throw(CAS_ERR_DOMAIN, "blockdiag: too large");
        E **rows = salloc(sizeof(E *) * N);
        uint32_t i0 = 0, j0 = 0;
        for (uint32_t k = 0; k < n; k++) {
            for (uint32_t i = 0; i < br[k]; i++) {
                E **v = salloc(sizeof(E *) * C);
                for (uint32_t j = 0; j < C; j++)
                    v[j] = j < j0 || j >= j0 + bc[k] ? K->c0 : a[k]->t == E_LIST ? AT(a[k], i, j - j0) : a[k];
                rows[i0 + i] = s_list(C, v);
            }
            i0 += br[k], j0 += bc[k];
        }
        return s_list(N, rows);
    }
    case G_GETELEM: {
        if (a[0]->t != E_LIST) return NULL;
        E *e = EK(a[0])[idx(a[1], a[0]->n)];
        if (n == 3) {
            if (e->t != E_LIST) cas_throw(CAS_ERR_DOMAIN, "index out of range");
            e = EK(e)[idx(a[2], e->n)];
        }
        return e;
    }
    }
    return NULL;
}


/* xcmds.c: commands of this round, by name; NULL = not ours or unevaluated */
E *gap_cmd(const char *nm, uint32_t n, E **a) {
    for (size_t i = 0; i < sizeof gtab / sizeof gtab[0]; i++)
        if (!strcmp(nm, gtab[i].name)) {
            if (n < gtab[i].lo || n > gtab[i].hi) cas_throw(CAS_ERR_DOMAIN, "wrong number of arguments");
            return gcmd(gtab[i].op, n, a);
        }
    E *nv[2];
    if (n == 1 && !strcmp(nm, "laplacian")) { /* ∇²f (parse.c): the variables from cmds.c */
        nv[0] = a[0], nv[1] = nabla_vars(a[0]);
        a = nv, n = 2;
    }
    return casx_dispatch(nm, n, a); /* extension commands (casx.c) */
}
