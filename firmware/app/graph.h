/*
 * Graphing internals shared by the graphing screens (graph.c, yeq.c, table.c,
 * form.c) and the graphing modules app/gr_*.c (placed in flash bank 2).
 *
 *   gr_model.c    graph modes (FUNC/PAR/POL/SEQ/IMPL/DE), entries, compilation,
 *                 evaluation, slider parameters, saved state (persist_graphx)
 *   gr_plot.c     rendering of every mode, line styles, shading, implicit
 *                 curves (marching squares), sequence time/web plots
 *   gr_num.c      numeric methods (Brent root/minimum, Gauss-Kronrod, derivatives)
 *   gr_calc.c     CALC menu (SHIFT F4): value, zero, min, max, intersect,
 *                 dy/dx, integral, inflection, arc length; special points
 *   gr_slider.c   on-graph sliders and their settings screen
 *   gr_de.c       differential equations (DE mode): slope fields, solution
 *                 curves (adaptive RK4), the curves' list for TRACE
 *   gr_overlay.c  graph overlays (stat plots) registry helpers + stub
 *
 * State. FUNC mode keeps its six functions Y1..Y6 in the shared app state
 * (g.f[], ui.h: editors + compiled code; WINDOW/TBLSET in g.xmin.. too, saved
 * by persist_graph in graph_state.c). Everything the other modes add lives in
 * the global `gx` (graphx_t below): the current mode, PAR/POL/SEQ/IMPL entries
 * stored compactly as token arrays, their ranges and the slider parameters
 * (saved by persist_graphx in gr_model.c). Screens that edit entries convert
 * between gtext_t and an editor_t with gtext_to_editor / gtext_from_editor.
 */
#ifndef GRAPH_H
#define GRAPH_H
#include <stdbool.h>
#include <stdint.h>
#include "ui.h"

/* ------------------------------------------------------------ modes */
/* graph modes: functions y(x), parametric (x(t), y(t)), polar r(θ),
 * sequences u(n), implicit relations in x and y, first-order differential
 * equations y' = f(x, y) (slope field and solution curves). The numbers are
 * saved (persist_graphx): new modes go at the end. */
enum { GM_FUNC = 0, GM_PAR, GM_POL, GM_SEQ, GM_IMPL, GM_DE, GM_NMODES };
/* line styles (Y= style column) */
enum { GS_LINE = 0, GS_THICK, GS_THIN, GS_DOT, GS_ABOVE, GS_BELOW, GS_NSTYLES };
enum { SEQ_TIME = 0, SEQ_WEB }; /* sequence plots: u(n) against n, or cobweb */
enum { REL_EQ = 0, REL_LT, REL_LE, REL_GT, REL_GE }; /* implicit relations */

#define GNENT 6    /* entries per mode */
#define GTOK 48    /* tokens per stored expression (PAR/POL/SEQ/IMPL) */
#define GNPARAM 8  /* slider parameters */
#define DE_NIC 4   /* DE: initial points typed per entry (its IC line) */
#define DE_NTAP 8  /* DE: solution curves added on the graph (tap, ENTER), all entries together */
#define DE_MAXC 16 /* DE: solution curves drawn and traced at most */

/* Expression text stored compactly (token ids, see editor.h): a token such as
 * `sin(` is one id, so 48 ids hold a long expression in 97 bytes; this keeps
 * 4 modes x 6 entries x 2 expressions small in RAM and in the saved session. */
typedef struct {
    uint16_t t[GTOK];
    uint8_t len;    /* tokens used */
} gtext_t;

/* One entry of the PAR/POL/SEQ/IMPL/DE modes (FUNC entries are g.f[]). */
typedef struct {
    gtext_t ex[2];  /* PAR: x(t), y(t); SEQ: u(n), initial values; DE: f(x, y), initial points; POL, IMPL: ex[0] */
    bool enabled;
    uint8_t color, style; /* index into func_colors; GS_* */
} gentry_t;

/* A slider parameter: a free one-letter variable of the plotted expressions
 * (e.g. `a` in a*x^2). Its value is an input of the compiled code, so moving
 * the slider re-plots without recompiling; gx_param_commit() also stores it
 * in the CAS variable. Slots keep their settings when unused. */
typedef struct {
    char name[2];   /* one letter */
    bool used;      /* slot holds settings */
    bool active;    /* referenced by the current graph */
    bool shown;     /* referenced by an enabled entry of the current mode (slider shown) */
    double val, min, max, step; /* step 0: continuous (keys step by 1/100 of the range) */
    bool dirty;     /* val changed by + / - and not yet stored in the CAS (gx_params_flush) */
} gparam_t;

/* Graphing state beyond FUNC mode (global `gx`, saved by persist_graphx). */
typedef struct {
    uint8_t mode;     /* GM_* */
    uint8_t seqplot;  /* SEQ_TIME / SEQ_WEB */
    uint8_t fstyle[NFUNC];              /* line style (GS_*) of Y1..Y6 */
    gentry_t ent[GM_NMODES - 1][GNENT]; /* PAR, POL, SEQ, IMPL, DE (FUNC uses g.f) */
    double tmin, tmax, tstep;           /* PAR parameter range (WINDOW) */
    double thmin, thmax, thstep;        /* POL angle range (in the MODE angle unit) */
    double nmin, nmax, plotstart, plotstep; /* SEQ: n range, first plotted n, plot every k-th n */
    gparam_t prm[GNPARAM];
    uint8_t sel_param;  /* slider selected with the keys */
    struct { double x, y; uint8_t ent; } dtap[DE_NTAP]; /* DE: curves added on the graph (oldest first) */
    uint8_t ndtap;
} graphx_t;
extern graphx_t gx;

extern const char *const gmode_names[GM_NMODES];  /* "FUNC", "PAR", ... */
extern const char *const gstyle_names[GS_NSTYLES];

void graphx_init(void);      /* defaults; called from app_init (statics survive sim resume) */
void graphx_defaults(void);
void gx_set_mode(int m);
gentry_t *gx_entry(int i);   /* entry i of the current mode (NULL in FUNC mode) */
bool gx_enabled(int i);
uint8_t gx_color(int i);
uint8_t gx_style(int i);
void gx_set_style(int i, int style);
bool gx_entry_empty(int i);
const char *gx_entry_label(int i, int part, char *buf, size_t n); /* "Y1", "X1T", "r1", "u1", "I1", "y1'" */
const char *gx_indep_name(void);  /* display name of the independent variable: x, T, θ, n */

/* gtext <-> editor */
void gtext_to_editor(const gtext_t *t, editor_t *e);
extern editor_t gx_tmp_ed; /* scratch editor for conversions (not kept across calls) */
bool gtext_from_editor(gtext_t *t, const editor_t *e); /* false if too long */
bool gtext_empty(const gtext_t *t);

/* compilation (all modes); cheap if nothing changed */
void gx_compile(void);
bool gx_plottable(int i);          /* enabled and compiled without error */
const char *gx_error(int i);       /* compile error of entry i ("" if none) */
int gx_rel(int i);                 /* IMPL: relation of entry i */

/* evaluation in the current mode (NAN where undefined) */
double gx_f(int i, double x);                       /* FUNC: Yi(x) */
bool gx_par(int i, double t, double *x, double *y); /* PAR: (x(t), y(t)); POL: r(θ) -> (x, y) */
double gx_pol_r(int i, double th);
double gx_impl(int i, double x, double y);          /* IMPL: lhs-rhs; DE: the slope f(x, y) */
int gx_de_nic(int i);                               /* DE: initial points typed for entry i */
void gx_de_ic(int i, int k, double *x, double *y);  /* DE: the k-th of them */
/* SEQ: u_i(n) for an integer n >= nMin (iterates from nMin; NAN if undefined) */
double gx_seq(int i, long n);
/* SEQ: all sequences from nMin to n1 in one pass; cb(n, values[GNENT]) for n >= n0 */
void gx_seq_run(long n0, long n1, void (*cb)(long n, const double *u, void *user), void *user);
/* SEQ web plot: u_i(n) as a function of u_i(n-1) (n fixed at nMin+1) */
double gx_seq_step(int i, double prev);
int gx_seq_depth(int i);           /* deepest recursion u(n-1)=1, u(n-2)=2, 0 = explicit */

/* parameters (sliders) */
int gx_nparams_shown(void);
int gx_param_shown(int k);         /* index into gx.prm of the k-th shown slider */
void gx_param_set(int p, double v);/* clamp/snap, graph redraw needed */
void gx_param_commit(int p);       /* store the value in the CAS variable (a:=v) */
void gx_params_flush(void);        /* commit the values + / - changed (before the CAS reads them) */
void gx_params_changed(void);      /* values changed: refresh evaluation arrays */
bool gx_params_sync(void);         /* follow CAS variables changed elsewhere; true if any changed */
bool gx_seq_selfonly(int i);

/* angle factor for polar coordinates (π/180 in DEG mode, else 1) */
double gx_angle_unit(void);

/* ------------------------------------------------------------ view mapping */
/* The plot is the whole body: screen rectangle (GX0, GY0, GW x GH) -- or, while a
 * notebook graph cell is drawn (app/gcell.c, graph_plot_view), the cell's plot. Pixel
 * coordinates below are relative to its top-left corner (add GX0/GY0 for the
 * screen). Pixel 0 is xmin and pixel GW-1 is xmax (the end pixels are exactly
 * on the window edges, hence the GW-1 / GH-1 divisors); y grows upwards while
 * pixel rows grow downwards, so row 0 is ymax. The functions are exact inverses
 * and accept fractional / off-screen values. */
typedef struct {
    int16_t x, y, w, h;
} gr_view_t;
extern gr_view_t gr_vp; /* {0, BODY_Y, LCD_WIDTH, BODY_H} except while a graph cell is drawn */
#define GX0 gr_vp.x
#define GY0 gr_vp.y
#define GW gr_vp.w
#define GH gr_vp.h
static inline double gv_px_to_x(double px) { return g.xmin + px * (g.xmax - g.xmin) / (GW - 1); }
static inline double gv_py_to_y(double py) { return g.ymax - py * (g.ymax - g.ymin) / (GH - 1); }
static inline double gv_x_to_px(double x) { return (x - g.xmin) * (GW - 1) / (g.xmax - g.xmin); }
static inline double gv_y_to_py(double y) { return (g.ymax - y) * (GH - 1) / (g.ymax - g.ymin); }

/* ------------------------------------------------------------ plotting (gr_plot.c) */
void gr_plot_all(void);                 /* every enabled entry of the current mode */
void gr_blend_rect(int x, int y, int w, int h, uint16_t c); /* tint pixels (shading) */
/* a line between graph-relative points with a style (clipped to the plot) */
void gr_seg(double x0, double y0, double x1, double y1, uint16_t c, int style);
void gr_dot(int px, int py, uint16_t c, int r);
/* shade between a FUNC curve and y = 0 (or another curve) for x in [a, b] */
void gr_shade_between(int fa, int fb, double a, double b, uint16_t c);

/* ------------------------------------------------------------ differential equations (gr_de.c) */
/* The solution curves of DE mode, numbered 0..gx_de_ncurves()-1: the typed initial points of the
 * plottable entries in entry order, then the ones added on the graph (gx.dtap), at most DE_MAXC.
 * They are the trace targets of DE mode. */
int gx_de_ncurves(void);
/* curve k: its entry, initial point and whether it was added on the graph (*tap: index in gx.dtap,
 * -1 if typed); false if there is no curve k */
bool gx_de_curve(int k, int *ent, double *x0, double *y0, int *tap);
/* y of curve k at x (RK4 from its initial point, steps sized for the window); NAN past the end of
 * the solution (undefined, vertical, blow-up) */
double gx_de_solve(int k, double x);
/* the points of curve k drawn in the window, backward then forward from the initial point:
 * cb(x, y, first) with first = true at the start of each direction */
void gx_de_walk(int k, void (*cb)(double x, double y, bool first, void *u), void *u);
/* add a curve through (x, y) for entry ent (the oldest added one goes when DE_NTAP are there);
 * returns its curve number, -1 if it cannot be drawn */
int gx_de_add(int ent, double x, double y);
void gx_de_remove_tap(int tap);   /* remove an added curve */
void gx_de_clear_taps(int ent);   /* remove entry ent's added curves (-1: all) */
void gr_de_plot(void);            /* slope fields, then the solution curves (gr_plot_all) */

/* ------------------------------------------------------------ numerics (gr_num.c) */
typedef double (*gn_fn)(double x, void *user);
/* root in [a, b] with f(a), f(b) of opposite signs (Brent); false if no bracket */
bool gn_root(gn_fn f, void *u, double a, double b, double *root);
/* root nearest to `guess` in [a, b]: scan for sign changes (n steps), then Brent; also
 * accepts touching zeros (|f| tiny at a minimum of |f|) */
bool gn_root_near(gn_fn f, void *u, double a, double b, double guess, int n, double *root);
/* minimum of f in [a, b] (global over n samples, then Brent's parabolic/golden search) */
bool gn_min(gn_fn f, void *u, double a, double b, int n, double *xmin);
/* derivative (Richardson-extrapolated central differences); order 1 or 2 */
double gn_deriv(gn_fn f, void *u, double x, int order);
/* adaptive Gauss-Kronrod 7-15; *err gets the estimated error (may be NULL) */
double gn_integrate(gn_fn f, void *u, double a, double b, double tol, double *err);

/* ------------------------------------------------------------ graph screen (graph.c) */
/* The plot layer alone in the view gr_vp (background, grid, axes, every enabled entry, the first
 * error or `empty` as a hint when nothing is plotted): a notebook graph cell (app/gcell.c). */
void graph_plot_view(const char *empty);
/* Touch preview shared by the graph screen and graph cells: the w x h image src (row stride
 * `stride`) drawn at (vx, vy) shifted by (dx, dy) and scaled by `scale` about the gesture's start
 * centre (cx0, cy0) moved to (cx, cy) (screen coordinates); outside: the background. Clipped to
 * the body. */
void gr_preview(const uint16_t *src, int stride, int vx, int vy, int w, int h, int dx, int dy, float scale, int cx0,
                int cy0, int cx, int cy);
/* The window after a pinch of `scale` that started centred at plot pixel (x0, y0) and ended at
 * (x1, y1), for a plot of w x h pixels (what gr_preview showed); false: a degenerate window. */
bool gr_pinch_window(double *xmin, double *xmax, double *ymin, double *ymax, int w, int h, double x0, double y0,
                     double x1, double y1, double scale);
void graph_redraw_now(void);            /* re-plot and draw the overlays (slider drag) */
bool graph_trace_value(double *v);      /* traced x / t / θ / n, if tracing */
void graph_calc_start(int op);          /* begin a CALC operation (gr_calc.c menu) */
void graph_show_point(int fn, double x, double y, const char *label); /* trace there + label */
int graph_trace_fn(void);               /* traced target (entry index), -1 if not tracing */
void graph_set_result(const char *band); /* text of the result band below the graph */
void graph_view_changed(void);          /* WINDOW changed elsewhere: re-plot on the next draw */
void table_center_on(double v, int fn);  /* the next TABLE visit is centred on v (table.c) */
void yeq_state(void (*kv)(const char *, const char *, void *), void *user);
void graph_editor_switch(bool d3);      /* the graph editor's 2D/3D switch: Y= <-> 3D Z= (yeq.c) */

/* CALC (gr_calc.c) */
enum {
    CALC_NONE = 0, CALC_VALUE, CALC_ZERO, CALC_MIN, CALC_MAX, CALC_ISECT, CALC_DYDX, CALC_INTEG,
    CALC_INFL, CALC_ARC, CALC_TABLE
};
extern const menu_def_t menu_calc;
/* prompt for input `step` (0-based) of op ("Left bound?"); always a string:
 * the graph screen ends the operation itself after calc_steps() inputs */
const char *calc_prompt(int op, int step);
/* how to answer that prompt, shown next to it ("◀▶ or type, enter") */
const char *calc_hint(int op, int step);
int calc_steps(int op);                      /* inputs op asks for (1..3) */
/* run an operation; inputs: fn, fn2, bounds lo/hi, guess; fills the result band */
void calc_run(int op, int fn, int fn2, double lo, double hi, double guess);
/* nearest special point (zero, extremum, intersection, y-intercept) of the plotted FUNC
 * curves within `radius` pixels of (px, py); false if none */
bool calc_snap(int px, int py, int radius, int *fn, double *x, double *y, const char **what);
void calc_store_ans(double v);
/* integral shading request for the next render (fa: function, a..b), cleared by graph_clear_shade */
extern struct calc_shade { bool on; int fn, fn2; double a, b; } calc_shade;

/* sliders (gr_slider.c) */
#define SLIDER_H 18                     /* height of one slider row in pixels */
int slider_rows(void);                  /* slider rows drawn at the bottom of the graph */
int slider_top(void);                   /* y of the first slider row */
void slider_draw(void);                 /* draws onto the screen (caller registers the overlay) */
/* screen point on a slider row? *k = its parameter, *on_track: on the track
 * (knob area) rather than the name */
bool slider_hit(int x, int y, int *k, bool *on_track);
void slider_drag_to(int k, int x);      /* select k, value from screen x (no CAS store) */
void slider_step(int dir);              /* +/- keys: step the selected slider */
void slider_select_next(void);          /* `,` key: next shown slider (wraps) */
void slider_open_settings(int k);       /* scr_slider form for parameter k (form.c) */
extern const screen_t scr_slider;

/* overlays (gr_overlay.c): stat plots and other drawings in the graph's coordinates */
#include "graph_overlay.h"
void gr_overlays_draw(void);            /* draw every active overlay (render) */
/* union of the active overlays' data bounds; false if none reports any */
bool gr_overlays_bounds(double *xmin, double *xmax, double *ymin, double *ymax);

#endif
