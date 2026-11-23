/*
 * Registration tables: the one place where features plug into the application
 * (see registry.h and app/EXTENDING.md). Each feature adds ONE line to the
 * table(s) it needs, in its own section, and declares the object it adds in
 * the matching `extern` block. The sections are separated so that features
 * developed in parallel do not touch the same lines.
 */
#include "registry.h"
#include <string.h>

/* ================================================================== APPS ==
 * Shown in this order in the APPS menu (APPS key). */
extern const app_def_t app_help, app_graph, app_table, app_mem, app_settings;
/* -- 2D math -- (no app: templates are on the home screen soft keys) */
/* -- graphing -- */
extern const app_def_t app_graph_calc;
extern const app_def_t app_graph3d;
extern const app_def_t app_animation;
extern const app_def_t app_mathquest; /* Math Quest code module (app/mquest_core.c) */
/* -- statistics -- */
extern const app_def_t app_stat, app_distr, app_statplot;
/* -- matrix / solvers / finance / base-n / units -- */
extern const app_def_t app_matrix, app_solver, app_finance, app_basen, app_units;
/* -- spreadsheet / microSD tools -- */
extern const app_def_t app_sheet, app_sdtools;
/* -- RPN -- */
/* -- geometry -- */
extern const app_def_t app_geometry;
/* -- python -- */
extern const app_def_t app_python; /* Python code module (app/py_core.c) */
extern const app_def_t app_notebook; /* the Notebook app in the Python module (app/py_core.c) */

const app_def_t *const app_registry[] = {
    &app_graph,
    &app_table,

    /* -- 2D math -- */

    /* -- graphing -- */
    &app_graph_calc,
    &app_graph3d,
    &app_animation,
    &app_mathquest,

    /* -- statistics -- */
    &app_stat,
    &app_distr,
    &app_statplot,

    /* -- matrix / solvers / finance / base-n / units -- */
    &app_matrix,
    &app_solver,
    &app_finance,
    &app_basen,
    &app_units,

    /* -- spreadsheet / microSD tools -- */
    &app_sheet,
    &app_sdtools,

    /* -- RPN -- */

    /* -- geometry -- */
    &app_geometry,

    /* -- python -- */
    &app_python,
    &app_notebook,

    &app_settings,
    &app_mem,
    &app_help,
};
const int app_registry_count = (int)(sizeof app_registry / sizeof app_registry[0]);

/* Look an app up by its id (console `app ID`, soft keys, global key actions);
 * NULL if none is registered under that id. Linear search: the table is short. */
const app_def_t *app_find(const char *id)
{
    for (int i = 0; i < app_registry_count; i++)
        if (!strcmp(app_registry[i]->id, id))
            return app_registry[i];
    return NULL;
}

/* ================================================================= STATE ==
 * Saved/restored in this order (the CAS last: it is the largest and is
 * dropped first if the session buffer overflows; persist_cas itself also
 * skips single records that do not fit, app/vars.c). */
extern const persist_def_t persist_settings, persist_graph, persist_home, persist_cas;
/* -- 2D math -- */
extern const persist_def_t persist_m2d, persist_theme;
/* -- graphing -- */
extern const persist_def_t persist_graphx;
extern const persist_def_t persist_graph3d;
extern const persist_def_t persist_anim;
extern const persist_def_t persist_modx_save; /* code modules' saved bytes (app/modx.c) */
/* -- statistics -- */
extern const persist_def_t persist_stat_lists, persist_stat_plots;
/* -- matrix / solvers / finance / base-n / units -- */
extern const persist_def_t persist_matrix, persist_solver, persist_finance, persist_basen;
/* -- spreadsheet -- */
extern const persist_def_t persist_sheet;
/* -- RPN -- */
extern const persist_def_t persist_rpn;
/* -- input comforts (clipboard) -- */
extern const persist_def_t persist_clip;
/* -- geometry -- */
extern const persist_def_t persist_geo;
/* -- result actions / auto-simplify -- */
extern const persist_def_t persist_simplify;
/* -- the event log (core/evlog.h; app/session.c) -- */
extern const persist_def_t persist_evlog;

const persist_def_t *const persist_registry[] = {
    &persist_settings,
    &persist_graph,

    /* -- 2D math -- */
    &persist_m2d,
    &persist_theme,

    /* -- graphing -- */
    &persist_graphx,
    &persist_graph3d,
    &persist_anim,
    &persist_modx_save,

    /* -- statistics -- */
    &persist_stat_lists,
    &persist_stat_plots,

    /* -- matrix / solvers / finance / base-n / units -- */
    &persist_matrix,
    &persist_solver,
    &persist_finance,
    &persist_basen,

    /* -- spreadsheet -- */
    &persist_sheet,

    /* -- RPN -- */
    &persist_rpn,

    /* -- input comforts (clipboard) -- */
    &persist_clip,
    /* -- geometry -- */
    &persist_geo,

    /* -- result actions / auto-simplify -- */
    &persist_simplify,

    /* -- the event log -- */
    &persist_evlog,

    &persist_home,
    &persist_cas,
};
const int persist_registry_count = (int)(sizeof persist_registry / sizeof persist_registry[0]);

/* ================================================================== VARS ==
 * Categories on the first page of the VARS menu (VARS key). */
extern const vars_provider_t vars_cas_values, vars_cas_functions, vars_yvars;
/* -- graphing -- */
/* -- statistics -- */
extern const vars_provider_t vars_stat_lists, vars_stat_results;
/* -- matrix / solvers / finance / base-n / units -- */
extern const vars_provider_t vars_matrix;

const vars_provider_t *const vars_registry[] = {
    &vars_cas_values,
    &vars_cas_functions,
    &vars_yvars,

    /* -- graphing -- */

    /* -- statistics -- */
    &vars_stat_lists,
    &vars_stat_results,

    /* -- matrix / solvers / finance / base-n / units -- */
    &vars_matrix,
};
const int vars_registry_count = (int)(sizeof vars_registry / sizeof vars_registry[0]);

/* ================================================================== MODE ==
 * Rows of the MODE screen, top to bottom. */
extern const mode_row_t mode_simplify; /* result actions (result_actions.c) */
extern const mode_row_t mode_angle, mode_result, mode_digits, mode_format, mode_decimals, mode_complex,
    mode_grid, mode_axes, mode_light, mode_usbpwr;
/* -- 2D math -- */
extern const mode_row_t mode_m2d_display, mode_m2d_preview, mode_m2d_repeat, mode_theme, mode_font;
/* -- graphing -- */
extern const mode_row_t mode_graphmode;
/* -- statistics -- */
/* -- matrix / solvers / finance / base-n / units -- */
/* -- RPN -- */
extern const mode_row_t mode_entry;

const mode_row_t *const mode_registry[] = {
    &mode_angle,
    &mode_result,
    &mode_digits,
    &mode_format,
    &mode_decimals,
    &mode_complex,

    /* -- 2D math -- */
    &mode_m2d_display,
    &mode_m2d_preview,
    &mode_m2d_repeat,
    &mode_theme,
    &mode_font,

    /* -- graphing -- */
    &mode_graphmode,

    /* -- statistics -- */

    /* -- matrix / solvers / finance / base-n / units -- */

    /* -- RPN -- */
    &mode_entry,

    /* -- result actions / auto-simplify -- */
    &mode_simplify,

    &mode_grid,
    &mode_axes,
    &mode_light,
    &mode_usbpwr,
};
const int mode_registry_count = (int)(sizeof mode_registry / sizeof mode_registry[0]);

/* ======================================================== GRAPH OVERLAYS ==
 * Layers drawn into the GRAPH screen in graph coordinates (stat plots), in
 * this order, with TRACE and ZoomFit support (app/graph_overlay.h). */
#include "graph_overlay.h"
/* graph_overlay_stub (gr_overlay.c) is never active: a placeholder that keeps
 * the table non-empty; graph_overlay_stat (stat_overlay.c) draws the stat plots. */
extern const graph_overlay_t graph_overlay_stub;
extern const graph_overlay_t graph_overlay_stat;
/* -- statistics -- */
/* -- geometry -- (graph_overlay_geo, app/geo_core.c: the construction, View > Show on GRAPH) */
extern const graph_overlay_t graph_overlay_geo;

const graph_overlay_t *const graph_overlay_registry[] = {
    &graph_overlay_stub,
    &graph_overlay_stat,

    /* -- statistics -- */

    /* -- geometry -- */
    &graph_overlay_geo,
};
const int graph_overlay_registry_count = (int)(sizeof graph_overlay_registry / sizeof graph_overlay_registry[0]);
