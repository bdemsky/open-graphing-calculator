/*
 * linalg.c -- exact linear algebra on matrices represented as lists of rows.
 * Entries are simplified with the rational normal form, so symbolic
 * matrices work (slowly); floating-point matrices use partial pivoting.
 *
 * Representation.  There is no matrix node type: a matrix is an E_LIST of
 * r >= 1 rows, each an E_LIST of the same length c >= 1 whose elements are
 * not lists (is_matrix); a vector is a non-empty list of non-lists
 * (is_vector).  Internally the algorithms copy the entries into a dense
 * row-major array of E pointers (struct mat, macro M) in scratch, work on
 * that, and convert back with m_to.  Entries are ordinary expressions, so
 * the same code serves integer, rational, float and symbolic matrices.
 *
 * Algorithms:
 *   rref_m      Gauss-Jordan elimination to reduced row echelon form; used
 *               for rref, rank, inv (on [A | I]), ker, linsolve, and det of
 *               float matrices.  Exact entries: first nonzero pivot; float
 *               matrices: partial pivoting (largest |entry|).
 *   mat_det     Bareiss's fraction-free elimination (E. H. Bareiss, "Sylvester's
 *               identity and multistep integer-preserving Gaussian
 *               elimination", Math. Comp. 22 (1968); Geddes/Czapor/Labahn
 *               ch. 9): every division is exact, so integer matrices stay
 *               integer and symbolic entries stay polynomial, avoiding the
 *               growth of nested fractions.  O(n^3) ring operations.
 *   mat_charpoly det(x I - A) by Bareiss.
 *   mat_pow_list binary powering (repeated squaring), inverse for k < 0.
 *   mat_lu      Doolittle LU with row pivoting, P A = L U.
 *   mat_qr      classical Gram-Schmidt with exact square-root norms.
 *   mat_cholesky the Cholesky-Banachiewicz recurrence, A = L L^T.
 *   linsolve    linear systems via the augmented matrix and rref.
 * Eigenvalues/eigenvectors (cmds.c) are built on mat_charpoly + solve and
 * mat_kernel.
 *
 * Simplification: after each elimination step the entries are put into
 * rational normal form (nz -> rat_normal; plain numbers are left alone), so
 * zero tests (is_zero) on symbolic entries are reliable as long as zero
 * equivalence is decided by rat_normal (no trig or radical identities).
 * QR and Cholesky need square roots and use simplify_full instead.
 *
 * Callers: matrix commands in cmds.c / xcmds.c (det inv rref rank ker
 * charpoly eigen* lu qr cholesky ...), matrix arithmetic in simp.c and cmds.c
 * (mat_mul_lists, mat_pow_list), solve.c (linsolve for linear systems).
 * Errors: dimension mismatches, singular matrices etc. throw CAS_ERR_DOMAIN;
 * long loops poll for interrupts.  All results live in scratch.
 */
#include "cas_int.h"
#include <math.h>
#include <string.h>

/* 1 if e is a non-empty list none of whose elements is a list. */
int is_vector(E *e) {
    if (e->t != E_LIST || e->n == 0) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (EK(e)[i]->t == E_LIST) return 0;
    return 1;
}

/* 1 if e is a non-empty list of equal-length vectors; stores the
 * dimensions in *rows / *cols (either may be NULL). */
int is_matrix(E *e, uint32_t *rows, uint32_t *cols) {
    if (e->t != E_LIST || e->n == 0) return 0;
    uint32_t c = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        E *r = EK(e)[i];
        if (!is_vector(r)) return 0;
        if (i == 0) c = r->n;
        else if (r->n != c) return 0;
    }
    if (rows) *rows = e->n;
    if (cols) *cols = c;
    return 1;
}

/* Dense working matrix: r x c entries, row-major in scratch.  Passed by
 * value (the struct is a small handle; the entry array is shared). */
typedef struct {
    uint32_t r, c;
    E **a;
} mat;

/* entry (i, j) of a mat, as an lvalue */
#define M(m, i, j) ((m).a[(size_t)(i) * (m).c + (j)])

/* New r x c working matrix in scratch, all entries 0. */
static mat m_alloc(uint32_t r, uint32_t c) {
    mat m;
    m.r = r;
    m.c = c;
    m.a = salloc(sizeof(E *) * (size_t)r * c + 1);
    for (size_t i = 0; i < (size_t)r * c; i++) m.a[i] = K->c0;
    return m;
}

/* Copy a matrix expression into a working matrix (the entries themselves
 * are shared, not copied).  Throws CAS_ERR_DOMAIN if e is not a matrix. */
static mat m_from(E *e) {
    uint32_t r, c;
    if (!is_matrix(e, &r, &c)) cas_throw(CAS_ERR_DOMAIN, "not a matrix");
    mat m = m_alloc(r, c);
    for (uint32_t i = 0; i < r; i++)
        for (uint32_t j = 0; j < c; j++) M(m, i, j) = EK(EK(e)[i])[j];
    return m;
}

/* Convert a working matrix back into a list-of-rows expression. */
static E *m_to(mat m) {
    E **rows = salloc(sizeof(E *) * m.r);
    for (uint32_t i = 0; i < m.r; i++) rows[i] = s_list(m.c, &M(m, i, 0));
    return s_list(m.r, rows);
}

/* 1 if any entry contains a float (selects partial pivoting). */
static int m_has_float(mat m) {
    for (size_t i = 0; i < (size_t)m.r * m.c; i++)
        if (has_float(m.a[i])) return 1;
    return 0;
}

/* Pivot tolerance for pick_pivot: -1 for an exact matrix, else 1e-13 times
 * the largest |entry| (relative, so tiny-scaled matrices keep their rank). */
static double m_tol(mat m) {
    if (!m_has_float(m)) return -1;
    double mx = 0, v;
    for (size_t i = 0; i < (size_t)m.r * m.c; i++)
        if (to_double(m.a[i], &v) && fabs(v) > mx) mx = fabs(v);
    return 1e-13 * mx;
}

/* 1 if all entries are exact rationals (Bareiss then needs no rat_normal). */
static int m_all_rational(mat m) {
    for (size_t i = 0; i < (size_t)m.r * m.c; i++)
        if (!is_exactnum(m.a[i])) return 0;
    return 1;
}

/* Normalise an entry: rational normal form, so a symbolic zero becomes 0. */
static E *nz(E *e) { return is_exactnum(e) ? e : rat_normal(e); }

/* Transpose; a plain vector [a,b,c] becomes the column [[a],[b],[c]]. */
E *mat_transpose(E *e) {
    if (is_vector(e)) {
        E **rows = salloc(sizeof(E *) * e->n);
        for (uint32_t i = 0; i < e->n; i++) rows[i] = s_list(1, &EK(e)[i]);
        return s_list(e->n, rows);
    }
    mat m = m_from(e), t = m_alloc(m.c, m.r);
    for (uint32_t i = 0; i < m.r; i++)
        for (uint32_t j = 0; j < m.c; j++) M(t, j, i) = M(m, i, j);
    return m_to(t);
}

/* n x n identity matrix, 1 <= n <= 1000 (CAS_ERR_DOMAIN otherwise). */
E *mat_identity(long n) {
    if (n < 1 || n > 1000) cas_throw(CAS_ERR_DOMAIN, "bad matrix size");
    mat m = m_alloc((uint32_t)n, (uint32_t)n);
    for (long i = 0; i < n; i++) M(m, i, i) = K->c1;
    return m_to(m);
}

/* sum a[i]*b[i], i < n, built as one n-ary sum (simplified once). */
static E *dot(uint32_t n, E **a, E **b) {
    E **t = salloc(sizeof(E *) * (n + 1));
    for (uint32_t i = 0; i < n; i++) t[i] = s_mul2(a[i], b[i]);
    return s_add(n, t);
}

/*
 * Product of two lists, following the shapes:
 *   matrix * matrix  -> matrix (ar x ac times br x bc, ac == br)
 *   matrix * vector  -> vector (the vector is a column)
 *   vector * matrix  -> vector (the vector is a row)
 *   vector * vector  -> scalar dot product (no conjugation)
 * Entries are not normalised (s_* simplification only).  Throws
 * CAS_ERR_DOMAIN on a dimension mismatch or for other list shapes.
 * Called for the product of two lists (simp.c, cmds.c) and by mat_pow_list.
 */
E *mat_mul_lists(E *a, E *b) {
    uint32_t ar, ac, br, bc;
    int am = is_matrix(a, &ar, &ac), bm = is_matrix(b, &br, &bc);
    if (am && bm) {
        if (ac != br) cas_throw(CAS_ERR_DOMAIN, "matrix dimensions do not match");
        mat A = m_from(a), B = m_from(b), C = m_alloc(ar, bc);
        E **col = salloc(sizeof(E *) * br);
        for (uint32_t j = 0; j < bc; j++) {
            for (uint32_t k = 0; k < br; k++) col[k] = M(B, k, j);
            for (uint32_t i = 0; i < ar; i++) M(C, i, j) = dot(ac, &M(A, i, 0), col);
        }
        return m_to(C);
    }
    if (am && is_vector(b)) {
        if (ac != b->n) cas_throw(CAS_ERR_DOMAIN, "matrix dimensions do not match");
        mat A = m_from(a);
        E **r = salloc(sizeof(E *) * ar);
        for (uint32_t i = 0; i < ar; i++) r[i] = dot(ac, &M(A, i, 0), EK(b));
        return s_list(ar, r);
    }
    if (is_vector(a) && bm) {
        if (a->n != br) cas_throw(CAS_ERR_DOMAIN, "matrix dimensions do not match");
        mat B = m_from(b);
        E **r = salloc(sizeof(E *) * bc);
        E **col = salloc(sizeof(E *) * br);
        for (uint32_t j = 0; j < bc; j++) {
            for (uint32_t k = 0; k < br; k++) col[k] = M(B, k, j);
            r[j] = dot(br, EK(a), col);
        }
        return s_list(bc, r);
    }
    if (is_vector(a) && is_vector(b)) {
        if (a->n != b->n) cas_throw(CAS_ERR_DOMAIN, "vector sizes do not match");
        return dot(a->n, EK(a), EK(b));
    }
    cas_throw(CAS_ERR_DOMAIN, "invalid list product");
}

/* --- Gaussian elimination --- */
/*
 * Choose the pivot row for column col among rows from..r-1, or -1 if the
 * column is zero there.
 *   exact (fl = 0): the first entry that is not syntactically zero (entries
 *     are in rational normal form, so this is a true zero test, except for
 *     trig entries, which are also tested with simplify_full);
 *   float (tol >= 0, from m_tol): partial pivoting, the entry of largest
 *     absolute value, for numerical stability; numeric entries not above tol
 *     (relative to the matrix scale) count as zero.  If no numeric entry
 *     qualifies, the first entry that does not evaluate to a real double
 *     (symbolic or complex) and is not zero is chosen.
 */
int has_trig(E *e);
static int pick_pivot(mat m, uint32_t col, uint32_t from, double tol) {
    int best = -1, sym = -1;
    double bv = tol;
    for (uint32_t i = from; i < m.r; i++) {
        E *x = M(m, i, col);
        if (tol >= 0) {
            double v;
            if (to_double(x, &v)) {
                if (fabs(v) > bv) {
                    bv = fabs(v);
                    best = (int)i;
                }
            } else if (sym < 0 && !is_zero(x))
                sym = (int)i;
        } else if (!is_zero(x)) {
            /* trig identities hide zeros from rat_normal (2 sin t cos t - sin 2t) */
            if (!has_trig(x) || !is_zero(simplify_full(x))) return (int)i;
            M(m, i, col) = K->c0;
        }
    }
    return best >= 0 ? best : sym;
}

/* reduced row echelon form in place; returns rank; det accumulates when non-NULL */
/*
 * Gauss-Jordan elimination on the first ncols columns (the remaining columns
 * of m, e.g. the right-hand side or the identity block of [A | I], are
 * carried along).  For each column: find a pivot (pick_pivot), swap it into
 * the current row, scale the row so the pivot is 1, and eliminate the column
 * from all other rows (above and below), normalising every new entry with
 * nz.  Returns the rank; afterwards rows 0..rank-1 are the non-zero rows of
 * the RREF.
 * det (optional): the product of the pivots with a sign flip per row swap,
 * i.e. the determinant of the leading ncols x ncols block, or 0 if a column
 * had no pivot.  Only meaningful for square input (mat_det float path).
 * Cost: O(r * c * min(r, ncols)) entry operations, each a rat_normal for
 * symbolic entries.  Polls for interrupts once per column.
 */
static uint32_t rref_m(mat m, uint32_t ncols, E **det) {
    double tol = m_tol(m);
    uint32_t row = 0;
    E *d = K->c1;
    for (size_t i = 0; i < (size_t)m.r * m.c; i++) m.a[i] = nz(m.a[i]);
    for (uint32_t col = 0; col < ncols && row < m.r; col++) {
        poll_intr();
        int p = pick_pivot(m, col, row, tol);
        if (p < 0) {
            d = K->c0;
            continue;
        }
        if ((uint32_t)p != row) {
            for (uint32_t j = 0; j < m.c; j++) {
                E *t = M(m, row, j);
                M(m, row, j) = M(m, (uint32_t)p, j);
                M(m, (uint32_t)p, j) = t;
            }
            d = s_neg(d);
        }
        E *pv = M(m, row, col);
        d = s_mul2(d, pv);
        E *ipv = s_inv(pv);
        for (uint32_t j = 0; j < m.c; j++) M(m, row, j) = nz(s_mul2(M(m, row, j), ipv));
        for (uint32_t i = 0; i < m.r; i++) {
            if (i == row) continue;
            E *f = M(m, i, col);
            if (is_zero(f)) continue;
            for (uint32_t j = 0; j < m.c; j++)
                M(m, i, j) = nz(s_sub(M(m, i, j), s_mul2(f, M(m, row, j))));
        }
        row++;
    }
    if (row < m.r || row < ncols) d = K->c0;
    if (det) *det = nz(d);
    return row;
}

/* rref command (and rank via *rank, which may be NULL). */
E *mat_rref(E *e, long *rank) {
    mat m = m_from(e);
    uint32_t r = rref_m(m, m.c, NULL);
    if (rank) *rank = (long)r;
    return m_to(m);
}

/* Bareiss fraction-free determinant */
/*
 * Determinant.  Float matrices go through rref_m (partial pivoting).
 * Otherwise Bareiss's one-step fraction-free elimination:
 *   for k = 0 .. n-2, for i, j > k:
 *     a[i][j] = (a[i][j] a[k][k] - a[i][k] a[k][j]) / p,   p = previous pivot
 *   (p = 1 initially, then a[k-1][k-1]).
 * By Sylvester's identity each division is exact and a[i][j] after step k is
 * the (k+2) x (k+2) leading minor bordered by row i and column j, so entries
 * stay as small as minors (no fraction growth); the determinant is the last
 * entry a[n-1][n-1].  A zero pivot is replaced by swapping in a lower row
 * with a nonzero entry in column k (flipping the sign); if there is none the
 * determinant is 0.
 * For all-rational matrices the s_* arithmetic is exact; otherwise each new
 * entry is put into rational normal form and the result is expanded.
 */
E *mat_det(E *e) {
    mat m = m_from(e);
    if (m.r != m.c) cas_throw(CAS_ERR_DOMAIN, "det: matrix must be square");
    uint32_t n = m.r;
    if (m_has_float(m)) {
        E *d;
        rref_m(m, n, &d);
        return d;
    }
    int rat = m_all_rational(m);
    E *prev = K->c1;
    int sign = 1;
    for (uint32_t k = 0; k + 1 < n; k++) {
        poll_intr();
        if (is_zero(M(m, k, k))) {
            uint32_t p;
            for (p = k + 1; p < n; p++)
                if (!is_zero(nz(M(m, p, k)))) break;
            if (p == n) return K->c0;
            for (uint32_t j = 0; j < n; j++) {
                E *t = M(m, k, j);
                M(m, k, j) = M(m, p, j);
                M(m, p, j) = t;
            }
            sign = -sign;
        }
        for (uint32_t i = k + 1; i < n; i++)
            for (uint32_t j = k + 1; j < n; j++) {
                E *v = s_sub(s_mul2(M(m, i, j), M(m, k, k)), s_mul2(M(m, i, k), M(m, k, j)));
                v = s_div(v, prev);
                M(m, i, j) = rat ? v : nz(v);
            }
        prev = M(m, k, k);
    }
    E *d = M(m, n - 1, n - 1);
    if (sign < 0) d = s_neg(d);
    return rat ? d : expand(nz(d));
}

/* Inverse by Gauss-Jordan on the augmented matrix [A | I]: after rref the
 * right block is A^-1.  Throws CAS_ERR_DOMAIN for non-square or singular
 * matrices. */
E *mat_inv(E *e) {
    mat m = m_from(e);
    if (m.r != m.c) cas_throw(CAS_ERR_DOMAIN, "inverse: matrix must be square");
    uint32_t n = m.r;
    mat a = m_alloc(n, 2 * n);
    for (uint32_t i = 0; i < n; i++) {
        for (uint32_t j = 0; j < n; j++) M(a, i, j) = M(m, i, j);
        M(a, i, n + i) = K->c1;
    }
    uint32_t r = rref_m(a, n, NULL);
    if (r < n) cas_throw(CAS_ERR_DOMAIN, "matrix is singular");
    mat inv = m_alloc(n, n);
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t j = 0; j < n; j++) M(inv, i, j) = M(a, i, n + j);
    return m_to(inv);
}

/*
 * b^k.  For a square matrix and an integer k (|k| < 2^31): binary powering
 * (right-to-left square and multiply, Knuth TAOCP vol. 2, 4.6.3), O(log k)
 * matrix products; k < 0 inverts first; k = 0 gives the identity.  Any other
 * list (vector, non-square matrix) is raised to the power elementwise.
 * Throws CAS_ERR_DOMAIN for a non-integer exponent of a square matrix.
 */
E *mat_pow_list(E *b, E *k) {
    uint32_t r, c;
    if (!is_matrix(b, &r, &c) || r != c) {
        /* elementwise power for plain lists */
        E **v = salloc(sizeof(E *) * (b->n + 1));
        for (uint32_t i = 0; i < b->n; i++) v[i] = s_pow(EK(b)[i], k);
        return s_list(b->n, v);
    }
    long kv;
    if (!num_is_small_int(k, &kv)) cas_throw(CAS_ERR_DOMAIN, "matrix power too large");
    if (kv < 0) {
        b = mat_inv(b);
        kv = -kv;
    }
    E *res = mat_identity(r), *base = b;
    while (kv) {
        if (kv & 1) res = mat_mul_lists(res, base);
        kv >>= 1;
        if (kv) base = mat_mul_lists(base, base);
    }
    return res;
}

/* Characteristic polynomial det(x I - A) in the variable x, expanded
 * (monic of degree n).  Computed with mat_det, so Bareiss for exact
 * matrices; a float matrix takes mat_det's rref path, where the symbolic
 * diagonal entries x - a_ii serve as pivots (see pick_pivot). */
E *mat_charpoly(E *e, E *x) {
    mat m = m_from(e);
    if (m.r != m.c) cas_throw(CAS_ERR_DOMAIN, "charpoly: matrix must be square");
    uint32_t n = m.r;
    mat a = m_alloc(n, n);
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t j = 0; j < n; j++) M(a, i, j) = i == j ? s_sub(x, M(m, i, j)) : s_neg(M(m, i, j));
    return expand(mat_det(m_to(a)));
}

/* nullspace basis (list of vectors) */
/*
 * From the RREF: the pivot column of each non-zero row is its first nonzero
 * entry; every other (free) column f gives one basis vector with
 * v[f] = 1, v[pivot of row i] = -R[i][f], all other components 0.  Returns
 * [] for a matrix of full column rank.  Used by ker and eigenvects.
 */
E *mat_kernel(E *e) {
    mat m = m_from(e);
    uint32_t rank = rref_m(m, m.c, NULL);
    int *pivcol = salloc(sizeof(int) * (m.r + 1));
    uint32_t pr = 0;
    for (uint32_t i = 0; i < rank; i++) {
        uint32_t j = 0;
        while (j < m.c && is_zero(M(m, i, j))) j++;
        pivcol[pr++] = (int)j;
    }
    E **basis = salloc(sizeof(E *) * (m.c + 1));
    uint32_t nb = 0;
    for (uint32_t f = 0; f < m.c; f++) {
        int isp = 0;
        for (uint32_t i = 0; i < pr; i++)
            if (pivcol[i] == (int)f) isp = 1;
        if (isp) continue;
        E **v = salloc(sizeof(E *) * m.c);
        for (uint32_t j = 0; j < m.c; j++) v[j] = K->c0;
        v[f] = K->c1;
        for (uint32_t i = 0; i < pr; i++) v[pivcol[i]] = nz(s_neg(M(m, i, f)));
        basis[nb++] = s_list(m.c, v);
    }
    return s_list(nb, basis);
}

/* a + b or a - b (the simplifier adds lists elementwise). */
E *mat_add(E *a, E *b, int sub) { return sub ? s_sub(a, b) : s_add2(a, b); }

/* m^k for a machine integer k. */
E *mat_pow(E *m, long k) { return mat_pow_list(m, mk_int(k)); }

/* linear system: eqs list of equations/expressions, vars list of symbols.
 * Returns a list of equations var=value, or [] if inconsistent. */
/*
 * Details.  Each equation lhs = rhs becomes q = lhs - rhs (expanded).  The
 * coefficient of variable j is dq/dvar_j; the system is linear iff every
 * such derivative is free of all the variables, otherwise NULL is returned
 * ("not linear", the caller tries other methods).  The constant term is
 * q with all variables set to 0, moved to the right-hand side.  The
 * augmented matrix [A | b] is reduced with rref_m on the first nv columns:
 *   - a nonzero right-hand side in a zero row means inconsistency -> [];
 *   - otherwise each pivot row gives  pivot var = b_i - sum (free vars),
 *     so for an underdetermined system the free variables appear as
 *     parameters on the right-hand sides and get no equation of their own.
 * At most 64 unknowns (CAS_ERR_UNSUPPORTED beyond).  Coefficients may be
 * symbolic (parameters), as long as zero tests via rat_normal suffice.
 */
E *linsolve(E *eqs, E *vars) {
    uint32_t ne = eqs->t == E_LIST ? eqs->n : 1, nv = vars->t == E_LIST ? vars->n : 1;
    E **ev = eqs->t == E_LIST ? EK(eqs) : &eqs;
    E **vv = vars->t == E_LIST ? EK(vars) : &vars;
    mat a = m_alloc(ne, nv + 1);
    E *zero_sub[64];
    if (nv > 64) cas_throw(CAS_ERR_UNSUPPORTED, "too many unknowns");
    for (uint32_t j = 0; j < nv; j++) zero_sub[j] = K->c0;
    for (uint32_t i = 0; i < ne; i++) {
        E *q = ev[i];
        if (IFN(q, F_EQ)) q = s_sub(K0(q), K1(q));
        q = expand(q);
        for (uint32_t j = 0; j < nv; j++) {
            E *c = diff(q, vv[j]);
            if (!freeof(c, vv[j])) return NULL; /* nonlinear */
            for (uint32_t k = 0; k < nv; k++)
                if (!freeof(c, vv[k])) return NULL;
            M(a, i, j) = c;
        }
        M(a, i, nv) = s_neg(subst_many(q, nv, vv, zero_sub));
    }
    uint32_t rank = rref_m(a, nv, NULL);
    /* inconsistent? */
    for (uint32_t i = rank; i < ne; i++)
        if (!is_zero(M(a, i, nv))) return s_list(0, NULL);
    E **sol = salloc(sizeof(E *) * (nv + 1));
    uint32_t ns = 0;
    for (uint32_t i = 0; i < rank; i++) {
        uint32_t p = 0;
        while (p < nv && is_zero(M(a, i, p))) p++;
        if (p == nv) continue;
        E *val = M(a, i, nv);
        for (uint32_t j = p + 1; j < nv; j++)
            if (!is_zero(M(a, i, j))) val = s_sub(val, s_mul2(M(a, i, j), vv[j]));
        sol[ns++] = s_fn2(F_EQ, vv[p], nz(val));
    }
    return s_list(ns, sol);
}

/* LU with row pivoting: P*A = L*U, returns [L, U, P] */
/*
 * Doolittle elimination (unit lower-triangular L).  At step k the pivot row
 * is chosen by pick_pivot (first nonzero, or largest magnitude for float
 * matrices); swapping rows of A also swaps the already computed multipliers
 * in L and the permutation perm.  The multipliers f = a[i][k]/a[k][k] are
 * stored in L, and a becomes U.  P has P[i][perm[i]] = 1.  Singular
 * matrices (no pivot in some column) throw CAS_ERR_DOMAIN.  O(n^3).
 */
E *mat_lu(E *e) {
    mat a = m_from(e);
    if (a.r != a.c) cas_throw(CAS_ERR_DOMAIN, "lu: matrix must be square");
    uint32_t n = a.r;
    double tol = m_tol(a);
    mat L = m_alloc(n, n), P = m_alloc(n, n);
    uint32_t *perm = salloc(sizeof(uint32_t) * n);
    for (uint32_t i = 0; i < n; i++) perm[i] = i;
    for (size_t i = 0; i < (size_t)n * n; i++) a.a[i] = nz(a.a[i]);
    for (uint32_t k = 0; k < n; k++) {
        poll_intr();
        int p = pick_pivot(a, k, k, tol);
        if (p < 0) cas_throw(CAS_ERR_DOMAIN, "lu: matrix is singular");
        if ((uint32_t)p != k) {
            for (uint32_t j = 0; j < n; j++) {
                E *t = M(a, k, j); M(a, k, j) = M(a, (uint32_t)p, j); M(a, (uint32_t)p, j) = t;
                t = M(L, k, j); M(L, k, j) = M(L, (uint32_t)p, j); M(L, (uint32_t)p, j) = t;
            }
            uint32_t t = perm[k]; perm[k] = perm[p]; perm[p] = t;
        }
        for (uint32_t i = k + 1; i < n; i++) {
            E *f = nz(s_div(M(a, i, k), M(a, k, k)));
            M(L, i, k) = f;
            for (uint32_t j = k; j < n; j++) M(a, i, j) = nz(s_sub(M(a, i, j), s_mul2(f, M(a, k, j))));
        }
    }
    for (uint32_t i = 0; i < n; i++) {
        M(L, i, i) = K->c1;
        M(P, i, perm[i]) = K->c1;
    }
    E *v[3] = {m_to(L), m_to(a), m_to(P)};
    return s_list(3, v);
}

/* QR by Gram-Schmidt (exact, with square-root norms): A = Q*R, returns [Q, R] */
/*
 * Classical Gram-Schmidt on the columns a_j of the r x c matrix A:
 *   R[k][j] = <q_k, a_j> = sum conj(q_k[i]) a_j[i]   (k < j)
 *   v = a_j - sum_k R[k][j] q_k,   R[j][j] = |v| = sqrt(sum |v_i|^2),
 *   q_j = v / R[j][j].
 * Q is r x c with orthonormal columns, R is c x c upper triangular (the
 * "thin" QR).  Classical rather than modified Gram-Schmidt is fine here
 * because the arithmetic is exact (for float input it loses orthogonality on
 * ill-conditioned matrices).  Every intermediate goes through simplify_full
 * to denest the square roots; this is expensive, so QR is practical only for
 * small matrices.  Linearly dependent columns (zero norm) throw
 * CAS_ERR_DOMAIN.
 */
E *mat_qr(E *e) {
    mat a = m_from(e);
    uint32_t r = a.r, c = a.c;
    mat Q = m_alloc(r, c), R = m_alloc(c, c);
    E **v = salloc(sizeof(E *) * r);
    for (uint32_t j = 0; j < c; j++) {
        poll_intr();
        for (uint32_t i = 0; i < r; i++) v[i] = M(a, i, j);
        for (uint32_t k = 0; k < j; k++) {
            E *d = K->c0;
            for (uint32_t i = 0; i < r; i++) d = s_add2(d, s_mul2(s_fn1(F_CONJ, M(Q, i, k)), M(a, i, j)));
            d = simplify_full(d);
            M(R, k, j) = d;
            for (uint32_t i = 0; i < r; i++) v[i] = s_sub(v[i], s_mul2(d, M(Q, i, k)));
        }
        E *nn = K->c0;
        for (uint32_t i = 0; i < r; i++) {
            v[i] = simplify_full(v[i]);
            nn = s_add2(nn, s_powi(s_fn1(F_ABS, v[i]), 2));
        }
        E *norm = simplify_full(s_sqrt(simplify_full(nn)));
        if (is_zero(norm)) cas_throw(CAS_ERR_DOMAIN, "qr: columns are linearly dependent");
        M(R, j, j) = norm;
        for (uint32_t i = 0; i < r; i++) M(Q, i, j) = simplify_full(s_div(v[i], norm));
    }
    E *out[2] = {m_to(Q), m_to(R)};
    return s_list(2, out);
}

/* Cholesky: A = L*L^T for symmetric positive definite A */
/*
 * Column by column (Cholesky-Banachiewicz order):
 *   L[j][j] = sqrt(A[j][j] - sum_{k<j} L[j][k]^2)
 *   L[i][j] = (A[i][j] - sum_{k<j} L[i][k] L[j][k]) / L[j][j],  i > j.
 * Only the lower triangle of A is read; symmetry is not checked.  If a
 * radicand evaluates numerically to a value <= 0 the matrix is not positive
 * definite (CAS_ERR_DOMAIN); symbolic radicands are accepted as they are.
 * Entries are simplified with simplify_full (square roots).
 */
E *mat_cholesky(E *e) {
    mat a = m_from(e);
    if (a.r != a.c) cas_throw(CAS_ERR_DOMAIN, "cholesky: matrix must be square");
    uint32_t n = a.r;
    mat L = m_alloc(n, n);
    for (uint32_t j = 0; j < n; j++) {
        poll_intr();
        E *s = M(a, j, j);
        for (uint32_t k = 0; k < j; k++) s = s_sub(s, s_powi(M(L, j, k), 2));
        s = simplify_full(s);
        double sv;
        if (to_double(s, &sv) && sv <= 0) cas_throw(CAS_ERR_DOMAIN, "cholesky: matrix is not positive definite");
        E *d = simplify_full(s_sqrt(s));
        M(L, j, j) = d;
        for (uint32_t i = j + 1; i < n; i++) {
            E *t = M(a, i, j);
            for (uint32_t k = 0; k < j; k++) t = s_sub(t, s_mul2(M(L, i, k), M(L, j, k)));
            M(L, i, j) = simplify_full(s_div(t, d));
        }
    }
    return m_to(L);
}
