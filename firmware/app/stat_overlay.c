/* Stat plots as a graph overlay (graph_overlay.h): drawn by the graph screen with
 * the same window as the functions. Tracing and ZoomFit bounds are not provided
 * (ZoomStat lives in the STAT PLOTS menu).
 *
 * Registered in graph_overlay_registry[] (app/registry.c). The overlay is
 * active while any of the three stat plots is switched on (stat_plots_any);
 * the drawing itself lives in stat_plot.c, which only needs the window and
 * its pixel rectangle.
 * This file just adapts the overlay interface to stat_plots_draw(). */
#include "graph_overlay.h"
#include "stat.h"

/* Overlay draw callback (called by the graph screen while it renders, with
 * the clip set to the plot area). Converts the current WINDOW to the pixel
 * rectangle of the plot: gx/gy = top-left pixel (xmin, ymax), width/height
 * in pixels including the pixel of (xmax, ymin) (stat_plot.c maps xmax to
 * column gx + gw - 1, like the function plotter); screen y grows downward. */
static void stat_overlay_draw(void)
{
    double xmin, xmax, ymin, ymax;
    gov_window(&xmin, &xmax, &ymin, &ymax);
    int gx = gov_px(xmin), gy = gov_py(ymax);
    stat_plots_draw(xmin, xmax, ymin, ymax, gx, gy, gov_px(xmax) - gx + 1, gov_py(ymin) - gy + 1);
}

const graph_overlay_t graph_overlay_stat = {
    .id = "stat",
    .active = stat_plots_any,
    .draw = stat_overlay_draw,
};
