/*
 * big.c -- arbitrary precision integers.
 *
 * Every exact number in the CAS bottoms out here: num.c builds rationals
 * from pairs of these integers, poly.c / factor.c / gbasis.c / linalg.c do
 * their coefficient arithmetic with them, bf.c uses them as bigfloat
 * mantissas, and cmds.c exposes the number-theory routines (isprime,
 * ifactor, nextprime, powmod, iegcd, ...).
 *
 * Representation.  An integer is an E_INT node (see cas_int.h): the header
 * holds the sign in `s` (0 = non-negative, 1 = negative) and the limb count
 * in `n`; the payload is n 32-bit limbs, least significant first.  The
 * canonical form has a nonzero top limb, and zero is n = 0 with s = 0
 * (int_norm establishes this).  The limb size is 32 bits so that a limb
 * product fits the 64-bit `dlimb` and maps onto the Cortex-M7 UMULL / UMLAL
 * instructions.
 *
 * The file has two layers:
 *  - "mpn" layer (static helpers, named after the GMP convention): raw
 *    little-endian limb arrays with explicit lengths, no signs, no nodes.
 *    The caller provides the output buffer.  Temporary buffers come from
 *    scratch and are released with smark/srelease before returning.
 *  - E level (int_*): sign handling, allocation of result nodes in scratch,
 *    and the algorithms built on top.  Arguments are never modified; a
 *    result may be one of the arguments itself (e.g. int_add(a, 0) returns
 *    a), which is fine because nodes are immutable.
 *
 * Algorithms (references: Knuth, TAOCP vol. 2, 3rd ed., sec. 4.3 and 4.5;
 * Crandall & Pomerance, "Prime Numbers: A Computational Perspective"):
 *  - addition / subtraction: schoolbook with carry propagation in a dlimb;
 *  - multiplication: schoolbook (Knuth 4.3.1 algorithm M) below KARA_THRESH
 *    limbs, Karatsuba (Knuth 4.3.3) above it, with unbalanced operands cut
 *    into balanced chunks;
 *  - division: Knuth 4.3.1 algorithm D (normalise, estimate qhat from the
 *    top two limbs, correct, multiply-subtract, add back), in the variant
 *    popularised by Warren, "Hacker's Delight" (divmnu); single-limb
 *    divisors use a simple short division;
 *  - gcd: Euclid's algorithm with a 64-bit fast path once both operands
 *    fit; extended Euclid for Bezout coefficients;
 *  - integer k-th roots: Newton's iteration on integers (decreasing from
 *    an over-estimate);
 *  - primality: Miller-Rabin; deterministic for 32-bit inputs with bases
 *    {2, 7, 61} (Jaeschke) and for n < 3.3e24 with the first 12 primes as
 *    bases (Sorenson & Webster), plus 4 random bases above that;
 *  - factoring: trial division to 50000, perfect-power detection, then
 *    Pollard's rho with Brent's cycle detection and batched gcds
 *    (Brent 1980, "An improved Monte Carlo factorization algorithm").
 *
 * Memory: results live in scratch.  Iterative algorithms (gcd, powmod,
 * iroot, nextprime, rho, factorial) compact their loop state with skeep so
 * they run in memory proportional to the operand size rather than the
 * number of iterations.  Errors: division by zero raises CAS_ERR_DOMAIN;
 * results that would be absurdly large (int_pow, int_fact) raise
 * CAS_ERR_MEMORY up front; salloc raises CAS_ERR_MEMORY when scratch is
 * exhausted, and long loops poll for interrupts (CAS_ERR_INTERRUPTED).
 */
#include "cas_int.h"
#include <string.h>
#include <math.h>

/* Operand size (in limbs of the shorter factor) from which Karatsuba beats
 * the schoolbook loop.  Tuned empirically; the crossover is fairly flat. */
#define KARA_THRESH 32

/* ---------------------------------------------------------- mpn layer --- */
/* Length of a[0..n) without its leading (most significant) zero limbs. */
static uint32_t mpn_norm(const limb *a, uint32_t n) {
    while (n && a[n - 1] == 0) n--;
    return n;
}

/* Compare the magnitudes a[0..an) and b[0..bn); returns -1, 0 or 1.  Leading
 * zero limbs are ignored, so the inputs need not be normalised. */
static int mpn_cmp(const limb *a, uint32_t an, const limb *b, uint32_t bn) {
    an = mpn_norm(a, an);
    bn = mpn_norm(b, bn);
    if (an != bn) return an < bn ? -1 : 1;
    while (an--) {
        if (a[an] != b[an]) return a[an] < b[an] ? -1 : 1;
    }
    return 0;
}

/* r = a + b for an >= bn.  r must have room for an+1 limbs (it may alias a).
 * The carry is propagated in a 64-bit dlimb: each step adds two limbs plus
 * a carry of at most 1, so the sum fits in 33 bits.  The final carry is
 * stored in r[an] and the returned length is always an+1 (the caller
 * normalises).  O(an). */
static uint32_t mpn_add(limb *r, const limb *a, uint32_t an, const limb *b, uint32_t bn) {
    dlimb c = 0;
    uint32_t i;
    for (i = 0; i < bn; i++) {
        c += (dlimb)a[i] + b[i];
        r[i] = (limb)c;
        c >>= 32;
    }
    for (; i < an; i++) {
        c += a[i];
        r[i] = (limb)c;
        c >>= 32;
    }
    r[i] = (limb)c;
    return an + 1;
}

/* r = a - b; requires a >= b as magnitudes and an >= bn (r has an limbs and
 * may alias a).  The borrow is kept in a signed 64-bit accumulator: after
 * each limb, c >> 32 (an arithmetic shift) is 0 or -1.  Returns an; the
 * result may have leading zeros. */
static uint32_t mpn_sub(limb *r, const limb *a, uint32_t an, const limb *b, uint32_t bn) {
    int64_t c = 0;
    uint32_t i;
    for (i = 0; i < bn; i++) {
        c += (int64_t)a[i] - b[i];
        r[i] = (limb)c;
        c >>= 32;
    }
    for (; i < an; i++) {
        c += a[i];
        r[i] = (limb)c;
        c >>= 32;
    }
    return an;
}

/* r[0..rn) += x[0..xn) in place.  Limbs of x beyond rn and a carry out of
 * r[rn-1] are dropped: the Karatsuba code only adds partial products whose
 * true sum fits in the destination, so any such limbs are known to be 0.
 * The second loop stops as soon as the carry dies out. */
static void mpn_add_in(limb *r, uint32_t rn, const limb *x, uint32_t xn) {
    dlimb c = 0;
    uint32_t i;
    for (i = 0; i < xn && i < rn; i++) {
        c += (dlimb)r[i] + x[i];
        r[i] = (limb)c;
        c >>= 32;
    }
    for (; c && i < rn; i++) {
        c += r[i];
        r[i] = (limb)c;
        c >>= 32;
    }
}

/* r[0..rn) -= x[0..xn) in place.  The caller guarantees r >= x (no
 * underflow); used by Karatsuba to form z1 - z0 - z2, which is a
 * non-negative cross product. */
static void mpn_sub_in(limb *r, uint32_t rn, const limb *x, uint32_t xn) {
    int64_t c = 0;
    uint32_t i;
    for (i = 0; i < xn && i < rn; i++) {
        c += (int64_t)r[i] - x[i];
        r[i] = (limb)c;
        c >>= 32;
    }
    for (; c && i < rn; i++) {
        c += r[i];
        r[i] = (limb)c;
        c >>= 32;
    }
}

/* Schoolbook product r[0..an+bn) = a * b (Knuth 4.3.1 algorithm M).  r must
 * not alias a or b.  The inner step a[i]*b[j] + r[i+j] + carry is at most
 * (2^32-1)^2 + 2*(2^32-1) = 2^64 - 1, so it never overflows a dlimb.  Zero
 * limbs of b are skipped (common for small or shifted operands).  O(an*bn). */
static void mpn_mul_base(limb *r, const limb *a, uint32_t an, const limb *b, uint32_t bn) {
    memset(r, 0, sizeof(limb) * (an + bn));
    for (uint32_t j = 0; j < bn; j++) {
        dlimb c = 0, bj = b[j];
        if (!bj) continue;
        for (uint32_t i = 0; i < an; i++) {
            c += a[i] * bj + r[i + j];
            r[i + j] = (limb)c;
            c >>= 32;
        }
        r[an + j] = (limb)c;
    }
}

/* r[0..an+bn) = a * b.  The operands are swapped if needed so that an >= bn;
 * r must not alias either operand.  Temporaries are taken from scratch and
 * released before returning (r itself is caller-owned).
 *
 * Three cases:
 *  1. bn < KARA_THRESH: schoolbook.
 *  2. an >= 2*bn (unbalanced): Karatsuba would waste work splitting a long
 *     operand against a short one, so a is cut into bn-limb chunks, each
 *     chunk is multiplied by b recursively (balanced products), and the
 *     partial products are added into r at the chunk offsets.
 *  3. balanced (bn <= an < 2*bn): Karatsuba with split point m = an/2.
 *     Writing B = 2^(32m), a = a1*B + a0, b = b1*B + b0:
 *         a*b = z2*B^2 + (z1 - z2 - z0)*B + z0,
 *         z0 = a0*b0, z2 = a1*b1, z1 = (a0+a1)*(b0+b1),
 *     i.e. three half-size products instead of four, giving
 *     O(n^log2(3)) = O(n^1.585).  z0 is computed directly into r[0..2m)
 *     and z2 into r[2m..an+bn) (the two ranges tile r exactly); the middle
 *     term is then added at offset m.
 * The interrupt flag is polled once per Karatsuba node so huge products
 * remain interruptible. */
static void mpn_mul(limb *r, const limb *a, uint32_t an, const limb *b, uint32_t bn) {
    if (an < bn) {
        const limb *t = a; a = b; b = t;
        uint32_t tn = an; an = bn; bn = tn;
    }
    if (bn < KARA_THRESH) {
        mpn_mul_base(r, a, an, b, bn);
        return;
    }
    char *mark = smark();
    if (an >= 2 * bn) {
        /* unbalanced: chunk a into pieces of bn limbs */
        memset(r, 0, sizeof(limb) * (an + bn));
        limb *t = salloc(sizeof(limb) * 2 * bn);
        for (uint32_t off = 0; off < an; off += bn) {
            uint32_t len = an - off < bn ? an - off : bn;
            mpn_mul(t, a + off, len, b, bn);
            mpn_add_in(r + off, an + bn - off, t, len + bn);
        }
        srelease(mark);
        return;
    }
    poll_intr();
    uint32_t m = an / 2;
    /* Split b at m as well.  Since an < 2*bn here, bn > m always holds, so
     * this branch (b entirely in the low half) appears to be unreachable;
     * it would compute a0*b and a1*b separately. */
    if (bn <= m) {
        memset(r, 0, sizeof(limb) * (an + bn));
        limb *t = salloc(sizeof(limb) * (an + bn));
        mpn_mul(t, a, m, b, bn);
        mpn_add_in(r, an + bn, t, m + bn);
        mpn_mul(t, a + m, an - m, b, bn);
        mpn_add_in(r + m, an + bn - m, t, an - m + bn);
        srelease(mark);
        return;
    }
    /* Halves: a0, b0 have m limbs (possibly with leading zeros); a1 has
     * an - m >= m limbs and b1 has bn - m limbs, which can be m + 1 when an
     * is odd and bn == an.  The sums sa = a0 + a1 and sb = b0 + b1 get one
     * extra limb for the carry. */
    const limb *a0 = a, *a1 = a + m, *b0 = b, *b1 = b + m;
    uint32_t a1n = an - m, b1n = bn - m;
    limb *sa = salloc(sizeof(limb) * (a1n + 1));
    limb *sb = salloc(sizeof(limb) * (m + 1 > b1n + 1 ? m + 1 : b1n + 1));
    uint32_t san = mpn_add(sa, a1, a1n, a0, m); /* a1n >= m */
    uint32_t sbn = (m >= b1n) ? mpn_add(sb, b0, m, b1, b1n) : mpn_add(sb, b1, b1n, b0, m);
    limb *z1 = salloc(sizeof(limb) * (san + sbn));
    mpn_mul(z1, sa, san, sb, sbn);
    uint32_t z1n = san + sbn;
    /* z0 into r[0..2m), z2 into r[2m..) */
    mpn_mul(r, a0, m, b0, m);
    mpn_mul(r + 2 * m, a1, a1n, b1, b1n);
    /* z1 - z0 - z2 = a0*b1 + a1*b0 >= 0, then add it in at B^1 */
    mpn_sub_in(z1, z1n, r, 2 * m);
    mpn_sub_in(z1, z1n, r + 2 * m, a1n + b1n);
    mpn_add_in(r + m, an + bn - m, z1, z1n);
    srelease(mark);
}

/* Short division by a single nonzero limb: q[0..an) = a / d, returns a mod d.
 * Processes limbs from the top, keeping the running remainder r < d so that
 * (r << 32) | a[i] fits a dlimb.  q may alias a (used for in-place repeated
 * division, e.g. decimal conversion and trial division).  O(an). */
static limb mpn_divrem_1(limb *q, const limb *a, uint32_t an, limb d) {
    dlimb r = 0;
    for (uint32_t i = an; i-- > 0;) {
        dlimb cur = (r << 32) | a[i];
        q[i] = (limb)(cur / d);
        r = cur % d;
    }
    return (limb)r;
}

/* Number of leading zero bits of x (32 for x = 0).  A portable loop instead
 * of __builtin_clz; only used on cold paths (bit lengths, normalisation). */
static int clz32(uint32_t x) {
    int n = 0;
    if (!x) return 32;
    while (!(x & 0x80000000u)) {
        x <<= 1;
        n++;
    }
    return n;
}

/* Long division, Knuth TAOCP vol. 2, 4.3.1 algorithm D (structured like
 * divmnu in Warren, "Hacker's Delight", 2nd ed., sec. 9-2).
 * a: an limbs, b: bn limbs with bn >= 2, b[bn-1] != 0, an >= bn.
 * q gets an-bn+1 quotient limbs and r gets bn remainder limbs (either may be
 * NULL when not wanted); neither may alias a or b.  The normalised copies
 * un, vn are scratch temporaries released before returning.  Polls for
 * interrupts every 64 quotient limbs.  O((an-bn+1) * bn).
 *
 * Steps:
 *  D1  normalise: shift b left by s bits so its top limb has the high bit
 *      set (vn), and shift a by the same amount into an+1 limbs (un).  This
 *      makes the quotient-digit estimate below off by at most 2.
 *  D3  for each quotient position j (from the top), estimate
 *      qhat = (un[j+bn]*B + un[j+bn-1]) / vn[bn-1] and correct it with the
 *      second divisor limb until qhat*vn[bn-2] <= rhat*B + un[j+bn-2]
 *      (after this qhat is exact or one too large).
 *  D4  multiply and subtract qhat*vn from un[j..j+bn].
 *  D6  if that went negative, qhat was one too large: decrement it and add
 *      vn back (this happens with probability about 2/B).
 *  D8  the remainder is un[0..bn) shifted right by s bits. */
static void mpn_divrem(limb *q, limb *r, const limb *a, uint32_t an, const limb *b, uint32_t bn) {
    char *mark = smark();
    int s = clz32(b[bn - 1]);
    limb *vn = salloc(sizeof(limb) * bn);
    limb *un = salloc(sizeof(limb) * (an + 1));
    if (s) {
        for (uint32_t i = bn - 1; i > 0; i--) vn[i] = (b[i] << s) | (b[i - 1] >> (32 - s));
        vn[0] = b[0] << s;
        un[an] = a[an - 1] >> (32 - s);
        for (uint32_t i = an - 1; i > 0; i--) un[i] = (a[i] << s) | (a[i - 1] >> (32 - s));
        un[0] = a[0] << s;
    } else {
        memcpy(vn, b, sizeof(limb) * bn);
        memcpy(un, a, sizeof(limb) * an);
        un[an] = 0;
    }
    const dlimb B = (dlimb)1 << 32;
    for (int64_t j = (int64_t)an - bn; j >= 0; j--) {
        if ((j & 63) == 0) poll_intr();
        dlimb num = ((dlimb)un[j + bn] << 32) | un[j + bn - 1];
        dlimb qhat = num / vn[bn - 1];
        dlimb rhat = num % vn[bn - 1];
        /* qhat >= B is tested first so that qhat * vn[bn-2] cannot
         * overflow; once rhat >= B the test can no longer succeed. */
        while (qhat >= B || qhat * vn[bn - 2] > ((rhat << 32) | un[j + bn - 2])) {
            qhat--;
            rhat += vn[bn - 1];
            if (rhat >= B) break;
        }
        /* multiply-subtract; k carries the high half of each product plus
         * the borrow into the next limb */
        int64_t k = 0, t;
        for (uint32_t i = 0; i < bn; i++) {
            dlimb p = qhat * vn[i];
            t = (int64_t)un[i + j] - k - (int64_t)(p & 0xFFFFFFFFu);
            un[i + j] = (limb)t;
            k = (int64_t)(p >> 32) - (t >> 32);
        }
        t = (int64_t)un[j + bn] - k;
        un[j + bn] = (limb)t;
        if (t < 0) { /* D6: add back */
            qhat--;
            dlimb c = 0;
            for (uint32_t i = 0; i < bn; i++) {
                c += (dlimb)un[i + j] + vn[i];
                un[i + j] = (limb)c;
                c >>= 32;
            }
            un[j + bn] += (limb)c;
        }
        if (q) q[j] = (limb)qhat;
    }
    if (r) {
        if (s) {
            for (uint32_t i = 0; i < bn - 1; i++) r[i] = (un[i] >> s) | (un[i + 1] << (32 - s));
            r[bn - 1] = un[bn - 1] >> s;
        } else
            memcpy(r, un, sizeof(limb) * bn);
    }
    srelease(mark);
}

/* ------------------------------------------------------------ E level --- */
/* New E_INT node in scratch with room for n limbs; the limbs are left
 * uninitialised, s = 0 and n is set to the capacity.  Callers fill the limbs
 * and then call int_norm. */
E *int_alloc(uint32_t n) {
    E *e = ealloc(E_INT, sizeof(limb) * n);
    e->n = n;
    return e;
}

/* Establish the canonical form in place (only valid on a node that is still
 * being built): drop leading zero limbs, clear the sign of zero, and mark the
 * node F_SIMP (an integer is always simplified).  Returns a. */
E *int_norm(E *a) {
    a->n = mpn_norm(EL(a), a->n);
    if (!a->n) a->s = 0;
    a->f = F_SIMP;
    return a;
}

/* Integer node for an unsigned 64-bit value. */
E *mk_uint(uint64_t v) {
    E *e = int_alloc(2);
    EL(e)[0] = (limb)v;
    EL(e)[1] = (limb)(v >> 32);
    return int_norm(e);
}

/* Integer node for a signed 64-bit value: build |v| and set the sign.  |v|
 * is computed in uint64_t so that INT64_MIN does not overflow. */
E *mk_int(int64_t v) {
    if (v >= 0) return mk_uint((uint64_t)v);
    E *e = mk_uint(0 - (uint64_t)v);
    e->s = 1;
    return e;
}

/* Constant-time predicates on the header / lowest limb. */
int int_sgn(const E *a) { return a->n == 0 ? 0 : (a->s ? -1 : 1); }
int int_is_zero(const E *a) { return a->n == 0; }
int int_is_one(const E *a) { return a->n == 1 && !a->s && EL(a)[0] == 1; }
int int_is_even(const E *a) { return a->n == 0 || !(EL(a)[0] & 1); }

/* Compare |a| with |b|: -1, 0 or 1. */
int int_cmpabs(const E *a, const E *b) { return mpn_cmp(EL(a), a->n, EL(b), b->n); }

/* Signed comparison: -1, 0 or 1.  Different signs decide immediately;
 * otherwise compare magnitudes and flip for negatives. */
int int_cmp(const E *a, const E *b) {
    int sa = int_sgn(a), sb = int_sgn(b);
    if (sa != sb) return sa < sb ? -1 : 1;
    int c = int_cmpabs(a, b);
    return sa < 0 ? -c : c;
}

/* If a fits in int64_t, store it in *out (when out is non-NULL) and return 1;
 * otherwise return 0.  The asymmetric range check admits -2^63 but not
 * +2^63.  Used everywhere a small machine integer is needed (exponents,
 * degrees, loop bounds). */
int int_fits(const E *a, int64_t *out) {
    if (a->n > 2) return 0;
    uint64_t v = a->n == 0 ? 0 : a->n == 1 ? EL(a)[0] : ((uint64_t)EL(a)[1] << 32 | EL(a)[0]);
    if (a->s) {
        if (v > (uint64_t)1 << 63) return 0;
        if (out) *out = (int64_t)(0 - v);
    } else {
        if (v >= (uint64_t)1 << 63) return 0;
        if (out) *out = (int64_t)v;
    }
    return 1;
}

/* Fresh scratch copy of a (needed before changing the sign, since nodes are
 * immutable). */
static E *int_copy(const E *a) {
    E *r = int_alloc(a->n);
    memcpy(EL(r), EL(a), sizeof(limb) * a->n);
    r->s = a->s;
    r->f = F_SIMP;
    return r;
}

/* -a (zero is returned as is) and |a| (a itself when already >= 0). */
E *int_neg(E *a) {
    if (!a->n) return a;
    E *r = int_copy(a);
    r->s = !a->s;
    return r;
}

E *int_abs(E *a) { return a->s ? int_neg(a) : a; }

/* Signed addition core shared by int_add and int_sub: computes
 * (-1)^sa*|a| + (-1)^sb*|b|, where sa, sb are explicit signs that override
 * the nodes' own (int_sub passes the flipped sign of b).  Equal signs add the
 * magnitudes; different signs subtract the smaller magnitude from the larger
 * and take the sign of the larger.  Returns a new normalised node. */
static E *add_signed(const E *a, int sa, const E *b, int sb) {
    /* sa, sb: 0 positive, 1 negative */
    const E *x = a, *y = b;
    if (sa == sb) {
        if (x->n < y->n) { const E *t = x; x = y; y = t; }
        E *r = int_alloc(x->n + 1);
        mpn_add(EL(r), EL(x), x->n, EL(y), y->n);
        r->s = (uint16_t)sa;
        return int_norm(r);
    }
    int c = mpn_cmp(EL(a), a->n, EL(b), b->n);
    if (c == 0) return mk_int(0);
    if (c < 0) {
        const E *t = x; x = y; y = t;
        sa = sb;
    }
    E *r = int_alloc(x->n);
    mpn_sub(EL(r), EL(x), x->n, EL(y), y->n);
    r->s = (uint16_t)sa;
    return int_norm(r);
}

/* a + b and a - b.  A zero operand short-circuits and returns the other
 * argument itself (no copy). */
E *int_add(E *a, E *b) {
    if (!a->n) return b;
    if (!b->n) return a;
    return add_signed(a, a->s, b, b->s);
}

/* a - b = a + (-b): add_signed with b's sign flipped. */
E *int_sub(E *a, E *b) {
    if (!b->n) return a;
    if (!a->n) return int_neg(b);
    return add_signed(a, a->s, b, !b->s);
}

/* a * b.  The 1x1-limb case (by far the most common in practice) is done
 * inline; everything else goes through mpn_mul (schoolbook / Karatsuba). */
E *int_mul(E *a, E *b) {
    if (!a->n || !b->n) return mk_int(0);
    E *r = int_alloc(a->n + b->n);
    if (a->n == 1 && b->n == 1) {
        dlimb p = (dlimb)EL(a)[0] * EL(b)[0];
        EL(r)[0] = (limb)p;
        EL(r)[1] = (limb)(p >> 32);
    } else
        mpn_mul(EL(r), EL(a), a->n, EL(b), b->n);
    r->s = (uint16_t)(a->s ^ b->s);
    return int_norm(r);
}

/* a * b for a machine integer b. */
E *int_mul_si(E *a, int64_t b) { return int_mul(a, mk_int(b)); }

/* Truncating division (C semantics): a = q*b + r with q rounded toward zero
 * and r carrying the sign of a, |r| < |b|.  Either output pointer may be
 * NULL.  Throws CAS_ERR_DOMAIN "division by zero" if b = 0.  When |a| < |b|
 * the quotient is 0 and r is a itself.  Single-limb divisors use the short
 * division, others Knuth D. */
void int_tdivmod(E *a, E *b, E **q, E **r) {
    if (!b->n) cas_throw(CAS_ERR_DOMAIN, "division by zero");
    if (mpn_cmp(EL(a), a->n, EL(b), b->n) < 0) {
        if (q) *q = mk_int(0);
        if (r) *r = a;
        return;
    }
    E *qq = int_alloc(a->n - b->n + 1), *rr;
    if (b->n == 1) {
        limb rem = mpn_divrem_1(EL(qq), EL(a), a->n, EL(b)[0]);
        qq->n = a->n;
        rr = mk_uint(rem);
    } else {
        rr = int_alloc(b->n);
        mpn_divrem(EL(qq), EL(rr), EL(a), a->n, EL(b), b->n);
    }
    qq->s = (uint16_t)(a->s ^ b->s);
    rr->s = a->s;
    int_norm(qq);
    int_norm(rr);
    if (q) *q = qq;
    if (r) *r = rr;
}

/* Floor division: q = floor(a/b) and r = a - q*b, which has the sign of b
 * (or is 0).  Derived from the truncating result by moving q down one and r
 * by b whenever the truncated remainder has the wrong sign.  Same error
 * behaviour and NULL conventions as int_tdivmod. */
void int_fdivmod(E *a, E *b, E **q, E **r) {
    E *qq, *rr;
    int_tdivmod(a, b, &qq, &rr);
    if (rr->n && (rr->s != b->s)) {
        qq = int_sub(qq, mk_int(1));
        rr = int_add(rr, b);
    }
    if (q) *q = qq;
    if (r) *r = rr;
}

/* Truncated quotient only.  int_divexact is the same operation, named for
 * call sites that know b divides a (no check is made, and no faster exact
 * division algorithm is used). */
E *int_tdiv(E *a, E *b) {
    E *q;
    int_tdivmod(a, b, &q, NULL);
    return q;
}
E *int_divexact(E *a, E *b) { return int_tdiv(a, b); }

/* Floor remainder a mod b (sign of b; in [0, b) for b > 0). */
E *int_mod(E *a, E *b) {
    E *r;
    int_fdivmod(a, b, NULL, &r);
    return r;
}

/* |a| mod m for a word-sized modulus m > 0, without allocating (Horner over
 * the limbs from the top).  Used for trial division and modular images in
 * the factoriser. */
uint32_t int_mod_u(const E *a, uint32_t m) {
    dlimb r = 0;
    for (uint32_t i = a->n; i-- > 0;) r = ((r << 32) | EL(a)[i]) % m;
    return (uint32_t)r;
}

/* a mod p in [0, p), respecting the sign of a (image of a in Z/pZ). */
uint32_t int_modp(const E *a, uint32_t p) {
    uint32_t r = int_mod_u(a, p);
    if (a->s && r) r = p - r;
    return r;
}

/* Euclid on machine words (the fast path of int_gcd). */
static uint64_t gcd64(uint64_t a, uint64_t b) {
    while (b) {
        uint64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

/* gcd(a, b) >= 0 (gcd(0, 0) = 0), by Euclid's algorithm (Knuth 4.5.2
 * algorithm A).  Each step replaces (a, b) by (b, a mod b) with a full
 * Knuth-D division; once both operands fit in 64 bits the rest is done by
 * gcd64.  Every 16 iterations the pair is compacted with skeep (packed into a
 * temporary 2-element list because skeep preserves a single root), so the
 * loop uses O(size of operands) scratch.  The result is skeep'ed to the mark
 * taken on entry.  Number of steps O(log min(a, b)) (Lame), each step
 * quadratic in the limb count; no Lehmer or binary acceleration. */
E *int_gcd(E *a, E *b) {
    a = int_abs(a);
    b = int_abs(b);
    char *mark = smark();
    unsigned it = 0;
    while (b->n) {
        if (a->n <= 2 && b->n <= 2) {
            uint64_t x = a->n == 0 ? 0 : a->n == 1 ? EL(a)[0] : ((uint64_t)EL(a)[1] << 32 | EL(a)[0]);
            uint64_t y = b->n == 1 ? EL(b)[0] : ((uint64_t)EL(b)[1] << 32 | EL(b)[0]);
            E *g = mk_uint(gcd64(x, y));
            return skeep(mark, g);
        }
        E *r;
        int_tdivmod(a, b, NULL, &r);
        a = b;
        b = r;
        if ((++it & 15) == 0) {
            E *pr[2] = {a, b};
            E *l = skeep(mark, mk_list(2, pr));
            a = EK(l)[0];
            b = EK(l)[1];
        }
    }
    return skeep(mark, a);
}

/* lcm(a, b) = |a/gcd * b| >= 0; 0 if either argument is 0.  Dividing before
 * multiplying keeps the intermediate small. */
E *int_lcm(E *a, E *b) {
    if (!a->n || !b->n) return mk_int(0);
    E *g = int_gcd(a, b);
    return int_abs(int_mul(int_tdiv(a, g), b));
}

/* Extended Euclid (Knuth 4.5.2 algorithm X): returns g = gcd(a, b) >= 0 and
 * sets *s, *t (either may be NULL) to Bezout coefficients with
 * s*a + t*b = g.  Uses floor division; the invariants r_i = s_i*a + t_i*b
 * hold for both rows throughout.  If the last remainder is negative (possible
 * with negative inputs) g, s, t are all negated.  No skeep compaction:
 * scratch grows with the number of steps times the operand size, which is
 * acceptable for the modular-inverse / iegcd uses. */
E *int_egcd(E *a, E *b, E **s, E **t) {
    E *r0 = a, *r1 = b, *s0 = mk_int(1), *s1 = mk_int(0), *t0 = mk_int(0), *t1 = mk_int(1);
    while (r1->n) {
        E *q, *r;
        int_fdivmod(r0, r1, &q, &r);
        r0 = r1; r1 = r;
        E *ns = int_sub(s0, int_mul(q, s1)); s0 = s1; s1 = ns;
        E *nt = int_sub(t0, int_mul(q, t1)); t0 = t1; t1 = nt;
    }
    if (r0->s) {
        r0 = int_neg(r0);
        s0 = int_neg(s0);
        t0 = int_neg(t0);
    }
    if (s) *s = s0;
    if (t) *t = t0;
    return r0;
}

/* a^k by right-to-left binary exponentiation
 * (Knuth 4.6.3 algorithm A), O(log k) multiplications.  a^0 = 1.  Throws
 * CAS_ERR_MEMORY "integer too large" up front when the result would exceed
 * about 4 million bits (~1.2 million decimal digits), which keeps absurd
 * inputs like 10^(10^9) from exhausting scratch slowly.  Intermediate
 * squares are not compacted. */
E *int_pow(E *a, uint32_t k) {
    E *r = mk_int(1), *b = a;
    if (a->n && (uint64_t)int_bits(a) * k > 4000000)
        cas_throw(CAS_ERR_MEMORY, "integer too large");
    while (k) {
        if (k & 1) r = int_mul(r, b);
        k >>= 1;
        if (k) b = int_mul(b, b);
    }
    return r;
}

/* a^e mod m (floor remainder, so in [0, m) for m > 0) by left-to-right
 * binary exponentiation: scan the bits of e from the top, square, and
 * multiply by the base when the bit is set; every product is reduced mod m.
 * The sign of e is ignored (e is treated as |e|); callers must handle
 * negative exponents.  m = 0 throws CAS_ERR_DOMAIN via the division.  r and
 * the base are compacted with skeep after every bit, so scratch stays
 * O(size of m).  O(bits(e)) multiplications and divisions. */
E *int_powmod(E *a, E *e, E *m) {
    E *r = mk_int(1), *b = int_mod(a, m);
    uint32_t nb = int_bits(e);
    char *mark = smark();
    for (uint32_t i = nb; i-- > 0;) {
        r = int_mod(int_mul(r, r), m);
        if ((EL(e)[i / 32] >> (i % 32)) & 1) r = int_mod(int_mul(r, b), m);
        E *pair[2] = {r, b};
        E *l = skeep(mark, mk_list(2, pair));
        r = EK(l)[0];
        b = EK(l)[1];
    }
    return int_mod(r, m);
}

/* Bit length of |a| (0 for 0): floor(log2|a|) + 1. */
uint32_t int_bits(const E *a) {
    if (!a->n) return 0;
    return 32 * (a->n - 1) + (32 - (uint32_t)clz32(EL(a)[a->n - 1]));
}

/* a * 2^bits.  Shifts the magnitude by whole limbs (w) and a sub-limb amount
 * (s), each source limb spilling into two destination limbs; keeps the
 * sign. */
E *int_shl(E *a, uint32_t bits) {
    if (!a->n) return a;
    uint32_t w = bits / 32, s = bits % 32;
    E *r = int_alloc(a->n + w + 1);
    memset(EL(r), 0, sizeof(limb) * (a->n + w + 1));
    for (uint32_t i = 0; i < a->n; i++) {
        dlimb v = (dlimb)EL(a)[i] << s;
        EL(r)[i + w] |= (limb)v;
        EL(r)[i + w + 1] |= (limb)(v >> 32);
    }
    r->s = a->s;
    return int_norm(r);
}

/* Shift the magnitude right by `bits` and keep the sign, i.e.
 * sign(a) * floor(|a| / 2^bits): truncation toward zero, NOT floor division
 * for negative a (-1 >> 1 gives 0, not -1).  Callers use it on non-negative
 * values (bit lengths, bigfloat mantissas with separate sign, MR's odd part)
 * or rely on truncation. */
E *int_shr(E *a, uint32_t bits) {
    uint32_t w = bits / 32, s = bits % 32;
    if (w >= a->n) return mk_int(0);
    E *r = int_alloc(a->n - w);
    for (uint32_t i = 0; i < a->n - w; i++) {
        dlimb v = EL(a)[i + w];
        if (i + w + 1 < a->n) v |= (dlimb)EL(a)[i + w + 1] << 32;
        EL(r)[i] = (limb)(v >> s);
    }
    r->s = a->s;
    return int_norm(r);
}

/* floor(a^(1/k)) for a >= 0 and k >= 1 by Newton's iteration on integers;
 * *exact (if non-NULL) is set to 1 iff the root is exact (a is a perfect
 * k-th power).  The sign of a is not checked.
 *
 * Start from x0 = 2^ceil(bits(a)/k) >= a^(1/k) and iterate
 *     x' = floor(((k-1)*x + floor(a / x^(k-1))) / k).
 * For x above the root, the iteration (Newton for f(x) = x^k - a, with
 * floors) strictly decreases and never drops below floor(a^(1/k)), by the
 * AM-GM inequality; so the first x with x' >= x is the answer.  Converges
 * quadratically once close.  Loop state is compacted with skeep each step;
 * polls for interrupts.  Used by perfect-power detection in factoring, by
 * exact radical simplification (simp.c) and by bf.c square roots. */
E *int_iroot(E *a, uint32_t k, int *exact) {
    if (a->n == 0 || int_is_one(a) || k == 1) {
        if (exact) *exact = 1;
        return a;
    }
    uint32_t b = int_bits(a);
    if (k >= b) { /* 2 <= a < 2^b <= 2^k, so 1 <= root < 2 and a != 1^k */
        if (exact) *exact = 0;
        return mk_int(1);
    }
    E *kk = mk_int(k), *km1 = mk_int(k - 1);
    char *mark = smark();
    E *x = int_shl(mk_int(1), (b + k - 1) / k); /* >= root */
    for (;;) {
        poll_intr();
        /* y = ((k-1) x + a / x^(k-1)) / k */
        E *y = int_tdiv(int_add(int_mul(km1, x), int_tdiv(a, int_pow(x, k - 1))), kk);
        if (int_cmp(y, x) >= 0) break;
        x = skeep(mark, y);
    }
    x = skeep(mark, x);
    if (exact) {
        char *m2 = smark();
        *exact = int_cmp(int_pow(x, k), a) == 0;
        srelease(m2);
    }
    return x;
}

/* Nearest double (up to the rounding noted at int_to_double_exp); +-HUGE_VAL
 * (infinity) when the value exceeds the double range.  The ex > 2000 test
 * avoids passing huge exponents to ldexp's int argument; ldexp itself
 * overflows to infinity for ex between about 1024 and 2000. */
double int_to_double(const E *a) {
    long ex;
    double m = int_to_double_exp(a, &ex);
    if (ex > 2000) return a->s ? -HUGE_VAL : HUGE_VAL;
    return ldexp(m, (int)ex);
}

/* Returns the value as m * 2^ex with m a double and ex a long, so that
 * numbers far beyond the double range can still be handled (e.g. ratios of
 * huge integers in num_to_double, logarithms of big integers).  For up to 2
 * limbs m is the value itself (exponent 0, rounded once to 53 bits);
 * otherwise m is built from the top three limbs (96 bits) with ex = 32*(n-3).
 * The lower limbs are ignored and the three-term sum may round twice, so the
 * result can differ from the correctly rounded value in the last bit. */
double int_to_double_exp(const E *a, long *ex) {
    if (!a->n) {
        *ex = 0;
        return 0;
    }
    uint32_t nb = int_bits(a);
    double m = 0;
    if (a->n <= 2) {
        uint64_t v = a->n == 1 ? EL(a)[0] : ((uint64_t)EL(a)[1] << 32 | EL(a)[0]);
        m = (double)v;
        *ex = 0;
    } else {
        /* take top 3 limbs */
        uint32_t n = a->n;
        m = (double)EL(a)[n - 1] * 4294967296.0 * 4294967296.0 + (double)EL(a)[n - 2] * 4294967296.0 +
            (double)EL(a)[n - 3];
        *ex = 32L * (n - 3);
        (void)nb;
    }
    return a->s ? -m : m;
}

/* Parse n decimal digits (no sign, no other characters: the parser has
 * already validated them) into a non-negative integer.  Digits are consumed
 * in chunks of 9 (10^9 < 2^32) and the accumulator is updated as
 * r = r * 10^len + chunk, one single-limb multiply-add pass per chunk, so
 * the conversion is O(n^2 / 81) limb operations.  Capacity n/9 + 2 limbs is
 * enough because 9 digits need fewer than 30 bits. */
E *int_from_dec(const char *s, size_t n) {
    uint32_t cap = (uint32_t)(n / 9 + 2);
    E *r = int_alloc(cap);
    uint32_t rn = 0;
    size_t i = 0;
    while (i < n) {
        uint32_t chunk = 0, mul = 1;
        size_t j;
        for (j = 0; j < 9 && i < n; j++, i++) {
            chunk = chunk * 10 + (uint32_t)(s[i] - '0');
            mul *= 10;
        }
        dlimb c = chunk;
        for (uint32_t k = 0; k < rn; k++) {
            c += (dlimb)EL(r)[k] * mul;
            EL(r)[k] = (limb)c;
            c >>= 32;
        }
        if (c) EL(r)[rn++] = (limb)c;
    }
    r->n = rn;
    return int_norm(r);
}

/* Append the decimal representation of a (with '-' if negative) to b.
 * Repeatedly divides a scratch copy of |a| by 10^9 (short division) and
 * collects base-10^9 chunks, then prints the top chunk without padding and
 * the rest zero-padded to 9 digits.  Quadratic in the size of a (no
 * divide-and-conquer conversion).  If the output would certainly not fit,
 * nothing is written and b->overflow is set instead (the API reports
 * CAS_ERR_OUTPUT_TRUNC), so printing 10^1000000 into a small buffer is
 * cheap.  Scratch temporaries are released; polls for interrupts. */
void int_to_dec(sbuf *b, const E *a) {
    if (!a->n) {
        sb_putc(b, '0');
        return;
    }
    /* more digits than the output can hold: report truncation, skip the
     * (quadratic) conversion */
    /* |a| >= 2^(bits-1), which has floor((bits-1)*log10(2)) + 1 digits */
    size_t mindigits = (size_t)((double)(int_bits(a) - 1) * 0.30102999566398) + 1;
    if (b->len + mindigits >= b->cap) {
        b->overflow = 1;
        return;
    }
    char *mark = smark();
    uint32_t n = a->n;
    limb *t = salloc(sizeof(limb) * n);
    memcpy(t, EL(a), sizeof(limb) * n);
    /* a limb holds 32*log10(2) = 9.63 digits, i.e. < 10/9 chunks of 9 */
    uint32_t nchunks = 0;
    uint32_t *chunks = salloc(sizeof(uint32_t) * (n * 10 / 9 + 2));
    while (n) {
        if ((nchunks & 255) == 255) poll_intr();
        chunks[nchunks++] = mpn_divrem_1(t, t, n, 1000000000u);
        n = mpn_norm(t, n);
    }
    if (a->s) sb_putc(b, '-');
    sb_int(b, (long)chunks[nchunks - 1]);
    for (uint32_t i = nchunks - 1; i-- > 0;) {
        char d[9];
        uint32_t v = chunks[i];
        for (int k = 8; k >= 0; k--) {
            d[k] = (char)('0' + v % 10);
            v /= 10;
        }
        sb_putn(b, d, 9);
    }
    srelease(mark);
}

/* n! as an exact integer.  Throws CAS_ERR_MEMORY for n > 20000 (20000! has
 * about 77000 digits).  Small factors are accumulated in a 64-bit word until
 * it exceeds 32 bits (acc <= 2^32 * 20000 < 2^47 cannot overflow), and only
 * then multiplied into the bignum, which cuts the number of bignum
 * multiplications several-fold.  The running product is compacted
 * occasionally with skeep.  This is a linear accumulation (each step is a
 * bignum times a word), not a balanced product tree, so it costs about
 * O(n^2) limb operations and does not benefit from Karatsuba; fine for
 * n <= 20000. */
E *int_fact(uint32_t n) {
    if (n > 20000) cas_throw(CAS_ERR_MEMORY, "factorial argument too large");
    /* batch small factors in acc; flush into r once acc exceeds 32 bits */
    char *mark = smark();
    E *r = mk_int(1);
    uint64_t acc = 1;
    for (uint32_t i = 2; i <= n; i++) {
        if (acc > 0xFFFFFFFFull) {
            r = int_mul(r, mk_uint(acc));
            acc = 1;
            if ((i & 255) == 0) r = skeep(mark, r);
        }
        acc *= i;
    }
    r = int_mul(r, mk_uint(acc));
    return skeep(mark, r);
}

/* ------------------------------------------------------- primality --- *
 * Miller-Rabin strong probable-prime test (Knuth 4.5.4 algorithm P): write
 * n - 1 = d * 2^s with d odd; n passes for base a if a^d = 1 or
 * a^(d*2^r) = -1 (mod n) for some 0 <= r < s.  A composite passes for at most
 * 1/4 of the bases, and for specific base sets no composite below a known
 * bound passes at all. */
/* Word-sized modular multiply and power (the product fits in 64 bits). */
static uint32_t mulmod32(uint32_t a, uint32_t b, uint32_t m) { return (uint32_t)((uint64_t)a * b % m); }
/* a^e mod m by right-to-left binary powering (1 % m handles m = 1). */
static uint32_t powmod32(uint32_t a, uint32_t e, uint32_t m) {
    uint32_t r = 1 % m;
    a %= m;
    while (e) {
        if (e & 1) r = mulmod32(r, a, m);
        a = mulmod32(a, a, m);
        e >>= 1;
    }
    return r;
}

/* Deterministic primality for 32-bit n: trial division by the primes up to
 * 37, then Miller-Rabin with bases 2, 7, 61, which has no strong
 * pseudoprimes below 4,759,123,141 > 2^32 (Jaeschke 1993).  A base that is
 * a multiple of n (only n = 61 here) proves nothing and is skipped. */
static int isprime32(uint32_t n) {
    if (n < 2) return 0;
    static const uint32_t sp[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37};
    for (int i = 0; i < 12; i++) {
        if (n == sp[i]) return 1;
        if (n % sp[i] == 0) return 0;
    }
    uint32_t d = n - 1;
    int s = 0;
    while (!(d & 1)) {
        d >>= 1;
        s++;
    }
    static const uint32_t bases[] = {2, 7, 61};
    for (int i = 0; i < 3; i++) {
        if (bases[i] % n == 0) continue;
        uint32_t x = powmod32(bases[i], d, n);
        if (x == 1 || x == n - 1) continue;
        int ok = 0;
        for (int r = 1; r < s; r++) {
            x = mulmod32(x, x, n);
            if (x == n - 1) {
                ok = 1;
                break;
            }
        }
        if (!ok) return 0;
    }
    return 1;
}

/* 1 if n is (almost certainly) prime, 0 otherwise; n <= 1 and negative n are
 * not prime.  Single-limb n uses the deterministic isprime32.  Larger n:
 * trial division by the primes below 72, then Miller-Rabin with the first 12
 * primes (2..37) as bases, which is deterministic for n < 3.3e24 (Sorenson &
 * Webster 2015), followed by 4 random bases from K->rng (error probability
 * below 4^-4 for any composite that survives the fixed bases, in practice
 * far smaller).  Because K->rng is reseeded at each cas_eval, results are
 * reproducible.  Scratch used is released before returning.  Cost: 16
 * modular exponentiations, i.e. O(16 * bits(n)) multiplications mod n. */
int int_isprime(E *n) {
    if (n->s || n->n == 0) return 0;
    if (n->n == 1) return isprime32(EL(n)[0]);
    static const uint32_t sp[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71};
    for (int i = 0; i < 20; i++)
        if (int_mod_u(n, sp[i]) == 0) return 0;
    char *mark = smark();
    E *one = mk_int(1), *nm1 = int_sub(n, one), *d = nm1;
    uint32_t s = 0;
    while (int_is_even(d)) {
        d = int_shr(d, 1);
        s++;
    }
    int res = 1;
    for (int i = 0; i < 16 && res; i++) {
        E *a = mk_int(i < 12 ? sp[i] : 41 + (rng_next() % 100000));
        E *x = int_powmod(a, d, n);
        if (int_is_one(x) || int_cmp(x, nm1) == 0) continue;
        int ok = 0;
        for (uint32_t r = 1; r < s; r++) {
            x = int_mod(int_mul(x, x), n);
            if (int_cmp(x, nm1) == 0) {
                ok = 1;
                break;
            }
        }
        if (!ok) res = 0;
    }
    srelease(mark);
    return res;
}

/* Smallest prime > n (2 for n < 2): test odd candidates in turn with
 * int_isprime, compacting the candidate with skeep each step. */
E *int_nextprime(E *n) {
    E *one = mk_int(1);
    if (int_cmp(n, mk_int(2)) < 0) return mk_int(2);
    E *c = int_add(n, one);
    if (int_is_even(c) && int_cmp(c, mk_int(2)) != 0) c = int_add(c, one);
    char *mark = smark();
    while (!int_isprime(c)) {
        c = skeep(mark, int_add(c, mk_int(2)));
    }
    return c;
}

/* Pollard's rho with Brent's improvements (Brent 1980; Knuth 4.5.4 algorithm
 * B is the Floyd version): returns a nontrivial factor of the odd composite
 * n (not necessarily prime), or NULL after giving up.
 *
 * Iterate y -> y^2 + c (mod n).  Modulo an unknown prime p | n the sequence
 * becomes periodic after about sqrt(p) steps, at which point
 * gcd(x - y, n) picks up p.  Brent's cycle finding keeps x fixed at
 * positions r = 1, 2, 4, 8, ... and advances y r steps past it, which needs
 * fewer function evaluations than Floyd's tortoise and hare.  Instead of a
 * gcd per step, up to 64 differences are multiplied into q (mod n) and one
 * gcd is taken per batch; ys remembers the start of the batch so that, if
 * the batch overshoots and the gcd is n itself, the steps can be replayed
 * one gcd at a time (backtracking).  If even that yields n (x and y met
 * modulo every prime factor at once), the next constant c is tried
 * (c = 1..19).  Each c gets a budget of 2,000,000 batched steps.
 *
 * Memory: the loop state is skeep'ed after every batch, but note that the
 * "advance y by r steps" loop at the top of each doubling is not compacted
 * (for large r this can exhaust scratch and throw CAS_ERR_MEMORY before the
 * iteration budget is reached).  Expected cost
 * O(sqrt(p)) multiplications mod n for the smallest prime factor p. */
static E *rho(E *n) {
    for (uint32_t cseed = 1; cseed < 20; cseed++) {
        E *c = mk_int(cseed), *y = mk_int(2), *x = y, *g = mk_int(1), *q = mk_int(1), *ys = y;
        char *mark = smark();
        uint64_t r = 1, iters = 0;
        while (int_is_one(g)) {
            x = y;
            for (uint64_t i = 0; i < r; i++) y = int_mod(int_add(int_mul(y, y), c), n);
            uint64_t k = 0;
            while (k < r && int_is_one(g)) {
                ys = y;
                uint64_t lim = r - k < 64 ? r - k : 64;
                for (uint64_t i = 0; i < lim; i++) {
                    y = int_mod(int_add(int_mul(y, y), c), n);
                    q = int_mod(int_mul(q, int_abs(int_sub(x, y))), n);
                }
                g = int_gcd(q, n);
                k += lim;
                E *keepv[5] = {x, y, q, g, ys};
                E *l = skeep(mark, mk_list(5, keepv));
                x = EK(l)[0]; y = EK(l)[1]; q = EK(l)[2]; g = EK(l)[3]; ys = EK(l)[4];
                iters += lim;
                if (iters > 2000000) return NULL;
            }
            r *= 2;
        }
        if (int_cmp(g, n) == 0) { /* batch overshot: replay it step by step */
            do {
                ys = int_mod(int_add(int_mul(ys, ys), c), n);
                g = int_gcd(int_abs(int_sub(x, ys)), n);
            } while (int_is_one(g));
        }
        if (int_cmp(g, n) != 0) return g;
    }
    return NULL;
}

/* Record prime p with multiplicity e in ps/es[0..*np): an existing entry
 * for p (factor_rec may find a prime more than once) is incremented, a new
 * prime is appended unless `cap` distinct primes are already recorded. */
static void add_factor(E *p, uint32_t e, E **ps, uint32_t *es, uint32_t *np, uint32_t cap) {
    for (uint32_t i = 0; i < *np; i++)
        if (int_cmp(ps[i], p) == 0) {
            es[i] += e;
            return;
        }
    if (*np < cap) {
        ps[*np] = p;
        es[(*np)++] = e;
    }
}

/* Split n^e (n > 1, free of factors below the trial-division bound) into
 * prime factors, recording them with add_factor.  Order: prime test;
 * perfect power n = r^k (k = 2..min(bits, 65); r is factored once with
 * multiplicity k e); otherwise one rho split and recursion on both parts.
 * If rho gives up, n is recorded as is although it is composite, so the
 * caller may receive a composite "prime".  Distinct primes beyond `cap` are
 * silently dropped (see int_factor).  Recursion is guarded by stack_check. */
static void factor_rec(E *n, uint32_t e, E **ps, uint32_t *es, uint32_t *np, uint32_t cap) {
    stack_check();
    if (int_is_one(n)) return;
    if (int_isprime(n)) {
        add_factor(n, e, ps, es, np, cap);
        return;
    }
    /* perfect power? */
    for (uint32_t k = 2; k <= int_bits(n); k++) {
        int ex;
        E *r = int_iroot(n, k, &ex);
        if (ex) {
            factor_rec(r, k * e, ps, es, np, cap);
            return;
        }
        if (k > 64) break;
    }
    E *f = rho(n);
    if (!f) { /* give up: record composite */
        add_factor(n, e, ps, es, np, cap);
        return;
    }
    factor_rec(f, e, ps, es, np, cap);
    factor_rec(int_tdiv(n, f), e, ps, es, np, cap);
}

/* Prime factorisation of |n| as a LIST of [p, e] pairs with p ascending
 * (empty list for |n| = 1).  n must be nonzero: for n = 0 the trial division
 * would divide by 2 forever; all callers (factor_int in factor.c for
 * ifactor/factor, divisors and euler in cmds.c, lnexpand in trig.c) check
 * this first.  Steps:
 *  1. trial division by 2 and odd numbers below 50000 (stopping early once
 *     p^2 exceeds a single-limb cofactor), dividing out each p in place;
 *  2. factor_rec on the remaining cofactor (prime test, perfect powers,
 *     Pollard-Brent rho);
 *  3. insertion sort of the collected (prime, multiplicity) pairs.
 * At most 256 distinct primes are recorded (a number with more has over
 * 600 digits); multiplicities are unbounded.  All allocations are in
 * scratch; polls for interrupts. */
E *int_factor(E *n) {
    n = int_abs(n);
    uint32_t cap = 256, np = 0;
    E **ps = salloc(sizeof(E *) * cap);
    uint32_t *es = salloc(sizeof(uint32_t) * cap);
    /* trial division */
    uint32_t p = 2;
    while (!int_is_one(n) && p < 50000 && np < cap) {
        if (n->n == 1 && (uint64_t)p * p > EL(n)[0]) break;
        if (int_mod_u(n, p) == 0) {
            uint32_t e = 0;
            do {
                E *q = int_alloc(n->n);
                mpn_divrem_1(EL(q), EL(n), n->n, p);
                n = int_norm(q);
                e++;
            } while (int_mod_u(n, p) == 0);
            ps[np] = mk_int(p);
            es[np++] = e;
        }
        p += (p == 2) ? 1 : 2;
        if ((p & 1023) == 1) poll_intr();
    }
    if (!int_is_one(n)) factor_rec(n, 1, ps, es, &np, cap);
    /* sort ascending */
    for (uint32_t i = 1; i < np; i++)
        for (uint32_t j = i; j > 0 && int_cmp(ps[j - 1], ps[j]) > 0; j--) {
            E *t = ps[j]; ps[j] = ps[j - 1]; ps[j - 1] = t;
            uint32_t te = es[j]; es[j] = es[j - 1]; es[j - 1] = te;
        }
    E **pairs = salloc(sizeof(E *) * (np + 1));
    for (uint32_t i = 0; i < np; i++) {
        E *pr[2] = {ps[i], mk_int(es[i])};
        pairs[i] = mk_list(2, pr);
    }
    return mk_list(np, pairs);
}
