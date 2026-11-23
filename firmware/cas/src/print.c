/*
 * print.c -- expression printers: linear text that the parser reads back,
 * and LaTeX for 2D display.
 *
 * Where it fits:
 *   print_expr()  is the inverse of parse() (parse.c).  cas.c calls it to turn
 *                 every evaluation result into the output string; session.c
 *                 uses it to save variables as text records, and many modules
 *                 use it for messages.  The fuzzer (tests/fuzz.c) checks that
 *                 every exact output parses back to the same value, so any
 *                 change here must keep the text unambiguous for parse.c.
 *   print_latex() renders the last result (cas_latex() in cas.c).  It is
 *                 one-way: LaTeX output is never parsed.
 *
 * Both printers are recursive descent over the (immutable, canonical) node
 * DAG, written in the classic "precedence context" style: every call gets the
 * binding strength `ctx` of the position it is printed in, and a node wraps
 * itself in parentheses exactly when its own operator binds more loosely
 * than that context.  The levels (enum below), loosest first:
 *
 *   P_LOW   top level, function arguments, list elements, `;` sequences
 *   P_REL   operands of `and` / `or`  (relations need no parentheses there)
 *   P_SUM   operands of a relation, terms of a sum
 *   P_PROD  a term printed after a '-' sign; a product's factors use P_PROD+1
 *   P_NEG   (= P_PROD + 1) factors of a product
 *   P_POW   a lone denominator after '/', the operand of 1/x^k
 *   P_ATOM  base and exponent of '^', operand of '!'
 *
 * Presentation decisions (the canonical order of simp.c is chosen for fast
 * comparison, not for reading, so the printer reorders):
 *   - Sums print by descending degree in the variables x, y, z first and then
 *     the remaining symbols alphabetically; numbers come last except that a
 *     complex constant prints real part first (3+2*i).  A leading negative
 *     term is swapped with a positive term of equal total degree when there
 *     is one (b^2-4*a*c rather than -4*a*c+b^2).
 *   - Products are split into numeric coefficient, factors with positive
 *     exponent (numerator) and factors with negative rational/integer
 *     exponent (denominator): 3*x*y^(-2) prints as 3*x/y^2.
 *   - x^(1/2) prints as sqrt(x), e^u as exp(u), x^(-k) as 1/x^k.
 *   - Floats print with K->set.digits significant digits (fmt_float_node,
 *     numeric.c), so in approximate mode the output is rounded.
 *
 * Memory: printers allocate only small temporaries in scratch (term arrays,
 * negated terms) and release them with smark/srelease before returning, so
 * printing a huge result does not grow scratch with its size.  Output goes to
 * a bounded sbuf; once it overflows the printers stop recursing (the API
 * reports CAS_ERR_OUTPUT_TRUNC).  Recursion is guarded by stack_check().
 *
 * Globals: g_sv / g_nsv (below) pass the variable order to the comparison
 * function used while sorting the terms of a sum; they are saved and
 * restored around each use, so nested sums sort correctly.
 */
#include "cas_int.h"
#include <string.h>

/* Precedence contexts, loosest binding first (see the file header).  P_NEG is
 * not referenced by name; P_PROD + 1 is used for factors of a product. */
enum { P_LOW = 0, P_REL = 1, P_SUM = 2, P_PROD = 3, P_NEG = 4, P_POW = 5, P_ATOM = 6 };

static void pr(sbuf *b, E *e, int ctx);

/*
 * Print a number node (E_INT, E_RAT or E_FLT) in context ctx.
 * Negative numbers are parenthesised inside products and tighter contexts
 * (x*(-2), (-2)^x); a rational is parenthesised when it is an operand of '^'
 * or a factor, because "1/2" contains a '/' that would otherwise rebind
 * ((1/2)^x, not 1/2^x).  Floats use K->set.digits significant digits (12 if
 * the setting is 0).  No scratch is retained beyond fmt_float_node's own.
 */
static void pr_num(sbuf *b, E *e, int ctx) {
    int neg = num_sgn(e) < 0;
    int paren = (neg && ctx > P_SUM) || (e->t == E_RAT && ctx > P_PROD);
    if (paren) sb_putc(b, '(');
    if (e->t == E_INT)
        int_to_dec(b, e);
    else if (e->t == E_RAT) {
        int_to_dec(b, K0(e));
        sb_putc(b, '/');
        int_to_dec(b, K1(e));
    } else
        fmt_float_node(b, e, K->set.digits > 0 ? K->set.digits : 12);
    if (paren) sb_putc(b, ')');
}

/* 1 if e is the integer -1 (the coefficient of a negated product). */
static int num_is_m1_safe(E *e) { return e->t == E_INT && num_is_m1(e); }

/* 1 if e is the rational sign/2 (is_half(y, 1): exponent 1/2 -> sqrt). */
static int is_half(E *e, int sign) {
    return e->t == E_RAT && is_int_val(K1(e), 2) && is_int_val(K0(e), sign);
}

/* print a factor that appears with a positive exponent inside a product */
static void pr_factor(sbuf *b, E *f) { pr(b, f, P_PROD + 1); }

/*
 * Print a product as  [-] coef * f1 * f2 ... / (d * g1 * g2 ...).
 *
 * Two cases:
 *  1. More than one numeric factor (a number or number^integer).  Canonical
 *     products fold all numbers into one coefficient, so this only happens for
 *     deliberately unsimplified products such as the ifactor result
 *     2^3*3*5; those are printed literally, factor by factor, with a leading
 *     -1 shown as a unary minus.
 *  2. The normal case.  Factors are partitioned:
 *       c    = product of all numeric factors (normally exactly one),
 *       num  = factors printed in the numerator,
 *       den  = factors u^(-k) with an exact negative exponent, rebuilt as
 *              u^k (or u when k = 1).
 *     A float exponent stays in the numerator (x^(-0.5)) so that floats are
 *     printed exactly as stored.  The rebuilt powers are marked F_SIMP so the
 *     recursive print treats them as canonical nodes.  The numerator of c is
 *     printed unless it is 1 and other factors exist; the denominator of a
 *     rational c joins the denominator list.  The denominator gets
 *     parentheses when it has more than one factor (x/(2*y)).
 *
 * Parentheses: the whole product is parenthesised in contexts tighter than
 * P_PROD, and also at P_PROD when it carries a minus sign.
 * Scratch: the num/den arrays, the folded coefficient and the rebuilt powers
 * are released before returning (smark/srelease).
 * Note: when the reduced exponent is 1 the base node K0(f) itself (an
 * existing, already canonical node) gets its F_SIMP flag set.
 */
static void pr_mul(sbuf *b, E *e, int ctx) {
    uint32_t nnum = 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (is_num(EK(e)[i]) || (EK(e)[i]->t == E_POW && is_num(K0(EK(e)[i])) && K1(EK(e)[i])->t == E_INT)) nnum++;
    if (nnum > 1) {
        /* non-canonical product of numbers (integer factorisation): print literally */
        if (ctx > P_PROD) sb_putc(b, '(');
        for (uint32_t i = 0; i < e->n; i++) {
            if (i == 0 && num_is_m1_safe(EK(e)[0]) && e->n > 1) {
                sb_putc(b, '-');
                continue;
            }
            if (i && !(i == 1 && num_is_m1_safe(EK(e)[0]))) sb_putc(b, '*');
            E *f = EK(e)[i];
            if (f->t == E_POW && is_num(K0(f))) {
                pr(b, K0(f), P_ATOM);
                sb_putc(b, '^');
                pr(b, K1(f), P_ATOM);
            } else
                pr(b, f, P_PROD + 1);
        }
        if (ctx > P_PROD) sb_putc(b, ')');
        return;
    }
    /* case 2: split into coefficient, numerator factors, denominator factors */
    char *mark = smark();
    E *c = K->c1;
    uint32_t n = e->n, nn = 0, nd = 0;
    E **num = salloc(sizeof(E *) * (n + 1)), **den = salloc(sizeof(E *) * (n + 1));
    for (uint32_t i = 0; i < n; i++) {
        E *f = EK(e)[i];
        if (is_num(f))
            c = num_mul(c, f);
        else if (f->t == E_POW && is_num(K1(f)) && num_sgn(K1(f)) < 0 && K1(f)->t != E_FLT) {
            E *ne = num_neg(K1(f));
            den[nd++] = is_one(ne) ? K0(f) : mk_pow(K0(f), ne);
            den[nd - 1]->f |= F_SIMP;
        } else
            num[nn++] = f;
    }
    int neg = num_sgn(c) < 0;
    if (neg) c = num_neg(c);
    int paren = ctx > P_PROD || (neg && ctx > P_SUM);
    if (paren) sb_putc(b, '(');
    if (neg) sb_putc(b, '-');
    /* coefficient: a float is printed whole (unless it is 1.0 in front of
     * other factors); for an exact c only its numerator goes here */
    int first = 1;
    if (c->t == E_FLT) {
        if (!(nn > 0 && EF(c) == 1.0)) {
            pr_num(b, c, P_PROD);
            first = 0;
        }
    } else {
        E *cn = num_numer(c);
        if (!int_is_one(cn) || nn == 0) {
            int_to_dec(b, cn);
            first = 0;
        }
    }
    for (uint32_t i = 0; i < nn; i++) {
        if (!first) sb_putc(b, '*');
        pr_factor(b, num[i]);
        first = 0;
    }
    /* denominator: integer part of the coefficient first, then the factors.
     * A single denominator factor is printed at P_POW so that a product or
     * quotient there gets parentheses but a power does not (x/y^2). */
    E *cd = c->t == E_RAT ? K1(c) : NULL;
    uint32_t ndt = nd + (cd ? 1 : 0);
    if (ndt) {
        sb_putc(b, '/');
        if (ndt > 1) sb_putc(b, '(');
        int f2 = 1;
        if (cd) {
            int_to_dec(b, cd);
            f2 = 0;
        }
        for (uint32_t i = 0; i < nd; i++) {
            if (!f2) sb_putc(b, '*');
            pr(b, den[i], ndt > 1 ? P_PROD + 1 : P_POW);
            f2 = 0;
        }
        if (ndt > 1) sb_putc(b, ')');
    }
    if (paren) sb_putc(b, ')');
    srelease(mark);
}

/* Degree of term t in symbol v: 1 for v itself, k for v^k with a numeric k
 * (rational and float exponents included, hence double), the sum over the
 * factors of a product, 0 otherwise.  Only a presentation heuristic: v inside
 * function arguments or under a non-numeric power counts as degree 0. */
static double term_deg(E *t, E *v) {
    stack_check();
    if (t->t == E_SYM) return t->s == v->s ? 1 : 0;
    if (t->t == E_POW && K0(t)->t == E_SYM && K0(t)->s == v->s && is_num(K1(t))) return num_to_double(K1(t));
    if (t->t == E_MUL) {
        double d = 0;
        for (uint32_t i = 0; i < t->n; i++) d += term_deg(EK(t)[i], v);
        return d;
    }
    return 0;
}

/* Variable order for print_term_cmp (a comparison callback cannot take extra
 * arguments).  Set by pr_add / lx around the sort and restored afterwards. */
static E **g_sv;
static uint32_t g_nsv;
void print_reset_statics(void); /* cas.c eval_impl: state an error may have left set */
void print_reset_statics(void) { g_nsv = 0; }

/*
 * Print order of two terms of a sum (result < 0: a goes first).
 * Graded lexicographic by variable: compare the degrees in g_sv[0], then
 * g_sv[1], ...; higher degree first.  Ties: a pure number goes last, except
 * that it goes before an imaginary term c*i so complex numbers print as 3+2*i.
 * Remaining ties fall back to the canonical order expr_cmp, which makes the
 * order total and deterministic.
 */
static int print_term_cmp(E *a, E *b) {
    /* returns <0 if a should be printed before b */
    for (uint32_t i = 0; i < g_nsv; i++) {
        double da = term_deg(a, g_sv[i]), db = term_deg(b, g_sv[i]);
        if (da != db) return da > db ? -1 : 1;
    }
    int an = is_num(a), bn = is_num(b);
    if (an != bn) {
        /* complex constants: real part first (3+2*i) */
        E *o = an ? b : a;
        if (ISYM(o, S_I) || (o->t == E_MUL && o->n == 2 && is_num(K0(o)) && ISYM(K1(o), S_I))) return an ? -1 : 1;
        return an ? 1 : -1;
    }
    return expr_cmp(a, b);
}

/*
 * Print a sum.  Steps:
 *   1. Collect up to 32 symbols of e (collect_syms returns them sorted by
 *      name) and move z, y, x to the front, in that order of moves, giving
 *      the variable order x, y, z, then the others alphabetically.
 *   2. Copy the terms in reverse canonical order (simp.c keeps sums sorted
 *      by increasing expr_cmp, which roughly puts high degrees last) and
 *      stable-insertion-sort them with print_term_cmp.  Insertion sort is
 *      O(n^2) comparisons, each of which walks the terms; sums printed on a
 *      calculator are short, and an almost-sorted input costs only O(n).
 *   3. If the first term is negative, promote the first positive term of the
 *      same total degree (the degree order is preserved).
 *   4. Print, turning "+ (-t)" into "- t": negative terms are negated with
 *      s_neg and printed at P_PROD, so -(a*b) prints as "-a*b" and a product
 *      with a negative sign of its own cannot appear.  A nested sum term is
 *      never written with an outer '-'.
 * Parentheses when ctx is tighter than P_SUM.  Scratch is released.
 */
static void pr_add(sbuf *b, E *e, int ctx) {
    int paren = ctx > P_SUM;
    if (paren) sb_putc(b, '(');
    char *mark = smark();
    E **sv = salloc(sizeof(E *) * 32);
    uint32_t nsv = collect_syms(e, sv, 32);
    /* x, y, z lead the variable order (calculator convention) */
    for (int pass = 2; pass >= 0; pass--) {
        const char *nm = pass == 0 ? "x" : pass == 1 ? "y" : "z";
        for (uint32_t k = 0; k < nsv; k++)
            if (!strcmp(sym_name(sv[k]->s), nm)) {
                E *v = sv[k];
                memmove(sv + 1, sv, sizeof(E *) * k);
                sv[0] = v;
                break;
            }
    }
    E **t = salloc(sizeof(E *) * e->n);
    for (uint32_t k = 0; k < e->n; k++) t[k] = EK(e)[e->n - 1 - k];
    E **osv = g_sv;
    uint32_t onsv = g_nsv;
    g_sv = sv;
    g_nsv = nsv;
    /* insertion sort (stable) */
    for (uint32_t i = 1; i < e->n; i++)
        for (uint32_t j = i; j > 0 && print_term_cmp(t[j - 1], t[j]) > 0; j--) {
            E *x = t[j]; t[j] = t[j - 1]; t[j - 1] = x;
        }
    g_sv = osv;
    g_nsv = onsv;
    /* start with a positive term of the same total degree when there is one
     * (b^2-4*a*c rather than -4*a*c+b^2; descending degree order is kept) */
    if (is_negative_term(t[0])) {
        double d0 = 0;
        for (uint32_t q = 0; q < nsv; q++) d0 += term_deg(t[0], sv[q]);
        for (uint32_t k = 1; k < e->n; k++) {
            double dk = 0;
            for (uint32_t q = 0; q < nsv; q++) dk += term_deg(t[k], sv[q]);
            if (dk != d0) break;
            if (!is_negative_term(t[k])) {
                E *x = t[k];
                memmove(t + 1, t, sizeof(E *) * k);
                t[0] = x;
                break;
            }
        }
    }
    for (uint32_t k = 0; k < e->n; k++) {
        E *term = t[k];
        if (k > 0) {
            if (is_negative_term(term) && term->t != E_ADD) {
                sb_putc(b, '-');
                char *m2 = smark();
                E *nt = s_neg(term);
                pr(b, nt, P_PROD);
                srelease(m2);
                continue;
            }
            sb_putc(b, '+');
        }
        pr(b, term, P_SUM);
    }
    srelease(mark);
    if (paren) sb_putc(b, ')');
}

/* Print "(a_from,...,a_{n-1})": the arguments of a function call, each at
 * P_LOW since the commas delimit them. */
static void pr_args(sbuf *b, E *e, uint32_t from) {
    sb_putc(b, '(');
    for (uint32_t i = from; i < e->n; i++) {
        if (i > from) sb_putc(b, ',');
        pr(b, EK(e)[i], P_LOW);
    }
    sb_putc(b, ')');
}

/*
 * Print a power x^y.  Special forms, in order:
 *   e^y      -> exp(y)        (always, so the output never depends on how
 *                              the parser treats the symbol e)
 *   x^(1/2)  -> sqrt(x)
 *   x^(-k)   -> 1/x^k  (exact negative exponent; 1/x when k = 1).  This is
 *                       reached for a power standing alone; inside products
 *                       pr_mul already moved such factors to the denominator.
 *   otherwise x^y with both operands at P_ATOM, so any compound base or
 *   exponent is parenthesised: (x+1)^2, x^(n+1), x^(2/3), (-2)^x.
 * The rebuilt x^k is a temporary scratch node (not released here; it is
 * small and dies with the caller's mark).
 */
static void pr_pow(sbuf *b, E *e, int ctx) {
    E *x = K0(e), *y = K1(e);
    if (ISYM(x, S_E)) {
        sb_puts(b, "exp(");
        pr(b, y, P_LOW);
        sb_putc(b, ')');
        return;
    }
    if (is_half(y, 1)) {
        sb_puts(b, "sqrt(");
        pr(b, x, P_LOW);
        sb_putc(b, ')');
        return;
    }
    if (is_num(y) && num_sgn(y) < 0 && y->t != E_FLT) {
        /* 1/x^k */
        int paren = ctx > P_PROD;
        if (paren) sb_putc(b, '(');
        sb_puts(b, "1/");
        E *ne = num_neg(y);
        if (is_one(ne))
            pr(b, x, P_POW);
        else {
            E *p = mk_pow(x, ne);
            p->f |= F_SIMP;
            pr(b, p, P_POW);
        }
        if (paren) sb_putc(b, ')');
        return;
    }
    int paren = ctx > P_POW;
    if (paren) sb_putc(b, '(');
    pr(b, x, P_ATOM);
    sb_putc(b, '^');
    pr(b, y, P_ATOM);
    if (paren) sb_putc(b, ')');
}

/* Infix spelling of a relation / assignment function id, NULL for others. */
static const char *rel_op(int id) {
    switch (id) {
    case F_EQ: return "=";
    case F_LT: return "<";
    case F_GT: return ">";
    case F_LE: return "<=";
    case F_GE: return ">=";
    case F_NE: return "!=";
    case F_ASSIGN: return ":=";
    }
    return NULL;
}

/*
 * Main text printer: dispatch on the node type and print e in context ctx.
 * Function nodes that the parser builds from operator syntax are printed back
 * in that syntax so the output round-trips:
 *   relations / :=      a=b, a<=b, f(x):=...  (parenthesised inside tighter
 *                       contexts, e.g. inside a sum: (x+1=3)-1 style input)
 *   factorial(n)        n!
 *   derivative(f,k,a)   f''(a)  (k primes; parse.c builds this node from the
 *                       f'(a) syntax and eval.c resolves it when it can)
 *   and / or            a and b, operands at P_REL
 *   not(a), a;b;c
 * Sums with uncertainty sources and pm(v,s) calls print as v±s (unc_print,
 * unc.c).
 * Everything else prints as name(args).  Products first try units_print
 * (units.c), which renders value*unit products as 29.4_m/s and returns 0
 * when the product is not a quantity.  Lists print as [a,b,...]; matrices
 * are lists of row lists.  Strings print between double quotes.
 * Returns early (printing nothing more) once the buffer has overflowed.
 * Throws CAS_ERR_MEMORY via stack_check on pathological nesting.
 */
static void pr(sbuf *b, E *e, int ctx) {
    stack_check();
    if (b->overflow) return;
    switch (e->t) {
    case E_INT: case E_RAT: case E_FLT: pr_num(b, e, ctx); return;
    case E_SYM: sb_puts(b, sym_name(e->s)); return;
    case E_STR:
        sb_putc(b, '"');
        sb_puts(b, ESTR(e));
        sb_putc(b, '"');
        return;
    case E_ADD: { extern int unc_print(sbuf *b, E *e, int ctx); if (!unc_print(b, e, ctx)) pr_add(b, e, ctx); return; }
    case E_MUL: { extern int units_print(sbuf *b, E *e, int ctx); if (!units_print(b, e, ctx)) pr_mul(b, e, ctx); return; }
    case E_POW: pr_pow(b, e, ctx); return;
    case E_LIST:
        sb_putc(b, '[');
        for (uint32_t i = 0; i < e->n; i++) {
            if (i) sb_putc(b, ',');
            pr(b, EK(e)[i], P_LOW);
        }
        sb_putc(b, ']');
        return;
    case E_FN: {
        const char *op = rel_op(e->s);
        if (op && e->n == 2) {
            int paren = ctx > P_REL;
            if (paren) sb_putc(b, '(');
            pr(b, K0(e), P_SUM);
            sb_puts(b, op);
            pr(b, K1(e), P_SUM);
            if (paren) sb_putc(b, ')');
            return;
        }
        if (e->s == F_FACT && e->n == 1) {
            pr(b, K0(e), P_ATOM);
            sb_putc(b, '!');
            return;
        }
        if (e->s == F_DERIV && e->n == 3 && K0(e)->t == E_SYM && K1(e)->t == E_INT) {
            long k = 0;
            num_is_small_int(K1(e), &k);
            sb_puts(b, sym_name(K0(e)->s));
            for (long q = 0; q < k && q < 100; q++) sb_putc(b, '\'');
            sb_putc(b, '(');
            pr(b, EK(e)[2], P_LOW);
            sb_putc(b, ')');
            return;
        }
        if ((e->s == F_AND || e->s == F_OR) && e->n == 2) {
            /* no parentheses around a left operand with the same operator (left associative: it
             * parses back to the same tree) */
            int paren = ctx > P_LOW;
            if (paren) sb_putc(b, '(');
            pr(b, K0(e), IFN(K0(e), e->s) && K0(e)->n == 2 ? P_LOW : P_REL);
            sb_puts(b, e->s == F_AND ? " and " : " or ");
            pr(b, K1(e), P_REL);
            if (paren) sb_putc(b, ')');
            return;
        }
        if (e->s == F_NOT && e->n == 1) {
            sb_puts(b, "not(");
            pr(b, K0(e), P_LOW);
            sb_putc(b, ')');
            return;
        }
        if (e->s == F_SEQ) {
            for (uint32_t i = 0; i < e->n; i++) {
                if (i) sb_putc(b, ';');
                pr(b, EK(e)[i], P_LOW);
            }
            return;
        }
        { extern int unc_print(sbuf *b, E *e, int ctx); if (unc_print(b, e, ctx)) return; } /* pm(v,s): v±s */
        sb_puts(b, sym_name(e->s));
        pr_args(b, e, 0);
        return;
    }
    default: sb_puts(b, "?"); return;
    }
}

/* The result of a top-level series/taylor command (cas.c), in textbook
 * form: the terms c*v^q in ascending order with the coefficient in front
 * (x-1/6*x^3, sin(1)/2*(x-1)^2), then the order term +O(v^n).  d is
 * series.c's g_sdisp [o, x, c0, p0, c1, p1, ...]; coefficients that contain
 * x (ln x of a log series) and powers of 1/x print as one product.  In
 * approximate mode the coefficients are evaluated (the powers stay exact). */
void print_series(sbuf *b, E *d) {
    uint32_t nt = (d->n - 2) / 2;
    for (uint32_t k = 0; k < nt; k++) {
        E *c = EK(d)[2 + 2 * k], *p = EK(d)[3 + 2 * k];
        if (!K->set.exact) c = evalf_digits(c, K->set.digits);
        int neg = c->t != E_ADD && is_negative_term(c); /* (a sum keeps its signs, in parentheses) */
        if (neg) c = s_neg(c);
        if (k || neg) sb_putc(b, neg ? '-' : '+');
        if (is_one(p))
            pr(b, c, neg ? P_PROD : P_SUM);
        else if (is_one(c))
            pr(b, p, k || neg ? P_PROD : P_SUM);
        else if (!freeof(c, EK(d)[1]) || (p->t == E_POW && is_negative_term(K1(p))))
            pr(b, s_mul2(c, p), P_PROD);
        else {
            pr(b, c, P_PROD);
            sb_putc(b, '*');
            pr(b, p, P_PROD + 1);
        }
    }
    sb_puts(b, nt ? "+O(" : "O(");
    pr(b, EK(d)[0], P_LOW);
    sb_putc(b, ')');
}

/* Public entry: append the linear text form of e to b (see file header). */
void print_expr(sbuf *b, E *e) { pr(b, e, P_LOW); }

/* ------------------------------------------------------------ LaTeX --- *
 * The LaTeX printer mirrors the text printer (same precedence contexts and
 * term order, minus the x/y/z preference and the positive-first rule of
 * pr_add) but emits \frac, \sqrt[n]{}, \left( \right), pmatrix and the usual
 * operator names.  Its output is not parsed back. */
static void lx(sbuf *b, E *e, int ctx);

/* Symbol names rendered as Greek letters (\alpha ...); NULL-terminated.
 * pi is handled separately; "gamma" as a symbol is \gamma, while the gamma
 * function F_GAMMA is printed as \Gamma via lx_fname. */
static const char *greek[] = {"alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta", "theta", "iota",
                              "kappa", "lambda", "mu", "nu", "xi", "rho", "sigma", "tau", "phi", "chi",
                              "psi", "omega", "Gamma", "Delta", "Theta", "Lambda", "Sigma", "Phi", "Omega", 0};

/*
 * LaTeX for a symbol: pi -> \pi, inf -> \infty, Greek names -> \name,
 * single letters as is, a letter followed by digits as a subscript
 * (x12 -> x_{12}), anything else upright as \mathrm{name} with '_' escaped.
 */
static void lx_sym(sbuf *b, int id) {
    const char *n = sym_name(id);
    if (id == S_PI) { sb_puts(b, "\\pi"); return; }
    if (id == S_INF) { sb_puts(b, "\\infty"); return; }
    if (id == S_E) { sb_puts(b, "e"); return; }
    for (int i = 0; greek[i]; i++)
        if (!strcmp(n, greek[i])) {
            sb_putc(b, '\\');
            sb_puts(b, n);
            return;
        }
    if (n[0] && n[1] == 0) { sb_puts(b, n); return; }
    /* x1 -> x_{1} */
    size_t len = strlen(n), k = len;
    while (k > 0 && n[k - 1] >= '0' && n[k - 1] <= '9') k--;
    if (k == 1 && k < len) {
        sb_putc(b, n[0]);
        sb_puts(b, "_{");
        sb_puts(b, n + 1);
        sb_putc(b, '}');
        return;
    }
    sb_puts(b, "\\mathrm{");
    for (const char *p = n; *p; p++) {
        if (*p == '_') sb_putc(b, '\\');
        sb_putc(b, *p);
    }
    sb_putc(b, '}');
}

/*
 * LaTeX for a number: integers in decimal, rationals as \frac{p}{q} with the
 * sign in front, floats formatted as in text mode and then rewritten from
 * 1.5e-7 to 1.5\times 10^{-7}.  Negative numbers get \left( \right) in
 * contexts tighter than P_SUM.  Unlike pr_num, a rational is not
 * parenthesised as the base of a power.
 * Scratch: a 4096-byte temporary for the float text (enough for the
 * maximum of 1000 digits); it is not released here but by the caller's mark.
 */
static void lx_num(sbuf *b, E *e, int ctx) {
    int neg = num_sgn(e) < 0;
    int paren = neg && ctx > P_SUM;
    if (paren) sb_puts(b, "\\left(");
    if (e->t == E_INT) int_to_dec(b, e);
    else if (e->t == E_RAT) {
        if (neg) sb_putc(b, '-');
        sb_puts(b, "\\frac{");
        int_to_dec(b, int_abs(K0(e)));
        sb_puts(b, "}{");
        int_to_dec(b, K1(e));
        sb_putc(b, '}');
    } else {
        char *tmp = salloc(4096);
        sbuf t = {tmp, 4096, 0, 0};
        fmt_float_node(&t, e, K->set.digits > 0 ? K->set.digits : 12);
        char *ep = strchr(tmp, 'e');
        if (ep) {
            *ep = 0;
            sb_puts(b, tmp);
            sb_puts(b, "\\times 10^{");
            sb_puts(b, ep + 1);
            sb_putc(b, '}');
        } else
            sb_puts(b, tmp);
    }
    if (paren) sb_puts(b, "\\right)");
}

/* LaTeX operator name for functions that have one (\sin, \ln, \Gamma ...),
 * NULL otherwise (those print as \operatorname{name}). */
static const char *lx_fname(int id) {
    switch (id) {
    case F_SIN: return "\\sin"; case F_COS: return "\\cos"; case F_TAN: return "\\tan";
    case F_COT: return "\\cot"; case F_SEC: return "\\sec"; case F_CSC: return "\\csc";
    case F_SINH: return "\\sinh"; case F_COSH: return "\\cosh"; case F_TANH: return "\\tanh";
    case F_ASIN: return "\\arcsin"; case F_ACOS: return "\\arccos"; case F_ATAN: return "\\arctan";
    case F_LN: return "\\ln"; case F_LOG: return "\\log"; case F_EXP: return "\\exp";
    case F_GAMMA: return "\\Gamma"; case F_ARG: return "\\arg"; case F_MAX: return "\\max";
    case F_MIN: return "\\min";
    }
    return NULL;
}

/* Print e as a TeX group {...} (exponents, \sqrt and \frac arguments). */
static void lx_group(sbuf *b, E *e) {
    sb_putc(b, '{');
    lx(b, e, P_LOW);
    sb_putc(b, '}');
}

/*
 * LaTeX for a product.  Same split as pr_mul (numeric coefficient,
 * numerator factors, factors with exact negative exponent moved to the
 * denominator); when there is a denominator the whole product becomes
 * [-]\frac{coef f1 f2}{d g1 g2}.  Factors are separated by a thin space
 * (implicit multiplication), or by \cdot before a numeric-base power such as
 * 2^{x}, where juxtaposition would read as digits.  Scratch is released.
 */
static void lx_mul(sbuf *b, E *e, int ctx) {
    char *mark = smark();
    E *c = K->c1;
    uint32_t nn = 0, nd = 0;
    E **num = salloc(sizeof(E *) * (e->n + 1)), **den = salloc(sizeof(E *) * (e->n + 1));
    for (uint32_t i = 0; i < e->n; i++) {
        E *f = EK(e)[i];
        if (is_num(f)) c = num_mul(c, f);
        else if (f->t == E_POW && is_num(K1(f)) && num_sgn(K1(f)) < 0 && K1(f)->t != E_FLT) {
            E *ne = num_neg(K1(f));
            E *p = is_one(ne) ? K0(f) : mk_pow(K0(f), ne);
            p->f |= F_SIMP;
            den[nd++] = p;
        } else
            num[nn++] = f;
    }
    int neg = num_sgn(c) < 0;
    if (neg) c = num_neg(c);
    int paren = ctx > P_PROD;
    if (paren) sb_puts(b, "\\left(");
    if (neg) sb_putc(b, '-');
    E *cn = c->t == E_FLT ? c : num_numer(c), *cd = c->t == E_RAT ? K1(c) : NULL;
    int frac = nd > 0 || cd;
    if (frac) sb_puts(b, "\\frac{");
    int first = 1;
    if (!(is_one(cn) && nn > 0)) {
        lx_num(b, cn, P_PROD);
        first = 0;
    }
    for (uint32_t i = 0; i < nn; i++) {
        if (!first) sb_puts(b, (is_num(num[i]) || (num[i]->t == E_POW && is_num(K0(num[i])))) ? " \\cdot " : " ");
        lx(b, num[i], P_PROD + 1);
        first = 0;
    }
    if (frac) {
        sb_puts(b, "}{");
        int f2 = 1;
        if (cd) {
            int_to_dec(b, cd);
            f2 = 0;
        }
        for (uint32_t i = 0; i < nd; i++) {
            if (!f2) sb_putc(b, ' ');
            lx(b, den[i], nd + (cd ? 1 : 0) > 1 ? P_PROD + 1 : P_PROD);
            f2 = 0;
        }
        sb_putc(b, '}');
    }
    if (paren) sb_puts(b, "\\right)");
    srelease(mark);
}

/*
 * Main LaTeX printer (dispatch on node type, precedence context ctx as in
 * pr).  Notable renderings:
 *   sums        sorted with print_term_cmp, "+ (-t)" printed as "- t"
 *   powers      e^{u}; x^{1/n} as \sqrt[n]{x}; x^{-k} as \frac{1}{x^k};
 *               f(u)^k as f^{k}\left(u\right) for named functions (sin^2 x)
 *   lists       a rectangular list of lists as a pmatrix, otherwise [a, b]
 *   functions   relations (\le, \ne, \land ...), |x|, n!, floor/ceil
 *               brackets, the inert integrate(f,x[,a,b]) left unevaluated by
 *               integ.c as \int_a^b f\,dx, otherwise \name or
 *               \operatorname{name} followed by \left( args \right).
 * Stops once the buffer has overflowed; stack_check guards deep nesting.
 */
static void lx(sbuf *b, E *e, int ctx) {
    stack_check();
    if (b->overflow) return;
    switch (e->t) {
    case E_INT: case E_RAT: case E_FLT: lx_num(b, e, ctx); return;
    case E_SYM: lx_sym(b, e->s); return;
    case E_STR: sb_puts(b, "\\text{"); sb_puts(b, ESTR(e)); sb_putc(b, '}'); return;
    case E_ADD: {
        int paren = ctx > P_SUM;
        if (paren) sb_puts(b, "\\left(");
        char *mark = smark();
        E **sv = salloc(sizeof(E *) * 32);
        uint32_t nsv = collect_syms(e, sv, 32);
        E **t = salloc(sizeof(E *) * e->n);
        for (uint32_t k = 0; k < e->n; k++) t[k] = EK(e)[e->n - 1 - k];
        E **osv = g_sv;
        uint32_t onsv = g_nsv;
        g_sv = sv;
        g_nsv = nsv;
        for (uint32_t i = 1; i < e->n; i++)
            for (uint32_t j = i; j > 0 && print_term_cmp(t[j - 1], t[j]) > 0; j--) {
                E *x = t[j]; t[j] = t[j - 1]; t[j - 1] = x;
            }
        g_sv = osv;
        g_nsv = onsv;
        for (uint32_t k = 0; k < e->n; k++) {
            if (k > 0) {
                if (is_negative_term(t[k]) && t[k]->t != E_ADD) {
                    sb_putc(b, '-');
                    lx(b, s_neg(t[k]), P_PROD);
                    continue;
                }
                sb_putc(b, '+');
            }
            lx(b, t[k], P_SUM);
        }
        srelease(mark);
        if (paren) sb_puts(b, "\\right)");
        return;
    }
    case E_MUL: lx_mul(b, e, ctx); return;
    case E_POW: {
        E *x = K0(e), *y = K1(e);
        if (ISYM(x, S_E)) {
            sb_puts(b, "e^");
            lx_group(b, y);
            return;
        }
        if (y->t == E_RAT && is_int_val(K0(y), 1)) {
            sb_puts(b, "\\sqrt");
            if (!is_int_val(K1(y), 2)) {
                sb_putc(b, '[');
                int_to_dec(b, K1(y));
                sb_putc(b, ']');
            }
            lx_group(b, x);
            return;
        }
        if (is_num(y) && num_sgn(y) < 0 && y->t != E_FLT) {
            sb_puts(b, "\\frac{1}{");
            E *ne = num_neg(y);
            E *p = is_one(ne) ? x : mk_pow(x, ne);
            p->f |= F_SIMP;
            lx(b, p, P_LOW);
            sb_putc(b, '}');
            return;
        }
        int paren = ctx > P_POW;
        if (paren) sb_puts(b, "\\left(");
        if (x->t == E_FN && lx_fname(x->s) && x->n == 1 && is_num(y)) {
            /* sin^2(x) */
            sb_puts(b, lx_fname(x->s));
            sb_putc(b, '^');
            lx_group(b, y);
            sb_puts(b, "\\left(");
            lx(b, K0(x), P_LOW);
            sb_puts(b, "\\right)");
        } else {
            lx(b, x, P_ATOM);
            sb_putc(b, '^');
            lx_group(b, y);
        }
        if (paren) sb_puts(b, "\\right)");
        return;
    }
    case E_LIST: {
        uint32_t r, c;
        if (is_matrix(e, &r, &c)) {
            sb_puts(b, "\\begin{pmatrix}");
            for (uint32_t i = 0; i < r; i++) {
                if (i) sb_puts(b, "\\\\");
                for (uint32_t j = 0; j < c; j++) {
                    if (j) sb_putc(b, '&');
                    lx(b, EK(EK(e)[i])[j], P_LOW);
                }
            }
            sb_puts(b, "\\end{pmatrix}");
            return;
        }
        sb_puts(b, "\\left[");
        for (uint32_t i = 0; i < e->n; i++) {
            if (i) sb_puts(b, ", ");
            lx(b, EK(e)[i], P_LOW);
        }
        sb_puts(b, "\\right]");
        return;
    }
    case E_FN: {
        static const struct {
            int id;
            const char *op;
        } rels[] = {{F_EQ, "="}, {F_LT, "<"}, {F_GT, ">"}, {F_LE, "\\le "}, {F_GE, "\\ge "}, {F_NE, "\\ne "},
                    {F_ASSIGN, ":="}, {F_AND, "\\land "}, {F_OR, "\\lor "}};
        for (size_t i = 0; i < sizeof rels / sizeof rels[0]; i++)
            if (e->s == rels[i].id && e->n == 2) {
                lx(b, K0(e), P_SUM);
                sb_puts(b, rels[i].op);
                lx(b, K1(e), P_SUM);
                return;
            }
        if (e->s == F_ABS && e->n == 1) {
            sb_puts(b, "\\left|");
            lx(b, K0(e), P_LOW);
            sb_puts(b, "\\right|");
            return;
        }
        if (e->s == F_FACT && e->n == 1) {
            lx(b, K0(e), P_ATOM);
            sb_putc(b, '!');
            return;
        }
        if (e->s == F_FLOOR && e->n == 1) {
            sb_puts(b, "\\left\\lfloor ");
            lx(b, K0(e), P_LOW);
            sb_puts(b, "\\right\\rfloor ");
            return;
        }
        if (e->s == F_CEIL && e->n == 1) {
            sb_puts(b, "\\left\\lceil ");
            lx(b, K0(e), P_LOW);
            sb_puts(b, "\\right\\rceil ");
            return;
        }
        if (e->s == C_INTEGRATE && (e->n == 2 || e->n == 4)) {
            sb_puts(b, "\\int");
            if (e->n == 4) {
                sb_puts(b, "_");
                lx_group(b, EK(e)[2]);
                sb_puts(b, "^");
                lx_group(b, EK(e)[3]);
            }
            sb_putc(b, ' ');
            lx(b, K0(e), P_PROD);
            sb_puts(b, "\\,d");
            lx(b, K1(e), P_ATOM);
            return;
        }
        const char *fn = lx_fname(e->s);
        if (fn) sb_puts(b, fn);
        else {
            sb_puts(b, "\\operatorname{");
            for (const char *p = sym_name(e->s); *p; p++) {
                if (*p == '_') sb_putc(b, '\\');
                sb_putc(b, *p);
            }
            sb_putc(b, '}');
        }
        sb_puts(b, "\\left(");
        for (uint32_t i = 0; i < e->n; i++) {
            if (i) sb_puts(b, ", ");
            lx(b, EK(e)[i], P_LOW);
        }
        sb_puts(b, "\\right)");
        return;
    }
    default: sb_putc(b, '?');
    }
}

/* Public entry (declared in cas.c, used by cas_latex): append LaTeX for e. */
void print_latex(sbuf *b, E *e) { lx(b, e, P_LOW); }
