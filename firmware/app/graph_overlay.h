/*
 * Graph overlays: extra layers drawn into the GRAPH screen in graph
 * coordinates, with the same WINDOW, TRACE and ZoomFit as the functions.
 * Used for statistics plots (scatter, xyLine, histogram, box plots); see
 * app/EXTENDING.md "Graph overlays". Register a `graph_overlay_t` in
 * graph_overlay_registry[] (app/registry.c).
 *
 * The graph screen (app/graph.c) calls draw() of every active overlay while it
 * renders the plot, so overlays share the plot cache: they are not redrawn
 * when the cursor moves, only after graph_invalidate() or a view change. Trace
 * targets are numbered after the graph entries (see graph.c "trace targets").
 * Helpers are implemented in app/gr_overlay.c.
 */
#ifndef GRAPH_OVERLAY_H
#define GRAPH_OVERLAY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct graph_overlay {
    const char *id;  /* short name (diagnostics) */
    /* true when there is something to draw (e.g. a stat plot is switched on);
     * NULL means always active */
    bool (*active)(void);
    /* draw with the gov_* helpers; called after the axes, before the functions,
     * clipped to the plot area. The result is cached with the plot. */
    void (*draw)(void);
    /* TRACE: point sets the cursor can walk along ({▲}{▼} cycle through the
     * functions, then the sets of every active overlay). Any may be NULL. */
    int (*nsets)(void);
    int (*npoints)(int set);
    bool (*point)(int set, int i, double *x, double *y);
    const char *(*label)(int set, char *buf, size_t n); /* trace band text, e.g. "P1 L1,L2" */
    uint16_t (*color)(int set);
    /* ZoomFit (and ZoomStat): data range; false if unknown */
    bool (*bounds)(double *xmin, double *xmax, double *ymin, double *ymax);
} graph_overlay_t;

extern const graph_overlay_t *const graph_overlay_registry[];
extern const int graph_overlay_registry_count;

/* ---- drawing helpers (valid inside draw()) ---- */
enum { GOV_DOT = 0, GOV_BOX, GOV_CROSS, GOV_PLUS };
/* screen x of a graph x (may be off screen; clamped to ±30000 so huge or NAN
 * values cannot overflow the line drawing) */
int gov_px(double x);
int gov_py(double y);  /* screen y of a graph y (same clamping) */
void gov_line(double x0, double y0, double x1, double y1, uint16_t color);
/* 5x5 mark (GOV_DOT: a 3x3 dot) centred on the point */
void gov_mark(double x, double y, int mark, uint16_t color);
/* rectangle between two graph corners (any order), outline or filled */
void gov_rect(double x0, double y0, double x1, double y1, uint16_t color, bool fill);
void gov_window(double *xmin, double *xmax, double *ymin, double *ymax); /* current WINDOW */

/* data changed: re-plot when the graph is shown next */
void graph_invalidate(void);
/* set the window to the overlays' bounds (ZoomStat), with a 5 % margin; false if none */
bool graph_zoom_overlays(void);

#endif
