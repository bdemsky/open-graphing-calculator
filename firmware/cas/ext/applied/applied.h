/*
 * applied.h -- shared declarations of the applied-mathematics part of the casx module
 * (cas/ext/applied: numeric linear algebra, transforms, linear programming, Boolean logic,
 * splines).  Like cas/ext/sci it is left out of the core tier's flash unless
 * CONFIG_CASX_CORE_SCI; its names are in casx.c's known list, so they report "needs the
 * advanced CAS module" there.
 *
 * Doubles only through the API: no libm in the module image (X->sqrt; ap_sincos below).
 */
#ifndef APPLIED_H
#define APPLIED_H
#include "casx_mod.h"

#define AP_EPS 2.220446049250313e-16
#define AFABS(v) __builtin_fabs(v)

/* sin and cos of t (argument reduction by pi/2, Taylor polynomials on [-pi/4, pi/4]) */
void ap_sincos(double t, double *s, double *c);
/* the complex number re + im i as a float node (im == 0: a real float) */
E *ap_cnum(XP, double re, double im);
/* a real double matrix from a matrix of real constants (row-major, *m x *n), or a throw */
double *ap_dmat(XP, E *A, uint32_t *m, uint32_t *n);
/* the matrix node of a row-major m x n double array (|v| <= tiny -> 0.0) */
E *ap_mat(XP, const double *a, uint32_t m, uint32_t n, double tiny);
/* a fresh private symbol */
E *ap_sym(XP, const char *nm);

/* nla.c: numeric linear algebra */
E *ap_svd(XP, E *A);
E *ap_singvals(XP, E *A);
E *ap_pinv(XP, E *A);
E *ap_norm2(XP, E *A);
E *ap_cond(XP, E *A, E *p);
E *ap_rank(XP, E *A, E *tol);
int ap_nla_register(XP); /* the slot CASX_FN_NUMLA */

/* xform.c: Fourier and z transforms; fft.c */
E *ap_fourier(XP, E *f, E *x, E *w, int inv);
E *ap_ztrans(XP, E *f, E *n, E *z);
E *ap_iztrans(XP, E *F, E *z, E *n);
E *ap_fft(XP, E *L, int inv);

/* lp.c: linear programming and constrained extrema */
E *ap_linprog(XP, uint32_t n, E **a);
E *ap_extremum(XP, E *f, E *cons, E *vars, int maxi);

/* logic.c: Boolean logic; op = AP_TT ... AP_IFF */
enum { AP_TT, AP_BSIMP, AP_CNF, AP_DNF, AP_SAT, AP_IMPL, AP_IFF };
E *ap_logic(XP, int op, uint32_t n, E **a);

/* spline.c */
E *ap_spline(XP, E *xs, E *ys, E *x);

#endif
