/*
 * trigsolve.c -- general solutions of periodic equations and solutions in an interval
 * (algorithm slot CASX_FN_SOLVE, casx.h: the solve command asks it before the core's
 * solve_expr; without this module solve gives the principal solutions).
 *
 * solve(eq, x): when every occurrence of x in eq = 0 is inside sin, cos or tan (sec, csc and
 * cot are written with them) of a linear argument d_i x + b_i, and the d_i are rational
 * multiples of one another, eq is periodic with period T = lcm of the 2 pi / d_i (pi / d_i for
 * tan).  The core's solutions (one period's worth: asin and pi - asin, ...) are reduced mod T,
 * checked for completeness and grouped into families s + p n, n an integer:
 *   - completeness: x with d_i x + b_i = pi (the half-angle substitution's blind spot) is added
 *     when it solves the equation; then f is sampled over one period and every sign change that
 *     bisects to a root (not a pole) must be one of the residues, or the module declines;
 *   - grouping: residues r, r + T/m, ..., r + (m-1) T/m with m as large as possible become one
 *     family with step T/m (cos(x) = 0: pi/2 + pi n; sin(x) cos(x) = 0: pi n/2); the
 *     representative is the core's solution of the family closest to 0 (positive on a tie),
 *     or a rational multiple of T at the same place that solves the equation exactly.
 *   The integer parameter is the symbol n_1 (n_2, ... when n_1 is taken: it occurs in the
 *   equation, has a value or a sign assumption), declared an integer as assume(n_1, integer)
 *   does, so sin(pi/6 + 2 pi n_1) simplifies to 1/2 afterwards (simp.c drop_period).  With
 *   other symbols in the equation the core's solutions get + T n_1 without the grouping and
 *   the checks.
 * Decimals in the equation give decimal results.
 * solve(eq, x, a, b): the solutions in [a, b], sorted: the families' members for a periodic
 * equation (at most MAXN of them), otherwise the core's solutions that lie in [a, b].
 */
#include "casx_mod.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

#define MAXK 16  /* trig kernels */
#define MAXR 64  /* residues in a period */
#define MAXN 100 /* solutions listed for an interval */

typedef struct {
    E *u, *d; /* the argument and its coefficient of x */
    int c;    /* period 2 pi / d (c = 2) or pi / d (c = 1, tan) */
} kern_t;

static double dabs(double v) { return v < 0 ? -v : v; }
static double dfloor(double v) {
    double t = (double)(int64_t)v;
    return t > v ? t - 1 : t;
}
static int64_t gcd64(int64_t a, int64_t b) {
    while (b) {
        int64_t t = a % b;
        a = b, b = t;
    }
    return a < 0 ? -a : a;
}

/* sec, csc, cot written with sin, cos (the core's solve inverts those) */
static E *rw(XP, E *e) {
    stack_check();
    if (e->t == E_FN && e->n == 1 && (e->s == F_SEC || e->s == F_CSC || e->s == F_COT)) {
        E *u = rw(X, K0(e)), *s = s_fn1(F_SIN, u), *c = s_fn1(F_COS, u);
        return e->s == F_SEC ? s_inv(c) : e->s == F_CSC ? s_inv(s) : s_div(c, s);
    }
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN && e->t != E_LIST) return e;
    E **k = salloc(sizeof(E *) * (e->n + 1));
    int ch = 0;
    for (uint32_t i = 0; i < e->n; i++) ch |= (k[i] = rw(X, EK(e)[i])) != EK(e)[i];
    return ch ? X->rebuild(e, k) : e;
}

/* the trig kernels of e in x; 0 if x occurs outside them (not periodic) */
static int kerns(XP, E *e, E *x, kern_t *ks, int *nk) {
    stack_check();
    if (freeof(e, x)) return 1;
    if (e->t == E_FN && e->n == 1 && (e->s == F_SIN || e->s == F_COS || e->s == F_TAN)) {
        E *d = diff(K0(e), x);
        if (freeof(d, x) && !is_zero(d)) {
            if (*nk == MAXK) return 0;
            ks[*nk].u = K0(e), ks[*nk].d = d, ks[(*nk)++].c = e->s == F_TAN ? 1 : 2;
            return 1;
        }
    }
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (!kerns(X, EK(e)[i], x, ks, nk)) return 0;
    return 1;
}

/* every symbol of s (but the constants) occurs in f */
static int syms_in(XP, E *s, E *f) {
    if (s->t == E_SYM) return is_const_expr(s) || !freeof(f, s);
    if (s->t != E_ADD && s->t != E_MUL && s->t != E_POW && s->t != E_FN && s->t != E_LIST) return 1;
    for (uint32_t i = 0; i < s->n; i++)
        if (!syms_in(X, EK(s)[i], f)) return 0;
    return 1;
}

/* p/q of a small rational */
static int rat64(XP, E *r, int64_t *p, int64_t *q) {
    long a, b = 1;
    if (r->t == E_INT) return num_is_small_int(r, &a) && (*p = a, *q = 1, 1);
    if (r->t != E_RAT || !num_is_small_int(K0(r), &a) || !num_is_small_int(K1(r), &b)) return 0;
    *p = a, *q = b;
    return 1;
}

/* a symbol other than x and the constants */
static int has_param(XP, E *e, E *x) {
    if (e->t == E_SYM) return !is_const_expr(e) && !expr_eq(e, x);
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN && e->t != E_LIST) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_param(X, EK(e)[i], x)) return 1;
    return 0;
}

/* f at x = t (protected: a = [f, x, t, out]); non-NULL when real and finite */
static E *at_p(XP, void **a) {
    double t = *(double *)a[2], re, im;
    if (!X->eval_complex(subst((E *)a[0], (E *)a[1], mk_flt(t)), &re, &im)) return NULL;
    if (!(re - re == 0) || dabs(im) > 1e-9 * (1 + dabs(re))) return NULL;
    *(double *)a[3] = re;
    return K->c1;
}
static int fat(XP, E *f, E *x, double t, double *v) {
    char *m = K->s_top;
    void *a[4] = {f, x, &t, v};
    E *r = X->protect(at_p, a);
    K->s_top = m;
    return r != NULL;
}

/* f at the exact point s is exactly 0 (protected: a = [f, x, s]) */
static E *zero_p(XP, void **a) {
    E *v = subst((E *)a[0], (E *)a[1], (E *)a[2]);
    double re, im;
    if (!X->eval_complex(v, &re, &im) || dabs(re) + dabs(im) > 1e-12) return NULL;
    return is_zero(v) || is_zero(eval_cmd(C_SIMPLIFY, 1, &v)) ? K->c1 : NULL;
}

/* distance of a and b mod P */
static double cdist(double a, double b, double P) {
    double d = a - b;
    d -= P * dfloor(d / P);
    return d < P - d ? d : P - d;
}

/* residue index of v (mod P) in r[0..n), or -1 */
static int find_res(double v, const double *r, int n, double P) {
    for (int i = 0; i < n; i++)
        if (cdist(v, r[i], P) < 1e-8 * P) return i;
    return -1;
}

/* the integer parameter: n_1, ..., n_9 (free in e, no value, no sign assumption) */
static E *int_param(XP, E *e) {
    char nm[4] = {'n', '_', '1', 0};
    for (; nm[2] <= '9'; nm[2]++) {
        E *s = mk_sym_name(nm);
        if (s->t != E_SYM || !freeof(e, s)) continue;
        uint8_t *fl = &K->syms[s->s].assume;
        if (K->syms[s->s].val || K->syms[s->s].fdef || (*fl & ~A_INT)) continue;
        *fl |= A_INT;
        return s;
    }
    cas_throw(CAS_ERR_DOMAIN, "solve: no name for the integer parameter");
}

/* numeric value of a bound (inf, -inf allowed) */
static double bound(XP, E *b) {
    double v;
    if (expr_eq(b, K->inf_)) return 1e300;
    if (expr_eq(b, s_neg(K->inf_))) return -1e300;
    if (!is_const_expr(b) || !to_double(b, &v)) cas_throw(CAS_ERR_DOMAIN, "solve: the interval bounds must be real numbers");
    return v;
}

static void sort_vals(E **s, double *v, uint32_t n) {
    for (uint32_t i = 1; i < n; i++)
        for (uint32_t j = i; j > 0 && v[j - 1] > v[j]; j--) {
            double t = v[j];
            E *e = s[j];
            v[j] = v[j - 1], s[j] = s[j - 1], v[j - 1] = t, s[j - 1] = e;
        }
}

/* the solutions in sols inside [lo, hi] */
static E *in_interval(XP, E *sols, double lo, double hi) {
    if (sols->n == 1 && !is_const_expr(EK(sols)[0])) return sols; /* [x]: every x */
    E **s = salloc(sizeof(E *) * (sols->n + 1));
    double *v = salloc(sizeof(double) * (sols->n + 1));
    uint32_t n = 0;
    for (uint32_t i = 0; i < sols->n; i++) {
        double w;
        if (!is_const_expr(EK(sols)[i]) || !to_double(EK(sols)[i], &w))
            cas_throw(CAS_ERR_UNSUPPORTED, "solve: the solutions are not numeric, no interval");
        double tol = 1e-12 * (1 + dabs(w));
        if (w >= lo - tol && w <= hi + tol) s[n] = EK(sols)[i], v[n++] = w;
    }
    sort_vals(s, v, n);
    return s_list(n, s);
}

/* not sure of every solution: the core's principal ones, or no list for an interval */
#define DECLINE                                                         \
    do {                                                                \
        if (iv) cas_throw(CAS_ERR_UNSUPPORTED, "cannot solve");         \
        return NULL;                                                    \
    } while (0)

static E *solve_slot(XP, uint32_t na, E **A) {
    E *eq = A[0], *x = A[1];
    int iv = na == 4;
    double lo = 0, hi = 0;
    if (eq->t == E_LIST && eq->n == 1) eq = EK(eq)[0];
    if (x->t == E_LIST && x->n == 1) x = EK(x)[0];
    if (eq->t == E_LIST || x->t != E_SYM || (eq->t == E_FN && eq->s != F_EQ && eq->s >= F_EQ && eq->s <= F_NE)) {
        if (iv) cas_throw(CAS_ERR_DOMAIN, "solve: an interval needs one equation and a variable");
        return NULL;
    }
    if (iv) {
        lo = bound(X, A[2]), hi = bound(X, A[3]);
        if (lo > hi) {
            double t = lo;
            lo = hi, hi = t;
        }
    }
    /* sec, csc, cot -> sin, cos */
    E *eq2 = eq;
    if (eq->t == E_FN && eq->s == F_EQ) {
        E *l = rw(X, K0(eq)), *r = rw(X, K1(eq));
        if (l != K0(eq) || r != K1(eq)) eq2 = mk_fn(F_EQ, 2, (E *[]){l, r});
    } else
        eq2 = rw(X, eq);
    E *f = eq2->t == E_FN && eq2->s == F_EQ ? s_sub(K0(eq2), K1(eq2)) : eq2;
    kern_t ks[MAXK];
    int nk = 0;
    int periodic = kerns(X, f, x, ks, &nk) && nk > 0;
    /* the period T = L pi / d_0 with L = lcm of the kernels' c q / p (d_i / d_0 = p / q) */
    int64_t Ln = 1, Ld = 1;
    double Tmin = 0; /* the shortest kernel period, in units of pi / d_0 */
    for (int i = 0; periodic && i < nk; i++) {
        int64_t p, q;
        if (!rat64(X, s_div(ks[i].d, ks[0].d), &p, &q) || p > 10000 || p < -10000 || q > 10000) {
            periodic = 0;
            break;
        }
        if (p < 0) p = -p;
        int64_t a = ks[i].c * q, b = p, g = gcd64(a, b);
        a /= g, b /= g;
        Ln = Ln / gcd64(Ln, a) * a, Ld = gcd64(Ld, b);
        if (i == 0) Ld = b;
        if (Ln > 100000) periodic = 0;
        if (!Tmin || (double)a / b < Tmin) Tmin = (double)a / b;
    }
    if (!periodic && !iv) return NULL; /* not periodic: the core's solve */
    E *sols = X->solve_expr(eq2, x, 0);
    if (sols->t != E_LIST || (sols->n == 1 && expr_eq(EK(sols)[0], x))) return sols; /* identity: [x] */
    if (!periodic) return in_interval(X, sols, lo, hi);
    E *T = s_div(s_mul2(mk_frac(Ln, Ld), K->pi_), ks[0].d);
    double Td;
    int param = has_param(X, f, x);
    if (!param && to_double(T, &Td) && Td < 0) T = s_neg(T), Td = -Td;
    if (param) {
        if (iv) cas_throw(CAS_ERR_UNSUPPORTED, "solve: the solutions are not numeric, no interval");
        for (uint32_t i = 0; i < sols->n; i++)
            if (!syms_in(X, EK(sols)[i], f)) DECLINE; /* a stray symbol: not a solution family */
        E *nn = int_param(X, f), **out = salloc(sizeof(E *) * (sols->n + 1));
        for (uint32_t i = 0; i < sols->n; i++) out[i] = s_add2(EK(sols)[i], s_mul2(T, nn));
        return s_list(sols->n, out);
    }
    if (!(Td > 0)) DECLINE;
    int flt = has_float(f); /* decimals in: decimals out (no exact representatives) */
    /* the candidates: the core's solutions and the points where a kernel's argument is pi */
    uint32_t nc = 0, cap = sols->n + MAXR + 1;
    E **cs = salloc(sizeof(E *) * cap);
    double *cv = salloc(sizeof(double) * cap), res[MAXR];
    int nr = 0, *cr = salloc(sizeof(int) * cap);
    for (uint32_t i = 0; i < sols->n; i++) {
        double v;
        if (!is_const_expr(EK(sols)[i]) || !to_double(EK(sols)[i], &v)) DECLINE;
        cs[nc] = EK(sols)[i], cv[nc++] = v;
    }
    for (int i = 0; i < nk; i++) {
        if (ks[i].c != 2) continue;
        E *x0 = s_div(s_sub(K->pi_, subst(ks[i].u, x, K->c0)), ks[i].d), *P = s_div(s_mul2(K->c2, K->pi_), ks[i].d);
        double pv, x0v;
        if (!to_double(P, &pv) || !to_double(x0, &x0v)) DECLINE;
        long reps = (long)(Td / dabs(pv) + 0.5);
        for (long j = 0; j < reps && j < 8 && nc < cap; j++) {
            E *s = s_add2(x0, s_mul2(mk_int(j), P));
            double v = x0v + (double)j * pv;
            int dup = 0;
            for (uint32_t q = 0; q < nc && !dup; q++) dup = cdist(v, cv[q], Td) < 1e-8 * Td;
            void *a[3] = {f, x, s};
            if (!dup && X->protect(zero_p, a)) cs[nc] = s, cv[nc++] = v;
        }
    }
    for (uint32_t i = 0; i < nc; i++) {
        int r = find_res(cv[i], res, nr, Td);
        if (r < 0) {
            if (nr == MAXR) DECLINE;
            r = nr, res[nr] = cv[i] - Td * dfloor(cv[i] / Td), nr++;
        }
        cr[i] = r;
    }
    /* completeness: every sign change of f over one period that is a root is a residue */
    long ns = (long)(48 * (Ln / (double)Ld) / Tmin);
    if (ns < 96) ns = 96;
    if (ns > 1536) ns = 1536;
    double h = Td / ns, t0 = 0.1234567 * h, pf = 0, scale = 1;
    int pok = 0;
    for (long i = 0; i <= ns; i++) {
        double t = t0 + i * h, v;
        int ok = fat(X, f, x, t, &v);
        if (ok && dabs(v) > scale) scale = dabs(v) < 1e6 ? dabs(v) : 1e6;
        if (ok && pok && (v == 0 || (v < 0) != (pf < 0))) {
            double a = t - h, b = t, fa = pf, m = b, fm = v;
            int okm = 1;
            for (int it = 0; it < 60 && fm != 0 && okm; it++) {
                m = 0.5 * (a + b);
                if (!(okm = fat(X, f, x, m, &fm)) || fm == 0) break;
                if ((fm < 0) == (fa < 0)) a = m, fa = fm;
                else b = m;
            }
            if (okm && dabs(fm) < 1e-6 * scale && find_res(m, res, nr, Td) < 0) DECLINE; /* a root it missed */
        }
        pok = ok, pf = v;
    }
    /* families: residue cosets of T/m, m as large as possible */
    int used[MAXR] = {0}, fam_of[MAXR], nf = 0, fm_m[MAXR];
    for (int m = nr; m >= 1; m--)
        for (int i = 0; i < nr; i++) {
            if (used[i]) continue;
            int ok = 1, mem[MAXR];
            for (int j = 0; j < m && ok; j++) {
                int r = find_res(res[i] + j * Td / m, res, nr, Td);
                ok = r >= 0 && !used[r];
                mem[j] = r;
                for (int q = 0; q < j && ok; q++) ok = mem[q] != r;
            }
            if (!ok) continue;
            for (int j = 0; j < m; j++) used[mem[j]] = 1, fam_of[mem[j]] = nf;
            fm_m[nf++] = m;
        }
    E *nn = nr ? int_param(X, f) : NULL;
    E **out = salloc(sizeof(E *) * (nf + 1)), **step = salloc(sizeof(E *) * (nf + 1));
    double *ov = salloc(sizeof(double) * (nf + 1));
    for (int k = 0; k < nf; k++) {
        int best = -1;
        for (uint32_t i = 0; i < nc; i++)
            if (fam_of[cr[i]] == k &&
                (best < 0 || dabs(cv[i]) < dabs(cv[best]) - 1e-12 || (dabs(cv[i]) <= dabs(cv[best]) + 1e-12 && cv[i] > cv[best])))
                best = (int)i;
        out[k] = cs[best], ov[k] = cv[best], step[k] = s_div(T, mk_int(fm_m[k]));
        /* a simpler representative: a rational multiple of T that solves exactly (2 atan(sqrt(2) - 1) -> pi/4) */
        for (int q = 1; q <= 48 && !flt; q++) {
            double y = ov[k] / Td * q, p = dfloor(y + 0.5);
            if (dabs(y - p) > 1e-9 * q) continue;
            E *c = s_mul2(mk_frac((int64_t)p, q), T);
            void *a[3] = {f, x, c};
            if (!expr_eq(c, out[k]) && X->protect(zero_p, a)) out[k] = c;
            break;
        }
    }
    /* sort the families by representative, keeping the steps with them */
    for (int i = 1; i < nf; i++)
        for (int j = i; j > 0 && ov[j - 1] > ov[j]; j--) {
            double t = ov[j];
            E *a = out[j], *b = step[j];
            ov[j] = ov[j - 1], out[j] = out[j - 1], step[j] = step[j - 1];
            ov[j - 1] = t, out[j - 1] = a, step[j - 1] = b;
        }
    if (!iv) {
        for (int k = 0; k < nf; k++) out[k] = s_add2(out[k], s_mul2(step[k], nn));
        E *r = s_list((uint32_t)nf, out);
        return flt ? eval_cmd(C_APPROX, 1, &r) : r;
    }
    /* [lo, hi]: the members s + k p of every family */
    double cnt = 0;
    for (int k = 0; k < nf; k++) {
        double p = Td / fm_m[k], eps = 1e-9;
        if ((ov[k] - lo) / p > 1e9 || (hi - ov[k]) / p > 1e9) {
            cnt = MAXN + 1;
            break;
        }
        double k0 = -dfloor((ov[k] - lo) / p + eps), k1 = dfloor((hi - ov[k]) / p + eps);
        if (k1 >= k0) cnt += k1 - k0 + 1;
    }
    if (cnt > MAXN) cas_throw(CAS_ERR_DOMAIN, "solve: more than 100 solutions in the interval");
    E **s = salloc(sizeof(E *) * (MAXN + 1));
    double *v = salloc(sizeof(double) * (MAXN + 1));
    uint32_t n = 0;
    for (int k = 0; k < nf; k++) {
        double p = Td / fm_m[k], eps = 1e-9;
        long k0 = (long)-dfloor((ov[k] - lo) / p + eps), k1 = (long)dfloor((hi - ov[k]) / p + eps);
        for (long j = k0; j <= k1 && n < MAXN; j++) s[n] = s_add2(out[k], s_mul2(mk_int(j), step[k])), v[n++] = ov[k] + j * p;
    }
    sort_vals(s, v, n);
    E *r = s_list(n, s);
    return flt ? eval_cmd(C_APPROX, 1, &r) : r;
}

int casx_trigsolve_register(XP) {
    X->reg_fn(CASX_FN_SOLVE, (void *)solve_slot); /* an older core without the slot: solve as before */
    return 0;
}
