/*
 * bf.c -- arbitrary-precision binary floating point for numeric evaluation
 * at a chosen number of digits (approx(expr, n), digits > 15).
 *
 * A value is m * 2^e with m a signed bignum normalised to at most P bits
 * (P = working precision).  Elementary functions use argument reduction plus
 * Taylor series: exp (x = k ln2 + r, r/2^s, squaring), ln (binary exponent +
 * atanh series), sin/cos (reduction mod pi/2, halving + double-angle), atan
 * (half-angle reduction + series), pi (Machin), and derived functions.
 * The result is stored in a float node that carries the exact mantissa for
 * display; further arithmetic on such nodes uses doubles.
 *
 * Data flow: numeric.c (evalf_digits, used by approx(e, n) / evalf(e, n)
 * and by approximate mode with more than 15 digits) calls bf_evalf on
 * constant subexpressions.  bf_evalf walks the expression with bfev; if any
 * part is unsupported (unknown function, non-real intermediate, symbol other
 * than pi / e) it returns NULL and numeric.c falls back to double precision.
 * The result is an E_FLT node whose payload is extended (see cas_int.h):
 *     double | int32 binary exponent | int32 requested digits | n limbs
 * with node->n = limb count and node->s = sign; the double is the nearest
 * double for any later arithmetic.  The printer (fmt_float_node in
 * numeric.c) calls bigflt_parts to recover the exact value and formats it
 * to the requested number of digits.
 *
 * Precision model.  A BF value is m * 2^e with a signed bignum mantissa m.
 * Every operation rounds its result to P bits (bf_norm), where
 * P = 3.33 * digits + 40: about log2(10) bits per decimal digit plus 40
 * guard bits that absorb the error growth of argument reduction, repeated
 * squaring and series truncation.  Errors are not tracked rigorously
 * (this is not interval / ball arithmetic); the guard bits are a heuristic.
 * In particular absolute (not relative) accuracy is all that survives
 * cancellation, so results that are tiny compared with intermediate
 * quantities lose bits (see the notes at bf_ln, bf_sincos and the sinh
 * case of bfev); bf_evalf compensates by re-evaluating at a higher
 * precision until two evaluations agree (Ziv's strategy).
 *
 * Algorithms (Brent & Zimmermann, "Modern Computer Arithmetic", ch. 4;
 * Muller, "Elementary Functions"):
 *  - pi by Machin's formula pi = 16 atan(1/5) - 4 atan(1/239), each arctan
 *    by its Taylor series in fixed point;
 *  - ln 2 = 2 atanh(1/3);
 *  - exp: x = k ln2 + r, |r| <= ln2/2, then r / 2^12, Taylor series, and 12
 *    squarings;
 *  - ln: scale to [1/2, 1), take 6 square roots (y^(1/64), close to 1),
 *    then ln y = 2 atanh((y-1)/(y+1)) by series;
 *  - sin / cos: reduction modulo pi/2, division by 2^10, both Taylor series,
 *    10 double-angle steps, quadrant fix-up;
 *  - atan: x > 1 via pi/2 - atan(1/x), 5 half-angle steps
 *    x -> x / (1 + sqrt(1 + x^2)), series, times 2^5;
 *  - the remaining functions are composed from these.
 *
 * Memory: everything is allocated in scratch and nothing is compacted; the
 * series loops allocate O(number of terms * P bits), which is modest for
 * the supported digits (<= 1000).  Errors raised inside bf_evalf are caught
 * there: interrupts and out-of-memory are re-thrown, all other errors make
 * it return NULL.  The working precision is the file-static P, so this
 * module is not reentrant (consistent with the single-context design).
 */
#include "cas_int.h"
#include <math.h>
#include <limits.h>
#include <string.h>

/* A binary bigfloat: value = m * 2^e.  m = 0 means zero (e is then 0). */
typedef struct {
    E *m; /* signed integer mantissa */
    long e;
} BF;

static long P; /* working precision in bits */

/* Round a to at most P significant bits (in practice P, or P+1 when rounding
 * carries out of the top bit, which is harmless).  Rounds the magnitude
 * half up, i.e. to nearest with ties away from zero, and adjusts the
 * exponent.  Zero gets exponent 0.  Called by every arithmetic operation, so
 * mantissas never grow beyond about P bits. */
static BF bf_norm(BF a) {
    if (int_is_zero(a.m)) {
        a.e = 0;
        return a;
    }
    long nb = (long)int_bits(a.m);
    if (nb > P) {
        long sh = nb - P;
        int neg = a.m->s;
        E *am = int_abs(a.m);
        /* round half up */
        E *r = int_shr(int_add(am, int_shl(K->c1, (uint32_t)(sh - 1))), (uint32_t)sh);
        a.m = neg ? int_neg(r) : r;
        a.e += sh;
    }
    return a;
}

/* Exact integer n (rounded to P bits if longer) and machine integer v. */
static BF bf_int(E *n) {
    BF r = {n, 0};
    return bf_norm(r);
}

static BF bf_si(long v) { return bf_int(mk_int(v)); }

/* Exact number q (integer or rational) to P bits: p/d is computed as
 * floor(p * 2^s / d) * 2^-s, with s chosen so the integer quotient has about
 * P + 4 bits (s is clamped at 0).  Truncating division, then rounded by
 * bf_norm. */
static BF bf_rat(E *q) {
    if (q->t == E_INT) return bf_int(q);
    E *p = K0(q), *d = K1(q);
    long s = P + (long)int_bits(d) - (long)int_bits(p) + 4;
    if (s < 0) s = 0;
    BF r = {int_tdiv(int_shl(p, (uint32_t)s), d), -s};
    return bf_norm(r);
}

/* Sign of a: -1, 0 or 1. */
static int bf_sgn(BF a) { return int_sgn(a.m); }

/* -a (exact). */
static BF bf_neg(BF a) {
    a.m = int_neg(a.m);
    return a;
}

/* a + b.  The operand with the larger exponent is shifted left so both
 * mantissas share the smaller exponent, then the exact sum is rounded to P
 * bits.  If b lies entirely more than P + 64 bits below a's lowest bit it
 * cannot affect the rounded result and a is returned unchanged; this also
 * avoids huge shifts when adding numbers of wildly different magnitude.
 * Subtraction of nearly equal values is exact here but leaves fewer
 * significant bits (cancellation is the caller's problem). */
static BF bf_add(BF a, BF b) {
    if (int_is_zero(a.m)) return b;
    if (int_is_zero(b.m)) return a;
    if (a.e < b.e) {
        BF t = a; a = b; b = t;
    }
    long d = a.e - b.e;
    if (d > P + 64 + (long)int_bits(b.m)) return a; /* b negligible */
    BF r = {int_add(int_shl(a.m, (uint32_t)d), b.m), b.e};
    return bf_norm(r);
}

/* a - b. */
static BF bf_sub(BF a, BF b) { return bf_add(a, bf_neg(b)); }

/* a * b: exact product of the mantissas (up to 2P bits, Karatsuba for large
 * P), then rounding. */
static BF bf_mul(BF a, BF b) {
    BF r = {int_mul(a.m, b.m), a.e + b.e};
    return bf_norm(r);
}

/* a / b to about P + 4 bits: shift a's mantissa left by s so the integer
 * quotient has P + 4 bits, truncate, then round.  Throws CAS_ERR_DOMAIN on
 * division by zero (caught by bf_evalf, which then returns NULL). */
static BF bf_div(BF a, BF b) {
    if (int_is_zero(b.m)) cas_throw(CAS_ERR_DOMAIN, "division by zero");
    long s = P + (long)int_bits(b.m) - (long)int_bits(a.m) + 4;
    if (s < 0) s = 0;
    BF r = {int_tdiv(int_shl(a.m, (uint32_t)s), b.m), a.e - b.e - s};
    return bf_norm(r);
}

/* a / d for a machine integer d (series coefficients). */
static BF bf_div_si(BF a, long d) { return bf_div(a, bf_si(d)); }

/* a * 2^k: exact, only the exponent changes. */
static BF bf_shift(BF a, long k) {
    a.e += k;
    return a;
}

/* sqrt(a) for a >= 0 (throws CAS_ERR_DOMAIN for a < 0).  Uses the integer
 * square root: shift the mantissa left by s so it has about 2P + 4 bits and
 * so that the exponent e - s is even, then
 *     sqrt(m * 2^e) = isqrt(m * 2^s) * 2^((e - s) / 2),
 * giving about P + 2 correct (truncated) bits. */
static BF bf_sqrt(BF a) {
    if (bf_sgn(a) < 0) cas_throw(CAS_ERR_DOMAIN, "sqrt of negative");
    if (int_is_zero(a.m)) return a;
    long s = 2 * P - (long)int_bits(a.m) + 4;
    if (s < 0) s = 0;
    if ((a.e - s) & 1) s++;
    int ex;
    BF r = {int_iroot(int_shl(a.m, (uint32_t)s), 2, &ex), (a.e - s) / 2};
    return bf_norm(r);
}

/* Nearest double (overflows to inf / underflows to 0 via ldexp). */
static double bf_to_double(BF a) {
    long ex;
    double m = int_to_double_exp(a.m, &ex);
    return ldexp(m, (int)(ex + a.e));
}

/* Compare a and b (-1 / 0 / 1) via the sign of the rounded difference. */
static int bf_cmp(BF a, BF b) { return bf_sgn(bf_sub(a, b)); }

/* Round a to the nearest integer (ties toward +infinity: floor(a + 1/2)).
 * The integer is stored in *k as an exact E_INT (used as the multiple of
 * ln 2 or pi/2 in argument reduction) and also returned as a BF. */
static BF bf_round_int(BF a, E **k) {
    /* nearest integer */
    if (a.e >= 0) {
        *k = int_shl(a.m, (uint32_t)a.e);
        return bf_int(*k);
    }
    E *h = int_shl(K->c1, (uint32_t)(-a.e - 1));
    E *q;
    int_fdivmod(int_add(a.m, h), int_shl(K->c1, (uint32_t)(-a.e)), &q, NULL);
    *k = q;
    return bf_int(q);
}

/* --------------------------------------------------------- constants --- */
/* atan(1/n) as a fixed-point integer with scale 2^bits (callers pass
 * bits = P + 16), from the alternating Gregory series
 *     atan(1/n) = sum_k (-1)^k / ((2k+1) n^(2k+1)).
 * term holds 2^bits / n^(2k+1) (truncated); each series term costs two
 * single-limb divisions, no multiplications.  The truncation errors (at most
 * one unit per term) are covered by the 16 extra bits.  Polls for
 * interrupts. */
static E *atan_inv_fixed(long n, long bits) {
    E *one = int_shl(K->c1, (uint32_t)bits);
    E *nn = mk_int(n), *n2 = mk_int(n * n);
    E *term = int_tdiv(one, nn), *sum = term;
    for (long k = 1;; k++) {
        poll_intr();
        term = int_tdiv(term, n2);
        if (int_is_zero(term)) break;
        E *t = int_tdiv(term, mk_int(2 * k + 1));
        sum = (k & 1) ? int_sub(sum, t) : int_add(sum, t);
    }
    return sum;
}

/* pi to P bits by Machin's formula pi/4 = 4 atan(1/5) - atan(1/239).
 * Recomputed on every call (no cache); each call costs about
 * P / log2(25) + P / log2(239^2) series terms. */
static BF bf_pi(void) {
    long bits = P + 16;
    E *a = atan_inv_fixed(5, bits), *b = atan_inv_fixed(239, bits);
    E *pi = int_sub(int_mul(mk_int(16), a), int_mul(mk_int(4), b));
    BF r = {pi, -bits};
    return bf_norm(r);
}

/* ln 2 to P bits in fixed point (scale 2^(P+16)); each term gains log2(9)
 * bits.  Recomputed on every call. */
static BF bf_ln2(void) {
    /* ln 2 = 2 atanh(1/3) = 2 sum 1/((2k+1) 3^(2k+1)) */
    long bits = P + 16;
    E *one = int_shl(K->c1, (uint32_t)bits);
    E *term = int_tdiv(one, mk_int(3)), *sum = term, *nine = mk_int(9);
    for (long k = 1;; k++) {
        poll_intr();
        term = int_tdiv(term, nine);
        if (int_is_zero(term)) break;
        sum = int_add(sum, int_tdiv(term, mk_int(2 * k + 1)));
    }
    BF r = {int_shl(sum, 1), -bits};
    return bf_norm(r);
}

/* -------------------------------------------------------- functions --- */
/* exp(x).
 *  1. k = round(x / ln 2), r = x - k ln 2, so |r| <= ln2 / 2 and
 *     exp(x) = 2^k exp(r);
 *  2. r' = r / 2^12 is tiny, so the Taylor series sum r'^i / i! converges
 *     fast (stops when a term drops below 2^-(P+8));
 *  3. exp(r) = exp(r')^(2^12) by 12 squarings (each squaring doubles the
 *     relative error, costing about 12 of the 40 guard bits);
 *  4. multiply by 2^k exactly.
 * |k| > 10^8 throws CAS_ERR_DOMAIN "exp overflow" (also for large negative
 * x, i.e. underflow), which bf_evalf turns into a NULL result and hence a
 * double-precision fallback.  Note that ln 2 carries only P bits, so k ln 2
 * has absolute error about |k| 2^-P: roughly log2|k| bits are lost for large
 * arguments. */
static BF bf_exp(BF x) {
    BF l2 = bf_ln2();
    E *k;
    bf_round_int(bf_div(x, l2), &k);
    BF r = bf_sub(x, bf_mul(bf_int(k), l2));
    long s = 12;
    r = bf_shift(r, -s);
    /* Taylor */
    BF sum = bf_si(1), term = bf_si(1);
    for (long i = 1; i < 10000; i++) {
        poll_intr();
        term = bf_div_si(bf_mul(term, r), i);
        if (int_is_zero(term.m) || (long)int_bits(term.m) + term.e < -P - 8) break;
        sum = bf_add(sum, term);
    }
    for (long i = 0; i < s; i++) sum = bf_mul(sum, sum);
    long kv;
    int64_t k64;
    if (!int_fits(k, &k64) || k64 > 100000000 || k64 < -100000000) cas_throw(CAS_ERR_DOMAIN, "exp overflow");
    kv = (long)k64;
    return bf_shift(sum, kv);
}

/* ln(x) for x > 0 (CAS_ERR_DOMAIN otherwise).
 *  1. x = y * 2^k with y in [1/2, 1) (exact exponent shuffle);
 *  2. y <- y^(1/64) by 6 square roots, bringing y very close to 1;
 *  3. z = (y-1)/(y+1), ln y = 2 atanh z = 2 sum z^(2j+1) / (2j+1); |z| is
 *     tiny so few terms are needed;
 *  4. ln x = 64 * ln y + k ln 2 (the factor 2 * 64 is the shift by 1 + s).
 * Accuracy caveat: steps 1 and 4 cancel when x is close to 1 (e.g.
 * x = 1 + 10^-50 gives k = 1, ln y ~ -ln 2), so only absolute accuracy
 * ~2^-P survives; bf_evalf raises P until the result is stable. */
static BF bf_ln(BF x) {
    if (bf_sgn(x) <= 0) cas_throw(CAS_ERR_DOMAIN, "ln of non-positive number");
    /* x = y 2^k with y in [1/2, 1) */
    long k = (long)int_bits(x.m) + x.e;
    BF y = bf_shift(x, -k);
    /* y = sqrt(y) a few times for faster convergence */
    long s = 6;
    for (long i = 0; i < s; i++) y = bf_sqrt(y);
    BF z = bf_div(bf_sub(y, bf_si(1)), bf_add(y, bf_si(1)));
    BF z2 = bf_mul(z, z), term = z, sum = z;
    for (long j = 1; j < 100000; j++) {
        poll_intr();
        term = bf_mul(term, z2);
        if (int_is_zero(term.m) || (long)int_bits(term.m) + term.e < -P - 8) break;
        sum = bf_add(sum, bf_div_si(term, 2 * j + 1));
    }
    BF lny = bf_shift(sum, 1 + s);
    return bf_add(lny, bf_mul(bf_si(k), bf_ln2()));
}

/* sin(x) and cos(x) together (both are needed by the double-angle steps).
 *  1. kq = round(x / (pi/2)), r = x - kq * pi/2, |r| <= pi/4;
 *  2. r' = r / 2^10, Taylor series for sin r' and cos r' until both terms
 *     are below 2^-(P+8);
 *  3. 10 double-angle steps sin 2t = 2 sin t cos t, cos 2t = 2 cos^2 t - 1;
 *  4. rotate by kq quarter turns: (sin, cos) of x from those of r according
 *     to kq mod 4.
 * Accuracy caveat: pi/2 is only known to P bits, so kq * pi/2 has absolute
 * error about |x| 2^-P and about log2|x| bits are lost (x = 10^40 at P
 * bits for 30 digits leaves about 2 right); bf_evalf's re-evaluation at a
 * higher P recovers them.  Results at exact zeros of sin / cos (e.g.
 * sin(pi)) stay accurate only in the absolute sense. */
static void bf_sincos(BF x, BF *sn, BF *cs) {
    BF pi = bf_pi();
    BF hp = bf_shift(pi, -1);
    E *kq;
    bf_round_int(bf_div(x, hp), &kq);
    /* reduce: r = x - kq*pi/2, computed at the working precision P */
    BF r = bf_sub(x, bf_mul(bf_int(kq), hp));
    long s = 10;
    r = bf_shift(r, -s);
    BF r2 = bf_mul(r, r);
    BF st = r, ct = bf_si(1), ssum = r, csum = bf_si(1);
    for (long i = 1; i < 10000; i++) {
        poll_intr();
        st = bf_neg(bf_div_si(bf_mul(st, r2), (2 * i) * (2 * i + 1)));
        ct = bf_neg(bf_div_si(bf_mul(ct, r2), (2 * i - 1) * (2 * i)));
        int small = (int_is_zero(st.m) || (long)int_bits(st.m) + st.e < -P - 8) &&
                    (int_is_zero(ct.m) || (long)int_bits(ct.m) + ct.e < -P - 8);
        ssum = bf_add(ssum, st);
        csum = bf_add(csum, ct);
        if (small) break;
    }
    for (long i = 0; i < s; i++) {
        BF ns = bf_shift(bf_mul(ssum, csum), 1);
        BF nc = bf_sub(bf_shift(bf_mul(csum, csum), 1), bf_si(1));
        ssum = ns;
        csum = nc;
    }
    /* x = r + q*pi/2: sin(r + pi/2) = cos r, cos(r + pi/2) = -sin r, ... */
    uint32_t q = int_modp(kq, 4);
    switch (q) {
    case 0: *sn = ssum; *cs = csum; break;
    case 1: *sn = csum; *cs = bf_neg(ssum); break;
    case 2: *sn = bf_neg(ssum); *cs = bf_neg(csum); break;
    default: *sn = bf_neg(csum); *cs = ssum; break;
    }
}

/* atan(x).
 *  1. odd symmetry: work with |x|;
 *  2. |x| > 1: atan x = pi/2 - atan(1/x), so the argument is <= 1;
 *  3. 5 half-angle steps x <- x / (1 + sqrt(1 + x^2)) (= tan(t/2) when
 *     x = tan t), each halving the angle, so x <= tan(pi/128);
 *  4. Gregory series atan x = sum (-1)^j x^(2j+1) / (2j+1), times 2^5. */
static BF bf_atan(BF x) {
    int neg = bf_sgn(x) < 0;
    if (neg) x = bf_neg(x);
    BF res;
    int inv = bf_cmp(x, bf_si(1)) > 0;
    if (inv) x = bf_div(bf_si(1), x);
    long s = 5;
    for (long i = 0; i < s; i++) x = bf_div(x, bf_add(bf_si(1), bf_sqrt(bf_add(bf_si(1), bf_mul(x, x)))));
    BF x2 = bf_mul(x, x), term = x, sum = x;
    for (long j = 1; j < 100000; j++) {
        poll_intr();
        term = bf_neg(bf_mul(term, x2));
        if (int_is_zero(term.m) || (long)int_bits(term.m) + term.e < -P - 8) break;
        sum = bf_add(sum, bf_div_si(term, 2 * j + 1));
    }
    res = bf_shift(sum, s);
    if (inv) res = bf_sub(bf_shift(bf_pi(), -1), res);
    return neg ? bf_neg(res) : res;
}

/* --------------------------------------------------------- evaluator --- */
static int bfev(E *e, BF *out);

/* b^k for an integer node k by binary powering (rounding after every
 * multiplication); negative k via 1/b^|k|.  Returns 0 (unsupported, caller
 * falls back to doubles) when |k| > 10^6. */
static int bf_pow_int(BF b, E *k, BF *out) {
    int64_t kv;
    if (!int_fits(k, &kv) || kv > 1000000 || kv < -1000000) return 0;
    int neg = kv < 0;
    uint64_t u = (uint64_t)(neg ? -kv : kv);
    BF r = bf_si(1);
    while (u) {
        if (u & 1) r = bf_mul(r, b);
        u >>= 1;
        if (u) b = bf_mul(b, b);
    }
    *out = neg ? bf_div(bf_si(1), r) : r;
    return 1;
}

/* Recursive evaluator: on success stores the value of the constant real
 * expression e in *out and returns 1; returns 0 if e contains anything
 * unsupported or leaves the real domain (the caller then uses doubles).
 * Supported: exact numbers, floats (taken at their exact binary value, so
 * they contribute only 53 correct bits), pi, e, sums, products, powers
 * (e^x; integer exponents; exponents p/2 via sqrt; otherwise exp(x ln b)
 * for b > 0, and in real mode (-1)^p exp(x ln |b|) for b < 0 and x = p/q,
 * q odd), and the unary functions listed in the switch.  sqrt and exp
 * need no cases because the simplifier represents them as powers (x^(1/2),
 * e^x).  Errors thrown by the arithmetic (division by zero, exp overflow)
 * propagate to bf_evalf's handler.  Recursion is guarded by stack_check. */
static int bfev(E *e, BF *out) {
    stack_check();
    switch (e->t) {
    case E_INT: *out = bf_int(e); return 1;
    case E_RAT: *out = bf_rat(e); return 1;
    case E_FLT: *out = bf_rat(num_from_double(EF(e))); return 1;
    case E_SYM:
        if (e->s == S_PI) { *out = bf_pi(); return 1; }
        if (e->s == S_E) { *out = bf_exp(bf_si(1)); return 1; }
        return 0;
    case E_ADD: {
        BF s = bf_si(0), t;
        for (uint32_t i = 0; i < e->n; i++) {
            if (!bfev(EK(e)[i], &t)) return 0;
            s = bf_add(s, t);
        }
        *out = s;
        return 1;
    }
    case E_MUL: {
        BF s = bf_si(1), t;
        for (uint32_t i = 0; i < e->n; i++) {
            if (!bfev(EK(e)[i], &t)) return 0;
            s = bf_mul(s, t);
        }
        *out = s;
        return 1;
    }
    case E_POW: {
        BF b, x;
        if (ISYM(K0(e), S_E)) {
            if (!bfev(K1(e), &x)) return 0;
            *out = bf_exp(x);
            return 1;
        }
        if (!bfev(K0(e), &b)) return 0;
        if (K1(e)->t == E_INT) return bf_pow_int(b, K1(e), out);
        if (K1(e)->t == E_RAT && is_int_val(K1(K1(e)), 2)) {
            if (bf_sgn(b) < 0) return 0;
            BF r = bf_sqrt(b);
            return bf_pow_int(r, K0(K1(e)), out);
        }
        if (!bfev(K1(e), &x)) return 0;
        int podd;
        if (bf_sgn(b) < 0 && real_odd_root(K1(e), &podd)) {
            /* real mode, exponent p/q with q odd: (-1)^p |b|^(p/q) */
            BF r = bf_exp(bf_mul(x, bf_ln(bf_neg(b))));
            *out = podd ? bf_neg(r) : r;
            return 1;
        }
        if (bf_sgn(b) <= 0) return 0;
        *out = bf_exp(bf_mul(x, bf_ln(b)));
        return 1;
    }
    case E_FN: {
        if (e->n != 1) return 0;
        BF a;
        if (!bfev(K0(e), &a)) return 0;
        BF s, c, one = bf_si(1);
        switch (e->s) {
        case F_SIN: bf_sincos(a, &s, &c); *out = s; return 1;
        case F_COS: bf_sincos(a, &s, &c); *out = c; return 1;
        case F_TAN: bf_sincos(a, &s, &c); *out = bf_div(s, c); return 1;
        case F_COT: bf_sincos(a, &s, &c); *out = bf_div(c, s); return 1;
        case F_SEC: bf_sincos(a, &s, &c); *out = bf_div(one, c); return 1;
        case F_CSC: bf_sincos(a, &s, &c); *out = bf_div(one, s); return 1;
        case F_LN: if (bf_sgn(a) <= 0) return 0; *out = bf_ln(a); return 1;
        case F_ATAN: *out = bf_atan(a); return 1;
        case F_ASIN: case F_ACOS: {
            /* asin a = atan(a / sqrt(1 - a^2)) for |a| < 1, +-pi/2 at the
             * endpoints; acos a = pi/2 - asin a (cancels for a near 1) */
            int c1 = bf_cmp(a, one), c2 = bf_cmp(a, bf_neg(one));
            if (c1 > 0 || c2 < 0) return 0;
            BF as;
            if (c1 == 0) as = bf_shift(bf_pi(), -1);
            else if (c2 == 0) as = bf_neg(bf_shift(bf_pi(), -1));
            else as = bf_atan(bf_div(a, bf_sqrt(bf_sub(one, bf_mul(a, a)))));
            *out = e->s == F_ASIN ? as : bf_sub(bf_shift(bf_pi(), -1), as);
            return 1;
        }
        case F_SINH: case F_COSH: case F_TANH: {
            /* via e^a and e^-a.  For |a| << 1, sinh and tanh suffer
             * cancellation in p - m, compensated by bf_evalf's guard
             * precision. */
            BF p = bf_exp(a), m = bf_div(one, p);
            if (e->s == F_SINH) *out = bf_shift(bf_sub(p, m), -1);
            else if (e->s == F_COSH) *out = bf_shift(bf_add(p, m), -1);
            else *out = bf_div(bf_sub(p, m), bf_add(p, m));
            return 1;
        }
        case F_ASINH: {
            int neg = bf_sgn(a) < 0;
            if (neg) a = bf_neg(a);
            BF r = bf_ln(bf_add(a, bf_sqrt(bf_add(bf_mul(a, a), one))));
            *out = neg ? bf_neg(r) : r;
            return 1;
        }
        case F_ACOSH:
            if (bf_cmp(a, one) < 0) return 0;
            *out = bf_ln(bf_add(a, bf_sqrt(bf_sub(bf_mul(a, a), one))));
            return 1;
        case F_ATANH:
            if (bf_cmp(a, one) >= 0 || bf_cmp(a, bf_neg(one)) <= 0) return 0;
            *out = bf_shift(bf_ln(bf_div(bf_add(one, a), bf_sub(one, a))), -1);
            return 1;
        case F_ABS: *out = bf_sgn(a) < 0 ? bf_neg(a) : a; return 1;
        default: return 0;
        }
    }
    default: return 0;
    }
}

/* Number of leading bits on which a and b agree: -(log2 |a - b| / |b|),
 * roughly; LONG_MAX if they are equal, 0 if exactly one is zero. */
static long bf_agree(BF a, BF b) {
    BF d = bf_sub(a, b);
    if (int_is_zero(d.m)) return LONG_MAX;
    if (int_is_zero(b.m)) return 0;
    long r = ((long)int_bits(b.m) + b.e) - ((long)int_bits(d.m) + d.e);
    return r < 0 ? 0 : r;
}

/* evaluate a constant real expression to `digits` decimal digits; returns a
 * float node carrying the exact binary mantissa, or NULL
 *
 * Guard digits against cancellation (ln(1+10^-50), sin(10^40), sinh of a
 * tiny argument): the expression is evaluated at P0 = 3.33 * digits + 40
 * bits and again at P0 + 32; if the two values agree to 3.33 * digits + 8
 * bits the second is used, otherwise the precision is raised by the number
 * of bits lost plus 64 (at most to 4 P0 + 256) and the comparison repeated,
 * for at most 5 evaluations (the last value is used even if it still
 * disagrees, e.g. for a result that is exactly 0).  Typical cost: about
 * 2.2 times one evaluation.
 *
 * The working precision P is set for the duration of the call (restored on
 * every exit path) and bfev runs under a local error handler.  NULL means "not representable here": unsupported subexpression or any error other than
 * CAS_ERR_INTERRUPTED / CAS_ERR_MEMORY, which are re-thrown.  The returned
 * node stores the full working-precision mantissa (rounding to `digits` is
 * left to the printer) in the extended E_FLT layout described at the top
 * of this file; its double is the nearest double so that ordinary float
 * arithmetic on it still works.  Scratch used by the computation is not
 * released (it is reclaimed with the rest of the evaluation). */
E *bf_evalf(E *e, int digits) {
    long saved = P;
    long p0 = (long)(digits * 3.33) + 40, want = (long)(digits * 3.33) + 8;
    tryf tf;
    try_enter(&tf);
    E *volatile res = NULL;
    if (!setjmp(tf.jb)) {
        BF v, prev;
        int ok = 0;
        P = p0;
        for (int att = 0; att < 5; att++) {
            ok = bfev(e, &v);
            if (!ok) break;
            if (att > 0) {
                long ag = bf_agree(prev, v);
                if (ag >= want) break;
                long np = P + (want - ag) + 64, cap = 4 * p0 + 256;
                if (P >= cap) break;
                P = np < cap ? np : cap;
            } else
                P = p0 + 32;
            prev = v;
        }
        if (ok) {
            /* round to the requested precision with guard bits kept */
            E *m = v.m;
            uint32_t nl = m->n;
            E *node = ealloc(E_FLT, sizeof(double) + 8 + sizeof(limb) * nl);
            node->n = nl;
            node->s = m->s;
            node->f = F_SIMP;
            EF(node) = bf_to_double(v);
            int32_t *hdr = (int32_t *)((char *)(node + 1) + sizeof(double));
            hdr[0] = (int32_t)v.e;
            hdr[1] = digits;
            memcpy((char *)(node + 1) + sizeof(double) + 8, EL(m), sizeof(limb) * nl);
            res = node;
        }
        try_leave(&tf);
    } else {
        try_leave(&tf);
        if (K->err == CAS_ERR_INTERRUPTED || K->err == CAS_ERR_MEMORY) {
            P = saved;
            rethrow();
        }
        res = NULL;
    }
    P = saved;
    return res;
}

/* exact value of an extended float node: (+-)m * 2^e, and its digits
 *
 * Inverse of the packing in bf_evalf.  Returns 0 for a plain double node
 * (n = 0) or a non-float.  Otherwise *val receives the exact value as an
 * integer or a rational with a power-of-two denominator (new scratch
 * nodes) and *digits the digit count requested when it was computed.  A
 * binary exponent beyond +-3000 (exp(-10^6)) is first scaled by 10^-*dexp
 * at the working precision, so that the printer's exact decimal arithmetic
 * stays small; *dexp is 0 otherwise.  Used by the float printer (numeric.c). */
int bigflt_parts(E *node, E **val, int *digits, long *dexp) {
    if (node->t != E_FLT || node->n == 0) return 0;
    int32_t *hdr = (int32_t *)((char *)(node + 1) + sizeof(double));
    E *m = int_alloc(node->n);
    memcpy(EL(m), (char *)(node + 1) + sizeof(double) + 8, sizeof(limb) * node->n);
    m = int_norm(m);
    long ex = hdr[0], k = (long)floor(((long)int_bits(m) + ex) * 0.30102999566398);
    BF p10, t = {m, ex};
    long saved = P;
    P = (long)(hdr[1] * 3.33) + 64;
    *dexp = 0;
    if ((ex > 3000 || ex < -3000) && bf_pow_int(bf_si(10), mk_int(k < 0 ? -k : k), &p10)) {
        t = k > 0 ? bf_div(t, p10) : bf_mul(t, p10);
        m = t.m;
        ex = t.e;
        *dexp = k;
    }
    P = saved;
    E *v = ex >= 0 ? int_shl(m, (uint32_t)ex) : mk_rat(m, int_shl(K->c1, (uint32_t)(-ex)));
    if (node->s) v = num_neg(v);
    *val = v;
    *digits = hdr[1];
    return 1;
}
