/*
 * assume.c -- relational assumptions beyond the sign flags of the symbol
 * records (A_POS, A_NEG, A_NONNEG, A_INT): assume(-3*pi < x and x < -5*pi/2),
 * assume(x > y), assume(y >= x - 1), assume(abs(x) < 1).
 *
 * Facts.  Every assumption f > 0 or f >= 0 (f = lhs - rhs, or rhs - lhs for
 * < and <=) is kept in the hidden variable _facts as a list of [f, strict]
 * pairs, so it lives in the persistent heap, survives the symbol GC and is
 * saved with the session like any variable.  A fact linear in one symbol
 * with a numeric bound also sets that symbol's sign flag when the bound
 * decides it (x > 1 gives A_POS).
 *
 * Uses (sign_of in simp.c falls back to facts_sign):
 *  - intervals: a symbol's numeric bounds come from its one-symbol linear
 *    facts; the sign of a linear expression or of sin / cos / tan of a
 *    linear argument in that symbol is decided on the interval when no zero
 *    lies inside it (|cos x| = -cos x for -3 pi < x < -5 pi/2);
 *  - combinations: e > 0 when e - f_i or e - f_i - f_j is 0 with a strict
 *    fact among them, or of known positive sign (x > 1, y >= x - 1 give
 *    y > 0); a^n - b^n has the sign of a - b for n > 0, b > 0;
 *  - top-level relations (assume_decide, eval.c): a relation that is the
 *    whole input is decided by these signs, and a polynomial in one variable
 *    that has no real root (Sturm sequence) by its leading coefficient
 *    (x^4 - x + 1 > 0 is true for real x);
 *  - interval_of: bounds of a linear expression in one symbol, for
 *    atan(tan(u)) = u on (-pi/2, pi/2) (simp.c).
 */
#include "cas_int.h"
#include "upoly.h"
#include <math.h>
#include <string.h>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

#define FACTS_NAME "_facts"
#define FACTS_MAX 16

static int facts_id(void) { return sym_intern(FACTS_NAME, sizeof FACTS_NAME - 1); }

static E *facts_list(void) {
    int id = sym_find(FACTS_NAME, sizeof FACTS_NAME - 1); /* lookup only: sign_of runs this often */
    if (id < 0) return NULL;
    E *v = K->syms[id].val;
    return v && v->t == E_LIST ? v : NULL;
}

/* any assumption stored (assume): gates work that only pays off with one */
int facts_any(void) {
    E *f = facts_list();
    return f && f->n;
}

static void facts_set(uint32_t n, E **v) {
    symrec *r = &K->syms[facts_id()];
    E *old = r->val;
    r->val = n ? hstore(s_list(n, v)) : NULL;
    if (old) hfree_expr(old);
}

/* one symbol (not a constant) in e: its node, else NULL */
static E *single_sym(E *e) {
    E *sy[3];
    uint32_t n = collect_syms(e, sy, 3), k = 0;
    E *s = NULL;
    for (uint32_t i = 0; i < n; i++)
        if (sy[i]->s >= S_X) s = sy[i], k++;
    return k == 1 ? s : NULL;
}

/* e = c x + d with numeric c != 0, d: 1 */
static int lin1(E *e, E *x, double *c, double *d) {
    E *dc = diff(e, x);
    if (!is_const_expr(dc) || !to_double(dc, c) || *c == 0) return 0;
    E *rest = expand(s_sub(e, s_mul2(dc, x)));
    return freeof(rest, x) && is_const_expr(rest) && to_double(rest, d);
}

/* numeric bounds of symbol x from its facts: lo < x (or <=) < hi */
static void sym_bounds(E *x, double *lo, int *lo_st, double *hi, int *hi_st) {
    *lo = -INFINITY, *hi = INFINITY, *lo_st = *hi_st = 1;
    uint8_t a = K->syms[x->s].assume;
    if (a & A_POS) *lo = 0, *lo_st = 1;
    else if (a & A_NONNEG) *lo = 0, *lo_st = 0;
    if (a & A_NEG) *hi = 0, *hi_st = 1;
    E *F = facts_list();
    for (uint32_t i = 0; F && i < F->n; i++) {
        E *f = K0(EK(F)[i]);
        int st = !is_zero(K1(EK(F)[i]));
        double c, d;
        if (freeof(f, x) || single_sym(f) != x || !lin1(f, x, &c, &d)) continue;
        double b = -d / c; /* c x + d > 0: x > b (c > 0) or x < b (c < 0) */
        if (c > 0 && (b > *lo || (b == *lo && st))) *lo = b, *lo_st = st;
        if (c < 0 && (b < *hi || (b == *hi && st))) *hi = b, *hi_st = st;
    }
}

int interval_of(E *u, double *lo, double *hi, int *lo_st, int *hi_st) {
    E *x = single_sym(u);
    double c, d, l, h;
    int ls, hs;
    if (!x || !lin1(u, x, &c, &d)) return 0;
    sym_bounds(x, &l, &ls, &h, &hs);
    if (!isfinite(l) || !isfinite(h)) return 0;
    *lo = c > 0 ? c * l + d : c * h + d;
    *hi = c > 0 ? c * h + d : c * l + d;
    *lo_st = c > 0 ? ls : hs;
    *hi_st = c > 0 ? hs : ls;
    return 1;
}

/* sign of e on the interval of its single symbol (linear or circular) */
static int interval_sign(E *e) {
    E *x = single_sym(e);
    if (!x) return 0;
    double lo, hi, c, d;
    int ls, hs;
    sym_bounds(x, &lo, &ls, &hi, &hs);
    if (lin1(e, x, &c, &d)) { /* monotone: the values at both ends */
        double a = c * (c > 0 ? lo : hi) + d, b = c * (c > 0 ? hi : lo) + d;
        int as = c > 0 ? ls : hs, bs = c > 0 ? hs : ls;
        if (a > 0 || (a == 0 && as)) return 1;
        if (b < 0 || (b == 0 && bs)) return -1;
        return 0;
    }
    if (!isfinite(lo) || !isfinite(hi) || e->t != E_FN || e->n != 1 || (e->s != F_SIN && e->s != F_COS && e->s != F_TAN))
        return 0;
    if (!lin1(K0(e), x, &c, &d)) return 0;
    /* argument range [p, q]; zeros (and tan's poles) at k pi/2 for tan, k pi (sin), pi/2 + k pi (cos) */
    double p = c > 0 ? c * lo + d : c * hi + d, q = c > 0 ? c * hi + d : c * lo + d;
    double step = e->s == F_TAN ? M_PI / 2 : M_PI, off = e->s == F_COS ? M_PI / 2 : 0;
    int ps = c > 0 ? ls : hs, qs = c > 0 ? hs : ls; /* the ends excluded? */
    double tp = 1e-12 * (1 + fabs(p)), tq = 1e-12 * (1 + fabs(q));
    for (double k = ceil((p - tp - off) / step);; k++) {
        double z = off + k * step;
        if (fabs(z - p) <= tp) { /* a zero at the lower end: fine if the end is excluded */
            if (!ps) return 0;
            continue;
        }
        if (z < q - tq) return 0;                /* a zero inside */
        if (fabs(z - q) <= tq && !qs) return 0;  /* at the upper end, included */
        break;
    }
    double m = (p + q) / 2, v = e->s == F_SIN ? sin(m) : e->s == F_COS ? cos(m) : tan(m);
    return v > 0 ? 1 : v < 0 ? -1 : 0;
}

static int g_in_facts;

/* e > 0 from facts: e - f_i (- f_j) zero with a strict fact, or positive */
static int from_facts(E *e, E *F) {
    uint32_t m = F->n;
    for (uint32_t i = 0; i < m; i++) {
        E *fi = K0(EK(F)[i]);
        int si = !is_zero(K1(EK(F)[i]));
        for (uint32_t j = i; j <= m; j++) { /* j == m: f_i alone; j == i: 2 f_i is not tried */
            if (j == i) continue;
            E *fj = j < m ? K0(EK(F)[j]) : K->c0;
            int sj = j < m && !is_zero(K1(EK(F)[j]));
            E *r = expand(s_sub(e, s_add2(fi, fj)));
            if ((is_zero(r) && (si || sj)) || sign_of(r) > 0) return 1;
        }
    }
    return 0;
}

int facts_sign(E *e) {
    if (g_in_facts || !facts_list() || is_const_expr(e)) return 0;
    int s = interval_sign(e);
    if (s) return s;
    E *F = facts_list();
    if (!F) return 0;
    int r = 0;
    g_in_facts = 1;
    tryf tf;
    try_enter(&tf);
    if (!setjmp(tf.jb)) {
        /* a^n - b^n, n > 0, b > 0: the sign of a - b */
        if (e->t == E_ADD && e->n == 2) {
            E *p = EK(e)[0], *q = EK(e)[1], *nq;
            for (int t = 0; t < 2 && !r; t++, nq = p, p = q, q = nq) {
                E *mq = s_neg(q);
                if (p->t == E_POW && mq->t == E_POW && expr_eq(K1(p), K1(mq)) && sign_of(K1(p)) > 0 &&
                    sign_of(K0(mq)) > 0) {
                    g_in_facts = 0;
                    r = sign_of(s_sub(K0(p), K0(mq)));
                    g_in_facts = 1;
                }
            }
        }
        if (!r && from_facts(e, F)) r = 1;
        if (!r && from_facts(s_neg(e), F)) r = -1;
        try_leave(&tf);
    } else {
        try_leave(&tf);
        g_in_facts = 0;
        rethrow();
    }
    g_in_facts = 0;
    return r;
}

/* ------------------------------------------------------------- assume --- */
/* set the sign flag of x when a fact c x + d > 0 decides it */
static void flag_from(E *f, int strict) {
    E *x = single_sym(f);
    double c, d;
    if (!x || !lin1(f, x, &c, &d)) return;
    double b = -d / c;
    uint8_t *a = &K->syms[x->s].assume;
    if (c > 0 && (b > 0 || (b == 0 && strict))) *a |= A_POS | A_NONNEG;
    else if (c > 0 && b == 0) *a |= A_NONNEG;
    else if (c < 0 && (b < 0 || (b == 0 && strict))) *a = (uint8_t)((*a & ~(A_POS | A_NONNEG)) | A_NEG);
}

void facts_add(E *f, int strict) {
    E *F = facts_list();
    uint32_t n = F ? F->n : 0;
    if (n >= FACTS_MAX) cas_throw(CAS_ERR_DOMAIN, "assume: too many assumptions (unassume some)");
    E **v = salloc(sizeof(E *) * (n + 1));
    for (uint32_t i = 0; i < n; i++) v[i] = EK(F)[i];
    E *p[2] = {f, strict ? K->c1 : K->c0};
    v[n] = s_list(2, p);
    facts_set(n + 1, v);
    flag_from(f, strict);
}

/* assume(c) for a relation (or a conjunction) that is not a plain sign of a
 * symbol: 1 if taken */
int assume_rel(E *c) {
    if (ISYM(c, S_TRUE)) return 1;
    if (IFN(c, F_AND) && c->n == 2) return assume_rel(K0(c)) && assume_rel(K1(c));
    if (c->t != E_FN || c->n != 2 || (c->s != F_GT && c->s != F_GE && c->s != F_LT && c->s != F_LE)) return 0;
    E *l = K0(c), *r = K1(c);
    int strict = c->s == F_GT || c->s == F_LT, less = c->s == F_LT || c->s == F_LE;
    if (less && IFN(l, F_ABS) && l->n == 1) { /* |u| < r: -r < u < r */
        facts_add(s_add2(r, K0(l)), strict);
        facts_add(s_sub(r, K0(l)), strict);
        return 1;
    }
    E *f = expand(less ? s_sub(r, l) : s_sub(l, r));
    if (freeof_sym(f, S_I) && !is_const_expr(f)) {
        facts_add(f, strict);
        return 1;
    }
    return 0;
}

/* unassume(x) / purge(x): the facts that mention x go */
void facts_forget(int id) {
    E *F = facts_list();
    if (!F) return;
    E **v = salloc(sizeof(E *) * (F->n + 1));
    uint32_t n = 0;
    for (uint32_t i = 0; i < F->n; i++)
        if (freeof_sym(K0(EK(F)[i]), id)) v[n++] = EK(F)[i];
    if (n != F->n) facts_set(n, v);
}

/* about(x): the facts that mention x, as relations */
E *facts_about(int id) {
    E *F = facts_list();
    if (!F) return NULL;
    E **v = salloc(sizeof(E *) * (F->n + 1));
    uint32_t n = 0;
    for (uint32_t i = 0; i < F->n; i++)
        if (!freeof_sym(K0(EK(F)[i]), id))
            v[n++] = s_fn2(is_zero(K1(EK(F)[i])) ? F_GE : F_GT, K0(EK(F)[i]), K->c0);
    return n ? s_list(n, v) : NULL;
}

/* ------------------------------------------------- top-level decisions --- */
/* number of distinct real roots of the squarefree polynomial p (Sturm) */
static int sturm_count(UP p) {
    UP s[64];
    int n = 0;
    s[n++] = p;
    s[n++] = up_deriv(p);
    while (n < 64 && s[n - 1].d > 0) {
        UP r;
        up_divrem(s[n - 2], s[n - 1], NULL, &r);
        if (r.d < 0) break;
        s[n++] = up_scale(r, K->cm1);
    }
    int vm = 0, vp = 0, lm = 0, lp = 0;
    for (int i = 0; i < n; i++) {
        int sg = sign_of(s[i].c[s[i].d]);
        if (!sg) return -1;
        int sm = (s[i].d & 1) ? -sg : sg;
        if (lp && sg != lp) vp++;
        if (lm && sm != lm) vm++;
        lp = sg, lm = sm;
    }
    return vm - vp;
}

/* sign of a polynomial in one variable with rational coefficients that has
 * no real root (0 if it has one or is not of that kind) */
static int poly_sign(E *d) {
    E *x = single_sym(d);
    UP p;
    if (!x || !up_from(expand(d), x, &p) || p.d < 2 || p.d > 40) return 0;
    for (long i = 0; i <= p.d; i++)
        if (p.c[i]->t != E_INT && p.c[i]->t != E_RAT) return 0;
    UP g = up_gcd(p, up_deriv(p)), q = p;
    if (g.d > 0) up_divrem(p, g, &q, NULL);
    if (sturm_count(q) != 0) return 0;
    return sign_of(p.c[p.d]);
}

/* the input's relation (or and / or / not of relations) decided, or e */
E *assume_decide(E *e) {
    stack_check();
    if (e->t != E_FN) return e;
    if ((e->s == F_AND || e->s == F_OR) && e->n == 2) {
        E *a = assume_decide(K0(e)), *b = assume_decide(K1(e));
        int ta = ISYM(a, S_TRUE) ? 1 : ISYM(a, S_FALSE) ? 0 : -1, tb = ISYM(b, S_TRUE) ? 1 : ISYM(b, S_FALSE) ? 0 : -1;
        if (e->s == F_AND && (ta == 0 || tb == 0)) return mk_sym(S_FALSE);
        if (e->s == F_AND && ta == 1 && tb == 1) return mk_sym(S_TRUE);
        if (e->s == F_OR && (ta == 1 || tb == 1)) return mk_sym(S_TRUE);
        if (e->s == F_OR && ta == 0 && tb == 0) return mk_sym(S_FALSE);
        return e;
    }
    if (e->n != 2 || (e->s != F_GT && e->s != F_GE && e->s != F_LT && e->s != F_LE)) return e;
    E *d = expand(e->s == F_GT || e->s == F_GE ? s_sub(K0(e), K1(e)) : s_sub(K1(e), K0(e)));
    if (freeof_sym(d, S_I) == 0) return e;
    int s = sign_of(d);
    if (!s) s = sign_of(factor_expr(d));
    if (!s && d->t == E_ADD && EK(d)[0]->t == E_MUL) /* a factor of known sign common to the terms */
        for (uint32_t i = 0; i < EK(d)[0]->n && !s; i++) {
            E *t = EK(EK(d)[0])[i];
            int st = is_const_expr(t) ? 0 : sign_of(t);
            if (st) s = st * sign_of(expand(s_div(d, t)));
        }
    if (!s) s = poly_sign(d);
    return s > 0 ? mk_sym(S_TRUE) : s < 0 ? mk_sym(S_FALSE) : e;
}
