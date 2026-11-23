/* Graph cells of the notebook (app/nbdoc.h, help page "graph-cells"): a plot inline in the home
 * screen with its own functions, window and style, interactive in place, opened in the graph app.
 *
 * The cell keeps its state as text in the field NBF_GRAPH (the "spec"), one item a line:
 *   w XMIN XMAX YMIN YMAX XSCL YSCL      the window
 *   o GRID AXES HEIGHT                   grid and axes (0/1), the height on the home screen (px)
 *   f ON COLOR STYLE EXPR                a function y(x) as typed (display text), up to six
 *   a TMIN TMAX SECS MODE                animated cells: t's range, seconds per sweep, loop/bounce/once
 * and, for older readers and the browser page, src "f(x), xmin, xmax" of its first function and
 * plot = that function's CAS text.
 *
 * Animation. A cell whose functions that are on use t (3D: not a curve's own t) is animated: its
 * spec has the "a" line (written only then; defaults 0..2π, 4 s, loop) and it is plotted at a
 * value of t: the start of the range, or the frame the home screen plays or steps it to (A: one
 * cell at a time; the Animation module's timing through app/anim_core.c, ANIM_T_CELL).
 *
 * Drawing. A cell is plotted by the graph app's own code (graph_plot_view, gr_plot.c): its
 * functions, window and style are swapped into the Y= state (g.f, gx.fstyle, the WINDOW) for the
 * moment of the plot and the plot view gr_vp set to the image, drawn offscreen (gfx_target) and
 * kept in a small cache of images (SDRAM), so scrolling the notebook re-plots nothing; the cache
 * key is the spec's hash, the size and the theme.
 *
 * In place (app/home.c sends keys and gestures of the selected cell): arrows pan by an eighth of
 * the window, + and − zoom by 2 about the centre; a drag pans (the cached image follows the finger,
 * re-plotted on release), a pinch zooms (gr_preview / gr_pinch_window: the graph app's gesture
 * code), a drag on the bottom edge changes the height. ENTER binds the cell to the graph app
 * (gcell_bind: its state becomes the Y= state); coming back to the home screen writes the Y=
 * state back into the cell (gcell_unbind). */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "anim.h"
#include "g3d.h"
#include "graph.h"
#include "m2d.h"
#include "nbdoc.h"
#include "theme.h"
#include "ui.h"

typedef struct {
    char fn[NFUNC][ED_MAX * 2]; /* display text ("" none) */
    uint8_t on[NFUNC], color[NFUNC], style[NFUNC];
    double w[6]; /* xmin xmax ymin ymax xscl yscl */
    uint8_t grid, axes;
    int h;
    double an[4]; /* animation: t from, to, seconds per sweep, mode (anim.h ANIM_LOOP ...) */
} gspec_t;

#define NSLOT 6
typedef struct {
    char id[24];
    uint32_t key;
    int w, h;
    uint32_t used; /* LRU clock */
    uint16_t *pix;
} slot_t;

static struct {
    uint16_t *scratch;              /* offscreen frame (LCD_WIDTH x LCD_HEIGHT) */
    slot_t slot[NSLOT];
    uint32_t clock;
    gspec_t sp, tmp;                /* working specs */
    func_t savef[NFUNC];            /* the Y= state while a cell is plotted */
    g3_state_t g3keep, g3cell;      /* the 3D graph's state while a 3D cell is drawn; a 3D cell's state */
    uint8_t savestyle[NFUNC];
    double an3[4];                  /* a 3D cell's animation settings */
    char spec[NFUNC * ED_MAX * 2 + 256];
} *G;
/* the animated cell the home screen plays or steps, and its t ("" none: cells show their start) */
static struct {
    char id[24];
    double t;
} A;
static char bound[24];             /* the cell the graph app is bound to ("" none) */
/* a gesture on a cell: pan (drag) or resize (bottom edge) */
static struct {
    int cell, mode, h0, live_h; /* mode 0 none, 1 pan, 2 resize, 3 pinch */
} T = {-1, 0, 0, 0};

size_t gcell_mem_init(void *mem, bool big)
{
    if (!big) {
        G = NULL;
        return 0;
    }
    uint8_t *a = mem;
    size_t used = (sizeof *G + 7) & ~(size_t)7;
    G = (void *)a;
    memset(G, 0, sizeof *G);
    G->scratch = (uint16_t *)(void *)(a + used);
    used += (size_t)LCD_WIDTH * LCD_HEIGHT * 2;
    for (int i = 0; i < NSLOT; i++) {
        G->slot[i].pix = (uint16_t *)(void *)(a + used);
        used += (size_t)LCD_WIDTH * BODY_H * 2;
    }
    return used;
}

/* ------------------------------------------------------------ the spec */
/* animation settings: t from 0 to 2π in 4 s, loop */
static void an_defaults(double an[4]) { an[0] = 0, an[1] = 2 * M_PI, an[2] = 4, an[3] = ANIM_LOOP; }
/* those of the graph app's animation of t, when it has one (a cell made or written back from it) */
static void an_from_app(double an[4])
{
    const anim_cfg_t *c = anim_cfg('t', false, 0, 0);
    if (c && c->start < c->end && c->secs > 0)
        an[0] = c->start, an[1] = c->end, an[2] = c->secs, an[3] = c->mode >= 0 && c->mode < ANIM_NMODES ? (int)c->mode : 0;
}

static int nums(const char *t, const char *e, int skip, double *v, int max);
/* the "a" line of spec sp into an (defaults without one, or a bad one) */
static void an_parse(const char *sp, double an[4])
{
    an_defaults(an);
    const char *a = sp && sp[0] == 'a' && sp[1] == ' ' ? sp : sp ? strstr(sp, "\na ") : NULL;
    double v[4];
    if (a && a[0] == '\n')
        a++;
    if (a && nums(a, strchr(a, '\n'), 2, v, 4) == 4 && v[0] < v[1] && isfinite(v[0] + v[1]) && v[2] >= 0.1 &&
        v[2] <= 3600)
        an[0] = v[0], an[1] = v[1], an[2] = v[2], an[3] = v[3] >= 0 && v[3] < ANIM_NMODES ? (int)v[3] : ANIM_LOOP;
}

/* p..e (display text) names t, not as a call: the identifiers are letters, digits, _ from a letter */
static bool names_t(const char *p, const char *e)
{
    while (p < e) {
        if (isalpha((uint8_t)*p) || *p == '_') {
            const char *s = p;
            while (p < e && (isalnum((uint8_t)*p) || *p == '_'))
                p++;
            if (p - s == 1 && *s == 't' && (p == e || *p != '('))
                return true;
        } else {
            p++;
        }
    }
    return false;
}

/* a spec (2D or 3D) is animated: a function that is on uses t (3D: not a curve's own t) */
static bool spec_anim(const char *sp)
{
    bool d3 = sp && !strncmp(sp, "3d", 2);
    for (const char *t = sp; t && *t;) {
        const char *e = strchr(t, '\n'), *end = e ? e : t + strlen(t);
        if (t[0] == 'f' && t[1] == ' ') {
            char *p;
            long on = strtol(t + 2, &p, 10);
            strtol(p, &p, 10);
            long k = strtol(p, &p, 10);
            if (on && !(d3 && k == G3K_CURVE) && names_t(p, end))
                return true;
        }
        t = e ? e + 1 : NULL;
    }
    return false;
}

/* the "a" line after a formatted spec, when it is animated */
static void an_append(char *out, size_t n, const double an[4])
{
    size_t o = strlen(out);
    if (o + 1 < n && spec_anim(out))
        snprintf(out + o, n - o, "\na %.15g %.15g %.15g %d", an[0], an[1], an[2], (int)an[3]);
}

static void spec_defaults(gspec_t *s)
{
    memset(s, 0, sizeof *s);
    s->w[0] = -10, s->w[1] = 10, s->w[2] = -10, s->w[3] = 10, s->w[4] = 1, s->w[5] = 1;
    s->axes = 1;
    s->h = GCELL_H;
    for (int i = 0; i < NFUNC; i++)
        s->on[i] = 1, s->color[i] = (uint8_t)(i % NCOLORS);
    an_defaults(s->an);
}

static int clamp_h(int h) { return h < GCELL_HMIN ? GCELL_HMIN : h > BODY_H ? BODY_H : h; }

/* up to `max` numbers of the line t (ending at e or its NUL), after `skip` characters; -> how many */
static int nums(const char *t, const char *e, int skip, double *v, int max)
{
    char b[200];
    size_t n = e ? (size_t)(e - t) : strlen(t);
    if (n >= sizeof b)
        n = sizeof b - 1;
    memcpy(b, t, n);
    b[n] = 0;
    const char *q = (size_t)skip < n ? b + skip : b + n;
    int k = 0;
    for (char *p; k < max; k++, q = p) {
        v[k] = strtod(q, &p);
        if (p == q)
            break;
    }
    return k;
}

static void spec_parse(const char *t, gspec_t *s)
{
    spec_defaults(s);
    an_parse(t, s->an);
    int nf = 0;
    while (t && *t) {
        const char *e = strchr(t, '\n');
        char *p;
        double v[6];
        if (t[0] == 'w' && t[1] == ' ') {
            int k = nums(t, e, 2, v, 6);
            if (k >= 4 && v[0] < v[1] && v[2] < v[3] && isfinite(v[0] + v[1] + v[2] + v[3])) {
                if (k < 6 || !(v[4] > 0) || !(v[5] > 0)) /* (scales: 1 unless given) */
                    v[4] = v[5] = 1;
                memcpy(s->w, v, sizeof v);
            }
        } else if (t[0] == 'o' && t[1] == ' ' && nums(t, e, 2, v, 3) == 3) {
            s->grid = v[0] != 0, s->axes = v[1] != 0, s->h = clamp_h((int)v[2]);
        } else if (t[0] == 'f' && t[1] == ' ' && nf < NFUNC) {
            long on = strtol(t + 2, &p, 10), c = strtol(p, &p, 10), st = strtol(p, &p, 10);
            if (*p == ' ')
                p++;
            size_t n = e ? (size_t)(e - p) : strlen(p);
            if (n >= sizeof s->fn[0])
                n = sizeof s->fn[0] - 1;
            memcpy(s->fn[nf], p, n);
            s->fn[nf][n] = 0;
            s->on[nf] = on != 0;
            s->color[nf] = (uint8_t)((unsigned long)c % NCOLORS);
            s->style[nf] = (uint8_t)((unsigned long)st % GS_NSTYLES);
            nf++;
        }
        t = e ? e + 1 : NULL;
    }
}

static void spec_format(const gspec_t *s, char *out, size_t n)
{
    size_t o = (size_t)snprintf(out, n, "w %.15g %.15g %.15g %.15g %.15g %.15g\no %d %d %d", s->w[0], s->w[1], s->w[2],
                                s->w[3], s->w[4], s->w[5], s->grid, s->axes, s->h);
    for (int i = 0; i < NFUNC && o < n; i++)
        if (s->fn[i][0])
            o += (size_t)snprintf(out + o, n - o, "\nf %d %d %d %s", s->on[i], s->color[i], s->style[i], s->fn[i]);
}

static uint32_t hash(const char *s, uint32_t h)
{
    while (*s)
        h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

/* display text -> CAS text (a scratch editor, not an undo step) */
static void to_cas(const char *disp, char *out, size_t n)
{
    void (*hook)(editor_t *) = ed_change_hook;
    ed_change_hook = NULL;
    m2d_set_linear(&gx_tmp_ed, disp);
    ed_cas(&gx_tmp_ed, out, n);
    ed_change_hook = hook;
}

/* the spec into cell c, with the src / plot / range older readers use */
static void spec_store(nb_cell_t *c, const gspec_t *s)
{
    char legacy[ED_MAX * 2 + 64], cas[ED_MAX * 4];
    spec_format(s, G->spec, sizeof G->spec);
    an_append(G->spec, sizeof G->spec, s->an);
    nbd_set(c, NBF_GRAPH, G->spec);
    int f = -1;
    for (int i = 0; i < NFUNC && f < 0; i++)
        if (s->fn[i][0] && s->on[i])
            f = i;
    c->xmin = s->w[0], c->xmax = s->w[1];
    if (f < 0) {
        nbd_set(c, NBF_SRC, NULL);
        nbd_set(c, NBF_PLOT, NULL);
        return;
    }
    snprintf(legacy, sizeof legacy, "%s, %.6g, %.6g", s->fn[f], s->w[0], s->w[1]);
    nbd_set(c, NBF_SRC, legacy);
    to_cas(s->fn[f], cas, sizeof cas);
    nbd_set(c, NBF_PLOT, cas);
}

/* ------------------------------------------------------------ 3D cells */
/* A 3D graph cell's spec (first line "3d"):
 *   3d AZIM ELEV ZOOM STYLE COLORMODE AXES BOX GRID TSTEPS
 *   w XMIN XMAX YMIN YMAX ZMIN ZMAX
 *   p UMIN UMAX VMIN VMAX TMIN TMAX
 *   o 0 0 HEIGHT
 *   c CONTOURS MAP LEVELS                     (only when not the defaults 0 0 9)
 *   f ON COLOR KIND EXPR[TAB EXPR TAB EXPR]   (z(x,y); x, y, z of (u,v) or of t) */
static bool is3d(int i) { return !strncmp(nbd_get(&nbd->cell[i], NBF_GRAPH), "3d ", 3); }
bool gcell_is3d(int i) { return i >= 0 && i < nbd->ncells && nbd->cell[i].kind == NBK_GRAPH && is3d(i); }

static void s3_defaults(g3_state_t *st)
{
    memcpy(&G->g3keep, &g3, sizeof g3);
    g3_defaults();
    memcpy(st, &g3, sizeof g3);
    memcpy(&g3, &G->g3keep, sizeof g3);
}

static void gt_from(gtext_t *t, const char *disp, size_t n)
{
    char b[ED_MAX * 2];
    if (n >= sizeof b)
        n = sizeof b - 1;
    memcpy(b, disp, n);
    b[n] = 0;
    void (*hook)(editor_t *) = ed_change_hook;
    ed_change_hook = NULL;
    m2d_set_linear(&gx_tmp_ed, b);
    if (!gtext_from_editor(t, &gx_tmp_ed))
        t->len = 0;
    ed_change_hook = hook;
}
static void gt_to(const gtext_t *t, char *out, size_t n)
{
    void (*hook)(editor_t *) = ed_change_hook;
    ed_change_hook = NULL;
    gtext_to_editor(t, &gx_tmp_ed);
    ed_display(&gx_tmp_ed, out, n);
    ed_change_hook = hook;
}

static int s3_parse(const char *t, g3_state_t *st)
{
    s3_defaults(st);
    int h = GCELL_H, nf = 0;
    for (int k = 0; k < G3_NENT; k++)
        memset(st->ent[k].ex, 0, sizeof st->ent[k].ex);
    while (t && *t) {
        const char *e = strchr(t, '\n');
        double v[9];
        int k = t[0] != 'f' ? nums(t, e, 2, v, 9) : 0;
        if (t[0] == '3' && k >= 9) {
            st->azim = v[0], st->elev = v[1], st->zoom = v[2] > 0.05 ? v[2] : 1;
            st->style = (uint8_t)v[3], st->colormode = (uint8_t)v[4], st->axes = v[5] != 0, st->box = v[6] != 0;
            st->grid = v[7], st->tsteps = v[8];
        } else if (t[0] == 'w' && k >= 6 && v[0] < v[1] && v[2] < v[3] && v[4] < v[5]) {
            st->xmin = v[0], st->xmax = v[1], st->ymin = v[2], st->ymax = v[3], st->zmin = v[4], st->zmax = v[5];
        } else if (t[0] == 'p' && k >= 6) {
            st->umin = v[0], st->umax = v[1], st->vmin = v[2], st->vmax = v[3], st->tmin = v[4], st->tmax = v[5];
        } else if (t[0] == 'o' && k >= 3) {
            h = clamp_h((int)v[2]);
        } else if (t[0] == 'c' && k >= 3) {
            st->contours = v[0] != 0, st->map = v[1] != 0;
            st->levels = v[2] >= G3_LEVELS_MIN && v[2] <= G3_LEVELS_MAX ? round(v[2]) : G3_LEVELS_DEF;
        } else if (t[0] == 'f' && nf < G3_NENT) {
            char *p2;
            long on = strtol(t + 2, &p2, 10), c = strtol(p2, &p2, 10), kind = strtol(p2, &p2, 10);
            if (*p2 == ' ')
                p2++;
            g3_entry_t *en = &st->ent[nf++];
            en->enabled = on != 0, en->color = (uint8_t)((unsigned long)c % NCOLORS);
            en->kind = (uint8_t)((unsigned long)kind % G3K_N);
            for (int part = 0; part < 3 && p2; part++) {
                const char *tab = strchr(p2, '\t');
                const char *end = tab && (!e || tab < e) ? tab : e ? e : p2 + strlen(p2);
                gt_from(&en->ex[part], p2, (size_t)(end - p2));
                p2 = tab && (!e || tab < e) ? (char *)tab + 1 : NULL;
            }
        }
        t = e ? e + 1 : NULL;
    }
    return h;
}

static void s3_format(const g3_state_t *st, int h, char *out, size_t n)
{
    size_t o = (size_t)snprintf(out, n,
                                "3d %.15g %.15g %.15g %d %d %d %d %.15g %.15g\nw %.15g %.15g %.15g %.15g %.15g %.15g\n"
                                "p %.15g %.15g %.15g %.15g %.15g %.15g\no 0 0 %d",
                                st->azim, st->elev, st->zoom, st->style, st->colormode, st->axes, st->box, st->grid,
                                st->tsteps, st->xmin, st->xmax, st->ymin, st->ymax, st->zmin, st->zmax, st->umin,
                                st->umax, st->vmin, st->vmax, st->tmin, st->tmax, h);
    if (st->contours || st->map || st->levels != G3_LEVELS_DEF)
        o += (size_t)snprintf(out + o, n - o, "\nc %d %d %d", st->contours, st->map, (int)st->levels);
    char b[ED_MAX * 2];
    for (int k = 0; k < G3_NENT && o < n; k++) {
        const g3_entry_t *en = &st->ent[k];
        bool any = false;
        for (int part = 0; part < 3; part++)
            any |= en->ex[part].len > 0;
        if (!any)
            continue;
        o += (size_t)snprintf(out + o, n - o, "\nf %d %d %d ", en->enabled, en->color, en->kind);
        int parts = en->kind == G3K_FUNC ? 1 : 3;
        for (int part = 0; part < parts && o < n; part++) {
            gt_to(&en->ex[part], b, sizeof b);
            o += (size_t)snprintf(out + o, n - o, "%s%s", part ? "\t" : "", b);
        }
    }
}

/* the 3D spec into cell c (src: the first entry, for older readers), an: its animation settings */
static void s3_store(nb_cell_t *c, const g3_state_t *st, int h, const double an[4])
{
    s3_format(st, h, G->spec, sizeof G->spec);
    an_append(G->spec, sizeof G->spec, an);
    nbd_set(c, NBF_GRAPH, G->spec);
    char b[ED_MAX * 2 + 16];
    b[0] = 0;
    for (int k = 0; k < G3_NENT && !b[0]; k++)
        if (st->ent[k].ex[0].len || st->ent[k].ex[2].len) {
            char e[ED_MAX * 2];
            gt_to(&st->ent[k].ex[st->ent[k].kind == G3K_FUNC ? 0 : 2], e, sizeof e);
            snprintf(b, sizeof b, "z=%s", e);
        }
    nbd_set(c, NBF_SRC, b);
    nbd_set(c, NBF_PLOT, NULL);
}

/* render 3D state st into the scratch frame at (0, 0), w x h (the 3D graph's state swapped), at
 * t = tv (NAN: not animated) */
static void plot3d(const g3_state_t *st, int w, int h, double tv)
{
    gfx_target(G->scratch);
    gfx_fill_rect(0, 0, w, h, C_WHITE);
    gfx_target(NULL);
    if (!g3d_have_mem())
        return;
    memcpy(&G->g3keep, &g3, sizeof g3);
    memcpy(&g3, st, sizeof g3);
    g3_invalidate();
    g3_anim_keep(false);
    if (!isnan(tv))
        g3_anim_set("t", tv, 0);
    g3_evaluate();
    g3_view_t v;
    if (g3.map) /* the contour map: its square fills the height (less a margin) */
        g3_view_setup(&v, 0, 90, w / 2.0f, h / 2.0f, ((float)h / 2 - 6) * (float)g3.zoom);
    else
        g3_view_setup(&v, g3.azim, g3.elev, w / 2.0f, h / 2.0f, 52.0f * (float)h / (float)(BODY_H - 14) * (float)g3.zoom);
    g3_scene_t sc;
    memset(&sc, 0, sizeof sc);
    sc.view = &v;
    for (int i = 0; i < G3_NENT; i++) {
        sc.mesh[i] = g3_mesh(i);
        sc.color[i] = func_colors[g3.ent[i].color % NCOLORS];
    }
    sc.style = g3.style, sc.colormode = g3.colormode, sc.axes = g3.axes, sc.box = g3.box, sc.stride = 1;
    sc.levels = g3.contours || g3.map ? g3_levels() : 0;
    sc.contour_map = g3.map;
    g3_box(&sc.win);
    for (int k = 0; k < 3; k++)
        sc.origin[k] = -(sc.win.lo[k] + sc.win.hi[k]) / (sc.win.hi[k] - sc.win.lo[k]);
    sc.map = gfx_color_map ? gfx_map : NULL;
    g3_target_t t = {G->scratch, g3d_zbuf, 0, 0, w, h, LCD_WIDTH};
    g3_render(&t, &sc);
    memcpy(&g3, &G->g3keep, sizeof g3);
    g3_invalidate();
    g3_anim_keep(true);
}

/* the spec of cell i (an older graph cell "f(x), xmin, xmax" without one: that function) */
static bool spec_of(int i, gspec_t *s)
{
    if (!G || i < 0 || i >= nbd->ncells || nbd->cell[i].kind != NBK_GRAPH)
        return false;
    const nb_cell_t *c = &nbd->cell[i];
    if (c->f[NBF_GRAPH]) {
        spec_parse(c->f[NBF_GRAPH], s);
        return true;
    }
    spec_defaults(s); /* one function a line ("f(x), xmin, xmax": that range) */
    if (c->xmin < c->xmax)
        s->w[0] = c->xmin, s->w[1] = c->xmax;
    int nf = 0;
    for (const char *src = nbd_get(c, NBF_SRC); *src && nf < NFUNC;) {
        const char *e = strchr(src, '\n'), *end = e ? e : src + strlen(src), *cut = NULL;
        int depth = 0, commas = 0;
        for (const char *p = src; p < end; p++) {
            depth += *p == '(' ? 1 : *p == ')' ? -1 : 0;
            if (*p == ',' && !depth && !commas++)
                cut = p;
        }
        if (cut && commas == 2) {
            double a = strtod(cut + 1, NULL), b = strtod(strchr(cut + 1, ',') + 1, NULL);
            if (a < b && !nf)
                s->w[0] = a, s->w[1] = b;
        }
        size_t n = (size_t)((cut && commas == 2 ? cut : end) - src);
        while (n && src[n - 1] == ' ')
            n--;
        while (n && *src == ' ')
            src++, n--;
        if (n >= sizeof s->fn[0])
            n = sizeof s->fn[0] - 1;
        if (n) {
            memcpy(s->fn[nf], src, n);
            s->fn[nf++][n] = 0;
        }
        src = e ? e + 1 : end;
    }
    return true;
}

/* ------------------------------------------------------------ the Y= state <-> a spec */
static void spec_from_g(gspec_t *s)
{
    spec_defaults(s);
    for (int i = 0; i < NFUNC; i++) {
        ed_display(&g.f[i].ed, s->fn[i], sizeof s->fn[i]);
        s->on[i] = g.f[i].enabled, s->color[i] = g.f[i].color, s->style[i] = gx.fstyle[i];
    }
    double w[6] = {g.xmin, g.xmax, g.ymin, g.ymax, g.xscl, g.yscl};
    memcpy(s->w, w, sizeof w);
    s->grid = g.grid, s->axes = g.axes;
}

static void spec_to_g(const gspec_t *s)
{
    void (*hook)(editor_t *) = ed_change_hook;
    ed_change_hook = NULL;
    for (int i = 0; i < NFUNC; i++) {
        func_t *f = &g.f[i];
        f->fn = NULL, f->compiled = false, f->err[0] = 0;
        m2d_set_linear(&f->ed, s->fn[i]);
        f->enabled = s->on[i], f->color = s->color[i];
        gx.fstyle[i] = s->style[i];
    }
    ed_change_hook = hook;
    gx.mode = GM_FUNC;
    g.xmin = s->w[0], g.xmax = s->w[1], g.ymin = s->w[2], g.ymax = s->w[3], g.xscl = s->w[4], g.yscl = s->w[5];
    g.grid = s->grid, g.axes = s->axes;
}

/* Plot spec s into the scratch frame at (0, 0), w x h: the Y= state swapped for the moment; at
 * t = tv (NAN: not animated; t is a slider parameter of the graph app, its value set here). */
static void plot(const gspec_t *s, int w, int h, double tv)
{
    struct {
        double w[6];
        bool grid, axes, valid;
        uint8_t mode;
    } keep = {{g.xmin, g.xmax, g.ymin, g.ymax, g.xscl, g.yscl}, g.grid, g.axes, g.graph_valid, gx.mode};
    funcs_invalidate(); /* (the compiled Y= functions are freed: compiled again when next used) */
    memcpy(G->savef, g.f, sizeof g.f);
    memcpy(G->savestyle, gx.fstyle, sizeof gx.fstyle);
    spec_to_g(s);
    int tk = -1; /* (the slider of t, its value as the scan of the functions left it: back after the plot) */
    double t0 = 0;
    if (!isnan(tv)) {
        gx_compile();
        for (int k = 0; k < GNPARAM; k++)
            if (gx.prm[k].used && gx.prm[k].active && gx.prm[k].name[0] == 't')
                tk = k, t0 = gx.prm[k].val, gx.prm[k].val = tv;
        gx_params_changed();
    }
    gr_view_t vp = gr_vp;
    int cr[4];
    gfx_get_clip(cr);
    gr_vp = (gr_view_t){0, 0, (int16_t)w, (int16_t)h};
    gfx_textlog_enable(false);
    gfx_target(G->scratch);
    graph_plot_view("empty: ENTER to add a function");
    gfx_target(NULL);
    gfx_textlog_enable(true);
    gfx_set_clip(cr[0], cr[1], cr[2] - cr[0], cr[3] - cr[1]);
    gr_vp = vp;
    funcs_invalidate(); /* the cell's compiled functions */
    memcpy(g.f, G->savef, sizeof g.f);
    memcpy(gx.fstyle, G->savestyle, sizeof gx.fstyle);
    if (tk >= 0)
        gx.prm[tk].val = t0;
    gx.mode = keep.mode;
    g.xmin = keep.w[0], g.xmax = keep.w[1], g.ymin = keep.w[2], g.ymax = keep.w[3], g.xscl = keep.w[4],
    g.yscl = keep.w[5];
    g.grid = keep.grid, g.axes = keep.axes;
    funcs_invalidate();
    g.graph_valid = false;
}

/* What a plot depends on besides its spec: the size, the theme, the MODE settings (angle, ...) and,
 * for functions naming anything other than their variables (a, f(x), ...), the CAS's variables,
 * which every calculation may change (cas_ans_serial). A stale picture is never shown. */
static bool free_names(const char *t)
{
    static const char *const known[] = {"x", "y", "u", "v", "t", "e", "i", "pi"};
    while (t && *t) {
        if (!((*t >= 'a' && *t <= 'z') || (*t >= 'A' && *t <= 'Z'))) {
            t++;
            continue;
        }
        const char *s = t;
        while ((*t >= 'a' && *t <= 'z') || (*t >= 'A' && *t <= 'Z') || (*t >= '0' && *t <= '9') || *t == '_')
            t++;
        size_t n = (size_t)(t - s);
        if (*t == '(' && n > 1)
            continue; /* (a built-in function: sin, sqrt, ...) */
        bool k = false;
        for (size_t j = 0; j < sizeof known / sizeof known[0] && !k; j++)
            k = strlen(known[j]) == n && !strncmp(known[j], s, n);
        if (!k)
            return true;
    }
    return false;
}

/* the t cell i is drawn at: NAN (not animated), the frame it is played or stepped to, its start */
static double frame_t(int i)
{
    const nb_cell_t *c = &nbd->cell[i];
    const char *sp = c->f[NBF_GRAPH];
    if (!sp || !spec_anim(sp))
        return NAN;
    if (A.id[0] && !strcmp(A.id, c->id))
        return A.t;
    double an[4];
    an_parse(sp, an);
    return an[0];
}

static uint32_t key_of(int i, int w, int h)
{
    const nb_cell_t *c = &nbd->cell[i];
    const char *sp = c->f[NBF_GRAPH] ? nbd_get(c, NBF_GRAPH) : nbd_get(c, NBF_SRC);
    uint32_t k = hash(nbd_get(c, NBF_GRAPH), hash(nbd_get(c, NBF_SRC), 2166136261u));
    const uint8_t *m = (const uint8_t *)&g.cs;
    for (size_t j = 0; j < sizeof g.cs; j++)
        k = (k ^ m[j]) * 16777619u;
    if (g.cas && free_names(c->f[NBF_GRAPH] && strchr(sp, '\n') ? strchr(sp, '\n') : sp))
        k = (k ^ cas_ans_serial(g.cas)) * 16777619u;
    double tv = frame_t(i);
    if (!isnan(tv)) {
        uint32_t b[2];
        memcpy(b, &tv, sizeof b);
        k = ((k ^ b[0]) * 16777619u ^ b[1]) * 16777619u;
    }
    return k ^ (uint32_t)(w * 7919 + h * 31 + theme_is_dark());
}

/* the image of cell i at w x h from the cache (plotted when missing); NULL without memory */
static const uint16_t *image(int i, int w, int h)
{
    if (!G || !g.cas || w <= 0 || w > LCD_WIDTH || h <= 0 || h > BODY_H || !spec_of(i, &G->sp)) /* (no CAS yet: boot) */
        return NULL;
    const nb_cell_t *c = &nbd->cell[i];
    uint32_t key = key_of(i, w, h);
    slot_t *s = NULL, *old = &G->slot[0];
    for (int k = 0; k < NSLOT; k++) {
        slot_t *t = &G->slot[k];
        if (!strcmp(t->id, c->id) && t->key == key && t->w == w && t->h == h)
            s = t;
        if (t->used < old->used)
            old = t;
    }
    for (int k = 0; k < NSLOT; k++) /* (an older picture of this cell gives way first: the frames of an animation) */
        if (!strcmp(G->slot[k].id, c->id) && G->slot[k].w == w && G->slot[k].h == h)
            old = &G->slot[k];
    if (!s) {
        s = old;
        if (is3d(i)) {
            s3_parse(nbd_get(c, NBF_GRAPH), &G->g3cell);
            plot3d(&G->g3cell, w, h, frame_t(i));
        } else {
            plot(&G->sp, w, h, frame_t(i));
        }
        for (int y = 0; y < h; y++)
            memcpy(s->pix + y * w, G->scratch + y * LCD_WIDTH, (size_t)w * 2);
        snprintf(s->id, sizeof s->id, "%s", c->id);
        s->key = key, s->w = w, s->h = h;
    }
    s->used = ++G->clock;
    return s->pix;
}

uint16_t *gcell_scratch(void) { return G ? G->scratch : NULL; }

/* the image cached for cell i at w x h as it is now (row stride w), NULL: none */
static const uint16_t *cached(int i, int w, int h)
{
    if (!G || i < 0 || i >= nbd->ncells)
        return NULL;
    const nb_cell_t *c = &nbd->cell[i];
    uint32_t key = key_of(i, w, h);
    for (int k = 0; k < NSLOT; k++) {
        const slot_t *t = &G->slot[k];
        if (!strcmp(t->id, c->id) && t->w == w && t->h == h && t->key == key)
            return t->pix;
    }
    return NULL;
}

/* the cached image of cell i at w x h (row stride LCD_WIDTH: copied to the scratch frame), NULL: none */
const uint16_t *gcell_cached(int i, int w, int h)
{
    const uint16_t *pix = cached(i, w, h);
    if (!pix)
        return NULL;
    for (int y = 0; y < h; y++)
        memcpy(G->scratch + y * LCD_WIDTH, pix + y * w, (size_t)w * 2);
    return G->scratch;
}

/* Function k (0, 1, ...) of cell i for its caption: "y1=x²" (3D "z1=..."), compact (², ³, ⁻¹, √,
 * π, ·), its colour and whether it is on; false: no more. */
bool gcell_fn(int i, int k, char *out, size_t n, uint16_t *color, bool *on)
{
    if (!G || !spec_of(i, &G->tmp) || k < 0 || k >= NFUNC || !G->tmp.fn[k][0])
        return false;
    static const char *const rep[][2] = {{"^(-1)", "⁻¹"}, {"^2", "²"}, {"^3", "³"}, {"sqrt", "√"}, {"pi", "π"},
                                         {"*", "·"}, {"\t", ", "}};
    const char *f = G->tmp.fn[k];
    size_t o = (size_t)snprintf(out, n, "%c%d=", is3d(i) ? 'z' : 'y', k + 1);
    for (const char *p = f; *p && o + 8 < n;) {
        size_t j = 0, l = 0;
        for (; j < sizeof rep / sizeof rep[0]; j++)
            if (!strncmp(p, rep[j][0], l = strlen(rep[j][0])))
                break;
        bool ok = j < sizeof rep / sizeof rep[0];
        if (ok && (j == 1 || j == 2) && p[l] >= '0' && p[l] <= '9') /* (x^23) */
            ok = false;
        if (ok && j == 4 && ((p[l] >= 'a' && p[l] <= 'z') || (p > f && p[-1] >= 'a' && p[-1] <= 'z')))
            ok = false; /* (pi inside a name) */
        if (ok) {
            o += (size_t)snprintf(out + o, n - o, "%s", rep[j][1]);
            p += l;
        } else {
            out[o++] = *p++;
            out[o] = 0;
        }
    }
    if (color)
        *color = func_colors[G->tmp.color[k] % NCOLORS];
    if (on)
        *on = G->tmp.on[k];
    return true;
}

/* "y1=x^2  y2=sin(x)" of cell i */
void gcell_functions(int i, char *out, size_t n)
{
    out[0] = 0;
    if (!G || !spec_of(i, &G->tmp))
        return;
    size_t o = 0;
    for (int k = 0; k < NFUNC && o < n; k++)
        if (G->tmp.fn[k][0])
            o += (size_t)snprintf(out + o, n - o, "%s%c%d=%s", o ? "  " : "", is3d(i) ? 'z' : 'y', k + 1, G->tmp.fn[k]);
}

void gcell_invalidate(void)
{
    if (G)
        for (int k = 0; k < NSLOT; k++)
            G->slot[k].id[0] = 0;
}

/* copy a w x h image to (x, y), clipped to the clip rectangle */
static void blit(const uint16_t *pix, int w, int h, int x, int y)
{
    int r[4];
    gfx_get_clip(r);
    int x0 = x < r[0] ? r[0] : x, x1 = x + w > r[2] ? r[2] : x + w;
    int y0 = y < r[1] ? r[1] : y, y1 = y + h > r[3] ? r[3] : y + h;
    if (x1 <= x0 || y1 <= y0)
        return;
    uint16_t *fb = gfx_framebuffer();
    for (int yy = y0; yy < y1; yy++)
        memcpy(fb + yy * LCD_WIDTH + x0, pix + (yy - y) * w + (x0 - x), (size_t)(x1 - x0) * 2);
    gfx_mark_dirty(x0, y0, x1 - x0, y1 - y0);
}

/* ------------------------------------------------------------ cells */
bool gcell_empty(int i)
{
    if (!G || !spec_of(i, &G->tmp))
        return true;
    if (is3d(i))
        return !strstr(nbd_get(&nbd->cell[i], NBF_GRAPH), "\nf 1 ");
    for (int k = 0; k < NFUNC; k++)
        if (G->tmp.fn[k][0] && G->tmp.on[k])
            return false;
    return true;
}

/* (a cell without a function to plot is one line: "graph: enter a function") */
static int spec_h(int i)
{
    if (G && i >= 0 && i < nbd->ncells && nbd->cell[i].kind == NBK_GRAPH && is3d(i)) {
        const char *o = strstr(nbd_get(&nbd->cell[i], NBF_GRAPH), "\no 0 0 ");
        return o ? clamp_h(atoi(o + 7)) : GCELL_H;
    }
    gspec_t *s = G ? &G->tmp : NULL;
    if (!s || !spec_of(i, s))
        return GCELL_H;
    return s->h;
}

int gcell_height(int i)
{
    if (T.cell == i && T.mode == 2)
        return T.live_h;
    return gcell_empty(i) ? GCELL_HEMPTY : spec_h(i);
}

nb_cell_t *gcell_new3d(int at)
{
    if (!G)
        return NULL;
    nb_cell_t *c = nbd_insert(at, NBK_GRAPH);
    if (c) {
        an_defaults(G->an3);
        an_from_app(G->an3);
        s3_store(c, &g3, GCELL_H, G->an3);
    }
    return c;
}

nb_cell_t *gcell_new(int at)
{
    if (!G)
        return NULL;
    nb_cell_t *c = nbd_insert(at, NBK_GRAPH);
    if (!c)
        return NULL;
    spec_from_g(&G->sp);
    an_from_app(G->sp.an);
    spec_store(c, &G->sp);
    return c;
}

/* Cell i's spec from the browser page (`nb spec`), normalised (2D, or "3d ..." a 3D cell); spec
 * NULL: a graph cell without one (an older notebook, a cell made a graph) gets one from its text. */
bool gcell_set_spec(int i, const char *spec)
{
    if (!G || i < 0 || i >= nbd->ncells || nbd->cell[i].kind != NBK_GRAPH)
        return false;
    nb_cell_t *c = &nbd->cell[i];
    if (spec && spec[0] == '3' && spec[1] == 'd' && (spec[2] == ' ' || spec[2] == '\n' || !spec[2])) {
        int h = s3_parse(spec, &G->g3cell);
        an_parse(spec, G->an3);
        s3_store(c, &G->g3cell, h, G->an3);
        return true;
    }
    if (spec)
        spec_parse(spec, &G->sp);
    else if (c->f[NBF_GRAPH] || !spec_of(i, &G->sp))
        return false;
    spec_store(c, &G->sp);
    return true;
}

/* A block (x, y, w x h): the caption line, the plot below it in a frame, the edge that resizes. */
#define PX 2
#define CAPH 16 /* the caption: the functions, in their colours */
#define PB 6    /* below the plot: the edge that resizes */
void gcell_plot_size(int i, int *w, int *h)
{
    *w = LCD_WIDTH - 2 * PX;
    *h = gcell_height(i) - CAPH - PB;
}

/* "y1=x²  y2=sin(x)" in the functions' colours (off: dim), cut with … at the right edge */
static void caption(int i, int x, int y, int w)
{
    const font_t *f = &font_medium;
    char b[ED_MAX * 2 + 16], all[160];
    size_t ao = 0;
    int cx = x + PX + 1, lim = x + w - PX - 1;
    uint16_t col = TH_FG;
    bool on, cut = false;
    all[0] = 0;
    for (int k = 0; k < NFUNC && !cut && gcell_fn(i, k, b, sizeof b, &col, &on); k++) {
        if (k)
            cx += f->w * 2;
        int room = (lim - cx) / f->w, n = 0;
        size_t len = 0;
        for (const char *p = b; *p; p++) /* (characters, not bytes) */
            n += ((uint8_t)*p & 0xC0) != 0x80;
        if (n > room) { /* cut: what fits before the … (a function with no room for 3 characters: only …) */
            cut = true;
            int keep = room - 1 < 3 ? 0 : room - 1, c = 0;
            for (const char *p = b; *p; p++, len++)
                if (((uint8_t)*p & 0xC0) != 0x80 && c++ == keep)
                    break;
            b[len] = 0;
            n = keep;
            if (!keep && k)
                cx -= f->w;
        }
        if (!on)
            col = TH_DIM;
        gfx_text(f, cx, y, b, col, TH_BG);
        ao += (size_t)snprintf(all + ao, ao < sizeof all ? sizeof all - ao : 0, "%s%s", k && b[0] ? "  " : "", b);
        cx += n * f->w;
        if (cut) { /* the ellipsis, drawn (the fonts have no …) */
            if (cx > lim - 8)
                cx = lim - 8;
            for (int d = 0; d < 3; d++)
                gfx_fill_rect(cx + d * 3, y + f->ascent - 2, 2, 2, col);
            if (ao < sizeof all)
                snprintf(all + ao, sizeof all - ao, "…");
        }
    }
    if (cut)
        gfx_textlog_note(x + PX, y < BODY_Y ? BODY_Y : y, w - 2 * PX, f->h, all);
}

void gcell_draw(int i, int x, int y, int w, int h, bool sel, bool active)
{
    gfx_fill_rect(x, y, w, h, TH_BG);
    uint16_t fc = active ? TH_FOCUS : sel ? TH_SEL_BG : TH_PANEL;
    if (gcell_empty(i)) { /* nothing to plot: one dim line, no axes */
        gfx_text(&font_medium, x + PX + 2, y + (h - font_medium.h) / 2,
                 is3d(i) ? "3D graph: enter z=f(x,y)" : "graph: enter a function", TH_DIM, TH_BG);
        if (sel || active)
            gfx_rect(x + PX - 1, y + 1, w - 2 * PX + 2, h - 2, fc);
        return;
    }
    int bw = 0, pw = w - 2 * PX, ph = h - CAPH - PB, py = y + CAPH;
    double tv = frame_t(i);
    if (!isnan(tv)) { /* animated: the badge at the right of the caption ("anim", the frame's t once played) */
        char b[32], v[16];
        snprintf(v, sizeof v, "%.4g", fabs(tv) < 1e-12 ? 0.0 : tv);
        if (A.id[0] && !strcmp(A.id, nbd->cell[i].id))
            snprintf(b, sizeof b, "%sanim t=%s", anim_playing() ? "▶ " : "", v);
        else
            snprintf(b, sizeof b, "anim");
        bw = gfx_text_width(&font_small, b) + 6;
        gfx_fill_rect(x + w - PX - bw, y + 1, bw, CAPH - 2, TH_PANEL);
        gfx_text(&font_small, x + w - PX - bw + 3, y + 1, b, TH_LABEL, GFX_TRANSPARENT);
    }
    caption(i, x, y + 1, w - bw - 4);
    const uint16_t *pix = image(i, pw, ph);
    if (pix)
        blit(pix, pw, ph, x + PX, py);
    if (!isnan(tv) && !anim_available()) { /* no Animation module: a still frame, and why */
        static const char note[] = "still frame: animation needs the Animation module";
        int nw = gfx_text_width(&font_small, note) + 6;
        gfx_fill_rect(x + PX, py + ph - 14, nw, 14, TH_BG);
        gfx_text(&font_small, x + PX + 3, py + ph - 14, note, TH_DIM, GFX_TRANSPARENT);
    }
    gfx_rect(x + PX - 1, py - 1, pw + 2, ph + 2, fc);
    if (sel || active) {
        gfx_rect(x + PX - 2, py - 2, pw + 4, ph + 4, fc);
        gfx_fill_rect(x + w / 2 - 16, y + h - 3, 32, 2, fc); /* the grip: drag to resize */
    }
    if (G && spec_of(i, &G->tmp)) { /* the functions as typed, for `expect text` and the text log */
        char b[200];
        size_t o = 0;
        b[0] = 0;
        for (int k = 0; k < NFUNC && o < sizeof b; k++)
            if (G->tmp.fn[k][0])
                o += (size_t)snprintf(b + o, sizeof b - o, "%s%c%d=%s", o ? "  " : "", is3d(i) ? 'z' : 'y', k + 1, G->tmp.fn[k]);
        if (b[0])
            gfx_textlog_note(x + PX, py < BODY_Y ? BODY_Y : py, pw, 20, b);
    }
}

void gcell_draw_api(int i, int x, int y, int w, int h)
{
    const uint16_t *pix = image(i, w, h);
    if (pix)
        blit(pix, w, h, x, y);
}

const uint16_t *gcell_image(int i, int w, int h)
{
    if (!G || !g.cas || w > LCD_WIDTH || h > BODY_H || !spec_of(i, &G->sp)) /* (no CAS yet: boot) */
        return NULL;
    if (is3d(i)) {
        s3_parse(nbd_get(&nbd->cell[i], NBF_GRAPH), &G->g3cell);
        plot3d(&G->g3cell, w, h, frame_t(i));
    } else {
        plot(&G->sp, w, h, frame_t(i));
    }
    return G->scratch;
}

static void store(int i)
{
    spec_store(&nbd->cell[i], &G->sp);
    nbd_changed();
}

/* a 3D cell turned (degrees), zoomed (factor) or resized */
static void view3d(int i, double daz, double del, double zf, int h)
{
    int h0 = s3_parse(nbd_get(&nbd->cell[i], NBF_GRAPH), &G->g3cell);
    g3_state_t *st = &G->g3cell;
    if (daz != 0 || del != 0)
        st->map = false; /* turned: the 3D view */
    st->azim += daz;
    st->elev += del;
    st->elev = st->elev > 90 ? 90 : st->elev < -90 ? -90 : st->elev;
    st->zoom *= zf;
    st->zoom = st->zoom < 0.2 ? 0.2 : st->zoom > 8 ? 8 : st->zoom;
    an_parse(nbd_get(&nbd->cell[i], NBF_GRAPH), G->an3);
    s3_store(&nbd->cell[i], st, h > 0 ? clamp_h(h) : h0, G->an3);
    nbd_changed();
}

void gcell_pan(int i, double fx, double fy)
{
    if (is3d(i)) {
        view3d(i, fx * 120, fy * 80, 1, 0);
        return;
    }
    if (!spec_of(i, &G->sp))
        return;
    double dx = (G->sp.w[1] - G->sp.w[0]) * fx, dy = (G->sp.w[3] - G->sp.w[2]) * fy;
    G->sp.w[0] += dx, G->sp.w[1] += dx, G->sp.w[2] += dy, G->sp.w[3] += dy;
    store(i);
}

void gcell_zoom(int i, double s, double cx, double cy)
{
    if (!spec_of(i, &G->sp) || !(s > 0))
        return;
    if (is3d(i)) {
        view3d(i, 0, 0, s > 1 ? 1.25 : s < 1 ? 0.8 : 1, 0);
        return;
    }
    double *w = G->sp.w;
    double X = w[0] + cx * (w[1] - w[0]), Y = w[3] - cy * (w[3] - w[2]);
    double nw = (w[1] - w[0]) / s, nh = (w[3] - w[2]) / s;
    if (!(nw > 1e-12 && nh > 1e-12 && nw < 1e12 && nh < 1e12))
        return;
    w[0] = X - cx * nw, w[1] = w[0] + nw;
    w[3] = Y + cy * nh, w[2] = w[3] - nh;
    store(i);
}

void gcell_resize(int i, int h)
{
    if (!spec_of(i, &G->sp))
        return;
    if (is3d(i)) {
        view3d(i, 0, 0, 1, h);
        return;
    }
    G->sp.h = clamp_h(h);
    store(i);
}

/* ------------------------------------------------------------ animated cells on the home screen */
bool gcell_animated(int i)
{
    return G && i >= 0 && i < nbd->ncells && nbd->cell[i].kind == NBK_GRAPH && !isnan(frame_t(i));
}

bool gcell_anim_cfg(int i, double an[4])
{
    if (!gcell_animated(i))
        return false;
    an_parse(nbd_get(&nbd->cell[i], NBF_GRAPH), an);
    return true;
}

/* the cell played or stepped (-1: none; one deleted is forgotten) */
int gcell_anim_cell(void)
{
    int i = A.id[0] ? nbd_find(A.id) : -1;
    if (A.id[0] && !gcell_animated(i))
        A.id[0] = 0, i = -1;
    return i;
}

/* cell i becomes the one played or stepped (at its start, unless it already is); its settings */
void gcell_anim_take(int i, double an[4])
{
    an_parse(nbd_get(&nbd->cell[i], NBF_GRAPH), an);
    if (strcmp(A.id, nbd->cell[i].id)) {
        snprintf(A.id, sizeof A.id, "%s", nbd->cell[i].id);
        A.t = an[0];
    }
}

double gcell_anim_t(void) { return A.t; }
void gcell_anim_set_t(double t) { A.t = t; }

/* one step of t (1/50 of its range) forwards or back: around in loop mode, else up to the ends */
void gcell_anim_step(int i, int dir)
{
    double an[4];
    gcell_anim_take(i, an);
    double r = an[1] - an[0], t = A.t + dir * r / 50, eps = r * 1e-9;
    if (an[3] == ANIM_LOOP)
        t = t > an[1] + eps ? t - r : t < an[0] - eps ? t + r : t;
    A.t = t < an[0] + eps ? an[0] : t > an[1] - eps ? an[1] : t; /* (the ends exactly) */
}

/* the cell's settings for the graph app's animation of t (ENTER on the cell) */
static void an_to_app(const double an[4])
{
    anim_cfg_t *c = anim_cfg('t', true, an[0], an[1]);
    if (c)
        c->start = an[0], c->end = an[1], c->secs = an[2], c->mode = an[3];
}

bool gcell_key(int i, int key, int mod)
{
    (void)mod;
    if (gcell_empty(i))
        return false;
    switch (key) {
    case KEY_LEFT: gcell_pan(i, -0.125, 0); return true;
    case KEY_RIGHT: gcell_pan(i, 0.125, 0); return true;
    case KEY_UP: gcell_pan(i, 0, 0.125); return true;
    case KEY_DOWN: gcell_pan(i, 0, -0.125); return true;
    case KEY_ADD: gcell_zoom(i, 2, 0.5, 0.5); return true;
    case KEY_SUB: gcell_zoom(i, 0.5, 0.5, 0.5); return true;
    }
    return false;
}

/* Gestures on cell i's block (x, y, w x h on the screen); true: redraw the home screen. */
bool gcell_touch(int i, const touch_event_t *ev, int x, int y, int w, int h)
{
    if (!G)
        return false;
    if (gcell_empty(i))
        return false;
    int pw = w - 2 * PX, ph = h - CAPH - PB, px = x + PX, py = y + CAPH;
    switch (ev->type) {
    case TEV_DOWN:
        T.cell = i;
        T.mode = ev->y >= y + h - GCELL_EDGE ? 2 : 1;
        T.h0 = T.live_h = h;
        return false;
    case TEV_DRAG:
        if (T.cell != i)
            return false;
        if (T.mode == 2) {
            int nh = clamp_h(T.h0 + ev->y - ev->y0);
            if (nh == T.live_h)
                return false;
            T.live_h = nh;
            return true;
        }
        if (T.mode == 1) {
            const uint16_t *pix = image(i, pw, ph);
            if (pix)
                gr_preview(pix, pw, px, py, pw, ph, ev->x - ev->x0, ev->y - ev->y0, 1.0f, 0, 0, 0, 0);
        }
        return false;
    case TEV_PINCH: {
        T.cell = i, T.mode = 3;
        const uint16_t *pix = image(i, pw, ph);
        if (pix)
            gr_preview(pix, pw, px, py, pw, ph, 0, 0, ev->scale, ev->x0, ev->y0, ev->x, ev->y);
        return false;
    }
    case TEV_PINCH_END:
        T.mode = 0, T.cell = -1;
        if (is3d(i)) {
            view3d(i, 0, 0, ev->scale, 0);
            return true;
        }
        if (spec_of(i, &G->sp) && gr_pinch_window(&G->sp.w[0], &G->sp.w[1], &G->sp.w[2], &G->sp.w[3], pw, ph,
                                                  ev->x0 - px, ev->y0 - py, ev->x - px, ev->y - py, ev->scale))
            store(i);
        return true;
    case TEV_UP: {
        int mode = T.mode;
        T.mode = 0, T.cell = -1;
        int dx = ev->x - ev->x0, dy = ev->y - ev->y0;
        if (mode == 2) {
            gcell_resize(i, h + dy);
            return true;
        }
        if (mode == 1 && (dx || dy) && is3d(i)) { /* a drag turns a 3D graph (as in the 3D app) */
            view3d(i, dx * 0.5, dy * 0.5, 1, 0);
            return true;
        }
        if (mode == 1 && (dx || dy)) {
            gcell_pan(i, -dx / (double)(pw - 1), dy / (double)(ph - 1));
            return true;
        }
        return false;
    }
    case TEV_CANCEL:
        T.mode = 0, T.cell = -1;
        return true;
    default:
        return false;
    }
}

/* ------------------------------------------------------------ the graph app on a cell */
void gcell_bind(int i)
{
    if (!spec_of(i, &G->sp))
        return;
    double tv = frame_t(i); /* an animated cell: the app's animation of t gets its settings and frame */
    if (!isnan(tv))
        an_to_app(G->sp.an);
    if (is3d(i)) {
        s3_parse(nbd_get(&nbd->cell[i], NBF_GRAPH), &g3);
        g3_invalidate();
        if (!isnan(tv))
            g3_anim_set("t", tv, 0);
        g3d_redraw();
        snprintf(bound, sizeof bound, "%s", nbd->cell[i].id);
        return;
    }
    spec_to_g(&G->sp);
    funcs_invalidate();
    if (!isnan(tv)) { /* t: the selected slider, at the cell's frame */
        gx_compile();
        for (int k = 0; k < GNPARAM; k++)
            if (gx.prm[k].used && gx.prm[k].shown && gx.prm[k].name[0] == 't') {
                gx.prm[k].val = tv;
                gx.sel_param = (uint8_t)k;
                gx_param_commit(k);
                gx_params_changed();
            }
    }
    graph_view_changed();
    snprintf(bound, sizeof bound, "%s", nbd->cell[i].id);
}

void gcell_unbind(void)
{
    if (!bound[0] || !G)
        return;
    int i = nbd_find(bound);
    bound[0] = 0;
    if (i < 0 || nbd->cell[i].kind != NBK_GRAPH)
        return;
    int h = spec_h(i); /* (the height it has with a function) */
    double an[4]; /* the animation settings: the cell's, as the app's animation of t has them now */
    an_parse(nbd_get(&nbd->cell[i], NBF_GRAPH), an);
    an_from_app(an);
    if (is3d(i)) {
        s3_store(&nbd->cell[i], &g3, h, an);
        nbd_changed();
    } else {
        spec_from_g(&G->sp);
        G->sp.h = h;
        memcpy(G->sp.an, an, sizeof an);
        store(i);
    }
    if (!strcmp(A.id, nbd->cell[i].id)) /* (the cell shows its start again) */
        A.id[0] = 0;
}

const char *gcell_bound(void) { return bound; }
void gcell_set_bound(const char *id) { snprintf(bound, sizeof bound, "%s", id ? id : ""); }

/* console `state` of graph cell i: gcell_fn (the first function), gcell_fns, gcell_window, gcell_h,
 * gcell_empty (no function: one line, no plot), gcell_caption (as drawn, colours as #RRGGBB: before
 * each function), gcell_fresh (on the home screen; 1: the picture cached for it is the one plotting it
 * now gives, pixel for pixel: not stale after a change of a variable or a setting); 3D: gcell_view (azimuth
 * elevation zoom), gcell_contours (contours, map, levels) */
void gcell_state(int i, void (*kv)(const char *, const char *, void *), void *user)
{
    if (!G || !spec_of(i, &G->tmp))
        return;
    char b[ED_MAX * 2 + 8];
    int n = 0;
    b[0] = 0;
    for (int k = 0; k < NFUNC; k++)
        if (G->tmp.fn[k][0] && !n++)
            snprintf(b, sizeof b, "%s", G->tmp.fn[k]);
    kv("gcell_fn", b, user);
    snprintf(b, sizeof b, "%d", n);
    kv("gcell_fns", b, user);
    snprintf(b, sizeof b, "%g %g %g %g", G->tmp.w[0], G->tmp.w[1], G->tmp.w[2], G->tmp.w[3]);
    kv("gcell_window", b, user);
    snprintf(b, sizeof b, "%d", gcell_height(i));
    kv("gcell_h", b, user);
    kv("gcell_empty", gcell_empty(i) ? "1" : "0", user);
    int pw, ph;
    gcell_plot_size(i, &pw, &ph);
    if (ui_current() == &scr_home) { /* (plotting swaps the Y= state: not under the graph app) */
        const uint16_t *have = gcell_empty(i) ? NULL : cached(i, pw, ph), *now = have ? gcell_image(i, pw, ph) : NULL;
        bool fresh = now != NULL;
        for (int y = 0; fresh && y < ph; y++)
            fresh = !memcmp(have + y * pw, now + y * LCD_WIDTH, (size_t)pw * 2);
        kv("gcell_fresh", fresh ? "1" : "0", user);
    }
    char cap[256], f[ED_MAX * 2 + 16];
    size_t o = 0;
    uint16_t col;
    bool on;
    cap[0] = 0;
    for (int k = 0; o < sizeof cap && gcell_fn(i, k, f, sizeof f, &col, &on); k++)
        o += (size_t)snprintf(cap + o, sizeof cap - o, "%s#%02X%02X%02X%s%s", k ? " " : "", (col >> 8) & 0xF8,
                              (col >> 3) & 0xFC, (col << 3) & 0xF8, on ? "" : "(off)", f);
    kv("gcell_caption", cap, user);
    double tv = frame_t(i), an[4];
    kv("gcell_anim", isnan(tv) ? "0" : "1", user);
    if (!isnan(tv)) {
        an_parse(nbd_get(&nbd->cell[i], NBF_GRAPH), an);
        snprintf(b, sizeof b, "%g", tv);
        kv("gcell_t", b, user);
        snprintf(b, sizeof b, "%g %g %g %d", an[0], an[1], an[2], (int)an[3]);
        kv("gcell_anim_cfg", b, user);
    }
    if (is3d(i)) {
        s3_parse(nbd_get(&nbd->cell[i], NBF_GRAPH), &G->g3cell);
        snprintf(b, sizeof b, "%g %g %g", G->g3cell.azim, G->g3cell.elev, G->g3cell.zoom);
        kv("gcell_view", b, user);
        snprintf(b, sizeof b, "%d %d %d", G->g3cell.contours, G->g3cell.map, (int)G->g3cell.levels);
        kv("gcell_contours", b, user);
    }
}
