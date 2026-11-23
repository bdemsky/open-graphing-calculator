/*
 * logic.c -- Boolean logic of the casx module (applied.h).
 *
 * Expressions: and, or, not (core operators), xor(a, b), implies(a, b), iff(a, b) (the parser
 * writes `a xor b`, `a implies b`, `a iff b` as these calls), true, false, 0, 1.  Every other
 * subexpression (a symbol, a relation x > 1, ...) is an atom; symbols come first, alphabetically,
 * then the other atoms in order of appearance (truthtable(e, vars) fixes the order).
 *
 * All commands work from the truth table, evaluated 64 rows at a time (bit-parallel words; row r
 * gives the k-th of nv atoms the value bit nv-1-k of r, so the first atom changes slowest):
 *   truthtable(e [, vars])   the matrix [[atoms..., e], [0, 0, ..., value], ...] (1 = true,
 *                            0 = false; at most 10 atoms; e may be a list of expressions)
 *   dnf(e)                   a minimal sum of products (Quine-McCluskey): the prime implicants,
 *                            computed recursively over the atoms (the maximal cubes among x'P(f0),
 *                            x P(f1) and the pairwise intersections), then a minimum cover
 *                            (essential implicants, a greedy bound, then branch and bound over
 *                            the minterm with the fewest choices; fewest terms, then literals)
 *   cnf(e)                   a minimal product of sums: not(dnf(not e)) by De Morgan
 *   boolsimplify(e) = logicsimplify(e)   the one of the two with fewer literals (dnf on a tie);
 *                            true / false for a tautology / contradiction  (at most 12 atoms)
 *   satisfiable(e)           the first satisfying assignment [p = false, q = true, ...] in row
 *                            order, or false (at most 24 atoms)
 *   implies(a, b), iff(a, b) evaluate when an argument is true or false, else stay.
 */
#include "applied.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

enum { L_VAR, L_CONST, L_NOT, L_AND, L_OR, L_XOR, L_IMP, L_IFF };

typedef struct {
    uint8_t op;
    int32_t a, b; /* children, or the atom index / constant value */
} ln;

typedef struct {
    ln *nd;
    int nn, cap;
    E **var;
    int nv, vcap;
    int xor_id, imp_id, iff_id;
} lg;

static int add(XP, lg *G, int op, int a, int b) {
    if (G->nn == G->cap) {
        int nc = G->cap ? 2 * G->cap : 32;
        ln *n2 = salloc(sizeof(ln) * (size_t)nc);
        for (int i = 0; i < G->nn; i++) n2[i] = G->nd[i];
        G->nd = n2, G->cap = nc;
    }
    G->nd[G->nn] = (ln){(uint8_t)op, a, b};
    return G->nn++;
}

static int atom(XP, lg *G, E *e) {
    for (int i = 0; i < G->nv; i++)
        if (expr_eq(G->var[i], e)) return i;
    if (G->nv == G->vcap) {
        int nc = G->vcap ? 2 * G->vcap : 16;
        E **v2 = salloc(sizeof(E *) * (size_t)nc);
        for (int i = 0; i < G->nv; i++) v2[i] = G->var[i];
        G->var = v2, G->vcap = nc;
    }
    G->var[G->nv] = e;
    return G->nv++;
}

static int build(XP, lg *G, E *e) {
    stack_check();
    long v;
    if (ISYM(e, S_TRUE) || ISYM(e, S_FALSE)) return add(X, G, L_CONST, ISYM(e, S_TRUE), 0);
    if (e->t == E_INT || e->t == E_RAT || e->t == E_FLT) {
        if (!num_is_small_int(e, &v) || (v != 0 && v != 1)) cas_throw(CAS_ERR_DOMAIN, "not a logical expression");
        return add(X, G, L_CONST, (int)v, 0);
    }
    if (e->t == E_LIST || e->t == E_STR) cas_throw(CAS_ERR_DOMAIN, "not a logical expression");
    if (e->t == E_FN) {
        int op = -1;
        if (e->s == F_NOT && e->n == 1) return add(X, G, L_NOT, build(X, G, K0(e)), 0);
        if (e->s == F_AND) op = L_AND;
        else if (e->s == F_OR) op = L_OR;
        else if (e->s == G->xor_id) op = L_XOR;
        else if (e->s == G->imp_id && e->n == 2) op = L_IMP;
        else if (e->s == G->iff_id && e->n == 2) op = L_IFF;
        if (op >= 0 && e->n >= 2) {
            int r = build(X, G, K0(e));
            for (uint32_t i = 1; i < e->n; i++) r = add(X, G, op, r, build(X, G, EK(e)[i]));
            return r;
        }
    }
    return add(X, G, L_VAR, atom(X, G, e), 0);
}

static int name_lt(const char *a, const char *b) {
    for (; *a && *a == *b; a++, b++) {}
    return (unsigned char)*a < (unsigned char)*b;
}

/* symbols first (alphabetical), then the other atoms; renumbers the L_VAR nodes */
static void order(XP, lg *G) {
    int n = G->nv, *pos = salloc(sizeof(int) * (size_t)(n + 1)), *ix = salloc(sizeof(int) * (size_t)(n + 1));
    for (int i = 0; i < n; i++) ix[i] = i;
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0; j--) {
            E *p = G->var[ix[j - 1]], *q = G->var[ix[j]];
            int sw = q->t == E_SYM && (p->t != E_SYM || name_lt(sym_name(q->s), sym_name(p->s)));
            if (!sw) break;
            int t = ix[j - 1];
            ix[j - 1] = ix[j], ix[j] = t;
        }
    E **v2 = salloc(sizeof(E *) * (size_t)(n + 1));
    for (int i = 0; i < n; i++) v2[i] = G->var[ix[i]], pos[ix[i]] = i;
    G->var = v2;
    for (int i = 0; i < G->nn; i++)
        if (G->nd[i].op == L_VAR) G->nd[i].a = pos[G->nd[i].a];
}

static uint64_t ev(XP, const lg *G, int i, const uint64_t *col) {
    const ln *d = &G->nd[i];
    if (d->op == L_VAR) return col[d->a];
    if (d->op == L_CONST) return d->a ? ~(uint64_t)0 : 0;
    stack_check();
    uint64_t a = ev(X, G, d->a, col);
    if (d->op == L_NOT) return ~a;
    uint64_t b = ev(X, G, d->b, col);
    switch (d->op) {
    case L_AND: return a & b;
    case L_OR: return a | b;
    case L_XOR: return a ^ b;
    case L_IMP: return ~a | b;
    default: return ~(a ^ b);
    }
}

/* the values of node `root` on rows 64 blk .. 64 blk + 63 (nv atoms) */
static uint64_t block(XP, const lg *G, int root, uint64_t blk, uint64_t *col) {
    static const uint64_t pat[6] = {0xAAAAAAAAAAAAAAAAull, 0xCCCCCCCCCCCCCCCCull, 0xF0F0F0F0F0F0F0F0ull,
                                    0xFF00FF00FF00FF00ull, 0xFFFF0000FFFF0000ull, 0xFFFFFFFF00000000ull};
    for (int k = 0; k < G->nv; k++) {
        int j = G->nv - 1 - k;
        col[k] = j < 6 ? pat[j] : ((blk >> (j - 6)) & 1) ? ~(uint64_t)0 : 0;
    }
    uint64_t w = ev(X, G, root, col);
    if (G->nv < 6) w &= ((uint64_t)1 << (1u << G->nv)) - 1;
    return w;
}

/* the truth table of root as a byte per row (2^nv rows) */
static uint8_t *table(XP, const lg *G, int root) {
    uint32_t rows = 1u << G->nv;
    uint8_t *t = salloc(rows);
    uint64_t *col = salloc(sizeof(uint64_t) * (size_t)(G->nv + 1));
    for (uint64_t b = 0; b * 64 < rows; b++) {
        poll_intr();
        uint64_t w = block(X, G, root, b, col);
        for (uint32_t l = 0; l < 64 && b * 64 + l < rows; l++) t[b * 64 + l] = (w >> l) & 1;
    }
    return t;
}

/* ------------------------------------------------- prime implicants, cover */
typedef struct {
    uint32_t val, dc; /* row bits; dc: the bits that do not matter */
} imp;

static int popc(uint32_t v) {
    int c = 0;
    for (; v; v &= v - 1) c++;
    return c;
}

#define QM_CAP 20000

typedef struct {
    uint32_t care, val; /* a cube: the variables in care have the values in val */
} cube;

/* c inside d: d's variables are c's and agree */
static int inside(cube c, cube d) { return (d.care & ~c.care) == 0 && ((c.val ^ d.val) & d.care) == 0; }

/* the prime implicants of the subfunction t[0..2^(nv-lv)) of the variables lv..nv-1 (rows with
 * value want), recursively: P(f) = maximal cubes of x'P(f0) + x P(f1) + {p q : p in P(f0),
 * q in P(f1)} for the first variable x */
static cube *prim(XP, const uint8_t *t, int nv, int lv, int want, int *np) {
    stack_check();
    uint32_t size = 1u << (nv - lv), ones = 0;
    for (uint32_t r = 0; r < size; r++) ones += t[r] == want;
    if (!ones || ones == size) {
        *np = ones ? 1 : 0;
        cube *c = salloc(sizeof(cube));
        c->care = c->val = 0;
        return c;
    }
    int n0, n1;
    cube *p0 = prim(X, t, nv, lv + 1, want, &n0), *p1 = prim(X, t + size / 2, nv, lv + 1, want, &n1);
    uint32_t bit = 1u << (nv - 1 - lv);
    long nk = n0 + n1;
    for (int i = 0; i < n0; i++) {
        poll_intr();
        for (int j = 0; j < n1; j++) nk += !((p0[i].val ^ p1[j].val) & p0[i].care & p1[j].care);
    }
    if (nk > QM_CAP) cas_throw(CAS_ERR_MEMORY, "too many implicants");
    cube *r = salloc(sizeof(cube) * (size_t)(nk + 1));
    int k = 0;
    for (int i = 0; i < n0; i++) r[k++] = (cube){p0[i].care | bit, p0[i].val};
    for (int i = 0; i < n1; i++) r[k++] = (cube){p1[i].care | bit, p1[i].val | bit};
    for (int i = 0; i < n0; i++) {
        poll_intr();
        for (int j = 0; j < n1; j++)
            if (!((p0[i].val ^ p1[j].val) & p0[i].care & p1[j].care))
                r[k++] = (cube){p0[i].care | p1[j].care, p0[i].val | p1[j].val};
    }
    /* keep the maximal cubes (the first of equal ones) */
    int m = 0;
    for (int i = 0; i < k; i++) {
        if (!(i & 63)) poll_intr();
        int drop = 0;
        for (int j = 0; j < k && !drop; j++)
            drop = j != i && inside(r[i], r[j]) && (!inside(r[j], r[i]) || j < i);
        if (!drop) r[m++] = r[i];
    }
    *np = m;
    return r;
}

/* prime implicants of the rows r with t[r] == want */
static imp *primes(XP, const uint8_t *t, int nv, int want, int *np) {
    cube *c = prim(X, t, nv, 0, want, np);
    imp *p = salloc(sizeof(imp) * (size_t)(*np + 1));
    uint32_t all = (1u << nv) - 1;
    for (int i = 0; i < *np; i++) p[i] = (imp){c[i].val, all & ~c[i].care};
    return p;
}

typedef struct {
    const imp *p;
    int np, nm, best_n, best_l, cur_n, cur_l;
    const uint32_t *m;
    int *cnt;  /* per minterm: chosen implicants covering it */
    int *sel, *best;
    long nodes;
} cov;

static int covers(imp p, uint32_t m) { return (m & ~p.dc) == p.val; }

static void bb(XP, cov *C, int nv) {
    stack_check();
    if (++C->nodes > 200000) return;
    if (C->cur_n > C->best_n || (C->cur_n == C->best_n && C->cur_l >= C->best_l)) return;
    int pick = -1, popts = 1 << 30;
    for (int i = 0; i < C->nm; i++) {
        if (C->cnt[i]) continue;
        int o = 0;
        for (int j = 0; j < C->np; j++) o += covers(C->p[j], C->m[i]);
        if (o < popts) popts = o, pick = i;
    }
    if (pick < 0) { /* all covered */
        C->best_n = C->cur_n, C->best_l = C->cur_l;
        for (int j = 0; j < C->np; j++) C->best[j] = C->sel[j];
        return;
    }
    if (C->cur_n + 1 > C->best_n) return;
    for (int j = 0; j < C->np; j++) {
        if (C->sel[j] || !covers(C->p[j], C->m[pick])) continue;
        C->sel[j] = 1, C->cur_n++, C->cur_l += nv - popc(C->p[j].dc);
        for (int i = 0; i < C->nm; i++) C->cnt[i] += covers(C->p[j], C->m[i]);
        bb(X, C, nv);
        for (int i = 0; i < C->nm; i++) C->cnt[i] -= covers(C->p[j], C->m[i]);
        C->sel[j] = 0, C->cur_n--, C->cur_l -= nv - popc(C->p[j].dc);
    }
}

/* a minimum cover of the rows with t == want by prime implicants: their list (*nk entries) */
static imp *qm(XP, const uint8_t *t, int nv, int want, int *nk) {
    int np, nm = 0;
    imp *p = primes(X, t, nv, want, &np);
    uint32_t rows = 1u << nv, *m = salloc(sizeof(uint32_t) * rows);
    for (uint32_t r = 0; r < rows; r++)
        if (t[r] == want) m[nm++] = r;
    cov C = {p, np, nm, 1 << 30, 1 << 30, 0, 0, m, salloc(sizeof(int) * (size_t)(nm + 1)),
             salloc(sizeof(int) * (size_t)(np + 1)), salloc(sizeof(int) * (size_t)(np + 1)), 0};
    for (int i = 0; i < nm; i++) C.cnt[i] = 0;
    for (int j = 0; j < np; j++) C.sel[j] = C.best[j] = 0;
    /* essential implicants */
    for (int i = 0; i < nm; i++) {
        int o = 0, w = -1;
        for (int j = 0; j < np; j++)
            if (covers(p[j], m[i])) o++, w = j;
        if (o == 1 && !C.sel[w]) {
            C.sel[w] = 1, C.cur_n++, C.cur_l += nv - popc(p[w].dc);
            for (int k = 0; k < nm; k++) C.cnt[k] += covers(p[w], m[k]);
        }
    }
    /* a greedy cover first (a bound for the search, and the answer if the search is cut off) */
    {
        int *cnt = salloc(sizeof(int) * (size_t)(nm + 1)), *sel = salloc(sizeof(int) * (size_t)(np + 1)), gn = C.cur_n,
            gl = C.cur_l;
        for (int i = 0; i < nm; i++) cnt[i] = C.cnt[i];
        for (int j = 0; j < np; j++) sel[j] = C.sel[j];
        for (;;) {
            int bj = -1, bc = 0;
            for (int j = 0; j < np; j++) {
                if (sel[j]) continue;
                int c = 0;
                for (int i = 0; i < nm; i++) c += !cnt[i] && covers(p[j], m[i]);
                if (c > bc || (c == bc && c && popc(p[j].dc) > popc(p[bj].dc))) bc = c, bj = j;
            }
            if (bj < 0) break;
            sel[bj] = 1, gn++, gl += nv - popc(p[bj].dc);
            for (int i = 0; i < nm; i++) cnt[i] += covers(p[bj], m[i]);
        }
        C.best_n = gn, C.best_l = gl;
        for (int j = 0; j < np; j++) C.best[j] = sel[j];
    }
    bb(X, &C, nv);
    imp *r = salloc(sizeof(imp) * (size_t)(np + 1));
    int k = 0;
    for (int j = 0; j < np; j++)
        if (C.best[j]) r[k++] = p[j];
    /* order: by the literal of the first atom (positive, negative, absent), then the next */
    for (int i = 1; i < k; i++)
        for (int j = i; j > 0; j--) {
            int sw = 0;
            for (int v = 0; v < nv; v++) {
                uint32_t bit = 1u << (nv - 1 - v);
                int ka = r[j - 1].dc & bit ? 2 : r[j - 1].val & bit ? 0 : 1, kb = r[j].dc & bit ? 2 : r[j].val & bit ? 0 : 1;
                if (ka != kb) {
                    sw = kb < ka;
                    break;
                }
            }
            if (!sw) break;
            imp tt = r[j - 1];
            r[j - 1] = r[j], r[j] = tt;
        }
    *nk = k;
    return r;
}

static E *lit(XP, const lg *G, int v, int pos) { return pos ? G->var[v] : s_fn1(F_NOT, G->var[v]); }

/* the implicants as a sum of products (dual: product of sums of the negated literals) */
static E *form(XP, const lg *G, const imp *r, int k, int dual, int *nlit) {
    int nv = G->nv, op = dual ? F_OR : F_AND;
    E *acc = NULL;
    *nlit = 0;
    for (int i = 0; i < k; i++) {
        E *t = NULL;
        for (int v = 0; v < nv; v++) {
            uint32_t bit = 1u << (nv - 1 - v);
            if (r[i].dc & bit) continue;
            E *l = lit(X, G, v, ((r[i].val & bit) != 0) ^ dual);
            t = t ? s_fn2(op, t, l) : l;
            ++*nlit;
        }
        if (!t) t = mk_sym(dual ? S_FALSE : S_TRUE);
        acc = acc ? s_fn2(dual ? F_AND : F_OR, acc, t) : t;
    }
    return acc ? acc : mk_sym(dual ? S_TRUE : S_FALSE);
}

E *ap_logic(XP, int op, uint32_t n, E **a) {
    lg G = {0};
    G.xor_id = mk_sym_name("xor")->s, G.imp_id = mk_sym_name("implies")->s, G.iff_id = mk_sym_name("iff")->s;
    if (op == AP_IMPL || op == AP_IFF) {
        int t0 = ISYM(a[0], S_TRUE), f0 = ISYM(a[0], S_FALSE), t1 = ISYM(a[1], S_TRUE), f1 = ISYM(a[1], S_FALSE);
        if (op == AP_IMPL) {
            if (f0 || t1) return mk_sym(S_TRUE);
            if (t0) return a[1];
            if (f1) return s_fn1(F_NOT, a[0]);
        } else {
            if (t0) return a[1];
            if (t1) return a[0];
            if (f0) return f1 ? mk_sym(S_TRUE) : s_fn1(F_NOT, a[1]);
            if (f1) return s_fn1(F_NOT, a[0]);
            if (expr_eq(a[0], a[1])) return mk_sym(S_TRUE);
        }
        return NULL;
    }
    E *e = a[0];
    int nroot = 1, *root;
    if (op == AP_TT && e->t == E_LIST) {
        if (!e->n) cas_throw(CAS_ERR_DOMAIN, "truthtable: empty list");
        nroot = (int)e->n;
    }
    root = salloc(sizeof(int) * (size_t)nroot);
    if (op == AP_TT && n == 2) { /* the given atom order */
        E *vs = a[1]->t == E_LIST ? a[1] : s_list(1, &a[1]);
        for (uint32_t i = 0; i < vs->n; i++) atom(X, &G, EK(vs)[i]);
    }
    int fixed = G.nv;
    for (int i = 0; i < nroot; i++) root[i] = build(X, &G, nroot > 1 || e->t == E_LIST ? EK(e)[i] : e);
    if (!fixed) order(X, &G);
    int nv = G.nv, lim = op == AP_TT ? 10 : op == AP_SAT ? 24 : 12;
    if (nv > lim) cas_throw(CAS_ERR_DOMAIN, op == AP_TT ? "truthtable: at most 10 variables" : op == AP_SAT ? "satisfiable: at most 24 variables" : "at most 12 variables");
    if (op == AP_SAT) {
        uint64_t *col = salloc(sizeof(uint64_t) * (size_t)(nv + 1)), rows = (uint64_t)1 << nv;
        for (uint64_t b = 0; b * 64 < rows; b++) {
            poll_intr();
            uint64_t w = block(X, &G, root[0], b, col);
            if (!w) continue;
            if (!nv) return mk_sym(S_TRUE);
            uint64_t r = b * 64 + (uint64_t)__builtin_ctzll(w);
            E **eq = salloc(sizeof(E *) * (size_t)nv);
            for (int k = 0; k < nv; k++) eq[k] = s_fn2(F_EQ, G.var[k], mk_sym((r >> (nv - 1 - k)) & 1 ? S_TRUE : S_FALSE));
            return s_list((uint32_t)nv, eq);
        }
        return mk_sym(S_FALSE);
    }
    if (op == AP_TT) {
        uint32_t rows = 1u << nv;
        uint8_t **tt = salloc(sizeof(uint8_t *) * (size_t)nroot);
        for (int i = 0; i < nroot; i++) tt[i] = table(X, &G, root[i]);
        E **out = salloc(sizeof(E *) * (rows + 1)), **hd = salloc(sizeof(E *) * (size_t)(nv + nroot));
        for (int k = 0; k < nv; k++) hd[k] = G.var[k];
        for (int i = 0; i < nroot; i++) hd[nv + i] = e->t == E_LIST ? EK(e)[i] : e;
        out[0] = s_list((uint32_t)(nv + nroot), hd);
        for (uint32_t r = 0; r < rows; r++) {
            E **row = salloc(sizeof(E *) * (size_t)(nv + nroot));
            for (int k = 0; k < nv; k++) row[k] = (r >> (nv - 1 - k)) & 1 ? K->c1 : K->c0;
            for (int i = 0; i < nroot; i++) row[nv + i] = tt[i][r] ? K->c1 : K->c0;
            out[r + 1] = s_list((uint32_t)(nv + nroot), row);
        }
        return s_list(rows + 1, out);
    }
    uint8_t *t = table(X, &G, root[0]);
    uint32_t rows = 1u << nv, ones = 0;
    for (uint32_t r = 0; r < rows; r++) ones += t[r];
    if (ones == 0) return mk_sym(S_FALSE);
    if (ones == rows) return mk_sym(S_TRUE);
    int k, ld = 0, lc = 0;
    E *d = NULL, *c = NULL;
    if (op != AP_CNF) {
        imp *r = qm(X, t, nv, 1, &k);
        d = form(X, &G, r, k, 0, &ld);
    }
    if (op != AP_DNF) {
        imp *r = qm(X, t, nv, 0, &k);
        c = form(X, &G, r, k, 1, &lc);
    }
    if (op == AP_DNF) return d;
    if (op == AP_CNF) return c;
    return lc < ld ? c : d;
}
