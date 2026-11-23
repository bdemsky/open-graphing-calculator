/*
 * intrad.c -- integrands R(x, B1^q1, B2^q2, ...) whose radicands B are rational functions of x
 * built from at most two linear forms L1, L2 (B = c L1^e1 L2^e2, e integers, c free of x):
 * products of powers of linear binomials, sqrt(c - c/(a x)), (c - a^2 c x^2)^p, ...
 *
 * Every radical is rewritten as  B^q = K_B * L1^(q e1) * L2^(q e2)  with the locally constant
 * factor K_B = B^q / (L1^(q e1) L2^(q e2)) (its derivative vanishes wherever it is defined;
 * it is 1 when B is L1 itself, and keeps the result exact on every branch).  Then, with k the
 * common denominator of the exponents q e1:
 *   one form L = a x + b:   t = L^(1/k),  x = (t^k - b)/a,  dx = k t^(k-1)/a dt,
 *                           L^(q e) = t^(k q e);
 *   two forms, when every q (e1 + e2) = s is an integer (Chebyshev):
 *                           t = L1^(1/k) L2^(-1/k),  t^k = L1/L2,
 *                           x = (b2 t^k - b1)/(a1 - a2 t^k),  L2 = D/(a1 - a2 t^k),
 *                           dx = k D t^(k-1)/(a1 - a2 t^k)^2 dt,  D = a1 b2 - a2 b1,
 *                           L1^(q e1) L2^(q e2) = L2^s t^(k q e1)  (exact for principal roots).
 * The integrand becomes rational in t (the factors K_B stand in as constants _xk<i>); the
 * core integrates it (with intrat.c for binomial denominators), and t and K_B are put back.
 * Symbolic exponents are allowed when the two-form condition holds (k = 1).
 */
#include "intx.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

#define NA 12 /* radicals */

typedef struct {
    E *b, *q;     /* the radical b^q */
    long e[3];    /* exponents of the forms in b */
} atom;

typedef struct {
    atom a[NA];
    uint32_t n;
    E *L[3], *al[3], *be[3], *r[3]; /* forms L = al x + be, r = be/al */
    uint32_t nl;
    int bad;
} rctx;

/* a symbol _xt<i> that does not occur in f, from *i on (*i is advanced past it) */
E *ix_fresh(XP, E *f, int *i) {
    for (;; (*i)++) {
        char nm[6] = {'_', 'x', 't', (char)('0' + *i / 10), (char)('0' + *i % 10), 0};
        E *s = mk_sym_name(nm);
        if (freeof(f, s)) {
            (*i)++;
            return s;
        }
    }
}

int ix_ratx(XP, E *e, E *x) {
    if (freeof(e, x) || expr_eq(e, x)) return 1;
    stack_check();
    if (e->t == E_ADD || e->t == E_MUL) {
        for (uint32_t i = 0; i < e->n; i++)
            if (!ix_ratx(X, EK(e)[i], x)) return 0;
        return 1;
    }
    if (e->t == E_POW) return is_int(K1(e)) && ix_ratx(X, K0(e), x);
    return 0;
}

/* the form class of the linear polynomial f (index), adding it if new; -1 if a third */
static int form_of(XP, rctx *c, E *f, E *x) {
    E *al = poly_coeff(f, x, 1), *be = poly_coeff(f, x, 0);
    E *r = rat_normal(s_div(be, al));
    for (uint32_t i = 0; i < c->nl; i++)
        if (is_zero(rat_normal(s_sub(r, c->r[i])))) return (int)i;
    if (c->nl == 3) return -1;
    c->L[c->nl] = f, c->al[c->nl] = al, c->be[c->nl] = be, c->r[c->nl] = r;
    return (int)c->nl++;
}

static int lin_in(XP, E *f, E *x) {
    long d;
    return poly_degree_in(f, x, &d) && d == 1;
}

/* collect the radicals of e; 0 if e is not of the class */
static int collect(XP, rctx *c, E *e, E *x) {
    if (freeof(e, x) || expr_eq(e, x)) return 1;
    stack_check();
    if (e->t == E_ADD || e->t == E_MUL) {
        for (uint32_t i = 0; i < e->n; i++)
            if (!collect(X, c, EK(e)[i], x)) return 0;
        return 1;
    }
    if (e->t != E_POW || !freeof(K1(e), x)) return 0;
    if (is_int(K1(e))) return collect(X, c, K0(e), x);
    if (!ix_ratx(X, K0(e), x)) return 0;
    for (uint32_t i = 0; i < c->n; i++)
        if (expr_eq(c->a[i].b, K0(e)) && expr_eq(c->a[i].q, K1(e))) return 1;
    if (c->n == NA) return 0;
    atom *a = &c->a[c->n++];
    a->b = K0(e), a->q = K1(e), a->e[0] = a->e[1] = a->e[2] = 0;
    return 1;
}

/* exponents of the forms in the radicand b; 0 if b has another x-dependent factor */
static int split(XP, rctx *c, atom *a, E *x) {
    if (lin_in(X, a->b, x)) {
        int j = form_of(X, c, a->b, x);
        if (j < 0) return 0;
        a->e[j] = 1;
        return 1;
    }
    E *nd[2];
    X->numden(rat_normal(a->b), &nd[0], &nd[1]);
    for (int s = 0; s < 2; s++) {
        if (freeof(nd[s], x)) continue;
        E *cont, *L = X->factor_list_in(nd[s], x, &cont);
        if (!L || L->t != E_LIST) return 0;
        for (uint32_t i = 0; i < L->n; i++) {
            E *p = EK(L)[i], *f = EK(p)[0];
            long m;
            if (!num_is_small_int(EK(p)[1], &m)) return 0;
            if (freeof(f, x)) continue;
            if (!lin_in(X, f, x)) return 0;
            int j = form_of(X, c, f, x);
            if (j < 0) return 0;
            a->e[j] += s ? -m : m;
        }
    }
    return 1;
}

/* e with every radical a[i] replaced by the symbol ph[i] */
static E *replace(XP, rctx *c, E *e, E *x, E **ph) {
    if (freeof(e, x)) return e;
    if (e->t == E_POW)
        for (uint32_t i = 0; i < c->n; i++)
            if (expr_eq(K0(e), c->a[i].b) && expr_eq(K1(e), c->a[i].q)) return ph[i];
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW) return e;
    stack_check();
    E **k = salloc(sizeof(E *) * e->n);
    for (uint32_t i = 0; i < e->n; i++) k[i] = replace(X, c, EK(e)[i], x, ph);
    return X->rebuild(e, k);
}

static long lcm(long a, long b) {
    long x = a, y = b;
    while (y) {
        long t = x % y;
        x = y, y = t;
    }
    return a / x * b;
}

/* substitution state.  Up to three forms; the "radical" ones (a non-integer total exponent in
 * some product) are at most two, ri[0..nr), and give t; the others only have integer
 * exponents in every product and stay rational functions of x. */
typedef struct {
    rctx *c;
    E *t, *kk, *L2t;
    long k;
    int pass, sym, bad; /* pass 0: radical forms, 1: k and the two-form condition, 2: build */
    int rad[3], ri[2], nr;
} sctx;

static int is_form_pow(rctx *c, XP, E *e, int *j) {
    if (e->t != E_POW || is_int(K1(e))) return 0;
    for (uint32_t i = 0; i < c->nl; i++)
        if (expr_eq(K0(e), c->L[i])) {
            *j = (int)i;
            return 1;
        }
    return 0;
}

static void need_k(XP, sctx *S, E *r) {
    if (is_int(r)) return;
    long d;
    if (is_rat(r) && num_is_small_int(K1(r), &d) && d <= 12) {
        S->k = lcm(S->k, d);
        if (S->k > 12) S->bad = 1;
    } else if (is_rat(r))
        S->bad = 1;
    else
        S->sym = 1;
}

/* the radical part prod L_j^r_j of a product, in t (pass 2) */
static E *rad_t(XP, sctx *S, E **r) {
    rctx *c = S->c;
    if (S->pass == 0) {
        for (uint32_t j = 0; j < c->nl; j++)
            if (!is_int(rat_normal(r[j]))) S->rad[j] = 1;
        return K->c1;
    }
    E *out = K->c1;
    for (uint32_t j = 0; j < c->nl; j++)
        if (!S->rad[j]) {
            E *rj = rat_normal(r[j]);
            if (!is_int(rj)) {
                S->bad = 1;
                return K->c1;
            }
            if (S->pass == 2 && !is_zero(rj)) out = s_mul2(out, s_pow(c->L[j], rj));
        }
    if (!S->nr) return out;
    E *r1 = r[S->ri[0]], *r2 = S->nr == 2 ? r[S->ri[1]] : K->c0;
    if (S->nr == 1) {
        if (S->pass == 1) {
            need_k(X, S, r1);
            return K->c1;
        }
        return s_mul2(out, s_pow(S->t, s_mul2(S->kk, r1)));
    }
    E *sm = rat_normal(s_add2(r1, r2));
    if (!is_int(sm)) {
        S->bad = 1;
        return K->c1;
    }
    if (S->pass == 1) {
        need_k(X, S, r1);
        return K->c1;
    }
    return s_mul(3, (E *[]){out, s_pow(S->L2t, sm), s_pow(S->t, s_mul2(S->kk, r1))});
}

static E *tsub(XP, sctx *S, E *e, E *x) {
    if (S->bad || freeof(e, x)) return e;
    stack_check();
    int j;
    E *r[3] = {K->c0, K->c0, K->c0};
    if (is_form_pow(S->c, X, e, &j)) {
        r[j] = K1(e);
        return rad_t(X, S, r);
    }
    if (e->t == E_MUL) {
        int any = 0;
        E **k = salloc(sizeof(E *) * (e->n + 1));
        uint32_t n = 0;
        for (uint32_t i = 0; i < e->n; i++) {
            if (is_form_pow(S->c, X, EK(e)[i], &j)) {
                r[j] = s_add2(r[j], K1(EK(e)[i]));
                any = 1;
            } else
                k[n++] = tsub(X, S, EK(e)[i], x);
        }
        if (any) k[n++] = rad_t(X, S, r);
        return s_mul(n, k);
    }
    if (e->t != E_ADD && e->t != E_POW) return e;
    E **k = salloc(sizeof(E *) * e->n);
    for (uint32_t i = 0; i < e->n; i++) k[i] = tsub(X, S, EK(e)[i], x);
    return X->rebuild(e, k);
}

/* passes 0 and 1 over g0: the radical forms, k; 0 if g0 is not of the class */
static int analyse(XP, sctx *S, E *g0, E *x) {
    rctx *c = S->c;
    S->bad = 0, S->k = 1, S->sym = 0, S->nr = 0;
    for (int j = 0; j < 3; j++) S->rad[j] = 0;
    S->pass = 0;
    tsub(X, S, g0, x);
    if (S->bad) return 0;
    for (uint32_t j = 0; j < c->nl; j++)
        if (S->rad[j]) {
            if (S->nr == 2) return 0;
            S->ri[S->nr++] = (int)j;
        }
    if (!S->nr) return 1; /* every exponent an integer after all: rational (K_B apart) */
    S->pass = 1;
    tsub(X, S, g0, x);
    return !S->bad && !(S->sym && S->k > 1);
}

E *ix_radsub(XP, E *f, E *x) {
    rctx *c = salloc(sizeof(rctx));
    c->n = c->nl = 0;
    if (!collect(X, c, f, x) || !c->n) return NULL;
    /* prefer the radicands that are linear themselves as the representatives */
    for (uint32_t i = 0; i < c->n; i++)
        if (lin_in(X, c->a[i].b, x) && form_of(X, c, c->a[i].b, x) < 0) return NULL;
    for (uint32_t i = 0; i < c->n; i++)
        if (!split(X, c, &c->a[i], x)) return NULL;
    if (!c->nl) return NULL;
    /* every radical as K_B L1^(q e1) L2^(q e2) L3^(q e3) */
    int fi = 0;
    E **kv = salloc(sizeof(E *) * c->n * 3), **ks = kv + c->n, **rv = kv + 2 * c->n;
    for (uint32_t i = 0; i < c->n; i++) {
        atom *a = &c->a[i];
        E *Lq = K->c1;
        for (uint32_t j = 0; j < c->nl; j++)
            if (a->e[j]) Lq = s_mul2(Lq, s_pow(c->L[j], s_mul2(a->q, mk_int(a->e[j]))));
        E *kb = s_div(s_pow(a->b, a->q), Lq);
        kv[i] = kb;
        ks[i] = expr_eq(kb, K->c1) ? NULL : ix_fresh(X, f, &fi);
        rv[i] = ks[i] ? s_mul2(ks[i], Lq) : Lq;
    }
    E **ph = salloc(sizeof(E *) * c->n);
    for (uint32_t i = 0; i < c->n; i++) ph[i] = ix_fresh(X, f, &fi);
    E *g0 = subst_many(replace(X, c, f, x, ph), c->n, ph, rv);
    sctx S;
    S.c = c, S.t = S.kk = S.L2t = NULL;
    if (!analyse(X, &S, g0, x)) {
        g0 = expand(g0);
        if (!analyse(X, &S, g0, x)) return NULL;
    }
    if (!S.nr) { /* rational once the radicals are split into the forms */
        S.pass = 2;
        E *g = tsub(X, &S, g0, x), *G;
        if (S.bad || !(G = ix_int(X, rat_normal(g), x))) return NULL;
        for (uint32_t i = 0; i < c->n; i++)
            if (ks[i]) G = subst(G, ks[i], kv[i]);
        return G;
    }
    long k = S.k;
    int i0 = S.ri[0], i1 = S.nr == 2 ? S.ri[1] : -1;
    E *t = ix_fresh(X, f, &fi), *tk = s_powi(t, k), *kk = mk_int(k);
    E *xt, *dxt, *L2t = NULL;
    if (i1 < 0) {
        xt = s_div(s_sub(tk, c->be[i0]), c->al[i0]);
        dxt = s_div(s_mul2(kk, s_powi(t, k - 1)), c->al[i0]);
    } else {
        E *D = rat_normal(s_sub(s_mul2(c->al[i0], c->be[i1]), s_mul2(c->al[i1], c->be[i0])));
        if (is_zero(D)) return NULL;
        E *den = s_sub(c->al[i0], s_mul2(c->al[i1], tk));
        xt = s_div(s_sub(s_mul2(c->be[i1], tk), c->be[i0]), den);
        L2t = s_div(D, den);
        dxt = s_div(s_mul(3, (E *[]){kk, D, s_powi(t, k - 1)}), s_powi(den, 2));
    }
    S.pass = 2, S.t = t, S.kk = kk, S.L2t = L2t;
    E *g = tsub(X, &S, g0, x);
    if (S.bad) return NULL;
    g = s_mul2(subst(g, x, xt), dxt);
    if (S.sym && g->t == E_MUL) { /* t^(symbolic) factors stay out of rat_normal */
        E **k1 = salloc(sizeof(E *) * g->n), **k2 = salloc(sizeof(E *) * g->n);
        uint32_t n1 = 0, n2 = 0;
        for (uint32_t i = 0; i < g->n; i++) {
            E *q = EK(g)[i];
            if (q->t == E_POW && expr_eq(K0(q), t) && !is_num(K1(q))) k1[n1++] = q;
            else k2[n2++] = q;
        }
        k1[n1++] = rat_normal(s_mul(n2, k2));
        g = s_mul(n1, k1);
    } else
        g = rat_normal(g);
    IXD("radsub g", g);
    if (!freeof(g, x)) return NULL;
    {
        E *gn, *gd;
        long dg;
        X->numden(g, &gn, &gd);
        /* numeric denominators of high degree (cyclotomic splits) take too long */
        if (poly_degree_in(gd, t, &dg) && dg > 12) {
            if (dg > 40) return NULL;
            int nz = 0;
            for (long j = 0; j <= dg; j++) nz += !is_zero(poly_coeff(gd, t, j));
            E *sy[2];
            uint32_t ns = 0;
            ix_syms(X, gd, t, sy, &ns, 1);
            if (nz <= 3 && !ns) return NULL; /* t^18 - 1: slow cyclotomic split */
        }
    }
    E *G = ix_int(X, g, t);
    IXD("radsub G", G);
    if (!G) return NULL;
    /* back: t, then the constants K_B */
    E *T = i1 < 0 ? s_pow(c->L[i0], s_inv(kk))
                  : s_mul2(s_pow(c->L[i0], s_inv(kk)), s_pow(c->L[i1], s_neg(s_inv(kk))));
    G = subst(G, t, T);
    for (uint32_t i = 0; i < c->n; i++)
        if (ks[i]) G = subst(G, ks[i], kv[i]);
    return G;
}

/* ---- u = x^n: x^m R(x^n, radicals of rational functions of x^n) (Chebyshev's binomials
 * x^m (a + b x^n)^p with (m+1)/n or (m+1)/n + p an integer, a + b/x^3, ...) ----
 * n = the gcd of the exponents of x in the radicands; x = u^(1/n), dx = u^(1/n - 1)/n du,
 * and the integral in u goes to ix_radsub (valid for x > 0: the result is also checked at
 * negative x by the caller) */
static long gcdl(long a, long b) {
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b) {
        long t = a % b;
        a = b, b = t;
    }
    return a;
}

static int rad_degs(XP, E *e, E *x, long *g) {
    if (freeof(e, x)) return 1;
    stack_check();
    if (e->t == E_POW && !is_int(K1(e)) && freeof(K1(e), x)) {
        E *nd[2];
        X->numden(rat_normal(K0(e)), &nd[0], &nd[1]);
        for (int s = 0; s < 2; s++) {
            long d;
            if (!poly_degree_in(nd[s], x, &d) || d > 64) return 0;
            for (long j = 1; j <= d; j++)
                if (!is_zero(poly_coeff(nd[s], x, j))) *g = gcdl(*g, j);
        }
        return 1;
    }
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW) return 1;
    for (uint32_t i = 0; i < e->n; i++)
        if (!rad_degs(X, EK(e)[i], x, g)) return 0;
    return 1;
}

/* u^r -> x^(n r), u -> x^n (not (x^n)^r: that is |x|^(n r) for x < 0) */
static E *upow_back(XP, E *e, E *u, E *x, E *n) {
    if (freeof(e, u)) return e;
    if (expr_eq(e, u)) return s_pow(x, n);
    stack_check();
    if (e->t == E_POW && expr_eq(K0(e), u)) return s_pow(x, rat_normal(s_mul2(n, K1(e))));
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN) return subst(e, u, s_pow(x, n));
    E **k = salloc(sizeof(E *) * e->n);
    for (uint32_t i = 0; i < e->n; i++) k[i] = upow_back(X, EK(e)[i], u, x, n);
    return X->rebuild(e, k);
}

/* forward: x^e -> u^(e/n), x -> u^(1/n), exponents of u normalised */
static E *upow_fwd(XP, E *e, E *u, E *x, E *n) {
    if (freeof(e, x)) return e;
    if (expr_eq(e, x)) return s_pow(u, s_inv(n));
    stack_check();
    if (e->t == E_POW && expr_eq(K0(e), x)) return s_pow(u, rat_normal(s_div(K1(e), n)));
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN) return e;
    E **k = salloc(sizeof(E *) * e->n);
    for (uint32_t i = 0; i < e->n; i++) k[i] = upow_fwd(X, EK(e)[i], u, x, n);
    E *r = X->rebuild(e, k);
    if (r->t == E_POW && expr_eq(K0(r), u)) r = s_pow(u, rat_normal(K1(r)));
    if (r->t == E_MUL) { /* u^a u^b with symbolic a, b: one power */
        E *ex = K->c0, **o = salloc(sizeof(E *) * (r->n + 1));
        uint32_t no = 0, nu = 0;
        for (uint32_t i = 0; i < r->n; i++) {
            E *q = EK(r)[i];
            if (expr_eq(q, u)) ex = s_add2(ex, K->c1), nu++;
            else if (q->t == E_POW && expr_eq(K0(q), u)) ex = s_add2(ex, K1(q)), nu++;
            else o[no++] = q;
        }
        if (nu > 1) {
            o[no++] = s_pow(u, rat_normal(ex));
            r = s_mul(no, o);
        }
    }
    return r;
}

/* the symbolic exponent n of x in the radicands (x^n, all the same), or NULL */
static void sym_deg(XP, E *e, E *x, E **n, int *bad, int inrad) {
    if (*bad || freeof(e, x)) return;
    stack_check();
    if (e->t == E_POW && expr_eq(K0(e), x)) {
        if (!inrad || is_num(K1(e))) return;
        if (*n && !expr_eq(*n, K1(e))) *bad = 1;
        *n = K1(e);
        return;
    }
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW) return;
    int rad = inrad || (e->t == E_POW && !is_int(K1(e)));
    for (uint32_t i = 0; i < e->n; i++) sym_deg(X, EK(e)[i], x, n, bad, rad && !(e->t == E_POW && i == 1));
}

E *ix_powsub(XP, E *f, E *x) {
    long nn = 0;
    E *n;
    if (rad_degs(X, f, x, &nn)) {
        if (nn < 2 || nn > 24) return NULL;
        n = mk_int(nn);
    } else { /* x^n with a symbolic n */
        int bad = 0;
        n = NULL;
        sym_deg(X, f, x, &n, &bad, 0);
        if (bad || !n) return NULL;
    }
    int fi = 0;
    E *u = ix_fresh(X, f, &fi), *in = s_inv(n);
    E *g = s_mul2(upow_fwd(X, f, u, x, n), s_div(s_pow(u, rat_normal(s_sub(in, K->c1))), n));
    g = upow_fwd(X, g, u, u, K->c1); /* merge the powers of u */
    if (!freeof(g, x)) return NULL;
    E *G = ix_radsub(X, g, u);
    if (!G) return NULL;
    return upow_back(X, G, u, x, n);
}
