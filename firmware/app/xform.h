/*
 * Small widgets shared by the matrix, solver, finance and base-n apps:
 *   xform: a scrolling list of "label=value" rows edited in place (solver /
 *          TVM style); one row at a time is edited with a line editor.
 *   xgrid: a scrolling 2-D grid of cells with a dimensions line (matrix editor,
 *          linear-system coefficients).
 *
 * Both are plain structs owned by the app (usually a static in the app's
 * file) and driven from the app's screen callbacks: draw() calls
 * xform_draw()/xgrid_draw(), key() forwards to xform_key()/xgrid_key() and
 * handles the keys they return false for (CLEAR to leave, soft keys), the
 * screen's editor() returns xform_editor()/xgrid_editor() so MATH/CAS/VARS
 * menus insert into the field being edited. The widgets never own the data:
 * they read and write it through callbacks, so validation and storage stay in
 * the app. Values are entered as expressions (`2π`, `1/3`) and passed to the
 * callbacks as CAS text.
 */
#ifndef XFORM_H
#define XFORM_H
#include <stdbool.h>
#include <stddef.h>
#include "ui.h"

enum { XR_EDIT = 0, XR_CHOICE, XR_INFO }; /* row kinds: editable, ENTER/LEFT/RIGHT cycles, read-only */

/* Callbacks describing the rows of an xform (row index i = 0..count()-1). */
typedef struct {
    int (*count)(void);                          /* number of rows (may change, e.g. cash flows) */
    int (*kind)(int i);                          /* XR_* */
    void (*label)(int i, char *buf, size_t n);   /* "N=", "PMT=", ... drawn before the value */
    void (*get)(int i, char *buf, size_t n);   /* value text (display form) */
    bool (*set)(int i, const char *cas, const char *disp); /* commit an edited row; false: keep editing */
    void (*cycle)(int i, int dir);              /* XR_CHOICE rows */
    void (*action)(int i);                      /* F5 (solve) / ALPHA ENTER / SHIFT ENTER: solve (may be NULL) */
    /* Optional footer hint for selected row i (NULL or "": xform_t.hint).
     * Lets a form explain each field where the user is; a message wins. */
    const char *(*hint)(int i);
} xform_ops_t;

typedef struct xform {
    const xform_ops_t *o; /* set before the screen is first drawn */
    int sel, top;         /* selected row, first visible row */
    editor_t ed;          /* editor of the selected XR_EDIT row */
    /* fresh: ed holds the row's value untouched (typing replaces it);
     * loaded: ed has been loaded from the selected row */
    bool fresh, loaded;
    char msg[80];     /* footer (red if err) */
    bool err;
    const char *hint; /* footer when there is no message (and no ops->hint text) */
} xform_t;

void xform_reset(xform_t *f);           /* reload the selected row (after external changes) */
void xform_draw(xform_t *f);
/* UP/DOWN/ENTER move between rows (committing the edited one; a refused value
 * stays selected), ENTER/LEFT/RIGHT cycle XR_CHOICE rows, F5 and SHIFT/ALPHA
 * ENTER run ops->action, CLEAR empties the field (false if already empty). */
bool xform_key(xform_t *f, int key, int mod); /* false: key not used (caller may handle it) */
void xform_touch(xform_t *f, const touch_event_t *ev);
editor_t *xform_editor(xform_t *f); /* for screen_t.editor: menu text is appended, not replacing */
/* Store the edited row through ops->set (no-op if untouched); false if refused. */
bool xform_commit(xform_t *f);
void xform_msg(xform_t *f, bool err, const char *fmt, ...); /* footer message (printf) */

/* Grid state plus callbacks; the app fills rows/cols/max*, the callbacks and
 * title. Row 0 of the display is the dimensions line "TITLE r×c". */
typedef struct xgrid {
    int rows, cols, maxr, maxc; /* current and maximum dimensions */
    int sr, sc;     /* selected cell; sr = -1: the dimensions line (sc 0 rows, 1 cols) */
    int tr, tc;     /* first visible row / column */
    editor_t ed;    /* editor of the selected cell (shown in the bottom line) */
    bool editing;   /* ed is open on (sr, sc) */
    void (*cell)(int r, int c, char *buf, size_t n);                    /* display text of a cell */
    bool (*set)(int r, int c, const char *cas, char *err, size_t errn); /* store; false + err: refused */
    bool (*resize)(int r, int c, char *err, size_t errn);               /* new dimensions; false: refused */
    char title[24];
    char msg[64];   /* bottom-line message, cleared by the next key */
    bool err;
    int dacc, dsel; /* dimensions line: number typed so far (0: none) and the field (sc) it is for */
    /* Optional: column header text (e.g. "x1" .. "b"), drawn in a small row
     * above the cells; NULL: no header row. */
    void (*colname)(int c, char *buf, size_t n);
    char note[40];  /* optional grey text after the dimensions ("3 equations, 3 unknowns") */
    bool dpend;     /* dacc is typed but not applied yet (it could still get a second digit) */
} xgrid_t;

void xgrid_draw(xgrid_t *g);
/* Dimensions line: a typed number is applied as soon as it cannot get another
 * digit (e.g. 3 when the maximum is 20), else on the next key (ENTER, an
 * arrow) or tap; CLEAR drops it. So typing 12 never passes through 1 row
 * (which would drop rows 2..). Cells: see xgrid_key in xform.c. */
bool xgrid_key(xgrid_t *g, int key, int mod);
void xgrid_touch(xgrid_t *g, const touch_event_t *ev);
editor_t *xgrid_editor(xgrid_t *g);

/* The form / grid drawn last and the screen that was current then (set by
 * xform_draw / xgrid_draw), so copy and paste (app/input_clip.c) can reach
 * the widget of the current screen without knowing the app. */
extern xform_t *xform_shown;
extern xgrid_t *xgrid_shown;
extern const screen_t *xform_shown_on, *xgrid_shown_on;

/* helpers */
void x_fmt(double v, char *buf, size_t n);                     /* MODE-formatted number */
bool x_number(const char *cas, double *out, char *err, size_t errn); /* evaluate a numeric field */
void x_cas_of(const char *disp, char *cas, size_t n);           /* display text -> CAS text */
void x_footer_text(const char *s, int y, uint16_t color);       /* status line: shrinks the font to fit */

#endif
