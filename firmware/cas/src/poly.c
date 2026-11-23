/*
 * poly.c -- recursive dense multivariate polynomial arithmetic over Q,
 * exact division, pseudo-remainders, primitive-PRS GCD, squarefree
 * decomposition (Yun), and conversion to/from expressions.
 *
 * Role in the CAS
 *   Expressions (E) are canonical trees of sums, products and powers; this
 *   module translates them into polynomials (P, see poly.h for the layout)
 *   whose variables are "kernels": symbols and every subexpression that is not
 *   polynomial (sin(x), exp(x), x^(1/3), (x+1)^(-1) in expand mode, ...).
 *   The algebra is done on P and the result is translated back with p2e,
 *   which rebuilds an auto-simplified expression.  Entry points:
 *     expand            e2p_expand -> p2e  (used almost everywhere)
 *     rat_normal        e2r -> cancel gcd -> p2e  (normal / ratsimp / together)
 *     numden            numerator / denominator with integer coefficients
 *                       (solve, integrate, factor, limits, sums, ...)
 *     poly_degree_in, poly_coeff, poly_coeff_list, collect_expr,
 *     poly_quo_rem, poly_gcd_expr, poly_lcm_expr   (user commands, cmds.c)
 *   factor.c and heurisch.c use the P layer directly (p_gcd, p_sqf, e2r, ...).
 *
 * Algorithms
 *   - Addition/multiplication: schoolbook on the recursive dense form
 *     (Knuth TAOCP vol. 2, 4.6.1; Geddes/Czapor/Labahn "Algorithms for
 *     Computer Algebra" (GCL) ch. 2).  Products cost O(n*m) coefficient
 *     multiplications per level; there is no Karatsuba at this level.
 *   - Exact division and pseudo-division in the main variable (GCL 2.4;
 *     Knuth 4.6.1 Algorithm R).
 *   - GCD: primitive polynomial remainder sequence over Z[x_0..x_v]
 *     (Collins/Brown; GCL ch. 7, Knuth 4.6.1 Algorithm E), recursive in the
 *     variables through the content computation.  Inputs over Q are first
 *     scaled to primitive integer polynomials.  No modular or heuristic gcd.
 *   - Squarefree decomposition: Yun's algorithm (GCL ch. 8; von zur Gathen &
 *     Gerhard "Modern Computer Algebra" (MCA) section 14.6, Alg. 14.21).
 *   - Rational normal form: common denominators combined with gcds,
 *     cancellation of the final gcd; symbols under fractional powers are
 *     replaced by x = T^q first so that radicals become polynomial
 *     (README, "Polynomials").
 *
 * Memory and errors
 *   Everything is allocated in scratch with salloc; nothing is freed here.
 *   Hard errors (division by the zero polynomial, absurd exponents, too many
 *   kernels, out of memory, interrupts) are raised with cas_throw.  Deep
 *   recursions call stack_check(); long loops call poll_intr().
 */
#include "poly.h"
#include "casx.h" /* casx_pgcd: the extension's modular gcd (cas/ext/alg/mgcd.c) */
#include <string.h>

/* --------------------------------------------------------------- basics --- */
/* Constant polynomial with value c (an E_INT, E_RAT or E_FLT node). */
/* P nodes are immutable, so every zero constant can be the same node: dense recursive results
 * are mostly zero slots (Wester H27 needs this to fit) */
static P zero_p = {-1, 0, NULL, NULL};

P *p_const(E *c) {
    if (c == K->c0) {
        zero_p.c = K->c0;
        return &zero_p;
    }
    P *p = salloc(sizeof(P));
    p->v = -1;
    p->n = 0;
    p->c = c;
    p->k = NULL;
    return p;
}

/* Raw polynomial node in x_v with room for n coefficients (at least one slot
 * is allocated so that k is never NULL).  The coefficients are left
 * uninitialised: the caller fills k[0..n-1] and usually finishes with
 * p_norm. */
P *p_mk(int v, uint32_t n) {
    P *p = salloc(sizeof(P));
    p->v = v;
    p->n = n;
    p->c = NULL;
    p->k = salloc(sizeof(P *) * (n ? n : 1));
    return p;
}

/* The polynomial x_v, i.e. coefficients [0, 1]. */
P *p_var(int v) {
    P *p = p_mk(v, 2);
    p->k[0] = p_const(K->c0);
    p->k[1] = p_const(K->c1);
    return p;
}

int p_is_zero(P *p) { return p->v < 0 && num_is_zero(p->c); }
int p_is_one(P *p) { return p->v < 0 && is_one(p->c); }
int p_is_const(P *p) { return p->v < 0; }

/* Normalise a freshly built polynomial: drop zero leading coefficients (this
 * modifies p->n in place, so it must only be applied to nodes the caller has
 * just created), return the constant 0 if nothing is left and the single
 * coefficient if the degree in x_v is 0.  Constants are returned unchanged. */
P *p_norm(P *p) {
    if (p->v < 0) return p;
    while (p->n > 0 && p_is_zero(p->k[p->n - 1])) p->n--;
    if (p->n == 0) return p_const(K->c0);
    if (p->n == 1) return p->k[0];
    return p;
}

/* Structural equality of two normalised polynomials, which is mathematical
 * equality thanks to the normal form.  Constants compare by value, but a
 * float and an exact number are never equal (so that a float result is not
 * mistaken for an exact one, e.g. in rf_add). */
int p_eq(P *a, P *b) {
    stack_check();
    if (a->v != b->v) return 0;
    if (a->v < 0) return num_cmp(a->c, b->c) == 0 && (a->c->t == E_FLT) == (b->c->t == E_FLT);
    if (a->n != b->n) return 0;
    for (uint32_t i = 0; i < a->n; i++)
        if (!p_eq(a->k[i], b->k[i])) return 0;
    return 1;
}

/* a + b.  Three cases, by main variable:
 *   both constants         numeric addition;
 *   same main variable     coefficient-wise addition, then p_norm (leading
 *                          terms can cancel);
 *   different main vars    the one with the smaller variable is free of the
 *                          other's main variable, so it is added to the
 *                          constant coefficient k[0] only (no normalisation
 *                          needed: the leading coefficient is unchanged).
 * Zero operands are returned as is (results may share structure with a and
 * b). */
P *p_add(P *a, P *b) {
    stack_check();
    if (p_is_zero(a)) return b;
    if (p_is_zero(b)) return a;
    if (a->v < 0 && b->v < 0) return p_const(num_add(a->c, b->c));
    if (a->v == b->v) {
        uint32_t n = a->n > b->n ? a->n : b->n;
        P *r = p_mk(a->v, n);
        for (uint32_t i = 0; i < n; i++) {
            if (i < a->n && i < b->n) r->k[i] = p_add(a->k[i], b->k[i]);
            else r->k[i] = i < a->n ? a->k[i] : b->k[i];
        }
        return p_norm(r);
    }
    if (a->v < b->v) {
        P *t = a; a = b; b = t;
    }
    P *r = p_mk(a->v, a->n);
    memcpy(r->k, a->k, sizeof(P *) * a->n);
    r->k[0] = p_add(a->k[0], b);
    return r;
}

/* -a, coefficient by coefficient (normal form is preserved). */
P *p_neg(P *a) {
    stack_check();
    if (a->v < 0) return p_const(num_neg(a->c));
    P *r = p_mk(a->v, a->n);
    for (uint32_t i = 0; i < a->n; i++) r->k[i] = p_neg(a->k[i]);
    return r;
}

P *p_sub(P *a, P *b) { return p_add(a, p_neg(b)); }

/* c * a for a number c.  Scaling by zero yields the zero constant (a float
 * zero when c is a float, so that float-ness is not lost).  Scaling by a
 * nonzero exact number cannot create zero coefficients, so the result is not
 * re-normalised. */
P *p_scale(P *a, E *c) {
    stack_check();
    if (num_is_zero(c)) return p_const(c->t == E_FLT ? c : K->c0);
    if (a->v < 0) return p_const(num_mul(a->c, c));
    P *r = p_mk(a->v, a->n);
    for (uint32_t i = 0; i < a->n; i++) r->k[i] = p_scale(a->k[i], c);
    return r;
}

/* a * b.  Constants scale the other operand.  If the main variables differ,
 * the operand with the larger variable has its coefficients multiplied by
 * the other operand (which is a "constant" with respect to that variable).
 * With equal main variables this is the schoolbook convolution
 *     r_{i+j} += a_i * b_j,
 * recursing into the coefficient ring; cost O(deg a * deg b) recursive
 * multiplications.  Polls for interrupts once per univariate product. */
P *p_mul(P *a, P *b) {
    stack_check();
    if (p_is_zero(a) || p_is_zero(b)) return p_const(K->c0);
    if (a->v < 0 && b->v < 0) return p_const(num_mul(a->c, b->c));
    if (a->v < 0) return p_scale(b, a->c);
    if (b->v < 0) return p_scale(a, b->c);
    if (a->v < b->v) {
        P *t = a; a = b; b = t;
    }
    if (a->v > b->v) {
        P *r = p_mk(a->v, a->n);
        for (uint32_t i = 0; i < a->n; i++) r->k[i] = p_mul(a->k[i], b);
        return p_norm(r);
    }
    poll_intr();
    uint32_t n = a->n + b->n - 1;
    P *r = p_mk(a->v, n);
    for (uint32_t i = 0; i < n; i++) r->k[i] = p_const(K->c0);
    for (uint32_t i = 0; i < a->n; i++) {
        if (p_is_zero(a->k[i])) continue;
        for (uint32_t j = 0; j < b->n; j++) {
            if (p_is_zero(b->k[j])) continue;
            r->k[i + j] = p_add(r->k[i + j], p_mul(a->k[i], b->k[j]));
        }
    }
    return p_norm(r);
}

/* a^k by binary powering (right-to-left over the bits of k): O(log k)
 * multiplications.  p_pow(a, 0) = 1. */
/* max degree of p in each variable index < 64 (deg[]), and the highest index + 1 */
static int p_degs(P *p, long *deg) {
    stack_check();
    if (p->v < 0) return 0;
    int top = p->v + 1;
    if (p->v < 64 && (long)p->n - 1 > deg[p->v]) deg[p->v] = (long)p->n - 1;
    for (uint32_t i = 0; i < p->n; i++) {
        int t = p_degs(p->k[i], deg);
        if (t > top) top = t;
    }
    return top;
}

int p_degs_pub(P *p, long *deg);
int p_degs_pub(P *p, long *deg) { return p_degs(p, deg); }

P *p_pow(P *a, uint32_t k) {
    if (k > 1) {
        /* size precheck: the recursive dense result has about prod_v (k deg_v + 1) / nv!
         * coefficient slots (the simplex of total degree k deg); refuse up front what
         * cannot fit in the free scratch instead of grinding until it runs out */
        long deg[64] = {0};
        int top = p_degs(a, deg), nv = 0;
        double slots = 1;
        for (int v = 0; v < top && v < 64; v++)
            if (deg[v] > 0) slots *= (double)k * (double)deg[v] + 1, nv++, slots /= nv;
        if (slots * 40.0 > (double)(size_t)(K->s_hi - K->s_top)) cas_throw(CAS_ERR_MEMORY, "result too large");
    }
    P *r = p_const(K->c1), *b = a;
    while (k) {
        if (k & 1) r = p_mul(r, b);
        k >>= 1;
        if (k) b = p_mul(b, b);
    }
    return r;
}

/* Degree of p in x_v, -1 for the zero polynomial.  v need not be the main
 * variable: for a larger main variable the maximum over the coefficients is
 * taken; a polynomial whose variables are all smaller than v has degree 0. */
long p_deg(P *p, int v) {
    stack_check();
    if (p_is_zero(p)) return -1;
    if (p->v < 0 || p->v < v) return 0;
    if (p->v == v) return (long)p->n - 1;
    long m = 0;
    for (uint32_t i = 0; i < p->n; i++) {
        long d = p_deg(p->k[i], v);
        if (d > m) m = d;
    }
    return m;
}

/* 1 if x_v occurs in p.  Uses the ordering invariant: variables smaller than
 * v cannot contain x_v, so recursion stops as soon as p->v < v. */
int p_has_var(P *p, int v) {
    stack_check();
    if (p->v < 0 || p->v < v) return 0;
    if (p->v == v) return 1;
    for (uint32_t i = 0; i < p->n; i++)
        if (p_has_var(p->k[i], v)) return 1;
    return 0;
}

int p_maxvar(P *p) { return p->v; }

/* Coefficient of x_v^d in p, as a polynomial in the remaining variables.
 * For the main variable this is just k[d] (shared, not copied); for an
 * inner variable the coefficient is extracted from every coefficient of p
 * and the result is rebuilt in p's main variable.  Out-of-range d gives 0. */
P *p_coef(P *p, int v, long d) {
    stack_check();
    if (p->v < 0 || p->v < v) return d == 0 ? p : p_const(K->c0);
    if (p->v == v) return d >= 0 && (uint32_t)d < p->n ? p->k[d] : p_const(K->c0);
    P *r = p_mk(p->v, p->n);
    for (uint32_t i = 0; i < p->n; i++) r->k[i] = p_coef(p->k[i], v, d);
    return p_norm(r);
}

/* Leading coefficient with respect to the main variable (p itself for a
 * constant). */
P *p_lc(P *p) { return p->v < 0 ? p : p->k[p->n - 1]; }

/* Leading numeric coefficient: follow leading coefficients down to a
 * constant.  This is the coefficient of the lexicographically largest
 * monomial (variables ordered by kernel index); its sign is used as the
 * "sign" of a polynomial when normalising gcds, contents and factors. */
E *p_lnum(P *p) {
    while (p->v >= 0) p = p->k[p->n - 1];
    return p->c;
}

/* t * x_v^sh * b, where t is free of x_v and b has main variable x_v (the
 * subtrahend of one division step in p_divexact / p_prem).  The shift by sh
 * is done by prepending sh zero coefficients instead of multiplying. */
static P *p_mul_xk(P *b, int v, uint32_t sh, P *t) {
    P *tb = p_mul(t, b);
    if (p_is_zero(tb)) return tb;
    if (tb->v != v) {
        P *r = p_mk(v, sh + 1);
        for (uint32_t i = 0; i < sh; i++) r->k[i] = p_const(K->c0);
        r->k[sh] = tb;
        return p_norm(r);
    }
    P *r = p_mk(v, tb->n + sh);
    for (uint32_t i = 0; i < sh; i++) r->k[i] = p_const(K->c0);
    for (uint32_t i = 0; i < tb->n; i++) r->k[i + sh] = tb->k[i];
    return r;
}

/* Exact quotient a / b over Q, or NULL if b does not divide a.
 *
 * Cases by main variable:
 *   b constant         scale by 1/b;
 *   a->v < b->v        b contains a variable that a lacks: no exact quotient
 *                      unless a = 0;
 *   a->v > b->v        b is "constant" in a's main variable: divide every
 *                      coefficient (fails if any of them fails);
 *   a->v == b->v       classical long division in x_v.  Each step divides
 *                      the leading coefficients recursively (which must be
 *                      exact) and subtracts t * x_v^sh * b.  The remainder must
 *                      vanish at the end.
 * With float coefficients the leading term may fail to cancel exactly; the
 * "no progress" check stops instead of looping forever and reports failure.
 * Throws CAS_ERR_DOMAIN for b = 0.  Recursive (stack_check) and interruptible.
 * Cost: (deg a - deg b + 1) * deg b recursive multiplications per level. */
P *p_divexact(P *a, P *b) {
    stack_check();
    if (p_is_zero(b)) cas_throw(CAS_ERR_DOMAIN, "division by zero polynomial");
    if (p_is_zero(a)) return a;
    if (b->v < 0) return p_scale(a, num_inv(b->c));
    if (a->v < b->v) return NULL;
    if (a->v > b->v) {
        P *r = p_mk(a->v, a->n);
        for (uint32_t i = 0; i < a->n; i++) {
            r->k[i] = p_divexact(a->k[i], b);
            if (!r->k[i]) return NULL;
        }
        return r;
    }
    int v = b->v;
    if (a->n < b->n) return NULL;
    P *q = p_mk(v, a->n - b->n + 1);
    for (uint32_t i = 0; i < q->n; i++) q->k[i] = p_const(K->c0);
    P *r = a;
    P *lb = p_lc(b);
    while (!p_is_zero(r) && r->v == v && r->n >= b->n) {
        poll_intr();
        P *t = p_divexact(p_lc(r), lb);
        if (!t) return NULL;
        uint32_t sh = r->n - b->n;
        q->k[sh] = t;
        P *nr = p_sub(r, p_mul_xk(b, v, sh, t));
        if (!p_is_zero(nr) && nr->v == v && nr->n >= r->n) return NULL; /* no progress (floats) */
        r = nr;
    }
    if (!p_is_zero(r)) return NULL;
    return p_norm(q);
}

/* Pseudo-remainder of a by b with respect to b's main variable v (Knuth
 * 4.6.1 Algorithm R): repeatedly r := lc(b) * r - lc(r) * x_v^sh * b until
 * deg_v r < deg_v b.  No division in the coefficient ring is needed, so this
 * works over Z[other variables]; the result equals lc(b)^k * a mod b where k
 * is the number of steps actually performed (at most deg a - deg b + 1).
 * Precondition: b has main variable v >= 0 (a constant b is not handled) and
 * a has main variable <= v.  Used by the primitive PRS gcd (gcd_int), which
 * removes the extra lc(b) powers by taking primitive parts. */
P *p_prem(P *a, P *b) {
    int v = b->v;
    P *lb = p_lc(b);
    uint32_t db = b->n - 1;
    P *r = a;
    while (!p_is_zero(r) && r->v == v && r->n - 1 >= db) {
        poll_intr();
        P *lr = p_lc(r);
        uint32_t sh = r->n - 1 - db;
        r = p_sub(p_mul(r, lb), p_mul_xk(b, v, sh, lr));
    }
    return r;
}

/* Partial derivative dp / dx_v.  Inner variables are differentiated
 * coefficient-wise; for the main variable, k[i] * x^i becomes
 * i * k[i] * x^(i-1). */
P *p_diff(P *p, int v) {
    stack_check();
    if (p->v < 0 || p->v < v) return p_const(K->c0);
    if (p->v > v) {
        P *r = p_mk(p->v, p->n);
        for (uint32_t i = 0; i < p->n; i++) r->k[i] = p_diff(p->k[i], v);
        return p_norm(r);
    }
    P *r = p_mk(v, p->n - 1);
    for (uint32_t i = 1; i < p->n; i++) r->k[i - 1] = p_scale(p->k[i], mk_int((int64_t)i));
    return p_norm(r);
}

/* p with x_v replaced by the polynomial val.
 * If v is the main variable this is Horner's rule, r = (..(k[n-1] val + ..)
 * val + k[0]).  If v is an inner variable, every coefficient is substituted
 * and p is rebuilt as sum k_i' * x_pv^i with full multiplications, because
 * val may contain variables >= p->v and the result then no longer has x_pv as
 * its main variable.  Variables smaller than v cannot contain x_v. */
P *p_subst_var(P *p, int v, P *val) {
    stack_check();
    if (p->v < 0 || p->v < v) return p;
    if (p->v > v) {
        P *r = p_const(K->c0);
        /* rebuild as sum of coef_i * x_pv^i (val may contain higher vars) */
        P *xv = p_var(p->v), *pw = p_const(K->c1);
        for (uint32_t i = 0; i < p->n; i++) {
            r = p_add(r, p_mul(p_subst_var(p->k[i], v, val), pw));
            pw = p_mul(pw, xv);
        }
        return r;
    }
    /* Horner */
    P *r = p_const(K->c0);
    for (uint32_t i = p->n; i-- > 0;) r = p_add(p_mul(r, val), p->k[i]);
    return r;
}

P *p_eval(P *p, int v, E *val) { return p_subst_var(p, v, p_const(val)); }

/* ------------------------------------------------------------ content --- */
/* 1 if any coefficient of p is a float.  Float polynomials have no
 * meaningful gcd or content; the gcd routines treat them as coprime. */
static int p_has_float(P *p) {
    stack_check();
    if (p->v < 0) return p->c->t == E_FLT;
    for (uint32_t i = 0; i < p->n; i++)
        if (p_has_float(p->k[i])) return 1;
    return 0;
}

/* Accumulate over all nonzero numeric coefficients n/d of p:
 * *g = gcd of the numerators (NULL until the first one is seen) and
 * *l = lcm of the denominators.  Coefficients must be exact (no floats). */
static void numcontent_rec(P *p, E **g, E **l) {
    stack_check();
    if (p->v < 0) {
        if (num_is_zero(p->c)) return;
        E *nu = num_numer(p->c), *de = num_denom(p->c);
        *g = *g ? int_gcd(*g, nu) : int_abs(nu);
        *l = int_lcm(*l, de);
        return;
    }
    for (uint32_t i = 0; i < p->n; i++) numcontent_rec(p->k[i], g, l);
}

/* Rational content of p: the positive rational c = gcd(numerators) /
 * lcm(denominators) such that p / c has coprime integer coefficients.
 * Returns 1 for the zero polynomial and for polynomials with floats. */
E *p_numcontent(P *p) {
    if (p_has_float(p)) return K->c1;
    E *g = NULL, *l = K->c1;
    numcontent_rec(p, &g, &l);
    if (!g) return K->c1;
    return mk_rat(g, l);
}

/* Split p = c * q with q a primitive integer polynomial whose leading
 * numeric coefficient (p_lnum) is positive; c (sign included) is stored in
 * *c and q is returned.  This is the unit normalisation used for gcds and
 * factors over Q.  For p = 0, *c = 1 and p is returned; for float
 * polynomials c is +-1. */
P *p_intprim(P *p, E **c) {
    if (p_is_zero(p)) {
        *c = K->c1;
        return p;
    }
    E *ct = p_numcontent(p);
    if (num_sgn(p_lnum(p)) < 0) ct = num_neg(ct);
    *c = ct;
    if (num_is_one(ct)) return p;
    return p_scale(p, num_inv(ct));
}

/* p or -p, whichever has a positive leading numeric coefficient. */
static P *p_sign_norm(P *p) {
    if (!p_is_zero(p) && num_sgn(p_lnum(p)) < 0) return p_neg(p);
    return p;
}

static P *gcd_int(P *a, P *b);

/* Content of p with respect to its main variable: the gcd (over
 * Z[smaller variables], via gcd_int) of its coefficients, normalised to a
 * positive leading numeric coefficient.  Stops early when the gcd becomes 1.
 * For a constant p, p itself is returned; the zero polynomial gives 0.
 * Coefficients should be integral: rational constants make gcd_int return 1.
 * Mutually recursive with gcd_int. */
P *p_content(P *p) {
    if (p->v < 0) return p;
    P *g = NULL;
    for (uint32_t i = 0; i < p->n; i++) {
        if (p_is_zero(p->k[i])) continue;
        g = g ? gcd_int(g, p->k[i]) : p_sign_norm(p->k[i]);
        if (p_is_one(g)) break;
    }
    if (!g) return p_const(K->c0);
    /* sign: the first coefficient is sign-normalised and gcd_int returns
     * sign-normalised results, so the content always has a positive leading
     * numeric coefficient (it does not carry the sign of p) */
    return g;
}

/* Primitive part p / content(p), sign-normalised.  Constants map to 1 (or 0
 * for zero).  If the exact division unexpectedly fails, p is returned. */
static P *p_primpart(P *p) {
    if (p->v < 0) return p_const(p_is_zero(p) ? K->c0 : K->c1);
    P *c = p_content(p);
    P *r = p_divexact(p, c);
    if (!r) return p;
    return p_sign_norm(r);
}

/* GCD over Z[vars] (rational coefficients are made integral first, by the
 * caller p_gcd: a non-integer constant here just yields 1).
 *
 * Primitive PRS algorithm (GCL ch. 7, primitive PRS):
 *   gcd(a, b) = gcd(cont a, cont b) * gcd(pp a, pp b), and the gcd of the
 *   primitive parts is the primitive part of the last nonzero element of the
 *   sequence r_{i+1} = pp(prem(r_{i-1}, r_i)).
 * Contents are gcds of coefficients, computed by recursion on the smaller
 * variables, so this is a recursive multivariate algorithm.  Taking primitive
 * parts at every step keeps coefficient growth polynomial, at the price of
 * one content computation per step.  If the main variables differ, the
 * operand with the smaller main variable is free of the other's main
 * variable, so the gcd is the gcd of it with all coefficients of the other.
 * The result has a positive leading numeric coefficient.  Recursive
 * (stack_check). */
/* ---- coprimality by modular images (a degree bound) ----
 * gcd(a, b) has degree <= deg gcd(a(u, pt), b(u, pt)) mod p in every variable
 * u when the point pt and the prime p keep both leading coefficients in u
 * nonzero (the image of the gcd divides the gcd of the images).  So when the
 * image gcds have degree 0 in every variable, the gcd is a number: the dense
 * PRS below, which can need far more memory than the inputs (sparse
 * multivariate inputs, Wester H9 and H11), is not run at all. */
#define IMG_P 2147483629u /* prime < 2^31 */
static uint32_t img_mod(E *c) {
    long v;
    if (num_is_small_int(c, &v)) return (uint32_t)(((v % (long)IMG_P) + (long)IMG_P) % (long)IMG_P);
    E *m = int_mod(c, mk_int(IMG_P));
    num_is_small_int(m, &v);
    return (uint32_t)v;
}
static uint32_t img_mul(uint32_t a, uint32_t b) { return (uint32_t)((uint64_t)a * b % IMG_P); }
static uint32_t img_pow(uint32_t a, uint32_t e) {
    uint32_t r = 1;
    for (; e; e >>= 1, a = img_mul(a, a))
        if (e & 1) r = img_mul(r, a);
    return r;
}

/* p evaluated at pt for every variable but u, as coefficients out[0..du] in u
 * (added into out); 0 if a coefficient is not an integer */
static int img_eval(P *p, int u, const uint32_t *pt, uint32_t mul, uint32_t *out, long du) {
    stack_check();
    if (p->v < 0) {
        if (p->c->t != E_INT) return 0;
        out[0] = (uint32_t)(((uint64_t)out[0] + img_mul(mul, img_mod(p->c))) % IMG_P);
        return 1;
    }
    for (uint32_t k = 0; k < p->n; k++) {
        if (p_is_zero(p->k[k])) continue;
        if (p->v == u) {
            if ((long)k > du) return 0;
            if (!img_eval(p->k[k], u, pt, mul, out + k, du - (long)k)) return 0;
        } else if (!img_eval(p->k[k], u, pt, img_mul(mul, img_pow(pt[p->v], k)), out, du))
            return 0;
    }
    return 1;
}

/* degree of gcd of two univariate images mod IMG_P (dense, ascending) */
static long img_gcd_deg(uint32_t *a, long da, uint32_t *b, long db) {
    while (da >= 0 && !a[da]) da--;
    while (db >= 0 && !b[db]) db--;
    while (db >= 0) {
        if (da < db) {
            uint32_t *t = a; a = b; b = t;
            long tn = da; da = db; db = tn;
            continue;
        }
        uint32_t inv = img_pow(b[db], IMG_P - 2);
        while (da >= db) {
            uint32_t f = img_mul(a[da], inv);
            for (long i = 0; i <= db; i++) a[da - db + i] = (uint32_t)(((uint64_t)a[da - db + i] + IMG_P - img_mul(f, b[i])) % IMG_P);
            while (da >= 0 && !a[da]) da--;
            if (da < 0) break;
        }
        uint32_t *t = a; a = b; b = t;
        long tn = da; da = db; db = tn;
    }
    return da;
}

static int img_coprime0(P *a, P *b);
/* the image arrays are scratch only: released at once (p_gcd may call this twice) */
static int img_coprime(P *a, P *b) {
    char *m = smark();
    int r = img_coprime0(a, b);
    srelease(m);
    return r;
}

static int img_coprime0(P *a, P *b) {
    long deg[64] = {0}, degb[64] = {0};
    extern int p_degs_pub(P *p, long *deg);
    int top = p_degs_pub(a, deg), topb = p_degs_pub(b, degb);
    if (top > 64 || topb > 64) return 0;
    if (topb > top) top = topb;
    uint32_t pt[64];
    uint32_t seed = 0x2545F491u;
    for (int u = 0; u < top; u++) {
        if (!deg[u] || !degb[u]) continue; /* one of them is free of u: so is the gcd */
        int ok = 0;
        for (int tries = 0; tries < 3 && !ok; tries++) {
            for (int v = 0; v < top; v++) seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5, pt[v] = 2 + seed % 1000003u;
            uint32_t *ia = salloc(sizeof(uint32_t) * (size_t)(deg[u] + 1)), *ib = salloc(sizeof(uint32_t) * (size_t)(degb[u] + 1));
            memset(ia, 0, sizeof(uint32_t) * (size_t)(deg[u] + 1));
            memset(ib, 0, sizeof(uint32_t) * (size_t)(degb[u] + 1));
            if (!img_eval(a, u, pt, 1, ia, deg[u]) || !img_eval(b, u, pt, 1, ib, degb[u])) return 0;
            if (!ia[deg[u]] || !ib[degb[u]]) continue; /* a leading coefficient vanished: another point */
            ok = 1;
            if (img_gcd_deg(ia, deg[u], ib, degb[u]) > 0) return 0;
        }
        if (!ok) return 0;
    }
    return 1;
}

/* the gcd of the integer coefficients of p */
static E *p_numcontent_int(P *p, E *g) {
    stack_check();
    if (p->v < 0) return p->c->t == E_INT ? int_gcd(g, p->c) : K->c1;
    for (uint32_t i = 0; i < p->n; i++) {
        if (!p_is_zero(p->k[i])) g = p_numcontent_int(p->k[i], g);
        if (is_one(g)) break;
    }
    return g;
}

static P *gcd_int(P *a, P *b) {
    stack_check();
    if (p_is_zero(a)) return p_sign_norm(b);
    if (p_is_zero(b)) return p_sign_norm(a);
    if (a->v >= 0 && b->v >= 0 && img_coprime(a, b)) {
        E *g = p_numcontent_int(b, p_numcontent_int(a, K->c0));
        return p_const(g->t == E_INT && num_sgn(g) < 0 ? int_neg(g) : g);
    }
    if (a->v < 0 && b->v < 0) {
        if (a->c->t == E_INT && b->c->t == E_INT) return p_const(int_gcd(a->c, b->c));
        return p_const(K->c1);
    }
    if (a->v != b->v) {
        if (a->v < b->v) {
            P *t = a; a = b; b = t;
        }
        /* b is free of a's main variable */
        P *g = p_sign_norm(b);
        for (uint32_t i = 0; i < a->n; i++) {
            if (p_is_zero(a->k[i])) continue;
            g = gcd_int(g, a->k[i]);
            if (p_is_one(g)) break;
        }
        return g;
    }
    int v = a->v;
    P *ca = p_content(a), *cb = p_content(b);
    P *pa = p_divexact(a, ca), *pb = p_divexact(b, cb);
    if (!pa || !pb) return p_const(K->c1);
    P *c = gcd_int(ca, cb);
    if (pa->n < pb->n) {
        P *t = pa; pa = pb; pb = t;
    }
    /* primitive PRS: (pa, pb) <- (pb, pp(prem(pa, pb))) until the remainder
     * vanishes (pa is then the gcd up to content) or drops out of x_v */
    while (!p_is_zero(pb) && pb->v == v) {
        P *r = p_prem(pa, pb);
        pa = pb;
        if (p_is_zero(r)) {
            pb = r;
            break;
        }
        if (r->v != v) {
            /* constant in v: primitive parts are coprime */
            pa = p_const(K->c1);
            pb = p_const(K->c0);
            break;
        }
        pb = p_primpart(r);
    }
    P *g = p_primpart(pa);
    return p_sign_norm(p_mul(c, g));
}

/* GCD over Q of two polynomials in kernels, returned as a primitive integer
 * polynomial with positive leading numeric coefficient (over Q every nonzero
 * number is a unit, so the numeric part is normalised away; callers that
 * want the numeric gcd too, like poly_gcd_expr, combine it separately).
 * gcd(0, b) is the normalised b.  If either input contains a float the
 * polynomials are treated as coprime and 1 is returned. */
/* nonzero terms of p, counted up to cap */
static long p_terms(P *p, long cap) {
    if (p->v < 0) return !p_is_zero(p);
    long n = 0;
    for (uint32_t i = 0; i < p->n && n < cap; i++) n += p_terms(p->k[i], cap - n);
    return n;
}


P *p_gcd(P *a, P *b) {
    if (p_has_float(a) || p_has_float(b)) return p_const(K->c1);
    E *ca, *cb;
    P *ia = p_intprim(a, &ca), *ib = p_intprim(b, &cb);
    if (p_is_zero(a)) return p_sign_norm(ib);
    if (p_is_zero(b)) return p_sign_norm(ia);
    /* large inputs with a common factor: the modular algorithm, where the remainder
     * sequence would blow up (falls back to it when the extension declines) */
    if (CASX_SLOTS && ia->v >= 0 && ia->v == ib->v && (p_terms(ia, 64) + p_terms(ib, 64) >= 16 || ia->n + ib->n > 24) && !img_coprime(ia, ib)) {
        P *g = casx_pgcd(ia, ib);
        if (g) return g;
    }
    P *g = gcd_int(ia, ib);
    /* over Q the numeric part is a unit: return primitive integer gcd */
    E *cg;
    g = p_intprim(g, &cg);
    return g;
}

/* Yun squarefree decomposition of a primitive polynomial wrt its main var.
 * Returns factors with multiplicities (product = f up to a constant).
 *
 * Yun's algorithm (MCA Algorithm 14.21; GCL section 8.2), valid in
 * characteristic 0.  With f = prod a_i^i (a_i squarefree, pairwise coprime):
 *   a0 = gcd(f, f'),  b_1 = f / a0,  c_1 = f' / a0,  d_1 = c_1 - b_1'
 *   loop: a_i = gcd(b_i, d_i),  b_{i+1} = b_i / a_i,
 *         c_{i+1} = d_i / a_i,  d_{i+1} = c_{i+1} - b_{i+1}'
 *   until b is constant in v.
 * Only the non-trivial a_i (those that still contain v) are stored, at most
 * cap of them; further ones are dropped silently.  The stored factors are
 * not unit-normalised (callers apply p_intprim).  Content with respect to
 * v would be lost (gcd(f, f') absorbs it), hence the requirement that f be
 * primitive.  If an exact division fails (float coefficients) the whole of f
 * is returned as a single factor of multiplicity 1.  Returns 0 for a
 * constant f.  The i > 100000 guard is a safety net only. */
uint32_t p_sqf(P *f, P **facs, uint32_t *mult, uint32_t cap) {
    uint32_t nf = 0;
    if (f->v < 0) return 0;
    int v = f->v;
    P *fp = p_diff(f, v);
    P *a0 = p_gcd(f, fp);
    P *b = p_divexact(f, a0), *c = p_divexact(fp, a0);
    if (!b || !c) {
        facs[0] = f;
        mult[0] = 1;
        return 1;
    }
    P *d = p_sub(c, p_diff(b, v));
    uint32_t i = 1;
    while (!(b->v != v)) {
        poll_intr();
        P *a = p_gcd(b, d);
        P *nb = p_divexact(b, a), *nc = p_divexact(d, a);
        if (!nb || !nc) break;
        if (a->v == v && nf < cap) {
            facs[nf] = a;
            mult[nf] = i;
            nf++;
        }
        b = nb;
        c = nc;
        d = p_sub(c, p_diff(b, v));
        i++;
        if (i > 100000) break;
    }
    return nf;
}

/* ------------------------------------------------------- conversions --- */
/* Initialise an empty kernel table in scratch (initial capacity 16). */
void kt_init(ktab *kt) {
    kt->n = 0;
    kt->cap = 16;
    kt->k = salloc(sizeof(E *) * kt->cap);
    kt->has_float = 0;
}

/* Index (= polynomial variable number) of kernel k in kt, appending it if
 * it is not present yet.  Lookup is a linear scan with structural equality
 * (expr_eq), so building a polynomial with n kernels costs O(n) per kernel
 * occurrence; tables are small in practice.  The array is grown by doubling
 * (the old copy is abandoned in scratch).  Throws CAS_ERR_UNSUPPORTED beyond
 * 30000 kernels. */
int kt_index(ktab *kt, E *k) {
    for (uint32_t i = 0; i < kt->n; i++)
        if (expr_eq(kt->k[i], k)) return (int)i;
    if (kt->n >= 30000) cas_throw(CAS_ERR_UNSUPPORTED, "too many kernels");
    if (kt->n == kt->cap) {
        E **nk = salloc(sizeof(E *) * kt->cap * 2);
        memcpy(nk, kt->k, sizeof(E *) * kt->n);
        kt->k = nk;
        kt->cap *= 2;
    }
    kt->k[kt->n] = k;
    return (int)kt->n++;
}

/* The polynomial variable standing for kernel k. */
static P *kvar(ktab *kt, E *k) { return p_var(kt_index(kt, k)); }

/* kernel for base^(p/q): returns kernel b^(1/q) (or b^(-1/q)) and integer power
 *
 * b^(p/q) is represented as (b^(1/q))^|p|, with the kernel b^(-1/q) for
 * negative p, so that e.g. x^(3/2) and sqrt(x) share the kernel x^(1/2)
 * (as T^3 and T).  mapk
 * (id_map or expand_kernel) is applied to the base first.  Returns NULL when
 * the exponent numerator is not a small integer or when the kernel
 * simplifies to a non-power (e.g. a numeric base), in which case the caller
 * falls back to treating the whole power as one kernel.  Throws
 * CAS_ERR_UNSUPPORTED for |p| > 10000.  `nested` is unused. */
static P *rad_kernel(E *b, E *ex, ktab *kt, int (*nested)(void), E *(*mapk)(E *)) {
    (void)nested;
    E *pp = num_numer(ex), *q = num_denom(ex);
    int neg = int_sgn(pp) < 0;
    long pv;
    if (!num_is_small_int(pp, &pv)) return NULL;
    if (pv < 0) pv = -pv;
    E *kexp = mk_rat(neg ? K->cm1 : K->c1, q);
    E *kb = mapk ? mapk(b) : b;
    E *kern = s_pow(kb, kexp);
    if (kern->t != E_POW) {
        /* simplified away (e.g. numeric); expand directly */
        return NULL;
    }
    if (pv > 10000) cas_throw(CAS_ERR_UNSUPPORTED, "exponent too large");
    return p_pow(kvar(kt, kern), (uint32_t)pv);
}

/* Identity kernel map for rad_kernel in non-expanding mode. */
static E *id_map(E *e) { return e; }

static P *e2p_gen(E *e, ktab *kt, int expand_mode);

static E *expand_kernel(E *e);

P *e2p(E *e, ktab *kt) { return e2p_gen(e, kt, 0); }
P *e2p_expand(E *e, ktab *kt) { return e2p_gen(e, kt, 1); }

/* Expression -> polynomial in kernels (shared code of e2p and e2p_expand).
 *   numbers           constants (a float sets kt->has_float)
 *   symbols           kernels
 *   sums, products    p_add / p_mul of the converted operands
 *   b^k, k > 0 int    p_pow of the converted base (k <= 100000, otherwise
 *                     CAS_ERR_UNSUPPORTED)
 *   b^(p/q)           power of the kernel b^(+-1/q) (rad_kernel)
 *   anything else     a kernel (functions, symbolic exponents, lists, ...)
 * In expand mode (em = 1) kernels are expanded inside before they are
 * entered, so that equal kernels are recognised: function arguments and
 * radical bases are expanded, b^(-k) becomes the kernel (expanded b)^(-1)
 * raised to k, b^(u+v) is split into b^u * b^v, and other powers get an
 * expanded base and exponent.  Recursive (stack_check).
 * NULL is propagated from the operands but no base case produces it. */
/* The terms c v^k of a long sum (exact c, one symbol v, 0 <= k <= 100000, at least 16 of
 * them) as one dense polynomial in a single array (summing term by term copies the
 * coefficient array for every term, quadratic in the degree: T_1008 - 2 x T_1007 + T_1006
 * needed 40 MB); other[i] marks the terms left for the caller.  NULL if there are fewer. */
static P *add_dense(E *e, ktab *kt, uint8_t *other) {
    E *v = NULL;
    long maxk = 0;
    uint32_t nm = 0;
    for (uint32_t pass = 0; pass < 2; pass++) {
        P *r = NULL;
        if (pass) {
            if (!v || nm < 16) return NULL;
            r = p_mk(kt_index(kt, v), (uint32_t)maxk + 1);
            for (long i = 0; i <= maxk; i++) r->k[i] = p_const(K->c0);
        }
        for (uint32_t i = 0; i < e->n; i++) {
            E *t = EK(e)[i], *c = K->c1, *m = t;
            long k = 0;
            other[i] = 1;
            if (t->t == E_MUL && t->n == 2 && (EK(t)[0]->t == E_INT || EK(t)[0]->t == E_RAT)) c = EK(t)[0], m = EK(t)[1];
            if (m->t == E_INT || m->t == E_RAT) c = m, k = 0;
            else if (m->t == E_SYM) k = 1;
            else if (m->t == E_POW && K0(m)->t == E_SYM && num_is_small_int(K1(m), &k) && k > 0 && k <= 100000)
                m = K0(m);
            else
                continue;
            if (k) {
                if (!v) v = m;
                else if (!expr_eq(v, m)) continue;
            }
            other[i] = 0;
            if (!pass) {
                nm++;
                if (k > maxk) maxk = k;
            } else
                r->k[k] = p_const(num_add(r->k[k]->c, c));
        }
        if (pass) return p_norm(r);
    }
    return NULL;
}

static P *e2p_gen(E *e, ktab *kt, int em) {
    stack_check();
    switch (e->t) {
    case E_INT: case E_RAT: return p_const(e);
    case E_FLT: kt->has_float = 1; return p_const(e);
    case E_SYM: return kvar(kt, e);
    case E_ADD: {
        uint8_t *oth = e->n >= 16 ? salloc(e->n) : NULL;
        P *r = oth ? add_dense(e, kt, oth) : NULL;
        if (!r) oth = NULL, r = p_const(K->c0);
        for (uint32_t i = 0; i < e->n; i++) {
            if (oth && !oth[i]) continue;
            P *t = e2p_gen(EK(e)[i], kt, em);
            if (!t) return NULL;
            r = p_add(r, t);
        }
        return r;
    }
    case E_MUL: {
        P *r = p_const(K->c1);
        for (uint32_t i = 0; i < e->n; i++) {
            P *t = e2p_gen(EK(e)[i], kt, em);
            if (!t) return NULL;
            r = p_mul(r, t);
        }
        return r;
    }
    case E_POW: {
        E *b = K0(e), *x = K1(e);
        long k;
        if (num_is_small_int(x, &k) && k > 0) {
            if (k > 100000) cas_throw(CAS_ERR_UNSUPPORTED, "exponent too large");
            P *pb = e2p_gen(b, kt, em);
            if (!pb) return NULL;
            return p_pow(pb, (uint32_t)k);
        }
        if (!em) {
            if (x->t == E_RAT) {
                P *r = rad_kernel(b, x, kt, NULL, id_map);
                if (r) return r;
            }
            return kvar(kt, e);
        }
        /* expand mode */
        if (num_is_small_int(x, &k) && k < 0) {
            if (k < -100000) cas_throw(CAS_ERR_UNSUPPORTED, "exponent too large");
            E *kb = s_pow(expand_kernel(b), K->cm1);
            if (kb->t != E_POW) {
                P *pp = e2p_gen(kb, kt, em);
                return pp ? p_pow(pp, (uint32_t)(-k)) : NULL;
            }
            return p_pow(kvar(kt, kb), (uint32_t)(-k));
        }
        if (x->t == E_RAT) {
            P *r = rad_kernel(b, x, kt, NULL, expand_kernel);
            if (r) return r;
        }
        if (x->t == E_ADD) {
            /* a^(u+v) = a^u a^v */
            P *r = p_const(K->c1);
            for (uint32_t i = 0; i < x->n; i++) {
                E *f = s_pow(b, EK(x)[i]);
                P *t = e2p_gen(f, kt, em);
                if (!t) return NULL;
                r = p_mul(r, t);
            }
            return r;
        }
        E *ne = s_pow(expand_kernel(b), expand(x));
        if (ne->t != E_POW) return e2p_gen(ne, kt, em);
        return kvar(kt, ne);
    }
    case E_FN:
        if (em) {
            E *k = expand_kernel(e);
            return kvar(kt, k);
        }
        return kvar(kt, e);
    default: return kvar(kt, e);
    }
}

/* Expanded form of a kernel: a function call with every argument expanded,
 * or the expansion of a sum, product or power; other nodes unchanged. */
static E *expand_kernel(E *e) {
    if (e->t == E_FN) {
        E **a = salloc(sizeof(E *) * (e->n + 1));
        for (uint32_t i = 0; i < e->n; i++) a[i] = expand(EK(e)[i]);
        return s_fn(e->s, e->n, a);
    }
    if (e->t == E_ADD || e->t == E_MUL || e->t == E_POW) return expand(e);
    return e;
}

/* --- poly -> expanded expression --- */
/* Growable array of terms (scratch, doubling) collected by p2e_rec. */
typedef struct {
    E **t;
    uint32_t n, cap;
} tlist;

/* Append e to l, doubling the array when full (initial capacity 16). */
static void tl_push(tlist *l, E *e) {
    if (l->n == l->cap) {
        uint32_t nc = l->cap ? l->cap * 2 : 16;
        E **nt = salloc(sizeof(E *) * nc);
        if (l->n) memcpy(nt, l->t, sizeof(E *) * l->n);
        l->t = nt;
        l->cap = nc;
    }
    l->t[l->n++] = e;
}

/* Depth-first walk of p that emits one term per nonzero numeric leaf.
 * fac[0..nf-1] holds the kernel powers on the path from the root (one per
 * variable level, so the depth is bounded by the number of kernels); a
 * leaf c produces the auto-simplified product c * fac[0] * ... * fac[nf-1]. */
static void p2e_rec(P *p, ktab *kt, E **fac, uint32_t nf, tlist *out) {
    stack_check();
    if (p_is_zero(p)) return;
    if (p->v < 0) {
        E **f = salloc(sizeof(E *) * (nf + 1));
        f[0] = p->c;
        memcpy(f + 1, fac, sizeof(E *) * nf);
        tl_push(out, s_mul(nf + 1, f));
        return;
    }
    E *kern = kt->k[p->v];
    for (uint32_t i = 0; i < p->n; i++) {
        if (p_is_zero(p->k[i])) continue;
        if (i == 0)
            p2e_rec(p->k[i], kt, fac, nf, out);
        else {
            fac[nf] = s_pow(kern, mk_int(i));
            p2e_rec(p->k[i], kt, fac, nf + 1, out);
        }
    }
}

/* Polynomial -> expression: the canonical (auto-simplified) sum of all
 * monomials, with each variable replaced by its kernel from kt.  Kernels
 * such as x^(1/2) raised to integer powers are re-simplified by s_pow, so
 * radicals come back as rational powers.  Returns 0 for the zero
 * polynomial. */
E *p2e(P *p, ktab *kt) {
    tlist out = {NULL, 0, 0};
    E **fac = salloc(sizeof(E *) * (kt->n + 2));
    p2e_rec(p, kt, fac, 0, &out);
    if (!out.n) return K->c0;
    return s_add(out.n, out.t);
}

/* main-variable structured output: sum coef_i * x^i, coefficients expanded
 * (intended behaviour; the current implementation is simply p2e, and it has
 * no callers outside this file's declarations) */
E *p2e_factor(P *p, ktab *kt) { return p2e(p, kt); }

/* ------------------------------------------------- rational functions --- */
/* In-place addition of rational functions: n1/d1 += n2/d2.  Equal or unit
 * denominators are handled without a gcd; otherwise, with g = gcd(d1, d2),
 * the sum is (n1 * d2/g + n2 * d1/g) / (d1/g * d2), which keeps the common
 * denominator at lcm size instead of d1*d2.  The final cancellation of
 * numerator against denominator is left to rf_normalize. */
static void rf_add(P **n1, P **d1, P *n2, P *d2) {
    if (p_eq(*d1, d2)) {
        *n1 = p_add(*n1, n2);
        return;
    }
    if (p_is_one(*d1)) {
        *n1 = p_add(p_mul(*n1, d2), n2);
        *d1 = d2;
        return;
    }
    if (p_is_one(d2)) {
        *n1 = p_add(*n1, p_mul(n2, *d1));
        return;
    }
    P *g = p_gcd(*d1, d2);
    P *a = p_divexact(*d1, g), *b = p_divexact(d2, g);
    if (!a || !b) {
        a = *d1;
        b = d2;
        g = p_const(K->c1);
    }
    *n1 = p_add(p_mul(*n1, b), p_mul(n2, a));
    *d1 = p_mul(a, d2);
}

static E *normal_kernel(E *e);

/* Expression -> rational function num/den in kernels (not cancelled).
 *   sums       combined with rf_add over a common denominator
 *   products   numerators and denominators multiplied
 *   b^k        integer k (|k| <= 100000): powers of the converted base,
 *              numerator and denominator swapped for k < 0
 *   b^(p/q)    (b^(1/q))^|p| in the numerator (p > 0) or denominator (p < 0),
 *              with the base put in rational normal form first, so that
 *              sqrt((x^2-1)/(x-1)) and sqrt(x+1) give the same kernel; if
 *              the kernel simplifies to a non-power the full power is
 *              re-simplified and converted again
 *   b^(-u)     (u not a sum, negative leading coefficient) 1 / kernel b^u
 *   b^(u+v)    split into b^u * b^v so that exp(x+1)/exp(x) cancels; a factor
 *              that does not split (exponent equals the whole sum) is used as
 *              a kernel directly to avoid infinite recursion
 *   functions  kernels with rational-normalised arguments (normal_kernel)
 * Mutually recursive with rat_normal (through normal_kernel); stack_check
 * guards the depth.  Floats set kt->has_float. */
void e2r(E *e, ktab *kt, P **num, P **den) {
    stack_check();
    switch (e->t) {
    case E_INT: case E_RAT:
        *num = p_const(e);
        *den = p_const(K->c1);
        return;
    case E_FLT:
        kt->has_float = 1;
        *num = p_const(e);
        *den = p_const(K->c1);
        return;
    case E_SYM:
        *num = kvar(kt, e);
        *den = p_const(K->c1);
        return;
    case E_ADD: {
        uint8_t *oth = e->n >= 16 ? salloc(e->n) : NULL;
        P *n = oth ? add_dense(e, kt, oth) : NULL, *d = p_const(K->c1);
        if (!n) oth = NULL, n = p_const(K->c0);
        for (uint32_t i = 0; i < e->n; i++) {
            if (oth && !oth[i]) continue;
            P *n2, *d2;
            e2r(EK(e)[i], kt, &n2, &d2);
            rf_add(&n, &d, n2, d2);
        }
        *num = n;
        *den = d;
        return;
    }
    case E_MUL: {
        P *n = p_const(K->c1), *d = p_const(K->c1);
        for (uint32_t i = 0; i < e->n; i++) {
            P *n2, *d2;
            e2r(EK(e)[i], kt, &n2, &d2);
            n = p_mul(n, n2);
            d = p_mul(d, d2);
        }
        *num = n;
        *den = d;
        return;
    }
    case E_POW: {
        E *b = K0(e), *x = K1(e);
        long k;
        if (num_is_small_int(x, &k)) {
            if (k > 100000 || k < -100000) cas_throw(CAS_ERR_UNSUPPORTED, "exponent too large");
            P *bn, *bd;
            e2r(b, kt, &bn, &bd);
            if (k >= 0) {
                *num = p_pow(bn, (uint32_t)k);
                *den = p_pow(bd, (uint32_t)k);
            } else {
                *num = p_pow(bd, (uint32_t)-k);
                *den = p_pow(bn, (uint32_t)-k);
            }
            return;
        }
        if (x->t == E_RAT) {
            E *nb = normal_kernel(b);
            E *pp = K0(x), *q = K1(x);
            long pv;
            if (num_is_small_int(pp, &pv) && pv > -10000 && pv < 10000) {
                E *kern = s_pow(nb, mk_rat(K->c1, q));
                if (kern->t == E_POW) {
                    P *kv = kvar(kt, kern);
                    if (pv >= 0) {
                        *num = p_pow(kv, (uint32_t)pv);
                        *den = p_const(K->c1);
                    } else {
                        *num = p_const(K->c1);
                        *den = p_pow(kv, (uint32_t)-pv);
                    }
                    return;
                }
                /* kernel simplified to something else: recurse on full power */
                E *full = s_pow(nb, x);
                if (!expr_eq(full, e)) {
                    e2r(full, kt, num, den);
                    return;
                }
            }
        }
        /* exponent with negative coefficient: 1/kernel */
        E *nx = normal_kernel(x);
        if (is_negative_term(nx) && nx->t != E_ADD) {
            E *kern = s_pow(normal_kernel(b), s_neg(nx));
            *num = p_const(K->c1);
            *den = kvar(kt, kern);
            return;
        }
        if (nx->t == E_ADD) {
            /* b^(u+v) = b^u * b^v so that exp(x+1)/exp(x) cancels */
            P *n = p_const(K->c1), *d = p_const(K->c1);
            for (uint32_t i = 0; i < nx->n; i++) {
                P *n2, *d2;
                E *f = s_pow(normal_kernel(b), EK(nx)[i]);
                if (f->t == E_POW && expr_eq(K1(f), nx)) {
                    n2 = kvar(kt, f);
                    d2 = p_const(K->c1);
                } else
                    e2r(f, kt, &n2, &d2);
                n = p_mul(n, n2);
                d = p_mul(d, d2);
            }
            *num = n;
            *den = d;
            return;
        }
        *num = kvar(kt, s_pow(normal_kernel(b), nx));
        *den = p_const(K->c1);
        return;
    }
    case E_FN:
        *num = kvar(kt, normal_kernel(e));
        *den = p_const(K->c1);
        return;
    default:
        *num = kvar(kt, e);
        *den = p_const(K->c1);
        return;
    }
}

/* Kernel normalisation for e2r: rat_normal of every argument of a function
 * call, or of a sum, product or power; other nodes unchanged.  This lets
 * kernels that differ only by a rational rewrite (ln((x^2-1)/(x-1)) vs
 * ln(x+1)) be recognised as equal. */
static E *normal_kernel(E *e) {
    if (e->t == E_FN) {
        E **a = salloc(sizeof(E *) * (e->n + 1));
        for (uint32_t i = 0; i < e->n; i++) a[i] = rat_normal(EK(e)[i]);
        return s_fn(e->s, e->n, a);
    }
    if (e->t == E_ADD || e->t == E_MUL || e->t == E_POW) return rat_normal(e);
    return e;
}

/* --------------------------------------------------- public entries --- */
/* expand(): full expansion of products and positive integer powers into a
 * canonical sum of monomials in kernels.  Lists and relations (=, <, >, <=,
 * >=) are mapped elementwise; numbers and symbols are returned unchanged.
 * Negative integer powers are kept as powers of an expanded base (no
 * partial fractions), function arguments are expanded too.  The result is
 * built in scratch.  Cost is dominated by the dense multiplications in
 * p_mul / p_pow; e.g. (x+y+z+1)^20 builds all 1771 monomials. */
E *expand(E *e) {
    stack_check();
    if (e->t == E_LIST) {
        E **a = salloc(sizeof(E *) * (e->n + 1));
        for (uint32_t i = 0; i < e->n; i++) a[i] = expand(EK(e)[i]);
        return s_list(e->n, a);
    }
    if (e->t == E_FN && (e->s == F_EQ || e->s == F_LT || e->s == F_GT || e->s == F_LE || e->s == F_GE)) {
        E **a = salloc(sizeof(E *) * (e->n + 1));
        for (uint32_t i = 0; i < e->n; i++) a[i] = expand(EK(e)[i]);
        return s_fn(e->s, e->n, a);
    }
    if (e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM) return e;
    ktab kt;
    kt_init(&kt);
    P *p = e2p_expand(e, &kt);
    if (!p) return e;
    return p2e(p, &kt);
}

/* num/den with den normalised: integer primitive, positive leading coeff.
 * With cancel set (no floats), the gcd of num and den is divided out first
 * (unless it is trivial or the division fails); then the denominator's
 * rational content and sign are moved into the numerator. */
static void rf_normalize(P **num, P **den, int cancel) {
    if (cancel) {
        P *g = p_gcd(*num, *den);
        if (!p_is_one(g) && !p_is_const(g)) {
            P *a = p_divexact(*num, g), *b = p_divexact(*den, g);
            if (a && b) {
                *num = a;
                *den = b;
            }
        }
    }
    E *cd;
    P *d = p_intprim(*den, &cd);
    *den = d;
    *num = p_scale(*num, num_inv(cd));
}

/* symbols appearing with fractional exponents: x -> T^Q
 * Collects (into syms / qs, at most 8 entries) every symbol that occurs as
 * the base of a rational power anywhere in e, including inside function
 * arguments, together with the lcm of the denominators of its exponents.
 * Symbols beyond the eighth are ignored (they simply stay kernels). */
static void rad_syms(E *e, E **syms, E **qs, uint32_t *n) {
    stack_check();
    if (e->t == E_POW && K0(e)->t == E_SYM && K1(e)->t == E_RAT) {
        E *q = K1(K1(e));
        for (uint32_t i = 0; i < *n; i++)
            if (syms[i] == K0(e) || syms[i]->s == K0(e)->s) {
                qs[i] = int_lcm(qs[i], q);
                return;
            }
        if (*n < 8) {
            syms[*n] = K0(e);
            qs[*n] = q;
            (*n)++;
        }
        return;
    }
    if (e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR) return;
    for (uint32_t i = 0; i < e->n; i++) rad_syms(EK(e)[i], syms, qs, n);
}

/* Substitute x = T^Q in e, re-simplifying on the way up.  x^r (r an exact
 * number) is mapped directly to T^(r*Q) rather than to (T^Q)^r, so that with
 * Q the lcm of the denominators all radicals of x become integer powers of T
 * (no |T| or branch rewriting from simplifying (T^Q)^(1/Q)).  Symbols are
 * compared by id.  Unchanged subtrees are shared.  Also used by integ.c
 * (substitution x = T^m for radical integrands) and solve.c. */
E *subst_rad(E *e, E *x, E *T, E *Q) {
    stack_check();
    if (e->t == E_SYM) return e->s == x->s ? s_pow(T, Q) : e;
    if (e->t == E_POW && K0(e)->t == E_SYM && K0(e)->s == x->s && is_num(K1(e)) && K1(e)->t != E_FLT)
        return s_pow(T, num_mul(K1(e), Q));
    if (e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_STR) return e;
    E **k = salloc(sizeof(E *) * (e->n + 1));
    int ch = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        k[i] = subst_rad(EK(e)[i], x, T, Q);
        if (k[i] != EK(e)[i]) ch = 1;
    }
    return ch ? rebuild(e, k) : e;
}

static E *rat_normal_core(E *e);

/* Rational normal form (normal / ratsimp / together): e as a single fraction
 * num/den of expanded polynomials in kernels with their gcd cancelled and
 * den integral, primitive and with positive leading coefficient.
 *
 * Radicals of symbols are handled by a change of variables: if x occurs as
 * x^(a/q) (q the lcm of all denominators, q <= 24) then x is replaced by T^q
 * with a fresh symbol T = _rq0.._rq7, the expression is normalised as a
 * rational function of T, and T is replaced by x^(1/q) at the end.  This
 * makes (x-1)/(sqrt(x)-1) = (T^2-1)/(T-1) cancel to sqrt(x)+1.  The
 * substitution assumes the principal branch, T = x^(1/q).  Only applied when
 * e is a sum, product or power.  Recursive (through e2r / normal_kernel). */
E *rat_normal(E *e) {
    stack_check();
    if (e->t == E_ADD || e->t == E_MUL || e->t == E_POW) {
        E *syms[8], *qs[8];
        uint32_t n = 0;
        rad_syms(e, syms, qs, &n);
        if (n) {
            E *Ts[8], *backs[8];
            long qv;
            uint32_t m = 0;
            for (uint32_t i = 0; i < n; i++) {
                if (!num_is_small_int(qs[i], &qv) || qv > 24) continue;
                char nm[8] = "_rq0";
                nm[3] = (char)('0' + m);
                Ts[m] = mk_sym_name(nm);
                e = subst_rad(e, syms[i], Ts[m], qs[i]);
                backs[m] = s_pow(syms[i], mk_rat(K->c1, qs[i]));
                m++;
            }
            /* e is now rational in the T's; normalise and substitute back */
            E *r = rat_normal_core(e);
            return m ? subst_many(r, m, Ts, backs) : r;
        }
    }
    return rat_normal_core(e);
}

/* rat_normal without the radical substitution: maps over lists and
 * relations; otherwise e2r, cancel (unless floats are present, since float
 * gcds are meaningless) and rebuild num * den^(-1) with p2e. */
static E *rat_normal_core(E *e) {
    stack_check();
    if (e->t == E_LIST) {
        E **a = salloc(sizeof(E *) * (e->n + 1));
        for (uint32_t i = 0; i < e->n; i++) a[i] = rat_normal(EK(e)[i]);
        return s_list(e->n, a);
    }
    if (e->t == E_FN && (e->s == F_EQ || e->s == F_LT || e->s == F_GT || e->s == F_LE || e->s == F_GE)) {
        E **a = salloc(sizeof(E *) * (e->n + 1));
        for (uint32_t i = 0; i < e->n; i++) a[i] = rat_normal(EK(e)[i]);
        return s_fn(e->s, e->n, a);
    }
    if (e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM) return e;
    ktab kt;
    kt_init(&kt);
    P *n, *d;
    e2r(e, &kt, &n, &d);
    rf_normalize(&n, &d, !kt.has_float);
    E *ne = p2e(n, &kt), *de = p2e(d, &kt);
    if (is_one(de)) return ne;
    return s_mul2(ne, s_pow(de, K->cm1));
}

/* together() is the same as rat_normal. */
E *together(E *e) { return rat_normal(e); }

/* Numerator and denominator of the rational normal form of e, as expanded
 * expressions.  For exact input both have integer coefficients with no
 * common rational content and the denominator has a positive leading
 * coefficient, e.g. (x/2+1/3)/(y) -> num 3x+2, den 6y.  Unlike rat_normal
 * there is no radical substitution and no mapping over lists.  Used widely
 * (solve, integrate, factor, series, ...). */
void numden(E *e, E **num, E **den) {
    ktab kt;
    kt_init(&kt);
    P *n, *d;
    e2r(e, &kt, &n, &d);
    rf_normalize(&n, &d, !kt.has_float);
    if (!kt.has_float) {
        /* move the rational content of the numerator: num/den both integral */
        E *c;
        P *ni = p_intprim(n, &c);
        if (!p_is_zero(n) && !num_is_one(c)) {
            n = p_scale(ni, num_numer(c));
            d = p_scale(d, num_denom(c));
        }
    }
    *num = p2e(n, &kt);
    *den = p2e(d, &kt);
}

/* numer(e) / denom(e): the two halves of numden. */
E *numer_of(E *e) {
    E *n, *d;
    numden(e, &n, &d);
    return n;
}
/* denom(e): see numer_of. */
E *denom_of(E *e) {
    E *n, *d;
    numden(e, &n, &d);
    return d;
}

/* polynomial in x? (e expanded with x as a kernel, other kernels free of x)
 * Builds a fresh kernel table in *kt with x as variable 0 and the expanded
 * polynomial in *p; *xv is x's variable index.  Returns 0 when some other
 * kernel depends on x (e.g. sin(x), 1/x, sqrt(x)). */
static int poly_in(E *e, E *x, ktab *kt, P **p, int *xv) {
    kt_init(kt);
    *xv = kt_index(kt, x);
    *p = e2p_expand(e, kt);
    if (!*p) return 0;
    for (uint32_t i = 0; i < kt->n; i++)
        if ((int)i != *xv && !freeof(kt->k[i], x)) return 0;
    return 1;
}

/* degree(e, x): returns 1 and stores the degree (-1 for e = 0) if e is a
 * polynomial in x, else returns 0 and leaves *deg untouched. */
int poly_degree_in(E *e, E *x, long *deg) {
    ktab kt;
    P *p;
    int xv;
    if (!poly_in(e, x, &kt, &p, &xv)) return 0;
    *deg = p_deg(p, xv);
    return 1;
}

/* coeff(e, x, k): coefficient of x^k as an expanded expression (0 when k
 * is out of range).  Throws CAS_ERR_DOMAIN if e is not polynomial in x. */
E *poly_coeff(E *e, E *x, long k) {
    ktab kt;
    P *p;
    int xv;
    if (!poly_in(e, x, &kt, &p, &xv)) cas_throw(CAS_ERR_DOMAIN, "not a polynomial");
    return p2e(p_coef(p, xv, k), &kt);
}

/* coeffs(e, x): LIST of all coefficients in descending order of powers,
 * [lc, ..., c0] (the zero polynomial gives [0]); the degree (>= 0) is
 * stored in *deg when deg is non-NULL.  Throws CAS_ERR_DOMAIN if e is not
 * polynomial in x. */
E *poly_coeff_list(E *e, E *x, long *deg) {
    ktab kt;
    P *p;
    int xv;
    if (!poly_in(e, x, &kt, &p, &xv)) cas_throw(CAS_ERR_DOMAIN, "not a polynomial");
    long d = p_deg(p, xv);
    if (d < 0) d = 0;
    E **c = salloc(sizeof(E *) * (d + 1));
    for (long i = 0; i <= d; i++) c[d - i] = p2e(p_coef(p, xv, i), &kt);
    if (deg) *deg = d;
    return s_list((uint32_t)(d + 1), c);
}

/* collect(e, x): e as sum c_i * x^i with each coefficient c_i expanded and
 * kept together.  The result is assembled from raw nodes flagged F_SIMP so
 * that automatic simplification does not distribute or re-sort it: a sum
 * coefficient stays grouped as (a+b)*x^i, and the terms appear in ascending
 * powers of x.  For a rational function the numerator is collected and
 * divided by the denominator; an expression that is not even rational in x
 * (e.g. containing sin(x)) is returned via that path essentially unchanged. */
E *collect_expr(E *e, E *x) {
    stack_check();
    ktab kt;
    P *p;
    int xv;
    if (!poly_in(e, x, &kt, &p, &xv)) {
        /* rational: collect numerator */
        E *n, *d;
        numden(e, &n, &d);
        if (is_one(d)) return e;
        return s_div(collect_expr(n, x), d);
    }
    long d = p_deg(p, xv);
    if (d < 0) return K->c0;
    E **t = salloc(sizeof(E *) * (d + 1));
    uint32_t nt = 0;
    for (long i = 0; i <= d; i++) {
        P *c = p_coef(p, xv, i);
        if (p_is_zero(c)) continue;
        E *ce = p2e(c, &kt);
        E *xi = s_pow(x, mk_int(i));
        if (ce->t == E_ADD && i > 0) {
            E *k2[2] = {ce, xi};
            E *m = enode(E_MUL, 2);
            EK(m)[0] = k2[0];
            EK(m)[1] = k2[1];
            m->f = F_SIMP;
            t[nt++] = m;
        } else
            t[nt++] = s_mul2(ce, xi);
    }
    if (nt == 1) return t[0];
    E *r = enode(E_ADD, nt);
    for (uint32_t i = 0; i < nt; i++) EK(r)[i] = t[i];
    r->f = F_SIMP;
    return r;
}

/* univariate division in x with coefficients rational functions of the rest
 *
 * quo(a, b, x) / rem: classical long division (Knuth 4.6.1 Algorithm D) on
 * the coefficient lists of a and b in x; coefficients are arbitrary
 * expressions in the other variables and every step is put in rational
 * normal form, so a = q*b + r holds over Q(other variables).  Returns the
 * expanded quotient (0 when deg a < deg b) and stores the expanded remainder
 * in *rem if rem is non-NULL.  Throws CAS_ERR_DOMAIN for b = 0 or non-
 * polynomial inputs (through poly_coeff_list). */
E *poly_quo_rem(E *a, E *b, E *x, E **rem) {
    long da, db;
    E *ca = poly_coeff_list(a, x, &da), *cb = poly_coeff_list(b, x, &db);
    /* ascending arrays */
    E **r = salloc(sizeof(E *) * (da + 1));
    for (long i = 0; i <= da; i++) r[i] = EK(ca)[da - i];
    E **bb = salloc(sizeof(E *) * (db + 1));
    for (long i = 0; i <= db; i++) bb[i] = EK(cb)[db - i];
    while (db > 0 && is_zero(bb[db])) db--;
    if (db == 0 && is_zero(bb[0])) cas_throw(CAS_ERR_DOMAIN, "division by zero polynomial");
    long dq = da - db;
    E **q = salloc(sizeof(E *) * (dq > 0 ? dq + 1 : 1));
    for (long i = 0; i <= dq; i++) q[i] = K->c0;
    for (long i = da; i >= db; i--) {
        if (is_zero(r[i])) continue;
        E *t = rat_normal(s_div(r[i], bb[db]));
        q[i - db] = t;
        for (long j = 0; j <= db; j++) r[i - db + j] = rat_normal(s_sub(r[i - db + j], s_mul2(t, bb[j])));
    }
    E **qt = salloc(sizeof(E *) * (dq > 0 ? dq + 2 : 2)); /* dq < -1 when deg a < deg b - 1 */
    uint32_t nq = 0;
    for (long i = 0; i <= dq; i++) qt[nq++] = s_mul2(q[i], s_pow(x, mk_int(i)));
    E **rt = salloc(sizeof(E *) * (db + 1));
    uint32_t nr = 0;
    for (long i = 0; i < db && i <= da; i++) rt[nr++] = s_mul2(r[i], s_pow(x, mk_int(i)));
    if (rem) *rem = expand(s_add(nr, rt));
    return dq < 0 ? K->c0 : expand(s_add(nq, qt));
}

/* gcd(a, b) for polynomial expressions.  Numbers: gcd of integers, and for
 * rationals gcd(numerators) / lcm(denominators).  Polynomials: the gcd of
 * the rational contents times the primitive polynomial gcd (p_gcd), e.g.
 * gcd(4x+4, 6x^2-6) = 2*(x+1).  Throws CAS_ERR_DOMAIN when the conversion
 * reports a non-polynomial (which the current e2p never does: non-polynomial
 * parts are treated as kernels). */
E *poly_gcd_expr(E *a, E *b) {
    stack_check();
    if (is_exactnum(a) && is_exactnum(b)) {
        if (a->t == E_INT && b->t == E_INT) return int_gcd(a, b);
        return mk_rat(int_gcd(num_numer(a), num_numer(b)), int_lcm(num_denom(a), num_denom(b)));
    }
    ktab kt;
    kt_init(&kt);
    P *pa = e2p(a, &kt), *pb = e2p(b, &kt);
    if (!pa || !pb) cas_throw(CAS_ERR_DOMAIN, "gcd: not polynomials");
    E *ca = p_numcontent(pa), *cb = p_numcontent(pb);
    E *cg = poly_gcd_expr(ca, cb);
    P *g = p_gcd(pa, pb);
    return s_mul2(cg, p2e(g, &kt));
}

/* lcm(a, b) = a*b / gcd(a, b), normalised and expanded; integer lcm for two
 * integers, 0 if the gcd is 0. */
E *poly_lcm_expr(E *a, E *b) {
    if (is_exactnum(a) && is_exactnum(b)) {
        if (a->t == E_INT && b->t == E_INT) return int_lcm(a, b);
    }
    E *g = poly_gcd_expr(a, b);
    if (is_zero(g)) return K->c0;
    return expand(rat_normal(s_div(s_mul2(a, b), g)));
}
