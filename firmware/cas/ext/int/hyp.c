/*
 * hyp.c -- the upper incomplete gamma function and the Gauss hypergeometric function of the
 * casx module:
 *   uppergamma(a, z)       Gamma(a, z) = int_z^inf t^(a-1) e^(-t) dt;  d/dz = -z^(a-1) e^(-z).
 *       Special values: Gamma(a, 0) = gamma(a); Gamma(1, z) = e^(-z); Gamma(n, z) =
 *       (n-1)! e^(-z) sum_(k<n) z^k/k! (integer 1 <= n <= 20); Gamma(1/2, z) = sqrt(pi) erfc(sqrt z);
 *       Gamma(0, z) = expint(1, z).  Values (real a, complex z): the series of the lower
 *       function, gamma(a, z) = z^a e^(-z) sum z^k/(a (a+1) ... (a+k)), and Gamma(a) - gamma(a, z)
 *       for |z| < 1.5 + |a| or Re z <= 0, else the continued fraction (Lentz); a = 0, -1, -2, ...
 *       from E_1: Gamma(-n, z) = (-1)^n/n! (E_1(z) - e^(-z) sum_(k<n) (-1)^k k!/z^(k+1)).
 *   hyp2f1(a, b, c, z)     2F1(a, b; c; z);  d/dz = a b/c 2F1(a+1, b+1; c+1; z).
 *       Special values: z = 0 or a = 0 or b = 0: 1; a or b a negative integer >= -20: the
 *       polynomial.  Values (real a, b, c; complex z): the series in whichever of z, z/(z-1)
 *       (Pfaff: (1-z)^(-a) 2F1(a, c-b; c; z/(z-1))), 1/z and 1/(1-z) (the connection formulas,
 *       b - a not an integer) is smallest, if it is at most 0.9 in modulus.
 */
#include "cx.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

#define EG 0.57721566490153286061

static const char hnames[] = "uppergamma\0hyp2f1\0";

int hy_which(XP, E *e) {
    if (e->t != E_FN) return -1;
    const char *s = sym_name(e->s), *a = hnames;
    for (int w = 0; w < 2; w++) {
        const char *b = s, *p = a;
        while (*p && *p == *b) p++, b++;
        if (!*p && !*b) return e->n == (uint32_t)(w ? 4 : 2) ? w : -1;
        while (*a) a++;
        a++;
    }
    return -1;
}

static double rgam(XP, double v) { /* Gamma(v), 0 at the poles */
    cx r;
    if (v <= 0 && (double)(long)v == v) return 0;
    if (!cfn(X, F_GAMMA, cmk(v, 0), &r) || !cfinite(r)) return 0;
    return r.re;
}
static cx cpw(XP, cx z, double a) { /* z^a, principal */
    if (a == 0) return cmk(1, 0);
    if (cabs2(z) == 0) return cmk(0, 0);
    return cf(X, F_EXP, cscale(cf(X, F_LN, z), a));
}

/* E_1 by series or continued fraction (as in sfn.c, n = 1) */
static int e1(XP, cx z, cx *out) {
    double a2 = cabs2(z);
    if (a2 == 0) return 0;
    if (a2 <= 1 || (z.re <= 0 && a2 <= 1600)) {
        cx mz = cscale(z, -1), p = mz, s = cmk(0, 0);
        for (long k = 1; k < 600; k++) { /* -gamma - ln z - sum (-z)^k/(k k!) */
            cx t = cscale(p, 1.0 / (double)k);
            s = cadd(s, t);
            if (cabs2(t) < 1e-36 * (cabs2(s) + 1e-300)) break;
            p = cscale(cmul(p, mz), 1.0 / (double)(k + 1));
        }
        *out = csub(cmk(-EG, 0), cadd(cf(X, F_LN, z), s));
        return cfinite(*out);
    }
    if (z.re <= 0) return 0;
    const double tiny = 1e-300;
    cx b = cadd(z, cmk(1, 0)), c = cmk(1 / tiny, 0), d = cdivx(cmk(1, 0), b), h = d;
    for (long i = 1; i < 5000; i++) {
        double an = -(double)i * (double)i;
        b = cadd(b, cmk(2, 0));
        d = cadd(cscale(d, an), b);
        if (cabs2(d) < tiny) d = cmk(tiny, 0);
        d = cdivx(cmk(1, 0), d);
        c = cadd(b, cdivx(cmk(an, 0), c));
        if (cabs2(c) < tiny) c = cmk(tiny, 0);
        cx del = cmul(c, d);
        h = cmul(h, del);
        if (cabs2(csub(del, cmk(1, 0))) < 1e-34) {
            *out = cmul(h, cf(X, F_EXP, cscale(z, -1)));
            return cfinite(*out);
        }
    }
    return 0;
}

static int ugam(XP, double a, cx z, cx *out) {
    double a2 = cabs2(z);
    if (a2 == 0) {
        double g = rgam(X, a);
        if (!(a > 0) || g == 0) return 0;
        *out = cmk(g, 0);
        return 1;
    }
    if (a <= 0 && (double)(long)a == a && a > -30) { /* Gamma(-n, z) from E_1 */
        long n = (long)-a;
        cx E, s = cmk(0, 0), zp = z, emz = cf(X, F_EXP, cscale(z, -1));
        if (!e1(X, z, &E)) return 0;
        double f = 1;
        for (long k = 0; k < n; k++) { /* (-1)^k k!/z^(k+1) */
            s = cadd(s, cscale(cdivx(cmk(1, 0), zp), (k & 1) ? -f : f));
            zp = cmul(zp, z), f *= (double)(k + 1);
        }
        double nf = 1;
        for (long k = 2; k <= n; k++) nf *= (double)k;
        *out = cscale(csub(E, cmul(emz, s)), ((n & 1) ? -1.0 : 1.0) / nf);
        return cfinite(*out);
    }
    if (a2 < (1.5 + (a > 0 ? a : -a)) * (1.5 + (a > 0 ? a : -a)) || z.re <= 0) {
        if (a2 > 1600) return 0;
        cx t = cmk(1.0 / a, 0), s = t; /* sum z^k/(a (a+1) ... (a+k)) */
        for (int k = 1; k < 2000; k++) {
            t = cscale(cmul(t, z), 1.0 / (a + k));
            s = cadd(s, t);
            if (cabs2(t) < 1e-36 * (cabs2(s) + 1e-300)) break;
        }
        cx low = cmul(cmul(cpw(X, z, a), cf(X, F_EXP, cscale(z, -1))), s);
        double g = rgam(X, a);
        if (g == 0) return 0;
        *out = csub(cmk(g, 0), low);
        return cfinite(*out);
    }
    /* continued fraction: Gamma(a, z) = e^(-z) z^a / (z + 1 - a - 1 (1-a)/(z + 3 - a - ...)) */
    const double tiny = 1e-300;
    cx b = cadd(z, cmk(1 - a, 0)), c = cmk(1 / tiny, 0), d = cdivx(cmk(1, 0), b), h = d;
    for (int i = 1; i < 5000; i++) {
        if (!(i & 63)) poll_intr();
        double an = -i * (i - a);
        b = cadd(b, cmk(2, 0));
        d = cadd(cscale(d, an), b);
        if (cabs2(d) < tiny) d = cmk(tiny, 0);
        d = cdivx(cmk(1, 0), d);
        c = cadd(b, cdivx(cmk(an, 0), c));
        if (cabs2(c) < tiny) c = cmk(tiny, 0);
        cx del = cmul(c, d);
        h = cmul(h, del);
        if (cabs2(csub(del, cmk(1, 0))) < 1e-34) {
            *out = cmul(cmul(cf(X, F_EXP, cscale(z, -1)), cpw(X, z, a)), h);
            return cfinite(*out);
        }
    }
    return 0;
}

/* the series of 2F1, |z| <= 0.9 */
static int f21s(XP, double a, double b, double c, cx z, cx *out) {
    if (cabs2(z) > 0.81) return 0;
    if (c <= 0 && (double)(long)c == c) return 0;
    cx t = cmk(1, 0), s = t;
    for (int k = 0; k < 3000; k++) {
        if (!(k & 255)) poll_intr();
        double f = (a + k) * (b + k) / ((c + k) * (k + 1.0));
        if (f == 0) { /* a terminating series */
            *out = s;
            return cfinite(s);
        }
        t = cscale(cmul(t, z), f);
        s = cadd(s, t);
        if (cabs2(t) < 1e-36 * (cabs2(s) + 1e-300)) {
            *out = s;
            return cfinite(s);
        }
    }
    return 0;
}

/* 1/z: Gamma(c)Gamma(b-a)/(Gamma(b)Gamma(c-a)) (-z)^(-a) 2F1(a, a-c+1; a-b+1; 1/z) + (a <-> b) */
static int f21inv(XP, double a, double b, double c, cx z, cx *out) {
    double ba = b - a;
    if ((double)(long)ba == ba) return 0;
    double gc = rgam(X, c), g1 = rgam(X, ba), g2 = rgam(X, -ba), gb = rgam(X, b), gca = rgam(X, c - a),
           ga = rgam(X, a), gcb = rgam(X, c - b);
    if (gc == 0 || g1 == 0 || g2 == 0) return 0;
    cx iz = cdivx(cmk(1, 0), z), mz = cscale(z, -1), s1, s2;
    cx t1 = cmk(0, 0), t2 = cmk(0, 0);
    if (gb != 0 && gca != 0) { /* 1/Gamma at a pole is 0 */
        if (!f21s(X, a, a - c + 1, a - b + 1, iz, &s1)) return 0;
        t1 = cscale(cmul(cpw(X, mz, -a), s1), gc * g1 / (gb * gca));
    }
    if (ga != 0 && gcb != 0) {
        if (!f21s(X, b, b - c + 1, b - a + 1, iz, &s2)) return 0;
        t2 = cscale(cmul(cpw(X, mz, -b), s2), gc * g2 / (ga * gcb));
    }
    *out = cadd(t1, t2);
    return cfinite(*out);
}

static int f21(XP, double a, double b, double c, cx z, cx *out) {
    cx one = cmk(1, 0), w = cdivx(z, csub(z, one));
    double az = cabs2(z), aw = cabs2(w), ai = az > 0 ? 1 / az : 1e300, aiw = aw > 0 ? 1 / aw : 1e300;
    double best = az;
    int which = 0;
    if (aw < best) best = aw, which = 1;
    if (ai < best) best = ai, which = 2;
    if (aiw < best) best = aiw, which = 3;
    if (best > 0.81) return 0;
    switch (which) {
    case 0: return f21s(X, a, b, c, z, out);
    case 1: case 3: { /* Pfaff: (1 - z)^(-a) 2F1(a, c - b; c; w) */
        cx s;
        if (which == 1 ? !f21s(X, a, c - b, c, w, &s) : !f21inv(X, a, c - b, c, w, &s)) return 0;
        *out = cmul(cpw(X, csub(one, z), -a), s);
        return cfinite(*out);
    }
    default: return f21inv(X, a, b, c, z, out);
    }
}

E *hy_val(XP, E *e) {
    int w = hy_which(X, e);
    if (w < 0) return NULL;
    cx v[4], r;
    for (uint32_t i = 0; i < e->n; i++)
        if (!X->eval_complex(EK(e)[i], &v[i].re, &v[i].im)) return NULL;
    int ok;
    if (w == 0) {
        if (v[0].im != 0) return NULL;
        ok = ugam(X, v[0].re, v[1], &r);
        if (ok && v[1].im == 0 && v[1].re > 0) r.im = 0;
    } else {
        if (v[0].im != 0 || v[1].im != 0 || v[2].im != 0) return NULL;
        ok = f21(X, v[0].re, v[1].re, v[2].re, v[3], &r);
        if (ok && v[3].im == 0 && v[3].re < 1) r.im = 0;
    }
    if (!ok || !cfinite(r)) return NULL;
    return cx_e(X, r);
}

E *hy_diff(XP, E *e, E *x) {
    int w = hy_which(X, e);
    if (w < 0) return NULL;
    E *z = EK(e)[e->n - 1], *dz = diff(z, x);
    for (uint32_t i = 0; i + 1 < e->n; i++)
        if (!freeof(EK(e)[i], x)) return NULL;
    if (is_zero(dz)) return K->c0;
    E *a = K0(e), *d;
    if (w == 0) d = s_neg(s_mul2(s_pow(z, s_sub(a, K->c1)), s_exp(s_neg(z))));
    else {
        E *b = EK(e)[1], *c = EK(e)[2];
        E *v[4] = {s_add2(a, K->c1), s_add2(b, K->c1), s_add2(c, K->c1), z};
        E *g = mk_fn(e->s, 4, v);
        g->f = F_SIMP;
        d = s_mul2(s_div(s_mul2(a, b), c), g);
    }
    return s_mul2(d, dz);
}

E *hy_simp(XP, E *e) {
    int w = hy_which(X, e);
    if (w < 0) return NULL;
    int allnum = 1, flt = 0;
    for (uint32_t i = 0; i < e->n; i++) allnum &= is_const_expr(EK(e)[i]), flt |= has_float(EK(e)[i]);
    if (allnum && flt) {
        E *v = hy_val(X, e);
        if (v) return v;
    }
    E *z = EK(e)[e->n - 1], *a = K0(e);
    long n;
    if (w == 0) {
        if (is_zero(z)) return s_fn1(F_GAMMA, a);
        if (num_is_small_int(a, &n) && n >= 1 && n <= 20) {
            E *s = K->c0, *t = K->c1;
            for (long k = 0; k < n; k++) s = s_add2(s, t), t = s_div(s_mul2(t, z), mk_int(k + 1));
            E *f = K->c1;
            for (long k = 2; k < n; k++) f = s_mul2(f, mk_int(k));
            return s_mul(3, (E *[]){f, s_exp(s_neg(z)), s});
        }
        if (is_zero(a)) return s_fn2(mk_sym_name("expint")->s, K->c1, z);
        if (is_rat(a) && expr_eq(a, K->chalf)) return s_mul2(s_sqrt(K->pi_), s_fn1(F_ERFC, s_sqrt(z)));
        return NULL;
    }
    E *b = EK(e)[1], *c = EK(e)[2];
    if (is_zero(z) || is_zero(a) || is_zero(b)) return K->c1;
    for (int j = 0; j < 2; j++) { /* a terminating series */
        E *p = j ? b : a, *q = j ? a : b;
        if (num_is_small_int(p, &n) && n < 0 && n >= -20) {
            E *s = K->c1, *t = K->c1;
            for (long k = 0; k < -n; k++) {
                t = s_mul2(t, s_div(s_mul2(s_add2(p, mk_int(k)), s_add2(q, mk_int(k))),
                                    s_mul2(s_add2(c, mk_int(k)), mk_int(k + 1))));
                s = s_add2(s, s_mul2(t, s_powi(z, k + 1)));
            }
            return s;
        }
    }
    return NULL;
}
