/*
 * eval.c -- the evaluator: turns a raw parse tree (parse.c) into a value.
 *
 * Position in the pipeline: cas_eval (cas.c) calls eval_top on the output of
 * parse(); the result is stored as ans and printed.  cmds.c and a few other
 * modules call eval() again for user-function applications they construct
 * (matrix(r, c, f), map, apply), and session.c uses it to rebuild restored
 * values.
 *
 * Evaluation is a single bottom-up pass over the tree (eval_rec):
 *   - numbers and strings evaluate to themselves (they only get F_SIMP set);
 *   - a symbol evaluates to its stored value (K->syms[id].val) if it has one,
 *     otherwise to itself -- free symbols are the normal case in a CAS.
 *     Stored values are not re-evaluated (one level of substitution);
 *   - sums, products, powers and lists have their children evaluated and are
 *     rebuilt with the automatic simplifier (rebuild -> s_add/s_mul/...);
 *   - function nodes (E_FN) go to eval_fn, which handles, in order:
 *     assignment (:=), sequences (;), when/ifte, f'(a), dsolve (which wants
 *     its arguments raw), assume/unassume/about (which want names, not
 *     values), purge, user-function calls, commands (dispatched to eval_cmd
 *     in cmds.c), extension commands / logic (ext_dispatch in xcmds.c), the
 *     angle-mode conversion for trig functions, decidable comparisons and
 *     boolean operators, and finally ordinary math functions via s_fn.
 *
 * Key design decisions:
 *   - User functions are stored *unevaluated* (K->syms[id].fdef holds
 *     LIST[param1, ..., paramN, body] in the persistent heap) and applied by
 *     substituting the evaluated arguments into the raw body (subst_raw) and
 *     evaluating the result -- i.e. call-by-value with textual substitution,
 *     which lets definitions refer to functions and variables defined later.
 *     There is no protection against variable capture: a parameter name also
 *     used as a bound variable inside the body (e.g. a sum index) is replaced
 *     there as well.
 *   - Commands that bind variables (diff(f, x), integrate, solve, sum, ...)
 *     evaluate their arguments with those variables' stored values hidden
 *     (eval_hiding), so that x:=3 does not turn diff(x^2, x) into diff(9, 3).
 *   - In degree mode the conversion is done here, not in the simplifier:
 *     arguments of sin/cos/tan/sec/csc/cot are multiplied by pi/180 and
 *     results of the inverse functions by 180/pi.  Internally everything is
 *     radians.
 *   - While K->restoring is set (cas_restore in session.c) neither variables
 *     nor user functions are substituted, so saved values come back exactly
 *     as they were stored.
 *
 * Errors are raised with cas_throw (wrong argument counts, reserved names,
 * recursion deeper than 2000 user-function calls); all results live in
 * scratch except values returned from the symbol table, which are the
 * persistent copies themselves (read-only, like every E node).
 */
#include "cas_int.h"
#include <math.h>
#include <string.h>

E *rebuild(E *e, E **k);
void esort(E **a, uint32_t n, int (*cmp)(E *, E *));

/* ------------------------------------------------------- variable hiding --- */
/* One hidden variable: its symbol id and the value to put back. */
typedef struct {
    int id;
    E *val, *fdef; /* saved value; definition at hiding time */
} hidden;

static E *eval_rec(E *e);

/* Evaluate e with the stored values of the symbols vars[0..nvars) temporarily
 * removed, so that those names stay free variables during the evaluation.
 * Used for the variable arguments of binding commands (eval_fn), for
 * displaying a user-function body (do_assign) and by cas_compile.
 *
 * At most 24 variables (the most cas_compile_vars accepts) are hidden;
 * extra ones are silently ignored.  The old values are restored on both the
 * normal and the error path (the error is then rethrown).  They are restored
 * in reverse order so that a variable listed twice ends with its original
 * value (the second save recorded NULL).  Only val is hidden; user-function
 * definitions (fdef) stay visible.  A name assigned or redefined during the
 * evaluation keeps its new value or definition (the saved value is freed);
 * a purge during the evaluation is undone. */
static void unhide(hidden *h, int nh) {
    for (int i = nh; i-- > 0;) {
        symrec *r = &K->syms[h[i].id];
        if (!r->val && r->fdef == h[i].fdef) r->val = h[i].val;
        else if (h[i].val) hfree_expr(h[i].val);
    }
}

E *eval_hiding(E *e, int nvars, int *vars) {
    hidden h[24];
    volatile int nh = 0;
    for (int i = 0; i < nvars && nh < 24; i++) {
        symrec *r = &K->syms[vars[i]];
        h[nh].id = vars[i];
        h[nh].val = r->val;
        h[nh].fdef = r->fdef;
        r->val = NULL;
        nh++;
    }
    tryf tf;
    try_enter(&tf);
    E *volatile res = NULL;
    if (!setjmp(tf.jb)) {
        res = eval_rec(e);
        try_leave(&tf);
    } else {
        try_leave(&tf);
        unhide(h, nh);
        rethrow();
    }
    unhide(h, nh);
    return res;
}

/* ----------------------------------------------------------- helpers --- */
/* Node-visit budget of the current collect_syms walk (static scratch state:
 * one reason the library is single-threaded). */
static uint32_t g_visit_budget;
void eval_reset_statics(void); /* cas.c eval_impl: state an error may have left set */
void eval_reset_statics(void) { g_visit_budget = 0; }

/* Worker of collect_syms: depth-first walk appending each distinct free
 * symbol to out[0..cap).  Constants (e, pi, i, inf, undef, true, false,
 * euler_gamma) are skipped; function ids are not symbols and are skipped
 * too.  Because expressions are DAGs with shared subtrees, a naive walk can
 * take exponential time; g_visit_budget caps the number of visited nodes,
 * after which the result may be incomplete.  Duplicate check is linear in
 * the number found (cap is small, <= 32 in practice). */
static void collect_syms_rec(E *e, E **out, uint32_t *n, uint32_t cap) {
    stack_check();
    if (g_visit_budget == 0) return;
    g_visit_budget--;
    if (e->t == E_SYM) {
        if (e->s == S_E || e->s == S_PI || e->s == S_I || e->s == S_INF || e->s == S_UNDEF || e->s == S_TRUE ||
            e->s == S_FALSE || e->s == S_EULERG)
            return;
        for (uint32_t i = 0; i < *n; i++)
            if (out[i] == e || out[i]->s == e->s) return;
        if (*n < cap) out[(*n)++] = e;
        return;
    }
    if (e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_STR) return;
    for (uint32_t i = 0; i < e->n; i++) collect_syms_rec(EK(e)[i], out, n, cap);
}

/* Collect up to `cap` distinct free symbols of e into out[], sorted by the
 * canonical order (expr_cmp, i.e. alphabetically).  Returns how many were
 * found.  The walk visits at most 20000 nodes. */
uint32_t collect_syms(E *e, E **out, uint32_t cap) {
    uint32_t n = 0;
    g_visit_budget = 20000; /* expressions are DAGs: bound the walk */
    collect_syms_rec(e, out, &n, cap);
    esort(out, n, expr_cmp);
    return n;
}

/* The variable a command uses when the user did not name one: x if it occurs
 * in e, else the first free symbol in canonical order, else x.  E.g.
 * degree(t^3+t) is taken in t, diff(a*x^2) differentiates in x. */
E *default_var(E *e) {
    E *v[32];
    uint32_t n = collect_syms(e, v, 32);
    for (uint32_t i = 0; i < n; i++)
        if (v[i]->s == S_X) return v[i];
    if (n >= 1) return v[0];
    return mk_sym(S_X);
}

/* Truth values for when/ifte/and/or/not: the symbols true/false, and numbers
 * (nonzero = true).  An expression that is neither is "undecided". */
static int is_true(E *e) { return ISYM(e, S_TRUE) || (is_num(e) && !num_is_zero(e)); }
static int is_false(E *e) { return ISYM(e, S_FALSE) || (is_num(e) && num_is_zero(e)); }

/* The symbol true or false. */
static E *bool_e(int b) { return mk_sym(b ? S_TRUE : S_FALSE); }

/* Replace every occurrence of the symbols ids[0..n) in e by vals[i], without
 * simplifying (the result is a fresh raw tree to be evaluated).  Used to
 * apply user functions.  Copies every interior node on the way; leaves
 * (numbers, strings, other symbols) are shared.  F_SIMP is cleared on the
 * copies so that the evaluator re-simplifies them.  Substitution is purely
 * syntactic: it also descends into commands that bind the same name. */
static E *subst_raw(E *e, uint32_t n, int *ids, E **vals) {
    if (e->t == E_SYM) {
        for (uint32_t i = 0; i < n; i++)
            if (e->s == ids[i]) return vals[i];
        return e;
    }
    if (e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_STR) return e;
    stack_check();
    E *c = enode(e->t, e->n);
    c->s = e->s;
    for (uint32_t i = 0; i < e->n; i++) c->f = 0, EK(c)[i] = subst_raw(EK(e)[i], n, ids, vals);
    return c;
}

/* 1 if the raw tree e applies conj, re or im to something containing z. */
static int has_cpart(E *e, E *z) {
    stack_check();
    if (e->t == E_INT || e->t == E_RAT || e->t == E_FLT || e->t == E_STR || e->t == E_SYM) return 0;
    if (e->t == E_FN && (e->s == F_CONJ || e->s == F_RE || e->s == F_IM) && !freeof(e, z)) return 1;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_cpart(EK(e)[i], z)) return 1;
    return 0;
}

/* ------------------------------------------------------------ assign --- */
/* Names the user may assign: user symbols, plus the builtins x and _t, which
 * are pre-interned for internal use but are ordinary variables to the user. */
static int assignable(int id) { return id >= S__COUNT || id == S_X || id == S_T__; }

/* Execute `lhs := rhs_raw` (also produced by `rhs => name`).
 *
 *   name := expr        evaluate expr and store a heap copy as the value;
 *                       returns the stored value.  Any previous value or
 *                       function definition of the name is freed.
 *   f(a, b) := body     store LIST[a, b, body] with the body *unevaluated*
 *                       as the function definition (removing any value), and
 *                       return the body evaluated with the parameters hidden,
 *                       for display.
 *
 * Throws CAS_ERR_DOMAIN for builtin names (constants, functions, commands)
 * and CAS_ERR_SYNTAX for non-name parameters or other targets.  The old heap
 * blocks are released through hfree_expr, which defers the actual free to
 * the end of the API call because scratch values (here: v) may still share
 * nodes with them. */
static E *do_assign(E *lhs, E *rhs_raw) {
    if (lhs->t == E_SYM) {
        if (!assignable(lhs->s)) cas_throw(CAS_ERR_DOMAIN, "cannot assign to a reserved name");
        E *v = eval_rec(rhs_raw);
        symrec *r = &K->syms[lhs->s];
        E *old = r->val, *oldf = r->fdef;
        r->val = hstore(v);
        r->fdef = NULL;
        if (old) hfree_expr(old);
        if (oldf) hfree_expr(oldf);
        /* v may share subtrees with the old value just freed: return the copy */
        return r->val;
    }
    if (lhs->t == E_FN) {
        if (!assignable(lhs->s)) cas_throw(CAS_ERR_DOMAIN, "cannot redefine a builtin function");
        for (uint32_t i = 0; i < lhs->n; i++)
            if (EK(lhs)[i]->t != E_SYM) cas_throw(CAS_ERR_SYNTAX, "function parameters must be names");
        E **k = salloc(sizeof(E *) * (lhs->n + 1));
        for (uint32_t i = 0; i < lhs->n; i++) k[i] = EK(lhs)[i];
        k[lhs->n] = rhs_raw;
        E *def = mk_list(lhs->n + 1, k);
        symrec *r = &K->syms[lhs->s];
        E *old = r->val, *oldf = r->fdef;
        r->fdef = hstore(def);
        r->val = NULL;
        if (old) hfree_expr(old);
        if (oldf) hfree_expr(oldf);
        /* display the body with parameters unevaluated */
        int ids[16];
        uint32_t np = lhs->n < 16 ? lhs->n : 16;
        for (uint32_t i = 0; i < np; i++) ids[i] = EK(lhs)[i]->s;
        return eval_hiding(rhs_raw, (int)np, ids);
    }
    cas_throw(CAS_ERR_SYNTAX, "invalid assignment target");
}

/* -------------------------------------------------------------- eval --- */
/* Trig functions whose argument is an angle (scaled in degree mode). */
static int is_trig_in(int id) {
    return id == F_SIN || id == F_COS || id == F_TAN || id == F_SEC || id == F_CSC || id == F_COT;
}
/* e contains diff(name, var), the form y' parses to (a derivative of an
 * unknown function written with primes) */
static int has_name_deriv(E *e) {
    stack_check();
    if (IFN(e, C_DIFF) && e->n == 2 && K0(e)->t == E_SYM && K1(e)->t == E_SYM && K0(e)->s != K1(e)->s) return 1;
    if (e->t == E_INT || e->t == E_FLT || e->t == E_SYM || e->t == E_STR) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_name_deriv(EK(e)[i])) return 1;
    return 0;
}

/* Inverse trig functions whose result is an angle (scaled in degree mode). */
static int is_trig_out(int id) {
    return id == F_ASIN || id == F_ACOS || id == F_ATAN || id == F_ACOT || id == F_ASEC || id == F_ACSC;
}

/* Commands whose arguments after the first name variables (diff(f, x),
 * integrate(f, x, a, b), solve(eq, [x, y]), sum(f, k, 1, n), ...).  For these,
 * eval_fn hides the stored values of those variables while evaluating all of
 * the command's arguments. */
static int cmd_binds_vars(int id) {
    switch (id) {
    case C_DIFF: case C_INTEGRATE: case C_INT: case C_LIMIT: case C_SERIES: case C_TAYLOR: case C_SUM:
    case C_PRODUCT: case C_SOLVE: case C_CSOLVE: case C_FSOLVE: case C_NSOLVE: case C_LINSOLVE: case C_SEQ:
    case C_MAKELIST: case C_SUBST: case C_SUBS: case C_COLLECT: case C_DEGREE: case C_COEFF: case C_COEFFS:
    case C_LCOEFF: case C_QUO: case C_REM: case C_PARTFRAC: case C_APART: case C_ROOTS: case C_RESULTANT:
    case C_DISCRIM: case C_TANGENT: case C_DIVIDE: case C_PADE: case C_DESOLVE: case C_ODESOLVE:
    case C_LAPLACE: case C_ILAPLACE: case C_HORNER: case C_ROMBERG: case C_NDIFF: case C_FOURIERA:
    case C_HESSIAN: case C_GRAD: case C_JACOBIAN: case C_DIVERGENCE: case C_CURL: case C_CHARPOLY:
    case C_INTERP: case C_FACTOR: case C_SQRFREE: case C_GBASIS: case C_IMPLICITDIFF: case C_ODESOLVE_N:
        return 1;
    }
    return 0;
}

/* Add the variable names that occur in a command argument a to ids[0..*n)
 * (at most 16): a bare assignable symbol, the left side of `x = a` (as in
 * limit(f, x = 0)), and recursively the elements of a list ([x, y]). */
static void add_hide(E *a, int *ids, int *n) {
    stack_check();
    if (a->t == E_SYM) {
        if (assignable(a->s) && *n < 16) ids[(*n)++] = a->s;
    } else if (IFN(a, F_EQ) && K0(a)->t == E_SYM)
        add_hide(K0(a), ids, n);
    else if (a->t == E_LIST)
        for (uint32_t i = 0; i < a->n; i++) add_hide(EK(a)[i], ids, n);
}

/* Evaluate a function node e (E_FN, raw).  This is the dispatcher of the
 * evaluator; see the file header for the order of the cases.  Special forms
 * (:=, ;, when/ifte, f'(a), dsolve, assume/unassume/about, purge) look at
 * their arguments before or instead of evaluating them; everything else
 * evaluates all arguments first (call by value).  Returns the value in
 * scratch (or a persistent stored value for assignments); throws on errors. */
static E *eval_fn(E *e) {
    int id = e->s;
    uint32_t n = e->n;
    if (id == F_ASSIGN && n == 2) return do_assign(K0(e), K1(e));
    if (id == F_SEQ) {
        E *r = K->c0;
        for (uint32_t i = 0; i < n; i++) r = eval_rec(EK(e)[i]);
        return r;
    }
    /* when(c, a, b): lazy in the branches when the condition is decided;
     * otherwise both branches are evaluated and the call is kept symbolic */
    if ((id == C_WHEN || id == C_IFTE) && n == 3) {
        E *c = eval_rec(K0(e));
        if (is_true(c)) return eval_rec(K1(e));
        if (is_false(c)) return eval_rec(EK(e)[2]);
        E *v[3] = {c, eval_rec(K1(e)), eval_rec(EK(e)[2])};
        return s_fn(C_WHEN, 3, v);
    }
    if (id == F_DERIV && n == 3 && K0(e)->t == E_SYM && K1(e)->t == E_INT) {
        /* f'(a): derivative of a (user or builtin) function at a point.
         * The parser produces derivative(f, k, a) with k = number of primes.
         * Build the function body in a fresh dummy variable _dv:
         *   - one-parameter user function: its body with the parameter
         *     replaced by _dv, evaluated;
         *   - builtin math function: f(_dv);
         *   - variable holding an expression: that expression read as a
         *     function of x.
         * then differentiate k times in _dv and substitute _dv = a.  If none
         * applies (unknown f), the derivative node is kept symbolic. */
        int fid = K0(e)->s;
        long k;
        if (!num_is_small_int(K1(e), &k) || k < 0 || k > 100) cas_throw(CAS_ERR_DOMAIN, "bad derivative order");
        E *pt = eval_rec(EK(e)[2]);
        E *t = mk_sym_name("_dv");
        E *body = NULL;
        if (fid >= S__COUNT && K->syms[fid].fdef && K->syms[fid].fdef->n == 2) {
            E *def = K->syms[fid].fdef;
            int pid = K0(def)->s;
            body = eval_rec(subst_raw(K1(def), 1, &pid, &t));
        } else if (fid >= F_SQRT && fid < S__FIRSTCMD)
            body = s_fn1(fid, t);
        else if (fid >= S__COUNT && K->syms[fid].val)
            body = subst(K->syms[fid].val, mk_sym(S_X), t);
        if (!body) {
            E *v[3] = {K0(e), K1(e), pt};
            return s_fn(F_DERIV, 3, v);
        }
        return subst(diffn(body, t, k), t, pt);
    }
    /* solve([x'=y, y'=-x]): a prime on a bare name is diff(name, x), which
     * evaluates to 0 or 1 and gave a meaningless solution; say what to use */
    if ((id == C_SOLVE || id == C_CSOLVE || id == C_LINSOLVE || id == C_FSOLVE || id == C_NSOLVE) && n >= 1 &&
        has_name_deriv(EK(e)[0]))
        cas_throw(CAS_ERR_DOMAIN, "solve: equations with derivatives (y'): use dsolve");
    /* dsolve sees its arguments unevaluated: y(x), y'(x) and y(a)=b must not
     * be evaluated as function calls (ode.c evaluates what it needs) */
    if (id == C_DESOLVE || id == C_ODESOLVE) {
        extern E *dsolve_raw(uint32_t n, E **raw);
        return dsolve_raw(n, EK(e));
    }
    if (id == C_ASSUME || id == C_UNASSUME || id == C_ABOUT) {
        /* operate on names, not values: hide the values of a bare name and of
         * the left operand of a relation (assume(a > 0) with a:=5 must still
         * talk about a) */
        E **args = salloc(sizeof(E *) * (n + 1));
        int hide[16], nh = 0;
        for (uint32_t i = 0; i < n; i++) {
            E *q = EK(e)[i];
            if (q->t == E_SYM && assignable(q->s) && nh < 16) hide[nh++] = q->s;
            if (q->t == E_FN && q->n == 2 && K0(q)->t == E_SYM && assignable(K0(q)->s) && nh < 16) hide[nh++] = K0(q)->s;
        }
        for (uint32_t i = 0; i < n; i++) args[i] = nh ? eval_hiding(EK(e)[i], nh, hide) : eval_rec(EK(e)[i]);
        return eval_cmd(id, n, args);
    }
    /* purge(a, f, ...): forget values, definitions and assumptions of the
     * named (unevaluated) symbols; other arguments are ignored */
    if (id == C_PURGE) {
        for (uint32_t i = 0; i < n; i++) {
            E *a = EK(e)[i];
            if (a->t == E_SYM && assignable(a->s)) {
                symrec *r = &K->syms[a->s];
                if (r->val) hfree_expr(r->val);
                if (r->fdef) hfree_expr(r->fdef);
                r->val = r->fdef = NULL;
                r->assume = 0;
                facts_forget(a->s);
            }
        }
        return mk_sym(S_TRUE);
    }
    /* user function call: evaluate the arguments (call by value), substitute
     * them for the parameters in the raw body and evaluate that.  K->depth
     * counts nested calls so that runaway recursion (f(n):=n*f(n-1) without
     * a base case) ends in an error before the C stack check would; it is
     * not decremented when the body throws, and is reset by the next
     * cas_eval. */
    if (id >= S__COUNT && K->syms[id].fdef && !K->restoring) {
        E *def = K->syms[id].fdef;
        uint32_t np = def->n - 1;
        if (np != n) cas_throw(CAS_ERR_DOMAIN, "wrong number of arguments");
        E **vals = salloc(sizeof(E *) * (n + 1));
        int *ids = salloc(sizeof(int) * (n + 1));
        for (uint32_t i = 0; i < n; i++) {
            vals[i] = eval_rec(EK(e)[i]);
            ids[i] = EK(def)[i]->s;
        }
        if (++K->depth > 2000) cas_throw(CAS_ERR_MEMORY, "recursion too deep");
        E *body = subst_raw(EK(def)[np], n, ids, vals);
        E *r = eval_rec(body);
        K->depth--;
        return r;
    }
    /* csolve with conj/re/im of the unknown z: symbols are real, so conj(z)
     * would become z; solve re = 0, im = 0 for z = _u + i _v instead */
    if (id == C_CSOLVE && n == 2 && K1(e)->t == E_SYM && assignable(K1(e)->s) && has_cpart(K0(e), K1(e))) {
        int zid = K1(e)->s;
        E *u = mk_sym_name("_u"), *v = mk_sym_name("_v"), *z = mk_add2(u, mk_mul2(mk_sym(S_I), v));
        E *q = eval_hiding(subst_raw(K0(e), 1, &zid, &z), 1, &zid);
        if (IFN(q, F_EQ)) q = s_sub(K0(q), K1(q));
        E *eq[2] = {s_fn1(F_RE, q), s_fn1(F_IM, q)}, *uv[2] = {u, v};
        E *sol = solve_expr(s_list(2, eq), s_list(2, uv), 0);
        E **r = salloc(sizeof(E *) * (sol->n + 1));
        for (uint32_t i = 0; i < sol->n; i++) {
            r[i] = eval_rec(z);
            for (uint32_t j = 0; j < EK(sol)[i]->n; j++) {
                E *s = EK(EK(sol)[i])[j];
                if (IFN(s, F_EQ)) r[i] = subst(r[i], K0(s), K1(s));
            }
        }
        return s_list(sol->n, r);
    }
    /* commands: evaluate the arguments (with variable arguments' values
     * hidden for binding commands) and dispatch to cmds.c */
    if (id > S__FIRSTCMD && id < S__COUNT) {
        int hide[16], nh = 0;
        if (cmd_binds_vars(id))
            for (uint32_t i = 1; i < n; i++) add_hide(EK(e)[i], hide, &nh);
        E **args = salloc(sizeof(E *) * (n + 1));
        for (uint32_t i = 0; i < n; i++) args[i] = nh ? eval_hiding(EK(e)[i], nh, hide) : eval_rec(EK(e)[i]);
        return eval_cmd(id, n, args);
    }
    /* everything else: operators, math functions, unknown (free) function
     * names and extension commands.  ext_dispatch (xcmds.c) handles commands
     * that have no builtin id (looked up by name) and bitwise and/or/not on
     * integers; it returns NULL to decline. */
    E **args = salloc(sizeof(E *) * (n + 1));
    for (uint32_t i = 0; i < n; i++) args[i] = eval_rec(EK(e)[i]);
    { extern E *ext_dispatch(int id, uint32_t n, E **a); E *x = ext_dispatch(id, n, args); if (x) return x; }
    if (K->set.angle == CAS_ANGLE_DEG && n == 1) {
        /* an argument with an angle unit (30_deg, 2_rad) is converted by
         * units.c instead and must not be scaled again */
        extern int units_has_angle(E *e);
        if (is_trig_in(id) && !units_has_angle(args[0])) args[0] = s_mul2(args[0], s_div(K->pi_, mk_int(180)));
        if (is_trig_out(id)) return s_mul2(s_fn(id, 1, args), s_div(mk_int(180), K->pi_));
    }
    /* Decide what can be decided.  Inequalities between constant expressions
     * are compared in double precision (so values closer than about 1e-16
     * relative can be misjudged unless they simplify to the same form);
     * structurally equal operands make != false.  `=` always stays an
     * equation (equations are first-class values: (x+1=3)-1 gives x=2).
     * Undecided relations and logic stay symbolic via s_fn. */
    switch (id) {
    case F_LT: case F_GT: case F_LE: case F_GE: case F_NE: {
        if (n != 2) break;
        double a, b;
        if (is_const_expr(args[0]) && is_const_expr(args[1]) && to_double(args[0], &a) && to_double(args[1], &b)) {
            int r = id == F_LT ? a < b : id == F_GT ? a > b : id == F_LE ? a <= b : id == F_GE ? a >= b : a != b;
            if (id == F_NE && expr_eq(args[0], args[1])) r = 0;
            return bool_e(r);
        }
        if (id == F_NE && expr_eq(args[0], args[1])) return bool_e(0);
        break;
    }
    case F_AND:
        if (n == 2 && (is_true(args[0]) || is_false(args[0])) && (is_true(args[1]) || is_false(args[1])))
            return bool_e(is_true(args[0]) && is_true(args[1]));
        if (n == 2 && (is_false(args[0]) || is_false(args[1]))) return bool_e(0); /* x and false */
        break;
    case F_OR:
        if (n == 2 && (is_true(args[0]) || is_false(args[0])) && (is_true(args[1]) || is_false(args[1])))
            return bool_e(is_true(args[0]) || is_true(args[1]));
        if (n == 2 && (is_true(args[0]) || is_true(args[1]))) return bool_e(1); /* x or true */
        break;
    case F_NOT:
        if (n == 1 && (is_true(args[0]) || is_false(args[0]))) return bool_e(!is_true(args[0]));
        break;
    case F_EQ:
        break;
    }
    return s_fn(id, n, args);
}

/* Recursive evaluator (see the file header).  stack_check() bounds the
 * recursion depth for deeply nested input.  `ans` evaluates to the stored
 * previous result (0 before the first evaluation).  Setting F_SIMP on
 * numbers and strings is the only mutation of the input tree: those leaves
 * are already canonical. */
static E *eval_rec(E *e) {
    stack_check();
    switch (e->t) {
    case E_INT: case E_RAT: case E_FLT: case E_STR:
        e->f |= F_SIMP;
        return e;
    case E_SYM: {
        if (e->s == S_ANS) return K->ans ? K->ans : K->c0;
        symrec *r = &K->syms[e->s];
        if (r->val && !K->restoring) return r->val;
        return e;
    }
    case E_FN: return eval_fn(e);
    default: {
        E **k = salloc(sizeof(E *) * (e->n + 1));
        for (uint32_t i = 0; i < e->n; i++) k[i] = eval_rec(EK(e)[i]);
        return rebuild(e, k);
    }
    }
}

/* Public (internal API) entry points: eval is used by other modules to
 * evaluate trees they build; eval_top by cas.c for a whole input line.
 * They are identical today; eval_top exists as a separate hook. */
E *eval(E *e) { return eval_rec(e); }
/* A relation (or and / or of relations) that is the whole input is decided
 * with the assumptions and, for a polynomial in one variable, over the reals
 * (assume.c assume_decide); nested ones keep the constant-only rule. */
E *eval_top(E *e) {
    E *r = eval_rec(e);
    if (e->t == E_FN && e->n == 2 && (e->s == F_GT || e->s == F_GE || e->s == F_LT || e->s == F_LE || e->s == F_AND || e->s == F_OR))
        r = assume_decide(r);
    return r;
}
