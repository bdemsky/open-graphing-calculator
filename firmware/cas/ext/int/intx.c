/*
 * intx.c -- the casx integrator: antiderivatives the core's heuristic search (integ.c) does not
 * find.  The core calls it through the algorithm slot CASX_FN_INTEGRATE after its own search
 * failed (nested at most 2 deep: a strategy here calls the core's integrate on a rewritten
 * integrand, which may come back here once more).
 *
 * Strategies (cheapest first; each one ends in an integral the core or a nested call can do):
 *   1. exponentials of inverse hyperbolic / trigonometric functions become algebraic:
 *        e^(c atanh u)   = (1+u)^(c/2) (1-u)^(-c/2)
 *        e^(c i atan u)  = (1+i u)^(c/2) (1-i u)^(-c/2)
 *        e^(c asinh u)   = (u + sqrt(1+u^2))^c,   e^(c acosh u) = (u + sqrt(u-1) sqrt(u+1))^c
 *      (exact for the principal branches);
 *   2. radicals of linear forms (intrad.c): R(x, L1^(1/k), L2^(1/k)) with at most two linear
 *      forms, rationalised by t = L1^(1/k) L2^(-1/k) (Chebyshev / Euler);
 *   3. rational integrands with a binomial factor a + b t^k (k >= 3) or a trinomial
 *      a + b t^k + c t^(2k) in the denominator and symbolic coefficients (intrat.c): scaled to
 *      numeric coefficients or split over the roots of the quadratic.
 * Every antiderivative is checked before it is returned: its derivative must agree with the
 * integrand at sample points (the other symbols set to numbers in (0.2, 1.7), x in
 * (0.15, 1.85)); a mismatch at a point where the integrand is real rejects it.
 */
#include "intx.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

int ix_has_fn(XP, E *e, int id) {
    if (e->t == E_FN && e->s == id) return 1;
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN && e->t != E_LIST) return 0;
    stack_check();
    for (uint32_t i = 0; i < e->n; i++)
        if (ix_has_fn(X, EK(e)[i], id)) return 1;
    return 0;
}

/* the core's integrate (and through it this module once more): NULL unless complete */
E *ix_int(XP, E *g, E *x) {
    E *v[2] = {g, x};
    E *r = try_cmd(C_INTEGRATE, 2, v);
    if (!r || ix_has_fn(X, r, C_INTEGRATE) || !freeof(r, mk_sym(S_UNDEF))) return NULL;
    return r;
}

/* the symbols of e (no constants) other than x, appended to out (at most cap) */
void ix_syms(XP, E *e, E *x, E **out, uint32_t *n, uint32_t cap) {
    if (e->t == E_SYM) {
        if (is_const_expr(e) || expr_eq(e, x)) return;
        for (uint32_t i = 0; i < *n; i++)
            if (expr_eq(out[i], e)) return;
        if (*n < cap) out[(*n)++] = e;
        return;
    }
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN && e->t != E_LIST) return;
    stack_check();
    for (uint32_t i = 0; i < e->n; i++) ix_syms(X, EK(e)[i], x, out, n, cap);
}

/* F' = f at sample points: 1 yes, 0 no.  Symbols other than x get values in (0.2, 1.7) (the
 * same box for every point, three boxes), x five values in (0.15, 1.85).  Points where f or F'
 * cannot be evaluated are skipped; a disagreement where f is real rejects F, one where f is
 * complex is tolerated (F may sit on another branch there).  At least 4 points must agree. */
static int vcheck(XP, E *f, E *F, E *dF, E *x, int neg) {
    if (!dF) dF = diff(F, x);
    E *vs[17], *vals[17];
    uint32_t nv = 0, nf;
    ix_syms(X, f, x, vs, &nv, 16);
    nf = nv;
    ix_syms(X, F, x, vs, &nv, 16);
    if (nv != nf || nv >= 16) return 0; /* F has a symbol f has not (a substitution variable) */
    static const int16_t pv[3][16] = {
        {731, 1172, 433, 1530, 917, 288, 1364, 615, 1091, 502, 1249, 840, 377, 1458, 694, 1023},
        {1287, 541, 1093, 376, 1612, 829, 455, 1170, 667, 1391, 258, 984, 1530, 713, 1102, 449},
        {398, 1456, 862, 1145, 513, 1301, 977, 344, 1528, 790, 1061, 627, 1219, 905, 1377, 571}};
    static const int16_t xv[5] = {317, 1243, 689, 1681, 952};
    int good = 0;
    for (int b = 0; b < 3; b++) {
        for (uint32_t i = 0; i < nv; i++) vals[i] = mk_flt(pv[b][i] / 1000.0);
        vs[nv] = x;
        for (int j = 0; j < (b ? 2 : 5); j++) {
            poll_intr();
            vals[nv] = mk_flt((neg ? -1 : 1) * xv[(j + 2 * b) % 5] / 1000.0);
            double fr, fi, dr, di;
            char *pm = K->s_top; /* the substituted copies are released after each point */
            E *fv = subst_many(f, nv + 1, vs, vals);
            int okf = X->eval_complex(fv, &fr, &fi);
            K->s_top = pm;
            if (!okf) continue;
            E *dv = subst_many(dF, nv + 1, vs, vals);
            int okd = X->eval_complex(dv, &dr, &di);
            K->s_top = pm;
            if (!okd) continue;
            double sc = 1 + __builtin_fabs(fr) + __builtin_fabs(fi);
            if (!(sc < 1e12)) continue;
            double err = (__builtin_fabs(dr - fr) + __builtin_fabs(di - fi)) / sc;
#ifdef IX_DEBUG
            fprintf(stderr, "  pt f=%g%+gi F'=%g%+gi\n", fr, fi, dr, di);
#endif
            if (err < 1e-7) good++;
            else if (__builtin_fabs(fi) <= 1e-12 * sc) return 0;
        }
    }
    IXD(neg ? "verify(x<0) ok" : good >= 4 ? "verify ok" : "verify FAIL", F);
    return neg || good >= 4;
}

int ix_verify(XP, E *f, E *F, E *x) { return vcheck(X, f, F, NULL, x, 0); }

/* the same, with an error in the derivative meaning "no" */
int ix_verify_safe(XP, E *f, E *F, E *x) {
    E *v[2] = {F, x}, *dF = try_cmd(C_DIFF, 2, v);
    return dF && vcheck(X, f, F, dF, x, 0);
}

/* also no disagreement at negative x where f is real (substitutions valid for x > 0 only) */
int ix_verify_neg(XP, E *f, E *F, E *x) { return vcheck(X, f, F, NULL, x, 0) && vcheck(X, f, F, NULL, x, 1); }

/* ---- 1. exponentials of inverse hyperbolic and trigonometric functions ---- */

/* e = c * g with g = fn(u) (or i * atan(u)): c and u, else 0 */
static int arc_term(XP, E *e, int fn, E **c, E **u) {
    E **fs = e->t == E_MUL ? EK(e) : &e;
    uint32_t m = e->t == E_MUL ? e->n : 1;
    E *g = NULL, *cc = K->c1;
    for (uint32_t i = 0; i < m; i++) {
        if (IFN(fs[i], fn) && !g) g = fs[i];
        else cc = s_mul2(cc, fs[i]);
    }
    if (!g) return 0;
    *c = cc, *u = K0(g);
    return 1;
}

static E *exp_arc_rw(XP, E *e, E *x, int *ch) {
    if (freeof(e, x) || (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN)) return e;
    stack_check();
    E **k = salloc(sizeof(E *) * e->n);
    int c0 = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        k[i] = exp_arc_rw(X, EK(e)[i], x, ch);
        c0 |= k[i] != EK(e)[i];
    }
    if (c0) e = X->rebuild(e, k);
    /* e^w is a power of e or exp(w) */
    E *w = NULL;
    if (e->t == E_POW && ISYM(K0(e), S_E)) w = K1(e);
    else if (IFN(e, F_EXP)) w = K0(e);
    if (!w) return e;
    E *c, *u;
    if (w->t == E_ADD) return e; /* e^(a + c atanh u): the core splits e^a off first */
    if (arc_term(X, w, F_ATANH, &c, &u) && freeof(c, x)) {
        E *h = s_div(c, K->c2);
        *ch = 1;
        return s_mul2(s_pow(s_add2(K->c1, u), h), s_pow(s_sub(K->c1, u), s_neg(h)));
    }
    if (arc_term(X, w, F_ATAN, &c, &u) && freeof(c, x)) {
        E *ci = rat_normal(s_div(c, K->i_));
        if (!freeof(ci, K->i_)) return e;
        E *h = s_div(ci, K->c2), *iu = s_mul2(K->i_, u);
        *ch = 1;
        return s_mul2(s_pow(s_add2(K->c1, iu), h), s_pow(s_sub(K->c1, iu), s_neg(h)));
    }
    if (arc_term(X, w, F_ASINH, &c, &u) && freeof(c, x)) {
        *ch = 1;
        return s_pow(s_add2(u, s_sqrt(s_add2(K->c1, s_mul2(u, u)))), c);
    }
    if (arc_term(X, w, F_ACOSH, &c, &u) && freeof(c, x)) {
        *ch = 1;
        return s_pow(s_add2(u, s_mul2(s_sqrt(s_sub(u, K->c1)), s_sqrt(s_add2(u, K->c1)))), c);
    }
    return e;
}

static E *ix_exparc(XP, E *f, E *x) {
    int ch = 0;
    E *g = exp_arc_rw(X, f, x, &ch);
    if (!ch) return NULL;
    E *r = ix_int(X, g, x);
    if (!r) r = ix_radsub(X, g, x);
    return r;
}

/* ---- the slot ---- */

/* nodes of e counted as a tree, stopping at cap */
static long ix_size(XP, E *e, long cap) {
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN && e->t != E_LIST) return 1;
    long n = 1;
    for (uint32_t i = 0; i < e->n && n <= cap; i++) n += ix_size(X, EK(e)[i], cap - n);
    return n;
}

/* ix_ellip with its errors meaning "not found" (it runs before the substitutions) */
static E *ell_p(XP, void **a) { return ix_ellip(X, a[0], a[1]); }
static E *ell_safe(XP, E *f, E *x) {
    void *a[2] = {f, x};
    char *mk = K->s_top;
    E *r = X->protect(ell_p, a);
    if (!r) K->s_top = mk;
    return r;
}

static E *slot0(XP, E *f, E *x) {
    E *r;
    /* large integrands (often the rational functions the substitutions here produce) get only
     * the algebraic strategies: the others blow them up */
    int big = ix_size(X, f, 160) > 160;
    char *m = K->s_top;
    if (!big && (r = ix_exparc(X, f, x)) && ix_verify(X, f, r, x)) return r;
    K->s_top = m;
    if ((r = ix_radsub(X, f, x)) && ix_verify(X, f, r, x)) return r;
    K->s_top = m;
    if (!big && (r = ix_powsub(X, f, x)) && ix_verify_neg(X, f, r, x)) return r;
    K->s_top = m;
    if ((r = ix_ratbin(X, f, x)) && ix_verify(X, f, r, x)) return r;
    K->s_top = m;
    if (big) return NULL;
    if ((r = ell_safe(X, f, x))) return r; /* checked there, the locally constant factors as symbols */
    K->s_top = m;
    if ((r = ix_trigsub(X, f, x)) && ix_verify(X, f, r, x)) return r;
    K->s_top = m;
    if ((r = ix_invsub(X, f, x)) && ix_verify(X, f, r, x)) return r;
    K->s_top = m;
    if ((r = ix_special(X, f, x)) && ix_verify(X, f, r, x)) return r;
    K->s_top = m;
    if ((r = ix_algell(X, f, x))) return r; /* verified there */
    K->s_top = m;
    if ((r = ix_trigalg(X, f, x))) return r; /* verified there */
    K->s_top = m;
    if ((r = ix_trighyp(X, f, x))) return r; /* verified there */
    K->s_top = m;
    if ((r = ix_sfn(X, f, x))) return r; /* verified there */
    K->s_top = m;
    if ((r = ix_polylog(X, f, x))) return r; /* verified there */
    K->s_top = m;
    if ((r = ix_plog2(X, f, x))) return r; /* verified there */
    K->s_top = m;
    if ((r = ix_hyp(X, f, x))) return r; /* verified there */
    K->s_top = m;
    return NULL;
}

static E *slot(XP, uint32_t na, E **A) {
    E *f = A[0], *x = A[1];
    if (x->t != E_SYM || f->t == E_LIST || ix_has_fn(X, f, F_PIECEWISE)) return NULL;
    if (na == 3) return ix_tidy(X, f, A[2], x); /* the core's final antiderivative */
    return slot0(X, f, x);
}

int casx_spfn_register(XP); /* spfn.c */
int casx_tidy_register(XP); /* tidy.c */
int casx_fourier_register(XP); /* fourier.c */
int casx_fnx_register(XP);  /* fnx.c */
int casx_int_register(XP) {
    return X->reg_fn(CASX_FN_INTEGRATE, (void *)slot) | casx_spfn_register(X) | casx_fnx_register(X) |
           casx_tidy_register(X) | casx_fourier_register(X);
}
