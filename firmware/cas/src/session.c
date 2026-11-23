/*
 * session.c -- persistence of the user state (include/cas.h "session state"):
 * cas_save writes variables, user functions, assumptions and ans as text
 * records; cas_restore reads them back; cas_vars lists user names.
 *
 * The calculator application (firmware/app/vars.c) stores the cas_save
 * output as one blob of its persistent settings and restores it at start-up,
 * since the arena itself lives in volatile SDRAM.  The format is plain
 * text, one record per line, so it is independent of the arena layout,
 * symbol numbering and engine version as long as the printer and the parser
 * agree (print_expr output is designed to be read back by parse):
 *
 *   CAS1                     header (any line starting with 'C' is skipped)
 *   v NAME EXPR              variable NAME has value EXPR
 *   f NAME [p1,...,pn,BODY]  user function: parameters and unevaluated body,
 *                            exactly the LIST stored in symrec.fdef
 *   a NAME FLAGS             assumption bits A_* (decimal)
 *   n EXPR                   ans
 *
 * Names starting with '_' (internal dummies and units, which carry
 * assumption bits) are not saved by cas_save; cas_save_prefix writes only
 * the records of the names with a given prefix, '_' names included (the
 * application's hidden variables, e.g. the RPN stack levels _rpnN).  Floats are printed with 17 significant
 * digits so that doubles round-trip exactly.
 *
 * Restoring evaluates each EXPR with K->restoring set, which makes the
 * evaluator (eval.c) skip variable and user-function substitution: a value
 * that mentions another variable is restored as the same expression, not as
 * the other variable's current value.  The restore runs in exact mode and
 * radians so that neither evalf nor the degree conversion alters the
 * restored values.  Commands inside a value are still evaluated.
 */
#include "cas_int.h"
#include <string.h>

/* cold code: compiled for size (flash bank 2) */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

/* Each record is formatted into one REC_MAX scratch buffer before it is
 * passed to the writer. */
#define REC_MAX (64u << 10) /* longest record cas_save writes */

/* eval.c */
extern E *eval(E *e);

/* Symbols whose state belongs to the user (same rule as assignable() in
 * eval.c): user-created ids plus the builtins x and _t. */
static int user_sym(uint32_t i) { return i >= S__COUNT || i == S_X || i == S_T__; }

/* Common exit of the session entry points: drop all scratch memory and the
 * handler stack, return st.  (No deferred frees can be pending here: cas_save
 * frees nothing and cas_restore flushes after every record.) */
static cas_status finish_call(cas_ctx *ctx, cas_status st) {
    ctx->s_top = ctx->s_lo;
    ctx->tf = NULL;
    return st;
}

/* Emit one record "<tag> NAME EXPR\n" (name may be NULL). Returns 0 = ok,
 * 1 = skipped (too long), -1 = writer asked to stop.  The record is formatted
 * into buf (REC_MAX bytes of scratch) before it is handed to the writer.  e
 * is printed with print_expr; when e is NULL the integer num is printed
 * instead (assumption records). */
static int emit(char *buf, char tag, const char *name, E *e, long num, cas_writer wr, void *user) {
    sbuf sb = {buf, REC_MAX, 0, 0};
    buf[0] = 0;
    sb_putc(&sb, tag);
    if (name) {
        sb_putc(&sb, ' ');
        sb_puts(&sb, name);
    }
    sb_putc(&sb, ' ');
    if (e)
        print_expr(&sb, e);
    else
        sb_int(&sb, num);
    sb_putc(&sb, '\n');
    if (sb.overflow) return 1;
    return wr(user, buf, sb.len) ? -1 : 0;
}

/* Shared body of cas_save (prefix NULL: everything but '_' names, with the
 * header and ans) and cas_save_prefix (only names starting with prefix).
 * Serialises through the writer callback wr(user, data, len), which returns nonzero to abort (then CAS_ERR_OUTPUT_TRUNC).  Records
 * longer than REC_MAX are skipped silently (the status stays CAS_OK), as
 * documented in cas.h.  The digits setting is raised to 17 while printing
 * and restored afterwards, also on error.  Errors inside the printer (out of
 * scratch, interrupt) are returned as their status.  Nothing in the
 * persistent heap is modified. */
static cas_status save_impl(cas_ctx *ctx, const char *prefix, cas_writer wr, void *user) {
    char anchor;
    K = ctx;
    ctx->intr = 0;
    ctx->err = CAS_OK;
    ctx->errmsg[0] = 0;
    ctx->stack_base = &anchor;
    ctx->s_top = ctx->s_lo;
    cas_settings saved = ctx->set;
    volatile cas_status st = CAS_OK;
    tryf tf;
    try_enter(&tf);
    extern void unc_set_raw(int on); /* unc.c: keep uncertainty sources (9.81+0.02*_unc1) */
    if (setjmp(tf.jb)) {
        try_leave(&tf);
        ctx->set = saved;
        unc_set_raw(0);
        return finish_call(ctx, ctx->err ? ctx->err : CAS_ERR_MEMORY);
    }
    ctx->set.digits = 17; /* floats round-trip exactly */
    unc_set_raw(1);
    char *buf = salloc(REC_MAX);
    size_t plen = prefix ? strlen(prefix) : 0;
    if (!prefix && wr(user, "CAS1\n", 5)) st = CAS_ERR_OUTPUT_TRUNC;
    for (uint32_t i = 0; i < ctx->nsyms && st == CAS_OK; i++) {
        symrec *r = &ctx->syms[i];
        if (!user_sym(i) || (!r->val && !r->fdef && !r->assume)) continue;
        const char *name = sym_name((int)i);
        if (prefix ? strncmp(name, prefix, plen) != 0 : name[0] == '_' && strcmp(name, "_facts")) continue;
        int rc = 0;
        if (r->val) rc = emit(buf, 'v', name, r->val, 0, wr, user);
        if (rc >= 0 && r->fdef) rc = emit(buf, 'f', name, r->fdef, 0, wr, user);
        if (rc >= 0 && r->assume) rc = emit(buf, 'a', name, NULL, r->assume, wr, user);
        if (rc < 0) st = CAS_ERR_OUTPUT_TRUNC;
    }
    if (!prefix && st == CAS_OK && ctx->ans && emit(buf, 'n', NULL, ctx->ans, 0, wr, user) < 0) st = CAS_ERR_OUTPUT_TRUNC;
    try_leave(&tf);
    ctx->set = saved;
    unc_set_raw(0);
    return finish_call(ctx, st);
}

/* Public: the whole user session (header, variables, functions,
 * assumptions, ans; no '_' names). */
cas_status cas_save(cas_ctx *ctx, cas_writer wr, void *user) { return save_impl(ctx, NULL, wr, user); }

/* Public: only the records of the names starting with prefix (no header,
 * no ans), for hidden application variables. */
cas_status cas_save_prefix(cas_ctx *ctx, const char *prefix, cas_writer wr, void *user) {
    return save_impl(ctx, prefix ? prefix : "", wr, user);
}

/* Field separator inside a record. */
static int is_space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

/* Restore one record (one line, NUL-terminated, newline removed) into the
 * current context.  The name is interned (it may be new); reserved names and
 * malformed records throw CAS_ERR_SYNTAX.  Values are rebuilt with
 * eval(parse(text)) -- with K->restoring set this re-simplifies without
 * substituting names (command calls in the text do run again) -- and
 * stored as new heap copies, replacing (and freeing, deferred) any previous
 * value or definition of that name, exactly as an assignment would.  A
 * function record is stored as parsed, without evaluation, since bodies are
 * kept raw.  The old values are replaced only after the new one has been
 * built successfully, so a failing record leaves that name unchanged. */
static void restore_record(char *line) {
    char tag = line[0];
    if (tag == 'C' || tag == 0) return; /* header */
    char *p = line + 1;
    while (is_space(*p)) p++;
    int id = -1;
    if (tag != 'n') {
        char *nm = p;
        while (*p && !is_space(*p)) p++;
        if (p == nm) cas_throw(CAS_ERR_SYNTAX, "bad record");
        id = sym_intern(nm, (size_t)(p - nm));
        if (!user_sym((uint32_t)id)) cas_throw(CAS_ERR_SYNTAX, "reserved name");
        while (is_space(*p)) p++;
    }
    symrec *r = id >= 0 ? &K->syms[id] : NULL;
    switch (tag) {
    case 'v': {
        E *v = eval(parse(p));
        E *old = r->val, *oldf = r->fdef;
        r->val = hstore(v);
        r->fdef = NULL;
        if (old) hfree_expr(old);
        if (oldf) hfree_expr(oldf);
        break;
    }
    case 'f': {
        E *d = parse(p);
        if (d->t != E_LIST || d->n < 1) cas_throw(CAS_ERR_SYNTAX, "bad function record");
        for (uint32_t i = 0; i + 1 < d->n; i++)
            if (EK(d)[i]->t != E_SYM) cas_throw(CAS_ERR_SYNTAX, "bad function parameter");
        E *old = r->val, *oldf = r->fdef;
        r->fdef = hstore(d);
        r->val = NULL;
        if (old) hfree_expr(old);
        if (oldf) hfree_expr(oldf);
        break;
    }
    case 'a': {
        long v = 0;
        while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
        r->assume = (uint8_t)v;
        break;
    }
    case 'n': {
        E *v = eval(parse(p));
        E *old = K->ans;
        K->ans = hstore(v);
        K->ans_serial++;
        if (old) hfree_expr(old);
        break;
    }
    default:
        cas_throw(CAS_ERR_SYNTAX, "unknown record");
    }
}

/* Public: read records written by cas_save from text[0..len) (stops early at
 * a NUL byte) and add them to the current state.  Each line is restored
 * under its own error handler with a fresh scratch region, so one bad
 * record does not prevent the others; if any record failed the result is the
 * status of the first failure (with a generic message); an interrupt stops
 * the restore and returns CAS_ERR_INTERRUPTED.
 * Pending heap frees are flushed after every record because the pending
 * list lives in the scratch region that the next record resets.  Settings
 * are forced to exact/radians during the restore and restored afterwards;
 * K->restoring disables variable / function substitution (see the header). */
cas_status cas_restore(cas_ctx *ctx, const char *text, size_t len) {
    char anchor;
    K = ctx;
    ctx->intr = 0;
    ctx->err = CAS_OK;
    ctx->errmsg[0] = 0;
    ctx->stack_base = &anchor;
    ctx->depth = 0;
    ctx->pending = NULL;
    cas_settings saved = ctx->set;
    ctx->set.angle = CAS_ANGLE_RAD;
    ctx->set.exact = 1;
    ctx->restoring = 1;
    cas_status result = CAS_OK;
    size_t pos = 0;
    while (pos < len) {
        size_t e = pos;
        while (e < len && text[e] != '\n' && text[e]) e++;
        size_t n = e - pos;
        ctx->s_top = ctx->s_lo;
        ctx->pending = NULL;
        tryf tf;
        try_enter(&tf);
        if (setjmp(tf.jb)) {
            try_leave(&tf);
            /* keep the first failure's actual status (syntax, memory, ...);
             * an interrupt stops the restore */
            if (result == CAS_OK) result = ctx->err ? ctx->err : CAS_ERR_SYNTAX;
            if (ctx->err == CAS_ERR_INTERRUPTED) {
                result = CAS_ERR_INTERRUPTED;
                flush_frees();
                break;
            }
        } else {
            char *line = salloc(n + 1);
            memcpy(line, text + pos, n);
            line[n] = 0;
            restore_record(line);
            try_leave(&tf);
        }
        flush_frees();
        if (e < len && !text[e]) break;
        pos = e + 1;
    }
    ctx->restoring = 0;
    ctx->set = saved;
    if (result != CAS_OK) {
        ctx->err = result;
        strcpy(ctx->errmsg, "some saved records could not be restored");
    }
    return finish_call(ctx, result);
}

/* Public: enumerate user variables and functions (not assumption-only
 * names, not names starting with '_') in symbol-table order.  nparams is
 * the function's parameter count, or -1 for a variable.  Reads the table
 * only: no allocation, no error handling, does not set K. */
void cas_vars(cas_ctx *ctx, cas_var_fn fn, void *user) {
    for (uint32_t i = 0; i < ctx->nsyms; i++) {
        symrec *r = &ctx->syms[i];
        if (!user_sym(i) || (!r->val && !r->fdef)) continue;
        const char *name = ctx->names + r->name;
        if (name[0] == '_') continue;
        fn(user, name, r->fdef ? (int)r->fdef->n - 1 : -1);
    }
}
