/*
 * mfactor.c -- multivariate factorisation over Q by Hensel lifting, for the core's factor
 * (algorithm slot CASX_FN_PFACTOR, casx.h).  The core calls it for a squarefree primitive
 * polynomial F in x (its main variable) and z_1..z_k that is not linear in x; it returns the
 * irreducible factors, or 0 to let the core use its own methods.
 *
 * Method (Wang's evaluation-and-lifting scheme with the leading coefficient imposed on every
 * factor; Geddes/Czapor/Labahn, "Algorithms for Computer Algebra", 6.5-6.7):
 *   1. L = lc_x(F).  Points a (0 where possible, else small integers) with L(a) != 0 and
 *      F(x, a) squarefree; the univariate F(x, a) is factored over Z (the core's Zassenhaus)
 *      at up to four points and the one with the fewest factors u_1..u_r is kept (r = 1:
 *      F is irreducible).
 *   2. F' = L^(r-1) F has the factorisation prod (L / lc f_i) f_i whose factors all have the
 *      leading coefficient L; so the u_i are scaled to lc u_i = L(a) and their leading terms
 *      replaced by L(z) x^(d_i).
 *   3. With z shifted to z + a, the factors are lifted modulo the powers of the ideal (z):
 *      at total degree k, for each monomial z^m of degree k of the error F' - prod f_i, the
 *      univariate equation sum s_i prod_{j != i} u_j = c_m(x) is solved over Q (deg s_i <
 *      deg u_i, by s_i = (c_m t_i) mod u_i with t_i prod_{j != i} u_j = 1 mod u_i) and
 *      f_i += s_i z^m.  An error that vanishes means an exact factorisation; one that is still
 *      there at the total degree of F' means the univariate image had extra factors and the
 *      method gives up (0).
 *   4. The factors are shifted back and their contents in x removed; the product of their
 *      primitive parts must divide F exactly to a constant (checked).
 * Polynomials are sparse (sorted term arrays, rational coefficients) while lifting; the
 * shift by a makes them dense, so the size of the boxes is limited (MF_MAXTERMS), the point
 * with the smallest entries wins a tie (smaller coefficients after the shift), and the scratch
 * is compacted after every lifting step (compact: only Fs, the f_i, u_i and t_i survive), so a
 * lift to total degree 52 (Wester H11) runs in a fixed amount of memory.
 */
#include "casx_mod.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif

#define MV 8                /* variables */
#define MF_MAXTERMS 60000L  /* terms of a shifted polynomial */
#define MF_MAXR 24          /* univariate factors */

typedef struct {
    uint16_t e[MV];
    E *c;
} TM;
typedef struct {
    long n;
    TM *t;
} SP;
typedef struct {
    int k;        /* variables: position 0 is x */
    int var[MV];  /* kernel indices */
} VM;

/* term order: lex, position 0 first, descending */
static int tcmp(const TM *a, const TM *b, int k) {
    for (int i = 0; i < k; i++)
        if (a->e[i] != b->e[i]) return a->e[i] > b->e[i] ? -1 : 1;
    return 0;
}

static SP sp_new(XP, long n) {
    SP r;
    r.n = 0;
    r.t = salloc(sizeof(TM) * (size_t)(n ? n : 1));
    return r;
}

/* sort and combine like terms in place (merge sort into scratch) */
static SP sp_canon(XP, SP a, int k) {
    if (a.n > 1) {
        TM *w = salloc(sizeof(TM) * (size_t)a.n);
        for (long width = 1; width < a.n; width *= 2) {
            for (long lo = 0; lo < a.n; lo += 2 * width) {
                long mid = lo + width < a.n ? lo + width : a.n, hi = lo + 2 * width < a.n ? lo + 2 * width : a.n;
                long i = lo, j = mid, o = lo;
                while (i < mid && j < hi) w[o++] = tcmp(&a.t[i], &a.t[j], k) <= 0 ? a.t[i++] : a.t[j++];
                while (i < mid) w[o++] = a.t[i++];
                while (j < hi) w[o++] = a.t[j++];
            }
            TM *s = a.t;
            a.t = w, w = s;
        }
    }
    long o = 0;
    for (long i = 0; i < a.n; i++) {
        if (o && !tcmp(&a.t[o - 1], &a.t[i], k)) a.t[o - 1].c = s_add2(a.t[o - 1].c, a.t[i].c);
        else a.t[o++] = a.t[i];
    }
    long z = 0;
    for (long i = 0; i < o; i++)
        if (!is_zero(a.t[i].c)) a.t[z++] = a.t[i];
    a.n = z;
    return a;
}

static SP sp_add(XP, SP a, SP b, int sub, int k) {
    SP r = sp_new(X, a.n + b.n);
    long i = 0, j = 0;
    while (i < a.n || j < b.n) {
        int c = i == a.n ? 1 : j == b.n ? -1 : tcmp(&a.t[i], &b.t[j], k);
        if (c < 0) r.t[r.n++] = a.t[i++];
        else if (c > 0) {
            r.t[r.n] = b.t[j++];
            if (sub) r.t[r.n].c = s_neg(r.t[r.n].c);
            r.n++;
        } else {
            E *v = sub ? s_sub(a.t[i].c, b.t[j].c) : s_add2(a.t[i].c, b.t[j].c);
            if (!is_zero(v)) {
                r.t[r.n] = a.t[i];
                r.t[r.n++].c = v;
            }
            i++, j++;
        }
    }
    return r;
}

static SP sp_mul(XP, SP a, SP b, int k) {
    if ((double)a.n * (double)b.n > 4e6) cas_throw(CAS_ERR_MEMORY, "factor: too large");
    SP r = sp_new(X, a.n * b.n);
    for (long i = 0; i < a.n; i++) {
        poll_intr();
        for (long j = 0; j < b.n; j++) {
            TM *t = &r.t[r.n++];
            for (int v = 0; v < k; v++) t->e[v] = (uint16_t)(a.t[i].e[v] + b.t[j].e[v]);
            t->c = s_mul2(a.t[i].c, b.t[j].c);
        }
    }
    return sp_canon(X, r, k);
}

/* ---------------------------------------------------- conversions --- */
static int vm_pos(VM *m, int v) {
    for (int i = 0; i < m->k; i++)
        if (m->var[i] == v) return i;
    return -1;
}

static int collect(XP, P *a, VM *m) {
    stack_check();
    if (a->v < 0) return 1;
    if (vm_pos(m, a->v) < 0) {
        if (m->k == MV) return 0;
        m->var[m->k++] = a->v;
    }
    for (uint32_t j = 0; j < a->n; j++)
        if (!collect(X, a->k[j], m)) return 0;
    return 1;
}

static void p2s(XP, P *a, VM *m, uint16_t *e, SP *r) {
    stack_check();
    if (a->v < 0) {
        if (is_zero(a->c)) return;
        TM *t = &r->t[r->n++];
        for (int v = 0; v < MV; v++) t->e[v] = e[v];
        t->c = a->c;
        return;
    }
    int pos = vm_pos(m, a->v);
    for (uint32_t j = 0; j < a->n; j++) {
        e[pos] = (uint16_t)j;
        p2s(X, a->k[j], m, e, r);
    }
    e[pos] = 0;
}

static long nterms(P *a) {
    if (a->v < 0) return 1;
    long n = 0;
    for (uint32_t j = 0; j < a->n; j++) n += nterms(a->k[j]);
    return n;
}

static SP from_p(XP, P *a, VM *m) {
    SP r = sp_new(X, nterms(a));
    uint16_t e[MV] = {0};
    p2s(X, a, m, e, &r);
    return sp_canon(X, r, m->k);
}

/* terms t[lo..hi) (sorted) as a P in the variables from level lvl on (vars sorted descending) */
static P *s2p(XP, TM *t, long lo, long hi, VM *m, const int *ord, int lvl) {
    if (lo >= hi) return p_const(K->c0);
    if (lvl == m->k) {
        E *c = K->c0;
        for (long i = lo; i < hi; i++) c = s_add2(c, t[i].c);
        return p_const(c);
    }
    int pos = ord[lvl];
    uint16_t mx = 0;
    for (long i = lo; i < hi; i++)
        if (t[i].e[pos] > mx) mx = t[i].e[pos];
    P *r = p_mk(m->var[pos], (uint32_t)mx + 1);
    for (uint32_t d = 0; d <= mx; d++) {
        /* the terms with exponent d at pos, kept in order by a stable partition */
        TM *sub = salloc(sizeof(TM) * (size_t)(hi - lo));
        long n = 0;
        for (long i = lo; i < hi; i++)
            if (t[i].e[pos] == d) sub[n++] = t[i];
        r->k[d] = s2p(X, sub, 0, n, m, ord, lvl + 1);
    }
    return p_norm(r);
}

static P *to_p(XP, SP a, VM *m) {
    int ord[MV];
    for (int i = 0; i < m->k; i++) ord[i] = i;
    for (int i = 0; i < m->k; i++) /* descending kernel index = recursion order of P */
        for (int j = i + 1; j < m->k; j++)
            if (m->var[ord[j]] > m->var[ord[i]]) {
                int t = ord[i];
                ord[i] = ord[j], ord[j] = t;
            }
    return s2p(X, a.t, 0, a.n, m, ord, 0);
}

/* univariate (only position 0 used) <-> UP */
static UP sp_to_up(XP, SP a) {
    long d = 0;
    for (long i = 0; i < a.n; i++)
        if (a.t[i].e[0] > d) d = a.t[i].e[0];
    UP u;
    u.d = d, u.c = salloc(sizeof(E *) * (size_t)(d + 1));
    for (long i = 0; i <= d; i++) u.c[i] = K->c0;
    for (long i = 0; i < a.n; i++) u.c[a.t[i].e[0]] = s_add2(u.c[a.t[i].e[0]], a.t[i].c);
    while (u.d >= 0 && is_zero(u.c[u.d])) u.d--;
    return u;
}

/* the UP u times z^m (exponents of positions 1..k-1 from m) */
static SP up_to_sp(XP, UP u, const uint16_t *m, int k) {
    SP r = sp_new(X, u.d + 1);
    for (long i = u.d; i >= 0; i--) {
        if (is_zero(u.c[i])) continue;
        TM *t = &r.t[r.n++];
        for (int v = 0; v < MV; v++) t->e[v] = v < k && v ? m[v] : 0;
        t->e[0] = (uint16_t)i;
        t->c = u.c[i];
    }
    return r;
}

/* a with z_j -> z_j + s_j (positions 1..k-1): binomial expansion term by term */
static SP sp_shift(XP, SP a, const long *s, int k) {
    for (int v = 1; v < k; v++) {
        if (!s[v]) continue;
        long n = 0;
        for (long i = 0; i < a.n; i++) n += a.t[i].e[v] + 1;
        if (n > MF_MAXTERMS * 4) cas_throw(CAS_ERR_MEMORY, "factor: too large");
        SP r = sp_new(X, n);
        for (long i = 0; i < a.n; i++) {
            long d = a.t[i].e[v];
            E *bin = K->c1, *pw = s_powi(mk_int(s[v]), d);
            for (long j = 0; j <= d; j++) { /* C(d, j) s^(d-j) z^j */
                TM *t = &r.t[r.n++];
                *t = a.t[i];
                t->e[v] = (uint16_t)j;
                t->c = s_mul(3, (E *[]){a.t[i].c, bin, pw});
                bin = s_div(s_mul2(bin, mk_int(d - j)), mk_int(j + 1));
                if (j < d) pw = s_div(pw, mk_int(s[v]));
            }
        }
        a = sp_canon(X, r, k);
        if (a.n > MF_MAXTERMS) cas_throw(CAS_ERR_MEMORY, "factor: too large");
    }
    return a;
}

static long zdeg(const TM *t, int k) {
    long d = 0;
    for (int v = 1; v < k; v++) d += t->e[v];
    return d;
}

/* ------------------------------------------------------- lifting --- */
/* Compaction between lifting steps: the factors fi[0..r) (term arrays and their exact number
 * coefficients) are copied to the top of scratch, slid down to the mark m0 and their pointers
 * adjusted, so every step frees the products and errors of the step before (the scratch is a
 * bump allocator and an extension has no skeep). */
static E *num_copy(XP, E *c, char *lo, char *hi) {
    if ((char *)c < lo || (char *)c >= hi) return c; /* permanent constants, older scratch */
    if (c->t == E_RAT) {
        E *n = num_copy(X, K0(c), lo, hi), *d = num_copy(X, K1(c), lo, hi);
        E *r = salloc(sizeof(E) + 2 * sizeof(E *));
        *r = *c;
        K0(r) = n, K1(r) = d;
        return r;
    }
    size_t pay = c->t == E_INT ? c->n * sizeof(limb) : c->t == E_FLT ? sizeof(double) + 8 + c->n * sizeof(limb) : 0;
    if (pay < sizeof(void *)) pay = sizeof(void *);
    E *r = salloc(sizeof(E) + pay);
    char *dst = (char *)r;
    const char *src = (const char *)c;
    for (size_t i = 0; i < sizeof(E) + pay; i++) dst[i] = src[i];
    return r;
}

static int inb(const void *p, const char *a, const char *b) { return (const char *)p >= a && (const char *)p < b; }

/* keep sps[0..nsp) and ups[0..nup) (with their coefficients), drop everything else above m0 */
static void compact(XP, char *m0, SP *sps, int nsp, UP *ups, int nup) {
    char *hi = K->s_top;
    char *start = salloc(8);
    for (int i = 0; i < nsp; i++) {
        TM *t = salloc(sizeof(TM) * (size_t)(sps[i].n ? sps[i].n : 1));
        for (long j = 0; j < sps[i].n; j++) {
            t[j] = sps[i].t[j];
            t[j].c = num_copy(X, sps[i].t[j].c, m0, hi);
        }
        sps[i].t = t;
    }
    for (int i = 0; i < nup; i++) {
        E **c = salloc(sizeof(E *) * (size_t)(ups[i].d + 1));
        for (long j = 0; j <= ups[i].d; j++) c[j] = num_copy(X, ups[i].c[j], m0, hi);
        ups[i].c = c;
    }
    char *end = K->s_top;
    size_t delta = (size_t)(start - m0), len = (size_t)(end - start);
#define FIXP(p) do { if (inb((p), start, end)) (p) = (void *)((char *)(p) - delta); } while (0)
    /* every copied node and array lies in [start, end): fix the pointers, then slide down */
    for (int i = 0; i < nsp; i++) {
        for (long j = 0; j < sps[i].n; j++) {
            E *c = sps[i].t[j].c;
            if (inb(c, start, end) && c->t == E_RAT) {
                FIXP(K0(c));
                FIXP(K1(c));
            }
            FIXP(sps[i].t[j].c);
        }
        FIXP(sps[i].t);
    }
    for (int i = 0; i < nup; i++) {
        for (long j = 0; j <= ups[i].d; j++) {
            E *c = ups[i].c[j];
            if (inb(c, start, end) && c->t == E_RAT) {
                FIXP(K0(c));
                FIXP(K1(c));
            }
            FIXP(ups[i].c[j]);
        }
        FIXP(ups[i].c);
    }
#undef FIXP
    for (size_t i = 0; i < len; i++) m0[i] = start[i];
    K->s_top = m0 + len;
}

static uint32_t mfactor(XP, P *F, P **out, uint32_t cap) {
    VM m;
    m.k = 1, m.var[0] = F->v;
    if (!collect(X, F, &m) || m.k < 2) return 0;
    int k = m.k;
    long n = (long)F->n - 1;
    if (n < 2 || n > 200) return 0;
    SP f = from_p(X, F, &m);
    /* L = lc_x(F) */
    SP L = sp_new(X, f.n);
    for (long i = 0; i < f.n; i++)
        if (f.t[i].e[0] == n) {
            L.t[L.n] = f.t[i];
            L.t[L.n++].e[0] = 0;
        }
    /* evaluation points: 0, then small integers */
    static const signed char cand[] = {0, 1, -1, 2, -2, 3, -3, 4, -4, 5};
    long best[MV] = {0}, a[MV] = {0};
    int bestr = 1 << 30, tries = 0;
    UP bu[MF_MAXR];
    uint32_t seed = 12345;
    for (int att = 0; att < 40 && tries < 6; att++) {
        a[0] = 0;
        for (int v = 1; v < k; v++) {
            seed = seed * 1103515245u + 12345u;
            a[v] = att == 0 ? 0 : cand[1 + (seed >> 16) % 9];
        }
        /* L(a) != 0 */
        E *la = K->c0;
        for (long i = 0; i < L.n; i++) {
            E *t = L.t[i].c;
            for (int v = 1; v < k; v++) t = s_mul2(t, s_powi(mk_int(a[v]), L.t[i].e[v]));
            la = s_add2(la, t);
        }
        if (is_zero(la)) continue;
        P *g = F;
        for (int v = 1; v < k; v++) g = p_eval(g, m.var[v], mk_int(a[v]));
        if (g->v != F->v || (long)g->n - 1 != n) continue;
        P *fs[MF_MAXR + 1];
        uint32_t ml[MF_MAXR + 1];
        uint32_t r = p_factor(g, fs, ml, MF_MAXR + 1);
        int sqf = r >= 1 && r <= MF_MAXR;
        long dsum = 0;
        for (uint32_t i = 0; i < r && sqf; i++) sqf = ml[i] == 1 && fs[i]->v == F->v, dsum += (long)fs[i]->n - 1;
        if (!sqf || dsum != n) continue; /* not squarefree there (or a content) */
        tries++;
        long mag = 0, bmag = 0; /* on a tie the smaller point: smaller coefficients after the shift */
        for (int v = 1; v < k; v++) mag += a[v] < 0 ? -a[v] : a[v], bmag += best[v] < 0 ? -best[v] : best[v];
        if ((int)r < bestr || ((int)r == bestr && mag < bmag)) {
            bestr = (int)r;
            for (int v = 0; v < k; v++) best[v] = a[v];
            VM m1;
            m1.k = 1, m1.var[0] = F->v;
            for (uint32_t i = 0; i < r; i++) bu[i] = sp_to_up(X, from_p(X, fs[i], &m1));
        }
        if (bestr == 1) break;
    }
    if (bestr == 1 << 30) return 0;
    if (bestr == 1) {
        out[0] = F;
        return 1;
    }
    int r = bestr;
    if ((uint32_t)r > cap) return 0;
    /* F' = L^(r-1) F, shifted */
    char *m0 = K->s_top; /* the lifting keeps Fs, fi, u and tinv above it (compact) */
    SP Fp = f;
    for (int i = 1; i < r; i++) Fp = sp_mul(X, Fp, L, k);
    SP Fs = sp_shift(X, Fp, best, k), Ls = sp_shift(X, L, best, k);
    E *La = K->c0; /* L(a) = the constant term of the shifted L */
    for (long i = 0; i < Ls.n; i++)
        if (!zdeg(&Ls.t[i], k)) La = s_add2(La, Ls.t[i].c);
    /* u_i scaled to lc L(a); f_i = L(z) x^d_i + the rest of u_i */
    UP u[MF_MAXR];
    SP fi[MF_MAXR];
    for (int i = 0; i < r; i++) {
        u[i] = up_scale(bu[i], s_div(La, bu[i].c[bu[i].d]));
        UP low = u[i];
        low.d = u[i].d - 1;
        uint16_t z0[MV] = {0};
        SP lead = Ls;
        SP l2 = sp_new(X, lead.n);
        for (long j = 0; j < lead.n; j++) {
            l2.t[l2.n] = lead.t[j];
            l2.t[l2.n++].e[0] = (uint16_t)u[i].d;
        }
        fi[i] = sp_add(X, l2, up_to_sp(X, low, z0, k), 0, k);
        fi[i] = sp_canon(X, fi[i], k);
    }
    /* t_i: t_i * prod_{j != i} u_j = 1 mod u_i */
    UP tinv[MF_MAXR];
    for (int i = 0; i < r; i++) {
        UP b;
        b.d = 0, b.c = salloc(sizeof(E *));
        b.c[0] = K->c1;
        for (int j = 0; j < r; j++)
            if (j != i) b = up_mul(b, u[j]);
        UP s, t, g = up_egcd(b, u[i], &s, &t);
        if (g.d != 0) return 0; /* not coprime: should not happen for a squarefree image */
        tinv[i] = up_scale(s, s_inv(g.c[0]));
    }
    long D = 0;
    for (long i = 0; i < Fs.n; i++)
        if (zdeg(&Fs.t[i], k) > D) D = zdeg(&Fs.t[i], k);
    SP keep[MF_MAXR + 1];
    UP ukeep[2 * MF_MAXR];
    for (long deg = 1; deg <= D + 1; deg++) {
        poll_intr();
        for (int i = 0; i < r; i++) keep[i] = fi[i], ukeep[i] = u[i], ukeep[r + i] = tinv[i];
        keep[r] = Fs;
        compact(X, m0, keep, r + 1, ukeep, 2 * r);
        for (int i = 0; i < r; i++) fi[i] = keep[i], u[i] = ukeep[i], tinv[i] = ukeep[r + i];
        Fs = keep[r];
        SP pr = fi[0];
        for (int i = 1; i < r; i++) pr = sp_mul(X, pr, fi[i], k);
        SP e = sp_add(X, Fs, pr, 1, k);
        if (!e.n) break;
        if (deg > D) return 0; /* extra univariate factors: give up */
        /* group the degree-deg terms of e by their z-monomial */
        long lowdeg = 1L << 30;
        for (long i = 0; i < e.n; i++) {
            long zd = zdeg(&e.t[i], k);
            if (zd < lowdeg) lowdeg = zd;
        }
        if (lowdeg < deg) return 0; /* inconsistent */
        char *used = salloc((size_t)e.n);
        for (long i = 0; i < e.n; i++) used[i] = 0;
        for (long i = 0; i < e.n; i++) {
            if (used[i] || zdeg(&e.t[i], k) != deg) continue;
            /* c_m(x): all terms with the z-exponents of term i */
            SP c = sp_new(X, e.n - i);
            for (long j = i; j < e.n; j++) {
                int same = !used[j];
                for (int v = 1; v < k && same; v++) same = e.t[j].e[v] == e.t[i].e[v];
                if (!same) continue;
                used[j] = 1;
                c.t[c.n++] = e.t[j];
            }
            UP cu = sp_to_up(X, c);
            for (int q = 0; q < r; q++) {
                UP s, rem;
                up_divrem(up_mul(tinv[q], cu), u[q], &s, &rem);
                fi[q] = sp_add(X, fi[q], up_to_sp(X, rem, e.t[i].e, k), 0, k);
            }
        }
    }
    /* back: shift by -a, primitive parts, check */
    long nb[MV];
    for (int v = 0; v < k; v++) nb[v] = -best[v];
    P *rest = F;
    for (int i = 0; i < r; i++) {
        P *h = to_p(X, sp_shift(X, fi[i], nb, k), &m);
        P *c = p_content(h);
        if (c->v >= 0 || !is_zero(c->c)) h = p_divexact(h, c);
        if (!h) return 0;
        E *ic;
        h = p_intprim(h, &ic);
        rest = p_divexact(rest, h);
        if (!rest) return 0;
        out[i] = h;
    }
    if (rest->v >= 0) return 0;
    return (uint32_t)r;
}

static uint32_t pfactor_slot(XP, P *f, P **facs, uint32_t cap) {
    char *mk = K->s_top;
    uint32_t n = mfactor(X, f, facs, cap);
    if (!n) K->s_top = mk;
    return n;
}

int casx_mfactor_register(XP) { return X->reg_fn(CASX_FN_PFACTOR, (void *)pfactor_slot); }
