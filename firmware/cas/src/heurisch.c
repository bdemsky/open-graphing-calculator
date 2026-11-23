/*
 * heurisch.c -- "parallel Risch" heuristic integration (after Risch-Norman /
 * Bronstein's heurisch).  The integrand is written as a rational function
 * P/Q in x and a differentially closed set of kernels t_i (sin/cos, exp, ln,
 * radicals, ...).  We posit
 *        F = U/V + sum_j b_j ln(q_j)
 * where q_j are the irreducible factors of Q, V = prod q_j^(e_j - 1), and U
 * is a polynomial in (x, t_1..t_m) with unknown rational coefficients up to
 * some total degree.  Requiring F' = P/Q as a polynomial identity (kernels
 * treated as independent) gives a sparse linear system over Q.
 *
 * Place in the CAS: heurisch() is the last strategy of the integration driver ir() in
 * integ.c, tried only at recursion depth <= 1 because it is the most expensive one.
 *
 * Details.
 *  - Kernels come from e2r() (poly.c), which turns f into num/den polynomials P, Q over a
 *    kernel table (kernel 0 is x).  The derivative of every kernel is converted the same
 *    way, which may add new kernels; these are differentiated in turn, so the table ends
 *    up closed under d/dx (at most MAXV = 8 variables including x).
 *  - Kernels free of x (sqrt(2), parameter symbols, ...) have derivative 0; they do not
 *    appear in the ansatz monomials but are folded into the coefficients, so the system is then over
 *    Q(constants) and the elimination uses rat_normal'ised expressions (g_symbolic).
 *  - D = d/dx + sum_i t_i' d/dt_i is the derivation on Q(x, t).  L = lcm of the
 *    denominators of the t_i' makes DL = L*D map polynomials to polynomials.  Multiplying
 *    F' = P/Q by L V^2 Q gives the polynomial identity
 *        (DL(U) V - U DL(V)) Q + sum_j b_j DL(q_j) V^2 Q/q_j = L P V^2,
 *    linear in the coefficients of U and the b_j.  Comparing coefficients of every
 *    monomial in (x, t) gives the linear system: one row per monomial, one column per
 *    unknown plus the right-hand side.
 *  - Any solution of the formal identity is a genuine antiderivative (the identity
 *    survives substituting the actual kernels), so no verification is needed.  The
 *    converse fails (kernels that are algebraically dependent, an ansatz that is too
 *    small, logarithms of non-factors of Q), which only means the integral is not found.
 *  - The total degree of U is increased from 1 to a heuristic bound (max 8); each degree
 *    is a fresh system with at most MAXU monomials and 1500 rows, solved by dense exact
 *    Gauss-Jordan elimination.  The scratch of a failed degree is released.
 *
 * References: A. C. Norman, P. M. A. Moore, "Implementing the new Risch integration
 * algorithm" (1977); M. Bronstein, "Poor Man's Integrator" (pmint) notes, the basis of
 * SymPy's heurisch.
 *
 * Not reentrant: one call uses the file statics g_isconst, g_kt and g_symbolic.
 */
#include "poly.h"
#include <string.h>

/* MAXV: max variables (x plus kernels); each owns one byte of the 64-bit monomial key.
 * MAXU: max monomials in the ansatz for U; reaching it ends the degree search. */
#define MAXV 8
#define MAXU 160

/* ---------------------------------------------------- monomial index --- */
/*
 * Monomial -> row map: open addressing with linear probing.  key packs the exponent
 * vector (byte v = exponent of variable v; x-free kernels are not part of the key), row is
 * the equation index, 0xffffffff marks an empty slot.  cap must be a power of two.
 */
typedef struct {
    uint64_t *key;
    uint32_t *row;
    uint32_t cap, n;
} mmap;

/* mm_init -- empty map with cap (power of two) slots, allocated in scratch */
static void mm_init(mmap *m, uint32_t cap) {
    m->cap = cap;
    m->n = 0;
    m->key = salloc(sizeof(uint64_t) * cap);
    m->row = salloc(sizeof(uint32_t) * cap);
    for (uint32_t i = 0; i < cap; i++) m->row[i] = 0xffffffffu;
}

/*
 * mm_get -- row index of monomial key k; a new key gets the next row number (rows are
 * numbered in order of discovery).  Fibonacci hashing (multiply by 2^64/phi, take high
 * bits).  Throws CAS_ERR_UNSUPPORTED "heurisch: too many monomials" before the table
 * becomes more than half full.
 */
static uint32_t mm_get(mmap *m, uint64_t k) {
    uint32_t h = (uint32_t)((k * 0x9E3779B97F4A7C15ull) >> 40) & (m->cap - 1);
    while (m->row[h] != 0xffffffffu) {
        if (m->key[h] == k) return m->row[h];
        h = (h + 1) & (m->cap - 1);
    }
    if (m->n * 2 >= m->cap) cas_throw(CAS_ERR_UNSUPPORTED, "heurisch: too many monomials");
    m->key[h] = k;
    m->row[h] = m->n++;
    return m->row[h];
}

/* sparse column: list of (row, coef).  One column per unknown (a coefficient of U or a
 * log coefficient b_j) plus one for the right-hand side.  A row may occur several times
 * in one column; the entries are summed when the dense matrix is assembled. */
typedef struct {
    uint32_t *r;
    E **c;
    uint32_t n, cap;
} col;

/* col_add -- append (r, c), doubling the scratch arrays when full (old arrays are
 * simply abandoned in scratch) */
static void col_add(col *cl, uint32_t r, E *c) {
    if (cl->n == cl->cap) {
        uint32_t nc = cl->cap ? cl->cap * 2 : 16;
        uint32_t *nr = salloc(sizeof(uint32_t) * nc);
        E **ncc = salloc(sizeof(E *) * nc);
        if (cl->n) {
            memcpy(nr, cl->r, sizeof(uint32_t) * cl->n);
            memcpy(ncc, cl->c, sizeof(E *) * cl->n);
        }
        cl->r = nr;
        cl->c = ncc;
        cl->cap = nc;
    }
    cl->r[cl->n] = r;
    cl->c[cl->n] = c;
    cl->n++;
}

/* constant kernels (free of x) go into the coefficient, not the monomial key.
 * g_isconst[v]: kernel v is free of x.  g_kt: the kernel table of the current call. */
static int g_isconst[MAXV];
static ktab *g_kt;

/*
 * flatten_c -- walk the recursive dense polynomial p and emit its terms into column out.
 * key accumulates the packed exponents of the non-constant variables seen so far; cf
 * accumulates powers of x-free kernels, which go into the coefficient instead of the
 * key.  Each leaf adds (mm_get(key), cf * leaf) to out.  Throws CAS_ERR_UNSUPPORTED for a
 * variable index >= MAXV or an exponent > 255 (it would overflow its key byte).
 */
static void flatten_c(P *p, uint64_t key, E *cf, mmap *mm, col *out) {
    stack_check();
    if (p_is_zero(p)) return;
    if (p->v < 0) {
        col_add(out, mm_get(mm, key), is_one(cf) ? p->c : s_mul2(p->c, cf));
        return;
    }
    if (p->v >= MAXV) cas_throw(CAS_ERR_UNSUPPORTED, "heurisch: too many kernels");
    for (uint32_t i = 0; i < p->n; i++) {
        if (i > 255) cas_throw(CAS_ERR_UNSUPPORTED, "heurisch: degree");
        if (g_isconst[p->v])
            flatten_c(p->k[i], key, i ? s_mul2(cf, s_pow(g_kt->k[p->v], mk_int(i))) : cf, mm, out);
        else
            flatten_c(p->k[i], key + ((uint64_t)i << (8 * p->v)), cf, mm, out);
    }
}

/* flatten -- flatten_c with coefficient multiplier 1 */
static void flatten(P *p, uint64_t key, mmap *mm, col *out) { flatten_c(p, key, K->c1, mm, out); }

/* tdeg -- total degree of p in all its variables, -1 for the zero polynomial */
static long tdeg(P *p) {
    stack_check();
    if (p_is_zero(p)) return -1;
    if (p->v < 0) return 0;
    long m = 0;
    for (uint32_t i = 0; i < p->n; i++) {
        long d = tdeg(p->k[i]);
        if (d >= 0 && d + (long)i > m) m = d + (long)i;
    }
    return m;
}

/*
 * DL -- derivation scaled by L: DL(p) = L dp/dx + sum_i Lq_i dp/dt_i, where
 * Lq_i = L * t_i' is a polynomial.  Variables absent from p are skipped.  For p a
 * polynomial, DL(p) = L * dp/dx is again a polynomial.
 */
static P *DL(P *p, int nv, P *L, P **Lq) {
    P *r = p_mul(L, p_diff(p, 0));
    for (int i = 1; i < nv; i++) {
        if (!p_has_var(p, i)) continue;
        r = p_add(r, p_mul(Lq[i], p_diff(p, i)));
    }
    return r;
}

/*
 * gen_monos -- enumerate the monomials of total degree <= left in variables var..nv-1
 * (x-free kernels skipped), each multiplied by cur, appending to out[*n].  Stops when
 * cap entries exist; the caller treats *n == cap as "too many".
 */
static void gen_monos(int nv, int var, long left, P *cur, P **out, uint32_t *n, uint32_t cap) {
    stack_check();
    if (*n >= cap) return;
    if (var == nv) {
        out[(*n)++] = cur;
        return;
    }
    if (g_isconst[var]) {
        gen_monos(nv, var + 1, left, cur, out, n, cap);
        return;
    }
    P *pw = p_const(K->c1);
    for (long e = 0; e <= left && *n < cap; e++) {
        gen_monos(nv, var + 1, left - e, p_mul(cur, pw), out, n, cap);
        pw = p_mul(pw, p_var(var));
    }
}

static int g_symbolic; /* entries are expressions (constant kernels present) */

/*
 * Field operations for the elimination: plain rational arithmetic when all entries are
 * numbers, otherwise expression arithmetic kept in rational normal form (ez) so that zero
 * tests over Q(constants) are reliable.
 */
static E *ez(E *e) { return is_exactnum(e) ? e : rat_normal(e); }
static int e_is_zero(E *e) { return is_zero(g_symbolic ? ez(e) : e); }
static E *e_mul(E *a, E *b) { return g_symbolic ? ez(s_mul2(a, b)) : num_mul(a, b); }
static E *e_sub(E *a, E *b) { return g_symbolic ? ez(s_sub(a, b)) : num_sub(a, b); }
static E *e_inv(E *a) { return g_symbolic ? ez(s_inv(a)) : num_inv(a); }

/*
 * solve_dense -- exact Gauss-Jordan elimination on the dense augmented matrix A
 * (rows x (nu+1), last column = right-hand side); rows are swapped in place.  The pivot
 * is the first nonzero entry of the column (exact arithmetic needs no numerical pivoting).
 * Returns 0 if the system is inconsistent, else 1 with a particular solution in
 * sol[0..nu-1] (free unknowns set to 0).  O(rows * nu * rank) field operations; polls for
 * interrupts once per column.
 */
static int solve_dense(E ***A, uint32_t rows, uint32_t nu, E **sol) {
    uint32_t r = 0;
    int *pivcol = salloc(sizeof(int) * (rows + 1));
    for (uint32_t c = 0; c < nu && r < rows; c++) {
        poll_intr();
        uint32_t p = r;
        while (p < rows && e_is_zero(A[p][c])) p++;
        if (p == rows) continue;
        E **t = A[p]; A[p] = A[r]; A[r] = t;
        E *iv = e_inv(A[r][c]);
        for (uint32_t j = c; j <= nu; j++)
            if (!is_zero(A[r][j])) A[r][j] = e_mul(A[r][j], iv);
        for (uint32_t i = 0; i < rows; i++) {
            if (i == r || e_is_zero(A[i][c])) continue;
            E *f = A[i][c];
            for (uint32_t j = c; j <= nu; j++)
                if (!is_zero(A[r][j])) A[i][j] = e_sub(A[i][j], e_mul(f, A[r][j]));
        }
        pivcol[r] = (int)c;
        r++;
    }
    for (uint32_t i = r; i < rows; i++)
        if (!e_is_zero(A[i][nu])) return 0;
    for (uint32_t j = 0; j < nu; j++) sol[j] = K->c0;
    for (uint32_t i = 0; i < r; i++) sol[pivcol[i]] = A[i][nu];
    return 1;
}

/* exact_coeffs -- are all numeric leaves of p exact rationals? */
static int exact_coeffs(P *p) {
    stack_check();
    if (p->v < 0) return is_exactnum(p->c);
    for (uint32_t i = 0; i < p->n; i++)
        if (!exact_coeffs(p->k[i])) return 0;
    return 1;
}

/*
 * heurisch -- integrate f with respect to the symbol x by the ansatz described above.
 * Returns the antiderivative (scratch) or NULL if f contains floats, needs more than MAXV
 * variables, the kernel-derivative denominators do not combine exactly, or no solution
 * exists up to the degree bound.  Besides memory/interrupt errors it can throw
 * CAS_ERR_UNSUPPORTED for oversized systems (monomial table full, exponent > 255); these
 * are not caught here or in ir().  Overwrites the file statics g_isconst, g_kt and
 * g_symbolic.
 */
E *heurisch(E *f, E *x) {
    ktab kt;
    kt_init(&kt);
    kt_index(&kt, x);
    P *Pn, *Qd;
    e2r(f, &kt, &Pn, &Qd);
    if (kt.has_float) return NULL;
    /* kernel derivatives t_i' = dn[i]/dd[i]; kt.n grows while new kernels appear in the
     * derivatives, so the loop visits them too (differential closure) */
    P *dn[MAXV], *dd[MAXV];
    for (uint32_t i = 1; i < kt.n; i++) {
        if (kt.n > MAXV) return NULL;
        E *k = kt.k[i];
        E *dk = diff(k, x);
        e2r(dk, &kt, &dn[i], &dd[i]);
        if (kt.has_float) return NULL;
    }
    int nv = (int)kt.n;
    if (nv > MAXV) return NULL;
    /* x-free kernels have derivative 0 and become part of the coefficients */
    g_symbolic = 0;
    g_kt = &kt;
    for (int i = 0; i < MAXV; i++) g_isconst[i] = 0;
    for (int i = 1; i < nv; i++)
        if (freeof(kt.k[i], x)) {
            g_isconst[i] = 1;
            g_symbolic = 1;
        }
    if (!exact_coeffs(Pn) || !exact_coeffs(Qd)) return NULL;
    /* L = lcm of derivative denominators */
    P *L = p_const(K->c1);
    for (int i = 1; i < nv; i++) {
        P *g = p_gcd(L, dd[i]);
        P *q = p_divexact(dd[i], g);
        if (!q) return NULL;
        L = p_mul(L, q);
    }
    P *Lq[MAXV];
    Lq[0] = L;
    for (int i = 1; i < nv; i++) {
        P *q = p_divexact(L, dd[i]);
        if (!q) return NULL;
        Lq[i] = p_mul(q, dn[i]);
    }
    /* factor the denominator */
    P *qf[32];
    uint32_t qm[32];
    uint32_t nq = p_is_const(Qd) ? 0 : p_factor(Qd, qf, qm, 32);
    /* V = prod q_j^(e_j - 1): denominator of the rational part U/V */
    P *V = p_const(K->c1);
    for (uint32_t j = 0; j < nq; j++)
        if (qm[j] > 1) V = p_mul(V, p_pow(qf[j], qm[j] - 1));
    P *V2 = p_mul(V, V);
    P *DV = DL(V, nv, L, Lq);
    P *rhs = p_mul(p_mul(L, Pn), V2);
    /* log-term contributions (independent of the degree) */
    P *logc[32];
    for (uint32_t j = 0; j < nq; j++) {
        P *qq = p_divexact(Qd, qf[j]);
        if (!qq) return NULL;
        logc[j] = p_mul(p_mul(DL(qf[j], nv, L, Lq), V2), qq);
    }
    /* heuristic bound for the total degree of U, clamped to [1, 8] */
    long dmax = tdeg(Pn) - tdeg(Qd) + tdeg(V) + 2;
    if (dmax < 1) dmax = 1;
    if (dmax > 8) dmax = 8;
    for (long dU = 1; dU <= dmax; dU++) {
        poll_intr();
        char *mark = smark();
        P *monos[MAXU];
        uint32_t nm = 0;
        gen_monos(nv, 0, dU, p_const(K->c1), monos, &nm, MAXU);
        if (nm >= MAXU) break;
        uint32_t nu = nm + nq;
        mmap mm;
        mm_init(&mm, 8192);
        col *cols = salloc(sizeof(col) * (nu + 1));
        memset(cols, 0, sizeof(col) * (nu + 1));
        /* column of monomial m_i of U: (DL(m_i) V - m_i DL(V)) Q; then the log columns
         * and the right-hand side L P V^2 */
        for (uint32_t i = 0; i < nm; i++) {
            P *c = p_mul(p_sub(p_mul(DL(monos[i], nv, L, Lq), V), p_mul(monos[i], DV)), Qd);
            flatten(c, 0, &mm, &cols[i]);
        }
        for (uint32_t j = 0; j < nq; j++) flatten(logc[j], 0, &mm, &cols[nm + j]);
        flatten(rhs, 0, &mm, &cols[nu]);
        uint32_t rows = mm.n;
        if (rows > 1500) {
            srelease(mark);
            break;
        }
        /* scatter the sparse columns into a dense matrix (duplicate rows are summed) */
        E ***A = salloc(sizeof(E **) * (rows + 1));
        for (uint32_t r = 0; r < rows; r++) {
            A[r] = salloc(sizeof(E *) * (nu + 1));
            for (uint32_t c = 0; c <= nu; c++) A[r][c] = K->c0;
        }
        for (uint32_t c = 0; c <= nu; c++)
            for (uint32_t i = 0; i < cols[c].n; i++)
                A[cols[c].r[i]][c] = g_symbolic ? s_add2(A[cols[c].r[i]][c], cols[c].c[i])
                                                : num_add(A[cols[c].r[i]][c], cols[c].c[i]);
        E **sol = salloc(sizeof(E *) * (nu + 1));
        /* F = (sum sol_i m_i)/V + sum_j b_j ln(q_j) */
        if (solve_dense(A, rows, nu, sol)) {
            E *Ue = K->c0;
            for (uint32_t i = 0; i < nm; i++)
                if (!is_zero(sol[i])) Ue = s_add2(Ue, s_mul2(sol[i], p2e(monos[i], &kt)));
            E *res = s_div(Ue, p2e(V, &kt));
            for (uint32_t j = 0; j < nq; j++)
                if (!is_zero(sol[nm + j])) res = s_add2(res, s_mul2(sol[nm + j], s_ln(p2e(qf[j], &kt))));
            return res;
        }
        srelease(mark);
    }
    return NULL;
}
