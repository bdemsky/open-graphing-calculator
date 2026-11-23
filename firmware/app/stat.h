/*
 * stat.h -- internal interface of the statistics app (app/stat_*.c).
 *
 *   stat_lists.c  list storage (L1..L6, RESID, named lists), column formulas,
 *                 CAS synchronisation, persistence, VARS categories
 *   stat_edit.c   list editor screen (STAT > Edit)
 *   stat_menu.c   STAT / DISTR menus, CALC / TESTS / DISTR input forms, results
 *   stat_form.c   generic input form + result screens
 *   stat_draw.c   shaded distribution curves (DISTR DRAW, test results)
 *   stat_plot.c   stat plots (SHIFT F1 on the graph screens), drawing on the graph screen, ZoomStat
 *   stat_csv.c    CSV import/export on the microSD card (core/fat.h)
 *   stat_math.c   computations (stat_math.h), cas/src/statdist.c distributions
 *   stat_overlay.c the stat plots as a graph overlay (graph_overlay.h)
 *   stat_console.c console commands `stat ...` / `sd ...` for the UI tests
 *
 * State: the list pool, the scratch area (st_scratch, 3 x capacity doubles)
 * and the shared CAS text buffer are carved from the arena by
 * stat_mem_init() at app_init (smaller capacities in the fallback arena when
 * the SDRAM is missing). Lists and plot settings are saved with the session
 * (persist_stat_lists, persist_stat_plots); results (stat_last) are not.
 * Registry entries (app/registry.c): apps Statistics / Distributions / Stat
 * plots, the VARS category, the persist blobs and the graph overlay.
 */
#ifndef STAT_H
#define STAT_H
#include <stdbool.h>
#include <stddef.h>
#include "stat_math.h"
#include "ui.h"

/* ------------------------------------------------------------ lists */
#define SL_MAX 20      /* lists: L1..L6, RESID, 13 named */
#define SL_NAME 8      /* name length incl. NUL (named lists: up to 7 chars) */
#define SL_FORMULA 64  /* column formula (CAS text of the editor's display form) */
#define SL_RESID 6     /* index of RESID */
/* One list. Slots 0..5 are L1..L6, slot SL_RESID the residuals of the last
 * regression, the rest named lists created on demand. Values live in the
 * arena pool (fixed capacity sl_cap() per list); in the CAS a list is a
 * variable of the same name, synchronised around evaluations. */
typedef struct {
    char name[SL_NAME];
    int len;                 /* number of values (0..sl_cap()) */
    double *v;               /* capacity sl_cap() */
    char formula[SL_FORMULA]; /* "" = none; display text, e.g. "L1*2" */
    bool used;               /* named lists: slot in use (L1..L6, RESID always) */
} slist_t;

/* Carve the list pool + scratch from the arena (app_init); returns bytes used. */
size_t stat_mem_init(void *mem, bool big);
int sl_cap(void);                  /* capacity of every list, in values */
/* shared text buffer (32 KB; 2 KB in the fallback arena), used by the CAS sync */
char *stat_text(size_t *n);
slist_t *sl_at(int slot);          /* slot 0..SL_MAX-1 (NULL if unused) */
slist_t *sl_find(const char *name);
slist_t *sl_create(const char *name); /* named list (or existing); NULL if full / bad name */
bool sl_valid_name(const char *name);
void sl_delete(slist_t *l);        /* named: remove (and purge from the CAS); L1..L6, RESID: clear */
/* "L1,L2 DATA" -> list slots; -1 (err set) if a name is unknown */
int sl_parse(const char *s, int *slots, int max, char *err, size_t errn);
/* The list changed: recompute dependent formula lists, resync with the CAS. */
void sl_changed(slist_t *l);
/* Set the list from values (truncated to the capacity); calls sl_changed. */
void sl_set(slist_t *l, const double *v, int n);
/* Evaluate a formula/expression giving a list into `l` (0 = ok, else err set). */
int sl_eval_into(slist_t *l, const char *disp_expr, char *err, size_t errn);
void sl_recompute(void);           /* re-evaluate every formula list */
/* editor columns (SetUpEditor): list slots shown, in order */
extern int sl_cols[SL_MAX];
extern int sl_ncols;
void sl_editor_default(void);

/* CAS synchronisation around home-screen evaluations (called by app_eval) */
void stat_pre_eval(const char *cas_text);
void stat_post_eval(const char *cas_text);

/* last computed results (VARS > Statistics) */
extern sres_t stat_last;
extern sreg_t stat_last_reg;
extern bool stat_have_reg;

/* ------------------------------------------------------------ screens */
extern const screen_t scr_stat_edit, scr_stat_form, scr_stat_result, scr_stat_draw, scr_stat_plots;
void stat_edit_open(int col);
void stat_menu_open(void);
void stat_calc_open(void), stat_tests_open(void), stat_sorta(void), stat_sortd(void), stat_clrlist(void);           /* APPS > Statistics, list editor soft keys */
void stat_distr_open(void);          /* APPS > Distributions */
void stat_plot_menu_open(void);      /* SHIFT F1 (plots) on the graph screens */
void stat_csv_menu_open(void);
slist_t *stat_edit_current(void); /* editor column (when the menu was opened from the editor) */
void stat_edit_state(void (*kv)(const char *, const char *, void *), void *user);
/* Copy (app/input_clip.c): the entry line while editing, else what ENTER
 * would load (the cell's value; on the header the list as {1,2,3} or its
 * formula). false: nothing to copy. Leaves the editor's state unchanged. */
bool stat_edit_clip_text(char *out, size_t n);

/* generic forms (stat_form.c)
 * A form is a static table of fields; the form screen keeps the edited text
 * of each field and the menus read them back with sf_num / sf_list /
 * sf_choice / sf_text, looking fields up by label. Field types: a number
 * (any expression, evaluated), a list name, a choice among options (stored
 * as the option index), free text, an action line ("Calculate",
 * "Draw") that calls sform_t.action on ENTER, and a colour choice (a choice
 * whose options are drawn as swatches of func_colors[]). */
enum { SF_NUM = 0, SF_LIST, SF_CHOICE, SF_TEXT, SF_ACTION, SF_COLOR };
/* sfield_t.cond_val flag: the low bits are a mask of choice indices, any of
 * which shows the field (e.g. YList only for the scatter and xyLine plots) */
#define SF_ANYOF 0x80
typedef struct {
    const char *label;  /* shown and used as the field's key, e.g. "Xlist:" */
    uint8_t type;       /* SF_* */
    uint8_t cond;      /* 0, or 1 + index of a choice field that must equal cond_val
                        * for this field to be shown (e.g. Data vs Stats input) */
    uint8_t cond_val;  /* the value, or SF_ANYOF | mask of values (indices 0..6) */
    uint8_t act;       /* SF_ACTION: action code passed to `action` */
    const char *opts;  /* SF_CHOICE: "a|b|c"; others: default text */
} sfield_t;
typedef struct sform {
    const char *title;
    const sfield_t *f;
    int n;
    void (*action)(int act); /* called on ENTER on an action field */
    const char *help;
    int id;                  /* user data (e.g. the test id) */
    void (*changed)(void);   /* optional: a field value changed (applied immediately) */
    const char *(*hint)(void); /* optional: grey hint line (NULL result: the hint of the field type) */
} sform_t;
#define SF_MAXF 16
void sf_open(const sform_t *def);
const sform_t *sf_current(void);
int sf_selected(void);                        /* index of the selected field */
double sf_num(int field, bool *ok);           /* evaluate; sets ok=false + message on error */
const char *sf_text(int field);
int sf_choice(int field);
void sf_set_text(int field, const char *text);
/* the list named in a field: values, length (frequency fields: NULL if "1" or empty) */
bool sf_list(int field, const double **v, int *n, bool optional);
void sf_error(const char *msg);               /* show an error line on the form */
/* result screen: lines of `r` with their meaning; soft key draw if `drawable` */
void sr_show(const sres_t *r, bool drawable);
/* for the next sr_show only: a note right of the title ("Ha: mu>5") and the
 * action of the soft key plot (NULL: no plot key) */
void sr_note(const char *note);
void sr_set_plot(void (*plot)(void));
void sr_update_note(const char *note); /* change the note of the result shown */
void stat_fmt(double v, char *b, size_t n); /* result value with the MODE format; integers plain */

/* shaded distribution drawing (stat_draw.c) */
void sd_draw_open(int dist, double p1, double p2, double lo, double hi, const sres_t *res);
void sd_draw_result(const sres_t *r);

/* stat plots (stat_plot.c): Plot1..Plot3, saved with the session */
#define SP_N 3
typedef struct {
    bool on;
    uint8_t type, mark, color;                 /* SP_* type, mark style, colour */
    char xl[SL_NAME], yl[SL_NAME], fl[SL_NAME]; /* x list, y list, frequency list (by name) */
} splot_t;
enum { SP_SCATTER = 0, SP_XYLINE, SP_HIST, SP_MODBOX, SP_BOX, SP_NORMPROB, SP_NTYPES };
extern splot_t stat_plots[SP_N];
/* Draw the enabled plots into the graph area: window [xmin,xmax]x[ymin,ymax],
 * pixel rectangle (gx, gy, gw, gh). Called by the graph screen (graph.c). */
void stat_plots_draw(double xmin, double xmax, double ymin, double ymax, int gx, int gy, int gw, int gh);
bool stat_plots_any(void);
int stat_zoomstat(void); /* set the window from the plots' data; 0 = ok */
/* show xl (and yl: scatter / xyLine) as a plot of SP_* type: reuses or
 * takes a plot, turns it on, ZoomStat, graph screen */
void stat_plot_show(int type, const char *xl, const char *yl);

/* console commands (stat_console.c) */
int stat_console(int argc, char **argv);
int sd_console(int argc, char **argv);
void stat_state(void (*kv)(const char *, const char *, void *), void *user);
void stat_plot_init(void);

/* CSV (stat_csv.c) */
int stat_csv_export(const char *path, const int *slots, int n, char *msg, size_t msgn);
int stat_csv_import(const char *path, char *msg, size_t msgn);

#endif
