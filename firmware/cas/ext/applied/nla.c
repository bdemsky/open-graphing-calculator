/*
 * nla.c -- numeric linear algebra of the casx module (applied.h), in double precision.
 *
 *   svd(A)       thin singular value decomposition A = U*S*transpose(V): [U, S, V] with U m x k,
 *                S = diag(s_1 >= ... >= s_k), V n x k, k = min(m, n); one-sided Jacobi (Hestenes)
 *                on the columns (on transpose(A) when m < n), sweeps until no pair has
 *                |u_p . u_q| > eps sqrt(|u_p|^2 |u_q|^2); columns of U for zero singular values
 *                completed by Gram-Schmidt; the largest entry of each column of V made positive.
 *                Entries below 64 eps (U, V) or max(m, n) eps s_1 (S) are shown as 0.
 *   singvals(A)  the singular values, descending: exact (square roots of the eigenvalues of
 *                transpose(A) A or A transpose(A), whichever is smaller) for an exact matrix whose
 *                characteristic polynomial the core can solve, else numeric.
 *   norm(A, 2)   the largest singular value; cond(A, 2) = s_1/s_k (inf if s_k = 0);
 *                cond(A, 1), cond(A, inf) = norm(A, p) norm(inv(A), p).
 *   rank(A, tol) the number of singular values > tol (numeric).
 *   pinv(A)      Moore-Penrose inverse: exact through the rank factorisation A = C F (C the pivot
 *                columns of A, F the nonzero rows of rref(A)): pinv = F' (F F')^-1 (C' C)^-1 C';
 *                with floats V diag(1/s_i) U' over s_i > max(m, n) eps s_1.
 * Slot CASX_FN_NUMLA (the core's eigenvals, eigenvects and rank of a matrix with floats, and its
 * eigenvects when the eigenvalues are numeric roots):
 *   symmetric:  cyclic Jacobi rotations (eigenvalues and orthonormal eigenvectors);
 *   otherwise:  balancing, reduction to Hessenberg form by stabilised elimination, Francis
 *               double-shift QR iteration (real arithmetic, complex conjugate pairs), and an
 *               eigenvector per eigenvalue by inverse iteration with complex LU.
 *   Eigenvalues are sorted by real, then imaginary part; values within 1e-10 |A| are merged (one
 *   entry, like the exact eigenvals).  The slot declines (NULL) entries that are not real numbers.
 */
#include "applied.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

#define SQ(v) ((v) * (v))
#define MAXSWEEP 80

typedef struct {
    double re, im;
} cx;

/* ---------------------------------------------------------------- SVD */

/* one-sided Jacobi on u (m x n, m >= n, row-major), v = n x n: on return u has orthonormal
 * columns (completed where s_j = 0), s the singular values descending, v orthogonal */
static void jsvd(XP, double *u, uint32_t m, uint32_t n, double *v, double *s) {
    for (uint32_t i = 0; i < n * n; i++) v[i] = 0;
    for (uint32_t i = 0; i < n; i++) v[i * n + i] = 1;
    for (int sw = 0; sw < MAXSWEEP; sw++) {
        int rot = 0;
        poll_intr();
        for (uint32_t p = 0; p + 1 < n; p++)
            for (uint32_t q = p + 1; q < n; q++) {
                double al = 0, be = 0, ga = 0;
                for (uint32_t i = 0; i < m; i++) {
                    double x = u[i * n + p], y = u[i * n + q];
                    al += x * x, be += y * y, ga += x * y;
                }
                if (ga == 0 || AFABS(ga) <= AP_EPS * X->sqrt(al * be)) continue;
                rot = 1;
                double ze = (be - al) / (2 * ga);
                double t = (ze >= 0 ? 1 : -1) / (AFABS(ze) + X->sqrt(1 + ze * ze));
                double c = 1 / X->sqrt(1 + t * t), sn = c * t;
                for (uint32_t i = 0; i < m; i++) {
                    double x = u[i * n + p], y = u[i * n + q];
                    u[i * n + p] = c * x - sn * y, u[i * n + q] = sn * x + c * y;
                }
                for (uint32_t i = 0; i < n; i++) {
                    double x = v[i * n + p], y = v[i * n + q];
                    v[i * n + p] = c * x - sn * y, v[i * n + q] = sn * x + c * y;
                }
            }
        if (!rot) break;
    }
    for (uint32_t j = 0; j < n; j++) {
        double nr = 0;
        for (uint32_t i = 0; i < m; i++) nr += SQ(u[i * n + j]);
        s[j] = X->sqrt(nr);
    }
    /* sort descending (columns of u and v with them) */
    for (uint32_t j = 0; j < n; j++) {
        uint32_t b = j;
        for (uint32_t k = j + 1; k < n; k++)
            if (s[k] > s[b]) b = k;
        if (b == j) continue;
        double t = s[j];
        s[j] = s[b], s[b] = t;
        for (uint32_t i = 0; i < m; i++) t = u[i * n + j], u[i * n + j] = u[i * n + b], u[i * n + b] = t;
        for (uint32_t i = 0; i < n; i++) t = v[i * n + j], v[i * n + j] = v[i * n + b], v[i * n + b] = t;
    }
    double tiny = (m > n ? m : n) * AP_EPS * s[0];
    for (uint32_t j = 0; j < n; j++) {
        if (s[j] > tiny && s[j] > 0) {
            for (uint32_t i = 0; i < m; i++) u[i * n + j] /= s[j];
        } else { /* complete: the first unit vector with a large part orthogonal to columns 0..j-1 */
            s[j] = 0;
            for (uint32_t e = 0; e < m; e++) {
                for (uint32_t i = 0; i < m; i++) u[i * n + j] = i == e;
                for (int pass = 0; pass < 2; pass++)
                    for (uint32_t k = 0; k < j; k++) {
                        double d = 0;
                        for (uint32_t i = 0; i < m; i++) d += u[i * n + k] * u[i * n + j];
                        for (uint32_t i = 0; i < m; i++) u[i * n + j] -= d * u[i * n + k];
                    }
                double nr = 0;
                for (uint32_t i = 0; i < m; i++) nr += SQ(u[i * n + j]);
                if (nr > 0.25) {
                    nr = X->sqrt(nr);
                    for (uint32_t i = 0; i < m; i++) u[i * n + j] /= nr;
                    break;
                }
            }
        }
        /* sign: the largest entry of v's column positive */
        uint32_t b = 0;
        for (uint32_t i = 1; i < n; i++)
            if (AFABS(v[i * n + j]) > AFABS(v[b * n + j]) * (1 + 1e-12)) b = i;
        if (v[b * n + j] < 0) {
            for (uint32_t i = 0; i < m; i++) u[i * n + j] = -u[i * n + j];
            for (uint32_t i = 0; i < n; i++) v[i * n + j] = -v[i * n + j];
        }
    }
}

/* SVD of the m x n row-major a: U (m x k), s (k), V (n x k), k = min(m, n) */
static void svd_d(XP, const double *a, uint32_t m, uint32_t n, double **U, double **s, double **V) {
    int tr = m < n;
    uint32_t r = tr ? n : m, c = tr ? m : n; /* the tall one: r x c */
    double *u = salloc(sizeof(double) * r * c), *v = salloc(sizeof(double) * c * c);
    *s = salloc(sizeof(double) * c);
    for (uint32_t i = 0; i < r; i++)
        for (uint32_t j = 0; j < c; j++) u[i * c + j] = tr ? a[j * n + i] : a[i * n + j];
    jsvd(X, u, r, c, v, *s);
    if (tr) *U = v, *V = u; /* A' = u s v'  ->  A = v s u' */
    else *U = u, *V = v;
}

/* the singular values of a numeric matrix as doubles (k of them) */
static double *svals_d(XP, E *A, uint32_t *k, uint32_t *mm, uint32_t *nn) {
    uint32_t m, n;
    double *a = ap_dmat(X, A, &m, &n), *U, *s, *V;
    svd_d(X, a, m, n, &U, &s, &V);
    *k = m < n ? m : n;
    if (mm) *mm = m, *nn = n;
    return s;
}

E *ap_svd(XP, E *A) {
    uint32_t m, n;
    double *a = ap_dmat(X, A, &m, &n), *U, *s, *V;
    svd_d(X, a, m, n, &U, &s, &V);
    uint32_t k = m < n ? m : n;
    double *S = salloc(sizeof(double) * k * k);
    for (uint32_t i = 0; i < k * k; i++) S[i] = 0;
    for (uint32_t i = 0; i < k; i++) S[i * k + i] = s[i];
    E *r[3] = {ap_mat(X, U, m, k, 64 * AP_EPS), ap_mat(X, S, k, k, (m > n ? m : n) * AP_EPS * s[0]),
               ap_mat(X, V, n, k, 64 * AP_EPS)};
    return s_list(3, r);
}

static E *tr(XP, E *M) { return eval_cmd(C_TRANSPOSE, 1, &M); }
static E *minv(XP, E *M) { return eval_cmd(C_INV, 1, &M); }

/* an exact matrix (no floats); numeric: every entry a constant (else a throw) */
static int exact_mat(XP, E *A, int numeric) {
    uint32_t m, n;
    if (!is_matrix(A, &m, &n)) cas_throw(CAS_ERR_DOMAIN, "expected a matrix");
    if (has_float(A)) return 0;
    for (uint32_t i = 0; numeric && i < m; i++)
        for (uint32_t j = 0; j < n; j++)
            if (!is_const_expr(EK(EK(A)[i])[j])) cas_throw(CAS_ERR_DOMAIN, "expected a numeric matrix");
    return 1;
}

static double dval(XP, E *e) {
    double re, im;
    return X->eval_complex(e, &re, &im) ? re : 0;
}

/* exact singular values (descending, with multiplicity) of an exact constant matrix, or NULL
 * when the eigenvalues of A'A (A A') are not all found as real radicals */
static E *svals_exact(XP, E *A) {
    uint32_t m, n;
    is_matrix(A, &m, &n);
    uint32_t k = m < n ? m : n;
    if (k > 8) return NULL;
    E *At = tr(X, A), *B = m <= n ? mat_mul_lists(A, At) : mat_mul_lists(At, A);
    E *L = mk_sym_name("_lambda"), *cp = mat_charpoly(B, L);
    E **lam = salloc(sizeof(E *) * k), **v = salloc(sizeof(E *) * k);
    long *mult = salloc(sizeof(long) * k);
    double *d = salloc(sizeof(double) * k);
    int nr = roots_mult(cp, L, k, lam, mult);
    if (nr < 0) return NULL;
    uint32_t c = 0;
    for (int i = 0; i < nr; i++) {
        if (has_float(lam[i]) || !freeof(lam[i], K->i_)) return NULL;
        for (long j = 0; j < mult[i] && c < k; j++) v[c] = s_sqrt(lam[i]), d[c] = dval(X, v[c]), c++;
    }
    if (c != k) return NULL;
    for (uint32_t i = 0; i < k; i++) /* descending */
        for (uint32_t j = i + 1; j < k; j++)
            if (d[j] > d[i]) {
                double t = d[i];
                E *e = v[i];
                d[i] = d[j], d[j] = t, v[i] = v[j], v[j] = e;
            }
    return s_list(k, v);
}

E *ap_singvals(XP, E *A) {
    uint32_t m, n, k;
    if (exact_mat(X, A, 1)) {
        E *r = svals_exact(X, A);
        if (r) return r;
    }
    double *s = svals_d(X, A, &k, &m, &n);
    E **v = salloc(sizeof(E *) * k);
    double tiny = (m > n ? m : n) * AP_EPS * s[0];
    for (uint32_t i = 0; i < k; i++) v[i] = mk_flt(s[i] <= tiny ? 0.0 : s[i]);
    return s_list(k, v);
}

E *ap_norm2(XP, E *A) { return EK(ap_singvals(X, A))[0]; }

E *ap_cond(XP, E *A, E *p) {
    long v;
    int inf = expr_eq(p, K->inf_);
    if (num_is_small_int(p, &v) && v == 2) {
        E *s = ap_singvals(X, A), *lo = EK(s)[s->n - 1];
        if (is_zero(lo)) return K->inf_;
        E *q = s_div(EK(s)[0], lo);
        return eval_cmd(C_SIMPLIFY, 1, &q);
    }
    if (!inf && !(num_is_small_int(p, &v) && v == 1)) cas_throw(CAS_ERR_DOMAIN, "cond: p must be 1, 2 or inf");
    uint32_t m, n;
    if (!is_matrix(A, &m, &n) || m != n) cas_throw(CAS_ERR_DOMAIN, "cond: need a square matrix");
    if (is_zero(eval_cmd(C_DET, 1, &A))) return K->inf_;
    E *a1[2] = {A, p}, *a2[2] = {minv(X, A), p};
    return s_mul2(eval_cmd(C_NORM, 2, a1), eval_cmd(C_NORM, 2, a2));
}

E *ap_rank(XP, E *A, E *tol) {
    double t;
    uint32_t k;
    if (!to_double(tol, &t) || !(t >= 0)) cas_throw(CAS_ERR_DOMAIN, "rank: the tolerance must be a number >= 0");
    double *s = svals_d(X, A, &k, NULL, NULL);
    long r = 0;
    for (uint32_t i = 0; i < k; i++) r += s[i] > t;
    return mk_int(r);
}

E *ap_pinv(XP, E *A) {
    uint32_t m, n;
    if (exact_mat(X, A, 0)) {
        long r;
        E *R = mat_rref(A, &r);
        is_matrix(A, &m, &n);
        if (r == 0) {
            E **rows = salloc(sizeof(E *) * n), **z = salloc(sizeof(E *) * m);
            for (uint32_t j = 0; j < m; j++) z[j] = K->c0;
            for (uint32_t i = 0; i < n; i++) rows[i] = s_list(m, z);
            return s_list(n, rows);
        }
        E **Cr = salloc(sizeof(E *) * m), **Fr = salloc(sizeof(E *) * (size_t)r);
        uint32_t *piv = salloc(sizeof(uint32_t) * (size_t)r);
        for (long i = 0; i < r; i++) {
            uint32_t j = 0;
            while (j < n && is_zero(EK(EK(R)[i])[j])) j++;
            piv[i] = j < n ? j : 0;
            Fr[i] = EK(R)[i];
        }
        for (uint32_t i = 0; i < m; i++) {
            E **row = salloc(sizeof(E *) * (size_t)r);
            for (long j = 0; j < r; j++) row[j] = EK(EK(A)[i])[piv[j]];
            Cr[i] = s_list((uint32_t)r, row);
        }
        E *C = s_list(m, Cr), *F = s_list((uint32_t)r, Fr), *Ct = tr(X, C), *Ft = tr(X, F);
        E *P = mat_mul_lists(mat_mul_lists(Ft, minv(X, mat_mul_lists(F, Ft))),
                             mat_mul_lists(minv(X, mat_mul_lists(Ct, C)), Ct));
        E **rows = salloc(sizeof(E *) * n);
        for (uint32_t i = 0; i < n; i++) {
            E **row = salloc(sizeof(E *) * m);
            for (uint32_t j = 0; j < m; j++) row[j] = rat_normal(EK(EK(P)[i])[j]);
            rows[i] = s_list(m, row);
        }
        return s_list(n, rows);
    }
    double *a = ap_dmat(X, A, &m, &n), *U, *s, *V;
    svd_d(X, a, m, n, &U, &s, &V);
    uint32_t k = m < n ? m : n;
    double tol = (m > n ? m : n) * AP_EPS * s[0], *P = salloc(sizeof(double) * n * m), mx = 0;
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t j = 0; j < m; j++) {
            double t = 0;
            for (uint32_t l = 0; l < k; l++)
                if (s[l] > tol) t += V[i * k + l] * U[j * k + l] / s[l];
            P[i * m + j] = t;
            if (AFABS(t) > mx) mx = AFABS(t);
        }
    return ap_mat(X, P, n, m, 64 * AP_EPS * mx);
}

/* -------------------------------------------------------- eigenvalues */
#define H(i, j) a[(i) * n + (j)]

/* cyclic Jacobi on the symmetric a (destroyed: the eigenvalues end on its diagonal), v the
 * eigenvectors (columns) */
static void jeig(XP, double *a, uint32_t n, double *v) {
    for (uint32_t i = 0; i < n * n; i++) v[i] = 0;
    for (uint32_t i = 0; i < n; i++) v[i * n + i] = 1;
    for (int sw = 0; sw < MAXSWEEP; sw++) {
        double off = 0, tot = 0;
        for (uint32_t i = 0; i < n; i++)
            for (uint32_t j = 0; j < n; j++) {
                tot += SQ(H(i, j));
                if (i != j) off += SQ(H(i, j));
            }
        if (off <= SQ(AP_EPS) * tot) break;
        poll_intr();
        for (uint32_t p = 0; p + 1 < n; p++)
            for (uint32_t q = p + 1; q < n; q++) {
                if (H(p, q) == 0) continue;
                double tau = (H(q, q) - H(p, p)) / (2 * H(p, q));
                double t = (tau >= 0 ? 1 : -1) / (AFABS(tau) + X->sqrt(1 + tau * tau));
                double c = 1 / X->sqrt(1 + t * t), s = c * t;
                for (uint32_t k = 0; k < n; k++) {
                    double x = H(k, p), y = H(k, q);
                    H(k, p) = c * x - s * y, H(k, q) = s * x + c * y;
                    x = v[k * n + p], y = v[k * n + q];
                    v[k * n + p] = c * x - s * y, v[k * n + q] = s * x + c * y;
                }
                for (uint32_t k = 0; k < n; k++) {
                    double x = H(p, k), y = H(q, k);
                    H(p, k) = c * x - s * y, H(q, k) = s * x + c * y;
                }
            }
    }
}

/* balancing (row and column scaling by powers of 2) */
static void balance(double *a, uint32_t n) {
    for (int done = 0; !done;) {
        done = 1;
        for (uint32_t i = 0; i < n; i++) {
            double r = 0, c = 0;
            for (uint32_t j = 0; j < n; j++)
                if (j != i) c += AFABS(H(j, i)), r += AFABS(H(i, j));
            if (c == 0 || r == 0) continue;
            double g = r / 2, f = 1, s = c + r;
            while (c < g) f *= 2, c *= 4;
            g = r * 2;
            while (c > g) f /= 2, c /= 4;
            if ((c + r) / f < 0.95 * s) {
                done = 0;
                for (uint32_t j = 0; j < n; j++) H(i, j) /= f;
                for (uint32_t j = 0; j < n; j++) H(j, i) *= f;
            }
        }
    }
}

/* reduction to upper Hessenberg form by elimination with pivoting (a similarity) */
static void elmhes(double *a, uint32_t n) {
    for (uint32_t m = 1; m + 1 < n; m++) {
        double x = 0, t;
        uint32_t i = m;
        for (uint32_t j = m; j < n; j++)
            if (AFABS(H(j, m - 1)) > AFABS(x)) x = H(j, m - 1), i = j;
        if (i != m) {
            for (uint32_t j = m - 1; j < n; j++) t = H(i, j), H(i, j) = H(m, j), H(m, j) = t;
            for (uint32_t j = 0; j < n; j++) t = H(j, i), H(j, i) = H(j, m), H(j, m) = t;
        }
        if (x != 0)
            for (i = m + 1; i < n; i++) {
                double y = H(i, m - 1);
                if (y == 0) continue;
                y /= x;
                H(i, m - 1) = 0;
                for (uint32_t j = m; j < n; j++) H(i, j) -= y * H(m, j);
                for (uint32_t j = 0; j < n; j++) H(j, m) += y * H(j, i);
            }
    }
}

static double sgn(double a, double b) { return b >= 0 ? AFABS(a) : -AFABS(a); }

/* eigenvalues wr + i wi of the upper Hessenberg a (destroyed) by the Francis double-shift QR
 * iteration with exceptional shifts; 0 when an eigenvalue needs more than 60 iterations */
static int hqr(XP, double *a, int n, double *wr, double *wi) {
    double anorm = 0, p = 0, q = 0, r = 0, s, t = 0, u, v, w, x, y, z;
    for (int i = 0; i < n; i++)
        for (int j = i > 0 ? i - 1 : 0; j < n; j++) anorm += AFABS(H(i, j));
    for (int nn = n - 1; nn >= 0;) {
        int its = 0, l;
        do {
            poll_intr();
            for (l = nn; l >= 1; l--) {
                s = AFABS(H(l - 1, l - 1)) + AFABS(H(l, l));
                if (s == 0) s = anorm;
                if (AFABS(H(l, l - 1)) + s == s) {
                    H(l, l - 1) = 0;
                    break;
                }
            }
            x = H(nn, nn);
            if (l == nn) {
                wr[nn] = x + t, wi[nn--] = 0;
            } else {
                y = H(nn - 1, nn - 1);
                w = H(nn, nn - 1) * H(nn - 1, nn);
                if (l == nn - 1) {
                    p = 0.5 * (y - x);
                    q = p * p + w;
                    z = X->sqrt(AFABS(q));
                    x += t;
                    if (q >= 0) {
                        z = p + sgn(z, p);
                        wr[nn - 1] = wr[nn] = x + z;
                        if (z != 0) wr[nn] = x - w / z;
                        wi[nn - 1] = wi[nn] = 0;
                    } else {
                        wr[nn - 1] = wr[nn] = x + p;
                        wi[nn - 1] = -(wi[nn] = z);
                    }
                    nn -= 2;
                } else {
                    if (its == 60) return 0;
                    if (its == 10 || its == 20 || its == 40) { /* exceptional shift */
                        t += x;
                        for (int i = 0; i <= nn; i++) H(i, i) -= x;
                        s = AFABS(H(nn, nn - 1)) + AFABS(H(nn - 1, nn - 2));
                        y = x = 0.75 * s;
                        w = -0.4375 * s * s;
                    }
                    ++its;
                    int m;
                    for (m = nn - 2; m >= l; m--) {
                        z = H(m, m);
                        r = x - z;
                        s = y - z;
                        p = (r * s - w) / H(m + 1, m) + H(m, m + 1);
                        q = H(m + 1, m + 1) - z - r - s;
                        r = H(m + 2, m + 1);
                        s = AFABS(p) + AFABS(q) + AFABS(r);
                        p /= s, q /= s, r /= s;
                        if (m == l) break;
                        u = AFABS(H(m, m - 1)) * (AFABS(q) + AFABS(r));
                        v = AFABS(p) * (AFABS(H(m - 1, m - 1)) + AFABS(z) + AFABS(H(m + 1, m + 1)));
                        if (u + v == v) break;
                    }
                    for (int i = m + 2; i <= nn; i++) {
                        H(i, i - 2) = 0;
                        if (i != m + 2) H(i, i - 3) = 0;
                    }
                    for (int k = m; k <= nn - 1; k++) {
                        if (k != m) {
                            p = H(k, k - 1);
                            q = H(k + 1, k - 1);
                            r = 0;
                            if (k != nn - 1) r = H(k + 2, k - 1);
                            if ((x = AFABS(p) + AFABS(q) + AFABS(r)) != 0) p /= x, q /= x, r /= x;
                        }
                        if ((s = sgn(X->sqrt(p * p + q * q + r * r), p)) != 0) {
                            if (k == m) {
                                if (l != m) H(k, k - 1) = -H(k, k - 1);
                            } else
                                H(k, k - 1) = -s * x;
                            p += s;
                            x = p / s, y = q / s, z = r / s, q /= p, r /= p;
                            for (int j = k; j <= nn; j++) {
                                p = H(k, j) + q * H(k + 1, j);
                                if (k != nn - 1) {
                                    p += r * H(k + 2, j);
                                    H(k + 2, j) -= p * z;
                                }
                                H(k + 1, j) -= p * y;
                                H(k, j) -= p * x;
                            }
                            int mmin = nn < k + 3 ? nn : k + 3;
                            for (int i = l; i <= mmin; i++) {
                                p = x * H(i, k) + y * H(i, k + 1);
                                if (k != nn - 1) {
                                    p += z * H(i, k + 2);
                                    H(i, k + 2) -= p * r;
                                }
                                H(i, k + 1) -= p * q;
                                H(i, k) -= p;
                            }
                        }
                    }
                }
            }
        } while (nn >= 0 && l < nn - 1);
    }
    return 1;
}
#undef H

static cx cmul(cx a, cx b) { return (cx){a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re}; }
static cx cdiv(cx a, cx b) {
    double d = b.re * b.re + b.im * b.im;
    return (cx){(a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d};
}
static double cabs2(cx a) { return a.re * a.re + a.im * a.im; }

/* an eigenvector of A for the eigenvalue lam by inverse iteration (complex LU with partial
 * pivoting of A - (lam + d) I), unit length, its largest entry real positive */
static void inv_iter(XP, const double *A, uint32_t n, cx lam, double nrm, cx *x) {
    cx *M = salloc(sizeof(cx) * n * n), *y = salloc(sizeof(cx) * n);
    uint32_t *pv = salloc(sizeof(uint32_t) * n);
    double sc = nrm > 1 ? nrm : 1;
    lam.re += 1e-10 * sc;
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t j = 0; j < n; j++) M[i * n + j] = (cx){A[i * n + j] - (i == j ? lam.re : 0), i == j ? -lam.im : 0};
    for (uint32_t k = 0; k < n; k++) {
        uint32_t b = k;
        for (uint32_t i = k + 1; i < n; i++)
            if (cabs2(M[i * n + k]) > cabs2(M[b * n + k])) b = i;
        pv[k] = b;
        if (b != k)
            for (uint32_t j = 0; j < n; j++) {
                cx t = M[k * n + j];
                M[k * n + j] = M[b * n + j], M[b * n + j] = t;
            }
        if (cabs2(M[k * n + k]) == 0) M[k * n + k] = (cx){AP_EPS * sc, 0};
        for (uint32_t i = k + 1; i < n; i++) {
            cx f = cdiv(M[i * n + k], M[k * n + k]);
            M[i * n + k] = f;
            for (uint32_t j = k + 1; j < n; j++) {
                cx t = cmul(f, M[k * n + j]);
                M[i * n + j].re -= t.re, M[i * n + j].im -= t.im;
            }
        }
    }
    for (uint32_t i = 0; i < n; i++) x[i] = (cx){1.0 / (1 + i), 0};
    for (int it = 0; it < 3; it++) {
        for (uint32_t i = 0; i < n; i++) y[i] = x[i];
        for (uint32_t k = 0; k < n; k++) /* P x (the row swaps moved whole rows, multipliers too) */
            if (pv[k] != k) {
                cx t = y[k];
                y[k] = y[pv[k]], y[pv[k]] = t;
            }
        for (uint32_t k = 0; k < n; k++) { /* forward: L y = P x */
            for (uint32_t i = k + 1; i < n; i++) {
                cx t = cmul(M[i * n + k], y[k]);
                y[i].re -= t.re, y[i].im -= t.im;
            }
        }
        for (uint32_t k = n; k-- > 0;) { /* back: U x = y */
            cx t = y[k];
            for (uint32_t j = k + 1; j < n; j++) {
                cx u = cmul(M[k * n + j], x[j]);
                t.re -= u.re, t.im -= u.im;
            }
            x[k] = cdiv(t, M[k * n + k]);
        }
        double mx = 0;
        for (uint32_t i = 0; i < n; i++)
            if (cabs2(x[i]) > mx) mx = cabs2(x[i]);
        mx = X->sqrt(mx);
        for (uint32_t i = 0; i < n; i++) x[i].re /= mx, x[i].im /= mx;
    }
    uint32_t b = 0;
    double nr = 0;
    for (uint32_t i = 0; i < n; i++) {
        nr += cabs2(x[i]);
        if (cabs2(x[i]) > cabs2(x[b]) * (1 + 1e-12)) b = i;
    }
    nr = X->sqrt(nr);
    double ab = X->sqrt(cabs2(x[b]));
    cx ph = {x[b].re / ab / nr, -x[b].im / ab / nr};
    for (uint32_t i = 0; i < n; i++) x[i] = cmul(x[i], ph);
}

static E *cvec(XP, const cx *v, uint32_t n, double tiny) {
    E **e = salloc(sizeof(E *) * n);
    for (uint32_t i = 0; i < n; i++)
        e[i] = ap_cnum(X, AFABS(v[i].re) <= tiny ? 0.0 : v[i].re, AFABS(v[i].im) <= tiny ? 0.0 : v[i].im);
    return s_list(n, e);
}

/* the slot: k = 0 eigenvals, 1 eigenvects, 2 rank of a real numeric matrix; NULL otherwise */
static E *numla(XP, uint32_t na, E **ar) {
    long k;
    uint32_t m, n;
    E *A = ar[0];
    if (na != 2 || !num_is_small_int(ar[1], &k) || !is_matrix(A, &m, &n) || (uint64_t)m * n > 40000) return NULL;
    for (uint32_t i = 0; i < m; i++)
        for (uint32_t j = 0; j < n; j++) {
            double re, im;
            E *e = EK(EK(A)[i])[j];
            if (!is_const_expr(e) || !X->eval_complex(e, &re, &im) || im != 0) return NULL;
        }
    if (k == 2) {
        uint32_t kk;
        double *s = svals_d(X, A, &kk, &m, &n);
        double tol = (m > n ? m : n) * AP_EPS * s[0];
        long r = 0;
        for (uint32_t i = 0; i < kk; i++) r += s[i] > tol;
        return mk_int(r);
    }
    if (m != n || n > 120) return NULL;
    double *a = ap_dmat(X, A, &m, &n), *a0 = salloc(sizeof(double) * n * n), nrm = 0, mx = 0;
    double *wr = salloc(sizeof(double) * n), *wi = salloc(sizeof(double) * n), *V = NULL;
    int sym = 1;
    for (uint32_t i = 0; i < n * n; i++) {
        a0[i] = a[i], nrm += SQ(a[i]);
        if (AFABS(a[i]) > mx) mx = AFABS(a[i]);
    }
    nrm = X->sqrt(nrm);
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t j = 0; j < i; j++)
            if (AFABS(a[i * n + j] - a[j * n + i]) > 1e-13 * mx) sym = 0;
    if (sym) {
        V = salloc(sizeof(double) * n * n);
        jeig(X, a, n, V);
        for (uint32_t i = 0; i < n; i++) wr[i] = a[i * n + i], wi[i] = 0;
    } else {
        balance(a, n);
        elmhes(a, n);
        if (!hqr(X, a, (int)n, wr, wi)) cas_throw(CAS_ERR_UNSUPPORTED, "eigenvalues: no convergence");
    }
    double sc = nrm > 1 ? nrm : 1, tiny = 64 * AP_EPS * sc, mrg = 1e-10 * sc;
    /* order by real, then imaginary part (insertion sort of the indices) */
    uint32_t *ix = salloc(sizeof(uint32_t) * n);
    for (uint32_t i = 0; i < n; i++) ix[i] = i;
    for (uint32_t i = 1; i < n; i++)
        for (uint32_t j = i; j > 0; j--) {
            uint32_t p = ix[j - 1], q = ix[j];
            if (wr[q] < wr[p] - mrg || (!(wr[q] > wr[p] + mrg) && wi[q] < wi[p])) ix[j - 1] = q, ix[j] = p;
            else break;
        }
    E **out = salloc(sizeof(E *) * n);
    uint32_t no = 0;
    cx *vec = salloc(sizeof(cx) * n);
    for (uint32_t i = 0; i < n;) {
        uint32_t j = i + 1, p = ix[i];
        while (j < n && AFABS(wr[ix[j]] - wr[p]) <= mrg && AFABS(wi[ix[j]] - wi[p]) <= mrg) j++;
        cx lam = {AFABS(wr[p]) <= tiny ? 0.0 : wr[p], AFABS(wi[p]) <= tiny ? 0.0 : wi[p]};
        E *ev = ap_cnum(X, lam.re, lam.im);
        if (k == 0) out[no++] = ev;
        else {
            E **bs = salloc(sizeof(E *) * (j - i));
            uint32_t nb = 0;
            if (sym)
                for (uint32_t t = i; t < j; t++) {
                    uint32_t b = 0;
                    for (uint32_t r = 0; r < n; r++) {
                        vec[r] = (cx){V[r * n + ix[t]], 0};
                        if (AFABS(vec[r].re) > AFABS(vec[b].re) * (1 + 1e-12)) b = r;
                    }
                    if (vec[b].re < 0)
                        for (uint32_t r = 0; r < n; r++) vec[r].re = -vec[r].re;
                    bs[nb++] = cvec(X, vec, n, 64 * AP_EPS);
                }
            else {
                inv_iter(X, a0, n, lam, nrm, vec);
                bs[nb++] = cvec(X, vec, n, 64 * AP_EPS);
            }
            E *pr[2] = {ev, s_list(nb, bs)};
            out[no++] = s_list(2, pr);
        }
        i = j;
    }
    return s_list(no, out);
}

/* address taken here: a static function (an XIP image has no GOT) */
int ap_nla_register(XP) { return X->reg_fn(CASX_FN_NUMLA, (void *)numla); }
