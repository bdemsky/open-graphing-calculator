/*
 * 3D graphing meshes (see g3d.h): sample an entry on its parameter grid and
 * map the values into the normalised view box.
 *
 * A surface is sampled on a regular nu x nv grid of its parameters (x, y for
 * z = f(x,y); u, v for parametric surfaces), a curve at nu equally spaced t.
 * The values are kept twice: in double (g3w_t, for the trace read-out) and
 * normalised to the box in float (g3v_t, for the renderer, which projects them
 * again for every view without evaluating anything). Points that are undefined
 * (NAN / infinite) or farther than G3_FAR half-widths outside the box are
 * marked undefined (x = NAN); the renderer skips the triangles and segments
 * that touch them and clips the rest to the box per pixel.
 *
 * The edge of a surface's domain (sqrt(1-x^2-y^2) on the unit disc): an
 * undefined node next to a defined one is moved onto the edge of the domain
 * (g3_mesh_edges), so the cells across the edge are drawn whole and the
 * surface reaches its rim instead of stopping at a jagged row of cells.
 */
#ifdef __clang__
#pragma clang fp contract(off)
#endif
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "g3d.h"

/* Normalised coordinates of w in box b: n = (w - centre) / half-range, per axis. */
void g3_normalize(const g3_box_t *b, const g3w_t *w, g3v_t *n)
{
    double c[3] = {w->x, w->y, w->z}, r[3];
    for (int k = 0; k < 3; k++) {
        double h = (b->hi[k] - b->lo[k]) / 2;
        r[k] = (c[k] - (b->lo[k] + h)) / h;
        if (!isfinite(r[k]) || fabs(r[k]) > G3_FAR) {
            n->x = n->y = n->z = NAN;
            return;
        }
    }
    n->x = (float)r[0], n->y = (float)r[1], n->z = (float)r[2];
}

/* Sample f at every node of m (layout in g3d.h: index j * nu + i). The
 * parameter of node i is u0 + i (u1 - u0) / (nu - 1) (computed from i, not
 * accumulated, so the last node is exactly u1). Also records the finite z range
 * (for zoom-fit) and the number of defined nodes. */
void g3_mesh_sample(g3_mesh_t *m, g3_eval_fn f, void *user, const g3_box_t *box)
{
    m->zlo = INFINITY, m->zhi = -INFINITY;
    m->defined = 0;
    double du = m->nu > 1 ? (m->u1 - m->u0) / (m->nu - 1) : 0;
    double dv = m->nv > 1 ? (m->v1 - m->v0) / (m->nv - 1) : 0;
    for (int j = 0; j < m->nv; j++) {
        double v = m->nv > 1 ? (j == m->nv - 1 ? m->v1 : m->v0 + j * dv) : 0;
        for (int i = 0; i < m->nu; i++) {
            double u = i == m->nu - 1 ? m->u1 : m->u0 + i * du, o[3];
            g3w_t *w = &m->w[j * m->nu + i];
            if (f(user, u, v, o) && isfinite(o[0]) && isfinite(o[1]) && isfinite(o[2])) {
                w->x = o[0], w->y = o[1], w->z = o[2];
                if (o[2] < m->zlo)
                    m->zlo = o[2];
                if (o[2] > m->zhi)
                    m->zhi = o[2];
            } else {
                w->x = w->y = w->z = NAN;
            }
            g3_normalize(box, w, &m->n[j * m->nu + i]);
            m->defined += !isnan(m->n[j * m->nu + i].x);
        }
    }
    if (m->kind != G3K_CURVE)
        g3_mesh_edges(m, f, user, box);
}

/* parameters of node (i, j), as in g3_mesh_sample */
static void node_uv(const g3_mesh_t *m, int i, int j, double *u, double *v)
{
    *u = i == m->nu - 1 ? m->u1 : m->u0 + i * ((m->u1 - m->u0) / (m->nu - 1));
    *v = j == m->nv - 1 ? m->v1 : m->v0 + j * ((m->v1 - m->v0) / (m->nv - 1));
}

#define EDGE_STEPS 12 /* bisection steps: the edge to 1/4096 of a grid cell */

/* The edge of the domain: every node where f is undefined (not one that is
 * merely far outside the box) with a defined neighbour along a grid line is
 * moved to the last defined point found by bisecting from that neighbour
 * towards it; of several neighbours, the one whose edge point is nearest to
 * the node (in grid cells) wins. Only nodes defined by f itself are used as
 * starting points (a bitmap of them is taken first). */
void g3_mesh_edges(g3_mesh_t *m, g3_eval_fn f, void *user, const g3_box_t *box)
{
    static uint8_t def[(G3_NODES_MAX + 7) / 8];
    int nu = m->nu, nv = m->nv;
    if (nu < 2 || nv < 2 || nu * nv > G3_NODES_MAX)
        return;
    memset(def, 0, (size_t)(nu * nv + 7) / 8);
    for (int k = 0; k < nu * nv; k++)
        if (!isnan(m->w[k].x))
            def[k >> 3] |= (uint8_t)(1 << (k & 7));
#define DEF(k) (def[(k) >> 3] >> ((k) & 7) & 1)
    static const int8_t dir[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int j = 0; j < nv; j++)
        for (int i = 0; i < nu; i++) {
            int k = j * nu + i;
            if (DEF(k))
                continue;
            double u1, v1, best_t = 2, bo[3] = {0, 0, 0};
            node_uv(m, i, j, &u1, &v1);
            for (int d = 0; d < 4; d++) {
                int i0 = i + dir[d][0], j0 = j + dir[d][1];
                if (i0 < 0 || i0 >= nu || j0 < 0 || j0 >= nv || !DEF(j0 * nu + i0))
                    continue;
                double u0, v0, o[3], lo = 0, hi = 1, lo_o[3];
                node_uv(m, i0, j0, &u0, &v0);
                const g3w_t *w0 = &m->w[j0 * nu + i0];
                lo_o[0] = w0->x, lo_o[1] = w0->y, lo_o[2] = w0->z;
                for (int it = 0; it < EDGE_STEPS; it++) {
                    double t = (lo + hi) / 2;
                    if (f(user, u0 + t * (u1 - u0), v0 + t * (v1 - v0), o) && isfinite(o[0]) &&
                        isfinite(o[1]) && isfinite(o[2])) {
                        lo = t;
                        memcpy(lo_o, o, sizeof o);
                    } else {
                        hi = t;
                    }
                }
                if (lo > 0 && 1 - lo < best_t) { /* the edge point nearest to this node */
                    best_t = 1 - lo;
                    memcpy(bo, lo_o, sizeof bo);
                }
            }
            if (best_t > 1)
                continue;
            g3w_t *w = &m->w[k];
            w->x = bo[0], w->y = bo[1], w->z = bo[2];
            if (bo[2] < m->zlo)
                m->zlo = bo[2];
            if (bo[2] > m->zhi)
                m->zhi = bo[2];
            g3_normalize(box, w, &m->n[k]);
            m->defined += !isnan(m->n[k].x);
        }
#undef DEF
}
