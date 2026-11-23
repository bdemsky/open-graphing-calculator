/*
 * Internal interface shared by the app's screens: display geometry, the
 * screen, soft-key and menu types, the global application state `g`, and the
 * services every screen uses (screen switching, status bar, cursor, editors,
 * menus, help, number formatting, evaluation).
 *
 * Display geometry (320x240 landscape):
 *   y 0..17     status bar (STATUS_H), drawn by ui_draw_status() only
 *   y 18..223   body (BODY_Y .. UI_BOTTOM, BODY_H = 206 px): the screen's area
 *   y 224..239  soft-key bar (SOFT_Y, SOFT_H), drawn by app/softkeys.c only
 * In the 10x20 FONT the body is 32 columns by 10 rows.
 *
 * Everything here runs in the main-loop context (no locking). How to add
 * screens, menus and the other extension points: app/EXTENDING.md.
 */
#ifndef UI_H
#define UI_H
#include <stdbool.h>
#include <stdint.h>
#include "cas.h"
#include "editor.h"
#include "gfx.h"
#include "keyq.h"
#include "touch.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define STATUS_H 18
#define SOFT_H 16                   /* soft-key bar at the bottom of the display */
#define SOFT_Y (LCD_HEIGHT - SOFT_H)
#define UI_BOTTOM SOFT_Y            /* screens draw in [BODY_Y, UI_BOTTOM) */
#define BODY_Y STATUS_H
#define BODY_H (UI_BOTTOM - STATUS_H)
#define FONT (&font_large)
#define CW 10 /* large font cell */
#define CH 20
#define COLS (LCD_WIDTH / CW)

/* Modifier passed with a key to screen_t.key(): the layer it was pressed in.
 * Keys without an ALPHA letter never get MOD_ALPHA (app.c handle_key). */
enum { MOD_NONE = 0, MOD_SHIFT = 1, MOD_ALPHA = 2 };

struct screen;
struct menu_def;

/* Soft keys (app/softkeys.c): F1..F5 under the display. Every screen labels
 * them in the soft-key bar at the bottom of the display; while SHIFT is
 * pending the bar shows the SHIFT F1..F5 labels. Tapping a label presses the
 * key. MENU opens `menu`, or (NULL) a menu listing these soft keys. */
typedef struct softkey {
    const char *label;           /* bar label (<= 9 chars); NULL: unused (the key does nothing) */
    void (*fn)(void);            /* action, or NULL: */
    const struct screen *screen; /*   switch to this screen (its label is highlighted on it), or NULL: */
                                 /*   the screen's key() gets KEY_F1..KEY_F5 with the modifier */
    bool (*on)(void); /* optional: true = highlighted as the current choice (base-n input base,
                       * geometry tool), like a `screen` key on its screen; NULL: no state */
} softkey_t;
typedef struct softkeys {
    softkey_t key[5];             /* F1..F5 */
    softkey_t shift[5];           /* SHIFT F1..F5 */
    const struct menu_def *menu;  /* MENU key: context menu (NULL: generated from the soft keys) */
} softkeys_t;

/* A screen: a full-body UI page. Definitions are const objects (scr_*) and
 * the screen keeps its state in its own module statics. ui_switch() calls
 * enter() then draw(); afterwards key(), touch() and tick() are called while
 * it is current. All callbacks except draw may be NULL. */
typedef struct screen {
    const char *name;  /* id for the console (`state screen`, `expect screen`) */
    const char *title; /* centred in the status bar (NULL: none) */
    void (*enter)(void);           /* on every switch to it (check ui_returning) */
    void (*draw)(void);            /* redraw the body */
    void (*key)(int key, int mod); /* keys not taken by the soft keys or global actions */
    void (*tick)(uint32_t now);    /* every main-loop pass while current (ms clock) */
    editor_t *(*editor)(void); /* where menu selections are inserted (NULL: none) */
    void (*touch)(const touch_event_t *ev); /* touch gestures (NULL: ignored) */
    const char *help; /* help topic id for SHIFT HELP on this screen (NULL: topic named like `name`) */
    const softkeys_t *softkeys;             /* soft-key bar (NULL: empty bar) */
    const softkeys_t *(*softkeys_fn)(void); /* or computed from the screen's state (overrides) */
    uint8_t style; /* SCREEN_*: how the core draws the status and soft-key bars around it */
} screen_t;
/* SCREEN_GAME: a game owns the whole display (Math Quest): the status bar shows no calculator
 * state (angle mode, SHIFT/ALPHA, title), only toasts and a small battery icon when it is low;
 * the soft-key bar is drawn as pastel game buttons. */
enum { SCREEN_CALC = 0, SCREEN_GAME = 1 };
#define GAME_BAR_BG 0xFF9E   /* RGB(255, 240, 246) */
#define GAME_BAR_INK 0x59EB  /* RGB(90, 61, 92) */
#define GAME_BAR_PILL 0xFDDB /* RGB(255, 186, 216) */

extern const screen_t scr_home, scr_mode, scr_yeq, scr_window, scr_tblset, scr_graph, scr_table, scr_menu,
    scr_msg, scr_help;

/* The Y= functions Y1..Y6 of FUNC mode (other graph modes keep their entries
 * in app/graph.h). Each is an editor holding the expression text plus the
 * compiled form used by the graph and table. */
#define NFUNC 6
typedef struct {
    editor_t ed;   /* expression as typed (saved with the session as text) */
    bool enabled;  /* the `=` toggle: plotted / shown in the table */
    uint8_t color; /* index into func_colors */
    cas_fn *fn;    /* compiled, NULL if not compiled or error */
    bool compiled; /* compilation was attempted since the last change */
    char err[48];  /* compile error message, "" if none */
} func_t;

/* number display (MODE): Normal / SCI / ENG, FLOAT or a fixed number of decimals */
enum { FMT_NORMAL = 0, FMT_SCI, FMT_ENG };
enum { CPLX_REAL = 0, CPLX_RECT, CPLX_POLAR }; /* complex results: error, a+bi, r∠θ */

/* The application state shared by all screens (one global, `g`, defined in
 * app.c and initialised by app_init()). The MODE settings, Y= functions,
 * WINDOW/TBLSET values and backlight are saved with the session by
 * persist_settings (mode.c) and persist_graph (graph_state.c). */
typedef struct {
    cas_ctx *cas;     /* the one CAS instance (heap = the rest of the arena) */
    cas_settings cs;  /* MODE angle / exact / digits as the user set them */
    uint8_t fmt;   /* FMT_* */
    int8_t fix;    /* decimals 0..9, or -1 = FLOAT */
    uint8_t cplx;  /* CPLX_* */
    bool grid, axes;  /* graph format settings */
    func_t f[NFUNC];  /* Y1..Y6 */
    double xmin, xmax, xscl, ymin, ymax, yscl; /* WINDOW */
    double tblstart, tbldelta;                 /* TBLSET */
    bool graph_valid; /* graph cache is up to date */
    uint16_t *graph_cache; /* full-screen buffer */
    int backlight;    /* percent */
    bool usb_awake;   /* on USB power, idle only darkens the screen (SETTINGS ON USB) */
    /* shift state */
    int shift;  /* 1 = SHIFT pending */
    int alpha;  /* 0 none, 1 lower once, 2 upper once, 3 lower lock, 4 upper lock */
    int alpha_snapshot_for_key; /* alpha state when the current key was pressed */
} app_t;
extern app_t g;
/* Y= colours (NCOLORS RGB565 values) and their names for `state` */
extern const uint16_t func_colors[];
extern const char *const func_color_names[];
#define NCOLORS 8

/* screen management (app.c) */
/* Make s current: enter(), draw(), status and soft-key bars. */
void ui_switch(const screen_t *s);
/* The home screen (QUIT, CLEAR from screens, after power-on). An app such as
 * RPN mode may replace it with ui_set_home(); NULL restores scr_home. */
const screen_t *ui_home(void);
void ui_set_home(const screen_t *s);
/* true while switching back to a screen after a menu/message (don't reset its state) */
extern bool ui_returning;
const screen_t *ui_current(void);
const screen_t *ui_previous(void); /* last screen before the current one, menus/messages skipped */
void ui_draw_status(void);
void ui_message(const char *title, const char *text); /* modal message, any key or a tap returns */
/* Short non-modal notice ("copied", "nothing to undo") in the middle of the
 * status bar, shown until the next key or tap (<= 38 bytes; copied). Use it
 * as feedback for actions that change nothing visible; ui_message for what
 * the user must read. Console: `state toast`. */
void ui_toast(const char *text);
/* Ask before a destructive action: a two-item menu titled `question`
 * ("No" first, so ENTER or a double key press is safe; CLEAR or F5 no says no, F1 yes says yes).
 * Yes runs on_yes() after returning to the screen the question was asked
 * on. `question` must stay valid (a literal). */
void ui_confirm(const char *question, void (*on_yes)(void));
/* Move an editor's cursor to display column `col` (touch). */
void ui_editor_tap(editor_t *e, int col);

/* Drag to scroll (app/touch_scroll.c), shared by every scrolling screen so
 * they all behave alike: the content follows the finger (finger up / left
 * shows later rows / columns), one row or column per `step` px of movement.
 * The movement is accumulated per axis between events and restarted on every
 * TEV_DOWN (by app_touch_event); movement past either end is dropped, so
 * reversing scrolls back at once. A drag never ends in a TEV_TAP (core/touch.c),
 * so screens that act on taps only never act on a drag. */
enum { UI_DRAG_X, UI_DRAG_Y };
void ui_drag_reset(void);
/* Whole steps the drag event ev adds up to along `axis`, clamped to [lo, hi]
 * (lo <= 0 <= hi); positive = later content. 0 for other events. */
int ui_drag_steps(const touch_event_t *ev, int axis, int step, int lo, int hi);
/* Scroll *top (first visible row/column, within [0, max_top]) by the drag;
 * true if it changed. */
bool ui_drag_scroll(const touch_event_t *ev, int axis, int step, int *top, int max_top);
/* focus pulled into the visible range [top, top + nvis - 1] */
int ui_drag_focus(int focus, int top, int nvis);
/* The usual list: count rows of step px, nvis visible from *top; scrolls by
 * the drag and pulls *sel (may be NULL) into view, like the menus. True if
 * *top or *sel changed (redraw). */
bool ui_drag_list(const touch_event_t *ev, int step, int *top, int count, int nvis, int *sel);

/* blinking cursor: screens call ui_cursor_at() while drawing an editor */
void ui_cursor_at(int x, int y, uint32_t ch, uint16_t fg, uint16_t bg);
void ui_cursor_hide(void);
void ui_cursor_reset_blink(void);

/* text from a key for editors (keys/layout.py): returns true and fills disp (UTF-8) */
bool ui_key_text(int key, int mod, int alpha, char *disp, size_t n);
/* text a key inserts after the cursor (templates such as abs( ... ), or NULL */
const char *ui_key_suffix(int key, int mod);
/* standard editor key handling (arrows, ⌫, SHIFT ⌫ insert mode; CLEAR not included). true if handled */
bool ui_editor_key(editor_t *e, int key, int mod);
/* draw a single-line horizontally scrolled editor */
void ui_draw_editor_line(editor_t *e, int x, int y, int cols, uint16_t fg, uint16_t bg, bool focus);

/* menus (menu.c): a full-screen list on scr_menu. Choosing an item either
 * calls the menu's on_select() or inserts the item's text into the editor of
 * the screen the menu was opened from, then returns to that screen. */
typedef struct {
    const char *label;  /* shown in the list */
    const char *insert; /* text inserted before the cursor (display form) */
    const char *suffix; /* text inserted after the cursor (template) */
    int action;         /* for menus with on_select */
    const char *help;   /* help topic for SHIFT HELP on this item (NULL: derived from `insert`) */
} menu_item_t;
typedef struct menu_def {
    const char *title;        /* title line of the menu */
    const menu_item_t *items; /* static items (n of them), or NULL for a dynamic menu: */
    int n;
    void (*on_select)(const menu_item_t *it); /* NULL: insert into the editor of the screen below */
    bool alpha_jump; /* catalog: letter keys jump */
    const char *help; /* help topic for the menu itself */
    int (*count)(void);                    /* dynamic menus: number of items, and */
    void (*get)(int i, menu_item_t *out);  /* item i (its strings stay valid until the next get) */
    const softkeys_t *softkeys;            /* soft keys while it is open (NULL: the default menu bar) */
    /* Type-to-filter menus (CATALOG, HISTORY; app/input.h): keys other than
     * UP/DOWN/ENTER/F1/F2/CLEAR go to filter_key() first; true = the item
     * list changed (the selection goes back to the first item). query()
     * returns the text shown at the right of the title line. NULL: none. */
    bool (*filter_key)(int key, int mod);
    const char *(*query)(void);
} menu_def_t;
void menu_open(const menu_def_t *m); /* show m (selection at the top); m must stay valid */
extern const menu_def_t menu_cas, menu_math, menu_zoom, menu_angle;
/* close the open menu: back to the screen it was opened from */
void menu_close(void);
const char *menu_current_title(void);
int menu_current_sel(void);
/* The highlighted item of the open menu (false if no menu is open). */
bool menu_current_item(menu_item_t *out);
const menu_def_t *menu_current(void);
void menu_page_room(bool *up, bool *down); /* can a page key scroll the open menu up / down */
void help_page_room(bool *up, bool *down); /* the same for the help page */
const char *ui_softkey_label(const char *label); /* NULL for a page key that cannot scroll */

/* functions/graphing helpers */
void funcs_invalidate(void);  /* Y= or settings changed */
void graphx_init(void);       /* graphing state defaults (app/graph.h), from app_init */
void funcs_compile(void);     /* compile the Y= functions not compiled since the last invalidate */
double func_eval(int i, double x); /* Y(i+1) at x in double precision (NAN if not compiled) */
int window_check(void); /* 0 ok */
void graph_zoom(int action);  /* ZOOM menu action (menu.c Z_* values) */
/* key=value pairs for app_state_foreach() (console `state`) */
void graph_state(void (*kv)(const char *, const char *, void *), void *user);
void table_state(void (*kv)(const char *, const char *, void *), void *user);
void home_state(void (*kv)(const char *, const char *, void *), void *user);

/* number formatting for tables/trace (fits in `width` chars) */
void fmt_num(double v, int width, char *out, size_t n);
/* evaluate a numeric expression (no side effects on ans); 0 = ok */
int eval_number(const char *cas_text, double *out, char *err, size_t errn);

/* settings: push g's MODE settings to the CAS and editors, invalidate the graph (app.c) */
void app_apply_settings(void);
/* open SETTINGS (scr_mode) with the row labelled `label` ("GRID", "ANGLE", ...) focused */
void mode_open_at(const char *label);

/* result formatting (numfmt.c): apply MODE's number format (FIX/SCI/ENG) to the
 * floating-point numbers in CAS output text */
void numfmt_apply(const char *in, char *out, size_t n);
/* true if CAS result text is not real: the word i, or a root of -1 such as
 * (-1)^(1/6) (REAL mode refuses such results; r∠θ shows them in polar form) */
bool numfmt_nonreal(const char *cas_result);
/* format one number with the MODE settings (for apps that print doubles) */
void numfmt_double(double v, char *out, size_t n);

/* Evaluate CAS input like the home screen (numfmt.c): cas_eval, then the
 * complex mode (REAL: error on non-real results, r∠θ: polar form) and the
 * number format; `out` gets display text ("ERR:..." on errors). */
cas_status app_eval(const char *cas_text, char *out, size_t n);
/* The same for a displayed home result: also the MODE SIMPLIFY AUTO pass
 * (app/result_actions.c). Not for helper evaluations (graphs, solvers). */
cas_status app_eval_result(const char *cas_text, char *out, size_t n);

/* Result actions and auto-simplification (result_actions.c, help
 * "result-actions"): SIMPLIFY AUTO state; the autosimplify pass on a result
 * `raw` of the CAS input `in` (true: raw was replaced) via ans (home), the
 * text itself (quiet: preview) or an RPN level variable; the REWRITE menu;
 * apply command fn (NULL: approx/exact toggle) to the current result of the
 * home screen or RPN. */
bool autosimp_on(void);
bool autosimp_wanted(const char *in, const char *raw);
bool autosimp_ans(const char *in, char *raw, size_t n);
bool autosimp_text(const char *in, char *raw, size_t n);
bool autosimp_var(const char *in, const char *var, char *raw, size_t n);
bool result_is_approx(const char *result_text);
void rewrite_open(const char *value); /* the REWRITE menu for this result (matrix / unit / other items) */
void result_action(const char *fn, const char *extra);
void home_result_action(const char *fn, const char *extra); /* home.c */
void rpn_result_action(const char *fn, const char *extra);  /* rpn.c */
void home_result_convert(void); /* REWRITE convert to (unit): template + UNITS menu */
void rpn_result_convert(void);

/* VARS (vars.c): define Y1(x)..Y6(x) in the CAS from the Y= editor (cheap when
 * nothing changed); call before evaluating user input that may use them */
void yvars_sync(void);
void yvars_invalidate(void); /* the CAS was reset: define them again */
void vars_init(void);

/* home screen (home.c) */
void home_reset(void); /* clear the history and the entry line */
void home_notebook_menu(void); /* HOME on the home screen: the notebook menu */
/* graphing started on the home screen ends in its notebook (home.c, help page "graph-cells") */
void home_graph_flow_check(const struct screen *s); /* (ui_switch) the flow ends off the graph screens */
bool home_graph_embed(bool d3); /* the graph key: true when the graph went into the notebook */
void home_graph_enter(bool d3); /* the full graph screen entered: in the flow, a cell bound to it */
void home_graph_new(bool d3);   /* a graph cell of the current functions (none: their editor, bound) */

/* help viewer (help.c, content in firmware/help/, see app/EXTENDING.md) */
#define HELP_WORK (128u << 10) /* topic text + layout workspace (from the arena) */
void help_init(void *work, size_t size);
void help_open(const char *topic);   /* push a topic (unknown ids open the index) */
bool help_exists(const char *topic);
/* topic for menu text such as "factor(" -> "factor" (NULL if none) */
const char *help_topic_for_insert(const char *insert);
void help_context(void);             /* SHIFT HELP: help for the current screen / menu item */
/* show generated markup (same syntax as help files) in the viewer; both strings
 * must stay valid while help is open (back to the page after a link reshows it) */
void help_show_text(const char *title, const char *markup);
void help_state(void (*kv)(const char *, const char *, void *), void *user);

/* global actions (app/keys.c, named in keys/layout.py): APPS, VARS and MEM menus, power off */
void apps_open(void);
void vars_open(void);
void mem_open(void);
void mem_clear_all(void); /* SHIFT CLEAR: asks, then clears the CAS variables, entry line and history */
void app_about(void);   /* About page (version, memory) */
void units_open(void);
void units_open_constants(void);
void app_request_off(void);
bool keys_dispatch(int key, int mod);
const char *keys_legend(int key, int mod); /* keycap legend of key in layer mod (MOD_SHIFT: SHIFT legend) */
bool keys_action(const char *name);        /* run a global action by name (false: unknown) */

/* soft keys (app/softkeys.c) */
const softkeys_t *ui_softkeys(void);       /* of the current screen (or open menu) */
void ui_draw_softkeys(void);               /* (re)draw the bar if its content changed */
void ui_softkeys_invalidate(void);         /* force a redraw of the bar */
bool ui_softkey(int fkey, int mod);        /* press F1..F5 (index 0..4); false: unused */
void ui_context_menu(void);                /* MENU key */
/* the soft keys of the graph screens: y= window zoom trace graph / plots tblset format calc table */
extern const softkeys_t softkeys_graph;

/* the screen a menu was opened from (menu.c) */
const screen_t *menu_origin(void);

#endif
