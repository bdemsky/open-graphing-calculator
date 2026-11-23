/*
 * xcmds.c -- extension commands looked up by name (no entry in BUILTIN_SYMS,
 * so features can add commands without touching the shared symbol table),
 * the top-level hooks of cas_eval, and builtins that used to say "command not
 * implemented" (digits, pade, fourier_an, randpoly, egcd), expexpand,
 * piecewise evaluation, bitwise and/or/not on integers.
 *
 * Hooks (one line each in the shared files):
 *   eval.c   ext_dispatch(id, n, args) for calls that are neither commands
 *            nor user functions, with evaluated arguments; it forwards to
 *            ext_cmd (name-dispatched commands, NULL: not ours) for
 *            non-builtin names and to ext_logic (and/or/not on integers ->
 *            bitwise) for builtins
 *   cas.c    ext_pre(e) / ext_post(e, r) around the top-level evaluation
 *   cmds.c   xcmd_builtin(id, n, args) for the builtins listed above,
 *            exp_expand for expexpand
 *   simp.c   piecewise_simp(n, args)   automatic evaluation of piecewise
 *   parse.c  basen_lexlen / basen_value (0x.. 0b.. 0o.. literals), unit tails
 *   print.c  units_print (29.4_m/s)
 * The unit commands (convert, deltaconvert, usimplify, uvalue, uunits) are
 * name-dispatched here and implemented in units.c; the uncertainty commands
 * (pm, nominal, uncert, ucov) in unc.c.
 *
 * Name dispatch: a call such as rowSwap(M, 1, 2) parses as an ordinary
 * function application whose head is a *user* symbol (id >= S__COUNT).
 * When it is evaluated and the name has no user definition, eval.c offers it
 * to ext_dispatch, which looks the name up in xtab (name, opcode, argument
 * count range); unknown names fall through and stay symbolic f(x).  This
 * keeps the BUILTIN_SYMS table (and therefore the ids of everything else)
 * unchanged when commands are added.
 *
 * State: besides scratch allocations, this file keeps three statics that
 * persist across evaluations (and are not reset by cas_reset): the
 * xorshift state of randpoly / randMat (so successive calls differ, unlike
 * K->rng, which is reseeded per evaluation) and the word size / signedness
 * of the base-n commands.
 *
 * References: Pade approximants and the linear system for their
 * denominator, e.g. Baker & Graves-Morris, "Pade Approximants"; extended
 * Euclidean algorithm, Knuth TAOCP vol. 2, 4.5.2 / von zur Gathen & Gerhard
 * ch. 3; condition number in the 1-norm, Golub & Van Loan, "Matrix
 * Computations", 2.6.
 */
#include "casx.h" /* casx_dispatch: cond(A, p) */
#include <math.h>
#include <string.h>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

E *units_cmd(int op, uint32_t n, E **a);
E *units_post(E *r, int force);
E *unc_cmd(int op, uint32_t n, E **a); /* unc.c: pm, nominal, uncert, ucov */
E *unc_post(E *e, E *r);

/* Throw CAS_ERR_DOMAIN unless lo <= n <= hi. */
static void xnargs(uint32_t n, uint32_t lo, uint32_t hi) {
    if (n < lo || n > hi) cas_throw(CAS_ERR_DOMAIN, "wrong number of arguments");
}

/* The value of a machine-size integer node; throws CAS_ERR_DOMAIN
 * otherwise. */
static long xint(E *e) {
    long v;
    if (!num_is_small_int(e, &v)) cas_throw(CAS_ERR_DOMAIN, "expected an integer");
    return v;
}

/* random numbers that survive between evaluations (K->rng is reset per eval).
 * Marsaglia's xorshift32 (shifts 13, 17, 5); period 2^32 - 1. */
static uint32_t xr_state = 0x2545F491u;
/* Next 32-bit value of the persistent generator. */
static uint32_t xrand(void) {
    uint32_t x = xr_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return xr_state = x;
}
/* Uniform-ish integer in [lo, hi] (modulo bias is negligible for the small
 * ranges used; hi - lo + 1 must fit in 32 bits). */
static long xrand_range(long lo, long hi) { return lo + (long)(xrand() % (uint32_t)(hi - lo + 1)); }

/* ------------------------------------------------------------ matrices --- */
/* Mutable working copy of a matrix: r x c entry pointers in row-major order
 * in one scratch array (the E_LIST-of-rows representation is immutable).
 * XM(m, i, j) is entry (i, j), 0-based.  xm_from copies a list of rows in,
 * xm_to builds a new list of rows. */
typedef struct {
    uint32_t r, c;
    E **a;
} xm;
#define XM(m, i, j) ((m).a[(size_t)(i) * (m).c + (j)])

/* Zero r x c matrix; at most 10000 entries (CAS_ERR_DOMAIN otherwise). */
static xm xm_new(uint32_t r, uint32_t c) {
    if ((size_t)r * c > 10000 || !r || !c) cas_throw(CAS_ERR_DOMAIN, "bad matrix size");
    xm m = {r, c, salloc(sizeof(E *) * ((size_t)r * c + 1))};
    for (size_t i = 0; i < (size_t)r * c; i++) m.a[i] = K->c0;
    return m;
}

/* Working copy of matrix e (CAS_ERR_DOMAIN if e is not a matrix). */
static xm xm_from(E *e) {
    uint32_t r, c;
    if (!is_matrix(e, &r, &c)) cas_throw(CAS_ERR_DOMAIN, "expected a matrix");
    xm m = xm_new(r, c);
    for (uint32_t i = 0; i < r; i++)
        for (uint32_t j = 0; j < c; j++) XM(m, i, j) = EK(EK(e)[i])[j];
    return m;
}

/* Back to the canonical list-of-rows form. */
static E *xm_to(xm m) {
    E **rows = salloc(sizeof(E *) * (m.r + 1));
    for (uint32_t i = 0; i < m.r; i++) rows[i] = s_list(m.c, &XM(m, i, 0));
    return s_list(m.r, rows);
}

/* 1-based user row index -> 0-based index, range-checked. */
static uint32_t xrow(E *e, xm m) {
    long i = xint(e);
    if (i < 1 || i > (long)m.r) cas_throw(CAS_ERR_DOMAIN, "row index out of range");
    return (uint32_t)(i - 1);
}

/* Normalise an entry so zero tests are reliable: numbers as they are,
 * anything else through rat_normal. */
static E *xnz(E *e) { return is_exactnum(e) ? e : rat_normal(e); }

/* row echelon form (leading 1s, zeros below), ref( as on other calculators.
 * Gaussian elimination column by column.  Pivot choice:
 *   - exact / symbolic matrices: the first row whose entry is not zero
 *     after rat_normal (so symbolic entries work, like rref in linalg.c);
 *   - matrices containing floats: partial pivoting (largest |entry|), and a
 *     column whose largest entry is below 1e-13 in absolute value is
 *     treated as zero (no pivot), the usual numerical safeguard.
 * The pivot row is scaled to a leading 1 and multiples of it are
 * subtracted from the rows below only (unlike rref).  O(r c min(r, c))
 * entry operations; polls for interrupts once per column. */
static E *mat_ref(E *e) {
    xm m = xm_from(e);
    int fl = 0;
    for (size_t i = 0; i < (size_t)m.r * m.c; i++) fl |= has_float(m.a[i]);
    uint32_t row = 0;
    for (uint32_t col = 0; col < m.c && row < m.r; col++) {
        poll_intr();
        int p = -1;
        double best = 0;
        for (uint32_t i = row; i < m.r; i++) {
            double v;
            if (fl) {
                if (to_double(XM(m, i, col), &v) && fabs(v) > best) best = fabs(v), p = (int)i;
            } else if (!is_zero(xnz(XM(m, i, col)))) {
                p = (int)i;
                break;
            }
        }
        if (p < 0 || (fl && best < 1e-13)) continue;
        for (uint32_t j = 0; j < m.c; j++) {
            E *t = XM(m, row, j);
            XM(m, row, j) = XM(m, (uint32_t)p, j);
            XM(m, (uint32_t)p, j) = t;
        }
        E *ip = s_inv(XM(m, row, col));
        for (uint32_t j = 0; j < m.c; j++) XM(m, row, j) = xnz(s_mul2(XM(m, row, j), ip));
        for (uint32_t i = row + 1; i < m.r; i++) {
            E *f = XM(m, i, col);
            if (is_zero(f)) continue;
            for (uint32_t j = 0; j < m.c; j++) XM(m, i, j) = xnz(s_sub(XM(m, i, j), s_mul2(f, XM(m, row, j))));
        }
        row++;
    }
    return xm_to(m);
}

/* 1-norm (maximum absolute column sum), returned as the exact column sum
 * expression; the maximum is chosen by comparing double values, so the
 * matrix must be numeric (CAS_ERR_DOMAIN otherwise). */
static E *norm1(xm m) {
    E *best = NULL;
    double bv = -1;
    for (uint32_t j = 0; j < m.c; j++) {
        E **t = salloc(sizeof(E *) * m.r);
        for (uint32_t i = 0; i < m.r; i++) t[i] = s_fn1(F_ABS, XM(m, i, j));
        E *s = s_add(m.r, t);
        double v;
        if (!to_double(s, &v)) cas_throw(CAS_ERR_DOMAIN, "cond: numeric matrix expected");
        if (v > bv) bv = v, best = s;
    }
    return best;
}

/* cumSum: running sums of a list, or of each column of a matrix (row i
 * becomes the sum of rows 0..i). */
static E *cumsum(E *e) {
    uint32_t r, c;
    if (is_matrix(e, &r, &c)) {
        xm m = xm_from(e);
        for (uint32_t i = 1; i < r; i++)
            for (uint32_t j = 0; j < c; j++) XM(m, i, j) = s_add2(XM(m, i - 1, j), XM(m, i, j));
        return xm_to(m);
    }
    if (e->t != E_LIST) cas_throw(CAS_ERR_DOMAIN, "cumSum: expected a list or matrix");
    E **v = salloc(sizeof(E *) * (e->n + 1));
    for (uint32_t i = 0; i < e->n; i++) v[i] = i ? s_add2(v[i - 1], EK(e)[i]) : EK(e)[i];
    return s_list(e->n, v);
}

/* ---------------------------------------------------------------- base-n --- */
/* Programmer-mode word: integers are reduced modulo 2^ws_bits for the bit
 * commands, and results are read back as two's complement when ws_signed
 * (set with wordsize(bits, signed); default 64-bit signed). */
static uint8_t ws_bits = 64, ws_signed = 1;

/* Integer node -> its w-bit pattern (value mod 2^w, so negative numbers
 * become their two's complement).  Throws for non-integers. */
static uint64_t to_u64(E *e) {
    if (e->t != E_INT) cas_throw(CAS_ERR_DOMAIN, "expected an integer");
    E *two_w = int_shl(mk_int(1), ws_bits);
    E *m = int_mod(e, two_w); /* 0 <= m < 2^w */
    uint64_t v = 0;
    for (uint32_t i = m->n; i-- > 0;) v = (v << 32) | EL(m)[i];
    return v;
}

/* Mask of the low ws_bits bits. */
static uint64_t wmask(void) { return ws_bits >= 64 ? ~(uint64_t)0 : (((uint64_t)1 << ws_bits) - 1); }

/* w-bit pattern -> integer node: truncated to the word, then interpreted as
 * two's complement (top bit set means v - 2^w) in signed mode, unsigned
 * otherwise. */
static E *from_u64(uint64_t v) {
    v &= wmask();
    if (ws_signed && ws_bits && (v >> (ws_bits - 1)) & 1) {
        if (ws_bits == 64) return mk_int((int64_t)v);
        return mk_int((int64_t)v - ((int64_t)1 << ws_bits));
    }
    return mk_uint(v);
}

/* Bit operations on the w-bit words of integer arguments.
 * op: 0 and, 1 or, 2 xor (over all n arguments), 3 not, 4 shl, 5 shr
 * (logical, on the word pattern), 6 rotl, 7 rotr; for 4..7 a[1] is the
 * shift count (negative shifts go the other way, |k| >= 64 gives 0;
 * rotations are taken mod w, and rotr by k is rotl by w - k).  The result
 * is truncated to the word and read back via from_u64. */
static E *bitop(int op, uint32_t n, E **a) {
    uint64_t r = to_u64(a[0]), m = wmask();
    long k = n > 1 && op >= 3 ? xint(a[1]) : 0;
    unsigned w = ws_bits;
    switch (op) {
    case 0: for (uint32_t i = 1; i < n; i++) r &= to_u64(a[i]); break;
    case 1: for (uint32_t i = 1; i < n; i++) r |= to_u64(a[i]); break;
    case 2: for (uint32_t i = 1; i < n; i++) r ^= to_u64(a[i]); break;
    case 3: r = ~r; break;
    case 4: r = k >= 64 || k <= -64 ? 0 : k >= 0 ? r << k : r >> -k; break; /* shl */
    case 5: r = k >= 64 || k <= -64 ? 0 : k >= 0 ? r >> k : r << -k; break; /* shr (logical) */
    case 6: case 7: {
        k %= (long)w;
        if (k < 0) k += w;
        if (op == 7) k = ((long)w - k) % (long)w;
        if (k) r = ((r << k) | (r >> (w - (unsigned)k))) & m;
        break;
    }
    }
    return from_u64(r);
}

/* integer -> "0x1F" / "0b101" / "0o17"; negative values in two's complement.
 * Returns a string node (the calculator displays it verbatim; the parser
 * reads the same literal syntax back).  Negative numbers are first reduced
 * modulo 2^ws_bits; non-negative ones are printed in full even if wider
 * than the word.  Digits are extracted directly from the bignum limbs, 1, 3
 * or 4 bits at a time, most significant first; numbers above 4096 bits are
 * refused. */
static E *to_base_str(E *e, int base) {
    if (e->t != E_INT) cas_throw(CAS_ERR_DOMAIN, "expected an integer");
    int sh = base == 16 ? 4 : base == 8 ? 3 : 1;
    E *v = e;
    if (int_sgn(v) < 0) v = int_mod(v, int_shl(mk_int(1), ws_bits));
    uint32_t bits = int_bits(v);
    if (bits > 4096) cas_throw(CAS_ERR_DOMAIN, "number too large");
    char *buf = salloc(bits + 8);
    size_t n = 0;
    buf[n++] = '0';
    buf[n++] = base == 16 ? 'x' : base == 8 ? 'o' : 'b';
    int nd = (int)((bits + (uint32_t)sh - 1) / (uint32_t)sh);
    if (nd == 0) nd = 1;
    for (int d = nd - 1; d >= 0; d--) {
        uint32_t dig = 0;
        for (int b = sh - 1; b >= 0; b--) {
            uint32_t bit = (uint32_t)(d * sh + b), li = bit / 32;
            dig = dig << 1 | (li < v->n ? (EL(v)[li] >> (bit % 32)) & 1 : 0);
        }
        buf[n++] = "0123456789ABCDEF"[dig];
    }
    return mk_str(buf, n);
}

/* lexer support for 0x.. 0b.. 0o.. (parse.c) */
/* Digit value of a hex character, 99 for anything else (so `>= base`
 * rejects it for every base). */
static int dig_val(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 99;
}
/* Base selected by the letter after a leading 0 (x/b/o, either case), or 0. */
static int base_of(char c) { return c == 'x' || c == 'X' ? 16 : c == 'b' || c == 'B' ? 2 : c == 'o' || c == 'O' ? 8 : 0; }

/* Length of a base-n literal at s ("0x1F", "0b101", "0o17"), or 0 if s does
 * not start with one.  At least one digit is required, and the literal must
 * not run straight into a letter or '_' (so 0b1e or 0xFFg are not literals
 * and the lexer falls back to its normal rules). */
size_t basen_lexlen(const char *s) {
    int b = s[0] == '0' ? base_of(s[1]) : 0;
    if (!b || dig_val((unsigned char)s[2]) >= b) return 0;
    size_t p = 2;
    while (dig_val((unsigned char)s[p]) < b) p++;
    if ((s[p] >= 'a' && s[p] <= 'z') || (s[p] >= 'A' && s[p] <= 'Z') || s[p] == '_') return 0;
    return p;
}

/* Value of a literal accepted by basen_lexlen (n = its length): Horner's
 * rule on bignums, so arbitrarily long literals are exact. */
E *basen_value(const char *s, size_t n) {
    int b = base_of(s[1]);
    E *v = K->c0;
    for (size_t i = 2; i < n; i++) v = int_add(int_mul_si(v, b), mk_int(dig_val((unsigned char)s[i])));
    return v;
}

/* and / or / not with integer arguments are bitwise (eval.c); NULL otherwise
 * (booleans and symbolic conditions keep their logical meaning). */
static E *ext_logic(int id, uint32_t n, E **a) {
    if ((id != F_AND && id != F_OR && id != F_NOT) || n < 1 || n > 2) return NULL;
    for (uint32_t i = 0; i < n; i++)
        if (a[i]->t != E_INT) return NULL;
    return bitop(id == F_AND ? 0 : id == F_OR ? 1 : 3, n, a);
}

/* ------------------------------------------------------------ calculus --- */
/* [m/n] Pade approximant of f at x = 0: the rational function P/Q with
 * deg P <= m, deg Q <= n, Q(0) = 1 and f - P/Q = O(x^(m+n+1)).
 * With the Taylor coefficients c_0..c_(m+n) of f (series_expr; they must be
 * free of x, i.e. a genuine power series) the denominator q_1..q_n solves
 * the n x n linear system
 *   sum_{j=1..n} q_j c_(k-j) = -c_k,   k = m+1 .. m+n   (c_i = 0 for i < 0),
 * solved exactly by rref of the augmented matrix; a singular system (the
 * approximant does not exist in this normalisation) throws CAS_ERR_DOMAIN.
 * The numerator is then the truncated product: p_i = sum_{j<=i} q_j c_(i-j),
 * i = 0..m.  Orders are limited to m + n <= 60.  Result P/Q in scratch. */
static E *pade(E *f, E *x, long m, long n) {
    if (m < 0 || n < 0 || m + n > 60) cas_throw(CAS_ERR_DOMAIN, "pade: bad orders");
    E *s = series_expr(f, x, K->c0, m + n + 1);
    if (!s) cas_throw(CAS_ERR_UNSUPPORTED, "pade: no series");
    long N = m + n;
    E **c = salloc(sizeof(E *) * (size_t)(N + 1));
    for (long k = 0; k <= N; k++) {
        c[k] = poly_coeff(s, x, k);
        if (!freeof(c[k], x)) cas_throw(CAS_ERR_UNSUPPORTED, "pade: not a power series");
    }
#define CC(k) ((k) < 0 ? K->c0 : c[k])
    /* q_0 = 1; sum_{j=0..n} q_j c_{k-j} = 0 for k = m+1..m+n */
    E **q = salloc(sizeof(E *) * (size_t)(n + 1));
    q[0] = K->c1;
    if (n > 0) {
        xm A = xm_new((uint32_t)n, (uint32_t)n + 1);
        for (long i = 0; i < n; i++) {
            for (long j = 1; j <= n; j++) XM(A, i, j - 1) = CC(m + 1 + i - j);
            XM(A, i, n) = s_neg(CC(m + 1 + i));
        }
        long rk;
        E *R = mat_rref(xm_to(A), &rk);
        for (long i = 0; i < n; i++) {
            E *row = EK(R)[i];
            if (!is_one(EK(row)[i])) cas_throw(CAS_ERR_DOMAIN, "pade: singular system");
            q[i + 1] = EK(row)[n];
        }
    }
    E **pt = salloc(sizeof(E *) * (size_t)(m + 1)), **qt = salloc(sizeof(E *) * (size_t)(n + 1));
    for (long i = 0; i <= m; i++) {
        E *p = K->c0;
        for (long j = 0; j <= n && j <= i; j++) p = s_add2(p, s_mul2(q[j], CC(i - j)));
        pt[i] = s_mul2(p, s_powi(x, i));
    }
    for (long j = 0; j <= n; j++) qt[j] = s_mul2(q[j], s_powi(x, j));
#undef CC
    return s_div(s_add((uint32_t)m + 1, pt), s_add((uint32_t)n + 1, qt));
}

/* Fourier coefficients over [a, a+T]: an = 2/T int f cos(2 pi n x/T) (a0 = mean), bn with sin.
 * fourier_an(f, x, T, n [, a]) / fourier_bn(...): sinp selects b_n.  When n
 * is a symbol it is temporarily assumed to be an integer (A_INT), so that
 * the definite integral simplifies sin(2 pi n) = 0 and cos(2 pi n) = 1 and a
 * closed form in n results; the previous assumption flags are restored
 * afterwards, also on error.  n = 0 for a_n gives the mean value (1/T) int
 * f, i.e. the a_0/2 convention.  Integration by integrate_def; result
 * simplified. */
static E *fourier(int sinp, uint32_t nargs_, E **a) {
    xnargs(nargs_, 4, 5);
    E *f = a[0], *x = a[1], *T = a[2], *n = a[3], *lo = nargs_ > 4 ? a[4] : K->c0;
    if (x->t != E_SYM) cas_throw(CAS_ERR_DOMAIN, "fourier: expected a variable");
    uint8_t saved = 0;
    if (n->t == E_SYM && n->s >= S__COUNT) {
        saved = K->syms[n->s].assume;
        K->syms[n->s].assume |= A_INT;
    }
    E *w = s_div(s_mul2(s_mul2(K->c2, K->pi_), s_mul2(n, x)), T);
    E *volatile r = NULL;
    tryf tf;
    try_enter(&tf);
    if (!setjmp(tf.jb)) {
        if (!sinp && is_zero(n))
            r = s_div(integrate_def(f, x, lo, s_add2(lo, T)), T);
        else
            r = s_mul2(s_div(K->c2, T),
                       integrate_def(s_mul2(f, s_fn1(sinp ? F_SIN : F_COS, w)), x, lo, s_add2(lo, T)));
        r = simplify_full(r);
        try_leave(&tf);
    } else {
        /* errors (e.g. a divergent integral) must not leave n an integer */
        try_leave(&tf);
        if (n->t == E_SYM && n->s >= S__COUNT) K->syms[n->s].assume = saved;
        rethrow();
    }
    if (n->t == E_SYM && n->s >= S__COUNT) K->syms[n->s].assume = saved;
    return r;
}

/* extended Euclid: [u, v, d] with u*a + v*b = d = gcd(a, b).
 * Integers use int_egcd.  Polynomials in x (coefficients in Q or symbolic,
 * treated as a field through poly_quo_rem) run the classical extended
 * Euclidean algorithm keeping the cofactor sequences s_i, t_i with
 * s_i a + t_i b = r_i (Knuth 4.6.1, von zur Gathen & Gerhard 3.2); at the
 * end everything is divided by the leading coefficient so d is monic.
 * At most 500 division steps (CAS_ERR_UNSUPPORTED beyond); intermediate
 * coefficient growth is not controlled (no subresultants), which is fine for
 * the classroom-sized inputs it is meant for. */
static E *egcd(E *a, E *b, E *x) {
    if (a->t == E_INT && b->t == E_INT) {
        E *s, *t;
        E *g = int_egcd(a, b, &s, &t);
        E *v[3] = {s, t, g};
        return s_list(3, v);
    }
    E *r0 = a, *r1 = b, *s0 = K->c1, *s1 = K->c0, *t0 = K->c0, *t1 = K->c1;
    for (int it = 0; !is_zero(r1); it++) {
        if (it > 500) cas_throw(CAS_ERR_UNSUPPORTED, "egcd: too many steps");
        poll_intr();
        E *rem;
        E *q = poly_quo_rem(r0, r1, x, &rem);
        E *s2 = expand(s_sub(s0, s_mul2(q, s1))), *t2 = expand(s_sub(t0, s_mul2(q, t1)));
        r0 = r1, r1 = expand(rem), s0 = s1, s1 = s2, t0 = t1, t1 = t2;
    }
    long d;
    E *lc = poly_degree_in(r0, x, &d) ? poly_coeff(r0, x, d) : r0;
    if (is_zero(lc)) lc = K->c1;
    E *v[3] = {expand(s_div(s0, lc)), expand(s_div(t0, lc)), expand(s_div(r0, lc))};
    return s_list(3, v);
}

/* randpoly([x], [d]): random polynomial of degree d (default 5, at most
 * 200) in x (default x) with integer coefficients in [-99, 99] and a
 * positive leading coefficient.  Uses the persistent xorshift state. */
static E *randpoly(uint32_t n, E **a) {
    E *x = mk_sym(S_X);
    long d = 5;
    for (uint32_t i = 0; i < n; i++) {
        if (a[i]->t == E_SYM) x = a[i];
        else d = xint(a[i]);
    }
    if (d < 0 || d > 200) cas_throw(CAS_ERR_DOMAIN, "randpoly: bad degree");
    E **t = salloc(sizeof(E *) * (size_t)(d + 1));
    for (long k = 0; k <= d; k++) {
        long c = xrand_range(-99, 99);
        if (k == d && c == 0) c = 1 + xrand_range(0, 98);
        t[k] = s_mul2(mk_int(c), s_powi(x, k));
    }
    return s_add((uint32_t)d + 1, t);
}

/* digits(n [, b]): the list of base-b digits of |n|, most significant first
 * (default base 10, 2 <= b <= 10^6); repeated bignum division, O(len^2). */
static E *digits_of(uint32_t n, E **a) {
    xnargs(n, 1, 2);
    E *v = a[0];
    if (v->t != E_INT) cas_throw(CAS_ERR_DOMAIN, "digits: expected an integer");
    long b = n > 1 ? xint(a[1]) : 10;
    if (b < 2 || b > 1000000) cas_throw(CAS_ERR_DOMAIN, "digits: bad base");
    v = int_abs(v);
    uint32_t cap = int_bits(v) + 2, k = 0;
    E **d = salloc(sizeof(E *) * cap);
    E *B = mk_int(b);
    do {
        E *q, *r;
        int_tdivmod(v, B, &q, &r);
        d[k++] = r;
        v = q;
        poll_intr();
    } while (!int_is_zero(v) && k < cap);
    for (uint32_t i = 0; i < k / 2; i++) {
        E *t = d[i];
        d[i] = d[k - 1 - i];
        d[k - 1 - i] = t;
    }
    return s_list(k, d);
}

/* exp(a+b) -> exp(a)*exp(b), kept as an unsimplified product.
 * The automatic simplifier would immediately merge the factors back into
 * exp(a+b), so the new product node (and every ancestor rebuilt on the way
 * up) is built raw and flagged F_SIMP, which tells simp() to leave it
 * alone.  Unchanged subtrees are shared, not copied. */
static E *expx(E *e) {
    if (e->t <= E_SYM || e->t == E_STR) return e;
    stack_check();
    if (e->t == E_POW && ISYM(K0(e), S_E) && K1(e)->t == E_ADD) {
        E *s = K1(e);
        E *p = enode(E_MUL, s->n);
        for (uint32_t i = 0; i < s->n; i++) EK(p)[i] = s_exp(EK(s)[i]);
        p->f = F_SIMP;
        return p;
    }
    E *c = enode(e->t, e->n);
    c->s = e->s;
    int ch = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        EK(c)[i] = expx(EK(e)[i]);
        ch |= EK(c)[i] != EK(e)[i];
    }
    if (!ch) return e;
    c->f = F_SIMP;
    return c;
}
/* expexpand(e): expand first (so exponents become sums), then split. */
E *exp_expand(E *e) { return expx(expand(e)); }

/* ------------------------------------------------------------ piecewise --- */
/* 1 true, 0 false, -1 unknown.
 * Truth value of a piecewise condition: true / false, numbers (nonzero is
 * true), not / and / or with three-valued logic (false and unknown = false,
 * true or unknown = true), and relations: structurally equal sides decide
 * =, <=, >= (true) and <, >, != (false); otherwise both sides must be
 * constant expressions, = and != are decided exactly via rat_normal and the
 * order relations in double precision (so values closer than ~1e-16
 * relative can be misjudged, as in eval.c). */
static int truth(E *c) {
    stack_check();
    if (ISYM(c, S_TRUE)) return 1;
    if (ISYM(c, S_FALSE)) return 0;
    if (is_num(c)) return !num_is_zero(c);
    if (c->t != E_FN) return -1;
    if (c->s == F_NOT && c->n == 1) {
        int t = truth(K0(c));
        return t < 0 ? -1 : !t;
    }
    if ((c->s == F_AND || c->s == F_OR) && c->n == 2) {
        int x = truth(K0(c)), y = truth(K1(c));
        if (c->s == F_AND) return (x == 0 || y == 0) ? 0 : (x == 1 && y == 1) ? 1 : -1;
        return (x == 1 || y == 1) ? 1 : (x == 0 && y == 0) ? 0 : -1;
    }
    if (c->n != 2 || c->s < F_EQ || c->s > F_NE || c->s == F_ASSIGN) return -1;
    E *l = K0(c), *r = K1(c);
    double a, b;
    if (expr_eq(l, r)) return c->s == F_EQ || c->s == F_LE || c->s == F_GE;
    if (!is_const_expr(l) || !is_const_expr(r) || !to_double(l, &a) || !to_double(r, &b)) return -1;
    switch (c->s) {
    case F_EQ: return is_zero(rat_normal(s_sub(l, r)));
    case F_NE: return !is_zero(rat_normal(s_sub(l, r)));
    case F_LT: return a < b;
    case F_GT: return a > b;
    case F_LE: return a <= b;
    case F_GE: return a >= b;
    }
    return -1;
}

/* piecewise(c1, v1, c2, v2, ..., [otherwise]).
 * Automatic simplification rule (called from simp.c with simplified
 * arguments).  Conditions are scanned in order: a true one selects its
 * value; false ones are dropped; the scan stops at the first undecidable
 * condition.  If every condition was false the result is the trailing
 * `otherwise` value, or undef if there is none.  Returns NULL ("no change",
 * the caller keeps the node) when the very first condition is undecidable,
 * otherwise a new piecewise node without the false prefix (flagged F_SIMP). */
E *piecewise_simp(uint32_t n, E **a) {
    uint32_t i = 0;
    while (i + 1 < n) {
        int t = truth(a[i]);
        if (t == 1) return a[i + 1];
        if (t < 0) break;
        i += 2;
    }
    if (i + 1 >= n) return i < n ? a[i] : K->undef_;
    if (i == 0) return NULL;
    E *e = enode(E_FN, n - i);
    e->s = F_PIECEWISE;
    memcpy(EK(e), a + i, sizeof(E *) * (n - i));
    e->f = F_SIMP;
    return e;
}

/* --------------------------------------------- builtins from cmds.c --- */
/* Builtin commands that have a C_* id in BUILTIN_SYMS but are implemented
 * here (eval_cmd forwards them): digits, pade(f, x, m, n), fourier_an,
 * randpoly, egcd(a, b [, x]).  Arguments are evaluated; any other id
 * throws "command not implemented". */
E *xcmd_builtin(int id, uint32_t n, E **a) {
    switch (id) {
    case C_DIGITS: return digits_of(n, a);
    case C_PADE: {
        xnargs(n, 4, 4);
        if (a[1]->t != E_SYM) cas_throw(CAS_ERR_DOMAIN, "pade(f, x, m, n)");
        return pade(a[0], a[1], xint(a[2]), xint(a[3]));
    }
    case C_FOURIERA: return fourier(0, n, a);
    case C_RANDPOLY: xnargs(n, 0, 2); return randpoly(n, a);
    case C_EGCD: {
        xnargs(n, 2, 3);
        E *x = n > 2 ? a[2] : default_var(s_add2(a[0], a[1]));
        return egcd(a[0], a[1], x);
    }
    }
    cas_throw(CAS_ERR_UNSUPPORTED, "command not implemented");
}

/* ------------------------------------------------- name-dispatched table --- */
/* Opcodes of the name-dispatched commands; X_CONVERT..X_UUNITS must stay
 * last and in this order, because xcmd passes op - X_CONVERT to units_cmd
 * (0 convert, 1 deltaconvert, 2 usimplify, 3 uvalue, 4 uunits). */
enum {
    X_DIM, X_FILL, X_RANDMAT, X_AUGMENT, X_REF, X_ROWSWAP, X_ROWADD, X_MROW, X_MROWADD, X_CUMSUM, X_COND,
    X_SETELEM, X_REDIM, X_FOURIERB, X_BITAND, X_BITOR, X_BITXOR, X_BITNOT, X_SHL, X_SHR, X_ROTL, X_ROTR,
    X_XOR, X_WORDSIZE, X_HEX, X_BIN, X_OCT, X_AUTOSIMP, X_FORMS, X_PM, X_NOMINAL, X_UNCERT, X_UCOV, X_CONVERT, X_DCONVERT, X_USIMPLIFY, X_UVALUE, X_UUNITS
};
/* Name -> opcode and allowed argument count [lo, hi].  Names are case
 * sensitive (rowSwap, mRowAdd: the usual calculator spellings).  Searched linearly
 * (about 30 entries, first character compared first). */
static const struct {
    char name[13];
    uint8_t op, lo, hi;
} xtab[] = {
    {"dim", X_DIM, 1, 1},         {"fill", X_FILL, 2, 2},         {"randMat", X_RANDMAT, 2, 3},
    {"augment", X_AUGMENT, 2, 2}, {"ref", X_REF, 1, 1},           {"rowSwap", X_ROWSWAP, 3, 3},
    {"rowAdd", X_ROWADD, 3, 3},   {"mRow", X_MROW, 3, 3},         {"mRowAdd", X_MROWADD, 4, 4},
    {"cumSum", X_CUMSUM, 1, 1},   {"cond", X_COND, 1, 2},         {"setElem", X_SETELEM, 4, 4},
    {"redim", X_REDIM, 3, 3},     {"fourier_bn", X_FOURIERB, 4, 5}, {"bitand", X_BITAND, 2, 16},
    {"bitor", X_BITOR, 2, 16},    {"bitxor", X_BITXOR, 2, 16},    {"bitnot", X_BITNOT, 1, 1},
    {"shl", X_SHL, 1, 2},         {"shr", X_SHR, 1, 2},           {"rotl", X_ROTL, 1, 2},
    {"rotr", X_ROTR, 1, 2},       {"xor", X_XOR, 2, 2},           {"wordsize", X_WORDSIZE, 0, 2},
    {"hex", X_HEX, 1, 1},         {"bin", X_BIN, 1, 1},           {"oct", X_OCT, 1, 1},
    {"convert", X_CONVERT, 2, 2}, {"deltaconvert", X_DCONVERT, 2, 2}, {"usimplify", X_USIMPLIFY, 1, 1},
    {"uvalue", X_UVALUE, 1, 1},   {"uunits", X_UUNITS, 1, 1},
    {"pm", X_PM, 2, 2},           {"nominal", X_NOMINAL, 1, 1},   {"uncert", X_UNCERT, 1, 1},
    {"ucov", X_UCOV, 2, 2},       {"autosimplify", X_AUTOSIMP, 1, 1},
    {"forms", X_FORMS, 1, 3},
};

/* Execute name-dispatched command `op` on n evaluated arguments (the count
 * has already been checked against xtab).  Matrix commands work on xm
 * copies and return new matrices (no in-place update of variables, so
 * rowSwap(A,1,2) must be stored back explicitly).  Errors are thrown as
 * CAS_ERR_DOMAIN with a message naming the command. */
static E *xcmd(int op, uint32_t n, E **a) {
    switch (op) {
    case X_DIM: {
        uint32_t r, c;
        if (is_matrix(a[0], &r, &c)) {
            E *v[2] = {mk_int(r), mk_int(c)};
            return s_list(2, v);
        }
        if (a[0]->t == E_LIST) return mk_int(a[0]->n);
        cas_throw(CAS_ERR_DOMAIN, "dim: expected a list or matrix");
    }
    case X_FILL: {
        xm m = xm_from(a[1]);
        for (size_t i = 0; i < (size_t)m.r * m.c; i++) m.a[i] = a[0];
        return xm_to(m);
    }
    case X_RANDMAT: {
        long r = xint(a[0]), c = xint(a[1]), hi = n > 2 ? xint(a[2]) : 9;
        if (r < 1 || c < 1 || hi < 0 || hi > 1000000000L) cas_throw(CAS_ERR_DOMAIN, "randMat: bad arguments");
        xm m = xm_new((uint32_t)r, (uint32_t)c);
        for (size_t i = 0; i < (size_t)r * (size_t)c; i++) m.a[i] = mk_int(xrand_range(-hi, hi));
        return xm_to(m);
    }
    case X_AUGMENT: {
        uint32_t r1, c1, r2, c2;
        E *A = a[0], *B = a[1];
        if (is_vector(A) && is_vector(B)) return eval_cmd(C_CONCAT, 2, a);
        if (is_vector(B) && is_matrix(A, &r1, &c1) && B->n == r1) { /* column */
            E **col = salloc(sizeof(E *) * r1);
            for (uint32_t i = 0; i < r1; i++) col[i] = s_list(1, &EK(B)[i]);
            B = s_list(r1, col);
        }
        if (!is_matrix(A, &r1, &c1) || !is_matrix(B, &r2, &c2) || r1 != r2)
            cas_throw(CAS_ERR_DOMAIN, "augment: row counts differ");
        xm m = xm_new(r1, c1 + c2), ma = xm_from(A), mb = xm_from(B);
        for (uint32_t i = 0; i < r1; i++) {
            for (uint32_t j = 0; j < c1; j++) XM(m, i, j) = XM(ma, i, j);
            for (uint32_t j = 0; j < c2; j++) XM(m, i, c1 + j) = XM(mb, i, j);
        }
        return xm_to(m);
    }
    case X_REF: return mat_ref(a[0]);
    case X_ROWSWAP: case X_ROWADD: case X_MROW: case X_MROWADD: {
        /* rowSwap(M,i,j)  rowAdd(M,i,j): row j += row i  mRow(k,M,i)  mRowAdd(k,M,i,j): row j += k*row i */
        int scaled = op == X_MROW || op == X_MROWADD;
        xm m = xm_from(a[scaled]);
        uint32_t i = xrow(a[scaled + 1], m), j = op == X_MROW ? i : xrow(a[scaled + 2], m);
        E *k = scaled ? a[0] : K->c1;
        for (uint32_t c = 0; c < m.c; c++) {
            E *x = XM(m, i, c), *y = XM(m, j, c);
            if (op == X_ROWSWAP) XM(m, i, c) = y, XM(m, j, c) = x;
            else if (op == X_MROW) XM(m, i, c) = s_mul2(k, x);
            else XM(m, j, c) = s_add2(y, s_mul2(k, x));
        }
        return xm_to(m);
    }
    case X_CUMSUM: return cumsum(a[0]);
    case X_COND: {
        if (n == 2) return casx_dispatch("cond", n, a); /* cond(A, p): cas/ext/applied */
        /* condition number in the 1-norm, ||A||_1 ||A^-1||_1, computed
         * exactly from the inverse; inf for a singular matrix */
        xm m = xm_from(a[0]);
        if (m.r != m.c) cas_throw(CAS_ERR_DOMAIN, "cond: need a square matrix");
        if (is_zero(mat_det(a[0]))) return K->inf_;
        return s_mul2(norm1(m), norm1(xm_from(mat_inv(a[0]))));
    }
    case X_SETELEM: {
        xm m = xm_from(a[0]);
        uint32_t i = xrow(a[1], m);
        long j = xint(a[2]);
        if (j < 1 || j > (long)m.c) cas_throw(CAS_ERR_DOMAIN, "column index out of range");
        XM(m, i, j - 1) = a[3];
        return xm_to(m);
    }
    case X_REDIM: {
        /* redim(M, r, c): new r x c matrix, the overlapping top-left block
         * copied from M, the rest zero (a non-matrix M gives all zeros) */
        long r = xint(a[1]), c = xint(a[2]);
        if (r < 1 || c < 1 || r > 99 || c > 99) cas_throw(CAS_ERR_DOMAIN, "redim: 1..99 rows and columns");
        xm m = xm_new((uint32_t)r, (uint32_t)c);
        uint32_t r0 = 0, c0 = 0;
        if (is_matrix(a[0], &r0, &c0)) {
            xm o = xm_from(a[0]);
            for (uint32_t i = 0; i < r0 && i < m.r; i++)
                for (uint32_t j = 0; j < c0 && j < m.c; j++) XM(m, i, j) = XM(o, i, j);
        }
        return xm_to(m);
    }
    case X_FOURIERB: return fourier(1, n, a);
    case X_XOR: /* xor of integers is bitwise; of truth values logical; otherwise it stays */
        if (a[0]->t != E_INT || a[1]->t != E_INT) {
            int f0 = ISYM(a[0], S_FALSE), f1 = ISYM(a[1], S_FALSE);
            if ((f0 || ISYM(a[0], S_TRUE)) && (f1 || ISYM(a[1], S_TRUE))) return mk_sym(f0 != f1 ? S_TRUE : S_FALSE);
            if (f0 || f1) return f0 ? a[1] : a[0];
            if (ISYM(a[0], S_TRUE) || ISYM(a[1], S_TRUE)) return s_fn1(F_NOT, ISYM(a[0], S_TRUE) ? a[1] : a[0]);
            return NULL;
        }
        return bitop(2, n, a);
    case X_BITAND: case X_BITOR: case X_BITXOR:
        return bitop(op == X_BITAND ? 0 : op == X_BITOR ? 1 : 2, n, a);
    case X_BITNOT: return bitop(3, n, a);
    case X_SHL: case X_SHR: case X_ROTL: case X_ROTR: {
        E *v[2] = {a[0], n > 1 ? a[1] : K->c1};
        return bitop(4 + op - X_SHL, 2, v);
    }
    case X_WORDSIZE: {
        /* wordsize([bits [, signed]]): sets and/or reports the word used by
         * the bit commands; the setting persists across evaluations */
        if (n > 0) {
            long w = xint(a[0]);
            if (w < 1 || w > 64) cas_throw(CAS_ERR_DOMAIN, "wordsize: 1..64 bits");
            ws_bits = (uint8_t)w;
        }
        if (n > 1) ws_signed = !ISYM(a[1], S_FALSE) && !(is_num(a[1]) && num_is_zero(a[1]));
        E *v[2] = {mk_int(ws_bits), mk_sym(ws_signed ? S_TRUE : S_FALSE)};
        return s_list(2, v);
    }
    case X_HEX: return to_base_str(a[0], 16);
    case X_BIN: return to_base_str(a[0], 2);
    case X_OCT: return to_base_str(a[0], 8);
    case X_AUTOSIMP: return simplify_auto(a[0]); /* simplify.c */
    case X_FORMS: { extern E *forms_cmd(uint32_t n, E **a); return forms_cmd(n, a); } /* forms.c */
    case X_PM: case X_NOMINAL: case X_UNCERT: case X_UCOV: return unc_cmd(op - X_PM, n, a);
    default: return units_cmd(op - X_CONVERT, n, a);
    }
}

/* Called by eval for function calls that are not builtins or user functions;
 * returns NULL if the name is not an extension command.  Throws
 * CAS_ERR_DOMAIN for a known name with the wrong number of arguments. */
static E *ext_cmd(int id, uint32_t n, E **a) {
    const char *nm = sym_name(id);
    for (size_t i = 0; i < sizeof xtab / sizeof xtab[0]; i++)
        if (nm[0] == xtab[i].name[0] && !strcmp(nm, xtab[i].name)) {
            xnargs(n, xtab[i].lo, xtab[i].hi);
            return xcmd(xtab[i].op, n, a);
        }
    extern E *gap_cmd(const char *nm, uint32_t n, E **a);
    return gap_cmd(nm, n, a);
}

/* parse.c: the extension command whose name equals s[0..n) ignoring case
 * (ROWSWAP, Rowswap -> rowSwap), as a symbol id; -1 if none or several. */
int ext_name_ci(const char *s, size_t n) {
    int found = -1;
    for (size_t i = 0; i < sizeof xtab / sizeof xtab[0]; i++) {
        const char *nm = xtab[i].name;
        if (strlen(nm) != n || !name_eq_ci(s, nm, n)) continue;
        if (found >= 0) return -1;
        found = sym_intern(nm, n);
    }
    return found;
}

/* eval.c: builtin-free function calls and and/or/not (NULL: evaluate normally).
 * User symbols (id >= S__COUNT) are looked up as extension command names,
 * builtin ids are only checked for bitwise and/or/not. */
E *ext_dispatch(int id, uint32_t n, E **a) { return id >= S__COUNT ? ext_cmd(id, n, a) : ext_logic(id, n, a); }

/* ----------------------------------------------------- top-level hooks --- */
/* Rewrite the parsed input before evaluation (cas.c): a bare `restart`
 * (a symbol, which would otherwise just evaluate to itself) becomes the
 * command call restart(). */
E *ext_pre(E *e) {
    if (ISYM(e, C_RESTART)) return mk_fn(C_RESTART, 0, NULL); /* bare `restart` */
    return e;
}

/* Post-process the value r of the top-level input e (cas.c), before the
 * approximate-mode conversion and the store into ans:
 *  - unc_post: results with uncertain values linearised (unc.c);
 *  - units_post: dimension checks / unit conversions, constants replaced by
 *    values (forced when the input is a top-level approx / evalf / float);
 *  - approx(e, n) / evalf(e, n) at top level: re-evaluate the result with
 *    n significant digits (evalf_digits; arbitrary precision for constant
 *    subexpressions), otherwise with the current `digits` setting. */
E *ext_post(E *e, E *r) {
    /* approx(...) at top level also replaces constants by their values */
    int ap = e->t == E_FN && (e->s == C_APPROX || e->s == C_EVALF || e->s == C_FLOATS);
    r = unc_post(e, r); /* uncertainties: linear form in the sources (unc.c) */
    r = units_post(r, ap);
    if (ap) {
        long d = K->set.digits;
        if (e->n == 2) num_is_small_int(EK(e)[1], &d);
        r = evalf_digits(r, (int)d);
    }
    return r;
}
