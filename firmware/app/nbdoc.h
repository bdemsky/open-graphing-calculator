/*
 * The notebook document (core): the one document model of the calculator.
 *
 * The home screen IS a notebook: every calculation is a math cell (input +
 * result), graphing started there adds a graph cell with its own functions,
 * window and style, and the notebook menu (MENU on the home screen) adds
 * text and Python cells, saves the notebook under a name, opens another one,
 * starts a new one. The same document is what the browser page follows and
 * edits (named or not), what is written as .ipynb, what NOTEBOOK.HTML shows;
 * the Python module (modules/python/nb_*.c) adds the list of notebooks and the
 * editor of a cell of several lines. Docs: docs/NOTEBOOK.md.
 *
 * Ownership. The document `nbd` (one nb_t, about 18 KB) and the arena its
 * strings live in (NB_ARENA) are core memory in the SDRAM (carved by
 * app_init through nbdoc_mem_init), so the home screen keeps working without
 * the Python module (core tier, module not installed). The module reaches
 * them through py_core_t.doc (nbdoc_api_t below): the same struct, the
 * allocator, and the core's graph-cell renderer.
 *
 *   unsaved ("scratch", !open): the home screen's calculations; kept in the
 *        session (blob "home", app/home.c) like the history always was;
 *   named (open): a notebook of the notebook store (or the card), saved by
 *        the module (.ipynb) at power-off, when left, after 2 minutes idle,
 *        on low battery; the session keeps a copy too, so the home screen
 *        shows it right after a reboot.
 *
 * Cells: kind NBK_*, text fields NBF_* (arena strings, NULL = empty).
 *   math   src = input as typed (display text), val = the result (display
 *          text) or ename "CASError" + evalue = the "ERR:..." message
 *   graph  src = "f(x), xmin, xmax" (the first function, for older readers
 *          and the browser page), plot = its CAS text; graph = the spec of
 *          the cell (app/gcell.c): functions, window, style, height
 *   text   src = Markdown
 *   python src = code, outputs from the module (out, val, tex, ename, ...)
 */
#ifndef NBDOC_H
#define NBDOC_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "touch.h"

enum { NBK_TEXT = 0, NBK_MATH, NBK_PY, NBK_GRAPH, NBK_N };
/* text fields of a cell (arena strings, NULL: empty). Outputs are [NBF_OUT, NBF_NOUT). */
enum { NBF_SRC = 0, NBF_OUT, NBF_VAL, NBF_TEX, NBF_ENAME, NBF_EVALUE, NBF_TB, NBF_PLOT, NBF_GRAPH, NBF_N };
#define NBF_NOUT NBF_GRAPH
enum { NBC_RAN = 1, NBC_EXPAND = 2, NBC_IMAGE = 4 /* an image output that was not ours (not kept) */,
       NBC_RAW = 8 /* an imported raw cell */ };
#define NB_MAX_CELLS 200
#define NB_ARENA (256u << 10)
#define NB_ARENA_SMALL (8u << 10) /* (the fallback arena after an SDRAM failure) */
#define NB_NAME 36 /* bytes of a notebook name with its NUL (35 characters) */
typedef struct {
    uint8_t kind, flags;
    int16_t count;           /* execution count (0: none) */
    char id[24];
    char *f[NBF_N];
    double xmin, xmax;       /* graph range (graph cells; a Python cell's plot) */
    int16_t h;               /* the module's layout height cache (-1: measure) */
    int16_t hh[2];           /* the home screen's block heights (input, output; -1: measure) */
} nb_cell_t;
typedef struct {
    char name[NB_NAME];
    int where;               /* 0 the QSPI store, 1 the microSD card */
    int ncells;
    nb_cell_t cell[NB_MAX_CELLS];
    int counter;             /* execution counter */
    bool open, dirty;        /* open: a named notebook (else the unsaved scratch notebook) */
    char note[96];           /* what the last import or load dropped ("" none) */
    uint32_t gen;            /* bumped on every change the home screen must redraw for */
    char msg[120];           /* why the module's last notebook command for the core failed ("" ok) */
} nb_t;

extern nb_t *nbd; /* the document (core) */

/* What the Python module gets (py_core_t.doc): the document, its arena and
 * the graph cells the core draws. */
typedef struct nbdoc_api {
    nb_t *(*doc)(void);
    void *(*alloc)(size_t n);  /* arena memory (NULL: full) */
    void (*free)(void *p);
    size_t (*used)(void);      /* arena bytes in use */
    void (*reset)(void);       /* every cell and string gone (the name etc. kept for the caller to set) */
    /* graph cell i drawn at (x, y), w x h on the screen (clipped to the body) */
    void (*graph_draw)(int i, int x, int y, int w, int h);
    /* graph cell i rendered offscreen, w x h (<= 320 x 240) RGB565 with a row stride of 320 pixels;
     * valid until the next graph cell is drawn; NULL: none */
    const uint16_t *(*graph_image)(int i, int w, int h);
    int (*graph_height)(int i); /* its height on the home screen */
    /* the module changed the document (a load, a run): the home screen redraws */
    void (*touched)(void);
    /* the document as it is now kept aside (a copy of it and its strings) and put back: the
     * module reads another notebook in between (a copy to the card, an import); one level */
    void (*stash)(void);
    void (*unstash)(void);
    /* graph cell i's spec set (the browser page: `nb spec`), normalised; NULL: a graph cell without
     * one gets one from its text. false: not a graph cell (or nothing to do) */
    bool (*graph_set)(int i, const char *spec);
} nbdoc_api_t;
extern const nbdoc_api_t nbdoc_api;

/* memory (app_init): the document, its arena and the graph cells' images; -> bytes used */
size_t nbdoc_mem_init(void *mem, bool big);
size_t gcell_mem_init(void *mem, bool big); /* (nbdoc_mem_init) */
void *nbd_alloc(size_t n);
void nbd_free(void *p);
size_t nbd_used(void);
void nbd_reset(void);            /* empty (cells, arena); name/open/where untouched */
void nbd_scratch(void);          /* a new unsaved notebook: empty, no name */
void nbd_stash(void);            /* (nbdoc_api_t.stash / unstash) */
void nbd_unstash(void);
char *nbd_strdup(const char *s, size_t n);
void nbd_set(nb_cell_t *c, int f, const char *s); /* field = copy of s (NULL / "": empty) */
const char *nbd_get(const nb_cell_t *c, int f);    /* "" when empty */
nb_cell_t *nbd_insert(int at, int kind);          /* a new empty cell at `at` (NULL: full) */
void nbd_delete(int i);
void nbd_clear_out(nb_cell_t *c);
void nbd_changed(void);          /* the document changed: dirty (named), home redraw, the module's events */
bool nbd_special(void);          /* any cell other than math (the home screen uses its 2D view) */
int nbd_find(const char *id);    /* cell index by id, -1 */

/* References to earlier results (help "home-notebook"; docs/NOTEBOOK.md "Ans and Out[n]"):
 * `Ans` is the latest result of the notebook (the cell of the highest execution count with a value:
 * a math cell's result or a Python cell's last expression; errors and cells without a value do not
 * count), `Out[n]` the result of the cell of execution count n (Jupyter numbering, the [n] shown
 * next to cells; `out[n]` too). */
int nbd_result(int count);       /* the cell of that count with a value (count <= 0: the latest), -1 */
const char *nbd_value(int i);    /* its value text ("" none) */
/* the CAS's ans holds cell c's result (home.c, after a calculation): Ans and Out[n] of that cell
 * then use ans itself (no digits lost) for as long as the CAS's ans is not replaced */
void nbd_ans_mark(const nb_cell_t *c);
/* display text (as typed, or a stored result) -> CAS text; false: too long for the editor */
bool nbd_to_cas(const char *disp, char *out, size_t n);
/* cell i's value as CAS text (parenthesized); false: no value / too long (msg says why) */
bool nbd_value_cas(int i, char *out, size_t n, char *msg, size_t mn);
/* CAS text `in` with Ans and Out[n] replaced by the results they name (Ans with no result yet is
 * left to the CAS); false: a reference names no result, msg says so ("Out[7]: no such result") */
bool nbd_refs(const char *in, char *out, size_t n, char *msg, size_t mn);

/* graph cells (app/gcell.c) */
nb_cell_t *gcell_new(int at);
nb_cell_t *gcell_new3d(int at);  /* a 3D graph cell of the 3D graph's current state */
bool gcell_is3d(int i);    /* a graph cell of the current Y= functions and window */
void gcell_draw(int i, int x, int y, int w, int h, bool sel, bool active);
void gcell_draw_api(int i, int x, int y, int w, int h);
int gcell_height(int i);
const uint16_t *gcell_image(int i, int w, int h); /* (nbdoc_api_t.graph_image) */
bool gcell_empty(int i);         /* no function to plot (drawn as one line, no plot) */
bool gcell_set_spec(int i, const char *spec); /* (nbdoc_api_t.graph_set) */
void gcell_plot_size(int i, int *w, int *h);  /* its plot's size on the home screen */
/* function k of cell i for its caption ("y1=x²"), its colour, on; false: no more */
bool gcell_fn(int i, int k, char *out, size_t n, uint16_t *color, bool *on);
/* the selected graph cell, interactive in place */
bool gcell_key(int i, int key, int mod); /* true: used (redraw) */
void gcell_pan(int i, double fx, double fy);
void gcell_zoom(int i, double s, double cx, double cy); /* by s about the point (cx, cy) in [0,1]^2 of the plot */
void gcell_resize(int i, int h);
/* bind cell i to the graph app: its functions and window become the Y= state; gcell_unbind (home)
 * writes them back */
void gcell_bind(int i);
void gcell_unbind(void);
const char *gcell_bound(void);   /* the bound cell's id ("" none) */
void gcell_set_bound(const char *id);
void gcell_invalidate(void);     /* settings / theme changed: images drawn again */
/* touch on graph cell i drawn at (x, y, w x h): pan (drag), pinch zoom, resize (bottom edge) */
bool gcell_touch(int i, const touch_event_t *ev, int x, int y, int w, int h);
uint16_t *gcell_scratch(void);   /* the offscreen frame (LCD_WIDTH x LCD_HEIGHT; NULL: none) */
const uint16_t *gcell_cached(int i, int w, int h); /* cell i's image if cached (in the scratch frame) */
void gcell_functions(int i, char *out, size_t n);  /* "y1=x^2  y2=sin(x)" */
/* NOTEBOOK.HTML (app/nbhtml.c): the open notebook as a static page */
size_t nbhtml_mem_init(void *mem, bool big);
uint32_t nbhtml_build(void);     /* -> its size */
void nbhtml_read(void *user, uint32_t off, uint8_t *buf, uint32_t n);
void gcell_state(int i, void (*kv)(const char *, const char *, void *), void *user);
/* animated graph cells (functions of t; the spec's "a" line): the one the home screen plays or steps */
bool gcell_animated(int i);
bool gcell_anim_cfg(int i, double an[4]);  /* an animated cell's t from, to, seconds, mode; false: not one */
int gcell_anim_cell(void);                 /* -1: none */
void gcell_anim_take(int i, double an[4]); /* cell i is the one now; its t from, to, seconds, mode */
double gcell_anim_t(void);
void gcell_anim_set_t(double t);
void gcell_anim_step(int i, int dir);      /* t one step (1/50 of its range) forwards (1) or back (-1) */
#define GCELL_H 120              /* default height (px) */
#define GCELL_HMIN 48
#define GCELL_HEMPTY 20          /* a graph cell without a function: one line */
#define GCELL_EDGE 10            /* px at the bottom edge that resize */

#endif
