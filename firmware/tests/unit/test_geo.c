/*
 * Unit tests of the geometry kernel (modules/geometry/geo_kernel.c): constructions and
 * their values, intersections (line/segment/ray/circle/polygon/graph) and
 * their ordering, degenerate cases that must make objects undefined instead
 * of producing garbage, the dependency graph (dragging a free point updates
 * every derived object, deleting removes dependents, undo restores), loci,
 * measurements and their texts, and the serialized form (round trip,
 * corrupt blobs). Function graphs use a local evaluator (geo_fn_eval), so no
 * CAS is involved. The kernel is the geometry module's code: its state is a
 * module RW block (here a static one, reached through MODX_RW like on the
 * calculator) and it calls the core through geo_core_table(), the same
 * tables as the module. Built and run by `make test`.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../../modules/geometry/geo.h"

static int fails, checks;
#define CHECK(c)                                                                  \
    do {                                                                          \
        checks++;                                                                 \
        if (!(c)) {                                                               \
            fails++;                                                              \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
        }                                                                         \
    } while (0)
/* |got - want| <= 1e-9 (1 + |want|); an expression, so checks can be chained with commas */
static void near_(double got, double want, const char *what, int line)
{
    checks++;
    if (!(fabs(got - want) <= 1e-9 * (1 + fabs(want)))) {
        fails++;
        fprintf(stderr, "test_geo.c:%d: %s = %.17g, want %.17g\n", line, what, got, want);
    }
}
#define NEAR(a, b) near_((a), (b), #a, __LINE__)
/* value text of object i equals s */
#define TEXT(i, s)                                                                       \
    do {                                                                                 \
        char t_[96];                                                                     \
        geo_value_text((i), t_, sizeof t_);                                              \
        checks++;                                                                        \
        if (strcmp(t_, (s))) {                                                           \
            fails++;                                                                     \
            fprintf(stderr, "%s:%d: text \"%s\", want \"%s\"\n", __FILE__, __LINE__, t_, (s)); \
        }                                                                                \
    } while (0)

static double X(int i) { return geo.shape[i].x1; }
static double Y(int i) { return geo.shape[i].y1; }
static bool OK(int i) { return geo.shape[i].ok; }
static int add(int type, int a, int b, int c, int ival, double v)
{
    int p[3] = {a, b, c}, n = a < 0 ? 0 : b < 0 ? 1 : c < 0 ? 2 : 3;
    return geo_add(type, p, n, ival, 0, v, 0);
}

static double fn_test(int fn, double x)
{
    switch (fn) {
    case 0: return x * x - 2;       /* parabola */
    case 1: return 1 / x;           /* pole at 0 */
    case 2: return sin(x);
    }
    return NAN;
}

static void reset(void)
{
    geo_clear();
    geo.rxmin = -10, geo.rxmax = 10, geo.rymin = -8, geo.rymax = 8;
    geo_fn_eval = fn_test;
}

static void test_basic(void)
{
    reset();
    int A = geo_add_free(0, 0), B = geo_add_free(4, 0), C = geo_add_free(0, 3);
    CHECK(!strcmp(geo.obj[A].name, "A") && !strcmp(geo.obj[C].name, "C"));
    int s = add(GT_SEGMENT, A, B, -1, 0, 0);
    CHECK(!strcmp(geo.obj[s].name, "a"));
    TEXT(s, "a = 4");
    int l = add(GT_LINE, B, C, -1, 0, 0);
    TEXT(l, "b: y = -0.75x + 3");
    int m = add(GT_MIDPOINT, B, C, -1, 0, 0);
    TEXT(m, "D = (2, 1.5)");
    int c = add(GT_CIRCLE, A, B, -1, 0, 0);
    TEXT(c, "c: x² + y² = 16");
    int c3 = add(GT_CIRCLE3, A, B, C, 0, 0);
    NEAR(X(c3), 2), NEAR(Y(c3), 1.5), NEAR(geo.shape[c3].r, 2.5);
    int pb = add(GT_PBISECT, A, B, -1, 0, 0); /* x = 2 */
    TEXT(pb, "e: x = 2");
    int pe = add(GT_PERP, l, A, -1, 0, 0); /* through A, slope 4/3 */
    TEXT(pe, "f: y = 1.33333x");
    int pa = add(GT_PARALLEL, l, A, -1, 0, 0);
    TEXT(pa, "g: y = -0.75x");
    int ab = add(GT_ABISECT, B, A, C, 0, 0); /* bisector of the right angle at A: y = x */
    TEXT(ab, "h: y = x");
    /* measurements */
    int ang = add(GT_MEASURE, B, A, C, GM_ANGLE, 0);
    TEXT(ang, "∠BAC = 90°");
    int d = add(GT_MEASURE, B, C, -1, GM_DIST, 0);
    TEXT(d, "BC = 5");
    int poly;
    {
        int v[3] = {A, B, C};
        poly = geo_add(GT_POLYGON, v, 3, 0, 0, 0, 0);
    }
    int ar = add(GT_MEASURE, poly, -1, -1, GM_AREA, 0), per = add(GT_MEASURE, poly, -1, -1, GM_LENGTH, 0);
    TEXT(ar, "area i = 6");
    TEXT(per, "perim i = 12");
    int sl = add(GT_MEASURE, l, -1, -1, GM_SLOPE, 0);
    TEXT(sl, "slope b = -0.75");
    int sv = add(GT_MEASURE, pb, -1, -1, GM_SLOPE, 0);
    TEXT(sv, "slope e undefined");
    int eq = add(GT_MEASURE, c3, -1, -1, GM_EQUATION, 0);
    TEXT(eq, "d: (x - 2)² + (y - 1.5)² = 6.25");
    int len = add(GT_MEASURE, s, -1, -1, GM_LENGTH, 0);
    TEXT(len, "AB = 4");
    int ca = add(GT_MEASURE, c, -1, -1, GM_AREA, 0);
    NEAR(geo_measure_value(ca), 16 * M_PI);

    /* dependency graph: drag B, everything follows */
    geo_move(B, 6, 0, false);
    TEXT(s, "a = 6");
    TEXT(m, "D = (3, 1.5)");
    TEXT(ar, "area i = 9");
    TEXT(pb, "e: x = 3");
    CHECK(geo_depends_on(ar, B) && geo_depends_on(ar, A) && !geo_depends_on(s, C));
    /* undo the move */
    CHECK(geo_undo());
    TEXT(s, "a = 4");
    TEXT(len, "AB = 4");
}

static void test_intersections(void)
{
    reset();
    double xs[8], ys[8];
    int A = geo_add_free(-2, 0), B = geo_add_free(2, 0), C = geo_add_free(0, -2), D = geo_add_free(0, 2);
    int l1 = add(GT_LINE, A, B, -1, 0, 0), l2 = add(GT_LINE, C, D, -1, 0, 0);
    CHECK(geo_intersections(l1, l2, xs, ys, 8) == 1);
    NEAR(xs[0], 0), NEAR(ys[0], 0);
    int I = add(GT_INTERSECT, l1, l2, -1, 0, 0);
    TEXT(I, "E = (0, 0)");
    /* parallel lines: no intersection -> undefined, defined again after a drag */
    geo_move(D, 1, -1, false); /* l2 from (0,-2) to (1,-1): crosses y=0 at x=2 */
    NEAR(X(I), 2);
    int E = geo_add_free(5, 1), F = geo_add_free(6, 1);
    int l3 = add(GT_LINE, E, F, -1, 0, 0);
    int J = add(GT_INTERSECT, l1, l3, -1, 0, 0);
    CHECK(!OK(J));
    TEXT(J, "H undefined");
    geo_move(F, 6, 2, false);
    CHECK(OK(J));
    NEAR(X(J), 4), NEAR(Y(J), 0);
    /* segments: only within their extent */
    reset();
    A = geo_add_free(0, 0), B = geo_add_free(1, 0), C = geo_add_free(2, -1), D = geo_add_free(2, 1);
    int s1 = add(GT_SEGMENT, A, B, -1, 0, 0), s2 = add(GT_SEGMENT, C, D, -1, 0, 0);
    CHECK(geo_intersections(s1, s2, xs, ys, 8) == 0);
    int r1 = add(GT_RAY, A, B, -1, 0, 0);
    CHECK(geo_intersections(r1, s2, xs, ys, 8) == 1);
    int r2 = add(GT_RAY, B, A, -1, 0, 0);
    CHECK(geo_intersections(r2, s2, xs, ys, 8) == 0);
    /* line x circle: two points ordered along the line; tangent: double */
    reset();
    int O = geo_add_free(0, 0), P = geo_add_free(5, 0);
    int c = add(GT_CIRCLE, O, P, -1, 0, 0);
    A = geo_add_free(-10, 3), B = geo_add_free(10, 3);
    int l = add(GT_LINE, A, B, -1, 0, 0);
    CHECK(geo_intersections(l, c, xs, ys, 8) == 2);
    NEAR(xs[0], -4), NEAR(ys[0], 3), NEAR(xs[1], 4);
    int i0 = add(GT_INTERSECT, l, c, -1, 0, 0), i1 = geo_add(GT_INTERSECT, (int[]){l, c}, 2, 1, 0, 0, 0);
    NEAR(X(i0), -4), NEAR(X(i1), 4);
    geo_move(A, -10, 5, false), geo_move(B, 10, 5, false); /* tangent at (0,5) */
    CHECK(OK(i0) && OK(i1));
    NEAR(X(i0), 0), NEAR(Y(i0), 5);
    geo_move(A, -10, 6, false), geo_move(B, 10, 6, false); /* misses */
    CHECK(!OK(i0) && !OK(i1));
    /* circle x circle */
    int Q = geo_add_free(6, 0), R = geo_add_free(6, 5);
    int c2 = add(GT_CIRCLE, Q, R, -1, 0, 0);
    CHECK(geo_intersections(c, c2, xs, ys, 8) == 2);
    NEAR(xs[0], 3), NEAR(ys[0], -4), NEAR(xs[1], 3), NEAR(ys[1], 4);
    /* concentric and disjoint circles do not intersect */
    geo_move(Q, 0, 0, false);
    CHECK(geo_intersections(c, c2, xs, ys, 8) == 0);
    geo_move(Q, 20, 0, false);
    CHECK(geo_intersections(c, c2, xs, ys, 8) == 0);
    /* circle touching from outside: one point twice */
    geo_move(Q, 10, 0, false), geo_move(R, 10, 5, false);
    CHECK(geo_intersections(c, c2, xs, ys, 8) == 2);
    NEAR(xs[0], 5), NEAR(ys[0], 0);
    /* polygon edges */
    reset();
    int v[4] = {geo_add_free(-1, -1), geo_add_free(1, -1), geo_add_free(1, 1), geo_add_free(-1, 1)};
    int sq = geo_add(GT_POLYGON, v, 4, 0, 0, 0, 0);
    A = geo_add_free(-5, 0), B = geo_add_free(5, 0);
    l = add(GT_LINE, A, B, -1, 0, 0);
    CHECK(geo_intersections(sq, l, xs, ys, 8) == 2);
    NEAR(xs[0], 1), NEAR(xs[1], -1);
}

static void test_graphs(void)
{
    reset();
    double xs[8], ys[8];
    int f = geo_func_obj(0);
    CHECK(geo_func_obj(0) == f); /* found, not added again */
    CHECK(!strcmp(geo.obj[f].name, "Y1"));
    int A = geo_add_free(-1, 0), B = geo_add_free(1, 0);
    int l = add(GT_LINE, A, B, -1, 0, 0); /* y = 0: roots of x^2 - 2 */
    CHECK(geo_intersections(f, l, xs, ys, 8) == 2);
    NEAR(xs[0], -sqrt(2)), NEAR(xs[1], sqrt(2));
    /* vertical line */
    int C = geo_add_free(3, 0), D = geo_add_free(3, 1);
    int v = add(GT_LINE, C, D, -1, 0, 0);
    CHECK(geo_intersections(f, v, xs, ys, 8) == 1);
    NEAR(ys[0], 7);
    /* a pole is not an intersection: 1/x with y = 0 */
    int g = geo_func_obj(1);
    CHECK(geo_intersections(g, l, xs, ys, 8) == 0);
    /* graph x graph: x^2 - 2 = 1/x has three roots */
    CHECK(geo_intersections(f, g, xs, ys, 8) == 3);
    /* point on a graph follows its x */
    int P = geo_add(GT_ON, &f, 1, 0, 0, 2, 0);
    TEXT(P, "E = (2, 2)");
    geo_move(P, 3, 100, false);
    TEXT(P, "E = (3, 7)");
    /* graph x circle */
    int O = geo_add_free(0, 0), E = geo_add_free(0, 1.5);
    int c = add(GT_CIRCLE, O, E, -1, 0, 0); /* x^4 - 3x^2 + 1.75 = 0: four roots */
    int n = geo_intersections(f, c, xs, ys, 8);
    CHECK(n == 4);
    for (int k = 0; k < n; k++)
        NEAR(hypot(xs[k], ys[k]), 1.5);
}

static void test_tangent_transform(void)
{
    reset();
    int O = geo_add_free(0, 0), R = geo_add_free(1, 0), P = geo_add_free(2, 0);
    int c = add(GT_CIRCLE, O, R, -1, 0, 0);
    int t0 = add(GT_TANGENT, P, c, -1, 0, 0), t1 = add(GT_TANGENT, P, c, -1, 1, 0);
    /* tangent points at 60 degrees: slopes -+1/sqrt(3) */
    double s0 = (geo.shape[t0].y2 - geo.shape[t0].y1) / (geo.shape[t0].x2 - geo.shape[t0].x1);
    double s1 = (geo.shape[t1].y2 - geo.shape[t1].y1) / (geo.shape[t1].x2 - geo.shape[t1].x1);
    NEAR(fabs(s0), 1 / sqrt(3)), NEAR(s0, -s1);
    double xs[4], ys[4];
    CHECK(geo_intersections(t0, c, xs, ys, 4) == 2); /* touches: double point */
    NEAR(xs[0], xs[1]), NEAR(xs[0], 0.5);
    geo_move(P, 0.5, 0, false); /* inside: no tangent */
    CHECK(!OK(t0) && !OK(t1));
    geo_move(P, 0, 1, false); /* on the circle: the horizontal tangent */
    CHECK(OK(t0));
    NEAR(geo.shape[t0].y2, 1);
    /* transformations of points, lines, circles and polygons */
    reset();
    int A = geo_add_free(1, 0), B = geo_add_free(0, 0), C = geo_add_free(3, 1), Dp = geo_add_free(0, 2);
    int axis = add(GT_LINE, B, Dp, -1, 0, 0); /* the y axis */
    int ra = geo_transform(GT_REFLECT, A, &axis, 1, 0);
    TEXT(ra, "A' = (-1, 0)");
    int pr = geo_transform(GT_REFLECT, C, &B, 1, 0); /* point reflection in the origin */
    TEXT(pr, "C' = (-3, -1)");
    int rot = geo_transform(GT_ROTATE, A, &B, 1, 90);
    NEAR(X(rot), 0), NEAR(Y(rot), 1);
    int tv[2] = {B, Dp};
    int tr = geo_transform(GT_TRANSLATE, C, tv, 2, 0);
    TEXT(tr, "F = (3, 3)"); /* C' is taken */
    int c1 = add(GT_CIRCLE, C, A, -1, 0, 0); /* radius sqrt(5) */
    int dl = geo_transform(GT_DILATE, c1, &B, 1, -2);
    NEAR(X(dl), -6), NEAR(Y(dl), -2), NEAR(geo.shape[dl].r, 2 * sqrt(5));
    int v[3] = {A, C, Dp};
    int tri = geo_add(GT_POLYGON, v, 3, 0, 0, 0, 0);
    int n0 = geo.n, img = geo_transform(GT_ROTATE, tri, &B, 1, 180);
    CHECK(img == n0 + 3 && geo_kind_of(img) == GK_POLYGON);
    int ar = add(GT_MEASURE, tri, -1, -1, GM_AREA, 0), ar2 = add(GT_MEASURE, img, -1, -1, GM_AREA, 0);
    NEAR(geo_measure_value(ar), geo_measure_value(ar2));
    /* moving the source moves the image */
    geo_move(A, 2, 0, false);
    TEXT(ra, "A' = (-2, 0)");
    /* regular hexagon on the edge (0,0)-(1,0) */
    reset();
    A = geo_add_free(0, 0), B = geo_add_free(1, 0);
    int vs[6] = {A, B};
    for (int k = 2; k < 6; k++)
        vs[k] = geo_add(GT_REGVERT, (int[]){A, B}, 2, 6, k, 0, 0);
    NEAR(X(vs[3]), 1), NEAR(Y(vs[3]), sqrt(3)), NEAR(X(vs[5]), -0.5);
    int hex = geo_add(GT_POLYGON, vs, 6, 0, 0, 0, 0);
    int ha = add(GT_MEASURE, hex, -1, -1, GM_AREA, 0);
    NEAR(geo_measure_value(ha), 1.5 * sqrt(3));
}

static void test_degenerate(void)
{
    reset();
    int A = geo_add_free(1, 1), B = geo_add_free(1, 1), C = geo_add_free(2, 2);
    int l = add(GT_LINE, A, B, -1, 0, 0);
    CHECK(!OK(l));
    int pl = add(GT_PERP, l, C, -1, 0, 0); /* parent undefined -> undefined */
    CHECK(!OK(pl));
    int c = add(GT_CIRCLE, A, B, -1, 0, 0); /* radius 0 */
    CHECK(!OK(c));
    int D = geo_add_free(3, 3);
    int c3 = add(GT_CIRCLE3, A, C, D, 0, 0); /* collinear */
    CHECK(!OK(c3));
    int pb = add(GT_PBISECT, A, B, -1, 0, 0);
    CHECK(!OK(pb));
    int ang = add(GT_MEASURE, A, B, C, GM_ANGLE, 0);
    CHECK(!OK(ang));
    /* straight angle: bisector perpendicular */
    int E = geo_add_free(-1, 0), F = geo_add_free(0, 0), G = geo_add_free(1, 0);
    int ab = add(GT_ABISECT, E, F, G, 0, 0);
    CHECK(OK(ab));
    TEXT(ab, "f: x = 0");
    /* everything recovers when the points separate */
    geo_move(B, 4, 1, false);
    CHECK(OK(l) && OK(pl) && OK(c) && OK(pb));
    /* invalid adds */
    CHECK(geo_add(GT_LINE, (int[]){A}, 1, 0, 0, 0, 0) < 0);
    CHECK(geo_add(GT_LINE, (int[]){A, 99}, 2, 0, 0, 0, 0) < 0);
    CHECK(geo_add(GT_MEASURE, (int[]){A}, 1, GM_ANGLE, 0, 0, 0) < 0);
    CHECK(geo_add(GT_REGVERT, (int[]){A, B}, 2, 2, 0, 0, 0) < 0);
}

/* locus callback: points defined, and their distance from (2, 2) should be 2 */
typedef struct {
    int n;
    double maxerr;
} locus_acc_t;
static void cb(int i, bool ok, double x, double y, void *u)
{
    locus_acc_t *a = u;
    a->n += ok;
    a->maxerr = fmax(a->maxerr, fabs(hypot(x - 2, y - 2) - 2));
}

static void test_delete_undo_locus(void)
{
    reset();
    int A = geo_add_free(0, 0), B = geo_add_free(2, 0), C = geo_add_free(5, 5);
    geo_undo_reset();
    geo_undo_begin_add();
    int s = add(GT_SEGMENT, A, B, -1, 0, 0);
    int M = add(GT_MIDPOINT, A, B, -1, 0, 0);
    int len = add(GT_MEASURE, s, -1, -1, GM_LENGTH, 0);
    CHECK(geo.n == 6);
    geo_delete(A); /* removes s, M, len too, not C */
    CHECK(geo_find("A") < 0 && geo_find("a") < 0 && geo_find("D") < 0 && geo_find("C") == C);
    CHECK(geo_distance(s, 1, 0) == INFINITY);
    CHECK(geo_undo()); /* delete undone */
    CHECK(geo_find("A") == A && geo_find("m1") == len);
    TEXT(len, "AB = 2");
    CHECK(geo_undo()); /* the add step: s, M, len gone */
    CHECK(geo.n == 3);
    CHECK(!geo_undo());
    (void)M;
    /* drags coalesce into one undo step */
    geo_move(C, 1, 1, false);
    geo_move(C, 2, 2, true);
    geo_move(C, 3, 3, true);
    CHECK(geo_undo());
    NEAR(X(C), 5);
    /* compaction renumbers parents */
    reset();
    A = geo_add_free(0, 0), B = geo_add_free(1, 0), C = geo_add_free(0, 1);
    s = add(GT_SEGMENT, B, C, -1, 0, 0);
    geo_delete(A);
    geo_compact();
    CHECK(geo.n == 3 && geo.obj[2].par[0] == 0 && geo.obj[2].par[1] == 1);
    TEXT(2, "a = 1.41421");
    /* locus: the midpoint of a fixed point and a point running on a circle
     * traces a circle of half the radius */
    reset();
    int O = geo_add_free(0, 0), R = geo_add_free(4, 0), F = geo_add_free(4, 4);
    int c = add(GT_CIRCLE, O, R, -1, 0, 0);
    int P = geo_add(GT_ON, &c, 1, 0, 0, 0.3, 0);
    M = add(GT_MIDPOINT, P, F, -1, 0, 0);
    int L = add(GT_LOCUS, M, P, -1, 0, 0);
    CHECK(OK(L));
    locus_acc_t acc = {0, 0};
    geo_locus(L, 50, cb, &acc);
    CHECK(acc.n == 50 && acc.maxerr < 1e-9);
    NEAR(geo.obj[P].a, 0.3); /* the driver is restored */
    NEAR(X(M), 0.5 * (4 * cos(0.3) + 4));
}


static void test_serialize(void)
{
    reset();
    int A = geo_add_free(0.5, -1.25), B = geo_add_free(3, 4);
    int s = add(GT_SEGMENT, A, B, -1, 0, 0);
    int Z = geo_add_free(9, 9);
    int c = add(GT_CIRCLE, A, B, -1, 0, 0);
    int r = geo_transform(GT_ROTATE, s, &A, 1, 30);
    add(GT_MEASURE, s, -1, -1, GM_LENGTH, 0);
    geo_delete(Z);
    char t1[96], t2[96];
    geo_value_text(r, t1, sizeof t1);
    uint8_t buf[4096];
    pw_t w = {buf, sizeof buf, 0, false};
    geo_save(&w);
    CHECK(!w.overflow);
    reset();
    pr_t rd = {buf, w.len, 0, false};
    CHECK(geo_load(&rd));
    CHECK(geo.n == 6); /* Z left out */
    CHECK(geo_find("Z") < 0 && geo_find("b") >= 0);
    geo_value_text(geo_find("a'"), t2, sizeof t2);
    CHECK(!strcmp(t1, t2));
    TEXT(geo_find("m1"), "AB = 5.81485");
    (void)c;
    /* truncated blob: empty construction, false */
    pr_t tr = {buf, w.len - 3, 0, false};
    CHECK(!geo_load(&tr) && geo.n == 0);
    /* forward reference: rejected */
    buf[0] = 1;
    buf[1] = GT_SEGMENT, buf[4] = 2, buf[5] = 0, buf[6] = 0;
    pr_t bad = {buf, w.len, 0, false};
    CHECK(!geo_load(&bad));
}

int main(void)
{
    static geo_rw_t rw; /* the module's RW block, as the core would allocate it */
    rw.core = geo_core_table();
    rw.api = &modx_api;
    modx_ctx = &rw;
    test_basic();
    test_intersections();
    test_graphs();
    test_tangent_transform();
    test_degenerate();
    test_delete_undo_locus();
    test_serialize();
    printf("test_geo: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
