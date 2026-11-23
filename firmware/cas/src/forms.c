/*
 * forms.c -- alternate forms: forms(e [, detail [, budget]]) lists distinct
 * expressions equivalent to e that are about as simple as e or simpler
 * (like the "Alternate forms" of computer algebra web front ends).
 *
 * Method ("generate, verify, rank"):
 *   1. Candidates: every rewriting of the table `ops` below that applies to
 *      e (simplify, rational normal form, factor, expand, partfrac and
 *      collect in each variable, texpand, tlin, trigsimp, trig2exp,
 *      exp2trig, lnexpand, lncollect, expexpand, rationalised denominator),
 *      then the same rewritings once more on each result that survived step
 *      2 (depth <= 2), cheapest results first.  tcollect is the same
 *      transformation as tlin (trig.c) and powexpand the same as expand, so
 *      they are not repeated; powers of the same base are combined by the
 *      automatic simplifier already.
 *   2. Dedupe by canonical form (expr_eq on the simplified results), then a
 *      numeric spot check against the original at deterministic
 *      pseudo-random points: FP_POS points where every variable is positive,
 *      FP_REAL points with mixed signs and FP_CPLX complex points, evaluated
 *      with principal branches (numeric.c eval_complex).  A candidate must
 *      agree at the positive points; if it fails at the mixed-sign points it
 *      is kept with the condition "x>0", if it fails only at the complex
 *      points with "x real" (abs(x)^2 vs x^2, ln(x*y) vs ln(x)+ln(y)).
 *      Excluded points come from denominators: a factor of a denominator
 *      (also cos u of tan u / sec u and sin u of cot u / csc u) that only one
 *      of the two expressions has is reported as "x≠1" (a linear factor in
 *      one variable) or "f≠0": (x^2-1)/(x-1) = x+1 for x≠1.
 *   3. Rank by the cost() measure of simplify.c (weighted node count).  The
 *      forms whose cost is within FORMS_TOL_PCT % of the minimum are
 *      "equally simple" and come first; the others follow, each group by
 *      cost and then in the order found.
 *
 * Budget: a work budget counted in poll ticks (one tick = 1024 allocations
 * or recursion checks, cas_poll_step; on the host roughly 15-30 us of
 * work), FORMS_TICKS by default (about 60-80 ms on the host, about 1 s on
 * the MCU), and a memory bound (half of the free scratch).  forms installs
 * its own poll callback for the run and chains the application's one, so
 * an application time box (or a BREAK) also ends the search early.  When
 * the budget runs out the forms verified so far are returned; nothing is
 * an error unless e itself is unusable.
 *
 * Result: forms(e) is the list of forms; forms(e, 1) is a list of
 * [form, cost, equally_simple (0/1), "condition"] ("" when the forms are
 * equal wherever both are defined), which the calculator's forms browser
 * (app/forms_ui.c) reads.  The input itself is always one of the forms.
 * Lists, equations and atoms come back as the one-element list [e].
 *
 * Memory: every candidate is computed above a scratch mark and kept with
 * skeep() (garbage of the rewriting dropped) or released; the tables are
 * scratch arrays allocated before the search.  State of the search lives
 * in scratch, reached through the static pointer fsp (the library is
 * single-threaded), so that it survives the longjmp of an interrupt.
 *
 * Hooks: xcmds.c (name "forms" in xtab), simplify.c (simplify_cost,
 * simplify_rationalize).
 */
#include "cas_int.h"
#include <math.h>
#include <string.h>

/* Firmware builds (autoconf.h -include'd) without CONFIG_CAS_FORMS (the
 * core tier, whose flash bank 2 is full) get only a stub; the host library
 * (cas/Makefile, no autoconf) always has the command. */
#if defined(CONFIG_CAS_SRC) && !defined(CONFIG_CAS_FORMS)
E *forms_cmd(uint32_t n, E **a) {
    (void)n, (void)a;
    cas_throw(CAS_ERR_UNSUPPORTED, "forms: not in this build");
}
#else

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

#define FORMS_TOL_PCT 20 /* "equally simple": cost <= min * (1 + 20 %) */
#define FORMS_TICKS 800 /* default work budget (poll ticks) */
#define FORMS_MAX 24     /* forms returned at most */
#define SEEN_MAX 96      /* distinct candidates remembered (kept or not) */
#define NV_MAX 6         /* variables substituted in the spot check */
#define DEN_MAX 10       /* denominator factors compared per expression */
#define FP_POS 3
#define FP_REAL 3
#define FP_CPLX 2
#define NPTS (FP_POS + FP_REAL + FP_CPLX)

/* simplify.c / trig.c / xcmds.c */
size_t simplify_cost(E *e);
E *simplify_rationalize(E *e);
int has_trig(E *e);
E *trig_expand(E *e);
E *trig_linearize(E *e);
E *trig_simplify(E *e);
E *trig_to_exp(E *e);
E *exp_to_trig(E *e);
E *ln_expand(E *e);
E *ln_collect(E *e);
E *exp_expand(E *e);
uint32_t collect_syms(E *e, E **out, uint32_t cap);

/* The rewritings.  PV: applied once per variable (the variable is the
 * second argument). */
enum {
    OP_SIMPLIFY, OP_NORMAL, OP_FACTOR, OP_EXPAND, OP_PARTFRAC, OP_COLLECT, OP_TEXPAND, OP_TLIN, OP_TRIGSIMP,
    OP_TRIG2EXP, OP_EXP2TRIG, OP_LNEXPAND, OP_LNCOLLECT, OP_EXPEXPAND, OP_RATIONALIZE, OP_N
};

/* what an expression contains (kinds_of), deciding which ops apply */
enum { K_TRIG = 1, K_HYP = 2, K_EXP = 4, K_LN = 8, K_ROOT = 16, K_DEN = 32 };

/* One stored candidate: the form, its condition string (NULL: none), its
 * cost, whether it passed the check, the op that made it, its order. */
typedef struct {
    E *f, *cond;
    uint32_t cost;
    uint32_t hash; /* of the printed form (dedupe) */
    uint8_t ok, op;
    uint16_t ord;
} cand_t;

/* Search state, in scratch (fsp, allocated by forms_cmd: no RAM is taken
 * outside a search) and reached through a static pointer, so that it is
 * valid after a longjmp. */
static struct fs_t {
    cand_t *c;               /* SEEN_MAX candidates, c[0] = the input */
    int n, nok;              /* candidates, verified ones */
    E *vars[NV_MAX];         /* the variables of e */
    int nv;
    double pre[NPTS][NV_MAX], pim[NPTS][NV_MAX]; /* the points */
    double ore[NPTS], oim[NPTS];                 /* e at the points */
    uint8_t ook[NPTS];                           /* e is defined there */
    E *oden[DEN_MAX];                            /* denominator factors of e */
    int noden;
    unsigned ticks, max_ticks;
    char *mark0;      /* scratch top at the start */
    char *clean;      /* scratch top before the candidate being made */
    size_t mem_max;   /* scratch the search may use */
    int stop;         /* the budget (or the application) ended the search */
    int (*outer)(void *);
    void *outer_user;
} *fsp;
#define fs (*fsp)

/* Poll callback for the run: count ticks, check the memory bound, ask the
 * application's callback; any of them ends the search (K->intr is set by
 * cas_poll_step when this returns 1). */
static int forms_poll(void *user) {
    (void)user;
    if (++fs.ticks > fs.max_ticks || (size_t)(K->s_top - fs.mark0) > fs.mem_max ||
        (fs.outer && fs.outer(fs.outer_user))) {
        fs.stop = 1;
        return 1;
    }
    return 0;
}

/* f(e) or f(e, v) under a handler: NULL on an ordinary error; interrupts
 * and out-of-memory go on to the search's handler. */
static E *try_op(E *(*f1)(E *), E *(*f2)(E *, E *), E *e, E *v) {
    tryf tf;
    try_enter(&tf);
    E *volatile r = NULL;
    if (!setjmp(tf.jb)) {
        r = f1 ? f1(e) : f2(e, v);
        try_leave(&tf);
    } else {
        try_leave(&tf);
        if (K->err == CAS_ERR_INTERRUPTED || K->err == CAS_ERR_MEMORY) rethrow();
        r = NULL;
    }
    return r;
}

static int is_leaf(E *e) {
    return e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR;
}

/* Bit set K_* of what e contains (recursive walk). */
static int kinds_of(E *e) {
    stack_check();
    if (is_leaf(e)) return 0;
    int k = 0;
    if (e->t == E_FN) {
        int s = e->s;
        if (s == F_SIN || s == F_COS || s == F_TAN || s == F_COT || s == F_SEC || s == F_CSC) k |= K_TRIG;
        if (s == F_SINH || s == F_COSH || s == F_TANH) k |= K_HYP;
        if (s == F_EXP) k |= K_EXP;
        if (s == F_LN || s == F_LOG || s == F_LOG10) k |= K_LN;
        if (s == F_SQRT) k |= K_ROOT;
    }
    if (e->t == E_POW) {
        if (ISYM(K0(e), S_E)) k |= K_EXP;
        if (K1(e)->t == E_RAT) k |= K_ROOT;
        if (is_num(K1(e)) && num_sgn(K1(e)) < 0 && !is_num(K0(e))) k |= K_DEN;
    }
    for (uint32_t i = 0; i < e->n; i++) k |= kinds_of(EK(e)[i]);
    return k;
}

/* Does op apply to an expression with kinds k (and nv variables)? */
static int op_applies(int op, int k) {
    switch (op) {
    case OP_PARTFRAC: return k & K_DEN;
    case OP_COLLECT: return fs.nv >= 2;
    case OP_TEXPAND: case OP_TLIN: case OP_TRIGSIMP: return k & K_TRIG;
    case OP_TRIG2EXP: return k & (K_TRIG | K_HYP);
    case OP_EXP2TRIG: case OP_EXPEXPAND: return k & K_EXP;
    case OP_LNEXPAND: case OP_LNCOLLECT: return k & K_LN;
    case OP_RATIONALIZE: return (k & (K_ROOT | K_DEN)) == (K_ROOT | K_DEN);
    default: return 1;
    }
}

/* Partial fractions of the rational normal form (partfrac of an
 * uncancelled quotient only splits the numerator: x^2/(x-1)-1/(x-1)). */
static E *partfrac_normal(E *e, E *v) { return partfrac(rat_normal(e), v); }

/* Apply op to e (v: the variable of the per-variable ops); NULL if it fails. */
static E *apply_op(int op, E *e, E *v) {
    switch (op) {
    case OP_SIMPLIFY: return try_op(simplify_full, NULL, e, NULL);
    case OP_NORMAL: return try_op(rat_normal, NULL, e, NULL);
    case OP_FACTOR: return try_op(factor_expr, NULL, e, NULL);
    case OP_EXPAND: return try_op(expand, NULL, e, NULL);
    case OP_PARTFRAC: return try_op(NULL, partfrac_normal, e, v);
    case OP_COLLECT: return try_op(NULL, collect_expr, e, v);
    case OP_TEXPAND: return try_op(trig_expand, NULL, e, NULL);
    case OP_TLIN: return try_op(trig_linearize, NULL, e, NULL);
    case OP_TRIGSIMP: return try_op(trig_simplify, NULL, e, NULL);
    case OP_TRIG2EXP: return try_op(trig_to_exp, NULL, e, NULL);
    case OP_EXP2TRIG: return try_op(exp_to_trig, NULL, e, NULL);
    case OP_LNEXPAND: return try_op(ln_expand, NULL, e, NULL);
    case OP_LNCOLLECT: return try_op(ln_collect, NULL, e, NULL);
    case OP_EXPEXPAND: return try_op(exp_expand, NULL, e, NULL);
    case OP_RATIONALIZE: return try_op(simplify_rationalize, NULL, e, NULL);
    }
    return NULL;
}

/* ------------------------------------------------------- spot check --- */
/* Copy of e with variable k of fs.vars replaced by the number of point p
 * (raw nodes: only for numeric evaluation). */
static E *at_point(E *e, int p) {
    stack_check();
    if (e->t == E_SYM) {
        for (int k = 0; k < fs.nv; k++)
            if (fs.vars[k]->s == e->s) {
                E *re = mk_flt(fs.pre[p][k]);
                if (fs.pim[p][k] == 0) return re;
                return mk_add2(re, mk_mul2(mk_flt(fs.pim[p][k]), K->i_));
            }
        return e;
    }
    if (is_leaf(e)) return e;
    E *c = expr_copy_shallow(e);
    for (uint32_t i = 0; i < e->n; i++) EK(c)[i] = at_point(EK(e)[i], p);
    return c;
}

/* Value of e at point p into (*re, *im); 0 if undefined or not numeric.
 * The copy is released again. */
static int value_at(E *e, int p, double *re, double *im) {
    char *m = smark();
    int ok = eval_complex(at_point(e, p), re, im) && isfinite(*re) && isfinite(*im);
    srelease(m);
    return ok;
}

/* The points: deterministic pseudo-random values (a fixed LCG, so forms
 * gives the same answer every time).  Positive points in [0.3, 2.3]; the
 * mixed points give each variable both signs across the points; complex
 * points have |re| <= 1.5, 0.3 <= |im| <= 1.5.  (Points where every
 * variable is negative are needed for ln(x*y) = ln(x)+ln(y): with opposite
 * signs the branch terms cancel.) */
static void make_points(void) {
    uint32_t s = 0x2545F491u;
    for (int p = 0; p < NPTS; p++)
        for (int k = 0; k < NV_MAX; k++) {
            s = s * 1664525u + 1013904223u;
            double u = (double)(s >> 8) / 16777216.0, w;
            s = s * 1664525u + 1013904223u;
            w = (double)(s >> 8) / 16777216.0;
            fs.pim[p][k] = 0;
            if (p < FP_POS) fs.pre[p][k] = 0.3 + 2 * u;
            else if (p < FP_POS + FP_REAL) /* all negative, then alternating signs both ways */
                fs.pre[p][k] = (p == FP_POS || (p + k) % 2 ? -1 : 1) * (0.3 + 2 * u);
            else {
                fs.pre[p][k] = 3 * (u - 0.5);
                fs.pim[p][k] = ((p + k) % 2 ? -1 : 1) * (0.3 + 1.2 * w);
            }
        }
}

/* Spot check of candidate f against e (values cached in fs.ore/oim).
 * Returns 0 (rejected), 1 (equal at every point), 2 (equal for positive
 * variables only), 3 (equal for real variables only).  Without variables
 * there is one point. */
static int check(E *f) {
    int np = fs.nv ? NPTS : 1, agree[3] = {0, 0, 0}, seen[3] = {0, 0, 0};
    for (int p = 0; p < np; p++) {
        if (!fs.ook[p]) continue;
        int cls = p < FP_POS ? 0 : p < FP_POS + FP_REAL ? 1 : 2;
        double re, im;
        seen[cls]++;
        if (!value_at(f, p, &re, &im)) continue;
        double d = hypot(re - fs.ore[p], im - fs.oim[p]), sc = 1 + hypot(fs.ore[p], fs.oim[p]);
        if (d <= 1e-7 * sc) agree[cls]++;
    }
    if (!seen[0] || agree[0] < seen[0]) return 0;
    if (agree[1] < seen[1]) return 2;
    if (agree[2] < seen[2]) return 3;
    return 1;
}

/* --------------------------------------------------- excluded points --- */
/* 1 if a and b differ by a nonzero constant factor. */
static int same_factor(E *a, E *b) {
    if (expr_eq(a, b)) return 1;
    E *q = try_op(rat_normal, NULL, s_div(a, b), NULL);
    return q && is_num(q) && !num_is_zero(q);
}

static void den_add(E *f, E **d, int *nd) {
    if (f->t == E_POW && is_num(K1(f)) && num_sgn(K1(f)) > 0) f = K0(f);
    if (is_num(f) || is_const_expr(f) || (kinds_of(f) & K_EXP)) return; /* e^u is never 0 */
    if (is_negative_term(f)) f = s_neg(f);
    for (int i = 0; i < *nd; i++)
        if (same_factor(d[i], f)) return;
    if (*nd < DEN_MAX) d[(*nd)++] = f;
}

/* Collect the factors of the denominators of e into d[0..*nd): bases with a
 * negative numeric exponent (factored), cos u for tan u / sec u, sin u for
 * cot u / csc u. */
static void den_walk(E *e, E **d, int *nd) {
    stack_check();
    if (is_leaf(e)) return;
    if (e->t == E_POW && is_num(K1(e)) && num_sgn(K1(e)) < 0) {
        E *b = K0(e), *f = try_op(factor_expr, NULL, b, NULL);
        if (!f) f = b;
        if (f->t == E_MUL)
            for (uint32_t i = 0; i < f->n; i++) den_add(EK(f)[i], d, nd);
        else
            den_add(f, d, nd);
    } else if (e->t == E_FN && e->n == 1 && (e->s == F_TAN || e->s == F_SEC)) {
        den_add(s_fn1(F_COS, K0(e)), d, nd);
    } else if (e->t == E_FN && e->n == 1 && (e->s == F_COT || e->s == F_CSC)) {
        den_add(s_fn1(F_SIN, K0(e)), d, nd);
    }
    for (uint32_t i = 0; i < e->n; i++) den_walk(EK(e)[i], d, nd);
}

/* Append ", " (unless first) and the condition "f≠0" to b, or "x≠r" when f
 * is linear in its only variable x (root r). */
static void cond_factor(sbuf *b, E *f) {
    E *v[2];
    if (b->len) sb_puts(b, ", ");
    long deg;
    if (collect_syms(f, v, 2) == 1 && poly_degree_in(f, v[0], &deg) && deg == 1) {
        E *r = try_op(rat_normal, NULL, s_neg(s_div(poly_coeff(f, v[0], 0), poly_coeff(f, v[0], 1))), NULL);
        if (r) {
            print_expr(b, v[0]);
            sb_puts(b, "≠");
            print_expr(b, r);
            return;
        }
    }
    print_expr(b, f);
    sb_puts(b, "≠0");
}

/* Condition string of candidate f with check result how (check()): the
 * denominator factors only one of e and f has, then "x>0" / "x real".
 * NULL when there is none. */
static E *condition(E *f, int how) {
    char buf[200];
    sbuf b = {buf, sizeof buf - 1, 0, 0};
    E *d[DEN_MAX];
    int nd = 0;
    den_walk(f, d, &nd);
    for (int i = 0; i < fs.noden; i++) {
        int j = 0;
        while (j < nd && !same_factor(fs.oden[i], d[j])) j++;
        if (j == nd) cond_factor(&b, fs.oden[i]);
    }
    for (int j = 0; j < nd; j++) {
        int i = 0;
        while (i < fs.noden && !same_factor(fs.oden[i], d[j])) i++;
        if (i == fs.noden) cond_factor(&b, d[j]);
    }
    if (how >= 2)
        for (int k = 0; k < fs.nv; k++) {
            if (b.len) sb_puts(&b, ", ");
            print_expr(&b, fs.vars[k]);
            sb_puts(&b, how == 2 ? ">0" : " real");
        }
    if (!b.len || b.overflow) return NULL;
    return mk_str(buf, b.len);
}

/* ------------------------------------------------------------ search --- */
/* FNV-1a hash of the printed form of e: two forms are the same when they
 * print the same (some rewritings return trees that are equal but not
 * structurally identical to the canonical form, e.g. nested sums). */
static uint32_t print_hash(E *e) {
    char buf[512];
    sbuf b = {buf, sizeof buf, 0, 0};
    print_expr(&b, e);
    uint32_t h = 2166136261u ^ (uint32_t)b.overflow;
    for (size_t i = 0; i < b.len && i < sizeof buf; i++) h = (h ^ (uint8_t)buf[i]) * 16777619u;
    return h;
}

/* Offer candidate r (made by op): dedupe against everything seen, check it,
 * store it.  Everything allocated since mark m is dropped except the stored
 * candidate. */
static void offer(E *r, int op, char *m) {
    if (!r || r->t == E_LIST || fs.n >= SEEN_MAX) {
        srelease(m);
        return;
    }
    r = simp(r);
    uint32_t h = print_hash(r);
    for (int i = 0; i < fs.n; i++)
        if (fs.c[i].hash == h) {
            srelease(m);
            return;
        }
    int how = fs.nok < FORMS_MAX ? check(r) : 0;
    E *cond = how ? condition(r, how) : NULL;
    E *pair[2] = {r, cond ? cond : K->c0};
    E *kept = skeep(m, mk_list(2, pair));
    cand_t *c = &fs.c[fs.n];
    c->f = K0(kept);
    c->cond = cond ? K1(kept) : NULL;
    c->cost = (uint32_t)simplify_cost(c->f);
    c->hash = h;
    c->ok = how != 0;
    c->op = (uint8_t)op;
    c->ord = (uint16_t)fs.n;
    fs.n++;
    if (how) fs.nok++;
}

/* Apply every op that fits to base (candidate index bi), offering the
 * results; skips an op on its own result (they are idempotent).  trig2exp
 * is only applied to the input, and its result only gets simplify, the
 * rational normal form and exp2trig: exponential forms of every trig
 * rewriting would fill the list with long, equally valid noise. */
static void expand_from(int bi) {
    E *e = fs.c[bi].f;
    int k = kinds_of(e), from_exp = fs.c[bi].op == OP_TRIG2EXP;
    for (int op = 0; op < OP_N && !fs.stop; op++) {
        if (!op_applies(op, k) || (bi > 0 && op == fs.c[bi].op) || (bi > 0 && op == OP_TRIG2EXP) ||
            (from_exp && op != OP_SIMPLIFY && op != OP_NORMAL && op != OP_EXP2TRIG))
            continue;
        int per_var = op == OP_PARTFRAC || op == OP_COLLECT;
        for (int vi = 0; vi < (per_var ? fs.nv : 1); vi++) {
            if (fs.nok >= FORMS_MAX) return;
            char *m = fs.clean = smark();
            E *v = per_var ? fs.vars[vi] : NULL;
            if (per_var && freeof(e, v)) continue;
            offer(apply_op(op, e, v), op, m);
        }
    }
}

/* The whole search: the input, depth 1 from it, depth 2 from the verified
 * depth-1 forms in increasing cost. */
static void search(E *e) {
    fs.c[0].f = e;
    fs.c[0].cond = NULL;
    fs.c[0].cost = (uint32_t)simplify_cost(e);
    fs.c[0].hash = print_hash(e);
    fs.c[0].ok = 1;
    fs.c[0].op = OP_N;
    fs.c[0].ord = 0;
    fs.n = fs.nok = 1;
    expand_from(0);
    int n1 = fs.n, order[SEEN_MAX], no = 0;
    for (int i = 1; i < n1; i++)
        if (fs.c[i].ok) order[no++] = i;
    for (int i = 1; i < no; i++) /* insertion sort by cost */
        for (int j = i; j > 0 && fs.c[order[j]].cost < fs.c[order[j - 1]].cost; j--) {
            int t = order[j];
            order[j] = order[j - 1];
            order[j - 1] = t;
        }
    for (int i = 0; i < no && !fs.stop; i++) expand_from(order[i]);
}

/* 1 if candidate a ranks before b: equally simple first, then cost, then
 * the order found. */
static int before(const cand_t *a, int ea, const cand_t *b, int eb) {
    if (ea != eb) return ea > eb;
    if (a->cost != b->cost) return a->cost < b->cost;
    return a->ord < b->ord;
}

/* forms(e [, detail [, budget]]): see the file header. */
E *forms_cmd(uint32_t n, E **a) {
    E *e = a[0];
    long detail = 0, budget = FORMS_TICKS;
    if (n > 1 && !num_is_small_int(a[1], &detail)) cas_throw(CAS_ERR_DOMAIN, "forms: detail must be 0 or 1");
    if (n > 2 && (!num_is_small_int(a[2], &budget) || budget < 0))
        cas_throw(CAS_ERR_DOMAIN, "forms: budget must be a nonnegative integer");
    if (is_leaf(e) || e->t == E_LIST || (e->t == E_FN && e->s == F_EQ)) {
        E *one = detail ? s_list(4, (E *[]){e, mk_int((int64_t)simplify_cost(e)), K->c1, mk_str("", 0)}) : e;
        return s_list(1, &one);
    }
    fsp = salloc(sizeof *fsp);
    memset(fsp, 0, sizeof *fsp);
    fs.c = salloc(sizeof(cand_t) * SEEN_MAX);
    fs.nv = (int)collect_syms(e, fs.vars, NV_MAX);
    make_points();
    for (int p = 0; p < (fs.nv ? NPTS : 1); p++) fs.ook[p] = (uint8_t)value_at(e, p, &fs.ore[p], &fs.oim[p]);
    den_walk(e, fs.oden, &fs.noden);
    /* budget: own poll callback chained to the application's */
    fs.mark0 = smark();
    fs.mem_max = (size_t)(K->s_hi - K->s_top) / 2;
    fs.max_ticks = (unsigned)budget;
    fs.outer = K->poll;
    fs.outer_user = K->poll_user;
    K->poll = forms_poll;
    K->poll_user = NULL;
    fs.clean = smark();
    tryf tf;
    try_enter(&tf);
    if (!setjmp(tf.jb)) {
        search(e);
        try_leave(&tf);
    } else {
        try_leave(&tf);
        int ours = (K->err == CAS_ERR_INTERRUPTED && fs.stop) || K->err == CAS_ERR_MEMORY;
        if (!ours) {
            K->poll = fs.outer;
            K->poll_user = fs.outer_user;
            rethrow();
        }
        K->intr = 0;
        fs.stop = 1;
        srelease(fs.clean); /* the unfinished candidate */
    }
    /* the result, built with no poll callback (the application's budget may
     * have run out too) */
    K->poll = NULL;
    uint32_t best = UINT32_MAX;
    for (int i = 0; i < fs.n; i++)
        if (fs.c[i].ok && fs.c[i].cost < best) best = fs.c[i].cost;
    int idx[SEEN_MAX], m = 0;
    uint8_t eq[SEEN_MAX];
    for (int i = 0; i < fs.n; i++) {
        if (!fs.c[i].ok) continue;
        eq[i] = 100u * fs.c[i].cost <= (100u + FORMS_TOL_PCT) * best;
        int j = m++;
        while (j > 0 && before(&fs.c[i], eq[i], &fs.c[idx[j - 1]], eq[idx[j - 1]])) {
            idx[j] = idx[j - 1];
            j--;
        }
        idx[j] = i;
    }
    if (m > FORMS_MAX) m = FORMS_MAX;
    E **out = salloc(sizeof(E *) * (size_t)(m + 1));
    for (int i = 0; i < m; i++) {
        cand_t *c = &fs.c[idx[i]];
        out[i] = detail ? s_list(4, (E *[]){c->f, mk_int(c->cost), eq[idx[i]] ? K->c1 : K->c0,
                                            c->cond ? c->cond : mk_str("", 0)})
                        : c->f;
    }
    K->poll = fs.outer;
    K->poll_user = fs.outer_user;
    return s_list((uint32_t)m, out);
}
#endif /* CONFIG_CAS_FORMS */
