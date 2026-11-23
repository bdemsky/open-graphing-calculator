/* On-graph sliders for the free parameters of the plotted expressions (single
 * letters other than the independent variable). Up to three rows above the
 * coordinate band; the selected slider and its neighbours are shown. Dragging
 * changes the value and re-plots at once (parameters are inputs of the compiled
 * functions: no recompilation). Settings (min, max, step): scr_slider (form.c).
 *
 * Geometry (screen pixels): rows of SLIDER_H = 18 px stacked upwards from
 * BOTTOM, the top of the 16 px coordinate band. Each row: "a=1.5" in
 * font_medium at the left (highlighted when selected), and a horizontal track
 * from TX0 = 100 to TX1 = 308 with end ticks; the knob is a 7x13 block at
 *     kx = TX0 + (val - min) / (max - min) * (TX1 - TX0)
 * (clamped to the track). A touch maps back with the inverse of that formula;
 * the value is then clamped and snapped to the step by gx_param_set().
 *
 * The graph screen (graph.c) owns the interaction: it draws the sliders as a
 * decoration over the cached plot, routes touches and the + - `,` ENTER keys
 * here, and commits values to the CAS (gx_param_commit) when a drag ends;
 * values stepped with + - are committed later by gx_params_flush(). The
 * parameter state lives in gx.prm[] / gx.sel_param (graph.h, saved with
 * persist_graphx). */
#include <math.h>
#include <stdio.h>
#include "graph.h"

#define ROWS_MAX 3
#define BOTTOM (UI_BOTTOM - 16) /* above the coordinate band */
#define TX0 100                 /* track: left end (min) ... */
#define TX1 (LCD_WIDTH - 12)    /* ... right end (max) */

/* With more shown sliders (n) than rows, the rows show a window of them
 * centred on the selected one (clamped at both ends of the list). */
static int first_row(int n, int rows)
{
    /* index (among the shown sliders) of the first visible row: keep the selected one visible */
    int sel = 0;
    for (int i = 0; i < n; i++)
        if (gx_param_shown(i) == gx.sel_param)
            sel = i;
    int first = sel - rows / 2;
    if (first > n - rows)
        first = n - rows;
    return first < 0 ? 0 : first;
}

/* number of slider rows on screen (0..3): one per shown parameter */
int slider_rows(void)
{
    int n = gx_nparams_shown();
    return n > ROWS_MAX ? ROWS_MAX : n;
}

int slider_top(void) { return BOTTOM - slider_rows() * SLIDER_H; }

/* screen x of the knob (see the geometry above); a NAN fraction (max == min)
 * puts it at the left end */
static int knob_x(const gparam_t *p)
{
    double f = (p->val - p->min) / (p->max - p->min);
    if (!(f >= 0))
        f = 0;
    if (f > 1)
        f = 1;
    return TX0 + (int)lround(f * (TX1 - TX0));
}

/* Draw the visible slider rows onto the screen (over the plot; the graph
 * screen registers the area so it can restore it from the cache). */
void slider_draw(void)
{
    int n = gx_nparams_shown(), rows = slider_rows();
    if (!rows)
        return;
    int first = first_row(n, rows), y0 = slider_top();
    for (int r = 0; r < rows; r++) {
        int k = gx_param_shown(first + r), y = y0 + r * SLIDER_H;
        const gparam_t *p = &gx.prm[k];
        bool sel = k == gx.sel_param;
        gfx_fill_rect(0, y, LCD_WIDTH, SLIDER_H, C_WHITE);
        gfx_hline(0, y, LCD_WIDTH, C_LTGRAY);
        char v[24], t[40];
        fmt_num(p->val, 9, v, sizeof v);
        snprintf(t, sizeof t, "%s=%s", p->name, v);
        if (sel)
            gfx_fill_rect(1, y + 2, TX0 - 10, SLIDER_H - 3, C_SEL_BG);
        gfx_text(&font_medium, 4, y + 2, t, sel ? C_WHITE : C_BLACK, GFX_TRANSPARENT);
        int cy = y + SLIDER_H / 2;
        gfx_hline(TX0, cy, TX1 - TX0 + 1, C_GRAY);
        gfx_vline(TX0, cy - 3, 7, C_GRAY);
        gfx_vline(TX1, cy - 3, 7, C_GRAY);
        int kx = knob_x(p);
        gfx_fill_rect(kx - 3, cy - 6, 7, 13, sel ? C_SEL_BG : C_DKGRAY);
    }
}

/* Hit test for screen point (x, y): which row (-> *k, an index into gx.prm)
 * and whether it is on the track side (x >= TX0 - 8, a little slack so the
 * knob at the minimum is easy to grab) or on the name. */
bool slider_hit(int x, int y, int *k, bool *on_track)
{
    int rows = slider_rows();
    if (!rows || y < slider_top() || y >= BOTTOM)
        return false;
    int r = (y - slider_top()) / SLIDER_H;
    *k = gx_param_shown(first_row(gx_nparams_shown(), rows) + r);
    *on_track = x >= TX0 - 8;
    return *k >= 0;
}

/* Select slider k and set its value from screen x (inverse knob mapping;
 * outside the track gx_param_set clamps to min/max). Only the evaluation
 * input changes; the caller re-plots and commits to the CAS at the end. */
void slider_drag_to(int k, int x)
{
    gparam_t *p = &gx.prm[k];
    gx.sel_param = (uint8_t)k;
    double f = (double)(x - TX0) / (TX1 - TX0);
    gx_param_set(k, p->min + f * (p->max - p->min));
}

/* + / - keys: move the selected slider by its step (1/100 of the range when
 * the step is 0). The value is only marked dirty: gx_params_flush() stores it
 * in the CAS once, before anything reads the variable, instead of one CAS
 * evaluation per key press. The caller re-plots. */
void slider_step(int dir)
{
    int k = gx.sel_param;
    gparam_t *p = &gx.prm[k];
    if (!p->used || !p->shown)
        return;
    double st = p->step > 0 ? p->step : (p->max - p->min) / 100;
    gx_param_set(k, p->val + dir * st);
    p->dirty = true;
}

/* `,` key: select the next shown slider (wrapping); if the selection is not a
 * shown slider (e.g. its expression was removed), select the first one. */
void slider_select_next(void)
{
    int n = gx_nparams_shown();
    for (int i = 0; i < n; i++)
        if (gx_param_shown(i) == gx.sel_param) {
            gx.sel_param = (uint8_t)gx_param_shown((i + 1) % n);
            return;
        }
    if (n)
        gx.sel_param = (uint8_t)gx_param_shown(0);
}
