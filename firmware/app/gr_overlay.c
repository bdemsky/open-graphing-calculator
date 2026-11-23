/* Graph overlays (graph_overlay.h): drawing helpers, bounds, and a stub
 * overlay that shows how the statistics app plugs in (inactive).
 *
 * The gov_* helpers map graph coordinates to screen pixels with the same view
 * mapping as the plot (graph.h gv_*), so overlay drawings line up with the
 * curves. The registry itself is in app/registry.c; this file only walks it.
 * No state of its own: the window is g.xmin..g.ymax, the cache flag
 * g.graph_valid. */
#include <math.h>
#include <stdlib.h>
#include "graph.h"

/* Round a pixel coordinate to int, clamped to ±30000 (NAN -> -30000) so that
 * points far outside the window still give safe line endpoints. */
static int clampi(double v)
{
    if (!(v > -30000))
        return -30000;
    if (v > 30000)
        return 30000;
    return (int)lround(v);
}

int gov_px(double x) { return GX0 + clampi(gv_x_to_px(x)); }
int gov_py(double y) { return GY0 + clampi(gv_y_to_py(y)); }

void gov_line(double x0, double y0, double x1, double y1, uint16_t color)
{
    gfx_line(gov_px(x0), gov_py(y0), gov_px(x1), gov_py(y1), color);
}

void gov_mark(double x, double y, int mark, uint16_t color)
{
    int px = gov_px(x), py = gov_py(y);
    switch (mark) {
    case GOV_BOX: gfx_rect(px - 2, py - 2, 5, 5, color); break;
    case GOV_CROSS:
        gfx_line(px - 2, py - 2, px + 2, py + 2, color);
        gfx_line(px - 2, py + 2, px + 2, py - 2, color);
        break;
    case GOV_PLUS:
        gfx_hline(px - 2, py, 5, color);
        gfx_vline(px, py - 2, 5, color);
        break;
    default: gfx_fill_rect(px - 1, py - 1, 3, 3, color); break;
    }
}

void gov_rect(double x0, double y0, double x1, double y1, uint16_t color, bool fill)
{
    int a = gov_px(x0), b = gov_px(x1), c = gov_py(y0), d = gov_py(y1);
    int x = a < b ? a : b, y = c < d ? c : d, w = abs(b - a) + 1, h = abs(d - c) + 1;
    if (fill)
        gfx_fill_rect(x, y, w, h, color);
    else
        gfx_rect(x, y, w, h, color);
}

void gov_window(double *xmin, double *xmax, double *ymin, double *ymax)
{
    *xmin = g.xmin, *xmax = g.xmax, *ymin = g.ymin, *ymax = g.ymax;
}

void graph_invalidate(void) { g.graph_valid = false; }

/* Draw every active overlay, in registry order (called by the graph screen's
 * render() with the clip set to the plot area). */
void gr_overlays_draw(void)
{
    for (int i = 0; i < graph_overlay_registry_count; i++) {
        const graph_overlay_t *o = graph_overlay_registry[i];
        if (o->draw && (!o->active || o->active()))
            o->draw();
    }
}

/* Union of the data bounds of the active overlays that report some (ZoomFit,
 * ZoomStat). The outputs are only written when the result is true. */
bool gr_overlays_bounds(double *xmin, double *xmax, double *ymin, double *ymax)
{
    bool any = false;
    for (int i = 0; i < graph_overlay_registry_count; i++) {
        const graph_overlay_t *o = graph_overlay_registry[i];
        double a, b, c, d;
        if (!o->bounds || (o->active && !o->active()) || !o->bounds(&a, &b, &c, &d))
            continue;
        if (!any || a < *xmin)
            *xmin = a;
        if (!any || b > *xmax)
            *xmax = b;
        if (!any || c < *ymin)
            *ymin = c;
        if (!any || d > *ymax)
            *ymax = d;
        any = true;
    }
    return any;
}

/* ZoomStat: window = overlay bounds + 5 % on each side (a single value or a
 * flat range is widened by ±1 first). The caller switches to the graph. */
bool graph_zoom_overlays(void)
{
    double a, b, c, d;
    if (!gr_overlays_bounds(&a, &b, &c, &d))
        return false;
    if (b - a < 1e-9)
        a -= 1, b += 1;
    if (d - c < 1e-9)
        c -= 1, d += 1;
    double mx = (b - a) * 0.05, my = (d - c) * 0.05;
    g.xmin = a - mx, g.xmax = b + mx, g.ymin = c - my, g.ymax = d + my;
    graph_view_changed();
    return true;
}

/* ---------------------------------------------------------------- stub
 * Placeholder for the statistics app: never active. It documents the hook
 * and keeps the registry non-empty; the statistics overlay replaces it. */
static bool stub_active(void) { return false; }
const graph_overlay_t graph_overlay_stub = {"stub", stub_active};
