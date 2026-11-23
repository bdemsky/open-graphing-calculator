/*
 * pdenum.c -- finite-difference solutions of partial differential equations
 * (science part of the advanced CAS extension, casx.h):
 *
 *   pdenum(eq, u(x,t), conditions, [x, a, b, nx], [t, t0, t1, nt] [, method])
 *   gridval(M, [a, b], [c, d], x, t)
 *
 * The equation is classified as by pdesolve (pde.h):
 *   heat   u_t = k u_xx      explicit (forward Euler in time, central in space;
 *                            stable for r = k dt/dx^2 <= 1/2, an error otherwise)
 *                            or Crank-Nicolson (method `cn`, unconditionally
 *                            stable; tridiagonal systems by the Thomas
 *                            algorithm); without a method: explicit when it is
 *                            stable, else Crank-Nicolson
 *   wave   u_tt = c^2 u_xx   leapfrog (second order; the first step uses
 *                            u_t(x,0) and the Taylor term), stable for the
 *                            Courant number s = c dt/dx <= 1 (an error otherwise)
 *   Laplace / Poisson u_xx + u_yy = rho(x,y) on the rectangle [a,b] x [c,d]:
 *                            successive over-relaxation with the optimal factor
 *                            of the square grid, to a change below 1e-12 (relative)
 * Boundary conditions u(a,t) = h(t) (Dirichlet) or u_x(a,t) = h(t) (Neumann,
 * through a ghost node) at both ends for heat and wave, values h(x) / h(y) on
 * the four sides for Laplace; data may be any expressions (evaluated at the
 * nodes).  The result is the matrix M with M[j][i] = u(x_i, t_j): one row per
 * time level (Laplace: per y_j), x_i = a + i (b-a)/nx.  Its size is checked
 * against the free CAS memory before anything is computed.
 *
 * gridval(M, [a,b], [c,d], x, t) interpolates M bilinearly (undef outside the
 * rectangle).  With x and t symbolic it stays unevaluated, and the graph
 * compiler (cas/src/numeric.c, OP_GRID) turns it into a table lookup: so
 * U := pdenum(...) can be drawn as a surface in the 3D grapher
 * (z = gridval(U,[a,b],[t0,t1],x,y)) or animated in time on a 2D graph
 * (y = gridval(U,[a,b],[t0,t1],x,t) with the slider t played by the
 * Animation module).
 */
#include "pde.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

#define PN_MAXCELLS 250000L

/* numeric value of e with v = x (and w = y) substituted */
static double nval(XP, E *e, E *v, double x, E *w, double y) {
    E *s = e;
    if (v) s = subst(s, v, mk_flt(x));
    if (w) s = subst(s, w, mk_flt(y));
    double d;
    if (!to_double(evalf_digits(s, 15), &d) || d != d || d - d != 0) cas_throw(CAS_ERR_DOMAIN, "pdenum: data is not real and finite at a grid point");
    return d;
}

/* [v, lo, hi, n] */
static void range(XP, E *r, E *v, double *lo, double *hi, long *n, const char *what) {
    if (r->t != E_LIST || r->n != 4 || !expr_eq(EK(r)[0], v) || !to_double(evalf_digits(EK(r)[1], 15), lo) ||
        !to_double(evalf_digits(EK(r)[2], 15), hi) || !num_is_small_int(EK(r)[3], n) || *n < 2 || *n > 10000 || !(*lo < *hi))
        cas_throw(CAS_ERR_DOMAIN, what);
}

/* solve the tridiagonal system a[i] y[i-1] + b[i] y[i] + c[i] y[i+1] = d[i] in place (into d) */
static void thomas(double *a, double *b, double *c, double *d, long n) {
    for (long i = 1; i < n; i++) {
        double m = a[i] / b[i - 1];
        b[i] -= m * c[i - 1];
        d[i] -= m * d[i - 1];
    }
    d[n - 1] /= b[n - 1];
    for (long i = n - 2; i >= 0; i--) d[i] = (d[i] - c[i] * d[i + 1]) / b[i];
}

static E *row(XP, double *u, long n) {
    E **r = salloc(sizeof(E *) * (size_t)n);
    for (long i = 0; i < n; i++) r[i] = mk_flt(u[i]);
    return s_list((uint32_t)n, r);
}

/* the boundary data: ends[0] at a, ends[1] at b (heat, wave); NULL if a condition is not at an end */
static int ends_of(XP, pde_t *p, pcond_t *c, int nc, double a, double b, pcond_t **e) {
    e[0] = e[1] = NULL;
    for (int i = 0; i < nc; i++) {
        if (c[i].var != 0) continue;
        double v;
        if (!to_double(evalf_digits(c[i].at, 15), &v)) return 0;
        int k = __builtin_fabs(v - a) <= 1e-12 * (1 + __builtin_fabs(a)) ? 0 : __builtin_fabs(v - b) <= 1e-12 * (1 + __builtin_fabs(b)) ? 1 : -1;
        if (k < 0 || e[k]) return 0;
        e[k] = &c[i];
    }
    return e[0] && e[1];
}

/* the result (a float node and a list slot per cell, twice in scratch while it is built) and the
 * work arrays must fit in the free scratch, and the result once more in the persistent heap,
 * where it is kept as ans */
static void need(XP, long cells, long work) {
    size_t res = (size_t)cells * (sizeof(E) + 8 + 2 * sizeof(E *)), heap = (size_t)(K->h_hi - K->h_lo) - K->h_used;
    if ((size_t)(K->s_hi - K->s_top) < res + (size_t)work * sizeof(double) + 65536 || heap < res + 65536)
        cas_throw(CAS_ERR_MEMORY, "pdenum: the grid does not fit in memory (fewer points)");
}

static E *pdenum(XP, uint32_t na, E **A) {
    pde_t p;
    pcond_t c[PDE_MAXC];
    if (!pde_init(X, &p, A[1])) cas_throw(CAS_ERR_DOMAIN, "pdenum: the unknown looks like u(x,t)");
    if (!pde_parse(X, &p, A[0])) cas_throw(CAS_ERR_DOMAIN, "pdenum: not a linear equation in u");
    int kind = pde_kind(X, &p), nc = pde_conds(X, &p, A[2], c);
    if (nc < 0) cas_throw(CAS_ERR_DOMAIN, "pdenum: conditions look like u(0,t)=..., u_x(L,t)=..., u(x,0)=..., u_t(x,0)=...");
    if (kind != PK_HEAT && kind != PK_WAVE && kind != PK_LAPLACE)
        cas_throw(CAS_ERR_UNSUPPORTED, "pdenum: u_t = k u_xx, u_tt = c^2 u_xx or u_xx + u_yy = rho");
    double a, b, t0, t1, k;
    long nx, nt;
    E *x = p.v[0], *t = p.v[1];
    range(X, A[3], x, &a, &b, &nx, "pdenum: expected [x, a, b, nx], nx = 2..10000");
    range(X, A[4], t, &t0, &t1, &nt, "pdenum: expected [t, t0, t1, nt], nt = 2..10000");
    if ((nx + 1) * (nt + 1) > PN_MAXCELLS) cas_throw(CAS_ERR_MEMORY, "pdenum: too many grid points (at most 250000)");
    int meth = 0; /* 0 auto, 1 explicit, 2 Crank-Nicolson */
    if (na > 5) {
        if (A[5]->t == E_SYM && A[5]->s == mk_sym_name("explicit")->s) meth = 1;
        else if (A[5]->t == E_SYM && A[5]->s == mk_sym_name("cn")->s) meth = 2;
        else cas_throw(CAS_ERR_DOMAIN, "pdenum: method explicit or cn");
    }
    long n = nx + 1;
    double dx = (b - a) / (double)nx, dt = (t1 - t0) / (double)nt;
    E **rows = salloc(sizeof(E *) * (size_t)(nt + 1));
    if (kind == PK_LAPLACE) { /* t is y here */
        need(X, n * (nt + 1), 2 * n * (nt + 1));
        pcond_t *sx[2], *sy[2] = {NULL, NULL};
        if (!ends_of(X, &p, c, nc, a, b, sx)) cas_throw(CAS_ERR_DOMAIN, "pdenum: give u on the sides x = a and x = b");
        for (int i = 0; i < nc; i++) {
            if (c[i].var != 1) continue;
            double v;
            if (!to_double(evalf_digits(c[i].at, 15), &v)) cas_throw(CAS_ERR_DOMAIN, "pdenum: sides must be numeric");
            int s = __builtin_fabs(v - t0) <= 1e-12 * (1 + __builtin_fabs(t0)) ? 0 : __builtin_fabs(v - t1) <= 1e-12 * (1 + __builtin_fabs(t1)) ? 1 : -1;
            if (s < 0 || sy[s]) cas_throw(CAS_ERR_DOMAIN, "pdenum: a condition is not on a side of the rectangle");
            sy[s] = &c[i];
        }
        if (!sy[0] || !sy[1] || sx[0]->d || sx[1]->d || sy[0]->d || sy[1]->d)
            cas_throw(CAS_ERR_DOMAIN, "pdenum: give u on the four sides (Dirichlet)");
        long m = nt + 1;
        double *u = salloc(sizeof(double) * (size_t)(n * m)), *rho = salloc(sizeof(double) * (size_t)(n * m));
        int zr = is_zero(p.param);
        for (long j = 0; j < m; j++)
            for (long i = 0; i < n; i++) {
                double xi = a + (double)i * dx, yj = t0 + (double)j * dt;
                rho[j * n + i] = zr ? 0 : nval(X, p.param, x, xi, t, yj);
                double v = 0;
                if (j == 0) v = nval(X, sy[0]->val, x, xi, NULL, 0);
                else if (j == m - 1) v = nval(X, sy[1]->val, x, xi, NULL, 0);
                else if (i == 0) v = nval(X, sx[0]->val, t, yj, NULL, 0);
                else if (i == n - 1) v = nval(X, sx[1]->val, t, yj, NULL, 0);
                u[j * n + i] = v;
            }
        double hx2 = 1 / (dx * dx), hy2 = 1 / (dt * dt), dg = 2 * (hx2 + hy2);
        /* optimal factor 2 / (1 + sin(pi/N)) of the square grid (sin by its series: no libm here) */
        double big = nx > nt ? (double)nx : (double)nt, h = 3.141592653589793 / big, h2 = h * h;
        double w = 2 / (1 + h * (1 - h2 / 6 * (1 - h2 / 20 * (1 - h2 / 42 * (1 - h2 / 72)))));
        for (long it = 0; it < 20000; it++) {
            double ch = 0, mag = 1e-300;
            poll_intr();
            for (long j = 1; j < m - 1; j++)
                for (long i = 1; i < n - 1; i++) {
                    double *q = &u[j * n + i];
                    double g = ((q[1] + q[-1]) * hx2 + (q[n] + q[-n]) * hy2 - rho[j * n + i]) / dg, d = w * (g - *q);
                    *q += d;
                    ch = __builtin_fabs(d) > ch ? __builtin_fabs(d) : ch;
                    mag = __builtin_fabs(*q) > mag ? __builtin_fabs(*q) : mag;
                }
            if (ch <= 1e-12 * mag) break;
            if (it == 19999) cas_throw(CAS_ERR_UNSUPPORTED, "pdenum: relaxation did not converge");
        }
        for (long j = 0; j < m; j++) rows[j] = row(X, &u[j * n], n);
        return s_list((uint32_t)m, rows);
    }
    need(X, n * (nt + 1), 8 * n);
    pcond_t *e[2];
    if (!ends_of(X, &p, c, nc, a, b, e)) cas_throw(CAS_ERR_DOMAIN, "pdenum: give a condition at x = a and at x = b");
    E *f = NULL, *g = NULL;
    for (int i = 0; i < nc; i++)
        if (c[i].var == 1) {
            double v;
            if (!to_double(evalf_digits(c[i].at, 15), &v) || __builtin_fabs(v - t0) > 1e-12 * (1 + __builtin_fabs(t0)))
                cas_throw(CAS_ERR_DOMAIN, "pdenum: initial conditions at t = t0");
            if (c[i].d == D_U) f = c[i].val;
            else g = c[i].val;
        }
    if (!f || (kind == PK_WAVE && !g)) cas_throw(CAS_ERR_DOMAIN, kind == PK_WAVE ? "pdenum: give u(x,t0) and u_t(x,t0)" : "pdenum: give u(x,t0)");
    if (!to_double(evalf_digits(p.param, 15), &k) || !(k > 0)) cas_throw(CAS_ERR_DOMAIN, "pdenum: the coefficient must be a positive number");
    double *u = salloc(sizeof(double) * (size_t)n), *v = salloc(sizeof(double) * (size_t)n), *w = salloc(sizeof(double) * (size_t)n);
    for (long i = 0; i < n; i++) u[i] = nval(X, f, x, a + (double)i * dx, NULL, 0);
    /* boundary rows: Dirichlet value or Neumann slope at time tm */
#define BVAL(s, tm) nval(X, e[s]->val, t, (tm), NULL, 0)
    for (int s = 0; s < 2; s++)
        if (!e[s]->d) u[s ? n - 1 : 0] = BVAL(s, t0);
    rows[0] = row(X, u, n);
    if (kind == PK_HEAT) {
        double r = k * dt / (dx * dx);
        if (meth == 1 && r > 0.5) cas_throw(CAS_ERR_DOMAIN, "pdenum: explicit scheme unstable (k dt/dx^2 > 1/2): more time steps or cn");
        int cn = meth == 2 || (meth == 0 && r > 0.5);
        double *ta = salloc(sizeof(double) * (size_t)n), *tb = salloc(sizeof(double) * (size_t)n), *tc = salloc(sizeof(double) * (size_t)n);
        for (long j = 1; j <= nt; j++) {
            double tp = t0 + (double)(j - 1) * dt, tn = t0 + (double)j * dt;
            poll_intr();
            if (!cn) {
                for (long i = 1; i < n - 1; i++) v[i] = u[i] + r * (u[i + 1] - 2 * u[i] + u[i - 1]);
                for (int s = 0; s < 2; s++) {
                    long i = s ? n - 1 : 0, o = s ? n - 2 : 1;
                    if (!e[s]->d) v[i] = BVAL(s, tn);
                    else v[i] = u[i] + 2 * r * (u[o] - u[i] + (s ? 1 : -1) * dx * BVAL(s, tp));
                }
            } else {
                for (long i = 1; i < n - 1; i++) {
                    ta[i] = tc[i] = -r / 2, tb[i] = 1 + r;
                    v[i] = r / 2 * (u[i - 1] + u[i + 1]) + (1 - r) * u[i];
                }
                for (int s = 0; s < 2; s++) {
                    long i = s ? n - 1 : 0, o = s ? n - 2 : 1;
                    double *off = s ? ta : tc;
                    ta[i] = tc[i] = 0;
                    if (!e[s]->d) tb[i] = 1, v[i] = BVAL(s, tn);
                    else {
                        tb[i] = 1 + r, off[i] = -r;
                        v[i] = (1 - r) * u[i] + r * u[o] + (s ? 1 : -1) * r * dx * (BVAL(s, tp) + BVAL(s, tn));
                    }
                }
                thomas(ta, tb, tc, v, n);
            }
            double *sw = u;
            u = v, v = sw;
            rows[j] = row(X, u, n);
        }
        return s_list((uint32_t)(nt + 1), rows);
    }
    /* wave: w = u^{j-1}, u = u^j, v = u^{j+1} */
    double s = X->sqrt(k) * dt / dx, s2 = s * s;
    if (s > 1 + 1e-12) cas_throw(CAS_ERR_DOMAIN, "pdenum: unstable (c dt/dx > 1): more time steps");
    for (long j = 1; j <= nt; j++) {
        double tp = t0 + (double)(j - 1) * dt, tn = t0 + (double)j * dt;
        poll_intr();
        for (long i = 0; i < n; i++) {
            double l = i > 0 ? u[i - 1] : 0, rr = i < n - 1 ? u[i + 1] : 0;
            if (i == 0 && e[0]->d) l = u[1] - 2 * dx * BVAL(0, tp);
            if (i == n - 1 && e[1]->d) rr = u[n - 2] + 2 * dx * BVAL(1, tp);
            double lap = rr - 2 * u[i] + l;
            v[i] = j == 1 ? u[i] + dt * nval(X, g, x, a + (double)i * dx, NULL, 0) + s2 / 2 * lap : 2 * u[i] - w[i] + s2 * lap;
        }
        for (int q = 0; q < 2; q++)
            if (!e[q]->d) v[q ? n - 1 : 0] = BVAL(q, tn);
        double *sw = w;
        w = u, u = v, v = sw;
        rows[j] = row(X, u, n);
    }
#undef BVAL
    return s_list((uint32_t)(nt + 1), rows);
}

/* bilinear interpolation of the grid M over [a,b] x [c,d] */
static E *gridval(XP, E **A) {
    uint32_t rows, cols;
    double r[4], x, y;
    if (!is_matrix(A[0], &rows, &cols) || A[1]->t != E_LIST || A[1]->n != 2 || A[2]->t != E_LIST || A[2]->n != 2)
        cas_throw(CAS_ERR_DOMAIN, "gridval: expected gridval(M, [a,b], [c,d], x, y)");
    for (int i = 0; i < 4; i++)
        if (!to_double(evalf_digits(EK(A[1 + i / 2])[i % 2], 15), &r[i])) return NULL;
    if (!to_double(evalf_digits(A[3], 15), &x) || !to_double(evalf_digits(A[4], 15), &y)) return NULL;
    if (x < r[0] || x > r[1] || y < r[2] || y > r[3] || !(r[0] < r[1]) || !(r[2] < r[3])) return mk_sym(S_UNDEF);
    double fx = cols > 1 ? (x - r[0]) / (r[1] - r[0]) * (cols - 1) : 0, fy = rows > 1 ? (y - r[2]) / (r[3] - r[2]) * (rows - 1) : 0;
    long i = (long)fx, j = (long)fy;
    if (i > (long)cols - 2) i = cols > 1 ? (long)cols - 2 : 0;
    if (j > (long)rows - 2) j = rows > 1 ? (long)rows - 2 : 0;
    double tx = fx - (double)i, ty = fy - (double)j, m[4];
    for (int q = 0; q < 4; q++) {
        long jj = j + (q >> 1 && rows > 1), ii = i + ((q & 1) && cols > 1);
        if (!to_double(EK(EK(A[0])[jj])[ii], &m[q])) return NULL;
    }
    return mk_flt((1 - tx) * (1 - ty) * m[0] + tx * (1 - ty) * m[1] + (1 - tx) * ty * m[2] + tx * ty * m[3]);
}

enum { N_PDENUM, N_GRIDVAL };
static const casx_cmd_t ntab[] = {{"pdenum", 5, 6}, {"gridval", 5, 5}};

static E *ncmd(XP, int op, uint32_t n, E **a) { return op == N_PDENUM ? pdenum(X, n, a) : gridval(X, a); }

int casx_pdenum_register(XP) { return X->reg("casx", ntab, (int)(sizeof ntab / sizeof ntab[0]), ncmd); }
