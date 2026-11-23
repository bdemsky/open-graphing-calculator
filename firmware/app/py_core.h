/*
 * Python as a code module: the interface between the core and the "python"
 * module (modules/python: MicroPython, the `calc` library, the console screen;
 * mechanism in app/modx.h, design in docs/QSPI_CONTENT.md section 12.8).
 *
 * The module is the first of kind code-load: its image (code, constants and
 * initialised data) is copied from the QSPI into an executable SDRAM area
 * and relocated when it binds, so MicroPython keeps its ordinary static data
 * and its tables of pointers (app/modx.c, modx_load). The core keeps what the
 * rest of the calculator refers to by address (app/py_core.c): the screen
 * scr_python with its entry line (a core editor, so the CAS / MATH menus, the
 * catalog and the clipboard insert into it), the soft-key bar, the APPS
 * entry, the console commands `py` / `pyexec`, the break key and the bridge
 * to the CAS and the graph.
 *
 *   py_core_t   core -> module: the services the module calls, reached
 *               through modx_api_t.py_core (API size 11).
 *   modx_ops_t  module -> core: the usual ops (event, draw, console, state).
 *               Events: MODX_EV_ENTER, MODX_EV_KEY (-> 1 when the module used
 *               the key; 0 lets the core's editor take it), MODX_EV_TOUCH (-> 2
 *               when the output scrolled: the core redraws).
 *
 * Versioning: PY_CORE_ABI covers the table and the constants below. A module
 * refuses to start on another PY_CORE_ABI; new entries are appended (size
 * grows) without a bump.
 */
#ifndef PY_CORE_H
#define PY_CORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cas.h"

#define PY_MODULE "python"
#define PY_CORE_ABI 1

/* what to show after the module call that asked for it (py_core_t.show) */
enum { PY_SHOW_NONE = 0, PY_SHOW_GRAPH, PY_SHOW_HOME };

typedef struct py_core {
    uint16_t abi, size; /* PY_CORE_ABI, sizeof(py_core_t) */
    /* The CAS. eval: cas_eval_quiet (ans is kept) with the C stack the caller has
     * already used (`used` bytes) taken off the CAS's stack limit, so a CAS call
     * made from deep Python code stays inside the main stack. (LaTeX is made by
     * the module from the linear form: cas_latex is not linked into the firmware.) */
    cas_status (*eval)(const char *in, char *out, size_t n, size_t used);
    const char *(*error)(void);           /* message of the last CAS error */
    /* system: poll = what a long CAS evaluation does (watchdog, USB);
     * breaks = ON presses so far (the break key; counted in the key interrupt);
     * brk = the same as pressing ON (tests); delay = wait, polling */
    void (*poll)(void);
    uint32_t (*breaks)(void);
    void (*brk)(void);
    void (*delay_ms)(uint32_t ms);
    uint32_t (*stack_limit)(void);        /* bytes of C stack Python may use (Kconfig PYTHON_STACK_KB) */
    /* the screen's entry line (a core editor): text as displayed, replace, draw at (x, y) */
    void (*line_get)(char *out, size_t n);
    void (*line_set)(const char *text);
    void (*line_insert)(const char *text); /* at the cursor */
    void (*line_draw)(int x, int y, int cols, bool focus);
    int (*line_height)(void);
    /* graph: put `expr` (CAS text in x) into a Y= function of FUNC mode, x range
     * [xmin, xmax], y fitted; the graph is shown after the module call returns.
     * 0, or -1 with a message in err. */
    int (*plot)(const char *expr, double xmin, double xmax, char *err, size_t n);
    void (*show)(int what);               /* PY_SHOW_*: switch screens after the module call */
    bool (*shown)(void);                  /* the Python screen (or the Notebook screen) is the current screen */
    /* ---- appended (size 2): the Notebook app (modules/python/nb_*.c, help page "notebook").
     * One entry for everything the notebook needs from the core: op NBIO_*, see below. ---- */
    int (*nb_io)(int op, uint32_t a, void *p, size_t n);
    /* ---- appended (size 3): the notebook document is the core's (app/nbdoc.h): the home screen's
     * notebook; the module shows and edits it, saves it, runs its Python cells ---- */
    const struct nbdoc_api *doc;
} py_core_t;

/* nb_io operations (a, p, n as noted; the result is 0 / -1 unless noted) */
enum {
    NBIO_STORE_SIZE = 0, /* -> bytes of the notebook region of the QSPI (NB_STORE_*), 0: none */
    NBIO_READ,           /* a = region offset, p, n */
    NBIO_ERASE,          /* a = region offset, n: whole sectors */
    NBIO_WRITE,          /* a = region offset, p, n (NOR: only clears bits) */
    NBIO_SCREEN,         /* -> 1 when the Notebook screen is current, 2 the Python console, 0 another */
    NBIO_SOFTKEY,        /* a = 0..4 (F1..F5), 5..9 (SHIFT F1..F5); p = label (NULL: none) */
    NBIO_EDITING,        /* a = 1: the entry line edits a cell (menus insert there), 0: not */
    NBIO_TO_CAS,         /* p = nbio_text_t: display text (as typed) -> CAS text */
    NBIO_PRETTY,         /* p = nbio_text_t: CAS text -> display text */
    NBIO_MATH,           /* p = nbio_math_t: textbook layout: measure (and draw); -1: not as 2D */
    NBIO_USB_HOST,       /* -> 1 when a computer has the CALC drive */
    NBIO_USB_READ,       /* a = offset, p, n: the .ipynb being imported -> bytes, <0 */
    NBIO_USB_FILE,       /* p = nbio_usb_file_t: one notebook of the drive's NOTEBOOKS folder */
    NBIO_USB_RESULT,     /* a = 1 imported / 0 not; p = "toast\ndetail" (the toast and RESULT.TXT) */
    NBIO_USB_REFRESH,    /* the store changed: the drive is generated again when the app is left */
    NBIO_SD_CREATE,      /* p = path: a card file for writing (then api file_write, file_close) */
    NBIO_SD_REMOVE,      /* p = path */
    NBIO_SD_LIST,        /* p = nbio_list_t: a card directory with long names -> count, <0 FS_E* */
    NBIO_LINE_CURSOR,    /* a = (uint32_t)-1: -> the entry line's cursor (code points); else set it to a */
    NBIO_REFS,           /* p = nbio_text_t: CAS text -> with Ans and Out[n] replaced (nbd_refs); 1: no
                          * such result, the message in out */
    NBIO_VALUE_CAS,      /* a = cell index, p = nbio_text_t (out, n): its value as CAS text; 1: none / too long */
    NBIO_FW_VERSION,     /* p, n: the firmware's version string (core/version.h); an older core answers -1 */
};
/* QSPI region of the notebook store (offsets from 0x90000000; modules below it, the session log
 * above: core/modstore.h, app/session.h; docs/QSPI_CONTENT.md 12.9) */
#define NB_STORE_BASE 0x00A00000u
#define NB_STORE_SIZE 0x00400000u
/* a stored notebook is a chain of 4 KB sectors, each a 64-byte header and 4032 bytes of the .ipynb */
#define NB_SECTOR 4096u
#define NB_SECTOR_HDR 64u
#define NB_SECTOR_DATA (NB_SECTOR - NB_SECTOR_HDR)
typedef struct {
    const char *in;
    char *out;
    size_t n;
} nbio_text_t;
typedef struct {
    const char *disp;  /* display text (pretty_from_cas) */
    int x, y, maxw;    /* top-left, the width it may take */
    uint16_t fg;
    bool draw;         /* false: measure only */
    int w, h;          /* out: the box */
} nbio_math_t;
typedef struct {
    const char *name;  /* "Plan.ipynb" (<= 47 characters) */
    uint32_t size;     /* bytes of the .ipynb */
    const uint32_t *off; /* region offset of each sector, in order (n = ceil(size / NB_SECTOR_DATA)) */
} nbio_usb_file_t;
typedef struct {
    const char *dir;
    int (*cb)(const char *name, uint32_t size, void *user); /* nonzero stops */
    void *user;
} nbio_list_t;

const py_core_t *py_core_table(void);
void app_open_notebook(void);
bool py_available(void); /* the Python module is there (bound now if it was not) */ /* APPS > Notebook: the module's view of the notebook (or why there is none) */ /* modx_api_t.py_core (NULL: no Python in this firmware) */
int py_console(int argc, char **argv);
void py_state(void (*kv)(const char *, const char *, void *), void *user);
void py_break_isr(void); /* the ON key (app.c's hook, from the key interrupt) */
/* A notebook command of the module (`nb CMD ARG` through its console op) from the core's main
 * loop: its result, -1 without the module, -2 while module code runs (try again later). */
int py_nb_call(const char *cmd, const char *arg);
bool py_nb_shown(void); /* the Notebook screen is current */
bool py_nb_following(void); /* the browser page follows the notebook (nb sync on) */
void py_tick(uint32_t now); /* main loop (app_tick): the Notebook's idle save, its browser page's locks */
void py_nb_flush(void);     /* the open notebook's unsaved changes saved now (power-off, low battery) */
/* app/usb_app.c: nb_io's USB operations (NBIO_USB_*) */
int usb_nb_io(int op, uint32_t a, void *p, size_t n);

#endif
