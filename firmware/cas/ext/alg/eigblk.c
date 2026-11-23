/*
 * eigblk.c -- eigenvalues of large reducible matrices (algorithm slot CASX_FN_EIGENVALS, tried by
 * the core's eigenvals before its own determinant of x I - A).
 *
 * The strongly connected components of the graph i -> j (A[i][j] != 0) give a permutation that
 * makes A block upper triangular (Tarjan), so the characteristic polynomial is the product of
 * those of the diagonal blocks: a 1 x 1 block is its own eigenvalue, a larger one goes to the
 * core's charpoly and roots_mult (exact roots with multiplicities).  (2 - a) I_100 is one
 * hundred 1 x 1 blocks instead of a 100 x 100 symbolic determinant.  When the roots of a block
 * are not all found exactly the call stays unevaluated (as in the core).  An irreducible
 * matrix, or one smaller than 6 x 6, is left to the core (NULL).
 */
#include "casx_mod.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

#define AT(A, i, j) EK(EK(A)[i])[j]
#define NMAX 400

typedef struct {
    E *A;
    uint32_t n, idx, nst, ncomp;
    int *index, *low, *comp;
    uint32_t *stack;
    uint8_t *on;
} tj;

/* Tarjan's strongly connected components, recursive (depth <= n <= NMAX, stack_check) */
static void strong(XP, tj *T, uint32_t v) {
    stack_check();
    T->index[v] = T->low[v] = (int)T->idx++;
    T->stack[T->nst++] = v, T->on[v] = 1;
    for (uint32_t w = 0; w < T->n; w++) {
        if (w == v || is_zero(AT(T->A, v, w))) continue;
        if (T->index[w] < 0) {
            strong(X, T, w);
            if (T->low[w] < T->low[v]) T->low[v] = T->low[w];
        } else if (T->on[w] && T->index[w] < T->low[v])
            T->low[v] = T->index[w];
    }
    if (T->low[v] == T->index[v]) {
        uint32_t w;
        do {
            w = T->stack[--T->nst];
            T->on[w] = 0;
            T->comp[w] = (int)T->ncomp;
        } while (w != v);
        T->ncomp++;
    }
}

static int has_val(XP, E **v, uint32_t n, E *x) {
    for (uint32_t i = 0; i < n; i++)
        if (expr_eq(v[i], x) || is_zero(rat_normal(s_sub(v[i], x)))) return 1;
    return 0;
}

static E *eig_slot(XP, uint32_t na, E **a) {
    E *A = a[0];
    uint32_t n, c;
    if (!is_matrix(A, &n, &c) || n != c || n < 6 || n > NMAX || has_float(A)) return NULL;
    tj T;
    T.A = A, T.n = n, T.idx = T.nst = T.ncomp = 0;
    T.index = salloc(sizeof(int) * n * 3), T.low = T.index + n, T.comp = T.index + 2 * n;
    T.stack = salloc(sizeof(uint32_t) * n), T.on = salloc(n);
    for (uint32_t i = 0; i < n; i++) T.index[i] = -1, T.on[i] = 0;
    for (uint32_t i = 0; i < n; i++)
        if (T.index[i] < 0) strong(X, &T, i);
    if (T.ncomp < 2) return NULL;
    E **ev = salloc(sizeof(E *) * (n + 1)), *L = mk_sym_name("_lambda");
    uint32_t ne = 0;
    uint32_t *mem = salloc(sizeof(uint32_t) * n);
    E **lam = salloc(sizeof(E *) * n);
    long *mult = salloc(sizeof(long) * n);
    for (uint32_t k = 0; k < T.ncomp; k++) {
        uint32_t m = 0;
        for (uint32_t i = 0; i < n; i++)
            if (T.comp[i] == (int)k) mem[m++] = i;
        if (m == 1) {
            E *v = AT(A, mem[0], mem[0]);
            if (!has_val(X, ev, ne, v)) ev[ne++] = v;
            continue;
        }
        E **rows = salloc(sizeof(E *) * m);
        for (uint32_t i = 0; i < m; i++) {
            E **r = salloc(sizeof(E *) * m);
            for (uint32_t j = 0; j < m; j++) r[j] = AT(A, mem[i], mem[j]);
            rows[i] = s_list(m, r);
        }
        E *B = s_list(m, rows), *cp = mat_charpoly(B, L);
        int nr = roots_mult(cp, L, m, lam, mult);
        if (nr < 0) { /* not all roots exactly: unevaluated, like the core (without its determinant) */
            E *ro = mk_fn(C_EIGENVALS, 1, a);
            ro->f = F_SIMP;
            return ro;
        }
        for (int i = 0; i < nr; i++)
            if (!has_val(X, ev, ne, lam[i])) ev[ne++] = lam[i];
    }
    return s_list(ne, ev);
}

int casx_eig_register(XP) { return X->reg_fn(CASX_FN_EIGENVALS, (void *)eig_slot); }
