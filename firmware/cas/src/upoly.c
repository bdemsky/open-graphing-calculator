/*
 * upoly.c -- univariate polynomials in x whose coefficients are expressions
 * free of x (field Q(parameters)); coefficients are kept in rational normal
 * form.  Used for partial fractions, Hermite reduction, Rothstein-Trager,
 * resultants.  Also: numeric polynomial roots (Aberth iteration), exact().
 *
 * Contents:
 *  - the UP type (see upoly.h): dense, ascending coefficient arrays in
 *    scratch.  Every coefficient produced here passes through cn(), i.e.
 *    rat_normal (poly.c), so that zero coefficients are recognised
 *    syntactically and the Euclidean algorithm over the coefficient field
 *    K = Q(parameters) is sound.  This is simple but can be slow: there is
 *    no subresultant / primitive PRS here, so coefficient growth is
 *    controlled only by rat_normal's gcd cancellation.  Callers in integ.c
 *    (Hermite reduction, Rothstein-Trager, the rational-function
 *    integrator), solve.c, ode.c and sum.c use these routines on small
 *    degrees;
 *  - partfrac(): partial fraction decomposition over Q (the factorisation
 *    of the denominator comes from factor.c), via Bezout identities and
 *    f-adic expansion (von zur Gathen & Gerhard, "Modern Computer
 *    Algebra", sec. 5.11; Geddes/Czapor/Labahn, "Algorithms for Computer
 *    Algebra", ch. 11);
 *  - resultant_expr(): resultant as the determinant of the Sylvester matrix
 *    (computed by linalg.c's mat_det);
 *  - poly_roots_c() / proot_expr(): all complex roots of a numeric
 *    polynomial by the Aberth-Ehrlich simultaneous iteration (Aberth 1973;
 *    Bini 1996, "Numerical computation of polynomial zeros by means of
 *    Aberth's method"), used by proot and, through it, by solve / roots for
 *    degrees that have no closed form;
 *  - exact_expr(): the exact() command, turning floats into nearby
 *    rationals with continued fractions (Knuth TAOCP vol. 2, 4.5.3).
 */
#include "poly.h"
#include "upoly.h"
#include <math.h>
#include <string.h>

/* Canonical coefficient: numbers are already canonical; anything else is put
 * into rational normal form (cancelled numerator / denominator), so that a
 * coefficient that is mathematically zero becomes the literal 0. */
static E *cn(E *e) { return is_exactnum(e) ? e : rat_normal(e); }

/* Polynomial with room for degree d (at least one slot), all coefficients 0
 * and r.d = d (so it is not normalised unless c[d] gets set). */
UP up_alloc(long d) {
    UP r;
    r.d = d;
    r.c = salloc(sizeof(E *) * (size_t)(d < 0 ? 1 : d + 1));
    for (long i = 0; i <= d; i++) r.c[i] = K->c0;
    return r;
}

/* Lower the degree past zero leading coefficients (in place on the copy of
 * the struct; the coefficient array is shared, not copied). */
UP up_norm(UP a) {
    while (a.d >= 0 && is_zero(a.c[a.d])) a.d--;
    return a;
}

/* Convert e to a UP in x.  Returns 0 (and leaves *out untouched) when e is
 * not a polynomial in x, i.e. when x occurs in a coefficient (e.g. inside
 * sin(x), or in a denominator).  poly_coeff_list gives the coefficients in
 * descending order; they are reversed and normalised with cn. */
int up_from(E *e, E *x, UP *out) {
    long d;
    if (!poly_degree_in(e, x, &d)) return 0;
    E *l = poly_coeff_list(e, x, &d);
    UP r = up_alloc(d);
    for (long i = 0; i <= d; i++) r.c[i] = cn(EK(l)[d - i]);
    *out = up_norm(r);
    return 1;
}

/* Back to an expression: the simplified sum of c_i * x^i over the nonzero
 * coefficients (0 for the zero polynomial). */
E *up_to(UP a, E *x) {
    if (a.d < 0) return K->c0;
    E **t = salloc(sizeof(E *) * (size_t)(a.d + 1));
    uint32_t n = 0;
    for (long i = 0; i <= a.d; i++)
        if (!is_zero(a.c[i])) t[n++] = s_mul2(a.c[i], s_pow(x, mk_int(i)));
    return s_add(n, t);
}

/* Constant polynomial c (degree 0), or the zero polynomial when c is 0. */
UP up_const(E *c) {
    UP r = up_alloc(0);
    r.c[0] = c;
    return up_norm(r);
}

/* a + b (sub = 0) or a - b (sub != 0), coefficientwise; the result is
 * normalised, so cancellation of leading terms lowers the degree. */
UP up_add(UP a, UP b, int sub) {
    long d = a.d > b.d ? a.d : b.d;
    UP r = up_alloc(d);
    for (long i = 0; i <= d; i++) {
        E *x = i <= a.d ? a.c[i] : K->c0, *y = i <= b.d ? b.c[i] : K->c0;
        r.c[i] = cn(sub ? s_sub(x, y) : s_add2(x, y));
    }
    return up_norm(r);
}

/* a * b by the schoolbook method.  Products are accumulated unnormalised and
 * each coefficient is run through cn once at the end, which is cheaper than
 * normalising after every addition.  O(deg a * deg b) coefficient
 * operations. */
UP up_mul(UP a, UP b) {
    if (a.d < 0 || b.d < 0) return up_alloc(-1);
    UP r = up_alloc(a.d + b.d);
    for (long i = 0; i <= a.d; i++) {
        if (is_zero(a.c[i])) continue;
        for (long j = 0; j <= b.d; j++) r.c[i + j] = s_add2(r.c[i + j], s_mul2(a.c[i], b.c[j]));
    }
    for (long i = 0; i <= r.d; i++) r.c[i] = cn(r.c[i]);
    return up_norm(r);
}

/* c * a for an expression c free of x (c = 0 gives the zero polynomial). */
UP up_scale(UP a, E *c) {
    UP r = up_alloc(a.d);
    for (long i = 0; i <= a.d; i++) r.c[i] = cn(s_mul2(a.c[i], c));
    return up_norm(r);
}

/* Euclidean division in K[x]: a = q*b + r with deg r < deg b.  q and/or r may
 * be NULL.  Throws CAS_ERR_DOMAIN "division by zero polynomial" if b is
 * zero.  Classical long division: for each degree i of the running
 * remainder from the top, the quotient term t = rem_i / lc(b) is recorded
 * and t*b*x^(i - deg b) is subtracted.  The leading coefficient of the
 * remainder is set to 0 explicitly instead of trusting the subtraction (it is
 * exactly zero in K anyway).  a itself is not modified (its coefficients
 * are copied into a fresh remainder array).  Polls for interrupts once per
 * quotient term.  O((deg a - deg b + 1) * deg b) coefficient operations,
 * each a rat_normal call. */
void up_divrem(UP a, UP b, UP *q, UP *r) {
    b = up_norm(b);
    if (b.d < 0) cas_throw(CAS_ERR_DOMAIN, "division by zero polynomial");
    UP rr = up_alloc(a.d);
    for (long i = 0; i <= a.d; i++) rr.c[i] = a.c[i];
    long dq = a.d - b.d;
    UP qq = up_alloc(dq < 0 ? -1 : dq);
    E *ilc = cn(s_inv(b.c[b.d]));
    for (long i = a.d; i >= b.d; i--) {
        poll_intr();
        if (is_zero(rr.c[i])) continue;
        E *t = cn(s_mul2(rr.c[i], ilc));
        qq.c[i - b.d] = t;
        for (long j = 0; j <= b.d; j++) rr.c[i - b.d + j] = cn(s_sub(rr.c[i - b.d + j], s_mul2(t, b.c[j])));
        rr.c[i] = K->c0;
    }
    if (q) *q = up_norm(qq);
    if (r) *r = up_norm(rr);
}

/* a / lc(a), the monic associate used as the canonical gcd; the zero
 * polynomial is returned unchanged. */
UP up_monic(UP a) {
    if (a.d < 0) return a;
    return up_scale(a, s_inv(a.c[a.d]));
}

/* gcd(a, b) in K[x], made monic (the zero polynomial if both are zero).
 * Plain Euclidean remainder sequence over the field K; correct because cn
 * keeps every coefficient in canonical form, but intermediate coefficients
 * can grow (no PRS). */
UP up_gcd(UP a, UP b) {
    a = up_norm(a);
    b = up_norm(b);
    while (b.d >= 0) {
        UP r;
        up_divrem(a, b, NULL, &r);
        a = b;
        b = r;
    }
    return up_monic(a);
}

/* s*a + t*b = g (monic)
 *
 * Extended Euclidean algorithm in K[x] (von zur Gathen & Gerhard, ch. 3):
 * alongside the remainders r_i it carries cofactors with
 * r_i = s_i * a + t_i * b.  At the end g = r_last / lc(r_last) and s, t are
 * scaled by the same factor.  If a and b are both zero, g is the zero
 * polynomial with s = 1, t = 0.  s and t must be non-NULL.  The cofactors
 * satisfy deg s < deg b and deg t < deg a in the generic case.  Used for
 * partial fractions (Bezout identities between coprime factors) and in
 * integ.c (Hermite reduction / solving s a + t b = c). */
UP up_egcd(UP a, UP b, UP *s, UP *t) {
    UP r0 = up_norm(a), r1 = up_norm(b);
    UP s0 = up_const(K->c1), s1 = up_alloc(-1), t0 = up_alloc(-1), t1 = up_const(K->c1);
    while (r1.d >= 0) {
        UP q, r;
        up_divrem(r0, r1, &q, &r);
        r0 = r1;
        r1 = r;
        UP ns = up_add(s0, up_mul(q, s1), 1);
        s0 = s1;
        s1 = ns;
        UP nt = up_add(t0, up_mul(q, t1), 1);
        t0 = t1;
        t1 = nt;
    }
    if (r0.d < 0) {
        *s = s0;
        *t = t0;
        return r0;
    }
    E *il = s_inv(r0.c[r0.d]);
    *s = up_scale(s0, il);
    *t = up_scale(t0, il);
    return up_scale(r0, il);
}

/* Formal derivative: sum i * c_i x^(i-1); constants give the zero
 * polynomial. */
UP up_deriv(UP a) {
    if (a.d <= 0) return up_alloc(-1);
    UP r = up_alloc(a.d - 1);
    for (long i = 1; i <= a.d; i++) r.c[i - 1] = cn(s_mul2(mk_int(i), a.c[i]));
    return up_norm(r);
}

/* a^k for k >= 0 by k successive multiplications (k <= 0 gives 1).  Linear
 * rather than binary powering; the exponents used (factor multiplicities)
 * are small. */
UP up_pow(UP a, long k) {
    UP r = up_const(K->c1);
    for (long i = 0; i < k; i++) r = up_mul(r, a);
    return r;
}

/* ------------------------------------------------------- partfrac --- */
/* partfrac(e, x) / apart: partial fraction decomposition of a rational
 * function of x over Q(parameters), with the denominator factored over Q.
 * Lists are mapped elementwise.  Returns e unchanged when it is not a
 * rational function in x (numerator or denominator not polynomial, or a
 * factor that up_from cannot handle) or when a Bezout identity unexpectedly
 * fails; a denominator free of x just gives expand(e).
 *
 * Algorithm, for e = N / D:
 *  1. N = Q D + R (polynomial part Q, deg R < deg D);
 *  2. D = cst * prod f_i^m_i (factor_list_in; x-free factors and the content
 *     are folded into cst, and R is divided by cst);
 *  3. for each i, with P_i = f_i^m_i and Q_i = prod_{j != i} P_j coprime,
 *     find s, t with s Q_i + t P_i = 1; then R / (P_i Q_i) contributes
 *     A_i / P_i where A_i = (R s) mod P_i (the standard Chinese-remainder
 *     form of partial fractions);
 *  4. expand A_i in powers of f_i (f-adic expansion by repeated division,
 *     A = r_m + f (r_(m-1) + f (...))), giving terms r_j / f_i^j with
 *     deg r_j < deg f_i for j = m_i .. 1.
 * The result is the simplified sum of Q and all terms.  A single simple
 * factor (nf = 1, m = 1) short-cuts to Q + R/f.
 *
 * Known issue: the `terms` array holds fl->n * 64 + 2 entries but step 4
 * adds up to m_i entries per factor, so a factor with multiplicity above
 * about 64 overflows it (partfrac(x^70/(x+1)^70, x) crashes the REPL).
 * Memory: all scratch; polls for interrupts once per factor. */
E *partfrac(E *e, E *x) {
    stack_check();
    if (e->t == E_LIST) {
        E **a = salloc(sizeof(E *) * (e->n + 1));
        for (uint32_t i = 0; i < e->n; i++) a[i] = partfrac(EK(e)[i], x);
        return s_list(e->n, a);
    }
    E *num, *den;
    numden(e, &num, &den);
    UP N, D;
    if (!up_from(num, x, &N) || !up_from(den, x, &D)) return e;
    if (D.d <= 0) return expand(e);
    UP Q, R;
    up_divrem(N, D, &Q, &R);
    E *poly = up_to(Q, x);
    /* factor denominator */
    E *content;
    E *fl = factor_list_in(den, x, &content);
    /* constant factors (free of x) fold into the leading coefficient */
    E *cst = content;
    uint32_t nf = 0;
    UP *fac = salloc(sizeof(UP) * (fl->n + 1));
    long *mult = salloc(sizeof(long) * (fl->n + 1));
    /* split the factor list into x-free factors (into cst) and the
     * polynomial factors f_i with multiplicities m_i */
    for (uint32_t i = 0; i < fl->n; i++) {
        E *f = K0(EK(fl)[i]);
        long m;
        num_is_small_int(K1(EK(fl)[i]), &m);
        if (freeof(f, x)) {
            cst = s_mul2(cst, s_powi(f, m));
            continue;
        }
        if (!up_from(f, x, &fac[nf])) return e;
        mult[nf] = m;
        nf++;
    }
    /* one term for the polynomial part plus at most m_i per factor */
    size_t nterms = 2;
    for (uint32_t i = 0; i < nf; i++) nterms += (size_t)mult[i];
    E **terms = salloc(sizeof(E *) * nterms);
    uint32_t nt = 0;
    terms[nt++] = poly;
    /* R / (cst * prod f^m) */
    R = up_scale(R, s_inv(cst));
    if (nf == 1 && mult[0] == 1) {
        terms[nt++] = s_div(up_to(R, x), up_to(fac[0], x));
        return s_add(nt, terms);
    }
    for (uint32_t i = 0; i < nf; i++) {
        poll_intr();
        UP Pi = up_pow(fac[i], mult[i]);
        UP Qi = up_const(K->c1);
        for (uint32_t j = 0; j < nf; j++)
            if (j != i) Qi = up_mul(Qi, up_pow(fac[j], mult[j]));
        /* s Q_i + t P_i = 1, so R s / P_i is the P_i-part of R / D */
        UP s, t;
        UP g = up_egcd(Qi, Pi, &s, &t);
        if (g.d != 0) return e;
        UP A;
        up_divrem(up_mul(R, s), Pi, NULL, &A);
        /* expand A in powers of fac[i] */
        for (long j = mult[i]; j >= 1; j--) {
            UP q, r;
            up_divrem(A, fac[i], &q, &r);
            if (r.d >= 0) terms[nt++] = s_mul2(up_to(r, x), s_pow(up_to(fac[i], x), mk_int(-j)));
            A = q;
        }
    }
    return s_add(nt, terms);
}

/* ------------------------------------------------------- resultant --- */
/* resultant(a, b, x): Res_x(a, b) as the determinant of the
 * (m+n) x (m+n) Sylvester matrix, where m = deg a, n = deg b: n shifted rows
 * of a's coefficients followed by m shifted rows of b's (descending
 * order).  The determinant is computed by mat_det (linalg.c, fraction-free
 * elimination), which works with symbolic coefficients.  Used by integ.c
 * (Rothstein-Trager resultant in a new variable z), solve.c (eliminating a
 * variable from polynomial systems) and the resultant / discriminant
 * commands.
 * Special cases: a constant a gives a^n and a constant b gives b^m (Res of a
 * constant with a degree-k polynomial is constant^k); two constants give 1.
 * Throws CAS_ERR_DOMAIN (from poly_coeff_list) if a or b is not a polynomial
 * in x, and CAS_ERR_UNSUPPORTED when m + n > 60 to bound the determinant
 * cost. */
E *resultant_expr(E *a, E *b, E *x) {
    long m, n;
    E *ca = poly_coeff_list(a, x, &m), *cb = poly_coeff_list(b, x, &n);
    if (m == 0 && n == 0) return K->c1;
    if (m == 0) return s_powi(EK(ca)[0], n);
    if (n == 0) return s_powi(EK(cb)[0], m);
    long N = m + n;
    if (N > 60) { /* too big for the Sylvester determinant; a common factor still makes it 0 */
        long dg;
        if (poly_degree_in(poly_gcd_expr(a, b), x, &dg) && dg > 0) return K->c0;
        cas_throw(CAS_ERR_UNSUPPORTED, "resultant: degree too large");
    }
    E **rows = salloc(sizeof(E *) * (size_t)N);
    for (long i = 0; i < N; i++) {
        E **r = salloc(sizeof(E *) * (size_t)N);
        for (long j = 0; j < N; j++) r[j] = K->c0;
        if (i < n)
            for (long j = 0; j <= m; j++) r[i + j] = EK(ca)[j];
        else
            for (long j = 0; j <= n; j++) r[i - n + j] = EK(cb)[j];
        rows[i] = s_list((uint32_t)N, r);
    }
    return mat_det(s_list((uint32_t)N, rows));
}

/* ------------------------------------------------- numeric roots --- */
/* Minimal complex double arithmetic (C99 complex.h is avoided to stay
 * freestanding-friendly on the ARM target). */
typedef struct {
    double re, im;
} cd;

/* complex product */
static cd cmul(cd a, cd b) {
    cd r = {a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re};
    return r;
}
/* a / b by the textbook formula (no scaling against overflow; b = 0 gives
 * inf / nan, which the Aberth loop below guards against). */
static cd cdivv(cd a, cd b) {
    double d = b.re * b.re + b.im * b.im;
    cd r = {(a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d};
    return r;
}

/* roots of polynomial with complex coefficients c[0..n] (ascending)
 *
 * Aberth-Ehrlich method: all n approximations z_i are refined
 * simultaneously by
 *     w_i = N_i / (1 - N_i * sum_{j != i} 1 / (z_i - z_j)),  N_i = p(z_i)/p'(z_i),
 *     z_i <- z_i - w_i,
 * i.e. Newton's step corrected by the repulsion of the other approximations,
 * which converges cubically to simple roots and does not let two
 * approximations converge to the same simple root.  Updates are applied in
 * place (Gauss-Seidel style: later z_i see the already-updated earlier
 * ones).
 * Details:
 *  - the polynomial is first made monic (c[n] must be nonzero);
 *  - starting points lie on a circle of radius (1 + max |c_i|) / 2, i.e.
 *    half the Cauchy bound for the monic polynomial, at angles
 *    2 pi i / n + 0.4 (the offset breaks the symmetry with respect to the
 *    real axis, so real polynomials can converge to complex pairs);
 *  - p and p' are evaluated together by Horner's rule;
 *  - coincident approximations get a tiny offset to avoid division by zero,
 *    and a NaN correction falls back to the plain Newton step;
 *  - stop when every relative step |w_i| / (1 + |z_i|) is below 1e-15, or
 *    after 500 sweeps.  No failure is reported: the current approximations
 *    are returned either way (multiple roots converge only linearly and
 *    end up accurate to about eps^(1/multiplicity)).
 * Writes the roots, unsorted, to rr / ri (n entries) and returns n; uses
 * O(n) scratch.  O(n^2) work per sweep. */
int poly_roots_c(int n, const double *cr, const double *ci, double *rr, double *ri) {
    if (n < 1) return 0;
    cd *c = salloc(sizeof(cd) * (size_t)(n + 1));
    for (int i = 0; i <= n; i++) {
        c[i].re = cr[i];
        c[i].im = ci[i];
    }
    /* normalise */
    cd lc = c[n];
    for (int i = 0; i <= n; i++) c[i] = cdivv(c[i], lc);
    cd *z = salloc(sizeof(cd) * (size_t)n);
    double rad = 0;
    for (int i = 0; i < n; i++) {
        double m = hypot(c[i].re, c[i].im);
        if (m > rad) rad = m;
    }
    rad = 1 + rad;
    for (int i = 0; i < n; i++) {
        double th = 2 * M_PI * i / n + 0.4;
        z[i].re = rad * 0.5 * cos(th);
        z[i].im = rad * 0.5 * sin(th);
    }
    for (int it = 0; it < 500; it++) {
        double maxd = 0;
        for (int i = 0; i < n; i++) {
            /* p(z), p'(z) by Horner */
            cd p = {1, 0}, dp = {0, 0};
            for (int k = n - 1; k >= 0; k--) {
                dp = cmul(dp, z[i]);
                dp.re += p.re;
                dp.im += p.im;
                p = cmul(p, z[i]);
                p.re += c[k].re;
                p.im += c[k].im;
            }
            /* Newton correction p/p' and the Aberth sum of 1/(z_i - z_j) */
            cd ratio = cdivv(p, dp);
            cd s = {0, 0};
            for (int j = 0; j < n; j++) {
                if (j == i) continue;
                cd dz = {z[i].re - z[j].re, z[i].im - z[j].im};
                if (dz.re == 0 && dz.im == 0) dz.re = 1e-300;
                cd one = {1, 0};
                cd q = cdivv(one, dz);
                s.re += q.re;
                s.im += q.im;
            }
            cd one = {1, 0};
            cd den = {one.re - (ratio.re * s.re - ratio.im * s.im), one.im - (ratio.re * s.im + ratio.im * s.re)};
            cd w = cdivv(ratio, den);
            if (w.re != w.re) w = ratio;
            z[i].re -= w.re;
            z[i].im -= w.im;
            double dm = hypot(w.re, w.im) / (1 + hypot(z[i].re, z[i].im));
            if (dm > maxd) maxd = dm;
        }
        if (maxd < 1e-15) break;
    }
    for (int i = 0; i < n; i++) {
        rr[i] = z[i].re;
        ri[i] = z[i].im;
    }
    return n;
}

/* Float (or complex float re + im*i) node for a computed root, with parts
 * that are below 1e-12 relative to the root's magnitude snapped to 0 so that
 * real roots of real polynomials print as reals. */
static E *cnum(double re, double im) {
    double mag = fabs(re) + fabs(im);
    if (fabs(im) < 1e-12 * (mag + 1e-300)) im = 0;
    if (fabs(re) < 1e-12 * (mag + 1e-300)) re = 0;
    if (im == 0) return mk_flt(re);
    return s_add2(mk_flt(re), s_mul2(mk_flt(im), K->i_));
}

/* proot(p[, x]) and the numeric fallback of solve / roots: all complex roots
 * of a polynomial with numeric (possibly complex) coefficients, as a LIST of
 * floats.  p is either a polynomial in x (x defaults to default_var(p);
 * x may be NULL) or a LIST of coefficients in descending order.  Leading
 * zero coefficients are stripped; constants give the empty list.  Throws
 * CAS_ERR_DOMAIN if a coefficient does not evaluate to a number (and, via
 * poly_coeff_list, if p is not a polynomial).  Output order: real roots
 * (|im| < 1e-10) first in increasing order, then the complex ones by real
 * part, then imaginary part (insertion sort with tolerances). */
E *proot_expr(E *p, E *x) {
    E *l;
    if (p->t == E_LIST) l = p;
    else {
        if (!x) x = default_var(p);
        l = poly_coeff_list(p, x, NULL);
    }
    int n = (int)l->n - 1;
    while (n > 0 && is_zero(EK(l)[l->n - 1 - n])) {
        /* strip leading zeros (descending list) */
        l = s_list(l->n - 1, EK(l) + 1);
        n--;
    }
    if (n < 1) return s_list(0, NULL);
    double *cr = salloc(sizeof(double) * (size_t)(n + 1)), *ci = salloc(sizeof(double) * (size_t)(n + 1));
    for (int i = 0; i <= n; i++) {
        double re, im;
        if (!eval_complex(EK(l)[n - i], &re, &im)) cas_throw(CAS_ERR_DOMAIN, "proot: coefficients must be numeric");
        cr[i] = re;
        ci[i] = im;
    }
    double *rr = salloc(sizeof(double) * (size_t)n), *ri = salloc(sizeof(double) * (size_t)n);
    poly_roots_c(n, cr, ci, rr, ri);
    /* sort: real first by value, then by real part */
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0; j--) {
            int a_real = fabs(ri[j - 1]) < 1e-10, b_real = fabs(ri[j]) < 1e-10;
            int sw = 0;
            if (a_real != b_real) sw = !a_real;
            else if (rr[j - 1] > rr[j] + 1e-12) sw = 1;
            else if (fabs(rr[j - 1] - rr[j]) <= 1e-12 && ri[j - 1] > ri[j]) sw = 1;
            if (!sw) break;
            double t = rr[j]; rr[j] = rr[j - 1]; rr[j - 1] = t;
            t = ri[j]; ri[j] = ri[j - 1]; ri[j - 1] = t;
        }
    E **v = salloc(sizeof(E *) * (size_t)n);
    for (int i = 0; i < n; i++) v[i] = cnum(rr[i], ri[i]);
    return s_list((uint32_t)n, v);
}

/* ---------------------------------------------------------- exact() --- */
/* A "nice" rational close to the double v (exact() of a float).
 *  - 0 and integers below 9e15 convert exactly;
 *  - |v| >= 9e15 or |v| < 1e-15: the exact binary value (num_from_double),
 *    since a short continued fraction would not be meaningful there;
 *  - otherwise the continued-fraction expansion of |v| is followed
 *    (a = floor(r), r <- 1/(r - a)), building convergents
 *    h_k / k_k with h_k = a h_(k-1) + h_(k-2) (likewise k), and stopping at
 *    the first convergent within relative 1e-12 of v, i.e. the simplest
 *    fraction that explains the float to about 12 digits (0.333333333333
 *    becomes 1/3).  Also stops after 40 terms, on a zero or huge remainder,
 *    or a partial quotient above 9e15.
 * Throws CAS_ERR_DOMAIN for inf / nan. */
static E *dbl_to_rat(double v) {
    if (v == 0) return K->c0;
    if (v != v || v - v != 0) cas_throw(CAS_ERR_DOMAIN, "exact: non-finite number");
    if (v == floor(v) && fabs(v) < 9e15) return mk_int((int64_t)v);
    if (fabs(v) >= 9e15 || fabs(v) < 1e-15) return num_from_double(v);
    /* continued fraction to ~1e-12 relative */
    double x = fabs(v);
    E *h0 = K->c0, *h1 = K->c1, *k0 = K->c1, *k1 = K->c0;
    double r = x;
    for (int it = 0; it < 40; it++) {
        double a = floor(r);
        if (a > 9e15) break;
        E *ae = mk_int((int64_t)a);
        E *h2 = int_add(int_mul(ae, h1), h0), *k2 = int_add(int_mul(ae, k1), k0);
        h0 = h1; h1 = h2; k0 = k1; k1 = k2;
        double approx = int_to_double(h1) / int_to_double(k1);
        if (fabs(approx - x) <= 1e-12 * x) break;
        if (r - a < 1e-300) break;
        r = 1.0 / (r - a);
        if (r > 1e15) break;
    }
    E *q = mk_rat(h1, k1);
    return v < 0 ? num_neg(q) : q;
}

/* exact(e): replace every float in e by dbl_to_rat of its double value and
 * re-simplify (rebuild).  Extended-precision floats from bf.c are converted
 * through their double only.  Recursion is guarded by stack_check. */
E *exact_expr(E *e) {
    stack_check();
    if (e->t == E_FLT) return dbl_to_rat(EF(e));
    if (e->t == E_INT || e->t == E_RAT || e->t == E_SYM || e->t == E_STR) return e;
    E **k = salloc(sizeof(E *) * (e->n + 1));
    for (uint32_t i = 0; i < e->n; i++) k[i] = exact_expr(EK(e)[i]);
    return rebuild(e, k);
}
