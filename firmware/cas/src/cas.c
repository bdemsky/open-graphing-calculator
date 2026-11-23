/*
 * cas.c -- public API entry points (include/cas.h): context setup, settings,
 * cas_eval / cas_eval_quiet, cas_compile, cas_latex, memory statistics.
 *
 * This is the outermost layer of the engine.  Every function here that runs
 * engine code follows the same protocol, which students should recognise
 * whenever they read an entry point (session.c follows it too):
 *
 *   1. K = ctx                 make the context current (the library keeps a
 *                              single global context pointer, see cas_int.h);
 *   2. reset per-call state    interrupt flag, error status/message, the
 *                              C-stack anchor used by stack_check(), the
 *                              scratch region (s_top = s_lo) and the list of
 *                              deferred heap frees;
 *   3. install a tryf handler  every error inside the engine is a cas_throw()
 *                              that longjmps back here; the handler turns it
 *                              into a cas_status, so no entry point ever
 *                              aborts and the context stays usable;
 *   4. finish()                run deferred persistent-heap frees, drop all
 *                              scratch memory, forget the handler stack.
 *
 * Data flow of one cas_eval:
 *
 *   text --parse()--> raw tree --ext_pre()--> eval_top() (eval.c, which
 *   dispatches commands to cmds.c and the algorithm modules) --ext_post()-->
 *   [evalf_digits() in approximate mode] --hstore()--> ans (persistent heap)
 *   --print_expr()--> caller's text buffer.
 *
 * Arena carving (cas_init), in address order, see also mem.c and README.md:
 *
 *   [cas_ctx][symrec x maxsyms][hash buckets x 1024][name pool]
 *   [permanent E_SYM nodes x maxsyms][PERM_BYTES of permanent constants]
 *   [persistent heap: 1/4 of the remainder][scratch: the other 3/4]
 *
 * Everything before the persistent heap is "fixed" (reported as such by
 * cas_mem); only the heap and scratch change during a session.
 */
#include "cas_int.h"
#include <string.h>

/* Size of the permanent area that holds the small numeric constants
 * (0, 1, -1, 2, 1/2, -1/2) built by make_constants.  They need only a few
 * hundred bytes; the rest is slack.  Nodes in this area are never released,
 * so the constants survive every scratch reset and cas_reset. */
#define PERM_BYTES 8192u

/* Address of the permanent E_SYM node of symbol id i.  The node array follows
 * the name pool (see cas_init); sym_intern and sym_gc (expr.c) write the
 * node, mk_sym returns it.  Exported for expr.c. */
E *sym_node_area(uint32_t i) { return (E *)(K->symnodes + K->symnode_size * i); }

/* Names of the builtin symbols in id order, generated from the same X-macro
 * (BUILTIN_SYMS in cas_int.h) as the id enum, so that interning them in this
 * order in setup_symbols_and_heap gives every builtin its enum value as id. */
static const char *builtin_names[] = {
#define X_NAME(id, name) name,
    BUILTIN_SYMS(X_NAME)
#undef X_NAME
};

/* (Re)initialise the dynamic state of a context: empty the symbol table and
 * re-intern every builtin (so builtin i gets symbol id i), turn the whole
 * persistent heap into one free block, and forget ans and pending frees.
 * Used by cas_init and cas_reset.  Anything previously stored in the heap
 * (variables, functions, ans, compiled functions) is abandoned, not freed --
 * the free list is simply rebuilt.  Requires K == c (sym_intern uses K).
 * Can only throw if the symbol table is too small for the builtins. */
static void setup_symbols_and_heap(cas_ctx *c) {
    /* symbol table */
    c->nsyms = 0;
    c->names_used = 0;
    for (uint32_t i = 0; i < 1024; i++) c->shash[i] = 0xffff;
    for (uint32_t i = 0; i < S__COUNT; i++) sym_intern(builtin_names[i], strlen(builtin_names[i]));
    /* heap: one free block */
    c->freel = (hblk *)c->h_lo;
    ((size_t *)c->h_lo)[0] = (size_t)(c->h_hi - c->h_lo);
    ((void **)c->h_lo)[1] = NULL;
    c->h_used = 0;
    c->ans = NULL;
    c->ans_serial++;
    c->pending = NULL;
}

/* Build the shared numeric constants (K->c0, c1, cm1, c2, chalf, cmhalf) in
 * the permanent area: the scratch allocator is temporarily pointed at
 * [perm, perm+permsz) so that mk_int / mk_rat allocate there, then restored.
 * The symbol constants (e, pi, i, inf, undef) are just the permanent symbol
 * nodes.  Requires the builtins to be interned already (ids S_E ...). */
static void make_constants(cas_ctx *c, char *perm, size_t permsz) {
    char *slo = c->s_lo, *shi = c->s_hi, *stop = c->s_top;
    c->s_lo = c->s_top = perm;
    c->s_hi = perm + permsz;
    c->c0 = mk_int(0);
    c->c1 = mk_int(1);
    c->cm1 = mk_int(-1);
    c->c2 = mk_int(2);
    c->chalf = mk_rat(mk_int(1), mk_int(2));
    c->cmhalf = mk_rat(mk_int(-1), mk_int(2));
    c->s_lo = slo;
    c->s_hi = shi;
    c->s_top = stop;
    c->e_ = mk_sym(S_E);
    c->pi_ = mk_sym(S_PI);
    c->i_ = mk_sym(S_I);
    c->inf_ = mk_sym(S_INF);
    c->undef_ = mk_sym(S_UNDEF);
}

/* Run setup_symbols_and_heap and make_constants for a fresh context under a
 * local error handler.  Returns 1 on success; 0 if something threw (arena
 * too small), in which case K is restored to its previous value.  On success
 * K is left pointing at c. */
static int init_tables(cas_ctx *c, char *perm) {
    cas_ctx *saved = K;
    K = c;
    tryf tf;
    try_enter(&tf);
    if (setjmp(tf.jb)) {
        try_leave(&tf);
        K = saved;
        return 0;
    }
    setup_symbols_and_heap(c);
    make_constants(c, perm, PERM_BYTES);
    try_leave(&tf);
    return 1;
}

/* Public: create a context inside a caller-provided arena (at least 256 KB).
 * The arena start is rounded up to 16 bytes and carved as described in the
 * file header.  The symbol table holds 8192 symbols for arenas of 4 MB and
 * more, else 2048, with 12 bytes of name pool per symbol on average.
 * Defaults: radians, exact mode, 12 digits, a stack budget of 48 KB on ARM
 * and 2 MB on the host, and a fixed RNG seed.  Returns NULL if the arena is
 * missing or too small; nothing is allocated outside the arena. */
cas_ctx *cas_init(void *arena, size_t size) {
    if (!arena || size < (256u << 10)) return NULL;
    char *base = (char *)(((uintptr_t)arena + 15) & ~(uintptr_t)15);
    size -= (size_t)(base - (char *)arena);
    cas_ctx *c = (cas_ctx *)base;
    memset(c, 0, sizeof *c);
    c->base = base;
    c->size = size;
    char *p = base + ((sizeof(cas_ctx) + 15) & ~(size_t)15);
    c->maxsyms = size >= (4u << 20) ? 8192 : 2048;
    c->syms = (symrec *)p;
    p += (sizeof(symrec) * c->maxsyms + 15) & ~(size_t)15;
    c->shash = (uint16_t *)p;
    p += sizeof(uint16_t) * 1024;
    c->names_cap = c->maxsyms * 12;
    c->names = p;
    p += (c->names_cap + 15) & ~(size_t)15;
    c->symnode_size = (sizeof(E) + sizeof(void *) + 7) & ~(size_t)7;
    c->symnodes = p;
    p += c->symnode_size * c->maxsyms;
    char *perm = p;
    p += PERM_BYTES;
    size_t rest = (size_t)(base + size - p);
    size_t heap = rest / 4;
    heap &= ~(size_t)15;
    c->h_lo = p;
    c->h_hi = p + heap;
    c->s_lo = c->s_top = c->h_hi;
    c->s_hi = base + size;
    c->set.angle = CAS_ANGLE_RAD;
    c->set.exact = 1;
    c->set.digits = 12;
    c->set.real = 0; /* principal branch until the application selects COMPLEX = REAL */
#ifdef __arm__
    c->stack_limit = 48u << 10;
#else
    c->stack_limit = 2u << 20;
#endif
    c->rng = 2463534242u;
    return init_tables(c, perm) ? c : NULL;
}

/* Public: replace the settings (angle mode, exact/approx, digits, real roots), clamping
 * digits to 1..1000 (values < 1 mean "default", 12).  Takes effect on the
 * next evaluation. */
void cas_set_settings(cas_ctx *ctx, const cas_settings *s) {
    ctx->set = *s;
    if (ctx->set.digits < 1) ctx->set.digits = 12;
    if (ctx->set.digits > 1000) ctx->set.digits = 1000;
    ctx->set.real = s->real != 0;
}
/* Public: copy out the current settings. */
void cas_get_settings(const cas_ctx *ctx, cas_settings *s) { *s = ctx->set; }

/* Public: bytes of C stack the engine may use below the entry point before
 * stack_check() raises CAS_ERR_MEMORY ("too deeply nested"). */
void cas_set_stack_limit(cas_ctx *ctx, size_t bytes) { ctx->stack_limit = bytes; }

/* Public: message of the most recent failure, or "ok". */
const char *cas_last_error(const cas_ctx *ctx) { return ctx->errmsg[0] ? ctx->errmsg : "ok"; }

/* Public: changes whenever ans is replaced (an evaluation, a restored session, a reset). */
uint32_t cas_ans_serial(const cas_ctx *ctx) { return ctx->ans_serial; }

/* Public: request cancellation of the running evaluation.  Only sets the
 * volatile flag K->intr, so it is safe from an interrupt / signal handler;
 * the engine notices it in salloc, stack_check and poll_intr. */
void cas_interrupt(cas_ctx *ctx) { ctx->intr = 1; }

/* Public: install a callback that the engine calls every 1024th allocation /
 * recursion check (cas_poll_step in mem.c); a nonzero return interrupts the
 * evaluation, as if cas_interrupt had been called (the application uses it
 * to time-box helper evaluations).  Resets the call counter. */
void cas_set_poll(cas_ctx *ctx, int (*poll)(void *user), void *user) {
    ctx->poll = poll;
    ctx->poll_user = user;
    ctx->poll_n = 0;
}

/* Public: forget all variables, user functions, assumptions, ans and
 * compiled functions (the heap is re-initialised wholesale, so any cas_fn
 * obtained earlier becomes invalid and must not be passed to cas_fn_free).
 * Settings, the stack limit and the poll callback are kept.  Errors cannot
 * normally occur here; one would be swallowed by the local handler. */
void cas_reset(cas_ctx *ctx) {
    K = ctx;
    tryf tf;
    try_enter(&tf);
    if (!setjmp(tf.jb)) setup_symbols_and_heap(ctx);
    try_leave(&tf);
    ctx->s_top = ctx->s_lo;
    ctx->errmsg[0] = 0;
}

/* Public: memory statistics.  "used" = fixed area (everything before the
 * persistent heap) + allocated heap bytes + scratch currently in use (zero
 * between calls, since scratch is reset by every entry point); "peak" sums
 * the individual high-water marks of heap and scratch, so it is an upper
 * bound on the true simultaneous peak. */
void cas_mem(const cas_ctx *ctx, cas_mem_stats *out) {
    size_t fixed = (size_t)(ctx->h_lo - ctx->base);
    out->arena_size = ctx->size;
    out->used = fixed + ctx->h_used + (size_t)(ctx->s_top - ctx->s_lo);
    out->peak = fixed + ctx->h_peak + ctx->s_peak;
}

/* Common exit path of the API calls: release the persistent blocks whose
 * free was deferred during the call (hfree_expr cannot free immediately
 * while scratch expressions may still point into them), drop all scratch
 * memory and the handler stack, and return st.  The deferred-free list
 * itself lives in scratch, so it must be flushed before s_top is reset. */
static cas_status finish(cas_ctx *ctx, cas_status st) {
    cas_ctx *saved = K;
    K = ctx;
    flush_frees();
    K = saved;
    ctx->s_top = ctx->s_lo;
    ctx->s_reserve = 0;
    ctx->tf = NULL;
    return st;
}

/* eval.c: evaluate a parsed top-level input (variables, user functions,
 * commands). */
extern E *eval_top(E *e);

/* the file-scope state of the algorithms (budgets, mode flags, recursion guards): an
 * error thrown through them can leave it set, so every evaluation starts from zero */
void integ_reset_statics(void);
void solve_reset_statics(void);
void trig_reset_statics(void);
void series_reset_statics(void);
void numeric_reset_statics(void);
void gbasis_reset_statics(void);
void print_reset_statics(void);
void eval_reset_statics(void);
static void cas_reset_statics(void) {
    integ_reset_statics();
    solve_reset_statics();
    trig_reset_statics();
    series_reset_statics();
    numeric_reset_statics();
    gbasis_reset_statics();
    print_reset_statics();
    eval_reset_statics();
}

/* Shared body of cas_eval and cas_eval_quiet.
 *
 * Parses `input`, evaluates it, stores the result as ans (unless keep_ans),
 * and prints it into text[0..text_len) as linear text that parse() can read
 * back.  Returns CAS_OK, CAS_ERR_OUTPUT_TRUNC if the text did not fit (the
 * value was still computed and stored in ans), or the status of the error
 * thrown during parsing/evaluation (then text is "" and ans is unchanged).
 *
 * Per-call resets: the RNG is reseeded so that randomized algorithms
 * (factorisation mod p, Miller-Rabin bases, randpoly) give reproducible
 * results for the same input, and the user-function call depth is zeroed.
 * The symbol-table GC (expr.c, sym_gc) runs here, before parsing, when the
 * table or name pool is 3/4 full: at that moment no scratch expression
 * exists, so renumbering symbols cannot leave dangling ids.
 *
 * `anchor` is a local whose address marks the top of the engine's C stack
 * use for stack_check(). */
/* A relation (=, <, >, <=, >=, !=) left inside a sum, product, power or a
 * mathematical function of the result: `x^(2=3)`, `sin(x=1)`, `(x<2)+1`.
 * Arithmetic on a whole relation applies to both sides ((x=2)+1 is x+1=3),
 * so a relation still nested after evaluation has no meaning; lists,
 * and/or/not, piecewise conditions and commands may hold relations. */
static int stray_rel(E *e, int in_math) {
    stack_check();
    if (e->t == E_FN && e->s >= F_EQ && e->s <= F_NE && e->s != F_ASSIGN && in_math) return 1;
    if (e->t != E_ADD && e->t != E_MUL && e->t != E_POW && e->t != E_FN && e->t != E_LIST) return 0;
    int m = e->t == E_ADD || e->t == E_MUL || e->t == E_POW ||
            (e->t == E_FN && ((e->s >= F_SQRT && e->s < S__FIRSTCMD && e->s != F_PIECEWISE) || e->s == F_FACT));
    for (uint32_t i = 0; i < e->n; i++)
        if (stray_rel(EK(e)[i], m)) return 1;
    return 0;
}

static cas_status eval_impl(cas_ctx *ctx, const char *input, char *text, size_t text_len, int keep_ans) {
    char anchor;
    K = ctx;
    ctx->intr = 0;
    ctx->err = CAS_OK;
    ctx->errmsg[0] = 0;
    ctx->stack_base = &anchor;
    ctx->s_top = ctx->s_lo;
    ctx->rng = 2463534242u;
    ctx->depth = 0;
    ctx->pending = NULL;
    cas_reset_statics();
    /* keep a reserve of the scratch for printing and storing the result */
    {
        size_t sc = (size_t)(ctx->s_hi - ctx->s_lo) / 16;
        ctx->s_reserve = sc < (32u << 10) ? sc : (32u << 10);
    }
    if (text && text_len) text[0] = 0;
    tryf tf;
    try_enter(&tf);
    if (setjmp(tf.jb)) {
        try_leave(&tf);
        if (text && text_len) text[0] = 0;
        return finish(ctx, ctx->err ? ctx->err : CAS_ERR_UNSUPPORTED);
    }
    if (ctx->nsyms > ctx->maxsyms * 3 / 4 || ctx->names_used > ctx->names_cap * 3 / 4) {
        extern void sym_gc(void);
        sym_gc();
        ctx->s_top = ctx->s_lo;
    }
    /* xcmds.c hooks: ext_pre rewrites the raw tree (bare `restart`), ext_post
     * post-processes the value (unit simplification, top-level approx(e, n)
     * with its own digit count). */
    extern E *ext_pre(E *e), *ext_post(E *e, E *r);
    E *e = ext_pre(parse(input));
    E *r = ext_post(e, eval_top(e));
    if (stray_rel(r, 0)) cas_throw(CAS_ERR_DOMAIN, "an equation or inequality cannot be part of an expression");
    if (!ctx->set.exact) r = evalf_digits(r, ctx->set.digits);
    ctx->s_reserve = 0;
    /* remember as ans: copy into the heap first, then free the old value
     * (deferred until finish, since r may share nodes with it) */
    if (!keep_ans) {
        E *old = ctx->ans;
        ctx->ans = hstore(r);
        ctx->ans_serial++;
        if (old) hfree_expr(old);
    }
    sbuf sb = {text, text_len, 0, 0};
    if (text && text_len) {
        /* a series/taylor command: its terms in ascending order and +O() */
        extern E *g_sdisp;
        extern void print_series(sbuf *b, E *d);
        if (g_sdisp && e->t == E_FN && (e->s == C_SERIES || e->s == C_TAYLOR))
            print_series(&sb, g_sdisp);
        else
            print_expr(&sb, r);
    }
    try_leave(&tf);
    if (sb.overflow) {
        ctx->err = CAS_ERR_OUTPUT_TRUNC;
        strcpy(ctx->errmsg, "output truncated");
        return finish(ctx, CAS_ERR_OUTPUT_TRUNC);
    }
    return finish(ctx, CAS_OK);
}

/* Public: evaluate one input line, update ans, print the result. */
cas_status cas_eval(cas_ctx *ctx, const char *input, char *text, size_t text_len) {
    return eval_impl(ctx, input, text, text_len, 0);
}

/* Public: like cas_eval but ans is left unchanged (used by the application
 * for helper evaluations between user entries: defining Y1(x)..Y6(x),
 * polar display of ans, statistics lists). */
cas_status cas_eval_quiet(cas_ctx *ctx, const char *input, char *text, size_t text_len) {
    return eval_impl(ctx, input, text, text_len, 1);
}

/* numeric.c: bytecode compilers for cas_compile; eval.c: evaluation with
 * some variables temporarily unbound. */
extern cas_fn *compile_expr(E *e, int var);
extern cas_fn *compile_expr_vars(E *e, const int *vars, int nvars);
extern E *eval_hiding(E *e, int nvars, int *vars);

/* Public: compile `expr` as a function of the single variable `var` into
 * stack bytecode for fast double-precision evaluation (graphing).
 *
 * The expression is evaluated symbolically first (so stored variables and
 * user functions are substituted), but with `var` hidden: if the user has
 * assigned x:=3, graphing "x^2" must still be a function of x.  The compiled
 * function is allocated in the persistent heap (*out), valid until
 * cas_fn_free or cas_reset.  On failure *out is NULL and the status says why.
 * Note: unlike eval_impl this does not reset the RNG; K->depth (user
 * function call depth) is reset, since an earlier error may have left it
 * high. */
cas_status cas_compile(cas_ctx *ctx, const char *expr, const char *var, cas_fn **out) {
    char anchor;
    K = ctx;
    ctx->intr = 0;
    ctx->err = CAS_OK;
    ctx->errmsg[0] = 0;
    ctx->stack_base = &anchor;
    ctx->s_top = ctx->s_lo;
    ctx->depth = 0;
    ctx->pending = NULL;
    *out = NULL;
    tryf tf;
    try_enter(&tf);
    if (setjmp(tf.jb)) {
        try_leave(&tf);
        return finish(ctx, ctx->err ? ctx->err : CAS_ERR_UNSUPPORTED);
    }
    int v = sym_intern(var, strlen(var));
    E *e = parse(expr);
    E *r = eval_hiding(e, 1, &v);
    *out = compile_expr(r, v);
    try_leave(&tf);
    return finish(ctx, CAS_OK);
}

/* Public: like cas_compile with 1..24 independent variables (bounded by the
 * bytecode's variable slots, CFN_MAXVARS in numeric.c), all hidden by
 * eval_hiding. */
cas_status cas_compile_vars(cas_ctx *ctx, const char *expr, const char *const *vars, int nvars, cas_fn **out) {
    char anchor;
    K = ctx;
    ctx->intr = 0;
    ctx->err = CAS_OK;
    ctx->errmsg[0] = 0;
    ctx->stack_base = &anchor;
    ctx->s_top = ctx->s_lo;
    ctx->depth = 0;
    ctx->pending = NULL;
    *out = NULL;
    if (nvars < 1 || nvars > 24) {
        strcpy(ctx->errmsg, "too many variables to compile");
        ctx->err = CAS_ERR_UNSUPPORTED;
        return finish(ctx, CAS_ERR_UNSUPPORTED);
    }
    tryf tf;
    try_enter(&tf);
    if (setjmp(tf.jb)) {
        try_leave(&tf);
        return finish(ctx, ctx->err ? ctx->err : CAS_ERR_UNSUPPORTED);
    }
    int v[24];
    for (int i = 0; i < nvars; i++) v[i] = sym_intern(vars[i], strlen(vars[i]));
    E *e = parse(expr);
    E *r = eval_hiding(e, nvars, v);
    *out = compile_expr_vars(r, v, nvars);
    try_leave(&tf);
    return finish(ctx, CAS_OK);
}

/* Public: release a compiled function (a single persistent-heap block).
 * NULL is ignored.  Must not be used on functions compiled before the last
 * cas_reset (their block no longer exists). */
void cas_fn_free(cas_ctx *ctx, cas_fn *fn) {
    K = ctx;
    if (fn) hfree(fn);
}

/* print.c: LaTeX printer. */
void print_latex(sbuf *b, E *e);

/* Public: render the current ans as LaTeX into buf (for the 2D display).
 * With no ans, buf is "" and CAS_OK is returned.  Uses scratch for the
 * printer's temporaries; returns CAS_ERR_OUTPUT_TRUNC if buf is too small.
 * A stale interrupt request is cleared first; unlike the other entry points
 * the previous error message is kept. */
cas_status cas_latex(cas_ctx *ctx, char *buf, size_t len) {
    char anchor;
    K = ctx;
    ctx->intr = 0;
    ctx->stack_base = &anchor;
    ctx->s_top = ctx->s_lo;
    if (len) buf[0] = 0;
    if (!ctx->ans) return CAS_OK;
    tryf tf;
    try_enter(&tf);
    if (setjmp(tf.jb)) {
        try_leave(&tf);
        return finish(ctx, ctx->err ? ctx->err : CAS_ERR_UNSUPPORTED);
    }
    sbuf sb = {buf, len, 0, 0};
    print_latex(&sb, ctx->ans);
    try_leave(&tf);
    return finish(ctx, sb.overflow ? CAS_ERR_OUTPUT_TRUNC : CAS_OK);
}
