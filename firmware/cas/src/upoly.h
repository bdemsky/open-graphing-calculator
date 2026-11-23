/* upoly.h -- univariate polynomials with expression coefficients (internal)
 *
 * A small dense polynomial type for algorithms that work in K[x] where K is
 * the field of rational functions in everything except x (numbers, other
 * symbols, kernels such as sqrt(2) or sin(y)).  Coefficients are ordinary
 * expressions kept in rational normal form, so zero tests are reliable and
 * Euclidean division works.  Used by integ.c (Hermite reduction, partial
 * fractions, Rothstein-Trager), solve.c, ode.c and sum.c; implemented in
 * upoly.c, which also holds partfrac, resultants, numeric roots and exact().
 *
 * Contrast with poly.c, whose recursive dense multivariate polynomials over
 * Q are faster but cannot carry symbolic coefficients through divisions.
 *
 * Memory: every UP and its coefficient array live in scratch; UP values are
 * passed and returned by value (the struct is two words) and are never
 * mutated after being returned, except where a function documents that it
 * builds a fresh copy.  Errors propagate with cas_throw (CAS_ERR_DOMAIN for
 * division by the zero polynomial, plus whatever the coefficient arithmetic
 * raises).
 */
#ifndef UPOLY_H
#define UPOLY_H
#include "cas_int.h"

/* Polynomial c[0] + c[1] x + ... + c[d] x^d.  Normalised (up_norm) means
 * c[d] is not zero, or d = -1 for the zero polynomial.  The array always has
 * at least one slot, even for d = -1. */
typedef struct {
    long d;   /* degree, -1 for zero */
    E **c;    /* ascending coefficients */
} UP;

/* Construction / conversion:
 *   up_alloc(d)       d+1 coefficients all 0 (not normalised)
 *   up_norm(a)        drop zero leading coefficients (shares a.c)
 *   up_from(e, x, &p) e as a polynomial in x; 0 if e is not polynomial in x
 *   up_to(a, x)       back to a simplified expression sum c_i x^i
 *   up_const(c)       constant polynomial c (zero polynomial if c = 0) */
UP up_alloc(long d);
UP up_norm(UP a);
int up_from(E *e, E *x, UP *out);
E *up_to(UP a, E *x);
UP up_const(E *c);
/* Arithmetic (results normalised, coefficients in rational normal form):
 *   up_add(a, b, sub) a + b, or a - b when sub != 0
 *   up_mul(a, b)      schoolbook product
 *   up_scale(a, c)    c * a for an x-free expression c
 *   up_divrem         Euclidean division a = q b + r, deg r < deg b (q or r
 *                     may be NULL); throws CAS_ERR_DOMAIN if b = 0
 *   up_monic(a)       a / lc(a) (zero stays zero)
 *   up_gcd(a, b)      monic gcd by Euclid's algorithm
 *   up_egcd(a,b,s,t)  monic g = gcd with s a + t b = g
 *   up_deriv(a)       d/dx
 *   up_pow(a, k)      a^k, k >= 0, by repeated multiplication */
UP up_add(UP a, UP b, int sub);
UP up_mul(UP a, UP b);
UP up_scale(UP a, E *c);
void up_divrem(UP a, UP b, UP *q, UP *r);
UP up_monic(UP a);
UP up_gcd(UP a, UP b);
UP up_egcd(UP a, UP b, UP *s, UP *t);
UP up_deriv(UP a);
UP up_pow(UP a, long k);
/* All n complex roots of sum (cr[i] + i ci[i]) x^i (ascending, c[n] != 0) by
 * Aberth's method in double precision; writes rr[0..n), ri[0..n) and returns
 * n (0 if n < 1).  Currently only called from upoly.c (proot). */
int poly_roots_c(int n, const double *cr, const double *ci, double *rr, double *ri);

#endif
