/*
 * ellip.c -- incomplete elliptic integrals of the casx module (Maxima / SymPy conventions,
 * parameter m = k^2):
 *   elliptic_f(phi, m)     F = int_0^phi dt/sqrt(1 - m sin^2 t)
 *   elliptic_e(phi, m)     E = int_0^phi sqrt(1 - m sin^2 t) dt
 *   elliptic_pi(n, phi, m) Pi = int_0^phi dt/((1 - n sin^2 t) sqrt(1 - m sin^2 t))
 * d/dphi of each is its integrand (n and m constant; otherwise no derivative).  Values from
 * Carlson's symmetric forms (duplication algorithm, complex arithmetic):
 *   F = s RF(c^2, D, 1),  E = F - m s^3 RD(c^2, D, 1)/3,  Pi = F + n s^3 RJ(c^2, D, 1, 1 - n s^2)/3
 * (s = sin phi, c = cos phi, D = 1 - m s^2) for |Re phi| <= pi/2; a real phi outside is reduced by
 * F(phi + k pi) = F(phi) + 2 k K(m) (also E, Pi) when m < 1.  No value when an argument of RF,
 * RD, RJ lies on the negative real axis (the branch would be a guess).
 * Complete integrals (one argument): elliptic_k(m) = F(pi/2, m), elliptic_e(m) = E(pi/2, m),
 * with dK/dm = E/(2m(1-m)) - K/(2m), dE/dm = (E - K)/(2m).
 * Special values: F(0, m) = E(0, m) = Pi(n, 0, m) = 0, F(phi, 0) = E(phi, 0) = phi,
 * Pi(0, phi, m) = F(phi, m); for real |phi| <= pi/2: E(phi, 1) = sin(phi),
 * F(phi, 1) = atanh(sin(phi)) (+-inf at +-pi/2); K(0) = E(0) = pi/2, K(1) = inf, E(1) = 1,
 * K(1/2) = gamma(1/4)^2/(4 sqrt(pi)), E(1/2) = gamma(1/4)^2/(8 sqrt(pi)) + pi^(3/2)/gamma(1/4)^2.
 */
#include "cx.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

static const char enames[] = "elliptic_f\0elliptic_e\0elliptic_pi\0elliptic_k\0"; /* no pointer table (XIP) */

/* 0 F, 1 E, 2 Pi (incomplete); 3 K(m), 4 E(m) (complete); -1 other */
int el_which(XP, E *e) {
    if (e->t != E_FN) return -1;
    const char *s = sym_name(e->s), *a = enames;
    for (int w = 0; w < 4; w++) {
        const char *b = s, *p = a;
        while (*p && *p == *b) p++, b++;
        if (!*p && !*b) {
            if (w == 3) return e->n == 1 ? 3 : -1;
            if (w == 1 && e->n == 1) return 4;
            return e->n == (uint32_t)(w == 2 ? 3 : 2) ? w : -1;
        }
        while (*a) a++;
        a++;
    }
    return -1;
}

static E *el_fn(XP, const char *name, uint32_t n, E *a, E *b) {
    E *v[2] = {a, b};
    return mk_fn(mk_sym_name(name)->s, n, v);
}

static cx csq(XP, cx z) { return cf(X, F_SQRT, z); }
static double cmax3(cx a, cx b, cx c) {
    double x = cabs2(a), y = cabs2(b), z = cabs2(c);
    return x > y ? (x > z ? x : z) : (y > z ? y : z);
}
static int on_cut(cx z) { return z.im == 0 && z.re < 0; }

static cx rf(XP, cx x, cx y, cx z) {
    for (int it = 0; it < 60; it++) {
        cx sx = csq(X, x), sy = csq(X, y), sz = csq(X, z);
        cx l = cadd(cmul(sx, cadd(sy, sz)), cmul(sy, sz));
        x = cscale(cadd(x, l), 0.25), y = cscale(cadd(y, l), 0.25), z = cscale(cadd(z, l), 0.25);
        cx a = cscale(cadd(cadd(x, y), z), 1.0 / 3);
        cx dx = cdivx(csub(a, x), a), dy = cdivx(csub(a, y), a), dz = cdivx(csub(a, z), a);
        if (cmax3(dx, dy, dz) < 0.0025 * 0.0025 || it == 59) {
            cx e2 = csub(cmul(dx, dy), cmul(dz, dz)), e3 = cmul(cmul(dx, dy), dz);
            cx t = cadd(cmk(1, 0), cadd(cmul(csub(cscale(e2, 1.0 / 24), cadd(cmk(0.1, 0), cscale(e3, 3.0 / 44))), e2),
                                       cscale(e3, 1.0 / 14)));
            return cdivx(t, csq(X, a));
        }
    }
    return cmk(0.0 / 0.0, 0);
}

static cx rd(XP, cx x, cx y, cx z) {
    cx sum = cmk(0, 0);
    double fac = 1;
    for (int it = 0; it < 60; it++) {
        cx sx = csq(X, x), sy = csq(X, y), sz = csq(X, z);
        cx l = cadd(cmul(sx, cadd(sy, sz)), cmul(sy, sz));
        sum = cadd(sum, cdivx(cmk(fac, 0), cmul(sz, cadd(z, l))));
        fac *= 0.25;
        x = cscale(cadd(x, l), 0.25), y = cscale(cadd(y, l), 0.25), z = cscale(cadd(z, l), 0.25);
        cx a = cscale(cadd(cadd(x, y), cscale(z, 3)), 0.2);
        cx dx = cdivx(csub(a, x), a), dy = cdivx(csub(a, y), a), dz = cdivx(csub(a, z), a);
        if (cmax3(dx, dy, dz) < 0.0015 * 0.0015 || it == 59) {
            const double C1 = 3.0 / 14, C2 = 1.0 / 6, C3 = 9.0 / 22, C4 = 3.0 / 26, C5 = 0.25 * C3, C6 = 1.5 * C4;
            cx ea = cmul(dx, dy), eb = cmul(dz, dz), ec = csub(ea, eb), ed = csub(ea, cscale(eb, 6)),
               ee = cadd(ed, cscale(ec, 2));
            /* 1 + ed (-C1 + C5 ed - C6 dz ee) + dz (C2 ee + dz (-C3 ec + dz C4 ea)) */
            cx t1 = cadd(cmk(-C1, 0), csub(cscale(ed, C5), cscale(cmul(dz, ee), C6)));
            cx t2 = cadd(cscale(ee, C2), cmul(dz, cadd(cscale(ec, -C3), cscale(cmul(dz, ea), C4))));
            cx t = cadd(cmk(1, 0), cadd(cmul(ed, t1), cmul(dz, t2)));
            return cadd(cscale(sum, 3), cscale(cdivx(t, cmul(a, csq(X, a))), fac));
        }
    }
    return cmk(0.0 / 0.0, 0);
}

static cx rc(XP, cx x, cx y) {
    for (int it = 0; it < 60; it++) {
        cx l = cadd(cscale(cmul(csq(X, x), csq(X, y)), 2), y);
        x = cscale(cadd(x, l), 0.25), y = cscale(cadd(y, l), 0.25);
        cx a = cscale(cadd(x, cscale(y, 2)), 1.0 / 3), s = cdivx(csub(y, a), a);
        if (cabs2(s) < 0.0012 * 0.0012 || it == 59) {
            cx p = cadd(cmk(0.375, 0), cscale(s, 9.0 / 22));
            p = cadd(cmk(1.0 / 7, 0), cmul(s, p));
            p = cadd(cmk(0.3, 0), cmul(s, p));
            return cdivx(cadd(cmk(1, 0), cmul(cmul(s, s), p)), csq(X, a));
        }
    }
    return cmk(0.0 / 0.0, 0);
}

static cx rj(XP, cx x, cx y, cx z, cx p) {
    cx sum = cmk(0, 0);
    double fac = 1;
    for (int it = 0; it < 60; it++) {
        cx sx = csq(X, x), sy = csq(X, y), sz = csq(X, z);
        cx l = cadd(cmul(sx, cadd(sy, sz)), cmul(sy, sz));
        cx al = cadd(cmul(p, cadd(cadd(sx, sy), sz)), cmul(cmul(sx, sy), sz));
        al = cmul(al, al);
        cx pl = cadd(p, l), be = cmul(p, cmul(pl, pl));
        sum = cadd(sum, cscale(rc(X, al, be), fac));
        fac *= 0.25;
        x = cscale(cadd(x, l), 0.25), y = cscale(cadd(y, l), 0.25), z = cscale(cadd(z, l), 0.25),
        p = cscale(cadd(p, l), 0.25);
        cx a = cscale(cadd(cadd(cadd(x, y), z), cscale(p, 2)), 0.2);
        cx dx = cdivx(csub(a, x), a), dy = cdivx(csub(a, y), a), dz = cdivx(csub(a, z), a), dp = cdivx(csub(a, p), a);
        double mx = cmax3(dx, dy, dz);
        if (cabs2(dp) > mx) mx = cabs2(dp);
        if (mx < 0.0015 * 0.0015 || it == 59) {
            const double C1 = 3.0 / 14, C2 = 1.0 / 3, C3 = 3.0 / 22, C4 = 3.0 / 26, C5 = 0.75 * C3, C6 = 1.5 * C4,
                         C7 = 0.5 * C2, C8 = C3 + C3;
            cx ea = cadd(cmul(dx, cadd(dy, dz)), cmul(dy, dz)), eb = cmul(cmul(dx, dy), dz), ec = cmul(dp, dp);
            cx ed = csub(ea, cscale(ec, 3)), ee = cadd(eb, cscale(cmul(dp, csub(ea, ec)), 2));
            /* 1 + ed (-C1 + C5 ed - C6 ee) + eb (C7 + dp (-C8 + dp C4)) + dp ea (C2 - dp C3) - C2 dp ec */
            cx t = cmk(1, 0);
            t = cadd(t, cmul(ed, cadd(cmk(-C1, 0), csub(cscale(ed, C5), cscale(ee, C6)))));
            t = cadd(t, cmul(eb, cadd(cmk(C7, 0), cmul(dp, cadd(cmk(-C8, 0), cscale(dp, C4))))));
            t = cadd(t, cmul(cmul(dp, ea), csub(cmk(C2, 0), cscale(dp, C3))));
            t = csub(t, cscale(cmul(dp, ec), C2));
            return cadd(cscale(sum, 3), cscale(cdivx(t, cmul(a, csq(X, a))), fac));
        }
    }
    return cmk(0.0 / 0.0, 0);
}

/* w: 0 F, 1 E, 2 Pi; phi with |Re phi| <= pi/2 */
static int ell0(XP, int w, cx n, cx ph, cx m, cx *out) {
    cx s = cf(X, F_SIN, ph), c = cf(X, F_COS, ph);
    cx c2 = cmul(c, c), s2 = cmul(s, s), D = csub(cmk(1, 0), cmul(m, s2));
    if (on_cut(c2) || (w == 2 && on_cut(D))) return 0; /* F, E: the principal square root matches mpmath */
    if (cabs2(s) == 0) {
        *out = cmk(0, 0);
        return 1;
    }
    cx F = cmul(s, rf(X, c2, D, cmk(1, 0)));
    if (w == 0) *out = F;
    else if (w == 1) *out = csub(F, cscale(cmul(m, cmul(s2, cmul(s, rd(X, c2, D, cmk(1, 0))))), 1.0 / 3));
    else {
        cx P = csub(cmk(1, 0), cmul(n, s2));
        if (on_cut(P) || cabs2(P) == 0) return 0;
        *out = cadd(F, cscale(cmul(n, cmul(s2, cmul(s, rj(X, c2, D, cmk(1, 0), P)))), 1.0 / 3));
    }
    return cfinite(*out);
}

E *el_val(XP, E *e) {
    int w = el_which(X, e);
    if (w < 0) return NULL;
    cx a[3], r;
    const double PI = 3.14159265358979323846;
    for (uint32_t i = 0; i < e->n; i++)
        if (!X->eval_complex(EK(e)[i], &a[i].re, &a[i].im)) return NULL;
    if (w >= 3) { /* complete: phi = pi/2 */
        a[1] = a[0], a[0] = cmk(PI / 2, 0);
        w -= 3;
        if (a[1].im == 0 && a[1].re == 1) return w ? K->c1 : NULL;
    }
    uint32_t na = e->n == 1 ? 2 : e->n;
    cx n = w == 2 ? a[0] : cmk(0, 0), ph = a[na - 2], m = a[na - 1];
    double k = 0;
    if (ph.re > PI / 2 || ph.re < -PI / 2) {
        if (ph.im != 0 || m.im != 0 || !(m.re < 1) || n.im != 0 || !(n.re < 1)) return NULL;
        k = (double)(long)(ph.re / PI + (ph.re > 0 ? 0.5 : -0.5));
        ph.re -= k * PI;
    }
    if (!ell0(X, w, n, ph, m, &r)) return NULL;
    if (k != 0) {
        cx full;
        if (!ell0(X, w, n, cmk(PI / 2, 0), m, &full)) return NULL;
        r = cadd(r, cscale(full, 2 * k));
    }
    if (ph.im == 0 && m.im == 0 && m.re <= 1 && n.im == 0 && (w != 2 || n.re < 1)) r.im = 0;
    return cx_e(X, r);
}

E *el_diff(XP, E *e, E *x) {
    int w = el_which(X, e);
    if (w < 0) return NULL;
    if (w >= 3) { /* complete integrals: d/dm */
        E *m = K0(e), *dm = diff(m, x);
        if (is_zero(dm)) return K->c0;
        E *Kf = el_fn(X, "elliptic_k", 1, m, NULL), *Ef = el_fn(X, "elliptic_e", 1, m, NULL), *tm = s_mul2(K->c2, m);
        E *d = w == 3 ? s_sub(s_div(Ef, s_mul2(tm, s_sub(K->c1, m))), s_div(Kf, tm)) : s_div(s_sub(Ef, Kf), tm);
        return s_mul2(d, dm);
    }
    E *ph = EK(e)[e->n - 2], *m = EK(e)[e->n - 1];
    if (!freeof(m, x) || (w == 2 && !freeof(K0(e), x))) return NULL;
    E *dph = diff(ph, x);
    if (is_zero(dph)) return K->c0;
    E *s2 = s_powi(s_fn1(F_SIN, ph), 2), *D = s_sqrt(s_sub(K->c1, s_mul2(m, s2))), *d;
    if (w == 0) d = s_inv(D);
    else if (w == 1) d = D;
    else d = s_inv(s_mul2(s_sub(K->c1, s_mul2(K0(e), s2)), D));
    return s_mul2(d, dph);
}

E *el_simp(XP, E *e) {
    int w = el_which(X, e);
    if (w < 0) return NULL;
    int allnum0 = 1, flt0 = 0;
    for (uint32_t i = 0; i < e->n; i++) allnum0 &= is_const_expr(EK(e)[i]), flt0 |= has_float(EK(e)[i]);
    if (w >= 3) {
        E *m = K0(e);
        if (allnum0 && flt0) return el_val(X, e);
        if (is_zero(m)) return s_mul2(K->chalf, K->pi_);
        if (expr_eq(m, K->c1)) return w == 3 ? K->inf_ : K->c1;
        if (expr_eq(m, K->chalf)) {
            E *g2 = s_powi(s_fn1(F_GAMMA, mk_frac(1, 4)), 2), *sp = s_sqrt(K->pi_);
            E *k = s_div(g2, s_mul2(mk_int(4), sp));
            return w == 3 ? k : s_add2(s_div(g2, s_mul2(mk_int(8), sp)), s_div(s_mul2(K->pi_, sp), g2));
        }
        return NULL;
    }
    E *ph = EK(e)[e->n - 2], *m = EK(e)[e->n - 1];
    int allnum = 1, flt = 0;
    for (uint32_t i = 0; i < e->n; i++) allnum &= is_const_expr(EK(e)[i]), flt |= has_float(EK(e)[i]);
    if (allnum && flt) {
        E *v = el_val(X, e);
        if (v) return v;
    }
    if (is_zero(ph)) return K->c0;
    if (w < 2 && is_zero(m)) return ph;
    if (w < 2 && expr_eq(m, K->c1) && is_const_expr(ph) && !flt) { /* real |phi| <= pi/2 */
        double re, im;
        if (X->eval_complex(ph, &re, &im) && im == 0 && __builtin_fabs(re) <= 1.5707963267948966 * (1 + 1e-15)) {
            if (w == 1) return s_fn1(F_SIN, ph);
            if (__builtin_fabs(re) < 1.5707963267948966 * (1 - 1e-12)) return s_fn1(F_ATANH, s_fn1(F_SIN, ph));
            return re > 0 ? K->inf_ : s_neg(K->inf_);
        }
    }
    if (w == 2 && is_zero(K0(e))) return s_fn2(mk_sym_name("elliptic_f")->s, ph, m);
    return NULL;
}
