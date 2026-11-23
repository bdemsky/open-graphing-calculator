/*
 * simp.c -- automatic simplification into canonical form.
 *
 * Every expression the CAS computes with is kept in a canonical form, so
 * that structurally equal (expr_eq) means "the same after the obvious
 * rewritings".  The design follows the "automatic simplification" of Cohen,
 * "Computer Algebra and Symbolic Computation: Elementary Algorithms", ch. 3
 * (algorithms Simplify_sum / Simplify_product / Simplify_power /
 * Simplify_function), with the order relation implemented by expr_cmp
 * (expr.c).  The rules applied are:
 *
 *  sums:      flattened, numeric terms folded, like terms collected
 *             (coefficient * rest), numeric multiples of sums distributed,
 *             operands sorted by the canonical order; +-inf / undef rules.
 *  products:  flattened, numeric factors folded, equal bases combined by
 *             adding exponents, c*(a+b) with numeric c distributed, Gaussian
 *             (complex rational) factors multiplied out, integer radicals
 *             with a common index merged (sqrt(2)*sqrt(3) = sqrt(6)),
 *             a^u*b^u = (ab)^u for positive numeric a, b.
 *  powers:    exact integer/rational powers of numbers (radicals reduced to
 *             A * B^(s/t) with B not a perfect power), (x^a)^n -> x^(a n)
 *             for integer n, (xy)^n -> x^n y^n, i^n reduced, exp(ln u) = u.
 *  functions: exact special values (trig at rational multiples of pi with
 *             denominators 1,2,3,4,5,6,8,10,12; inverse trig tables; gamma at
 *             half-integers; zeta at even and negative integers), parity,
 *             periodic shifts by multiples of pi/2, inverse-function
 *             cancellation, log/log10/log2 -> ln quotients, numeric
 *             evaluation when an argument is a float.
 *
 * Representation invariants established here (and relied on everywhere):
 *  - E_ADD: >= 2 operands, none of them a sum, at most one number (first,
 *    since numbers sort first), no two terms with the same non-numeric part;
 *  - E_MUL: >= 2 operands, none a product, at most one number (first; the
 *    exact integer 1 is dropped, a float 1.0 is kept to mark the product as
 *    approximate), bases pairwise distinct;
 *  - x - y is x + (-1)*y, x / y is x * y^(-1), sqrt(x) is x^(1/2), exp(x)
 *    is e^x;
 *  - every node built by an s_* constructor carries F_SIMP, so simp() of an
 *    already simplified tree costs O(1).
 *
 * Inputs: the s_* constructors assume their ARGUMENTS are already simplified
 * (canonical); they only re-simplify the top level.  simp() simplifies a raw
 * tree (from parse.c or the mk_* constructors) bottom-up.  Everything is
 * allocated in scratch; nothing here touches the persistent heap.
 *
 * Errors: domain errors (division by zero, poles of tan/cot/sec/csc/gamma/
 * zeta, wrong number of arguments, list size mismatch) are raised with
 * cas_throw(CAS_ERR_DOMAIN); huge factorials raise CAS_ERR_MEMORY.
 *
 * Real-variable conventions: symbols are treated as real where a rule
 * needs it (ln(e^u) = u, (e^u)^r = e^(ur), re/im/conj); sign information
 * comes from sign_of(), which uses numeric evaluation for constants and the
 * assume() flags of symbols.
 *
 * Callers: practically every module builds expressions through s_add,
 * s_mul, s_pow, s_fn and friends; eval.c calls simp() on parsed input and
 * rebuild() after evaluating children; subst() is the generic substitution.
 */
#include "cas_int.h"
#include "casx.h"
#include <math.h>
#include <string.h>

/* Defined in numeric.c (double evaluation of builtin functions) and linalg.c
 * (matrix product of nested lists); declared here to keep cas_int.h lean. */
int num_fn_double(int id, int n, const double *x, double *out);
E *mat_mul_lists(E *a, E *b);

/* -------------------------------------------------------------- sort --- */
/*
 * Stable merge sort of a[0..n-1] with comparison cmp, using tmp (n slots) as
 * merge buffer; insertion sort below 9 elements.  O(n log n) comparisons,
 * which matters because expr_cmp itself can be expensive.  Stability keeps
 * equal-comparing elements in input order (needed by callers that sort by
 * a partial key, e.g. cmp_be).
 */
static void msort_rec(E **a, E **tmp, uint32_t n, int (*cmp)(E *, E *)) {
    if (n < 2) return;
    if (n <= 8) {
        for (uint32_t i = 1; i < n; i++)
            for (uint32_t j = i; j > 0 && cmp(a[j - 1], a[j]) > 0; j--) {
                E *t = a[j]; a[j] = a[j - 1]; a[j - 1] = t;
            }
        return;
    }
    uint32_t m = n / 2;
    msort_rec(a, tmp, m, cmp);
    msort_rec(a + m, tmp, n - m, cmp);
    uint32_t i = 0, j = m, k = 0;
    while (i < m && j < n) tmp[k++] = cmp(a[i], a[j]) <= 0 ? a[i++] : a[j++];
    while (i < m) tmp[k++] = a[i++];
    while (j < n) tmp[k++] = a[j++];
    memcpy(a, tmp, sizeof(E *) * n);
}

/*
 * Sort an array of expression pointers in place (stable).  The temporary
 * buffer is taken from scratch and released before returning, so the call
 * leaves scratch usage unchanged.  Used for canonical operand order here and
 * by many other modules.
 */
void esort(E **a, uint32_t n, int (*cmp)(E *, E *)) {
    if (n < 2) return;
    char *mark = smark();
    E **tmp = salloc(sizeof(E *) * n);
    msort_rec(a, tmp, n, cmp);
    srelease(mark);
}

/* ------------------------------------------------------------ helpers --- */
/* is_zero: exact 0 or float 0.0.  is_one: exact integer 1 only (1.0 is not).
 * is_int_val: e is a number equal to the machine integer v. */
int is_zero(E *e) { return is_num(e) && num_is_zero(e); }
int is_one(E *e) { return e->t == E_INT && int_is_one(e); }
/* e is a number whose value is the machine integer v (see num_is_small_int). */
int is_int_val(E *e, long v) {
    long x;
    return num_is_small_int(e, &x) && x == v;
}

/*
 * Allocate a node of type t with the n children k[] and mark it F_SIMP
 * WITHOUT simplifying anything: the caller guarantees that the operands are
 * already canonical (sorted, collected, ...).  This is how the s_*
 * constructors emit their final result.
 */
static E *build(int t, uint32_t n, E **k) {
    E *e = enode(t, n);
    for (uint32_t i = 0; i < n; i++) EK(e)[i] = k[i];
    e->f = F_SIMP;
    return e;
}

/* -inf, represented canonically as the product (-1)*inf. */
static E *m_inf(void) {
    E *k[2] = {K->cm1, K->inf_};
    return build(E_MUL, 2, k);
}

/*
 * Split a canonical term t into numeric coefficient and the rest, so that
 * t = coef * rest:  3*x*y -> (3, x*y),  x -> (1, x),  5 -> (5, 1).
 * Relies on the canonical product having its number (if any) first.  May
 * allocate a new product node for the rest (in scratch).
 */
void split_coef(E *t, E **coef, E **rest) {
    if (t->t == E_MUL && is_num(K0(t))) {
        *coef = K0(t);
        if (t->n == 2)
            *rest = K1(t);
        else
            *rest = build(E_MUL, t->n - 1, EK(t) + 1);
    } else if (is_num(t)) {
        *coef = t;
        *rest = K->c1;
    } else {
        *coef = K->c1;
        *rest = t;
    }
}

/*
 * "Syntactically negative": a negative number, a product with a negative
 * leading coefficient, or a sum whose LAST (highest-ordered) term is
 * negative.  Used to normalise signs (sin(-x) = -sin(x), printing).  For a
 * sum, negation flips the sign of every term but keeps the term order, so
 * s_neg of a negative-term sum is never negative-term: rules that recurse on
 * s_neg(x) terminate.
 */
int is_negative_term(E *e) {
    stack_check();
    switch (e->t) {
    case E_INT: case E_RAT: case E_FLT: return num_sgn(e) < 0;
    case E_MUL: return is_num(K0(e)) && num_sgn(K0(e)) < 0;
    case E_ADD: return is_negative_term(EK(e)[e->n - 1]);
    default: return 0;
    }
}

/* undef is absorbing: almost every constructor returns undef if an operand is. */
static int is_undef(E *e) { return ISYM(e, S_UNDEF); }

/*
 * 1 if e is known to be an integer: integer literals, symbols with
 * assume(s, integer), and sums, products and non-negative integer powers of
 * such.  Purely syntactic (0 means "unknown", not "non-integer").
 */
int is_int_assumed(E *e) {
    stack_check();
    if (e->t == E_INT) return 1;
    if (e->t == E_SYM) return (K->syms[e->s].assume & A_INT) != 0;
    if (e->t == E_ADD || e->t == E_MUL) {
        for (uint32_t i = 0; i < e->n; i++)
            if (!is_int_assumed(EK(e)[i])) return 0;
        return 1;
    }
    if (e->t == E_POW) {
        long k;
        return num_is_small_int(K1(e), &k) && k >= 0 && is_int_assumed(K0(e));
    }
    return 0;
}

/*
 * Known sign of a real expression: +1, -1, or 0 (unknown -- never "zero").
 * Sources of information:
 *   - numbers; pi, e, euler_gamma > 0; assume() flags of symbols;
 *   - closed constants (is_const_expr): evaluated in complex double
 *     precision; accepted only if the imaginary part is negligible
 *     (relative 1e-12) and |re| > 1e-200, so a constant that is really 0 or
 *     non-real gives "unknown";
 *   - products: product of the factor signs (unknown if any is unknown);
 *     sums: the common sign if all terms agree;
 *   - powers: e^u > 0 and b^u > 0 for b > 0 when u is free of i; a negative
 *     base with an integer exponent by parity, and in real mode with an
 *     exponent p/q, q odd, by the parity of p (real_odd_root);
 *   - cosh > 0; sinh, tanh, atan, asinh, erf have the sign of the argument.
 * Recursive (stack_check); no allocation except inside eval_complex.
 */
static int sign_of0(E *e);

/* assume.c: relational facts and intervals decide what the flags do not */
int sign_of(E *e) {
    int s = sign_of0(e);
    return s ? s : facts_sign(e);
}

static int sign_of0(E *e) {
    stack_check();
    if (is_num(e)) return num_sgn(e);
    if (e->t == E_SYM) {
        if (e->s == S_PI || e->s == S_E || e->s == S_EULERG) return 1;
        uint8_t a = K->syms[e->s].assume;
        if (a & A_POS) return 1;
        if (a & A_NEG) return -1;
        return 0;
    }
    if (is_const_expr(e)) {
        double re, im;
        if (!eval_complex(e, &re, &im)) return 0;
        double mag = fabs(re) + fabs(im);
        if (fabs(im) > 1e-12 * (mag + 1e-300)) return 0;
        return re > 1e-200 ? 1 : re < -1e-200 ? -1 : 0;
    }
    switch (e->t) {
    case E_MUL: {
        int s = 1;
        for (uint32_t i = 0; i < e->n; i++) {
            int t = sign_of(EK(e)[i]);
            if (!t) return 0;
            s *= t;
        }
        return s;
    }
    case E_ADD: {
        int s = sign_of(EK(e)[0]);
        if (!s) return 0;
        for (uint32_t i = 1; i < e->n; i++)
            if (sign_of(EK(e)[i]) != s) return 0;
        return s;
    }
    case E_POW: {
        E *b = K0(e), *x = K1(e);
        if (ISYM(b, S_E)) return freeof_sym(x, S_I) ? 1 : 0;
        int sb = sign_of(b), podd;
        if (sb > 0 && freeof_sym(x, S_I)) return 1;
        if (sb < 0 && x->t == E_INT) return int_is_even(x) ? 1 : -1;
        if (sb < 0 && real_odd_root(x, &podd)) return podd ? -1 : 1; /* real root (real mode) */
        return 0;
    }
    case E_FN:
        if (e->n == 1) {
            int sa = sign_of(K0(e));
            switch (e->s) {
            case F_COSH: return 1;
            case F_SINH: case F_TANH: case F_ATAN: case F_ASINH: case F_ERF: return sa;
            case F_LN: return 0;
            default: return 0;
            }
        }
        return 0;
    default: return 0;
    }
}

/* Known positive / known negative (0 from sign_of means unknown). */
int is_poslit(E *e) { return sign_of(e) > 0; }

static int is_neglit(E *e) { return sign_of(e) < 0; }

/* -------------------------------------------------------------- lists --- */
/*
 * Elementwise sum (op 0) or product (op 1) where at least one of a, b is a
 * list; a scalar operand is broadcast.  Elements are combined with
 * s_add2 / s_mul2, so nested lists (matrices) recurse naturally.  Throws
 * CAS_ERR_DOMAIN when both are lists of different lengths.
 */
static E *list_elementwise(int op, E *a, E *b) {
    /* op: 0 add, 1 mul */
    uint32_t n = a->t == E_LIST ? a->n : b->n;
    if (a->t == E_LIST && b->t == E_LIST && a->n != b->n) cas_throw(CAS_ERR_DOMAIN, "list size mismatch");
    E **v = salloc(sizeof(E *) * n);
    for (uint32_t i = 0; i < n; i++) {
        E *x = a->t == E_LIST ? EK(a)[i] : a;
        E *y = b->t == E_LIST ? EK(b)[i] : b;
        v[i] = op == 0 ? s_add2(x, y) : s_mul2(x, y);
    }
    return build(E_LIST, n, v);
}

/* List of already simplified elements (lists are not reordered). */
E *s_list(uint32_t n, E **a) { return build(E_LIST, n, a); }

/* ---------------------------------------------------------------- sum --- */
/* A term of a sum split as c * r: c a number, r the canonical non-numeric
 * rest (1 for a pure number).  Like terms are those with equal r. */
typedef struct {
    E *c, *r;
} term;

/* Order terms by their non-numeric part, so like terms become adjacent. */
static int term_cmp(const term *a, const term *b) { return expr_cmp(a->r, b->r); }

/*
 * Stable merge sort of terms by term_cmp (insertion sort below 9 elements).
 * The merge buffers are taken from scratch and not released; they become
 * garbage of the enclosing s_add call (O(n log n) words in total).
 */
static void tsort(term *a, uint32_t n) {
    stack_check();
    /* merge sort on terms */
    if (n < 2) return;
    if (n <= 8) {
        for (uint32_t i = 1; i < n; i++)
            for (uint32_t j = i; j > 0 && term_cmp(&a[j - 1], &a[j]) > 0; j--) {
                term t = a[j]; a[j] = a[j - 1]; a[j - 1] = t;
            }
        return;
    }
    uint32_t m = n / 2;
    tsort(a, m);
    tsort(a + m, n - m);
    term *tmp = salloc(sizeof(term) * n);
    uint32_t i = 0, j = m, k = 0;
    while (i < m && j < n) tmp[k++] = term_cmp(&a[i], &a[j]) <= 0 ? a[i++] : a[j++];
    while (i < m) tmp[k++] = a[i++];
    while (j < n) tmp[k++] = a[j++];
    memcpy(a, tmp, sizeof(term) * n);
}

/*
 * Rebuild the canonical term c*r from a nonzero numeric coefficient and a
 * canonical rest (inverse of split_coef): 1*r = r, c*1 = c, and if r is a
 * product the coefficient is prepended to its factor list (numbers sort
 * first, so the result is still sorted).
 */
static E *mk_term(E *c, E *r) {
    if (is_one(r)) return c;
    if (is_one(c)) return r;
    if (r->t == E_MUL) {
        E *e = enode(E_MUL, r->n + 1);
        K0(e) = c;
        memcpy(EK(e) + 1, EK(r), sizeof(E *) * r->n);
        e->f = F_SIMP;
        return e;
    }
    E *k[2] = {c, r};
    return build(E_MUL, 2, k);
}

/*
 * Arithmetic on equations applies to both sides: (a=b)+c -> a+c=b+c,
 * (a=b)*(c=d) -> a*c=b*d (op 0 sum, op 1 product).  Non-equation operands are
 * used on both sides.  Returns the F_SIMP equation node (built directly, not
 * via s_fn, so no further equation rules apply).
 */
static E *eq_map(int op, uint32_t n, E **a) {
    E **l = salloc(sizeof(E *) * n), **r = salloc(sizeof(E *) * n);
    for (uint32_t i = 0; i < n; i++) {
        if (IFN(a[i], F_EQ) && a[i]->n == 2) {
            l[i] = K0(a[i]);
            r[i] = K1(a[i]);
        } else
            l[i] = r[i] = a[i];
    }
    E *lv = op ? s_mul(n, l) : s_add(n, l), *rv = op ? s_mul(n, r) : s_add(n, r);
    E *e = enode(E_FN, 2);
    e->s = F_EQ;
    K0(e) = lv;
    K1(e) = rv;
    e->f = F_SIMP;
    return e;
}

/* Is any of the n operands an equation a = b? */
static int has_eq_arg(uint32_t n, E **a) {
    for (uint32_t i = 0; i < n; i++)
        if (IFN(a[i], F_EQ) && a[i]->n == 2) return 1;
    return 0;
}

/*
 * Canonical sum of n already simplified operands (Cohen's Simplify_sum).
 *
 * Steps:
 *  1. trivial cases (0 or 1 operand); equations distribute over the sum
 *     (eq_map); any list operand makes the whole sum elementwise;
 *  2. undef anywhere -> undef;
 *  3. flatten nested sums one level (operands are canonical, so their own
 *     operands are not sums) and split every term into (coef, rest):
 *     numbers are folded into the constant c, +-c*inf terms only record
 *     the sign of the infinity, and c*(a+b) with numeric c is distributed
 *     term by term (so 2*(x+1) + x -> 3*x + 2);
 *  4. inf + (-inf) -> undef; otherwise an infinity absorbs every finite
 *     term (x + inf -> inf; no sign analysis of x is attempted);
 *  5. sort terms by their rest and add the coefficients of equal rests;
 *     terms whose coefficient becomes 0 disappear;
 *  6. prepend the constant (if nonzero) and sort once more with expr_cmp,
 *     because the canonical order of c*r is not exactly the order of r.
 * Result: a number, a single term, or an F_SIMP E_ADD node in scratch.
 * Cost O(N log N) comparisons for N flattened terms.
 */
/* t = k*ln(q)*R with an integer k (|k| <= 64), a whole number q >= 2 and R
 * the rest (1 for a plain k*ln(q); 1/ln(10) for log(q)): 1 and the parts */
static int ln_term(E *t, int64_t *k, E **q, E **R) {
    E *co, *r;
    split_coef(t, &co, &r);
    if (co->t != E_INT || !int_fits(co, k) || *k > 64 || *k < -64) return 0;
    E *l = NULL;
    if (IFN(r, F_LN)) l = r;
    else if (r->t == E_MUL)
        for (uint32_t i = 0; i < r->n; i++)
            if (IFN(EK(r)[i], F_LN)) {
                if (l) return 0; /* two logarithms: a product of them, not a log */
                l = EK(r)[i];
            }
    if (!l || l->n != 1 || K0(l)->t != E_INT || num_sgn(K0(l)) <= 0 || is_one(K0(l))) return 0;
    *q = K0(l);
    if (l == r) {
        *R = K->c1;
    } else {
        E **f = salloc(sizeof(E *) * r->n);
        uint32_t m = 0;
        for (uint32_t i = 0; i < r->n; i++)
            if (EK(r)[i] != l) f[m++] = EK(r)[i];
        *R = m == 1 ? f[0] : s_mul(m, f);
    }
    return 1;
}

/* Logarithms of whole numbers that cancel: in the terms out[0..*o), the
 * terms k_i*ln(q_i)*R with integers k_i, whole q_i >= 2 and the same rest R,
 * when there are two or more, at least one subtracted, and the product P of
 * the q_i^k_i is a whole number or its reciprocal, are removed from out and
 * their sum ln(P)*R (or -ln(1/P)*R) returned: ln(10)-ln(2)-ln(5) = 0,
 * ln(6)-ln(2) = ln(3), ln(12)-ln(3) = 2*ln(2), log(10)-log(2)-log(5) = 1.
 * Sums with no minus (ln(2)+ln(3)) and quotients that are not whole
 * (ln(2)-ln(3)) stay as they are. NULL when nothing collapses (the first
 * group of the same R that collapses is done; s_add runs again on the rest). */
static E *ln_collapse(E **out, uint32_t *o) {
    for (uint32_t g = 0; g < *o; g++) {
        int64_t k;
        E *q, *R;
        if (!ln_term(out[g], &k, &q, &R)) continue;
        uint32_t nl = 0;
        int neg = 0;
        E *P = K->c1;
        for (uint32_t i = g; i < *o; i++) {
            int64_t k2;
            E *q2, *R2;
            if (!ln_term(out[i], &k2, &q2, &R2) || !expr_eq(R2, R)) continue;
            nl++;
            neg |= k2 < 0;
            P = num_mul(P, num_pow_int(q2, mk_int(k2)));
        }
        if (nl < 2 || !neg) continue;
        E *res;
        if (P->t == E_INT) res = s_ln(P);
        else if (P->t == E_RAT && is_one(K0(P))) res = s_neg(s_ln(K1(P)));
        else continue;
        uint32_t m = 0;
        for (uint32_t i = 0; i < *o; i++) {
            int64_t k2;
            E *q2, *R2;
            if (i >= g && ln_term(out[i], &k2, &q2, &R2) && expr_eq(R2, R)) continue;
            out[m++] = out[i];
        }
        *o = m;
        return s_mul2(res, R);
    }
    return NULL;
}

E *s_add(uint32_t n, E **a) {
    if (n == 0) return K->c0;
    if (n == 1) return a[0];
    stack_check();
    if (has_eq_arg(n, a)) return eq_map(0, n, a);
    /* lists */
    for (uint32_t i = 0; i < n; i++)
        if (a[i]->t == E_LIST) {
            E *acc = a[0];
            for (uint32_t j = 1; j < n; j++) {
                if (acc->t == E_LIST || a[j]->t == E_LIST)
                    acc = list_elementwise(0, acc, a[j]);
                else
                    acc = s_add2(acc, a[j]);
            }
            return acc;
        }
    uint32_t cnt = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (is_undef(a[i])) return K->undef_;
        cnt += a[i]->t == E_ADD ? a[i]->n : 1;
    }
    term *ts = salloc(sizeof(term) * (cnt + 1));
    uint32_t m = 0;
    E *c = K->c0;
    int infs = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t kn = a[i]->t == E_ADD ? a[i]->n : 1;
        E **kk = a[i]->t == E_ADD ? EK(a[i]) : &a[i];
        for (uint32_t j = 0; j < kn; j++) {
            E *t = kk[j];
            if (is_num(t)) {
                c = num_add(c, t);
                continue;
            }
            E *co, *re;
            split_coef(t, &co, &re);
            if (ISYM(re, S_INF)) {
                int sg = num_sgn(co);
                if (sg > 0) infs |= 1;
                else if (sg < 0) infs |= 2;
                continue;
            }
                /* The term is c*(t1 + t2 + ...): grow the term array (the old
                 * one becomes garbage) and add c*t_i for each summand. */
            if (re->t == E_ADD) {
                /* distribute numeric coefficient over a sum */
                term *nts = salloc(sizeof(term) * (cnt + re->n + 1));
                memcpy(nts, ts, sizeof(term) * m);
                ts = nts;
                cnt += re->n;
                for (uint32_t q = 0; q < re->n; q++) {
                    E *t2 = EK(re)[q], *c2, *r2;
                    if (is_num(t2)) {
                        c = num_add(c, num_mul(co, t2));
                        continue;
                    }
                    split_coef(t2, &c2, &r2);
                    ts[m].c = num_mul(co, c2);
                    ts[m].r = r2;
                    m++;
                }
                continue;
            }
            ts[m].c = co;
            ts[m].r = re;
            m++;
        }
    }
    if (infs == 3) return K->undef_;
    if (infs == 1) return K->inf_;
    if (infs == 2) return m_inf();
    tsort(ts, m);
    E **out = salloc(sizeof(E *) * (m + 1));
    uint32_t o = 0;
    for (uint32_t i = 0; i < m;) {
        uint32_t j = i + 1;
        E *co = ts[i].c;
        while (j < m && expr_eq(ts[j].r, ts[i].r)) co = num_add(co, ts[j++].c);
        if (!num_is_zero(co)) out[o++] = mk_term(co, ts[i].r);
        i = j;
    }
    {
        E *r = ln_collapse(out, &o);
        if (r) {
            out[o++] = r;
            if (!num_is_zero(c)) out[o++] = c;
            return s_add(o, out);
        }
    }
    int cz = num_is_zero(c);
    if (o == 0) return c;
    if (cz && o == 1) return out[0];
    E **fin = salloc(sizeof(E *) * (o + 1));
    uint32_t f = 0;
    if (!cz) fin[f++] = c;
    memcpy(fin + f, out, sizeof(E *) * o);
    f += o;
    esort(fin, f, expr_cmp);
    return build(E_ADD, f, fin);
}

/* Binary sum, with a fast path for two numbers. */
E *s_add2(E *a, E *b) {
    if (is_num(a) && is_num(b)) return num_add(a, b);
    E *v[2] = {a, b};
    return s_add(2, v);
}
/* a - b = a + (-1)*b.  s_neg: -a, folding numbers, else the product (-1)*a. */
E *s_sub(E *a, E *b) { return s_add2(a, s_neg(b)); }
/* -a: numbers are negated directly, anything else becomes (-1)*a. */
E *s_neg(E *a) {
    if (is_num(a)) return num_neg(a);
    return s_mul2(K->cm1, a);
}

static E *pow_node(E *b, E *e);

/* ------------------------------------------------- gaussian numbers --- */
/*
 * Gaussian (complex numeric) values are not a node type: a + b i is the
 * canonical sum a + b*i of numbers and the symbol i.  gauss_parts tests
 * whether e has that shape (a number, i, c*i, or a + c*i with a, c numbers)
 * and if so returns 1 with *re = a, *im = c.
 */
static int gauss_parts(E *e, E **re, E **im) {
    stack_check();
    if (is_num(e)) {
        *re = e;
        *im = K->c0;
        return 1;
    }
    if (ISYM(e, S_I)) {
        *re = K->c0;
        *im = K->c1;
        return 1;
    }
    if (e->t == E_MUL && e->n == 2 && is_num(K0(e)) && ISYM(K1(e), S_I)) {
        *re = K->c0;
        *im = K0(e);
        return 1;
    }
    if (e->t == E_ADD && e->n == 2 && is_num(K0(e))) {
        E *r2, *i2;
        if (gauss_parts(K1(e), &r2, &i2) && num_is_zero(r2)) {
            *re = K0(e);
            *im = i2;
            return 1;
        }
    }
    return 0;
}

/* A Gaussian number with nonzero symbolic i-part, i.e. one of i, c*i, a+c*i
 * (plain real numbers excluded). */
static int is_gauss_nonreal(E *e) {
    E *r, *i;
    return !is_num(e) && gauss_parts(e, &r, &i);
}

/*
 * Canonical node for re + im*i (re, im numbers): drops an exact zero real or
 * imaginary part (a float 0.0 is kept so the result stays approximate) and
 * writes 1*i as i.  The parts are assembled with build() because their order
 * (number, then c*i) is already canonical.
 */
static E *gauss_make(E *re, E *im) {
    if (num_is_zero(im) && im->t != E_FLT) return re;
    E *t = num_is_one(im) ? K->i_ : NULL;
    if (!t) {
        E *k[2] = {im, K->i_};
        t = build(E_MUL, 2, k);
    }
    if (num_is_zero(re) && re->t != E_FLT) return t;
    E *k[2] = {re, t};
    return build(E_ADD, 2, k);
}

/* (a + b i)(c + d i) = (ac - bd) + (ad + bc) i on numeric parts. */
static void gauss_mul(E *a, E *b, E *c, E *d, E **re, E **im) {
    *re = num_sub(num_mul(a, c), num_mul(b, d));
    *im = num_add(num_mul(a, d), num_mul(b, c));
}

/*
 * (re + im i)^k for an integer k with |k| <= 100000 by binary
 * exponentiation (O(log k) Gaussian multiplications); a negative k first
 * inverts: 1/(a + b i) = (a - b i)/(a^2 + b^2).  Returns NULL (caller keeps
 * the power unevaluated) if k is too large or not a small integer.
 */
static E *gauss_pow(E *re, E *im, E *k) {
    long kv;
    if (!num_is_small_int(k, &kv) || kv > 100000 || kv < -100000) return NULL;
    if (kv < 0) {
        E *den = num_add(num_mul(re, re), num_mul(im, im));
        re = num_div(re, den);
        im = num_neg(num_div(im, den));
        kv = -kv;
    }
    E *rr = K->c1, *ri = K->c0;
    while (kv) {
        if (kv & 1) gauss_mul(rr, ri, re, im, &rr, &ri);
        kv >>= 1;
        if (kv) gauss_mul(re, im, re, im, &re, &im);
    }
    return gauss_make(rr, ri);
}

/* ------------------------------------------------------------ product --- */
/* View any factor as base^exponent (x is x^1) for collecting equal bases. */
static E *base_of(E *e) { return e->t == E_POW ? K0(e) : e; }
static E *exp_of(E *e) { return e->t == E_POW ? K1(e) : K->c1; }

/* Order factors by base, then exponent, so equal bases become adjacent. */
static int cmp_be(E *a, E *b) {
    int c = expr_cmp(base_of(a), base_of(b));
    if (c) return c;
    return expr_cmp(exp_of(a), exp_of(b));
}

/*
 * Canonical product of n already simplified operands (Cohen's
 * Simplify_product).
 *
 * Steps:
 *  1. trivial cases; equations distribute (eq_map); if an operand is a list
 *     the operands are multiplied left to right, list*list as a matrix
 *     product (mat_mul_lists) and scalar*list elementwise -- matrix products
 *     are not commutative, so lists are never sorted;
 *  2. undef -> undef; flatten nested products, fold numbers into c;
 *  3. Gaussian factors: when at least two factors are Gaussian numbers and
 *     one of them is a sum a+b*i, multiply them out exactly (products like
 *     i*i are left to the power rule below);
 *  4. 0 * inf -> undef, otherwise 0 * anything -> 0 (keeping a float 0.0);
 *  5. collect equal bases: sort by (base, exponent), replace each run
 *     b^e1 * b^e2 * ... by s_pow(b, e1 + e2 + ...).  The new power may be a
 *     number (folded into c) or a product (e.g. 12^(1/2) -> 2*3^(1/2), or
 *     (xy)^(1/2)*(xy)^(1/2) -> x*y); product factors go into a "redo" list
 *     and the pass is repeated (at most 12 passes) because they may combine
 *     with other factors;
 *  6. merge integer radicals with the same root index:
 *     2^(1/2)*3^(1/2) -> 6^(1/2), 2^(1/3)*3^(2/3) -> 18^(1/3) (skipped when
 *     the radicand would exceed about 4000 bits);
 *  7. a^u * b^u -> (ab)^u for positive exact numbers a, b and symbolic u;
 *  8. +-c*inf -> +-inf; a numeric coefficient times a single sum is
 *     distributed (c*(a+b) -> c*a + c*b, Cohen's rule that keeps 2*(x+1)
 *     and 2*x+2 identical); otherwise sort with expr_cmp and prepend c.
 * Result in scratch, F_SIMP.
 */
E *s_mul(uint32_t n, E **a) {
    if (n == 0) return K->c1;
    if (n == 1) return a[0];
    stack_check();
    if (has_eq_arg(n, a)) return eq_map(1, n, a);
    /* lists / matrices: multiply in the given order */
    for (uint32_t i = 0; i < n; i++)
        if (a[i]->t == E_LIST) {
            E *acc = a[0];
            for (uint32_t j = 1; j < n; j++) {
                if (acc->t == E_LIST && a[j]->t == E_LIST)
                    acc = mat_mul_lists(acc, a[j]);
                else if (acc->t == E_LIST || a[j]->t == E_LIST)
                    acc = list_elementwise(1, acc, a[j]);
                else
                    acc = s_mul2(acc, a[j]);
            }
            return acc;
        }
    uint32_t cnt = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (is_undef(a[i])) return K->undef_;
        cnt += a[i]->t == E_MUL ? a[i]->n : 1;
    }
    uint32_t cap = cnt + 8;
    E **fs = salloc(sizeof(E *) * cap);
    uint32_t m = 0;
    E *c = K->c1;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t kn = a[i]->t == E_MUL ? a[i]->n : 1;
        E **kk = a[i]->t == E_MUL ? EK(a[i]) : &a[i];
        for (uint32_t j = 0; j < kn; j++) {
            if (is_num(kk[j]))
                c = num_mul(c, kk[j]);
            else
                fs[m++] = kk[j];
        }
    }
    /* multiply out gaussian (complex numeric) factors */
    {
        uint32_t ng = 0, nadd = 0;
        for (uint32_t i = 0; i < m; i++)
            if (is_gauss_nonreal(fs[i])) {
                ng++;
                if (fs[i]->t == E_ADD) nadd++;
            }
        if (ng >= 2 && nadd >= 1) {
            E *gr = K->c1, *gi = K->c0;
            uint32_t k = 0;
            for (uint32_t i = 0; i < m; i++) {
                E *r, *im;
                if (is_gauss_nonreal(fs[i]) && gauss_parts(fs[i], &r, &im))
                    gauss_mul(gr, gi, r, im, &gr, &gi);
                else
                    fs[k++] = fs[i];
            }
            m = k;
            E *g = gauss_make(gr, gi);
            if (is_num(g))
                c = num_mul(c, g);
            else if (g->t == E_MUL) {
                c = num_mul(c, K0(g));
                fs[m++] = K1(g);
            } else
                fs[m++] = g;
        }
    }
    int has_inf = 0;
    for (uint32_t i = 0; i < m; i++)
        if (ISYM(fs[i], S_INF)) has_inf = 1;
    if (num_is_zero(c)) {
        if (has_inf) return K->undef_;
        return c->t == E_FLT ? c : K->c0;
    }
    /* Step 5: combine equal bases, repeating while products re-appear. */
    for (int pass = 0; pass < 12; pass++) {
        esort(fs, m, cmp_be);
        E **nf = salloc(sizeof(E *) * (cap + 8));
        uint32_t nm = 0;
        E **redo = NULL;
        uint32_t nredo = 0, rcap = 0;
        for (uint32_t i = 0; i < m;) {
            uint32_t j = i + 1;
            E *b = base_of(fs[i]);
            while (j < m && expr_eq(base_of(fs[j]), b)) j++;
            E *p;
            if (j == i + 1)
                p = fs[i];
            else {
                E **ex = salloc(sizeof(E *) * (j - i));
                for (uint32_t q = i; q < j; q++) ex[q - i] = exp_of(fs[q]);
                p = s_pow(b, s_add(j - i, ex));
            }
            i = j;
            if (is_num(p)) {
                c = num_mul(c, p);
            } else if (p->t == E_MUL) {
                if (nredo + p->n > rcap) { /* grow: a power may expand to many factors */
                    rcap = 2 * (nredo + p->n) + 8;
                    E **nr = salloc(sizeof(E *) * rcap);
                    if (nredo) memcpy(nr, redo, sizeof(E *) * nredo);
                    redo = nr;
                }
                for (uint32_t q = 0; q < p->n; q++) {
                    if (is_num(EK(p)[q]))
                        c = num_mul(c, EK(p)[q]);
                    else
                        redo[nredo++] = EK(p)[q];
                }
            } else
                nf[nm++] = p;
        }
        if (nm + nredo > cap) {
            cap = nm + nredo + 8;
            E **g = salloc(sizeof(E *) * (cap + 8));
            memcpy(g, nf, sizeof(E *) * nm);
            nf = g;
        }
        for (uint32_t q = 0; q < nredo; q++) nf[nm++] = redo[q];
        fs = nf;
        m = nm;
        if (!nredo) break;
        if (num_is_zero(c)) return K->c0;
    }
    /* merge integer radicals with the same index: 2^(1/2) 3^(1/2) -> 6^(1/2) */
    if (m >= 2) {
        for (uint32_t i = 0; i < m; i++) {
            E *fi = fs[i];
            if (!fi || fi->t != E_POW || K0(fi)->t != E_INT || K0(fi)->s || K1(fi)->t != E_RAT) continue;
            E *q = K1(K1(fi));
            E *acc = int_pow(K0(fi), (uint32_t)EL(K0(K1(fi)))[0]);
            int merged = 0;
            for (uint32_t j = i + 1; j < m; j++) {
                E *fj = fs[j];
                if (!fj || fj->t != E_POW || K0(fj)->t != E_INT || K0(fj)->s || K1(fj)->t != E_RAT) continue;
                if (int_cmp(K1(K1(fj)), q) != 0 || K0(K1(fj))->s || K0(K1(fj))->n != 1) continue;
                if (int_bits(acc) + int_bits(K0(fj)) * EL(K0(K1(fj)))[0] > 4000) continue;
                acc = int_mul(acc, int_pow(K0(fj), (uint32_t)EL(K0(K1(fj)))[0]));
                fs[j] = NULL;
                merged = 1;
            }
            if (merged) {
                E *p = s_pow(acc, mk_rat(K->c1, q));
                fs[i] = NULL;
                if (is_num(p)) c = num_mul(c, p);
                else if (p->t == E_MUL) {
                    for (uint32_t k = 0; k < p->n; k++) {
                        if (is_num(EK(p)[k])) c = num_mul(c, EK(p)[k]);
                        else fs[i] = EK(p)[k];
                    }
                } else
                    fs[i] = p;
            }
        }
        uint32_t k = 0;
        for (uint32_t i = 0; i < m; i++)
            if (fs[i]) fs[k++] = fs[i];
        m = k;
    }
    /* a^u * b^u -> (a b)^u for positive numeric bases and symbolic u */
    if (m >= 2) {
        for (uint32_t i = 0; i < m; i++) {
            E *fi = fs[i];
            if (!fi || fi->t != E_POW || !is_exactnum(K0(fi)) || num_sgn(K0(fi)) <= 0 || is_num(K1(fi))) continue;
            for (uint32_t j = i + 1; j < m; j++) {
                E *fj = fs[j];
                if (!fj || fj->t != E_POW || !is_exactnum(K0(fj)) || num_sgn(K0(fj)) <= 0 || !expr_eq(K1(fi), K1(fj)))
                    continue;
                fi = pow_node(num_mul(K0(fi), K0(fj)), K1(fi));
                fs[j] = NULL;
            }
            fs[i] = num_is_one(K0(fi)) ? NULL : fi; /* 2^x (1/2)^x = 1 */
        }
        uint32_t k = 0;
        for (uint32_t i = 0; i < m; i++)
            if (fs[i]) fs[k++] = fs[i];
        m = k;
    }
    if (num_is_zero(c)) return c->t == E_FLT ? c : K->c0;
    if (m == 1 && ISYM(fs[0], S_INF)) {
        int sg = num_sgn(c);
        return sg > 0 ? K->inf_ : m_inf();
    }
    if (m == 0) return c;
    if (num_is_one(c) && m == 1) return fs[0];
    /* distribute a numeric coefficient over a single sum */
    if (m == 1 && fs[0]->t == E_ADD && !num_is_one(c)) {
        E *s = fs[0];
        E **t = salloc(sizeof(E *) * s->n);
        for (uint32_t i = 0; i < s->n; i++) t[i] = s_mul2(c, EK(s)[i]);
        return s_add(s->n, t);
    }
    esort(fs, m, expr_cmp);
    if (num_is_one(c)) return build(E_MUL, m, fs);
    E **fin = salloc(sizeof(E *) * (m + 1));
    fin[0] = c;
    memcpy(fin + 1, fs, sizeof(E *) * m);
    return build(E_MUL, m + 1, fin);
}

/* Binary product with a fast path for numbers; a / b = a * b^(-1);
 * 1/a = a^(-1). */
E *s_mul2(E *a, E *b) {
    if (is_num(a) && is_num(b)) return num_mul(a, b);
    E *v[2] = {a, b};
    return s_mul(2, v);
}
E *s_div(E *a, E *b) { return s_mul2(a, s_pow(b, K->cm1)); }
E *s_inv(E *a) { return s_pow(a, K->cm1); }

/* -------------------------------------------------------------- power --- */
/* Raw canonical power node b^e (F_SIMP set, no rules applied); the caller
 * has already decided that no rule applies. */
static E *pow_node(E *b, E *e) {
    E *k[2] = {b, e};
    return build(E_POW, 2, k);
}

/*
 * The real-root rule of COMPLEX mode REAL (settings.real, cas.h): returns 1
 * when real mode is on and e is an exact rational p/q with q odd, so that
 * b^e of a negative real b is the real root (-1)^p |b|^(p/q); *podd (may be
 * NULL) is then set to p mod 2.  Returns 0 otherwise (principal branch).
 * Used by every evaluator of powers: s_pow (minus1_pow, flt_pow, products),
 * sign_of, ceval (numeric.c) and bfev (bf.c), so exact, double and
 * high-precision results agree.  Integer exponents are not E_RAT.
 */
int real_odd_root(const E *e, int *podd) {
    if (!K->set.real || e->t != E_RAT || int_is_even(K1(e))) return 0;
    if (podd) *podd = !int_is_even(K0(e));
    return 1;
}

/*
 * (-1)^q for rational q, principal value e^(i pi q): q is reduced mod 2 into
 * [0, 2); 0 -> 1, 1 -> -1, 1/2 -> i, 3/2 -> -i, other values stay as the
 * power (-1)^q with the reduced exponent (so (-1)^(7/3) = (-1)^(1/3)).
 * In real mode an odd denominator gives the real root (-1)^p = +-1.
 */
static E *minus1_pow(E *q) {
    int podd;
    if (real_odd_root(q, &podd)) return podd ? K->cm1 : K->c1;
    /* reduce q mod 2 into [0,2) */
    E *two = K->c2;
    E *fl = num_floor(num_div(q, two));
    q = num_sub(q, num_mul(fl, two));
    if (num_is_zero(q)) return K->c1;
    if (num_is_one(q)) return K->cm1;
    if (q->t == E_RAT && is_int_val(K1(q), 2)) {
        /* 1/2 -> i, 3/2 -> -i */
        return is_int_val(K0(q), 1) ? K->i_ : s_neg(K->i_);
    }
    return pow_node(K->cm1, q);
}

/* trial-factor |n| into primes (bounded); returns arrays of (base, mult)
 * where an unfactored cofactor is returned as its own "prime" */
typedef struct {
    E *p;
    E *m; /* exact rational multiplicity contribution */
} pfac;

/*
 * Factor |n| by trial division by 2 and the odd numbers below 2000 (stopping
 * early once p^2 > n for single-limb n).  Each prime p with multiplicity m is
 * stored as (p, m * sign_mult), where sign_mult is +1 for a numerator and -1
 * for a denominator.  A remaining cofactor > 1 is stored as one more "prime"
 * (it may be composite); if it is a perfect g-th power (largest g found
 * first, g <= 64) its root is stored with multiplicity g.  At most cap
 * entries are written; returns the count.  A count equal to cap means
 * factors may have been dropped; there are 303 primes below 2000, so
 * cap = TF_CAP (304, with the cofactor) always records every factor.
 */
#define TF_CAP 304
static uint32_t trial_factor(E *n, pfac *out, uint32_t cap, E *sign_mult) {
    uint32_t k = 0;
    n = int_abs(n);
    for (uint32_t p = 2; p < 2000 && !int_is_one(n); p += (p == 2 ? 1 : 2)) {
        if (n->n == 1 && (uint64_t)p * p > EL(n)[0]) break;
        if (int_mod_u(n, p) == 0) {
            long m = 0;
            E *pe = mk_int(p);
            while (int_mod_u(n, p) == 0) {
                n = int_tdiv(n, pe);
                m++;
            }
            if (k < cap) {
                out[k].p = pe;
                out[k].m = num_mul(mk_int(m), sign_mult);
                k++;
            }
        }
    }
    if (!int_is_one(n) && k < cap) {
        /* cofactor: if a perfect power, record its root */
        E *root = n;
        long g = 1;
        for (uint32_t e = int_bits(n); e >= 2; e--) {
            int ex;
            E *r = int_iroot(n, e, &ex);
            if (ex) {
                root = r;
                g = e;
                break;
            }
            if (e > 64) e = 65; /* cap search */
        }
        out[k].p = root;
        out[k].m = num_mul(mk_int(g), sign_mult);
        k++;
    }
    return k;
}

/*
 * b^e with b an exact nonzero rational != 1 and e a non-integer rational:
 * the canonical radical form  sign * A * B^(s/t).
 *
 *   1. negative b: factor out (-1)^e (minus1_pow) and continue with |b|;
 *   2. factor numerator and denominator (trial_factor), giving
 *      b = prod p_i^(m_i) with m_i < 0 for denominator primes;
 *   3. for each p_i split m_i * e = floor + frac with frac in [0, 1): the
 *      integer parts go into the rational A (so denominators are
 *      rationalised: 2^(-1/2) = 2^(-1) * 2^(1/2) = sqrt(2)/2);
 *   4. with t = lcm of the denominators of the fractional parts and s = gcd
 *      of the numerators frac_i * t, the radical is B^(s/t) with
 *      B = prod p_i^(frac_i * t / s).  Dividing by s makes the exponents of
 *      B coprime, so B is not a perfect power and the form is unique, e.g.
 *      12^(1/2) = 2 * 3^(1/2), 4^(1/3) = 2^(2/3), 72^(1/2) = 6 * 2^(1/2).
 * Only primes below 2000 are split off; a large composite cofactor is kept
 * whole inside B.  The result is built directly (A * B^(s/t) is canonical).
 */
static E *pow_rat_rat(E *b, E *e) {
    E *res_sign = K->c1;
    if (num_sgn(b) < 0) {
        res_sign = minus1_pow(e);
        b = num_neg(b);
    }
    /* usually 48 entries per side suffice; retry with TF_CAP when full
     * (always allocating TF_CAP costs too much scratch in hot loops) */
    uint32_t cap = 48, nf;
    pfac *fac;
    for (;;) {
        fac = salloc(sizeof(pfac) * 2 * cap);
        uint32_t n1 = trial_factor(num_numer(b), fac, cap, K->c1);
        uint32_t n2 = trial_factor(num_denom(b), fac + n1, cap, K->cm1);
        nf = n1 + n2;
        if ((n1 < cap && n2 < cap) || cap == TF_CAP) break;
        cap = TF_CAP;
    }
    /* sort by prime so equal "primes" merge (cofactors could repeat) */
    E *A = K->c1;
    E *t = NULL; /* common denominator */
    E **fr = salloc(sizeof(E *) * (nf + 1));
    for (uint32_t i = 0; i < nf; i++) {
        E *x = num_mul(fac[i].m, e);
        E *fl = num_floor(x);
        if (!num_is_zero(fl)) A = num_mul(A, num_pow_int(fac[i].p, fl));
        fr[i] = num_sub(x, fl);
        if (!num_is_zero(fr[i])) {
            E *d = num_denom(fr[i]);
            t = t ? int_lcm(t, d) : d;
        }
    }
    E *res = num_mul(A, K->c1);
    if (t) {
        /* s = gcd of numerators fr[i]*t ; B = prod p^(fr*t/s) */
        E *s = NULL;
        for (uint32_t i = 0; i < nf; i++) {
            if (num_is_zero(fr[i])) continue;
            E *nu = num_mul(fr[i], t);
            s = s ? int_gcd(s, nu) : nu;
        }
        E *B = K->c1;
        for (uint32_t i = 0; i < nf; i++) {
            if (num_is_zero(fr[i])) continue;
            E *ex = int_tdiv(num_mul(fr[i], t), s);
            B = int_mul(B, int_pow(fac[i].p, (uint32_t)EL(ex)[0]));
        }
        E *rad = pow_node(B, mk_rat(s, t));
        E *k[2] = {res, rad};
        res = num_is_one(res) ? rad : build(E_MUL, 2, k);
    }
    if (!num_is_one(res_sign)) res = s_mul2(res_sign, res);
    return res;
}

/*
 * Floating-point b^e.  For a negative base and non-integer exponent the
 * principal complex value |b|^e (cos(pi e) + i sin(pi e)) is returned, the
 * same branch as the exact (-1)^q rule.  realroot = 1 (the exponent is an
 * exact p/q, q odd, in real mode; podd = p mod 2) gives the real root
 * (-1)^p |b|^e instead.
 */
static E *flt_pow(double b, double e, int realroot, int podd) {
    if (b >= 0 || e == floor(e)) return mk_flt(pow(b, e));
    if (realroot) return mk_flt(podd ? -pow(-b, e) : pow(-b, e));
    double r = pow(-b, e), th = M_PI * e;
    return s_add2(mk_flt(r * cos(th)), s_mul2(mk_flt(r * sin(th)), K->i_));
}

/*
 * If the product t contains a factor ln(u) with u a known-positive constant,
 * return u^r where r is the product of the other factors (so e^t = u^r);
 * else NULL.  Declined when r is a non-numeric constant (e^(pi ln 2) is kept
 * rather than turned into 2^pi).  u > 0 makes (e^ln u)^r = u^r valid.
 */
static E *ln_term_pow(E *t) {
    if (t->t != E_MUL) return NULL;
    for (uint32_t i = 0; i < t->n; i++) {
        E *f = EK(t)[i];
        if (IFN(f, F_LN) && f->n == 1 && is_const_expr(K0(f)) && is_poslit(K0(f))) {
            E **r = salloc(sizeof(E *) * t->n);
            uint32_t k = 0;
            for (uint32_t j = 0; j < t->n; j++)
                if (j != i) r[k++] = EK(t)[j];
            E *ex = s_mul(k, r);
            if (is_const_expr(ex) && !is_num(ex)) return NULL;
            return s_pow(K0(f), ex);
        }
    }
    return NULL;
}

/*
 * Rules for e^x; returns NULL if none applies (caller keeps e^x).
 *   e^ln(u) = u;  e^(c ln u) = u^c for numeric c;  e^(r ln u) = u^r for
 *   positive constant u (ln_term_pow);
 *   sums: e^(a + c ln u + ...) = u^c * ... * e^a, splitting off every
 *   logarithmic term (first the ln_term_pow kind, then plain c*ln(u));
 *   e^inf = inf, e^(c inf) = inf or 0; float x -> exp(x).
 */
static E *sin_exact(E *q);
static E *cos_exact(E *q);

/* t = q*i*pi with exact rational q whose denominator is 1, 2, 3, 4 or 6:
 * e^t = cos(q pi) + i sin(q pi) (exp(i*pi) = -1, exp(i*pi/3) = 1/2 + sqrt(3)/2 i).
 * Other denominators stay as exp(...) (exp2trig expands them). */
static E *exp_ipi(E *t) {
    E *q = NULL;
    int si = 0, sp = 0;
    if (t->t != E_MUL) return NULL;
    for (uint32_t i = 0; i < t->n; i++) {
        E *f = EK(t)[i];
        if (ISYM(f, S_I) && !si) si = 1;
        else if (ISYM(f, S_PI) && !sp) sp = 1;
        else if (!q && is_exactnum(f)) q = f;
        else return NULL;
    }
    if (!si || !sp) return NULL;
    if (!q) q = K->c1;
    long d = 1;
    if (q->t != E_INT && !num_is_small_int(K1(q), &d)) return NULL;
    if (d != 1 && d != 2 && d != 3 && d != 4 && d != 6) return NULL;
    return s_add2(cos_exact(q), s_mul2(K->i_, sin_exact(q)));
}

static E *exp_rules(E *x) {
    stack_check();
    {
        E *v = exp_ipi(x);
        if (v) return v;
        if (x->t == E_ADD)
            for (uint32_t i = 0; i < x->n; i++)
                if ((v = exp_ipi(EK(x)[i]))) {
                    E **r = salloc(sizeof(E *) * x->n);
                    uint32_t k = 0;
                    for (uint32_t j = 0; j < x->n; j++)
                        if (j != i) r[k++] = EK(x)[j];
                    return s_mul2(v, s_pow(K->e_, s_add(k, r)));
                }
    }
    if (IFN(x, F_LN)) return K0(x);
    if (x->t == E_MUL && x->n == 2 && is_num(K0(x)) && IFN(K1(x), F_LN)) return s_pow(K0(K1(x)), K0(x));
    {
        E *lp = ln_term_pow(x);
        if (lp) return lp;
    }
    if (x->t == E_ADD) {
        int found = 0;
        for (uint32_t i = 0; i < x->n; i++)
            if (ln_term_pow(EK(x)[i])) found = 1;
        if (found) {
            E **f = salloc(sizeof(E *) * (x->n + 1)), **rest = salloc(sizeof(E *) * (x->n + 1));
            uint32_t nf = 0, nr = 0;
            for (uint32_t i = 0; i < x->n; i++) {
                E *lp = ln_term_pow(EK(x)[i]);
                if (lp) f[nf++] = lp;
                else rest[nr++] = EK(x)[i];
            }
            if (nr) f[nf++] = s_pow(K->e_, s_add(nr, rest));
            return s_mul(nf, f);
        }
    }
    if (x->t == E_ADD) {
        int found = 0;
        for (uint32_t i = 0; i < x->n; i++) {
            E *t = EK(x)[i];
            if (IFN(t, F_LN) || (t->t == E_MUL && t->n == 2 && is_num(K0(t)) && IFN(K1(t), F_LN))) found = 1;
        }
        if (found) {
            E **rest = salloc(sizeof(E *) * x->n);
            E **facs = salloc(sizeof(E *) * (x->n + 1));
            uint32_t nr = 0, nf = 0;
            for (uint32_t i = 0; i < x->n; i++) {
                E *t = EK(x)[i];
                if (IFN(t, F_LN) || (t->t == E_MUL && t->n == 2 && is_num(K0(t)) && IFN(K1(t), F_LN)))
                    facs[nf++] = exp_rules(t);
                else
                    rest[nr++] = t;
            }
            facs[nf++] = s_pow(K->e_, s_add(nr, rest));
            return s_mul(nf, facs);
        }
    }
    if (ISYM(x, S_INF)) return K->inf_;
    if (x->t == E_MUL && x->n == 2 && is_num(K0(x)) && ISYM(K1(x), S_INF))
        return num_sgn(K0(x)) > 0 ? K->inf_ : K->c0;
    if (is_flt(x)) {
        /* beyond the double range: bigfloat, exp(-1000000.0) = 3.29683e-434295 */
        double v = exp(EF(x));
        extern E *bf_evalf(E *e, int digits);
        E *r = (v == 0 || isinf(v)) && isfinite(EF(x))
                   ? bf_evalf(pow_node(K->e_, num_from_double(EF(x))), K->set.digits)
                   : NULL;
        return r ? r : mk_flt(v);
    }
    return NULL;
}

/*
 * Canonical power b^e of simplified operands (Cohen's Simplify_power plus
 * the numeric, complex and exponential rules of this CAS).  In order:
 *   - undef propagates; (a = c)^e -> a^e = c^e;
 *   - x^0 = 1 (also 0^0 = 1, and 1.0 for a float exponent), x^1 = x;
 *   - numeric base: 0^e = 0 for e > 0 (known sign), division-by-zero error
 *     for e < 0; 1^e = 1; integer exponent -> exact power (num_pow_int);
 *     floats -> flt_pow; rational exponent -> pow_rat_rat;
 *     (-1)^(2k) = 1 for k assumed integer; b^inf = inf for b > 1, 0 for
 *     |b| < 1; anything else stays b^e (e.g. 2^x);
 *   - real constant base with float exponent -> float;
 *   - Gaussian base (a + b i)^n -> exact Gaussian number; i^n -> 1, i, -1, -i;
 *   - base e: exp_rules (e^ln(u) = u, e^(c ln u) = u^c, ...);
 *   - inf^c for numeric c: inf (c > 0) or 0;
 *   - (x^a)^e -> x^(a e) when e is an integer, when x = e (real-variable
 *     convention), or when a, e are numbers and x is known positive, or in
 *     real mode when e is an odd root and a an integer or odd root;
 *     otherwise kept, since e.g. (x^2)^(1/2) = |x| != x;
 *   - (x*y*...)^n -> x^n y^n ... for integer n; for other exponents only
 *     positive factors (and |c| of negative numeric factors, leaving a -1
 *     inside) are pulled out: (4x)^(1/2) -> 2 x^(1/2); in real mode with
 *     an odd root the negative numbers go out with their sign;
 *   - real mode, odd root (real_odd_root): numbers and negative constants
 *     use the real root, (-8)^(1/3) = -2; negative constant sums too:
 *     (1-pi)^(1/3) = -(pi-1)^(1/3);
 *   - list^n -> matrix power.
 * Throws CAS_ERR_DOMAIN for 0^negative.  Result in scratch.
 */
/* a symbol of e carries a sign assumption (assume(x>0): A_* flags) */
static int has_assumed_sym(E *e) {
    if (e->t == E_SYM) return K->syms[e->s].assume != 0;
    if (e->t == E_INT || e->t == E_FLT || e->t == E_STR || e->t == E_RAT) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_assumed_sym(EK(e)[i])) return 1;
    return 0;
}

/* no square root of a non-constant left in e (x^(k/2), k odd) */
static int freeof_halfpow(E *e) {
    if (e->t == E_POW && K1(e)->t == E_RAT && !is_const_expr(K0(e))) return 0;
    if (e->t == E_INT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR || e->t == E_RAT) return 1;
    for (uint32_t i = 0; i < e->n; i++)
        if (!freeof_halfpow(EK(e)[i])) return 0;
    return 1;
}

E *s_pow(E *b, E *e) {
    stack_check();
    if (is_undef(b) || is_undef(e)) return K->undef_;
    if (IFN(b, F_EQ) && b->n == 2 && !IFN(e, F_EQ)) {
        E *r = enode(E_FN, 2);
        r->s = F_EQ;
        K0(r) = s_pow(K0(b), e);
        K1(r) = s_pow(K1(b), e);
        r->f = F_SIMP;
        return r;
    }
    if (is_num(e) && num_is_zero(e)) return e->t == E_FLT ? mk_flt(1.0) : K->c1;
    if (is_one(e)) return b;
    if (is_num(b)) {
        if (num_is_zero(b)) {
            if (is_num(e)) {
                if (num_sgn(e) > 0) return b;
                cas_throw(CAS_ERR_DOMAIN, "division by zero");
            }
            int se = sign_of(e);
            if (se > 0) return b;
            if (se < 0) cas_throw(CAS_ERR_DOMAIN, "division by zero");
            return pow_node(b, e);
        }
        if (is_one(b)) return b;
        if (e->t == E_INT) return num_pow_int(b, e);
        if (e->t == E_FLT || b->t == E_FLT) {
            if (!is_num(e)) return pow_node(b, e);
            int podd = 0, rr = real_odd_root(e, &podd);
            return flt_pow(num_to_double(b), num_to_double(e), rr, podd);
        }
        if (e->t == E_RAT) return pow_rat_rat(b, e);
        if (num_is_m1(b) && e->t == E_MUL && is_exactnum(K0(e)) && K0(e)->t == E_INT && int_is_even(K0(e)) &&
            is_int_assumed(e))
            return K->c1;
        if (ISYM(e, S_INF)) {
            double bv = num_to_double(b);
            if (bv > 1) return K->inf_;
            if (bv > -1 && bv < 1) return K->c0;
        }
        return pow_node(b, e);
    }
    if (is_num(e) && e->t == E_FLT && is_const_expr(b) && !ISYM(b, S_E)) {
        double re, im;
        if (eval_complex(b, &re, &im) && im == 0) return flt_pow(re, EF(e), 0, 0);
    }
    if (b->t == E_ADD && e->t == E_INT) {
        E *re, *im;
        if (gauss_parts(b, &re, &im)) {
            E *r = gauss_pow(re, im, e);
            if (r) return r;
        }
    }
    if (ISYM(b, S_I) && e->t == E_INT) {
        uint32_t r = int_modp(e, 4);
        return r == 0 ? K->c1 : r == 1 ? K->i_ : r == 2 ? K->cm1 : s_neg(K->i_);
    }
    if (ISYM(b, S_E)) {
        E *r = exp_rules(e);
        if (r) return r;
        return pow_node(b, e);
    }
    if (ISYM(b, S_INF) && is_num(e)) return num_sgn(e) > 0 ? K->inf_ : K->c0;
    {
        /* real mode: a negative constant sum such as 1-pi gives the real
         * root -(pi-1)^(1/3).  Products are handled below (factor by factor);
         * other bases stay as they are (numeric evaluation and sign_of use
         * the real root, e.g. x^(1/3) < 0 for x < 0).  The negated base must
         * not be a product, whose -1 factor would lead straight back here. */
        int podd;
        if (b->t == E_ADD && real_odd_root(e, &podd) && is_const_expr(b) && sign_of(b) < 0) {
            E *nb = s_neg(b);
            if (nb->t != E_MUL) {
                E *r = s_pow(nb, e);
                return podd ? s_neg(r) : r;
            }
        }
    }
    if (b->t == E_POW) {
        E *b2 = K0(b), *e2 = K1(b);
        if (e->t == E_INT || (ISYM(b2, S_E) && freeof_sym(e2, S_I))) return s_pow(b2, s_mul2(e2, e));
        /* principal branch: (e^z)^p = e^(p (z + 2 pi i k)) with k moving Im z
         * into (-pi, pi]; a non-constant complex z stays: sqrt(e^(6i)) = -e^(3i) */
        double zr, zi;
        if (ISYM(b2, S_E) && is_const_expr(e2) && eval_complex(e2, &zr, &zi)) {
            E *k = mk_int((int64_t)floor((M_PI - zi) / (2 * M_PI)));
            E *f[5] = {mk_int(2), K->pi_, K->i_, k, e};
            return s_mul2(s_pow(b2, s_mul2(e2, e)), s_exp(s_mul(5, f)));
        }
        if (is_num(e) && is_num(e2) && is_poslit(b2)) return s_pow(b2, s_mul2(e2, e));
        /* real mode: real odd roots are multiplicative on the reals, so
         * (x^n)^(p/q) = x^(n p/q) for integer n or odd-root n:
         * (x^3)^(1/3) = x, (x^2)^(1/3) = x^(2/3) */
        if (real_odd_root(e, NULL) && (e2->t == E_INT || real_odd_root(e2, NULL)))
            return s_pow(b2, s_mul2(e2, e));
        /* sqrt(p^(2k)) = |p|^k: p^k or (-p)^k when the sign of p is known
         * (assume(x>0): sqrt((x+1)^2) = x+1) */
        if (num_eq(e, K->chalf) && e2->t == E_INT && num_sgn(e2) > 0 && int_is_even(e2)) {
            int sg = sign_of(b2);
            if (sg) return s_pow(sg > 0 ? b2 : s_neg(b2), num_mul(e2, K->chalf));
        }
        return pow_node(b, e);
    }
    /* (x y)^e: distributing is always valid for integer e; for other e
     * only factors with known positive value can be split off without
     * changing the principal branch. */
    if (b->t == E_MUL) {
        if (e->t == E_INT) {
            E **f = salloc(sizeof(E *) * b->n);
            for (uint32_t i = 0; i < b->n; i++) f[i] = s_pow(EK(b)[i], e);
            return s_mul(b->n, f);
        }
        /* pull out positive constant factors; in real mode with an odd
         * root (real_odd_root) a negative number goes out whole, sign
         * included: (-8x)^(1/3) = -2 x^(1/3), (-x)^(1/3) = -x^(1/3) */
        E **in = salloc(sizeof(E *) * (b->n + 1)), **out = salloc(sizeof(E *) * (b->n + 1));
        uint32_t ni = 0, no = 0;
        int rr = real_odd_root(e, NULL);
        for (uint32_t i = 0; i < b->n; i++) {
            E *f = EK(b)[i];
            if (rr && is_num(f) && num_sgn(f) < 0)
                out[no++] = s_pow(f, e);
            else if (is_num(f) && num_sgn(f) < 0 && !num_is_m1(f)) {
                out[no++] = s_pow(num_neg(f), e);
                in[ni++] = K->cm1;
            } else if (is_poslit(f))
                out[no++] = s_pow(f, e);
            else
                in[ni++] = f;
        }
        if (no) {
            E *inner = s_mul(ni, in);
            out[no++] = s_pow(inner, e);
            return s_mul(no, out);
        }
        return pow_node(b, e);
    }
    if (b->t == E_LIST && e->t == E_INT) {
        extern E *mat_pow_list(E *m, E *k);
        return mat_pow_list(b, e);
    }
    if (b->t == E_LIST && e->t == E_RAT) {
        extern E *mat_rpow(E *A, E *e);
        E *r = mat_rpow(b, e);
        if (r) return r;
    }
    /* sqrt of a perfect square under assumptions: assume(x>0) makes
     * sqrt(x^2+2*x+1) = x+1. Only with an assumption on one of its symbols or
     * a stored relation (the sign of the root decides), and only when the
     * factored form loses its square root. */
    static int in_sq;
    if (b->t == E_ADD && num_eq(e, K->chalf) && !in_sq && (facts_any() || has_assumed_sym(b)) && !is_const_expr(b)) {
        in_sq = 1;
        E *f = factor_expr(b), *r = NULL;
            if (f != b && (f->t == E_POW || f->t == E_MUL)) {
            r = s_pow(f, e);
            if (!freeof_halfpow(r)) r = NULL;
        }
        in_sq = 0;
        if (r) return r;
    }
    return pow_node(b, e);
}

/* Convenience forms: a^k, a^(1/2), e^a. */
E *s_powi(E *a, long k) { return s_pow(a, mk_int(k)); }
E *s_sqrt(E *a) { return s_pow(a, K->chalf); }
E *s_exp(E *a) { return s_pow(K->e_, a); }

/* ---------------------------------------------------------- functions --- */
/*
 * Recognise a = q*pi with q an exact rational (pi itself, exact 0, or the
 * canonical product q*pi).  Returns 1 and sets *q, else 0.  Gate for the
 * exact trig tables.
 */
static int pi_coef(E *a, E **q) {
    if (ISYM(a, S_PI)) {
        *q = K->c1;
        return 1;
    }
    if (is_num(a) && num_is_zero(a) && a->t != E_FLT) {
        *q = K->c0;
        return 1;
    }
    if (a->t == E_MUL && a->n == 2 && is_exactnum(K0(a)) && ISYM(K1(a), S_PI)) {
        *q = K0(a);
        return 1;
    }
    return 0;
}

/* q*pi for a rational q. */
E *s_frac_pi(E *q) { return s_mul2(q, K->pi_); }

/* q mod m for rationals, result in [0, m) (floor modulo). */
static E *qmod(E *q, E *m) {
    E *fl = num_floor(num_div(q, m));
    return num_sub(q, num_mul(fl, m));
}

/* sqrt(n), simplified. */
static E *rad(long n) { return s_sqrt(mk_int(n)); }

/*
 * sin(q*pi) for rational q in [0, 1/2]; NULL if not in the table.  Table
 * (d = denominator of q): d = 2: 1; 3: sqrt(3)/2; 4: sqrt(2)/2; 6: 1/2;
 * 12: (sqrt(6) -+ sqrt(2))/4; 10: (sqrt(5) -+ 1)/4;
 * 5: sqrt(10 -+ 2 sqrt(5))/4; 8: sqrt(2 -+ sqrt(2))/2.
 * Together with the symmetry reductions in sin_exact / cos_exact this covers
 * all multiples of pi with denominators 1, 2, 3, 4, 5, 6, 8, 10, 12.
 */
static E *sin_table(E *q) {
    long n = 0, d = 1;
    if (q->t == E_INT) {
        n = int_is_zero(q) ? 0 : 1;
        d = 1;
    } else {
        if (!num_is_small_int(K0(q), &n) || !num_is_small_int(K1(q), &d)) return NULL;
    }
    if (n == 0) return K->c0;
    switch (d) {
    case 2: return K->c1;
    case 3: return s_mul2(K->chalf, rad(3));
    case 4: return s_mul2(K->chalf, rad(2));
    case 6: return K->chalf;
    case 12:
        if (n == 1) return s_mul2(mk_frac(1, 4), s_sub(rad(6), rad(2)));
        if (n == 5) return s_mul2(mk_frac(1, 4), s_add2(rad(6), rad(2)));
        return NULL;
    case 10:
        if (n == 1) return s_mul2(mk_frac(1, 4), s_sub(rad(5), K->c1));
        if (n == 3) return s_mul2(mk_frac(1, 4), s_add2(rad(5), K->c1));
        return NULL;
    case 5:
        if (n == 1) return s_mul2(mk_frac(1, 4), s_sqrt(s_sub(mk_int(10), s_mul2(K->c2, rad(5)))));
        if (n == 2) return s_mul2(mk_frac(1, 4), s_sqrt(s_add2(mk_int(10), s_mul2(K->c2, rad(5)))));
        return NULL;
    case 8:
        if (n == 1) return s_mul2(K->chalf, s_sqrt(s_sub(K->c2, rad(2))));
        if (n == 3) return s_mul2(K->chalf, s_sqrt(s_add2(K->c2, rad(2))));
        return NULL;
    }
    return NULL;
}

/* One-argument F_SIMP function node built without applying any rules (used
 * to return e.g. sin(pi/7) after reduction, avoiding infinite recursion). */
static E *mkfn(int id, E *a) {
    E *e = enode(E_FN, 1);
    e->s = (uint16_t)id;
    K0(e) = a;
    e->f = F_SIMP;
    return e;
}

/*
 * sin(q pi) for rational q: reduce q mod 2, use sin(x + pi) = -sin(x) to get
 * q into [0, 1), then sin(pi - x) = sin(x) to get q into [0, 1/2], and look
 * up the table.  Values outside the table come back as +-sin(q' pi) with the
 * reduced q', so equal values always share one representation.
 */
static E *sin_exact(E *q) {
    q = qmod(q, K->c2);
    E *sg = K->c1;
    if (num_cmp(q, K->c1) >= 0) {
        q = num_sub(q, K->c1);
        sg = K->cm1;
    }
    if (num_cmp(q, K->chalf) > 0) q = num_sub(K->c1, q);
    E *v = sin_table(q);
    if (v) return s_mul2(sg, v);
    return s_mul2(sg, mkfn(F_SIN, s_frac_pi(q)));
}

/*
 * cos(q pi): reduce q into [0, 1] using period 2 and evenness, then into
 * [0, 1/2] with cos(pi - x) = -cos(x), and use cos(x) = sin(pi/2 - x) for the
 * table lookup.  Unknown values stay +-cos(q' pi).
 */
static E *cos_exact(E *q) {
    q = qmod(q, K->c2);
    if (num_cmp(q, K->c1) > 0) q = num_sub(K->c2, q);
    /* q in [0,1] */
    E *sg = K->c1;
    if (num_cmp(q, K->chalf) > 0) {
        q = num_sub(K->c1, q);
        sg = K->cm1;
    }
    /* cos(q pi) = sin((1/2 - q) pi) */
    E *v = sin_table(num_sub(K->chalf, q));
    if (v) return s_mul2(sg, v);
    return s_mul2(sg, mkfn(F_COS, s_frac_pi(q)));
}

/*
 * tan(q pi): period 1, oddness via tan(pi - x) = -tan(x), table value as
 * sin/cos.  Throws CAS_ERR_DOMAIN at the pole q = 1/2 (mod 1).
 */
static E *tan_exact(E *q) {
    q = qmod(q, K->c1);
    E *sg = K->c1;
    if (num_cmp(q, K->chalf) > 0) {
        q = num_sub(K->c1, q);
        sg = K->cm1;
    }
    if (num_eq(q, K->chalf)) cas_throw(CAS_ERR_DOMAIN, "tan undefined at pi/2");
    E *s = sin_table(q), *c = sin_table(num_sub(K->chalf, q));
    if (s && c) return s_mul2(sg, s_div(s, c));
    return s_mul2(sg, mkfn(F_TAN, s_frac_pi(q)));
}

/* Split a sum arg = rest + q*pi (q nonzero exact rational, first such term
 * wins); returns 1 if a pi term was found.  Used for the shift rules
 * sin(x + pi/2) = cos(x) etc. */
static int split_pi_term(E *a, E **rest, E **q) {
    if (a->t != E_ADD) return 0;
    for (uint32_t i = 0; i < a->n; i++) {
        E *qq;
        if (pi_coef(EK(a)[i], &qq) && !num_is_zero(qq)) {
            E **r = salloc(sizeof(E *) * a->n);
            uint32_t k = 0;
            for (uint32_t j = 0; j < a->n; j++)
                if (j != i) r[k++] = EK(a)[j];
            *rest = s_add(k, r);
            *q = qq;
            return 1;
        }
    }
    return 0;
}

/* The sum a without its terms c*pi with c/m an assumed integer (assume(n, integer)):
 * sin(x + 2*pi*n) = sin(x), tan(x + pi*n) = tan(x) (solve's general solutions);
 * NULL if there is none. */
static E *drop_period(E *a, E *m) {
    if (a->t != E_ADD) return NULL;
    E **r = salloc(sizeof(E *) * a->n);
    uint32_t k = 0;
    for (uint32_t i = 0; i < a->n; i++) {
        E *t = EK(a)[i];
        if (!(t->t == E_MUL && ISYM(EK(t)[t->n - 1], S_PI) && is_int_assumed(s_div(t, s_mul2(m, K->pi_)))))
            r[k++] = t;
    }
    return k < a->n ? s_add(k, r) : NULL;
}

/* Parity tables: f(-x) = -f(x) for odd functions, f(-x) = f(x) for even ones.
 * Applied in s_fn_inner whenever the argument is a negative term. */
static int is_odd_fn(int id) {
    switch (id) {
    case F_SIN: case F_TAN: case F_COT: case F_CSC: case F_ASIN: case F_ATAN:
    case F_SINH: case F_TANH: case F_ASINH: case F_ATANH: case F_ERF: case F_SIGN: case F_SI: case F_ERFI: return 1;
    }
    return 0;
}
static int is_even_fn(int id) { return id == F_COS || id == F_SEC || id == F_COSH || id == F_ABS; }

/* Exact value table for inverse trig: returns q such that asin(v) = q pi for
 * v in {0, 1/2, sqrt(2)/2, sqrt(3)/2, 1}, else NULL.  asin is odd, so a
 * negative v is reflected by the parity rule before this is reached; acos
 * reflects negative constants itself (acos(-v) = pi - acos(v)) and uses
 * acos(v) = pi/2 - asin(v). */
static E *asin_table(E *v) {
    if (is_zero(v)) return K->c0;
    if (is_one(v)) return K->chalf;
    if (expr_eq(v, K->chalf)) return mk_frac(1, 6);
    if (expr_eq(v, s_mul2(K->chalf, rad(2)))) return mk_frac(1, 4);
    if (expr_eq(v, s_mul2(K->chalf, rad(3)))) return mk_frac(1, 3);
    return NULL;
}
/* atan(v) = q pi for v in {0, 1, sqrt(3), sqrt(3)/3, 2 - sqrt(3), 2 + sqrt(3)};
 * returns q or NULL.  Comparison is structural, so v must be canonical. */
static E *atan_table(E *v) {
    if (is_zero(v)) return K->c0;
    if (is_one(v)) return mk_frac(1, 4);
    if (expr_eq(v, rad(3))) return mk_frac(1, 3);
    if (expr_eq(v, s_mul2(mk_frac(1, 3), rad(3)))) return mk_frac(1, 6);
    if (expr_eq(v, s_sub(K->c2, rad(3)))) return mk_frac(1, 12);
    if (expr_eq(v, s_add2(K->c2, rad(3)))) return mk_frac(5, 12);
    return NULL;
}

/* sqrt(1 - x^2) and sqrt(1 + x^2), for sin(acos x), cos(atan x), etc. */
static E *one_minus_sq(E *x) { return s_sqrt(s_sub(K->c1, s_powi(x, 2))); }
static E *one_plus_sq(E *x) { return s_sqrt(s_add2(K->c1, s_powi(x, 2))); }

/*
 * Simplification rules for ln(a); NULL means "keep ln(a)".
 *   ln 1 = 0, ln e = 1, ln 0 = -inf (exact zero only), ln inf = inf,
 *   ln i = i pi/2;
 *   ln(e^u) = u for non-constant u (real-variable convention) and for real
 *   constants u (checked numerically; for complex u it is not true);
 *   float argument: log(x), or log|x| + pi i for x < 0, error at 0.0;
 *   exact negative rational: ln|a| + pi i (principal branch);
 *   ln(1/n) = -ln n; ln(r^k) = k ln r for an integer that is a perfect k-th
 *   power (largest k found first, k <= 64 by the search cap);
 *   ln(b^r) = r ln b for b known positive and exact numeric r.
 * Note ln(p/q) with p != 1 is kept as is.
 */
static E *ln_rules(E *a) {
    if (is_one(a)) return K->c0;
    if (ISYM(a, S_E)) return K->c1;
    if (is_zero(a) && a->t != E_FLT) return m_inf();
    if (ISYM(a, S_INF)) return K->inf_;
    if (ISYM(a, S_I)) return s_mul2(K->chalf, s_mul2(K->pi_, K->i_));
    if (a->t == E_POW && ISYM(K0(a), S_E) && !is_const_expr(K1(a))) return K1(a);
    if (a->t == E_POW && ISYM(K0(a), S_E)) {
        /* ln(e^c) = c for real constants c, c - 2 k pi i for complex ones (principal branch,
         * Im in (-pi, pi]; not decided near an odd multiple of pi) */
        double re, im;
        if (eval_complex(K1(a), &re, &im) && im == 0) return K1(a);
        if (eval_complex(K1(a), &re, &im) && fabs(im) < 1e6) {
            double q = im / (2 * M_PI), k = floor(q + 0.5);
            if (fabs(q - k) < 0.49) return s_sub(K1(a), s_mul(3, (E *[]){mk_int((int64_t)(2 * k)), K->pi_, K->i_}));
        }
    }
    if (a->t == E_FLT) {
        double x = EF(a);
        if (x > 0) return mk_flt(log(x));
        if (x == 0) cas_throw(CAS_ERR_DOMAIN, "ln(0)");
        return s_add2(mk_flt(log(-x)), s_mul2(mk_flt(M_PI), K->i_));
    }
    if (is_exactnum(a)) {
        if (num_sgn(a) < 0) return s_add2(s_ln(num_neg(a)), s_mul2(K->pi_, K->i_));
        if (a->t == E_RAT && int_is_one(K0(a))) return s_neg(s_ln(K1(a)));
        if (a->t == E_INT) {
            for (uint32_t k = int_bits(a); k >= 2; k--) {
                int ex;
                E *r = int_iroot(a, k, &ex);
                if (ex) return s_mul2(mk_int(k), s_ln(r));
                if (k > 64) k = 65;
            }
        }
        return NULL;
    }
    if (a->t == E_POW && is_num(K1(a)) && K1(a)->t != E_FLT && is_poslit(K0(a)))
        return s_mul2(K1(a), s_ln(K0(a)));
    return NULL;
}

/* all_num: every argument is a number; any_flt: some argument is a float. */
static int all_num(uint32_t n, E **a) {
    for (uint32_t i = 0; i < n; i++)
        if (!is_num(a[i])) return 0;
    return 1;
}
/* Some argument is a float (triggers numeric evaluation of the function). */
static int any_flt(uint32_t n, E **a) {
    for (uint32_t i = 0; i < n; i++)
        if (a[i]->t == E_FLT) return 1;
    return 0;
}

static E *s_fn_inner(int id, uint32_t n, E **a);

/*
 * Accepted argument counts [lo, hi] for builtin functions/operators.  Default
 * is exactly one; sqrt(x, n), ln(x, b), log(x, b) and round(x, d) take an
 * optional second argument.  max/min, piecewise and the unevaluated
 * placeholders (rootof, derivative, integral, sum_, limit_) are variadic.
 */
static void fn_arity(int id, uint32_t *lo, uint32_t *hi) {
    *lo = 1;
    *hi = 1;
    switch (id) {
    case F_SQRT: case F_LN: case F_LOG: case F_ROUND: *hi = 2; break;
    case F_ATAN2: case F_MOD: case F_BINOM: *lo = 2; *hi = 2; break;
    case F_MAX: case F_MIN: *hi = 64; break;
    case F_ROOTOF: case F_PIECEWISE: case F_DERIV: case F_INTEGRAL: case F_SUMU: case F_LIMIT_U: *lo = 0; *hi = 64; break;
    case F_EQ: case F_LT: case F_GT: case F_LE: case F_GE: case F_NE: case F_AND: case F_OR: case F_ASSIGN:
    case F_LAMBDA: *lo = 2; *hi = 2; break;
    case F_SEQ: *lo = 1; *hi = 100000; break;
    }
}

/*
 * Canonical function application id(a[0..n-1]) with simplified arguments.
 * For builtin functions and operators (ids in [F_EQ, S__FIRSTCMD)) the
 * argument count is checked first (CAS_ERR_DOMAIN "wrong number of
 * arguments"); an undef argument gives undef (except for commands); then
 * s_fn_inner applies the rules.  If no rule fires, an F_SIMP E_FN node is
 * returned.  User functions and commands get no rules here (they are
 * expanded by eval.c / cmds.c).
 */
E *s_fn(int id, uint32_t n, E **a) {
    stack_check();
    if (id < S__FIRSTCMD && id >= F_EQ) {
        uint32_t lo, hi;
        fn_arity(id, &lo, &hi);
        if (n < lo || n > hi) cas_throw(CAS_ERR_DOMAIN, "wrong number of arguments");
    }
    for (uint32_t i = 0; i < n; i++)
        if (is_undef(a[i]) && id < S__FIRSTCMD) return K->undef_;
    E *r;
    /* re / im of a casx function first ask the module (it keeps them when the value may be
     * complex: besselj(1/2, -x)) */
    if (CASX_SLOTS && (id == F_RE || id == F_IM) && n == 1) {
        E *f = mk_fn(id, n, a);
        if ((r = casx_eslot(CASX_FN_FSIMP, 1, &f))) return r;
    }
    if ((r = s_fn_inner(id, n, a))) return r;
    if (CASX_SLOTS && id >= S__COUNT && n >= 1 && n <= 4) { /* functions of the casx module (fnx.c) */
        E *f = mk_fn(id, n, a);
        f->f = F_SIMP;
        if ((r = casx_eslot(CASX_FN_FSIMP, 1, &f))) return r;
    }
    E *e = enode(E_FN, n);
    e->s = (uint16_t)id;
    memcpy(EK(e), a, sizeof(E *) * n);
    e->f = F_SIMP;
    return e;
}

/* One- and two-argument forms of s_fn; s_ln(a) = ln(a). */
E *s_fn1(int id, E *a) { return s_fn(id, 1, &a); }
/* Two-argument form of s_fn. */
E *s_fn2(int id, E *a, E *b) {
    E *v[2] = {a, b};
    return s_fn(id, 2, v);
}
E *s_ln(E *a) { return s_fn1(F_LN, a); }

/*
 * floor (mode 0), ceil (1) or round (2, half away from zero) of a.  Exact for
 * rationals; float in -> float out.  For other real constants (sqrt(2), pi,
 * ...) the value is computed in double precision and accepted only when it is
 * below 1e15 in magnitude and not within 1e-9 of an integer or of a
 * half-integer, so that rounding error cannot pick the wrong integer.
 * Returns NULL (keep unevaluated) otherwise.
 */
static E *real_const_floor(E *a, int mode) {
    /* mode 0 floor, 1 ceil, 2 round */
    if (is_exactnum(a)) {
        if (mode == 0) return num_floor(a);
        if (mode == 1) return num_neg(num_floor(num_neg(a)));
        E *h = num_add(num_abs(a), K->chalf);
        E *f = num_floor(h);
        return num_sgn(a) < 0 ? num_neg(f) : f;
    }
    if (a->t == E_FLT) {
        double x = EF(a);
        return mk_flt(mode == 0 ? floor(x) : mode == 1 ? ceil(x) : (x < 0 ? -floor(-x + 0.5) : floor(x + 0.5)));
    }
    if (is_const_expr(a)) {
        double re, im;
        if (eval_complex(a, &re, &im) && im == 0 && fabs(re) < 1e15) {
            double f = mode == 0 ? floor(re) : mode == 1 ? ceil(re) : (re < 0 ? -floor(-re + 0.5) : floor(re + 0.5));
            /* reject if too close to the boundary to be sure */
            double frac = re - floor(re);
            if (frac > 1e-9 && frac < 1 - 1e-9 && fabs(frac - 0.5) > 1e-9) return mk_int((int64_t)f);
        }
    }
    return NULL;
}

/*
 * The function rule table (Cohen's Simplify_function, much extended).
 * Returns the simplified value, or NULL if no rule applies (s_fn then keeps
 * the application).  General rules first:
 *   - one or two numeric arguments with at least one float: evaluate in
 *     double precision via num_fn_double (sqrt/exp/ln are excluded because
 *     s_pow / ln_rules handle complex results);
 *   - parity for odd/even functions of a negative term.
 * Then per-function rules: exact trig values at rational multiples of pi,
 * kpi with k assumed integer, inverse-function compositions
 * (sin(asin x) = x, sin(acos x) = sqrt(1-x^2), ...), shifts by pi/2, pi,
 * 3pi/2; asin/acos/atan tables; acot/asec/acsc rewritten through
 * atan/acos/asin; abs and sign via known signs; floor/ceil/round; re, im,
 * conj by substituting i -> -i (all symbols treated as real) after writing
 * principal roots (-1)^q as cos(pi q) + i sin(pi q); abs and arg of
 * c (-1)^q directly (|c|, pi q); factorial,
 * gamma, binomial, max/min of numeric arguments, mod (floor modulo, mod(a,0)
 * = a), Heaviside (H(0) = 1/2), Dirac, zeta, erf/erfc/erfi/Si/Ci/Ei special
 * values.  May throw CAS_ERR_DOMAIN at poles.
 */
/* (-1)^q with an exact rational q: a principal root of -1 (complex mode;
 * minus1_pow leaves these for q not in {0, 1/2, 1, 3/2}). */
static int is_m1pow(E *e) { return e->t == E_POW && is_num(K0(e)) && num_is_m1(K0(e)) && K1(e)->t == E_RAT; }

/* Does e contain a principal root of -1 (is_m1pow)?  Recursive. */
static int has_m1pow(E *e) {
    stack_check();
    if (is_m1pow(e)) return 1;
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN && e->t != E_LIST) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_m1pow(EK(e)[i])) return 1;
    return 0;
}

/* e with every (-1)^q replaced by its rectangular form cos(pi q) + i sin(pi q)
 * (e^(i pi q)), so re/im/conj, which treat i as the only non-real symbol,
 * see the imaginary part: (-1)^(1/3) -> 1/2 + sqrt(3)/2 i. */
static E *m1pow_rect(E *e) {
    stack_check();
    if (is_m1pow(e)) {
        E *t = s_frac_pi(K1(e));
        return s_add2(s_fn1(F_COS, t), s_mul2(K->i_, s_fn1(F_SIN, t)));
    }
    if (!has_m1pow(e)) return e;
    E **k = salloc(sizeof(E *) * e->n);
    for (uint32_t i = 0; i < e->n; i++) k[i] = m1pow_rect(EK(e)[i]);
    return rebuild(e, k);
}

/* If x is (-1)^q or a product with exactly one factor (-1)^q, return q and
 * set *rest to the other factors (1 for a lone power); else NULL. */
static E *m1pow_factor(E *x, E **rest) {
    if (is_m1pow(x)) {
        *rest = K->c1;
        return K1(x);
    }
    if (x->t != E_MUL) return NULL;
    int at = -1;
    for (uint32_t i = 0; i < x->n; i++)
        if (is_m1pow(EK(x)[i])) {
            if (at >= 0) return NULL;
            at = (int)i;
        }
    if (at < 0) return NULL;
    E **f = salloc(sizeof(E *) * x->n);
    uint32_t k = 0;
    for (uint32_t i = 0; i < x->n; i++)
        if ((int)i != at) f[k++] = EK(x)[i];
    *rest = s_mul(k, f);
    return K1(EK(x)[at]);
}

static E *s_fn_inner(int id, uint32_t n, E **a) {
    stack_check();
    E *x = n ? a[0] : NULL, *q, *rest;
    /* numeric evaluation for float arguments */
    if (n >= 1 && n <= 2 && all_num(n, a) && any_flt(n, a) && id >= F_SQRT && id < S__FIRSTCMD &&
        id != F_SQRT && id != F_EXP && id != F_LN) {
        double v[2] = {num_to_double(a[0]), n > 1 ? num_to_double(a[1]) : 0}, out;
        if (num_fn_double(id, (int)n, v, &out)) return mk_flt(out);
    }
    if (n == 1 && id < S__FIRSTCMD) {
        if (is_odd_fn(id) && is_negative_term(x)) return s_neg(s_fn1(id, s_neg(x)));
        if (is_even_fn(id) && is_negative_term(x)) return s_fn1(id, s_neg(x));
    }
    switch (id) {
    case F_SQRT:
        if (n == 1) return s_sqrt(x);
        if (n == 2) return s_pow(x, s_inv(a[1]));
        break;
    case F_EXP:
        if (n == 1) return s_exp(x);
        break;
    case F_LN:
        if (n == 1) return ln_rules(x);
        /* fall through: ln(x, b) = log(x, b) */
    case F_LOG:
    case F_LOG10:
        if (n == 2) return s_div(s_ln(x), s_ln(a[1]));
        return s_div(s_ln(x), s_ln(mk_int(10)));
    case F_LOG2:
        if (n == 1) return s_div(s_ln(x), s_ln(K->c2));
        break;
    case F_SIN:
        if (n != 1) break;
        if (pi_coef(x, &q)) return sin_exact(q);
        if ((q = drop_period(x, K->c2))) return s_fn1(F_SIN, q);
        if (x->t == E_MUL && ISYM(EK(x)[x->n - 1], S_PI) && is_int_assumed(s_div(x, K->pi_))) return K->c0;
        if (IFN(x, F_ASIN)) return K0(x);
        if (IFN(x, F_ACOS)) return one_minus_sq(K0(x));
        if (IFN(x, F_ATAN)) return s_div(K0(x), one_plus_sq(K0(x)));
        if (split_pi_term(x, &rest, &q)) {
            q = qmod(q, K->c2);
            if (is_zero(q)) return s_fn1(F_SIN, rest);
            if (is_one(q)) return s_neg(s_fn1(F_SIN, rest));
            if (num_eq(q, K->chalf)) return s_fn1(F_COS, rest);
            if (num_eq(q, mk_frac(3, 2))) return s_neg(s_fn1(F_COS, rest));
        }
        break;
    case F_COS:
        if (n != 1) break;
        if (pi_coef(x, &q)) return cos_exact(q);
        if ((q = drop_period(x, K->c2))) return s_fn1(F_COS, q);
        if (x->t == E_MUL && ISYM(EK(x)[x->n - 1], S_PI) && is_int_assumed(s_div(x, K->pi_)))
            return s_pow(K->cm1, s_div(x, K->pi_));
        if (IFN(x, F_ACOS)) return K0(x);
        if (IFN(x, F_ASIN)) return one_minus_sq(K0(x));
        if (IFN(x, F_ATAN)) return s_inv(one_plus_sq(K0(x)));
        if (split_pi_term(x, &rest, &q)) {
            q = qmod(q, K->c2);
            if (is_zero(q)) return s_fn1(F_COS, rest);
            if (is_one(q)) return s_neg(s_fn1(F_COS, rest));
            if (num_eq(q, K->chalf)) return s_neg(s_fn1(F_SIN, rest));
            if (num_eq(q, mk_frac(3, 2))) return s_fn1(F_SIN, rest);
        }
        break;
    case F_TAN:
        if (n != 1) break;
        if (pi_coef(x, &q)) return tan_exact(q);
        if ((q = drop_period(x, K->c1))) return s_fn1(F_TAN, q);
        if (IFN(x, F_ATAN)) return K0(x);
        if (IFN(x, F_ASIN)) return s_div(K0(x), one_minus_sq(K0(x)));
        if (IFN(x, F_ACOS)) return s_div(one_minus_sq(K0(x)), K0(x));
        if (split_pi_term(x, &rest, &q)) {
            q = qmod(q, K->c1);
            if (is_zero(q)) return s_fn1(F_TAN, rest);
            if (num_eq(q, K->chalf)) return s_neg(s_inv(s_fn1(F_TAN, rest)));
        }
        break;
    case F_COT:
        if (n == 1 && pi_coef(x, &q)) {
            q = qmod(q, K->c1);
            if (num_eq(q, K->chalf)) return K->c0;
            if (is_zero(q)) cas_throw(CAS_ERR_DOMAIN, "cot undefined");
            E *t = tan_exact(q);
            return s_inv(t);
        }
        break;
    case F_SEC:
        if (n == 1 && pi_coef(x, &q)) {
            E *c = cos_exact(q);
            if (is_zero(c)) cas_throw(CAS_ERR_DOMAIN, "sec undefined");
            return s_inv(c);
        }
        break;
    case F_CSC:
        if (n == 1 && pi_coef(x, &q)) {
            E *s = sin_exact(q);
            if (is_zero(s)) cas_throw(CAS_ERR_DOMAIN, "csc undefined");
            return s_inv(s);
        }
        break;
    case F_ASIN:
        if (n == 1 && (q = asin_table(x)) != NULL) return s_frac_pi(q);
        if (n == 1 && IFN(x, F_SIN) && pi_coef(K0(x), &q) && num_cmp(num_abs(q), K->chalf) <= 0) return K0(x);
        break;
    case F_ACOS:
        if (n == 1) {
            if (is_negative_term(x) && is_const_expr(x)) {
                E *v = s_fn1(F_ACOS, s_neg(x));
                if (v->t != E_FN) return s_sub(K->pi_, v);
            }
            if ((q = asin_table(x)) != NULL) return s_frac_pi(num_sub(K->chalf, q));
        }
        break;
    case F_ATAN:
        if (n == 1 && (q = atan_table(x)) != NULL) return s_frac_pi(q);
        if (n == 1 && ISYM(x, S_INF)) return s_frac_pi(K->chalf);
        if (n == 1 && IFN(x, F_TAN) && pi_coef(K0(x), &q) && num_cmp(num_abs(q), K->chalf) < 0) return K0(x);
        if (n == 1 && IFN(x, F_TAN) && x->n == 1) {
            /* atan(tan(u)) = u - k pi, k = round(u/pi), for a real constant u (10 -> 10 - 3 pi),
             * and = u when u's interval (assumptions) lies inside (-pi/2, pi/2) */
            E *u = K0(x);
            double uv, lo, hi;
            int los, his;
            if (is_const_expr(u) && freeof_sym(u, S_I) && to_double(u, &uv) && isfinite(uv) && fabs(uv) < 1e9) {
                double k = floor(uv / M_PI + 0.5), rv = uv - k * M_PI;
                if (fabs(rv) < M_PI / 2 - 1e-9) return s_sub(u, s_mul2(mk_int((int64_t)k), K->pi_));
            }
            double h2 = M_PI / 2, t = 1e-12;
            if (!is_const_expr(u) && interval_of(u, &lo, &hi, &los, &his) &&
                (lo > -h2 + t || (lo > -h2 - t && los)) && (hi < h2 - t || (hi < h2 + t && his)))
                return u;
        }
        break;
    case F_ACOT:
        if (n == 1) return s_sub(s_frac_pi(K->chalf), s_fn1(F_ATAN, x));
        break;
    case F_ASEC:
        if (n == 1) return s_fn1(F_ACOS, s_inv(x));
        break;
    case F_ACSC:
        if (n == 1) return s_fn1(F_ASIN, s_inv(x));
        break;
    case F_SINH:
        if (n == 1 && is_zero(x)) return K->c0;
        if (n == 1 && IFN(x, F_ASINH)) return K0(x);
        break;
    case F_COSH:
        if (n == 1 && is_zero(x)) return K->c1;
        if (n == 1 && IFN(x, F_ACOSH)) return K0(x);
        break;
    case F_TANH:
        if (n == 1 && is_zero(x)) return K->c0;
        if (n == 1 && IFN(x, F_ATANH)) return K0(x);
        break;
    case F_ASINH:
    case F_ATANH:
        if (n == 1 && is_zero(x)) return K->c0;
        break;
    case F_ACOSH:
        if (n == 1 && is_one(x)) return K->c0;
        break;
    case F_ABS:
        if (n != 1) break;
        if (is_num(x)) return num_abs(x);
        if (ISYM(x, S_I)) return K->c1;
        if (IFN(x, F_ABS)) return x;
        {
            E *re, *im;
            if (gauss_parts(x, &re, &im)) return s_sqrt(num_add(num_mul(re, re), num_mul(im, im)));
        }
        if (is_poslit(x)) return x;
        if (is_neglit(x)) return s_neg(x);
        {
            /* |(-1)^q| = 1: abs(2 (-1)^(1/3) y) = 2 abs(y) */
            E *r1, *q1 = m1pow_factor(x, &r1);
            if (q1) return s_fn1(F_ABS, r1);
        }
        if (x->t == E_MUL && is_num(K0(x))) return s_mul2(num_abs(K0(x)), s_fn1(F_ABS, s_div(x, K0(x))));
        if (is_const_expr(x) && has_m1pow(x)) {
            /* sums with roots of -1: sqrt(re^2 + im^2) */
            E *re = s_fn1(F_RE, x), *im = s_fn1(F_IM, x);
            return s_sqrt(simplify_full(s_add2(s_powi(re, 2), s_powi(im, 2))));
        }
        break;
    case F_SIGN:
        if (n != 1) break;
        if (is_num(x)) return mk_int(num_sgn(x));
        if (is_poslit(x)) return K->c1;
        if (is_neglit(x)) return K->cm1;
        break;
    case F_FLOOR: if (n == 1) return real_const_floor(x, 0); break;
    case F_CEIL: if (n == 1) return real_const_floor(x, 1); break;
    case F_ROUND: if (n == 1) return real_const_floor(x, 2); break;
    case F_FRAC:
        if (n == 1 && is_num(x)) return num_sub(x, num_floor(x));
        break;
    case F_CONJ:
        if (n == 1 && has_m1pow(x)) x = m1pow_rect(x); /* (-1)^q = cos(pi q) + i sin(pi q) */
        if (n == 1 && freeof_sym(x, S_I)) return x;
        if (n == 1) return subst(x, K->i_, s_neg(K->i_));
        break;
    case F_RE:
        if (n == 1 && has_m1pow(x)) x = m1pow_rect(x);
        if (n == 1 && freeof_sym(x, S_I)) return x;
        if (n == 1) return expand(s_mul2(K->chalf, s_add2(x, subst(x, K->i_, s_neg(K->i_)))));
        break;
    case F_IM:
        if (n == 1 && has_m1pow(x)) x = m1pow_rect(x);
        if (n == 1 && freeof_sym(x, S_I)) return K->c0;
        if (n == 1)
            return expand(s_mul2(s_mul2(K->chalf, s_neg(K->i_)), s_sub(x, subst(x, K->i_, s_neg(K->i_)))));
        break;
    case F_ARG:
        if (n == 1) {
            /* arg(c (-1)^q) = pi q (+ pi for c < 0), reduced into (-pi, pi] */
            E *r1, *q1 = m1pow_factor(x, &r1);
            int sr = q1 && is_const_expr(r1) ? sign_of(r1) : 0;
            if (sr) {
                E *t = sr < 0 ? num_add(q1, K->c1) : q1;
                t = num_add(t, num_mul(K->c2, num_floor(num_div(num_sub(K->c1, t), K->c2))));
                return s_frac_pi(t);
            }
        }
        if (n == 1 && is_const_expr(x)) {
            E *re = s_fn1(F_RE, x), *im = s_fn1(F_IM, x);
            if (is_zero(im)) {
                if (is_poslit(re)) return K->c0;
                if (is_neglit(re)) return K->pi_;
            }
            if (is_zero(re)) {
                if (is_poslit(im)) return s_frac_pi(K->chalf);
                if (is_neglit(im)) return s_frac_pi(mk_frac(-1, 2));
            }
            if (is_poslit(re)) return s_fn1(F_ATAN, s_div(im, re));
            if (is_neglit(re) && has_m1pow(x)) { /* left half plane: atan(im/re) +- pi */
                if (is_poslit(im)) return s_add2(s_fn1(F_ATAN, s_div(im, re)), K->pi_);
                if (is_neglit(im)) return s_sub(s_fn1(F_ATAN, s_div(im, re)), K->pi_);
            }
        }
        break;
    case F_ATAN2:
        if (n == 2) {
            E *y = a[0], *xx = a[1];
            if (is_poslit(xx)) return s_fn1(F_ATAN, s_div(y, xx));
            if (is_zero(xx)) {
                if (is_poslit(y)) return s_frac_pi(K->chalf);
                if (is_neglit(y)) return s_frac_pi(mk_frac(-1, 2));
            }
            if (is_neglit(xx)) {
                if (is_poslit(y) || is_zero(y)) return s_add2(s_fn1(F_ATAN, s_div(y, xx)), K->pi_);
                if (is_neglit(y)) return s_sub(s_fn1(F_ATAN, s_div(y, xx)), K->pi_);
            }
        }
        break;
    /* n! for small non-negative integers; x! = gamma(x + 1) for rationals */
    case F_FACT:
        if (n == 1 && x->t == E_INT) {
            if (x->s) cas_throw(CAS_ERR_DOMAIN, "factorial of negative integer");
            long v;
            if (!num_is_small_int(x, &v)) cas_throw(CAS_ERR_MEMORY, "factorial argument too large");
            return int_fact((uint32_t)v);
        }
        if (n == 1 && x->t == E_RAT) return s_fn1(F_GAMMA, num_add(x, K->c1));
        break;
    case F_GAMMA:
        if (n == 1 && x->t == E_INT) {
            if (int_sgn(x) <= 0) cas_throw(CAS_ERR_DOMAIN, "gamma pole");
            long v;
            if (!num_is_small_int(x, &v)) cas_throw(CAS_ERR_MEMORY, "gamma argument too large");
            return int_fact((uint32_t)(v - 1));
        }
        if (n == 1 && x->t == E_RAT && is_int_val(K1(x), 2)) {
            /* gamma(m + 1/2) = (2m)! / (4^m m!) sqrt(pi) */
            E *m = num_floor(x);
            long mv;
            if (!num_is_small_int(m, &mv) || mv > 5000 || mv < -5000) break;
            E *sp = s_sqrt(K->pi_);
            if (mv >= 0)
                return s_mul2(mk_rat(int_fact((uint32_t)(2 * mv)), int_mul(int_pow(mk_int(4), (uint32_t)mv),
                                                                            int_fact((uint32_t)mv))), sp);
            long k = -mv;
            /* gamma(1/2 - k) = (-4)^k k! / (2k)! sqrt(pi) */
            E *num = int_mul(int_pow(mk_int(-4), (uint32_t)k), int_fact((uint32_t)k));
            return s_mul2(mk_rat(num, int_fact((uint32_t)(2 * k))), sp);
        }
        break;
        /* binomial(x, k) for an integer k <= 100000: 0 for k < 0 or for
         * integer 0 <= x < k; the falling-factorial product
         * x (x-1) ... (x-k+1) / k! evaluated exactly for numeric x, and
         * expanded symbolically for k <= 20. */
    case F_BINOM:
        if (n == 2 && a[1]->t == E_INT) {
            long k;
            if (!num_is_small_int(a[1], &k) || k > 100000) break;
            if (k < 0) return K->c0;
            if (x->t == E_INT && int_sgn(x) >= 0 && int_cmp(a[1], x) > 0) return K->c0;
            if (is_exactnum(x)) {
                E *r = K->c1;
                for (long i = 0; i < k; i++) r = num_div(num_mul(r, num_sub(x, mk_int(i))), mk_int(i + 1));
                return r;
            }
            if (k <= 20) {
                E *r = K->c1;
                for (long i = 0; i < k; i++) r = s_mul2(r, s_sub(x, mk_int(i)));
                return s_mul2(mk_rat(K->c1, int_fact((uint32_t)k)), r);
            }
        }
        break;
        /* max/min only of arguments that evaluate to doubles; the original
         * (exact) argument is returned.  A single list argument is unpacked. */
    case F_MAX:
    case F_MIN:
        if (n >= 1) {
            if (n == 1 && x->t == E_LIST) return s_fn_inner(id, x->n, EK(x));
            E *best = NULL;
            for (uint32_t i = 0; i < n; i++) {
                double v, bv;
                if (!to_double(a[i], &v)) return NULL;
                if (!best || (id == F_MAX ? v > (to_double(best, &bv), bv) : v < (to_double(best, &bv), bv)))
                    best = a[i];
            }
            return best;
        }
        break;
    case F_MOD:
        if (n == 2 && a[0]->t == E_LIST && a[1]->t != E_LIST) {
            E **v = salloc(sizeof(E *) * a[0]->n);
            for (uint32_t i = 0; i < a[0]->n; i++) v[i] = s_fn2(F_MOD, EK(a[0])[i], a[1]);
            return s_list(a[0]->n, v);
        }
        if (n == 2 && is_num(a[0]) && is_num(a[1])) {
            if (num_is_zero(a[1])) return a[0];
            if (a[0]->t == E_INT && a[1]->t == E_INT) return int_mod(a[0], a[1]);
            return num_sub(a[0], num_mul(a[1], num_floor(num_div(a[0], a[1]))));
        }
        break;
    case F_PIECEWISE: { extern E *piecewise_simp(uint32_t n, E **a); return piecewise_simp(n, a); }
    case F_HEAVISIDE:
        if (n == 1 && is_num(x)) return num_sgn(x) > 0 ? K->c1 : num_sgn(x) < 0 ? K->c0 : K->chalf;
        if (n == 1 && sign_of(x)) return sign_of(x) > 0 ? K->c1 : K->c0; /* assumptions, constants */
        break;
    case F_DIRAC:
        if (n == 1 && is_num(x) && !num_is_zero(x)) return K->c0;
        break;
        /* zeta(0) = -1/2; zeta(2k) exact via Bernoulli numbers (sum.c);
         * zeta(-n) from Bernoulli numbers; pole at 1; odd positive values
         * stay symbolic. */
    case F_ZETA:
        if (n == 1 && x->t == E_INT) {
            long v;
            if (num_is_small_int(x, &v)) {
                if (v == 1) cas_throw(CAS_ERR_DOMAIN, "zeta pole at 1");
                if (v == 0) return K->cmhalf;
                if (v > 0 && v % 2 == 0 && v <= 200) return zeta_exact(v);
                if (v < 0 && v >= -200) {
                    /* zeta(-n) = (-1)^n B_{n+1}/(n+1) */
                    long m = -v;
                    E *bn = bernoulli(m + 1);
                    E *r = num_div(bn, mk_int(m + 1));
                    return (m & 1) ? num_neg(r) : r;
                }
            }
        }
        break;
    case F_ERF:
        if (n == 1 && is_zero(x)) return K->c0;
        if (n == 1 && ISYM(x, S_INF)) return K->c1;
        break;
    case F_ERFC:
        if (n == 1 && is_zero(x)) return K->c1;
        if (n == 1 && ISYM(x, S_INF)) return K->c0;
        break;
    case F_ERFI: case F_SI:
        if (n == 1 && is_zero(x)) return K->c0;
        if (n == 1 && ISYM(x, S_INF) && id == F_SI) return s_frac_pi(K->chalf);
        break;
    case F_CI:
        if (n == 1 && ISYM(x, S_INF)) return K->c0;
        break;
    case F_EI:
        if (n == 1 && is_zero(x)) return m_inf();
        if (n == 1 && ISYM(x, S_INF)) return K->inf_;
        break;
    }
    return NULL;
}

/* ---------------------------------------------------------- traversal --- */
/*
 * Rebuild a node of e's type from new (simplified) children k[] through the
 * canonical constructors, i.e. "e with its children replaced, simplified
 * again at the top".  Atomic nodes and rationals are returned unchanged.
 * Used by simp(), subst() and by every module that maps over expressions.
 */
E *rebuild(E *e, E **k) {
    switch (e->t) {
    case E_ADD: return s_add(e->n, k);
    case E_MUL: return s_mul(e->n, k);
    case E_POW: return s_pow(k[0], k[1]);
    case E_FN: return s_fn(e->s, e->n, k);
    case E_LIST: return build(E_LIST, e->n, k);
    case E_RAT: return e;
    default: return e;
    }
}

/*
 * Automatic simplification of a possibly raw tree, bottom-up: children
 * first, then rebuild() with the s_* constructors.  Nodes already flagged
 * F_SIMP are returned immediately (so canonical subtrees cost O(1));
 * atoms get the flag set in place (the one mutation allowed on nodes).
 * Note: a raw DAG with sharing is simplified once per occurrence.
 */
E *simp(E *e) {
    if (e->f & F_SIMP) return e;
    stack_check();
    switch (e->t) {
    case E_INT: case E_RAT: case E_FLT: case E_SYM: case E_STR:
        e->f |= F_SIMP;
        return e;
    default: {
        E **k = salloc(sizeof(E *) * (e->n + 1));
        for (uint32_t i = 0; i < e->n; i++) k[i] = simp(EK(e)[i]);
        return rebuild(e, k);
    }
    }
}

/*
 * Replace every subexpression structurally equal to xs[i] by vs[i] (first
 * match wins; the replacement is not searched again) and re-simplify the
 * changed ancestors through rebuild().  Unchanged subtrees are returned as
 * is (pointer identity), so no allocation happens when nothing matches.
 * Only whole nodes match: subst(x+y+z, x+y, t) does not apply.  Not
 * memoised: shared subexpressions are visited once per occurrence.
 */
static E *subst_rec(E *e, uint32_t n, E **xs, E **vs) {
    for (uint32_t i = 0; i < n; i++)
        if (expr_eq(e, xs[i])) return vs[i];
    if (e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR) return e;
    stack_check();
    /* x is the variable of an unevaluated diff (or antiderivative): replacing
     * it by a non-symbol would give diff(f(0), 0), so keep subst(diff(..), x, v) */
    if ((IFN(e, C_DIFF) || (IFN(e, C_INTEGRATE) && e->n == 2)) && e->n >= 2)
        for (uint32_t i = 0; i < n; i++)
            for (uint32_t j = 1; j < e->n; j++)
                if (xs[i]->t == E_SYM && vs[i]->t != E_SYM && expr_eq(EK(e)[j], xs[i])) {
                    E **x2 = salloc(sizeof(E *) * n), **v2 = salloc(sizeof(E *) * n);
                    uint32_t m = 0;
                    for (uint32_t l = 0; l < n; l++)
                        if (l != i) x2[m] = xs[l], v2[m++] = vs[l];
                    E *a[3] = {subst_rec(e, m, x2, v2), xs[i], vs[i]}, *r = mk_fn(C_SUBST, 3, a);
                    r->f = F_SIMP;
                    return r;
                }
    E **k = salloc(sizeof(E *) * (e->n + 1));
    int ch = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        k[i] = subst_rec(EK(e)[i], n, xs, vs);
        if (k[i] != EK(e)[i]) ch = 1;
    }
    if (!ch) return e;
    return rebuild(e, k);
}

/* subst: e with x replaced by v; subst_many: simultaneous substitution of
 * n pairs (xs[i] -> vs[i]).  Results are canonical. */
E *subst(E *e, E *x, E *v) { return subst_rec(e, 1, &x, &v); }
E *subst_many(E *e, uint32_t n, E **xs, E **vs) { return subst_rec(e, n, xs, vs); }
