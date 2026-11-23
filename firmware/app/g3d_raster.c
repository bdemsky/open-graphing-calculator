/*
 * 3D graphing rasterizer and scene renderer (see g3d.h).
 *
 * Triangles. Scanline conversion with a z-buffer: for every pixel row whose
 * centre (y + 0.5) lies within the triangle, the span is bounded by the two
 * edges that row crosses, and a pixel is drawn when its centre (x + 0.5) lies
 * within [x_left, x_right], both ends inclusive. Each edge's crossing is
 * computed from its endpoints in a canonical order (upper endpoint first), so
 * two triangles sharing an edge get bit-identical crossings and no pixel along
 * the edge is left uncovered (it may be drawn by both, which the z-buffer makes
 * harmless). Depth d (1 / (E - z_eye), g3d_view.c) and, for triangles that
 * reach outside the view box, the normalised world coordinates are
 * interpolated as planes over the screen (attribute gradients from the
 * triangle's edge vectors divided by twice its signed area). Per-pixel box
 * clipping makes surfaces end exactly at the box faces; triangles entirely
 * inside the box skip it (a triangle inside a convex box stays inside). The
 * world coordinates are interpolated affinely, not perspective-correctly; at
 * this weak perspective the error moves a clip edge by well under a pixel.
 *
 * Lines. A DDA with one step per pixel of the longer axis; the depth (and the
 * world position, for box clipping) is interpolated along the line. Z-tested
 * lines (mesh lines over shaded surfaces, the box and axes) are drawn where
 * they are no more than G3_ZBIAS behind the z-buffer and never write it, so a
 * mesh line lying on its own surface is always visible.
 *
 * Shading. Flat per grid cell: the cell normal is the cross product of its two
 * diagonals in normalised world space, lit by a light fixed to the camera
 * (upper left, in front: it turns with the view) with Lambert's cosine law,
 * two-sided (|n . L|), intensity 0.3 + 0.7 |n . L|; the side facing away from
 * the viewer is darkened to 75 % so the underside of a surface reads as such.
 * The colour is the entry's colour or, in height mode, the colour map g3_heat
 * at the cell's mean normalised z.
 *
 * Contours (see g3d.h). Marching squares per grid cell: a level between the
 * cell's lowest and highest corner crosses two or four of its edges (linear
 * interpolation along each edge) and the crossings are joined by segments, a
 * saddle's four by the centre value (in 3D: per triangle of the cell as it is
 * filled, so the lines lie on the drawn surface). Neighbouring cells share their edges'
 * crossings, so the segments join into the level curves. In 3D the curves
 * replace the grid lines of z = f(x,y) surfaces (z-tested over shaded ones);
 * the contour map draws them flat (from the world x, y of the nodes) over
 * bands filled pixel by pixel from the height interpolated over four
 * triangles around the cell's centre, whose borders follow the lines within
 * about a pixel.
 *
 * Floating-point contraction is off (determinism between the native and
 * WebAssembly builds, see g3d_view.c). Everything here is float: the M7 FPU
 * does single precision in one cycle per operation.
 */
#ifdef __clang__
#pragma clang fp contract(off)
#endif
#include <math.h>
#include <string.h>
#include "g3d.h"
#include "theme.h"

#define BOX_EPS 1.0001f /* clip tolerance: nodes on the box faces are inside */

/* ------------------------------------------------------------ colours */
/* RGB565 colour with every channel scaled by k (0..1), rounded. */
uint16_t g3_shade(uint16_t c, float k)
{
    if (k < 0)
        k = 0;
    if (k > 1)
        k = 1;
    int r = (int)((float)(c >> 11) * k + 0.5f);
    int gr = (int)((float)((c >> 5) & 63) * k + 0.5f);
    int b = (int)((float)(c & 31) * k + 0.5f);
    return (uint16_t)(r << 11 | gr << 5 | b);
}

/* Height colour map: blue (low) - cyan - green - yellow - red (high), linear
 * between five stops; h is clamped to [-1, 1]. */
uint16_t g3_heat(float h)
{
    static const uint8_t stop[5][3] = {{30, 70, 230}, {0, 180, 220}, {40, 180, 60}, {240, 200, 0}, {225, 45, 30}};
    if (!(h > -1))
        h = -1;
    if (h > 1)
        h = 1;
    float t = (h + 1) * 2; /* 0..4 */
    int k = (int)t;
    if (k > 3)
        k = 3;
    float f = t - (float)k;
    int c[3];
    for (int i = 0; i < 3; i++)
        c[i] = (int)((float)stop[k][i] + ((float)stop[k + 1][i] - (float)stop[k][i]) * f + 0.5f);
    return RGB565(c[0], c[1], c[2]);
}

/* ------------------------------------------------------------ primitives */
/* Fill the target rectangle with bg and reset its z-buffer to "infinitely far". */
void g3r_clear(const g3_target_t *t, uint16_t bg)
{
    for (int y = 0; y < t->h; y++) {
        uint16_t *row = t->px + (t->y0 + y) * t->stride + t->x0;
        for (int x = 0; x < t->w; x++)
            row[x] = bg;
    }
    if (t->zb)
        for (int i = 0; i < t->w * t->h; i++)
            t->zb[i] = -INFINITY;
}

static bool in_box(const g3v_t *w)
{
    return fabsf(w->x) <= BOX_EPS && fabsf(w->y) <= BOX_EPS && fabsf(w->z) <= BOX_EPS;
}

/* x where the edge (p, q) crosses the row centre yc, with the endpoints taken in
 * canonical order (smaller y first, then smaller x) so shared edges agree.
 * false if the edge does not span yc or is horizontal. */
static bool edge_x(const g3p_t *p, const g3p_t *q, float yc, float *x)
{
    if (q->sy < p->sy || (q->sy == p->sy && q->sx < p->sx)) {
        const g3p_t *s = p;
        p = q, q = s;
    }
    if (yc < p->sy || yc > q->sy || q->sy == p->sy)
        return false;
    *x = p->sx + (yc - p->sy) * (q->sx - p->sx) / (q->sy - p->sy);
    return true;
}

/* Gradient (d/dx, d/dy) of an attribute with values a0, a1, a2 at the three
 * vertices, over the screen plane: from the edge vectors e1 = v1 - v0,
 * e2 = v2 - v0 and inv2a = 1 / (e1 x e2). */
static void grad(float a0, float a1, float a2, float e1x, float e1y, float e2x, float e2y, float inv2a,
                 float *ogx, float *ogy)
{
    float d1 = a1 - a0, d2 = a2 - a0;
    *ogx = (d1 * e2y - d2 * e1y) * inv2a;
    *ogy = (d2 * e1x - d1 * e2x) * inv2a;
}

/* Height bands of a filled triangle (the contour map): the heights h at the
 * three vertices, interpolated over the screen like the depth; a pixel of
 * height v takes the colour pal[floor((v + 1) q)] (clamped to 0..n). */
typedef struct {
    float h[3], q;
    int n;
    const uint16_t *pal;
} bands_t;

/* g3r_tri, with the colour from bands (bd) instead of `color` if bd is given */
static void tri(const g3_target_t *t, const g3p_t *a, const g3p_t *b, const g3p_t *c, const g3v_t *wa,
                const g3v_t *wb, const g3v_t *wc, uint16_t color, const bands_t *bd)
{
    if (isnan(a->d) || isnan(b->d) || isnan(c->d))
        return;
    float e1x = b->sx - a->sx, e1y = b->sy - a->sy, e2x = c->sx - a->sx, e2y = c->sy - a->sy;
    float area2 = e1x * e2y - e2x * e1y;
    if (area2 == 0 || !isfinite(area2))
        return;
    float inv = 1.0f / area2;
    /* depth plane */
    float dgx, dgy;
    grad(a->d, b->d, c->d, e1x, e1y, e2x, e2y, inv, &dgx, &dgy);
    float hgx = 0, hgy = 0;
    if (bd)
        grad(bd->h[0], bd->h[1], bd->h[2], e1x, e1y, e2x, e2y, inv, &hgx, &hgy);
    /* world planes, only when the triangle leaves the box */
    bool clip = wa && !(in_box(wa) && in_box(wb) && in_box(wc));
    float wgx[3] = {0}, wgy[3] = {0}, w0[3] = {0};
    if (clip) {
        const float A[3] = {wa->x, wa->y, wa->z}, B[3] = {wb->x, wb->y, wb->z}, C[3] = {wc->x, wc->y, wc->z};
        for (int k = 0; k < 3; k++) {
            grad(A[k], B[k], C[k], e1x, e1y, e2x, e2y, inv, &wgx[k], &wgy[k]);
            w0[k] = A[k];
        }
    }
    float ymin = fminf(a->sy, fminf(b->sy, c->sy)), ymax = fmaxf(a->sy, fmaxf(b->sy, c->sy));
    int py0 = (int)ceilf(ymin - 0.5f), py1 = (int)floorf(ymax - 0.5f);
    if (py0 < t->y0)
        py0 = t->y0;
    if (py1 > t->y0 + t->h - 1)
        py1 = t->y0 + t->h - 1;
    for (int py = py0; py <= py1; py++) {
        float yc = (float)py + 0.5f, xs[3];
        int n = 0;
        if (edge_x(a, b, yc, &xs[n]))
            n++;
        if (edge_x(b, c, yc, &xs[n]))
            n++;
        if (edge_x(c, a, yc, &xs[n]))
            n++;
        if (n < 2)
            continue;
        float xl = fminf(xs[0], fminf(xs[1], n > 2 ? xs[2] : xs[1]));
        float xr = fmaxf(xs[0], fmaxf(xs[1], n > 2 ? xs[2] : xs[1]));
        int px0 = (int)ceilf(xl - 0.5f), px1 = (int)floorf(xr - 0.5f);
        if (px0 < t->x0)
            px0 = t->x0;
        if (px1 > t->x0 + t->w - 1)
            px1 = t->x0 + t->w - 1;
        if (px0 > px1)
            continue;
        /* attribute values at the first pixel centre, then step by the x gradient */
        float ox = (float)px0 + 0.5f - a->sx, oy = yc - a->sy;
        float d = a->d + dgx * ox + dgy * oy;
        float hv = bd ? bd->h[0] + hgx * ox + hgy * oy : 0;
        float wx = 0, wy = 0, wz = 0;
        if (clip) {
            wx = w0[0] + wgx[0] * ox + wgy[0] * oy;
            wy = w0[1] + wgx[1] * ox + wgy[1] * oy;
            wz = w0[2] + wgx[2] * ox + wgy[2] * oy;
        }
        uint16_t *row = t->px + py * t->stride;
        float *zr = t->zb ? t->zb + (py - t->y0) * t->w - t->x0 : NULL;
        for (int px = px0; px <= px1; px++) {
            bool vis = !clip || (fabsf(wx) <= BOX_EPS && fabsf(wy) <= BOX_EPS && fabsf(wz) <= BOX_EPS);
            if (bd) {
                float f = (hv + 1) * bd->q;
                color = bd->pal[f < 0 ? 0 : f >= (float)bd->n ? bd->n : (int)f];
                hv += hgx;
            }
            if (vis && (!zr || d > zr[px])) {
                row[px] = color;
                if (zr)
                    zr[px] = d;
            }
            d += dgx;
            if (clip)
                wx += wgx[0], wy += wgx[1], wz += wgx[2];
        }
    }
}

void g3r_tri(const g3_target_t *t, const g3p_t *a, const g3p_t *b, const g3p_t *c, const g3v_t *wa,
             const g3v_t *wb, const g3v_t *wc, uint16_t color)
{
    tri(t, a, b, c, wa, wb, wc, color, NULL);
}

/* one pixel of a line: inside the target and, if z-tested, not behind the z-buffer */
static void line_px(const g3_target_t *t, int x, int y, float d, uint16_t color, bool ztest)
{
    if (x < t->x0 || y < t->y0 || x >= t->x0 + t->w || y >= t->y0 + t->h)
        return;
    if (ztest && t->zb && d < t->zb[(y - t->y0) * t->w + (x - t->x0)] - G3_ZBIAS)
        return;
    t->px[y * t->stride + x] = color;
}

void g3r_line(const g3_target_t *t, const g3p_t *a, const g3p_t *b, const g3v_t *wa, const g3v_t *wb,
              uint16_t color, bool ztest, bool thick)
{
    if (isnan(a->d) || isnan(b->d))
        return;
    float dx = b->sx - a->sx, dy = b->sy - a->sy;
    float len = fmaxf(fabsf(dx), fabsf(dy));
    if (!(len < 4096)) /* also NAN */
        return;
    int n = (int)ceilf(len);
    bool clip = wa && !(in_box(wa) && in_box(wb));
    for (int k = 0; k <= n; k++) {
        float s = n ? (float)k / (float)n : 0;
        if (clip) {
            float x = wa->x + (wb->x - wa->x) * s, y = wa->y + (wb->y - wa->y) * s, z = wa->z + (wb->z - wa->z) * s;
            if (fabsf(x) > BOX_EPS || fabsf(y) > BOX_EPS || fabsf(z) > BOX_EPS)
                continue;
        }
        int x = (int)floorf(a->sx + dx * s), y = (int)floorf(a->sy + dy * s);
        float d = a->d + (b->d - a->d) * s;
        line_px(t, x, y, d, color, ztest);
        if (thick) {
            line_px(t, x + 1, y, d, color, ztest);
            line_px(t, x, y + 1, d, color, ztest);
            line_px(t, x + 1, y + 1, d, color, ztest);
        }
    }
}

/* ------------------------------------------------------------ scene */
typedef struct {
    const g3_target_t *t;
    const g3_scene_t *s;
    float light[3];   /* world-space light direction (unit) */
    float toward[3];  /* world-space direction towards the viewer */
    /* contours */
    int n;            /* contour levels */
    float q;          /* (levels + 1) / 2: level k is at normalised height k / q - 1 */
    double zc, zh;    /* centre and half-range of the window's z (map heights) */
    bool ztest;       /* contour lines z-tested (over shaded surfaces) */
    int segs;         /* contour segments drawn */
    uint16_t lc[G3_LEVELS_MAX + 1];   /* line colour per level (1..levels), of the current entry */
    uint16_t band[G3_LEVELS_MAX + 1]; /* map band colours (0..levels) */
} rctx_t;

static uint16_t map(const rctx_t *r, uint16_t c) { return r->s->map ? r->s->map(c) : c; }

/* Lit colour of a cell with normal (nx, ny, nz) (any length) and mean height h. */
static uint16_t cell_color(const rctx_t *r, int e, float nx, float ny, float nz, float h)
{
    float len = sqrtf(nx * nx + ny * ny + nz * nz);
    uint16_t base = r->s->colormode == G3C_HEIGHT ? g3_heat(h) : r->s->color[e];
    if (!(len > 0))
        return map(r, base);
    float l = (nx * r->light[0] + ny * r->light[1] + nz * r->light[2]) / len;
    float k = 0.3f + 0.7f * fabsf(l);
    if (nx * r->toward[0] + ny * r->toward[1] + nz * r->toward[2] < 0)
        k *= 0.75f;
    return map(r, g3_shade(base, k));
}

/* index of the next grid line after i with stride s (the last line is always drawn) */
static int next_line(int i, int s, int n) { return i + s < n - 1 ? i + s : n - 1; }

/* Filled cells of a surface (shaded style). A cell with one undefined corner
 * is drawn as the triangle of the other three. */
static void draw_cells(const rctx_t *r, int e, const g3_mesh_t *m)
{
    int s = r->s->stride;
    for (int j = 0; j < m->nv - 1; j = next_line(j, s, m->nv)) {
        int j2 = next_line(j, s, m->nv);
        for (int i = 0; i < m->nu - 1; i = next_line(i, s, m->nu)) {
            int i2 = next_line(i, s, m->nu);
            int id[4] = {j * m->nu + i, j * m->nu + i2, j2 * m->nu + i2, j2 * m->nu + i};
            int ok[4], nok = 0;
            for (int k = 0; k < 4; k++)
                if (!isnan(m->n[id[k]].x))
                    ok[nok++] = id[k];
            if (nok < 3)
                continue;
            const g3v_t *A = &m->n[ok[0]], *B = &m->n[ok[1]], *C = &m->n[ok[2]];
            const g3v_t *D = nok == 4 ? &m->n[ok[3]] : NULL;
            /* normal: cross product of the diagonals (quad) or of two edges (triangle) */
            float ux, uy, uz, vx, vy, vz;
            if (D) {
                ux = C->x - A->x, uy = C->y - A->y, uz = C->z - A->z;
                vx = D->x - B->x, vy = D->y - B->y, vz = D->z - B->z;
            } else {
                ux = B->x - A->x, uy = B->y - A->y, uz = B->z - A->z;
                vx = C->x - A->x, vy = C->y - A->y, vz = C->z - A->z;
            }
            float h = (A->z + B->z + C->z + (D ? D->z : 0)) / (float)nok;
            uint16_t c = cell_color(r, e, uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx, h);
            g3r_tri(r->t, &m->p[ok[0]], &m->p[ok[1]], &m->p[ok[2]], A, B, C, c);
            if (D)
                g3r_tri(r->t, &m->p[ok[0]], &m->p[ok[2]], &m->p[ok[3]], A, C, D, c);
        }
    }
}

/* colour of a line segment of entry e between normalised points a and b */
static uint16_t seg_color(const rctx_t *r, int e, const g3v_t *a, const g3v_t *b, float k)
{
    uint16_t base = r->s->colormode == G3C_HEIGHT ? g3_heat((a->z + b->z) / 2) : r->s->color[e];
    return map(r, k < 1 ? g3_shade(base, k) : base);
}

static void seg(const rctx_t *r, int e, const g3_mesh_t *m, int i0, int i1, float k, bool ztest, bool thick)
{
    g3r_line(r->t, &m->p[i0], &m->p[i1], &m->n[i0], &m->n[i1], seg_color(r, e, &m->n[i0], &m->n[i1], k), ztest,
             thick);
}

/* Grid lines of a surface (every stride-th line in both directions) or the
 * polyline of a curve. Over shaded cells the lines are darker and z-tested. */
static void draw_lines(const rctx_t *r, int e, const g3_mesh_t *m)
{
    bool shaded = r->s->style == G3S_SHADE;
    float k = shaded ? 0.55f : 1.0f;
    if (m->kind == G3K_CURVE) {
        for (int i = 0; i + 1 < m->nu; i++)
            seg(r, e, m, i, i + 1, 1.0f, shaded, true);
        return;
    }
    int s = r->s->stride;
    for (int j = 0;; j = next_line(j, s, m->nv)) {
        for (int i = 0; i < m->nu - 1; i++)
            seg(r, e, m, j * m->nu + i, j * m->nu + i + 1, k, shaded, false);
        if (j >= m->nv - 1)
            break;
    }
    for (int i = 0;; i = next_line(i, s, m->nu)) {
        for (int j = 0; j < m->nv - 1; j++)
            seg(r, e, m, j * m->nu + i, (j + 1) * m->nu + i, k, shaded, false);
        if (i >= m->nu - 1)
            break;
    }
}

/* a line between two normalised points (box edges, axes) */
static void line3(const rctx_t *r, g3v_t a, g3v_t b, uint16_t c, bool ztest)
{
    g3p_t pa, pb;
    g3_project(r->s->view, &a, &pa);
    g3_project(r->s->view, &b, &pb);
    g3r_line(r->t, &pa, &pb, NULL, NULL, map(r, c), ztest, false);
}

/* The 12 edges of the box (corners differ in one coordinate) and the axes
 * through the world origin where it lies within the box's other two ranges. */
static void draw_frame(const rctx_t *r, bool ztest)
{
    const g3_scene_t *s = r->s;
    if (s->box)
        for (int c = 0; c < 8; c++)
            for (int k = 0; k < 3; k++) {
                if (c & (1 << k))
                    continue;
                int c2 = c | 1 << k;
                g3v_t a = {c & 1 ? 1.f : -1.f, c & 2 ? 1.f : -1.f, c & 4 ? 1.f : -1.f};
                g3v_t b = {c2 & 1 ? 1.f : -1.f, c2 & 2 ? 1.f : -1.f, c2 & 4 ? 1.f : -1.f};
                line3(r, a, b, C_GRAY, ztest);
            }
    if (s->axes) {
        float o[3] = {(float)s->origin[0], (float)s->origin[1], (float)s->origin[2]};
        for (int k = 0; k < 3; k++) {
            int k1 = (k + 1) % 3, k2 = (k + 2) % 3;
            if (fabsf(o[k1]) > 1 || fabsf(o[k2]) > 1)
                continue;
            float a[3], b[3];
            a[k] = -1, b[k] = 1;
            a[k1] = b[k1] = o[k1];
            a[k2] = b[k2] = o[k2];
            line3(r, (g3v_t){a[0], a[1], a[2]}, (g3v_t){b[0], b[1], b[2]}, TH_AXES, ztest);
        }
    }
}

/* ------------------------------------------------------------ contours */
/* Height of node k for the contour levels: its normalised z (3D), or the
 * normalised z of its world value (the map, which keeps nodes far outside the
 * box in z), bounded to keep the arithmetic finite. */
static float node_h(const rctx_t *r, const g3_mesh_t *m, int k)
{
    if (!r->s->contour_map)
        return m->n[k].z;
    double h = (m->w[k].z - r->zc) / r->zh;
    return h > 1e6 ? 1e6f : h < -1e6 ? -1e6f : (float)h;
}

/* node k is drawn: defined in the view box (3D), or defined at all (map) */
static bool node_ok(const rctx_t *r, const g3_mesh_t *m, int k)
{
    return r->s->contour_map ? !isnan(m->p[k].d) : !isnan(m->n[k].x);
}

typedef void (*cell_fn)(rctx_t *r, const g3_mesh_t *m, const int id[4], int n);

/* Every cell of a surface with three or four drawn corners, as draw_cells
 * walks them (the same grid stride): fn gets the drawn corners in their order
 * around the cell. */
static void each_cell(rctx_t *r, const g3_mesh_t *m, cell_fn fn)
{
    int s = r->s->stride;
    for (int j = 0; j < m->nv - 1; j = next_line(j, s, m->nv)) {
        int j2 = next_line(j, s, m->nv);
        for (int i = 0; i < m->nu - 1; i = next_line(i, s, m->nu)) {
            int i2 = next_line(i, s, m->nu);
            int id[4] = {j * m->nu + i, j * m->nu + i2, j2 * m->nu + i2, j2 * m->nu + i}, ok[4], nok = 0;
            for (int k = 0; k < 4; k++)
                if (node_ok(r, m, id[k]))
                    ok[nok++] = id[k];
            if (nok >= 3)
                fn(r, m, ok, nok);
        }
    }
}

/* The point where level L crosses the edge from node u (height hu) to node v
 * (hv): interpolated linearly, in normalised world space and then projected
 * (3D), or on the screen (the map). */
static void crossing(const rctx_t *r, const g3_mesh_t *m, int u, int v, float hu, float hv, float L, g3v_t *P,
                     g3p_t *S)
{
    float t = (L - hu) / (hv - hu);
    if (r->s->contour_map) {
        const g3p_t *pu = &m->p[u], *pv = &m->p[v];
        *S = (g3p_t){pu->sx + (pv->sx - pu->sx) * t, pu->sy + (pv->sy - pu->sy) * t, pu->d};
    } else {
        const g3v_t *wu = &m->n[u], *wv = &m->n[v];
        *P = (g3v_t){wu->x + (wv->x - wu->x) * t, wu->y + (wv->y - wu->y) * t, wu->z + (wv->z - wu->z) * t};
        g3_project(r->s->view, P, S);
    }
}

/* Marching squares on one cell (n = 4 corners, or 3: the triangle of a cell
 * with one undefined corner): for every level between the cell's lowest and
 * highest corner, the edges whose ends lie on either side of it (an end
 * exactly at the level counts as above) are crossed; two crossings are joined
 * by a segment; four (a saddle) are paired so that the two corners on the
 * same side as the cell's centre (the mean of its corners) stay connected. In
 * 3D a full cell is taken as the two triangles draw_cells fills, so that the
 * segments lie exactly on the drawn (z-buffered) surface even where a cell is
 * far from flat; they are z-tested over shaded surfaces and clipped to the
 * box like the grid lines. */
static void contour_cell(rctx_t *r, const g3_mesh_t *m, const int id[4], int n)
{
    if (n == 4 && !r->s->contour_map) { /* 3D: on the two triangles draw_cells fills */
        int t1[4] = {id[0], id[1], id[2], 0}, t2[4] = {id[0], id[2], id[3], 0};
        contour_cell(r, m, t1, 3);
        contour_cell(r, m, t2, 3);
        return;
    }
    float h[4], lo = INFINITY, hi = -INFINITY, sum = 0;
    for (int c = 0; c < n; c++) {
        h[c] = node_h(r, m, id[c]);
        lo = fminf(lo, h[c]), hi = fmaxf(hi, h[c]), sum += h[c];
    }
    float a = (lo + 1) * r->q, b = (hi + 1) * r->q;
    if (!(b > a) || b < 1 || a > (float)r->n) /* flat, or no level within */
        return;
    int k0 = a < 1 ? 1 : (int)ceilf(a), k1 = b > (float)r->n ? r->n : (int)floorf(b);
    bool flat = r->s->contour_map;
    for (int k = k0; k <= k1; k++) {
        float L = (float)k / r->q - 1;
        g3v_t P[4];
        g3p_t S[4];
        int nx = 0; /* crossings, in the order of their edges (edge c: corner c to c + 1) */
        for (int c = 0; c < n; c++) {
            int d = (c + 1) % n;
            if ((h[c] >= L) == (h[d] >= L))
                continue;
            crossing(r, m, id[c], id[d], h[c], h[d], L, &P[nx], &S[nx]);
            nx++;
        }
        int pair[4] = {0, 1, 2, 3};
        if (nx == 4 && (sum / 4 >= L) != (h[0] >= L)) /* saddle, corners 0 and 2 apart: cut them off */
            pair[0] = 3, pair[1] = 0, pair[2] = 1, pair[3] = 2;
        for (int c = 0; c + 1 < nx; c += 2) {
            int p0 = pair[c], p1 = pair[c + 1];
            g3r_line(r->t, &S[p0], &S[p1], flat ? NULL : &P[p0], flat ? NULL : &P[p1], r->lc[k], r->ztest, false);
            r->segs++;
        }
    }
}

/* Colours of the contour lines of entry e (r->lc[1..levels]): the colour map
 * at the level (colour by height) or the entry's colour; darkened over shaded
 * surfaces and filled bands, plain in the wireframe style. */
static void contour_colors(rctx_t *r, int e)
{
    const g3_scene_t *s = r->s;
    bool filled = s->style == G3S_SHADE;
    for (int k = 1; k <= r->n; k++) {
        float L = (float)k / r->q - 1;
        uint16_t base = s->colormode == G3C_HEIGHT || (s->contour_map && filled) ? g3_heat(L) : s->color[e];
        r->lc[k] = map(r, filled ? g3_shade(base, s->contour_map ? 0.45f : 0.4f) : base);
    }
}

static void draw_contours(rctx_t *r, int e, const g3_mesh_t *m)
{
    contour_colors(r, e);
    each_cell(r, m, contour_cell);
}

/* ------------------------------------------------------------ contour map */
/* A filled cell of the map, every pixel in the colour of its height's band:
 * a full cell as four triangles around its centre (the mean of its corners,
 * as contour_cell decides its saddles), else its one triangle. */
static void band_cell(rctx_t *r, const g3_mesh_t *m, const int id[4], int n)
{
    float h[4];
    for (int c = 0; c < n; c++)
        h[c] = node_h(r, m, id[c]);
    if (n == 3) {
        bands_t bd = {{h[0], h[1], h[2]}, r->q, r->n, r->band};
        tri(r->t, &m->p[id[0]], &m->p[id[1]], &m->p[id[2]], NULL, NULL, NULL, 0, &bd);
        return;
    }
    g3p_t ctr = {0, 0, m->p[id[0]].d};
    for (int c = 0; c < 4; c++)
        ctr.sx += m->p[id[c]].sx / 4, ctr.sy += m->p[id[c]].sy / 4;
    float hc = (h[0] + h[1] + h[2] + h[3]) / 4;
    for (int c = 0; c < 4; c++) {
        int d = (c + 1) % 4;
        bands_t bd = {{hc, h[c], h[d]}, r->q, r->n, r->band};
        tri(r->t, &ctr, &m->p[id[c]], &m->p[id[d]], NULL, NULL, NULL, 0, &bd);
    }
}

/* The map's projection of a z = f(x,y) mesh: every defined node at its world
 * (x, y) in the plane z = 0 (the view is the top view, where that projection
 * is orthographic); undefined nodes get d = NAN. */
static void project_map(const rctx_t *r, g3_mesh_t *m)
{
    const g3_box_t *b = &r->s->win;
    double cx = (b->lo[0] + b->hi[0]) / 2, hx = (b->hi[0] - b->lo[0]) / 2;
    double cy = (b->lo[1] + b->hi[1]) / 2, hy = (b->hi[1] - b->lo[1]) / 2;
    for (int k = 0; k < m->nu * m->nv; k++) {
        const g3w_t *w = &m->w[k];
        g3v_t q = {isnan(w->z) ? NAN : (float)((w->x - cx) / hx), (float)((w->y - cy) / hy), 0};
        g3_project(r->s->view, &q, &m->p[k]);
    }
}

/* The contour map: the z = f(x,y) surfaces seen from above, flat: in the
 * shaded style filled bands in the height colours between the levels, then the
 * axes, the level curves and the frame (the box's outline in the x-y plane).
 * Entries are painted in order without depth (a later one covers an earlier
 * one); parametric surfaces and curves are not drawn. */
static void render_map(rctx_t *r)
{
    const g3_scene_t *s = r->s;
    const g3_target_t *t0 = r->t;
    g3_target_t flat = *t0;
    flat.zb = NULL;
    r->t = &flat;
    for (int b = 0; b <= r->n; b++)
        r->band[b] = map(r, g3_heat(((float)b + 0.5f) / r->q - 1));
    for (int e = 0; e < G3_NENT; e++)
        if (s->mesh[e] && s->mesh[e]->kind == G3K_FUNC)
            project_map(r, s->mesh[e]);
    if (s->style == G3S_SHADE)
        for (int e = 0; e < G3_NENT; e++)
            if (s->mesh[e] && s->mesh[e]->kind == G3K_FUNC)
                each_cell(r, s->mesh[e], band_cell);
    float o[2] = {(float)s->origin[0], (float)s->origin[1]};
    if (s->axes) {
        if (fabsf(o[1]) <= 1)
            line3(r, (g3v_t){-1, o[1], 0}, (g3v_t){1, o[1], 0}, TH_AXES, false);
        if (fabsf(o[0]) <= 1)
            line3(r, (g3v_t){o[0], -1, 0}, (g3v_t){o[0], 1, 0}, TH_AXES, false);
    }
    for (int e = 0; e < G3_NENT; e++)
        if (s->mesh[e] && s->mesh[e]->kind == G3K_FUNC)
            draw_contours(r, e, s->mesh[e]);
    if (s->box)
        for (int k = 0; k < 4; k++) {
            static const int8_t c[5][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}, {-1, -1}};
            line3(r, (g3v_t){c[k][0], c[k][1], 0}, (g3v_t){c[k + 1][0], c[k + 1][1], 0}, C_GRAY, false);
        }
    r->t = t0;
}

/* Render: clear, project every node of every drawn mesh with the current view
 * (the cached normalised values; nothing is evaluated), then in the shaded
 * style all cells first (they fill the z-buffer), then the z-tested mesh
 * lines, curves, box and axes; in the wireframe style the box and axes first
 * and the lines over them, without depth. */
int g3_render(const g3_target_t *t, const g3_scene_t *s)
{
    rctx_t r;
    memset(&r, 0, sizeof r);
    r.t = t, r.s = s;
    int nlev = s->levels > G3_LEVELS_MAX ? G3_LEVELS_MAX : s->levels;
    r.n = nlev, r.q = (float)(nlev + 1) / 2;
    r.zc = (s->win.lo[2] + s->win.hi[2]) / 2, r.zh = (s->win.hi[2] - s->win.lo[2]) / 2;
    const g3_view_t *v = s->view;
    /* light direction in eye space (upper left, in front), rotated to world: M^T L */
    static const float le[3] = {-0.36f, 0.48f, 0.8f};
    for (int k = 0; k < 3; k++) {
        r.light[k] = v->m[0][k] * le[0] + v->m[1][k] * le[1] + v->m[2][k] * le[2];
        r.toward[k] = v->m[2][k];
    }
    g3r_clear(t, map(&r, TH_BG));
    if (s->contour_map) {
        if (nlev > 0)
            render_map(&r);
        return r.segs;
    }
    for (int e = 0; e < G3_NENT; e++)
        if (s->mesh[e])
            g3_project_all(v, s->mesh[e]->n, s->mesh[e]->p, s->mesh[e]->nu * s->mesh[e]->nv);
    bool shaded = s->style == G3S_SHADE && t->zb;
    if (!shaded)
        draw_frame(&r, false);
    if (shaded)
        for (int e = 0; e < G3_NENT; e++)
            if (s->mesh[e] && s->mesh[e]->kind != G3K_CURVE)
                draw_cells(&r, e, s->mesh[e]);
    r.ztest = shaded;
    for (int e = 0; e < G3_NENT; e++) {
        if (!s->mesh[e])
            continue;
        if (nlev > 0 && s->mesh[e]->kind == G3K_FUNC) /* the level curves instead of the grid lines */
            draw_contours(&r, e, s->mesh[e]);
        else
            draw_lines(&r, e, s->mesh[e]);
    }
    if (shaded)
        draw_frame(&r, true);
    return r.segs;
}
