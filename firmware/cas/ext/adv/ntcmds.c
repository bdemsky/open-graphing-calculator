/*
 * ntcmds.c -- integer, combinatorial and list-statistics commands
 * (an extension: cas/src/casx.h, registered by casx_nt_register): factorial2 (n!!), pochhammer,
 * bernoulli, fibonacci, stirling1, stirling2, partition, primroot, contfrac,
 * quartile1, quartile3, mode, stddevp, variancep, linreg, factormod.
 *
 * Algorithms: Stirling numbers by their triangle recurrences; partitions by
 * Euler's pentagonal number recurrence p(n) = sum_k (-1)^(k+1) (p(n - k(3k-1)/2)
 * + p(n - k(3k+1)/2)); primitive roots by testing g^((p-1)/q) != 1 for the
 * prime factors q of p - 1; continued fractions exactly for rationals and
 * otherwise from x_{k+1} = 1/(x_k - a_k) with each a_k decided by bigfloat
 * evaluation at two precisions (terms stop when they disagree).  factormod
 * factors a univariate polynomial over GF(p) (p < 2^31 prime): squarefree
 * decomposition, distinct-degree factorization, then Cantor-Zassenhaus
 * equal-degree splitting (von zur Gathen & Gerhard, ch. 14).
 */
#include "casx_mod.h"

#include <string.h> /* memcpy: the C library, or modules/modx_rt.c in the image */

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

static long nint(XP, E *e, long lo, long hi) {
    long v;
    if (!num_is_small_int(e, &v) || v < lo || v > hi) cas_throw(CAS_ERR_DOMAIN, "argument out of range");
    return v;
}

static int is_intlit(E *e) { return e->t == E_INT; }

/* list of numbers (sorted) or throw */
static E *num_list(XP, E *L, int sort) {
    if (L->t != E_LIST || !L->n) cas_throw(CAS_ERR_DOMAIN, "expected a nonempty list");
    return sort ? eval_cmd(C_SORT, 1, &L) : L;
}

static E *sub_list(XP, E *L, uint32_t from, uint32_t n) { return s_list(n, EK(L) + from); }

/* ------------------------------------------------------------ GF(p)[x] --- */
typedef struct {
    int d;
    uint32_t *c; /* ascending, c[d] != 0 unless d < 0 */
} gp;

/* GF(p) state of one factormod / primroot call (an extension has no static data) */
typedef struct {
    const casx_api_t *X;
    uint32_t p, rng;
} gfc;
#define g_p (G->p)
#define g_rng (G->rng)

static uint32_t mulm(gfc *G, uint32_t a, uint32_t b) { return (uint32_t)((uint64_t)a * b % g_p); }
static uint32_t powm(gfc *G, uint32_t a, uint64_t e) {
    uint32_t r = 1;
    for (; e; e >>= 1, a = mulm(G, a, a))
        if (e & 1) r = mulm(G, r, a);
    return r;
}
static uint32_t invm(gfc *G, uint32_t a) { return powm(G, a, g_p - 2); }

static gp gnew(gfc *G, int d) {
    XP = G->X;
    gp r = {d, salloc(sizeof(uint32_t) * (size_t)(d < 0 ? 1 : d + 1))};
    for (int i = 0; i <= d; i++) r.c[i] = 0;
    return r;
}
static gp gnorm(gfc *G, gp a) {
    while (a.d >= 0 && !a.c[a.d]) a.d--;
    return a;
}
static gp gcopy(gfc *G, gp a) {
    gp r = gnew(G, a.d);
    if (a.d >= 0) memcpy(r.c, a.c, sizeof(uint32_t) * (size_t)(a.d + 1));
    return r;
}
static gp gmul(gfc *G, gp a, gp b) {
    if (a.d < 0 || b.d < 0) return gnew(G, -1);
    gp r = gnew(G, a.d + b.d);
    for (int i = 0; i <= a.d; i++)
        for (int j = 0; j <= b.d; j++) r.c[i + j] = (r.c[i + j] + mulm(G, a.c[i], b.c[j])) % g_p;
    return gnorm(G, r);
}
/* a mod b (and quotient into *q if q) */
static gp gdivrem(gfc *G, gp a, gp b, gp *q) {
    gp r = gcopy(G, a);
    if (a.d < b.d) {
        if (q) *q = gnew(G, -1);
        return r;
    }
    uint32_t il = invm(G, b.c[b.d]);
    gp qq = gnew(G, a.d - b.d);
    for (int i = r.d; i >= b.d; i--) {
        uint32_t f = mulm(G, r.c[i], il);
        if (qq.d >= 0) qq.c[i - b.d] = f;
        if (!f) continue;
        for (int j = 0; j <= b.d; j++) r.c[i - b.d + j] = (r.c[i - b.d + j] + g_p - mulm(G, f, b.c[j])) % g_p;
    }
    if (q) *q = gnorm(G, qq);
    r.d = b.d - 1;
    return gnorm(G, r);
}
static gp gmonic(gfc *G, gp a) {
    if (a.d < 0) return a;
    uint32_t il = invm(G, a.c[a.d]);
    gp r = gnew(G, a.d);
    for (int i = 0; i <= a.d; i++) r.c[i] = mulm(G, a.c[i], il);
    return r;
}
static gp ggcd(gfc *G, gp a, gp b) {
    while (b.d >= 0) {
        gp r = gdivrem(G, a, b, NULL);
        a = b, b = r;
    }
    return gmonic(G, a);
}
static gp gderiv(gfc *G, gp a) {
    gp r = gnew(G, a.d - 1);
    for (int i = 1; i <= a.d; i++) r.c[i - 1] = mulm(G, a.c[i], (uint32_t)(i % g_p));
    return gnorm(G, r);
}
/* b^e mod m */
static gp gpowmod(gfc *G, gp b, uint64_t e, gp m) {
    gp r = gnew(G, 0);
    r.c[0] = 1;
    b = gdivrem(G, b, m, NULL);
    for (; e; e >>= 1) {
        if (e & 1) r = gdivrem(G, gmul(G, r, b), m, NULL);
        if (e > 1) b = gdivrem(G, gmul(G, b, b), m, NULL);
    }
    return r;
}
static gp gsub_x(gfc *G, gp a) { /* a - x */
    gp r = gnew(G, a.d < 1 ? 1 : a.d);
    for (int i = 0; i <= a.d; i++) r.c[i] = a.c[i];
    r.c[1] = (r.c[1] + g_p - 1) % g_p;
    return gnorm(G, r);
}

typedef struct {
    gp f[64];
    int m[64], n;
} gfl;

static void gadd(gfc *G, gfl *o, gp f, int m) {
    XP = G->X;
    if (o->n == 64) cas_throw(CAS_ERR_DOMAIN, "factormod: too many factors");
    o->f[o->n] = f, o->m[o->n++] = m;
}


/* split f (monic, product of distinct irreducibles of degree d) */
static void edf(gfc *G, gp f, int d, int m, gfl *o) {
    XP = G->X;
    stack_check();
    if (f.d <= d) {
        gadd(G, o, f, m);
        return;
    }
    for (int tries = 0; tries < 200; tries++) {
        poll_intr();
        gp a = gnew(G, f.d - 1);
        for (int i = 0; i < f.d; i++) g_rng ^= g_rng << 13, g_rng ^= g_rng >> 17, g_rng ^= g_rng << 5, a.c[i] = g_rng % g_p;
        a = gnorm(G, a);
        if (a.d < 1) continue;
        gp b;
        if (g_p == 2) { /* trace map a + a^2 + ... + a^(2^(d-1)) */
            b = gcopy(G, a);
            gp t = a;
            for (int i = 1; i < d; i++) {
                t = gdivrem(G, gmul(G, t, t), f, NULL);
                gp s = gnew(G, b.d > t.d ? b.d : t.d);
                for (int j = 0; j <= s.d; j++) s.c[j] = ((j <= b.d ? b.c[j] : 0) + (j <= t.d ? t.c[j] : 0)) % 2;
                b = gnorm(G, s);
            }
        } else {
            uint64_t e = 1;
            for (int i = 0; i < d; i++) e *= g_p;
            b = gpowmod(G, a, (e - 1) / 2, f);
            if (b.d >= 0) b.c[0] = (b.c[0] + g_p - 1) % g_p;
            b = gnorm(G, b);
        }
        gp g = ggcd(G, f, b);
        if (g.d > 0 && g.d < f.d) {
            gp q;
            gdivrem(G, f, g, &q);
            edf(G, g, d, m, o);
            edf(G, gmonic(G, q), d, m, o);
            return;
        }
    }
    cas_throw(CAS_ERR_DOMAIN, "factormod: splitting failed");
}

/* squarefree f: distinct-degree, then equal-degree factors with multiplicity m */
static void ddf(gfc *G, gp f, int m, gfl *o) {
    gp h = gnew(G, 1);
    h.c[1] = 1; /* x */
    for (int d = 1; 2 * d <= f.d; d++) {
        h = gpowmod(G, h, g_p, f);
        gp g = ggcd(G, f, gsub_x(G, h));
        if (g.d > 0) {
            edf(G, g, d, m, o);
            gp q;
            gdivrem(G, f, g, &q);
            f = gmonic(G, q);
            h = gdivrem(G, h, f, NULL);
        }
    }
    if (f.d > 0) gadd(G, o, f, m);
}

/* factors of monic f with multiplicities (Yun-style over GF(p), p-th roots
 * when f' = 0) */
static void gfactor(gfc *G, gp f, int m, gfl *o) {
    XP = G->X;
    stack_check();
    if (f.d <= 0) return;
    gp df = gderiv(G, f);
    if (df.d < 0) { /* f(x) = g(x^p) = g(x)^p over GF(p) */
        gp g = gnew(G, f.d / (int)g_p);
        for (int i = 0; i <= g.d; i++) g.c[i] = f.c[i * (int)g_p];
        gfactor(G, g, m * (int)g_p, o);
        return;
    }
    gp c = ggcd(G, f, df), w, q;
    gdivrem(G, f, c, &w);
    w = gmonic(G, w);
    for (int i = 1; w.d > 0; i++) {
        gp y = ggcd(G, w, c), z;
        gdivrem(G, w, y, &z);
        if (z.d > 0) ddf(G, gmonic(G, z), m * i, o);
        w = y;
        gdivrem(G, c, y, &q);
        c = gmonic(G, q);
    }
    if (c.d > 0) gfactor(G, c, m, o);
}

static E *gp_to(gfc *G, gp a, E *x) {
    XP = G->X;
    E **t = salloc(sizeof(E *) * (size_t)(a.d + 1));
    for (int i = 0; i <= a.d; i++) {
        int64_t v = a.c[i] > g_p / 2 ? (int64_t)a.c[i] - g_p : a.c[i];
        t[i] = s_mul2(mk_int(v), s_powi(x, i));
    }
    return s_add((uint32_t)a.d + 1, t);
}

static E *factormod(XP, E *P, E *pm) {
    long p = nint(X, pm, 2, 2147483647L);
    if (!int_isprime(pm)) cas_throw(CAS_ERR_DOMAIN, "factormod: modulus must be prime");
    E *x = default_var(P), *y = NULL;
    UP u;
    if (!x || !up_from(expand(P), x, &u) || u.d < 1 || u.d > 400) return NULL;
    /* a homogeneous polynomial in x and y: c_i = k_i y^(d-i); factored as the univariate
     * polynomial at y = 1 and homogenised again (Wester H30) */
    for (long i = 0; i <= u.d && !y; i++)
        if (!is_intlit(u.c[i])) y = default_var(u.c[i]);
    if (y) {
        for (long i = 0; i <= u.d; i++) {
            u.c[i] = rat_normal(s_div(u.c[i], s_powi(y, u.d - i)));
            if (!is_intlit(u.c[i])) return NULL;
        }
    }
    gfc gc = {X, (uint32_t)p, 0x9E3779B9u}, *G = &gc;
    gp f = gnew(G, (int)u.d);
    for (long i = 0; i <= u.d; i++) {
        if (!is_intlit(u.c[i])) return NULL;
        f.c[i] = (uint32_t)nint(X, int_mod(u.c[i], pm), 0, p - 1);
    }
    f = gnorm(G, f);
    if (f.d < 1) return y ? s_mul2(gp_to(G, f, x), s_powi(y, u.d)) : gp_to(G, f, x);
    uint32_t lc = f.c[f.d];
    gfl o;
    o.n = 0;
    gfactor(G, gmonic(G, f), 1, &o);
    E **t = salloc(sizeof(E *) * (size_t)(o.n + 2));
    for (int i = 0; i < o.n; i++) {
        E *fe = gp_to(G, o.f[i], x);
        if (y) fe = expand(s_mul2(subst(fe, x, s_div(x, y)), s_powi(y, o.f[i].d)));
        t[i] = s_powi(fe, o.m[i]);
    }
    t[o.n] = mk_int(lc > g_p / 2 ? (int64_t)lc - g_p : lc);
    t[o.n + 1] = y ? s_powi(y, u.d - f.d) : K->c1; /* y^k where the degree in x dropped mod p */
    return s_mul((uint32_t)o.n + 2, t);
}

/* msolve(p(x) = q(x), x, m): all residues 0 <= x < m (m <= 10^6) with
 * p(x) = q(x) mod m, by trying each; integer coefficients only */
static E *msolve(XP, E *eq, E *x, E *mm) {
    long m = nint(X, mm, 1, 1000000);
    UP u;
    if (IFN(eq, F_EQ)) eq = s_sub(K0(eq), K1(eq));
    if (x->t != E_SYM || !up_from(expand(eq), x, &u)) return NULL;
    int64_t *c = salloc(sizeof(int64_t) * (size_t)(u.d + 1));
    for (long i = 0; i <= u.d; i++) {
        if (!is_intlit(u.c[i])) return NULL;
        c[i] = nint(X, int_mod(u.c[i], mm), 0, m - 1);
    }
    E **v = salloc(sizeof(E *) * 64);
    uint32_t k = 0;
    for (int64_t r = 0; r < m; r++) {
        if (!(r & 4095)) poll_intr();
        int64_t s = 0;
        for (long i = u.d; i >= 0; i--) s = (s * r + c[i]) % m;
        if (!s) {
            if (k == 64) cas_throw(CAS_ERR_DOMAIN, "msolve: more than 64 solutions");
            v[k++] = mk_int(r);
        }
    }
    return s_list(k, v);
}

static int in_list(XP, E *L, E *x) {
    for (uint32_t i = 0; i < L->n; i++)
        if (expr_eq(EK(L)[i], x)) return 1;
    return 0;
}

/* union / intersect / setdiff of lists as sets: distinct elements in order
 * of first appearance */
static E *setop(XP, int op, uint32_t n, E **a) {
    uint32_t tot = 0, k = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (a[i]->t != E_LIST) return NULL;
        tot += a[i]->n;
    }
    E **v = salloc(sizeof(E *) * (tot + 1));
    for (uint32_t i = 0; i < n; i++) {
        if (op != 0 && i) break;
        for (uint32_t j = 0; j < a[i]->n; j++) {
            E *x = EK(a[i])[j], *cur = s_list(k, v);
            int keep = !in_list(X, cur, x);
            for (uint32_t m = 1; keep && op != 0 && m < n; m++) keep = in_list(X, a[m], x) == (op == 1);
            if (keep) v[k++] = x;
        }
    }
    return s_list(k, v);
}

/* orthogonal basis of the span of the vectors (rows), Gram-Schmidt without
 * normalisation; dependent vectors are dropped */
static E *gramschmidt(XP, E *V) {
    uint32_t r, c, k = 0;
    if (!is_matrix(V, &r, &c)) return NULL;
    E **u = salloc(sizeof(E *) * r), **d = salloc(sizeof(E *) * r);
    for (uint32_t i = 0; i < r; i++) {
        E *w = EK(V)[i];
        for (uint32_t j = 0; j < k; j++) w = s_sub(w, s_mul2(s_div(mat_mul_lists(EK(V)[i], u[j]), d[j]), u[j]));
        E **t = salloc(sizeof(E *) * c);
        int nz = 0;
        for (uint32_t q = 0; q < c; q++) nz |= !is_zero(t[q] = rat_normal(EK(w)[q]));
        if (!nz) continue;
        u[k] = s_list(c, t);
        d[k] = rat_normal(mat_mul_lists(u[k], u[k]));
        if (is_zero(d[k])) return NULL;
        k++;
    }
    return s_list(k, u);
}

/* ---------------------------------------------------------- commands --- */
static E *contfrac(XP, E *x, long n) {
    E **t = salloc(sizeof(E *) * (size_t)n);
    long k = 0;
    if (is_exactnum(x)) {
        for (; k < n; k++) {
            E *a = num_floor(x);
            t[k] = a;
            x = num_sub(x, a);
            if (num_is_zero(x)) {
                k++;
                break;
            }
            x = num_inv(x);
        }
        return s_list((uint32_t)k, t);
    }
    double v;
    if (!is_const_expr(x) || !to_double(x, &v)) return NULL;
    for (; k < n; k++) {
        poll_intr();
        double v1;
        if (!to_double(evalf_digits(x, 60), &v1) || __builtin_fabs(v1) > 1e15) break;
        long a = (long)__builtin_floor(v1);
        double f1 = 0, f2 = 0;
        for (int it = 0; it < 3; it++) {
            to_double(evalf_digits(s_sub(x, mk_int(a)), 60), &f1);
            if (f1 < 0) a--;
            else if (f1 >= 1) a++;
            else break;
        }
        to_double(evalf_digits(s_sub(x, mk_int(a)), 90), &f2);
        if (!(f1 >= 0 && f1 < 1) || __builtin_fabs(f1 - f2) > 1e-12 || f1 < 1e-30) break;
        t[k] = mk_int(a);
        x = s_inv(s_sub(x, mk_int(a)));
    }
    return k ? s_list((uint32_t)k, t) : NULL;
}

static E *quartile(XP, E *L, int upper) {
    L = num_list(X, L, 1);
    uint32_t h = L->n / 2;
    if (!h) return EK(L)[0];
    E *half = sub_list(X, L, upper ? L->n - h : 0, h);
    return eval_cmd(C_MEDIAN, 1, &half);
}

static E *mode(XP, E *L) {
    L = num_list(X, L, 0);
    uint32_t best = 0, nb = 0, *cnt = salloc(sizeof(uint32_t) * L->n);
    for (uint32_t i = 0; i < L->n; i++) {
        cnt[i] = 0;
        for (uint32_t j = 0; j < L->n; j++) cnt[i] += expr_eq(EK(L)[i], EK(L)[j]);
        if (cnt[i] > best) best = cnt[i];
    }
    E **v = salloc(sizeof(E *) * L->n);
    for (uint32_t i = 0; i < L->n; i++) {
        int seen = 0;
        for (uint32_t j = 0; j < nb; j++) seen |= expr_eq(v[j], EK(L)[i]);
        if (cnt[i] == best && !seen) v[nb++] = EK(L)[i];
    }
    return nb == 1 ? v[0] : s_list(nb, v);
}

enum { N_FACT2, N_POCH, N_BERN, N_FIB, N_ST1, N_ST2, N_PART, N_PRIMROOT, N_CONTFRAC, N_Q1, N_Q3, N_MODE,
       N_SDP, N_VARP, N_LINREG, N_FACTORMOD, N_MSOLVE, N_UNION, N_INTERSECT, N_SETDIFF, N_GRAMSCHMIDT,
       N_COV, N_COVP, N_CORR, N_TTEST, N_TTEST2, N_PTTEST, N_TINT,
       N_ZTEST, N_ZINT, N_PROPINT, N_CHI2GOF, N_CHI2TEST, N_ANOVA,
       /* pdf, cdf, inverse of each continuous distribution (in the order of the D_* kinds) */
       N_EXPPDF, N_EXPCDF, N_INVEXP, N_UNIFPDF, N_UNIFCDF, N_INVUNIF, N_GAMPDF, N_GAMCDF, N_INVGAM,
       N_BETAPDF, N_BETACDF, N_INVBETA, N_WEIBPDF, N_WEIBCDF, N_INVWEIB, N_LOGNPDF, N_LOGNCDF, N_INVLOGN,
       N_INVBINOM, N_INVPOIS,
       N_POLYREG, N_EXPREG, N_POWREG, N_LOGREG, N_MULTIREG, N_NLFIT };

/* in the order of the N_* ops (the handler gets the index) */
static const casx_cmd_t ntab[] = {
    {"factorial2", 1, 1}, /* N_FACT2 */
    {"pochhammer", 2, 2}, /* N_POCH */
    {"bernoulli", 1, 1}, /* N_BERN */
    {"fibonacci", 1, 1}, /* N_FIB */
    {"stirling1", 2, 2}, /* N_ST1 */
    {"stirling2", 2, 2}, /* N_ST2 */
    {"partition", 1, 1}, /* N_PART */
    {"primroot", 1, 1}, /* N_PRIMROOT */
    {"contfrac", 1, 2}, /* N_CONTFRAC */
    {"quartile1", 1, 1}, /* N_Q1 */
    {"quartile3", 1, 1}, /* N_Q3 */
    {"mode", 1, 1}, /* N_MODE */
    {"stddevp", 1, 1}, /* N_SDP */
    {"variancep", 1, 1}, /* N_VARP */
    {"linreg", 2, 2}, /* N_LINREG */
    {"factormod", 2, 2}, /* N_FACTORMOD */
    {"msolve", 3, 3}, /* N_MSOLVE */
    {"union", 1, 16}, /* N_UNION */
    {"intersect", 1, 16}, /* N_INTERSECT */
    {"setdiff", 2, 2}, /* N_SETDIFF */
    {"gramschmidt", 1, 1}, /* N_GRAMSCHMIDT */
    {"covariance", 2, 2}, /* N_COV */
    {"covariancep", 2, 2}, /* N_COVP */
    {"correlation", 2, 2}, /* N_CORR */
    {"ttest", 2, 3}, /* N_TTEST */
    {"ttest2", 2, 4}, /* N_TTEST2 */
    {"pairedttest", 2, 3}, /* N_PTTEST */
    {"tinterval", 1, 2}, /* N_TINT */
    {"ztest", 3, 4}, /* N_ZTEST */
    {"zinterval", 2, 3}, /* N_ZINT */
    {"propinterval", 2, 3}, /* N_PROPINT */
    {"chi2gof", 2, 3}, /* N_CHI2GOF */
    {"chi2test", 1, 2}, /* N_CHI2TEST */
    {"anova", 2, 16}, /* N_ANOVA */
    {"exppdf", 2, 2}, {"expcdf", 3, 3}, {"invExp", 2, 2},
    {"uniformpdf", 3, 3}, {"uniformcdf", 4, 4}, {"invUniform", 3, 3},
    {"gammapdf", 3, 3}, {"gammacdf", 4, 4}, {"invGamma", 3, 3},
    {"betapdf", 3, 3}, {"betacdf", 4, 4}, {"invBeta", 3, 3},
    {"weibullpdf", 3, 3}, {"weibullcdf", 4, 4}, {"invWeibull", 3, 3},
    {"lognormpdf", 3, 3}, {"lognormcdf", 4, 4}, {"invLognorm", 3, 3},
    {"invBinom", 3, 3}, /* N_INVBINOM */
    {"invPoisson", 2, 2}, /* N_INVPOIS */
    {"polyreg", 3, 3}, /* N_POLYREG */
    {"expreg", 2, 2}, /* N_EXPREG */
    {"powerreg", 2, 2}, /* N_POWREG */
    {"logreg", 2, 2}, /* N_LOGREG */
    {"multireg", 2, 2}, /* N_MULTIREG */
    {"nlfit", 5, 6}, /* N_NLFIT */
};


/* ------------------------------------------------- bivariate statistics, t tests --- */
/* Sum of (x_i - mean x)(y_i - mean y), exact for exact data; *m gets n. */
static E *cosum(XP, E *A, E *B) {
    A = num_list(X, A, 0), B = num_list(X, B, 0);
    if (A->n != B->n || A->n < 2) cas_throw(CAS_ERR_DOMAIN, "lists of equal length >= 2");
    E *ma = eval_cmd(C_MEAN, 1, &A), *mb = eval_cmd(C_MEAN, 1, &B), *s = K->c0;
    for (uint32_t i = 0; i < A->n; i++) s = s_add2(s, s_mul2(s_sub(EK(A)[i], ma), s_sub(EK(B)[i], mb)));
    return s;
}

/* real number or throw */
static double dbl(XP, E *e) {
    double v;
    if (!to_double(e, &v) || v != v) cas_throw(CAS_ERR_DOMAIN, "expected real numbers");
    return v;
}

/* n, mean and sample variance (n-1) of a numeric list, in doubles */
typedef struct { double n, m, v; } dstat;
static dstat dstats(XP, E *L) {
    L = num_list(X, L, 0);
    dstat d = {(double)L->n, 0, 0};
    if (L->n < 2) cas_throw(CAS_ERR_DOMAIN, "need at least 2 values");
    for (uint32_t i = 0; i < L->n; i++) d.m += dbl(X, EK(L)[i]);
    d.m /= d.n;
    for (uint32_t i = 0; i < L->n; i++) {
        double t = dbl(X, EK(L)[i]) - d.m;
        d.v += t * t;
    }
    d.v /= d.n - 1;
    return d;
}

/* name=value */
static E *eqn(XP, const char *name, E *v) { return mk_fn(F_EQ, 2, (E *[]){mk_sym_name(name), v}); }

/* level or alpha in (0,1), default def */
static double prob_arg(XP, uint32_t n, E **a, uint32_t i, double def) {
    if (n <= i) return def;
    double v = dbl(X, a[i]);
    if (!(v > 0 && v < 1)) cas_throw(CAS_ERR_DOMAIN, "probability must be in (0,1)");
    return v;
}

/* Two-sided t test result [t=, df=, p=, ..., reject=true|false] at level alpha */
static E *tresult(XP, double t, double df, double alpha, int ne, E **extra) {
    E *ta = mk_flt(t < 0 ? -t : t);
    E *q = eval_cmd(C_TCDF, 3, (E *[]){ta, mk_flt(1e99), mk_flt(df)});
    double p = 2 * dbl(X, q);
    if (p > 1) p = 1;
    E *r[8];
    int k = 0;
    r[k++] = eqn(X, "t", mk_flt(t));
    r[k++] = eqn(X, "df", df == (double)(long)df ? mk_int((long)df) : mk_flt(df));
    r[k++] = eqn(X, "p", mk_flt(p));
    for (int i = 0; i < ne; i++) r[k++] = extra[i];
    r[k++] = eqn(X, "reject", mk_sym(p < alpha ? S_TRUE : S_FALSE));
    return s_list((uint32_t)k, r);
}

static E *statcmd(XP, int op, uint32_t n, E **a) {
    switch (op) {
    case N_COV: case N_COVP: {
        E *s = cosum(X, a[0], a[1]);
        uint32_t m = a[0]->n;
        return s_div(s, mk_int(op == N_COV ? m : m - 1));
    }
    case N_CORR: {
        E *sxy = cosum(X, a[0], a[1]), *sxx = cosum(X, a[0], a[0]), *syy = cosum(X, a[1], a[1]);
        if (is_zero(sxx) || is_zero(syy)) cas_throw(CAS_ERR_DOMAIN, "correlation: a list is constant");
        return s_div(sxy, s_sqrt(s_mul2(sxx, syy)));
    }
    case N_TTEST: case N_PTTEST: {
        dstat d;
        double mu0 = 0, alpha;
        if (op == N_TTEST) {
            d = dstats(X, a[0]);
            mu0 = dbl(X, a[1]);
            alpha = prob_arg(X, n, a, 2, 0.05);
        } else {
            E *A = num_list(X, a[0], 0), *B = num_list(X, a[1], 0);
            if (A->n != B->n) cas_throw(CAS_ERR_DOMAIN, "pairedttest: lists of equal length");
            E **dd = salloc(sizeof(E *) * A->n);
            for (uint32_t i = 0; i < A->n; i++) dd[i] = s_sub(EK(A)[i], EK(B)[i]);
            d = dstats(X, s_list(A->n, dd));
            alpha = prob_arg(X, n, a, 2, 0.05);
        }
        if (d.v <= 0) cas_throw(CAS_ERR_DOMAIN, "ttest: zero variance");
        double se = X->sqrt(d.v / d.n);
        E *ex[2] = {eqn(X, "mean", mk_flt(d.m)), eqn(X, "sx", mk_flt(X->sqrt(d.v)))};
        return tresult(X, (d.m - mu0) / se, d.n - 1, alpha, 2, ex);
    }
    case N_TTEST2: {
        dstat d1 = dstats(X, a[0]), d2 = dstats(X, a[1]);
        int pooled = 0;
        if (n > 2) pooled = !is_zero(a[2]);
        double alpha = prob_arg(X, n, a, 3, 0.05), se, df;
        if (pooled) {
            df = d1.n + d2.n - 2;
            double sp2 = ((d1.n - 1) * d1.v + (d2.n - 1) * d2.v) / df;
            se = X->sqrt(sp2 * (1 / d1.n + 1 / d2.n));
        } else { /* Welch-Satterthwaite */
            double u1 = d1.v / d1.n, u2 = d2.v / d2.n;
            se = X->sqrt(u1 + u2);
            df = (u1 + u2) * (u1 + u2) / (u1 * u1 / (d1.n - 1) + u2 * u2 / (d2.n - 1));
        }
        if (!(se > 0)) cas_throw(CAS_ERR_DOMAIN, "ttest2: zero variance");
        E *ex[2] = {eqn(X, "mean1", mk_flt(d1.m)), eqn(X, "mean2", mk_flt(d2.m))};
        return tresult(X, (d1.m - d2.m) / se, df, alpha, 2, ex);
    }
    case N_TINT: {
        dstat d = dstats(X, a[0]);
        double lev = prob_arg(X, n, a, 1, 0.95);
        double ts = dbl(X, eval_cmd(C_INVT, 2, (E *[]){mk_flt((1 + lev) / 2), mk_flt(d.n - 1)}));
        double me = ts * X->sqrt(d.v / d.n);
        return s_list(2, (E *[]){mk_flt(d.m - me), mk_flt(d.m + me)});
    }
    }
    return NULL;
}

/* upper tail P(stat >= x) of command cdf `id` with parameters par[] (np of them) */
static double upper(XP, int id, double x, int np, const double *par) {
    E *a[4] = {mk_flt(x), mk_flt(1e99)};
    for (int i = 0; i < np; i++) a[2 + i] = mk_flt(par[i]);
    double p = dbl(X, eval_cmd(id, (uint32_t)(2 + np), a));
    return p < 0 ? 0 : p > 1 ? 1 : p;
}

/* [name=stat, df=... (or df1, df2), p=..., reject=...] for a right-tailed chi-square / F test */
static E *tailres(XP, const char *nm, double st, int ndf, const double *df, double p, double alpha) {
    E *r[6];
    int k = 0;
    r[k++] = eqn(X, nm, mk_flt(st));
    if (ndf == 1) r[k++] = eqn(X, "df", mk_int((long)df[0]));
    else r[k++] = eqn(X, "df1", mk_int((long)df[0])), r[k++] = eqn(X, "df2", mk_int((long)df[1]));
    r[k++] = eqn(X, "p", mk_flt(p));
    r[k++] = eqn(X, "reject", mk_sym(p < alpha ? S_TRUE : S_FALSE));
    return s_list((uint32_t)k, r);
}

/* z quantile for a two-sided confidence level */
static double zstar(XP, double lev) { return dbl(X, eval_cmd(C_INVNORM, 1, (E *[]){mk_flt((1 + lev) / 2)})); }

static E *statcmd2(XP, int op, uint32_t n, E **a) {
    switch (op) {
    case N_ZTEST: case N_ZINT: {
        E *L = num_list(X, a[0], 0);
        double cnt = L->n, m = 0;
        for (uint32_t i = 0; i < L->n; i++) m += dbl(X, EK(L)[i]);
        m /= cnt;
        double sig = dbl(X, a[op == N_ZTEST ? 2 : 1]);
        if (!(sig > 0)) cas_throw(CAS_ERR_DOMAIN, "sigma must be positive");
        double se = sig / X->sqrt(cnt);
        if (op == N_ZINT) {
            double me = zstar(X, prob_arg(X, n, a, 2, 0.95)) * se;
            return s_list(2, (E *[]){mk_flt(m - me), mk_flt(m + me)});
        }
        double z = (m - dbl(X, a[1])) / se, alpha = prob_arg(X, n, a, 3, 0.05);
        double p = 2 * upper(X, C_NORMALCDF, z < 0 ? -z : z, 0, NULL);
        if (p > 1) p = 1;
        return s_list(4, (E *[]){eqn(X, "z", mk_flt(z)), eqn(X, "p", mk_flt(p)), eqn(X, "mean", mk_flt(m)),
                                 eqn(X, "reject", mk_sym(p < alpha ? S_TRUE : S_FALSE))});
    }
    case N_PROPINT: {
        double x = dbl(X, a[0]), m = dbl(X, a[1]);
        if (!(m > 0) || x < 0 || x > m) cas_throw(CAS_ERR_DOMAIN, "propinterval: 0 <= successes <= trials");
        double ph = x / m, me = zstar(X, prob_arg(X, n, a, 2, 0.95)) * X->sqrt(ph * (1 - ph) / m);
        return s_list(2, (E *[]){mk_flt(ph - me), mk_flt(ph + me)});
    }
    case N_CHI2GOF: {
        E *O = num_list(X, a[0], 0), *Ex = num_list(X, a[1], 0);
        if (O->n != Ex->n || O->n < 2) cas_throw(CAS_ERR_DOMAIN, "chi2gof: lists of equal length >= 2");
        double so = 0, se = 0, c = 0;
        for (uint32_t i = 0; i < O->n; i++) so += dbl(X, EK(O)[i]), se += dbl(X, EK(Ex)[i]);
        if (!(se > 0)) cas_throw(CAS_ERR_DOMAIN, "chi2gof: expected values must be positive");
        for (uint32_t i = 0; i < O->n; i++) { /* expected scaled to the observed total */
            double e = dbl(X, EK(Ex)[i]) * so / se, o = dbl(X, EK(O)[i]);
            if (!(e > 0)) cas_throw(CAS_ERR_DOMAIN, "chi2gof: expected values must be positive");
            c += (o - e) * (o - e) / e;
        }
        double df = O->n - 1;
        return tailres(X, "chi2", c, 1, &df, upper(X, C_CHI2CDF, c, 1, &df), prob_arg(X, n, a, 2, 0.05));
    }
    case N_CHI2TEST: {
        uint32_t r, k;
        if (!is_matrix(a[0], &r, &k) || r < 2 || k < 2) cas_throw(CAS_ERR_DOMAIN, "chi2test: expected a matrix of counts");
        double *rs = salloc(sizeof(double) * (r + k)), *cs = rs + r, tot = 0, c = 0;
        for (uint32_t i = 0; i < r + k; i++) rs[i] = 0;
        for (uint32_t i = 0; i < r; i++)
            for (uint32_t j = 0; j < k; j++) {
                double v = dbl(X, EK(EK(a[0])[i])[j]);
                rs[i] += v, cs[j] += v, tot += v;
            }
        for (uint32_t i = 0; i < r; i++)
            for (uint32_t j = 0; j < k; j++) {
                double e = rs[i] * cs[j] / tot, o = dbl(X, EK(EK(a[0])[i])[j]);
                if (!(e > 0)) cas_throw(CAS_ERR_DOMAIN, "chi2test: a row or column sums to 0");
                c += (o - e) * (o - e) / e;
            }
        double df = (double)(r - 1) * (k - 1);
        return tailres(X, "chi2", c, 1, &df, upper(X, C_CHI2CDF, c, 1, &df), prob_arg(X, n, a, 1, 0.05));
    }
    case N_ANOVA: {
        uint32_t g = n;
        double alpha = 0.05;
        if (a[n - 1]->t != E_LIST) alpha = prob_arg(X, n, a, n - 1, 0.05), g--;
        if (g < 2) cas_throw(CAS_ERR_DOMAIN, "anova: at least 2 groups");
        double N = 0, tot = 0, ssb = 0, ssw = 0;
        dstat *d = salloc(sizeof(dstat) * g);
        for (uint32_t i = 0; i < g; i++) d[i] = dstats(X, a[i]), N += d[i].n, tot += d[i].m * d[i].n;
        tot /= N;
        for (uint32_t i = 0; i < g; i++)
            ssb += d[i].n * (d[i].m - tot) * (d[i].m - tot), ssw += (d[i].n - 1) * d[i].v;
        double df[2] = {g - 1, N - g};
        if (!(ssw > 0)) cas_throw(CAS_ERR_DOMAIN, "anova: zero variance within groups");
        double F = (ssb / df[0]) / (ssw / df[1]);
        return tailres(X, "F", F, 2, df, upper(X, C_FCDF, F, 2, df), alpha);
    }
    }
    return NULL;
}

/* ------------------------------------------------------- more distributions --- */
/* Continuous distributions by kind: exponential(rate l), uniform(a, b),
 * gamma(shape k, scale theta), beta(a, b), Weibull(shape k, scale l),
 * lognormal(mu, sigma of ln X).  Densities and closed-form cdfs / inverses
 * are built as CAS expressions, so exact arguments give exact results
 * (exppdf(1,2) = 2*e^(-2)); the gamma and beta cdfs are exact for integer
 * shapes (Erlang / binomial sums), otherwise they and their inverses come
 * from the chi-square and F routines of the core (gamma(k,theta) =
 * theta/2 chi2(2k); Beta(a,b) = aF/(aF+b) with F ~ F(2a,2b)), as decimals. */
enum { D_EXP, D_UNIF, D_GAM, D_BETA, D_WEIB, D_LOGN };

static E *flt(XP, double v) { return mk_flt(v); }

/* exp(-y) */
static E *emin(XP, E *y) { return s_exp(s_neg(y)); }

/* a small positive integer value (shape parameters of the exact cdfs), else 0 */
static long small_pos(XP, E *e) {
    long v;
    return e->t == E_INT && num_is_small_int(e, &v) && v >= 1 && v <= 60 ? v : 0;
}

/* P(X <= x) for distribution kind d with parameters p[0..1] (dp their values) */
/* value of a cdf bound: +-inf allowed (as +-1e300) */
static int bound(XP, E *x, double *v) {
    E *inf = mk_sym(S_INF);
    if (expr_eq(x, inf)) return *v = 1e300, 1;
    if (expr_eq(x, s_neg(inf))) return *v = -1e300, 1;
    return to_double(x, v);
}

static E *dcdf(XP, int d, E *x, E **p, const double *dp) {
    double dx;
    bound(X, x, &dx);
    if (dx <= -1e99) return K->c0;
    if (dx >= 1e99) return K->c1;
    switch (d) {
    case D_EXP: return dx <= 0 ? K->c0 : s_sub(K->c1, emin(X, s_mul2(p[0], x)));
    case D_UNIF:
        return dx <= dp[0] ? K->c0 : dx >= dp[1] ? K->c1 : s_div(s_sub(x, p[0]), s_sub(p[1], p[0]));
    case D_GAM: {
        if (dx <= 0) return K->c0;
        long k = small_pos(X, p[0]);
        if (k && !has_float(x) && !has_float(p[1])) { /* Erlang: 1 - e^-y sum y^j/j! */
            E *y = s_div(x, p[1]), *t = K->c1, *sm = K->c1;
            for (long j = 1; j < k; j++) t = s_div(s_mul2(t, y), mk_int(j)), sm = s_add2(sm, t);
            return s_sub(K->c1, s_mul2(emin(X, y), sm));
        }
        return eval_cmd(C_CHI2CDF, 3, (E *[]){K->c0, flt(X, 2 * dx / dp[1]), flt(X, 2 * dp[0])});
    }
    case D_BETA: {
        if (dx <= 0) return K->c0;
        if (dx >= 1) return K->c1;
        long a = small_pos(X, p[0]), b = small_pos(X, p[1]);
        if (a && b && !has_float(x)) { /* I_x(a,b) = sum_{j>=a} C(a+b-1,j) x^j (1-x)^(a+b-1-j) */
            long m = a + b - 1;
            E *sm = K->c0, *q = s_sub(K->c1, x);
            for (long j = a; j <= m; j++)
                sm = s_add2(sm, s_mul(3, (E *[]){eval_cmd(C_NCR, 2, (E *[]){mk_int(m), mk_int(j)}),
                                                 s_powi(x, j), s_powi(q, m - j)}));
            return sm;
        }
        return eval_cmd(C_FCDF, 4, (E *[]){K->c0, flt(X, dp[1] * dx / (dp[0] * (1 - dx))), flt(X, 2 * dp[0]),
                                           flt(X, 2 * dp[1])});
    }
    case D_WEIB: return dx <= 0 ? K->c0 : s_sub(K->c1, emin(X, s_pow(s_div(x, p[1]), p[0])));
    case D_LOGN:
        return dx <= 0 ? K->c0
                       : eval_cmd(C_NORMALCDF, 4, (E *[]){flt(X, -1e99), s_fn1(F_LN, x), p[0], p[1]});
    }
    return NULL;
}

static E *dpdf(XP, int d, E *x, E **p, const double *dp) {
    double dx = dbl(X, x);
    int pole = (d == D_GAM || d == D_WEIB) && dx == 0 && dp[0] < 1;
    if (d == D_BETA) pole = (dx == 0 && dp[0] < 1) || (dx == 1 && dp[1] < 1);
    if (pole) cas_throw(CAS_ERR_DOMAIN, "density is infinite there");
    switch (d) {
    case D_EXP: return dx < 0 ? K->c0 : s_mul2(p[0], emin(X, s_mul2(p[0], x)));
    case D_UNIF: return dx < dp[0] || dx > dp[1] ? K->c0 : s_inv(s_sub(p[1], p[0]));
    case D_GAM:
        if (dx < 0) return K->c0;
        return s_div(s_mul2(s_pow(x, s_sub(p[0], K->c1)), emin(X, s_div(x, p[1]))),
                     s_mul2(s_fn1(F_GAMMA, p[0]), s_pow(p[1], p[0])));
    case D_BETA: {
        if (dx < 0 || dx > 1) return K->c0;
        E *B = s_div(s_mul2(s_fn1(F_GAMMA, p[0]), s_fn1(F_GAMMA, p[1])), s_fn1(F_GAMMA, s_add2(p[0], p[1])));
        return s_div(s_mul2(s_pow(x, s_sub(p[0], K->c1)), s_pow(s_sub(K->c1, x), s_sub(p[1], K->c1))), B);
    }
    case D_WEIB: {
        if (dx < 0) return K->c0;
        E *y = s_div(x, p[1]);
        return s_mul(3, (E *[]){s_div(p[0], p[1]), s_pow(y, s_sub(p[0], K->c1)), emin(X, s_pow(y, p[0]))});
    }
    case D_LOGN: {
        if (dx <= 0) return K->c0;
        E *z = s_div(s_sub(s_fn1(F_LN, x), p[0]), p[1]);
        return s_div(emin(X, s_div(s_mul2(z, z), mk_int(2))),
                     s_mul(3, (E *[]){x, p[1], s_sqrt(s_mul2(mk_int(2), mk_sym(S_PI)))}));
    }
    }
    return NULL;
}

static E *dinv(XP, int d, E *pr, E **p, const double *dp) {
    double q = dbl(X, pr);
    if (!(q > 0 && q < 1)) cas_throw(CAS_ERR_DOMAIN, "probability must be in (0,1)");
    E *L = s_neg(s_fn1(F_LN, s_sub(K->c1, pr))); /* -ln(1-p) */
    switch (d) {
    case D_EXP: return s_div(L, p[0]);
    case D_UNIF: return s_add2(p[0], s_mul2(pr, s_sub(p[1], p[0])));
    case D_GAM: return flt(X, dbl(X, eval_cmd(C_INVCHI2, 2, (E *[]){pr, flt(X, 2 * dp[0])})) * dp[1] / 2);
    case D_BETA: {
        double f = dp[0] * dbl(X, eval_cmd(C_INVF, 3, (E *[]){pr, flt(X, 2 * dp[0]), flt(X, 2 * dp[1])}));
        return flt(X, f / (f + dp[1]));
    }
    case D_WEIB: return s_mul2(p[1], s_pow(L, s_inv(p[0])));
    case D_LOGN: return s_exp(eval_cmd(C_INVNORM, 3, (E *[]){pr, p[0], p[1]}));
    }
    return NULL;
}

/* smallest integer x with cdf(x) >= p (cdf from binomcdf / poissoncdf), by bisection */
static E *inv_discrete(XP, int op, E **a) {
    double q = dbl(X, a[0]), lo = -1, hi;
    if (!(q >= 0 && q <= 1)) cas_throw(CAS_ERR_DOMAIN, "probability must be in [0,1]");
    double par = dbl(X, a[1]), pp = op == N_INVBINOM ? dbl(X, a[2]) : 0;
    if (op == N_INVBINOM) {
        if (par < 0 || par != (double)(long)par || !(pp >= 0 && pp <= 1)) cas_throw(CAS_ERR_DOMAIN, "invBinom: n >= 0 integer, 0 <= p <= 1");
        hi = par;
    } else {
        if (!(par > 0)) cas_throw(CAS_ERR_DOMAIN, "invPoisson: mean must be positive");
        hi = (double)(long)(par + 12 * X->sqrt(par) + 20);
    }
    while (hi - lo > 1) { /* cdf(lo) < q <= cdf(hi) */
        poll_intr();
        double mid = (double)(long)((lo + hi) / 2);
        E *c = op == N_INVBINOM ? eval_cmd(C_BINOMCDF, 3, (E *[]){flt(X, par), flt(X, pp), flt(X, mid)})
                                : eval_cmd(C_POISSONCDF, 2, (E *[]){flt(X, par), flt(X, mid)});
        if (dbl(X, c) >= q * (1 - 1e-13)) hi = mid;
        else lo = mid;
    }
    return mk_int((long)hi);
}

static E *distcmd(XP, int op, uint32_t n, E **a) {
    if (op >= N_INVBINOM) return inv_discrete(X, op, a);
    int d = (op - N_EXPPDF) / 3, kind = (op - N_EXPPDF) % 3, np = d == D_EXP ? 1 : 2;
    E **p = a + n - np;
    double dp[2], v;
    for (uint32_t i = 0; i < n; i++)
        if (!(kind == 1 && i < 2 ? bound(X, a[i], &v) : to_double(a[i], &v))) return NULL;
    for (int i = 0; i < np; i++) dp[i] = dbl(X, p[i]);
    int ok = d == D_UNIF ? dp[0] < dp[1] : d == D_LOGN ? dp[1] > 0 : dp[0] > 0 && (np < 2 || dp[1] > 0);
    if (!ok) cas_throw(CAS_ERR_DOMAIN, "distribution parameters out of range");
    E *r = kind == 0 ? dpdf(X, d, a[0], p, dp)
         : kind == 2 ? dinv(X, d, a[0], p, dp)
                     : s_sub(dcdf(X, d, a[1], p, dp), dcdf(X, d, a[0], p, dp));
    for (uint32_t i = 0; i < n; i++) /* a decimal argument gives a decimal result */
        if (has_float(a[i])) return eval_cmd(C_APPROX, 1, &r);
    return r;
}

/* ------------------------------------------------------------- regression --- */
/* Least squares: the k coefficients c minimising |A c - y|^2 (A is m x k,
 * row-major) from the normal equations A^T A c = A^T y, solved by mat_rref
 * (exact for exact data).  Throws when A^T A is singular. */
static E **lsq(XP, uint32_t m, uint32_t k, E **A, E **y) {
    E **rows = salloc(sizeof(E *) * k);
    for (uint32_t i = 0; i < k; i++) {
        E **r = salloc(sizeof(E *) * (k + 1));
        for (uint32_t j = 0; j <= k; j++) {
            E *sm = K->c0;
            for (uint32_t t = 0; t < m; t++) sm = s_add2(sm, s_mul2(A[t * k + i], j < k ? A[t * k + j] : y[t]));
            r[j] = sm;
        }
        rows[i] = s_list(k + 1, r);
    }
    long rank;
    E *R = mat_rref(s_list(k, rows), &rank);
    if (rank < (long)k) cas_throw(CAS_ERR_DOMAIN, "regression: singular system (too few distinct points)");
    E **c = salloc(sizeof(E *) * k);
    for (uint32_t i = 0; i < k; i++) {
        if (!is_zero(s_sub(EK(EK(R)[i])[i], K->c1))) cas_throw(CAS_ERR_DOMAIN, "regression: singular system");
        c[i] = EK(EK(R)[i])[k];
    }
    return c;
}

/* R^2 = 1 - SSres/SStot of the fitted values f[] against y[] */
static E *rsq(XP, uint32_t m, E **y, E **f) {
    E *my = K->c0, *sr = K->c0, *st = K->c0;
    for (uint32_t i = 0; i < m; i++) my = s_add2(my, y[i]);
    my = s_div(my, mk_int(m));
    for (uint32_t i = 0; i < m; i++) {
        E *e = s_sub(y[i], f[i]), *d = s_sub(y[i], my);
        sr = s_add2(sr, s_mul2(e, e)), st = s_add2(st, s_mul2(d, d));
    }
    return eqn(X, "R2", is_zero(st) ? K->c1 : s_sub(K->c1, s_div(sr, st)));
}

/* two numeric lists of equal length >= 2 */
static uint32_t xy(XP, E *a, E *b, E ***xs, E ***ys) {
    a = num_list(X, a, 0), b = num_list(X, b, 0);
    if (a->n != b->n || a->n < 2) cas_throw(CAS_ERR_DOMAIN, "lists of equal length >= 2");
    for (uint32_t i = 0; i < a->n; i++)
        if (!is_const_expr(EK(a)[i]) || !is_const_expr(EK(b)[i])) cas_throw(CAS_ERR_DOMAIN, "expected numbers");
    *xs = EK(a), *ys = EK(b);
    return a->n;
}

/* ln of each element as a decimal (data must be positive) */
static E **lnlist(XP, uint32_t m, E **v) {
    E **r = salloc(sizeof(E *) * m);
    for (uint32_t i = 0; i < m; i++) {
        if (!(dbl(X, v[i]) > 0)) cas_throw(CAS_ERR_DOMAIN, "regression: data must be positive");
        r[i] = mk_flt(0), r[i] = s_fn1(F_LN, s_add2(r[i], v[i]));
        if (r[i]->t != E_FLT) r[i] = eval_cmd(C_APPROX, 1, &r[i]);
    }
    return r;
}

/* fitted line through (u, w): [c0, c1, R2=] */
static E **line(XP, uint32_t m, E **u, E **w, E **r2) {
    E **A = salloc(sizeof(E *) * 2 * m), **f = salloc(sizeof(E *) * m);
    for (uint32_t i = 0; i < m; i++) A[2 * i] = K->c1, A[2 * i + 1] = u[i];
    E **c = lsq(X, m, 2, A, w);
    for (uint32_t i = 0; i < m; i++) f[i] = s_add2(c[0], s_mul2(c[1], u[i]));
    *r2 = rsq(X, m, w, f);
    return c;
}

/* the fitted expression and R2 */
static E *fitres(XP, E *f, E *r2) { return s_list(2, (E *[]){f, r2}); }

/* Levenberg-Marquardt fit of expr(x; params) to (xs, ys) from the guesses */
static E *nlfit(XP, E *f, E *P, E *G, E *a3, E *a4, E *var) {
    E **xs, **ys;
    uint32_t m = xy(X, a3, a4, &xs, &ys);
    if (P->t != E_LIST || G->t != E_LIST || P->n != G->n || !P->n || P->n > 8)
        cas_throw(CAS_ERR_DOMAIN, "nlfit: parameter and guess lists of equal length (1..8)");
    uint32_t k = P->n;
    E **sym = salloc(sizeof(E *) * (k + 1)), **val = salloc(sizeof(E *) * (k + 1)), **df = salloc(sizeof(E *) * k);
    double *p = salloc(sizeof(double) * k * (k + 4) + sizeof(double) * m * (k + 1) * 2);
    double *np = p + k, *g = np + k, *dl = g + k, *H = dl + k, *J = H + k * k, *r = J + m * k, *r2 = r + m;
    for (uint32_t j = 0; j < k; j++) sym[j] = EK(P)[j], p[j] = dbl(X, EK(G)[j]), df[j] = diff(f, sym[j]);
    sym[k] = var;
    /* residuals (and Jacobian when J) at q; returns the sum of squares (inf if undefined) */
#define NL_EVAL(q, res, jac)                                                                 \
    ({                                                                                       \
        double ss_ = 0;                                                                      \
        for (uint32_t j = 0; j < k; j++) val[j] = mk_flt((q)[j]);                            \
        for (uint32_t i = 0; i < m && ss_ < 1e300; i++) {                                    \
            double fv;                                                                       \
            val[k] = xs[i];                                                                  \
            E *e = eval_cmd(C_APPROX, 1, (E *[]){subst_many(f, k + 1, sym, val)});           \
            if (!to_double(e, &fv) || fv != fv) { ss_ = 1e301; break; }                     \
            (res)[i] = dbl(X, ys[i]) - fv, ss_ += (res)[i] * (res)[i];                       \
            for (uint32_t j = 0; (jac) && j < k; j++) {                                      \
                E *d = eval_cmd(C_APPROX, 1, (E *[]){subst_many(df[j], k + 1, sym, val)});   \
                if (!to_double(d, &J[i * k + j])) J[i * k + j] = 0;                          \
            }                                                                                \
        }                                                                                    \
        ss_;                                                                                 \
    })
    double ss = NL_EVAL(p, r, 1), lam = 1e-3;
    if (ss >= 1e300) cas_throw(CAS_ERR_DOMAIN, "nlfit: model undefined at the initial guess");
    for (int it = 0; it < 200; it++) {
        poll_intr();
        for (uint32_t a = 0; a < k; a++) {
            g[a] = 0;
            for (uint32_t i = 0; i < m; i++) g[a] += J[i * k + a] * r[i];
            for (uint32_t b = 0; b < k; b++) {
                double h = 0;
                for (uint32_t i = 0; i < m; i++) h += J[i * k + a] * J[i * k + b];
                H[a * k + b] = h;
            }
        }
        int better = 0;
        double nss = ss;
        while (lam < 1e12) {
            /* solve (H + lam diag H) dl = g by Gaussian elimination with partial pivoting */
            double *M = salloc(sizeof(double) * k * (k + 1));
            for (uint32_t a = 0; a < k; a++) {
                for (uint32_t b = 0; b < k; b++) M[a * (k + 1) + b] = H[a * k + b] + (a == b ? lam * (H[a * k + a] + 1e-12) : 0);
                M[a * (k + 1) + k] = g[a];
            }
            int sing = 0;
            for (uint32_t c = 0; c < k && !sing; c++) {
                uint32_t pv = c;
                for (uint32_t a = c + 1; a < k; a++)
                    if ((M[a * (k + 1) + c] < 0 ? -M[a * (k + 1) + c] : M[a * (k + 1) + c]) >
                        (M[pv * (k + 1) + c] < 0 ? -M[pv * (k + 1) + c] : M[pv * (k + 1) + c])) pv = a;
                if (M[pv * (k + 1) + c] == 0) { sing = 1; break; }
                for (uint32_t b = 0; b <= k; b++) {
                    double t = M[c * (k + 1) + b];
                    M[c * (k + 1) + b] = M[pv * (k + 1) + b], M[pv * (k + 1) + b] = t;
                }
                for (uint32_t a = 0; a < k; a++)
                    if (a != c) {
                        double fct = M[a * (k + 1) + c] / M[c * (k + 1) + c];
                        for (uint32_t b = c; b <= k; b++) M[a * (k + 1) + b] -= fct * M[c * (k + 1) + b];
                    }
            }
            if (sing) { lam *= 10; continue; }
            for (uint32_t a = 0; a < k; a++) dl[a] = M[a * (k + 1) + k] / M[a * (k + 1) + a], np[a] = p[a] + dl[a];
            nss = NL_EVAL(np, r2, 0);
            if (nss < ss) { better = 1; break; }
            lam *= 10;
        }
        if (!better) break;
        double impr = ss - nss;
        for (uint32_t a = 0; a < k; a++) p[a] = np[a];
        lam = lam / 10 < 1e-12 ? 1e-12 : lam / 10;
        ss = NL_EVAL(p, r, 1);
        if (impr <= 1e-14 * (ss + 1e-300)) break;
    }
#undef NL_EVAL
    double my = 0, st = 0;
    for (uint32_t i = 0; i < m; i++) my += dbl(X, ys[i]);
    my /= m;
    for (uint32_t i = 0; i < m; i++) st += (dbl(X, ys[i]) - my) * (dbl(X, ys[i]) - my);
    for (uint32_t j = 0; j < k; j++) val[j] = mk_flt(p[j]);
    return fitres(X, subst_many(f, k, sym, val), eqn(X, "R2", mk_flt(st > 0 ? 1 - ss / st : 1)));
}

static E *regcmd(XP, int op, uint32_t n, E **a) {
    E *x = mk_sym(S_X), *r2;
    if (op == N_NLFIT) return nlfit(X, a[0], a[1], a[2], a[3], a[4], n > 5 ? a[5] : x);
    if (op == N_MULTIREG) {
        E *V = a[0], *Y = num_list(X, a[1], 0);
        if (V->t != E_LIST || !V->n || V->n > 16) cas_throw(CAS_ERR_DOMAIN, "multireg: a list of variable lists");
        uint32_t m = Y->n, k = V->n + 1;
        E **A = salloc(sizeof(E *) * m * k), **f = salloc(sizeof(E *) * m);
        for (uint32_t j = 1; j < k; j++) {
            E *col = EK(V)[j - 1];
            if (col->t != E_LIST || col->n != m) cas_throw(CAS_ERR_DOMAIN, "multireg: lists of equal length");
            for (uint32_t i = 0; i < m; i++) A[i * k + j] = EK(col)[i];
        }
        for (uint32_t i = 0; i < m; i++) A[i * k] = K->c1;
        E **c = lsq(X, m, k, A, EK(Y)), *res = c[0];
        for (uint32_t i = 0; i < m; i++) {
            f[i] = c[0];
            for (uint32_t j = 1; j < k; j++) f[i] = s_add2(f[i], s_mul2(c[j], A[i * k + j]));
        }
        char nm[4] = {'x', 0, 0, 0};
        for (uint32_t j = 1; j < k; j++) {
            nm[1] = (char)(j < 10 ? '0' + j : '1'), nm[2] = j < 10 ? 0 : (char)('0' + j - 10);
            res = s_add2(res, s_mul2(c[j], mk_sym_name(nm)));
        }
        return fitres(X, res, rsq(X, m, EK(Y), f));
    }
    E **xs, **ys;
    uint32_t m = xy(X, a[0], a[1], &xs, &ys);
    switch (op) {
    case N_POLYREG: {
        long d;
        if (!num_is_small_int(a[2], &d) || d < 1 || d >= (long)m || d > 20)
            cas_throw(CAS_ERR_DOMAIN, "polyreg: degree from 1 to (number of points - 1)");
        uint32_t k = (uint32_t)d + 1;
        E **A = salloc(sizeof(E *) * m * k), **f = salloc(sizeof(E *) * m);
        for (uint32_t i = 0; i < m; i++)
            for (uint32_t j = 0; j < k; j++) A[i * k + j] = j ? s_mul2(A[i * k + j - 1], xs[i]) : K->c1;
        E **c = lsq(X, m, k, A, ys), *res = K->c0;
        for (uint32_t i = 0; i < m; i++) {
            f[i] = K->c0;
            for (uint32_t j = 0; j < k; j++) f[i] = s_add2(f[i], s_mul2(c[j], A[i * k + j]));
        }
        for (uint32_t j = 0; j < k; j++) res = s_add2(res, s_mul2(c[j], s_powi(x, j)));
        return fitres(X, res, rsq(X, m, ys, f));
    }
    case N_EXPREG: { /* ln y = A + B x: y = a b^x */
        E **c = line(X, m, EK(eval_cmd(C_APPROX, 1, (E *[]){s_list(m, xs)})), lnlist(X, m, ys), &r2);
        return fitres(X, s_mul2(s_exp(c[0]), s_pow(s_exp(c[1]), x)), r2);
    }
    case N_POWREG: { /* ln y = A + B ln x: y = a x^b */
        E **c = line(X, m, lnlist(X, m, xs), lnlist(X, m, ys), &r2);
        return fitres(X, s_mul2(s_exp(c[0]), s_pow(x, c[1])), r2);
    }
    case N_LOGREG: { /* y = a + b ln x */
        E **c = line(X, m, lnlist(X, m, xs), EK(eval_cmd(C_APPROX, 1, (E *[]){s_list(m, ys)})), &r2);
        return fitres(X, s_add2(c[0], s_mul2(c[1], s_fn1(F_LN, x))), r2);
    }
    }
    return NULL;
}

static E *ncmd(XP, int op, uint32_t n, E **a) {
    if (op >= N_POLYREG) return regcmd(X, op, n, a);
    if (op >= N_EXPPDF) return distcmd(X, op, n, a);
    if (op >= N_ZTEST) return statcmd2(X, op, n, a);
    if (op >= N_COV) return statcmd(X, op, n, a);
    switch (op) {
    case N_FACT2: {
        if (!is_intlit(a[0])) return NULL;
        long m = nint(X, a[0], -1, 20000);
        E *r = K->c1;
        for (; m > 1; m -= 2) r = int_mul_si(r, m);
        return r;
    }
    case N_POCH: {
        if (!is_intlit(a[1])) return NULL;
        long k = nint(X, a[1], 0, 1000);
        E **t = salloc(sizeof(E *) * (size_t)(k + 1));
        for (long i = 0; i < k; i++) t[i] = s_add2(a[0], mk_int(i));
        return s_mul((uint32_t)k, t);
    }
    case N_BERN: return is_intlit(a[0]) ? bernoulli(nint(X, a[0], 0, 400)) : NULL;
    case N_FIB: {
        if (!is_intlit(a[0])) return NULL;
        long m = nint(X, a[0], -20000, 20000), am = m < 0 ? -m : m;
        E *f0 = K->c0, *f1 = K->c1;
        for (long i = 0; i < am; i++) {
            E *t = int_add(f0, f1);
            f0 = f1, f1 = t;
        }
        return m < 0 && !(am & 1) ? int_neg(f0) : f0;
    }
    case N_ST1: case N_ST2: {
        if (!is_intlit(a[0]) || !is_intlit(a[1])) return NULL;
        long N = nint(X, a[0], 0, 300), k = nint(X, a[1], 0, 300);
        if (k > N) return K->c0;
        E **row = salloc(sizeof(E *) * (size_t)(N + 2));
        row[0] = K->c1;
        for (long j = 1; j <= N; j++) row[j] = K->c0;
        for (long i = 1; i <= N; i++) {
            poll_intr();
            for (long j = i; j >= 1; j--) /* s(i,j) = s(i-1,j-1) - (i-1) s(i-1,j); S(i,j) = S(i-1,j-1) + j S(i-1,j) */
                row[j] = int_add(row[j - 1], int_mul_si(row[j], op == N_ST1 ? -(i - 1) : j));
            row[0] = K->c0;
        }
        return row[k];
    }
    case N_PART: {
        if (!is_intlit(a[0])) return NULL;
        long N = nint(X, a[0], 0, 5000);
        E **p = salloc(sizeof(E *) * (size_t)(N + 1));
        p[0] = K->c1;
        for (long m = 1; m <= N; m++) {
            poll_intr();
            E *s = K->c0;
            for (long k = 1;; k++) {
                long g1 = k * (3 * k - 1) / 2, g2 = k * (3 * k + 1) / 2;
                if (g1 > m) break;
                E *t = g2 <= m ? int_add(p[m - g1], p[m - g2]) : p[m - g1];
                s = k & 1 ? int_add(s, t) : int_sub(s, t);
            }
            p[m] = s;
        }
        return p[N];
    }
    case N_PRIMROOT: {
        if (!is_intlit(a[0])) return NULL;
        long p = nint(X, a[0], 2, 2147483647L);
        if (!int_isprime(a[0])) cas_throw(CAS_ERR_DOMAIN, "primroot: expected a prime");
        gfc gc = {X, (uint32_t)p, 0}, *G = &gc;
        E *fl = int_factor(mk_int(p - 1));
        for (uint32_t g = 1; g < (uint32_t)p; g++) {
            int ok = p == 2 || g > 1;
            for (uint32_t i = 0; ok && i < fl->n; i++)
                if (powm(G, g, (uint64_t)(p - 1) / (uint64_t)nint(X, K0(EK(fl)[i]), 2, p)) == 1) ok = 0;
            if (ok) return mk_int(g);
        }
        return NULL;
    }
    case N_CONTFRAC: return contfrac(X, a[0], n > 1 ? nint(X, a[1], 1, 200) : 200);
    case N_Q1: case N_Q3: return quartile(X, a[0], op == N_Q3);
    case N_MODE: return mode(X, a[0]);
    case N_SDP: case N_VARP: {
        E *L = num_list(X, a[0], 0);
        if (L->n < 2) cas_throw(CAS_ERR_DOMAIN, "need at least 2 values");
        E *v = s_mul2(eval_cmd(C_VARIANCE, 1, &L), mk_frac(L->n, L->n - 1));
        return op == N_SDP ? s_sqrt(v) : v;
    }
    case N_LINREG: {
        E *Xs = num_list(X, a[0], 0), *Y = num_list(X, a[1], 0);
        if (Xs->n != Y->n || Xs->n < 2) cas_throw(CAS_ERR_DOMAIN, "linreg: lists of equal length >= 2");
        E *mx = eval_cmd(C_MEAN, 1, &Xs), *my = eval_cmd(C_MEAN, 1, &Y), *sxy = K->c0, *sxx = K->c0;
        for (uint32_t i = 0; i < Xs->n; i++) {
            E *dx = s_sub(EK(Xs)[i], mx);
            sxy = s_add2(sxy, s_mul2(dx, s_sub(EK(Y)[i], my)));
            sxx = s_add2(sxx, s_mul2(dx, dx));
        }
        if (is_zero(sxx)) cas_throw(CAS_ERR_DOMAIN, "linreg: all x equal");
        E *m = s_div(sxy, sxx);
        return s_add2(s_mul2(m, mk_sym(S_X)), s_sub(my, s_mul2(m, mx)));
    }
    case N_FACTORMOD: return factormod(X, a[0], a[1]);
    case N_MSOLVE: return msolve(X, a[0], a[1], a[2]);
    case N_UNION: case N_INTERSECT: case N_SETDIFF: return setop(X, op - N_UNION, n, a); /* 0 union, 1 intersect, 2 setdiff */
    case N_GRAMSCHMIDT: return gramschmidt(X, a[0]);
    }
    return NULL;
}

static E *nt_handler(XP, int op, uint32_t n, E **a) { return ncmd(X, op, n, a); }

int casx_nt_register(XP) { return X->reg("casx", ntab, (int)(sizeof ntab / sizeof ntab[0]), nt_handler); }
