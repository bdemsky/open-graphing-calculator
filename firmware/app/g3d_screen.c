/*
 * The 3D graph screen (scr_g3d, help topic "graph3d"), its VIEW menu, the
 * APPS entry "3D graph" (app id graph3d) and the console command `g3d`.
 *
 * Keys. The arrows turn the box as if it were grabbed at its front: {◀}{▶}
 * change the azimuth by 15°, {▲}{▼} the elevation by 15° (-90..90). {+} / {−}
 * zoom the view in and out (x1.25, 0.25..8). F4 starts TRACE: a cursor on a
 * node of the first drawn entry; the arrows then move it along the grid
 * (◀▶ the first parameter x / u / t, ▲▼ the second y / v; curves: ▲▼ ten
 * steps), F4 ("next") goes to the next drawn entry and CLEAR ends the trace.
 * CLEAR otherwise goes home. Soft keys: z= (entries), window, view (VIEW menu),
 * trace, anim (F5: the ANIMATION menu of app/anim_core.c, "pause" while
 * playing; the module plays the free letter of the entries from this
 * screen's tick and draws each frame with g3d_anim_frame); SHIFT: reset
 * (standard view), style (wireframe / shaded), color (entry colours /
 * colour by height), zfit (z range from the surfaces).
 *
 * Contours (VIEW menu, g3d.h). "Contours" draws the level curves over the
 * z = f(x,y) surfaces in the 3D view; "Contour map" switches to the map: the
 * surfaces from straight above, flat, in a square left of centre (the x-y
 * window, x right, y up), the window's values at its corners, the levels'
 * key at the right. In the map {+} {−} and a pinch zoom, trace works on the
 * z = f(x,y) entries; turning (an arrow, a drag, a 3D view of the menu or
 * reset) goes back to the 3D view at its previous angles.
 *
 * Touch. Dragging turns the box the same way (0.5° per pixel) and pinching
 * zooms; during the gesture the image is rendered from a coarser grid (every
 * second or fourth grid line, G3_COARSE) and in full when the finger lifts.
 * A tap traces the nearest node within 24 px.
 *
 * Rendering. The meshes are evaluated (g3d_model.c) when the entries or the
 * window changed and on every fresh visit of the screen (not when coming back
 * from a menu); every view change only re-projects the cached nodes and
 * re-renders (g3d_raster.c) into g3d_image, a full-screen buffer; the screen
 * copies it to the framebuffer and draws the text on top: entry legend and
 * errors (top left), the numeric ranges at the ends of three box edges and
 * the axis names (box), the trace band (bottom). Moving the trace cursor
 * restores the image and redraws only the overlays. The renders' durations are
 * kept for `g3d bench` and the `state` keys (g3d_render_us).
 *
 * State: the module statics below (trace position, gesture, render
 * validity); the view itself is in g3 (saved by persist_graph3d).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "anim.h"
#include "console.h"
#include "g3d.h"
#include "hal.h"
#include "registry.h"
#include "theme.h"

#define BAND_H 14    /* trace band at the bottom of the body */
#define VIEW_SCALE 52.0f /* pixels per box half-width at zoom 1 */
/* the contour map: centre and half-width (zoom 1) of its square, x of its key */
#define MAP_CX 140
#define MAP_CY (BODY_Y + (BODY_H - BAND_H) / 2)
#define MAP_HALF 80
#define MAP_KEY_X 236

static bool img_valid;         /* g3d_image shows the current view at full resolution */
static bool tracing;
static int tr_ent, tr_i, tr_j; /* traced entry and node (i along u/x/t, j along v/y) */
static bool gesture;           /* drag / pinch in progress (coarse renders) */
static double zoom0;           /* zoom at the start of a pinch */
static uint32_t render_us, coarse_us; /* last full / coarse render */
static int contour_segs;       /* contour segments of the last full render */

void g3d_redraw(void) { img_valid = false; }

/* coarse grid stride while dragging: keeps a coarse frame at ~12 cells per side or more */
static int coarse_stride(void)
{
    int n = g3_grid();
    return n >= 48 ? 4 : n >= 16 ? 2 : 1;
}

/* ------------------------------------------------------------ rendering */
/* the camera: the 3D view, or the contour map's (straight down, x right, y up) */
static void view_setup(g3_view_t *v)
{
    if (g3.map) {
        g3_view_setup(v, 0, 90, MAP_CX, MAP_CY, MAP_HALF * (float)g3.zoom);
        return;
    }
    g3_view_setup(v, g3.azim, g3.elev, LCD_WIDTH / 2.0f, (float)BODY_Y + (BODY_H - BAND_H) / 2.0f + 2,
                  VIEW_SCALE * (float)g3.zoom);
}

static uint16_t map_color(uint16_t c) { return gfx_map(c); }

/* Render the current view into g3d_image (evaluating first if needed) with
 * grid stride `stride`; measures the time (hal_cycles). */
static void render(int stride)
{
    if (!g3_valid())
        g3_evaluate();
    uint32_t t0 = hal_cycles();
    g3_view_t v;
    view_setup(&v);
    g3_scene_t s;
    memset(&s, 0, sizeof s);
    s.view = &v;
    for (int i = 0; i < G3_NENT; i++) {
        s.mesh[i] = g3_mesh(i);
        s.color[i] = func_colors[g3.ent[i].color % NCOLORS];
    }
    s.style = g3.style;
    s.colormode = g3.colormode;
    s.axes = g3.axes;
    s.box = g3.box;
    s.stride = stride;
    s.levels = g3.contours || g3.map ? g3_levels() : 0;
    s.contour_map = g3.map;
    g3_box(&s.win);
    for (int k = 0; k < 3; k++) /* (0 - centre) / half-range */
        s.origin[k] = -(s.win.lo[k] + s.win.hi[k]) / (s.win.hi[k] - s.win.lo[k]);
    s.map = gfx_color_map ? map_color : NULL;
    g3_target_t t = {g3d_image, g3d_zbuf, 0, BODY_Y, LCD_WIDTH, BODY_H, LCD_WIDTH};
    int segs = g3_render(&t, &s);
    if (stride == 1)
        contour_segs = segs;
    uint32_t us = (hal_cycles() - t0) / hal_cycles_per_us();
    if (stride == 1)
        render_us = us;
    else
        coarse_us = us;
    img_valid = stride == 1;
}

/* ------------------------------------------------------------ overlays */
/* text centred at (x, y) in font_small */
static void text_at(int x, int y, const char *s, uint16_t c)
{
    gfx_text(&font_small, x - gfx_text_width(&font_small, s) / 2, y - 6, s, c, GFX_TRANSPARENT);
}

static void project(const g3_view_t *v, float x, float y, float z, g3p_t *p)
{
    g3v_t w = {x, y, z};
    g3_project(v, &w, p);
}

/* Label an edge of the box from a (value lo) to b (value hi): the values at
 * 12 % and 88 % of its length (not at the corners, which two labelled edges
 * may share) and the axis name at the midpoint, each pushed away from the
 * screen position c of the box centre by 10 / 14 px. Edges shorter than 24 px
 * on screen are not labelled. */
static void edge_labels(const g3p_t *a, const g3p_t *b, const g3p_t *c, double lo, double hi, const char *name)
{
    char s[16];
    static const float at[3] = {0.12f, 0.88f, 0.5f};
    float ex = b->sx - a->sx, ey = b->sy - a->sy;
    if (ex * ex + ey * ey < 24.0f * 24.0f) /* seen end-on (z in the top view): no room */
        return;
    for (int k = 0; k < 3; k++) {
        float px = a->sx + (b->sx - a->sx) * at[k], py = a->sy + (b->sy - a->sy) * at[k];
        float dx = px - c->sx, dy = py - c->sy, len = sqrtf(dx * dx + dy * dy);
        float off = k == 2 ? 14.0f : 10.0f;
        if (len < 1)
            dx = 0, dy = 1, len = 1;
        int x = (int)(px + dx / len * off), y = (int)(py + dy / len * off);
        if (k < 2)
            fmt_num(k == 0 ? lo : hi, 6, s, sizeof s);
        text_at(x, y, k < 2 ? s : name, k < 2 ? TH_DIM : TH_FG);
    }
}

/* text right-aligned at x, centred on y, in font_small */
static void text_right(int x, int y, const char *s, uint16_t c)
{
    gfx_text(&font_small, x - gfx_text_width(&font_small, s), y - 6, s, c, GFX_TRANSPARENT);
}

/* The contour map's frame labels: xmin and xmax under the bottom corners
 * with "x" between them, ymin and ymax left of the left corners with "y"
 * between; without the frame the axis names at the axes' positive ends. */
static void draw_map_labels(const g3_view_t *v)
{
    g3p_t a, b;
    char s[16];
    project(v, -1, -1, 0, &a);
    project(v, 1, 1, 0, &b);
    if (g3.box) {
        int yb = (int)a.sy + 9, xl = (int)a.sx - 4;
        fmt_num(g3.xmin, 6, s, sizeof s);
        text_at((int)a.sx, yb, s, TH_DIM);
        fmt_num(g3.xmax, 6, s, sizeof s);
        text_at((int)b.sx, yb, s, TH_DIM);
        text_at((int)(a.sx + b.sx) / 2, yb, "x", TH_FG);
        fmt_num(g3.ymin, 6, s, sizeof s);
        text_right(xl, (int)a.sy - 4, s, TH_DIM);
        fmt_num(g3.ymax, 6, s, sizeof s);
        text_right(xl, (int)b.sy + 4, s, TH_DIM);
        text_right(xl, (int)(a.sy + b.sy) / 2, "y", TH_FG);
    } else if (g3.axes) {
        g3_box_t bx;
        g3_box(&bx);
        float o[2];
        for (int k = 0; k < 2; k++)
            o[k] = (float)(-(bx.lo[k] + bx.hi[k]) / (bx.hi[k] - bx.lo[k]));
        if (fabsf(o[1]) <= 1)
            project(v, 1.1f, o[1], 0, &a), text_at((int)a.sx, (int)a.sy, "x", TH_FG);
        if (fabsf(o[0]) <= 1)
            project(v, o[0], 1.1f, 0, &a), text_at((int)a.sx, (int)a.sy, "y", TH_FG);
    }
}

/* The contour map's key, right of the map: a bar of the levels+1 bands
 * between zmin (bottom) and zmax (top), in the height colours when the map
 * is filled or coloured by height, and the level values at their boundaries
 * (every one that has room: at least 12 px apart). */
static void draw_map_key(void)
{
    int n = g3_levels(), x = MAP_KEY_X, y0 = MAP_CY - MAP_HALF, hgt = 2 * MAP_HALF;
    bool col = g3.style == G3S_SHADE || g3.colormode == G3C_HEIGHT;
    char s[16];
    text_at(x + 5, y0 - 8, "z", TH_FG);
    for (int b = 0; b <= n && col; b++) {
        int ya = y0 + hgt - hgt * (b + 1) / (n + 1), yb = y0 + hgt - hgt * b / (n + 1);
        gfx_fill_rect(x, ya, 10, yb - ya, g3_heat(((float)b + 0.5f) * 2 / (float)(n + 1) - 1));
    }
    gfx_rect(x, y0, 10, hgt + 1, TH_DIM);
    int every = (12 * (n + 1) + hgt - 1) / hgt;
    for (int k = every; k <= n; k += every) {
        int y = y0 + hgt - hgt * k / (n + 1);
        gfx_hline(x + 10, y, 3, TH_FG);
        fmt_num(g3_level(k), 5, s, sizeof s);
        gfx_text(&font_small, x + 15, y - 6, s, TH_FG, GFX_TRANSPARENT);
    }
}

/* Numeric ranges and names on three box edges: for x the bottom edge parallel
 * to x that is nearer to the viewer (y = ±1), for y likewise (x = ±1), for z
 * the vertical edge farthest left on screen. Axis names only when the box is
 * off and the axes are on: at the axes' positive ends. */
static void draw_box_labels(const g3_view_t *v)
{
    g3p_t c, a, b;
    project(v, 0, 0, 0, &c);
    if (g3.box) {
        g3p_t a0, b0, a1, b1;
        project(v, -1, -1, -1, &a0), project(v, 1, -1, -1, &b0);
        project(v, -1, 1, -1, &a1), project(v, 1, 1, -1, &b1);
        bool near0 = a0.d + b0.d >= a1.d + b1.d;
        edge_labels(near0 ? &a0 : &a1, near0 ? &b0 : &b1, &c, g3.xmin, g3.xmax, "x");
        project(v, -1, -1, -1, &a0), project(v, -1, 1, -1, &b0);
        project(v, 1, -1, -1, &a1), project(v, 1, 1, -1, &b1);
        near0 = a0.d + b0.d >= a1.d + b1.d;
        edge_labels(near0 ? &a0 : &a1, near0 ? &b0 : &b1, &c, g3.ymin, g3.ymax, "y");
        int best = 0;
        float bx = 1e9f;
        for (int k = 0; k < 4; k++) {
            g3p_t p;
            project(v, k & 1 ? 1.f : -1.f, k & 2 ? 1.f : -1.f, 0, &p);
            if (p.sx < bx)
                bx = p.sx, best = k;
        }
        float ex = best & 1 ? 1.f : -1.f, ey = best & 2 ? 1.f : -1.f;
        project(v, ex, ey, -1, &a), project(v, ex, ey, 1, &b);
        edge_labels(&a, &b, &c, g3.zmin, g3.zmax, "z");
    } else if (g3.axes) {
        g3_box_t bx;
        g3_box(&bx);
        float o[3];
        for (int k = 0; k < 3; k++)
            o[k] = (float)(-(bx.lo[k] + bx.hi[k]) / (bx.hi[k] - bx.lo[k]));
        static const char *const nm[3] = {"x", "y", "z"};
        for (int k = 0; k < 3; k++) {
            float p[3] = {o[0], o[1], o[2]};
            p[k] = 1.1f;
            if (fabsf(o[(k + 1) % 3]) > 1 || fabsf(o[(k + 2) % 3]) > 1)
                continue;
            project(v, p[0], p[1], p[2], &a);
            text_at((int)a.sx, (int)a.sy, nm[k], TH_FG);
        }
    }
}

/* Top left: one line per enabled non-empty entry, "Z1" in its colour, or
 * "Z1: message" in red when it does not compile. A hint when nothing is drawn. */
static void draw_legend(void)
{
    int y = BODY_Y + 2, nd = 0;
    char b[64];
    for (int i = 0; i < G3_NENT; i++) {
        if (!g3.ent[i].enabled || g3_entry_empty(i))
            continue;
        const char *err = g3_error(i);
        if (err[0])
            snprintf(b, sizeof b, "Z%d: %.40s", i + 1, err);
        else
            snprintf(b, sizeof b, "Z%d", i + 1);
        bool off = g3.map && g3.ent[i].kind != G3K_FUNC; /* not in the contour map */
        gfx_text(&font_small, 3, y, b, err[0] ? TH_ERR : off ? TH_DIM : func_colors[g3.ent[i].color % NCOLORS],
                 GFX_TRANSPARENT);
        y += 12;
        nd += g3_mesh(i) != NULL && !off;
    }
    if (!nd && g3.map)
        gfx_text(&font_small, 3, UI_BOTTOM - BAND_H + 1, "contour map: z=f(x,y) entries only", TH_DIM,
                 GFX_TRANSPARENT);
    else if (!nd)
        gfx_text(&font_small, 3, UI_BOTTOM - BAND_H + 1, "z= (F1): enter z=f(x,y) or a surface", TH_DIM,
                 GFX_TRANSPARENT);
}

/* entry e's mesh if it is drawn in the current view (the contour map: z = f(x,y) only) */
static g3_mesh_t *shown_mesh(int e)
{
    g3_mesh_t *m = g3_mesh(e);
    return m && (!g3.map || m->kind == G3K_FUNC) ? m : NULL;
}

/* the traced mesh, if tracing is possible */
static g3_mesh_t *trace_mesh(void) { return tracing ? shown_mesh(tr_ent) : NULL; }

/* Text of the trace band for the current node: "Z1 x=.. y=.. z=.." (z=f(x,y)),
 * "Z2 u=.. v=.. x=.. y=.. z=.." or "Z3 t=.. x=.. y=.. z=..". */
static void trace_text(char *out, size_t n)
{
    g3_mesh_t *m = trace_mesh();
    out[0] = 0;
    if (!m)
        return;
    const g3w_t *w = &m->w[tr_j * m->nu + tr_i];
    char a[16], b[16], c[16], u[16], v[16];
    double du = (m->u1 - m->u0) / (m->nu - 1), pu = tr_i == m->nu - 1 ? m->u1 : m->u0 + tr_i * du;
    double pv = m->nv > 1 ? (tr_j == m->nv - 1 ? m->v1 : m->v0 + tr_j * (m->v1 - m->v0) / (m->nv - 1)) : 0;
    if (isnan(w->x))
        snprintf(a, sizeof a, "?"), snprintf(b, sizeof b, "?"), snprintf(c, sizeof c, "undef");
    else
        fmt_num(w->x, 7, a, sizeof a), fmt_num(w->y, 7, b, sizeof b), fmt_num(w->z, 7, c, sizeof c);
    fmt_num(pu, 7, u, sizeof u);
    fmt_num(pv, 7, v, sizeof v);
    if (m->kind == G3K_FUNC)
        snprintf(out, n, "Z%d x=%s y=%s z=%s", tr_ent + 1, u, v, c);
    else if (m->kind == G3K_PSURF)
        snprintf(out, n, "Z%d u=%s v=%s x=%s y=%s z=%s", tr_ent + 1, u, v, a, b, c);
    else
        snprintf(out, n, "Z%d t=%s x=%s y=%s z=%s", tr_ent + 1, u, a, b, c);
}

/* Trace cursor at the projected node: a cross in the theme's text colour with
 * a 1 px outline in the background colour, around a dot in the entry's colour
 * ringed the same way, so it stays visible on a shaded surface of any colour;
 * and the band with its coordinates. */
static void draw_trace(void)
{
    g3_mesh_t *m = trace_mesh();
    if (!m)
        return;
    const g3p_t *p = &m->p[tr_j * m->nu + tr_i];
    if (!isnan(p->d)) {
        int x = (int)floorf(p->sx), y = (int)floorf(p->sy);
        /* outlines first: 3 px wide bars under the 1 px arms, a 7x7 ring under the dot */
        gfx_fill_rect(x - 9, y - 1, 6, 3, TH_BG);
        gfx_fill_rect(x + 4, y - 1, 6, 3, TH_BG);
        gfx_fill_rect(x - 1, y - 9, 3, 6, TH_BG);
        gfx_fill_rect(x - 1, y + 4, 3, 6, TH_BG);
        gfx_fill_rect(x - 3, y - 3, 7, 7, TH_BG);
        gfx_hline(x - 8, y, 5, TH_FG);
        gfx_hline(x + 4, y, 5, TH_FG);
        gfx_vline(x, y - 8, 5, TH_FG);
        gfx_vline(x, y + 4, 5, TH_FG);
        gfx_rect(x - 2, y - 2, 5, 5, TH_FG);
        gfx_fill_rect(x - 1, y - 1, 3, 3, func_colors[g3.ent[tr_ent].color % NCOLORS]);
    }
    char b[96];
    trace_text(b, sizeof b);
    gfx_fill_rect(0, UI_BOTTOM - BAND_H, LCD_WIDTH, BAND_H, TH_PANEL);
    gfx_text(&font_small, 3, UI_BOTTOM - BAND_H + 1, b, TH_FG, GFX_TRANSPARENT);
}

/* Copy the rendered image to the screen and draw the overlays. The body is
 * filled first so the text log forgets the previous labels. */
static void present(void)
{
    g3_view_t v;
    view_setup(&v);
    gfx_set_clip(0, BODY_Y, LCD_WIDTH, BODY_H);
    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, TH_BG);
    gfx_restore_rect(g3d_image, 0, BODY_Y, LCD_WIDTH, BODY_H);
    if (g3.map) {
        draw_map_labels(&v);
        draw_map_key();
    } else {
        draw_box_labels(&v);
    }
    draw_legend();
    draw_trace();
    int ar[4];
    anim_overlay(ANIM_SCR_3D, ar); /* the animation's play bar (modules/animation) */
    gfx_reset_clip();
}

/* anim.h: one animation frame with the animated variable already set:
 * re-sample the meshes on `grid` cells per side (0: the setting; coarser
 * keeps the frame rate, as dragging does), render, present. */
void g3d_anim_frame(int grid)
{
    if (!g3d_have_mem())
        return;
    g3_evaluate();
    render(1);
    img_valid = grid == 0;
    present();
}

/* Screen draw: full render if the image is stale, then present. */
static void g3_draw(void)
{
    if (!g3d_have_mem()) {
        gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, TH_BG);
        gfx_text(&font_medium, 8, BODY_Y + 40, "3D graphing needs the SDRAM", TH_ERR, GFX_TRANSPARENT);
        return;
    }
    if (!img_valid)
        render(1);
    present();
}

/* a coarse frame during a gesture */
static void draw_coarse(void)
{
    render(coarse_stride());
    present();
}

/* ------------------------------------------------------------ trace */
/* first drawn entry at or after i (wrapping); -1 if none */
static int next_drawn(int i)
{
    for (int k = 0; k < G3_NENT; k++) {
        int e = (i + k) % G3_NENT;
        if (shown_mesh(e))
            return e;
    }
    return -1;
}

/* Start tracing entry e at its centre node (false if nothing is drawn). */
static bool trace_start(int e)
{
    e = next_drawn(e);
    if (e < 0) {
        tracing = false;
        return false;
    }
    g3_mesh_t *m = shown_mesh(e);
    tracing = true;
    tr_ent = e;
    tr_i = m->nu / 2;
    tr_j = m->nv / 2;
    return true;
}

/* Move the trace cursor by (di, dj) nodes, clamped to the grid. */
static void trace_move(int di, int dj)
{
    g3_mesh_t *m = trace_mesh();
    if (!m)
        return;
    if (m->nv == 1)
        di += dj * 10, dj = 0;
    tr_i += di, tr_j += dj;
    if (tr_i < 0)
        tr_i = 0;
    if (tr_i > m->nu - 1)
        tr_i = m->nu - 1;
    if (tr_j < 0)
        tr_j = 0;
    if (tr_j > m->nv - 1)
        tr_j = m->nv - 1;
}

/* Trace the node nearest to screen point (x, y) over all drawn entries
 * (within 24 px); false if none is that close. */
static bool trace_pick(int x, int y)
{
    float best = 24.0f * 24.0f;
    bool found = false;
    for (int e = 0; e < G3_NENT; e++) {
        g3_mesh_t *m = shown_mesh(e);
        if (!m)
            continue;
        for (int k = 0; k < m->nu * m->nv; k++) {
            const g3p_t *p = &m->p[k];
            if (isnan(p->d) || (!g3.map && (isnan(m->n[k].x) || fabsf(m->n[k].x) > 1.0001f ||
                                            fabsf(m->n[k].y) > 1.0001f || fabsf(m->n[k].z) > 1.0001f)))
                continue;
            float dx = p->sx - (float)x, dy = p->sy - (float)y, d2 = dx * dx + dy * dy;
            if (d2 < best)
                best = d2, found = true, tracing = true, tr_ent = e, tr_i = k % m->nu, tr_j = k / m->nu;
        }
    }
    return found;
}

/* ------------------------------------------------------------ actions */
static void view_changed(void)
{
    if (g3.elev > 90)
        g3.elev = 90;
    if (g3.elev < -90)
        g3.elev = -90;
    g3.azim = fmod(g3.azim, 360.0);
    if (g3.azim > 180)
        g3.azim -= 360;
    if (g3.azim <= -180)
        g3.azim += 360;
    if (g3.zoom < 0.25)
        g3.zoom = 0.25;
    if (g3.zoom > 8)
        g3.zoom = 8;
    img_valid = false;
}

static void act_reset(void)
{
    g3.map = false;
    g3.azim = G3_AZIM_DEF, g3.elev = G3_ELEV_DEF, g3.zoom = 1;
    view_changed();
}
static void act_style(void)
{
    g3.style = g3.style == G3S_SHADE ? G3S_WIRE : G3S_SHADE;
    img_valid = false;
}
static void act_color(void)
{
    g3.colormode = g3.colormode == G3C_HEIGHT ? G3C_ENTRY : G3C_HEIGHT;
    img_valid = false;
}
/* z range from the drawn surfaces z = f(x,y) (their finite values over the
 * x-y window), then re-evaluate (the normalisation changed) */
static void act_zfit(void)
{
    if (!g3_valid())
        g3_evaluate();
    double lo = INFINITY, hi = -INFINITY;
    for (int i = 0; i < G3_NENT; i++) {
        g3_mesh_t *m = g3_mesh(i);
        if (!m || m->zlo > m->zhi)
            continue;
        lo = fmin(lo, m->zlo);
        hi = fmax(hi, m->zhi);
    }
    if (!(lo <= hi))
        return;
    if (hi - lo < 1e-9 * fmax(1, fabs(lo)))
        lo -= 1, hi += 1;
    g3.zmin = lo, g3.zmax = hi;
    g3_invalidate();
    img_valid = false;
}

/* soft keys of the graph screen: run on the graph, then show it */
static void show_graph(void)
{
    if (ui_current() == &scr_g3d) {
        g3_draw();
    } else {
        ui_returning = true; /* keep the trace */
        ui_switch(&scr_g3d);
        ui_returning = false;
    }
}
static void sk_reset(void) { act_reset(); show_graph(); }
static void sk_style(void) { act_style(); show_graph(); }
static void sk_color(void) { act_color(); show_graph(); }
static void sk_zfit(void) { act_zfit(); show_graph(); }

/* ------------------------------------------------------------ VIEW menu */
enum { V_RESET = 1, V_TOP, V_FRONT, V_SIDE, V_MAP, V_CONT, V_WIRE, V_SHADE, V_COLOR, V_AXES, V_BOX, V_ZIN, V_ZOUT, V_ZFIT, V_N };

static int vm_count(void) { return V_N - 1; }
/* items; the toggles show their current state */
static void vm_get(int i, menu_item_t *out)
{
    static char lb[32];
    int a = i + 1;
    switch (a) {
    case V_RESET: snprintf(lb, sizeof lb, "Standard view"); break;
    case V_TOP: snprintf(lb, sizeof lb, "Top view (x-y)"); break;
    case V_FRONT: snprintf(lb, sizeof lb, "Front view (x-z)"); break;
    case V_SIDE: snprintf(lb, sizeof lb, "Side view (y-z)"); break;
    case V_MAP: snprintf(lb, sizeof lb, "Contour map%s", g3.map ? " ◀" : ""); break;
    case V_CONT: snprintf(lb, sizeof lb, "Contours: %s", g3.contours ? "on" : "off"); break;
    case V_WIRE: snprintf(lb, sizeof lb, "Wireframe%s", g3.style == G3S_WIRE ? " ◀" : ""); break;
    case V_SHADE: snprintf(lb, sizeof lb, "Shaded%s", g3.style == G3S_SHADE ? " ◀" : ""); break;
    case V_COLOR: snprintf(lb, sizeof lb, "Colour by height: %s", g3.colormode == G3C_HEIGHT ? "on" : "off"); break;
    case V_AXES: snprintf(lb, sizeof lb, "Axes: %s", g3.axes ? "on" : "off"); break;
    case V_BOX: snprintf(lb, sizeof lb, "Box and labels: %s", g3.box ? "on" : "off"); break;
    case V_ZIN: snprintf(lb, sizeof lb, "Zoom in"); break;
    case V_ZOUT: snprintf(lb, sizeof lb, "Zoom out"); break;
    default: snprintf(lb, sizeof lb, "Zoom fit z"); break;
    }
    *out = (menu_item_t){lb, NULL, NULL, a, "graph3d-view"};
}

/* Apply a VIEW menu item and show the graph (from any 3D screen). */
static void vm_select(const menu_item_t *it)
{
    switch (it->action) {
    case V_RESET: act_reset(); break;
    case V_TOP: g3.azim = 0, g3.elev = 90, g3.map = false; break;
    case V_FRONT: g3.azim = 0, g3.elev = 0, g3.map = false; break;
    case V_SIDE: g3.azim = -90, g3.elev = 0, g3.map = false; break;
    case V_MAP: g3.map = true; break;
    case V_CONT: g3.contours = !g3.contours; break;
    case V_WIRE: g3.style = G3S_WIRE; break;
    case V_SHADE: g3.style = G3S_SHADE; break;
    case V_COLOR: act_color(); break;
    case V_AXES: g3.axes = !g3.axes; break;
    case V_BOX: g3.box = !g3.box; break;
    case V_ZIN: g3.zoom *= 1.25; break;
    case V_ZOUT: g3.zoom /= 1.25; break;
    case V_ZFIT: act_zfit(); break;
    }
    view_changed();
    ui_returning = true;
    ui_switch(&scr_g3d);
    ui_returning = false;
}

static const menu_def_t menu_g3d_view = {"3D VIEW", NULL, 0, vm_select, false, "graph3d-view", vm_count, vm_get};
void g3d_view_menu(void) { menu_open(&menu_g3d_view); }

/* "trace" from the entry and window screens: go to the graph and start TRACE */
void g3d_trace_key(void)
{
    if (ui_current() != &scr_g3d)
        ui_switch(&scr_g3d);
    scr_g3d.key(KEY_F4, MOD_NONE);
}

/* ------------------------------------------------------------ screen */
/* Fresh visit: re-evaluate (CAS variables may have changed), keep the view
 * and the trace position if it is still valid. */
static void g3_enter(void)
{
    if (ui_returning)
        return;
    home_graph_enter(true); /* (graphing from the home screen: the graph app on a new cell) */
    g3_invalidate();
    img_valid = false;
    gesture = false;
}

static void g3_key(int key, int mod)
{
    if (!g3d_have_mem()) {
        if (key == KEY_CLEAR)
            ui_switch(ui_home());
        return;
    }
    switch (key) {
    case KEY_LEFT:
    case KEY_RIGHT:
    case KEY_UP:
    case KEY_DOWN:
        if (trace_mesh()) {
            trace_move(key == KEY_LEFT ? -1 : key == KEY_RIGHT ? 1 : 0,
                       key == KEY_UP ? 1 : key == KEY_DOWN ? -1 : 0);
        } else if (key == KEY_LEFT || key == KEY_RIGHT) {
            g3.map = false; /* turning: back to the 3D view */
            g3.azim += key == KEY_RIGHT ? 15 : -15;
            view_changed();
        } else {
            g3.map = false;
            g3.elev += key == KEY_DOWN ? 15 : -15;
            view_changed();
        }
        break;
    case KEY_ADD: g3.zoom *= 1.25; view_changed(); break;
    case KEY_SUB: g3.zoom /= 1.25; view_changed(); break;
    case KEY_F4:
        if (mod != MOD_NONE)
            return;
        if (!g3_valid())
            g3_evaluate();
        if (trace_mesh())
            trace_start(tr_ent + 1);
        else
            trace_start(0);
        break;
    case KEY_CLEAR:
        if (tracing) {
            tracing = false;
            break;
        }
        ui_switch(ui_home());
        return;
    default:
        return;
    }
    g3_draw();
    ui_softkeys_invalidate();
}

/* Drag: rotate (0.5° per pixel, coarse frames); pinch: zoom; lift: full
 * frame; tap: trace the nearest node. */
static void g3_touch(const touch_event_t *ev)
{
    if (!g3d_have_mem() || ev->y < BODY_Y)
        return;
    switch (ev->type) {
    case TEV_DRAG:
        gesture = true;
        g3.map = false;
        g3.azim += ev->dx * 0.5;
        g3.elev += ev->dy * 0.5;
        view_changed();
        draw_coarse();
        break;
    case TEV_PINCH:
        if (!gesture)
            zoom0 = g3.zoom;
        gesture = true;
        g3.zoom = zoom0 * (double)ev->scale;
        view_changed();
        draw_coarse();
        break;
    case TEV_UP:
    case TEV_PINCH_END:
    case TEV_CANCEL:
        if (ev->type == TEV_PINCH_END && gesture)
            g3.zoom = zoom0 * (double)ev->scale, view_changed();
        gesture = false;
        img_valid = false;
        g3_draw();
        break;
    case TEV_TAP:
        if (trace_pick(ev->x, ev->y)) {
            g3_draw();
            ui_softkeys_invalidate();
        }
        break;
    default: break;
    }
}

/* soft keys: "trace" turns into "next" while tracing */
const softkeys_t g3d_softkeys = {
    {{"z=", NULL, &scr_g3d_eq}, {"window", NULL, &scr_g3d_win}, {"view", g3d_view_menu}, {"trace"},
     {"graph", NULL, &scr_g3d}},
    {{"reset", sk_reset}, {"style", sk_style}, {"color", sk_color}, {"zfit", sk_zfit}, {NULL}},
    NULL,
};
static const softkeys_t g3d_softkeys_trace = {
    {{"z=", NULL, &scr_g3d_eq}, {"window", NULL, &scr_g3d_win}, {"view", g3d_view_menu}, {"next"},
     {"graph", NULL, &scr_g3d}},
    {{"reset", sk_reset}, {"style", sk_style}, {"color", sk_color}, {"zfit", sk_zfit}, {NULL}},
    NULL,
};
static const softkeys_t *g3_softkeys(void)
{
    return anim_softkeys(trace_mesh() ? &g3d_softkeys_trace : &g3d_softkeys, ANIM_SCR_3D); /* F5 "anim" */
}
static void g3_tick(uint32_t now) { anim_tick(now, ANIM_SCR_3D); }

const screen_t scr_g3d = {"graph3d", "3D GRAPH", g3_enter, g3_draw, g3_key, g3_tick, NULL, g3_touch,
                          "graph3d", &g3d_softkeys, g3_softkeys};

/* 3D graph (CATALOG, the console app graph3d; not listed in APPS): from the home screen, a 3D graph cell */
void g3d_open(void)
{
    if (ui_current() == &scr_home)
        home_graph_new(true);
    else
        ui_switch(&scr_g3d);
}
const app_def_t app_graph3d = {"graph3d", "3D graph", g3d_open, {{0}}, "graph3d"};

/* ------------------------------------------------------------ console / state */
/* `state` keys: g3d_trace (band text, "" when not tracing), g3d_trace_on,
 * g3d_render_us / g3d_coarse_us / g3d_eval_us (last durations), g3d_contour_segs
 * (contour segments of the last full render). */
void g3d_trace_state(void (*kv)(const char *, const char *, void *), void *user)
{
    char b[96];
    trace_text(b, sizeof b);
    kv("g3d_trace", b, user);
    kv("g3d_trace_on", trace_mesh() ? "1" : "0", user);
    snprintf(b, sizeof b, "%lu", (unsigned long)render_us);
    kv("g3d_render_us", b, user);
    snprintf(b, sizeof b, "%lu", (unsigned long)coarse_us);
    kv("g3d_coarse_us", b, user);
    snprintf(b, sizeof b, "%lu", (unsigned long)g3_eval_us());
    kv("g3d_eval_us", b, user);
    snprintf(b, sizeof b, "%d", contour_segs);
    kv("g3d_contour_segs", b, user);
}

/* g3d bench [N]: evaluate once, then N full and N coarse renders turning the
 * view by 3° per frame (the view is restored); prints the mean times in us
 * (rendering into the image only; the copy to the display is not included). */
static int g3d_bench(int n)
{
    if (!g3d_have_mem())
        return con_error("no 3D work memory");
    g3_invalidate();
    g3_evaluate();
    double az = g3.azim;
    uint64_t full = 0, coarse = 0;
    for (int k = 0; k < n; k++) {
        g3.azim = az + 3 * k;
        render(1);
        full += render_us;
    }
    for (int k = 0; k < n; k++) {
        g3.azim = az + 3 * k;
        render(coarse_stride());
        coarse += coarse_us;
    }
    g3.azim = az;
    img_valid = false;
    int nodes = 0;
    for (int i = 0; i < G3_NENT; i++)
        if (g3_mesh(i))
            nodes += g3_mesh(i)->nu * g3_mesh(i)->nv;
    con_printf("nodes=%d grid=%d eval_us=%lu render_us=%lu coarse_us=%lu (stride %d)\n", nodes, g3_grid(),
               (unsigned long)g3_eval_us(), (unsigned long)(full / (unsigned)n), (unsigned long)(coarse / (unsigned)n),
               coarse_stride());
    if (ui_current() == &scr_g3d)
        g3_draw();
    return 0;
}

/* Console `g3d`: set N P TEXT (expression P=1..3 of entry N), kind N z|surf|curve,
 * view AZIM ELEV [ZOOM], bench [N]. */
int g3d_console(int argc, char **argv)
{
    if (argc >= 5 && !strcmp(argv[1], "set")) {
        g3_set_text(atoi(argv[2]) - 1, atoi(argv[3]) - 1, argv[4]);
        img_valid = false;
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "kind")) {
        int i = atoi(argv[2]) - 1;
        if (i < 0 || i >= G3_NENT)
            return con_error("entry 1..6");
        g3.ent[i].kind = !strcmp(argv[3], "surf") ? G3K_PSURF : !strcmp(argv[3], "curve") ? G3K_CURVE : G3K_FUNC;
        g3_invalidate();
        img_valid = false;
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "view")) {
        g3.azim = atof(argv[2]);
        g3.elev = atof(argv[3]);
        g3.map = false;
        if (argc >= 5)
            g3.zoom = atof(argv[4]);
        view_changed();
        if (ui_current() == &scr_g3d)
            g3_draw();
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "bench"))
        return g3d_bench(argc >= 3 && atoi(argv[2]) > 0 ? atoi(argv[2]) : 20);
    return con_error("usage: g3d set N P TEXT | kind N z|surf|curve | view AZ EL [ZOOM] | bench [N]");
}
