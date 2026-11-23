/* Home screen: a notebook (app/nbdoc.h; help "home-notebook"): entry line +
 * cells, Ans, ENTRY recall, selection, graph cells, the notebook menu.
 * Soft keys: graph frac func mtrx ≈ (an animated graph cell selected: play) / SHIFT: table solve simplify rewrite graph.
 * MENU: the NOTEBOOK menu (save, open, new, cells, run all, edit in place).
 *
 * The screen is a scrolling transcript of the open notebook's cells (nbd):
 * each evaluated entry adds a math cell with the input (left-aligned) and the
 * result (right-aligned, red on errors), and the token editor `ed`
 * (app/editor.h) is the line being typed below them. Keys:
 *   ENTER         evaluate (empty line: repeat the last entry); F5 (≈) the
 *                 same wrapped in approx() (empty line: the last result
 *                 approx <-> exact); "expr→var" stores (var:=expr)
 *   SHIFT F3      simplify the selected item, the line or the last result;
 *   SHIFT F4      the REWRITE menu: factor, expand, partfrac, trig forms...
 *                 (home_result_action; app/result_actions.c)
 *   SHIFT F5      a graph cell of the Y= functions (app/gcell.c)
 *   SHIFT ENTER   ENTRY recall: copy earlier inputs, newest first, cycling
 *   UP / DOWN     select cells' inputs and outputs; ENTER pastes the
 *                 selected one into the line (a text / Python cell: edits
 *                 it; a graph cell: the graph app on it), CLEAR deselects;
 *                 on a selected graph cell LEFT/RIGHT, + − act on its plot;
 *                 on a selected calculation the soft keys are input, result,
 *                 Out[n], edit, ≈ (a Python cell: edit, result, Out[n]) and
 *                 SHIFT ENTER pastes its result
 *   Ans, Out[n]   the notebook's latest result / the result of cell [n] (the
 *                 small [n] by each result): nbd_refs (app/nbdoc.c) rewrites
 *                 them before a calculation is evaluated
 *   CLEAR         clear the line, or (an unsaved notebook) every cell when
 *                 the line is empty (MENU F1 brings them back, app.c); SHIFT
 *                 CLEAR is the global "clear all" (keys.c)
 *   operator first  "+2" becomes "Ans+2"
 * Touch: tap to select a cell, tap it again to paste, tap the entry to move
 * the cursor; gestures on graph cells (graph_gesture).
 *
 * Two renderers: this file draws the Classic text view (one font cell per
 * codepoint, wrapped at COLS) of a notebook of calculations. When Textbook
 * display, the live preview, repeating decimals or the large font are on, or
 * the notebook has other cells, view2d() is true and drawing, most keys and
 * touch are delegated to app/m2d_home.c, which reads the cells through
 * home_hist_count()/home_hist_text()/home_cell_kind().
 *
 * State: the document is nbd (app/nbdoc.c); here the editor, the selection,
 * the recall index, the entry mode (math / text / Python line, a cell edited
 * in place). Saved in the session as the "home" persist blob (the notebook +
 * the serialized entry line). Evaluation goes through app_eval_result()
 * (app/numfmt.c: app_eval plus the MODE SIMPLIFY AUTO pass) after
 * yvars_sync(). */
#include <stdio.h>
#include <string.h>
#include "anim.h"
#include "forms_ui.h"
#include "g3d.h" /* scr_g3d, scr_g3d_eq */
#include "graph.h" /* gx_tmp_ed */
#include "m2d.h"
#include "nbdoc.h"
#include "py_core.h"
#include "session.h"
#include "ui.h"

#define HIST_MAX NB_MAX_CELLS /* cells kept (the oldest calculation dropped when an unsaved notebook is full) */
#define HIST_TEXT 192 /* bytes of an input / output text the home screen copies (longer texts are cut) */

static editor_t ed;
static int sel = -1;      /* selected item: 2*cell + (0 input, 1 output) */
static int recall = -1;   /* ENTRY recall index */
static int rw_sel = -1;   /* selection when a result action key was pressed (home_result_action) */
static bool gactive;      /* the selected graph cell takes the arrows (pan) and + - (zoom) */
static bool gflow;        /* the graph screens were opened from here (home_graph_embed) */
static char gsel[24];     /* the cell to select when the home screen is entered next */
static char gnew[24];     /* the graph cell the graph flow made (removed when left without a function) */
/* what ENTER does with the entry line: a calculation, or (notebook menu) a new text / Python
 * cell; edit_cell >= 0: the line replaces that cell's source (edit in place) */
enum { EM_MATH = 0, EM_TEXT, EM_PY };
static int emode, edit_cell = -1;
#define nhist (nbd->ncells)

/* The cell edited in place (-1: none). The browser page may not edit it meanwhile (the module's
 * calculator lock, `nb calcedit`); false: the page edits it (a message says so), not edited here. */
static bool set_edit(int i)
{
    if (i == edit_cell)
        return true;
    nbd->msg[0] = 0;
    int r = py_nb_call("calcedit", i >= 0 && i < nhist ? nbd->cell[i].id : NULL);
    if (r && nbd->msg[0]) {
        ui_message("NOTEBOOK", nbd->msg);
        return false;
    }
    edit_cell = i;
    return true;
}

/* the texts of cell i: its input (source) and its output (result, or the error message) */
static const char *cell_in(int i) { return nbd_get(&nbd->cell[i], NBF_SRC); }
static const char *cell_out(int i, bool *err)
{
    const nb_cell_t *c = &nbd->cell[i];
    bool e = c->f[NBF_ENAME] != NULL;
    if (err)
        *err = e;
    if (c->kind == NBK_PY && !e) {
        if (c->f[NBF_VAL])
            return c->f[NBF_VAL];
        return nbd_get(c, NBF_OUT);
    }
    if (e && c->kind == NBK_PY)
        return nbd_get(c, NBF_TB)[0] ? nbd_get(c, NBF_TB) : nbd_get(c, NBF_EVALUE);
    return e ? nbd_get(c, NBF_EVALUE) : nbd_get(c, NBF_VAL);
}
int home_cell_kind(int i) { return i >= 0 && i < nhist ? nbd->cell[i].kind : NBK_MATH; }

/* The graph app (2D or 3D) bound to graph cell i: the cell is the calculator's while it is open
 * there (the browser page's lock rules, `nb calcedit`); false: the page edits it (a message says so). */
static bool graph_bind(int i)
{
    nbd->msg[0] = 0;
    if (py_nb_call("calcedit", nbd->cell[i].id) && nbd->msg[0]) {
        ui_message("NOTEBOOK", nbd->msg);
        return false;
    }
    gcell_bind(i);
    return true;
}
bool home_graph_active(void) { return gactive && sel >= 0 && home_cell_kind(sel / 2) == NBK_GRAPH; }
/* the items shown: math both, Python its output only when it has one, graph and text cells one block */
bool home_item_shown(int it)
{
    if (it < 0 || it / 2 >= nhist)
        return false;
    int k = nbd->cell[it / 2].kind;
    if (!(it % 2))
        return true;
    if (k == NBK_MATH)
        return true;
    bool err;
    return k == NBK_PY && cell_out(it / 2, &err)[0];
}
/* what the entry line makes (a tag at its right; NULL: a calculation) */
const char *home_entry_tag(void)
{
    if (edit_cell >= 0)
        return "edit";
    return emode == EM_TEXT ? "text" : emode == EM_PY ? "python" : NULL;
}
/* the 2D view draws whenever the notebook has other cells than calculations (or the line is one) */
static bool view2d(void) { return m2d_home_active() || nbd_special() || home_entry_tag(); }

#define ROWS (BODY_H / CH) /* text rows in the body (10) */

/* Classic layout: the transcript is flattened into display lines of at most
 * COLS codepoints, rebuilt on every draw. Lines point into hist[] / ed_buf. */
typedef struct {
    const char *s; /* start (UTF-8) */
    int bytes;     /* length of this line in bytes */
    bool right;    /* right-aligned (results) */
    uint16_t fg;
    int item; /* history item for selection highlight, -1 for the editor */
} line_t;

#define MAX_LINES (32 * 8 + ED_MAX / COLS + 4) /* (the newest cells that fit; older ones when selected) */
static line_t lines[MAX_LINES];
static int nlines;
static char ed_buf[ED_MAX * 4]; /* display text of the entry line (lines[] point into it) */
static int vis_first, vis_ed_first; /* layout of the last draw, for touch */
static int hback; /* drag: lines scrolled back from the newest (any key returns) */

/* Display lines s takes when wrapped every COLS codepoints (at least 1). */
static int wrapped_count(const char *s)
{
    int n = utf8_len(s);
    return n ? (n + COLS - 1) / COLS : 1;
}

/* Append s to lines[], split every COLS codepoints (hard wrap, no word
 * breaks). An empty string still takes one (empty) line. Silently stops when
 * lines[] is full. */
static void add_wrapped(const char *s, bool right, uint16_t fg, int item)
{
    if (!*s) {
        if (nlines < MAX_LINES)
            lines[nlines++] = (line_t){s, 0, right, fg, item};
        return;
    }
    while (*s && nlines < MAX_LINES) {
        const char *p = s;
        for (int i = 0; i < COLS && *p; i++)
            utf8_next(&p);
        lines[nlines++] = (line_t){s, (int)(p - s), right, fg, item};
        s = p;
    }
}

/* Draw the body. Classic layout: flatten history + entry into lines[], then
 * show the last ROWS lines (the entry line stays at the bottom), scrolling up
 * only as far as needed to show a selected history item. A full history of
 * long texts can need more than MAX_LINES lines: the oldest items are then
 * left out, so the entry line always gets its lines. The selected item is
 * drawn on the selection background; the blinking cursor is placed at the
 * cursor's wrapped row/column unless an item is selected. Also records the
 * layout (vis_first, vis_ed_first) for home_touch(). */
static void nm_set_title(void);
static uint32_t drawn_gen; /* the document as last drawn (home_tick: changed by the module or the page) */
static void home_draw(void)
{
    drawn_gen = nbd->gen;
    nm_set_title(); /* (the menu's title is drawn before the menu asks for its items) */
    if (view2d()) { /* 2D math, preview, large font, graph / text / Python cells: app/m2d_home.c */
        m2d_home_draw(&ed, sel);
        return;
    }
    nlines = 0;
    ed_display(&ed, ed_buf, sizeof ed_buf);
    /* oldest item shown: the newest items that fit next to the entry's lines (+1 for a wrapped
     * cursor), or from the selected one on when it is older (the entry line may then be left out) */
    int from = nhist, room = MAX_LINES - wrapped_count(ed_buf) - 1;
    while (from > 0) {
        int k = wrapped_count(cell_in(from - 1)) + wrapped_count(cell_out(from - 1, NULL));
        if (k > room)
            break;
        room -= k;
        from--;
    }
    if (sel >= 0 && sel / 2 < from)
        from = sel / 2;
    for (int i = from; i < nhist; i++) {
        bool err;
        const char *out = cell_out(i, &err);
        add_wrapped(cell_in(i), false, C_BLACK, 2 * i);
        add_wrapped(out, true, err ? C_RED : C_BLACK, 2 * i + 1);
    }
    int ed_first = nlines;
    int ccol = ed_cursor_col(&ed);
    add_wrapped(ed_buf, false, C_BLACK, -1);
    int ed_lines = nlines - ed_first;
    int cur_line = ed_first + ccol / COLS;
    if (ccol / COLS >= ed_lines && nlines < MAX_LINES) /* cursor wrapped past the last full line */
        lines[nlines++] = (line_t){"", 0, false, C_BLACK, -1};

    /* choose the first visible line */
    int first = nlines - ROWS - hback;
    if (sel >= 0) {
        int sl = -1;
        for (int i = 0; i < nlines; i++)
            if (lines[i].item == sel) {
                sl = i;
                break;
            }
        if (sl >= 0 && sl < first)
            first = sl;
    }
    if (first < 0)
        first = 0;
    vis_first = first;
    vis_ed_first = ed_first;

    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, C_WHITE);
    char tmp[COLS * 4 + 1];
    for (int r = 0; r < ROWS && first + r < nlines; r++) {
        const line_t *l = &lines[first + r];
        int y = BODY_Y + r * CH;
        memcpy(tmp, l->s, (size_t)l->bytes);
        tmp[l->bytes] = 0;
        int w = gfx_text_width(FONT, tmp);
        int x = l->right ? LCD_WIDTH - w - 2 : 0;
        bool hl = sel >= 0 && l->item == sel;
        if (hl)
            gfx_fill_rect(x, y, w, CH, C_SEL_BG);
        if (l->bytes)
            gfx_text(FONT, x, y, tmp, hl ? C_WHITE : l->fg, GFX_TRANSPARENT);
        if (l->right && l->item >= 0 && l->fg != C_RED && (first + r == 0 || lines[first + r - 1].item != l->item))
            home_count_draw(l->item / 2, false, x, y, CH); /* the result's [n] (Out[n]) */
    }
    int crow = cur_line - first;
    if (sel < 0 && crow >= 0 && crow < ROWS)
        ui_cursor_at((ccol % COLS) * CW, BODY_Y + crow * CH, ed_char_at_cursor(&ed), C_BLACK, C_WHITE);
    else
        ui_cursor_hide();
}

static bool cas_fresh; /* the result about to be stored was just evaluated (it is the CAS's ans) */

/* The result of a calculation into math cell c: the value, or the error message (ename CASError). */
static void set_result(nb_cell_t *c, const char *out, bool err)
{
    nbd_clear_out(c);
    nbd_set(c, err ? NBF_EVALUE : NBF_VAL, out);
    if (err)
        nbd_set(c, NBF_ENAME, "CASError");
    c->count = (int16_t)++nbd->counter;
    c->flags |= NBC_RAN;
    if (!err && cas_fresh) /* (the CAS's ans is this result: Ans keeps every digit) */
        nbd_ans_mark(c);
    cas_fresh = false;
}

/* Append a calculation (a math cell). An unsaved notebook that is full (cells or memory) drops its
 * oldest cell, as the history always did; a named one says so. */
static void add_history(const char *in, const char *out, bool err)
{
    for (int tries = 0; tries < 8; tries++) {
        nb_cell_t *c = nbd_insert(nhist, NBK_MATH);
        if (c) {
            nbd_set(c, NBF_SRC, in);
            set_result(c, out, err);
            if ((!*in || c->f[NBF_SRC]) && (!*out || c->f[err ? NBF_EVALUE : NBF_VAL]))
                break;
            nbd_delete(nhist - 1); /* (no memory for its texts) */
        }
        if (nbd->open || !nhist) {
            ui_toast("the notebook is full");
            break;
        }
        nbd_delete(0);
        if (sel >= 0)
            sel = sel >= 2 ? sel - 2 : -1;
    }
    nbd_changed();
}

/* Evaluate display text disp / CAS text cas (from an editor) like the entry line: "expr→var" is
 * stored (var:=expr, never approx), ≈ wraps it in approx(). pretty gets the result; true on an error. */
static bool calc(char *cas, bool approx, char *pretty, size_t n)
{
    static char expr[ED_MAX * 8 + 40];
    char *sto = strchr(cas, '\x01'); /* "expr→var" becomes "var:=expr" */
    if (sto) {
        *sto = 0;
        char *var = sto + 1;
        while (*var == ' ')
            var++;
        snprintf(expr, sizeof expr, "%s:=%s", var, cas);
    } else if (approx) { /* ≈: the decimal value */
        snprintf(expr, sizeof expr, "approx(%s)", cas);
    } else {
        snprintf(expr, sizeof expr, "%s", cas);
    }
    static char ref[sizeof expr + 256];
    char msg[80];
    if (!nbd_refs(expr, ref, sizeof ref, msg, sizeof msg)) { /* Ans and Out[n]: the notebook's results */
        snprintf(pretty, n, "ERR:UNDEFINED %s", msg);
        return true;
    }
    yvars_sync(); /* Y1(..)..Y6(..) are CAS functions (VARS > Y-VARS) */
    cas_status st = app_eval_result(ref, pretty, n);
    cas_fresh = true;
    return st != CAS_OK && st != CAS_ERR_OUTPUT_TRUNC;
}

/* the last calculation (math cell), -1 none */
static int last_math(void)
{
    for (int i = nhist - 1; i >= 0; i--)
        if (nbd->cell[i].kind == NBK_MATH)
            return i;
    return -1;
}

static void run_python(int i); /* (the notebook menu, below) */

/* ENTER with a text or Python line, or editing a cell in place: the line becomes the cell's
 * source (a Python cell then runs). */
static void commit_cell(void)
{
    char disp[ED_MAX * 4];
    ed_display(&ed, disp, sizeof disp);
    int i = edit_cell;
    if (i < 0 || i >= nhist) {
        nb_cell_t *c = nbd_insert(nhist, emode == EM_PY ? NBK_PY : NBK_TEXT);
        if (!c) {
            ui_toast("the notebook is full");
            return;
        }
        i = nhist - 1;
    }
    nbd_set(&nbd->cell[i], NBF_SRC, disp);
    ed_clear(&ed);
    emode = EM_MATH;
    set_edit(-1);
    nbd_changed();
    if (nbd->cell[i].kind == NBK_PY)
        run_python(i);
}

/* ENTER / ≈: evaluate the entry line and add it to the history.
 * The CAS text comes from ed_cas(); the → token's CAS text is the marker byte
 * 0x01, so "expr→var" is rewritten as "var:=expr" (never wrapped in approx).
 * yvars_sync() first defines Y1(x)..Y6(x) so the input may use them. An
 * error result is recorded as such (except a merely truncated output). The
 * entry line is cleared and ENTRY recall restarts from the newest item.
 * Editing a cell in place: the cell gets the new input and result. */
static void evaluate(bool approx)
{
    if (emode != EM_MATH || (edit_cell >= 0 && edit_cell < nhist && nbd->cell[edit_cell].kind != NBK_MATH)) {
        commit_cell();
        return;
    }
    if (ed_empty(&ed)) {
        int l = last_math();
        if (l < 0 || edit_cell >= 0)
            return;
        ed_set_text(&ed, cell_in(l)); /* ENTER on an empty line repeats the last entry */
    }
    char disp[ED_MAX * 4], cas[ED_MAX * 4 + 16], pretty[HIST_TEXT];
    ed_display(&ed, disp, sizeof disp);
    ed_cas(&ed, cas, sizeof cas);
    bool err = calc(cas, approx, pretty, sizeof pretty);
    if (edit_cell >= 0 && edit_cell < nhist) {
        nb_cell_t *c = &nbd->cell[edit_cell];
        nbd_set(c, NBF_SRC, disp);
        set_result(c, pretty, err);
        nbd_changed();
    } else {
        add_history(disp, pretty, err);
    }
    set_edit(-1);
    ed_clear(&ed);
    recall = -1;
}

/* Re-run math cell i (run all, re-run): its input evaluated again, as typed. */
static void rerun_math(int i)
{
    char cas[ED_MAX * 4 + 16], pretty[HIST_TEXT];
    void (*hook)(editor_t *) = ed_change_hook; /* (a scratch editor: not an undo step) */
    ed_change_hook = NULL;
    m2d_set_linear(&gx_tmp_ed, cell_in(i));
    ed_cas(&gx_tmp_ed, cas, sizeof cas);
    ed_change_hook = hook;
    if (!cas[0])
        return;
    bool err = calc(cas, false, pretty, sizeof pretty);
    set_result(&nbd->cell[i], pretty, err);
}

/* True if a key's text is an operator that needs a left operand; typed on an
 * empty line it is preceded by Ans (chained calculations). */
static bool is_binary_op(const char *t)
{
    static const char *const ops[] = {"+", "-", "*", "/", "^", "²", "⁻¹", "→", "³"};
    for (size_t i = 0; i < sizeof ops / sizeof ops[0]; i++)
        if (!strcmp(t, ops[i]))
            return true;
    return false;
}

static void edit_in_place(int i);

/* text into the entry line at the cursor: a calculation's line as templates (Textbook), a text or
 * Python line as it is */
static void line_insert(const char *t)
{
    if (emode == EM_MATH && !(edit_cell >= 0 && edit_cell < nhist && nbd->cell[edit_cell].kind != NBK_MATH))
        m2d_insert_linear(&ed, t);
    else
        ed_insert_text(&ed, t, NULL);
}

/* The selected cell into the entry line (the soft keys of a selection, SHIFT ENTER): what 0 its
 * input, 1 its result, 2 the reference Out[n] to its result; the selection ends. */
static void sel_insert(int what)
{
    int i = sel / 2;
    if (sel < 0 || i >= nhist)
        return;
    const nb_cell_t *c = &nbd->cell[i];
    bool val = c->count > 0 && !c->f[NBF_ENAME] && c->f[NBF_VAL];
    if (what && !val) {
        ui_toast("this cell has no result");
        return;
    }
    char ref[16];
    snprintf(ref, sizeof ref, "Out[%d]", c->count);
    line_insert(what == 2 ? ref : what ? nbd_value(i) : cell_in(i));
    sel = -1;
    home_draw();
}

/* the item before / after `it` that the home screen shows (-1: none) */
static int item_step(int it, int dir)
{
    for (it += dir; it >= 0 && it < 2 * nhist; it += dir)
        if (home_item_shown(it))
            return it;
    return -1;
}

/* Key handler. Order: the 2D-math layer gets the first chance (templates,
 * scrolling wide results; it may switch screens, hence the ui_current()
 * check); then history selection mode (sel >= 0), where any key other than
 * UP/DOWN/ENTER/CLEAR leaves selection and is handled again as a normal key;
 * then the entry-line keys. Keys the editor does not use return without a
 * redraw. A selected graph cell: LEFT/RIGHT pan it, + / − zoom (and the arrows
 * then all pan until CLEAR), ENTER opens the graph app on it; an animated one
 * (with the Animation module): F5 plays it, LEFT/RIGHT step its t until + / −. */
static void home_key(int key, int mod)
{
    hback = 0; /* back to the newest lines (the entry line) */
    m2d_home_unscroll();
    if (sel >= 0 && sel / 2 < nhist && nbd->cell[sel / 2].kind == NBK_GRAPH) {
        int i = sel / 2;
        bool arrow = key == KEY_LEFT || key == KEY_RIGHT || key == KEY_UP || key == KEY_DOWN;
        bool zoom = key == KEY_ADD || key == KEY_SUB;
        if (mod == MOD_NONE && (key == KEY_LEFT || key == KEY_RIGHT) && !gactive && gcell_animated(i) &&
            anim_available()) {
            gcell_anim_step(i, key == KEY_RIGHT ? 1 : -1);
            home_draw();
            return;
        }
        if (mod == MOD_NONE && ((arrow && (gactive || key == KEY_LEFT || key == KEY_RIGHT)) || zoom)) {
            gactive = true;
            gcell_key(i, key, mod);
            home_draw();
            return;
        }
        if (key == KEY_CLEAR && gactive) {
            gactive = false;
            home_draw();
            return;
        }
        if (key == KEY_ENTER && mod == MOD_NONE) {
            gactive = false;
            if (!graph_bind(i))
                return;
            sel = -1;
            ui_switch(gcell_is3d(i) ? &scr_g3d : &scr_graph);
            return;
        }
    }
    gactive = false;
    if (m2d_home_key(&ed, key, mod, sel)) { /* Textbook templates, wide entries */
        if (ui_current() == &scr_home)
            home_draw();
        return;
    }
    if (sel >= 0) {
        if (key == KEY_UP) {
            int s = item_step(sel, -1);
            if (s >= 0)
                sel = s;
        } else if (key == KEY_DOWN) {
            sel = item_step(sel, 1);
        } else if (key == KEY_ENTER && mod == MOD_SHIFT) { /* the selected cell's result */
            sel_insert(1);
            return;
        } else if (key == KEY_ENTER) {
            int i = sel / 2;
            bool err;
            const char *text = sel % 2 ? cell_out(i, &err) : cell_in(i);
            if (nbd->cell[i].kind != NBK_MATH) {
                edit_in_place(i); /* a text or Python cell: its source to edit */
                return;
            }
            if (!(sel % 2 && err))
                m2d_insert_linear(&ed, text); /* Textbook: as templates */
            sel = -1;
        } else if (key == KEY_CLEAR) {
            sel = -1;
        } else if (key == KEY_F5) { /* ≈: the selected item approx <-> exact */
            rw_sel = sel;
            home_result_action(NULL, "");
            return;
        } else {
            sel = -1;
            home_key(key, mod);
            return;
        }
        home_draw();
        return;
    }
    switch (key) {
    case KEY_ENTER:
        if (mod == MOD_SHIFT) { /* ENTRY: recall previous inputs, newest first */
            for (int n = 0; n < nhist; n++) {
                recall = recall < 0 ? nhist - 1 : (recall + nhist - 1) % nhist;
                if (nbd->cell[recall].kind == NBK_MATH) {
                    m2d_set_linear(&ed, cell_in(recall));
                    break;
                }
            }
        } else {
            evaluate(false);
        }
        break;
    case KEY_F5: /* ≈; on an empty line: the last result approx <-> exact */
        if (ed_empty(&ed) && last_math() >= 0 && emode == EM_MATH) {
            rw_sel = -1;
            home_result_action(NULL, "");
            return;
        }
        evaluate(true);
        break;
    case KEY_CLEAR:
        if (!ed_empty(&ed)) { /* SHIFT CLEAR never gets here: global "clear all" (keys.c) */
            ed_clear(&ed);
        } else if (emode != EM_MATH || edit_cell >= 0) {
            emode = EM_MATH; /* the text / Python / edit line given up */
            set_edit(-1);
        } else if (nbd->open) {
            ui_toast("a saved notebook: HOME for its menu");
        } else {
            nbd_scratch();
            nbd_changed();
        }
        break;
    case KEY_UP:
        if (nhist)
            sel = item_step(2 * nhist, -1);
        break;
    case KEY_DOWN:
        break;
    default: {
        char t[16];
        if (emode == EM_MATH && ed_empty(&ed) && mod == MOD_NONE && ui_key_text(key, mod, 0, t, sizeof t) &&
            is_binary_op(t))
            ed_insert(&ed, TOK_ANS); /* starting with an operator uses Ans */
        if (!ui_editor_key(&ed, key, mod))
            return;
    }
    }
    home_draw();
}

static editor_t *home_editor(void) { return &ed; }

/* the cursor blink (2D view); the notebook changed under the screen (the browser page, the module):
 * drawn again, the selection kept on the same item where it still exists */
static void home_tick(uint32_t now)
{
    m2d_home_tick(now);
    if (nbd->gen != drawn_gen && ui_current() == &scr_home) {
        if (sel >= 2 * nhist)
            sel = -1;
        home_draw();
    }
    if (gcell_anim_cell() >= 0) /* an animated graph cell: its frames (the Animation module's timing) */
        anim_tick(now, ANIM_SCR_CELL);
}

/* (anim_core.c) a new frame of the animated graph cell: its block drawn again, the rest left */
void home_anim_frame(void)
{
    int i = gcell_anim_cell(), top, h;
    if (i < 0 || ui_current() != &scr_home)
        return;
    if (!view2d() || !m2d_home_box(2 * i, &top, &h)) {
        home_draw();
        return;
    }
    gfx_set_clip(0, BODY_Y, LCD_WIDTH, BODY_H);
    gcell_draw(i, 0, top, LCD_WIDTH, h, sel == 2 * i, sel == 2 * i && gactive);
    gfx_reset_clip();
}

/* Entering the home screen: never with a stale selection or scroll; a graph cell the graph app
 * was opened on gets its functions and window back (gcell_unbind). */
static void home_enter(void)
{
    char id[24];
    bool bound = gcell_bound()[0] != 0;
    snprintf(id, sizeof id, "%s", bound ? gcell_bound() : gsel);
    gcell_unbind();
    if (bound && edit_cell < 0)
        py_nb_call("calcedit", NULL); /* (the cell is the page's to edit again) */
    gsel[0] = 0;
    gflow = false;
    int i = id[0] ? nbd_find(id) : -1;
    if (i >= 0 && nbd->cell[i].kind == NBK_GRAPH && gcell_empty(i) && !strcmp(id, gnew)) {
        nbd_delete(i); /* (made by the graph flow and left without a function; not a cell made elsewhere) */
        nbd_changed();
        i = -1;
    }
    gnew[0] = 0;
    sel = i >= 0 ? 2 * i : -1; /* (the graph cell just made or used: selected, so it is in view) */
    gactive = false;
    hback = 0;
    m2d_home_unscroll();
}

/* Classic layout drag: scroll the lines of the last draw; a selected item
 * whose first line leaves the screen hands the selection to the nearest item
 * starting on screen (-1 if none). */
static bool home_drag(const touch_event_t *ev)
{
    int first = vis_first;
    if (!ui_drag_scroll(ev, UI_DRAG_Y, CH, &first, nlines - ROWS))
        return false;
    hback = nlines - ROWS - first;
    int sl = -1;
    for (int i = 0; i < nlines && sl < 0; i++)
        if (sel >= 0 && lines[i].item == sel)
            sl = i;
    if (sl >= 0 && (sl < first || sl >= first + ROWS)) {
        int s = -1;
        for (int i = first; i < first + ROWS && i < nlines; i++)
            if (lines[i].item >= 0 && (i == 0 || lines[i - 1].item != lines[i].item)) {
                s = lines[i].item;
                if (sl < first)
                    break; /* pushed off the top: the first item on screen */
            }
        sel = s;
    }
    return true;
}

/* Touch: tap a history entry to select it, tap it again to paste it (like
 * ENTER); tap the entry line to move the cursor there; drag to scroll the
 * history (any key returns to the entry line). Graph cells: a gesture that
 * starts on the selected one pans it (drag), resizes it (its bottom edge);
 * a pinch on any graph cell selects and zooms it (app/gcell.c). */
static int gtouch = -1, gtop, gh; /* the graph cell taking the gesture, its block on the screen */
static bool graph_gesture(const touch_event_t *ev)
{
    int item, top, h;
    if (ev->type == TEV_DOWN) {
        gtouch = -1;
        if (sel >= 0 && m2d_home_hit(ev->y, &item, &top, &h) && item == sel && item % 2 == 0 &&
            home_cell_kind(item / 2) == NBK_GRAPH)
            gtouch = item / 2, gtop = top, gh = h;
    } else if (ev->type == TEV_PINCH && gtouch < 0 && m2d_home_hit(ev->y0, &item, &top, &h) && item >= 0 &&
               item % 2 == 0 && home_cell_kind(item / 2) == NBK_GRAPH) {
        gtouch = item / 2, gtop = top, gh = h;
        sel = item;
    }
    if (gtouch < 0 || gtouch >= nhist || ev->type == TEV_TAP)
        return false;
    int i = gtouch;
    if (ev->type == TEV_UP || ev->type == TEV_PINCH_END || ev->type == TEV_CANCEL)
        gtouch = -1;
    if (gcell_touch(i, ev, 0, gtop, LCD_WIDTH, gh))
        home_draw();
    return true;
}

static void home_touch(const touch_event_t *ev)
{
    if (ev->type == TEV_TAP && ev->y < BODY_Y && ev->x >= 80 && ev->x < LCD_WIDTH - 80) {
        home_notebook_menu(); /* (the title: the notebook's name) */
        return;
    }
    if (view2d() && graph_gesture(ev))
        return;
    if (ev->type == TEV_DRAG) {
        if (view2d() ? m2d_home_drag(ev, &sel) : home_drag(ev))
            home_draw();
        return;
    }
    if (ev->type != TEV_TAP || ev->y < BODY_Y)
        return;
    gactive = false;
    if (view2d()) {
        int item = m2d_home_touch(ev, &ed);
        if (item >= 0 && item == sel) {
            home_key(KEY_ENTER, MOD_NONE);
            return;
        }
        sel = item >= 0 ? item : -1;
        home_draw();
        return;
    }
    /* Classic layout: map the tap's row to a line of the last draw */
    int li = vis_first + (ev->y - BODY_Y) / CH;
    if (li >= nlines) {
        if (sel >= 0) {
            sel = -1;
            home_draw();
        }
        return;
    }
    int item = lines[li].item;
    if (item >= 0) {
        if (item == sel)
            home_key(KEY_ENTER, MOD_NONE);
        else {
            sel = item;
            home_draw();
        }
        return;
    }
    sel = -1;
    ui_editor_tap(&ed, (li - vis_ed_first) * COLS + ev->x / CW);
    home_draw();
}

/* Soft-key helpers: insert a command template (text before the cursor, suffix
 * after) or open one of the 2D template menus (app/m2d_edit.c). */
static void ins(const char *a, const char *b)
{
    ed_insert_text(&ed, a, b);
    sel = -1;
    home_draw();
}
static void sk_frac(void) { menu_open(&menu_m2d_frac); }
static void sk_func(void) { menu_open(&menu_m2d_func); }
static void sk_mtrx(void) { menu_open(&menu_m2d_mtrx); }
static void sk_solve(void) { ins("solve(", ",x)"); }

/* SHIFT F3 simplify / SHIFT F4 rewrite: act on the selected history item
 * (remembered in rw_sel, since returning from the REWRITE menu deselects),
 * else the line, else the last result (home_result_action). */
static void sk_simplify(void)
{
    rw_sel = sel;
    home_result_action("simplify", "");
}
/* The argument of a result action, in this order: history item s (the
 * selection when the key was pressed; its input or result text, not an
 * error), the entry line, or Ans (the last result). arg gets the text to
 * wrap, *val the value text that decides which actions fit (for Ans: the
 * last result). Returns 0 if there is nothing to act on (no history, the
 * last result is an error, an entry with →, an error selected), 1 for an
 * item or Ans, 2 for the entry line. */
static int act_src; /* the cell whose result action_arg returned, or -1 */
static int action_arg(int s, char *arg, size_t n, const char **val)
{
    act_src = -1;
    if (s >= 0 && s / 2 < nhist) {
        bool err;
        const char *t = s % 2 ? cell_out(s / 2, &err) : cell_in(s / 2);
        if ((s % 2 && err) || nbd->cell[s / 2].kind != NBK_MATH)
            return 0;
        snprintf(arg, n, "%s", t);
        *val = arg;
        if (s % 2)
            act_src = s / 2;
        return 1;
    }
    if (!ed_empty(&ed)) {
        ed_display(&ed, arg, n);
        *val = arg;
        return strstr(arg, "→") ? 0 : 2;
    }
    int l = last_math();
    bool err;
    if (l < 0 || (cell_out(l, &err), err))
        return 0;
    snprintf(arg, n, "Ans");
    *val = cell_out(l, NULL);
    act_src = l;
    return 1;
}

/* ≈ back to exact: when the result of cell act_src came from "approx(X)"
 * (the ≈ key or typed), arg becomes X, so the exact value comes back
 * instead of a fraction near the decimal (exact(2.19722457734) is
 * 831889/378609, not 2*ln(3)). X "Ans" is the result of the math cell
 * before that one. arg is left alone when there is no such source. */
static void approx_source(char *arg, size_t n)
{
    if (act_src < 0)
        return;
    const char *in = cell_in(act_src);
    size_t len = strlen(in);
    if (strncmp(in, "approx(", 7) || len < 9 || in[len - 1] != ')')
        return;
    char x[ED_MAX * 4];
    snprintf(x, sizeof x, "%.*s", (int)(len - 8), in + 7);
    int depth = 0; /* "approx(a)+approx(b)" is not one call */
    for (const char *c = x; *c; c++) {
        depth += *c == '(' ? 1 : *c == ')' ? -1 : 0;
        if (depth < 0 || (*c == ',' && depth == 0))
            return; /* approx(x, digits) keeps exact() of the decimal too */
    }
    if (!strcmp(x, "Ans")) {
        int p = act_src - 1;
        while (p >= 0 && nbd->cell[p].kind != NBK_MATH)
            p--;
        bool err;
        if (p < 0 || (cell_out(p, &err), err))
            return;
        snprintf(arg, n, "%s", cell_out(p, NULL));
    } else {
        snprintf(arg, n, "%s", x);
    }
}

static char act_arg[ED_MAX * 4], act_line[ED_MAX * 4 + 48];

/* SHIFT F4: the REWRITE menu for the current argument (matrix and unit
 * results get their own items, rewrite_open). */
static void sk_rewrite(void)
{
    const char *val;
    rw_sel = sel;
    if (action_arg(sel, act_arg, sizeof act_arg, &val))
        rewrite_open(val);
}

/* Result action (app/result_actions.c): evaluate fn(arg extra) as a new
 * history entry, e.g. "simplify(Ans)", with arg from action_arg (the item
 * selected when the key was pressed, rw_sel; the entry line, which the new
 * entry replaces; or Ans; nothing happens when action_arg finds nothing).
 * fn NULL is approx <-> exact: exact() if the value is a decimal
 * approximation, else approx(). The line is built as linear text and set
 * into the editor (Textbook: as templates), so the history input reads as
 * if it had been typed; an entry line that was not the argument is kept.
 * Redraws. */
static editor_t keep; /* the entry line while a result action uses the editor */
void home_result_action(const char *fn, const char *extra)
{
    const char *val;
    int k = action_arg(rw_sel, act_arg, sizeof act_arg, &val);
    rw_sel = -1;
    if (!k)
        return;
    if (!fn) {
        fn = result_is_approx(val) ? "exact" : "approx";
        if (fn[0] == 'e' && k == 1)
            approx_source(act_arg, sizeof act_arg);
    }
    snprintf(act_line, sizeof act_line, "%s(%s%s)", fn, act_arg, extra ? extra : "");
    keep = ed;
    m2d_set_linear(&ed, act_line);
    sel = -1;
    recall = -1;
    evaluate(false);
    if (k != 2)
        ed = keep;
    if (ui_current() == &scr_home)
        home_draw();
}

/* REWRITE "forms" taking a form: a new history entry "forms(arg)" with
 * the form as its result (evaluated, so Ans is the form; no autosimplify,
 * which could reshape it); an entry line that was the argument is used up. */
static bool forms_line; /* the argument of the open forms browser is the entry line */
static void forms_paste(const char *in, const char *value)
{
    char pretty[HIST_TEXT];
    if (forms_line)
        ed_clear(&ed);
    yvars_sync();
    cas_status st = app_eval(value, pretty, sizeof pretty);
    cas_fresh = true;
    add_history(in, pretty, st != CAS_OK && st != CAS_ERR_OUTPUT_TRUNC);
    sel = recall = -1;
}

/* REWRITE "forms": the alternate forms browser (app/forms_ui.c) for the
 * same argument as the other result actions (action_arg). The command text
 * is built like home_result_action's, "forms(arg)", in the editor (so a
 * Textbook entry line becomes CAS text), then the line is restored: it
 * stays if the browser is left without taking a form. */
void home_result_forms(void)
{
    const char *val;
    int k = action_arg(rw_sel, act_arg, sizeof act_arg, &val);
    rw_sel = -1;
    if (!k)
        return;
    char disp[ED_MAX * 4 + 16], cas[ED_MAX * 4 + 32];
    snprintf(act_line, sizeof act_line, "forms(%s)", act_arg);
    keep = ed;
    m2d_set_linear(&ed, act_line);
    ed_display(&ed, disp, sizeof disp);
    ed_cas(&ed, cas, sizeof cas);
    ed = keep;
    size_t l = strlen(cas);
    if (strncmp(cas, "forms(", 6) || l < 7 || cas[l - 1] != ')')
        return;
    cas[l - 1] = 0;
    forms_line = k == 2;
    sel = -1;
    forms_open(cas + 6, disp, forms_paste);
}

/* REWRITE "convert to…": the entry line becomes convert(arg,|) with the
 * cursor where the unit goes, and the UNITS menu opens so the unit is
 * chosen from it (or typed); ENTER then converts. */
void home_result_convert(void)
{
    const char *val;
    int k = action_arg(rw_sel, act_arg, sizeof act_arg, &val);
    rw_sel = -1;
    if (!k)
        return;
    snprintf(act_line, sizeof act_line, "convert(%s,)", act_arg);
    m2d_set_linear(&ed, act_line);
    if (ed.cur > 0 && ed.cur == ed.len)
        ed.cur--; /* into the hole, before the ")" */
    sel = -1;
    recall = -1;
    units_open();
}

/* ------------------------------------------------------------ graphing into the notebook
 * Graphing started here ends in the notebook (help page "graph-cells"): the graph key (F1: the graph
 * editor, 2D Y= or 3D Z= by its switch), table (SHIFT F1) open the graph screens in the "home flow" (gflow); there the
 * graph key (the "graph" soft key, a ZOOM, the 3D editors' "graph") adds a graph cell of the
 * functions and window to the notebook (or, for the cell the screens are bound to, writes them
 * back) and comes back here with the cell selected. Entering the full graph screen in the flow
 * (trace, calc) makes the cell first and binds the graph app to it, as ENTER on a cell does; HOME
 * then writes the app's state back into the cell. The flow ends on any other screen. */

static bool graph_screen(const screen_t *s)
{
    return s == &scr_home || s == &scr_yeq || s == &scr_window || s == &scr_tblset || s == &scr_table ||
           s == &scr_graph || s == &scr_menu || s == &scr_msg || s == &scr_help || s == &scr_mode ||
           s == &scr_g3d || s == &scr_g3d_eq || s == &scr_g3d_win || s == &scr_slider;
}
/* (ui_switch) the flow ends where graphing does not go on */
void home_graph_flow_check(const screen_t *s)
{
    if (gflow && !graph_screen(s))
        gflow = false;
}

/* a new graph cell (2D: the Y= functions; 3D: the 3D graph's) after the last cell; NULL: the
 * notebook is full (a toast), or (!empty_ok) there is no function to graph (*none) */
static nb_cell_t *new_graph(bool d3, bool empty_ok, bool *none)
{
    nb_cell_t *c = d3 ? gcell_new3d(nhist) : gcell_new(nhist);
    *none = false;
    if (!c) {
        ui_toast("the notebook is full");
        return NULL;
    }
    if (!empty_ok && gcell_empty(nhist - 1)) {
        nbd_delete(nhist - 1);
        *none = true;
        return NULL;
    }
    snprintf(gnew, sizeof gnew, "%s", c->id);
    nbd_changed();
    return c;
}

/* the graph key of the graph screens: in the home flow the graph goes into the notebook (a new
 * cell, or the bound cell written back) and the home screen shows it selected; false: not in the
 * flow (the caller shows its graph screen) */
bool home_graph_embed(bool d3)
{
    if (!gflow || (!d3 && gx.mode != GM_FUNC)) /* (graph cells draw y = f(x)) */
        return false;
    if (!gcell_bound()[0]) {
        bool none;
        nb_cell_t *c = new_graph(d3, false, &none);
        if (none)
            return false; /* (nothing to graph: the graph screen, empty) */
        if (c)
            snprintf(gsel, sizeof gsel, "%s", c->id);
    }
    ui_switch(&scr_home);
    return true;
}

/* the full graph screen (2D or 3D) entered: in the home flow without a cell, the cell is made and
 * the graph app bound to it (HOME writes the app's state back) */
void home_graph_enter(bool d3)
{
    if (!gflow || (!d3 && gx.mode != GM_FUNC))
        return;
    gflow = false;
    if (gcell_bound()[0])
        return;
    bool none;
    nb_cell_t *c = new_graph(d3, false, &none);
    if (c)
        graph_bind((int)(c - nbd->cell));
}

/* SHIFT F5 graph, the console / CATALOG graph and 3D graph and MENU Insert 3D graph from here: a graph cell of the
 * current functions after the last cell, selected (with no function yet: the function editor opens
 * bound to the new cell; its graph key or HOME comes back with it) */
void home_graph_new(bool d3)
{
    if (!d3 && gx.mode != GM_FUNC) { /* (graph cells draw y = f(x): the other modes graph in the app) */
        ui_switch(&scr_graph);
        return;
    }
    bool none;
    nb_cell_t *c = new_graph(d3, true, &none);
    if (!c)
        return;
    int i = (int)(c - nbd->cell);
    if (gcell_empty(i)) {
        graph_bind(i);
        ui_switch(d3 ? &scr_g3d_eq : &scr_yeq);
        gflow = true;
        return;
    }
    if (ui_current() != &scr_home) {
        snprintf(gsel, sizeof gsel, "%s", c->id);
        ui_switch(&scr_home);
        return;
    }
    sel = 2 * i;
    gactive = false;
    home_draw();
}
static void sk_graph(void) { home_graph_new(false); }
/* graph (F1): the graph editor, 2D (Y=; its header and F1 switch to 3D, the Z= editor), and table:
 * the graph screens in the home flow */
static void sk_yeq(void)
{
    ui_switch(&scr_yeq);
    gflow = true;
}
static void sk_table(void)
{
    ui_switch(&scr_table);
    gflow = true;
}

static const menu_def_t menu_nb;
static void sk_input(void) { sel_insert(0); }
static void sk_result(void) { sel_insert(1); }
static void sk_ref(void) { sel_insert(2); }
static void sk_edit(void)
{
    if (sel >= 0)
        edit_in_place(sel / 2);
}
/* F5 "≈" has no action: it reaches home_key() as KEY_F5. */
static const softkeys_t home_soft = {
    {{"graph", sk_yeq}, {"frac", sk_frac}, {"func", sk_func}, {"mtrx", sk_mtrx}, {"≈"}},
    {{"table", sk_table}, {"solve", sk_solve}, {"simplify", sk_simplify}, {"rewrite", sk_rewrite},
     {"plot", sk_graph}},
    &menu_nb, /* MENU: the notebook menu (its bar: the edit actions, as in every context menu) */
};
/* An animated graph cell selected: F5 plays it (pause while it plays: any key pauses it) */
static void sk_play(void)
{
    if (sel >= 0)
        anim_cell_play(sel / 2);
}
static const softkeys_t sel_anim_soft[2] = {
    {{{"graph", sk_yeq}, {"frac", sk_frac}, {"func", sk_func}, {"mtrx", sk_mtrx}, {"play", sk_play}},
     {{"table", sk_table}, {"solve", sk_solve}, {"simplify", sk_simplify}, {"rewrite", sk_rewrite},
      {"plot", sk_graph}},
     &menu_nb},
    {{{"graph", sk_yeq}, {"frac", sk_frac}, {"func", sk_func}, {"mtrx", sk_mtrx}, {"pause", sk_play}},
     {{"table", sk_table}, {"solve", sk_solve}, {"simplify", sk_simplify}, {"rewrite", sk_rewrite},
      {"plot", sk_graph}},
     &menu_nb},
};

/* With a calculation or a Python cell selected: what of it goes into the entry line (ENTER: the
 * selected input or result, as ever; SHIFT ENTER: the result). */
static char ref_label[16]; /* "Out[n]" of the selected cell */
static const softkeys_t sel_math_soft = {
    {{"input", sk_input}, {"result", sk_result}, {ref_label, sk_ref}, {"edit", sk_edit}, {"≈"}},
    {{"table", sk_table}, {"solve", sk_solve}, {"simplify", sk_simplify}, {"rewrite", sk_rewrite},
     {"plot", sk_graph}},
    &menu_nb,
};
static const softkeys_t sel_py_soft = {
    {{"edit", sk_edit}, {"result", sk_result}, {ref_label, sk_ref}, {NULL}, {NULL}},
    {{"table", sk_table}, {"solve", sk_solve}, {"simplify", sk_simplify}, {"rewrite", sk_rewrite},
     {"plot", sk_graph}},
    &menu_nb,
};
static const softkeys_t *home_softkeys(void)
{
    int k = sel >= 0 ? home_cell_kind(sel / 2) : -1;
    if (k == NBK_GRAPH && gcell_animated(sel / 2))
        return &sel_anim_soft[anim_playing() && gcell_anim_cell() == sel / 2];
    if (k != NBK_MATH && k != NBK_PY)
        return &home_soft;
    const nb_cell_t *c = &nbd->cell[sel / 2];
    char t[sizeof ref_label];
    if (c->count > 0 && !c->f[NBF_ENAME] && c->f[NBF_VAL])
        snprintf(t, sizeof t, "Out[%d]", c->count);
    else
        snprintf(t, sizeof t, "Out[ ]");
    if (strcmp(t, ref_label)) { /* (the bar compares its labels by pointer: drawn again) */
        snprintf(ref_label, sizeof ref_label, "%s", t);
        ui_softkeys_invalidate();
    }
    return k == NBK_MATH ? &sel_math_soft : &sel_py_soft;
}

static char home_title[NB_NAME + 24]; /* (the status bar title, below) */
const screen_t scr_home = {"home", home_title, home_enter, home_draw, home_key, home_tick, home_editor, home_touch,
                           "home-screen", &home_soft, home_softkeys};

/* Empty history and entry line (MEM > Clear home history, Reset all, and
 * before loading a saved session): a new unsaved notebook (a named one is saved first). */
void home_reset(void)
{
    if (nbd->open)
        py_nb_flush();
    nbd_scratch();
    gcell_set_bound("");
    ed_clear(&ed);
    set_edit(-1);
    sel = recall = edit_cell = -1;
    emode = EM_MATH;
    gactive = false;
}

/* ------------------------------------------------------------ the notebook menu */
/* HOME on the home screen (app/keys.c): NOTEBOOK, what the home screen's notebook can do besides
 * calculations. Saving, opening and exporting need the Python module (the .ipynb files and the
 * notebook store are its); everything else is the core's. */
enum { NM_SAVE = 1, NM_SAVEAS, NM_OPEN, NM_NEW, NM_RENAME, NM_EXPORT, NM_TEXT, NM_PY, NM_RUNALL, NM_EDIT,
       NM_RERUN, NM_DELETE, NM_3D };
static int menu_cell = -1; /* the cell selected when the menu opened (else the last one) */

static bool have_py(void)
{
    if (py_available())
        return true;
    ui_message("NOTEBOOK", "Saving, opening and exporting notebooks and Python cells need the Python module.");
    return false;
}

/* the result of a module command for the core: a toast, or its reason in a message */
static void py_result(int r, const char *ok)
{
    if (r == -1 && !nbd->msg[0]) {
        have_py();
        return;
    }
    if (r)
        ui_message("NOTEBOOK", nbd->msg[0] ? nbd->msg : "The module is busy: try again.");
    else if (ok)
        ui_toast(ok);
}

static void run_python(int i)
{
    nbd->msg[0] = 0;
    int r = py_nb_call("runcell", nbd->cell[i].id);
    if (r == -1 && !nbd->msg[0]) {
        nb_cell_t *c = &nbd->cell[i];
        nbd_clear_out(c);
        nbd_set(c, NBF_ENAME, "ModuleError");
        nbd_set(c, NBF_EVALUE, "Python cells need the Python module");
    }
    nbd->gen++;
}

/* the name prompt (Save as, Rename): a line to type the name in, ENTER takes it */
static editor_t name_ed;
static int name_for; /* NM_SAVEAS / NM_RENAME */
static void name_draw(void)
{
    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, C_WHITE);
    gfx_text(FONT, 0, BODY_Y + 4, name_for == NM_RENAME ? "New name:" : "Save the notebook as:", C_BLACK,
             GFX_TRANSPARENT);
    ui_draw_editor_line(&name_ed, 0, BODY_Y + 4 + 2 * CH, COLS, C_BLACK, C_WHITE, true);
    gfx_text(&font_small, 4, BODY_Y + 4 + 4 * CH, "ENTER: save   CLEAR: cancel", C_GRAY, GFX_TRANSPARENT);
}
static void name_key(int key, int mod)
{
    if (key == KEY_CLEAR && ed_empty(&name_ed)) {
        ui_switch(&scr_home);
        return;
    }
    if (key == KEY_CLEAR) {
        ed_clear(&name_ed);
    } else if (key == KEY_ENTER) {
        char n[NB_NAME * 2];
        ed_display(&name_ed, n, sizeof n);
        ui_switch(&scr_home);
        nbd->msg[0] = 0;
        if (name_for == NM_RENAME) {
            char cmd[NB_NAME * 3];
            snprintf(cmd, sizeof cmd, "%s", n);
            py_result(py_nb_call("renameto", cmd), "renamed");
        } else {
            py_result(py_nb_call("saveas", n), "saved");
        }
        return;
    } else if (!ui_editor_key(&name_ed, key, mod)) {
        return;
    }
    name_draw();
}
static editor_t *name_editor(void) { return &name_ed; }
const screen_t scr_nbname = {"nbname", "NOTEBOOK", NULL, name_draw, name_key, NULL, name_editor, NULL,
                             "notebook-menu", NULL};
static void ask_name(int what)
{
    name_for = what;
    ed_set_text(&name_ed, what == NM_RENAME ? nbd->name : "");
    ui_switch(&scr_nbname);
}

/* A cell to change: a graph cell opens the graph app on it; a Python cell (and a text cell of
 * several lines) the module's editor of several lines (ENTER there starts a new line, F5 done, back
 * here); a calculation or a one-line text the entry line, marked "edit". */
static void edit_in_place(int i)
{
    if (i < 0 || i >= nhist)
        return;
    nb_cell_t *c = &nbd->cell[i];
    if (c->kind == NBK_GRAPH) {
        if (graph_bind(i))
            ui_switch(gcell_is3d(i) ? &scr_g3d : &scr_graph);
        return;
    }
    if (c->kind == NBK_PY || (c->kind == NBK_TEXT && strchr(cell_in(i), '\n'))) {
        nbd->msg[0] = 0;
        char id[sizeof c->id];
        snprintf(id, sizeof id, "%s", c->id);
        int r = -1;
        if (py_available()) {
            sel = -1;
            app_open_notebook(); /* (first: entering the screen starts its entry line afresh) */
            r = py_nb_call("editcell", id);
            if (!r)
                return;
            ui_switch(&scr_home);
        }
        if (nbd->msg[0] || c->kind == NBK_PY) { /* (refused, or no module: Python needs it) */
            py_result(r, NULL);
            return;
        }
    }
    if (!set_edit(i))
        return;
    emode = c->kind == NBK_PY ? EM_PY : c->kind == NBK_TEXT ? EM_TEXT : EM_MATH;
    if (c->kind == NBK_MATH)
        m2d_set_linear(&ed, cell_in(i));
    else
        ed_set_text(&ed, cell_in(i));
    sel = -1;
    if (ui_current() == &scr_home)
        home_draw();
}

static void run_all(void)
{
    for (int i = 0; i < nhist; i++) {
        nb_cell_t *c = &nbd->cell[i];
        if (c->kind == NBK_MATH)
            rerun_math(i);
        else if (c->kind == NBK_PY)
            run_python(i);
    }
    gcell_invalidate();
    nbd_changed();
}

static void new_notebook(void)
{
    home_reset();
    nbd_changed();
}

static void nm_select(const menu_item_t *it)
{
    int i = menu_cell;
    menu_close();
    nbd->msg[0] = 0;
    switch (it->action) {
    case NM_SAVE:
        if (!have_py())
            break;
        if (!nbd->open)
            ask_name(NM_SAVEAS);
        else
            py_result(py_nb_call("savenow", NULL), "saved");
        break;
    case NM_SAVEAS:
        if (have_py())
            ask_name(NM_SAVEAS);
        break;
    case NM_RENAME:
        if (!have_py())
            break;
        if (!nbd->open)
            ask_name(NM_SAVEAS);
        else
            ask_name(NM_RENAME);
        break;
    case NM_OPEN:
        if (have_py()) {
            py_nb_call("listview", NULL);
            app_open_notebook();
        }
        break;
    case NM_NEW:
        if (!nbd->open && nhist)
            ui_confirm("Clear the unsaved notebook?", new_notebook);
        else
            new_notebook();
        break;
    case NM_EXPORT:
        if (!have_py())
            break;
        if (!nbd->open)
            ui_message("EXPORT", "Save the notebook first (HOME, Save): then it is exported to the card.");
        else
            py_result(py_nb_call("export", NULL), "exported to the card");
        break;
    case NM_TEXT:
    case NM_PY:
        if (it->action == NM_PY && !have_py())
            break;
        emode = it->action == NM_PY ? EM_PY : EM_TEXT;
        set_edit(-1);
        ed_clear(&ed);
        ui_toast(emode == EM_PY ? "Python cell: type, ENTER runs" : "text cell: type, ENTER adds it");
        break;
    case NM_RUNALL:
        run_all();
        break;
    case NM_EDIT:
        edit_in_place(i);
        break;
    case NM_RERUN:
        if (i >= 0 && i < nhist) {
            if (nbd->cell[i].kind == NBK_MATH)
                rerun_math(i);
            else if (nbd->cell[i].kind == NBK_PY)
                run_python(i);
            nbd_changed();
        }
        break;
    case NM_DELETE:
        if (i >= 0 && i < nhist) {
            nbd_delete(i);
            nbd_changed();
        }
        break;
    case NM_3D: /* a 3D graph cell of the 3D graph's surfaces (none yet: their editor, bound to the cell) */
        home_graph_new(true);
        return;
    }
    if (ui_current() == &scr_home)
        home_draw();
}

static const menu_item_t nm_items[] = {
    {"Save", NULL, NULL, NM_SAVE, "notebook-menu"},
    {"Save as...", NULL, NULL, NM_SAVEAS, "notebook-menu"},
    {"Open...", NULL, NULL, NM_OPEN, "notebook-menu"},
    {"New", NULL, NULL, NM_NEW, "notebook-menu"},
    {"Rename...", NULL, NULL, NM_RENAME, "notebook-menu"},
    {"Export to the card", NULL, NULL, NM_EXPORT, "notebook-menu"},
    {"Insert text cell", NULL, NULL, NM_TEXT, "notebook-menu"},
    {"Insert Python cell", NULL, NULL, NM_PY, "notebook-menu"},
    {"Run all", NULL, NULL, NM_RUNALL, "notebook-menu"},
    {"Edit in place", NULL, NULL, NM_EDIT, "notebook-menu"},
    {"Re-run", NULL, NULL, NM_RERUN, "notebook-menu"},
    {"Delete cell", NULL, NULL, NM_DELETE, "notebook-menu"},
    {"Insert 3D graph", NULL, NULL, NM_3D, "graph-cells"},
};
static char nm_title[NB_NAME + 16];
/* the status bar's title on the home screen: the notebook, "Notebook: Unsaved" or "Notebook: *Lab 1"
 * (a star while a named one has changes not yet stored; the name alone, cut, when it is long), with
 * a "▼": MENU (or a tap on the title) opens the notebook menu */
static char home_title[NB_NAME + 24], title_text[NB_NAME + 24];
static void title_update(void)
{
    char t[sizeof title_text], n[NB_NAME + 4];
    snprintf(n, sizeof n, "%s%s", nbd->open && nbd->dirty ? "*" : "", nbd->open ? nbd->name : "Unsaved");
    snprintf(t, sizeof t, "Notebook: %s", n);
    if (gfx_text_width(&font_small, t) > 132) { /* (between the modes and the badges) */
        snprintf(t, sizeof t, "%s", n);
        while (strlen(t) > 4 && gfx_text_width(&font_small, t) > 120) {
            size_t k = strlen(t) - 1;
            while (k && ((unsigned char)t[k] & 0xC0) == 0x80)
                k--;
            t[k] = 0;
        }
        if (strcmp(t, n))
            snprintf(t + strlen(t), sizeof t - strlen(t), "...");
    }
    if (strcmp(t, title_text)) {
        snprintf(title_text, sizeof title_text, "%s", t);
        snprintf(home_title, sizeof home_title, "%s \u25BC", t);
        if (ui_current() == &scr_home)
            ui_draw_status();
    }
}
/* a dynamic menu so that opening it (MENU: softkeys_t.menu) notes the cell and the title */
static void nm_set_title(void)
{
    snprintf(nm_title, sizeof nm_title, "%s%s", nbd->open && nbd->dirty ? "* " : "",
             nbd->open ? nbd->name : "NOTEBOOK (not saved)");
    title_update();
}
static int nm_count(void)
{
    menu_cell = sel >= 0 ? sel / 2 : nhist - 1;
    nm_set_title();
    return (int)(sizeof nm_items / sizeof nm_items[0]);
}
static void nm_get(int i, menu_item_t *out) { *out = nm_items[i]; }
static const menu_def_t menu_nb = {nm_title, NULL, 0, nm_select, false, "notebook-menu", nm_count, nm_get};

void home_notebook_menu(void) { menu_open(&menu_nb); }

/* ------------------------------------------------------------ persistence */
/* "home" blob, version 2: the notebook (app/nbdoc.h) and the entry line.
 *   u8 flags (1 named, 2 unsaved changes), u8 where, str name, u16 counter, str bound graph cell
 *   u16 cells, per cell: u8 kind, u8 flags, u16 count, str id, f64 xmin, f64 xmax,
 *       u8 NBF_N, NBF_N fields (u16 length + bytes; each cut to field_cap())
 *   str the entry line (ed_serialize: tokens and 2D templates survive), u8 entry mode, u16 edited cell + 1
 * At most HOME_BUDGET bytes of cells: the newest that fit (a named notebook is complete in the
 * store; an unsaved one keeps its newest calculations, like the history did).
 * Version 1 (the history): u16 items, per item input, output, u8 error; the entry line. */
#define HOME_BUDGET (60u << 10)
static size_t field_cap(int f) { return f == NBF_SRC || f == NBF_GRAPH ? 4000 : f == NBF_TB ? 1000 : 2000; }
static size_t field_len(const char *s, size_t cap)
{
    size_t n = s ? strlen(s) : 0;
    if (n > cap) {
        n = cap;
        while (n && ((unsigned char)s[n] & 0xC0) == 0x80) /* (not inside a UTF-8 sequence) */
            n--;
    }
    return n;
}
static size_t cell_bytes(const nb_cell_t *c)
{
    size_t n = 1 + 1 + 2 + 2 + strlen(c->id) + 16 + 1;
    for (int f = 0; f < NBF_N; f++)
        n += 2 + field_len(c->f[f], field_cap(f));
    return n;
}

static void home_save(pw_t *w)
{
    char b[ED_MAX * 8];
    pw_u8(w, (nbd->open ? 1u : 0u) | (nbd->dirty ? 2u : 0u));
    pw_u8(w, (uint32_t)nbd->where);
    pw_str(w, nbd->name);
    pw_u16(w, (uint32_t)nbd->counter);
    pw_str(w, gcell_bound());
    int from = nhist;
    size_t total = 0;
    while (from > 0 && total + cell_bytes(&nbd->cell[from - 1]) <= HOME_BUDGET)
        total += cell_bytes(&nbd->cell[--from]);
    pw_u16(w, (uint32_t)(nhist - from));
    for (int i = from; i < nhist; i++) {
        const nb_cell_t *c = &nbd->cell[i];
        pw_u8(w, c->kind);
        pw_u8(w, c->flags);
        pw_u16(w, (uint16_t)c->count);
        pw_str(w, c->id);
        pw_f64(w, c->xmin);
        pw_f64(w, c->xmax);
        pw_u8(w, NBF_N);
        for (int f = 0; f < NBF_N; f++) {
            size_t n = field_len(c->f[f], field_cap(f));
            pw_u16(w, (uint32_t)n);
            pw_bytes(w, c->f[f], n);
        }
    }
    ed_serialize(&ed, b, sizeof b);
    pw_str(w, b);
    pw_u8(w, (uint32_t)emode);
    pw_u16(w, (uint32_t)(edit_cell + 1));
}

/* Restore the "home" blob; a truncated blob keeps the cells read so far. */
static void home_load(pr_t *r, unsigned version)
{
    char b[ED_MAX * 8];
    home_reset();
    if (version < 2) {
        unsigned n = pr_u16(r);
        for (unsigned i = 0; i < n && !r->err; i++) {
            char in[HIST_TEXT], out[HIST_TEXT];
            pr_str(r, in, sizeof in);
            pr_str(r, out, sizeof out);
            bool err = pr_u8(r) != 0;
            if (!r->err)
                add_history(in, out, err);
        }
    } else {
        unsigned fl = pr_u8(r);
        nbd->where = (int)pr_u8(r);
        pr_str(r, nbd->name, sizeof nbd->name);
        nbd->counter = (int)pr_u16(r);
        pr_str(r, b, sizeof b);
        gcell_set_bound(b);
        nbd->open = (fl & 1) && nbd->name[0];
        nbd->dirty = (fl & 2) != 0;
        unsigned n = pr_u16(r);
        for (unsigned i = 0; i < n && !r->err; i++) {
            unsigned kind = pr_u8(r), flags = pr_u8(r), count = pr_u16(r);
            char id[24];
            pr_str(r, id, sizeof id);
            double x0 = pr_f64(r), x1 = pr_f64(r);
            unsigned nf = pr_u8(r);
            if (r->err || kind >= NBK_N)
                break;
            nb_cell_t *c = nbd_insert(nhist, (int)kind);
            if (!c)
                break;
            if (id[0] && nbd_find(id) < 0)
                snprintf(c->id, sizeof c->id, "%s", id);
            c->flags = (uint8_t)flags, c->count = (int16_t)count, c->xmin = x0, c->xmax = x1;
            for (unsigned f = 0; f < nf && !r->err; f++) {
                unsigned len = pr_u16(r);
                char *t = len && f < NBF_N ? nbd_alloc(len + 1) : NULL;
                if (t) { /* (straight into the arena) */
                    if (!pr_bytes(r, t, len)) {
                        nbd_free(t);
                        break;
                    }
                    t[len] = 0;
                    c->f[f] = t;
                } else {
                    char skip[64];
                    for (unsigned k = 0; k < len && !r->err; k += sizeof skip)
                        pr_bytes(r, skip, len - k < sizeof skip ? len - k : sizeof skip);
                }
            }
        }
    }
    pr_str(r, b, sizeof b);
    if (!r->err)
        ed_deserialize(&ed, b);
    if (version >= 2) {
        unsigned m = pr_u8(r), e = pr_u16(r);
        if (!r->err && m <= EM_PY && (int)e <= nhist)
            emode = (int)m, edit_cell = (int)e - 1;
    }
    nbd->gen++;
}

const persist_def_t persist_home = {"home", 2, home_save, home_load, home_reset};

/* Console `state` keys of the home screen (UI tests): entry text and CAS text,
 * cursor column, history size, last item, selection, plus the 2D view's keys,
 * the notebook (name, cells, entry mode) and the selected graph cell. */
void home_state(void (*kv)(const char *, const char *, void *), void *user)
{
    char b[ED_MAX * 4 + 8];
    ed_display(&ed, b, sizeof b);
    kv("entry", b, user);
    ed_cas(&ed, b, sizeof b);
    kv("entry_cas", b, user);
    snprintf(b, sizeof b, "%d", ed_cursor_col(&ed));
    kv("cursor", b, user);
    snprintf(b, sizeof b, "%d", nhist);
    kv("history", b, user);
    bool err = false;
    int l = nhist - 1;
    kv("last_input", nhist ? cell_in(l) : "", user);
    kv("last_output", nhist ? cell_out(l, &err) : "", user);
    kv("last_error", err ? "1" : "0", user);
    snprintf(b, sizeof b, "%d", sel);
    kv("home_sel", b, user);
    kv("home_nb", nbd->open ? nbd->name : "", user);
    nm_set_title();
    kv("home_title", title_text, user);
    kv("home_dirty", nbd->dirty ? "1" : "0", user);
    static const char *const kinds[NBK_N] = {"text", "math", "python", "graph"};
    kv("home_kind", nhist ? kinds[nbd->cell[sel >= 0 ? sel / 2 : l].kind % NBK_N] : "", user);
    static const char *const modes[] = {"math", "text", "python"};
    kv("home_mode", modes[emode], user);
    snprintf(b, sizeof b, "%d", edit_cell);
    kv("home_edit", b, user);
    kv("home_gactive", home_graph_active() ? "1" : "0", user);
    kv("home_bound", gcell_bound(), user);
    int gi = sel >= 0 && home_cell_kind(sel / 2) == NBK_GRAPH ? sel / 2 : -1;
    for (int i = nhist - 1; gi < 0 && i >= 0; i--)
        if (nbd->cell[i].kind == NBK_GRAPH)
            gi = i;
    if (gi >= 0)
        gcell_state(gi, kv, user);
    m2d_home_state(kv, user);
}

/* History access for app/m2d_home.c (declared in m2d.h). */
int home_hist_count(void) { return nhist; }

const char *home_hist_text(int i, bool output, bool *err)
{
    if (err)
        *err = false;
    if (i < 0 || i >= nhist)
        return "";
    return output ? cell_out(i, err) : cell_in(i);
}

/* Text of the selected history item (input, or a result that is not an
 * error) for copying (app/input_clip.c); false if nothing is selected. */
bool home_selection_text(char *out, size_t n)
{
    if (sel < 0 || sel / 2 >= nhist)
        return false;
    bool err;
    const char *t = sel % 2 ? cell_out(sel / 2, &err) : cell_in(sel / 2);
    if ((sel % 2 && err) || nbd->cell[sel / 2].kind == NBK_GRAPH)
        return false;
    snprintf(out, n, "%s", t);
    return true;
}
