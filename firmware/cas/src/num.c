/*
 * num.c -- numbers: integers (E_INT), rationals (E_RAT), doubles (E_FLT).
 * Mixed arithmetic: exact op exact -> exact; anything with a float -> float.
 *
 * This is the numeric tower used by the automatic simplifier (simp.c folds
 * numeric operands of sums, products and powers through num_add / num_mul /
 * num_pow_int) and by every module that manipulates numeric coefficients
 * (poly.c, factor.c, gbasis.c, series.c, ...).  Integer arithmetic is
 * delegated to big.c.
 *
 * Canonical forms (established here, relied upon everywhere, e.g. by
 * expr_eq and the canonical order in expr.c):
 *  - an integer is an E_INT node (see big.c);
 *  - a rational is an E_RAT node with kids (num, den), den > 1,
 *    gcd(num, den) = 1 and num != 0; the sign lives in the numerator.  A
 *    rational whose denominator would be 1 is returned as the integer, and
 *    a zero rational as integer 0.  mk_rat is the only constructor that
 *    normalises; num_neg builds the negated rational directly since negation
 *    preserves the invariants;
 *  - an "approximate" number is an E_FLT holding a double (bf.c may append
 *    an extended-precision mantissa; see cas_int.h).  Arithmetic on floats
 *    is plain double arithmetic, so such extra precision is lost by any
 *    operation except the identities that return an operand unchanged
 *    (x + 0, x * 1).
 * All results are F_SIMP nodes in scratch.  Division by zero (exact or
 * float) raises CAS_ERR_DOMAIN; oversized exact powers raise
 * CAS_ERR_MEMORY.
 */
#include "cas_int.h"
#include <math.h>

/* Float node holding x (may be inf/nan; callers decide what that means). */
E *mk_flt(double x) {
    E *e = ealloc(E_FLT, sizeof(double));
    EF(e) = x;
    e->f = F_SIMP;
    return e;
}

/* The normalising rational constructor: returns n/d in canonical form (an
 * E_INT when the reduced denominator is 1).  n and d are E_INT nodes; the
 * sign is moved to the numerator and the gcd divided out (one int_gcd plus
 * two exact divisions).  Throws CAS_ERR_DOMAIN "division by zero" if
 * d = 0. */
E *mk_rat(E *n, E *d) {
    if (int_is_zero(d)) cas_throw(CAS_ERR_DOMAIN, "division by zero");
    if (int_is_zero(n)) return mk_int(0);
    if (d->s) {
        n = int_neg(n);
        d = int_neg(d);
    }
    E *g = int_gcd(n, d);
    if (!int_is_one(g)) {
        n = int_divexact(n, g);
        d = int_divexact(d, g);
    }
    if (int_is_one(d)) return n;
    E *r = ealloc(E_RAT, 2 * sizeof(E *));
    r->n = 2;
    K0(r) = n;
    K1(r) = d;
    r->f = F_SIMP;
    return r;
}

/* n/d from machine integers (normalised). */
E *mk_frac(int64_t n, int64_t d) { return mk_rat(mk_int(n), mk_int(d)); }

/* Numerator / denominator of an exact number (an integer is a/1).  Not
 * meaningful for floats: num_numer returns the float, num_denom returns 1. */
E *num_numer(E *a) { return a->t == E_RAT ? K0(a) : a; }
E *num_denom(E *a) { return a->t == E_RAT ? K1(a) : K->c1; }

/* Sign -1 / 0 / 1 of any number (a NaN float gives 0). */
int num_sgn(E *a) {
    switch (a->t) {
    case E_INT: return int_sgn(a);
    case E_RAT: return int_sgn(K0(a));
    default: return EF(a) > 0 ? 1 : EF(a) < 0 ? -1 : 0;
    }
}

/* Value predicates.  A canonical rational is never 0, 1 or -1, so only the
 * integer (and for zero, float) cases need checking; num_is_one / num_is_m1
 * deliberately do not match the floats 1.0 / -1.0. */
int num_is_zero(E *a) {
    if (a->t == E_INT) return a->n == 0;
    if (a->t == E_FLT) return EF(a) == 0.0;
    return 0;
}
int num_is_one(E *a) { return a->t == E_INT && int_is_one(a); }
int num_is_m1(E *a) { return a->t == E_INT && a->n == 1 && a->s && EL(a)[0] == 1; }

/* 1 if a is an integer with |a| <= 2^31 - 1 (stored in *v when v is
 * non-NULL), else 0.  The symmetric 32-bit range keeps the value safe in a
 * `long` on every target and lets callers negate it freely. */
int num_is_small_int(E *a, long *v) {
    int64_t x;
    if (a->t != E_INT || !int_fits(a, &x)) return 0;
    if (x > 2147483647 || x < -2147483647) return 0;
    if (v) *v = (long)x;
    return 1;
}

/* Approximate value as a double.  A rational is converted as
 * (mn * 2^en) / (md * 2^ed) using the scaled mantissas of int_to_double_exp,
 * so ratios of integers far outside the double range (e.g. 10^400/10^399)
 * still give accurate results; overflow / underflow happen only in the
 * final ldexp.  Accuracy is a few ulps, not correctly rounded. */
double num_to_double(E *a) {
    switch (a->t) {
    case E_INT: return int_to_double(a);
    case E_FLT: return EF(a);
    default: {
        long en, ed;
        double mn = int_to_double_exp(K0(a), &en);
        double md = int_to_double_exp(K1(a), &ed);
        return ldexp(mn / md, (int)(en - ed));
    }
    }
}

/* The exact binary value of x as an integer or rational with a power-of-two
 * denominator (e.g. 0.1 -> 3602879701896397/36028797018963968).  Used where
 * the true value of a float is needed: exact float formatting (numeric.c),
 * converting floats to bigfloats (bf.c) and exact() for very large/small
 * values (upoly.c).
 * frexp splits x = m * 2^ex with 0.5 <= |m| < 1; m * 2^53 is then an exact
 * 53-bit integer.  Throws CAS_ERR_DOMAIN for inf / nan (x != x detects nan,
 * x - x != 0 detects inf). */
E *num_from_double(double x) {
    if (x == 0) return mk_int(0);
    if (x != x || x - x != 0) cas_throw(CAS_ERR_DOMAIN, "non-finite number");
    int ex;
    double m = frexp(x, &ex); /* x = m * 2^ex, 0.5 <= |m| < 1 */
    int64_t im = (int64_t)ldexp(m, 53);
    ex -= 53;
    E *n = mk_int(im);
    if (ex >= 0) return int_shl(n, (uint32_t)ex);
    return mk_rat(n, int_shl(mk_int(1), (uint32_t)(-ex)));
}

/* Numeric order -1 / 0 / 1.  With a float involved both sides are compared
 * as doubles (NaN compares equal to everything); exact rationals are
 * compared exactly by cross-multiplication a.n * b.d <=> b.n * a.d (the
 * denominators are positive). */
int num_cmp(E *a, E *b) {
    if (a->t == E_FLT || b->t == E_FLT) {
        double x = num_to_double(a), y = num_to_double(b);
        return x < y ? -1 : x > y ? 1 : 0;
    }
    if (a->t == E_INT && b->t == E_INT) return int_cmp(a, b);
    E *l = int_mul(num_numer(a), num_denom(b));
    E *r = int_mul(num_numer(b), num_denom(a));
    return int_cmp(l, r);
}

/* Structural numeric equality: same node type and same value, so the exact
 * 1 and the float 1.0 are NOT equal (they are different expressions). */
int num_eq(E *a, E *b) {
    if (a->t != b->t) return 0;
    return num_cmp(a, b) == 0;
}

/* a + b.  Exact zero is an identity that returns the other operand itself,
 * which preserves bf.c's extended-precision float nodes and avoids work.
 * Float contagion: if either side is a float the sum is a double.
 * Rationals: a.n/a.d + b.n/b.d = (a.n*b.d + b.n*a.d) / (a.d*b.d), reduced by
 * mk_rat (no attempt to divide out gcd(a.d, b.d) first). */
E *num_add(E *a, E *b) {
    if (a->t == E_INT && a->n == 0) return b; /* keeps extended-precision floats intact */
    if (b->t == E_INT && b->n == 0) return a;
    if (a->t == E_FLT || b->t == E_FLT) return mk_flt(num_to_double(a) + num_to_double(b));
    if (a->t == E_INT && b->t == E_INT) return int_add(a, b);
    E *an = num_numer(a), *ad = num_denom(a), *bn = num_numer(b), *bd = num_denom(b);
    return mk_rat(int_add(int_mul(an, bd), int_mul(bn, ad)), int_mul(ad, bd));
}

/* -a.  A negated canonical rational is still canonical, so it is built
 * directly (sharing the denominator) instead of going through mk_rat. */
E *num_neg(E *a) {
    switch (a->t) {
    case E_INT: return int_neg(a);
    case E_FLT: return mk_flt(-EF(a));
    default: {
        E *r = ealloc(E_RAT, 2 * sizeof(E *));
        r->n = 2;
        K0(r) = int_neg(K0(a));
        K1(r) = K1(a);
        r->f = F_SIMP;
        return r;
    }
    }
}

/* a - b and |a| */
E *num_sub(E *a, E *b) { return num_add(a, num_neg(b)); }

E *num_abs(E *a) { return num_sgn(a) < 0 ? num_neg(a) : a; }

/* a * b with the same conventions as num_add (exact 1 is an identity that
 * returns the other operand; floats are contagious).  Note that exact 0
 * times a float gives the float 0.0, not exact 0. */
E *num_mul(E *a, E *b) {
    if (a->t == E_INT && int_is_one(a)) return b;
    if (b->t == E_INT && int_is_one(b)) return a;
    if (a->t == E_FLT || b->t == E_FLT) return mk_flt(num_to_double(a) * num_to_double(b));
    if (a->t == E_INT && b->t == E_INT) return int_mul(a, b);
    return mk_rat(int_mul(num_numer(a), num_numer(b)), int_mul(num_denom(a), num_denom(b)));
}

/* 1/a; throws CAS_ERR_DOMAIN for exact or float zero.  For exact a this is
 * just the swapped fraction (mk_rat moves the sign back to the numerator). */
E *num_inv(E *a) {
    if (a->t == E_FLT) {
        if (EF(a) == 0) cas_throw(CAS_ERR_DOMAIN, "division by zero");
        return mk_flt(1.0 / EF(a));
    }
    if (num_is_zero(a)) cas_throw(CAS_ERR_DOMAIN, "division by zero");
    return mk_rat(num_denom(a), num_numer(a));
}

/* a / b; throws CAS_ERR_DOMAIN when b is zero (exact, or a float equal to
 * 0.0).  Exact: (a.n * b.d) / (a.d * b.n), reduced by mk_rat. */
E *num_div(E *a, E *b) {
    if (a->t == E_FLT || b->t == E_FLT) {
        double y = num_to_double(b);
        if (y == 0) cas_throw(CAS_ERR_DOMAIN, "division by zero");
        return mk_flt(num_to_double(a) / y);
    }
    if (num_is_zero(b)) cas_throw(CAS_ERR_DOMAIN, "division by zero");
    return mk_rat(int_mul(num_numer(a), num_denom(b)), int_mul(num_denom(a), num_numer(b)));
}

/* a^k for an integer exponent k (an E_INT).
 *  - float a: pow() in double precision (no errors; 0.0^-1 gives inf).
 *  - exact a: (n/d)^k = n^k / d^k, which is already in lowest terms (no gcd
 *    needed beyond mk_rat's sign normalisation); for k < 0 the fraction is
 *    flipped, and 0^(negative) throws CAS_ERR_DOMAIN.  k = 0 gives 1
 *    (including 0^0 = 1).
 *  - |k| > 10^8 (or not fitting in 64 bits): only the trivial bases 1, 0 (for
 *    k > 0) and -1 are answered; anything else throws CAS_ERR_MEMORY
 *    "exponent too large" (so 0^(-huge) is reported as a size error rather
 *    than division by zero).  Smaller k may still throw "integer too large"
 *    from int_pow if the result exceeds its size limit. */
E *num_pow_int(E *a, E *k) {
    if (a->t == E_FLT) return mk_flt(pow(EF(a), int_to_double(k)));
    int64_t kv;
    if (!int_fits(k, &kv) || kv > 100000000 || kv < -100000000) {
        if (num_is_one(a)) return a;
        if (num_is_zero(a) && !k->s) return a;
        if (num_is_m1(a)) return int_is_even(k) ? K->c1 : a;
        cas_throw(CAS_ERR_MEMORY, "exponent too large");
    }
    if (kv == 0) return K->c1;
    int neg = kv < 0;
    uint32_t u = (uint32_t)(neg ? -kv : kv);
    if (neg && num_is_zero(a)) cas_throw(CAS_ERR_DOMAIN, "division by zero");
    E *n = int_pow(num_numer(a), u), *d = int_pow(num_denom(a), u);
    if (neg) {
        E *t = n; n = d; d = t;
    }
    if (int_is_one(d) && !d->s) return n;
    return mk_rat(n, d);
}

/* floor(a): an integer for exact a (floor division of numerator by
 * denominator), but a float (floor of the double) for float a. */
E *num_floor(E *a) {
    if (a->t == E_INT) return a;
    if (a->t == E_FLT) return mk_flt(floor(EF(a)));
    E *q;
    int_fdivmod(K0(a), K1(a), &q, NULL);
    return q;
}
