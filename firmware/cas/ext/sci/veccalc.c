/*
 * veccalc.c -- vector calculus of the advanced CAS extension (casx.h):
 * grad, divergence, curl and laplacian in Cartesian, polar, cylindrical and
 * spherical coordinates, scalar and vector potentials, line integrals,
 * surface integrals and flux.
 *
 * Coordinates.  An orthogonal system with scale factors h_i (Lamé
 * coefficients): Cartesian h = 1; polar (r, t) h = (1, r); cylindrical
 * (r, t, z) h = (1, r, 1); spherical (r, t, p) h = (1, r, r sin t), t the
 * polar angle from the z axis and p the azimuth (the physics convention, as
 * in Mathematica).  Vector fields are given by their components in the
 * orthonormal frame of the system.  With H = h1 h2 h3:
 *   grad f_i = (1/h_i) d_i f
 *   div F    = (1/H) sum_i d_i (H/h_i F_i)
 *   curl F_i = (1/(h_j h_k)) (d_j (h_k F_k) - d_k (h_j F_j))  (i, j, k cyclic)
 *   lap f    = (1/H) sum_i d_i (H/h_i^2 d_i f)
 * The core handles the Cartesian forms with two arguments (cmds.c) and
 * forwards a coordinate argument and the 2D curl here.
 *
 * potential(F, vars [, coords]): phi with grad phi = F.  The covariant
 * components G_i = h_i F_i must satisfy d_j G_i = d_i G_j (curl 0: checked
 * symbolically, a field whose curl is numerically non-zero is rejected);
 * phi is built one variable at a time by integrating the part of G_i that
 * the previous terms do not account for, and verified at the end.
 * vpotential(B, [x, y, z]): A with curl A = B in the gauge A_z = 0:
 *   A_x = int B_y dz,  A_y = -int B_x dz + int R dx,
 *   R = B_z - (d_x (-int B_x dz) - d_y int B_y dz), free of z when div B = 0.
 *
 * lineint(F, vars, curve, t, a, b): int F(r(t)) . r'(t) dt for a vector
 * field F, int f(r(t)) |r'(t)| dt for a scalar.  surfint(f, vars, surface,
 * u, u0, u1, v, v0, v1): int int f |r_u x r_v| du dv (u inside: its bounds
 * may depend on v); flux(F, ...) the same with F . (r_u x r_v), and with a
 * plane curve flux(F, [x, y], curve, t, a, b) = int F . (y', -x') dt
 * (outward for a counter-clockwise curve).  Whatever cannot be integrated
 * exactly leaves the call unevaluated.
 */
#include "casx_mod.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

enum { CS_CART, CS_POLAR, CS_CYL, CS_SPH };

static E *vsimp(XP, E *e) { return eval_cmd(C_SIMPLIFY, 1, &e); }

static int named(XP, E *e, const char *nm) { return e->t == E_SYM && e->s == mk_sym_name(nm)->s; }

/* the variables: a list of n symbols (n = 2 or 3 unless any is allowed) */
static E **vars_of(XP, E *v, uint32_t *n) {
    if (v->t != E_LIST || !v->n) cas_throw(CAS_ERR_DOMAIN, "expected a list of variables");
    for (uint32_t i = 0; i < v->n; i++)
        if (EK(v)[i]->t != E_SYM) cas_throw(CAS_ERR_DOMAIN, "expected a list of variables");
    *n = v->n;
    return EK(v);
}

/* the coordinate system c (NULL: Cartesian) for n variables v; fills h */
static int coords(XP, E *c, uint32_t n, E **v, E **h) {
    int cs = CS_CART;
    if (c && !named(X, c, "cartesian") && !named(X, c, "rectangular")) {
        if (named(X, c, "polar") && n == 2) cs = CS_POLAR;
        else if (named(X, c, "cylindrical") && n == 3) cs = CS_CYL;
        else if (named(X, c, "spherical") && n == 3) cs = CS_SPH;
        else cas_throw(CAS_ERR_DOMAIN, "coordinates: cartesian, polar [r,t], cylindrical [r,t,z] or spherical [r,t,p]");
    }
    for (uint32_t i = 0; i < n; i++) h[i] = K->c1;
    if (cs != CS_CART) h[1] = v[0];
    if (cs == CS_SPH) h[2] = s_mul2(v[0], s_fn1(F_SIN, v[1]));
    return cs;
}

static E *field(XP, E *F, uint32_t n, const char *who) {
    if (F->t != E_LIST || F->n != n) cas_throw(CAS_ERR_DOMAIN, who);
    return F;
}

static E *prod_h(XP, uint32_t n, E **h) { return s_mul(n, h); }

static E *grad_cs(XP, E *f, uint32_t n, E **v, E **h) {
    E **g = salloc(sizeof(E *) * n);
    for (uint32_t i = 0; i < n; i++) g[i] = vsimp(X, s_div(diff(f, v[i]), h[i]));
    return s_list(n, g);
}

static E *div_cs(XP, E *F, uint32_t n, E **v, E **h) {
    E *H = prod_h(X, n, h), **t = salloc(sizeof(E *) * n);
    for (uint32_t i = 0; i < n; i++) t[i] = diff(s_mul2(s_div(H, h[i]), EK(F)[i]), v[i]);
    return vsimp(X, s_div(s_add(n, t), H));
}

static E *curl_cs(XP, E *F, uint32_t n, E **v, E **h) {
    E **f = EK(F);
    if (n == 2) return vsimp(X, s_div(s_sub(diff(s_mul2(h[1], f[1]), v[0]), diff(s_mul2(h[0], f[0]), v[1])), s_mul2(h[0], h[1])));
    E *c[3];
    for (int i = 0; i < 3; i++) {
        int j = (i + 1) % 3, k = (i + 2) % 3;
        c[i] = vsimp(X, s_div(s_sub(diff(s_mul2(h[k], f[k]), v[j]), diff(s_mul2(h[j], f[j]), v[k])), s_mul2(h[j], h[k])));
    }
    return s_list(3, c);
}

static E *lap_cs(XP, E *f, uint32_t n, E **v, E **h, int cs) {
    if (f->t == E_LIST) {
        field(X, f, n, "laplacian: the field needs one component per variable");
        if (cs == CS_CART) {
            E **c = salloc(sizeof(E *) * n);
            for (uint32_t i = 0; i < n; i++) c[i] = lap_cs(X, EK(f)[i], n, v, h, cs);
            return s_list(n, c);
        }
        if (n != 3) cas_throw(CAS_ERR_DOMAIN, "laplacian: vector fields in 3D only");
        /* vector Laplacian = grad div F - curl curl F */
        return vsimp(X, s_sub(grad_cs(X, div_cs(X, f, n, v, h), n, v, h), curl_cs(X, curl_cs(X, f, n, v, h), n, v, h)));
    }
    E *H = prod_h(X, n, h), **t = salloc(sizeof(E *) * n);
    for (uint32_t i = 0; i < n; i++) t[i] = diff(s_mul2(s_div(H, s_powi(h[i], 2)), diff(f, v[i])), v[i]);
    return vsimp(X, s_div(s_add(n, t), H));
}

/* ------------------------------------------------------------ helpers --- */
static int has_fn(XP, E *e, int id) {
    stack_check();
    if (e->t == E_FN && e->s == id) return 1;
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN && e->t != E_LIST) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_fn(X, EK(e)[i], id)) return 1;
    return 0;
}

static int unevaluated(XP, E *e) { return has_fn(X, e, C_INTEGRATE) || has_fn(X, e, C_INT); }

/* 1: e is certainly not 0 (|e| > 1e-8 at a sample point), 0: undecided */
static int nonzero_at(XP, E *e, uint32_t n, E **v) {
    E **val = salloc(sizeof(E *) * n);
    for (int s = 0; s < 2; s++) {
        for (uint32_t i = 0; i < n; i++) val[i] = mk_frac(7 + 3 * (long)i + 5 * s, 10 + s);
        double d;
        if (to_double(evalf_digits(subst_many(e, n, v, val), 15), &d) && (d > 1e-8 || d < -1e-8)) return 1;
    }
    return 0;
}

/* simplify e and decide e == 0: 1 zero, 0 undecided, -1 certainly not */
static int zero3(XP, E *e, uint32_t n, E **v) {
    e = vsimp(X, e);
    if (is_zero(e)) return 1;
    return nonzero_at(X, e, n, v) ? -1 : 0;
}

static E *antider(XP, E *f, E *x) {
    E *a[2] = {f, x};
    E *r = eval_cmd(C_INTEGRATE, 2, a);
    return unevaluated(X, r) ? NULL : r;
}

/* abs(g) -> +-g when g keeps one sign at sample points of (lo, hi) */
static E *drop_abs(XP, E *e, E *t, double lo, double hi) {
    stack_check();
    if (IFN(e, F_ABS) && !freeof(e, t)) {
        int sg = 0;
        for (int k = 1; k < 16; k++) {
            double d;
            E *p = evalf_digits(subst(K0(e), t, evalf_digits(mk_frac((long)(1000 * (lo + (hi - lo) * k / 16.0)), 1000), 15)), 15);
            if (!to_double(p, &d)) return e;
            int s = d > 1e-12 ? 1 : d < -1e-12 ? -1 : 0;
            if (s && sg && s != sg) return e;
            if (s) sg = s;
        }
        if (sg) return sg > 0 ? K0(e) : s_neg(K0(e));
        return e;
    }
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN) return e;
    E **k = salloc(sizeof(E *) * e->n);
    int ch = 0;
    for (uint32_t i = 0; i < e->n; i++) k[i] = drop_abs(X, EK(e)[i], t, lo, hi), ch |= k[i] != EK(e)[i];
    return ch ? X->rebuild(e, k) : e;
}

/* int_a^b f dt with the factors free of t pulled out; NULL if not exact */
static E *dint(XP, E *f, E *t, E *a, E *b) {
    double lo, hi;
    f = vsimp(X, f);
    if (to_double(evalf_digits(a, 15), &lo) && to_double(evalf_digits(b, 15), &hi) && lo < hi) f = drop_abs(X, f, t, lo, hi);
    E *c = K->c1, *g = f;
    if (f->t == E_MUL) {
        E **in = salloc(sizeof(E *) * f->n), **out = salloc(sizeof(E *) * f->n);
        uint32_t ni = 0, no = 0;
        for (uint32_t i = 0; i < f->n; i++) {
            if (freeof(EK(f)[i], t)) out[no++] = EK(f)[i];
            else in[ni++] = EK(f)[i];
        }
        c = s_mul(no, out), g = s_mul(ni, in);
    }
    E *args[4] = {g, t, a, b};
    E *r = eval_cmd(C_INTEGRATE, 4, args);
    if (unevaluated(X, r)) return NULL;
    return s_mul2(c, r);
}

/* sqrt(N) with even powers of the factored radicand taken out */
static E *sqrt_nice(XP, E *N) {
    N = eval_cmd(C_TRIGSIMP, 1, &N);
    N = factor_expr(vsimp(X, N));
    uint32_t m = N->t == E_MUL ? N->n : 1;
    E **f = N->t == E_MUL ? EK(N) : &N, **out = salloc(sizeof(E *) * (m + 1)), **in = salloc(sizeof(E *) * m);
    uint32_t no = 0, ni = 0;
    for (uint32_t i = 0; i < m; i++) {
        long k;
        E *b = f[i];
        if (b->t == E_POW && num_is_small_int(K1(b), &k) && k > 0 && k % 2 == 0) {
            E *r = K0(b);
            if (k / 2 % 2) r = X->sign_of(r) > 0 ? r : s_fn1(F_ABS, r);
            out[no++] = s_powi(r, k / 2);
        } else if (is_num(b) || X->sign_of(b) > 0) {
            out[no++] = s_sqrt(b);
        } else in[ni++] = b;
    }
    out[no++] = s_sqrt(s_mul(ni, in));
    return s_mul(no, out);
}

/* ---------------------------------------------------------- potentials --- */
static E *potential(XP, E *F, E *V, E *c) {
    uint32_t n;
    E **v = vars_of(X, V, &n), **h = salloc(sizeof(E *) * n), **G = salloc(sizeof(E *) * n);
    coords(X, c, n, v, h);
    field(X, F, n, "potential: the field needs one component per variable");
    for (uint32_t i = 0; i < n; i++) G[i] = s_mul2(h[i], EK(F)[i]);
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t j = i + 1; j < n; j++) {
            int z = zero3(X, s_sub(diff(G[i], v[j]), diff(G[j], v[i])), n, v);
            if (z < 0) cas_throw(CAS_ERR_DOMAIN, "potential: the field is not conservative (its curl is not 0)");
            if (!z) return NULL;
        }
    E *phi = K->c0;
    for (uint32_t i = 0; i < n; i++) {
        E *R = vsimp(X, s_sub(G[i], diff(phi, v[i])));
        for (uint32_t j = 0; j < i; j++)
            if (!freeof(R, v[j])) return NULL;
        if (is_zero(R)) continue;
        E *I = antider(X, R, v[i]);
        if (!I) return NULL;
        phi = s_add2(phi, I);
    }
    for (uint32_t i = 0; i < n; i++)
        if (zero3(X, s_sub(diff(phi, v[i]), G[i]), n, v) != 1) return NULL;
    return vsimp(X, phi);
}

static E *vpotential(XP, E *B, E *V) {
    uint32_t n;
    E **v = vars_of(X, V, &n), *h[3];
    if (n != 3) cas_throw(CAS_ERR_DOMAIN, "vpotential: three variables");
    coords(X, NULL, n, v, h);
    field(X, B, 3, "vpotential: the field needs three components");
    E **b = EK(B);
    int z = zero3(X, s_add(3, (E *[]){diff(b[0], v[0]), diff(b[1], v[1]), diff(b[2], v[2])}), 3, v);
    if (z < 0) cas_throw(CAS_ERR_DOMAIN, "vpotential: the field is not solenoidal (its divergence is not 0)");
    if (!z) return NULL;
    E *Ax = is_zero(b[1]) ? K->c0 : antider(X, b[1], v[2]), *Ay = is_zero(b[0]) ? K->c0 : antider(X, b[0], v[2]);
    if (!Ax || !Ay) return NULL;
    Ay = s_neg(Ay);
    E *R = vsimp(X, s_sub(b[2], s_sub(diff(Ay, v[0]), diff(Ax, v[1]))));
    if (!freeof(R, v[2])) return NULL;
    if (!is_zero(R)) {
        E *I = antider(X, R, v[0]);
        if (!I) return NULL;
        Ay = s_add2(Ay, I);
    }
    E *A = s_list(3, (E *[]){vsimp(X, Ax), vsimp(X, Ay), K->c0}), *C = curl_cs(X, A, 3, v, h);
    for (int i = 0; i < 3; i++)
        if (zero3(X, s_sub(EK(C)[i], b[i]), 3, v) != 1) return NULL;
    return A;
}

/* ----------------------------------------------- line and surface integrals */
static E *dot(XP, uint32_t n, E **a, E **b) {
    E **t = salloc(sizeof(E *) * n);
    for (uint32_t i = 0; i < n; i++) t[i] = s_mul2(a[i], b[i]);
    return s_add(n, t);
}

static E *lineint(XP, E *F, E *V, E *C, E *t, E *a, E *b, int flux2) {
    uint32_t n;
    E **v = vars_of(X, V, &n);
    if (C->t != E_LIST || C->n != n) cas_throw(CAS_ERR_DOMAIN, "lineint: the curve needs one coordinate per variable");
    if (t->t != E_SYM) cas_throw(CAS_ERR_DOMAIN, "lineint: expected the curve parameter");
    E **d = salloc(sizeof(E *) * n), *g;
    for (uint32_t i = 0; i < n; i++) d[i] = diff(EK(C)[i], t);
    E *Fc = subst_many(F, n, v, EK(C));
    if (flux2) {
        if (n != 2) cas_throw(CAS_ERR_DOMAIN, "flux: a plane curve has two coordinates");
        field(X, Fc, 2, "flux: the field needs two components");
        g = s_sub(s_mul2(EK(Fc)[0], d[1]), s_mul2(EK(Fc)[1], d[0]));
    } else if (F->t == E_LIST) {
        field(X, Fc, n, "lineint: the field needs one component per variable");
        g = dot(X, n, EK(Fc), d);
    } else g = s_mul2(Fc, sqrt_nice(X, dot(X, n, d, d)));
    return dint(X, g, t, a, b);
}

static E *surface(XP, E *F, uint32_t na, E **A, int isflux) {
    uint32_t n;
    E **v = vars_of(X, A[1], &n), *S = A[2], *u = A[3], *w = A[6];
    if (na == 6 && isflux) return lineint(X, F, A[1], S, A[3], A[4], A[5], 1);
    if (na != 9) cas_throw(CAS_ERR_DOMAIN, "wrong number of arguments");
    if (n != 3 || S->t != E_LIST || S->n != 3) cas_throw(CAS_ERR_DOMAIN, "surface: three variables and [x(u,v), y(u,v), z(u,v)]");
    if (u->t != E_SYM || w->t != E_SYM) cas_throw(CAS_ERR_DOMAIN, "surface: expected the parameters");
    E *ru[3], *rv[3], *N[3];
    for (int i = 0; i < 3; i++) ru[i] = diff(EK(S)[i], u), rv[i] = diff(EK(S)[i], w);
    for (int i = 0; i < 3; i++) {
        int j = (i + 1) % 3, k = (i + 2) % 3;
        N[i] = s_sub(s_mul2(ru[j], rv[k]), s_mul2(ru[k], rv[j]));
    }
    E *Fs = subst_many(F, 3, v, EK(S)), *g;
    if (isflux) {
        field(X, Fs, 3, "flux: the field needs three components");
        g = dot(X, 3, EK(Fs), N);
    } else g = s_mul2(Fs, sqrt_nice(X, dot(X, 3, N, N)));
    E *in = dint(X, g, u, A[4], A[5]);
    return in ? dint(X, in, w, A[7], A[8]) : NULL;
}

/* ------------------------------------------------------------ commands --- */
enum { V_GRAD, V_DIV, V_CURL, V_LAP, V_POT, V_VPOT, V_LINE, V_SURF, V_FLUX };
static const casx_cmd_t vtab[] = {
    {"grad", 2, 3},      {"divergence", 2, 3}, {"curl", 2, 3},    {"laplacian", 2, 3}, {"potential", 2, 3},
    {"vpotential", 2, 2}, {"lineint", 6, 6},    {"surfint", 9, 9}, {"flux", 6, 9},
};

static E *vcmd(XP, int op, uint32_t n, E **a) {
    uint32_t nv;
    E *c = n > 2 ? a[2] : NULL, **v, **h;
    switch (op) {
    case V_GRAD: case V_DIV: case V_CURL: case V_LAP: {
        v = vars_of(X, a[1], &nv), h = salloc(sizeof(E *) * nv);
        int cs = coords(X, c, nv, v, h);
        if (op == V_GRAD) return grad_cs(X, a[0], nv, v, h);
        if (op == V_LAP) return lap_cs(X, a[0], nv, v, h, cs);
        field(X, a[0], nv, "the field needs one component per variable");
        if (op == V_DIV) return div_cs(X, a[0], nv, v, h);
        if (nv != 2 && nv != 3) cas_throw(CAS_ERR_DOMAIN, "curl: 2 or 3 components");
        return curl_cs(X, a[0], nv, v, h);
    }
    case V_POT: return potential(X, a[0], a[1], c);
    case V_VPOT: return vpotential(X, a[0], a[1]);
    case V_LINE: return lineint(X, a[0], a[1], a[2], a[3], a[4], a[5], 0);
    case V_SURF: return surface(X, a[0], n, a, 0);
    case V_FLUX: return surface(X, a[0], n, a, 1);
    }
    return NULL;
}

int casx_vec_register(XP) { return X->reg("casx", vtab, (int)(sizeof vtab / sizeof vtab[0]), vcmd); }
