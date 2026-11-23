/*
 * fft.c -- discrete Fourier transform of the casx module (applied.h):
 *   fft(list)    X_k = sum_{j=0}^{N-1} x_j e^(-2 pi i j k / N), k = 0..N-1
 *   ifft(list)   x_j = 1/N sum_k X_k e^(2 pi i j k / N)
 * in double precision (the convention of most numeric libraries).  Lengths that are powers of two
 * go through the iterative radix-2 Cooley-Tukey algorithm; any other length N through Bluestein's
 * chirp z-transform (a cyclic convolution of length 2^m >= 2N - 1 done with radix-2 transforms),
 * with the chirp angles pi (k^2 mod 2N)/N reduced exactly.  Entries may be complex; parts below
 * 64 eps max|X_k| are shown as 0.  At most 16384 entries.
 */
#include "applied.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

#define PI 3.14159265358979323846

/* in-place radix-2 transform of re/im (length n = 2^m), sign -1 forward, +1 backward (unscaled) */
static void fft2(XP, double *re, double *im, uint32_t n, int sign) {
    for (uint32_t i = 1, j = 0; i < n; i++) {
        uint32_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j |= bit;
        if (i < j) {
            double t = re[i];
            re[i] = re[j], re[j] = t;
            t = im[i], im[i] = im[j], im[j] = t;
        }
    }
    for (uint32_t len = 2; len <= n; len <<= 1) {
        poll_intr();
        uint32_t h = len >> 1;
        for (uint32_t j = 0; j < h; j++) {
            double s, c;
            ap_sincos(sign * 2 * PI * (double)j / (double)len, &s, &c);
            for (uint32_t i = j; i < n; i += len) {
                uint32_t k = i + h;
                double tr = re[k] * c - im[k] * s, ti = re[k] * s + im[k] * c;
                re[k] = re[i] - tr, im[k] = im[i] - ti;
                re[i] += tr, im[i] += ti;
            }
        }
    }
}

E *ap_fft(XP, E *L, int inv) {
    if (L->t != E_LIST || L->n == 0) cas_throw(CAS_ERR_DOMAIN, "fft: expected a list of numbers");
    uint32_t n = L->n;
    if (n > 16384) cas_throw(CAS_ERR_DOMAIN, "fft: at most 16384 entries");
    double *re = salloc(sizeof(double) * n), *im = salloc(sizeof(double) * n);
    int sign = inv ? 1 : -1;
    for (uint32_t i = 0; i < n; i++)
        if (!is_const_expr(EK(L)[i]) || !X->eval_complex(EK(L)[i], &re[i], &im[i]))
            cas_throw(CAS_ERR_DOMAIN, "fft: expected a list of numbers");
    if (!(n & (n - 1))) fft2(X, re, im, n, sign);
    else { /* Bluestein: X_k = w_k sum_j (x_j w_j) conj(w_(k-j)), w_k = e^(sign i pi k^2 / N) */
        uint32_t m = 1;
        while (m < 2 * n - 1) m <<= 1;
        double *ar = salloc(sizeof(double) * m), *ai = salloc(sizeof(double) * m);
        double *br = salloc(sizeof(double) * m), *bi = salloc(sizeof(double) * m);
        double *wr = salloc(sizeof(double) * n), *wi = salloc(sizeof(double) * n);
        for (uint32_t i = 0; i < m; i++) ar[i] = ai[i] = br[i] = bi[i] = 0;
        for (uint32_t k = 0; k < n; k++) {
            uint64_t q = ((uint64_t)k * k) % (2 * (uint64_t)n);
            ap_sincos(sign * PI * (double)q / (double)n, &wi[k], &wr[k]);
            ar[k] = re[k] * wr[k] - im[k] * wi[k], ai[k] = re[k] * wi[k] + im[k] * wr[k];
            br[k] = wr[k], bi[k] = -wi[k];
            if (k) br[m - k] = wr[k], bi[m - k] = -wi[k];
        }
        fft2(X, ar, ai, m, -1);
        fft2(X, br, bi, m, -1);
        for (uint32_t i = 0; i < m; i++) {
            double t = ar[i] * br[i] - ai[i] * bi[i];
            ai[i] = ar[i] * bi[i] + ai[i] * br[i], ar[i] = t;
        }
        fft2(X, ar, ai, m, 1);
        for (uint32_t k = 0; k < n; k++) {
            double cr = ar[k] / m, ci = ai[k] / m;
            re[k] = cr * wr[k] - ci * wi[k], im[k] = cr * wi[k] + ci * wr[k];
        }
    }
    double mx = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (inv) re[i] /= n, im[i] /= n;
        if (AFABS(re[i]) > mx) mx = AFABS(re[i]);
        if (AFABS(im[i]) > mx) mx = AFABS(im[i]);
    }
    double tiny = 64 * AP_EPS * mx;
    E **v = salloc(sizeof(E *) * n);
    for (uint32_t i = 0; i < n; i++)
        v[i] = ap_cnum(X, AFABS(re[i]) <= tiny ? 0.0 : re[i], AFABS(im[i]) <= tiny ? 0.0 : im[i]);
    return s_list(n, v);
}
