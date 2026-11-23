/*
 * The Python module (modules/python): what its files share.
 *
 *   py_entry.c  the module entry (modx), the MicroPython port (HAL, GC roots,
 *               the poll hook, output), running code (py_run: REPL lines,
 *               files, notebook-style cells), the console `py` / `pyexec`
 *   py_calc.c   the `calc` library: the CAS from Python (calc.Expr, solve,
 *               integrate, diff, limit, factor, ..., latex, plot)
 *   py_ui.c     the Python console screen (output, multi-line blocks,
 *               history, soft keys)
 *
 * The module is a code-load module (app/modx.h): its image runs from SDRAM
 * with ordinary static data, so this port needs no change to MicroPython's
 * own sources. In the simulators the same sources are linked into the host
 * binary (app/modx.c host_find).
 */
#ifndef PY_MOD_H
#define PY_MOD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "modx.h"
#include "py_core.h"
#include "py/obj.h"

extern const modx_api_t *py_api;  /* the core's tables (set by init) */
extern const py_core_t *py_pc;

/* Where output (print, tracebacks, REPL values) goes: any of these. */
enum { PY_OUT_SCREEN = 1, PY_OUT_CONSOLE = 2, PY_OUT_CAPTURE = 4 };
void py_out_set(int sinks);
int py_out_get(void);
void py_out_flush(void);                 /* the console's partial line */
const char *py_capture(size_t *len);     /* captured text (PY_OUT_CAPTURE) since py_capture_reset */
void py_capture_reset(void);
size_t py_capture_tb(void);              /* where the traceback of the last run starts in it ((size_t)-1: none) */
void py_restart(bool bare);
const char *py_srcbuf(void);             /* the `py buf` buffer (the notebook's long cell sources) */
void py_srcbuf_clear(void);              /* a fresh interpreter (`py reset`) */

/* Running code: the reusable core of the console screen, the console
 * commands and (phase 2) a notebook cell.
 *   PY_RUN_REPL  one REPL entry (single input: an expression statement prints its repr)
 *   PY_RUN_FILE  a program
 *   PY_RUN_CELL  a program whose last statement, when it is an expression, gives
 *                the value (*value; MP_OBJ_NULL otherwise)
 * Output goes to the current sinks; an uncaught exception prints its traceback
 * there. Returns PY_OK, PY_EXC (an exception; py_last_exc names its type) or
 * PY_INTR (KeyboardInterrupt). */
enum { PY_RUN_REPL, PY_RUN_FILE, PY_RUN_CELL };
enum { PY_OK = 0, PY_EXC = 1, PY_INTR = 2 };
int py_run(const char *src, int mode, mp_obj_t *value);
const char *py_last_exc(void);
bool py_running(void);
/* the text of a value: repr, and LaTeX when it is a calc.Expr (else "") */
void py_value_text(mp_obj_t v, char *out, size_t n);
void py_value_latex(mp_obj_t v, char *out, size_t n);

/* C stack in use below the module's entry (for CAS calls from Python) */
size_t py_stack_used(void);
/* the break key: true (once) when ON was pressed since the run began; raises
 * nothing by itself */
bool py_break_pending(void);

/* calc (py_calc.c) */
extern const mp_obj_type_t py_type_expr;
bool py_is_expr(mp_obj_t o);
const char *py_expr_text(mp_obj_t o);
mp_obj_t py_expr_new(const char *cas); /* a calc.Expr of CAS text (not evaluated) */
void py_calc_latex(mp_obj_t o, char *out, size_t n);
void py_latex_text(const char *cas_text, char *out, size_t n); /* py_latex.c */

/* the screen (py_ui.c) */
void pyui_init(void);
void py_translate(const char *in, char *out, size_t n); /* calculator text (√ π ² ^ ...) -> Python */
void pyui_out(const char *s, size_t n);
int pyui_event(int ev, int arg);
bool pyui_draw(void);
void pyui_progress(void);                /* while code runs: show new output now and then */
void pyui_state(void (*kv)(const char *, const char *, void *), void *user);
int pyui_console(int argc, char **argv); /* `py key|line|...` helpers for tests (-> 1 if handled) */

#endif
