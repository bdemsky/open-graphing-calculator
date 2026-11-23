/*
 * The Python module's entry and its MicroPython port (design in py_mod.h,
 * app/py_core.h and docs/QSPI_CONTENT.md 12.8).
 *
 * Memory: the module's RW block (CONFIG_PYTHON_HEAP_KB) is MicroPython's GC
 * heap; the interpreter's own state (mp_state_ctx) and this file's statics
 * are ordinary static data, which a code-load module may have (on the
 * calculator they live in the module's copy in SDRAM, zeroed at every bind;
 * in the simulators they are the host binary's, so init resets them).
 *
 * The C stack: every call from the core (init, event, draw, console) marks
 * the top of the stack MicroPython may use and scan (mp_cstack_init_with_top,
 * PYTHON_STACK_KB on the calculator), so deep recursion raises RuntimeError
 * instead of overflowing, and the garbage collector scans exactly the
 * module's frames. Work that holds Python objects happens in functions
 * called below that mark (noinline), never in the entry frames themselves.
 *
 * Responsiveness: MICROPY_VM_HOOK_POLL (mpconfigport.h) calls py_poll every
 * 64 backward jumps / returns: an ON press since the run began becomes a
 * KeyboardInterrupt (mp_sched_keyboard_interrupt), and every 20 ms the core's
 * poll runs (watchdog, USB) and the screen shows new output. A CAS call from
 * Python that the ON key interrupts raises KeyboardInterrupt too.
 */
#include <string.h>
#include "py_mod.h"
#include "nb.h"
#include "py/compile.h"
#include "py/cstack.h"
#include "py/gc.h"
#include "py/lexer.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "py/parse.h"
#include "py/repl.h"
#include "py/runtime.h"
#include "shared/runtime/gchelper.h"

#define PY_RW_SIZE ((uint32_t)CONFIG_PYTHON_HEAP_KB << 10)

int py_module_init(const modx_api_t *api, void *rw, modx_ops_t *ops);
#if defined(MODX_IMAGE)
extern const uint8_t __modx_end[];
__attribute__((section(".modx_desc"), used)) const modx_load_desc_t modx_descriptor = {
    {MODX_MAGIC, MODX_ABI, sizeof(modx_load_desc_t), "python", PY_RW_SIZE, (uint32_t)(uintptr_t)&py_module_init,
     (uint32_t)(uintptr_t)__modx_end, sizeof(modx_api_t)},
    0, 0, 0, 0}; /* bss_size, reloc_off, reloc_count: filled by tools/modreloc.py */
#else
const uint32_t py_module_rw_size = PY_RW_SIZE;
#endif

const modx_api_t *py_api;
const py_core_t *py_pc;

static struct {
    char *stack_top;     /* the outermost entry's mark */
    int depth;           /* entries from the core in progress */
    int running;         /* py_run in progress */
    uint32_t break_seen; /* py_pc->breaks() when last looked at */
    uint32_t break_after;/* test hook: press the break key at the Nth poll */
    uint32_t last_poll, polls;
    int sinks;           /* PY_OUT_* */
    char con[200];       /* the console's partial output line */
    size_t conlen;
    char cap[16384];     /* PY_OUT_CAPTURE */
    size_t caplen;
    size_t tb_at;        /* where the last uncaught exception's traceback starts in cap */
    int mode;            /* the screen: 0 the console, 1 the Notebook */
    char line[2][512];   /* the entry line of the other screen while one is shown */
    char exc[48];        /* type of the last uncaught exception ("" none) */
    uint32_t runs, errors;
    bool started;        /* mp_init done */
    char src[16384];     /* `py add` program buffer */
    size_t srclen;
} P;

/* ------------------------------------------------------------ entries */
static bool enter(char *mark)
{
    if (P.depth++)
        return false;
    P.stack_top = mark;
    mp_cstack_init_with_top(mark, py_pc->stack_limit());
    return true;
}
static void leave(void) { P.depth--; }
size_t py_stack_used(void)
{
    char here;
    return P.stack_top && P.stack_top > &here ? (size_t)(P.stack_top - &here) : 0;
}

/* ------------------------------------------------------------ output */
void py_out_set(int sinks) { P.sinks = sinks; }
int py_out_get(void) { return P.sinks; }
void py_capture_reset(void) { P.caplen = 0, P.cap[0] = 0, P.tb_at = (size_t)-1; }
size_t py_capture_tb(void) { return P.tb_at; }
const char *py_capture(size_t *len)
{
    if (len)
        *len = P.caplen;
    return P.cap;
}
void py_out_flush(void)
{
    if (P.conlen) {
        P.con[P.conlen] = 0;
        py_api->con_printf("out+=%s\n", P.con); /* the end without a newline */
        P.conlen = 0;
    }
}

void py_out(const char *s, size_t n)
{
    if (P.sinks & PY_OUT_SCREEN)
        pyui_out(s, n);
    if (P.sinks & PY_OUT_CAPTURE) {
        size_t k = n < sizeof P.cap - 1 - P.caplen ? n : sizeof P.cap - 1 - P.caplen;
        memcpy(P.cap + P.caplen, s, k);
        P.caplen += k;
        P.cap[P.caplen] = 0;
    }
    if (P.sinks & PY_OUT_CONSOLE)
        for (size_t i = 0; i < n; i++) {
            if (s[i] == '\n' || P.conlen >= sizeof P.con - 1) {
                /* out=TEXT ends a line; out+=TEXT is a piece without the newline (long lines, the end) */
                P.con[P.conlen] = 0;
                py_api->con_printf(s[i] == '\n' ? "out=%s\n" : "out+=%s\n", P.con);
                P.conlen = 0;
                if (s[i] == '\n')
                    continue;
            }
            P.con[P.conlen++] = s[i];
        }
}

/* ------------------------------------------------------------ the break key, polling */
/* new output on the screen that shows it now and then while code runs */
static void progress(void)
{
    int scr = py_pc->size >= offsetof(py_core_t, nb_io) + sizeof py_pc->nb_io ? py_pc->nb_io(NBIO_SCREEN, 0, NULL, 0) : 2;
    if (scr == 1)
        nbui_progress();
    else if (scr == 2)
        pyui_progress();
}

bool py_break_pending(void)
{
    uint32_t b = py_pc->breaks();
    if (b == P.break_seen)
        return false;
    P.break_seen = b;
    return true;
}

void py_poll(void)
{
    if (P.break_after && !--P.break_after)
        py_pc->brk(); /* the test hook presses ON here; seen below like a real press */
    if (py_break_pending())
        mp_sched_keyboard_interrupt();
    uint32_t now = py_api->millis();
    if (now - P.last_poll >= 20 || ++P.polls >= 256) {
        P.last_poll = now, P.polls = 0;
        py_pc->poll();
        progress();
    }
}

/* ------------------------------------------------------------ HAL */
mp_uint_t mp_hal_ticks_ms(void) { return py_api->millis(); }
mp_uint_t mp_hal_ticks_us(void) { return py_api->millis() * 1000u; }
mp_uint_t mp_hal_ticks_cpu(void) { return py_api->cycles(); }
void mp_hal_delay_us(mp_uint_t us)
{
    uint32_t c0 = py_api->cycles(), n = (uint32_t)us * py_api->cycles_per_us();
    while (py_api->cycles() - c0 < n) {
    }
}
/* time.sleep: wait in 10 ms steps, polling (the ON key ends it) */
void mp_hal_delay_ms(mp_uint_t ms)
{
    while (ms) {
        uint32_t k = ms > 10 ? 10 : (uint32_t)ms;
        py_pc->delay_ms(k);
        ms -= k;
        py_pc->poll();
        progress();
        if (P.break_after && !--P.break_after)
            py_pc->brk();
        if (py_break_pending()) {
            mp_sched_keyboard_interrupt();
            mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS);
        }
    }
}
#if defined(MODX_IMAGE)
/* Loops inside built-ins (sum(range(10**8)), list(...), sorted(...)) never reach the VM's hook:
 * the image is linked with --wrap=mp_iternext, so every 1024th item polls too, and the break key
 * raises KeyboardInterrupt there. */
mp_obj_t __real_mp_iternext(mp_obj_t o);
mp_obj_t __real_mp_iternext_allow_raise(mp_obj_t o);
mp_obj_t __wrap_mp_iternext(mp_obj_t o);
mp_obj_t __wrap_mp_iternext_allow_raise(mp_obj_t o);
static void iter_poll(void)
{
    static unsigned n;
    if (++n & 1023)
        return;
    py_poll();
    mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS);
}
mp_obj_t __wrap_mp_iternext(mp_obj_t o)
{
    iter_poll();
    return __real_mp_iternext(o);
}
mp_obj_t __wrap_mp_iternext_allow_raise(mp_obj_t o)
{
    iter_poll();
    return __real_mp_iternext_allow_raise(o);
}
#endif

uint32_t py_random_seed(void) { return py_api->cycles() ^ (py_api->millis() << 16) ^ 0x2545F491u; }

/* open(): no file system in phase 1 */
static mp_obj_t py_open(size_t n_args, const mp_obj_t *args, mp_map_t *kw)
{
    (void)n_args, (void)args, (void)kw;
    mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("no files yet: Python on the calculator has no file system"));
}
MP_DEFINE_CONST_FUN_OBJ_KW(mp_builtin_open_obj, 1, py_open);

void gc_collect(void)
{
    gc_collect_start();
    gc_helper_collect_regs_and_stack();
    gc_collect_end();
}

void nlr_jump_fail(void *val)
{
    (void)val;
    py_api->con_printf("# python: fatal: uncaught exception\n");
    for (;;) {
    }
}

#ifndef NDEBUG
void __assert_func(const char *file, int line, const char *func, const char *expr);
void __assert_func(const char *file, int line, const char *func, const char *expr)
{
    (void)func;
    py_api->con_printf("# python: assertion failed: %s (%s:%d)\n", expr, file, line);
    for (;;) {
    }
}
#endif

const char py_help_text[] =
    "Python (MicroPython) on the calculator.\n"
    "  calc.solve(x**2-2, x)   the CAS from Python: from calc import x, y\n"
    "  calc.integrate(sin(x), x), calc.diff, calc.limit, calc.factor\n"
    "  calc.plot(x**2, -3, 3)  graph it;  math, cmath, random, time, re, json\n"
    "  help(calc) lists the calc library; help('modules') the modules.\n"
    "Keys: ENTER runs a line (blocks after ':' continue), F1 runs the block,\n"
    "ON stops a running program, CLEAR on an empty line leaves.\n";

/* ------------------------------------------------------------ running code */
bool py_running(void) { return P.running > 0; }
const char *py_last_exc(void) { return P.exc; }

/* Compile and run src in `kind`; *ret gets the result of the code object. */
static int run_kind(const char *src, size_t len, mp_parse_input_kind_t kind, mp_obj_t *ret)
{
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, src, len, 0);
        qstr source_name = lex->source_name;
        mp_parse_tree_t pt = mp_parse(lex, kind);
        mp_obj_t fun = mp_compile(&pt, source_name, kind == MP_PARSE_SINGLE_INPUT);
        mp_obj_t r = mp_call_function_0(fun);
        mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS);
        nlr_pop();
        if (ret)
            *ret = r;
        return PY_OK;
    }
    mp_obj_t exc = MP_OBJ_FROM_PTR(nlr.ret_val);
    const mp_obj_type_t *t = mp_obj_get_type(exc);
    if (mp_obj_is_subclass_fast(MP_OBJ_FROM_PTR(t), MP_OBJ_FROM_PTR(&mp_type_SystemExit)))
        return PY_OK; /* sys.exit(): the program ended */
    py_api->snprintf(P.exc, sizeof P.exc, "%s", qstr_str(t->name));
    P.errors++;
    if (P.tb_at == (size_t)-1)
        P.tb_at = P.caplen;
    mp_obj_print_exception(&mp_plat_print, exc);
    return mp_obj_is_subclass_fast(MP_OBJ_FROM_PTR(t), MP_OBJ_FROM_PTR(&mp_type_KeyboardInterrupt)) ? PY_INTR : PY_EXC;
}

/* Does src parse as an expression? (no output, nothing run) */
static bool parses_as_expr(const char *src, size_t len)
{
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, src, len, 0);
        mp_parse_tree_t pt = mp_parse(lex, MP_PARSE_EVAL_INPUT);
        mp_parse_tree_clear(&pt);
        nlr_pop();
        return true;
    }
    return false;
}

/* The start of the last top-level statement: the last line that starts in
 * column 0 with something other than a comment or a block's continuation. */
static size_t last_stmt(const char *s, size_t len)
{
    size_t best = 0, i = 0;
    while (i < len) {
        size_t e = i;
        while (e < len && s[e] != '\n')
            e++;
        char c = s[i];
        if (e > i && c != ' ' && c != '\t' && c != '#' && c != '\r' && strncmp(s + i, "else", 4) &&
            strncmp(s + i, "elif", 4) && strncmp(s + i, "except", 6) && strncmp(s + i, "finally", 7) &&
            c != ')' && c != ']' && c != '}')
            best = i;
        i = e + 1;
    }
    return best;
}

int py_run(const char *src, int mode, mp_obj_t *value)
{
    if (value)
        *value = MP_OBJ_NULL;
    P.running++;
    P.runs++;
    P.exc[0] = 0;
    P.tb_at = (size_t)-1;
    P.break_seen = py_pc->breaks(); /* presses before the run do not count */
    MP_STATE_MAIN_THREAD(mp_pending_exception) = MP_OBJ_NULL;
    size_t len = strlen(src);
    int r;
    if (mode == PY_RUN_REPL) {
        r = run_kind(src, len, MP_PARSE_SINGLE_INPUT, NULL);
    } else if (mode == PY_RUN_CELL) {
        size_t k = last_stmt(src, len), tl = len - k;
        while (tl && (src[k + tl - 1] == '\n' || src[k + tl - 1] == ' ' || src[k + tl - 1] == '\r'))
            tl--;
        if (tl && parses_as_expr(src + k, tl)) {
            r = k ? run_kind(src, k, MP_PARSE_FILE_INPUT, NULL) : PY_OK;
            if (r == PY_OK)
                r = run_kind(src + k, tl, MP_PARSE_EVAL_INPUT, value);
        } else {
            r = run_kind(src, len, MP_PARSE_FILE_INPUT, NULL);
        }
    } else {
        r = run_kind(src, len, MP_PARSE_FILE_INPUT, NULL);
    }
    P.running--;
    return r;
}

/* repr(v) into out */
void py_value_text(mp_obj_t v, char *out, size_t n)
{
    vstr_t vs;
    vstr_init(&vs, 64);
    mp_print_t pr;
    vstr_init_print(&vs, 64, &pr);
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_print_helper(&pr, v, PRINT_REPR);
        nlr_pop();
    }
    py_api->snprintf(out, n, "%.*s", (int)vs.len, vs.buf);
    vstr_clear(&vs);
}

void py_value_latex(mp_obj_t v, char *out, size_t n)
{
    out[0] = 0;
    if (py_is_expr(v))
        py_calc_latex(v, out, n);
}

/* ------------------------------------------------------------ console */
/* `py STATEMENT...`  REPL entry (words joined by spaces), output as out= lines
 * `py exec CODE`     a cell: out= lines, then value= (repr) and latex= (calc.Expr)
 * `pyexec CODE`      the same
 * `py add LINE` / `py run` / `py clear`   build a program line by line and run it (file)
 * `py buf TEXT` / `py cell`   append TEXT (no newline added) / run the buffer as a cell and
 *                    clear it: cells longer than a console line (the browser notebook)
 * `py expect CODE OUTPUT`   run a REPL entry; ERR unless its output (trimmed) is OUTPUT
 * `py reset`         a fresh interpreter (the heap is cleared)
 * `py breakafter N`  test hook: the ON key is pressed at the Nth poll of the next run
 * `py key|...`       the screen's own test commands (py_ui.c) */
static void restart(bool bare);

static int report(int r)
{
    py_out_flush();
    if (r == PY_EXC || r == PY_INTR)
        py_api->con_printf("error=%s\n", P.exc);
    return 0;
}

/* key=TEXT; a longer text as key+=PIECE lines first (a console line is printed
 * through a 256-byte buffer), like out+= */
static void con_long(const char *key, const char *t)
{
    size_t n = strlen(t);
    while (n > 160) {
        py_api->con_printf("%s+=%.160s\n", key, t);
        t += 160, n -= 160;
    }
    py_api->con_printf("%s=%s\n", key, t);
}

static int console_cell(const char *code)
{
    mp_obj_t v;
    int sinks = P.sinks;
    py_out_set(PY_OUT_CONSOLE);
    int r = py_run(code, PY_RUN_CELL, &v);
    if (r == PY_OK && v != MP_OBJ_NULL && v != mp_const_none) {
        static char t[1024];
        py_out_flush();
        py_value_text(v, t, sizeof t);
        con_long("value", t);
        py_value_latex(v, t, sizeof t);
        if (t[0])
            con_long("latex", t);
    }
    report(r);
    py_out_set(sinks);
    return 0;
}

/* a program (file mode): its output, error= if it raised */
static int console_file(const char *code)
{
    int sinks = P.sinks;
    py_out_set(PY_OUT_CONSOLE);
    report(py_run(code, PY_RUN_FILE, NULL));
    py_out_set(sinks);
    return 0;
}

__attribute__((noinline)) static int console_work(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "";
    if (!strcmp(argv[0], "nb")) /* the Notebook (nb_doc.c) */
        return nb_console(argc, argv);
    if (!strcmp(argv[0], "pyexec") || !strcmp(sub, "exec")) {
        int first = !strcmp(argv[0], "pyexec") ? 1 : 2;
        if (argc <= first)
            return py_api->con_error("usage: pyexec CODE");
        static char code[400];
        py_api->snprintf(code, sizeof code, "%s", argv[first]);
        return console_cell(code);
    }
    if (!strcmp(sub, "add") && argc > 2) {
        size_t k = strlen(argv[2]);
        if (P.srclen + k + 2 > sizeof P.src)
            return py_api->con_error("program too long (%u bytes)", (unsigned)sizeof P.src);
        memcpy(P.src + P.srclen, argv[2], k);
        P.srclen += k;
        P.src[P.srclen++] = '\n';
        P.src[P.srclen] = 0;
        return 0;
    }
    if (!strcmp(sub, "buf") && argc == 3) {
        size_t k = strlen(argv[2]);
        if (P.srclen + k + 1 > sizeof P.src)
            return py_api->con_error("cell too long (%u bytes)", (unsigned)sizeof P.src);
        memcpy(P.src + P.srclen, argv[2], k + 1);
        P.srclen += k;
        return 0;
    }
    if (!strcmp(sub, "cell") && argc == 2) {
        int r = console_cell(P.src);
        P.srclen = 0, P.src[0] = 0;
        return r;
    }
    if (!strcmp(sub, "clear") && argc == 2) {
        P.srclen = 0, P.src[0] = 0;
        return 0;
    }
    if (!strcmp(sub, "run") && argc == 2) {
        int r = console_file(P.src);
        P.srclen = 0, P.src[0] = 0;
        return r;
    }
    if (!strcmp(sub, "reset") && (argc == 2 || (argc == 3 && !strcmp(argv[2], "bare")))) {
        restart(argc == 3);
        return 0;
    }
    if (!strcmp(sub, "breakafter") && argc == 3) {
        long n;
        if (!py_api->con_parse_int(argv[2], &n) || n < 1)
            return py_api->con_error("usage: py breakafter N");
        P.break_after = (uint32_t)n;
        return 0;
    }
    if (!strcmp(sub, "expect") && argc == 4) {
        static char code[400];
        py_api->snprintf(code, sizeof code, "%s", argv[2]);
        int sinks = P.sinks;
        py_out_set(PY_OUT_CAPTURE);
        py_capture_reset();
        py_run(code, PY_RUN_REPL, NULL);
        py_out_set(sinks);
        size_t n;
        char *c = (char *)py_capture(&n);
        while (n && (c[n - 1] == '\n' || c[n - 1] == ' '))
            c[--n] = 0;
        static char want[400];
        py_api->snprintf(want, sizeof want, "%s", argv[3]);
        if (strcmp(c, want))
            return py_api->con_error("python: %s -> \"%s\", expected \"%s\"", code, c, want);
        return 0;
    }
    int r = pyui_console(argc, argv);
    if (r >= 0)
        return r;
    if (argc < 2)
        return py_api->con_error("usage: py STATEMENT | exec CODE | add LINE | run | expect CODE OUT | reset");
    /* a REPL entry: the words joined */
    static char line[400];
    size_t n = 0;
    line[0] = 0;
    for (int i = 1; i < argc && n < sizeof line - 1; i++) {
        int w = py_api->snprintf(line + n, sizeof line - n, "%s%s", i > 1 ? " " : "", argv[i]);
        n = w < 0 || n + (size_t)w >= sizeof line ? sizeof line - 1 : n + (size_t)w;
    }
    int sinks = P.sinks;
    py_out_set(PY_OUT_CONSOLE);
    r = py_run(line, PY_RUN_REPL, NULL);
    report(r);
    py_out_set(sinks);
    return 0;
}

static int op_console(void *rw, int argc, char **argv)
{
    (void)rw;
    char mark;
    bool outer = enter(&mark);
    int r = console_work(argc, argv);
    if (outer)
        P.stack_top = NULL;
    leave();
    return r;
}

/* ------------------------------------------------------------ ops */
/* Two screens share the module: the console and the Notebook (MODX_EV_ENTER arg bit 1); each keeps
 * its own entry line while the other is shown. */
__attribute__((noinline)) static int event_work(int ev, int arg)
{
    if (ev == MODX_EV_ENTER) {
        int m = arg & 2 ? 1 : 0;
        if (m != P.mode) {
            py_pc->line_get(P.line[P.mode], sizeof P.line[0]);
            py_pc->line_set(P.line[m]);
            P.mode = m;
        }
    }
    return P.mode ? nbui_event(ev, arg) : pyui_event(ev, arg);
}
bool nbui_active(void) { return P.mode == 1; }
static int op_event(void *rw, int ev, int arg)
{
    (void)rw;
    char mark;
    enter(&mark);
    int r = event_work(ev, arg);
    leave();
    return r;
}

__attribute__((noinline)) static bool draw_work(void) { return P.mode ? nbui_draw() : pyui_draw(); }
static bool op_draw(void *rw, int scr, int rect[4])
{
    (void)rw, (void)scr;
    char mark;
    enter(&mark);
    bool r = draw_work();
    leave();
    rect[0] = 0, rect[1] = 0, rect[2] = 0, rect[3] = 0;
    return r;
}

static void op_state(void *rw, void (*kv)(const char *, const char *, void *), void *user)
{
    (void)rw;
    char b[48];
    kv("py_exc", P.exc, user);
    py_api->snprintf(b, sizeof b, "%lu", (unsigned long)P.runs);
    kv("py_runs", b, user);
    py_api->snprintf(b, sizeof b, "%lu", (unsigned long)P.errors);
    kv("py_errors", b, user);
    if (P.started) {
        gc_info_t gi;
        gc_info(&gi);
        py_api->snprintf(b, sizeof b, "%lu/%lu", (unsigned long)gi.used, (unsigned long)gi.total);
        kv("py_heap", b, user);
    }
    pyui_state(kv, user);
    nb_state(kv, user);
}

/* ------------------------------------------------------------ init */
static void *heap;

/* The console's namespace: math's functions and constants, the calc library (its sin, sqrt, ... are
 * symbolic for an Expr and math's for numbers), the symbols x, y, z, t; the built-in pow stays. */
static const char prelude[] = "import math, cmath, calc\nfrom math import *\nfrom calc import *\ndel pow\n"
                              "x, y, z, t = syms('x y z t')\n";

__attribute__((noinline)) static void start(bool bare)
{
    gc_init(heap, (char *)heap + PY_RW_SIZE);
    mp_init();
    P.started = true;
    int sinks = P.sinks;
    py_out_set(0);
    if (!bare)
        py_run(prelude, PY_RUN_FILE, NULL);
    P.runs = P.errors = 0;
    py_out_set(sinks);
}

/* a fresh interpreter in the same heap (bare: an empty namespace, as a plain Python program has) */
static void restart(bool bare)
{
    mp_deinit();
    start(bare);
    pyui_init();
}
void py_restart(bool bare) { restart(bare); }
const char *py_srcbuf(void) { return P.src; }
void py_srcbuf_clear(void) { P.srclen = 0, P.src[0] = 0; }

int py_module_init(const modx_api_t *api, void *rw, modx_ops_t *ops)
{
    if (api->size < offsetof(modx_api_t, py_core) + sizeof api->py_core || !api->py_core)
        return -1;
    const py_core_t *pc = api->py_core();
    if (!pc || pc->abi != PY_CORE_ABI || pc->size < sizeof(py_core_t))
        return -2;
    memset(&P, 0, sizeof P);
    py_api = api, py_pc = pc;
    nb_core = pc->doc; /* the notebook document is the core's (app/nbdoc.h) */
    nb_docp = nb_core->doc();
    heap = rw;
    char mark;
    enter(&mark);
    start(false);
    pyui_init();
    nbui_init();
    P.stack_top = NULL;
    leave();
    ops->size = sizeof *ops;
    ops->event = op_event;
    ops->draw = op_draw;
    ops->console = op_console;
    ops->state = op_state;
    return 0;
}
