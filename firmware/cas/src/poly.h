/*
 * poly.h -- recursive dense multivariate polynomials over Q (internal).
 * A polynomial in variable v (v >= 0) has coefficients k[0..n-1] that are
 * polynomials in variables < v (or constants, v = -1).  Variables are indices
 * into a kernel table (symbols or non-polynomial subexpressions).
 *
 * This is the internal polynomial layer shared by poly.c (arithmetic, gcd,
 * squarefree decomposition, conversions, rational normal form), factor.c
 * (factorisation over Q) and heurisch.c (which builds rational functions in
 * kernels with e2r and factors denominators with p_factor).  Everything else in
 * the CAS talks to it through the expression-level entry points declared in
 * cas_int.h (expand, rat_normal, factor_expr, numden, poly_coeff, ...).
 *
 * Representation ("recursive dense", Geddes/Czapor/Labahn ch. 2; Knuth TAOCP
 * vol. 2, 4.6): a P node is either
 *   - a constant: v = -1, n = 0, c = an E_INT / E_RAT / E_FLT number node,
 *     k = NULL; or
 *   - a polynomial in x_v: v >= 0, k[0..n-1] are the coefficients of
 *     x_v^0 .. x_v^(n-1), each itself a P whose main variable is < v.
 * Example with kernels {0: y, 1: x}: x^2*y + 3 is
 *     P{v=1, n=3, k = [P_const(3), P_const(0), P{v=0, n=2, k=[0, 1]}]}.
 *
 * Normal form (established by p_norm and kept by every operation here):
 *   - for v >= 0, n >= 2 and k[n-1] is not zero (a polynomial of degree 0 in
 *     x_v is collapsed to its constant coefficient, so the main variable of a
 *     non-constant P really occurs in it);
 *   - zero is the constant node with c = 0 (an E_FLT 0.0 also counts as zero);
 *   - the main variable of every coefficient is strictly smaller than v.
 * Because of this, structural equality (p_eq) is polynomial equality, and
 * "the variable with the largest kernel index" is always the outermost one.
 *
 * Memory: all P nodes and their coefficient arrays live in scratch (salloc);
 * they are never freed individually and are treated as immutable once built
 * (p_norm trims fresh results only).  Sub-polynomials are shared freely
 * between results, so P values form DAGs just like E expressions.  P nodes are
 * not E nodes, so skeep() cannot relocate them: callers that compact scratch
 * convert to expressions first.  Allocation failure and interrupts are raised
 * through cas_throw (out of salloc / poll_intr), never returned.
 */
#ifndef POLY_H
#define POLY_H
#include "cas_int.h"

/* A polynomial node; see the header comment for the layout and invariants. */
typedef struct P P;
struct P {
    int v;       /* main variable, -1 for constants */
    uint32_t n;  /* number of coefficients (degree+1) */
    E *c;        /* constant value when v < 0 */
    P **k;       /* coefficients k[0..n-1] (v >= 0), ascending powers of x_v */
};

/* Kernel table: maps variable indices of P to the expressions they stand for
 * (symbols, function calls such as sin(x), radicals such as x^(1/3), ...).
 * The index of a kernel is its position in k; kernels added later get larger
 * indices and therefore become "more main" variables.  Grown by doubling in
 * scratch (kt_index).  has_float is set by the converters when a floating
 * point constant was seen, which disables gcd cancellation (rf_normalize) and
 * factorisation (factor_poly_expr). */
typedef struct {
    E **k;              /* kernel expressions, k[0..n-1] */
    uint32_t n, cap;    /* used / allocated entries */
    int has_float;      /* an E_FLT coefficient occurred */
} ktab;

/* --- construction and basic predicates (poly.c) ---
 * p_const(c)      constant polynomial with value c (a number node)
 * p_var(v)        the polynomial x_v
 * p_mk(v, n)      raw node with n uninitialised coefficient slots (caller
 *                 fills k[] and normally calls p_norm)
 * p_norm(p)       trim zero leading coefficients in place; collapse degree 0
 * p_is_zero / p_is_one / p_is_const   tests on normalised polynomials
 * p_eq(a, b)      structural (= mathematical) equality; a float constant is
 *                 never equal to an exact one */
P *p_const(E *c);
P *p_var(int v);
P *p_mk(int v, uint32_t n);
P *p_norm(P *p);
int p_is_zero(P *p);
int p_is_one(P *p);
int p_is_const(P *p);
int p_eq(P *a, P *b);

/* --- ring arithmetic (poly.c); results are normalised, arguments untouched ---
 * p_scale multiplies by the number c; p_pow uses binary powering. */
P *p_add(P *a, P *b);
P *p_sub(P *a, P *b);
P *p_neg(P *a);
P *p_mul(P *a, P *b);
P *p_scale(P *a, E *c);
P *p_pow(P *a, uint32_t k);

/* --- degrees and coefficients ---
 * p_deg(p, v)     degree in x_v (any variable, not only the main one);
 *                 -1 for the zero polynomial
 * p_coef(p, v, d) coefficient of x_v^d as a polynomial in the other variables
 * p_lc(p)         leading coefficient with respect to the main variable */
long p_deg(P *p, int v);
P *p_coef(P *p, int v, long d);
P *p_lc(P *p);
E *p_lnum(P *p);                 /* leading numeric coefficient (recursive) */

/* --- division, content and gcd ---
 * p_divexact(a, b) a / b when b divides a exactly (over Q), else NULL; throws
 *                  CAS_ERR_DOMAIN when b is zero
 * p_prem(a, b)     pseudo-remainder of a by b in b's main variable (b must be
 *                  non-constant)
 * p_gcd(a, b)      gcd over Q, returned as a primitive integer polynomial with
 *                  positive leading numeric coefficient; 1 if either input
 *                  contains a float */
P *p_divexact(P *a, P *b);       /* NULL if not exact */
P *p_prem(P *a, P *b);
P *p_content(P *p);              /* gcd of coefficients wrt main variable */
E *p_numcontent(P *p);           /* rational content: p = c * (primitive integer poly) */
P *p_intprim(P *p, E **c);       /* p = c * result, result primitive over Z, lnum > 0 */
P *p_gcd(P *a, P *b);

/* --- calculus and substitution ---
 * p_diff(p, v)          partial derivative with respect to x_v
 * p_eval(p, v, val)     p with x_v replaced by the number val
 * p_subst_var(p, v, q)  p with x_v replaced by the polynomial q (q may contain
 *                       any variables, including ones larger than p's)
 * p_has_var(p, v)       1 if x_v occurs in p
 * p_maxvar(p)           main variable (-1 for constants) */
P *p_diff(P *p, int v);
P *p_eval(P *p, int v, E *val);
P *p_subst_var(P *p, int v, P *val);
int p_has_var(P *p, int v);
int p_maxvar(P *p);

/* --- decomposition ---
 * p_sqf     Yun squarefree decomposition of f with respect to its main
 *           variable: stores up to cap squarefree factors with their
 *           multiplicities and returns the count (poly.c)
 * p_factor  irreducible factorisation over Q of an arbitrary polynomial
 *           (factor.c); the numeric content is dropped, factors are primitive
 *           with positive leading numeric coefficient */
uint32_t p_sqf(P *f, P **facs, uint32_t *mult, uint32_t cap); /* f primitive, main var */
uint32_t p_factor(P *f, P **facs, uint32_t *mult, uint32_t cap);

/* --- expression <-> polynomial conversion (poly.c) ---
 * kt_init(kt)       empty kernel table (capacity 16, in scratch)
 * kt_index(kt, k)   index of kernel k, adding it if new (structural equality;
 *                   throws CAS_ERR_UNSUPPORTED beyond 30000 kernels)
 * e2p               expression -> polynomial; rational powers b^(p/q) become
 *                   powers of the kernel b^(1/q), other non-polynomial parts
 *                   (functions, symbolic or negative exponents) become kernels.
 *                   The NULL return is reserved for "not polynomial"; the
 *                   current converter makes every subexpression a kernel and so
 *                   never returns NULL, but callers still check it.
 * e2p_expand        like e2p but also expands inside kernels (function
 *                   arguments, bases) and splits b^(u+v); used by expand()
 * e2r               expression -> num/den pair of polynomials (common
 *                   denominator, not yet cancelled); kernels are put in
 *                   rational normal form first
 * p2e               polynomial -> canonical (expanded, auto-simplified) sum
 * p2e_factor        currently identical to p2e */
void kt_init(ktab *kt);
int kt_index(ktab *kt, E *k);
P *e2p(E *e, ktab *kt);          /* polynomial in kernels, NULL if not polynomial */
P *e2p_expand(E *e, ktab *kt);   /* negative powers become kernels */
void e2r(E *e, ktab *kt, P **num, P **den);  /* rational function */
E *p2e(P *p, ktab *kt);          /* expanded expression */
E *p2e_factor(P *p, ktab *kt);   /* keep structure in main var (for display of factors) */

/* univariate dense integer polys used by factorisation */
typedef struct {
    long d;   /* degree, -1 for zero */
    E **c;    /* c[0..d] integers */
} ZP;
/* Factor a squarefree, primitive f in Z[x] with positive leading coefficient
 * into irreducibles over Z (cyclotomic shortcut for x^n +- 1, otherwise
 * Cantor-Zassenhaus mod p + Hensel lifting + recombination).  Writes at most
 * cap factors (each primitive, lc > 0) to out and returns their number; the
 * product of the factors equals f unless cap was too small (excess factors
 * are dropped silently).  factor.c. */
uint32_t zp_factor_sqf(ZP f, ZP *out, uint32_t cap); /* f squarefree, primitive, lc>0 */

#endif
