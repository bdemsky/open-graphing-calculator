/* cx.h -- complex double arithmetic for the special-function values of the casx module (fnx.c,
 * sfn.c).  Elementary functions go through the core's complex evaluator (no libm in an XIP
 * image); each call releases the scratch it used. */
#ifndef CX_H
#define CX_H
#include "intx.h"

typedef struct {
    double re, im;
} cx;

static inline cx cmk(double r, double i) {
    cx z = {r, i};
    return z;
}
static inline cx cadd(cx a, cx b) { return cmk(a.re + b.re, a.im + b.im); }
static inline cx csub(cx a, cx b) { return cmk(a.re - b.re, a.im - b.im); }
static inline cx cmul(cx a, cx b) { return cmk(a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re); }
static inline cx cscale(cx a, double s) { return cmk(a.re * s, a.im * s); }
static inline cx cdivx(cx a, cx b) {
    double d = b.re * b.re + b.im * b.im;
    return cmk((a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d);
}
static inline double cabs2(cx a) { return a.re * a.re + a.im * a.im; }

static inline E *cx_e(XP, cx z) {
    return z.im == 0 ? mk_flt(z.re) : s_add2(mk_flt(z.re), s_mul2(mk_flt(z.im), K->i_));
}

/* a builtin function of a complex number, through the core */
static inline int cfn(XP, int id, cx z, cx *out) {
    char *m = K->s_top;
    int ok = X->eval_complex(s_fn1(id, cx_e(X, z)), &out->re, &out->im);
    K->s_top = m;
    return ok;
}
/* the same, NaN (and a failed evaluation) as 0 / 0 */
static inline cx cf(XP, int id, cx z) {
    cx r;
    if (!cfn(X, id, z, &r)) r = cmk(0.0 / 0.0, 0);
    return r;
}
static inline int cfinite(cx z) { return z.re - z.re == 0 && z.im - z.im == 0; }
#endif
