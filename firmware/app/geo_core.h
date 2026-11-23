/*
 * The Geometry app as a code module: the interface between the core and the
 * "geometry" module (modules/geometry; mechanism in app/modx.h, design in
 * docs/QSPI_CONTENT.md section 12.7).
 *
 * The app's code (the construction kernel, the screen and its tools, the
 * console `geo`, the drawing on GRAPH) runs in place from the QSPI. What the
 * rest of the calculator refers to by address stays in the core
 * (app/geo_core.c): the screen scr_geo, the APPS entry, the soft-key bar, the
 * menus, the session blob "geo", the GRAPH overlay, the prompt line's editor,
 * the files on the microSD card. Each of them forwards to the module through
 * the two tables below, or says why there is no Geometry ("Install the
 * Geometry module (Memory > Modules).").
 *
 *   geo_core_t  core -> module: the services the module calls (libm and libc
 *               functions, drawing, the prompt editor, Y= functions, GRAPH
 *               overlay helpers, files, the console). The module reaches it
 *               through modx_api_t.geo_core (API size 10).
 *   geo_ops_t   module -> core: the module's entry points, in its RW block;
 *               init hands them over with geo_core_t.attach. The core calls
 *               them only between modx_call_begin / modx_call_end (busy count,
 *               the RW pointer of modx.h MODX_RW).
 *
 * Versioning: GEO_CORE_ABI covers both tables and every core type or constant
 * the module is compiled against (KEY_*, TEV_*, pw_t / pr_t as opaque handles,
 * the theme colours). A module refuses to start on another GEO_CORE_ABI; new
 * entries are appended (size grows) without a bump.
 *
 * The saved construction is kept by the core while the module is not bound
 * (not installed yet, removed, between a store change and the next use): the
 * session blob is held as bytes and given to the module when it attaches, and
 * modx_unbind_all() takes the module's state back first (geo_core_unbind).
 */
#ifndef GEO_CORE_H
#define GEO_CORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "font.h"
#include "session.h"

#define GEO_MODULE "geometry"
#define GEO_CORE_ABI 1

/* menus the core shows for the module (geo_core_t.menu_open) */
enum { GEO_MENU_POINTS, GEO_MENU_DRAW, GEO_MENU_CONSTR, GEO_MENU_MEASURE, GEO_MENU_VIEW, GEO_MENU_FILE, GEO_NMENU };

/* a touch event, flattened (core/touch.h touch_event_t) */
typedef struct {
    int type;          /* TEV_* */
    int x, y, x0, y0;  /* where it is, where it began */
    int dx, dy;        /* movement since the last event */
    double scale;      /* pinch: scale since the pinch began */
} geo_touch_t;

/* The module's entry points (filled by its init, kept in its RW block). */
typedef struct geo_ops {
    uint16_t size;
    void (*enter)(bool returning);                 /* the screen is shown (returning: from a menu or message) */
    void (*draw)(void);                            /* redraw the body */
    void (*key)(int key, int mod);
    void (*touch)(const geo_touch_t *t);
    void (*softkey)(int i);                        /* F1..F5 = 0..4, SHIFT F1..F5 = 5..9 */
    int (*menu_count)(int menu);
    /* item i of menu: its label and help topic into the buffers, -> its action */
    int (*menu_item)(int menu, int i, char *label, size_t n, char *help, size_t hn);
    void (*menu_select)(int menu, int action);
    /* the construction with its view (session blob "geo" v1, files "GEO1" + the same) */
    void (*save)(pw_t *w);
    bool (*load)(pr_t *r);                         /* false (and an empty construction) on a bad blob */
    void (*reset)(void);                           /* empty construction, standard view */
    /* GRAPH overlay */
    bool (*ov_active)(void);
    void (*ov_draw)(void);
    int (*ov_npoints)(void);
    bool (*ov_point)(int i, double *x, double *y);
    bool (*ov_bounds)(double *x0, double *x1, double *y0, double *y1);
    bool (*clip_text)(char *out, size_t n);        /* the copy hook (app/input_clip.c) */
} geo_ops_t;

/* The core's services for the module (geo_core.c geo_core_table). */
typedef struct geo_core {
    uint16_t abi, size;                            /* GEO_CORE_ABI, sizeof(geo_core_t) */
    void (*attach)(const geo_ops_t *ops);          /* from init: the core keeps ops and loads the saved construction */
    /* libm */
    double (*sin)(double), (*cos)(double), (*tan)(double), (*acos)(double), (*atan2)(double, double);
    double (*sqrt)(double), (*hypot)(double, double), (*fmod)(double, double), (*pow)(double, double);
    double (*log10)(double), (*floor)(double), (*ceil)(double), (*round)(double), (*fmin)(double, double);
    double (*fmax)(double, double);
    long (*lround)(double);
    /* libc */
    size_t (*strlen)(const char *s);
    char *(*strstr)(const char *s, const char *t);
    double (*atof)(const char *s);
    /* session blob reader / writer (app/session.h) */
    void (*pw_u8)(pw_t *w, uint32_t v);
    void (*pw_f64)(pw_t *w, double v);
    void (*pw_str)(pw_t *w, const char *s);
    uint32_t (*pr_u8)(pr_t *r);
    double (*pr_f64)(pr_t *r);
    void (*pr_str)(pr_t *r, char *out, size_t n);
    bool (*pr_failed)(const pr_t *r);
    /* drawing (core/gfx.h) */
    void (*gfx_pixel)(int x, int y, uint16_t c);
    void (*gfx_hline)(int x, int y, int w, uint16_t c);
    void (*gfx_vline)(int x, int y, int h, uint16_t c);
    void (*gfx_line)(int x0, int y0, int x1, int y1, uint16_t c);
    void (*gfx_fill_rect)(int x, int y, int w, int h, uint16_t c);
    int (*gfx_text)(const font_t *f, int x, int y, const char *s, uint16_t fg, int bg);
    int (*gfx_text_width)(const font_t *f, const char *s);
    void (*gfx_set_clip)(int x, int y, int w, int h);
    void (*gfx_reset_clip)(void);
    const font_t *font_small, *font_medium;
    /* user interface */
    void (*cursor_hide)(void);
    void (*message)(const char *title, const char *text);
    void (*menu_open)(int menu);                   /* GEO_MENU_*: the core's menu, items from ops */
    void (*files_menu)(void);                      /* File > Open: the constructions on the card */
    void (*back)(void);                            /* (back) to the Geometry screen, state kept */
    void (*home)(void);                            /* leave to the home screen */
    bool (*shown)(void);                           /* the Geometry screen is current */
    void (*redraw)(void);
    /* the prompt line (one editor, the core's) */
    void (*ed_open)(const char *init);             /* clear, insert init, show it as the screen's editor */
    void (*ed_close)(void);
    bool (*ed_key)(int key, int mod);              /* the editor's keys (not CLEAR / ENTER); true if used */
    void (*ed_draw)(int x, int y, int cols);
    void (*ed_clear)(void);
    bool (*ed_empty)(void);
    void (*ed_text)(char *out, size_t n);          /* as displayed */
    int (*ed_number)(double *v, char *err, size_t n); /* evaluate it with the CAS: 0 ok */
    /* Y= functions and GRAPH */
    void (*funcs_compile)(void);
    bool (*func_on)(int f);                        /* Y(f+1) enabled and compiled */
    double (*func_eval)(int f, double x);
    uint16_t (*func_color)(int f);
    void (*graph_invalidate)(void);
    void (*gov_window)(double *x0, double *x1, double *y0, double *y1);
    void (*gov_line)(double x0, double y0, double x1, double y1, uint16_t c);
    void (*gov_mark)(double x, double y, int mark, uint16_t c);
    /* files on the microSD card: the construction (ops.save / ops.load) as /CALC/GEO/NAME.GEO */
    int (*file_save)(const char *name);            /* 0 or FS_E* (a message box says what failed) */
    int (*file_load)(const char *name);            /* 0 or FS_E* (FS_EINVAL: not a geometry file) */
    const char *(*fs_strerror)(int err);
    /* console */
    void (*expect_failed)(void);                   /* a failed `geo expect` (the simulator's exit status) */
} geo_core_t;

const geo_core_t *geo_core_table(void);

/* ---- core side (app/geo_core.c) ---- */
extern const struct screen scr_geo;
int geo_console(int argc, char **argv);       /* console `geo ...` */
bool geo_clip_text(char *out, size_t n);       /* the copy hook of the edit bar */
void geo_core_unbind(bool keep);               /* modx_unbind_all (keep: take the construction back first) */
#endif
