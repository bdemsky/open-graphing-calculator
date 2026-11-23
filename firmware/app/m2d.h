/*
 * Textbook (2D) math: 2D history on the home screen, Textbook template
 * entry, live preview and repeating decimals. Files app/m2d_*.c (flash bank 2),
 * help in help/textbook.txt, tests tests/ui/2x_*.txt and tests/unit/test_m2d.c.
 *
 * Files:
 *   m2d_box.c      box layout engine (measure, place, draw), m2d_box.h
 *   m2d_parse.c    linear math text -> boxes (history, preview)
 *   m2d_edit.c     Textbook templates in the token editor, their linear
 *                  forms, the FRAC/FUNC/MTRX menus, editor -> boxes
 *   m2d_home.c     the home screen view, MODE rows DISPLAY/PREVIEW/REPEAT,
 *                  the persist blob "textbook"
 *   m2d_preview.c  live, time-boxed result preview
 *   m2d_repdec.c   repeating decimals in input and results
 * The MODE rows and the persist blob are registered in app/registry.c;
 * app/home.c calls the m2d_home_* functions and opens the template menus
 * from its soft keys (F2 frac, F3 func, F4 mtrx).
 */
#ifndef M2D_H
#define M2D_H
#include <stdbool.h>
#include <stddef.h>
#include "ui.h"

/* ---- settings (MODE rows, saved by persist_m2d) ---- */
extern bool m2d_textbook; /* DISPLAY: Textbook (templates + 2D) or Classic (linear) */
extern bool m2d_preview;   /* PREVIEW: live result under the entry line */
extern bool m2d_repeat;    /* REPEAT: show 1/7 ≈ 0.(142857) */

/* app_init (before the session is restored): template tokens and the editor
 * hooks; idempotent */
void m2d_init(void);

/* ---- Textbook templates (m2d_edit.c) ---- */
enum {
    TPL_FRAC, TPL_POW, TPL_SQRT, TPL_NROOT, TPL_ABS, TPL_LOGB, TPL_DERIV, TPL_INTEG, TPL_INDEF, TPL_SUM,
    TPL_PROD, TPL_POWP, TPL_MATRIX, TPL_COUNT
};
/* Insert a template at the cursor (cursor into its first box). Matrices take
 * rows x cols (1..3). false if the line is full. */
bool m2d_insert_template(editor_t *e, int type, int rows, int cols);
bool m2d_has_templates(const editor_t *e);
/* Linear text -> tokens with templates (history recall, paste); Classic: as
 * typed text. */
void m2d_insert_linear(editor_t *e, const char *text);
void m2d_set_linear(editor_t *e, const char *text);
/* UP/DOWN (dir -1/+1): the box above/below the cursor's box, as a token
 * range [tb, te]; at_end = the cursor is at the end of its box. */
bool m2d_vertical_target(const editor_t *e, int dir, int *tb, int *te, bool *at_end);
/* Replace every template by its linear text (switching to Classic). */
void m2d_flatten(editor_t *e);
/* Textbook key handling on the home entry line; true if the key was used. */
bool m2d_edit_key(editor_t *e, int key, int mod);
/* Template menus (home soft keys frac, func, mtrx; in other editors they
 * insert the linear form). */
extern const menu_def_t menu_m2d_frac, menu_m2d_func, menu_m2d_mtrx;

/* ---- home screen (m2d_home.c), called from app/home.c ---- */
bool m2d_home_active(void); /* the 2D home view draws the home screen */
void m2d_home_draw(editor_t *ed, int sel);
/* keys for the 2D view (scrolling a selected wide entry, UP/DOWN between
 * boxes); true if used */
bool m2d_home_key(editor_t *ed, int key, int mod, int sel);
void m2d_home_tick(uint32_t now);
/* touch: returns the history item tapped (2*entry + 0 input / 1 output), -1
 * for the entry line (the cursor was moved), -2 for nothing */
int m2d_home_touch(const touch_event_t *ev, editor_t *ed);
/* drag: scroll the history back (ui_drag_scroll, one UI_CH per step); a
 * selected item pushed off screen moves to the nearest visible one (*sel,
 * -1 if none). True if the view or *sel changed. m2d_home_unscroll() returns
 * to the newest content (any key). */
bool m2d_home_drag(const touch_event_t *ev, int *sel);
void m2d_home_unscroll(void);
void m2d_home_state(void (*kv)(const char *, const char *, void *), void *user);
/* the Notebook app's textbook output (nbio_math_t of app/py_core.h): 0, or -1 not in 2D */
int m2d_math_box(void *m);

/* the block of the last draw at screen y: its item (2*cell + 0/1, -1 entry, -3 preview), top and
 * height on the screen; false: none */
bool m2d_home_hit(int y, int *item, int *top, int *h);
/* item's block of the last draw on the screen (top, height); false: not drawn or not in view */
bool m2d_home_box(int item, int *top, int *h);

/* home.c: read access to the history (the notebook's cells, app/nbdoc.h) */
int home_hist_count(void);
int home_cell_kind(int i);      /* NBK_* of cell i */
bool home_graph_active(void);   /* the selected graph cell takes the arrows (pan) */
bool home_item_shown(int item); /* item 2*i + k is a block of the home screen */
const char *home_entry_tag(void); /* "text", "python", "edit": what the entry line makes (NULL: a calculation) */
const char *home_hist_text(int i, bool output, bool *err);
/* the execution count of cell i as a small dim "[n]" (Out[n] names its result) in the row
 * [top, top + h): at the left edge, when it fits before x1 (the result's left edge), or (right) at
 * the right edge, when it fits after x1 (the text's right edge) */
void home_count_draw(int i, bool right, int x1, int top, int h);
/* the selected history item's text (copy, app/input_clip.c); false: none */
bool home_selection_text(char *out, size_t n);

/* ---- live preview (m2d_preview.c) ---- */
/* Text shown under the entry for the editor's current contents, or NULL. */
const char *m2d_preview_text(const editor_t *ed);
/* Evaluate the preview if the entry has been idle long enough; true if the
 * screen must be redrawn. */
bool m2d_preview_tick(const editor_t *ed, uint32_t now);
void m2d_preview_reset(void);

/* ---- repeating decimals (m2d_repdec.c) ---- */
/* "0.(3)" -> "((3-0)/9)", "1.2(34)" -> "((1234-12)/990)" in CAS input; false
 * if none (then `out` is unspecified: use `in`). */
bool repdec_rewrite(const char *in, char *out, size_t n);
/* Display: decimals that are a fraction with a short period -> "0.(142857)".
 * `sig` = significant digits of the CAS output. */
void repdec_apply(const char *in, char *out, size_t n, int sig);
/* One number: true and the repeating form if v (shown with sig digits as
 * `lit`) is p/q with a short period. */
bool repdec_format(const char *lit, int sig, char *out, size_t n);

#endif
