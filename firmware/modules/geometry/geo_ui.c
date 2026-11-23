/*
 * Geometry app (APPS > Geometry): interactive constructions on a square
 * world grid, built on the kernel in geo_kernel.c (model: geo.h). Part of
 * the "geometry" code module: the core shows the screen, the soft keys and
 * the menus and forwards them here (app/geo_core.c).
 *
 * Driving it:
 *   - A crosshair cursor moves with the D-pad (5 px per press). ENTER uses
 *     the current tool at the cursor; a tap on the canvas moves the cursor
 *     there and does the same. The object a tool would take is highlighted
 *     (orange) and its value is shown at the bottom left.
 *   - Tools come from the soft keys: F1 move, F2 points (point, point on
 *     object, intersection, midpoint), F3 draw (line, segment, ray, polygon,
 *     regular polygon, circles), F4 constr (perpendicular, parallel,
 *     bisectors, tangents, reflect, rotate, translate, dilate, locus), F5
 *     measure (length/distance, angle, area, slope, equation, coordinates);
 *     SHIFT F1 undo, F2 delete tool, F3 view menu (axes, grid, snap, Y=
 *     graphs, zoom, show on GRAPH, show hidden), F4 file menu (new, open,
 *     save on the microSD card), F5 hide tool. The prompt bar at the top
 *     names the tool and what to select next.
 *   - Steps that take a point accept an existing point, or create one: on
 *     an intersection of two curves near the cursor, on a path (line,
 *     circle, polygon, graph) under the cursor, else a free point (snapped
 *     to the grid when snap is on).
 *   - Move tool: ENTER on a free point or a point on a path picks it up, the
 *     D-pad moves it, ENTER (or CLEAR) drops it. With touch, a point can be
 *     dragged in any tool; dragging elsewhere pans; pinch zooms; + and -
 *     zoom about the cursor. Every drag is one undo step.
 *   - Rotations, dilations and regular polygons ask for a number in a
 *     prompt line at the bottom (any expression, evaluated by the CAS);
 *     File > Save asks for a name.
 *   - CLEAR: closes the prompt, else drops the selection, else returns to
 *     the Move tool, else leaves the app. ⌫ deletes the object under the
 *     cursor.
 *
 * Measurements are listed at the top left and recomputed on every redraw,
 * so they follow drags live. With Y= graphs on, the enabled Y= functions are
 * drawn in their colours and can carry points and be intersected (GT_FUNC).
 *
 * State: the construction in `geo` (kernel), the view in `geo_view`, the
 * tool state in `gs` (all in the module's RW block, geo.h). Saved by the
 * core's session blob "geo" through ops.save (geo_io.c); nothing of the tool
 * state is saved.
 */
#include <math.h>
#include <string.h>
#include "geo.h"
#include "theme.h"
#include "ui.h"
#include "geo_mod.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

/* ------------------------------------------------------------ tools */

/* What a tool step accepts (masks). M_PT also creates a point when none
 * is under the cursor; M_PTX takes only an existing point. */
enum {
    M_PT = 1, M_PTX = 2, M_LINE = 4, M_SEG = 8, M_CIRC = 16, M_POLY = 32, M_FUNC = 64, M_ONPT = 128,
    M_MEAS = 256,
    M_CURVE = M_LINE | M_CIRC | M_POLY | M_FUNC,
    M_OBJ = M_PTX | M_LINE | M_CIRC | M_POLY,
    M_ANY = M_OBJ | M_FUNC | M_MEAS,
};

enum {
    T_MOVE, T_POINT, T_ON, T_INTERSECT, T_MIDPOINT, T_LINE, T_SEGMENT, T_RAY, T_POLYGON, T_REGPOLY,
    T_CIRCLE, T_CIRCLE3, T_PERP, T_PARALLEL, T_PBISECT, T_ABISECT, T_TANGENT, T_REFLECT, T_ROTATE,
    T_TRANSLATE, T_DILATE, T_LOCUS, T_LENGTH, T_ANGLE, T_AREA, T_SLOPE, T_EQUATION, T_COORDS, T_DELETE,
    T_HIDE, T_COUNT
};

/* A tool: up to three steps (mask) and the number of steps; its texts are in
 * tool_text. A step whose object is not a point ends MIDPOINT, PBISECT and
 * LENGTH early (a segment instead of two points). Polygons repeat their one
 * step. */
typedef struct {
    uint8_t nsteps;
    uint16_t mask[3];
} tool_t;

static const tool_t tools[T_COUNT] = {
    [T_MOVE] = {1, {0}},
    [T_POINT] = {1, {M_PT}},
    [T_ON] = {1, {M_CURVE}},
    [T_INTERSECT] = {2, {M_CURVE, M_CURVE}},
    [T_MIDPOINT] = {2, {M_PT | M_SEG, M_PT}},
    [T_LINE] = {2, {M_PT, M_PT}},
    [T_SEGMENT] = {2, {M_PT, M_PT}},
    [T_RAY] = {2, {M_PT, M_PT}},
    [T_POLYGON] = {1, {M_PT}}, /* see tool_prompt */
    [T_REGPOLY] = {2, {M_PT, M_PT}},
    [T_CIRCLE] = {2, {M_PT, M_PT}},
    [T_CIRCLE3] = {3, {M_PT, M_PT, M_PT}},
    [T_PERP] = {2, {M_LINE, M_PT}},
    [T_PARALLEL] = {2, {M_LINE, M_PT}},
    [T_PBISECT] = {2, {M_PT | M_SEG, M_PT}},
    [T_ABISECT] = {3, {M_PT, M_PT, M_PT}},
    [T_TANGENT] = {2, {M_PT, M_CIRC}},
    [T_REFLECT] = {2, {M_OBJ, M_LINE | M_PTX}},
    [T_ROTATE] = {2, {M_OBJ, M_PT}},
    [T_TRANSLATE] = {3, {M_OBJ, M_PT, M_PT}},
    [T_DILATE] = {2, {M_OBJ, M_PT}},
    [T_LOCUS] = {2, {M_PTX, M_ONPT}},
    [T_LENGTH] = {2, {M_SEG | M_CIRC | M_POLY | M_PTX, M_PTX}},
    [T_ANGLE] = {3, {M_PTX, M_PTX, M_PTX}},
    [T_AREA] = {1, {M_POLY | M_CIRC}},
    [T_SLOPE] = {1, {M_LINE}},
    [T_EQUATION] = {1, {M_LINE | M_CIRC}},
    [T_COORDS] = {1, {M_PTX}},
    [T_DELETE] = {1, {M_ANY}},
    [T_HIDE] = {1, {M_OBJ}},
};

/* A tool's texts as one string of fields separated by NULs: its name (prompt
 * bar), its id (`geo state` tool=), then what each step asks for. String
 * literals, not a table of pointers: an image has no absolute addresses. */
#define F "\0"
static const char *tool_text(int t)
{
    switch (t) {
    case T_MOVE: return "Move" F "move" F "drag a point or ENTER on it";
    case T_POINT: return "Point" F "point" F "place a point";
    case T_ON: return "Point on object" F "on" F "select a path";
    case T_INTERSECT: return "Intersection" F "intersect" F "1st curve" F "2nd curve";
    case T_MIDPOINT: return "Midpoint" F "midpoint" F "segment or 1st point" F "2nd point";
    case T_LINE: return "Line" F "line" F "1st point" F "2nd point";
    case T_SEGMENT: return "Segment" F "segment" F "1st point" F "2nd point";
    case T_RAY: return "Ray" F "ray" F "start point" F "2nd point";
    case T_POLYGON: return "Polygon" F "polygon" F "vertices, then the 1st again";
    case T_REGPOLY: return "Regular polygon" F "regpoly" F "1st vertex" F "2nd vertex";
    case T_CIRCLE: return "Circle" F "circle" F "centre" F "point on circle";
    case T_CIRCLE3: return "Circle 3 points" F "circle3" F "1st point" F "2nd point" F "3rd point";
    case T_PERP: return "Perpendicular" F "perp" F "line" F "point";
    case T_PARALLEL: return "Parallel" F "parallel" F "line" F "point";
    case T_PBISECT: return "Perp. bisector" F "pbisector" F "segment or 1st point" F "2nd point";
    case T_ABISECT: return "Angle bisector" F "abisector" F "point on 1st side" F "vertex" F "point on 2nd side";
    case T_TANGENT: return "Tangents" F "tangent" F "point" F "circle";
    case T_REFLECT: return "Reflect" F "reflect" F "object" F "mirror line or point";
    case T_ROTATE: return "Rotate" F "rotate" F "object" F "centre";
    case T_TRANSLATE: return "Translate" F "translate" F "object" F "vector start" F "vector end";
    case T_DILATE: return "Dilate" F "dilate" F "object" F "centre";
    case T_LOCUS: return "Locus" F "locus" F "traced point" F "point on a path";
    case T_LENGTH: return "Length" F "length" F "segment, circle, polygon or point" F "2nd point";
    case T_ANGLE: return "Angle" F "angle" F "point on 1st side" F "vertex" F "point on 2nd side";
    case T_AREA: return "Area" F "area" F "polygon or circle";
    case T_SLOPE: return "Slope" F "slope" F "line";
    case T_EQUATION: return "Equation" F "equation" F "line or circle";
    case T_COORDS: return "Coordinates" F "coords" F "point";
    case T_DELETE: return "Delete" F "delete" F "object (and what depends on it)";
    case T_HIDE: return "Hide" F "hide" F "object to hide";
    }
    return "?" F "?" F "?";
}
#undef F
/* field k of tool t's texts: 0 name, 1 id, 2 + s what step s asks for */
static const char *tool_f(int t, int k)
{
    const char *s = tool_text(t);
    while (k-- > 0)
        s += strlen(s) + 1;
    return s;
}
#define tool_name(t) tool_f(t, 0)
#define tool_id(t) tool_f(t, 1)
#define tool_ask(t, s) tool_f(t, 2 + (s))

/* Prompt line kinds (a number or a name typed at the bottom). */
enum { P_NONE, P_SIDES, P_ANGLE, P_FACTOR, P_SAVE };
static const char prompt_label[][10] = {"", "Sides:", "Angle °:", "Factor:", "Save as:"};

#define STEP_PX 5 /* cursor step of the D-pad */
#define BAR_H 14  /* prompt bar at the top of the body */
#define PICK_PT 9 /* pick radius of points, px */
#define PICK_CV 6 /* of curves */
#define MAX_MEAS_ROWS 9

#define gs (GEO_RW->ui)

/* ------------------------------------------------------------ view */

static double wx(double px) { return (px - geo_view.ox) / geo_view.scale; }
static double wy(double py) { return (geo_view.oy - py) / geo_view.scale; }
static double px_of(double x) { return geo_view.ox + x * geo_view.scale; }
static double py_of(double y) { return geo_view.oy - y * geo_view.scale; }
/* rounded pixel, clamped so that far points cannot overflow int */
static int ipx(double v) { return v > 1e6 ? 1000000 : v < -1e6 ? -1000000 : (int)lround(v); }

void geo_view_reset(void)
{
    geo_view.ox = LCD_WIDTH / 2;
    geo_view.oy = BODY_Y + BODY_H / 2;
    geo_view.scale = 20;
    geo_view.opts = GO_AXES | GO_GRID | GO_SNAP;
}

void geo_view_changed(void)
{
    if (!(geo_view.scale > 1e-6 && geo_view.scale < 1e7))
        geo_view.scale = 20;
    geo.rxmin = wx(0), geo.rxmax = wx(LCD_WIDTH);
    geo.rymin = wy(UI_BOTTOM), geo.rymax = wy(BODY_Y);
    geo_update(); /* graph intersections and loci depend on the range */
}

void geo_changed(void)
{
    if (geo_view.opts & GO_ONGRAPH)
        graph_invalidate();
}

/* Grid spacing in world units: the smallest 1, 2 or 5 x 10^k that is at
 * least 16 px. */
static double grid_step(void)
{
    double s = pow(10, floor(log10(16 / geo_view.scale)));
    if (s * geo_view.scale < 16)
        s *= 2;
    if (s * geo_view.scale < 16)
        s *= 2.5;
    if (s * geo_view.scale < 16)
        s *= 2;
    return s;
}
static double snap(double v)
{
    if (!(geo_view.opts & GO_SNAP))
        return v;
    double s = grid_step(), r = round(v / s) * s;
    return fabs(r) < 1e-12 ? 0 : r;
}

/* Zoom by factor f about screen point (x, y). */
static void zoom_at(double f, double x, double y)
{
    double ns = geo_view.scale * f;
    if (ns < 1e-4 || ns > 1e6)
        return;
    geo_view.ox = x + (geo_view.ox - x) * f;
    geo_view.oy = y + (geo_view.oy - y) * f;
    geo_view.scale = ns;
    geo_view_changed();
}

/* Y= function i plotted here and usable as a path (enabled and compiled). */
static bool fn_shown(int i) { return (geo_view.opts & GO_GRAPHS) && GI->func_on(i); }
static double fn_eval_hook(int fn, double x) { return fn >= 0 && fn < NFUNC ? func_eval(fn, x) : (double)NAN; }

/* ------------------------------------------------------------ picking */

/* Masks an object satisfies. */
static unsigned obj_mask(int i)
{
    const geo_obj_t *o = &geo.obj[i];
    switch (geo_kind_of(i)) {
    case GK_POINT: return M_PT | M_PTX | (o->type == GT_ON ? M_ONPT : 0);
    case GK_LINE: return M_LINE | (geo.shape[i].sub == GL_SEGMENT ? M_SEG : 0);
    case GK_CIRCLE: return M_CIRC;
    case GK_POLYGON: return M_POLY;
    case GK_FUNC: return M_FUNC;
    }
    return 0;
}

/* Nearest object under screen point (x, y) accepted by mask: points within
 * PICK_PT px win over curves within PICK_CV px. Graphs of Y= functions that
 * have no object yet are returned as -10 - fn (see take()). -1: none. */
static int pick_at(int x, int y, unsigned mask)
{
    double X = wx(x), Y = wy(y), best = (double)INFINITY;
    int bi = -1;
    for (int pass = 0; pass < 2 && bi < 0; pass++) {
        double lim = (pass ? PICK_CV : PICK_PT) / geo_view.scale;
        for (int i = 0; i < geo.n; i++) {
            if ((geo.obj[i].flags & (GF_DELETED | GF_HIDDEN)) || !(obj_mask(i) & mask))
                continue;
            if ((geo_kind_of(i) == GK_POINT) != (pass == 0))
                continue;
            if (geo_kind_of(i) == GK_FUNC && !fn_shown(geo.obj[i].ival))
                continue;
            double d = geo_distance(i, X, Y);
            if (d <= lim && d < best)
                best = d, bi = i;
        }
        if (pass == 1 && (mask & M_FUNC))
            for (int f = 0; f < NFUNC; f++) {
                if (!fn_shown(f))
                    continue;
                double fy = func_eval(f, X), d = fabs(fy - Y);
                bool have = false;
                for (int i = 0; i < geo.n; i++)
                    have |= geo.obj[i].type == GT_FUNC && geo.obj[i].ival == f && !(geo.obj[i].flags & GF_DELETED);
                if (!have && isfinite(fy) && d <= lim && d < best)
                    best = d, bi = -10 - f;
            }
    }
    return bi;
}

/* Measurement shown in list row y (Delete tool taps on the list), -1. */
static int meas_row_at(int y)
{
    int row = (y - BODY_Y - BAR_H) / 13, k = 0;
    if (y < BODY_Y + BAR_H)
        return -1;
    for (int i = 0; i < geo.n; i++)
        if (geo.obj[i].type == GT_MEASURE && !(geo.obj[i].flags & (GF_DELETED | GF_HIDDEN))) {
            if (k == row && k < MAX_MEAS_ROWS)
                return i;
            k++;
        }
    return -1;
}

/* Open the undo step of the current action before its first object. */
static void begin_add(void)
{
    if (!gs.bracket)
        geo_undo_begin_add(), gs.bracket = true;
}

/* Turn a pick result into an object index (creating the GT_FUNC object of
 * a graph). */
static int take(int p)
{
    if (p <= -10) {
        begin_add();
        return geo_func_obj(-10 - p);
    }
    return p;
}

/* A point at the cursor for a point step: an existing point, else a new
 * intersection of two curves near the cursor (the intersection nearest the
 * cursor, within PICK_PT px), else a new point on the path under the
 * cursor, else a new free point (snapped). -1 if the construction is full. */
static int point_at_cursor(void)
{
    int p = pick_at(gs.cx, gs.cy, M_PTX);
    if (p >= 0)
        return p;
    double X = wx(gs.cx), Y = wy(gs.cy), lim = PICK_PT / geo_view.scale;
    /* the two nearest curves within PICK_PT */
    int c[2] = {-1, -1};
    double d[2] = {INFINITY, INFINITY};
    for (int i = 0; i < geo.n; i++) {
        if ((geo.obj[i].flags & (GF_DELETED | GF_HIDDEN)) || !(obj_mask(i) & M_CURVE))
            continue;
        if (geo_kind_of(i) == GK_FUNC && !fn_shown(geo.obj[i].ival))
            continue;
        double di = geo_distance(i, X, Y);
        if (di > lim)
            continue;
        if (di < d[0])
            c[1] = c[0], d[1] = d[0], c[0] = i, d[0] = di;
        else if (di < d[1])
            c[1] = i, d[1] = di;
    }
    if (c[1] >= 0) {
        double xs[2 * GEO_MAXPAR], ys[2 * GEO_MAXPAR];
        int n = geo_intersections(c[0], c[1], xs, ys, 2 * GEO_MAXPAR), bk = -1;
        double bd = lim;
        for (int k = 0; k < n; k++) {
            double dk = hypot(xs[k] - X, ys[k] - Y);
            if (dk <= bd)
                bd = dk, bk = k;
        }
        if (bk >= 0) {
            begin_add();
            return geo_add(GT_INTERSECT, c, 2, bk, 0, 0, 0);
        }
    }
    int path = take(pick_at(gs.cx, gs.cy, M_CURVE));
    begin_add();
    if (path >= 0)
        return geo_add(GT_ON, &path, 1, 0, 0, geo_path_project(path, X, Y), 0);
    return geo_add_free(snap(X), snap(Y));
}

/* ------------------------------------------------------------ tool actions */

static void message(const char *m) { snprintf(gs.msg, sizeof gs.msg, "%s", m); }

/* End the current action: selection cleared, undo step closed. */
static void reset_sel(void)
{
    gs.nsel = 0;
    gs.bracket = false;
    if (gs.prompt)
        GI->ed_close();
    gs.prompt = P_NONE;
}

void geo_ui_reset(void)
{
    reset_sel();
    gs.tool = T_MOVE;
    gs.grab = gs.drag = -1;
    gs.msg[0] = 0;
}

static void set_tool(int t)
{
    reset_sel();
    gs.grab = -1;
    gs.tool = t;
    gs.msg[0] = 0;
    /* compact now (no indices are held) when deleted slots crowd the array */
    if (geo.n > GEO_MAX - 16)
        geo_compact();
}

static void open_prompt(int kind, const char *init)
{
    gs.prompt = kind;
    GI->ed_open(init);
}

static int add2(int type, int a, int b, int ival)
{
    int p[2] = {a, b};
    begin_add();
    return geo_add(type, p, 2, ival, 0, 0, 0);
}
static int add3(int type, int a, int b, int c, int ival)
{
    int p[3] = {a, b, c};
    begin_add();
    return geo_add(type, p, 3, ival, 0, 0, 0);
}

/* All selections of the tool are made (or a number was entered: v):
 * create the result, then start over with the same tool. */
static void complete(double v)
{
    int *s = gs.sel, n = gs.nsel, r = 0;
    bool pt0 = n > 0 && geo_is_point(s[0]);
    switch (gs.tool) {
    case T_POINT: break; /* the point was made by the step */
    case T_ON: {
        begin_add();
        r = geo_add(GT_ON, s, 1, 0, 0, geo_path_project(s[0], wx(gs.cx), wy(gs.cy)), 0);
        break;
    }
    case T_INTERSECT: {
        /* the intersection nearest the cursor, or all of them if none is near */
        double xs[2 * GEO_MAXPAR], ys[2 * GEO_MAXPAR], X = wx(gs.cx), Y = wy(gs.cy);
        int m = geo_intersections(s[0], s[1], xs, ys, 2 * GEO_MAXPAR), bk = -1;
        double bd = PICK_PT / geo_view.scale;
        for (int k = 0; k < m; k++)
            if (hypot(xs[k] - X, ys[k] - Y) <= bd)
                bd = hypot(xs[k] - X, ys[k] - Y), bk = k;
        if (!m)
            message("No intersection");
        for (int k = 0; k < m; k++)
            if (bk < 0 || k == bk)
                r = add2(GT_INTERSECT, s[0], s[1], k);
        break;
    }
    case T_MIDPOINT:
        r = pt0 ? add2(GT_MIDPOINT, s[0], s[1], 0)
                : add2(GT_MIDPOINT, geo.obj[s[0]].par[0], geo.obj[s[0]].par[1], 0);
        break;
    case T_PBISECT:
        r = pt0 ? add2(GT_PBISECT, s[0], s[1], 0)
                : add2(GT_PBISECT, geo.obj[s[0]].par[0], geo.obj[s[0]].par[1], 0);
        break;
    case T_LINE: r = add2(GT_LINE, s[0], s[1], 0); break;
    case T_SEGMENT: r = add2(GT_SEGMENT, s[0], s[1], 0); break;
    case T_RAY: r = add2(GT_RAY, s[0], s[1], 0); break;
    case T_CIRCLE: r = add2(GT_CIRCLE, s[0], s[1], 0); break;
    case T_CIRCLE3: r = add3(GT_CIRCLE3, s[0], s[1], s[2], 0); break;
    case T_PERP: r = add2(GT_PERP, s[0], s[1], 0); break;
    case T_PARALLEL: r = add2(GT_PARALLEL, s[0], s[1], 0); break;
    case T_ABISECT: r = add3(GT_ABISECT, s[0], s[1], s[2], 0); break;
    case T_TANGENT: {
        r = add2(GT_TANGENT, s[0], s[1], 0);
        /* the second tangent only from outside (on the circle both coincide) */
        const geo_shape_t *c = &geo.shape[s[1]], *p = &geo.shape[s[0]];
        if (hypot(p->x1 - c->x1, p->y1 - c->y1) > c->r * (1 + 1e-9))
            r = add2(GT_TANGENT, s[0], s[1], 1);
        if (r >= 0 && !geo.shape[r].ok)
            message("Point inside the circle");
        break;
    }
    case T_POLYGON: begin_add(), r = geo_add(GT_POLYGON, s, n, 0, 0, 0, 0); break;
    case T_REGPOLY: {
        int k = (int)lround(v), vs[GEO_MAXPAR] = {s[0], s[1]};
        if (k < 3 || k > GEO_MAXPAR) {
            message("3 to 12 sides");
            r = 0;
            break;
        }
        begin_add();
        for (int j = 2; j < k && r >= 0; j++)
            r = vs[j] = geo_add(GT_REGVERT, s, 2, k, j, 0, 0);
        if (r >= 0)
            r = geo_add(GT_POLYGON, vs, k, 0, 0, 0, 0);
        break;
    }
    case T_REFLECT: begin_add(), r = geo_transform(GT_REFLECT, s[0], &s[1], 1, 0); break;
    case T_ROTATE: begin_add(), r = geo_transform(GT_ROTATE, s[0], &s[1], 1, v); break;
    case T_TRANSLATE: begin_add(), r = geo_transform(GT_TRANSLATE, s[0], &s[1], 2, 0); break;
    case T_DILATE: begin_add(), r = geo_transform(GT_DILATE, s[0], &s[1], 1, v); break;
    case T_LOCUS:
        if (!geo_depends_on(s[0], s[1])) {
            message("The point must depend on the driver");
            break;
        }
        r = add2(GT_LOCUS, s[0], s[1], 0);
        break;
    case T_LENGTH:
        if (pt0)
            r = add2(GT_MEASURE, s[0], s[1], GM_DIST);
        else
            begin_add(), r = geo_add(GT_MEASURE, s, 1, GM_LENGTH, 0, 0, 0);
        break;
    case T_ANGLE: r = add3(GT_MEASURE, s[0], s[1], s[2], GM_ANGLE); break;
    case T_AREA: begin_add(), r = geo_add(GT_MEASURE, s, 1, GM_AREA, 0, 0, 0); break;
    case T_SLOPE: begin_add(), r = geo_add(GT_MEASURE, s, 1, GM_SLOPE, 0, 0, 0); break;
    case T_EQUATION: begin_add(), r = geo_add(GT_MEASURE, s, 1, GM_EQUATION, 0, 0, 0); break;
    case T_COORDS: begin_add(), r = geo_add(GT_MEASURE, s, 1, GM_COORDS, 0, 0, 0); break;
    case T_DELETE: geo_delete(s[0]); break;
    case T_HIDE: {
        geo_undo_t u;
        memset(&u, 0, sizeof u);
        u.kind = GU_EDIT, u.idx = (uint8_t)s[0], u.old = geo.obj[s[0]];
        geo_undo_push(&u);
        geo.obj[s[0]].flags |= GF_HIDDEN;
        break;
    }
    }
    if (r < 0)
        message("Construction full");
    reset_sel();
    geo_changed();
}

/* The tool's next step with object i (already accepted by its mask). */
static void step(int i)
{
    const tool_t *t = &tools[gs.tool];
    if (i < 0) {
        message("Construction full");
        reset_sel();
        return;
    }
    if (gs.tool == T_POLYGON) {
        if (gs.nsel >= 3 && i == gs.sel[0]) {
            complete(0);
            return;
        }
        for (int k = 0; k < gs.nsel; k++)
            if (gs.sel[k] == i)
                return;
        gs.sel[gs.nsel++] = i;
        if (gs.nsel == GEO_MAXPAR)
            complete(0);
        return;
    }
    for (int k = 0; k < gs.nsel; k++)
        if (gs.sel[k] == i && gs.tool != T_ABISECT && gs.tool != T_ANGLE)
            return; /* the same object twice */
    gs.sel[gs.nsel++] = i;
    bool early = gs.nsel == 1 && !geo_is_point(i) &&
                 (gs.tool == T_MIDPOINT || gs.tool == T_PBISECT || gs.tool == T_LENGTH);
    if (gs.nsel < t->nsteps && !early)
        return;
    if (gs.tool == T_REGPOLY)
        open_prompt(P_SIDES, "5");
    else if (gs.tool == T_ROTATE)
        open_prompt(P_ANGLE, "90");
    else if (gs.tool == T_DILATE)
        open_prompt(P_FACTOR, "2");
    else
        complete(0);
}

/* ENTER / tap: use the tool at the cursor. */
static void act(void)
{
    gs.msg[0] = 0;
    if (gs.tool == T_MOVE) {
        if (gs.grab >= 0) {
            gs.grab = -1;
            return;
        }
        int p = pick_at(gs.cx, gs.cy, M_PTX);
        if (p >= 0 && geo_is_draggable(p))
            gs.grab = p, gs.drag_moved = false;
        else if (p >= 0)
            message("This point cannot be moved");
        return;
    }
    if (gs.tool == T_DELETE) {
        int m = meas_row_at(gs.cy);
        if (m >= 0 && gs.cx < LCD_WIDTH / 2) {
            geo_delete(m);
            geo_changed();
            return;
        }
    }
    unsigned mask = tools[gs.tool].mask[gs.tool == T_POLYGON ? 0 : gs.nsel];
    int p = pick_at(gs.cx, gs.cy, (mask & ~M_PT) | (mask & M_PT ? M_PTX : 0));
    if (p == -1 && (mask & M_PT))
        p = point_at_cursor();
    else if (p == -1) {
        message("Nothing to select here");
        return;
    } else
        p = take(p);
    step(p);
}

/* ------------------------------------------------------------ drawing */

/* Clip the pixel line (x0,y0)+t(x1-x0,y1-y0) of extent sub to the body
 * (with a 2 px margin); false if nothing is visible. */
static bool clip_px(double *x0, double *y0, double *x1, double *y1, int sub)
{
    static const double box[4] = {-2, LCD_WIDTH + 1, BODY_Y - 2, UI_BOTTOM + 1};
    double dx = *x1 - *x0, dy = *y1 - *y0, a, b;
    if (!geo_clip(*x0, *y0, dx, dy, sub, box, &a, &b))
        return false;
    double sx = *x0, sy = *y0;
    *x0 = sx + a * dx, *y0 = sy + a * dy, *x1 = sx + b * dx, *y1 = sy + b * dy;
    return true;
}

/* World line through (ax,ay)-(bx,by) of extent sub; thick: 2 px wide. */
static void wline(double ax, double ay, double bx, double by, int sub, uint16_t c, bool thick)
{
    double x0 = px_of(ax), y0 = py_of(ay), x1 = px_of(bx), y1 = py_of(by);
    if (!clip_px(&x0, &y0, &x1, &y1, sub))
        return;
    int a = ipx(x0), b = ipx(y0), e = ipx(x1), f = ipx(y1);
    gfx_line(a, b, e, f, c);
    if (thick) {
        bool steep = iabs(f - b) > iabs(e - a);
        gfx_line(a + !steep, b + steep, e + !steep, f + steep, c);
    }
}

/* Circle outline: midpoint algorithm for moderate radii on screen, else
 * 720 clipped chords (huge or far-away circles). */
static void wcircle(double cx, double cy, double r, uint16_t c, bool thick)
{
    double pcx = px_of(cx), pcy = py_of(cy), pr = r * geo_view.scale;
    if (pr < 0.5)
        return;
    if (pr < 3000 && fabs(pcx) < 10000 && fabs(pcy) < 10000) {
        int x0 = ipx(pcx), y0 = ipx(pcy);
        for (int w = 0; w <= (thick ? 1 : 0); w++) {
            int R = (int)lround(pr) + w, x = R, y = 0, err = 1 - R;
            while (x >= y) {
                const int o[8][2] = {{x, y}, {y, x}, {-y, x}, {-x, y}, {-x, -y}, {-y, -x}, {y, -x}, {x, -y}};
                for (int k = 0; k < 8; k++)
                    gfx_pixel(x0 + o[k][0], y0 + o[k][1], c);
                y++;
                if (err < 0)
                    err += 2 * y + 1;
                else
                    x--, err += 2 * (y - x) + 1;
            }
        }
        return;
    }
    double px = cx + r, py = cy;
    for (int k = 1; k <= 720; k++) {
        double t = k * (2 * M_PI / 720), nx = cx + r * cos(t), ny = cy + r * sin(t);
        wline(px, py, nx, ny, GL_SEGMENT, c, thick);
        px = nx, py = ny;
    }
}

/* Even-odd scanline fill of polygon p with a 50 % checkerboard stipple, so
 * what lies under it stays visible. */
static void fill_poly(int p, uint16_t c)
{
    int n = geo.obj[p].npar;
    double vx[GEO_MAXPAR], vy[GEO_MAXPAR], ymin = (double)INFINITY, ymax = -(double)INFINITY;
    for (int k = 0; k < n; k++) {
        const geo_shape_t *s = &geo.shape[geo.obj[p].par[k]];
        vx[k] = px_of(s->x1), vy[k] = py_of(s->y1);
        ymin = fmin(ymin, vy[k]), ymax = fmax(ymax, vy[k]);
    }
    int y0 = (int)fmax(BODY_Y, ceil(ymin)), y1 = (int)fmin(UI_BOTTOM - 1, floor(ymax));
    for (int y = y0; y <= y1; y++) {
        double xs[GEO_MAXPAR], yc = y + 0.5;
        int m = 0;
        for (int k = 0; k < n; k++) {
            int j = (k + 1) % n;
            if ((vy[k] <= yc) != (vy[j] <= yc))
                xs[m++] = vx[k] + (yc - vy[k]) * (vx[j] - vx[k]) / (vy[j] - vy[k]);
        }
        for (int a = 1; a < m; a++) /* insertion sort, m <= 12 */
            for (int b = a; b > 0 && xs[b - 1] > xs[b]; b--) {
                double t = xs[b];
                xs[b] = xs[b - 1], xs[b - 1] = t;
            }
        for (int k = 0; k + 1 < m; k += 2) {
            int xa = (int)fmax(0, ceil(xs[k])), xb = (int)fmin(LCD_WIDTH - 1, floor(xs[k + 1]));
            for (int x = xa + ((xa + y) & 1); x <= xb; x += 2)
                gfx_pixel(x, y, c);
        }
    }
}

/* Point mark: a disc of radius 3 (free points blue, others dark grey);
 * highlighted with a ring. */
static void draw_point(int i, bool hl)
{
    const geo_shape_t *s = &geo.shape[i];
    int x = ipx(px_of(s->x1)), y = ipx(py_of(s->y1));
    static const uint8_t hw[7] = {1, 2, 3, 3, 3, 2, 1};
    uint16_t c = geo_is_draggable(i) ? C_BLUE : C_DKGRAY;
    for (int k = 0; k < 7; k++)
        gfx_hline(x - hw[k], y - 3 + k, 2 * hw[k] + 1, c);
    if (hl) {
        int r = 5, xx = r, yy = 0, err = 1 - r;
        while (xx >= yy) {
            const int o[8][2] = {{xx, yy}, {yy, xx}, {-yy, xx}, {-xx, yy}, {-xx, -yy}, {-yy, -xx}, {yy, -xx}, {xx, -yy}};
            for (int k = 0; k < 8; k++)
                gfx_pixel(x + o[k][0], y + o[k][1], C_ORANGE);
            yy++;
            if (err < 0)
                err += 2 * yy + 1;
            else
                xx--, err += 2 * (yy - xx) + 1;
        }
    }
    gfx_text(&font_small, x + 5, y - 14, geo.obj[i].name, TH_FG, GFX_TRANSPARENT);
}

/* Locus drawing: consecutive defined samples are joined. */
typedef struct {
    bool have;
    double x, y;
    uint16_t c;
} locus_pen_t;
static void locus_cb(int i, bool ok, double x, double y, void *u)
{
    locus_pen_t *p = u;
    if (ok && p->have)
        wline(p->x, p->y, x, y, GL_SEGMENT, p->c, false);
    p->have = ok, p->x = x, p->y = y;
}

static void draw_axes_grid(void)
{
    double st = grid_step();
    if (geo_view.opts & GO_GRID) {
        for (double x = ceil(wx(0) / st) * st; x <= wx(LCD_WIDTH); x += st)
            gfx_vline(ipx(px_of(x)), BODY_Y, BODY_H, TH_GRID);
        for (double y = ceil(wy(UI_BOTTOM) / st) * st; y <= wy(BODY_Y); y += st)
            gfx_hline(0, ipx(py_of(y)), LCD_WIDTH, TH_GRID);
    }
    if (geo_view.opts & GO_AXES) {
        int ax = ipx(px_of(0)), ay = ipx(py_of(0));
        gfx_hline(0, ay, LCD_WIDTH, TH_AXES);
        gfx_vline(ax, BODY_Y, BODY_H, TH_AXES);
        for (double x = ceil(wx(0) / st) * st; x <= wx(LCD_WIDTH); x += st)
            gfx_vline(ipx(px_of(x)), ay - 2, 5, TH_AXES);
        for (double y = ceil(wy(UI_BOTTOM) / st) * st; y <= wy(BODY_Y); y += st)
            gfx_hline(ax - 2, ipx(py_of(y)), 5, TH_AXES);
    }
}

/* Graph of Y(f+1), one sample per pixel column, joined unless the jump is
 * taller than the body (asymptotes); dy = 1 draws it again one pixel lower
 * (the highlight). */
static void draw_fn(int f, uint16_t c, int dy)
{
    double prev = NAN;
    for (int x = 0; x < LCD_WIDTH; x++) {
        double y = py_of(func_eval(f, wx(x)));
        if (isfinite(y) && isfinite(prev) && fabs(y - prev) < BODY_H)
            gfx_line(x - 1, ipx(fmax(-1e4, fmin(1e4, prev))) + dy, x, ipx(fmax(-1e4, fmin(1e4, y))) + dy, c);
        prev = y;
    }
}
static void draw_graphs(void)
{
    for (int f = 0; f < NFUNC; f++)
        if (fn_shown(f))
            draw_fn(f, GI->func_color(f), 0);
}

static bool selected(int i)
{
    for (int k = 0; k < gs.nsel; k++)
        if (gs.sel[k] == i)
            return true;
    return false;
}

/* The object the tool would take at the cursor (highlighted), -1. */
static int hover(void)
{
    if (gs.prompt)
        return -1;
    if (gs.grab >= 0)
        return gs.grab;
    unsigned mask = gs.tool == T_MOVE ? M_PTX : tools[gs.tool].mask[gs.tool == T_POLYGON ? 0 : gs.nsel];
    int p = pick_at(gs.cx, gs.cy, (mask & ~M_PT) | (mask & M_PT ? M_PTX | M_CURVE : 0));
    return p;
}

static void draw_objects(int hv)
{
    /* figures first (polygon fills at the bottom), points on top */
    for (int pass = 0; pass < 3; pass++)
        for (int i = 0; i < geo.n; i++) {
            const geo_obj_t *o = &geo.obj[i];
            const geo_shape_t *s = &geo.shape[i];
            if ((o->flags & (GF_DELETED | GF_HIDDEN)) || !s->ok)
                continue;
            bool hl = i == hv || selected(i);
            int k = s->kind;
            if (pass == 0 && k == GK_POLYGON)
                fill_poly(i, RGB565(250, 210, 160));
            if (pass == 1) {
                char lb[GEO_NAME];
                snprintf(lb, sizeof lb, "%s", o->name);
                if (k == GK_LINE) {
                    uint16_t c = hl ? C_ORANGE : TH_FG;
                    wline(s->x1, s->y1, s->x2, s->y2, s->sub, c, hl);
                    /* label near the middle of the visible part */
                    double x0 = px_of(s->x1), y0 = py_of(s->y1), x1 = px_of(s->x2), y1 = py_of(s->y2);
                    if (clip_px(&x0, &y0, &x1, &y1, s->sub))
                        gfx_text(&font_small, ipx((x0 + x1) / 2) + 4, ipx((y0 + y1) / 2) - 14, lb, TH_DIM,
                                 GFX_TRANSPARENT);
                } else if (k == GK_CIRCLE) {
                    wcircle(s->x1, s->y1, s->r, hl ? C_ORANGE : C_NAVY, hl);
                    gfx_text(&font_small, ipx(px_of(s->x1 + s->r * 0.7071)) + 2,
                             ipx(py_of(s->y1 + s->r * 0.7071)) - 13, lb, TH_DIM, GFX_TRANSPARENT);
                } else if (k == GK_POLYGON) {
                    double cx = 0, cy = 0;
                    for (int v = 0; v < o->npar; v++) {
                        const geo_shape_t *a = &geo.shape[o->par[v]], *b = &geo.shape[o->par[(v + 1) % o->npar]];
                        wline(a->x1, a->y1, b->x1, b->y1, GL_SEGMENT, hl ? C_ORANGE : C_BROWN, hl);
                        cx += a->x1 / o->npar, cy += a->y1 / o->npar;
                    }
                    gfx_text(&font_small, ipx(px_of(cx)) - 3, ipx(py_of(cy)) - 6, lb, TH_DIM, GFX_TRANSPARENT);
                } else if (k == GK_LOCUS) {
                    locus_pen_t pen = {false, 0, 0, hl ? C_ORANGE : C_MAGENTA};
                    geo_locus(i, 160, locus_cb, &pen);
                } else if (k == GK_FUNC && hl && fn_shown(o->ival)) {
                    draw_fn(o->ival, C_ORANGE, 1);
                }
            }
            if (pass == 2 && k == GK_POINT)
                draw_point(i, hl);
        }
}

/* Prompt bar text of the current tool (no message, no grab, no prompt):
 * "Tool: what next". Tools with several steps add the step and the objects
 * taken so far, "Angle: vertex (step 2/3, picked B)", so a half-done
 * construction is visible; the polygon names the vertex that closes it. */
static void tool_prompt(char *b, size_t n)
{
    const tool_t *t = &tools[gs.tool];
    if (gs.tool == T_POLYGON) {
        if (!gs.nsel)
            snprintf(b, n, "%s: 1st vertex (then the others, then it again)", tool_name(gs.tool));
        else if (gs.nsel < 3)
            snprintf(b, n, "%s: next vertex (%d so far)", tool_name(gs.tool), gs.nsel);
        else
            snprintf(b, n, "%s: next vertex, or %s again to close", tool_name(gs.tool), geo.obj[gs.sel[0]].name);
        return;
    }
    size_t o = (size_t)snprintf(b, n, "%s: %s", tool_name(gs.tool), tool_ask(gs.tool, gs.nsel));
    if (t->nsteps < 2 || o >= n)
        return;
    o += (size_t)snprintf(b + o, n - o, " (step %d/%d", gs.nsel + 1, t->nsteps);
    for (int k = 0; k < gs.nsel && o < n; k++)
        o += (size_t)snprintf(b + o, n - o, "%s%s", k ? ", " : ", picked ", geo.obj[gs.sel[k]].name);
    if (o < n)
        snprintf(b + o, n - o, ")");
}

/* Body: canvas, prompt bar, measurement list, cursor, info line, prompt. */
static void geo_draw(void)
{
    GI->cursor_hide();
    gfx_reset_clip();
    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, TH_BG);
    gfx_set_clip(0, BODY_Y, LCD_WIDTH, BODY_H);
    draw_axes_grid();
    draw_graphs();
    int hv = hover();
    draw_objects(hv < 0 && hv > -10 ? -1 : hv);
    if (hv <= -10) /* a graph without object yet */
        draw_fn(-10 - hv, C_ORANGE, 1);
    /* cursor: red cross with a 2 px gap */
    gfx_hline(gs.cx - 7, gs.cy, 5, C_RED), gfx_hline(gs.cx + 3, gs.cy, 5, C_RED);
    gfx_vline(gs.cx, gs.cy - 7, 5, C_RED), gfx_vline(gs.cx, gs.cy + 3, 5, C_RED);
    /* prompt bar */
    char b[96];
    if (gs.msg[0])
        snprintf(b, sizeof b, "%s", gs.msg);
    else if (gs.grab >= 0)
        snprintf(b, sizeof b, "Moving %s: arrows, ENTER drops", geo.obj[gs.grab].name);
    else if (gs.prompt)
        snprintf(b, sizeof b, "%s: type a value, ENTER", tool_name(gs.tool));
    else
        tool_prompt(b, sizeof b);
    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BAR_H, TH_PANEL);
    gfx_text(&font_small, 3, BODY_Y + 1, b, gs.msg[0] ? TH_ERR : TH_FG, GFX_TRANSPARENT);
    /* measurements, top left */
    int y = BODY_Y + BAR_H, rows = 0;
    for (int i = 0; i < geo.n; i++) {
        if (geo.obj[i].type != GT_MEASURE || (geo.obj[i].flags & (GF_DELETED | GF_HIDDEN)))
            continue;
        if (rows++ >= MAX_MEAS_ROWS)
            continue;
        geo_value_text(i, b, sizeof b);
        uint16_t fg = i == hv ? C_ORANGE : TH_FG;
        gfx_fill_rect(0, y, gfx_text_width(&font_small, b) + 4, 13, TH_BG);
        gfx_text(&font_small, 2, y, b, fg, GFX_TRANSPARENT);
        y += 13;
    }
    if (rows > MAX_MEAS_ROWS) {
        snprintf(b, sizeof b, "+%d more", rows - MAX_MEAS_ROWS);
        gfx_text(&font_small, 2, y, b, TH_DIM, TH_BG);
    }
    gfx_reset_clip();
    if (gs.prompt) {
        int py = UI_BOTTOM - CH;
        gfx_fill_rect(0, py - 1, LCD_WIDTH, CH + 1, TH_PANEL);
        int lw = gfx_text(&font_medium, 3, py + 3, prompt_label[gs.prompt], TH_FG, GFX_TRANSPARENT) + 8;
        GI->ed_draw(lw, py, (LCD_WIDTH - lw) / CW);
        return;
    }
    /* info line: value of the highlighted object, cursor coordinates */
    char c[48], xs[20], ys[20];
    geo_fmt(gs.grab >= 0 ? geo.shape[gs.grab].x1 : snap(wx(gs.cx)), xs, sizeof xs);
    geo_fmt(gs.grab >= 0 ? geo.shape[gs.grab].y1 : snap(wy(gs.cy)), ys, sizeof ys);
    snprintf(c, sizeof c, "(%s, %s)", xs, ys);
    int cw = gfx_text_width(&font_small, c);
    gfx_text(&font_small, LCD_WIDTH - cw - 2, UI_BOTTOM - 13, c, TH_DIM, TH_BG);
    if (hv >= 0 && geo.obj[hv].type != GT_MEASURE) {
        geo_value_text(hv, b, sizeof b);
        gfx_text(&font_small, 2, UI_BOTTOM - 13, b, TH_FG, TH_BG);
    }
}

/* ------------------------------------------------------------ menus */
/* The menus are the core's (app/geo_core.c, GEO_MENU_*); their items come
 * from here: label, help topic and action. */

/* Tools offered by the four tool menus, in order (0 ends a list). */
static const uint8_t menu_tools[4][11] = {
    {T_POINT, T_ON, T_INTERSECT, T_MIDPOINT},
    {T_LINE, T_SEGMENT, T_RAY, T_POLYGON, T_REGPOLY, T_CIRCLE, T_CIRCLE3},
    {T_PERP, T_PARALLEL, T_PBISECT, T_ABISECT, T_TANGENT, T_REFLECT, T_ROTATE, T_TRANSLATE, T_DILATE, T_LOCUS},
    {T_LENGTH, T_ANGLE, T_AREA, T_SLOPE, T_EQUATION, T_COORDS},
};
/* menu labels where they differ from the tool's name */
static const char *tool_label(int t)
{
    switch (t) {
    case T_CIRCLE: return "Circle (centre, point)";
    case T_CIRCLE3: return "Circle (3 points)";
    case T_PERP: return "Perpendicular line";
    case T_PARALLEL: return "Parallel line";
    case T_PBISECT: return "Perpendicular bisector";
    case T_LENGTH: return "Length / distance";
    }
    return tool_name(t);
}
static const char *tool_help(int t)
{
    if (t >= T_REFLECT && t <= T_LOCUS)
        return "geometry-transform";
    return t >= T_LENGTH ? "geometry-measure" : "geometry-tools";
}

/* VIEW menu: toggles show their state in the label. */
enum { V_AXES = 1, V_GRID, V_SNAP, V_GRAPHS, V_ONGRAPH, V_ZIN, V_ZOUT, V_RESET, V_SHOW };
static const char view_names[9][20] = {"Axes", "Grid", "Snap to grid", "Y= graphs", "Show on GRAPH",
                                       "Zoom in", "Zoom out", "Standard view", "Show hidden objects"};
static const uint8_t view_bits[6] = {0, GO_AXES, GO_GRID, GO_SNAP, GO_GRAPHS, GO_ONGRAPH};

enum { F_NEW = 1, F_OPEN, F_SAVE };
static const char file_names[3][20] = {"New construction", "Open from microSD", "Save to microSD"};

static int menu_count(int menu)
{
    if (menu >= GEO_MENU_POINTS && menu <= GEO_MENU_MEASURE) {
        int n = 0;
        while (n < 11 && menu_tools[menu][n])
            n++;
        return n;
    }
    return menu == GEO_MENU_VIEW ? 9 : menu == GEO_MENU_FILE ? 3 : 0;
}

/* Item i of a menu: label and help topic into the buffers, -> its action (-1: none). */
static int menu_item(int menu, int i, char *label, size_t n, char *help, size_t hn)
{
    if (i < 0 || i >= menu_count(menu))
        return -1;
    if (menu <= GEO_MENU_MEASURE) {
        int t = menu_tools[menu][i];
        snprintf(label, n, "%s", tool_label(t));
        snprintf(help, hn, "%s", tool_help(t));
        return t;
    }
    if (menu == GEO_MENU_VIEW) {
        if (i < 5)
            snprintf(label, n, "%s: %s", view_names[i], geo_view.opts & view_bits[i + 1] ? "on" : "off");
        else
            snprintf(label, n, "%s", view_names[i]);
        snprintf(help, hn, "geometry");
        return i + 1;
    }
    snprintf(label, n, "%s", file_names[i]);
    snprintf(help, hn, "geometry-files");
    return i + 1;
}

static void view_select(int action)
{
    double cx = LCD_WIDTH / 2.0, cy = BODY_Y + BODY_H / 2.0;
    switch (action) {
    case V_ZIN: zoom_at(2, cx, cy); break;
    case V_ZOUT: zoom_at(0.5, cx, cy); break;
    case V_RESET: {
        uint8_t o = geo_view.opts;
        geo_view_reset();
        geo_view.opts = o;
        geo_view_changed();
        break;
    }
    case V_SHOW:
        for (int i = 0; i < geo.n; i++)
            geo.obj[i].flags &= (uint8_t)~GF_HIDDEN;
        geo_changed();
        break;
    default:
        if (action < V_AXES || action > V_ONGRAPH)
            break;
        geo_view.opts ^= view_bits[action];
        if (action == V_GRAPHS)
            funcs_compile();
        if (action == V_ONGRAPH)
            graph_invalidate();
        geo_view_changed();
    }
    GI->back();
}

static void file_select(int action)
{
    if (action == F_OPEN) {
        GI->files_menu();
        return;
    }
    set_tool(T_MOVE);
    if (action == F_NEW) {
        geo_delete_all();
        geo_changed();
    } else
        open_prompt(P_SAVE, "");
    GI->back();
}

static void menu_select(int menu, int action)
{
    if (menu <= GEO_MENU_MEASURE) {
        if (action > T_MOVE && action < T_COUNT)
            set_tool(action);
        GI->back();
    } else if (menu == GEO_MENU_VIEW)
        view_select(action);
    else if (menu == GEO_MENU_FILE)
        file_select(action);
}

static void sk_undo(void)
{
    reset_sel();
    gs.grab = -1;
    gs.msg[0] = 0;
    if (!geo_undo())
        message("Nothing to undo");
    geo_changed();
    geo_draw();
}

/* Soft keys: move, points, draw, constr, measure; SHIFT: undo, delete, view, file, hide. */
static void softkey(int i)
{
    switch (i) {
    case 0: set_tool(T_MOVE), geo_draw(); break;
    case 1: GI->menu_open(GEO_MENU_POINTS); break;
    case 2: GI->menu_open(GEO_MENU_DRAW); break;
    case 3: GI->menu_open(GEO_MENU_CONSTR); break;
    case 4: GI->menu_open(GEO_MENU_MEASURE); break;
    case 5: sk_undo(); break;
    case 6: set_tool(T_DELETE), geo_draw(); break;
    case 7: GI->menu_open(GEO_MENU_VIEW); break;
    case 8: GI->menu_open(GEO_MENU_FILE); break;
    case 9: set_tool(T_HIDE), geo_draw(); break;
    }
}

/* ------------------------------------------------------------ keys, touch */

/* ENTER in the prompt line: evaluate the number (or take the file name). */
static void prompt_enter(void)
{
    char name[40], err[64];
    if (gs.prompt == P_SAVE) {
        GI->ed_text(name, sizeof name);
        gs.prompt = P_NONE;
        GI->ed_close();
        int r = GI->file_save(name);
        message(r ? "Save failed (see message)" : "Saved");
        return;
    }
    double v;
    if (GI->ed_number(&v, err, sizeof err)) {
        message(err);
        return;
    }
    gs.prompt = P_NONE;
    GI->ed_close();
    complete(v);
}

/* Move the cursor (and a carried point) by (dx, dy) pixels; the cursor
 * stays in the canvas. */
static void move_cursor(int dx, int dy)
{
    if (gs.grab >= 0 && (geo_view.opts & GO_SNAP) && geo.obj[gs.grab].type == GT_FREE) {
        int gp = (int)lround(grid_step() * geo_view.scale);
        dx = dx > 0 ? gp : dx < 0 ? -gp : 0, dy = dy > 0 ? gp : dy < 0 ? -gp : 0;
        const geo_shape_t *s = &geo.shape[gs.grab];
        gs.cx = ipx(px_of(s->x1)), gs.cy = ipx(py_of(s->y1));
    }
    gs.cx += dx, gs.cy += dy;
    if (gs.cx < 0)
        gs.cx = 0;
    if (gs.cx > LCD_WIDTH - 1)
        gs.cx = LCD_WIDTH - 1;
    if (gs.cy < BODY_Y + BAR_H)
        gs.cy = BODY_Y + BAR_H;
    if (gs.cy > UI_BOTTOM - 1)
        gs.cy = UI_BOTTOM - 1;
    if (gs.grab >= 0) {
        geo_move(gs.grab, snap(wx(gs.cx)), snap(wy(gs.cy)), gs.drag_moved);
        gs.drag_moved = true;
        geo_changed();
    }
}

static void geo_key(int key, int mod)
{
    if (gs.prompt) {
        if (key == KEY_ENTER)
            prompt_enter();
        else if (key == KEY_CLEAR) {
            if (GI->ed_empty()) /* a second CLEAR cancels the tool's selection */
                reset_sel();
            else
                GI->ed_clear();
        } else if (!GI->ed_key(key, mod))
            return;
        geo_draw();
        return;
    }
    switch (key) {
    case KEY_UP: move_cursor(0, -STEP_PX); break;
    case KEY_DOWN: move_cursor(0, STEP_PX); break;
    case KEY_LEFT: move_cursor(-STEP_PX, 0); break;
    case KEY_RIGHT: move_cursor(STEP_PX, 0); break;
    case KEY_ENTER: act(); break;
    case KEY_ADD: zoom_at(2, gs.cx, gs.cy); break;
    case KEY_SUB: zoom_at(0.5, gs.cx, gs.cy); break;
    case KEY_DEL: {
        int p = pick_at(gs.cx, gs.cy, M_ANY & ~M_FUNC);
        if (p >= 0) {
            reset_sel();
            geo_delete(p);
            geo_changed();
        }
        break;
    }
    case KEY_CLEAR:
        if (gs.grab >= 0)
            gs.grab = -1;
        else if (gs.nsel)
            reset_sel();
        else if (gs.msg[0])
            gs.msg[0] = 0;
        else if (gs.tool != T_MOVE)
            set_tool(T_MOVE);
        else {
            GI->home();
            return;
        }
        break;
    default: return;
    }
    if (key != KEY_ENTER && key != KEY_CLEAR)
        gs.msg[0] = 0;
    geo_draw();
}

/* Touch: tap = cursor there + ENTER; a drag that starts on a movable point
 * moves it (any tool), any other drag pans; pinch zooms about its centre. */
static void geo_touch(const geo_touch_t *ev)
{
    if (gs.prompt)
        return;
    switch (ev->type) {
    case TEV_DOWN: {
        int p = ev->y >= BODY_Y + BAR_H && ev->y < UI_BOTTOM ? pick_at(ev->x, ev->y, M_PTX) : -1;
        gs.drag = p >= 0 && geo_is_draggable(p) ? p : -2;
        gs.drag_moved = false;
        return;
    }
    case TEV_DRAG:
        if (gs.drag >= 0) {
            gs.cx = ev->x, gs.cy = ev->y;
            geo_move(gs.drag, snap(wx(ev->x)), snap(wy(ev->y)), gs.drag_moved);
            gs.drag_moved = true;
            geo_changed();
        } else if (gs.drag == -2) {
            geo_view.ox += ev->dx, geo_view.oy += ev->dy;
            geo_view_changed();
        }
        break;
    case TEV_UP:
    case TEV_CANCEL: gs.drag = -1; return;
    case TEV_TAP:
        if (ev->y0 < BODY_Y || ev->y0 >= UI_BOTTOM)
            return;
        gs.cx = ev->x0, gs.cy = ev->y0 < BODY_Y + BAR_H ? BODY_Y + BAR_H : ev->y0;
        act();
        break;
    case TEV_PINCH:
        if (gs.pinch0 == 0)
            gs.pinch0 = geo_view.scale;
        zoom_at(gs.pinch0 * ev->scale / geo_view.scale, ev->x, ev->y);
        break;
    case TEV_PINCH_END: gs.pinch0 = 0; break;
    default: return;
    }
    geo_draw();
}

static bool ui_clip_text(char *out, size_t n)
{
    out[0] = 0;
    if (gs.prompt) {
        GI->ed_text(out, n);
        return out[0] != 0;
    }
    int i = hover();
    if ((i < 0 || geo.obj[i].type != GT_MEASURE) && gs.cx < LCD_WIDTH / 2) /* the cursor on its list row */
        i = meas_row_at(gs.cy);
    if (i < 0 || geo.obj[i].type != GT_MEASURE) /* none: the newest one */
        for (i = geo.n - 1; i >= 0; i--)
            if (geo.obj[i].type == GT_MEASURE && !(geo.obj[i].flags & (GF_DELETED | GF_HIDDEN)))
                break;
    if (i < 0 || !geo.shape[i].ok)
        return false;
    if (geo.obj[i].ival == GM_EQUATION || geo.obj[i].ival == GM_COORDS) {
        /* "a: y = 2x + 1" / "A = (1, 2)": the part after the name */
        char t[96];
        geo_value_text(i, t, sizeof t);
        const char *v = strstr(t, geo.obj[i].ival == GM_EQUATION ? ": " : " = ");
        snprintf(out, n, "%s", v ? v + (geo.obj[i].ival == GM_EQUATION ? 2 : 3) : t);
    } else if (isfinite(geo.shape[i].r)) {
        snprintf(out, n, "%.12g", geo.shape[i].r);
    }
    return out[0] != 0;
}

/* Every visit: the Y= functions may have changed, so recompile them and
 * recompute (points on graphs, intersections with graphs). The tool state
 * survives menus and help (ui_returning) and other screens. */
static void geo_enter(bool returning)
{
    geo_fn_eval = fn_eval_hook;
    funcs_compile();
    if (geo_view.scale <= 0)
        geo_view_reset();
    if (gs.cx == 0 && gs.cy == 0)
        gs.cx = LCD_WIDTH / 2, gs.cy = BODY_Y + BODY_H / 2;
    if (gs.grab >= geo.n)
        gs.grab = -1;
    if (!returning)
        gs.drag = -1;
    geo_view_changed();
}

/* key=value pairs for `geo state` / `geo expect`. */
void geo_ui_state(void (*kv)(const char *, const char *, void *), void *user)
{
    char b[64], xs[24], ys[24];
    int live = 0, hidden = 0;
    for (int i = 0; i < geo.n; i++)
        live += !(geo.obj[i].flags & GF_DELETED), hidden += (geo.obj[i].flags & (GF_DELETED | GF_HIDDEN)) == GF_HIDDEN;
    kv("tool", tool_id(gs.tool), user);
    snprintf(b, sizeof b, "%d", live);
    kv("count", b, user);
    snprintf(b, sizeof b, "%d", hidden);
    kv("hidden", b, user);
    snprintf(b, sizeof b, "%d", gs.nsel);
    kv("nsel", b, user);
    geo_fmt(wx(gs.cx), xs, sizeof xs), geo_fmt(wy(gs.cy), ys, sizeof ys);
    snprintf(b, sizeof b, "(%s, %s)", xs, ys);
    kv("cursor", b, user);
    snprintf(b, sizeof b, "%d,%d", gs.cx, gs.cy);
    kv("cursor_px", b, user);
    kv("grab", gs.grab >= 0 ? geo.obj[gs.grab].name : "", user);
    int h = hover();
    kv("hover", h >= 0 ? geo.obj[h].name : "", user);
    kv("prompt", prompt_label[gs.prompt], user);
    kv("message", gs.msg, user);
    geo_fmt(geo_view.scale, b, sizeof b);
    kv("scale", b, user);
    snprintf(b, sizeof b, "%d", geo.undo_count);
    kv("undo", b, user);
}

void geo_set_cursor(double x, double y)
{
    gs.cx = ipx(px_of(x)), gs.cy = ipx(py_of(y));
    if (GI->shown())
        geo_draw();
}

void geo_ui_ops(geo_ops_t *ops)
{
    ops->enter = geo_enter;
    ops->draw = geo_draw;
    ops->key = geo_key;
    ops->touch = geo_touch;
    ops->softkey = softkey;
    ops->menu_count = menu_count;
    ops->menu_item = menu_item;
    ops->menu_select = menu_select;
    ops->clip_text = ui_clip_text;
}
