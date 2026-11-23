/*
 * fnx.c -- special functions defined by the casx module: they stay symbolic calls (their
 * names are not builtins), and the core asks this file for their derivatives (slot
 * CASX_FN_DIFF, diff.c) and their complex values (slot CASX_FN_CEVAL, numeric.c: approx,
 * plots, the integrator's checks).
 *
 *   polylog(s, z)    d/dz Li_s(z) = Li_(s-1)(z)/z,  Li_1(z) = -ln(1 - z); values for integer s >= 1
 *                    (other real s: the series, |z| < 0.9).
 *                    Values: the power series for |z| <= 1/2; for |z| >= 2 the inversion
 *                    Li_s(z) + (-1)^s Li_s(1/z) = -(2 pi i)^s/s! B_s(1/2 + ln(-z)/(2 pi i));
 *                    in between the series around z = 1 in w = ln z (|w| < 2 pi):
 *                    Li_s(z) = sum_(k != s-1) zeta(s-k) w^k/k! + w^(s-1)/(s-1)! (H_(s-1) - ln(-w))
 *                    (zeta at negative integers -B_(n+1)/(n+1), at integers >= 2 by
 *                    Euler-Maclaurin).
 *   besselj(n, z), bessely(n, z), besseli(n, z), besselk(n, z)
 *                    derivatives  J' = (J_(n-1) - J_(n+1))/2 (also Y),  I' = (I_(n-1) + I_(n+1))/2,
 *                    K' = -(K_(n-1) + K_(n+1))/2  (J_0' = -J_1, I_0' = I_1, K_0' = -K_1);
 *                    values from the ascending series (|z| <= 25): any real order for J and I,
 *                    integer orders for Y and K (the series with digamma terms, DLMF 10.8.1,
 *                    10.31.1).
 */
#include "cx.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

static int is_name(XP, E *e, const char *nm) {
    if (e->t != E_FN) return 0;
    const char *s = sym_name(e->s);
    while (*nm && *s == *nm) s++, nm++;
    return !*nm && !*s;
}

static double bern(XP, long n) {
    double v;
    return to_double(bernoulli(n), &v) ? v : 0;
}

/* zeta(m) for an integer m != 1 */
static double zeta_i(XP, long m) {
    if (m == 0) return -0.5;
    if (m < 0) return (m & 1) ? -bern(X, 1 - m) / (double)(1 - m) : 0;
    double s = 0, N = 10;
    for (int n = 1; n < 10; n++) s += X->pow(n, -(double)m);
    s += X->pow(N, 1.0 - m) / (m - 1) + 0.5 * X->pow(N, -(double)m);
    double fac = 1, rise = m; /* (m)_(2j-1) / (2j)! */
    for (int j = 1; j <= 8; j++) {
        fac *= (2.0 * j - 1) * (2.0 * j);
        s += bern(X, 2 * j) / fac * rise * X->pow(N, -(double)m - 2 * j + 1);
        rise *= (m + 2.0 * j - 1) * (m + 2.0 * j);
    }
    return s;
}

static int polylog_v(XP, long s, cx z, cx *out, int depth) {
    if (s < 1 || s > 40 || depth > 2) return 0;
    if (s == 1) { /* -ln(1 - z) */
        cx l;
        if (!cfn(X, F_LN, csub(cmk(1, 0), z), &l)) return 0;
        *out = cscale(l, -1);
        return 1;
    }
    double a2 = cabs2(z);
    if (a2 <= 0.25) {
        cx p = z, sum = cmk(0, 0);
        for (int k = 1; k < 200; k++) {
            cx t = cscale(p, X->pow(k, -(double)s));
            sum = cadd(sum, t);
            if (cabs2(t) < 1e-36 * (cabs2(sum) + 1e-300)) break;
            p = cmul(p, z);
        }
        *out = sum;
        return 1;
    }
    if (a2 >= 4) { /* inversion */
        cx inv, l, w;
        if (!polylog_v(X, s, cdivx(cmk(1, 0), z), &inv, depth + 1)) return 0;
        if (!cfn(X, F_LN, cscale(z, -1), &l)) return 0;
        cx tpi = cmk(0, 2 * 3.14159265358979323846);
        w = cadd(cmk(0.5, 0), cdivx(l, tpi)); /* B_s(w) = sum C(s,k) B_k w^(s-k) */
        cx B = cmk(0, 0), wp = cmk(1, 0);
        double binom = 1;
        cx *pw = salloc(sizeof(cx) * (size_t)(s + 1));
        for (long k = 0; k <= s; k++) pw[k] = wp, wp = cmul(wp, w);
        for (long k = 0; k <= s; k++) {
            B = cadd(B, cscale(pw[s - k], binom * bern(X, k)));
            binom = binom * (double)(s - k) / (double)(k + 1);
        }
        cx tp = cmk(1, 0);
        double f = 1;
        for (long k = 1; k <= s; k++) tp = cmul(tp, tpi), f *= (double)k;
        cx r = cscale(cdivx(cmul(tp, B), cmk(f, 0)), -1);
        *out = csub(r, (s & 1) ? cscale(inv, -1) : inv);
        return 1;
    }
    /* around z = 1: w = ln z */
    cx w, lw;
    if (!cfn(X, F_LN, z, &w)) return 0;
    cx sum = cmk(0, 0), wk = cmk(1, 0);
    double fk = 1, H = 0;
    for (long j = 1; j < s; j++) H += 1.0 / (double)j;
    for (long k = 0; k < 80; k++) {
        if (k) fk *= (double)k;
        cx t;
        if (k == s - 1) {
            if (cabs2(w) == 0) t = cmk(0, 0);
            else {
                if (!cfn(X, F_LN, cscale(w, -1), &lw)) return 0;
                t = cscale(cmul(wk, csub(cmk(H, 0), lw)), 1 / fk);
            }
        } else
            t = cscale(wk, zeta_i(X, s - k) / fk);
        sum = cadd(sum, t);
        if (k > s + 2 && cabs2(wk) / (fk * fk) < 1e-34 * (cabs2(sum) + 1e-300)) break;
        wk = cmul(wk, w);
    }
    *out = sum;
    return 1;
}

/* ---- Bessel functions ---- */

static double gam(XP, double v) {
    double r;
    return to_double(s_fn1(F_GAMMA, mk_flt(v)), &r) ? r : 0;
}

/* sum_k s1^k (z/2)^(2k+nu) / (k! Gamma(k+nu+1)) with s1 = -1 (J) or 1 (I) */
static int bess_ji(XP, double nu, cx z, int s1, cx *out) {
    if (cabs2(z) > 625) return 0;
    cx h = cscale(z, 0.5), h2 = cmul(h, h), hp;
    if (nu == 0) hp = cmk(1, 0);
    else if (!X->eval_complex(s_pow(cx_e(X, h), mk_flt(nu)), &hp.re, &hp.im)) return 0;
    long ni = (long)nu;
    double g;
    if ((double)ni == nu && ni < 0) { /* J_-n = (-1)^n J_n, I_-n = I_n */
        if (!bess_ji(X, -nu, z, s1, out)) return 0;
        if (s1 < 0 && (ni & 1)) *out = cscale(*out, -1);
        return 1;
    }
    g = gam(X, nu + 1);
    if (g == 0) return 0;
    cx t = cscale(hp, 1 / g), sum = t;
    for (int k = 0; k < 300; k++) {
        t = cscale(cmul(t, h2), (double)s1 / ((k + 1.0) * (k + 1.0 + nu)));
        sum = cadd(sum, t);
        if (cabs2(t) < 1e-36 * (cabs2(sum) + 1e-300)) break;
    }
    *out = sum;
    return 1;
}

/* Y_n (kind 0) and K_n (kind 1), integer n >= 0 */
static int bess_yk(XP, long n, cx z, int kind, cx *out) {
    if (n < 0 || n > 60 || cabs2(z) > 625 || cabs2(z) == 0) return 0;
    const double PI = 3.14159265358979323846, EG = 0.57721566490153286061;
    cx h = cscale(z, 0.5), h2 = cmul(h, h), lh, jn;
    if (!cfn(X, F_LN, h, &lh)) return 0;
    if (!bess_ji(X, (double)n, z, kind ? 1 : -1, &jn)) return 0;
    cx hn = cmk(1, 0);
    for (long i = 0; i < n; i++) hn = cmul(hn, h);
    /* finite sum: sum_(k<n) (n-k-1)!/k! (sign)^k (z/2)^(2k-n) */
    cx fs = cmk(0, 0), hk = cdivx(cmk(1, 0), hn);
    for (long k = 0; k < n; k++) {
        double c = 1;
        for (long i = 1; i <= n - k - 1; i++) c *= (double)i;
        for (long i = 1; i <= k; i++) c /= (double)i;
        fs = cadd(fs, cscale(hk, c * ((kind && (k & 1)) ? -1 : 1)));
        hk = cmul(hk, h2);
    }
    /* digamma series: sum (psi(k+1) + psi(n+k+1)) (sign)^k (z/2)^(2k+n)/(k! (n+k)!) */
    double fn = 1;
    for (long i = 1; i <= n; i++) fn *= (double)i;
    cx t = cscale(hn, 1 / fn), ss = cmk(0, 0);
    double Hk = 0, Hnk = 0;
    for (long i = 1; i <= n; i++) Hnk += 1.0 / (double)i;
    for (long k = 0; k < 300; k++) {
        cx term = cscale(t, (Hk - EG) + (Hnk - EG));
        ss = cadd(ss, term);
        if (k > 3 && cabs2(term) < 1e-36 * (cabs2(ss) + 1e-300)) break;
        t = cscale(cmul(t, h2), (kind ? 1.0 : -1.0) / ((k + 1.0) * (n + k + 1.0)));
        Hk += 1.0 / (k + 1.0), Hnk += 1.0 / (n + k + 1.0);
    }
    if (!kind) {
        *out = cscale(csub(csub(cscale(cmul(jn, lh), 2), fs), ss), 1 / PI);
    } else {
        double sg = (n & 1) ? 1 : -1; /* (-1)^(n+1) */
        *out = cadd(cadd(cscale(cmul(lh, jn), sg), cscale(fs, 0.5)), cscale(ss, -sg * 0.5));
    }
    return 1;
}

/* ---- slots ---- */

enum { FX_POLYLOG, FX_BJ, FX_BY, FX_BI, FX_BK, FX_N };
static int which(XP, E *e) {
    if (is_name(X, e, "polylog")) return FX_POLYLOG;
    if (is_name(X, e, "besselj")) return FX_BJ;
    if (is_name(X, e, "bessely")) return FX_BY;
    if (is_name(X, e, "besseli")) return FX_BI;
    if (is_name(X, e, "besselk")) return FX_BK;
    return -1;
}
int fx_which(XP, E *e) { return which(X, e); }

static E *fn2(XP, E *f, E *a, E *b) {
    E *v[2] = {a, b};
    return mk_fn(f->s, 2, v); /* not auto-simplified further: an unknown function */
}

static E *dslot(XP, uint32_t na, E **A) {
    E *e = A[0], *x = A[1];
    int w = which(X, e);
    if (w < 0) {
        E *r = sf_diff(X, e, x);
        if (!r) r = el_diff(X, e, x);
        return r ? r : hy_diff(X, e, x);
    }
    if (e->n != 2 || !freeof(K0(e), x)) return NULL;
    E *n = K0(e), *z = K1(e), *dz = diff(z, x), *d;
    if (is_zero(dz)) return K->c0;
    if (w == FX_POLYLOG) {
        long s;
        if (!num_is_small_int(n, &s) || s < 1) /* any order: Li_(s-1)(z)/z */
            d = s_div(fn2(X, e, s_sub(n, K->c1), z), z);
        else d = s == 1 ? s_inv(s_sub(K->c1, z)) : s_div(s == 2 ? s_neg(s_fn1(F_LN, s_sub(K->c1, z))) : fn2(X, e, mk_int(s - 1), z), z);
    } else {
        long ni;
        int zero = num_is_small_int(n, &ni) && ni == 0;
        E *lo = fn2(X, e, s_sub(n, K->c1), z), *hi = fn2(X, e, s_add2(n, K->c1), z);
        if (w == FX_BJ || w == FX_BY) d = zero ? s_neg(hi) : s_div(s_sub(lo, hi), K->c2);
        else if (w == FX_BI) d = zero ? hi : s_div(s_add2(lo, hi), K->c2);
        else d = zero ? s_neg(hi) : s_neg(s_div(s_add2(lo, hi), K->c2));
    }
    return s_mul2(d, dz);
}

static int cnum(XP, E *e, cx *z) { return X->eval_complex(e, &z->re, &z->im); }

static E *vslot(XP, uint32_t na, E **A) {
    E *e = A[0];
    int w = which(X, e);
    if (w < 0) {
        E *r = sf_val(X, e);
        if (!r) r = el_val(X, e);
        return r ? r : hy_val(X, e);
    }
    if (e->n != 2) return NULL;
    cx n, z, r;
    if (!cnum(X, K0(e), &n) || !cnum(X, K1(e), &z) || n.im != 0) return NULL;
    long ni = (long)n.re;
    int isint = (double)ni == n.re;
    int ok = 0;
    switch (w) {
    case FX_POLYLOG:
        ok = isint && polylog_v(X, ni, z, &r, 0);
        if (!isint && cabs2(z) < 0.81) { /* sum z^k/k^s */
            cx p = z, sum = cmk(0, 0);
            for (int k = 1; k < 4000; k++) {
                cx t = cscale(p, X->pow(k, -n.re));
                sum = cadd(sum, t);
                if (cabs2(t) < 1e-36 * (cabs2(sum) + 1e-300)) break;
                p = cmul(p, z);
            }
            r = sum, ok = 1;
        }
        if (ok && z.im == 0 && z.re <= 1) r.im = 0; /* real there */
        break;
    case FX_BJ: ok = bess_ji(X, n.re, z, -1, &r); break;
    case FX_BI: ok = bess_ji(X, n.re, z, 1, &r); break;
    case FX_BY: case FX_BK:
        if (!isint) { /* Y_nu = (J_nu cos(nu pi) - J_-nu)/sin(nu pi), K_nu = pi/2 (I_-nu - I_nu)/sin(nu pi) */
            cx a, b, sn = cf(X, F_SIN, cmk(n.re * 3.14159265358979323846, 0)), cs;
            if (!cfinite(sn) || __builtin_fabs(sn.re) < 1e-6) return NULL;
            cs = cf(X, F_COS, cmk(n.re * 3.14159265358979323846, 0));
            int s1 = w == FX_BY ? -1 : 1;
            if (!bess_ji(X, n.re, z, s1, &a) || !bess_ji(X, -n.re, z, s1, &b)) return NULL;
            r = w == FX_BY ? cscale(csub(cscale(a, cs.re), b), 1 / sn.re)
                           : cscale(csub(b, a), 1.57079632679489661923 / sn.re);
            ok = 1;
            break;
        }
        ok = bess_yk(X, ni < 0 ? -ni : ni, z, w == FX_BK, &r);
        if (ok && ni < 0 && w == FX_BY && (ni & 1)) r = cscale(r, -1); /* Y_-n = (-1)^n Y_n, K_-n = K_n */
        break;
    }
    if (!ok || r.re != r.re || r.im != r.im) return NULL;
    return cx_e(X, r);
}

/* special values: polylog(1, z) = -ln(1 - z), polylog(0, z) = z/(1 - z), polylog(s, 0) = 0,
 * polylog(s, 1) = zeta(s), polylog(s, -1) = (2^(1-s) - 1) zeta(s), polylog(2, 1/2);
 * J, I at 0; J and I of half-integer order |nu| <= 15/2 in closed form from
 * J_(1/2) = sqrt(2/(pi z)) sin z, J_(-1/2) = sqrt(2/(pi z)) cos z and the recurrences
 * J_(nu+1) = 2 nu/z J_nu - J_(nu-1), I_(nu+1) = I_(nu-1) - 2 nu/z I_nu (sinh, cosh for I) */
static E *half_bessel(XP, int I, long twonu, E *z) {
    E *c = s_sqrt(s_div(K->c2, s_mul2(K->pi_, z)));
    E *a = s_mul2(c, s_fn1(I ? F_COSH : F_COS, z)), *b = s_mul2(c, s_fn1(I ? F_SINH : F_SIN, z)); /* nu = -1/2, 1/2 */
    if (twonu == -1) return a;
    if (twonu == 1) return b;
    if (twonu > 1) { /* up from (-1/2, 1/2) */
        E *lo = a, *hi = b;
        for (long m = 1; m < twonu; m += 2) { /* hi = order m/2 -> order m/2 + 1 */
            E *t = s_mul2(s_div(mk_int(m), z), hi);
            E *nx = I ? s_sub(lo, t) : s_sub(t, lo);
            lo = hi, hi = expand(nx);
        }
        return hi;
    }
    E *lo = a, *hi = b; /* down: J_(nu-1) = 2 nu/z J_nu - J_(nu+1);  I_(nu-1) = 2 nu/z I_nu + I_(nu+1) */
    for (long m = -1; m > twonu; m -= 2) { /* lo = order m/2 -> m/2 - 1 */
        E *t = s_mul2(s_div(mk_int(m), z), lo);
        E *nx = I ? s_add2(t, hi) : s_sub(t, hi);
        hi = lo, lo = expand(nx);
    }
    return lo;
}

/* f(args) is real for real symbols: 1, else 0 (unknown) */
static int fx_real(XP, int w, E *e) {
    E *n = K0(e), *z = K1(e);
    long ni;
    if (!freeof(n, K->i_) || !freeof(z, K->i_)) return 0;
    if (w == FX_POLYLOG) return is_zero(s_sub(K->c1, z)) || X->sign_of(s_sub(K->c1, z)) > 0;
    if ((w == FX_BJ || w == FX_BI) && num_is_small_int(n, &ni)) return 1;
    return X->sign_of(z) > 0;
}

/* no function of this module in e has a possibly non-real value */
static int xreal(XP, E *e) {
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN && e->t != E_LIST) return 1;
    stack_check();
    if (e->t == E_FN && e->s >= S__COUNT) {
        int w = which(X, e);
        if (w >= 0 && e->n == 2 && !fx_real(X, w, e)) return 0;
        if (w < 0 && sf_which(X, e) >= 0 && !sf_real(X, e)) return 0;
        if (w < 0 && (el_which(X, e) >= 0 || hy_which(X, e) >= 0)) return 0;
    }
    for (uint32_t i = 0; i < e->n; i++)
        if (!xreal(X, EK(e)[i])) return 0;
    return 1;
}

static E *fslot(XP, uint32_t na, E **A) {
    E *e = A[0];
    if (IFN(e, F_RE) || IFN(e, F_IM)) { /* re / im: kept when the value may be complex */
        if (e->n != 1 || xreal(X, K0(e))) return NULL;
        e->f = F_SIMP;
        return e;
    }
    int w = which(X, e);
    if (w < 0) {
        E *r = sf_simp(X, e);
        if (!r) r = el_simp(X, e);
        return r ? r : hy_simp(X, e);
    }
    if (e->n != 2) return NULL;
    if (is_const_expr(K0(e)) && is_const_expr(K1(e)) && (has_float(K0(e)) || has_float(K1(e)))) {
        E *v = vslot(X, 1, A); /* a decimal argument: the decimal value */
        if (v) return v;
    }
    E *n = K0(e), *z = K1(e);
    long s, zi;
    if (w == FX_POLYLOG) {
        if (!num_is_small_int(n, &s)) return NULL;
        if (s == 1) return s_neg(s_fn1(F_LN, s_sub(K->c1, z)));
        if (s == 0) return s_div(z, s_sub(K->c1, z));
        if (is_zero(z)) return K->c0;
        if (s >= 2 && num_is_small_int(z, &zi) && (zi == 1 || zi == -1)) {
            E *zs = s_fn1(F_ZETA, mk_int(s));
            return zi == 1 ? zs : s_mul2(s_sub(s_pow(K->c2, mk_int(1 - s)), K->c1), zs);
        }
        if (s == 2 && is_rat(z) && expr_eq(z, K->chalf)) {
            E *l2 = s_fn1(F_LN, K->c2);
            return s_sub(s_div(s_mul2(K->pi_, K->pi_), mk_int(12)), s_div(s_mul2(l2, l2), K->c2));
        }
        return NULL;
    }
    if (w != FX_BJ && w != FX_BI) return NULL;
    if (is_zero(z) && num_is_small_int(n, &s)) return s == 0 ? K->c1 : K->c0;
    if (is_rat(n)) {
        long p, q;
        if (num_is_small_int(K0(n), &p) && num_is_small_int(K1(n), &q) && q == 2 && p >= -15 && p <= 15)
            return half_bessel(X, w == FX_BI, p, z);
    }
    return NULL;
}

int casx_fnx_register(XP) {
    return X->reg_fn(CASX_FN_DIFF, (void *)dslot) | X->reg_fn(CASX_FN_CEVAL, (void *)vslot) |
           X->reg_fn(CASX_FN_FSIMP, (void *)fslot);
}
