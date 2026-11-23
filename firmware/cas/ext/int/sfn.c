/*
 * sfn.c -- more special functions of the casx module (fnx.c has polylog and the Bessel
 * functions; its slots hand the names below to this file):
 *
 *   lambertw(z)      principal branch W_0 of w e^w = z;  W' = W/(z (1 + W)).
 *                    Values: Halley's iteration from the branch-point series
 *                    -1 + p - p^2/3 + 11/72 p^3 (p = sqrt(2 (e z + 1))), ln(1 + z), or
 *                    L1 - L2 + L2/L1 (L1 = ln z, L2 = ln L1) for |z| > 3.  W(0) = 0, W(e) = 1,
 *                    W(-1/e) = -1, W(-pi/2) = i pi/2, W(x e^x) = x for x >= -1 (a number).
 *   fresnels(z), fresnelc(z)
 *                    S = int_0^z sin(pi t^2/2) dt, C = int_0^z cos(pi t^2/2) dt (odd).
 *                    Values: the power series of C + i S = int_0^z e^(i pi t^2/2) for
 *                    |z| <= 2.5, else C + i S = (1+i)/2 erf((1-i) sqrt(pi) z/2) and
 *                    C - i S = (1-i)/2 erf((1+i) sqrt(pi) z/2), with erfc from its continued
 *                    fraction (Re w > 0; erfc(-w) = 2 - erfc(w)).
 *   Shi(z), Chi(z)   hyperbolic sine and cosine integrals: Shi' = sinh z/z, Chi' = cosh z/z;
 *                    Shi = sum z^(2k+1)/((2k+1) (2k+1)!),
 *                    Chi = euler_gamma + ln z + sum_(k>=1) z^(2k)/(2k (2k)!)
 *                    (|z| <= 40 near the real axis, else |z| <= 10).
 *   expint(n, z)     E_n(z) = int_1^inf e^(-z t) t^(-n) dt for an integer n;  E_n' = -E_(n-1).
 *                    n <= 0 is elementary: E_(-m)(z) = m! e^(-z) sum_(k<=m) z^(k-m-1)/k!;
 *                    E_n(0) = 1/(n-1) for n >= 2.  Values: the series
 *                    E_n = (-z)^(n-1)/(n-1)! (psi(n) - ln z) - sum_(k != n-1) (-z)^k/((k-n+1) k!)
 *                    (|z| <= 1, or Re z <= 0 and |z| <= 40), else the continued fraction
 *                    (Re z > 0).
 *   li(z)            logarithmic integral Ei(ln z);  li' = 1/ln z;  li(e^u) = Ei(u) for real u.
 *                    Values for real z > 0 (z != 1).
 *
 * Every value routine gives up (no value, the call stays symbolic) outside the region where it
 * is accurate to about 1e-12.
 */
#include "cx.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

#define PI 3.14159265358979323846
#define EG 0.57721566490153286061

/* the names, NUL separated (no pointer table: an XIP image has no relocated data) */
static const char names[] = "lambertw\0fresnels\0fresnelc\0Shi\0Chi\0expint\0li\0";
static const uint8_t arity[SF_N] = {1, 1, 1, 1, 1, 2, 1};

int sf_which(XP, E *e) {
    if (e->t != E_FN) return -1;
    const char *s = sym_name(e->s), *a = names;
    for (int w = 0; w < SF_N; w++) {
        const char *b = s, *p = a;
        while (*p && *p == *b) p++, b++;
        if (!*p && !*b) return e->n == arity[w] ? w : -1;
        while (*a) a++;
        a++;
    }
    return -1;
}

static E *mkf(XP, E *proto, uint32_t n, E **a) { return n == 1 ? s_fn1(proto->s, a[0]) : s_fn2(proto->s, a[0], a[1]); }
static E *mkf1(XP, E *proto, E *a) { return mkf(X, proto, 1, &a); }

/* ---- values ---- */

static cx cexpx(XP, cx z) { return cf(X, F_EXP, z); }
static cx clogx(XP, cx z) { return cf(X, F_LN, z); }

static int lambertw_v(XP, cx z, cx *out) {
    if (z.re == 0 && z.im == 0) {
        *out = cmk(0, 0);
        return 1;
    }
    const double E_ = 2.71828182845904523536;
    cx w, zz = cadd(z, cmk(1 / E_, 0));
    if (cabs2(zz) < 1) { /* around the branch point -1/e */
        cx p = cf(X, F_SQRT, cscale(zz, 2 * E_)), p2 = cmul(p, p);
        w = cadd(cadd(cmk(-1, 0), p), cadd(cscale(p2, -1.0 / 3), cscale(cmul(p2, p), 11.0 / 72)));
    } else if (cabs2(z) <= 9) {
        w = clogx(X, cadd(z, cmk(1, 0)));
    } else {
        cx l1 = clogx(X, z), l2 = clogx(X, l1);
        w = cadd(csub(l1, l2), cdivx(l2, l1));
    }
    for (int it = 0; it < 80; it++) {
        poll_intr();
        cx ew = cexpx(X, w), f = csub(cmul(w, ew), z), w1 = cadd(w, cmk(1, 0));
        if (!cfinite(f) || cabs2(w1) == 0) return 0;
        cx den = csub(cmul(ew, w1), cdivx(cmul(cadd(w, cmk(2, 0)), f), cscale(w1, 2)));
        cx dw = cdivx(f, den);
        if (!cfinite(dw)) return 0;
        w = csub(w, dw);
        if (cabs2(dw) <= 1e-32 * (1 + cabs2(w))) break;
    }
    cx r = csub(cmul(w, cexpx(X, w)), z);
    if (!(cabs2(r) <= 1e-20 * (1 + cabs2(z)))) return 0;
    if (z.im == 0 && z.re >= -1 / E_) w.im = 0; /* real there */
    *out = w;
    return 1;
}

/* erfc(w), Re w >= 0, |w| >= 2.5, by Lentz's continued fraction
 * erfc w = e^(-w^2)/sqrt(pi) / (w + (1/2)/(w + 1/(w + (3/2)/(w + ...)))) */
static int erfc_cf(XP, cx w, cx *out) {
    const double tiny = 1e-300;
    cx f = w, C = w, D = cmk(0, 0);
    int ok = 0;
    for (int n = 1; n < 4000; n++) {
        if (!(n & 63)) poll_intr();
        double a = n * 0.5;
        D = cadd(w, cscale(D, a));
        if (cabs2(D) < tiny) D = cmk(tiny, 0);
        D = cdivx(cmk(1, 0), D);
        C = cadd(w, cdivx(cmk(a, 0), C));
        if (cabs2(C) < tiny) C = cmk(tiny, 0);
        cx del = cmul(C, D);
        f = cmul(f, del);
        if (cabs2(csub(del, cmk(1, 0))) < 1e-34) {
            ok = 1;
            break;
        }
    }
    if (!ok) return 0;
    cx e = cexpx(X, cscale(cmul(w, w), -1));
    *out = cdivx(e, cscale(f, 1.7724538509055160273));
    return cfinite(*out);
}

/* erf(w) for |w| >= 2.5 away from the imaginary axis */
static int erf_big(XP, cx w, cx *out) {
    int neg = w.re < 0;
    if (neg) w = cscale(w, -1);
    if (w.re * w.re < 0.09 * cabs2(w)) return 0;
    cx c;
    if (!erfc_cf(X, w, &c)) return 0;
    *out = csub(cmk(1, 0), c);
    if (neg) *out = cscale(*out, -1);
    return 1;
}

/* C(z) (s = 0) or S(z) (s = 1) */
static int fresnel_v(XP, cx z, int s, cx *out) {
    double a2 = cabs2(z);
    if (a2 <= 6.25) {
        /* A_k = (pi/2)^k z^(2k+1)/(k! (2k+1)); C = sum_(k even) (-1)^(k/2) A_k,
         * S = sum_(k odd) (-1)^((k-1)/2) A_k */
        cx z2 = cmul(z, z), p = z, sum = cmk(0, 0);
        for (int k = 0; k < 200; k++) {
            cx t = cscale(p, 1.0 / (2 * k + 1));
            if ((k & 1) == s) {
                sum = (k & 2) ? csub(sum, t) : cadd(sum, t);
                if (k > 4 && cabs2(t) < 1e-36 * (cabs2(sum) + 1e-300)) break;
            }
            p = cscale(cmul(p, z2), PI / 2 / (k + 1));
        }
        *out = sum;
        return 1;
    }
    double c = 0.88622692545275801365; /* sqrt(pi)/2 */
    cx w1 = cscale(cmul(cmk(1, -1), z), c), w2 = cscale(cmul(cmk(1, 1), z), c), e1, e2;
    if (!erf_big(X, w1, &e1) || !erf_big(X, w2, &e2)) return 0;
    cx F = cscale(cmul(cmk(1, 1), e1), 0.5), G = cscale(cmul(cmk(1, -1), e2), 0.5); /* C+iS, C-iS */
    *out = s ? cdivx(csub(F, G), cmk(0, 2)) : cscale(cadd(F, G), 0.5);
    if (z.im == 0) out->im = 0;
    return cfinite(*out);
}

/* Shi (c = 0) or Chi (c = 1) */
static int shichi_v(XP, cx z, int c, cx *out) {
    double a2 = cabs2(z);
    if (a2 == 0) return c ? 0 : (*out = cmk(0, 0), 1);
    int nearreal = z.re * z.re >= 4 * z.im * z.im;
    if (a2 > (nearreal ? 1600 : 100)) return 0;
    cx z2 = cmul(z, z), p = c ? z2 : z, sum = cmk(0, 0); /* p = z^m/m! */
    if (c) p = cscale(p, 0.5);
    for (int m = c ? 2 : 1; m < 400; m += 2) {
        cx t = cscale(p, 1.0 / m);
        sum = cadd(sum, t);
        if (m > 4 && cabs2(t) < 1e-36 * (cabs2(sum) + 1e-300)) break;
        p = cscale(cmul(p, z2), 1.0 / ((m + 1.0) * (m + 2.0)));
    }
    if (c) sum = cadd(sum, cadd(cmk(EG, 0), clogx(X, z)));
    *out = sum;
    if (z.im == 0 && (!c || z.re > 0)) out->im = 0;
    return cfinite(*out);
}

static int expint_v(XP, long n, cx z, cx *out) {
    if (n < 0 || n > 200) return 0;
    double a2 = cabs2(z);
    if (n == 0) {
        if (a2 == 0) return 0;
        *out = cdivx(cexpx(X, cscale(z, -1)), z);
        return 1;
    }
    if (a2 == 0) {
        if (n < 2) return 0;
        *out = cmk(1.0 / (n - 1), 0);
        return 1;
    }
    if (a2 <= 1 || (z.re <= 0 && a2 <= 1600)) {
        cx mz = cscale(z, -1), p = cmk(1, 0), sum = cmk(0, 0); /* p = (-z)^k/k! */
        double psi = -EG;
        for (long m = 1; m < n; m++) psi += 1.0 / (double)m;
        for (long k = 0; k < 500; k++) {
            cx t;
            if (k == n - 1) t = cmul(p, csub(cmk(psi, 0), clogx(X, z)));
            else t = cscale(p, -1.0 / (double)(k - n + 1));
            sum = cadd(sum, t);
            if (k > n + 2 && cabs2(t) < 1e-36 * (cabs2(sum) + 1e-300)) break;
            p = cscale(cmul(p, mz), 1.0 / (double)(k + 1));
        }
        *out = sum;
        if (z.im == 0 && z.re > 0) out->im = 0;
        return cfinite(*out);
    }
    if (z.re <= 0) return 0;
    /* continued fraction (modified Lentz), Re z > 0 */
    const double tiny = 1e-300;
    cx b = cadd(z, cmk((double)n, 0)), c = cmk(1 / tiny, 0), d = cdivx(cmk(1, 0), b), h = d;
    int ok = 0;
    for (long i = 1; i < 5000; i++) {
        if (!(i & 63)) poll_intr();
        double an = -(double)i * (double)(n - 1 + i);
        b = cadd(b, cmk(2, 0));
        d = cadd(cscale(d, an), b);
        if (cabs2(d) < tiny) d = cmk(tiny, 0);
        d = cdivx(cmk(1, 0), d);
        c = cadd(b, cdivx(cmk(an, 0), c));
        if (cabs2(c) < tiny) c = cmk(tiny, 0);
        cx del = cmul(c, d);
        h = cmul(h, del);
        if (cabs2(csub(del, cmk(1, 0))) < 1e-34) {
            ok = 1;
            break;
        }
    }
    if (!ok) return 0;
    *out = cmul(h, cexpx(X, cscale(z, -1)));
    if (z.im == 0) out->im = 0;
    return cfinite(*out);
}

E *sf_val(XP, E *e) {
    int w = sf_which(X, e);
    if (w < 0) return NULL;
    cx a[2], r;
    for (uint32_t i = 0; i < e->n; i++)
        if (!X->eval_complex(EK(e)[i], &a[i].re, &a[i].im)) return NULL;
    cx z = a[e->n - 1];
    int ok = 0;
    switch (w) {
    case SF_LAMBERTW: ok = lambertw_v(X, z, &r); break;
    case SF_FRESNELS: case SF_FRESNELC: ok = fresnel_v(X, z, w == SF_FRESNELS, &r); break;
    case SF_SHI: case SF_CHI: ok = shichi_v(X, z, w == SF_CHI, &r); break;
    case SF_EXPINT: {
        long n = (long)a[0].re;
        if (a[0].im != 0 || (double)n != a[0].re) return NULL;
        if (n < 0) { /* elementary */
            E *s = sf_simp(X, e);
            return s && s != e && X->eval_complex(s, &r.re, &r.im) ? cx_e(X, r) : NULL;
        }
        ok = expint_v(X, n, z, &r);
        break;
    }
    case SF_LI:
        if (z.im != 0 || !(z.re > 0) || z.re == 1) return NULL;
        ok = cfn(X, F_EI, cf(X, F_LN, z), &r);
        break;
    }
    if (!ok || !cfinite(r)) return NULL;
    return cx_e(X, r);
}

/* ---- derivatives ---- */

E *sf_diff(XP, E *e, E *x) {
    int w = sf_which(X, e);
    if (w < 0) return NULL;
    E *z = EK(e)[e->n - 1], *dz = diff(z, x), *d;
    if (e->n == 2 && !freeof(K0(e), x)) return NULL; /* d/dn: not known */
    if (is_zero(dz)) return K->c0;
    E *hpz2 = s_mul2(s_div(K->pi_, K->c2), s_mul2(z, z));
    switch (w) {
    case SF_LAMBERTW: d = s_div(e, s_mul2(z, s_add2(K->c1, e))); break;
    case SF_FRESNELS: d = s_fn1(F_SIN, hpz2); break;
    case SF_FRESNELC: d = s_fn1(F_COS, hpz2); break;
    case SF_SHI: d = s_div(s_fn1(F_SINH, z), z); break;
    case SF_CHI: d = s_div(s_fn1(F_COSH, z), z); break;
    case SF_EXPINT: {
        E *v[2] = {s_sub(K0(e), K->c1), z};
        d = s_neg(mkf(X, e, 2, v));
        break;
    }
    case SF_LI: d = s_inv(s_fn1(F_LN, z)); break;
    default: return NULL;
    }
    return s_mul2(d, dz);
}

/* ---- special values ---- */

/* z = c u with a negative number c: -z, else NULL */
static E *negated(XP, E *z) {
    E *c = z->t == E_MUL ? EK(z)[0] : z;
    if ((c->t == E_INT || c->t == E_RAT || c->t == E_FLT) && num_sgn(c) < 0) return s_neg(z);
    return NULL;
}

E *sf_simp(XP, E *e) {
    int w = sf_which(X, e);
    if (w < 0) return NULL;
    E *z = EK(e)[e->n - 1], *mz;
    long n;
    /* a numeric argument with a decimal: the decimal value */
    int allnum = 1, flt = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        E *a = EK(e)[i];
        allnum &= is_const_expr(a);
        flt |= has_float(a);
    }
    if (allnum && flt) {
        E *v = sf_val(X, e);
        if (v) return v;
    }
    switch (w) {
    case SF_LAMBERTW:
        if (is_zero(z)) return K->c0;
        if (ISYM(z, S_E)) return K->c1;
        if (expr_eq(z, s_neg(s_inv(K->e_)))) return K->cm1;
        if (expr_eq(z, s_neg(s_div(K->pi_, K->c2)))) return s_mul2(K->i_, s_div(K->pi_, K->c2));
        if (z->t == E_MUL && z->n == 2) { /* x e^x for a number x >= -1 */
            for (int j = 0; j < 2; j++) {
                E *a = EK(z)[j], *b = EK(z)[1 - j];
                E *ex = b->t == E_POW && ISYM(K0(b), S_E) ? K1(b) : IFN(b, F_EXP) ? K0(b) : NULL;
                if (ex && expr_eq(ex, a) && (a->t == E_INT || a->t == E_RAT) && num_cmp(a, K->cm1) >= 0)
                    return a;
            }
        }
        if (z->t == E_POW && ISYM(K0(z), S_E) && expr_eq(K1(z), K->c1)) return K->c1;
        return NULL;
    case SF_FRESNELS: case SF_FRESNELC: case SF_SHI:
        if (is_zero(z)) return K->c0;
        if (w != SF_SHI && ISYM(z, S_INF)) return K->chalf;
        if ((mz = negated(X, z))) return s_neg(mkf1(X, e, mz));
        return NULL;
    case SF_EXPINT:
        if (!num_is_small_int(K0(e), &n)) return NULL;
        if (n <= 0 && n >= -40) { /* m! e^(-z) sum_(k<=m) z^(k-m-1)/k! */
            long m = -n;
            E *s = K->c0, *f = K->c1;
            for (long k = m; k >= 0; k--) {
                s = s_add2(s, s_mul2(f, s_powi(z, k - m - 1)));
                f = s_mul2(f, mk_int(k));
            }
            return s_mul2(s_exp(s_neg(z)), s);
        }
        if (is_zero(z) && n >= 2) return mk_frac(1, n - 1);
        return NULL;
    case SF_LI:
        if (is_zero(z)) return K->c0;
        if (z->t == E_POW && ISYM(K0(z), S_E) && freeof(K1(z), K->i_)) return s_fn1(F_EI, K1(z));
        if (IFN(z, F_EXP) && freeof(K0(z), K->i_)) return s_fn1(F_EI, K0(z));
        return NULL;
    }
    return NULL;
}

/* f(args) is real whenever its symbols are: 1, else 0 (unknown) */
int sf_real(XP, E *e) {
    int w = sf_which(X, e);
    if (w < 0) return 0;
    E *z = EK(e)[e->n - 1];
    if (!freeof(z, K->i_)) return 0;
    switch (w) {
    case SF_FRESNELS: case SF_FRESNELC: case SF_SHI: return 1;
    case SF_LAMBERTW: return X->sign_of(s_add2(z, s_inv(K->e_))) > 0 || is_zero(z);
    default: return X->sign_of(z) > 0 && (e->n < 2 || freeof(K0(e), K->i_));
    }
}
