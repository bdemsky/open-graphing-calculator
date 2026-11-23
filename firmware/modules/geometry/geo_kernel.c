/*
 * Geometry kernel (see app/geo.h for the model): object creation and naming,
 * the forward recomputation pass, paths, intersections, transformations,
 * loci, measurements and their texts, undo, and serialization.
 *
 * Numerics. Everything is double precision in world coordinates. Degenerate
 * constructions are detected with small relative tolerances (EPS times the
 * size of the quantities involved) and make the object undefined rather than
 * producing huge or NAN coordinates: a line through two coincident points,
 * parallel lines, a circle through three collinear points, a tangent from a
 * point inside the circle, circles that do not meet. Intersections with
 * function graphs are found by sampling the difference on GEO_FN_SAMPLES
 * intervals of the visible x range (geo.rxmin..rxmax, set by the app from
 * its view) and bisecting every sign change; a sign change where the
 * difference does not become small (a pole such as 1/x) is rejected.
 *
 * Used by geo_ui.c (the Geometry app) and tests/unit/test_geo.c.
 */
#include <math.h>
#include <string.h>
#include "geo.h"
#include "geo_mod.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

#define EPS 1e-9
#define GEO_FN_SAMPLES 160
#define DEG (M_PI / 180)
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------ kinds */

static const char type_names[GT_COUNT][10] = {
    "?", "free", "on", "intersect", "midpoint", "regvertex", "line", "segment", "ray", "perp", "parallel",
    "pbisector", "abisector", "tangent", "circle", "circle3", "polygon", "reflect", "rotate", "translate",
    "dilate", "locus", "measure", "graph",
};
const char *geo_type_name(int type) { return type > 0 && type < GT_COUNT ? type_names[type] : "?"; }

/* Kind of a type, ignoring transformations (which take the kind of their
 * source, see geo_kind_of). */
static int type_kind(int t)
{
    if (t <= GT_REGVERT)
        return GK_POINT;
    if (t <= GT_TANGENT)
        return GK_LINE;
    if (t <= GT_CIRCLE3)
        return GK_CIRCLE;
    if (t == GT_POLYGON)
        return GK_POLYGON;
    if (t == GT_LOCUS)
        return GK_LOCUS;
    if (t == GT_MEASURE)
        return GK_MEASURE;
    if (t == GT_FUNC)
        return GK_FUNC;
    return GK_NONE;
}
static bool is_xform(int t) { return t >= GT_REFLECT && t <= GT_DILATE; }

int geo_kind_of(int i)
{
    if (i < 0 || i >= geo.n)
        return GK_NONE;
    /* a chain of images ends at an object of a fixed kind; parents have
     * smaller indices, so the walk terminates */
    while (is_xform(geo.obj[i].type))
        i = geo.obj[i].par[0];
    return type_kind(geo.obj[i].type);
}
bool geo_is_point(int i) { return geo_kind_of(i) == GK_POINT; }
bool geo_is_draggable(int i) { return i >= 0 && i < geo.n && (geo.obj[i].type == GT_FREE || geo.obj[i].type == GT_ON); }

static bool live(int i) { return i >= 0 && i < geo.n && !(geo.obj[i].flags & GF_DELETED); }

int geo_find(const char *name)
{
    for (int i = 0; i < geo.n; i++)
        if (live(i) && !strcmp(geo.obj[i].name, name))
            return i;
    return -1;
}

bool geo_depends_on(int i, int anc)
{
    /* forward pass from anc marking its descendants (children follow parents) */
    if (i < anc || anc < 0 || i >= geo.n)
        return false;
    uint8_t d[(GEO_MAX + 7) / 8] = {0};
    d[anc / 8] = (uint8_t)(1u << (anc % 8));
    for (int j = anc + 1; j <= i; j++)
        for (int k = 0; k < geo.obj[j].npar; k++)
            if (d[geo.obj[j].par[k] / 8] >> (geo.obj[j].par[k] % 8) & 1) {
                d[j / 8] |= (uint8_t)(1u << (j % 8));
                break;
            }
    return d[i / 8] >> (i % 8) & 1;
}

/* ------------------------------------------------------------ naming */

/* Next free name of a series: points A..Z, A1..Z1, ...; other figures a..z,
 * a1..; measurements m1, m2, ... */
static void gen_name(int kind, char *out)
{
    for (int k = 0; k < 2000; k++) {
        if (kind == GK_MEASURE)
            snprintf(out, GEO_NAME, "m%d", k + 1);
        else {
            char base = (char)((kind == GK_POINT ? 'A' : 'a') + k % 26);
            if (k < 26)
                snprintf(out, GEO_NAME, "%c", base);
            else
                snprintf(out, GEO_NAME, "%c%d", base, (k / 26) % 100);
        }
        if (geo_find(out) < 0)
            return;
    }
    out[0] = 0;
}

/* ------------------------------------------------------------ construction */

void geo_undo_reset(void) { geo.undo_top = geo.undo_count = 0; }

void geo_clear(void)
{
    geo.n = 0;
    geo_undo_reset();
}

/* Fewest parents of each type (measurements: see min_mpar), checked when
 * objects are added or loaded so that compute() never reads a missing one. */
static const uint8_t min_par[GT_COUNT] = {0, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 2, 2, 3, 3, 2, 2, 3, 2, 2, 1, 0};
static const uint8_t min_mpar[GM_COUNT] = {1, 2, 3, 1, 1, 1, 1};
static bool valid_shape(int type, int npar, int ival)
{
    if (type <= 0 || type >= GT_COUNT || npar > GEO_MAXPAR || npar < min_par[type])
        return false;
    if (type == GT_MEASURE)
        return ival >= 0 && ival < GM_COUNT && npar >= min_mpar[ival];
    if (type == GT_REGVERT)
        return ival >= 3 && ival <= GEO_MAXPAR;
    if (type == GT_FUNC)
        return ival >= 0 && ival < 6;
    return true;
}

int geo_add(int type, const int *par, int npar, int ival, int ival2, double a, double b)
{
    if (geo.n >= GEO_MAX || !valid_shape(type, npar, ival))
        return -1;
    for (int k = 0; k < npar; k++)
        if (!live(par[k]))
            return -1;
    if (is_xform(type) && geo_kind_of(par[0]) == GK_POLYGON)
        return -1; /* geo_transform() maps polygons vertex by vertex */
    int i = geo.n;
    geo_obj_t *o = &geo.obj[i];
    memset(o, 0, sizeof *o);
    o->type = (uint8_t)type;
    o->npar = (uint8_t)npar;
    for (int k = 0; k < npar; k++)
        o->par[k] = (uint8_t)par[k];
    o->ival = (int8_t)ival, o->ival2 = (int8_t)ival2;
    o->a = a, o->b = b;
    geo.n++;
    if (type == GT_FUNC)
        snprintf(o->name, GEO_NAME, "Y%d", (ival + 1) % 10);
    else {
        char nm[GEO_NAME]; /* not in place: geo_find() would see the half-made name */
        gen_name(geo_kind_of(i), nm);
        memcpy(o->name, nm, GEO_NAME);
    }
    geo_update_from(i);
    return i;
}

int geo_add_free(double x, double y) { return geo_add(GT_FREE, NULL, 0, 0, 0, x, y); }

int geo_func_obj(int fn)
{
    for (int i = 0; i < geo.n; i++)
        if (live(i) && geo.obj[i].type == GT_FUNC && geo.obj[i].ival == fn)
            return i;
    return geo_add(GT_FUNC, NULL, 0, fn, 0, 0, 0);
}

int geo_transform(int type, int src, const int *par, int npar, double a)
{
    int p[GEO_MAXPAR];
    if (!live(src) || npar > 2)
        return -1;
    if (geo_kind_of(src) == GK_POLYGON) {
        /* image polygon = polygon of the vertex images (shared vertices of
         * the source map to one image each) */
        const geo_obj_t *s = &geo.obj[src];
        int nv = s->npar, img[GEO_MAXPAR];
        for (int k = 0; k < nv; k++) {
            img[k] = geo_transform(type, geo.obj[src].par[k], par, npar, a);
            if (img[k] < 0)
                return -1;
        }
        return geo_add(GT_POLYGON, img, nv, 0, 0, 0, 0);
    }
    p[0] = src;
    for (int k = 0; k < npar; k++)
        p[k + 1] = par[k];
    int i = geo_add(type, p, npar + 1, 0, 0, a, 0);
    if (i >= 0) {
        char nm[GEO_NAME + 2];
        snprintf(nm, sizeof nm, "%s'", geo.obj[src].name);
        if (strlen(nm) < GEO_NAME && geo_find(nm) < 0)
            memcpy(geo.obj[i].name, nm, strlen(nm) + 1);
    }
    return i;
}

/* Mark i and everything after it that depends on a marked object (one
 * forward pass suffices: children come after their parents). */
void geo_delete(int i)
{
    if (!live(i))
        return;
    geo_undo_t u;
    memset(&u, 0, sizeof u);
    u.kind = GU_DELETE;
    for (int j = i; j < geo.n; j++) {
        geo_obj_t *o = &geo.obj[j];
        if (o->flags & GF_DELETED)
            continue;
        bool del = j == i;
        for (int k = 0; k < o->npar && !del; k++)
            del = (geo.obj[o->par[k]].flags & GF_DELETED) != 0;
        if (del) {
            o->flags |= GF_DELETED;
            u.del[j / 8] |= (uint8_t)(1u << (j % 8));
        }
    }
    geo_undo_push(&u);
    geo_update();
}

void geo_delete_all(void)
{
    geo_undo_t u;
    memset(&u, 0, sizeof u);
    u.kind = GU_DELETE;
    for (int j = 0; j < geo.n; j++)
        if (!(geo.obj[j].flags & GF_DELETED)) {
            geo.obj[j].flags |= GF_DELETED;
            u.del[j / 8] |= (uint8_t)(1u << (j % 8));
        }
    geo_undo_push(&u);
    geo_update();
}

void geo_compact(void)
{
    uint8_t map[GEO_MAX];
    int m = 0;
    for (int i = 0; i < geo.n; i++) {
        if (geo.obj[i].flags & GF_DELETED)
            continue;
        map[i] = (uint8_t)m;
        geo_obj_t o = geo.obj[i];
        for (int k = 0; k < o.npar; k++)
            o.par[k] = map[o.par[k]]; /* parents are live (else this one would be deleted) */
        geo.obj[m++] = o;
    }
    geo.n = m;
    geo_undo_reset();
    geo_update();
}

/* ------------------------------------------------------------ helpers */

static const geo_shape_t *S(int i) { return &geo.shape[i]; }
static double dist(double x1, double y1, double x2, double y2) { return hypot(x2 - x1, y2 - y1); }
static double cross(double ax, double ay, double bx, double by) { return ax * by - ay * bx; }

/* t within the extent of a line of sub-kind sub (with tolerance) */
static bool in_extent(int sub, double t)
{
    if (sub == GL_SEGMENT)
        return t >= -EPS && t <= 1 + EPS;
    if (sub == GL_RAY)
        return t >= -EPS;
    return true;
}
static double clamp_extent(int sub, double t)
{
    if (sub != GL_LINE && t < 0)
        t = 0;
    if (sub == GL_SEGMENT && t > 1)
        t = 1;
    return t;
}

static double fn_eval(int fn, double x) { return geo_fn_eval ? geo_fn_eval(fn, x) : (double)NAN; }

/* Vertex k of polygon object p (its parents' shapes). */
static const geo_shape_t *vert(int p, int k) { return S(geo.obj[p].par[k % geo.obj[p].npar]); }

/* ------------------------------------------------------------ transformations */

/* Apply the map of transformation object o to (x, y). False if the map is
 * undefined (mirror line of zero length). */
static bool xf_point(const geo_obj_t *o, double *x, double *y)
{
    const geo_shape_t *c = S(o->par[1]);
    double px = *x, py = *y;
    switch (o->type) {
    case GT_REFLECT:
        if (c->kind == GK_POINT) {
            *x = 2 * c->x1 - px, *y = 2 * c->y1 - py;
        } else {
            /* X' = P + 2((X-P).u)u - (X-P), u the unit direction of the mirror */
            double dx = c->x2 - c->x1, dy = c->y2 - c->y1, l = hypot(dx, dy);
            if (l < EPS)
                return false;
            dx /= l, dy /= l;
            double vx = px - c->x1, vy = py - c->y1, d = vx * dx + vy * dy;
            *x = c->x1 + 2 * d * dx - vx, *y = c->y1 + 2 * d * dy - vy;
        }
        return true;
    case GT_ROTATE: {
        double s = sin(o->a * DEG), co = cos(o->a * DEG), vx = px - c->x1, vy = py - c->y1;
        *x = c->x1 + co * vx - s * vy, *y = c->y1 + s * vx + co * vy;
        return true;
    }
    case GT_TRANSLATE: {
        const geo_shape_t *q = S(o->par[2]);
        *x = px + q->x1 - c->x1, *y = py + q->y1 - c->y1;
        return true;
    }
    case GT_DILATE:
        *x = c->x1 + o->a * (px - c->x1), *y = c->y1 + o->a * (py - c->y1);
        return true;
    }
    return false;
}

/* ------------------------------------------------------------ paths */

bool geo_path_point(int p, double t, double *x, double *y)
{
    const geo_shape_t *s = S(p);
    if (!s->ok)
        return false;
    switch (s->kind) {
    case GK_LINE:
        t = clamp_extent(s->sub, t);
        *x = s->x1 + t * (s->x2 - s->x1), *y = s->y1 + t * (s->y2 - s->y1);
        return true;
    case GK_CIRCLE:
        *x = s->x1 + s->r * cos(t), *y = s->y1 + s->r * sin(t);
        return true;
    case GK_POLYGON: {
        int n = geo.obj[p].npar;
        double f = fmod(t, n);
        if (f < 0)
            f += n;
        int k = (int)f;
        if (k >= n)
            k = n - 1;
        f -= k;
        const geo_shape_t *a = vert(p, k), *b = vert(p, k + 1);
        *x = a->x1 + f * (b->x1 - a->x1), *y = a->y1 + f * (b->y1 - a->y1);
        return true;
    }
    case GK_FUNC:
        *x = t, *y = fn_eval(geo.obj[p].ival, t);
        return isfinite(*y);
    }
    return false;
}

/* Parameter of the projection of (x, y) onto segment/line a->b (unclamped). */
static double proj_t(double ax, double ay, double bx, double by, double x, double y)
{
    double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    return l2 > 0 ? ((x - ax) * dx + (y - ay) * dy) / l2 : 0;
}

double geo_path_project(int p, double x, double y)
{
    const geo_shape_t *s = S(p);
    switch (s->kind) {
    case GK_LINE:
        return clamp_extent(s->sub, proj_t(s->x1, s->y1, s->x2, s->y2, x, y));
    case GK_CIRCLE:
        return atan2(y - s->y1, x - s->x1);
    case GK_POLYGON: {
        /* nearest point over all edges; t = edge index + fraction */
        double best = INFINITY, bt = 0;
        for (int k = 0; k < geo.obj[p].npar; k++) {
            const geo_shape_t *a = vert(p, k), *b = vert(p, k + 1);
            double f = clamp_extent(GL_SEGMENT, proj_t(a->x1, a->y1, b->x1, b->y1, x, y));
            double d = dist(x, y, a->x1 + f * (b->x1 - a->x1), a->y1 + f * (b->y1 - a->y1));
            if (d < best)
                best = d, bt = k + f;
        }
        return bt;
    }
    case GK_FUNC:
        return x;
    }
    return 0;
}

bool geo_clip(double x, double y, double dx, double dy, int sub, const double box[4], double *t0, double *t1)
{
    /* Liang-Barsky: each side of the box bounds t from one side */
    double a = sub == GL_LINE ? -(double)INFINITY : 0, b = sub == GL_SEGMENT ? 1 : (double)INFINITY;
    double p[4] = {-dx, dx, -dy, dy}, q[4] = {x - box[0], box[1] - x, y - box[2], box[3] - y};
    for (int k = 0; k < 4; k++) {
        if (p[k] == 0) {
            if (q[k] < 0)
                return false;
        } else if (p[k] < 0)
            a = fmax(a, q[k] / p[k]);
        else
            b = fmin(b, q[k] / p[k]);
    }
    *t0 = a, *t1 = b;
    return a <= b && isfinite(a) && isfinite(b);
}

/* Where the line s crosses the visible range, as parameters t0 < t1. */
static bool clip_range(const geo_shape_t *s, double *t0, double *t1)
{
    double box[4] = {geo.rxmin, geo.rxmax, geo.rymin, geo.rymax};
    return geo_clip(s->x1, s->y1, s->x2 - s->x1, s->y2 - s->y1, s->sub, box, t0, t1) && *t0 < *t1;
}

void geo_path_range(int p, double *t0, double *t1)
{
    const geo_shape_t *s = S(p);
    *t0 = 0, *t1 = 1;
    switch (s->kind) {
    case GK_LINE:
        if (!clip_range(s, t0, t1))
            *t0 = 0, *t1 = s->sub == GL_SEGMENT ? 1 : 0;
        break;
    case GK_CIRCLE: *t1 = 2 * M_PI; break;
    case GK_POLYGON: *t1 = geo.obj[p].npar; break;
    case GK_FUNC: *t0 = geo.rxmin, *t1 = geo.rxmax; break;
    }
}

/* ------------------------------------------------------------ intersections */

/* A primitive curve: an object is one primitive (line, circle, graph) or
 * several (a polygon's edges, as segments). */
typedef struct {
    uint8_t kind, sub;
    int8_t fn;
    double x1, y1, x2, y2, r;
} prim_t;

static int prims(int i, prim_t *out)
{
    const geo_shape_t *s = S(i);
    if (!s->ok)
        return 0;
    if (s->kind == GK_LINE || s->kind == GK_CIRCLE) {
        out[0] = (prim_t){s->kind, s->sub, 0, s->x1, s->y1, s->x2, s->y2, s->r};
        return 1;
    }
    if (s->kind == GK_FUNC) {
        out[0] = (prim_t){GK_FUNC, 0, geo.obj[i].ival, 0, 0, 0, 0, 0};
        return 1;
    }
    if (s->kind == GK_POLYGON) {
        int n = geo.obj[i].npar;
        for (int k = 0; k < n; k++) {
            const geo_shape_t *a = vert(i, k), *b = vert(i, k + 1);
            out[k] = (prim_t){GK_LINE, GL_SEGMENT, 0, a->x1, a->y1, b->x1, b->y1, 0};
        }
        return n;
    }
    return 0;
}

/* Collected intersection points. */
typedef struct {
    double *xs, *ys;
    int n, max;
} hits_t;
static void hit(hits_t *h, double x, double y)
{
    if (h->n < h->max)
        h->xs[h->n] = x, h->ys[h->n] = y;
    h->n++;
}

static void line_line(const prim_t *a, const prim_t *b, hits_t *h)
{
    double d1x = a->x2 - a->x1, d1y = a->y2 - a->y1, d2x = b->x2 - b->x1, d2y = b->y2 - b->y1;
    double den = cross(d1x, d1y, d2x, d2y);
    if (fabs(den) <= EPS * hypot(d1x, d1y) * hypot(d2x, d2y))
        return; /* parallel (or coincident): no single intersection */
    double ex = b->x1 - a->x1, ey = b->y1 - a->y1;
    double t = cross(ex, ey, d2x, d2y) / den, s = cross(ex, ey, d1x, d1y) / den;
    if (in_extent(a->sub, t) && in_extent(b->sub, s))
        hit(h, a->x1 + t * d1x, a->y1 + t * d1y);
}

/* Line a against circle c: |P + t d - C|^2 = r^2, a quadratic in t; both
 * roots in increasing t (a tangent gives the same point twice, so the
 * numbering of the intersections stays put while a line is dragged across). */
static void line_circle(const prim_t *a, const prim_t *c, hits_t *h)
{
    double dx = a->x2 - a->x1, dy = a->y2 - a->y1, fx = a->x1 - c->x1, fy = a->y1 - c->y1;
    double A = dx * dx + dy * dy, B = 2 * (fx * dx + fy * dy), C = fx * fx + fy * fy - c->r * c->r;
    if (A == 0)
        return;
    double disc = B * B - 4 * A * C;
    if (disc < 0) {
        if (disc < -EPS * B * B - 1e-24)
            return;
        disc = 0;
    }
    double sq = sqrt(disc), t[2] = {(-B - sq) / (2 * A), (-B + sq) / (2 * A)};
    for (int k = 0; k < 2; k++)
        if (in_extent(a->sub, t[k]))
            hit(h, a->x1 + t[k] * dx, a->y1 + t[k] * dy);
}

/* Two circles: radical line at distance a = (r1^2 - r2^2 + d^2) / 2d from
 * c1 along c1->c2, half chord h = sqrt(r1^2 - a^2); the point to the right of
 * c1->c2 first. Concentric circles do not intersect. */
static void circle_circle(const prim_t *p, const prim_t *q, hits_t *h)
{
    double dx = q->x1 - p->x1, dy = q->y1 - p->y1, d = hypot(dx, dy), tol = EPS * (p->r + q->r);
    if (d <= tol || d > p->r + q->r + tol || d < fabs(p->r - q->r) - tol)
        return;
    double a = (p->r * p->r - q->r * q->r + d * d) / (2 * d), hh = p->r * p->r - a * a;
    double hc = hh > 0 ? sqrt(hh) : 0;
    double mx = p->x1 + a * dx / d, my = p->y1 + a * dy / d, ux = -dy / d, uy = dx / d;
    hit(h, mx - hc * ux, my - hc * uy);
    hit(h, mx + hc * ux, my + hc * uy);
}

/* Graph against something: roots of g(x) = 0 in [x0, x1], increasing. */
typedef struct {
    const prim_t *f, *o; /* the graph, the other primitive */
} fx_t;
static double fx_g(const fx_t *c, double x, double *y)
{
    *y = fn_eval(c->f->fn, x);
    const prim_t *o = c->o;
    if (o->kind == GK_FUNC)
        return *y - fn_eval(o->fn, x);
    if (o->kind == GK_CIRCLE)
        return (x - o->x1) * (x - o->x1) + (*y - o->y1) * (*y - o->y1) - o->r * o->r;
    /* non-vertical line: y - (y1 + (x - x1) * slope) */
    return *y - (o->y1 + (x - o->x1) * (o->y2 - o->y1) / (o->x2 - o->x1));
}

static void func_roots(const fx_t *c, double x0, double x1, hits_t *h)
{
    if (!(x1 > x0))
        return;
    double y, ya, yb, step = (x1 - x0) / GEO_FN_SAMPLES, pa = x0, ga = fx_g(c, pa, &ya);
    for (int k = 1; k <= GEO_FN_SAMPLES; k++) {
        double pb = x0 + k * step, gb = fx_g(c, pb, &yb);
        if (ga == 0 && isfinite(ya)) {
            hit(h, pa, ya);
        } else if (isfinite(ga) && isfinite(gb) && ((ga < 0 && gb > 0) || (ga > 0 && gb < 0))) {
            /* bisection to full precision */
            double lo = pa, hi = pb, glo = ga;
            for (int it = 0; it < 64 && hi - lo > 1e-15 * fmax(1, fabs(lo)); it++) {
                double mid = 0.5 * (lo + hi), gm = fx_g(c, mid, &y);
                if ((gm < 0) == (glo < 0))
                    lo = mid, glo = gm;
                else
                    hi = mid;
            }
            double xr = 0.5 * (lo + hi), gr = fx_g(c, xr, &y);
            /* a pole changes sign too, but |g| does not get small there */
            if (isfinite(y) && fabs(gr) < 1e-6 * (1 + fabs(ga) + fabs(gb)))
                hit(h, xr, y);
        }
        pa = pb, ga = gb, ya = yb;
    }
}

static void func_any(const prim_t *f, const prim_t *o, hits_t *h)
{
    fx_t c = {f, o};
    double x0 = geo.rxmin, x1 = geo.rxmax;
    if (o->kind == GK_LINE) {
        double dx = o->x2 - o->x1;
        if (fabs(dx) <= EPS * fabs(o->y2 - o->y1)) { /* vertical line x = x1 */
            double y = fn_eval(f->fn, o->x1);
            double t = o->y2 != o->y1 ? (y - o->y1) / (o->y2 - o->y1) : 0;
            if (isfinite(y) && in_extent(o->sub, t))
                hit(h, o->x1, y);
            return;
        }
        /* x range of the line's extent */
        if (o->sub != GL_LINE) {
            double ex = o->sub == GL_SEGMENT ? o->x2 : (dx > 0 ? (double)INFINITY : -(double)INFINITY);
            x0 = fmax(x0, fmin(o->x1, ex)), x1 = fmin(x1, fmax(o->x1, ex));
        }
    } else if (o->kind == GK_CIRCLE) {
        x0 = o->x1 - o->r, x1 = o->x1 + o->r;
    }
    func_roots(&c, x0, x1, h);
}

static void prim_pair(const prim_t *a, const prim_t *b, hits_t *h)
{
    if (a->kind == GK_FUNC)
        func_any(a, b, h);
    else if (b->kind == GK_FUNC)
        func_any(b, a, h);
    else if (a->kind == GK_LINE && b->kind == GK_LINE)
        line_line(a, b, h);
    else if (a->kind == GK_LINE)
        line_circle(a, b, h);
    else if (b->kind == GK_LINE)
        line_circle(b, a, h);
    else
        circle_circle(a, b, h);
}

int geo_intersections(int a, int b, double *xs, double *ys, int max)
{
    prim_t pa[GEO_MAXPAR], pb[GEO_MAXPAR];
    if (a < 0 || b < 0 || a >= geo.n || b >= geo.n || a == b)
        return 0;
    int na = prims(a, pa), nb = prims(b, pb);
    hits_t h = {xs, ys, 0, max};
    for (int i = 0; i < na; i++)
        for (int j = 0; j < nb; j++)
            prim_pair(&pa[i], &pb[j], &h);
    return h.n < max ? h.n : max;
}

/* ------------------------------------------------------------ recomputation */

/* Set a line shape through (x1, y1) with direction (dx, dy); undefined for a
 * zero direction. */
static void set_line(geo_shape_t *s, int sub, double x1, double y1, double dx, double dy)
{
    s->kind = GK_LINE, s->sub = (uint8_t)sub;
    s->x1 = x1, s->y1 = y1, s->x2 = x1 + dx, s->y2 = y1 + dy;
    s->ok = hypot(dx, dy) > EPS * (1 + fabs(x1) + fabs(y1)) && isfinite(s->x2) && isfinite(s->y2);
}

/* Compute shape i from its parents' shapes (they are up to date). */
static void compute(int i)
{
    geo_obj_t *o = &geo.obj[i];
    geo_shape_t *s = &geo.shape[i];
    memset(s, 0, sizeof *s);
    s->kind = (uint8_t)type_kind(o->type);
    if (o->flags & GF_DELETED)
        return;
    for (int k = 0; k < o->npar; k++)
        if (!S(o->par[k])->ok)
            return;
    const geo_shape_t *p0 = o->npar > 0 ? S(o->par[0]) : NULL, *p1 = o->npar > 1 ? S(o->par[1]) : NULL,
                      *p2 = o->npar > 2 ? S(o->par[2]) : NULL;
    s->ok = true;
    switch (o->type) {
    case GT_FREE: s->x1 = o->a, s->y1 = o->b; break;
    case GT_ON: s->ok = geo_path_point(o->par[0], o->a, &s->x1, &s->y1); break;
    case GT_INTERSECT: {
        double xs[2 * GEO_MAXPAR], ys[2 * GEO_MAXPAR];
        int n = geo_intersections(o->par[0], o->par[1], xs, ys, 2 * GEO_MAXPAR);
        s->ok = o->ival >= 0 && o->ival < n;
        if (s->ok)
            s->x1 = xs[o->ival], s->y1 = ys[o->ival];
        break;
    }
    case GT_MIDPOINT: s->x1 = 0.5 * (p0->x1 + p1->x1), s->y1 = 0.5 * (p0->y1 + p1->y1); break;
    case GT_REGVERT: {
        /* regular n-gon counterclockwise on the edge P->Q: centre on the
         * left of PQ at distance (|PQ|/2) / tan(pi/n) from its midpoint;
         * vertex k = P rotated about the centre by 2 pi k / n */
        int n = o->ival;
        double dx = p1->x1 - p0->x1, dy = p1->y1 - p0->y1, h = 0.5 / tan(M_PI / n);
        double cx = 0.5 * (p0->x1 + p1->x1) - h * dy, cy = 0.5 * (p0->y1 + p1->y1) + h * dx;
        double an = 2 * M_PI * o->ival2 / n, vx = p0->x1 - cx, vy = p0->y1 - cy;
        s->x1 = cx + cos(an) * vx - sin(an) * vy, s->y1 = cy + sin(an) * vx + cos(an) * vy;
        s->ok = n >= 3 && hypot(dx, dy) > 0;
        break;
    }
    case GT_LINE:
    case GT_SEGMENT:
    case GT_RAY:
        set_line(s, o->type == GT_SEGMENT ? GL_SEGMENT : o->type == GT_RAY ? GL_RAY : GL_LINE, p0->x1, p0->y1,
                 p1->x1 - p0->x1, p1->y1 - p0->y1);
        break;
    case GT_PERP:
    case GT_PARALLEL: {
        double dx = p0->x2 - p0->x1, dy = p0->y2 - p0->y1, l = hypot(dx, dy);
        if (l > 0)
            dx /= l, dy /= l;
        if (o->type == GT_PERP)
            set_line(s, GL_LINE, p1->x1, p1->y1, -dy, dx);
        else
            set_line(s, GL_LINE, p1->x1, p1->y1, dx, dy);
        break;
    }
    case GT_PBISECT:
        set_line(s, GL_LINE, 0.5 * (p0->x1 + p1->x1), 0.5 * (p0->y1 + p1->y1), -(p1->y1 - p0->y1),
                 p1->x1 - p0->x1);
        break;
    case GT_ABISECT: {
        /* direction u + v of the unit vectors along the two arms; a straight
         * angle (u + v = 0) bisects along the normal of u */
        double ux = p0->x1 - p1->x1, uy = p0->y1 - p1->y1, vx = p2->x1 - p1->x1, vy = p2->y1 - p1->y1;
        double lu = hypot(ux, uy), lv = hypot(vx, vy);
        if (lu <= 0 || lv <= 0) {
            s->ok = false;
            break;
        }
        ux /= lu, uy /= lu, vx /= lv, vy /= lv;
        double dx = ux + vx, dy = uy + vy;
        if (hypot(dx, dy) < 1e-9)
            dx = -uy, dy = ux;
        set_line(s, GL_LINE, p1->x1, p1->y1, dx, dy);
        break;
    }
    case GT_TANGENT: {
        /* from P outside the circle the tangents touch it at angles
         * theta +- acos(r/d) seen from the centre; on the circle the one
         * tangent is perpendicular to the radius; inside there is none */
        double dx = p0->x1 - p1->x1, dy = p0->y1 - p1->y1, d = hypot(dx, dy), r = p1->r;
        if (fabs(d - r) <= 1e-9 * r) {
            set_line(s, GL_LINE, p0->x1, p0->y1, -dy, dx);
        } else if (d < r || r <= 0) {
            s->ok = false;
        } else {
            double th = atan2(dy, dx) + (o->ival ? -1 : 1) * acos(r / d);
            double tx = p1->x1 + r * cos(th), ty = p1->y1 + r * sin(th);
            set_line(s, GL_LINE, p0->x1, p0->y1, tx - p0->x1, ty - p0->y1);
        }
        break;
    }
    case GT_CIRCLE:
        s->x1 = p0->x1, s->y1 = p0->y1, s->r = dist(p0->x1, p0->y1, p1->x1, p1->y1);
        s->ok = s->r > 0;
        break;
    case GT_CIRCLE3: {
        /* circumcentre: intersection of two perpendicular bisectors,
         * closed form with D = 2 cross(B - A, C - A) */
        double bx = p1->x1 - p0->x1, by = p1->y1 - p0->y1, cx = p2->x1 - p0->x1, cy = p2->y1 - p0->y1;
        double D = 2 * cross(bx, by, cx, cy), b2 = bx * bx + by * by, c2 = cx * cx + cy * cy;
        if (fabs(D) <= EPS * (b2 + c2)) {
            s->ok = false;
            break;
        }
        double ux = (cy * b2 - by * c2) / D, uy = (bx * c2 - cx * b2) / D;
        s->x1 = p0->x1 + ux, s->y1 = p0->y1 + uy, s->r = hypot(ux, uy);
        break;
    }
    case GT_POLYGON: s->x1 = p0->x1, s->y1 = p0->y1; break;
    case GT_REFLECT:
    case GT_ROTATE:
    case GT_TRANSLATE:
    case GT_DILATE: {
        /* the image keeps the source's kind; map its defining points (a
         * circle: centre and a point on it, so the radius scales) */
        *s = *p0;
        s->ok = xf_point(o, &s->x1, &s->y1);
        if (p0->kind == GK_LINE)
            s->ok &= xf_point(o, &s->x2, &s->y2);
        if (p0->kind == GK_CIRCLE) {
            double ex = p0->x1 + p0->r, ey = p0->y1;
            xf_point(o, &ex, &ey);
            s->r = dist(s->x1, s->y1, ex, ey);
            s->ok &= s->r > 0;
        }
        if (p0->kind == GK_LINE)
            s->ok &= hypot(s->x2 - s->x1, s->y2 - s->y1) > 0;
        break;
    }
    case GT_LOCUS: s->ok = geo.obj[o->par[1]].type == GT_ON; break;
    case GT_MEASURE:
        s->r = geo_measure_value(i);
        s->ok = o->ival == GM_EQUATION || o->ival == GM_COORDS || !isnan(s->r);
        break;
    case GT_FUNC: s->ok = true; break;
    default: s->ok = false;
    }
    if (s->kind == GK_POINT)
        s->ok &= isfinite(s->x1) && isfinite(s->y1);
}

void geo_update_from(int first)
{
    for (int i = first < 0 ? 0 : first; i < geo.n; i++)
        compute(i);
}
void geo_update(void) { geo_update_from(0); }

/* ------------------------------------------------------------ moving */

void geo_move(int i, double x, double y, bool coalesce)
{
    if (!geo_is_draggable(i) || (geo.obj[i].flags & GF_DELETED))
        return;
    geo_undo_t *top = geo.undo_count ? &geo.undo[(geo.undo_top + GEO_UNDO - 1) % GEO_UNDO] : NULL;
    if (!(coalesce && top && top->kind == GU_MOVE && top->idx == i)) {
        geo_undo_t u;
        memset(&u, 0, sizeof u);
        u.kind = GU_MOVE, u.idx = (uint8_t)i, u.old = geo.obj[i];
        geo_undo_push(&u);
    }
    geo_obj_t *o = &geo.obj[i];
    if (o->type == GT_FREE)
        o->a = x, o->b = y;
    else
        o->a = geo_path_project(o->par[0], x, y);
    geo_update_from(i);
}

/* ------------------------------------------------------------ loci */

void geo_locus(int loc, int n, void (*cb)(int i, bool ok, double x, double y, void *u), void *u)
{
    const geo_obj_t *L = &geo.obj[loc];
    int P = L->par[0], D = L->par[1];
    if (!geo.shape[loc].ok || n < 2)
        return;
    int path = geo.obj[D].par[0];
    double save = geo.obj[D].a, t0, t1;
    geo_path_range(path, &t0, &t1);
    /* recompute only the objects between the driver and the traced point
     * that depend on the driver (dep[]: one forward pass) */
    uint8_t dep[(GEO_MAX + 7) / 8] = {0};
    for (int j = D; j <= P; j++) {
        bool d = j == D;
        for (int k = 0; k < geo.obj[j].npar && !d; k++) {
            int q = geo.obj[j].par[k];
            d = q >= D && (dep[q / 8] >> (q % 8) & 1);
        }
        if (d)
            dep[j / 8] |= (uint8_t)(1u << (j % 8));
    }
    for (int k = 0; k <= n; k++) {
        geo.obj[D].a = k < n ? t0 + (t1 - t0) * k / (n - 1) : save; /* last pass: restore */
        for (int j = D; j <= P; j++)
            if (dep[j / 8] >> (j % 8) & 1)
                compute(j);
        if (k < n)
            cb(k, S(P)->ok, S(P)->x1, S(P)->y1, u);
    }
}

/* ------------------------------------------------------------ measurements */

static double perimeter(int p)
{
    double s = 0;
    for (int k = 0; k < geo.obj[p].npar; k++)
        s += dist(vert(p, k)->x1, vert(p, k)->y1, vert(p, k + 1)->x1, vert(p, k + 1)->y1);
    return s;
}
/* Shoelace formula, absolute value. */
static double poly_area(int p)
{
    double s = 0;
    for (int k = 0; k < geo.obj[p].npar; k++)
        s += cross(vert(p, k)->x1, vert(p, k)->y1, vert(p, k + 1)->x1, vert(p, k + 1)->y1);
    return fabs(s) / 2;
}

double geo_measure_value(int i)
{
    const geo_obj_t *o = &geo.obj[i];
    if (o->type != GT_MEASURE)
        return NAN;
    int a = o->par[0];
    const geo_shape_t *s = S(a);
    if (!s->ok)
        return NAN;
    switch (o->ival) {
    case GM_LENGTH:
        if (s->kind == GK_LINE)
            return s->sub == GL_SEGMENT ? dist(s->x1, s->y1, s->x2, s->y2) : (double)NAN;
        if (s->kind == GK_CIRCLE)
            return 2 * M_PI * s->r;
        if (s->kind == GK_POLYGON)
            return perimeter(a);
        return NAN;
    case GM_DIST: return dist(s->x1, s->y1, S(o->par[1])->x1, S(o->par[1])->y1);
    case GM_ANGLE: {
        const geo_shape_t *b = S(o->par[1]), *c = S(o->par[2]);
        double ux = s->x1 - b->x1, uy = s->y1 - b->y1, vx = c->x1 - b->x1, vy = c->y1 - b->y1;
        if (hypot(ux, uy) == 0 || hypot(vx, vy) == 0)
            return NAN;
        return atan2(fabs(cross(ux, uy, vx, vy)), ux * vx + uy * vy) / DEG;
    }
    case GM_AREA:
        if (s->kind == GK_CIRCLE)
            return M_PI * s->r * s->r;
        return s->kind == GK_POLYGON ? poly_area(a) : (double)NAN;
    case GM_SLOPE: {
        if (s->kind != GK_LINE)
            return NAN;
        double dx = s->x2 - s->x1, dy = s->y2 - s->y1;
        return fabs(dx) <= EPS * fabs(dy) ? (double)INFINITY : dy / dx;
    }
    }
    return NAN;
}

void geo_fmt(double v, char *out, size_t n)
{
    if (isnan(v)) {
        snprintf(out, n, "undefined");
        return;
    }
    if (fabs(v) < 1e-10)
        v = 0;
    snprintf(out, n, "%.6g", v);
}

/* "y = 2x - 3", "y = -x", "y = 4", "x = 2": slope-intercept form, vertical
 * lines as x = c. */
static void line_eq(const geo_shape_t *s, char *out, size_t n)
{
    double dx = s->x2 - s->x1, dy = s->y2 - s->y1;
    char a[24], b[24];
    if (fabs(dx) <= EPS * fabs(dy)) {
        geo_fmt(s->x1, a, sizeof a);
        snprintf(out, n, "x = %s", a);
        return;
    }
    double m = dy / dx, c = s->y1 - m * s->x1;
    if (fabs(m) < 1e-10)
        m = 0;
    if (fabs(c) < 1e-10)
        c = 0;
    geo_fmt(fabs(c), b, sizeof b);
    if (m == 0) {
        geo_fmt(c, b, sizeof b);
        snprintf(out, n, "y = %s", b);
        return;
    }
    if (m == 1 || m == -1)
        snprintf(a, sizeof a, "%s", m < 0 ? "-" : "");
    else
        geo_fmt(m, a, sizeof a);
    if (c == 0)
        snprintf(out, n, "y = %sx", a);
    else
        snprintf(out, n, "y = %sx %c %s", a, c < 0 ? '-' : '+', b);
}

/* "(x - 1)² + (y + 2)² = 9" (x² when the centre coordinate is 0). */
static void circle_eq(const geo_shape_t *s, char *out, size_t n)
{
    char t[2][32], r2[20];
    double c[2] = {s->x1, s->y1};
    for (int k = 0; k < 2; k++) {
        char v[20];
        geo_fmt(fabs(c[k]), v, sizeof v);
        if (fabs(c[k]) < 1e-10)
            snprintf(t[k], sizeof t[k], "%c²", "xy"[k]);
        else
            snprintf(t[k], sizeof t[k], "(%c %c %s)²", "xy"[k], c[k] < 0 ? '+' : '-', v);
    }
    geo_fmt(s->r * s->r, r2, sizeof r2);
    snprintf(out, n, "%.31s + %.31s = %.19s", t[0], t[1], r2);
}

static void coords(const geo_shape_t *s, char *out, size_t n)
{
    char a[20], b[20];
    geo_fmt(s->x1, a, sizeof a);
    geo_fmt(s->y1, b, sizeof b);
    snprintf(out, n, "(%s, %s)", a, b);
}

void geo_value_text(int i, char *out, size_t n)
{
    out[0] = 0;
    if (i < 0 || i >= geo.n)
        return;
    const geo_obj_t *o = &geo.obj[i];
    const geo_shape_t *s = S(i);
    char v[96];
    if (!s->ok) {
        snprintf(out, n, "%s undefined", o->name);
        return;
    }
    if (o->type == GT_MEASURE) {
        const char *a = geo.obj[o->par[0]].name, *b = o->npar > 1 ? geo.obj[o->par[1]].name : "",
                   *c = o->npar > 2 ? geo.obj[o->par[2]].name : "";
        const geo_shape_t *ps = S(o->par[0]);
        geo_fmt(s->r, v, sizeof v);
        switch (o->ival) {
        case GM_LENGTH:
            if (geo.obj[o->par[0]].type == GT_SEGMENT) /* named by its end points: AB */
                snprintf(out, n, "%s%s = %s", geo.obj[geo.obj[o->par[0]].par[0]].name,
                         geo.obj[geo.obj[o->par[0]].par[1]].name, v);
            else if (ps->kind == GK_LINE)
                snprintf(out, n, "length %s = %s", a, v);
            else
                snprintf(out, n, "%s %s = %s", ps->kind == GK_CIRCLE ? "circ" : "perim", a, v);
            break;
        case GM_DIST: snprintf(out, n, "%s%s = %s", a, b, v); break;
        case GM_ANGLE: snprintf(out, n, "∠%s%s%s = %s°", a, b, c, v); break;
        case GM_AREA: snprintf(out, n, "area %s = %s", a, v); break;
        case GM_SLOPE:
            if (isinf(s->r))
                snprintf(out, n, "slope %s undefined", a);
            else
                snprintf(out, n, "slope %s = %s", a, v);
            break;
        case GM_EQUATION:
            if (ps->kind == GK_CIRCLE)
                circle_eq(ps, v, sizeof v);
            else
                line_eq(ps, v, sizeof v);
            snprintf(out, n, "%s: %s", a, v);
            break;
        case GM_COORDS:
            coords(ps, v, sizeof v);
            snprintf(out, n, "%s = %s", a, v);
            break;
        }
        return;
    }
    switch (s->kind) {
    case GK_POINT: coords(s, v, sizeof v); break;
    case GK_LINE:
        if (s->sub == GL_SEGMENT)
            geo_fmt(dist(s->x1, s->y1, s->x2, s->y2), v, sizeof v);
        else
            line_eq(s, v, sizeof v);
        break;
    case GK_CIRCLE: circle_eq(s, v, sizeof v); break;
    case GK_POLYGON: geo_fmt(poly_area(i), v, sizeof v); break;
    default: snprintf(v, sizeof v, "%s", geo_type_name(o->type));
    }
    snprintf(out, n, "%s%s%s", o->name, s->kind == GK_POINT || (s->kind == GK_LINE && s->sub == GL_SEGMENT) ||
                                              s->kind == GK_POLYGON ? " = " : ": ", v);
}

/* ------------------------------------------------------------ distance (picking) */

static double seg_dist(double ax, double ay, double bx, double by, int sub, double x, double y)
{
    double t = clamp_extent(sub, proj_t(ax, ay, bx, by, x, y));
    return dist(x, y, ax + t * (bx - ax), ay + t * (by - ay));
}

double geo_distance(int i, double x, double y)
{
    if (!live(i) || !S(i)->ok || (geo.obj[i].flags & GF_HIDDEN))
        return INFINITY;
    const geo_shape_t *s = S(i);
    switch (s->kind) {
    case GK_POINT: return dist(x, y, s->x1, s->y1);
    case GK_LINE: return seg_dist(s->x1, s->y1, s->x2, s->y2, s->sub, x, y);
    case GK_CIRCLE: return fabs(dist(x, y, s->x1, s->y1) - s->r);
    case GK_POLYGON: {
        double best = INFINITY;
        for (int k = 0; k < geo.obj[i].npar; k++)
            best = fmin(best, seg_dist(vert(i, k)->x1, vert(i, k)->y1, vert(i, k + 1)->x1, vert(i, k + 1)->y1,
                                       GL_SEGMENT, x, y));
        return best;
    }
    case GK_FUNC: {
        double fy = fn_eval(geo.obj[i].ival, x);
        return isfinite(fy) ? fabs(fy - y) : (double)INFINITY;
    }
    }
    return INFINITY;
}

/* ------------------------------------------------------------ undo */

void geo_undo_push(const geo_undo_t *u)
{
    geo.undo[geo.undo_top] = *u;
    geo.undo_top = (geo.undo_top + 1) % GEO_UNDO;
    if (geo.undo_count < GEO_UNDO)
        geo.undo_count++;
}

void geo_undo_begin_add(void)
{
    geo_undo_t u;
    memset(&u, 0, sizeof u);
    u.kind = GU_ADD, u.n = (uint8_t)geo.n;
    geo_undo_push(&u);
}

bool geo_undo(void)
{
    if (!geo.undo_count)
        return false;
    geo.undo_top = (geo.undo_top + GEO_UNDO - 1) % GEO_UNDO;
    geo.undo_count--;
    const geo_undo_t *u = &geo.undo[geo.undo_top];
    switch (u->kind) {
    case GU_ADD:
        if (u->n <= geo.n)
            geo.n = u->n;
        break;
    case GU_DELETE:
        for (int j = 0; j < geo.n; j++)
            if (u->del[j / 8] >> (j % 8) & 1)
                geo.obj[j].flags &= (uint8_t)~GF_DELETED;
        break;
    case GU_MOVE:
    case GU_EDIT:
        if (u->idx < geo.n)
            geo.obj[u->idx] = u->old;
        break;
    }
    geo_update();
    return true;
}

/* ------------------------------------------------------------ serialization */

/* Layout: u8 count; per object: u8 type, flags, color, npar, par[npar],
 * ival, ival2 (two's complement bytes), str name, f64 a, b. Deleted objects
 * are left out and the parents renumbered accordingly. */
void geo_save(pw_t *w)
{
    uint8_t map[GEO_MAX];
    int m = 0;
    for (int i = 0; i < geo.n; i++)
        if (!(geo.obj[i].flags & GF_DELETED))
            map[i] = (uint8_t)m++;
    pw_u8(w, (uint32_t)m);
    for (int i = 0; i < geo.n; i++) {
        const geo_obj_t *o = &geo.obj[i];
        if (o->flags & GF_DELETED)
            continue;
        pw_u8(w, o->type);
        pw_u8(w, o->flags);
        pw_u8(w, o->color);
        pw_u8(w, o->npar);
        for (int k = 0; k < o->npar; k++)
            pw_u8(w, map[o->par[k]]);
        pw_u8(w, (uint8_t)o->ival);
        pw_u8(w, (uint8_t)o->ival2);
        pw_str(w, o->name);
        pw_f64(w, o->a);
        pw_f64(w, o->b);
    }
}

bool geo_load(pr_t *r)
{
    geo_clear();
    unsigned m = pr_u8(r);
    if (m > GEO_MAX)
        return false;
    for (unsigned i = 0; i < m; i++) {
        geo_obj_t *o = &geo.obj[i];
        memset(o, 0, sizeof *o);
        o->type = (uint8_t)pr_u8(r);
        o->flags = (uint8_t)(pr_u8(r) & GF_HIDDEN);
        o->color = (uint8_t)pr_u8(r);
        o->npar = (uint8_t)pr_u8(r);
        bool bad = o->npar > GEO_MAXPAR;
        for (int k = 0; k < o->npar && !bad; k++) {
            o->par[k] = (uint8_t)pr_u8(r);
            bad = o->par[k] >= i;
        }
        o->ival = (int8_t)pr_u8(r);
        o->ival2 = (int8_t)pr_u8(r);
        pr_str(r, o->name, sizeof o->name);
        o->a = pr_f64(r);
        o->b = pr_f64(r);
        if (bad || GI->pr_failed(r) || !valid_shape(o->type, o->npar, o->ival)) {
            geo_clear();
            return false;
        }
        geo.n = (int)i + 1;
    }
    geo_update();
    return true;
}
