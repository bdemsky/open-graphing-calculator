/*
 * The Geometry code module ("geometry", interface modx; app/geo_core.h): the
 * module entry, the saved construction, the console command `geo` used by the
 * UI tests, and the drawing of the construction on the GRAPH screen (View >
 * Show on GRAPH). The screen and its tools are in geo_ui.c, the model in
 * geo_kernel.c; the core keeps the screen, menus, blob and overlay objects
 * and forwards them here (app/geo_core.c).
 *
 * Saved construction (session blob "geo" version 1, kept by the core; files
 * are "GEO1" + the same payload at /CALC/GEO/NAME.GEO, written by the core):
 * u8 view options, f64 origin x, origin y, scale, then the construction
 * (geo_save(): objects only, no undo).
 */
#include <math.h>
#include <stddef.h>
#include <string.h>
#include "geo.h"
#include "theme.h"
#include "geo_mod.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

/* ------------------------------------------------------------ the blob */

static void payload_save(pw_t *w)
{
    pw_u8(w, geo_view.opts);
    pw_f64(w, geo_view.ox);
    pw_f64(w, geo_view.oy);
    pw_f64(w, geo_view.scale);
    geo_save(w);
}

/* Read view + construction; on a corrupt payload keep an empty construction
 * and the standard view. */
static bool payload_load(pr_t *r)
{
    unsigned o = pr_u8(r);
    double ox = pr_f64(r), oy = pr_f64(r), sc = pr_f64(r);
    if (GI->pr_failed(r) || !geo_load(r) || !(sc > 0) || !isfinite(ox) || !isfinite(oy)) {
        geo_clear();
        geo_view_reset();
        geo_ui_reset();
        geo_view_changed();
        geo_changed();
        return false;
    }
    geo_view.opts = (uint8_t)o, geo_view.ox = ox, geo_view.oy = oy, geo_view.scale = sc;
    geo_ui_reset();
    geo_view_changed();
    geo_changed();
    return true;
}

static void payload_reset(void)
{
    geo_clear();
    geo_view_reset();
    geo_ui_reset();
    geo_view_changed();
}

/* ------------------------------------------------------------ console */

typedef struct {
    const char *key;
    char val[96];
    bool found;
} geo_lookup_t;
static void lookup_kv(const char *k, const char *v, void *u)
{
    geo_lookup_t *l = u;
    if (!l->found && !strcmp(k, l->key)) {
        snprintf(l->val, sizeof l->val, "%s", v);
        l->found = true;
    }
}
static void print_kv(const char *k, const char *v, void *u) { con_printf("%s=%s\n", k, v); }

/* geo state | list | expect KEY|NAME TEXT | cursor X Y | new | save NAME | load NAME
 * `expect` compares a state key (tool, count, nsel, cursor, grab, hover,
 * prompt, message, scale, undo) or an object's value text ("A = (1, 2)")
 * exactly; a failure counts for the simulator's exit status. */
int geo_console_mod(void *rw, int argc, char **argv)
{
    (void)rw;
    if (argc < 2)
        return con_error("usage: geo state|list|expect|cursor|new|save|load");
    const char *c = argv[1];
    if (!strcmp(c, "state")) {
        geo_ui_state(print_kv, NULL);
        return 0;
    }
    if (!strcmp(c, "list")) {
        char b[96];
        for (int i = 0; i < geo.n; i++) {
            if (geo.obj[i].flags & GF_DELETED)
                continue;
            geo_value_text(i, b, sizeof b);
            con_printf("%s %s%s | %s\n", geo.obj[i].name, geo_type_name(geo.obj[i].type),
                       geo.obj[i].flags & GF_HIDDEN ? " hidden" : "", b);
        }
        return 0;
    }
    if (!strcmp(c, "expect") && argc >= 3) {
        const char *want = argc > 3 ? argv[3] : "";
        geo_lookup_t l = {argv[2], "", false};
        geo_ui_state(lookup_kv, &l);
        if (!l.found) {
            int i = geo_find(argv[2]);
            if (i < 0) {
                GI->expect_failed();
                return con_error("no object %s", argv[2]);
            }
            geo_value_text(i, l.val, sizeof l.val);
        }
        if (strcmp(l.val, want)) {
            GI->expect_failed();
            return con_error("geo %s is \"%s\", expected \"%s\"", argv[2], l.val, want);
        }
        return 0;
    }
    if (!strcmp(c, "cursor") && argc >= 4) {
        geo_set_cursor(atof(argv[2]), atof(argv[3]));
        return 0;
    }
    if (!strcmp(c, "new")) {
        geo_clear();
        geo_ui_reset();
        geo_changed();
        if (GI->shown())
            GI->redraw();
        return 0;
    }
    if ((!strcmp(c, "save") || !strcmp(c, "load")) && argc >= 3) {
        int r = c[0] == 's' ? GI->file_save(argv[2]) : GI->file_load(argv[2]);
        if (r)
            return con_error("%s", GI->fs_strerror(r));
        if (GI->shown())
            GI->redraw();
        return 0;
    }
    return con_error("usage: geo state|list|expect|cursor|new|save|load");
}

/* ------------------------------------------------------------ GRAPH overlay */

/* Clip the world line of extent sub to the graph window (Liang-Barsky) and
 * draw it; gov_line alone would clamp far coordinates and bend the line. */
static void ov_line(double ax, double ay, double bx, double by, int sub, uint16_t col)
{
    double box[4], t0, t1, dx = bx - ax, dy = by - ay;
    gov_window(&box[0], &box[1], &box[2], &box[3]);
    if (geo_clip(ax, ay, dx, dy, sub, box, &t0, &t1))
        gov_line(ax + t0 * dx, ay + t0 * dy, ax + t1 * dx, ay + t1 * dy, col);
}

typedef struct {
    bool have;
    double x, y;
} ov_pen_t;
static void ov_locus_cb(int i, bool ok, double x, double y, void *u)
{
    ov_pen_t *p = u;
    if (ok && p->have)
        ov_line(p->x, p->y, x, y, GL_SEGMENT, C_MAGENTA);
    p->have = ok, p->x = x, p->y = y;
}

static bool ov_active(void) { return (geo_view.opts & GO_ONGRAPH) && geo.n > 0; }

/* Draw the construction in graph coordinates: lines clipped to the window,
 * circles as 96 chords (ellipses when the window is not square), polygons'
 * edges, loci, then the points. Uses the graph window as the kernel's range
 * meanwhile, restoring the geometry view's range afterwards. */
static void ov_draw(void)
{
    double sx0 = geo.rxmin, sx1 = geo.rxmax, sy0 = geo.rymin, sy1 = geo.rymax;
    gov_window(&geo.rxmin, &geo.rxmax, &geo.rymin, &geo.rymax);
    geo_update();
    for (int i = 0; i < geo.n; i++) {
        const geo_obj_t *o = &geo.obj[i];
        const geo_shape_t *s = &geo.shape[i];
        if ((o->flags & (GF_DELETED | GF_HIDDEN)) || !s->ok)
            continue;
        if (s->kind == GK_LINE)
            ov_line(s->x1, s->y1, s->x2, s->y2, s->sub, C_BLACK);
        else if (s->kind == GK_CIRCLE)
            for (int k = 0; k < 96; k++) {
                double t0 = k * (2 * M_PI / 96), t1 = (k + 1) * (2 * M_PI / 96);
                ov_line(s->x1 + s->r * cos(t0), s->y1 + s->r * sin(t0), s->x1 + s->r * cos(t1),
                        s->y1 + s->r * sin(t1), GL_SEGMENT, C_NAVY);
            }
        else if (s->kind == GK_POLYGON)
            for (int k = 0; k < o->npar; k++) {
                const geo_shape_t *a = &geo.shape[o->par[k]], *b = &geo.shape[o->par[(k + 1) % o->npar]];
                ov_line(a->x1, a->y1, b->x1, b->y1, GL_SEGMENT, C_BROWN);
            }
        else if (s->kind == GK_LOCUS) {
            ov_pen_t pen = {false, 0, 0};
            geo_locus(i, 160, ov_locus_cb, &pen);
        }
    }
    for (int i = 0; i < geo.n; i++)
        if (!(geo.obj[i].flags & (GF_DELETED | GF_HIDDEN)) && geo.shape[i].ok && geo.shape[i].kind == GK_POINT)
            gov_mark(geo.shape[i].x1, geo.shape[i].y1, 1 /* GOV_BOX */, geo_is_draggable(i) ? C_BLUE : C_DKGRAY);
    geo.rxmin = sx0, geo.rxmax = sx1, geo.rymin = sy0, geo.rymax = sy1;
    geo_update();
}

/* TRACE walks through the points of the construction (one set). */
static int ov_npoints(void) { return geo.n; }
static bool ov_point(int i, double *x, double *y)
{
    if (i < 0 || i >= geo.n || (geo.obj[i].flags & (GF_DELETED | GF_HIDDEN)) || !geo.shape[i].ok ||
        geo.shape[i].kind != GK_POINT)
        return false;
    *x = geo.shape[i].x1, *y = geo.shape[i].y1;
    return true;
}
static bool ov_bounds(double *x0, double *x1, double *y0, double *y1)
{
    bool any = false;
    for (int i = 0; i < geo.n; i++) {
        double x, y;
        if (!ov_point(i, &x, &y))
            continue;
        if (!any)
            *x0 = *x1 = x, *y0 = *y1 = y, any = true;
        *x0 = fmin(*x0, x), *x1 = fmax(*x1, x), *y0 = fmin(*y0, y), *y1 = fmax(*y1, y);
    }
    return any && *x1 > *x0 && *y1 > *y0;
}

void geo_io_ops(geo_ops_t *ops)
{
    ops->save = payload_save;
    ops->load = payload_load;
    ops->reset = payload_reset;
    ops->ov_active = ov_active;
    ops->ov_draw = ov_draw;
    ops->ov_npoints = ov_npoints;
    ops->ov_point = ov_point;
    ops->ov_bounds = ov_bounds;
}

/* ------------------------------------------------------------ module entry */

const uint32_t geo_module_rw_size = sizeof(geo_rw_t);
int geo_module_init(const modx_api_t *api, void *rw, modx_ops_t *ops);

/* Check the core's tables, fill the ops, attach (the core gives the saved
 * construction back through ops.load, or ops.reset when there is none). The
 * core points MODX_RW at rw while it calls init. */
int geo_module_init(const modx_api_t *api, void *rw, modx_ops_t *ops)
{
    if (api->abi != MODX_ABI || api->size < offsetof(modx_api_t, geo_core) + sizeof api->geo_core || !api->geo_core)
        return -1;
    const geo_core_t *c = api->geo_core();
    if (!c || c->abi != GEO_CORE_ABI || c->size < sizeof(geo_core_t) || MODX_RW() != rw)
        return -2;
    geo_rw_t *G = rw;
    G->api = api;
    G->core = c;
    G->ops.size = sizeof G->ops;
    geo_ui_ops(&G->ops);
    geo_io_ops(&G->ops);
    ops->size = sizeof *ops;
    ops->console = geo_console_mod;
    c->attach(&G->ops);
    return 0;
}

MODX_DESCRIPTOR(GEO_MODULE, sizeof(geo_rw_t), geo_module_init);
