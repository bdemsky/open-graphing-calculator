/*
 * mgcd.c -- modular multivariate polynomial gcd for the core's p_gcd (algorithm slot
 * CASX_FN_PGCD, casx.h): Brown's dense modular algorithm (W. S. Brown, "On Euclid's
 * algorithm and the computation of polynomial greatest common divisors", JACM 18,
 * 1971; Geddes/Czapor/Labahn, "Algorithms for Computer Algebra", Alg. 7.1 MGCD and
 * 7.2 PGCD).  It replaces the primitive remainder sequence, whose coefficients grow
 * without bound on the sparse problems of Wester's H section.
 *
 * Over GF(p) (pgcd): the polynomials are dense arrays in k variables (variable 0 the
 * main one, the last one with stride 1).  With y the last variable: contents in y are
 * divided out, g = gcd of the leading coefficients (in the other variables, lex) as
 * polynomials in y, and for points y = a with g(a) != 0 the gcd of the images (k - 1
 * variables, recursively) is scaled to leading coefficient g(a) and interpolated in y
 * (Newton).  An image whose leading monomial is larger than the others' is unlucky and
 * skipped; a smaller one restarts the interpolation.  deg g + min(deg_y) + 1 points
 * determine the result; its content in y is removed and the gcd of the contents put back.
 * Over Z (the slot function): images modulo primes just below 2^31 not dividing the
 * leading numeric coefficients, scaled to gcd(lc A, lc B), combined by Chinese
 * remaindering in the symmetric range; when a new prime leaves the combination unchanged
 * its primitive part is checked by exact division of A and B (so a result is always a
 * true gcd).  Larger dense sizes than MG_MAXSIZE, more than MG_MAXV variables or
 * failures return NULL and the core uses its remainder sequence.
 */
#include "casx_mod.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif

#define MG_MAXV 8
#define MG_MAXSIZE 2000000L

typedef uint32_t u32;
typedef uint64_t u64;

static u32 mulm(u32 a, u32 b, u32 p) { return (u32)((u64)a * b % p); }
static u32 addm(u32 a, u32 b, u32 p) {
    u32 s = a + b;
    return s >= p || s < a ? s - p : s;
}
static u32 subm(u32 a, u32 b, u32 p) { return a >= b ? a - b : a + (p - b); }
static u32 powm(u32 a, u32 e, u32 p) {
    u32 r = 1;
    while (e) {
        if (e & 1) r = mulm(r, a, p);
        a = mulm(a, a, p), e >>= 1;
    }
    return r;
}
static u32 invm(u32 a, u32 p) { return powm(a, p - 2, p); }

/* ------------------------------------------------------- univariate --- */
static long udeg(const u32 *a, long d) {
    while (d >= 0 && !a[d]) d--;
    return d;
}

/* monic gcd of a (deg da) and b (deg db) into a fresh array; its degree */
static long ugcd(XP, const u32 *a, long da, const u32 *b, long db, u32 p, u32 **out) {
    da = udeg(a, da), db = udeg(b, db);
    long n = (da > db ? da : db) + 1;
    u32 *x = salloc(sizeof(u32) * (size_t)(n + 1)), *y = salloc(sizeof(u32) * (size_t)(n + 1));
    for (long i = 0; i <= da; i++) x[i] = a[i];
    for (long i = 0; i <= db; i++) y[i] = b[i];
    long dx = da, dy = db;
    while (dy >= 0) {
        u32 il = invm(y[dy], p);
        while (dx >= dy) {
            u32 q = mulm(x[dx], il, p);
            for (long i = 0; i <= dy; i++) x[dx - dy + i] = subm(x[dx - dy + i], mulm(q, y[i], p), p);
            dx = udeg(x, dx - 1);
        }
        u32 *t = x;
        x = y, y = t;
        long td = dx;
        dx = dy, dy = td;
    }
    if (dx >= 0) {
        u32 il = invm(x[dx], p);
        for (long i = 0; i <= dx; i++) x[i] = mulm(x[i], il, p);
    }
    *out = x;
    return dx;
}

/* a / b exactly (b != 0); quotient degree da - db, written to q */
static void udiv(const u32 *a, long da, const u32 *b, long db, u32 p, u32 *q, u32 *w) {
    da = udeg(a, da), db = udeg(b, db);
    for (long i = 0; i <= da; i++) w[i] = a[i];
    u32 il = invm(b[db], p);
    for (long i = da - db; i >= 0; i--) {
        u32 c = mulm(w[i + db], il, p);
        q[i] = c;
        if (c)
            for (long j = 0; j <= db; j++) w[i + j] = subm(w[i + j], mulm(c, b[j], p), p);
    }
}

static u32 ueval(const u32 *a, long d, u32 x, u32 p) {
    u32 r = 0;
    for (long i = d; i >= 0; i--) r = addm(mulm(r, x, p), a[i], p);
    return r;
}

/* ------------------------------------------------- dense arrays mod p --- */
/* k variables, degree bounds d[0..k), strides: the last variable varies fastest */
typedef struct {
    int k;
    long d[MG_MAXV], size;
    u32 *c;
} DA;

static long da_size(int k, const long *d) {
    long s = 1;
    for (int i = 0; i < k; i++) {
        s *= d[i] + 1;
        if (s > MG_MAXSIZE) return -1;
    }
    return s;
}

static int da_new(XP, DA *a, int k, const long *d) {
    a->k = k;
    for (int i = 0; i < k; i++) a->d[i] = d[i];
    a->size = da_size(k, d);
    if (a->size < 0) return 0;
    a->c = salloc(sizeof(u32) * (size_t)a->size);
    for (long i = 0; i < a->size; i++) a->c[i] = 0;
    return 1;
}

/* the index of the lex-largest nonzero coefficient (-1: zero) */
static long da_lead(DA *a) {
    for (long i = a->size - 1; i >= 0; i--)
        if (a->c[i]) return i;
    return -1;
}

/* exponents of flat index i */
static void da_exps(DA *a, long i, long *e) {
    for (int v = a->k - 1; v >= 0; v--) e[v] = i % (a->d[v] + 1), i /= a->d[v] + 1;
}

/* a repacked with the tightest degree bounds */
static void da_trim(XP, DA *a) {
    long hi[MG_MAXV] = {0}, e[MG_MAXV];
    int any = 0;
    for (long i = 0; i < a->size; i++)
        if (a->c[i]) {
            any = 1;
            da_exps(a, i, e);
            for (int v = 0; v < a->k; v++)
                if (e[v] > hi[v]) hi[v] = e[v];
        }
    int same = 1;
    for (int v = 0; v < a->k; v++) same &= hi[v] == a->d[v];
    if (same || !any) {
        if (!any)
            for (int v = 0; v < a->k; v++) a->d[v] = 0;
        if (!any) a->size = 1, a->c[0] = 0;
        return;
    }
    DA b;
    da_new(X, &b, a->k, hi);
    for (long i = 0; i < a->size; i++)
        if (a->c[i]) {
            da_exps(a, i, e);
            long j = 0;
            for (int v = 0; v < a->k; v++) j = j * (hi[v] + 1) + e[v];
            b.c[j] = a->c[i];
        }
    *a = b;
}

/* a embedded in bounds d (d >= a's) */
static void da_widen(XP, DA *a, const long *d) {
    int same = 1;
    for (int v = 0; v < a->k; v++) same &= d[v] == a->d[v];
    if (same) return;
    DA b;
    long e[MG_MAXV];
    da_new(X, &b, a->k, d);
    for (long i = 0; i < a->size; i++)
        if (a->c[i]) {
            da_exps(a, i, e);
            long j = 0;
            for (int v = 0; v < a->k; v++) j = j * (d[v] + 1) + e[v];
            b.c[j] = a->c[i];
        }
    *a = b;
}

/* Scratch reuse: everything allocated after the mark mk is released except the n words at c,
 * which move down to mk (the bump allocator keeps 8-byte alignment, mk is aligned) */
static u32 *keep_down(XP, char *mk, u32 *c, long n) {
    u32 *dst = (u32 *)(void *)mk;
    for (long i = 0; i < n; i++) dst[i] = c[i];
    K->s_top = mk + (((size_t)n * sizeof(u32) + 7) & ~(size_t)7);
    return dst;
}

/* a with the last variable set to x (k - 1 variables) */
static void da_eval(XP, DA *a, u32 x, u32 p, DA *r) {
    long n = a->d[a->k - 1];
    da_new(X, r, a->k - 1, a->d);
    for (long j = 0; j < r->size; j++) r->c[j] = ueval(a->c + j * (n + 1), n, x, p);
}

/* gcd mod p of a and b (trimmed copies are made); 0 on failure */
/* budget: element operations left for this gcd (the call fails when they run out) */
static int pgcd(XP, DA *a0, DA *b0, u32 p, DA *g, long *budget) {
    poll_intr();
    *budget -= a0->size + b0->size;
    if (*budget < 0) return 0;
    DA a = *a0, b = *b0;
    da_trim(X, &a), da_trim(X, &b);
    int k = a.k;
    if (da_lead(&a) < 0 || da_lead(&b) < 0) return 0;
    if (k == 1) {
        u32 *u;
        long d = ugcd(X, a.c, a.d[0], b.c, b.d[0], p, &u);
        long dd[1] = {d};
        da_new(X, g, 1, dd);
        for (long i = 0; i <= d; i++) g->c[i] = u[i];
        return 1;
    }
    long ny = a.d[k - 1] > b.d[k - 1] ? a.d[k - 1] : b.d[k - 1];
    u32 *w = salloc(sizeof(u32) * (size_t)(ny + 2));
    /* contents in y */
    DA *ab[2] = {&a, &b};
    u32 *cont[2];
    long cdeg[2];
    for (int s = 0; s < 2; s++) {
        DA *x = ab[s];
        long n = x->d[k - 1], m = x->size / (n + 1), cd = -1;
        u32 *c = NULL;
        for (long j = 0; j < m && cd != 0; j++) {
            u32 *sl = x->c + j * (n + 1);
            if (udeg(sl, n) < 0) continue;
            if (!c) {
                u32 *t;
                cd = ugcd(X, sl, n, sl, n, p, &t);
                c = t;
            } else
                cd = ugcd(X, c, cd, sl, n, p, &c);
        }
        cont[s] = c, cdeg[s] = cd;
        if (cd > 0) { /* divide every slice by the content */
            DA y = *x;
            y.c = salloc(sizeof(u32) * (size_t)x->size);
            u32 *q = salloc(sizeof(u32) * (size_t)(n + 1));
            for (long j = 0; j < m; j++) {
                u32 *sl = x->c + j * (n + 1), *o = y.c + j * (n + 1);
                for (long i = 0; i <= n; i++) o[i] = 0;
                if (udeg(sl, n) < 0) continue;
                udiv(sl, n, c, cd, p, q, w);
                for (long i = 0; i <= udeg(sl, n) - cd; i++) o[i] = q[i];
            }
            *x = y;
            da_trim(X, x);
        }
    }
    u32 *cg;
    long cgd = ugcd(X, cont[0], cdeg[0], cont[1], cdeg[1], p, &cg);
    k = a.k;
    if (b.k != k) return 0;
    /* leading coefficients (polynomials in y) and their gcd */
    long na = a.d[k - 1], nb = b.d[k - 1];
    u32 *la = a.c + (da_lead(&a) / (na + 1)) * (na + 1), *lb = b.c + (da_lead(&b) / (nb + 1)) * (nb + 1);
    u32 *lg;
    long lgd = ugcd(X, la, na, lb, nb, p, &lg);
    /* points that determine the scaled gcd in y: deg lg + a bound on deg_y of the gcd, the
     * degree of the gcd of the images in y at a point of the other variables where both
     * degrees in y are kept (else min(deg_y a, deg_y b)) */
    long dy = na < nb ? na : nb;
    {
        u32 pt[MG_MAXV], *ia = salloc(sizeof(u32) * (size_t)(na + 1)), *ib = salloc(sizeof(u32) * (size_t)(nb + 1));
        for (int v = 0; v < k - 1; v++) pt[v] = 3 + 7u * (u32)v;
        DA *ab2[2] = {&a, &b};
        u32 *im[2] = {ia, ib};
        for (int s = 0; s < 2; s++) {
            DA *x = ab2[s];
            long n = x->d[k - 1], e[MG_MAXV];
            for (long i = 0; i <= n; i++) im[s][i] = 0;
            for (long j = 0; j < x->size / (n + 1); j++) {
                u32 *sl = x->c + j * (n + 1), w = 1;
                if (udeg(sl, n) < 0) continue;
                da_exps(x, j * (n + 1), e);
                for (int v = 0; v < k - 1; v++) w = mulm(w, powm(pt[v], (u32)e[v], p), p);
                for (long i = 0; i <= n; i++) im[s][i] = addm(im[s][i], mulm(w, sl[i], p), p);
            }
        }
        if (udeg(ia, na) == na && udeg(ib, nb) == nb) {
            u32 *t;
            long gd = ugcd(X, ia, na, ib, nb, p, &t);
            if (gd >= 0 && gd < dy) dy = gd;
        }
    }
    long need = lgd + dy + 1;
    DA H;
    H.k = 0;
    long hlead = -1, npts = 0;
    u32 *q = salloc(sizeof(u32) * (size_t)(need + 2)), *pts = salloc(sizeof(u32) * (size_t)(need + 2));
    long qd = 0;
    q[0] = 1;
    for (u32 x = 1; npts < need; x++) {
        if (x > (u32)(3 * need + 40)) return 0; /* too many unlucky points: give up (the caller falls back) */
        u32 gx = ueval(lg, lgd, x, p);
        if (!gx || !ueval(la, na, x, p) || !ueval(lb, nb, x, p)) continue;
        char *mk = K->s_top; /* the images of this point are released at its end */
        DA ax, bx, cx;
        da_eval(X, &a, x, p, &ax), da_eval(X, &b, x, p, &bx);
        if (!pgcd(X, &ax, &bx, p, &cx, budget)) return 0;
        long cl = da_lead(&cx);
        if (cl < 0) return 0;
        u32 sc = mulm(gx, invm(cx.c[cl], p), p);
        for (long i = 0; i < cx.size; i++) cx.c[i] = mulm(cx.c[i], sc, p);
        /* compare leading monomials (lex on exponent vectors) */
        int cmp = 0;
        if (hlead >= 0) {
            long e1[MG_MAXV], e2[MG_MAXV];
            DA hv = H;
            hv.k = k - 1;
            da_exps(&cx, cl, e1);
            da_exps(&hv, hlead, e2);
            for (int v = 0; v < k - 1 && !cmp; v++) cmp = e1[v] < e2[v] ? -1 : e1[v] > e2[v];
        }
        if (cmp > 0) { /* unlucky */
            K->s_top = mk;
            continue;
        }
        if (hlead < 0 || cmp < 0) { /* (re)start; hlead indexes the k-1 variable layout of H's prefixes */
            long d[MG_MAXV];
            for (int v = 0; v < k - 1; v++) d[v] = cx.d[v];
            d[k - 1] = need;
            if (!da_new(X, &H, k, d)) return 0;
            for (long j = 0; j < cx.size; j++) H.c[j * (need + 1)] = cx.c[j];
            hlead = cl, npts = 1, pts[0] = x;
            q[0] = p - x, q[1] = 1, qd = 1;
            H.c = keep_down(X, mk, H.c, H.size);
            continue;
        }
        /* equal: widen to common bounds, Newton step H += (C - H(x)) q / q(x) */
        long d[MG_MAXV];
        int grow = 0;
        for (int v = 0; v < k - 1; v++) {
            d[v] = cx.d[v] > H.d[v] ? cx.d[v] : H.d[v];
            grow |= d[v] != H.d[v];
        }
        d[k - 1] = need;
        da_widen(X, &cx, d);
        if (grow) { /* H in the wider layout; the lead index again (cx has the same lead) */
            da_widen(X, &H, d);
            hlead = da_lead(&cx);
        }
        u32 iq = invm(ueval(q, qd, x, p), p);
        for (long j = 0; j < cx.size; j++) {
            u32 *h = H.c + j * (need + 1);
            u32 dlt = mulm(subm(cx.c[j], ueval(h, npts - 1, x, p), p), iq, p);
            if (dlt)
                for (long i = 0; i <= qd; i++) h[i] = addm(h[i], mulm(dlt, q[i], p), p);
        }
        /* q *= (y - x) */
        q[qd + 1] = 0;
        for (long i = qd + 1; i >= 1; i--) q[i] = subm(q[i - 1], mulm(x, q[i], p), p);
        q[0] = mulm(p - x, q[0], p);
        qd++;
        pts[npts++] = x;
        if (grow) H.c = keep_down(X, mk, H.c, H.size);
        else K->s_top = mk;
    }
    /* H's content in y removed, the content gcd put back */
    da_trim(X, &H);
    long n = H.d[k - 1], m = H.size / (n + 1), hd = -1;
    u32 *hc = NULL;
    for (long j = 0; j < m && hd != 0; j++) {
        u32 *sl = H.c + j * (n + 1);
        if (udeg(sl, n) < 0) continue;
        if (!hc) {
            u32 *t;
            hd = ugcd(X, sl, n, sl, n, p, &t);
            hc = t;
        } else
            hd = ugcd(X, hc, hd, sl, n, p, &hc);
    }
    long d[MG_MAXV];
    for (int v = 0; v < k - 1; v++) d[v] = H.d[v];
    d[k - 1] = n - (hd > 0 ? hd : 0) + cgd;
    if (!da_new(X, g, k, d)) return 0;
    u32 *qq = salloc(sizeof(u32) * (size_t)(n + 1)), *w2 = salloc(sizeof(u32) * (size_t)(n + 1));
    long nd = d[k - 1];
    for (long j = 0; j < m; j++) {
        u32 *sl = H.c + j * (n + 1), *o = g->c + j * (nd + 1);
        if (udeg(sl, n) < 0) continue;
        long dq = n;
        if (hd > 0) {
            udiv(sl, n, hc, hd, p, qq, w2);
            dq = udeg(sl, n) - hd;
        } else
            for (long i = 0; i <= n; i++) qq[i] = sl[i];
        for (long i = 0; i <= dq; i++) /* times the content gcd */
            if (qq[i])
                for (long t = 0; t <= cgd; t++) o[i + t] = addm(o[i + t], mulm(qq[i], cg[t], p), p);
    }
    da_trim(X, g);
    return 1;
}

/* ------------------------------------------------------- over Z ------- */
typedef struct {
    int k, var[MG_MAXV];
    long d[MG_MAXV];
} vmap;

static int vm_pos(vmap *m, int v) {
    for (int i = 0; i < m->k; i++)
        if (m->var[i] == v) return i;
    return -1;
}

/* the variables of a, with degree bounds */
static int collect(XP, P *a, vmap *m) {
    stack_check();
    if (a->v < 0) return 1;
    int i = vm_pos(m, a->v);
    if (i < 0) {
        if (m->k == MG_MAXV) return 0;
        i = m->k++;
        m->var[i] = a->v, m->d[i] = 0;
    }
    if ((long)a->n - 1 > m->d[i]) m->d[i] = (long)a->n - 1;
    for (uint32_t j = 0; j < a->n; j++)
        if (!collect(X, a->k[j], m)) return 0;
    return 1;
}

/* a mod p into the dense array (index base for the exponents so far) */
static int to_da(XP, P *a, vmap *m, DA *r, long idx, int lvl, u32 p, E *pe) {
    if (a->v < 0) {
        long v;
        if (a->c->t != E_INT) return 0;
        E *t = int_mod(a->c, pe);
        if (!num_is_small_int(t, &v)) return 0;
        if (v < 0) v += p;
        /* the remaining levels have exponent 0 */
        for (int l = lvl; l < m->k; l++) idx = idx * (r->d[l] + 1);
        r->c[idx] = (u32)v;
        return 1;
    }
    int pos = vm_pos(m, a->v);
    for (; lvl < pos; lvl++) idx = idx * (r->d[lvl] + 1);
    for (uint32_t j = 0; j < a->n; j++)
        if (!to_da(X, a->k[j], m, r, idx * (r->d[pos] + 1) + j, pos + 1, p, pe)) return 0;
    return 1;
}

/* the polynomial of the integer coefficients c (dense layout d) */
/* (zero blocks share the constant z; the dense layout is mostly zeros for sparse results) */
static P *from_ints(XP, vmap *m, const long *d, E **c, long idx, int lvl, P *z) {
    if (lvl == m->k) return is_zero(c[idx]) ? z : p_const(c[idx]);
    long span = 1;
    for (int l = lvl; l < m->k; l++) span *= d[l] + 1;
    int any = 0;
    for (long i = idx * span; i < (idx + 1) * span && !any; i++) any = !is_zero(c[i]);
    if (!any) return z;
    P *r = p_mk(m->var[lvl], (uint32_t)(d[lvl] + 1));
    for (long j = 0; j <= d[lvl]; j++) r->k[j] = from_ints(X, m, d, c, idx * (d[lvl] + 1) + j, lvl + 1, z);
    return p_norm(r);
}

static int coprime_mod(XP, P *a, P *b, E *pe);

/* the next prime below pe: from a table of the largest primes below 2^31 (a primality test
 * per candidate made small gcds slow), then by search */
static const u32 mg_primes[] = {
    2147483629u, 2147483587u, 2147483579u, 2147483563u, 2147483549u, 2147483543u, 2147483497u, 2147483489u,
    2147483477u, 2147483423u, 2147483399u, 2147483353u, 2147483323u, 2147483269u, 2147483249u, 2147483237u,
    2147483179u, 2147483171u, 2147483137u, 2147483123u, 2147483077u, 2147483069u, 2147483059u, 2147483053u,
    2147483033u, 2147483029u, 2147482951u, 2147482949u, 2147482943u, 2147482937u, 2147482921u, 2147482877u,
    2147482873u, 2147482867u, 2147482859u, 2147482819u, 2147482817u, 2147482811u, 2147482801u, 2147482763u,
    2147482739u, 2147482697u, 2147482693u, 2147482681u, 2147482663u, 2147482661u, 2147482621u, 2147482591u};

static E *next_prime(XP, E *pe) {
    long v;
    num_is_small_int(pe, &v);
    for (unsigned i = 0; i < sizeof mg_primes / sizeof mg_primes[0]; i++)
        if ((long)mg_primes[i] < v) return mk_int((long)mg_primes[i]);
    do pe = int_sub(pe, K->c1);
    while (!int_isprime(pe));
    return pe;
}

/* a mod p with every variable but the one at position u set to pt[], as a polynomial in it
 * (added into out[0..du]); 0 if a coefficient is not an integer */
static int uimg(XP, P *a, vmap *m, int u, const u32 *pt, u32 mul, u32 p, E *pe, u32 *out, long du) {
    stack_check();
    if (a->v < 0) {
        long v;
        if (a->c->t != E_INT) return 0;
        if (!num_is_small_int(a->c, &v) || v <= -(long)p || v >= (long)p) num_is_small_int(int_mod(a->c, pe), &v);
        v %= (long)p;
        if (v < 0) v += p;
        out[0] = addm(out[0], mulm(mul, (u32)v, p), p);
        return 1;
    }
    int pos = vm_pos(m, a->v);
    for (uint32_t k = 0; k < a->n; k++) {
        if (a->k[k]->v < 0 && is_zero(a->k[k]->c)) continue;
        if (pos == u) {
            if ((long)k > du || !uimg(X, a->k[k], m, u, pt, mul, p, pe, out + k, du - (long)k)) return 0;
        } else if (!uimg(X, a->k[k], m, u, pt, mulm(mul, powm(pt[pos], k, p), p), p, pe, out, du))
            return 0;
    }
    return 1;
}

/* degree of a in the variable at position u */
static long p_degree_u(P *a, vmap *m, int u) {
    if (a->v < m->var[u]) return 0;
    if (a->v == m->var[u]) return (long)a->n - 1;
    long d = 0;
    for (uint32_t k = 0; k < a->n; k++) {
        long e = p_degree_u(a->k[k], m, u);
        if (e > d) d = e;
    }
    return d;
}

/* G | A, G | B and gcd(A/G, B/G) = 1, checked on univariate images in each variable at
 * random points modulo two primes below the ones used (leading coefficients kept) */
static int img_check(XP, P *A, P *B, P *G, vmap *m, E *pe) {
    u32 seed = 0x9E3779B9u, pt[MG_MAXV];
    for (int np = 0; np < 2; np++) {
        pe = next_prime(X, pe);
        long pl;
        num_is_small_int(pe, &pl);
        u32 p = (u32)pl;
        for (int u = 0; u < m->k; u++) {
            long da = m->d[u];
            int ok = 0;
            for (int tries = 0; tries < 4 && !ok; tries++) {
                char *mk = K->s_top;
                for (int v = 0; v < m->k; v++) seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5, pt[v] = 2 + seed % (p - 3);
                u32 *ia = salloc(sizeof(u32) * (size_t)(da + 1)), *ib = salloc(sizeof(u32) * (size_t)(da + 1)),
                    *ig = salloc(sizeof(u32) * (size_t)(da + 1));
                for (long i = 0; i <= da; i++) ia[i] = ib[i] = ig[i] = 0;
                if (!uimg(X, A, m, u, pt, 1, p, pe, ia, da) || !uimg(X, B, m, u, pt, 1, p, pe, ib, da) ||
                    !uimg(X, G, m, u, pt, 1, p, pe, ig, da))
                    return 0;
                long na = udeg(ia, da), nb = udeg(ib, da), ng = udeg(ig, da);
                /* the images must keep the degrees in u (leading coefficients nonzero at pt) */
                long ea = p_degree_u(A, m, u), eb = p_degree_u(B, m, u), eg = p_degree_u(G, m, u);
                if (na != ea || nb != eb || ng != eg || ng < 0) {
                    K->s_top = mk;
                    continue;
                }
                u32 *qa = salloc(sizeof(u32) * (size_t)(da + 1)), *qb = salloc(sizeof(u32) * (size_t)(da + 1)),
                    *w = salloc(sizeof(u32) * (size_t)(da + 1));
                udiv(ia, na, ig, ng, p, qa, w);
                if (udeg(w, ng - 1) >= 0) return 0; /* G does not divide A */
                udiv(ib, nb, ig, ng, p, qb, w);
                if (udeg(w, ng - 1) >= 0) return 0;
                u32 *t;
                if (ugcd(X, qa, na - ng, qb, nb - ng, p, &t) > 0) return 0; /* cofactors not coprime */
                K->s_top = mk;
                ok = 1;
            }
            if (!ok) return 0;
        }
    }
    return 1;
}

static P *mgcd(XP, P *A, P *B) {
    long budget = 3000000L; /* element operations over all primes */
    /* one divides the other (common in integration: the gcd is then that one) */
    {
        char *mk = K->s_top;
        P *s = A->n <= B->n ? A : B, *l = s == A ? B : A;
        if (p_divexact(l, s)) return s;
        K->s_top = mk;
    }
    vmap m;
    m.k = 0;
    if (!collect(X, A, &m) || !collect(X, B, &m)) return NULL;
    /* main variable first: the order of the P recursion (larger index = more main) */
    for (int i = 0; i < m.k; i++)
        for (int j = i + 1; j < m.k; j++)
            if (m.var[j] > m.var[i]) {
                int t = m.var[i];
                m.var[i] = m.var[j], m.var[j] = t;
                long u = m.d[i];
                m.d[i] = m.d[j], m.d[j] = u;
            }
    long sz = da_size(m.k, m.d);
    if (sz < 0 || sz > 100000 || (size_t)(K->s_hi - K->s_top) < (size_t)sz * 4 * sizeof(u32) + (1u << 20)) return NULL;
    E *la = p_lnum(A), *lb = p_lnum(B);
    if (la->t != E_INT || lb->t != E_INT) return NULL;
    E *gam = int_gcd(la, lb);
    E *M = NULL, **H = NULL;
    long hd[MG_MAXV], hsize = 0, hlead = -1, nprimes = 0;
    E *pe = mk_int(2147483647);
    for (int tries = 0; tries < 60; tries++) {
        /* the next prime below the last one */
        pe = next_prime(X, pe);
        long pl;
        num_is_small_int(pe, &pl);
        u32 p = (u32)pl;
        if (is_zero(int_mod(la, pe)) || is_zero(int_mod(lb, pe))) continue;
        DA a, b, g;
        char *mk = K->s_top; /* the work of one prime is released, its image kept */
        if (!da_new(X, &a, m.k, m.d) || !da_new(X, &b, m.k, m.d)) return NULL;
        if (!to_da(X, A, &m, &a, 0, 0, p, pe) || !to_da(X, B, &m, &b, 0, 0, p, pe)) return NULL;
        if (!pgcd(X, &a, &b, p, &g, &budget)) { /* a failed image (out of work budget or points): give up */
            K->s_top = mk;
            return NULL;
        }
        g.c = keep_down(X, mk, g.c, g.size);
        long gl = da_lead(&g);
        if (gl < 0) continue;
        long gm;
        num_is_small_int(int_mod(gam, pe), &gm);
        if (gm < 0) gm += p;
        u32 sc = mulm((u32)gm, invm(g.c[gl], p), p);
        for (long i = 0; i < g.size; i++) g.c[i] = mulm(g.c[i], sc, p);
        int cmp = 0;
        if (hlead >= 0) {
            long e1[MG_MAXV], e2[MG_MAXV];
            DA hv;
            hv.k = m.k;
            for (int v = 0; v < m.k; v++) hv.d[v] = hd[v];
            da_exps(&g, gl, e1);
            da_exps(&hv, hlead, e2);
            for (int v = 0; v < m.k && !cmp; v++) cmp = e1[v] < e2[v] ? -1 : e1[v] > e2[v];
        }
        if (cmp > 0) continue;
        if (hlead < 0 || cmp < 0) { /* (re)start */
            for (int v = 0; v < m.k; v++) hd[v] = g.d[v];
            hsize = g.size, hlead = gl;
            H = salloc(sizeof(E *) * (size_t)hsize);
            for (long i = 0; i < hsize; i++) {
                long v = g.c[i];
                H[i] = mk_int(v > (long)(p / 2) ? v - (long)p : v);
            }
            M = pe, nprimes = 1;
            continue;
        }
        /* widen to common bounds */
        long d[MG_MAXV];
        int grow = 0;
        for (int v = 0; v < m.k; v++) {
            d[v] = g.d[v] > hd[v] ? g.d[v] : hd[v];
            grow |= d[v] != hd[v];
        }
        da_widen(X, &g, d);
        if (grow) {
            DA hv;
            hv.k = m.k;
            for (int v = 0; v < m.k; v++) hv.d[v] = hd[v];
            long ns = da_size(m.k, d), e[MG_MAXV];
            E **H2 = salloc(sizeof(E *) * (size_t)ns);
            for (long i = 0; i < ns; i++) H2[i] = K->c0;
            for (long i = 0; i < hsize; i++) {
                da_exps(&hv, i, e);
                long j = 0;
                for (int v = 0; v < m.k; v++) j = j * (d[v] + 1) + e[v];
                H2[j] = H[i];
            }
            H = H2, hsize = ns;
            for (int v = 0; v < m.k; v++) hd[v] = d[v];
            hlead = da_lead(&g);
        }
        /* CRT: H += M ((g - H) / M mod p), symmetric range */
        E *Mi;
        long mi;
        num_is_small_int(int_mod(M, pe), &mi);
        if (mi < 0) mi += p;
        u32 im = invm((u32)mi, p);
        int changed = 0;
        E *M2 = int_mul(M, pe), *half = int_mul_si(M2, 1);
        (void)Mi;
        for (long i = 0; i < hsize; i++) {
            long hv;
            num_is_small_int(int_mod(H[i], pe), &hv);
            if (hv < 0) hv += p;
            u32 t = mulm(subm(g.c[i], (u32)hv, p), im, p);
            if (!t) continue;
            changed = 1;
            E *nv = int_add(H[i], int_mul_si(M, t));
            /* symmetric: |nv| <= M2/2 */
            if (num_cmp(int_mul_si(nv, 2), half) > 0) nv = int_sub(nv, M2);
            H[i] = nv;
        }
        M = M2, nprimes++;
        if (changed) continue;
        /* stable: primitive part over Z, checked by division */
        E *ct = K->c0;
        for (long i = 0; i < hsize; i++)
            if (!is_zero(H[i])) ct = int_gcd(ct, H[i]);
        if (is_zero(ct)) return NULL;
        /* primitive, with a positive leading (lex-largest) coefficient */
        long top = hsize - 1;
        while (top > 0 && is_zero(H[top])) top--;
        if (num_sgn(H[top]) < 0) ct = int_neg(ct);
        E **Q = salloc(sizeof(E *) * (size_t)hsize);
        for (long i = 0; i < hsize; i++) Q[i] = is_zero(H[i]) ? H[i] : s_div(H[i], ct);
        P *G = from_ints(X, &m, hd, Q, 0, 0, p_const(K->c0));
        /* exact check by division when the dense recursive arithmetic fits easily in memory,
         * else by images (G divides A and B, and the cofactors are coprime, in every variable
         * at random points modulo two fresh primes) */
        if ((double)sz * 64.0 * (double)(m.d[0] + 1) < (double)(size_t)(K->s_hi - K->s_top) / 2) {
            P *ca = p_divexact(A, G), *cb = ca ? p_divexact(B, G) : NULL;
            if (ca && cb && coprime_mod(X, ca, cb, pe)) return G;
        } else if (img_check(X, A, B, G, &m, pe))
            return G;
        if (nprimes > 40) return NULL;
    }
    return NULL;
}

/* an independent check that G is the greatest common divisor: the cofactors are coprime
 * modulo a prime other than the ones used (a nonconstant gcd there, with both leading
 * coefficients nonzero, means they are not) */
static int coprime_mod(XP, P *a, P *b, E *pe) {
    long budget = 20000000L;
    vmap m;
    m.k = 0;
    if (!collect(X, a, &m) || !collect(X, b, &m)) return 0;
    if (!m.k || a->v < 0 || b->v < 0) return 1;
    for (int i = 0; i < m.k; i++)
        for (int j = i + 1; j < m.k; j++)
            if (m.var[j] > m.var[i]) {
                int t = m.var[i];
                m.var[i] = m.var[j], m.var[j] = t;
                long u = m.d[i];
                m.d[i] = m.d[j], m.d[j] = u;
            }
    if (da_size(m.k, m.d) < 0) return 0;
    E *la = p_lnum(a), *lb = p_lnum(b);
    for (int tries = 0; tries < 8; tries++) {
        pe = next_prime(X, pe);
        if (is_zero(int_mod(la, pe)) || is_zero(int_mod(lb, pe))) continue;
        long pl;
        num_is_small_int(pe, &pl);
        u32 p = (u32)pl;
        char *mk = K->s_top;
        DA x, y, g;
        if (!da_new(X, &x, m.k, m.d) || !da_new(X, &y, m.k, m.d)) return 0;
        if (!to_da(X, a, &m, &x, 0, 0, p, pe) || !to_da(X, b, &m, &y, 0, 0, p, pe)) return 0;
        int ok = pgcd(X, &x, &y, p, &g, &budget);
        if (ok) ok = da_lead(&g) == 0 ? 1 : -1;
        K->s_top = mk;
        if (ok) return ok > 0;
    }
    return 0;
}

static P *pgcd_slot(XP, P *a, P *b) {
    char *mk = K->s_top;
    P *r = mgcd(X, a, b);
    if (!r) K->s_top = mk; /* declined: nothing of it is needed */
    return r;
}

int casx_mgcd_register(XP) { return X->reg_fn(CASX_FN_PGCD, (void *)pgcd_slot); }
