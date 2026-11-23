/*
 * The notebook's module side (help pages "notebook", "browser"; docs/NOTEBOOK.md):
 * saving, opening, .ipynb, Python cells, the browser page's protocol, the
 * list of notebooks and the cell editor (the notebook itself is shown on the
 * home screen, app/home.c); what the modules/python/nb_*.c files share. It lives in the Python module
 * (one interpreter for the console and the open notebook) and reaches the
 * core through py_core_t.nb_io (app/py_core.h).
 *
 *   nb_json.c   JSON writer (Jupyter's layout) and a streaming parser (events
 *               with the path of keys, strings in pieces: any size of input in
 *               bounded memory)
 *   (the PNG writer and base64: app/png.h, the same source as the core's)
 *   nb_store.c  the notebook store in the QSPI (a log of 4 KB sectors with
 *               CRCs: power-loss safe, wear-levelled, the previous version of
 *               each notebook kept while there is room)
 *   nb_doc.c    the notebook in memory, .ipynb (nbformat 4.5) reading and
 *               writing, running cells, saving (QSPI store or microSD card),
 *               the console `nb`, the USB drive's NOTEBOOKS folder and imports
 *   nb_plot.c   graph cells: a function plotted into an indexed image
 *   nb_ui.c     the screens: the list of notebooks and the editor of a cell
 *   nb_sync.c   the browser page as a second view of the open notebook: events
 *               ("# nb1 REV ..." lines), the page's commands, cell locks, and
 *               when the notebook is saved (power-off, leaving, idle)
 *
 * The JSON, PNG and store code is plain C without the module's API, so the
 * host unit tests (tests/unit/test_notebook.c) call it directly.
 */
#ifndef NB_H
#define NB_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "png.h"
#include "rle.h" /* crc32_update (core/rle.c, compiled into the module's image too) */

typedef png_sink_fn nb_sink_fn;
double nb_strtod(const char *s, const char **end); /* (nb_json.c) */

/* ---------------------------------------------------------------- JSON (nb_json.c) */
/* Writer: Jupyter's layout (one space of indent per level, keys in the order
 * written: the caller writes them sorted). Strings are written as valid UTF-8
 * (bad bytes become U+FFFD) with the escapes JSON needs. */
#define JW_DEPTH 24
typedef struct {
    nb_sink_fn put;
    void *u;
    int depth;
    uint32_t first;   /* bit d: nothing written yet at depth d */
    bool after_key;   /* a key was written: its value follows on the same line */
    size_t len;       /* bytes written */
    uint8_t ub[4];    /* a UTF-8 sequence cut between pieces (jw_str_piece) */
    int un;
} jw_t;
void jw_init(jw_t *w, nb_sink_fn put, void *u);
void jw_obj(jw_t *w);
void jw_obj_end(jw_t *w);
void jw_arr(jw_t *w);
void jw_arr_end(jw_t *w);
void jw_key(jw_t *w, const char *k);
void jw_str(jw_t *w, const char *s); /* NULL: "" */
void jw_str_begin(jw_t *w);
void jw_str_piece(jw_t *w, const char *s, size_t n);
void jw_str_end(jw_t *w);
void jw_int(jw_t *w, long v);
void jw_num(jw_t *w, double v);
void jw_null(jw_t *w);
void jw_bool(jw_t *w, bool v);
void jw_lines(jw_t *w, const char *s); /* text as an array of lines, each but the last with its "\n" */
void jw_raw(jw_t *w, const char *s);   /* a value written as is (a number's text) */

/* Parser: pull bytes through read(), call ev() for each event. The path of
 * the current value: depth containers are open; key[d] is the member name
 * (objects) and idx[d] the element index (arrays) at level d < depth. For
 * JE_OBJ / JE_ARR the event comes before the container opens and its _END
 * after it closed, both at the same depth and key. Strings come as JE_STR,
 * then JE_STR_PIECE (decoded UTF-8, up to 256 bytes each), then JE_STR_END;
 * member names as one JE_KEY (cut at 47 bytes). */
enum { JE_OBJ, JE_OBJ_END, JE_ARR, JE_ARR_END, JE_KEY, JE_STR, JE_STR_PIECE, JE_STR_END, JE_NUM, JE_TRUE, JE_FALSE,
       JE_NULL };
#define JP_DEPTH 24
typedef struct jp jp_t;
struct jp {
    int (*read)(void *u, uint8_t *buf, size_t n); /* bytes, 0 at the end, <0 an error */
    void *ru;
    void (*ev)(jp_t *p, int ev, const char *s, size_t n);
    void *u;
    uint32_t max;            /* bytes accepted (0: any) */
    /* state */
    uint8_t buf[512];
    size_t pos, len;
    bool eof;
    uint32_t off, line;      /* bytes consumed, line (1-based) */
    int depth;
    char key[JP_DEPTH][48];
    int32_t idx[JP_DEPTH];
    bool stop;               /* set by ev(): end the parse (the error is in err) */
    char err[96];
};
int jp_parse(jp_t *p); /* 0, or -1 with the reason in p->err */
/* the current path is key[0]/key[1]/... ("*" matches any member or element) */
bool jp_path(const jp_t *p, int n, const char *const *keys);

/* ---------------------------------------------------------------- the store (nb_store.c) */
#define NBS_SECTOR 4096u
#define NBS_HDR 64u
#define NBS_DATA (NBS_SECTOR - NBS_HDR)
#define NBS_MAX_SECT 1024
#define NBS_MAX_VER 192
#define NBS_NAME 36       /* bytes of a name with its NUL (35 characters) */
#define NBS_MAX_BYTES (256u << 10) /* a notebook's .ipynb */
enum { NBS_OK = 0, NBS_E_IO = -1, NBS_E_FULL = -2, NBS_E_NOTFOUND = -3, NBS_E_CRC = -4, NBS_E_SIZE = -5,
       NBS_E_NODEV = -6, NBS_E_NAME = -7 };
typedef struct {
    int (*read)(void *u, uint32_t off, void *buf, size_t n);
    int (*erase)(void *u, uint32_t off, size_t n);
    int (*write)(void *u, uint32_t off, const void *buf, size_t n);
    void *u;
    uint32_t size; /* bytes (a multiple of NBS_SECTOR) */
} nbs_dev_t;
typedef struct {
    uint32_t seq;   /* store-wide, increasing */
    uint32_t size;  /* bytes */
    uint16_t nchunks;
    int16_t last;   /* index of the chunk marked last (-1: none seen) */
    bool live;      /* complete and not reclaimed */
    bool pinned;    /* listed on the USB drive: its sectors are reused last */
    char name[NBS_NAME];
} nbs_ver_t;
typedef struct {
    const nbs_dev_t *dev;
    uint32_t nsec;
    int16_t owner[NBS_MAX_SECT];  /* version index, -1 free, -2 not ours (keep out) */
    uint16_t cidx[NBS_MAX_SECT];  /* chunk index */
    nbs_ver_t ver[NBS_MAX_VER];
    int nver;
    uint32_t max_seq, cursor;
    bool mounted;
    uint32_t writes, erases;      /* statistics */
    long fail_after;              /* test hook: device writes that succeed before all fail (-1: off) */
    int keep;                     /* a version being copied (not reclaimed meanwhile; -1 none) */
} nbs_t;
typedef struct {
    char name[NBS_NAME];
    uint32_t size, seq;
    int versions;   /* stored versions (1: no previous one) */
} nbs_entry_t;
int nbs_mount(nbs_t *s, const nbs_dev_t *dev);
int nbs_list(nbs_t *s, nbs_entry_t *out, int max);       /* newest version of each name, by name */
int nbs_find(nbs_t *s, const char *name, int which);      /* version index: 0 the current, 1 the previous; <0 */
uint32_t nbs_free(nbs_t *s);                               /* bytes for a new notebook (older versions count) */
bool nbs_name_ok(const char *name);
/* reading a version as a stream (each sector's CRC checked) */
typedef struct {
    nbs_t *s;
    int v;
    uint32_t pos;
    int err;
} nbs_rd_t;
int nbs_open(nbs_t *s, const char *name, int which, nbs_rd_t *r);
int nbs_read(void *rd, uint8_t *buf, size_t n);            /* bytes, 0 at the end, <0 (a jp_t reader) */
/* writing a new version of `name` of exactly `size` bytes; the sectors are reserved first */
typedef struct {
    nbs_t *s;
    char name[NBS_NAME];
    uint32_t size, pos, seq;
    int nsec, k;
    uint16_t sec[NBS_MAX_BYTES / NBS_DATA + 2];
    uint8_t buf[NBS_DATA];
    size_t blen;
    int err;
} nbs_wr_t;
int nbs_create(nbs_t *s, const char *name, uint32_t size, nbs_wr_t *w);
void nbs_put(void *wr, const void *p, size_t n);           /* (an nb_sink_fn) */
int nbs_close(nbs_wr_t *w);                                /* commits (the last sector's header) */
int nbs_delete(nbs_t *s, const char *name);
bool nbs_name_same(const char *a, const char *b); /* (without regard to case) */
int nbs_copy(nbs_t *s, const char *from, int which, const char *to, nbs_wr_t *w); /* rename, revert */
/* the sector offsets of the current version (USB drive); *size its bytes; -> sectors, <0 */
int nbs_sectors(nbs_t *s, const char *name, uint32_t *off, int max, uint32_t *size);
void nbs_unpin(nbs_t *s);

/* ---------------------------------------------------------------- the notebook (nb_doc.c) */
/* The document is the core's (app/nbdoc.h): the home screen's notebook. The module reaches it, its
 * arena and the core's graph cells through py_core_t.doc (nb_core, set when the module starts). */
#include "nbdoc.h"
extern const nbdoc_api_t *nb_core;
void nb_graph_text(int i); /* a graph cell's text set: its spec made from it (one function, "f(x), a, b") */
extern nb_t *nb_docp;
#define NB (*nb_docp)

/* arena strings (nb_doc.c) */
char *nb_strdup(const char *s, size_t n);
void nb_set(nb_cell_t *c, int f, const char *s);  /* field = copy of s (NULL / "": empty) */
const char *nb_get(const nb_cell_t *c, int f);     /* "" when empty */

/* the document */
int nb_store_mount(void);                 /* the QSPI store (once) */
nbs_t *nb_store(void);
void nb_new_doc(const char *name, int where);
void nb_close_doc(void);                  /* the notebook closed: an empty unsaved one (the home screen's) */
int nb_keep_scratch(void);                /* the home screen's unsaved cells saved under a new name before another
                                           * notebook replaces them (0, or -1 not saved) */
int nb_load(const char *name, int where, char *msg, size_t n); /* 0 or <0 with the reason */
int nb_save(char *msg, size_t n);         /* the open notebook to where it came from */
int nb_write_json(nb_sink_fn put, void *u); /* the open notebook as .ipynb -> bytes */
nb_cell_t *nb_insert(int at, int kind);   /* a new empty cell at index `at` (NULL: full) */
void nb_delete_cell(int i);
void nb_move(int i, int dir);
void nb_clear_out(nb_cell_t *c);
const char *nb_kind_name(int kind);       /* "text", "math", "python", "graph" */
int nb_kind_of(const char *s);            /* -1: unknown */
/* running: one cell (0, or 1 an error / interrupt that stops a run-all) */
int nb_run(int i);
int nb_run_all(void);
bool nb_running(void);
void nb_restart_python(void);
/* graph text: "sin(x)" or "sin(x), -5, 5" -> the expression (display text) and the range */
void nb_graph_parse(const char *src, char *expr, size_t n, double *xmin, double *xmax);
void nb_to_cas(const char *disp, char *out, size_t n); /* display text (as typed) -> CAS text */
int nb_png_cell(int i);                   /* `nb png ID`: graph cell i's picture, base64 PNG lines */
/* the list of notebooks: the store's, then the card's (where 1) */
typedef struct {
    char name[NBS_NAME];
    uint32_t size;
    uint8_t where, versions;
} nb_item_t;
int nb_list(nb_item_t *out, int max);
/* Example notebooks built into the module (nb_examples.c, generated by tools/gen_nb_examples.py
 * from firmware/notebooks/): listed with where NB_EXAMPLE after the user's notebooks, unless the
 * store has a notebook of that name (an example once changed and saved). nb_load() reads one as a
 * notebook of the store that is not written yet: it is saved there at the first change. */
#define NB_EXAMPLE 2
typedef struct {
    const char *name, *json;
    uint32_t len;
} nb_example_t;
extern const nb_example_t nb_examples[];
extern const int nb_nexamples;
int nb_list_all(nb_item_t *out, int max);  /* nb_list(), then the examples not in the store */
int nb_remove(const char *name, int where, char *msg, size_t n);
int nb_rename(const char *from, int where, const char *to, char *msg, size_t n);
int nb_revert(const char *name, char *msg, size_t n);
int nb_copy_to(const char *name, int where, int to, char *msg, size_t n); /* store <-> card */
void nb_default_name(char *out, size_t n);
int nb_console(int argc, char **argv);    /* `nb ...` (-> -1: not a notebook command) */
int nb_find_cell(const char *s);          /* a cell by its id, else by its number (1-based); -1 */
int nb_running_cell(void);                /* the cell running (-1 none) */
int nb_read_doc(int (*rd)(void *, uint8_t *, size_t), void *ru, const char *name, int where, uint32_t max, char *msg,
                size_t n);
void nb_state(void (*kv)(const char *, const char *, void *), void *user);
/* a calc.plot() while a cell runs: kept as the cell's plot (true), else the graph screen (false) */
bool nb_plot_hook(const char *expr, double xmin, double xmax);

/* plot (nb_plot.c): the function plotted into pix (w x h, NB_PAL colours) */
enum { NBP_BG = 0, NBP_GRID, NBP_AXIS, NBP_CURVE, NBP_TEXT, NBP_N };
extern const uint16_t nb_plot_pal[NBP_N];
#define NB_PLOT_W 288
#define NB_PLOT_H 132
int nb_plot(const char *cas, double xmin, double xmax, uint8_t *pix, int w, int h, char *err, size_t n);
/* the same, NB_PLOT_W x NB_PLOT_H, from a cache of the last two (nb_doc.c): NULL with err on a failure */
const uint8_t *nb_plot_get(const char *cas, double xmin, double xmax, char *err, size_t n);

/* the screens (nb_ui.c) */
void nbui_init(void);
int nbui_event(int ev, int arg);
bool nbui_draw(void);
void nbui_progress(void);
void nbui_state(void (*kv)(const char *, const char *, void *), void *user);
bool nbui_active(void);                   /* the Notebook screen is shown (else the console) */
void nbui_changed(void);                  /* the document changed under the screen (console commands) */
void nbui_list(void);                     /* the list of notebooks next (the open one stays open) */
int nbui_edit(int i);                     /* cell i in the editor (ENTER on the home screen): 0, or -1 */
void nbui_mark(void);                     /* before a console change: the chosen / edited cell kept by id */
void nbui_commit(void);                   /* the cell being edited on the calculator: committed, editing ends */
uint32_t nbui_draft(void (*put)(const char *, size_t)); /* the cell being edited as it is now (put NULL: a hash) */

/* the browser page (nb_sync.c). Every change of the open notebook is noted: the
 * revision goes up and, while a page follows (`nb sync on`), an event line goes out. */
enum { NBN_DOC,   /* another notebook (opened, new, closed, imported, reverted): the page reads it all */
       NBN_CELL,  /* cell i changed (source, kind, outputs): dirty */
       NBN_ORDER, /* cells added, deleted or moved: dirty */
       NBN_ALL,   /* every cell changed (clear all): dirty */
       NBN_RUN,   /* cell i runs (-1: none any more) */
       NBN_META,  /* name, counter, saved / not saved */
       NBN_LIST   /* the list of notebooks */
};
void nb_note(int what, int i);
void nb_calc_edit(int i);                 /* the calculator edits cell i (-1: ends) */
bool nb_page_edits(int i);                /* the page holds cell i (the calculator may not edit it) */
bool nb_synced(void);                     /* a page follows the notebook */
int nb_flush(void);                       /* save now when there are unsaved changes (0, or -1: a toast said why) */
int nb_sync_console(int argc, char **argv); /* `nb sync|rev|doc|get|...` (-> -2: not one of these) */
bool nb_calc_edits(int i);                /* the calculator edits cell i (the page may not) */
bool nb_sync_defer_refresh(void);         /* nb_save: true when the USB drive's refresh waits for the page to go */
void nb_sync_state(void (*kv)(const char *, const char *, void *), void *user);
#define NB_IDLE_SAVE_MS 120000u           /* unsaved changes are saved after this long without one */

#endif
