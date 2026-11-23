/*
 * solve.c -- equation solving.
 *
 * Single equation f(x) = 0: the numerator of the rational normal form is
 * factored over Q; each factor is solved exactly when possible (degree 1-2,
 * cubics by Cardano / trigonometric form, binomials x^n = c, polynomials in
 * x^k), otherwise numerically (Aberth iteration).  Non-polynomial equations
 * are reduced by isolating a single kernel (exp, ln, trig, radicals, ...) and
 * applying inverse functions, by the substitution T = e^(g x) for sums of
 * exponentials, or the half-angle substitution for trig; as a last resort
 * real roots are located numerically on a grid and refined.
 * Candidate solutions are checked against the original equation.
 * Systems: linear systems exactly (Gauss-Jordan), polynomial systems by
 * successive elimination (solve for a linear variable or use resultants).
 *
 * Entry points and callers
 *   solve_expr(eqs, vars, cplx)  commands solve / csolve (cmds.c), and
 *                                internally integ.c (partial fractions,
 *                                poles of definite integrals), ode.c
 *                                (solving for y / constants), cmds.c
 *                                (eigenvalues from the characteristic poly).
 *   solve_ineq(rel, x)           inequalities, via solve_expr.
 *   fsolve_expr(eq, x, guess)    fsolve / nsolve: numeric (Newton / scan).
 *   roots_expr(p, x)             roots: [[root, multiplicity], ...] over C.
 *
 * Data flow for one equation (solve1 -> solve_factor -> ...):
 *   f -> numerator of the normal form -> factor_list_in over Q in x ->
 *   each factor: polynomial (solve_poly) | radicals of x only (x = T^m) |
 *   one kernel (substitute T, solve, invert) | a radical appearing linearly
 *   (isolate, raise to a power) | sums of exponentials (T = e^(rate x)) |
 *   trig (Weierstrass t = tan(u/2)) | numeric sign-change scan.
 *   The candidates of every level are verified numerically by solve1, which
 *   removes extraneous roots introduced by squaring / raising to powers and
 *   points where the denominator vanishes.
 *
 * Conventions
 *   - Solutions are collected in an slist (growable array in scratch,
 *     duplicates removed with expr_eq).  The pseudo-solution `x` itself
 *     means "every x is a solution" (identity 0 = 0).
 *   - g_cplx (file static) selects real mode (0: complex candidates are
 *     dropped, only real n-th roots, principal branches) or complex mode
 *     (1: csolve / roots).  Entry points save, set and restore it on normal
 *     return.
 *   - Auxiliary unknowns are symbols named _m0/_m1, _k0/_k1, _r0/_r1,
 *     _e0/_e1, _w0/_w1; the digit alternates with the recursion depth so a
 *     nested substitution never reuses its parent's symbol.
 *   - Recursion is bounded by explicit depth limits (solve1: 10, invert: 8,
 *     radical isolation: 6, trig: 4, systems: 8) and stack_check().
 *   - Everything is allocated in scratch; an empty list means "no
 *     solution exists" only if no strategy gave up (g_incomplete), else
 *     solve_expr throws "cannot solve"; hard errors use cas_throw.
 *
 * Literature: Cardano's formula and the trigonometric (Viete) form for
 * cubics (e.g. Cohen, "Computer Algebra and Symbolic Computation" vol. 2;
 * Abramowitz & Stegun 3.8.2); Aberth iteration for numeric polynomial roots
 * lives in upoly.c (proot_expr); resultants (Geddes/Czapor/Labahn ch. 9) and
 * Groebner bases (gbasis.c) for polynomial systems; the tangent half-angle
 * (Weierstrass) substitution for rational trig equations.
 */
#include "cas_int.h"
#include "upoly.h"
#include <math.h>
#include <string.h>

/* helpers from trig.c / upoly.c */
E *trig_expand(E *e);
E *trig_sincos(E *e);
int has_trig(E *e);
E *proot_expr(E *p, E *x);

/* growable list of candidate solutions (scratch memory) */
typedef struct {
    E **v;
    uint32_t n, cap;
} slist;

/* Append s to l unless an expr_eq-equal entry is already present (O(n)).
 * Growth doubles the array; the old block is abandoned in scratch. */
static void sl_add(slist *l, E *s) {
    for (uint32_t i = 0; i < l->n; i++)
        if (expr_eq(l->v[i], s)) return;
    if (l->n == l->cap) {
        uint32_t nc = l->cap ? l->cap * 2 : 8;
        E **nv = salloc(sizeof(E *) * nc);
        if (l->n) memcpy(nv, l->v, sizeof(E *) * l->n);
        l->v = nv;
        l->cap = nc;
    }
    l->v[l->n++] = s;
}

static int g_cplx; /* 1 = complex solutions wanted (csolve, roots) */
/* 1 once a strategy gave up or only scanned numerically: an empty result
 * then means "cannot solve", not "no solution" (solve_expr) */
static int g_incomplete;
void solve_reset_statics(void); /* cas.c eval_impl: state an error may have left set */
void solve_reset_statics(void) { g_cplx = 0; g_incomplete = 0; }
static void solve1(E *f, E *x, slist *out, int depth);

/* 1 unless e is a constant whose numeric value has a non-negligible
 * imaginary part (relative 1e-10).  Symbolic or non-evaluable values are
 * assumed real. */
static int is_real_val(E *e) {
    if (!is_const_expr(e)) return 1; /* symbolic: assume real */
    double re, im;
    if (!eval_complex(e, &re, &im)) return 1;
    return fabs(im) <= 1e-10 * (1 + fabs(re));
}

/* 1 if e is a constant with a (real, double) value < 0 */
static int neg_const(E *e) {
    double v;
    return is_const_expr(e) && to_double(e, &v) && v < 0;
}

/* Real n-th root of c for odd n: -(-c)^(1/n) when c is a negative constant
 * (the principal root c^(1/n) of a negative number would be complex),
 * otherwise c^(1/n). */
static E *real_root(E *c, long n) {
    /* real n-th root (n odd) */
    if (neg_const(c)) return s_neg(s_pow(s_neg(c), mk_frac(1, n)));
    return s_pow(c, mk_frac(1, n));
}

/* ------------------------------------------------------ polynomials --- */
/*
 * solve_cubic -- roots of a x^3 + b x^2 + c x + d (p.c[] ascending).
 * The substitution x = t - b/(3a) gives the depressed cubic
 *     t^3 + P t + Q = 0,  P = (3ac - b^2)/(3a^2),
 *                         Q = (2b^3 - 9abc + 27a^2 d)/(27a^3).
 * Its discriminant is -(4P^3 + 27Q^2) (evaluated in doubles to choose a
 * form):
 *   > 0  three distinct real roots; Cardano would need cube roots of complex
 *        numbers ("casus irreducibilis"), so the trigonometric form is used:
 *        t_k = 2 sqrt(-P/3) cos( acos((3Q/(2P)) sqrt(-3/P))/3 - 2 pi k/3 ),
 *        k = 0, 1, 2 (here P < 0);
 *   <= 0 one real root by Cardano: t = u + v with u, v the real cube roots
 *        of -Q/2 +- sqrt(D), D = Q^2/4 + P^3/27; in complex mode also the
 *        pair -(u+v)/2 +- i sqrt(3)/2 (u - v).
 * If P or Q is not numeric (symbolic coefficients) only the principal
 * Cardano root is produced.  Results are appended to out unverified.
 * D = 0 (a repeated root; up to rounding for float coefficients) gives the
 * simple root t = 3Q/P and the double root -3Q/(2P) (t = 0 if P = 0).
 * Cubics reaching this point from the exact path are irreducible over Q
 * (hence squarefree), but float or irrational coefficients can hit this
 * case.
 */
static void solve_cubic(UP p, E *x, slist *out) {
    E *a = p.c[3], *b = p.c[2], *c = p.c[1], *d = p.c[0];
    /* x = t - b/(3a): t^3 + P t + Q */
    E *P = rat_normal(s_div(s_sub(s_mul2(mk_int(3), s_mul2(a, c)), s_powi(b, 2)), s_mul2(mk_int(3), s_powi(a, 2))));
    E *Q = rat_normal(s_div(s_add(3, (E *[]){s_mul2(K->c2, s_powi(b, 3)), s_mul2(mk_int(-9), s_mul(3, (E *[]){a, b, c})),
                                            s_mul2(mk_int(27), s_mul2(s_powi(a, 2), d))}),
                            s_mul2(mk_int(27), s_powi(a, 3))));
    E *shift = s_neg(s_div(b, s_mul2(mk_int(3), a)));
    double pv, qv;
    if (!to_double(P, &pv) || !to_double(Q, &qv)) {
        /* symbolic: Cardano principal root only */
        E *D = s_add2(s_div(s_powi(Q, 2), mk_int(4)), s_div(s_powi(P, 3), mk_int(27)));
        E *u = s_pow(s_add2(s_neg(s_div(Q, K->c2)), s_sqrt(D)), mk_frac(1, 3));
        E *t = s_sub(u, s_div(P, s_mul2(mk_int(3), u)));
        sl_add(out, s_add2(t, shift));
        if (g_cplx) {
            /* the other two roots: u -> w u, w^2 u with w = (-1 + i sqrt(3))/2 */
            E *w = s_mul2(K->chalf, s_add2(mk_int(-1), s_mul2(K->i_, s_sqrt(mk_int(3)))));
            for (int k = 1; k <= 2; k++) {
                E *wu = s_mul2(k == 1 ? w : s_powi(w, 2), u);
                sl_add(out, s_add2(s_sub(wu, s_div(P, s_mul2(mk_int(3), wu))), shift));
            }
        }
        return;
    }
    double disc = -(4 * pv * pv * pv + 27 * qv * qv);
    if (disc > 0) {
        /* three real roots: trigonometric form */
        E *m = s_mul2(K->c2, s_sqrt(s_div(s_neg(P), mk_int(3))));
        E *arg = s_mul2(s_div(s_mul2(mk_int(3), Q), s_mul2(K->c2, P)), s_sqrt(s_div(mk_int(-3), P)));
        E *th = s_div(s_fn1(F_ACOS, arg), mk_int(3));
        for (int k = 0; k < 3; k++) {
            E *ang = s_sub(th, s_frac_pi(mk_frac(2 * k, 3)));
            sl_add(out, s_add2(s_mul2(m, s_fn1(F_COS, ang)), shift));
        }
        return;
    }
    E *D = rat_normal(s_add2(s_div(s_powi(Q, 2), mk_int(4)), s_div(s_powi(P, 3), mk_int(27))));
    /* D = 0 (relative to rounding for floats): repeated root.  P = 0 gives
     * the triple root t = 0, else t = 3Q/P and the double root -3Q/(2P). */
    if (has_float(P) || has_float(Q) ? fabs(disc) <= 1e-10 * (4 * fabs(pv * pv * pv) + 27 * qv * qv)
                                     : is_zero(D)) {
        if (pv == 0) {
            sl_add(out, shift);
            return;
        }
        E *t1 = s_div(s_mul2(mk_int(3), Q), P);
        sl_add(out, s_add2(t1, shift));
        sl_add(out, s_add2(s_mul2(mk_frac(-1, 2), t1), shift));
        return;
    }
    E *sd = s_sqrt(D);
    E *u = real_root(s_add2(s_neg(s_div(Q, K->c2)), sd), 3);
    E *v = real_root(s_sub(s_neg(s_div(Q, K->c2)), sd), 3);
    E *t = s_add2(u, v);
    sl_add(out, s_add2(t, shift));
    if (g_cplx) {
        /* t = -(u+v)/2 +- i sqrt(3)/2 (u - v) */
        E *re = s_neg(s_div(t, K->c2));
        E *im = s_mul2(s_mul2(K->chalf, s_sqrt(mk_int(3))), s_sub(u, v));
        sl_add(out, s_add2(s_add2(re, s_mul2(K->i_, im)), shift));
        sl_add(out, s_add2(s_sub(re, s_mul2(K->i_, im)), shift));
    }
}

/* All roots of a polynomial with constant coefficients as floats via
 * proot_expr (Aberth iteration, upoly.c); non-real roots are skipped in
 * real mode. */
static void numeric_poly_roots(UP p, E *x, slist *out) {
    E *pe = up_to(p, x);
    E *r = proot_expr(pe, x);
    for (uint32_t i = 0; i < r->n; i++) {
        E *z = EK(r)[i];
        if (!g_cplx && !freeof_sym(z, S_I)) continue;
        sl_add(out, z);
    }
}

static long lgcd(long a, long b) {
    while (b) {
        long t = a % b;
        a = b;
        b = t;
    }
    return a < 0 ? -a : a;
}

static int up_exact(UP p) {
    for (long i = 0; i <= p.d; i++)
        if (!is_exactnum(p.c[i])) return 0;
    return 1;
}

/*
 * unity_roots -- p (rational coefficients) whose roots are all roots of
 * unity (cyclotomic factors such as x^4+x^3+x^2+x+1): the numeric roots
 * suggest the order n, p | x^n - 1 is then checked exactly and the roots
 * are given as cos(2 pi k/n) + i sin(2 pi k/n).  Returns 0 (nothing added)
 * if p is not of this kind.
 */
static int unity_roots(UP p, slist *out) {
    long d = p.d, n = 1, ks[40];
    if (d < 2 || d > 40 || !up_exact(p)) return 0;
    double cr[41], ci[41], rr[40], ri[40], t[40];
    for (long i = 0; i <= d; i++) {
        if (!to_double(p.c[i], &cr[i])) return 0;
        ci[i] = 0;
    }
    if (poly_roots_c((int)d, cr, ci, rr, ri) != d) return 0;
    for (long i = 0; i < d; i++) {
        if (fabs(hypot(rr[i], ri[i]) - 1) > 1e-7) return 0;
        t[i] = atan2(ri[i], rr[i]) / (2 * M_PI);
        if (t[i] < 0) t[i] += 1;
        long q = 1;
        while (q <= 120 && fabs(t[i] * q - round(t[i] * q)) > 1e-7) q++;
        if (q > 120) return 0;
        n = n / lgcd(n, q) * q;
        if (n > 240) return 0;
    }
    UP xn = up_alloc(n), qq, r;
    xn.c[n] = K->c1;
    xn.c[0] = mk_int(-1);
    up_divrem(xn, p, &qq, &r);
    if (r.d >= 0) return 0;
    for (long i = 0; i < d; i++) {
        ks[i] = lround(t[i] * n) % n;
        long g = lgcd(2 * ks[i], n), k = 2 * ks[i] / g, m = n / g; /* angle k pi / m */
        if (!g_cplx && m > 1) continue;
        E *ang = s_frac_pi(mk_frac(k, m));
        sl_add(out, m == 1 ? (k ? mk_int(-1) : K->c1)
                           : s_add2(s_fn1(F_COS, ang), s_mul2(K->i_, s_fn1(F_SIN, ang))));
    }
    return 1;
}

static void solve_poly(UP p, E *x, slist *out);

/*
 * shift_quartic -- exact p of degree >= 4: with the shift x = y - c[d-1]/(d c[d])
 * the polynomial may become one in y^g (Liska's degree 8 example becomes a
 * quartic in y^2); solve that.  For a depressed quartic y^4 + a y^2 + b y + c
 * whose Ferrari resolvent 8m^3 + 8a m^2 + (2a^2 - 8c) m - b^2 has a rational
 * root m != 0, the quartic splits into the quadratics
 *     y^2 -+ s y + a/2 + m +- b/(2s),  s = sqrt(2m).
 * Returns 1 if the roots were produced.
 */
static int shift_quartic(UP p, E *x, slist *out) {
    if (p.d < 4 || !up_exact(p)) return 0;
    E *sh = rat_normal(s_div(p.c[p.d - 1], s_mul2(mk_int(p.d), p.c[p.d])));
    UP q;
    if (!up_from(expand(s_div(subst(up_to(p, x), x, s_sub(x, sh)), p.c[p.d])), x, &q) || q.d != p.d) return 0;
    long g = 0;
    for (long i = 1; i <= q.d; i++)
        if (!is_zero(q.c[i])) g = lgcd(i, g);
    slist ys = {0, 0, 0};
    if (g > 1 && !is_zero(sh)) {
        solve_poly(q, x, &ys);
    } else if (q.d == 4 && g == 1) {
        E *a = q.c[2], *b = q.c[1], *c = q.c[0], *T = mk_sym_name("_fm"), *content, *m = NULL;
        E *res = s_add(4, (E *[]){s_mul2(mk_int(8), s_powi(T, 3)), s_mul(3, (E *[]){mk_int(8), a, s_powi(T, 2)}),
                                  s_mul2(s_sub(s_mul2(K->c2, s_powi(a, 2)), s_mul2(mk_int(8), c)), T), s_neg(s_powi(b, 2))});
        E *fl = factor_list_in(expand(res), T, &content);
        for (uint32_t i = 0; i < fl->n; i++) {
            UP f;
            if (!up_from(K0(EK(fl)[i]), T, &f) || f.d != 1) continue;
            E *r = rat_normal(s_neg(s_div(f.c[0], f.c[1])));
            double v;
            if (is_zero(r) || !to_double(r, &v)) continue;
            if (!m || v > 0) m = r;
        }
        if (!m) return 0;
        E *s = s_sqrt(s_mul2(K->c2, m)), *h = rat_normal(s_add2(s_div(a, K->c2), m));
        for (int sg = -1; sg <= 1; sg += 2) {
            UP qd = up_alloc(2);
            qd.c[2] = K->c1;
            qd.c[1] = sg < 0 ? s_neg(s) : s;
            qd.c[0] = s_sub(h, s_mul2(mk_int(sg), s_div(b, s_mul2(K->c2, s))));
            solve_poly(qd, x, &ys);
        }
    } else
        return 0;
    for (uint32_t i = 0; i < ys.n; i++) sl_add(out, s_sub(ys.v[i], sh));
    return 1;
}

/*
 * solve_poly -- roots of the univariate polynomial p (coefficients free of
 * x, possibly symbolic), appended to out.  Cases, in order:
 *   degree 1      -c0/c1;
 *   degree 2      (-b -+ sqrt(b^2 - 4ac)) / (2a); in real mode nothing if
 *                 the discriminant is a negative constant;
 *   binomial      a x^n + c: real mode gives the real root (n odd) or
 *                 +-c^(1/n) (n even, c not negative); complex mode gives all
 *                 n roots |c|^(1/n) (cos + i sin)((arg c + 2 pi k)/n);
 *   in x^g        if all exponents with nonzero coefficient share a gcd
 *                 g > 1, solve q(y) = 0 for y = x^g, then each x^g = y;
 *   degree 3      solve_cubic;
 *   otherwise     numeric roots if all coefficients are constants (quartic
 *                 formulas are not used); symbolic high-degree: nothing.
 * Candidates are not verified here (solve1 does that).
 */
static void solve_poly(UP p, E *x, slist *out) {
    stack_check();
    poll_intr();
    if (p.d <= 0) return;
    if (p.d == 1) {
        sl_add(out, rat_normal(s_neg(s_div(p.c[0], p.c[1]))));
        return;
    }
    if (p.d == 2) {
        E *a = p.c[2], *b = p.c[1], *c = p.c[0];
        E *disc = rat_normal(s_sub(s_powi(b, 2), s_mul2(mk_int(4), s_mul2(a, c))));
        if (!g_cplx && neg_const(disc)) return;
        /* disc = +-p^2 for a polynomial p (t^2 w^2): +-p, +-i p are the same
         * pair of roots as +-sqrt(disc) */
        E *sd = s_sqrt(disc);
        if (g_cplx && !is_const_expr(disc)) {
            int ng = is_negative_term(disc);
            E *content, *fl = factor_list(ng ? s_neg(disc) : disc, &content), *r = s_sqrt(content);
            for (uint32_t i = 0; r && i < fl->n; i++) {
                long m;
                if (!num_is_small_int(K1(EK(fl)[i]), &m) || (m & 1)) r = NULL;
                else r = s_mul2(r, s_powi(K0(EK(fl)[i]), m / 2));
            }
            if (r) sd = ng ? s_mul2(K->i_, r) : r;
        }
        E *den = s_mul2(K->c2, a);
        sl_add(out, s_div(s_sub(s_neg(b), sd), den));
        sl_add(out, s_div(s_add2(s_neg(b), sd), den));
        return;
    }
    /* binomial a x^n + c */
    int binom = 1;
    for (long i = 1; i < p.d; i++)
        if (!is_zero(p.c[i])) binom = 0;
    if (binom) {
        E *c = rat_normal(s_neg(s_div(p.c[0], p.c[p.d])));
        long n = p.d;
        if (!g_cplx) {
            if (n & 1) sl_add(out, real_root(c, n));
            else if (!neg_const(c)) {
                E *r = s_pow(c, mk_frac(1, n));
                sl_add(out, s_neg(r));
                sl_add(out, r);
            }
            return;
        }
        /* all n roots: |c|^(1/n) * exp(i (arg c + 2 pi k)/n) */
        E *r = s_pow(s_fn1(F_ABS, c), mk_frac(1, n));
        E *argc = s_fn1(F_ARG, c);
        for (long k = 0; k < n; k++) {
            E *ang = s_div(s_add2(argc, s_frac_pi(mk_int(2 * k))), mk_int(n));
            E *z = s_mul2(r, s_add2(s_fn1(F_COS, ang), s_mul2(K->i_, s_fn1(F_SIN, ang))));
            sl_add(out, z);
        }
        return;
    }
    if (unity_roots(p, out)) return;
    /* polynomial in x^k */
    long g = 0;
    for (long i = 1; i <= p.d; i++)
        if (!is_zero(p.c[i])) {
            long a = i, b = g;
            while (b) {
                long t = a % b;
                a = b;
                b = t;
            }
            g = a;
        }
    if (g > 1) {
        UP q = up_alloc(p.d / g);
        for (long i = 0; i <= q.d; i++) q.c[i] = p.c[i * g];
        slist ys = {0, 0, 0};
        solve_poly(q, x, &ys);
        for (uint32_t i = 0; i < ys.n; i++) {
            E *bi = s_powi(x, g);
            E *eq = s_sub(bi, ys.v[i]);
            UP pp;
            if (up_from(expand(eq), x, &pp)) solve_poly(pp, x, out);
        }
        return;
    }
    /* perfect power c (x - r)^d (irrational or complex coefficients: not factored over Q) */
    {
        E *r = rat_normal(s_neg(s_div(p.c[p.d - 1], s_mul2(mk_int(p.d), p.c[p.d]))));
        int pp = 1;
        E *bin = K->c1;
        for (long k = p.d - 1; k >= 0 && pp; k--) {
            /* coefficient of x^k in c (x - r)^d: c binom(d, k) (-r)^(d-k) */
            bin = rat_normal(s_div(s_mul2(bin, mk_int(k + 1)), mk_int(p.d - k)));
            E *ck = s_mul(3, (E *[]){p.c[p.d], bin, s_powi(s_neg(r), p.d - k)});
            if (!is_zero(rat_normal(s_sub(ck, p.c[k])))) pp = 0;
        }
        if (pp) {
            sl_add(out, r);
            return;
        }
    }
    if (p.d == 3) {
        solve_cubic(p, x, out);
        return;
    }
    if (shift_quartic(p, x, out)) return;
    /* numeric */
    for (long i = 0; i <= p.d; i++)
        if (!is_const_expr(p.c[i])) {
            /* symbolic coefficients of high degree: give up */
            return;
        }
    numeric_poly_roots(p, x, out);
}

/* ----------------------------------------------------------- kernels --- */
/*
 * find_kernels -- collect the distinct "kernels" of e with respect to x:
 * maximal non-polynomial subexpressions depending on x, i.e. function
 * applications f(...) and powers whose exponent depends on x or is not an
 * integer (e^x, x^(1/2), 2^x).  Sums, products and integer powers are
 * looked through.  If e is a polynomial in its kernels, solving reduces to
 * solving for the kernels.  At most cap kernels are stored (the rest are
 * silently ignored; callers use cap = 8).
 */
static void find_kernels(E *e, E *x, E **ks, uint32_t *n, uint32_t cap) {
    stack_check();
    if (freeof(e, x) || expr_eq(e, x)) return;
    if (e->t == E_FN || (e->t == E_POW && (!freeof(K1(e), x) || K1(e)->t != E_INT))) {
        for (uint32_t i = 0; i < *n; i++)
            if (expr_eq(ks[i], e)) return;
        if (*n < cap) ks[(*n)++] = e;
        return;
    }
    if (e->t == E_ADD || e->t == E_MUL || e->t == E_POW)
        for (uint32_t i = 0; i < e->n; i++) find_kernels(EK(e)[i], x, ks, n, cap);
}

/* can invert() handle this kernel shape?  (x itself, a unary function, or
 * a power whose base or exponent is free of x -- not x^x) */
static int invertible_kernel(E *g, E *x) {
    if (expr_eq(g, x)) return 1;
    if (g->t == E_FN && g->n == 1) return 1;
    if (g->t == E_POW) return freeof(K1(g), x) || freeof(K0(g), x);
    return 0;
}

/*
 * invert -- solve g(x) = v by undoing the outermost operation of g and
 * recursing (through solve1) on the inner argument u.
 *   sin u = v   u = asin v  and  u = pi - asin v   (none if |v| > 1, real)
 *   cos u = v   u = +-acos v                       (none if |v| > 1, real)
 *   tan, sinh, tanh, asin, acos, atan, asinh, acosh, atanh: one inverse
 *   cosh u = v  u = +-acosh v;   ln u = v -> u = e^v
 *   |u| = v     u = +-v (none if v is a negative constant)
 *   b^c = v, c free of x:  b = v^(1/c); for a rational c = p/q with q even
 *               no real solution when v < 0;
 *   b^c = v, b free of x:  c = ln v / ln b (v != 0; v > 0 in real mode).
 * Only principal solutions of periodic functions are produced (no 2 pi n
 * families).  Spurious candidates are removed by the verification in
 * solve1.  depth bounds the recursion (> 8: give up).
 */
static void invert(E *g, E *v, E *x, slist *out, int depth) {
    stack_check();
    if (expr_eq(g, x)) {
        sl_add(out, v);
        return;
    }
    if (depth > 8) {
        g_incomplete = 1;
        return;
    }
    E *u = NULL;
    if (g->t == E_FN && g->n == 1) u = K0(g);
    switch (g->t) {
    case E_FN:
        if (!u) return;
        switch (g->s) {
        case F_SIN:
            if (!g_cplx && is_const_expr(v)) {
                double d;
                if (to_double(v, &d) && fabs(d) > 1) return;
            }
            solve1(s_sub(u, s_fn1(F_ASIN, v)), x, out, depth + 1);
            solve1(s_sub(u, s_sub(K->pi_, s_fn1(F_ASIN, v))), x, out, depth + 1);
            return;
        case F_COS:
            if (!g_cplx && is_const_expr(v)) {
                double d;
                if (to_double(v, &d) && fabs(d) > 1) return;
            }
            solve1(s_sub(u, s_fn1(F_ACOS, v)), x, out, depth + 1);
            solve1(s_add2(u, s_fn1(F_ACOS, v)), x, out, depth + 1);
            return;
        case F_TAN: solve1(s_sub(u, s_fn1(F_ATAN, v)), x, out, depth + 1); return;
        case F_LN:
            solve1(s_sub(u, s_exp(v)), x, out, depth + 1);
            return;
        case F_ASIN: solve1(s_sub(u, s_fn1(F_SIN, v)), x, out, depth + 1); return;
        case F_ACOS: solve1(s_sub(u, s_fn1(F_COS, v)), x, out, depth + 1); return;
        case F_ATAN: solve1(s_sub(u, s_fn1(F_TAN, v)), x, out, depth + 1); return;
        case F_SINH: solve1(s_sub(u, s_fn1(F_ASINH, v)), x, out, depth + 1); return;
        case F_COSH:
            solve1(s_sub(u, s_fn1(F_ACOSH, v)), x, out, depth + 1);
            solve1(s_add2(u, s_fn1(F_ACOSH, v)), x, out, depth + 1);
            return;
        case F_TANH: solve1(s_sub(u, s_fn1(F_ATANH, v)), x, out, depth + 1); return;
        case F_ASINH: solve1(s_sub(u, s_fn1(F_SINH, v)), x, out, depth + 1); return;
        case F_ACOSH: solve1(s_sub(u, s_fn1(F_COSH, v)), x, out, depth + 1); return;
        case F_ATANH: solve1(s_sub(u, s_fn1(F_TANH, v)), x, out, depth + 1); return;
        case F_ABS:
            if (neg_const(v)) return;
            solve1(s_sub(u, v), x, out, depth + 1);
            solve1(s_add2(u, v), x, out, depth + 1);
            return;
        }
        g_incomplete = 1;
        return;
    case E_POW: {
        E *b = K0(g), *ex = K1(g);
        if (freeof(ex, x)) {
            /* b^ex = v -> b = v^(1/ex) */
            if (ex->t == E_RAT) {
                if (!g_cplx && neg_const(v) && int_is_even(K1(ex))) return;
                solve1(s_sub(b, s_pow(v, s_inv(ex))), x, out, depth + 1);
                return;
            }
            long k;
            if (num_is_small_int(ex, &k)) {
                solve1(s_sub(g, v), x, out, depth + 1);
                return;
            }
            solve1(s_sub(b, s_pow(v, s_inv(ex))), x, out, depth + 1);
            return;
        }
        if (freeof(b, x)) {
            if (is_zero(b)) {
                /* 0^u is 1 at u = 0 and 0 or undefined elsewhere */
                if (is_one(v)) solve1(ex, x, out, depth + 1);
                else if (is_zero(v)) g_incomplete = 1;
                return;
            }
            if (!g_cplx && neg_const(b)) {
                /* real mode: b^u with b < 0 is real only at integer u; u = k with
                 * |b|^k = |v| and the sign of b^k matching v */
                double dv, db;
                if (!is_const_expr(v) || !to_double(v, &dv) || !to_double(b, &db)) {
                    g_incomplete = 1;
                    return;
                }
                if (dv == 0) return;
                double kd = log(fabs(dv)) / log(fabs(db));
                if (!(fabs(kd) < 1e6) || fabs(kd - floor(kd + 0.5)) > 1e-9) return;
                E *kk = mk_int((long)floor(kd + 0.5));
                if (is_zero(rat_normal(s_sub(s_pow(b, kk), v)))) solve1(s_sub(ex, kk), x, out, depth + 1);
                return;
            }
            if (!g_cplx && neg_const(v)) return;
            if (is_zero(v)) return;
            E *lv = ISYM(b, S_E) ? s_ln(v) : s_div(s_ln(v), s_ln(b));
            solve1(s_sub(ex, lv), x, out, depth + 1);
            return;
        }
        if (is_one(v)) {
            /* b^u = 1 (x^(x-1) = 1): b = 1, u = 0, or b = -1 with u even there */
            slist m = {0, 0, 0};
            solve1(s_sub(b, K->c1), x, out, depth + 1);
            solve1(ex, x, out, depth + 1);
            solve1(s_add2(b, K->c1), x, &m, depth + 1);
            for (uint32_t i = 0; i < m.n; i++) {
                E *k = subst(ex, x, m.v[i]);
                if (k->t == E_INT && int_is_even(k)) sl_add(out, m.v[i]);
            }
            return;
        }
        g_incomplete = 1;
        return;
    }
    default: g_incomplete = 1; return;
    }
}

/*
 * exp_rate -- check that x occurs in e only inside exponentials
 * base^(a x + b) (base and a free of x) and find a common rate.
 * The rate of base^(a x + b) is r = a ln(base) (a for base e).  *rate
 * (initially NULL) receives the first rate; every further rate must be a
 * rational multiple of it, and for numeric rates *rate becomes their gcd
 * (poly_gcd_expr), so that each exponential is e^(b) * T^k with
 * T = e^(rate x) and rational k.  Returns 1 if e has this form, 0
 * otherwise (x outside an exponential, non-linear exponent, a function of
 * x, or incommensurable rates).
 */
static int exp_rate(E *e, E *x, E **rate) {
    stack_check();
    if (freeof(e, x)) return 1;
    if (e->t == E_POW && freeof(K0(e), x) && !freeof(K1(e), x)) {
        E *d = diff(K1(e), x);
        if (!freeof(d, x)) return 0;
        E *r = ISYM(K0(e), S_E) ? d : s_mul2(d, s_ln(K0(e)));
        /* symbolic rate whose ratio to *rate does not auto-simplify to a
         * number: decide via the rational normal form (the !*rate branch
         * inside is unreachable, since r / r simplifies to 1) */
        if (!is_exactnum(r) && !is_exactnum(s_div(r, *rate ? *rate : r))) {
            if (!*rate) {
                *rate = r;
                return 1;
            }
            E *q = rat_normal(s_div(r, *rate));
            if (!is_exactnum(q)) return 0;
            return 1;
        }
        if (!*rate) *rate = r;
        else {
            E *q = rat_normal(s_div(r, *rate));
            if (!is_exactnum(q)) return 0;
            if (is_exactnum(r) && is_exactnum(*rate)) *rate = poly_gcd_expr(r, *rate);
        }
        return 1;
    }
    if (e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM) return !expr_eq(e, x) ? 1 : 0;
    if (e->t == E_FN) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (!exp_rate(EK(e)[i], x, rate)) return 0;
    return 1;
}

/* Rewrite e (validated by exp_rate) in terms of T = e^(rate x): each
 * base^(a x + b) becomes base^b * T^(a ln(base) / rate); other nodes are
 * rebuilt with rewritten children.  The result is free of x. */
static E *exp_to_T(E *e, E *x, E *rate, E *T) {
    stack_check();
    if (freeof(e, x)) return e;
    if (e->t == E_POW && freeof(K0(e), x)) {
        E *d = diff(K1(e), x);
        E *r = ISYM(K0(e), S_E) ? d : s_mul2(d, s_ln(K0(e)));
        E *k = rat_normal(s_div(r, rate));
        E *b0 = expand(s_sub(K1(e), s_mul2(d, x)));
        return s_mul2(s_pow(K0(e), b0), s_pow(T, k));
    }
    E **kk = salloc(sizeof(E *) * (e->n + 1));
    for (uint32_t i = 0; i < e->n; i++) kk[i] = exp_to_T(EK(e)[i], x, rate, T);
    return rebuild(e, kk);
}

/* ln t expanded for t = c * prod b_i^u_i with all b_i and c free of x:
 * ln c + sum u_i ln b_i; NULL for any other shape. */
static E *ln_monomial(E *t, E *x) {
    uint32_t n = t->t == E_MUL ? t->n : 1;
    E **f = t->t == E_MUL ? EK(t) : &t, **l = salloc(sizeof(E *) * n);
    for (uint32_t i = 0; i < n; i++) {
        if (freeof(f[i], x)) l[i] = s_ln(f[i]);
        else if (f[i]->t == E_POW && freeof(K0(f[i]), x)) l[i] = s_mul2(K1(f[i]), s_ln(K0(f[i])));
        else return NULL;
    }
    return s_add(n, l);
}

/* ------------------------------------------------------------ numeric --- */
/*
 * numeric_real_roots -- last-resort real root finder for f(x) = 0.
 * Samples f at 4001 equally spaced points of [-100, 100] (step 0.05); in
 * every cell where f changes sign (or hits 0 exactly) the root is refined
 * by up to 200 bisection steps.  A sign change across a pole (e.g. tan x,
 * 1/x) is rejected by requiring |f(root)| < 1e-6 (1 + |f| at the cell
 * ends).  New roots within relative 1e-9 of an existing entry are dropped;
 * results are appended as float nodes.
 * Limitations: roots outside [-100, 100], roots of even multiplicity (no
 * sign change) and pairs closer than one cell are missed.  Points where f
 * cannot be evaluated in doubles simply break the chain of samples.
 */
/* f(x) as a real double: strict (|im| <= 1e-9 |re|), unlike eval_double_at whose
 * absolute tolerance calls tiny complex values like (-99.5)^(-99.5) real; *sc
 * gets the sum of |terms| of f (the rounding scale of the value) */
static int real_at(E *f, int var, double x, double *v, double *sc) {
    double re, im;
    if (!eval_complex_at(f, var, x, 0, &re, &im) || !isfinite(re) || fabs(im) > 1e-9 * fabs(re)) return 0;
    *v = re;
    *sc = fabs(re);
    if (f->t == E_ADD) {
        double s = 0, tr, ti;
        for (uint32_t j = 0; j < f->n; j++)
            if (eval_complex_at(EK(f)[j], var, x, 0, &tr, &ti)) s += hypot(tr, ti);
        if (isfinite(s) && s > *sc) *sc = s;
    }
    return 1;
}

/* best rational p/q with q <= qmax and |v - p/q| <= 1e-10 (1 + |v|), or NULL */
static E *rat_approx(double v, long qmax) {
    double a = v, h0 = 0, h1 = 1, k0 = 1, k1 = 0;
    for (int it = 0; it < 40; it++) {
        double fl = floor(a), h2 = fl * h1 + h0, k2 = fl * k1 + k0;
        if (k2 > qmax || fabs(h2) > 1e12) return NULL;
        if (fabs(v - h2 / k2) <= 1e-10 * (1 + fabs(v))) return mk_frac((int64_t)h2, (int64_t)k2);
        h0 = h1, h1 = h2, k0 = k1, k1 = k2;
        if (a - fl < 1e-14) return NULL;
        a = 1 / (a - fl);
    }
    return NULL;
}

/* an exact value for the numeric root r of f: p/q, exp(p/q) or +-sqrt(p/q),
 * kept only if f vanishes there exactly (abs and max split equations,
 * sqrt(ln x) = ln(sqrt x)); else NULL */
static E *snap_root(E *f, E *x, double r) {
    E *c[3] = {rat_approx(r, 1000), r > 0 ? rat_approx(log(r), 12) : NULL, rat_approx(r * r, 1000)};
    if (c[1]) c[1] = s_exp(c[1]);
    if (c[2]) c[2] = r < 0 ? s_neg(s_sqrt(c[2])) : s_sqrt(c[2]);
    for (int i = 0; i < 3; i++) {
        if (!c[i]) continue;
        tryf tf;
        try_enter(&tf);
        if (!setjmp(tf.jb)) {
            int z = is_zero(rat_normal(subst(f, x, c[i])));
            try_leave(&tf);
            if (z) return c[i];
        } else {
            try_leave(&tf);
            if (K->err == CAS_ERR_INTERRUPTED) rethrow();
        }
    }
    return NULL;
}

static void numeric_real_roots(E *f, E *x, slist *out) {
    /* (px, pv, pok) = previous sample: position, value, "value defined" */
    int var = x->s;
    double lo = -100, hi = 100;
    g_incomplete = 1;
    int N = 4000;
    double px = lo, pv, psc, csc;
    int pok = real_at(f, var, px, &pv, &psc);
    for (int i = 1; i <= N; i++) {
        if ((i & 63) == 0) poll_intr();
        double cx = lo + (hi - lo) * i / N, cv;
        int cok = real_at(f, var, cx, &cv, &csc);
        /* a sign change within rounding noise of the terms (cosh x + sinh x at
         * x = -99, tanh(x-3) tanh(x+3) - 1 for large x) is not a root */
        if (pok && cok && fmax(fabs(pv), fabs(cv)) < 1e-12 * fmax(psc, csc)) cok = 0;
        if (pok && cok && ((pv < 0 && cv > 0) || (pv > 0 && cv < 0) || cv == 0)) {
            double a = px, b = cx, fa = pv;
            if (cv == 0) {
                a = b = cx;
            } else {
                for (int it = 0; it < 200; it++) {
                    double m = (a + b) / 2, fm;
                    if (!eval_double_at(f, var, m, &fm)) break;
                    if ((fa < 0) == (fm < 0)) {
                        a = m;
                        fa = fm;
                    } else
                        b = m;
                }
            }
            double r = (a + b) / 2, fr;
            /* reject poles: |f| must be small */
            if (eval_double_at(f, var, r, &fr) && fabs(fr) < 1e-6 * (1 + fabs(pv) + fabs(cv))) {
                int dup = 0;
                for (uint32_t k = 0; k < out->n; k++) {
                    double v;
                    if (to_double(out->v[k], &v) && fabs(v - r) < 1e-9 * (1 + fabs(r))) dup = 1;
                }
                if (!dup) {
                    E *ex = snap_root(f, x, r);
                    sl_add(out, ex ? ex : mk_flt(r));
                }
            }
        }
        px = cx;
        pv = cv;
        psc = csc;
        pok = cok;
    }
}

/* ------------------------------------------------------------- driver --- */
/* 1 if f is a polynomial in x whose coefficients are free of x (up_from
 * alone would also accept coefficients like sin(x)); stores it in *p. */
static int poly_in_x(E *f, E *x, UP *p) {
    if (!up_from(f, x, p)) return 0;
    for (long i = 0; i <= p->d; i++)
        if (!freeof(p->c[i], x)) return 0;
    return 1;
}

/* poly.c: replace x by T^Q, x^c by T^(c Q) */
E *subst_rad(E *e, E *x, E *T, E *Q);

/*
 * radical_lcm -- if every occurrence of x in e is x itself or x^(p/q)
 * (inside sums, products and integer powers), return the lcm of the
 * denominators q (1 if there are no radicals).  Returns 0 if the lcm would
 * exceed 60 or a denominator is not a small integer; returns 0 and sets
 * *other when x appears in any other context (e.g. sqrt(x+1), sin x).
 * With m = the lcm, x = T^m turns e into a rational function of T.
 */
static long radical_lcm(E *e, E *x, int *other) {
    stack_check();
    if (expr_eq(e, x)) return 1;
    if (freeof(e, x)) return 1;
    if (e->t == E_POW && expr_eq(K0(e), x) && is_exactnum(K1(e))) {
        long q;
        if (!num_is_small_int(num_denom(K1(e)), &q)) return 0;
        return q;
    }
    if (e->t == E_ADD || e->t == E_MUL || (e->t == E_POW && K1(e)->t == E_INT)) {
        long l = 1;
        for (uint32_t i = 0; i < e->n; i++) {
            long r = radical_lcm(EK(e)[i], x, other);
            if (!r) return 0;
            long a = l, b = r;
            while (b) {
                long t = a % b;
                a = b;
                b = t;
            }
            l = l / a * r;
            if (l > 60) return 0;
        }
        return l;
    }
    *other = 1;
    return 0;
}

/*
 * solve_factor -- candidate solutions of g = 0 for one irreducible factor g
 * of the numerator (appended to out, NOT verified; solve1 verifies).
 * Strategies, first applicable wins:
 *   1. polynomial in x                       -> solve_poly
 *   2. only radicals x^(p/q) of x            -> x = T^m, solve in T, x = t^m
 *      (in real mode only roots t with Re t > 0 or t = 0 are kept, matching
 *      the principal branch of T = x^(1/m); with real odd roots,
 *      settings.real, and m odd every real t is kept)
 *   3. exactly one kernel k(x), g polynomial in k  -> solve for T = k, then
 *      invert(k, T-value); if k cannot be inverted (x^x) fall back to the
 *      numeric scan
 *   4. one radical kernel u^(p/q) appearing linearly: g = c1 T + c0 gives
 *      u^(p/q) = v, raised to the q-th power: u^p = v^q (may add extraneous
 *      roots, removed by verification); same with several kernels for the
 *      first radical that appears linearly (repeated isolation handles
 *      sqrt(x) + sqrt(x+1) = 3 style equations)
 *   5. sums of exponentials with commensurable rates -> T = e^(rate x),
 *      solve in T, x = ln(T)/rate (principal log; T <= 0 skipped in real
 *      mode)
 *   6. trig: expand multiple angles, rewrite tan/cot/sec/csc into sin/cos;
 *      if all kernels are sin(u)/cos(u) of one argument u, substitute
 *      sin u = 2t/(1+t^2), cos u = (1-t^2)/(1+t^2) (t = tan(u/2)), solve the
 *      rational equation in t and u = 2 atan(t); u = pi (t = infinity) is
 *      checked separately
 *   7. numeric_real_roots on g.
 * depth is the solve1 recursion depth (also selects auxiliary symbols).
 */
static void solve_factor(E *g, E *x, slist *out, int depth) {
    UP p;
    if (poly_in_x(g, x, &p)) {
        solve_poly(p, x, out);
        return;
    }
    {
        /* radicals of x only: x = T^m gives a polynomial equation in T */
        int other = 0;
        long m = radical_lcm(g, x, &other);
        if (m > 1 && !other) {
            E *T = mk_sym_name(depth & 1 ? "_m1" : "_m0");
            E *h = numer_of(subst_rad(g, x, T, mk_int(m)));
            slist ts = {0, 0, 0};
            solve1(h, T, &ts, depth + 1);
            for (uint32_t i = 0; i < ts.n; i++) {
                E *t = ts.v[i];
                /* principal roots: T = x^(1/m) has argument in (-pi/m, pi/m];
                 * with real odd roots (settings.real, m odd) T is any real */
                double re, im;
                int realt = K->set.real && (m & 1) && eval_complex(t, &re, &im) && im == 0;
                if (!realt && eval_complex(t, &re, &im) && !(re > 0 || (re == 0 && im == 0)) && !g_cplx) continue;
                sl_add(out, s_pow(t, mk_int(m)));
            }
            return;
        }
    }
    uint32_t before = out->n;
    E *ks[8];
    uint32_t nk = 0;
    find_kernels(g, x, ks, &nk, 8);
    if (nk == 1) {
        /* polynomial in the kernel? */
        E *T = mk_sym_name(depth & 1 ? "_k1" : "_k0");
        E *h = subst(g, ks[0], T);
        if (freeof(h, x)) {
            slist vals = {0, 0, 0};
            solve1(h, T, &vals, depth + 1);
            for (uint32_t i = 0; i < vals.n; i++) invert(ks[0], vals.v[i], x, out, depth + 1);
            /* inversion not available (e.g. x^x = 2): numeric search */
            if (out->n == before && vals.n && !invertible_kernel(ks[0], x)) numeric_real_roots(g, x, out);
            return;
        }
        /* radical kernel u^(p/q) appearing linearly: isolate and raise */
        if (ks[0]->t == E_POW && K1(ks[0])->t == E_RAT && depth < 6) {
            long dT;
            if (poly_degree_in(h, T, &dT) && dT == 1) {
                E *c1 = poly_coeff(h, T, 1), *c0 = poly_coeff(h, T, 0);
                E *v = s_neg(s_div(c0, c1));
                E *pp = K0(K1(ks[0])), *qq = K1(K1(ks[0]));
                /* u^p = v^q */
                E *eq = s_sub(s_pow(K0(ks[0]), pp), s_pow(v, qq));
                solve1(eq, x, out, depth + 1);
                return;
            }
        }
        /* x appears also outside: e.g. x*exp(x) = c -> numeric */
    }
    if (nk >= 2 && depth < 6) {
        /* several radicals: isolate the first one */
        for (uint32_t i = 0; i < nk; i++) {
            if (!(ks[i]->t == E_POW && K1(ks[i])->t == E_RAT)) continue;
            E *T = mk_sym_name(depth & 1 ? "_r1" : "_r0");
            E *h = expand(subst(g, ks[i], T));
            long dT;
            if (!poly_degree_in(h, T, &dT) || dT != 1) continue;
            E *c1 = poly_coeff(h, T, 1), *c0 = poly_coeff(h, T, 0);
            E *v = s_neg(s_div(c0, c1));
            E *eq = s_sub(s_pow(K0(ks[i]), K0(K1(ks[i]))), s_pow(v, K1(K1(ks[i]))));
            solve1(eq, x, out, depth + 1);
            return;
        }
    }
    /* sums of exponentials */
    E *rate = NULL;
    if (nk >= 1 && exp_rate(g, x, &rate) && rate) {
        E *T = mk_sym_name(depth & 1 ? "_e1" : "_e0");
        E *h = rat_normal(exp_to_T(g, x, rate, T));
        if (freeof(h, x)) {
            slist vals = {0, 0, 0};
            solve1(numer_of(h), T, &vals, depth + 1);
            for (uint32_t i = 0; i < vals.n; i++) {
                E *v = vals.v[i];
                if (!g_cplx && (neg_const(v) || is_zero(v))) continue;
                sl_add(out, s_div(s_ln(v), rate));
            }
            return;
        }
    }
    /* two terms with x only in exponents, incommensurable rates
     * (a b^x = c d^x): take logs, ln a + x ln b = ln c + x ln d;
     * x^a = x^b (x^x = x): x^(a-b) = 1, or x = 0 if a, b > 0 there */
    if (g->t == E_ADD && g->n == 2) {
        E *p[2] = {K0(g), s_neg(K1(g))}, *ex[2];
        E *l1 = ln_monomial(p[0], x), *l2 = ln_monomial(p[1], x);
        if (l1 && l2) {
            solve1(s_sub(l1, l2), x, out, depth + 1);
            return;
        }
        int ok = 1;
        if (is_negative_term(p[0])) p[0] = s_neg(p[0]), p[1] = s_neg(p[1]);
        for (int i = 0; i < 2; i++)
            if (expr_eq(p[i], x)) ex[i] = K->c1;
            else if (p[i]->t == E_POW && expr_eq(K0(p[i]), x)) ex[i] = K1(p[i]);
            else ok = 0;
        double r0, i0, r1, i1;
        if (ok) {
            if (eval_complex_at(ex[0], x->s, 0, 0, &r0, &i0) && eval_complex_at(ex[1], x->s, 0, 0, &r1, &i1) &&
                r0 > 0 && r1 > 0 && !i0 && !i1)
                sl_add(out, K->c0);
            invert(s_pow(x, s_sub(ex[0], ex[1])), K->c1, x, out, depth + 1);
            return;
        }
    }
    /* trig: expand multiple angles then half-angle substitution */
    if (has_trig(g) && depth < 4) {
        E *te = trig_sincos(trig_expand(g));
        E *ks2[8];
        uint32_t nk2 = 0;
        find_kernels(te, x, ks2, &nk2, 8);
        E *u = NULL;
        int same = nk2 > 0;
        for (uint32_t i = 0; i < nk2 && same; i++) {
            E *k = ks2[i];
            if (k->t == E_POW) k = K0(k);
            if (!(IFN(k, F_SIN) || IFN(k, F_COS))) same = 0;
            else if (!u) u = K0(k);
            else if (!expr_eq(u, K0(k))) same = 0;
        }
        if (same && u && nk2 >= 2) {
            E *T = mk_sym_name(depth & 1 ? "_w1" : "_w0");
            E *den = s_add2(K->c1, s_powi(T, 2));
            E *sx[2] = {s_fn1(F_SIN, u), s_fn1(F_COS, u)};
            E *sv[2] = {s_div(s_mul2(K->c2, T), den), s_div(s_sub(K->c1, s_powi(T, 2)), den)};
            E *h = rat_normal(subst_many(te, 2, sx, sv));
            if (freeof(h, x)) {
                slist vals = {0, 0, 0};
                solve1(numer_of(h), T, &vals, depth + 1);
                for (uint32_t i = 0; i < vals.n; i++)
                    solve1(s_sub(u, s_mul2(K->c2, s_fn1(F_ATAN, vals.v[i]))), x, out, depth + 1);
                /* u = pi (t -> inf) */
                E *atpi = subst(te, u, K->pi_);
                if (is_zero(rat_normal(atpi))) solve1(s_sub(u, K->pi_), x, out, depth + 1);
                return;
            }
        }
    }
    numeric_real_roots(g, x, out);
}

/*
 * solve1 -- solve f(x) = 0 for the symbol x; verified solutions are added
 * to out.  This is the recursive core used by every strategy above.
 *
 * Steps: split f into numerator / denominator (numden).  A numerator free
 * of x means no solution, or the identity (out gets x itself) if it is 0.
 * With floats in the numerator the polynomial is solved directly (or the
 * numeric scan is used), otherwise the numerator is factored over Q in x
 * (multiplicities are irrelevant here) and every factor goes through
 * solve_factor.
 *
 * Verification of each constant candidate s (symbolic candidates are kept
 * unchecked):
 *   - real mode: drop s if it is not real (is_real_val);
 *   - drop s if the denominator vanishes there (exact rat_normal or complex
 *     double value < 1e-12); errors during this check (e.g. division by
 *     zero) also drop s, except interrupts, which are re-thrown;
 *   - evaluate f(s) in complex doubles; reject if |f(s)| > 1e-6 * mag, where
 *     mag = 1 + |f| at a slightly perturbed point -- a cheap scale estimate
 *     so that large-valued functions are judged relative to their size.
 * depth > 10 returns nothing (guards against substitution loops).
 */
static void solve1(E *f, E *x, slist *out, int depth) {
    stack_check();
    if (depth > 10) {
        g_incomplete = 1;
        return;
    }
    E *num, *den;
    numden(f, &num, &den);
    if (freeof(num, x)) {
        if (is_zero(num)) sl_add(out, x); /* identity */
        return;
    }
    slist cand = {0, 0, 0};
    /* floats cannot be factored over Q: go straight to the numeric paths */
    if (has_float(num)) {
        UP p;
        if (poly_in_x(num, x, &p)) solve_poly(p, x, &cand);
        else numeric_real_roots(num, x, &cand);
    } else {
        E *content;
        E *fl = factor_list_in(num, x, &content);
        for (uint32_t i = 0; i < fl->n; i++) {
            E *g = K0(EK(fl)[i]);
            if (freeof(g, x)) continue;
            solve_factor(g, x, &cand, depth);
        }
    }
    /* verify: denominator nonzero, equation satisfied (numerically) */
    for (uint32_t i = 0; i < cand.n; i++) {
        E *s = cand.v[i];
        if (expr_eq(s, x)) {
            sl_add(out, s);
            continue;
        }
        if (!g_cplx && !is_real_val(s)) continue;
        int ok = 1;
        if (!freeof(den, x) && is_const_expr(s)) {
            tryf tf;
            try_enter(&tf);
            if (!setjmp(tf.jb)) {
                E *dv = subst(den, x, s);
                double re, im;
                if (eval_complex(dv, &re, &im) && fabs(re) + fabs(im) < 1e-12) ok = 0;
                if (is_zero(rat_normal(dv))) ok = 0;
                try_leave(&tf);
            } else {
                try_leave(&tf);
                if (K->err == CAS_ERR_INTERRUPTED) rethrow();
                ok = 0;
            }
        }
        if (ok && is_const_expr(s) && freeof(f, x) == 0) {
            /* numeric check of the original equation */
            double re, im, sr, si;
            if (eval_complex(s, &sr, &si)) {
                if (eval_complex_at(f, x->s, sr, si, &re, &im)) {
                    double mag = 1;
                    double fr2, fi2;
                    if (eval_complex_at(f, x->s, sr * (1 + 1e-6) + 1e-6, si, &fr2, &fi2))
                        mag = 1 + fabs(fr2) + fabs(fi2);
                    /* rounding scale: sum of |terms| (an expanded charpoly of
                     * degree 8 with roots near 1020 cancels to ~1e8) */
                    double sc = 0;
                    for (uint32_t j = 0; f->t == E_ADD && j < f->n; j++)
                        if (eval_complex_at(EK(f)[j], x->s, sr, si, &fr2, &fi2)) sc += hypot(fr2, fi2);
                    if (fabs(re) + fabs(im) > 1e-6 * mag && fabs(re) + fabs(im) > 1e-11 * sc) ok = 0;
                }
            }
        }
        if (ok) sl_add(out, s);
    }
}

/* order of the solution list: by numeric value when both are real
 * constants, otherwise the canonical expression order */
static int sol_cmp(E *a, E *b) {
    double x, y;
    if (to_double(a, &x) && to_double(b, &y)) return x < y ? -1 : x > y;
    return expr_cmp(a, b);
}

/* ------------------------------------------------------------ systems --- */
/* 1 if f is a polynomial in v (any coefficients) */
static int is_poly_eq(E *f, E *v) {
    long d;
    return poly_degree_in(f, v, &d);
}

static E *solve_system(E *eqs, E *vars, int depth);

/* Return a new list [eq, sol...] (sol is one solution: a list of var = val
 * equations); used when back-substituting an eliminated variable. */
static E *sys_append(E *sol, E *eq) {
    /* sol: list of equations; prepend eq */
    E **v = salloc(sizeof(E *) * (sol->n + 1));
    v[0] = eq;
    for (uint32_t i = 0; i < sol->n; i++) v[i + 1] = EK(sol)[i];
    return s_list(sol->n + 1, v);
}

/*
 * solve_system -- solve the system eqs = 0 (list of expressions) for vars.
 * Returns a list of solutions, each a list of equations var = value
 * (variables that end up free are simply absent), the empty list if the
 * system is inconsistent or no solution was found, or NULL if no method
 * applies (solve_expr then reports CAS_ERR_UNSUPPORTED).
 *
 * Methods, tried in order (each recursive call has one variable or one
 * equation less, depth <= 8):
 *   0. numerators only; 0 = 0 equations dropped; a nonzero constant
 *      equation makes the system inconsistent;
 *   1. linear system: linsolve (exact Gauss-Jordan, linalg.c);
 *   2. some equation linear in a variable v with a constant coefficient:
 *      solve it for v, substitute into the others, recurse, then compute
 *      v from each sub-solution;
 *   3. an equation in a single variable: solve it with solve1 and recurse
 *      on every value (at most 64 solutions are kept);
 *   4. (depth < 3) lex Groebner basis (gbasis.c): if it differs from the
 *      input, recurse on it (it contains an equation in the last variable
 *      only, so step 3 applies); [1] means inconsistent;
 *   5. eliminate a variable v from two equations polynomial in v by their
 *      resultant, recurse, then back-substitute: solve the first equation
 *      for v, substitute it into the sub-solution (other equations that
 *      still contain v are passed down with v left as a parameter) and keep
 *      values that satisfy all the equations (at most 128).
 */
static E *solve_system(E *eqs, E *vars, int depth) {
    stack_check();
    if (depth > 8) return NULL;
    uint32_t ne = eqs->n, nv = vars->n;
    /* drop trivial equations */
    E **ev = salloc(sizeof(E *) * (ne + 1));
    uint32_t k = 0;
    for (uint32_t i = 0; i < ne; i++) {
        E *f = numer_of(EK(eqs)[i]);
        if (is_zero(f)) continue;
        int has = 0;
        for (uint32_t j = 0; j < nv; j++)
            if (!freeof(f, EK(vars)[j])) has = 1;
        if (!has) return s_list(0, NULL); /* nonzero constant: inconsistent */
        ev[k++] = f;
    }
    ne = k;
    if (ne == 0) {
        E *empty = s_list(0, NULL);
        return s_list(1, &empty);
    }
    if (nv == 0) return s_list(0, NULL);
    /* linear? */
    E *lin = linsolve(s_list(ne, ev), vars);
    if (lin) {
        if (lin->n == 0) {
            /* linsolve returns [] for an inconsistent system (an empty
             * solution of a rank-0 system cannot occur: trivial equations
             * were dropped above).  Underdetermined systems come back with
             * the free variables on the right-hand sides. */
            int incons = 0;
            (void)incons;
            return s_list(0, NULL);
        }
        return s_list(1, &lin);
    }
    /* find equation linear in some variable with constant coefficient */
    for (uint32_t i = 0; i < ne; i++)
        for (uint32_t j = 0; j < nv; j++) {
            E *v = EK(vars)[j];
            long d;
            if (!poly_degree_in(ev[i], v, &d) || d != 1) continue;
            E *c1 = poly_coeff(ev[i], v, 1);
            int other = 0;
            for (uint32_t m = 0; m < nv; m++)
                if (m != j && !freeof(c1, EK(vars)[m])) other = 1;
            if (other || !is_const_expr(c1)) continue;
            /* ev[i] = c1 v + c0 with c1 a nonzero constant: v = -c0/c1 is
             * valid for every value of the other variables (no division by
             * an expression that might vanish) */
            E *val = rat_normal(s_neg(s_div(poly_coeff(ev[i], v, 0), c1)));
            E **rest = salloc(sizeof(E *) * ne);
            uint32_t nr = 0;
            for (uint32_t m = 0; m < ne; m++)
                if (m != i) rest[nr++] = subst(ev[m], v, val);
            E **rv = salloc(sizeof(E *) * nv);
            uint32_t nrv = 0;
            for (uint32_t m = 0; m < nv; m++)
                if (m != j) rv[nrv++] = EK(vars)[m];
            E *sub = solve_system(s_list(nr, rest), s_list(nrv, rv), depth + 1);
            if (!sub) return NULL;
            E **outs = salloc(sizeof(E *) * (sub->n + 1));
            for (uint32_t m = 0; m < sub->n; m++) {
                E *sol = EK(sub)[m];
                E **xs = salloc(sizeof(E *) * (sol->n + 1)), **vs = salloc(sizeof(E *) * (sol->n + 1));
                for (uint32_t q = 0; q < sol->n; q++) {
                    xs[q] = K0(EK(sol)[q]);
                    vs[q] = K1(EK(sol)[q]);
                }
                E *vv = rat_normal(subst_many(val, sol->n, xs, vs));
                outs[m] = sys_append(sol, s_fn2(F_EQ, v, vv));
            }
            return s_list(sub->n, outs);
        }
    /* univariate equation? */
    for (uint32_t i = 0; i < ne; i++) {
        uint32_t cnt = 0, which = 0;
        for (uint32_t j = 0; j < nv; j++)
            if (!freeof(ev[i], EK(vars)[j])) {
                cnt++;
                which = j;
            }
        if (cnt != 1) continue;
        E *v = EK(vars)[which];
        slist vals = {0, 0, 0};
        solve1(ev[i], v, &vals, 0);
        E **rv = salloc(sizeof(E *) * nv);
        uint32_t nrv = 0;
        for (uint32_t m = 0; m < nv; m++)
            if (m != which) rv[nrv++] = EK(vars)[m];
        E **outs = salloc(sizeof(E *) * 64);
        uint32_t no = 0;
        for (uint32_t s = 0; s < vals.n; s++) {
            E **rest = salloc(sizeof(E *) * ne);
            uint32_t nr = 0;
            for (uint32_t m = 0; m < ne; m++)
                if (m != i) rest[nr++] = subst(ev[m], v, vals.v[s]);
            E *sub = solve_system(s_list(nr, rest), s_list(nrv, rv), depth + 1);
            if (!sub) continue;
            for (uint32_t m = 0; m < sub->n && no < 64; m++) outs[no++] = sys_append(EK(sub)[m], s_fn2(F_EQ, v, vals.v[s]));
        }
        return s_list(no, outs);
    }
    /* polynomial system: lexicographic Groebner basis gives a triangular system.
     * groebner may throw (size limits) -- caught here so that the resultant
     * method can still be tried; interrupts are propagated.  If the basis
     * equals the input set nothing was gained and recursing would loop. */
    if (depth < 3) {
        tryf tf;
        try_enter(&tf);
        E *volatile gb = NULL;
        if (!setjmp(tf.jb)) {
            gb = groebner(s_list(ne, ev), vars, 0);
            try_leave(&tf);
        } else {
            try_leave(&tf);
            if (K->err == CAS_ERR_INTERRUPTED) rethrow();
            gb = NULL;
        }
        if (gb) {
            if (gb->n == 1 && is_one(EK(gb)[0])) return s_list(0, NULL);
            int same = gb->n == ne;
            for (uint32_t i = 0; i < gb->n && same; i++) {
                int found = 0;
                for (uint32_t j = 0; j < ne; j++)
                    if (expr_eq(EK(gb)[i], ev[j])) found = 1;
                if (!found) same = 0;
            }
            if (!same) {
                E *r = solve_system(gb, vars, depth + 1);
                if (r) return r;
            }
        }
    }
    /* eliminate with a resultant: res_v(A, B) vanishes exactly at the points
     * (of the remaining variables) where A and B have a common root in v
     * (or both leading coefficients vanish), so the reduced system
     * {res, others} has one variable less */
    for (uint32_t j = 0; j < nv; j++) {
        E *v = EK(vars)[j];
        int a = -1, b = -1;
        for (uint32_t i = 0; i < ne; i++)
            if (!freeof(ev[i], v) && is_poly_eq(ev[i], v)) {
                if (a < 0) a = (int)i;
                else if (b < 0) b = (int)i;
            }
        if (a < 0 || b < 0) continue;
        E *r = expand(resultant_expr(ev[a], ev[b], v));
        if (is_zero(r)) continue;
        E **rest = salloc(sizeof(E *) * ne);
        uint32_t nr = 0;
        rest[nr++] = r;
        for (uint32_t m = 0; m < ne; m++)
            if ((int)m != a && (int)m != b) rest[nr++] = ev[m];
        E **rv = salloc(sizeof(E *) * nv);
        uint32_t nrv = 0;
        for (uint32_t m = 0; m < nv; m++)
            if (m != j) rv[nrv++] = EK(vars)[m];
        E *sub = solve_system(s_list(nr, rest), s_list(nrv, rv), depth + 1);
        if (!sub) return NULL;
        E **outs = salloc(sizeof(E *) * 128);
        uint32_t no = 0;
        for (uint32_t m = 0; m < sub->n; m++) {
            E *sol = EK(sub)[m];
            E **xs = salloc(sizeof(E *) * (sol->n + 1)), **vs = salloc(sizeof(E *) * (sol->n + 1));
            for (uint32_t q = 0; q < sol->n; q++) {
                xs[q] = K0(EK(sol)[q]);
                vs[q] = K1(EK(sol)[q]);
            }
            /* solve the first equation for v */
            E *fa = subst_many(ev[a], sol->n, xs, vs), *fb = subst_many(ev[b], sol->n, xs, vs);
            int sa = a; /* index of the equation solved for v */
            if (is_zero(rat_normal(fa))) {
                /* first equation vanishes identically here: use the second */
                E *t = fa;
                fa = fb;
                fb = t;
                sa = b;
            }
            slist vals = {0, 0, 0};
            solve1(fa, v, &vals, 0);
            for (uint32_t s = 0; s < vals.n && no < 128; s++) {
                /* the sub-solution may still contain v (equations passed
                 * down with v as a parameter): substitute the value, then
                 * re-check every equation other than the one solved */
                E **es = salloc(sizeof(E *) * (sol->n + 1)), **xv = salloc(sizeof(E *) * (sol->n + 1));
                for (uint32_t q = 0; q < sol->n; q++) {
                    xv[q] = freeof(vs[q], v) ? vs[q] : rat_normal(subst(vs[q], v, vals.v[s]));
                    es[q] = s_fn2(F_EQ, xs[q], xv[q]);
                }
                xs[sol->n] = v;
                xv[sol->n] = vals.v[s];
                int zero = 1;
                for (uint32_t m = 0; m < ne && zero; m++) {
                    if ((int)m == sa) continue;
                    E *chk = rat_normal(subst_many(ev[m], sol->n + 1, xs, xv));
                    double re, im;
                    zero = is_zero(chk) || (is_const_expr(chk) && eval_complex(chk, &re, &im) && fabs(re) + fabs(im) < 1e-9);
                }
                if (zero) outs[no++] = sys_append(s_list(sol->n, es), s_fn2(F_EQ, v, vals.v[s]));
            }
        }
        return s_list(no, outs);
    }
    return NULL;
}

E *solve_ineq(E *rel, E *x);

/* ------------------------------------------------------------- public --- */
/*
 * solve_expr -- implementation of solve (cplx = 0) and csolve (cplx = 1).
 *   eqs   an equation / expression (= 0), an inequality, or a list of them
 *   vars  a symbol or a list of symbols
 * Single equation: returns the sorted list of solutions [s1, s2, ...] (empty
 * if none were found; [x] for an identity).  Single inequality: delegated
 * to solve_ineq.  Several equations or variables: returns a list of
 * solutions, each a list [v1 = a1, v2 = a2, ...] in the order of vars
 * (values simplified with simplify_full when small, non-real solutions
 * dropped in real mode), sorted by the numeric values.
 * Errors: CAS_ERR_DOMAIN for non-symbol variables, CAS_ERR_UNSUPPORTED when
 * no system method applies or a single equation has no solution found but
 * some strategy gave up ("cannot solve").  g_cplx is saved and restored on the normal
 * paths; the early empty-list return and the argument errors at the top
 * leave it set to cplx (harmless, each entry point sets it again).
 */
/* 1 unless the numeric value of the solution x = s contradicts the assume()
 * flags of x (solutions with parameters are kept) */
static int sol_assume_ok(E *x, E *s) {
    if (x->t != E_SYM) return 1;
    uint8_t f = K->syms[x->s].assume;
    double v;
    if (!f || !is_const_expr(s) || !to_double(s, &v)) return 1;
    if ((f & A_POS) && !(v > 0)) return 0;
    if ((f & A_NEG) && !(v < 0)) return 0;
    if ((f & A_NONNEG) && !(v >= 0)) return 0;
    if ((f & A_INT) && fabs(v - floor(v + 0.5)) > 1e-9 * (1 + fabs(v))) return 0;
    return 1;
}

/* 1 unless substituting x = s into a side of the original equation fails or
 * gives a non-finite value: f = x + 1/x - 1/x simplifies to x before solve1
 * sees the denominator, so poles are checked on the sides as written */
static int sol_domain_ok(E *side, E *x, E *s) {
    if (!freeof_sym(s, S_INF) || !freeof_sym(s, S_UNDEF)) return 0;
    if (freeof(side, x) || !is_const_expr(s)) return 1;
    int ok = 1;
    tryf tf;
    try_enter(&tf);
    if (!setjmp(tf.jb)) {
        E *v = subst(side, x, s);
        if (!freeof_sym(v, S_INF) || !freeof_sym(v, S_UNDEF)) ok = 0;
        else if (is_const_expr(v)) {
            double re, im;
            if (!eval_complex(v, &re, &im) || !isfinite(re) || !isfinite(im)) ok = 0;
        }
        try_leave(&tf);
    } else {
        try_leave(&tf);
        if (K->err == CAS_ERR_INTERRUPTED) rethrow();
        ok = 0;
    }
    return ok;
}

E *solve_expr(E *eqs, E *vars, int cplx) {
    int old = g_cplx;
    g_cplx = cplx;
    if (eqs->t == E_LIST && eqs->n == 0) return s_list(0, NULL);
    if (vars->t == E_LIST && vars->n == 0) cas_throw(CAS_ERR_DOMAIN, "solve: expected a variable");
    if (vars->t == E_LIST)
        for (uint32_t i = 0; i < vars->n; i++)
            if (EK(vars)[i]->t != E_SYM) cas_throw(CAS_ERR_DOMAIN, "solve: expected variables");
    E *res;
    if ((eqs->t == E_LIST && eqs->n > 1) || (vars->t == E_LIST && vars->n > 1)) {
        E *el = eqs->t == E_LIST ? eqs : s_list(1, &eqs);
        E *vl = vars->t == E_LIST ? vars : s_list(1, &vars);
        E **ev = salloc(sizeof(E *) * (el->n + 1));
        for (uint32_t i = 0; i < el->n; i++) {
            E *q = EK(el)[i];
            ev[i] = IFN(q, F_EQ) ? s_sub(K0(q), K1(q)) : q;
        }
        res = solve_system(s_list(el->n, ev), vl, 0);
        g_cplx = old;
        if (!res) cas_throw(CAS_ERR_UNSUPPORTED, "cannot solve this system");
        if (res->n == 0) {
            /* "no solution" for a nonlinear system with parameters is not
             * trustworthy (the elimination may divide by expressions that vanish
             * or drop branches): report failure instead of [] */
            E *sy[16];
            uint32_t ny = collect_syms(s_list(el->n, ev), sy, 16), npar = 0;
            for (uint32_t i = 0; i < ny; i++) {
                int isv = 0;
                for (uint32_t j = 0; j < vl->n; j++)
                    if (expr_eq(sy[i], EK(vl)[j])) isv = 1;
                if (!isv) npar++;
            }
            if (npar && !linsolve(s_list(el->n, ev), vl)) cas_throw(CAS_ERR_UNSUPPORTED, "cannot solve this system");
        }
        /* order equations as the variable list, simplify radical values, drop complex ones */
        E **sols = salloc(sizeof(E *) * (res->n + 1));
        uint32_t ns = 0;
        for (uint32_t i = 0; i < res->n; i++) {
            E *sol = EK(res)[i];
            E **eqs = salloc(sizeof(E *) * (sol->n + 1));
            uint32_t ne = 0;
            int real = 1;
            for (uint32_t v = 0; v < vl->n; v++)
                for (uint32_t j = 0; j < sol->n; j++) {
                    E *q = EK(sol)[j];
                    if (!IFN(q, F_EQ) || !expr_eq(K0(q), EK(vl)[v])) continue;
                    E *val = K1(q);
                    if (!has_float(val) && expr_size(val) < 200) val = simplify_full(val);
                    if (!cplx && !is_real_val(val)) real = 0;
                    if (!sol_assume_ok(K0(q), val)) real = 0;
                    eqs[ne++] = s_fn2(F_EQ, K0(q), val);
                }
            for (uint32_t j = 0; j < sol->n; j++) {
                int listed = 0;
                for (uint32_t v = 0; v < vl->n; v++)
                    if (IFN(EK(sol)[j], F_EQ) && expr_eq(K0(EK(sol)[j]), EK(vl)[v])) listed = 1;
                if (!listed) eqs[ne++] = EK(sol)[j];
            }
            if (real) sols[ns++] = s_list(ne, eqs);
        }
        /* deterministic order: by numeric values of the variables
         * (insertion sort, lexicographic on the first differing value) */
        for (uint32_t i = 1; i < ns; i++)
            for (uint32_t j = i; j > 0; j--) {
                E *A = sols[j - 1], *B = sols[j];
                int c = 0;
                for (uint32_t q = 0; q < A->n && q < B->n && !c; q++) {
                    double va, vb;
                    if (IFN(EK(A)[q], F_EQ) && IFN(EK(B)[q], F_EQ) && to_double(K1(EK(A)[q]), &va) &&
                        to_double(K1(EK(B)[q]), &vb))
                        c = va < vb - 1e-12 ? -1 : va > vb + 1e-12 ? 1 : 0;
                }
                if (c <= 0) break;
                sols[j - 1] = B;
                sols[j] = A;
            }
        return s_list(ns, sols);
    }
    E *eq = eqs->t == E_LIST ? EK(eqs)[0] : eqs;
    E *x = vars->t == E_LIST ? EK(vars)[0] : vars;
    if (eq->t == E_FN && eq->n == 2 && (eq->s == F_LT || eq->s == F_LE || eq->s == F_GT || eq->s == F_GE || eq->s == F_NE) &&
        x->t == E_SYM) {
        E *r = solve_ineq(eq, x);
        g_cplx = old;
        return r;
    }
    if (x->t != E_SYM) {
        g_cplx = old;
        cas_throw(CAS_ERR_DOMAIN, "solve: expected a variable");
    }
    E *f = IFN(eq, F_EQ) ? s_sub(K0(eq), K1(eq)) : eq;
    slist out = {0, 0, 0};
    int oldinc = g_incomplete;
    g_incomplete = 0;
    solve1(f, x, &out, 0);
    g_cplx = old;
    if (!out.n && g_incomplete) cas_throw(CAS_ERR_UNSUPPORTED, "cannot solve");
    g_incomplete = oldinc;
    E **v = salloc(sizeof(E *) * (out.n + 1));
    uint32_t nv = 0;
    for (uint32_t i = 0; i < out.n; i++) {
        E *s = out.v[i];
        if (!expr_eq(s, x)) {
            if (!sol_assume_ok(x, s)) continue;
            if (IFN(eq, F_EQ) ? !sol_domain_ok(K0(eq), x, s) || !sol_domain_ok(K1(eq), x, s)
                              : !sol_domain_ok(eq, x, s))
                continue;
        }
        v[nv++] = s;
    }
    esort(v, nv, sol_cmp);
    return s_list(nv, v);
}

/*
 * fsolve_system -- Newton's method for n nonlinear equations in n unknowns
 * (n <= 10): fsolve([eqs], [vars], [guess]).
 * The residuals F_i (converted to floats with evalf) and the symbolic
 * Jacobian J_ij = dF_i/dx_j are built once; each iteration substitutes the
 * current point, solves J d = -F by Gauss-Jordan elimination with partial
 * pivoting in doubles (augmented matrix jm[i][n] = -F_i) and sets x += d.
 * Stops when sum |F_i| < 1e-14, the step sum |d_i| < 1e-15 (1 + sum |x_i|),
 * or after 100 iterations; in the last case the result must satisfy
 * sum |F_i| < 1e-8.  The scratch used for the substitutions is released every
 * iteration (smark / srelease) since only doubles are kept.
 * Returns [x1 = v1, ...] as floats.  Throws CAS_ERR_DOMAIN for bad
 * arguments, non-numeric values, a (numerically) singular Jacobian, or "no
 * convergence".
 */
static E *fsolve_system(E *eqs, E *vars, E *guess) {
    uint32_t n = vars->n;
    if (eqs->n != n || !guess || guess->t != E_LIST || guess->n != n || n > 10)
        cas_throw(CAS_ERR_DOMAIN, "fsolve: need n equations, n variables and n starting values");
    E **F = salloc(sizeof(E *) * n), **J = salloc(sizeof(E *) * n * n);
    for (uint32_t i = 0; i < n; i++) {
        E *q = EK(eqs)[i];
        F[i] = evalf(IFN(q, F_EQ) ? s_sub(K0(q), K1(q)) : q);
        for (uint32_t j = 0; j < n; j++) J[i * n + j] = diff(F[i], EK(vars)[j]);
    }
    double xv[10], fv[10], jm[10][11];
    int conv = 0;
    for (uint32_t i = 0; i < n; i++)
        if (!to_double(EK(guess)[i], &xv[i])) cas_throw(CAS_ERR_DOMAIN, "fsolve: numeric starting values expected");
    for (int it = 0; it < 100 && !conv; it++) {
        poll_intr();
        char *mark = smark();
        E *vals[10];
        for (uint32_t i = 0; i < n; i++) vals[i] = mk_flt(xv[i]);
        double norm = 0;
        for (uint32_t i = 0; i < n; i++) {
            if (!to_double(subst_many(F[i], n, EK(vars), vals), &fv[i])) cas_throw(CAS_ERR_DOMAIN, "fsolve: not numeric");
            norm += fabs(fv[i]);
            for (uint32_t j = 0; j < n; j++)
                if (!to_double(subst_many(J[i * n + j], n, EK(vars), vals), &jm[i][j]))
                    cas_throw(CAS_ERR_DOMAIN, "fsolve: Jacobian not numeric");
            jm[i][n] = -fv[i];
        }
        srelease(mark);
        if (norm < 1e-14) {
            conv = 1;
            break;
        }
        /* Gaussian elimination with partial pivoting */
        for (uint32_t c = 0; c < n; c++) {
            uint32_t p = c;
            for (uint32_t r = c + 1; r < n; r++)
                if (fabs(jm[r][c]) > fabs(jm[p][c])) p = r;
            if (fabs(jm[p][c]) < 1e-300) cas_throw(CAS_ERR_DOMAIN, "fsolve: singular Jacobian");
            for (uint32_t j = 0; j <= n; j++) {
                double t = jm[c][j]; jm[c][j] = jm[p][j]; jm[p][j] = t;
            }
            for (uint32_t r = 0; r < n; r++) {
                if (r == c) continue;
                double f = jm[r][c] / jm[c][c];
                for (uint32_t j = c; j <= n; j++) jm[r][j] -= f * jm[c][j];
            }
        }
        double step = 0, size = 0;
        for (uint32_t i = 0; i < n; i++) {
            double d = jm[i][n] / jm[i][i];
            xv[i] += d;
            step += fabs(d);
            size += fabs(xv[i]);
        }
        conv = step < 1e-15 * (1 + size);
    }
    if (!conv) {
        /* iteration limit: accept the last iterate only if it solves the
         * system (as in the scalar case) */
        E *vals[10];
        for (uint32_t i = 0; i < n; i++) vals[i] = mk_flt(xv[i]);
        double norm = 0;
        for (uint32_t i = 0; i < n; i++)
            norm += to_double(subst_many(F[i], n, EK(vars), vals), &fv[i]) ? fabs(fv[i]) : HUGE_VAL;
        if (!(norm < 1e-8)) cas_throw(CAS_ERR_DOMAIN, "fsolve: no convergence");
    }
    E **out = salloc(sizeof(E *) * n);
    for (uint32_t i = 0; i < n; i++) out[i] = s_fn2(F_EQ, EK(vars)[i], mk_flt(xv[i]));
    return s_list(n, out);
}

/*
 * fsolve_expr -- numeric solving (fsolve / nsolve).
 *   lists of equations and variables  -> fsolve_system (Newton, needs guess)
 *   one equation with a guess          -> scalar Newton iteration from the
 *       guess (up to 100 steps; stops early on |f| < 1e-15, f' = 0 or a
 *       relative step < 1e-15); the result must satisfy |f| < 1e-8, else
 *       CAS_ERR_DOMAIN "no convergence"
 *   one equation without a guess       -> all real roots: Aberth roots for
 *       a polynomial (real ones only), otherwise the sign-change scan on
 *       [-100, 100]; returned sorted as a list of floats.
 */
E *fsolve_expr(E *eq, E *x, E *guess) {
    if (eq->t == E_LIST && x->t == E_LIST) return fsolve_system(eq, x, guess);
    E *f = IFN(eq, F_EQ) ? s_sub(K0(eq), K1(eq)) : eq;
    if (x->t != E_SYM) cas_throw(CAS_ERR_DOMAIN, "fsolve: expected a variable");
    f = evalf(f);
    if (guess) {
        /* Newton / secant from the guess */
        double x0;
        if (!to_double(guess, &x0)) cas_throw(CAS_ERR_DOMAIN, "fsolve: guess must be numeric");
        E *df = diff(f, x);
        for (int it = 0; it < 100; it++) {
            poll_intr();
            double fv, dv;
            if (!eval_double_at(f, x->s, x0, &fv)) break;
            if (fabs(fv) < 1e-15) break;
            if (!eval_double_at(df, x->s, x0, &dv) || dv == 0) break;
            double x1 = x0 - fv / dv;
            if (fabs(x1 - x0) < 1e-15 * (1 + fabs(x0))) {
                x0 = x1;
                break;
            }
            x0 = x1;
        }
        double fv;
        if (eval_double_at(f, x->s, x0, &fv) && fabs(fv) < 1e-8) return mk_flt(x0);
        cas_throw(CAS_ERR_DOMAIN, "fsolve: no convergence");
    }
    UP p;
    slist out = {0, 0, 0};
    if (poly_in_x(f, x, &p)) {
        int old = g_cplx;
        g_cplx = 0;
        numeric_poly_roots(p, x, &out);
        g_cplx = old;
    } else
        numeric_real_roots(f, x, &out);
    E **v = salloc(sizeof(E *) * (out.n + 1));
    for (uint32_t i = 0; i < out.n; i++) v[i] = evalf(out.v[i]);
    esort(v, out.n, sol_cmp);
    return s_list(out.n, v);
}

/* Is the polynomial g (in x) zero at x = r?  Exact test first (rational normal
 * form); otherwise numerically, |g(r)| <= tol * sum |terms of g at r|, with tol 1e-7
 * when g or r contains floats (roots from Aberth iteration) and 1e-10 for exact
 * radicals that do not simplify to 0. */
static int poly_zero_at(E *g, E *x, E *r) {
    E *v = rat_normal(subst(g, x, r));
    if (is_zero(v)) return 1;
    if (!is_const_expr(v)) return 0;
    E *ge = expand(g);
    double re, im, sc = 0;
    if (!eval_complex(subst(g, x, r), &re, &im)) return 0;
    for (uint32_t i = 0; i < (ge->t == E_ADD ? ge->n : 1); i++) {
        double tr, ti;
        if (eval_complex(subst(ge->t == E_ADD ? EK(ge)[i] : ge, x, r), &tr, &ti)) sc += hypot(tr, ti);
    }
    double tol = has_float(g) || has_float(r) ? 1e-7 : 1e-10;
    return hypot(re, im) <= tol * (sc + 1e-300);
}

/*
 * roots_expr -- roots(p, x): the numerator of p is factored over Q in x and
 * every factor solved in complex mode with solve_factor; each root is
 * paired with its multiplicity: [[root, mult], ...] (at most 256 entries).
 * Factoring over Q does not separate repeated roots with non-rational
 * (e.g. complex) coefficients, and solve_factor returns each root once, so
 * the multiplicity within a factor g is found by differentiating: the
 * number of derivatives g, g', g'', ... that vanish at the root.  Numeric
 * roots (Aberth) of a repeated root come back as a cluster of nearly equal
 * floats; a cluster (|a - b| <= 1e-6 (1 + |a|)) is merged into its mean
 * (imaginary part dropped if negligible) and counts at least its size.
 * The result is multiplied by the factor's own multiplicity.  Unlike
 * solve, the candidates are not verified, which is fine for polynomial
 * factors (the intended use; ode.c uses it for characteristic polynomials).
 */
E *roots_expr(E *p, E *x) {
    /* [[root, multiplicity], ...] */
    E *content;
    E *fl = factor_list_in(numer_of(p), x, &content);
    E **out = salloc(sizeof(E *) * 256);
    uint32_t no = 0;
    int old = g_cplx;
    g_cplx = 1;
    for (uint32_t i = 0; i < fl->n; i++) {
        E *g = K0(EK(fl)[i]);
        if (freeof(g, x)) continue;
        long fm, deg;
        if (!num_is_small_int(K1(EK(fl)[i]), &fm)) fm = 1;
        if (!poly_degree_in(g, x, &deg)) deg = 1;
        slist s = {0, 0, 0};
        solve_factor(g, x, &s, 0);
        char *used = salloc(s.n + 1);
        memset(used, 0, s.n + 1);
        for (uint32_t k = 0; k < s.n && no < 256; k++) {
            if (used[k]) continue;
            E *r = s.v[k];
            long csize = 1;
            double ar, ai, br, bi;
            if (has_float(r) && eval_complex(r, &ar, &ai)) {
                /* cluster of numeric approximations of one repeated root */
                double sr = ar, si = ai;
                for (uint32_t j = k + 1; j < s.n; j++)
                    if (!used[j] && eval_complex(s.v[j], &br, &bi) &&
                        hypot(br - ar, bi - ai) <= 1e-6 * (1 + hypot(ar, ai))) {
                        used[j] = 1;
                        sr += br;
                        si += bi;
                        csize++;
                    }
                if (csize > 1) {
                    sr /= csize;
                    si /= csize;
                    if (fabs(si) <= 1e-7 * (1 + fabs(sr))) si = 0;
                    r = si ? s_add2(mk_flt(sr), s_mul2(mk_flt(si), K->i_)) : mk_flt(sr);
                }
            }
            long m = 1;
            E *dg = g;
            while (m < deg) {
                dg = diff(dg, x);
                if (!poly_zero_at(dg, x, r)) break;
                m++;
            }
            if (csize > m) m = csize;
            E *pr[2] = {r, mk_int(m * fm)};
            out[no++] = s_list(2, pr);
        }
    }
    g_cplx = old;
    /* the same root from different factors over Q (e.g. (x^2+1)^2 (x-i) after
     * factoring (x-i)^3 (x+i)^2): add the multiplicities */
    uint32_t nm = 0;
    for (uint32_t k = 0; k < no; k++) {
        E *r = K0(out[k]);
        uint32_t j;
        for (j = 0; j < nm; j++) {
            E *d = rat_normal(s_sub(K0(out[j]), r));
            double re, im;
            if (is_zero(d) || (has_float(d) && eval_complex(d, &re, &im) && hypot(re, im) <= 1e-9)) break;
        }
        if (j < nm) {
            E *pr[2] = {K0(out[j]), num_add(K1(out[j]), K1(out[k]))};
            out[j] = s_list(2, pr);
        } else
            out[nm++] = out[k];
    }
    return s_list(nm, out);
}

/* ---------------------------------------------------------- inequalities --- */
/*
 * Inequalities are solved with a sign chart (cf. any precalculus text):
 * the real line is cut at all critical points -- zeros of the numerator,
 * zeros of the denominator, and domain boundaries of ln and even roots --
 * and the sign of f = lhs - rhs is constant on each open interval between
 * them (assuming f is continuous there), so one sample per interval plus an
 * exact check at each point decides the inequality everywhere.
 */

/* Add to pts the zeros of the arguments of ln(...) and of radicands of
 * even roots u^(p/q), q even, found anywhere in e (boundaries of the real
 * domain, where f can change from defined to undefined). */
static void crit_points(E *e, E *x, slist *pts) {
    stack_check();
    /* zeros of arguments of ln/sqrt (domain boundaries) */
    if (freeof(e, x)) return;
    if (IFN(e, F_LN) || (e->t == E_POW && K1(e)->t == E_RAT && int_is_even(K1(K1(e))))) {
        slist z = {0, 0, 0};
        solve1(K0(e), x, &z, 0);
        for (uint32_t i = 0; i < z.n; i++) sl_add(pts, z.v[i]);
    }
    if (e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR) return;
    for (uint32_t i = 0; i < e->n; i++) crit_points(EK(e)[i], x, pts);
}

/* 1 if e has a circular trig function of an argument containing x */
static int trig_in(E *e, E *x) {
    stack_check();
    if (freeof(e, x)) return 0;
    if (has_trig(e) && e->t == E_FN && e->n == 1 && !has_trig(K0(e))) return 1;
    for (uint32_t i = 0; i < e->n; i++)
        if (trig_in(EK(e)[i], x)) return 1;
    return 0;
}

/* sign of f at x=v: 1, -1, 0, or 2 when undefined/non-real */
static int sign_at(E *f, E *x, double v) {
    double r;
    if (!eval_double_at(f, x->s, v, &r)) return 2;
    if (r != r) return 2;
    if (fabs(r) < 1e-12) return 0;
    return r > 0 ? 1 : -1;
}

/* does a value of sign sg (as returned by sign_at) satisfy "f op 0"?
 * Undefined values (2) never do. */
static int ineq_ok(int op, int sg) {
    if (sg == 2) return 0;
    switch (op) {
    case F_LT: return sg < 0;
    case F_LE: return sg <= 0;
    case F_GT: return sg > 0;
    case F_GE: return sg >= 0;
    case F_NE: return sg != 0;
    }
    return 0;
}

/*
 * solve_ineq -- solve the inequality rel (lhs op rhs with op one of < > <=
 * >= !=) for x over the reals by a sign chart.
 * Critical points must be numeric (CAS_ERR_UNSUPPORTED "inequality with
 * symbolic critical points" otherwise); they are deduplicated and sorted.
 * Intervals I_0 = (-inf, p_0), ..., I_np = (p_{np-1}, inf) are tested at
 * one sample point (double evaluation), the points p_k exactly where
 * possible (rat_normal of f(p_k), errors meaning "undefined").  The
 * sequence I_0 P_0 I_1 P_1 ... I_np is then scanned for maximal runs of
 * satisfied elements, each printed as one condition: x = p, x > p,
 * x <= p, or p < x and x <= q (relations built raw and flagged F_SIMP so
 * the simplifier keeps them in this form).  Returns the list of conditions
 * ([] if none, [true] for everything, [true] / [false] for identities).
 * Sets g_cplx = 0 (real analysis); solve_expr restores it.
 */
E *solve_ineq(E *rel, E *x) {
    int op = rel->s, oldinc = g_incomplete;
    E *f = s_sub(K0(rel), K1(rel));
    E *num, *den;
    numden(f, &num, &den);
    g_cplx = 0;
    g_incomplete = 0;
    slist pts = {0, 0, 0};
    solve1(num, x, &pts, 0);
    if (!freeof(den, x)) solve1(den, x, &pts, 0);
    crit_points(f, x, &pts);
    /* the chart needs every critical point: not just the principal zeros of
     * a periodic f (sin x < 1), nor a numeric scan */
    if (g_incomplete || (pts.n && trig_in(f, x))) cas_throw(CAS_ERR_UNSUPPORTED, "cannot solve");
    g_incomplete = oldinc;
    /* collect numeric critical points; the pseudo-solution x (identity) means
     * f vanishes identically, so the answer is true or false everywhere */
    uint32_t np = 0;
    double *pv = salloc(sizeof(double) * (pts.n + 1));
    E **pe = salloc(sizeof(E *) * (pts.n + 1));
    for (uint32_t i = 0; i < pts.n; i++) {
        double v;
        if (expr_eq(pts.v[i], x)) return s_list(1, (E *[]){mk_sym(ineq_ok(op, 0) ? S_TRUE : S_FALSE)});
        if (!to_double(pts.v[i], &v)) cas_throw(CAS_ERR_UNSUPPORTED, "inequality with symbolic critical points");
        int dup = 0;
        for (uint32_t j = 0; j < np; j++)
            if (fabs(pv[j] - v) < 1e-12 * (1 + fabs(v))) dup = 1;
        if (dup) continue;
        pv[np] = v;
        pe[np] = pts.v[i];
        np++;
    }
    /* sort */
    for (uint32_t i = 1; i < np; i++)
        for (uint32_t j = i; j > 0 && pv[j - 1] > pv[j]; j--) {
            double t = pv[j]; pv[j] = pv[j - 1]; pv[j - 1] = t;
            E *te = pe[j]; pe[j] = pe[j - 1]; pe[j - 1] = te;
        }
    /* intervals: I_0 = (-inf, p0), I_k = (p_{k-1}, p_k), I_np = (p_{np-1}, inf); points P_k */
    int *iok = salloc(sizeof(int) * (np + 2)), *pok = salloc(sizeof(int) * (np + 1));
    for (uint32_t k = 0; k <= np; k++) {
        double s;
        if (np == 0) s = 0.5;
        else if (k == 0) s = pv[0] - 1 - fabs(pv[0]);
        else if (k == np) s = pv[np - 1] + 1 + fabs(pv[np - 1]);
        else s = (pv[k - 1] + pv[k]) / 2;
        iok[k] = ineq_ok(op, sign_at(f, x, s));
    }
    for (uint32_t k = 0; k < np; k++) {
        /* at the point itself: exact evaluation when possible */
        int sg = 2;
        tryf tf;
        try_enter(&tf);
        if (!setjmp(tf.jb)) {
            E *v = subst(f, x, pe[k]);
            double r;
            if (is_zero(rat_normal(v))) sg = 0;
            else if (to_double(v, &r)) sg = fabs(r) < 1e-12 ? 0 : (r > 0 ? 1 : -1);
            try_leave(&tf);
        } else {
            try_leave(&tf);
            if (K->err == CAS_ERR_INTERRUPTED) rethrow();
            sg = 2;
        }
        pok[k] = ineq_ok(op, sg);
    }
    /* assemble maximal runs */
    E **out = salloc(sizeof(E *) * (2 * np + 4));
    uint32_t no = 0;
    uint32_t k = 0;
    /* walk: element sequence I0 P0 I1 P1 ... I_np  (index 2k for I_k, 2k+1 for P_k) */
    uint32_t total = 2 * np + 1;
    uint32_t i = 0;
    while (i < total) {
        int ok = (i & 1) ? pok[i / 2] : iok[i / 2];
        if (!ok) {
            i++;
            continue;
        }
        uint32_t j = i;
        while (j + 1 < total && ((j + 1) & 1 ? pok[(j + 1) / 2] : iok[(j + 1) / 2])) j++;
        /* run from element i to j */
        E *cond;
        if (i == j && (i & 1)) cond = s_fn2(F_EQ, x, pe[i / 2]);
        else {
            E *lo = NULL, *hi = NULL;
            int lo_incl = 0, hi_incl = 0;
            if (i & 1) {
                lo = pe[i / 2];
                lo_incl = 1;
            } else if (i / 2 > 0)
                lo = pe[i / 2 - 1];
            if (j & 1) {
                hi = pe[j / 2];
                hi_incl = 1;
            } else if (j / 2 < np)
                hi = pe[j / 2];
            E *c1 = lo ? mk_fn2(lo_incl ? F_GE : F_GT, x, lo) : NULL;
            E *c2 = hi ? mk_fn2(hi_incl ? F_LE : F_LT, x, hi) : NULL;
            if (c1) c1->f = F_SIMP;
            if (c2) c2->f = F_SIMP;
            if (c1 && c2) {
                E *lo2 = mk_fn2(lo_incl ? F_LE : F_LT, lo, x);
                lo2->f = F_SIMP;
                cond = mk_fn2(F_AND, lo2, c2);
                cond->f = F_SIMP;
            } else if (c1)
                cond = c1;
            else if (c2)
                cond = c2;
            else
                cond = mk_sym(S_TRUE);
        }
        out[no++] = cond;
        i = j + 1;
        (void)k;
    }
    return s_list(no, out);
}
