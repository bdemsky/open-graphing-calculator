/*
 * pde.c -- closed-form solutions of partial differential equations
 * (science part of the advanced CAS extension, casx.h):
 *
 *   pdesolve(eq, u(x,t) [, conditions [, terms]])
 *
 * 1. First-order linear equations a u_x + b u_t + c u = f (coefficients
 *    functions of x and t) by characteristics.  The characteristic curves
 *    dt/dx = b/a have the invariant xi(x, t) (b/a free of t: t - int b/a dx;
 *    linear in t: t mu - int q mu dx with mu = exp(-int p dx); separable
 *    g(x) h(t): int dt/h - int g dx); along them u solves the linear ODE
 *    du/dx + (c/a) u = f/a, so u = (int f/a m dx + F(xi)) / m, m = exp(int
 *    c/a dx), with F arbitrary.  A condition u(x, t0) = g(x) (or u(x0, t) =
 *    g(t)) fixes F when xi(x, t0) can be inverted for x.  The result is
 *    checked numerically: the residual of the equation (with F replaced by a
 *    test function when it is still free) and of the condition must vanish
 *    at sample points, otherwise the call stays unevaluated.
 * 2. Separation of variables on an interval a < x < b:
 *      heat  u_t = k u_xx:   u = w + sum c_n exp(-k l_n^2 t) phi_n(x)
 *      wave  u_tt = c^2 u_xx: u = w + sum (A_n cos(c l_n t) + B_n/(c l_n) sin(c l_n t)) phi_n(x)
 *    with Dirichlet (u) or Neumann (u_x) conditions at each end, constant
 *    boundary values (w is the steady linear part that carries them) and the
 *    eigenfunctions phi_n: sin(n pi s/L) (D-D), cos(n pi s/L) (N-N, n >= 0),
 *    sin((2n-1) pi s/(2L)) (D-N), cos((2n-1) pi s/(2L)) (N-D), s = x - a.
 *    Laplace's equation u_xx + u_yy = 0 on a rectangle with Dirichlet data
 *    on its four sides: the sum of four problems with one non-zero side,
 *    sin(n pi (x-a)/A) sinh(n pi (y-c)/A)/sinh(n pi B/A) and so on.
 *    Coefficients: c_n = int F phi_n dx / int phi_n^2 dx by the integrator,
 *    once for a symbolic n (assumed a positive integer) and once for each
 *    of the first modes and the modes at which a trigonometric term of F
 *    resonates; where the general formula does not hold (a pole, or a
 *    different value) the modes up to the last such one are summed
 *    explicitly.  The result is a finite sum when F is a combination of
 *    eigenfunctions, otherwise explicit terms plus an inert
 *    sum(..., n, n0, inf); with `terms` = N the first N terms instead.
 * 3. The line (no boundary condition): d'Alembert's formula for the wave
 *    equation, u = (f(x-ct) + f(x+ct))/2 + (G(x+ct) - G(x-ct))/(2c),
 *    G' = g; for the heat equation the Gauss-Weierstrass integral
 *    u = 1/sqrt(pi) int f(x + 2 sqrt(k t) z) exp(-z^2) dz over the line.
 * Anything outside these forms, or an integral the integrator cannot do,
 * leaves the call unevaluated.
 */
#include "pde.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

E *pde_simp(XP, E *e) { return eval_cmd(C_SIMPLIFY, 1, &e); }

/* --------------------------------------------------------------- parsing */
static int sname(XP, pde_t *p, int d, int swap, char *buf) {
    const char *u = sym_name(p->fid);
    int n = 0;
    while (*u && n < 40) buf[n++] = *u++;
    buf[n++] = '_';
    int a = d == D_X || d == D_XX || d == D_XT ? 0 : 1, b = d == D_XX ? 0 : d == D_XT || d == D_TT ? 1 : -1;
    if (swap) {
        int t = a;
        a = b, b = t;
    }
    for (int k = 0; k < 2; k++) {
        int w = k ? b : a;
        if (w < 0) continue;
        const char *s = sym_name(p->v[w]->s);
        while (*s && n < 60) buf[n++] = *s++;
    }
    buf[n] = 0;
    return n;
}

/* derivative code of e (D_U ... D_TT) and its argument node, or -1 */
static int dcode(XP, pde_t *p, E *e, E **args) {
    if (e->t != E_FN) return -1;
    if (e->s == p->fid && e->n == 2) {
        *args = e;
        return D_U;
    }
    if (e->s == C_DIFF && e->n == 2 && EK(e)[1]->t == E_SYM) {
        int k = expr_eq(K1(e), p->v[0]) ? 0 : expr_eq(K1(e), p->v[1]) ? 1 : -1, d = dcode(X, p, K0(e), args);
        if (k < 0 || d < 0) return -1;
        static const signed char next[D_N][2] = {{D_X, D_T}, {D_XX, D_XT}, {D_XT, D_TT}, {-1, -1}, {-1, -1}, {-1, -1}};
        return next[d][k];
    }
    if (e->n != 2) return -1;
    char buf[64];
    for (int d = D_X; d < D_N; d++)
        for (int sw = 0; sw < (d == D_XT ? 2 : 1); sw++) {
            sname(X, p, d, sw, buf);
            if (mk_sym_name(buf)->s == e->s) {
                *args = e;
                return d;
            }
        }
    return -1;
}

int pde_init(XP, pde_t *p, E *u) {
    if (u->t != E_FN || u->n != 2 || K0(u)->t != E_SYM || K1(u)->t != E_SYM || expr_eq(K0(u), K1(u))) return 0;
    p->u = u, p->fid = u->s, p->v[0] = K0(u), p->v[1] = K1(u);
    char nm[4] = {'_', 'd', 0, 0};
    for (int k = 0; k < D_N; k++) nm[2] = (char)('0' + k), p->ph[k] = mk_sym_name(nm);
    p->param = NULL;
    return 1;
}

/* the derivatives at the generic point (x, t) replaced by placeholders; NULL if u occurs otherwise */
static E *repl(XP, pde_t *p, E *e) {
    stack_check();
    E *args;
    int d = dcode(X, p, e, &args);
    if (d >= 0) return expr_eq(K0(args), p->v[0]) && expr_eq(K1(args), p->v[1]) ? p->ph[d] : NULL;
    if (e->t == E_FN && e->s == p->fid) return NULL;
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN && e->t != E_LIST) return e;
    E **k = salloc(sizeof(E *) * e->n);
    int ch = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        if (!(k[i] = repl(X, p, EK(e)[i]))) return NULL;
        ch |= k[i] != EK(e)[i];
    }
    return ch ? X->rebuild(e, k) : e;
}

/* eq linear in u and its derivatives up to order 2: co[], rhs */
int pde_parse(XP, pde_t *p, E *eq) {
    E *e = IFN(eq, F_EQ) ? s_sub(K0(eq), K1(eq)) : eq, *z[D_N];
    if (!(e = repl(X, p, e))) return 0;
    for (int k = 0; k < D_N; k++) z[k] = K->c0;
    for (int k = 0; k < D_N; k++) {
        p->co[k] = rat_normal(diff(e, p->ph[k]));
        for (int j = 0; j < D_N; j++)
            if (!freeof(p->co[k], p->ph[j])) return 0;
    }
    p->rhs = rat_normal(s_neg(subst_many(e, D_N, p->ph, z)));
    return 1;
}

static int free2(pde_t *p, XP, E *e) { return freeof(e, p->v[0]) && freeof(e, p->v[1]); }

int pde_kind(XP, pde_t *p) {
    int z[D_N];
    for (int k = 0; k < D_N; k++) z[k] = is_zero(p->co[k]);
    if (z[D_XX] && z[D_XT] && z[D_TT]) return !z[D_X] || !z[D_T] ? PK_FIRST : PK_NONE;
    if (!z[D_XT] || z[D_XX] || !z[D_U] || !z[D_X]) return PK_NONE;
    if (!z[D_T] && z[D_TT]) {
        E *k = rat_normal(s_neg(s_div(p->co[D_XX], p->co[D_T])));
        if (!is_zero(p->rhs) || !free2(p, X, k) || X->sign_of(k) < 0) return PK_NONE;
        p->param = k;
        return PK_HEAT;
    }
    if (!z[D_T] || z[D_TT]) return PK_NONE;
    E *q = rat_normal(s_neg(s_div(p->co[D_XX], p->co[D_TT])));
    if (!free2(p, X, q)) return PK_NONE;
    if (expr_eq(q, K->cm1)) {
        p->param = rat_normal(s_div(p->rhs, p->co[D_XX]));
        return PK_LAPLACE;
    }
    if (!is_zero(p->rhs) || X->sign_of(q) < 0) return PK_NONE;
    p->param = q;
    return PK_WAVE;
}

/* the conditions of the list (or single equation) L; their number, -1 if one is not understood */
int pde_conds(XP, pde_t *p, E *L, pcond_t *c) {
    if (!L) return 0;
    uint32_t n = L->t == E_LIST ? L->n : 1;
    E **q = L->t == E_LIST ? EK(L) : &L;
    if (n > PDE_MAXC) return -1;
    for (uint32_t i = 0; i < n; i++) {
        E *args;
        if (!IFN(q[i], F_EQ)) return -1;
        int d = dcode(X, p, K0(q[i]), &args);
        if (d != D_U && d != D_X && d != D_T) return -1;
        E *a0 = K0(args), *a1 = K1(args);
        if (expr_eq(a1, p->v[1]) && !expr_eq(a0, p->v[0]) && free2(p, X, a0)) c[i].var = 0, c[i].at = a0;
        else if (expr_eq(a0, p->v[0]) && !expr_eq(a1, p->v[1]) && free2(p, X, a1)) c[i].var = 1, c[i].at = a1;
        else return -1;
        if (d != D_U && d != (c[i].var ? D_T : D_X)) return -1;
        c[i].d = d, c[i].val = K1(q[i]);
        if (!freeof(c[i].val, p->v[c[i].var]) || !freeof(c[i].val, p->ph[0])) return -1;
    }
    return (int)n;
}

/* --------------------------------------------------------------- helpers */
static int has_fn(XP, E *e, int id) {
    stack_check();
    if (e->t == E_FN && e->s == id) return 1;
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN && e->t != E_LIST) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_fn(X, EK(e)[i], id)) return 1;
    return 0;
}

static int bad(XP, E *e) {
    return !e || has_fn(X, e, C_INTEGRATE) || has_fn(X, e, C_INT) || has_fn(X, e, C_SOLVE) || !freeof(e, mk_sym(S_UNDEF));
}

static E *integ(XP, E *f, E *x) {
    if (is_zero(f)) return K->c0;
    E *a[2] = {f, x};
    E *r = try_cmd(C_INTEGRATE, 2, a);
    return bad(X, r) ? NULL : r;
}

/* int_lo^hi f dx with the factors free of x taken out */
static E *dinteg(XP, E *f, E *x, E *lo, E *hi) {
    if (is_zero(f)) return K->c0;
    E *c = K->c1;
    if (f->t == E_MUL) {
        E **in = salloc(sizeof(E *) * f->n), **out = salloc(sizeof(E *) * f->n);
        uint32_t ni = 0, no = 0;
        for (uint32_t i = 0; i < f->n; i++) {
            if (freeof(EK(f)[i], x)) out[no++] = EK(f)[i];
            else in[ni++] = EK(f)[i];
        }
        c = s_mul(no, out), f = s_mul(ni, in);
    }
    E *a[4] = {f, x, lo, hi};
    E *r = try_cmd(C_INTEGRATE, 4, a);
    return bad(X, r) ? NULL : s_mul2(c, r);
}

/* the same for a generic mode: the definite integral, or F(hi) - F(lo) of an antiderivative
 * (the integrator refuses a parameter that could make it singular; fcoef checks the modes) */
static E *ginteg(XP, E *f, E *x, E *lo, E *hi) {
    E *r = dinteg(X, f, x, lo, hi);
    if (r) return r;
    E *F = integ(X, f, x);
    return F ? s_sub(subst(F, x, hi), subst(F, x, lo)) : NULL;
}

/* the unique solution of e = 0 for x, or NULL (the core's solve: no general solutions) */
static E *solve_p(XP, void **a) { return X->solve_expr((E *)a[0], (E *)a[1], 0); }
static E *solve1(XP, E *e, E *x) {
    void *a[2] = {e, x};
    E *r = X->protect(solve_p, a);
    return r && r->t == E_LIST && r->n == 1 && !bad(X, EK(r)[0]) ? EK(r)[0] : NULL;
}

static double num_at(XP, E *e, uint32_t n, E **v, E **val, int *ok) {
    double d;
    *ok = to_double(evalf_digits(subst_many(e, n, v, val), 15), &d) && d == d && d - d == 0;
    return d;
}

/* the free symbols of e (not constants such as pi), at most 8 */
static void syms(XP, E *e, E **out, uint32_t *n) {
    stack_check();
    if (e->t == E_SYM) {
        if (is_const_expr(e)) return;
        for (uint32_t i = 0; i < *n; i++)
            if (out[i] == e || expr_eq(out[i], e)) return;
        if (*n < 8) out[(*n)++] = e;
        return;
    }
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN && e->t != E_LIST) return;
    for (uint32_t i = 0; i < e->n; i++) syms(X, EK(e)[i], out, n);
}

/* |r| small against the scale m at two sample points (values for every free symbol) where both are real */
static int vanishes(XP, E *r, E *m, E **v0) {
    int good = 0;
    E *v[8], *val[8];
    uint32_t nv = 0;
    (void)v0;
    syms(X, r, v, &nv);
    syms(X, m, v, &nv);
    for (int s = 0; s < 8 && good < 2; s++) {
        for (uint32_t i = 0; i < nv; i++) val[i] = mk_frac(3 + 2 * s + 5 * (long)i, 7 + (long)i);
        int ok1, ok2;
        double dr = num_at(X, r, nv, v, val, &ok1), dm = num_at(X, m, nv, v, val, &ok2);
        if (!ok1 || !ok2) continue;
        if (__builtin_fabs(dr) > 1e-9 * (1 + __builtin_fabs(dm))) return 0;
        good++;
    }
    return good == 2;
}

static E *sq(XP, E *e) { return s_powi(e, 2); }

/* the wave speed from c^2 (both formulas are even in c, so the base of a square will do) */
static E *wave_c(XP, E *c2) {
    long k;
    if (c2->t == E_POW && num_is_small_int(K1(c2), &k) && k == 2) return K0(c2);
    return pde_simp(X, s_sqrt(c2));
}

/* ------------------------------------------------------- characteristics */
static E *xi_of(XP, E *R, E *x, E *y) {
    if (freeof(R, y)) {
        E *I = integ(X, R, x);
        return I ? s_sub(y, I) : NULL;
    }
    E *P = rat_normal(diff(R, y));
    if (freeof(P, y)) { /* y' = P y + Q */
        E *Q = rat_normal(s_sub(R, s_mul2(P, y)));
        E *I = integ(X, P, x);
        if (!I || !freeof(Q, y)) return NULL;
        E *mu = pde_simp(X, s_exp(s_neg(I))), *J = integ(X, s_mul2(Q, mu), x);
        return J ? s_sub(s_mul2(y, mu), J) : NULL;
    }
    E *f = factor_expr(R), **k = f->t == E_MUL ? EK(f) : &f, *g = K->c1, *h = K->c1;
    uint32_t m = f->t == E_MUL ? f->n : 1;
    for (uint32_t i = 0; i < m; i++) {
        if (freeof(k[i], y)) g = s_mul2(g, k[i]);
        else if (freeof(k[i], x)) h = s_mul2(h, k[i]);
        else return NULL;
    }
    E *A = integ(X, s_inv(h), y), *B = integ(X, g, x);
    return A && B ? s_sub(A, B) : NULL;
}

/* (int f/a m dx + F) / m along the path variable x, m = exp(int c/a dx) */
static E *along(XP, E *a, E *c, E *f, E *x, E *F) {
    E *I = integ(X, rat_normal(s_div(c, a)), x);
    if (!I) return NULL;
    E *m = pde_simp(X, s_exp(I)), *J = integ(X, pde_simp(X, s_mul2(s_div(f, a), m)), x);
    return J ? s_div(s_add2(J, F), m) : NULL;
}

static E *char1(XP, pde_t *p, pcond_t *cd, int nc) {
    E *a = p->co[D_X], *b = p->co[D_T], *c = p->co[D_U], *f = p->rhs, *x = p->v[0], *y = p->v[1];
    E *Fs = mk_sym_name("_F"), *s = mk_sym_name("_s"), *xi, *U;
    if (nc > 1) return NULL;
    if (is_zero(a)) xi = x, U = along(X, b, c, f, y, Fs);
    else if (is_zero(b)) xi = y, U = along(X, a, c, f, x, Fs);
    else {
        xi = xi_of(X, rat_normal(s_div(b, a)), x, y);
        if (!xi) return NULL;
        xi = pde_simp(X, xi);
        E *Y = solve1(X, s_sub(xi, s), y);
        if (!Y) return NULL;
        U = along(X, subst(a, y, Y), subst(c, y, Y), subst(f, y, Y), x, Fs);
        if (U) U = subst(U, s, xi);
    }
    if (!U) return NULL;
    U = pde_simp(X, U);
    E *B = pde_simp(X, diff(U, Fs)), *A = pde_simp(X, subst(U, Fs, K->c0)), *res;
    if (!freeof(B, Fs) || is_zero(B)) return NULL;
    if (nc == 1) {
        if (cd->d != D_U) return NULL;
        int w = cd->var; /* the condition holds on v[w] = at */
        E *o = p->v[1 - w], *arg = pde_simp(X, subst(xi, p->v[w], cd->at));
        E *O = solve1(X, s_sub(arg, s), o);
        if (!O) return NULL;
        E *Bc = subst(subst(B, p->v[w], cd->at), o, O), *Ac = subst(subst(A, p->v[w], cd->at), o, O);
        E *Phi = s_div(s_sub(subst(cd->val, o, O), Ac), Bc);
        res = pde_simp(X, s_add2(A, s_mul2(B, subst(Phi, s, xi))));
    } else {
        E *Fn = mk_fn(mk_sym_name("F")->s, 1, &xi);
        res = pde_simp(X, s_add2(A, s_mul2(B, Fn)));
    }
    /* check: the residual with F replaced by a test function, and the condition */
    E *tst = nc ? res : s_add2(A, s_mul2(B, s_add2(s_fn1(F_SIN, xi), sq(X, xi))));
    E *ux = diff(tst, x), *uy = diff(tst, y);
    E *t1 = s_mul2(a, ux), *t2 = s_mul2(b, uy), *t3 = s_mul2(c, tst);
    E *r = s_sub(s_add(3, (E *[]){t1, t2, t3}), f);
    E *m = s_add(4, (E *[]){s_fn1(F_ABS, t1), s_fn1(F_ABS, t2), s_fn1(F_ABS, t3), s_fn1(F_ABS, f)});
    if (!vanishes(X, r, m, p->v)) return NULL;
    if (nc) {
        E *rc = s_sub(subst(res, p->v[cd->var], cd->at), cd->val);
        E *v2[2] = {p->v[1 - cd->var], p->v[1 - cd->var]};
        if (!vanishes(X, rc, cd->val, v2)) return NULL;
    }
    return res;
}

/* --------------------------------------------------- eigenfunction series */
enum { BC_DD, BC_NN, BC_DN, BC_ND };
typedef struct {
    int bc;
    E *x, *a, *L, *n; /* variable, left end, length, symbolic mode (assumed a positive integer) */
} eig_t;

static E *eig_lam(XP, eig_t *g, E *n) {
    E *k = g->bc == BC_DD || g->bc == BC_NN ? n : s_sub(n, K->chalf);
    return s_div(s_mul2(k, K->pi_), g->L);
}

static E *eig_phi(XP, eig_t *g, E *n) {
    E *arg = s_mul2(eig_lam(X, g, n), s_sub(g->x, g->a));
    return s_fn1(g->bc == BC_DD || g->bc == BC_DN ? F_SIN : F_COS, arg);
}

static E *eig_nrm(XP, eig_t *g, E *n) { return g->bc == BC_NN && is_zero(n) ? g->L : s_mul2(K->chalf, g->L); }

static long eig_n0(eig_t *g) { return g->bc == BC_NN ? 0 : 1; }

/* modes at which the trigonometric arguments of T resonate with lambda_n: added to ks[] */
static void resonant(XP, eig_t *g, E *T, long *ks, int *nk) {
    stack_check();
    if (IFN(T, F_SIN) || IFN(T, F_COS)) {
        E *w = rat_normal(diff(K0(T), g->x));
        if (freeof(w, g->x) && !is_zero(w)) {
            for (int sg = -1; sg <= 1; sg += 2) {
                E *k = rat_normal(s_div(s_mul2(mk_int(sg), s_mul2(w, g->L)), K->pi_));
                if (g->bc == BC_DN || g->bc == BC_ND) k = s_add2(k, K->chalf);
                long v;
                if (num_is_small_int(k, &v) && v >= eig_n0(g) && v < 200 && *nk < 24) ks[(*nk)++] = v;
            }
        }
    }
    if (T->t != E_ADD && T->t != E_MUL && T->t != E_POW && T->t != E_FN) return;
    for (uint32_t i = 0; i < T->n; i++) resonant(X, g, EK(T)[i], ks, nk);
}

/* exact coefficient of mode k */
static E *coef_at(XP, eig_t *g, E *F, long k) {
    E *kk = mk_int(k), *I = dinteg(X, s_mul2(F, eig_phi(X, g, kk)), g->x, g->a, s_add2(g->a, g->L));
    return I ? pde_simp(X, s_div(I, eig_nrm(X, g, kk))) : NULL;
}

/* coefficients of a data function: the general formula gen (NULL: 0) for every mode except the
 * exceptional ones kex[] (resonances, N-N mode 0), whose exact values are cex[]; kmax is the
 * largest exceptional mode (n0 - 1 if none) */
#define NEX 24
typedef struct {
    int nex;
    long kex[NEX], kmax;
    E *cex[NEX], *gen;
} coefs_t;

static E *coef_k(XP, eig_t *g, coefs_t *c, long k) {
    for (int i = 0; i < c->nex; i++)
        if (c->kex[i] == k) return c->cex[i];
    return c->gen ? pde_simp(X, subst(c->gen, g->n, mk_int(k))) : K->c0;
}

/* 0 if an integral fails or the general formula cannot be trusted */
static int fcoef(XP, eig_t *g, E *F, coefs_t *C) {
    long n0 = eig_n0(g);
    C->nex = 0, C->kmax = n0 - 1, C->gen = NULL;
    if (is_zero(F)) return 1;
    F = expand(eval_cmd(C_TLIN, 1, &F));
    uint32_t nt = F->t == E_ADD ? F->n : 1;
    E **T = F->t == E_ADD ? EK(F) : &F, **G = salloc(sizeof(E *) * nt);
    /* per term: its exceptional modes and their exact values */
    long *tk = salloc(sizeof(long) * nt * NEX);
    E **tc = salloc(sizeof(E *) * nt * NEX);
    int *tn = salloc(sizeof(int) * nt);
    for (uint32_t i = 0; i < nt; i++) {
        tn[i] = 0;
        E *I = ginteg(X, s_mul2(T[i], eig_phi(X, g, g->n)), g->x, g->a, s_add2(g->a, g->L));
        if (!I) return 0;
        G[i] = pde_simp(X, s_div(I, eig_nrm(X, g, g->n)));
        long ks[32];
        int nk = 0;
        for (long k = n0; k < n0 + 3; k++) ks[nk++] = k;
        resonant(X, g, T[i], ks, &nk);
        for (int j = 0; j < nk; j++) {
            int res = j >= 3 || (g->bc == BC_NN && ks[j] == 0), dup = 0; /* mode 0 of N-N has its own norm */
            for (int q = 0; q < nk; q++) {
                if (q != j && ks[q] == ks[j] && q >= 3) res = 1;
                if (q < j && ks[q] == ks[j]) dup = 1;
            }
            if (dup) continue;
            E *ex = coef_at(X, g, T[i], ks[j]), *num, *den;
            if (!ex) return 0;
            X->numden(rat_normal(G[i]), &num, &den);
            E *kk = mk_int(ks[j]);
            int ok = !is_zero(pde_simp(X, subst(den, g->n, kk)));
            if (ok) ok = is_zero(pde_simp(X, s_sub(subst(G[i], g->n, kk), ex)));
            if (ok) continue;
            if (!res) return 0; /* the general formula is wrong away from a resonance: give up */
            if (tn[i] == NEX) return 0;
            tk[i * NEX + tn[i]] = ks[j], tc[i * NEX + tn[i]++] = ex;
        }
    }
    /* the exceptional modes of any term: exact there, the general formula of the other terms */
    for (uint32_t i = 0; i < nt; i++)
        for (int j = 0; j < tn[i]; j++) {
            long k = tk[i * NEX + j];
            int seen = 0;
            for (int q = 0; q < C->nex; q++) seen |= C->kex[q] == k;
            if (seen) continue;
            if (C->nex == NEX) return 0;
            E *c = K->c0;
            for (uint32_t m = 0; m < nt; m++) {
                E *v = NULL;
                for (int q = 0; q < tn[m]; q++)
                    if (tk[m * NEX + q] == k) v = tc[m * NEX + q];
                c = s_add2(c, v ? v : subst(G[m], g->n, mk_int(k)));
            }
            C->kex[C->nex] = k, C->cex[C->nex++] = pde_simp(X, c);
            if (k > C->kmax) C->kmax = k;
        }
    E *Gs = pde_simp(X, s_add(nt, G));
    C->gen = is_zero(Gs) ? NULL : Gs;
    return 1;
}

/* the inert sum(term, n, from, inf) */
static E *isum(XP, E *term, E *n, long from) {
    E *a[4] = {term, n, mk_int(from), K->inf_};
    E *r = mk_fn(C_SUM, 4, a);
    r->f = F_SIMP;
    return r;
}

/* a free index name for the result */
static E *index_sym(XP, E *e) {
    const char nm[4][2] = {{'n', 0}, {'k', 0}, {'m', 0}, {'j', 0}};
    for (int i = 0; i < 4; i++) {
        E *s = mk_sym_name(nm[i]);
        if (freeof(e, s)) return s;
    }
    return mk_sym_name("_k");
}

/* term(k, c1, c2) of the series of a problem: the caller's builder */
typedef E *(*term_fn)(XP, void *ctx, E *n, E *c1, E *c2);

/* sum over the modes of coefficients (c1, c2) of the two data functions F1, F2 (F2 may be NULL) */
static E *series(XP, eig_t *g, E *F1, E *F2, term_fn tf, void *ctx, long nterms, E *ref) {
    coefs_t *A = salloc(sizeof(coefs_t)), *B = salloc(sizeof(coefs_t));
    if (!fcoef(X, g, F1, A)) return NULL;
    if (!fcoef(X, g, F2 ? F2 : K->c0, B)) return NULL;
    E *g1 = A->gen, *g2 = B->gen;
    long n0 = eig_n0(g), kmax = A->kmax > B->kmax ? A->kmax : B->kmax;
    if (kmax >= n0 + 200) return NULL;
    E *res = K->c0;
    long last = nterms > 0 ? n0 + nterms - 1 : kmax;
    for (long k = n0; k <= last; k++) {
        E *c1 = coef_k(X, g, A, k), *c2 = coef_k(X, g, B, k);
        if (is_zero(c1) && is_zero(c2)) continue;
        res = s_add2(res, tf(X, ctx, mk_int(k), c1, c2));
    }
    if (nterms > 0 || (!g1 && !g2)) return res;
    E *n = index_sym(X, ref), *t = tf(X, ctx, g->n, g1 ? g1 : K->c0, g2 ? g2 : K->c0);
    return s_add2(res, isum(X, subst(t, g->n, n), n, kmax + 1));
}

typedef struct {
    eig_t *g;
    E *t, *k, *c; /* time, diffusivity / wave speed */
    E *other, *ofrom, *oto; /* Laplace: the other variable, sinh(lam (other - ofrom)) / sinh(lam (oto - ofrom)) */
} tctx;

static E *heat_term(XP, void *v, E *n, E *c1, E *c2) {
    tctx *h = v;
    E *lam = eig_lam(X, h->g, n);
    return s_mul(3, (E *[]){c1, s_exp(s_neg(s_mul(3, (E *[]){h->k, sq(X, lam), h->t}))), eig_phi(X, h->g, n)});
}

static E *wave_term(XP, void *v, E *n, E *c1, E *c2) {
    tctx *h = v;
    E *cl = s_mul2(h->c, eig_lam(X, h->g, n)), *tm;
    if (is_zero(cl)) tm = s_add2(c1, s_mul2(c2, h->t));
    else tm = s_add2(s_mul2(c1, s_fn1(F_COS, s_mul2(cl, h->t))), s_div(s_mul2(c2, s_fn1(F_SIN, s_mul2(cl, h->t))), cl));
    return s_mul2(tm, eig_phi(X, h->g, n));
}

static E *lap_term(XP, void *v, E *n, E *c1, E *c2) {
    tctx *h = v;
    E *lam = eig_lam(X, h->g, n);
    E *r = s_div(s_fn1(F_SINH, s_mul2(lam, s_sub(h->other, h->ofrom))), s_fn1(F_SINH, s_mul2(lam, s_sub(h->oto, h->ofrom))));
    return s_mul(3, (E *[]){c1, r, eig_phi(X, h->g, n)});
}

/* the symbolic mode _n, assumed a positive integer */
static E *mode_sym(XP) {
    E *n = mk_sym_name("_n"), *a[2] = {n, mk_sym_name("integer")};
    eval_cmd(C_ASSUME, 2, a);
    a[1] = mk_sym_name("positive");
    eval_cmd(C_ASSUME, 2, a);
    return n;
}

/* order the two ends: 1 if a < b is known or assumed (a = 0), 0 if b < a, -1 unknown */
static int ordered(XP, E *a, E *b) {
    double x, y;
    if (to_double(evalf_digits(a, 15), &x) && to_double(evalf_digits(b, 15), &y)) return x < y ? 1 : x > y ? 0 : -1;
    int s = X->sign_of(rat_normal(s_sub(b, a)));
    if (s) return s > 0;
    return is_zero(a) ? 1 : is_zero(b) ? 0 : -1;
}

/* heat / wave on an interval or the line; laplace on a rectangle */
static E *sepvar(XP, pde_t *p, int kind, pcond_t *c, int nc, long nterms) {
    E *x = p->v[0], *t = p->v[1];
    pcond_t *bx[2] = {0, 0}, *it[2] = {0, 0}; /* x-boundary; initial u, u_t (Laplace: y sides) */
    int nb = 0, ni = 0;
    for (int i = 0; i < nc; i++) {
        if (c[i].var == 0) {
            if (nb == 2) return NULL;
            bx[nb++] = &c[i];
        } else {
            if (ni == 2) return NULL;
            it[ni++] = &c[i];
        }
    }
    E *ref = s_list(2, (E *[]){p->u, s_list((uint32_t)nc, (E *[]){c[0].val, nc > 1 ? c[1].val : K->c0, nc > 2 ? c[2].val : K->c0, nc > 3 ? c[3].val : K->c0})});
    if (kind == PK_LAPLACE) {
        if (!is_zero(p->param) || nb != 2 || ni != 2) return NULL;
        for (int i = 0; i < 2; i++)
            if (bx[i]->d != D_U || it[i]->d != D_U) return NULL;
        int ox = ordered(X, bx[0]->at, bx[1]->at), oy = ordered(X, it[0]->at, it[1]->at);
        if (ox < 0 || oy < 0) return NULL;
        pcond_t *xl = ox ? bx[0] : bx[1], *xr = ox ? bx[1] : bx[0], *yl = oy ? it[0] : it[1], *yr = oy ? it[1] : it[0];
        E *A = rat_normal(s_sub(xr->at, xl->at)), *B = rat_normal(s_sub(yr->at, yl->at)), *nn = mode_sym(X), *res = K->c0;
        /* sides y = c, y = d (data in x) and x = a, x = b (data in y) */
        pcond_t *side[4] = {yl, yr, xl, xr};
        for (int s = 0; s < 4; s++) {
            if (is_zero(side[s]->val)) continue;
            int ysd = s < 2; /* data along x */
            eig_t g = {BC_DD, ysd ? x : t, ysd ? xl->at : yl->at, ysd ? A : B, nn};
            tctx h = {&g, 0, 0, 0, ysd ? t : x, 0, 0};
            E *lo = ysd ? yl->at : xl->at, *hi = ysd ? yr->at : xr->at;
            if (s == 0 || s == 2) h.other = s_add2(lo, s_sub(hi, h.other)), h.ofrom = lo, h.oto = hi; /* reflect */
            else h.ofrom = lo, h.oto = hi;
            E *r = series(X, &g, side[s]->val, NULL, lap_term, &h, nterms, ref);
            if (!r) return NULL;
            res = s_add2(res, r);
        }
        return res;
    }
    /* initial data at t = t0 */
    E *f = NULL, *gv = NULL, *t0 = NULL;
    for (int i = 0; i < ni; i++) {
        if (t0 && !is_zero(rat_normal(s_sub(t0, it[i]->at)))) return NULL;
        t0 = it[i]->at;
        if (it[i]->d == D_U) f = it[i]->val;
        else gv = it[i]->val;
    }
    if (!f || (kind == PK_HEAT && gv) || (kind == PK_WAVE && !gv)) return NULL;
    E *tt = is_zero(t0) ? t : s_sub(t, t0);
    if (nb == 0) { /* the whole line */
        E *U;
        if (kind == PK_WAVE) {
            E *cc = wave_c(X, p->param), *G = integ(X, gv, x);
            if (!G) return NULL;
            E *xm = s_sub(x, s_mul2(cc, tt)), *xp = s_add2(x, s_mul2(cc, tt));
            U = s_add2(s_mul2(K->chalf, s_add2(subst(f, x, xm), subst(f, x, xp))),
                       s_div(s_sub(subst(G, x, xp), subst(G, x, xm)), s_mul2(mk_int(2), cc)));
        } else {
            E *z = mk_sym_name("_z");
            E *arg = s_add2(x, s_mul(4, (E *[]){mk_int(2), s_sqrt(p->param), s_sqrt(tt), z}));
            E *I = dinteg(X, s_mul2(subst(f, x, arg), s_exp(s_neg(sq(X, z)))), z, s_neg(K->inf_), K->inf_);
            if (!I || !freeof(I, z)) return NULL;
            U = s_div(I, s_sqrt(K->pi_));
        }
        U = pde_simp(X, U);
        /* check the equation and the initial conditions at sample points */
        E *ut = diff(U, t), *a1 = kind == PK_WAVE ? diff(ut, t) : ut, *a2 = s_mul2(p->param, diff(diff(U, x), x));
        if (!vanishes(X, s_sub(a1, a2), s_add2(s_fn1(F_ABS, a1), s_fn1(F_ABS, a2)), p->v)) return NULL;
        if (!vanishes(X, s_sub(subst(U, t, t0), f), f, p->v)) return NULL;
        if (kind == PK_WAVE && !vanishes(X, s_sub(subst(ut, t, t0), gv), gv, p->v)) return NULL;
        return U;
    }
    if (nb != 2) return NULL;
    int o = ordered(X, bx[0]->at, bx[1]->at);
    if (o < 0) return NULL;
    pcond_t *l = o ? bx[0] : bx[1], *r = o ? bx[1] : bx[0];
    if (!freeof(l->val, t) || !freeof(r->val, t)) return NULL;
    E *L = rat_normal(s_sub(r->at, l->at)), *w;
    int bc = l->d == D_U ? (r->d == D_U ? BC_DD : BC_DN) : (r->d == D_U ? BC_ND : BC_NN);
    switch (bc) {
    case BC_DD: w = s_add2(l->val, s_div(s_mul2(s_sub(r->val, l->val), s_sub(x, l->at)), L)); break;
    case BC_DN: w = s_add2(l->val, s_mul2(r->val, s_sub(x, l->at))); break;
    case BC_ND: w = s_add2(r->val, s_mul2(l->val, s_sub(x, r->at))); break;
    default:
        if (!is_zero(l->val) || !is_zero(r->val)) return NULL;
        w = K->c0;
    }
    w = pde_simp(X, w);
    eig_t g = {bc, x, l->at, L, mode_sym(X)};
    tctx h = {&g, tt, p->param, kind == PK_WAVE ? wave_c(X, p->param) : NULL, 0, 0, 0};
    E *ser = series(X, &g, pde_simp(X, s_sub(f, w)), kind == PK_WAVE ? gv : NULL, kind == PK_HEAT ? heat_term : wave_term,
                    &h, nterms, ref);
    return ser ? s_add2(w, ser) : NULL;
}

static E *pdesolve(XP, uint32_t n, E **a) {
    pde_t p;
    pcond_t c[PDE_MAXC];
    long nterms = 0;
    if (!pde_init(X, &p, a[1])) cas_throw(CAS_ERR_DOMAIN, "pdesolve: the unknown looks like u(x,t)");
    if (n > 3 && (!num_is_small_int(a[3], &nterms) || nterms < 1 || nterms > 200))
        cas_throw(CAS_ERR_DOMAIN, "pdesolve: the number of terms is 1..200");
    if (!pde_parse(X, &p, a[0])) return NULL;
    int nc = pde_conds(X, &p, n > 2 ? a[2] : NULL, c), kind = pde_kind(X, &p);
    if (nc < 0) cas_throw(CAS_ERR_DOMAIN, "pdesolve: conditions look like u(0,t)=..., u_x(L,t)=..., u(x,0)=..., u_t(x,0)=...");
    E *r = NULL;
    if (kind == PK_FIRST) r = char1(X, &p, c, nc);
    else if (kind != PK_NONE && nc > 0) r = sepvar(X, &p, kind, c, nc, nterms);
    E *un = mk_sym_name("_n");
    eval_cmd(C_UNASSUME, 1, &un);
    return r;
}

/* ------------------------------------------------------------ commands --- */
enum { P_PDESOLVE };
static const casx_cmd_t ptab[] = {{"pdesolve", 2, 4}};

static E *pcmd(XP, int op, uint32_t n, E **a) { return op == P_PDESOLVE ? pdesolve(X, n, a) : NULL; }

int casx_pde_register(XP) { return X->reg("casx", ptab, (int)(sizeof ptab / sizeof ptab[0]), pcmd); }
