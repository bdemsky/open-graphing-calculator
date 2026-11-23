/* Saved state of the graphing screens: Y= functions (text, enable, colour),
 * WINDOW and TBLSET values. Registered in registry.c (persist_graph).
 * The state itself lives in the global app state `g` (ui.h). The other graph
 * modes, line styles and sliders are saved separately (persist_graphx,
 * gr_model.c).
 *
 * Blob layout, version 1:
 *   u8 NFUNC, then per function: str (ed_serialize text), u8 enabled, u8 colour
 *   u8 8, then f64 xmin xmax xscl ymin ymax yscl tblstart tbldelta
 * Counts are stored so that a firmware with more or fewer functions can still
 * read the blob (extra functions are skipped). */
#include <math.h>
#include "session.h"
#include "ui.h"

/* persist save(): serialize Y1..Y6 and the 8 window/table values */
static void graph_save(pw_t *w)
{
    char b[ED_MAX * 8];
    pw_u8(w, NFUNC);
    for (int i = 0; i < NFUNC; i++) {
        ed_serialize(&g.f[i].ed, b, sizeof b);
        pw_str(w, b);
        pw_u8(w, g.f[i].enabled);
        pw_u8(w, g.f[i].color);
    }
    const double v[] = {g.xmin, g.xmax, g.xscl, g.ymin, g.ymax, g.yscl, g.tblstart, g.tbldelta};
    pw_u8(w, sizeof v / sizeof v[0]);
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++)
        pw_f64(w, v[i]);
}

/* persist load(): restore the functions, then the window values all or
 * nothing: a truncated blob, a non-finite value, an invalid window
 * (window_check) or ΔTbl = 0 keep the current (default) values. Marks the
 * functions for recompilation. */
static void graph_load(pr_t *r, unsigned version)
{
    char b[ED_MAX * 8];
    unsigned n = pr_u8(r);
    for (unsigned i = 0; i < n && !r->err; i++) {
        pr_str(r, b, sizeof b);
        bool en = pr_u8(r) != 0;
        unsigned col = pr_u8(r);
        if (r->err || i >= NFUNC)
            continue;
        ed_deserialize(&g.f[i].ed, b);
        g.f[i].enabled = en;
        g.f[i].color = (uint8_t)(col % NCOLORS);
    }
    double *dst[] = {&g.xmin, &g.xmax, &g.xscl, &g.ymin, &g.ymax, &g.yscl, &g.tblstart, &g.tbldelta};
    unsigned nv = pr_u8(r);
    double v[8];
    for (unsigned i = 0; i < nv && i < 8; i++)
        v[i] = pr_f64(r);
    if (r->err || nv < 8)
        return;
    for (int i = 0; i < 8; i++)
        if (!isfinite(v[i]))
            return;
    double save[8];
    for (int i = 0; i < 8; i++)
        save[i] = *dst[i], *dst[i] = v[i];
    if (window_check() || g.tbldelta == 0) /* keep the defaults if the saved values are invalid */
        for (int i = 0; i < 8; i++)
            *dst[i] = save[i];
    funcs_invalidate();
}

/* persist reset() (MEM > Reset all): empty enabled functions with colours
 * 0..5, ZStandard window, table from 0 in steps of 1 */
static void graph_reset(void)
{
    for (int i = 0; i < NFUNC; i++) {
        ed_clear(&g.f[i].ed);
        g.f[i].enabled = true;
        g.f[i].color = (uint8_t)(i % NCOLORS);
    }
    g.xmin = -10, g.xmax = 10, g.xscl = 1, g.ymin = -10, g.ymax = 10, g.yscl = 1;
    g.tblstart = 0, g.tbldelta = 1;
    funcs_invalidate();
}

const persist_def_t persist_graph = {"graph", 1, graph_save, graph_load, graph_reset};
