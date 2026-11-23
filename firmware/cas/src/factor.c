/*
 * factor.c -- polynomial factorisation over Q.
 *
 * Univariate (Zassenhaus): squarefree f in Z[x] is factored modulo a small
 * prime p (distinct-degree + Cantor-Zassenhaus equal-degree splitting), the
 * modular factors are Hensel-lifted (linear lifting) to p^k beyond the
 * Mignotte bound, and true factors are found by trial recombination of
 * subsets.  Multivariate: content + squarefree decomposition in the main
 * variable, homogeneous polynomials by dehomogenisation, otherwise Kronecker
 * substitution with recombination.
 *
 * Data flow
 *   factor_expr (factor command, simplify, integrate, ...)  and
 *   factor_list / factor_list_in (solve, integrate, partial fractions, sums,
 *   ODEs) convert an expression with numden + e2p into a polynomial in
 *   kernels (poly.h), run factor_poly_rec, and convert the factors back with
 *   p2e.  p_factor is the same pipeline for callers that already hold a P
 *   (heurisch.c).  sqrfree_expr only does the squarefree step.
 *
 *   factor_poly_rec(f)       content in the main variable (recursively
 *                            factored), then Yun squarefree decomposition
 *     factor_sqf_part(g)     one squarefree primitive part:
 *       univariate           zp_factor_sqf
 *       linear in main var   irreducible as it is
 *       homogeneous          set the lowest other variable to 1, factor the
 *                            result recursively, homogenise the factors again
 *       otherwise            kronecker: substitute x_j = t^(D^j), factor the
 *                            univariate image, recombine image factors
 *
 *   zp_factor_sqf(f)         x^n +- 1 via cyclotomic polynomials; x | f split
 *                            off; irreducible quadratics detected by the
 *                            discriminant; otherwise Zassenhaus:
 *     choose p               up to 10 primes p > deg f with p not dividing
 *                            lc(f) and f mod p squarefree; keep the one with
 *                            the fewest modular factors (count_ddf)
 *     mp_factor              distinct-degree factorisation + equal-degree
 *                            splitting (Cantor-Zassenhaus) mod p
 *     hensel_lift            lift each factor against the product of the
 *                            others to p^k > 2 * (Mignotte-type bound)
 *     recombination          subsets of increasing size, constant-term test,
 *                            then trial division over Z
 *
 * Representations
 *   MP  dense polynomial over GF(p): uint32_t coefficients in [0, p), c[0..d],
 *       d = -1 for zero.  p is the file-global P_ ("current prime"), so all
 *       mod-p helpers implicitly work modulo P_.  All primes used are below
 *       2^16, so a sum of two residues fits in 32 bits and a product in 64.
 *   ZP  dense polynomial over Z with bignum (E_INT) coefficients (poly.h).
 *   P   recursive multivariate polynomial (poly.h).
 *
 * Literature: von zur Gathen & Gerhard, "Modern Computer Algebra" (MCA):
 * distinct-degree factorisation (Alg. 14.3), equal-degree splitting
 * (Cantor-Zassenhaus, Alg. 14.8, applied recursively as in Alg. 14.10),
 * Hensel lifting (ch. 15), Zassenhaus recombination (Alg. 15.19, sec.
 * 15.6), Mignotte's factor bound (sec. 6.6);
 * Geddes/Czapor/Labahn "Algorithms for Computer Algebra" ch. 6 and 8 (linear
 * Hensel lifting, Berlekamp/Zassenhaus framework); Knuth TAOCP vol. 2, 4.6.2
 * (factoring polynomials, Kronecker's method); cyclotomic polynomials via the
 * Moebius inversion formula Phi_n = prod_{d | n} (x^d - 1)^mu(n/d).
 *
 * Memory: all intermediates live in scratch.  Hensel lifting compacts its
 * state with skeep each step, and the recombination loop releases failed
 * candidates with smark/srelease, so these loops run in bounded memory.
 * Errors (memory, interrupts via poll_intr, stack depth) propagate by
 * cas_throw.  Randomness comes from rng_next (K->rng, reseeded per cas_eval),
 * so results are reproducible.  Not reentrant: P_ and g_mainvar are globals.
 */
#include "poly.h"
#include "casx.h" /* casx_pfactor */
#include <string.h>

/* ------------------------------------------------------ mod-p polys --- */
/* Dense polynomial over GF(P_): c[0..d] ascending, residues in [0, P_);
 * d = -1 for the zero polynomial.  Arrays come from scratch and are treated
 * as immutable by the helpers below (each returns a fresh polynomial). */
typedef struct {
    long d;
    uint32_t *c;
} MP;

static uint32_t P_; /* current prime */

/* Arithmetic in GF(P_): mulm, addm, subm on residues in [0, P_), powm by
 * binary powering, invm by Fermat's little theorem (a^(P_-2), a != 0). */
static uint32_t mulm(uint32_t a, uint32_t b) { return (uint32_t)((uint64_t)a * b % P_); }
/* (a + b) mod P_; no overflow since P_ < 2^16. */
static uint32_t addm(uint32_t a, uint32_t b) {
    uint32_t s = a + b;
    return s >= P_ ? s - P_ : s;
}
static uint32_t subm(uint32_t a, uint32_t b) { return a >= b ? a - b : a + P_ - b; }
/* a^e mod P_ by right-to-left binary powering. */
static uint32_t powm(uint32_t a, uint64_t e) {
    uint32_t r = 1;
    while (e) {
        if (e & 1) r = mulm(r, a);
        a = mulm(a, a);
        e >>= 1;
    }
    return r;
}
static uint32_t invm(uint32_t a) { return powm(a, P_ - 2); }

/* Zeroed polynomial with room for degree d (d + 2 slots, one spare); the
 * degree field is set to d, callers adjust it. */
static MP mp_alloc(long d) {
    MP r;
    r.d = d;
    r.c = salloc(sizeof(uint32_t) * (size_t)(d + 2));
    memset(r.c, 0, sizeof(uint32_t) * (size_t)(d + 2));
    return r;
}
/* Lower d past zero leading coefficients (in place on the copy a). */
static MP mp_norm(MP a) {
    while (a.d >= 0 && a.c[a.d] == 0) a.d--;
    return a;
}
/* Fresh copy of a (zero stays zero). */
static MP mp_copy(MP a) {
    MP r = mp_alloc(a.d < 0 ? 0 : a.d);
    r.d = a.d;
    if (a.d >= 0) memcpy(r.c, a.c, sizeof(uint32_t) * (size_t)(a.d + 1));
    return r;
}
/* a + b, or a - b when sub is nonzero; normalised. */
static MP mp_add(MP a, MP b, int sub) {
    long d = a.d > b.d ? a.d : b.d;
    MP r = mp_alloc(d < 0 ? 0 : d);
    r.d = d;
    for (long i = 0; i <= d; i++) {
        uint32_t x = i <= a.d ? a.c[i] : 0, y = i <= b.d ? b.c[i] : 0;
        r.c[i] = sub ? subm(x, y) : addm(x, y);
    }
    return mp_norm(r);
}
/* Schoolbook product a * b mod P_, O(deg a * deg b). */
static MP mp_mul(MP a, MP b) {
    if (a.d < 0 || b.d < 0) {
        MP z = mp_alloc(0);
        z.d = -1;
        return z;
    }
    MP r = mp_alloc(a.d + b.d);
    r.d = a.d + b.d;
    for (long i = 0; i <= a.d; i++) {
        if (!a.c[i]) continue;
        for (long j = 0; j <= b.d; j++) r.c[i + j] = addm(r.c[i + j], mulm(a.c[i], b.c[j]));
    }
    return mp_norm(r);
}
/* s * a for a residue s. */
static MP mp_scale(MP a, uint32_t s) {
    MP r = mp_copy(a);
    for (long i = 0; i <= r.d; i++) r.c[i] = mulm(r.c[i], s);
    return mp_norm(r);
}
/* a = q*b + r
 * Classical division by b (b != 0) using the inverse of lc(b); q and/or r
 * may be NULL when not wanted.  deg r < deg b.  O((deg a - deg b + 1) *
 * deg b). */
static void mp_divrem(MP a, MP b, MP *q, MP *r) {
    MP rr = mp_copy(a);
    long dq = a.d - b.d;
    MP qq = mp_alloc(dq < 0 ? 0 : dq);
    qq.d = dq < 0 ? -1 : dq;
    uint32_t il = invm(b.c[b.d]);
    for (long i = a.d; i >= b.d; i--) {
        uint32_t t = mulm(rr.c[i], il);
        if (dq >= 0) qq.c[i - b.d] = t;
        if (!t) continue;
        for (long j = 0; j <= b.d; j++) rr.c[i - b.d + j] = subm(rr.c[i - b.d + j], mulm(t, b.c[j]));
    }
    if (b.d >= 0) rr.d = b.d - 1 < a.d ? b.d - 1 : a.d;
    if (q) *q = mp_norm(qq);
    if (r) *r = mp_norm(rr);
}
/* a mod b. */
static MP mp_rem(MP a, MP b) {
    MP r;
    mp_divrem(a, b, NULL, &r);
    return r;
}
/* a divided by its leading coefficient (zero stays zero). */
static MP mp_monic(MP a) {
    if (a.d < 0) return a;
    return mp_scale(a, invm(a.c[a.d]));
}
/* Monic gcd by Euclid's algorithm (gcd(0, 0) = 0). */
static MP mp_gcd(MP a, MP b) {
    a = mp_norm(a);
    b = mp_norm(b);
    while (b.d >= 0) {
        MP r = mp_rem(a, b);
        a = b;
        b = r;
    }
    return mp_monic(a);
}
/* extended gcd: s*a + t*b = g (monic)
 * Extended Euclid (MCA Alg. 3.14) carrying the Bezout cofactors; requires a
 * and b not both zero.  Used by hensel_lift with coprime g, h so that
 * s*g + t*h = 1. */
static MP mp_egcd(MP a, MP b, MP *s, MP *t) {
    MP r0 = a, r1 = b;
    MP s0 = mp_alloc(0), s1 = mp_alloc(0), t0 = mp_alloc(0), t1 = mp_alloc(0);
    s0.c[0] = 1; s0.d = 0; s1.d = -1;
    t0.d = -1; t1.c[0] = 1; t1.d = 0;
    while (r1.d >= 0) {
        MP q, r;
        mp_divrem(r0, r1, &q, &r);
        r0 = r1; r1 = r;
        MP ns = mp_add(s0, mp_mul(q, s1), 1); s0 = s1; s1 = ns;
        MP nt = mp_add(t0, mp_mul(q, t1), 1); t0 = t1; t1 = nt;
    }
    uint32_t il = invm(r0.c[r0.d]);
    *s = mp_scale(s0, il);
    *t = mp_scale(t0, il);
    return mp_scale(r0, il);
}
/* b^e mod f by binary powering; O(log e) multiplications of polynomials of
 * degree < deg f. */
static MP mp_powmod(MP b, uint64_t e, MP f) {
    MP r = mp_alloc(0);
    r.c[0] = 1;
    r.d = 0;
    b = mp_rem(b, f);
    while (e) {
        if (e & 1) r = mp_rem(mp_mul(r, b), f);
        e >>= 1;
        if (e) b = mp_rem(mp_mul(b, b), f);
    }
    return r;
}
/* Formal derivative mod P_ (used for the squarefree test gcd(f, f')). */
static MP mp_deriv(MP a) {
    if (a.d <= 0) {
        MP z = mp_alloc(0);
        z.d = -1;
        return z;
    }
    MP r = mp_alloc(a.d - 1);
    r.d = a.d - 1;
    for (long i = 1; i <= a.d; i++) r.c[i - 1] = mulm(a.c[i], (uint32_t)(i % P_));
    return mp_norm(r);
}
/* The polynomial x. */
static MP mp_x(void) {
    MP r = mp_alloc(1);
    r.c[1] = 1;
    r.d = 1;
    return r;
}

/* Reduce an integer polynomial modulo P_ (negative coefficients map to
 * their residues in [0, P_)). */
static MP zp_to_mp(ZP f) {
    MP r = mp_alloc(f.d < 0 ? 0 : f.d);
    r.d = f.d;
    for (long i = 0; i <= f.d; i++) r.c[i] = int_modp(f.c[i], P_);
    return mp_norm(r);
}

/* ---- Cantor-Zassenhaus ---- */
/* Fixed-capacity list of modular factors (pushes beyond cap are dropped;
 * zp_factor_sqf sizes it to deg f + 1, which can never be exceeded). */
typedef struct {
    MP *f;
    uint32_t n, cap;
} mplist;

/* Append f to l if there is room. */
static void ml_push(mplist *l, MP f) {
    if (l->n < l->cap) l->f[l->n++] = f;
}

/* Equal-degree factorisation (Cantor-Zassenhaus: MCA Alg. 14.8 splitting
 * step, applied recursively as in Alg. 14.10): g is monic, squarefree and a
 * product of irreducibles of the same degree d over GF(p), p odd.  Pushes
 * the irreducible factors (monic) to out.
 *
 * For a random a with deg a < deg g, in each residue field GF(p^d) =
 * GF(p)[x]/(g_i) the element a^((p^d - 1)/2) is 0 or +-1, and +1 with
 * probability about 1/2 independently for each factor g_i.  So
 * gcd(a^((p^d-1)/2) - 1, g) is a proper factor with probability about 1/2.
 * The exponent is computed as N(a)^((p-1)/2) where
 *     N(a) = a * a^p * a^(p^2) * ... * a^(p^(d-1)) = a^((p^d-1)/(p-1))
 * (the norm to GF(p)), which needs only d-1 Frobenius powers plus one small
 * power instead of an exponent with about d*log2(p) bits.
 * Splits recursively on success (recursion depth <= number of factors);
 * after 200 failed attempts (probability about 2^-200) g is pushed unsplit. */
static void edf(MP g, long d, mplist *out) {
    stack_check();
    if (g.d <= d) {
        ml_push(out, mp_monic(g));
        return;
    }
    for (int attempt = 0; attempt < 200; attempt++) {
        MP a = mp_alloc(g.d - 1);
        a.d = g.d - 1;
        for (long i = 0; i < g.d; i++) a.c[i] = rng_next() % P_;
        a = mp_norm(a);
        if (a.d < 1) continue;
        /* acc = a * a^p * ... * a^(p^(d-1)) */
        MP s = a, acc = a;
        for (long j = 1; j < d; j++) {
            s = mp_powmod(s, P_, g);
            acc = mp_rem(mp_mul(acc, s), g);
        }
        MP b = mp_powmod(acc, (P_ - 1) / 2, g);
        if (b.d < 0) continue;
        b.c[0] = subm(b.c[0], 1);
        b = mp_norm(b);
        MP h = mp_gcd(b, g);
        if (h.d > 0 && h.d < g.d) {
            MP q;
            mp_divrem(g, h, &q, NULL);
            edf(h, d, out);
            edf(q, d, out);
            return;
        }
    }
    ml_push(out, mp_monic(g)); /* should not happen */
}

/* factor monic squarefree f mod p into irreducibles
 * Distinct-degree factorisation (MCA Alg. 14.3): with w = x^(p^i) mod fs,
 * gcd(w - x, fs) is the product of all irreducible factors of degree i
 * (x^(p^i) - x is the product of all monic irreducibles of degree dividing
 * i, and smaller degrees were already removed).  Each such product is split
 * by edf and divided out.  Once 2i > deg fs, what remains has no factor of
 * degree <= i and therefore is irreducible.  Cost: O(deg f) modular
 * exponentiations by p, each O(log p * deg f^2). */
static void mp_factor(MP f, mplist *out) {
    MP w = mp_x(), x = mp_x();
    MP fs = f;
    long i = 1;
    while (2 * i <= fs.d) {
        poll_intr();
        w = mp_powmod(w, P_, fs);
        MP g = mp_gcd(mp_add(w, x, 1), fs);
        if (g.d > 0) {
            edf(g, i, out);
            MP q;
            mp_divrem(fs, g, &q, NULL);
            fs = q;
            w = mp_rem(w, fs);
        }
        i++;
    }
    if (fs.d > 0) ml_push(out, mp_monic(fs));
}

/* Number of irreducible factors of the monic squarefree f mod P_, from the
 * distinct-degree factorisation alone (a degree-i block of degree k holds
 * k/i factors) without splitting.  Used to choose the prime: fewer modular
 * factors mean a cheaper recombination, and a count of 1 proves f
 * irreducible over Z. */
static long count_ddf(MP f) {
    long cnt = 0;
    MP w = mp_x(), x = mp_x(), fs = f;
    long i = 1;
    while (2 * i <= fs.d) {
        w = mp_powmod(w, P_, fs);
        MP g = mp_gcd(mp_add(w, x, 1), fs);
        if (g.d > 0) {
            cnt += g.d / i;
            MP q;
            mp_divrem(fs, g, &q, NULL);
            fs = q;
            w = mp_rem(w, fs);
        }
        i++;
    }
    if (fs.d > 0) cnt++;
    return cnt;
}

/* ------------------------------------------------- Z[x] dense helpers --- */
/* Integer polynomial of degree d with all coefficients 0 (d + 2 slots). */
static ZP zp_alloc(long d) {
    ZP r;
    r.d = d;
    r.c = salloc(sizeof(E *) * (size_t)(d + 2));
    for (long i = 0; i <= d; i++) r.c[i] = K->c0;
    return r;
}
/* Lower d past zero leading coefficients. */
static ZP zp_norm(ZP a) {
    while (a.d >= 0 && int_is_zero(a.c[a.d])) a.d--;
    return a;
}
/* Schoolbook product over Z (bignum coefficients). */
static ZP zp_mul(ZP a, ZP b) {
    if (a.d < 0 || b.d < 0) {
        ZP z = zp_alloc(0);
        z.d = -1;
        return z;
    }
    ZP r = zp_alloc(a.d + b.d);
    for (long i = 0; i <= a.d; i++) {
        if (int_is_zero(a.c[i])) continue;
        for (long j = 0; j <= b.d; j++) r.c[i + j] = int_add(r.c[i + j], int_mul(a.c[i], b.c[j]));
    }
    return zp_norm(r);
}
/* Coefficients reduced to the symmetric range (-m/2, m/2] (m > 0).  Integer
 * factors with coefficients bounded by B < m/2 are recovered exactly from
 * their images mod m in this representation. */
static ZP zp_mods(ZP a, E *m) {
    /* symmetric residues */
    ZP r = zp_alloc(a.d < 0 ? 0 : a.d);
    r.d = a.d;
    E *half = int_shr(m, 1);
    for (long i = 0; i <= a.d; i++) {
        E *x = int_mod(a.c[i], m);
        if (int_cmp(x, half) > 0) x = int_sub(x, m);
        r.c[i] = x;
    }
    return zp_norm(r);
}
/* Residues in [0, P_) as non-negative integers (no symmetric shift). */
static ZP mp_to_zp(MP a) {
    ZP r = zp_alloc(a.d < 0 ? 0 : a.d);
    r.d = a.d;
    for (long i = 0; i <= a.d; i++) r.c[i] = mk_uint(a.c[i]);
    return r;
}
/* exact division over Z; returns 0 if not divisible
 * On success stores a / b in *q and returns 1.  Every quotient coefficient
 * must be an integer and the remainder must vanish; a = 0 gives q = 0.
 * Returns 0 for b = 0. */
static int zp_div(ZP a, ZP b, ZP *q) {
    if (b.d < 0) return 0;
    if (a.d < b.d) {
        if (a.d < 0) {
            *q = a;
            return 1;
        }
        return 0;
    }
    ZP r = zp_alloc(a.d);
    for (long i = 0; i <= a.d; i++) r.c[i] = a.c[i];
    ZP qq = zp_alloc(a.d - b.d);
    E *lb = b.c[b.d];
    for (long i = a.d; i >= b.d; i--) {
        if (int_is_zero(r.c[i])) continue;
        E *qt, *rm;
        int_tdivmod(r.c[i], lb, &qt, &rm);
        if (!int_is_zero(rm)) return 0;
        qq.c[i - b.d] = qt;
        for (long j = 0; j <= b.d; j++) r.c[i - b.d + j] = int_sub(r.c[i - b.d + j], int_mul(qt, b.c[j]));
    }
    for (long i = 0; i < b.d && i <= a.d; i++)
        if (!int_is_zero(r.c[i])) return 0;
    *q = zp_norm(qq);
    return 1;
}
/* Non-negative gcd of all coefficients (0 for the zero polynomial). */
static E *zp_content(ZP a) {
    E *g = K->c0;
    for (long i = 0; i <= a.d; i++) g = int_gcd(g, a.c[i]);
    return g;
}
/* Primitive part with positive leading coefficient: a / (+-content). */
static ZP zp_prim(ZP a) {
    E *g = zp_content(a);
    if (a.d >= 0 && a.c[a.d]->s) g = int_neg(g);
    if (int_is_one(g)) return a;
    ZP r = zp_alloc(a.d);
    for (long i = 0; i <= a.d; i++) r.c[i] = int_divexact(a.c[i], g);
    return r;
}

/* ---- Hensel lifting of f = lc*g*h (mod p) to modulus m >= bound ----
 *
 * Linear (p-adic) Hensel lifting, GCL section 6.5 (MCA ch. 15 lifts
 * quadratically, Alg. 15.10; this is the linear variant).  Input: f in
 * Z[x] with leading coefficient lc (p does not divide lc), and monic coprime
 * g, h over GF(p) with f = lc*g*h mod p.  Output: G (monic, deg G = deg g)
 * with f = lc*G*H mod m for some H, where m = p^k is the first power of p
 * that is >= pk_target; *mod_out receives m.  H is lifted alongside but
 * discarded.
 *
 * One step from m to m*p, with s*g + t*h = 1 mod p (mp_egcd):
 *   e     = (f - lc*G*H) / m              exact by the invariant
 *   ep    = e / lc mod p
 *   sigma = s*ep mod h,  q = s*ep div h
 *   tau   = t*ep + q*g                    (deg tau < deg g)
 *   G += m*tau,  H += m*sigma
 * Then sigma*g + tau*h = ep mod p, so lc*(G + m tau)(H + m sigma) =
 * lc*G*H + m*e = f mod m*p.  Keeping deg sigma < deg h and deg tau < deg g
 * keeps G and H monic.  Each step costs O(deg f^2) bignum operations on
 * numbers of size m; k steps grow m linearly (quadratic lifting would square
 * it, but needs lifted cofactors s, t too).
 *
 * Memory: after every step G, H and m are packed into one list and compacted
 * with skeep to the mark taken before the loop, so scratch use stays O(size
 * of one step) regardless of the number of steps.  Interruptible. */
static ZP hensel_lift(ZP f, MP g, MP h, E *pk_target, E **mod_out) {
    E *lc = f.c[f.d];
    MP s, t;
    mp_egcd(g, h, &s, &t);
    uint32_t ilc = invm(int_modp(lc, P_));
    ZP G = mp_to_zp(g), H = mp_to_zp(h);
    E *m = mk_uint(P_), *pe = mk_uint(P_);
    char *mark = smark();
    while (int_cmp(m, pk_target) < 0) {
        poll_intr();
        /* e = (f - lc*G*H) / m */
        ZP gh = zp_mul(G, H);
        ZP e = zp_alloc(f.d);
        for (long i = 0; i <= f.d; i++) {
            E *v = f.c[i];
            if (i <= gh.d) v = int_sub(v, int_mul(lc, gh.c[i]));
            e.c[i] = int_divexact(v, m);
        }
        e = zp_norm(e);
        MP ep = mp_scale(zp_to_mp(e), ilc);
        MP se = mp_mul(s, ep), q, sigma;
        mp_divrem(se, h, &q, &sigma);
        MP tau = mp_add(mp_mul(t, ep), mp_mul(q, g), 0);
        tau = mp_rem(tau, g); /* safety: deg tau < deg g */
        ZP T = mp_to_zp(tau), S = mp_to_zp(sigma);
        ZP nG = zp_alloc(G.d), nH = zp_alloc(H.d);
        for (long i = 0; i <= G.d; i++) nG.c[i] = int_add(G.c[i], i <= T.d ? int_mul(m, T.c[i]) : K->c0);
        for (long i = 0; i <= H.d; i++) nH.c[i] = int_add(H.c[i], i <= S.d ? int_mul(m, S.c[i]) : K->c0);
        m = int_mul(m, pe);
        /* compact: keep G, H, m */
        E **keep = salloc(sizeof(E *) * (size_t)(G.d + H.d + 3));
        uint32_t nk = 0;
        for (long i = 0; i <= G.d; i++) keep[nk++] = nG.c[i];
        for (long i = 0; i <= H.d; i++) keep[nk++] = nH.c[i];
        keep[nk++] = m;
        E *L = skeep(mark, mk_list(nk, keep));
        nk = 0;
        G = zp_alloc(nG.d);
        H = zp_alloc(nH.d);
        for (long i = 0; i <= G.d; i++) G.c[i] = EK(L)[nk++];
        for (long i = 0; i <= H.d; i++) H.c[i] = EK(L)[nk++];
        m = EK(L)[nk++];
        mark = smark();
    }
    *mod_out = m;
    return G;
}

/* Candidate primes for the modular factorisation: 36 primes just above 1000
 * (fast arithmetic, small lifting steps) and 10 near 32000 (for polynomials
 * of degree >= ~1000, since p must exceed deg f, and as a reserve when many
 * small primes divide lc(f) or the discriminant).  All are < 2^16 so that
 * GF(p) arithmetic fits in 32/64-bit integers. */
static uint32_t small_primes[] = {1009, 1013, 1019, 1021, 1031, 1033, 1039, 1049, 1051, 1061, 1063, 1069,
                                  1087, 1091, 1093, 1097, 1103, 1109, 1117, 1123, 1129, 1151, 1153, 1163,
                                  1171, 1181, 1187, 1193, 1201, 1213, 1217, 1223, 1229, 1231, 1237, 1249,
                                  32003, 32009, 32027, 32029, 32051, 32057, 32059, 32063, 32069, 32077};

/* Moebius function mu(n) by trial division: 0 if n has a square factor,
 * otherwise (-1)^(number of prime factors).  O(sqrt n). */
static int mobius(long n) {
    int r = 1;
    for (long p = 2; p * p <= n; p++)
        if (n % p == 0) {
            n /= p;
            if (n % p == 0) return 0;
            r = -r;
        }
    if (n > 1) r = -r;
    return r;
}

/* cyclotomic polynomial Phi_n = prod_{d|n} (x^d - 1)^mu(n/d), computed with
 * sparse binomial multiplications/divisions on int64 coefficients
 *
 * Pass 0 multiplies the factors with mu = +1, pass 1 divides by those with
 * mu = -1 (all divisions are exact).  Multiplying by x^d - 1 is
 * c_i := c_{i-d} - c_i (descending i, in place); dividing by x^d - 1 solves
 * c = q * (x^d - 1) from the bottom: q_i = q_{i-d} - c_i.  For n <= 100000
 * (the caller's limit) n has at most 6 distinct prime factors, so at most 32
 * binomials are multiplied in pass 0 and the sum of the absolute values of
 * the coefficients stays below 2^32: int64 cannot overflow.  The buffer is
 * sized for the degree reached after pass 0 (the sum of the d with
 * mu(n/d) = +1), which exceeds n in general (40 for n = 30). */
static ZP cyclotomic(long n) {
    long top = 0;
    for (long d = 1; d <= n; d++)
        if (n % d == 0 && mobius(n / d) == 1) top += d;
    int64_t *c = salloc(sizeof(int64_t) * (size_t)(top + 1));
    long deg = 0;
    c[0] = 1;
    /* multiply by (x^d - 1) for mu = +1 first, then divide for mu = -1 */
    for (int pass = 0; pass < 2; pass++)
        for (long d = 1; d <= n; d++) {
            if (n % d) continue;
            int mu = mobius(n / d);
            if ((pass == 0 && mu != 1) || (pass == 1 && mu != -1)) continue;
            if (pass == 0) {
                /* c *= (x^d - 1) */
                for (long i = deg + d; i >= 0; i--) {
                    int64_t v = (i >= d ? c[i - d] : 0) - (i <= deg ? c[i] : 0);
                    c[i] = v;
                }
                deg += d;
            } else {
                /* c /= (x^d - 1): q_i = -(c_i - q_{i-d}) for ascending order */
                long qd = deg - d;
                int64_t *q = salloc(sizeof(int64_t) * (size_t)(qd + 1));
                for (long i = 0; i <= qd; i++) q[i] = -(c[i] - (i >= d ? q[i - d] : 0));
                for (long i = 0; i <= qd; i++) c[i] = q[i];
                deg = qd;
            }
        }
    ZP r = zp_alloc(deg);
    for (long i = 0; i <= deg; i++) r.c[i] = mk_int(c[i]);
    return zp_norm(r);
}

/* f = x^n - 1 or x^n + 1 ?  returns 1 / -1 / 0
 * (1 for x^n - 1, -1 for x^n + 1, 0 otherwise; n >= 2 stored in *n) */
static int binomial_kind(ZP f, long *n) {
    if (f.d < 2 || !int_is_one(f.c[f.d])) return 0;
    for (long i = 1; i < f.d; i++)
        if (!int_is_zero(f.c[i])) return 0;
    *n = f.d;
    if (int_is_one(f.c[0])) return -1;
    if (num_is_m1(f.c[0])) return 1;
    return 0;
}

/* factor squarefree primitive f (lc > 0, deg >= 1) over Z
 *
 * Writes the irreducible factors (primitive, lc > 0) to out[0..cap-1] and
 * returns their number; see the file header for the overall plan.
 * Shortcuts first:
 *   - degree <= 1: irreducible;
 *   - x^n - 1 = prod_{d | n} Phi_d and x^n + 1 = prod Phi_d over the d that
 *     divide 2n but not n (n <= 50000);
 *   - x | f: split off x and recurse on f / x;
 *   - a quadratic with non-square discriminant is irreducible.
 * Then Zassenhaus' algorithm (MCA Alg. 15.19):
 *   1. prime choice (see small_primes): p > deg f, p does not divide lc(f),
 *      f mod p squarefree; among up to 10 such primes the one with the
 *      fewest modular factors wins; a single modular factor proves
 *      irreducibility.  If no prime qualifies, f is returned unfactored.
 *   2. factor f mod p completely (mp_factor);
 *   3. lift every modular factor to G_i mod M (hensel_lift against the
 *      product of the others), M = p^k >= 2^bits with
 *         bits = deg f + 8 + max coefficient bits + bits(lc f),
 *      so M > 2 * |lc| * 2^deg * sqrt(deg+1) * max|c| whenever
 *      sqrt(deg+1) <= 2^7 (deg < 16384).  This exceeds twice Mignotte's
 *      bound |lc| * 2^deg * ||f||_2 (with ||f||_2 <= sqrt(deg+1) * max|c|)
 *      on the coefficients of lc/lc(g) * g for any factor g of f, so such
 *      a factor is recovered exactly from its symmetric image mod M;
 *   4. recombination: for subsets S of the unused lifted factors of size
 *      s = 1, 2, ... (only up to half of the remaining ones: the complement
 *      of a factor is a factor), form lc(F) * prod_{i in S} G_i mods M,
 *      take its primitive part and test it by trial division of the
 *      remaining cofactor F.  A cheap constant-term test (the candidate's
 *      constant term must divide lc(F) * F(0)) rejects most subsets without
 *      building the product.  A successful factor removes its G_i and the
 *      search continues with the same s.  What remains of F at the end is
 *      irreducible.
 * Worst case exponential in the number of modular factors (e.g. the
 * Swinnerton-Dyer polynomials); typical inputs have few modular factors.
 * Uses the global P_ and K->rng (through edf).  Factors beyond cap are
 * dropped silently. */
uint32_t zp_factor_sqf(ZP f, ZP *out, uint32_t cap) {
    stack_check();
    if (f.d <= 1) {
        out[0] = f;
        return 1;
    }
    long bn;
    int bk = binomial_kind(f, &bn);
    if (bk && bn <= 50000) {
        uint32_t k = 0;
        long m = bk > 0 ? bn : 2 * bn;
        for (long d = 1; d <= m && k < cap; d++) {
            if (m % d) continue;
            if (bk < 0 && bn % d == 0) continue; /* x^n+1: divisors of 2n not dividing n */
            out[k++] = cyclotomic(d);
        }
        return k;
    }
    /* x | f ? */
    if (int_is_zero(f.c[0])) {
        ZP x = zp_alloc(1);
        x.c[1] = K->c1;
        ZP q;
        zp_div(f, x, &q);
        out[0] = x;
        return 1 + zp_factor_sqf(q, out + 1, cap - 1);
    }
    if (f.d == 2) {
        /* discriminant square test */
        E *a = f.c[2], *b = f.c[1], *c = f.c[0];
        E *disc = int_sub(int_mul(b, b), int_mul(mk_int(4), int_mul(a, c)));
        int ex = 0;
        if (int_sgn(disc) >= 0) int_iroot(disc, 2, &ex);
        if (!ex) {
            out[0] = f;
            return 1;
        }
    }
    /* choose prime: p must exceed deg f (no exponent is a multiple of p, so
     * f' mod p is never degenerate), must not divide lc(f) (degree kept mod
     * p) and must keep f squarefree (needed for distinct-degree splitting
     * and for coprime Hensel factors) */
    uint32_t best_p = 0;
    long best_cnt = 1L << 30;
    int tried = 0;
    for (uint32_t pi = 0; pi < sizeof small_primes / sizeof small_primes[0] && tried < 10; pi++) {
        P_ = small_primes[pi];
        if ((long)P_ <= f.d) continue;
        if (int_modp(f.c[f.d], P_) == 0) continue;
        char *mk = smark(); /* only the count is kept */
        MP fm = zp_to_mp(f);
        MP g = mp_gcd(fm, mp_deriv(fm));
        long cnt = g.d > 0 ? -1 : count_ddf(mp_monic(fm));
        srelease(mk);
        if (cnt < 0) continue;
        tried++;
        if (cnt < best_cnt) {
            best_cnt = cnt;
            best_p = P_;
        }
        if (cnt == 1) break;
    }
    if (!best_p) {
        out[0] = f;
        return 1;
    }
    /* irreducible mod p with the degree preserved => irreducible over Z */
    if (best_cnt == 1) {
        out[0] = f;
        return 1;
    }
    P_ = best_p;
    mplist ml;
    ml.cap = (uint32_t)f.d + 1;
    ml.n = 0;
    ml.f = salloc(sizeof(MP) * ml.cap);
    MP fm = mp_monic(zp_to_mp(f));
    mp_factor(fm, &ml);
    uint32_t r = ml.n;
    if (r <= 1) {
        out[0] = f;
        return 1;
    }
    /* bound: 2^(d+1) * sqrt(d+1) * max|c| * |lc|, doubled
     * (as bit counts: d + 8 + bits(max|c|) + bits(lc); the 8 spare bits
     * absorb the factor 2 for the symmetric range and sqrt(d+1) for
     * d < 16384 -- see the comment above zp_factor_sqf) */
    uint32_t mb = 0;
    for (long i = 0; i <= f.d; i++) {
        uint32_t b = int_bits(f.c[i]);
        if (b > mb) mb = b;
    }
    uint32_t bits = (uint32_t)f.d + 4 + mb + int_bits(f.c[f.d]) + 4;
    E *target = int_shl(mk_int(1), bits);
    /* lift each factor against the product of the others (r two-factor
     * lifts instead of a multifactor lifting tree; every lift ends at the
     * same modulus M) */
    ZP *lifted = salloc(sizeof(ZP) * r);
    E *M = NULL;
    for (uint32_t i = 0; i < r; i++) {
        MP h = mp_alloc(0);
        h.c[0] = 1;
        h.d = 0;
        for (uint32_t j = 0; j < r; j++)
            if (j != i) h = mp_mul(h, ml.f[j]);
        lifted[i] = hensel_lift(f, ml.f[i], h, target, &M);
    }
    /* recombination */
    uint32_t nout = 0;
    int *used = salloc(sizeof(int) * r);
    for (uint32_t i = 0; i < r; i++) used[i] = 0;
    uint32_t remaining = r;
    ZP F = f;
    int idx[64];
    uint32_t s = 1;
    /* s = subset size.  avail lists the unused lifted factors; idx[0..s-1]
     * is the current s-subset of positions in avail, enumerated in
     * lexicographic order.  Subsets larger than half of the remaining
     * factors need not be tried: their complement would have been found. */
    while (2 * s <= remaining && s < 64) {
        uint32_t *avail = salloc(sizeof(uint32_t) * 256), na = 0;
        for (uint32_t i = 0; i < r && na < 256; i++)
            if (!used[i]) avail[na++] = i;
        for (uint32_t i = 0; i < s; i++) idx[i] = (int)i;
        int found = 0;
        for (;;) {
            poll_intr();
            char *cm = smark();
            E *lc = F.c[F.d];
            /* constant-term test: lc * prod G_i(0) (symmetric mod M) must divide lc * F(0) */
            E *ct = lc;
            for (uint32_t i = 0; i < s; i++) ct = int_mod(int_mul(ct, lifted[avail[idx[i]]].c[0]), M);
            if (int_cmp(ct, int_shr(M, 1)) > 0) ct = int_sub(ct, M);
            int pass = int_is_zero(F.c[0]) || (!int_is_zero(ct) && int_is_zero(int_mod(int_mul(lc, F.c[0]), ct)));
            if (pass) {
                ZP g = zp_alloc(0);
                g.c[0] = lc;
                for (uint32_t i = 0; i < s; i++) g = zp_mods(zp_mul(g, lifted[avail[idx[i]]]), M);
                g = zp_prim(g);
                ZP q;
                if (g.d > 0 && !int_is_zero(g.c[0]) && int_is_zero(int_mod(F.c[0], g.c[0])) && zp_div(F, g, &q)) {
                    if (nout < cap) out[nout++] = g;
                    for (uint32_t i = 0; i < s; i++) used[avail[idx[i]]] = 1;
                    remaining -= s;
                    F = q;
                    found = 1;
                    break;
                }
            }
            /* candidate rejected: drop its scratch */
            srelease(cm);
            /* next s-subset: bump the rightmost index that can still move
             * and reset the ones after it to consecutive values */
            int k = (int)s - 1;
            while (k >= 0 && idx[k] == (int)(na - s + (uint32_t)k)) k--;
            if (k < 0) break;
            idx[k]++;
            for (uint32_t j = (uint32_t)k + 1; j < s; j++) idx[j] = idx[j - 1] + 1;
        }
        if (!found) s++;
    }
    /* no subset of at most half the remaining factors divides F: F is
     * irreducible (or 1) */
    if (F.d > 0 && nout < cap) out[nout++] = zp_prim(F);
    return nout;
}

/* ------------------------------------------------ multivariate layer --- */
/* Factor list for the multivariate layer: factors f[i] with multiplicities
 * m[i]; storage is provided by the caller (cap entries). */
typedef struct {
    P **f;
    uint32_t *m;
    uint32_t n, cap;
} flist;

/* Add factor f with multiplicity m, merging with an equal factor already in
 * the list (equal factors can arrive from different squarefree parts or
 * from content and primitive part).  Dropped silently when the list is
 * full. */
static void fl_push(flist *l, P *f, uint32_t m) {
    for (uint32_t i = 0; i < l->n; i++)
        if (p_eq(l->f[i], f)) {
            l->m[i] += m;
            return;
        }
    if (l->n < l->cap) {
        l->f[l->n] = f;
        l->m[l->n] = m;
        l->n++;
    }
}

/* 1 if p is a constant or a polynomial in x_v alone (all coefficients
 * numeric). */
static int p_is_univariate(P *p, int v) {
    if (p->v < 0) return 1;
    if (p->v != v) return 0;
    for (uint32_t i = 0; i < p->n; i++)
        if (p->k[i]->v >= 0) return 0;
    return 1;
}

/* Univariate P with integer coefficients -> ZP (p_is_univariate must hold
 * and the coefficients must be E_INT). */
static ZP p_to_zp(P *p) {
    long d = p->v < 0 ? 0 : (long)p->n - 1;
    ZP r = zp_alloc(d);
    if (p->v < 0) r.c[0] = p->c;
    else
        for (long i = 0; i <= d; i++) r.c[i] = p->k[i]->c;
    return r;
}

/* ZP -> polynomial in x_v (normalised). */
static P *zp_to_p(ZP z, int v) {
    if (z.d <= 0) return p_const(z.d < 0 ? K->c0 : z.c[0]);
    P *r = p_mk(v, (uint32_t)z.d + 1);
    for (long i = 0; i <= z.d; i++) r->k[i] = p_const(z.c[i]);
    return p_norm(r);
}

static void factor_sqf_part(P *f, flist *out, uint32_t mult);

/* all variables occurring
 * Sets mask[v] = 1 for every variable v < nmax occurring in p.  Variables
 * with index >= nmax are not recorded. */
static void vars_of(P *p, int *mask, int nmax) {
    stack_check();
    if (p->v < 0) return;
    if (p->v < nmax) mask[p->v] = 1;
    for (uint32_t i = 0; i < p->n; i++) vars_of(p->k[i], mask, nmax);
}

/* total degree of each monomial; returns 1 if homogeneous with degree *td
 * acc is the degree accumulated on the path from the root; *td must be -1
 * on entry and receives the total degree of the first monomial found. */
static int homog_rec(P *p, long acc, long *td) {
    stack_check();
    if (p_is_zero(p)) return 1;
    if (p->v < 0) {
        if (*td < 0) *td = acc;
        return *td == acc;
    }
    for (uint32_t i = 0; i < p->n; i++)
        if (!homog_rec(p->k[i], acc + (long)i, td)) return 0;
    return 1;
}

/* multiply each monomial by u^(td - deg(monomial))
 * Inverse of dehomogenisation (setting u = 1): with td the total degree of
 * p, the result is the homogeneous polynomial of degree td that becomes p
 * again for u = 1.  acc is the degree accumulated so far (0 at the top). */
static P *homogenize(P *p, int u, long td, long acc) {
    stack_check();
    if (p_is_zero(p)) return p;
    if (p->v < 0) {
        long e = td - acc;
        if (e <= 0) return p;
        return p_mul(p, p_pow(p_var(u), (uint32_t)e));
    }
    P *r = p_const(K->c0);
    P *xv = p_var(p->v);
    for (uint32_t i = 0; i < p->n; i++) {
        P *t = homogenize(p->k[i], u, td, acc + (long)i);
        if (i) t = p_mul(t, p_pow(xv, i));
        r = p_add(r, t);
    }
    return r;
}

/* Total degree (maximum over monomials of the sum of exponents); -1 for
 * zero. */
static long total_degree(P *p) {
    stack_check();
    if (p_is_zero(p)) return -1;
    if (p->v < 0) return 0;
    long m = 0;
    for (uint32_t i = 0; i < p->n; i++) {
        long d = total_degree(p->k[i]);
        if (d >= 0 && d + (long)i > m) m = d + (long)i;
    }
    return m;
}

/* Kronecker: map vars (list) to t = x_main with exponents base D
 * Kronecker substitution x_{vars[j]} -> t^(D^j) (Knuth 4.6.2): the monomial
 * prod x_{vars[j]}^{e_j} with all e_j < D becomes t^(sum e_j D^j), and this
 * map is injective, so the image is a faithful encoding of p.  pw[j] = D^j;
 * acc is the exponent accumulated so far; out must be preallocated with
 * degree >= D^nv - 1. */
static void kron_rec(P *p, int *vars, int nv, long D, long acc, long *pw, ZP *out) {
    stack_check();
    if (p_is_zero(p)) return;
    if (p->v < 0) {
        out->c[acc] = int_add(out->c[acc], p->c);
        return;
    }
    int j;
    for (j = 0; j < nv; j++)
        if (vars[j] == p->v) break;
    for (uint32_t i = 0; i < p->n; i++) kron_rec(p->k[i], vars, nv, D, acc + (long)i * pw[j], pw, out);
}

/* Inverse Kronecker map: read each exponent of t as base-D digits
 * (e_0, e_1, ...) and rebuild prod x_{vars[j]}^{e_j}.  Exact for images of
 * polynomials whose degree in every variable is < D, which holds for every
 * true factor of f; for other candidates the result is just some
 * polynomial that the caller's exact division will reject. */
static P *kron_inverse(ZP z, int *vars, int nv, long D) {
    P *r = p_const(K->c0);
    for (long e = 0; e <= z.d; e++) {
        if (int_is_zero(z.c[e])) continue;
        P *t = p_const(z.c[e]);
        long rem = e;
        for (int j = 0; j < nv; j++) {
            long ex = rem % D;
            rem /= D;
            if (ex) t = p_mul(t, p_pow(p_var(vars[j]), (uint32_t)ex));
        }
        r = p_add(r, t);
    }
    return r;
}

/* Factor a squarefree primitive multivariate f via Kronecker's substitution
 * (Knuth 4.6.2, "Kronecker's method" adapted to Z[t] factorisation):
 *   1. D = 1 + max degree of f in any variable; map f to the univariate
 *      image z(t) (kron_rec).  Gives up (returns 0) if the image degree
 *      D^nv would exceed 4000.
 *   2. Factor z completely over Z: z need not be squarefree, so it is split
 *      with p_sqf first and each part factored with zp_factor_sqf; the
 *      irreducible factors are listed with repetition (uf).  Gives up if
 *      there are more than 16 of them.
 *   3. Every true factor of f maps to a product of some of the uf, so try
 *      subsets of increasing size: map the product back (kron_inverse),
 *      make it primitive and test exact division of the remaining cofactor
 *      F.  Found factors are pushed with multiplicity mult and their image
 *      factors marked as used; the final cofactor is pushed too.
 * Returns 1 if f was handled (factors pushed to out), 0 if the caller must
 * treat f as irreducible.  Exponential in the number of image factors
 * (at most 2^16 subsets).  Variables with index >= 64 are not supported
 * (vars_of ignores them). */
static int kronecker(P *f, flist *out, uint32_t mult) {
    int nvmax = 64;
    int mask[64] = {0};
    vars_of(f, mask, nvmax);
    int vars[64], nv = 0;
    for (int i = 0; i < nvmax; i++)
        if (mask[i]) vars[nv++] = i;
    long D = 0;
    for (int j = 0; j < nv; j++) {
        long d = p_deg(f, vars[j]);
        if (d > D) D = d;
    }
    D += 1;
    long pw[64], tot = 1;
    for (int j = 0; j < nv; j++) {
        pw[j] = tot;
        tot *= D;
        if (tot > 4000) return 0;
    }
    /* image z(t) of f: largest exponent is sum (D-1) D^j = tot - 1 */
    ZP z = zp_alloc(tot);
    kron_rec(f, vars, nv, D, 0, pw, &z);
    z = zp_norm(z);
    /* factor the image completely (it may not be squarefree) */
    P *zpoly = zp_to_p(z, vars[nv - 1]);
    E *c;
    zpoly = p_intprim(zpoly, &c);
    P *sq[64];
    uint32_t sm[64];
    uint32_t ns = p_sqf(zpoly, sq, sm, 64);
    ZP *uf = salloc(sizeof(ZP) * 128);
    uint32_t nu = 0;
    for (uint32_t i = 0; i < ns; i++) {
        E *cc;
        P *pp = p_intprim(sq[i], &cc);
        ZP fz[64];
        uint32_t k = zp_factor_sqf(p_to_zp(pp), fz, 64);
        for (uint32_t j = 0; j < k; j++)
            for (uint32_t m = 0; m < sm[i] && nu < 128; m++) uf[nu++] = fz[j];
    }
    /* too many image factors: up to 2^nu subsets would have to be tried */
    if (nu > 16) return 0;
    /* try subsets in increasing size */
    P *F = f;
    int *used = salloc(sizeof(int) * 128);
    for (int q = 0; q < 128; q++) used[q] = 0;
    uint32_t remaining = nu;
    uint32_t s = 1;
    while (s < remaining && !p_is_const(F)) {
        uint32_t *avail = salloc(sizeof(uint32_t) * 128), na = 0;
        for (uint32_t i = 0; i < nu; i++)
            if (!used[i]) avail[na++] = i;
        if (s > na) break;
        int *idx = salloc(sizeof(int) * 128);
        for (uint32_t i = 0; i < s; i++) idx[i] = (int)i;
        int found = 0;
        for (;;) {
            poll_intr();
            ZP g = zp_alloc(0);
            g.c[0] = K->c1;
            for (uint32_t i = 0; i < s; i++) g = zp_mul(g, uf[avail[idx[i]]]);
            P *cand = kron_inverse(g, vars, nv, D);
            if (!p_is_const(cand)) {
                E *cc;
                cand = p_intprim(cand, &cc);
                if (!p_eq(cand, F)) {
                    P *q = p_divexact(F, cand);
                    if (q) {
                        fl_push(out, cand, mult);
                        F = q;
                        found = 1;
                    }
                }
            }
            if (found) {
                for (uint32_t i = 0; i < s; i++) used[avail[idx[i]]] = 1;
                remaining -= s;
                break;
            }
            int k = (int)s - 1;
            while (k >= 0 && idx[k] == (int)(na - s + (uint32_t)k)) k--;
            if (k < 0) break;
            idx[k]++;
            for (uint32_t j = (uint32_t)k + 1; j < s; j++) idx[j] = idx[j - 1] + 1;
        }
        if (!found) s++;
    }
    if (!p_is_const(F)) {
        E *cc;
        fl_push(out, p_intprim(F, &cc), mult);
    }
    return 1;
}

/* Factor a squarefree polynomial f (primitive with respect to its main
 * variable, integer coefficients) and push its irreducible factors to out
 * with multiplicity mult.  Strategy:
 *   - univariate: zp_factor_sqf (factors converted back to x_v);
 *   - degree 1 in the main variable: a primitive linear polynomial is
 *     irreducible (any factorisation would need a factor free of x_v, i.e. a
 *     divisor of the content);
 *   - homogeneous of total degree td > 0: put u = 1 for the lowest variable
 *     u other than the main one, factor the dehomogenised g recursively,
 *     homogenise each factor h again and divide it out of f (the factors
 *     of a homogeneous polynomial are homogeneous, and dehomogenisation
 *     is a bijection between factors not divisible by u and factors of g);
 *     this keeps e.g. (x+y+z)^3 (x-y+2z)^2 cheap;
 *   - otherwise Kronecker substitution; if that gives up, f is pushed as a
 *     single factor (it may then be reducible).
 * Recursive (stack_check). */
static void factor_sqf_part(P *f, flist *out, uint32_t mult) {
    stack_check();
    if (p_is_const(f)) return;
    int v = f->v;
    if (p_is_univariate(f, v)) {
        ZP *z = salloc(sizeof(ZP) * 256);
        uint32_t k = zp_factor_sqf(p_to_zp(f), z, 256);
        for (uint32_t i = 0; i < k; i++) fl_push(out, zp_to_p(z[i], v), mult);
        return;
    }
    if (f->n == 2) { /* linear in main variable: irreducible (primitive) */
        fl_push(out, f, mult);
        return;
    }
    if (CASX_SLOTS) { /* the extension's Hensel lifting (cas/ext/alg/mfactor.c), when installed */
        P *fx[64];
        uint32_t k = casx_pfactor(f, fx, 64);
        for (uint32_t i = 0; i < k; i++) fl_push(out, fx[i], mult);
        if (k) return;
    }
    long td = -1;
    if (homog_rec(f, 0, &td) && td > 0) {
        /* dehomogenise: set the lowest variable to 1 */
        int mask[64] = {0};
        vars_of(f, mask, 64);
        int u = -1;
        for (int i = 0; i < 64; i++)
            if (mask[i] && i != v) {
                u = i;
                break;
            }
        if (u >= 0) {
            P *g = p_eval(f, u, K->c1);
            flist sub;
            P *sf[64];
            uint32_t sm[64];
            sub.f = sf;
            sub.m = sm;
            sub.n = 0;
            sub.cap = 64;
            E *c;
            g = p_intprim(g, &c);
            factor_sqf_part(g, &sub, 1);
            P *rest = f;
            for (uint32_t i = 0; i < sub.n; i++) {
                P *h = homogenize(sub.f[i], u, total_degree(sub.f[i]), 0);
                E *cc;
                h = p_intprim(h, &cc);
                for (uint32_t m = 0; m < sub.m[i]; m++) {
                    P *q = p_divexact(rest, h);
                    if (!q) break;
                    fl_push(out, h, mult);
                    rest = q;
                }
            }
            if (!p_is_const(rest)) {
                E *cc;
                fl_push(out, p_intprim(rest, &cc), mult);
            }
            return;
        }
    }
    if (!kronecker(f, out, mult)) fl_push(out, f, mult);
}

/* Factor an arbitrary nonzero polynomial in kernels, pushing irreducible
 * factors with multiplicities (scaled by mult) to out; the numeric content
 * is discarded (factor_poly_expr recomputes it).  Steps: split off the
 * content with respect to the main variable (a polynomial in the smaller
 * variables, factored recursively), make the primitive part integral with
 * positive leading coefficient, decompose it with Yun (p_sqf, at most 64
 * squarefree parts) and factor each squarefree part. */
static void factor_poly_rec(P *f, flist *out, uint32_t mult) {
    stack_check();
    if (p_is_const(f)) return;
    /* content wrt main var */
    P *cont = p_content(f);
    if (!p_is_const(cont)) {
        factor_poly_rec(cont, out, mult);
        P *q = p_divexact(f, cont);
        if (q) f = q;
    }
    E *c;
    f = p_intprim(f, &c);
    if (p_is_const(f)) return;
    /* squarefree decomposition of the primitive part */
    P *sq[64];
    uint32_t sm[64];
    uint32_t ns = p_sqf(f, sq, sm, 64);
    for (uint32_t i = 0; i < ns; i++) {
        E *cc;
        P *g = p_intprim(sq[i], &cc);
        factor_sqf_part(g, out, mult * sm[i]);
    }
}

/* -------------------------------------------------------- expression --- */
/* b^m as a raw node flagged F_SIMP, so that automatic simplification does
 * not expand or merge it (factored output must stay factored); b itself for
 * m = 1. */
static E *raw_pow(E *b, long m) {
    if (m == 1) return b;
    E *e = enode(E_POW, 2);
    K0(e) = b;
    K1(e) = mk_int(m);
    e->f = F_SIMP;
    return e;
}

/* Display order of factors: smaller expressions first, ties broken by the
 * canonical order. */
static int fac_cmp(E *a, E *b) {
    /* sort factors: lower degree / simpler first */
    size_t sa = expr_size(a), sb = expr_size(b);
    if (sa != sb) return sa < sb ? -1 : 1;
    return expr_cmp(a, b);
}

void esort(E **a, uint32_t n, int (*cmp)(E *, E *));
uint32_t collect_syms(E *e, E **out, uint32_t cap);

/* preferred main variable for factor_list_in; NULL = default choice.  Set
 * only for the duration of factor_list_in (restored on errors too). */
static E *g_mainvar;

/* Pre-populate the kernel table with the symbols of e so that the variable
 * order is deterministic: symbols are entered in reverse canonical order and
 * the main variable last, so it gets the largest index (the outermost
 * variable of the recursive representation) and alphabetically earlier
 * symbols are "more main" than later ones.  The main variable is g_mainvar,
 * or x when it occurs (calculator convention).  Non-symbol kernels such as
 * sin(x) are added later by e2p and thus come after all symbols.  At most 64
 * symbols are considered. */
static void prime_kernels(E *e, ktab *kt) {
    E *v[64];
    uint32_t n = collect_syms(e, v, 64);
    E *mv = g_mainvar;
    if (!mv) /* calculator convention: x is the main variable when present */
        for (uint32_t i = 0; i < n; i++)
            if (v[i]->s == S_X) mv = v[i];
    for (uint32_t i = n; i-- > 0;)
        if (!mv || !expr_eq(v[i], mv)) kt_index(kt, v[i]);
    if (mv) kt_index(kt, mv);
}

/* factor a polynomial expression; returns content c and factor list
 * e is converted with e2p (callers pass numerators; a negative power would
 * just become a kernel), with the variable order of prime_kernels.  Stores
 * the rational content (with sign) in *cout, at most cap factors in fe with
 * multiplicities in fm, and returns the number of factors, so that
 * e = c * prod fe[i]^fm[i].  The content is fixed up at the end by dividing
 * f by the product of the factors, which also absorbs any sign changes from
 * normalising the factors.  Returns 0 (nothing factored) when e contains a
 * float or is constant. */
static uint32_t factor_poly_expr(E *e, E **cout, E **fe, long *fm, uint32_t cap) {
    ktab kt;
    kt_init(&kt);
    prime_kernels(e, &kt);
    P *p = e2p(e, &kt);
    if (!p || kt.has_float) return 0;
    E *c;
    P *f = p_intprim(p, &c);
    flist out;
    P **ff = salloc(sizeof(P *) * 256);
    uint32_t *mm = salloc(sizeof(uint32_t) * 256);
    out.f = ff;
    out.m = mm;
    out.n = 0;
    out.cap = 256;
    factor_poly_rec(f, &out, 1);
    /* fix the overall constant so that the product matches: the leading (lex-largest) numeric
     * coefficient is multiplicative, so no need to multiply the factors out (f^20 of Wester H27
     * would not fit) */
    E *ln = p_lnum(f);
    for (uint32_t i = 0; i < out.n; i++) ln = num_div(ln, num_pow_int(p_lnum(out.f[i]), mk_int(out.m[i])));
    c = num_mul(c, ln);
    *cout = c;
    uint32_t n = 0;
    for (uint32_t i = 0; i < out.n && n < cap; i++) {
        fe[n] = p2e(out.f[i], &kt);
        fm[n] = out.m[i];
        n++;
    }
    return n;
}

/* Assemble c * prod f[i]^m[i] as a raw F_SIMP product (so it is not
 * re-expanded), after sorting the factors with fac_cmp (insertion sort, n is
 * small).  Negative m give denominator factors.  The constant c is omitted
 * when it is 1. */
static E *build_product(E *c, uint32_t n, E **f, long *m) {
    /* sort factors */
    for (uint32_t i = 1; i < n; i++)
        for (uint32_t j = i; j > 0 && fac_cmp(f[j - 1], f[j]) > 0; j--) {
            E *t = f[j]; f[j] = f[j - 1]; f[j - 1] = t;
            long tm = m[j]; m[j] = m[j - 1]; m[j - 1] = tm;
        }
    E **k = salloc(sizeof(E *) * (n + 2));
    uint32_t nk = 0;
    if (!is_one(c)) k[nk++] = c;
    for (uint32_t i = 0; i < n; i++) k[nk++] = raw_pow(f[i], m[i]);
    if (nk == 0) return K->c1;
    if (nk == 1) return k[0];
    E *r = enode(E_MUL, nk);
    memcpy(EK(r), k, sizeof(E *) * nk);
    r->f = F_SIMP;
    return r;
}

/* factor(n) for an integer: the prime factorisation (int_factor: trial
 * division + Pollard-Brent rho) as a raw product of prime powers, with a
 * leading -1 for negative n.  0, 1 and -1 are returned unchanged. */
static E *factor_int(E *n) {
    if (int_is_zero(n) || int_is_one(int_abs(n))) return n;
    E *fl = int_factor(n);
    E **k = salloc(sizeof(E *) * (fl->n + 2));
    uint32_t nk = 0;
    if (n->s) k[nk++] = K->cm1;
    for (uint32_t i = 0; i < fl->n; i++) {
        long m;
        num_is_small_int(K1(EK(fl)[i]), &m);
        k[nk++] = raw_pow(K0(EK(fl)[i]), m);
    }
    if (nk == 1) return k[0];
    E *r = enode(E_MUL, nk);
    memcpy(EK(r), k, sizeof(E *) * nk);
    r->f = F_SIMP;
    return r;
}

/* factor(e): factorisation over Q of a rational expression in kernels.
 * Lists and equations are mapped; integers and rationals are factored into
 * primes; floats and symbols are returned unchanged.  Otherwise e is split
 * into numerator and denominator (numden), both are factored and the
 * result is c * prod num_i^m_i * prod den_j^(-m_j) built from raw nodes so
 * it prints factored.  If either part cannot be factored (floats), e is
 * returned unchanged.  Factor lists hold at most 256 entries. */
E *factor_expr(E *e) {
    stack_check();
    if (e->t == E_LIST) {
        E **a = salloc(sizeof(E *) * (e->n + 1));
        for (uint32_t i = 0; i < e->n; i++) a[i] = factor_expr(EK(e)[i]);
        return s_list(e->n, a);
    }
    if (IFN(e, F_EQ)) return s_fn2(F_EQ, factor_expr(K0(e)), factor_expr(K1(e)));
    if (e->t == E_INT) return factor_int(e);
    if (e->t == E_RAT) {
        E *n = factor_int(K0(e)), *d = factor_int(K1(e));
        E *k[2] = {n, raw_pow(d, -1)};
        if (d->t == E_MUL) {
            E *r = enode(E_POW, 2);
            K0(r) = d;
            K1(r) = K->cm1;
            r->f = F_SIMP;
            k[1] = r;
        }
        E *r = enode(E_MUL, 2);
        K0(r) = k[0];
        K1(r) = k[1];
        r->f = F_SIMP;
        return r;
    }
    if (e->t == E_FLT || e->t == E_SYM) return e;
    E *num, *den;
    numden(e, &num, &den);
    E **fn = salloc(sizeof(E *) * 256), **fd = salloc(sizeof(E *) * 256);
    long *mn = salloc(sizeof(long) * 256), *md = salloc(sizeof(long) * 256);
    E *cn, *cd;
    uint32_t nn = factor_poly_expr(num, &cn, fn, mn, 256);
    if (!nn && !is_num(num)) return e;
    if (is_num(num)) cn = num;
    uint32_t nd = 0;
    cd = K->c1;
    if (!is_one(den)) {
        nd = factor_poly_expr(den, &cd, fd, md, 256);
        if (!nd && !is_num(den)) return e;
        if (is_num(den)) cd = den;
    }
    E *c = num_div(cn, cd);
    for (uint32_t i = 0; i < nd && nn < 256; i++) {
        fn[nn] = fd[i];
        mn[nn] = -md[i];
        nn++;
    }
    return build_product(c, nn, fn, mn);
}

/* sqrfree(e): squarefree decomposition of the numerator of e (Yun, in the
 * main variable chosen by prime_kernels), as c * cont * prod h_i^i where
 * cont is the content with respect to the main variable (kept as a single
 * factor, not decomposed further) and the h_i are primitive.  The
 * denominator, if any, is divided back unchanged.  Returns e unchanged if
 * the numerator is constant. */
E *sqrfree_expr(E *e) {
    E *num, *den;
    numden(e, &num, &den);
    ktab kt;
    kt_init(&kt);
    prime_kernels(e, &kt);
    P *p = e2p(num, &kt);
    if (!p) return e;
    E *c;
    P *f = p_intprim(p, &c);
    if (p_is_const(f)) return e;
    P *cont = p_content(f);
    P *sq[64];
    uint32_t sm[64];
    P *g = p_divexact(f, cont);
    if (!g) g = f;
    uint32_t ns = p_sqf(g, sq, sm, 64);
    E **fe = salloc(sizeof(E *) * 66);
    long *fm = salloc(sizeof(long) * 66);
    uint32_t n = 0;
    if (!p_is_const(cont)) {
        fe[n] = p2e(cont, &kt);
        fm[n++] = 1;
    } else
        c = num_mul(c, cont->c);
    P *prod = p_const(K->c1);
    for (uint32_t i = 0; i < ns; i++) {
        E *cc;
        P *h = p_intprim(sq[i], &cc);
        prod = p_mul(prod, p_pow(h, sm[i]));
        fe[n] = p2e(h, &kt);
        fm[n++] = sm[i];
    }
    P *q = p_divexact(g, prod);
    if (q && p_is_const(q)) c = num_mul(c, q->c);
    E *r = build_product(c, n, fe, fm);
    if (!is_one(den)) r = s_div(r, den);
    return r;
}

E *factor_list(E *e, E **content);

/* factor with x as the main variable (factors normalised wrt x)
 * Same as factor_list, but x is forced to be the main variable, so the
 * returned factors have a positive leading coefficient in x.  g_mainvar is
 * set for the call and restored afterwards, also when an error is
 * propagated (try_enter/setjmp + rethrow). */
E *factor_list_in(E *e, E *x, E **content) {
    E *old = g_mainvar;
    g_mainvar = x;
    tryf tf;
    try_enter(&tf);
    E *volatile r = NULL;
    if (!setjmp(tf.jb)) {
        r = factor_list(e, content);
        try_leave(&tf);
    } else {
        try_leave(&tf);
        g_mainvar = old;
        rethrow();
    }
    g_mainvar = old;
    return r;
}

/* list of [factor, multiplicity] (used by solve / integrate)
 * e must be a polynomial expression.  Returns LIST([f1, m1], [f2, m2], ...)
 * and stores the numeric content in *content, so that e = content * prod
 * fi^mi.  A number gives an empty list with content e; a polynomial that
 * cannot be factored (floats) gives [[e, 1]] with content 1. */
E *factor_list(E *e, E **content) {
    E **fe = salloc(sizeof(E *) * 256);
    long *fm = salloc(sizeof(long) * 256);
    E *c;
    uint32_t n = factor_poly_expr(e, &c, fe, fm, 256);
    if (!n) {
        if (is_num(e)) {
            *content = e;
            return s_list(0, NULL);
        }
        *content = K->c1;
        E *pr[2] = {e, K->c1};
        E *l = s_list(2, pr);
        return s_list(1, &l);
    }
    *content = c;
    E **items = salloc(sizeof(E *) * n);
    for (uint32_t i = 0; i < n; i++) {
        E *pr[2] = {fe[i], mk_int(fm[i])};
        items[i] = s_list(2, pr);
    }
    return s_list(n, items);
}

/* irreducible factors (with multiplicity) of a polynomial in kernels
 * P-level interface for heurisch.c: up to cap factors in facs / mult, numeric
 * content dropped; returns the number of factors. */
uint32_t p_factor(P *f, P **facs, uint32_t *mult, uint32_t cap) {
    flist out;
    out.f = facs;
    out.m = mult;
    out.n = 0;
    out.cap = cap;
    factor_poly_rec(f, &out, 1);
    return out.n;
}
