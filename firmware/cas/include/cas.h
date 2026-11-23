/*
 * cas.h — interface between the CAS engine (firmware/cas) and the calculator
 * application (firmware/app). Portable C11, no OS, no malloc: all memory comes
 * from the arena passed to cas_init (on the calculator: 8-24 MB of SDRAM).
 *
 * This header is the contract. Changing a signature requires updating both
 * sides; adding functions is fine.
 *
 * Two implementations exist:
 *   - firmware/cas/src/      the symbolic engine (entry points in src/cas.c,
 *                            session persistence in src/session.c, compiled
 *                            numerics in src/numeric.c);
 *   - firmware/cas_stub/     a double-precision stand-in (cas_stub.c) that
 *                            implements the same contract numerically.
 *
 * Usage pattern:
 *   cas_ctx *c = cas_init(arena, size);       once, at boot
 *   cas_set_settings(c, &s);                  whenever the mode screen changes
 *   cas_eval(c, "factor(x^2-1)", out, n);     per entry line
 *   cas_compile(c, "sin(x)/x", "x", &fn);     per graphed function
 *   cas_fn_eval(fn, x);                       per plotted point (no allocation)
 *
 * Threading: the engine is single-threaded.  Each entry point records `ctx` as
 * the library's current context, so only one call may run at a time; the only
 * exception is cas_interrupt(), which merely sets a volatile flag and may be
 * called from an interrupt handler or signal handler during an evaluation.
 *
 * Errors: no entry point aborts.  Failures inside the engine are reported as a
 * cas_status, the context stays usable, and cas_last_error() describes the
 * most recent failure.
 */
#ifndef CAS_H
#define CAS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque engine state: settings, symbol table, stored variables and functions,
 * ans, error message and the memory regions carved from the arena.  It lives at
 * the (16-byte aligned) start of the arena passed to cas_init and is never
 * freed; discarding the arena discards the context. */
typedef struct cas_ctx cas_ctx;

/* Result of every fallible entry point.  On any value other than CAS_OK,
 * cas_last_error() returns a human-readable explanation. */
typedef enum {
    CAS_OK = 0,
    CAS_ERR_SYNTAX,       /* could not parse input */
    CAS_ERR_DOMAIN,       /* e.g. division by zero, log(0) */
    CAS_ERR_UNDEFINED,    /* unknown variable/function */
    CAS_ERR_MEMORY,       /* arena exhausted */
    CAS_ERR_INTERRUPTED,  /* cas_interrupt() was called (ON key) */
    CAS_ERR_UNSUPPORTED,  /* valid input the engine cannot handle yet */
    CAS_ERR_OUTPUT_TRUNC  /* result did not fit in the output buffer */
} cas_status;

/* Angle unit for trigonometric functions.  In degree mode, arguments of
 * sin/cos/tan/... are scaled by pi/180 and results of the inverse functions by
 * 180/pi. */
typedef enum {
    CAS_ANGLE_RAD = 0,
    CAS_ANGLE_DEG = 1
} cas_angle_mode;

/* User-visible evaluation mode (the MODE screen).  Read and written as a whole
 * with cas_get_settings / cas_set_settings.  Defaults after cas_init: radians,
 * exact = 1, digits = 12, real = 0.
 *
 * `real` is the COMPLEX = REAL setting.  With real = 1 a negative base with a
 * rational exponent p/q, q odd, means the real root (-1)^p |b|^(p/q):
 * (-8)^(1/3) = -2, (-1)^(2/3) = 1, (-8)^(-1/3) = -1/2, in simplification,
 * numeric and high-precision evaluation, integrals, series, limits and solve.
 * Even roots of negatives (sqrt(-1), (-8)^(1/6)) and non-rational exponents
 * stay principal (complex; the application refuses non-real results).  With
 * real = 0 every power uses the principal branch e^(e ln b).  Compiled
 * functions (cas_compile) use the real root in both modes. */
typedef struct {
    cas_angle_mode angle;
    int exact;            /* 1: keep exact results (sqrt(2), 1/3); 0: approximate to decimal */
    int digits;           /* significant digits for approximate output (default 12) */
    int real;             /* 1: real odd roots of negative numbers (COMPLEX mode REAL) */
} cas_settings;

/* Create a context inside [arena, arena+size). Returns NULL if size is too small.
 * The real engine needs at least 256 KB; it aligns the arena to 16 bytes and
 * splits it once into the context, the symbol table, a small block of permanent
 * constants, a persistent heap (1/4 of the remainder: variables, functions,
 * ans, compiled functions) and a scratch region (3/4: temporaries of one
 * evaluation).  The arena must stay valid for the lifetime of the context. */
cas_ctx *cas_init(void *arena, size_t size);
/* Replace the settings.  `digits` is clamped to 1..1000 (values < 1 become the
 * default 12).  Takes effect for the next evaluation. */
void cas_set_settings(cas_ctx *ctx, const cas_settings *s);
/* Copy the current settings into *s. */
void cas_get_settings(const cas_ctx *ctx, cas_settings *s);

/*
 * Evaluate one line of input, e.g. "factor(x^2-1)", "integrate(sin(x)^2, x)",
 * "a:=3", "solve(x^2=2, x)", "d/dx", "2+3", "ans*2".
 * Output forms (NUL-terminated):
 *   text  — linear text the user could re-enter, e.g. "(x-1)*(x+1)"
 *   pretty — optional: 2D layout hint; may be NULL (see cas_render_* later)
 * Assignments persist in ctx; "ans" refers to the previous result.
 *
 * `text` receives the result as linear text (it may be NULL / text_len 0 when
 * only the side effects are wanted).  On error, text is set to "" and the
 * status says why.  If the result does not fit, CAS_ERR_OUTPUT_TRUNC is
 * returned, but ans has already been updated.  In approximate mode
 * (settings.exact == 0) the result is converted to floating point with
 * settings.digits significant digits before it is stored and printed.
 * All temporaries of the evaluation are released when it returns.
 */
cas_status cas_eval(cas_ctx *ctx, const char *input, char *text, size_t text_len);

/* Like cas_eval, but ans is left unchanged: for helper evaluations made by the
 * application (defining Y1(x), polar conversion, ...) between user entries. */
cas_status cas_eval_quiet(cas_ctx *ctx, const char *input, char *text, size_t text_len);

/* A number that changes whenever ans is replaced (an evaluation that is not
 * quiet, a restored session, a reset): the application tells whether ans
 * still holds a result it noted. */
uint32_t cas_ans_serial(const cas_ctx *ctx);

/* LaTeX rendering of the most recent result ("ans"), for 2D display:
 * fractions as \frac, roots as \sqrt, matrices as pmatrix, etc.
 * Returns CAS_ERR_OUTPUT_TRUNC if it does not fit. */
cas_status cas_latex(cas_ctx *ctx, char *buf, size_t len);

/* Bytes of C stack the engine may use before reporting "too deeply nested"
 * (default 48 KB on ARM builds, 2 MB on host builds). */
void cas_set_stack_limit(cas_ctx *ctx, size_t bytes);

/* Human-readable message for the last error (static string or in ctx).
 * Returns "ok" when the last call succeeded.  The pointer stays valid until
 * the next call into the engine. */
const char *cas_last_error(const cas_ctx *ctx);

/* Request that a running evaluation stop soon (safe to call from an ISR).
 * The flag is polled by the allocator and inside long loops; the evaluation
 * then returns CAS_ERR_INTERRUPTED.  The flag is cleared at the start of every
 * cas_eval / cas_compile. */
void cas_interrupt(cas_ctx *ctx);

/* Optional poll callback: while cas_eval/cas_eval_quiet run, it is called
 * every 1024 allocation/recursion checks; a nonzero return interrupts the
 * evaluation (CAS_ERR_INTERRUPTED), like cas_interrupt(). NULL disables it.
 * The application uses it to time-box helper evaluations (the home screen's
 * live preview) and to abandon them when a key is pressed. */
void cas_set_poll(cas_ctx *ctx, int (*poll)(void *user), void *user);

/* Discard all variables/history and reclaim the arena.  Settings are kept.
 * The persistent heap is reinitialised, so every cas_fn obtained earlier
 * becomes invalid and must not be evaluated or freed afterwards. */
void cas_reset(cas_ctx *ctx);

/* --------------------------------------------------------- fast numerics ---
 * For graphing/tables: compile an expression in one variable to a form that
 * evaluates quickly in double precision (thousands of points per frame).
 * In the real engine the expression is first evaluated symbolically (stored
 * variables substituted, except the compiled variables) and then translated to
 * a small stack bytecode stored in the persistent heap.
 */
/* Opaque compiled function (bytecode in the context's persistent heap). */
typedef struct cas_fn cas_fn;

/* Compile `expr` (e.g. "sin(x)/x") with respect to variable `var` ("x").
 * On success *out receives a function that remains valid until cas_fn_free or
 * cas_reset; on failure *out is NULL and the status explains why. */
cas_status cas_compile(cas_ctx *ctx, const char *expr, const char *var, cas_fn **out);
/* Evaluate; returns NAN where undefined. Must not allocate.
 * Rational powers with an odd denominator use the real root (cbrt(-8) = -2),
 * as a graph of x^(1/3) expects.  fn == NULL yields NAN. */
double cas_fn_eval(const cas_fn *fn, double x);
/* Release a compiled function (NULL is ignored). */
void cas_fn_free(cas_ctx *ctx, cas_fn *fn);

/* Several independent variables (1..24), e.g. {"x","y"} for implicit plots or
 * {"x","a","b"} for graph sliders: names in `vars` are never replaced by stored
 * values. Evaluate with cas_fn_eval_vars(fn, vals), vals[i] = value of vars[i]
 * (must not allocate); cas_fn_eval(fn, x) sets vars[0] = x and the others 0. */
cas_status cas_compile_vars(cas_ctx *ctx, const char *expr, const char *const *vars, int nvars, cas_fn **out);
double cas_fn_eval_vars(const cas_fn *fn, const double *vals);

/* Memory statistics for the debug console.
 *   arena_size  usable bytes of the arena (after alignment)
 *   used        fixed tables + live persistent-heap bytes + current scratch use
 *   peak        fixed tables + peak heap use + peak scratch use (the two peaks
 *               may have occurred at different times, so this is an upper
 *               bound on the true high-water mark) */
typedef struct {
    size_t arena_size, used, peak;
} cas_mem_stats;
/* Fill *out with the current memory statistics (no side effects). */
void cas_mem(const cas_ctx *ctx, cas_mem_stats *out);

/* ------------------------------------------------------ session state ---
 * Persistence of the user state (variables, user functions, assumptions and
 * ans) across power cycles. The state is written as compact text records, not
 * as raw arena memory, so it survives firmware updates and its size is
 * proportional to what the user stored:
 *
 *   CAS1                     header (format version)
 *   v NAME EXPR              variable value (linear text, floats with 17 digits)
 *   f NAME [P1,...,BODY]     user function: parameters and unevaluated body
 *   a NAME FLAGS             assumptions (assume(...)), decimal flag bits
 *   n EXPR                   ans
 *
 * Each record ends with '\n'.
 */
/* Receives complete records (one or more whole lines). Return 0 to continue,
 * nonzero to stop (cas_save then returns CAS_ERR_OUTPUT_TRUNC). A writer that
 * runs out of room may also drop a record and return 0. */
typedef int (*cas_writer)(void *user, const char *data, size_t n);
/* Serialize the session. Records longer than 64 KB are skipped. */
cas_status cas_save(cas_ctx *ctx, cas_writer write, void *user);
/* Like cas_save, but writes only the records of the variables (and
 * functions, assumptions) whose names start with `prefix`, internal names
 * starting with '_' included, without the header and the ans record. The
 * application keeps hidden values this way (the RPN stack levels _rpnN,
 * app/rpn.c); cas_restore reads the records back. */
cas_status cas_save_prefix(cas_ctx *ctx, const char *prefix, cas_writer write, void *user);
/* Restore records written by cas_save (adds to the current state; call it on a
 * fresh or cas_reset context). Values are restored as stored (no variable
 * substitution, angle mode and exact/approx do not apply). Bad records are
 * skipped: the result is the status of the first failed record (e.g.
 * CAS_ERR_SYNTAX, CAS_ERR_MEMORY), else CAS_OK; an interrupt stops the restore
 * with CAS_ERR_INTERRUPTED. */
cas_status cas_restore(cas_ctx *ctx, const char *text, size_t len);

/* Enumerate user variables and functions (for the VARS menu), in symbol-table
 * order. nparams < 0 for a variable, >= 0 for a user function. Names starting
 * with '_' (internal) are not reported. */
typedef void (*cas_var_fn)(void *user, const char *name, int nparams);
/* Call fn(user, name, nparams) once per user variable / user function. */
void cas_vars(cas_ctx *ctx, cas_var_fn fn, void *user);

/* CAS extension modules (src/casx.h; docs/QSPI_CONTENT.md 12.6).  Commands of
 * an extension that is not registered call `load(module)` once per use; it
 * returns 0 after binding the module (whose init registers the commands
 * through the table cas_ext_api() returns), else the command fails with
 * "NAME needs the advanced CAS module (Memory > Modules)".  cas_ext_unload
 * forgets every registration (before the module code goes away: store
 * changes); cas_ext_busy is nonzero while extension code is running. Only
 * the real CAS (src/) has these. */
void cas_ext_set_loader(int (*load)(const char *module));
const void *cas_ext_api(void);
void cas_ext_unload(void);
int cas_ext_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* CAS_H */
