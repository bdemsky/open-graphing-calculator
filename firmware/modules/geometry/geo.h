/*
 * Geometry kernel: the construction model behind the Geometry app
 * (geo_ui.c; the "geometry" code module, app/geo_core.h) - objects, their dependency graph, the numeric
 * recomputation that keeps derived objects in step when a point is dragged,
 * intersections, transformations, loci, measurements, undo and the
 * serialized form used by the saved session and by files on the microSD card.
 * No UI and no CAS: tests/unit/test_geo.c drives it directly.
 *
 * Model. A construction is an array of at most GEO_MAX objects (geo.obj[]).
 * An object records HOW it is built (its type, the indices of the objects it
 * is built from, `par[]`, and a few numbers), never a cached position. Every
 * parent has a smaller index than its child, so the array is a topological
 * order of the dependency graph and geo_update() recomputes all positions in
 * one forward pass (geo.shape[], the "shapes": coordinates of points, the two
 * defining points of a line, centre and radius of a circle). An object whose
 * parents are undefined, or whose construction has no solution (parallel
 * lines do not intersect, the tangent from a point inside a circle), is
 * undefined (`shape.ok` false) and is not drawn; it comes back as soon as the
 * construction has a solution again.
 *
 * Only free points (GT_FREE: a = x, b = y), points on a path (GT_ON: a =
 * the path parameter) and the numbers typed for rotations, dilations and
 * regular polygons are inputs; everything else is computed. Deleting an
 * object also deletes everything that depends on it (GF_DELETED: the slot is
 * kept so that indices stay valid for undo; geo_compact() squeezes the
 * deleted slots out when the array is full or the construction is saved).
 *
 * Function graphs: GT_FUNC stands for the graph of Y1..Y6 (ival = 0..5) of
 * the Y= editor. The kernel evaluates it through geo_fn_eval, a hook the app
 * points at func_eval() (unit tests install their own), so points can be
 * put on a graph and graphs can be intersected with lines and circles.
 *
 * State: the module has no static RW data (it runs in place from the QSPI,
 * app/modx.h). Everything is in geo_rw_t, the RW block the core allocates
 * and zeroes when it binds the module; `geo`, `geo_view` and `geo_fn_eval`
 * below are names for its fields, reached through the RW pointer the core
 * keeps while it calls the module (MODX_RW). tests/unit/test_geo.c points
 * that at a block of its own.
 *
 * Image rules: no static RW data, no pointers in constant tables (tables
 * hold characters and numbers), every core function through a table
 * (geo_mod.h maps the usual names onto geo_core_t and modx_api_t).
 */
#ifndef GEO_H
#define GEO_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "geo_core.h"
#include "modx.h"
#include "session.h"

/* The image is position-independent: functions shared between the files must
 * be reached PC-relatively, not through a GOT, so every declaration is hidden. */
#if defined(MODX_IMAGE)
#pragma GCC visibility push(hidden)
#endif

#define GEO_MAX 96    /* objects in a construction (deleted slots included until compacted) */
#define GEO_MAXPAR 12 /* parents per object: polygons have up to 12 vertices */
#define GEO_UNDO 24   /* undo steps kept */
#define GEO_NAME 6    /* name buffer: up to 5 characters ("A", "B1", "m12") */

/* Object types. par[] lists the parents in the order given here. */
enum geo_type {
    GT_FREE = 1,   /* free point: a = x, b = y */
    GT_ON,         /* point on a path par0 (line, segment, ray, circle, polygon, graph): a = parameter */
    GT_INTERSECT,  /* intersection of par0 and par1; ival = which one (ordered list, geo_intersections) */
    GT_MIDPOINT,   /* midpoint of points par0, par1 */
    GT_REGVERT,    /* vertex ival2 (2..n-1) of the regular n-gon (n = ival) on the edge par0 -> par1 */
    GT_LINE,       /* line through points par0, par1 */
    GT_SEGMENT,    /* segment par0 - par1 */
    GT_RAY,        /* ray from par0 through par1 */
    GT_PERP,       /* perpendicular to line-like par0 through point par1 */
    GT_PARALLEL,   /* parallel to line-like par0 through point par1 */
    GT_PBISECT,    /* perpendicular bisector of points par0, par1 */
    GT_ABISECT,    /* bisector of the angle par0 par1 par2 (vertex par1) */
    GT_TANGENT,    /* tangent from point par0 to circle par1; ival = 0/1 (the two tangents) */
    GT_CIRCLE,     /* circle with centre par0 through point par1 */
    GT_CIRCLE3,    /* circle through points par0, par1, par2 */
    GT_POLYGON,    /* polygon with vertices par0..par[npar-1] */
    GT_REFLECT,    /* image of par0 reflected in par1 (a line-like: mirror line; a point: point reflection) */
    GT_ROTATE,     /* image of par0 rotated about point par1 by a degrees (counterclockwise) */
    GT_TRANSLATE,  /* image of par0 translated by the vector par1 -> par2 */
    GT_DILATE,     /* image of par0 dilated from point par1 by the factor a */
    GT_LOCUS,      /* locus of point par0 while point par1 (GT_ON) runs along its path */
    GT_MEASURE,    /* measurement ival (GM_*) of par0 [, par1, par2] */
    GT_FUNC,       /* graph of Y(ival+1) (drawn with the Y= functions) */
    GT_COUNT
};

/* Measurements (GT_MEASURE ival). */
enum geo_measure {
    GM_LENGTH = 0, /* segment: length; circle: circumference; polygon: perimeter */
    GM_DIST,       /* distance between points par0, par1 */
    GM_ANGLE,      /* angle par0 par1 par2 at par1, 0..180 degrees */
    GM_AREA,       /* polygon or circle */
    GM_SLOPE,      /* line-like */
    GM_EQUATION,   /* line-like or circle: equation text */
    GM_COORDS,     /* point: (x, y) */
    GM_COUNT
};

/* Shape kinds: what an object is geometrically, whatever its construction. */
enum geo_kind {
    GK_NONE = 0,
    GK_POINT,   /* x1, y1 */
    GK_LINE,    /* through (x1, y1) and (x2, y2); sub = GL_* */
    GK_CIRCLE,  /* centre (x1, y1), radius r */
    GK_POLYGON, /* vertices = the shapes of its parents (transformed polygons are polygons of transformed vertices) */
    GK_FUNC,    /* graph y = f(x) */
    GK_LOCUS,   /* sampled with geo_locus() */
    GK_MEASURE,
};
enum { GL_LINE = 0, GL_SEGMENT, GL_RAY }; /* extent of a GK_LINE (parameter t: all, [0,1], >= 0) */

enum {
    GF_DELETED = 1, /* removed (with everything depending on it); slot kept until geo_compact() */
    GF_HIDDEN = 2,  /* not drawn (helper objects) */
};

typedef struct {
    uint8_t type;  /* GT_* */
    uint8_t flags; /* GF_* */
    uint8_t color; /* index into the app's palette (0 = default for the kind) */
    uint8_t npar;
    uint8_t par[GEO_MAXPAR]; /* parent indices, all smaller than this object's */
    int8_t ival, ival2;      /* see enum geo_type */
    char name[GEO_NAME];
    double a, b; /* inputs: coordinates, path parameter, angle, factor */
} geo_obj_t;

typedef struct {
    uint8_t kind; /* GK_* */
    uint8_t sub;  /* GL_* for lines */
    bool ok;      /* defined (all parents defined and the construction has a solution) */
    double x1, y1, x2, y2, r;
} geo_shape_t;

/* One undo step: what to restore. */
enum { GU_ADD = 1, GU_MOVE, GU_DELETE, GU_EDIT, GU_CLEAR };
typedef struct {
    uint8_t kind; /* GU_* */
    uint8_t n;    /* GU_ADD: object count before the step */
    uint8_t idx;  /* GU_MOVE / GU_EDIT: the object */
    uint8_t del[(GEO_MAX + 7) / 8]; /* GU_DELETE: objects whose GF_DELETED was set */
    geo_obj_t old;                  /* GU_MOVE / GU_EDIT: previous contents */
} geo_undo_t;

typedef struct {
    int n;                         /* used slots of obj[] */
    geo_obj_t obj[GEO_MAX];
    geo_shape_t shape[GEO_MAX];    /* computed by geo_update() */
    geo_undo_t undo[GEO_UNDO];     /* ring buffer */
    int undo_top, undo_count;      /* next free entry; entries stored */
    double rxmin, rxmax, rymin, rymax; /* visible range: graph intersections and loci on lines sample it */
} geo_t;

/* ---- construction ---- */
void geo_clear(void); /* empty construction, empty undo (view range kept) */
/* Append an object (name generated from its kind, shape computed) and return
 * its index, or -1 when the array is full even after compaction or a parent
 * index is invalid. Does not record undo: callers bracket one user action
 * with geo_undo_begin_add() ... (any number of geo_add) so that one undo
 * removes all of it. */
int geo_add(int type, const int *par, int npar, int ival, int ival2, double a, double b);
int geo_add_free(double x, double y);
/* Find or create the GT_FUNC object for Y(fn+1). */
int geo_func_obj(int fn);
/* Delete object i and everything that depends on it (records undo). */
void geo_delete(int i);
/* Delete every object as one undo step (File > New). */
void geo_delete_all(void);
/* Squeeze out deleted slots (renumbers parents; clears undo). */
void geo_compact(void);
/* Recompute every shape (forward pass over the dependency order). */
void geo_update(void);
/* Recompute only objects first..n-1 (after changing an input of `first`). */
void geo_update_from(int first);
bool geo_depends_on(int i, int ancestor); /* i is built (directly or not) from ancestor */
int geo_find(const char *name);           /* index of a live object by name, -1 */
const char *geo_type_name(int type);      /* "free", "segment", ... (console, files) */
int geo_kind_of(int i);                   /* GK_* of object i */
bool geo_is_point(int i);
bool geo_is_draggable(int i);             /* GT_FREE or GT_ON */

/* ---- moving inputs ---- */
/* Move a free point to (x, y), or a point on a path to the path point
 * nearest (x, y); recompute. Records one GU_MOVE undo step unless `coalesce`
 * and the previous step moved the same object (a drag is one undo step). */
void geo_move(int i, double x, double y, bool coalesce);
/* Image of object src under a transformation (type GT_REFLECT, GT_ROTATE,
 * GT_TRANSLATE, GT_DILATE; par = the other parents, a = angle or factor).
 * A polygon's image is a polygon of the images of its vertices. Images of
 * named objects are named with a prime (A -> A'). Returns the image's index
 * or -1. */
int geo_transform(int type, int src, const int *par, int npar, double a);
/* Clip the line (x, y) + t (dx, dy) of extent sub (GL_*) to the box
 * {xmin, xmax, ymin, ymax}: the visible parameters t0..t1 (false if none). */
bool geo_clip(double x, double y, double dx, double dy, int sub, const double box[4], double *t0, double *t1);
/* Distance in world units from (x, y) to object i (graphs: vertical
 * distance); INFINITY when undefined or not drawable. */
double geo_distance(int i, double x, double y);
/* Path parameter of the point of path p nearest (x, y). */
double geo_path_project(int p, double x, double y);
/* Point of path p at parameter t (false if undefined). */
bool geo_path_point(int p, double t, double *x, double *y);
/* Parameter range for loci (lines: the part in the visible range). */
void geo_path_range(int p, double *t0, double *t1);

/* ---- computations ---- */
/* Intersections of objects a and b, ordered (line x line: one; line x circle:
 * by the line parameter; circle x circle: right then left of the centre
 * line; with polygons: edge by edge; with graphs: by x). Returns the count
 * (<= max), points in xs/ys. */
int geo_intersections(int a, int b, double *xs, double *ys, int max);
/* Locus: sample object `loc` (GT_LOCUS) at n parameters and call cb with each
 * point (ok false where the traced point is undefined). Leaves the
 * construction as it was. */
void geo_locus(int loc, int n, void (*cb)(int i, bool ok, double x, double y, void *u), void *u);
/* Value of a measurement (NAN if undefined); GM_EQUATION/COORDS use geo_value_text. */
double geo_measure_value(int i);
/* Text shown for object i: "A = (1, 2)", "AB = 3.16228", "a: 2x - y = 3", ... */
void geo_value_text(int i, char *out, size_t n);
/* Number formatting used by the texts (6 significant digits, no "-0"). */
void geo_fmt(double v, char *out, size_t n);

/* ---- undo ---- */
void geo_undo_begin_add(void);  /* the objects added from now on form one step */
void geo_undo_push(const geo_undo_t *u);
bool geo_undo(void);            /* false if nothing to undo */
void geo_undo_reset(void);

/* ---- serialization (session blob "geo", files *.GEO) ---- */
/* Objects only (no undo); deleted slots are skipped and parents renumbered. */
void geo_save(pw_t *w);
/* Replaces the construction; false (and an empty construction) on a corrupt
 * or truncated blob. */
bool geo_load(pr_t *r);

/* ---- the Geometry app (geo_ui.c: screen and tools; geo_io.c: module
 * entry, session blob payload, console, GRAPH overlay) ---- */
enum { GO_AXES = 1, GO_GRID = 2, GO_SNAP = 4, GO_GRAPHS = 8, GO_ONGRAPH = 16 };
typedef struct {
    double ox, oy; /* pixel position of the world origin */
    double scale;  /* pixels per unit (the same on both axes) */
    uint8_t opts;  /* GO_* */
} geo_view_t;

/* The tool state of the screen (geo_ui.c); nothing of it is saved. */
typedef struct {
    int tool;
    int sel[GEO_MAXPAR], nsel; /* objects taken by the current tool so far */
    int cx, cy;                /* cursor, screen pixels */
    int grab;                  /* point carried with the D-pad (Move tool), -1 */
    int drag;                  /* point dragged by touch, -1; -2: panning */
    bool drag_moved;           /* the drag has moved the point (later moves coalesce) */
    bool bracket;              /* an undo step for added objects is open */
    double pinch0;             /* scale when the pinch started (0: none) */
    int prompt;                /* P_* (the core's editor is open while it is not P_NONE) */
    char msg[48];              /* transient message in the prompt bar */
} geo_ui_t;

/* The module's RW block (zeroed at bind). */
typedef struct geo_rw {
    const modx_api_t *api;
    const geo_core_t *core;
    geo_ops_t ops;
    geo_t k;                               /* the construction */
    geo_view_t view;
    geo_ui_t ui;
    double (*fn_eval)(int fn, double x);   /* Y(fn+1)(x) for GT_FUNC (NAN outside the domain); set by the app */
} geo_rw_t;
#define GEO_RW ((geo_rw_t *)MODX_RW())
#define geo (GEO_RW->k)
#define geo_view (GEO_RW->view)
#define geo_fn_eval (GEO_RW->fn_eval)
extern const uint32_t geo_module_rw_size; /* host builds: the RW size (the image's is in its descriptor) */

void geo_view_reset(void);   /* origin at the centre of the body, 20 px per unit, axes + grid + snap */
void geo_view_changed(void); /* after changing geo_view: visible range, recompute, redraw on next draw */
void geo_changed(void);      /* the construction changed: re-plot the GRAPH overlay */
void geo_ui_reset(void);     /* Move tool, nothing selected, no prompt */
/* key=value pairs for the console (`geo state`, `geo expect`) */
void geo_ui_state(void (*kv)(const char *, const char *, void *), void *user);
int geo_console_mod(void *rw, int argc, char **argv);
void geo_set_cursor(double x, double y); /* cursor onto world point (x, y) (console `geo cursor`) */
/* the module's entry points (geo_ui.c, geo_io.c) */
void geo_ui_ops(geo_ops_t *ops);
void geo_io_ops(geo_ops_t *ops);

#if defined(MODX_IMAGE)
#pragma GCC visibility pop
#endif
#endif
