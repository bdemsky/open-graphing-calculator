/* 3D graphing: camera and projection (g3d_view.c), meshing (g3d_mesh.c),
 * the z-buffered rasterizer and scene renderer (g3d_raster.c) and the model
 * (g3d_model.c: CAS evaluation of the three entry kinds, saved state).
 *
 * The geometry tests need no application: they render into a small local
 * target and count pixels. The model test sets up just enough of the app by
 * hand, as test_graph.c does (`g`, the token table, a CAS in a static arena,
 * the 3D work memory from a static block). Also prints a timing of the
 * renderer on this machine (informational; the MCU estimate is in the help
 * of `g3d bench`). Built and run by `make test`. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "g3d.h"
#include "session.h"

static int fails, checks;
#define CHECK(c)                                                              \
    do {                                                                      \
        checks++;                                                             \
        if (!(c)) {                                                           \
            fails++;                                                          \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
        }                                                                     \
    } while (0)
#define NEAR(a, b, tol) CHECK(fabs((double)(a) - (double)(b)) <= (tol))

/* ------------------------------------------------------------ view */
/* sin/cos against libm, exact values at the quadrant angles; the camera
 * basis is orthonormal and right-handed; known projections for the front,
 * side and top views; perspective makes nearer points larger. */
static void test_view(void)
{
    for (double d = -720; d <= 720; d += 7.3) {
        NEAR(g3_sin_deg(d), sin(d * M_PI / 180), 1e-14);
        NEAR(g3_cos_deg(d), cos(d * M_PI / 180), 1e-14);
    }
    CHECK(g3_sin_deg(90) == 1 && g3_sin_deg(180) == 0 && g3_cos_deg(90) == 0 && g3_cos_deg(-180) == -1);
    NEAR(g3_sin_deg(30), 0.5, 1e-16);

    g3_view_t v;
    for (double az = -170; az < 180; az += 37)
        for (double el = -90; el <= 90; el += 30) {
            g3_view_setup(&v, az, el, 0, 0, 1);
            for (int r = 0; r < 3; r++)
                for (int c = 0; c < 3; c++) {
                    float dot = v.m[r][0] * v.m[c][0] + v.m[r][1] * v.m[c][1] + v.m[r][2] * v.m[c][2];
                    NEAR(dot, r == c ? 1 : 0, 1e-6);
                }
            /* right x up = towards */
            float cx = v.m[0][1] * v.m[1][2] - v.m[0][2] * v.m[1][1];
            float cy = v.m[0][2] * v.m[1][0] - v.m[0][0] * v.m[1][2];
            float cz = v.m[0][0] * v.m[1][1] - v.m[0][1] * v.m[1][0];
            NEAR(cx, v.m[2][0], 1e-6);
            NEAR(cy, v.m[2][1], 1e-6);
            NEAR(cz, v.m[2][2], 1e-6);
        }

    /* front view: x right, z up, -y towards the viewer */
    g3_view_setup(&v, 0, 0, 100, 50, 10);
    g3p_t p, q;
    g3_project(&v, &(g3v_t){0, 0, 0}, &p);
    NEAR(p.sx, 100, 1e-5);
    NEAR(p.sy, 50, 1e-5);
    NEAR(p.d, 1 / G3_EYE, 1e-7);
    g3_project(&v, &(g3v_t){1, 0, 0}, &p);
    NEAR(p.sx, 110, 1e-4); /* depth 0: no perspective scaling */
    NEAR(p.sy, 50, 1e-4);
    g3_project(&v, &(g3v_t){0, 0, 1}, &p);
    NEAR(p.sx, 100, 1e-4);
    NEAR(p.sy, 40, 1e-4);
    g3_project(&v, &(g3v_t){0, -1, 0}, &p);
    g3_project(&v, &(g3v_t){0, 1, 0}, &q);
    CHECK(p.d > q.d); /* -y is nearer */
    NEAR(p.d, 1 / (G3_EYE - 1), 1e-7);
    /* perspective: the same offset is larger nearer the eye */
    g3_project(&v, &(g3v_t){1, -1, 0}, &p);
    g3_project(&v, &(g3v_t){1, 1, 0}, &q);
    NEAR(p.sx - 100, 10 * G3_EYE / (G3_EYE - 1), 1e-4);
    NEAR(q.sx - 100, 10 * G3_EYE / (G3_EYE + 1), 1e-4);
    /* side view (azimuth -90, from +x): +y to the right */
    g3_view_setup(&v, -90, 0, 0, 0, 10);
    g3_project(&v, &(g3v_t){0, 1, 0}, &p);
    NEAR(p.sx, 10, 1e-4);
    /* top view: +y is up on the screen, z towards the viewer */
    g3_view_setup(&v, 0, 90, 0, 0, 10);
    g3_project(&v, &(g3v_t){0, 1, 0}, &p);
    NEAR(p.sy, -10, 1e-4);
    NEAR(p.sx, 0, 1e-4);
    g3_project(&v, &(g3v_t){0, 0, 1}, &p);
    g3_project(&v, &(g3v_t){0, 0, -1}, &q);
    CHECK(p.d > q.d);
    /* undefined points */
    g3_project(&v, &(g3v_t){NAN, 0, 0}, &p);
    CHECK(isnan(p.d));
}

/* ------------------------------------------------------------ meshing */
static bool f_plane(void *u, double x, double y, double o[3])
{
    (void)u;
    o[0] = x, o[1] = y, o[2] = x + 2 * y;
    return true;
}
static bool f_sqrt(void *u, double x, double y, double o[3])
{
    (void)u;
    o[0] = x, o[1] = y, o[2] = sqrt(x); /* NAN for x < 0 */
    return true;
}
static bool f_helix(void *u, double t, double v, double o[3])
{
    (void)u, (void)v;
    o[0] = cos(t), o[1] = sin(t), o[2] = t;
    return true;
}

static g3w_t mw[G3_NODES_MAX];
static g3v_t mn[G3_NODES_MAX];
static g3p_t mp[G3_NODES_MAX];

/* Grid layout and parameter values (exact end nodes), normalisation, the z
 * range, undefined values, points far outside the box, curves. */
static void test_mesh(void)
{
    g3_box_t box = {{-2, -2, -4}, {2, 2, 4}};
    g3_mesh_t m = {G3K_FUNC, 5, 3, -2, 2, -1, 1, mw, mn, mp, 0, 0, 0};
    g3_mesh_sample(&m, f_plane, NULL, &box);
    CHECK(m.defined == 15);
    /* node (i, j) at j * nu + i: x = -2 + i, y = -1 + j */
    NEAR(mw[0].x, -2, 0);
    NEAR(mw[4].x, 2, 0);
    NEAR(mw[2 * 5 + 4].y, 1, 0);
    NEAR(mw[1 * 5 + 3].z, 1 + 0, 1e-15); /* x = 1, y = 0 */
    NEAR(m.zlo, -4, 1e-15);
    NEAR(m.zhi, 4, 1e-15);
    /* normalised: x / 2, y / 2, z / 4 */
    NEAR(mn[4].x, 1, 1e-7);
    NEAR(mn[2 * 5 + 4].z, 1, 1e-7);
    NEAR(mn[1 * 5 + 1].y, 0, 1e-7);
    /* a node far outside the box (z = 4 in a box to 0.5: 15 half-widths) is dropped */
    g3_box_t flat = {{-2, -2, -0.5}, {2, 2, 0.5}};
    g3_mesh_sample(&m, f_plane, NULL, &flat);
    CHECK(isnan(mn[2 * 5 + 4].x));
    CHECK(!isnan(mn[1 * 5 + 2].x)); /* z = 0 */
    CHECK(!isnan(mw[2 * 5 + 4].z));  /* the world value is kept (trace, zoom fit) */
    /* undefined values */
    g3_mesh_sample(&m, f_sqrt, NULL, &box);
    CHECK(m.defined == 9); /* x = 0, 1, 2 */
    CHECK(isnan(mw[0].z) && isnan(mn[0].x));
    NEAR(m.zhi, sqrt(2), 1e-15);
    /* a curve: nu nodes, nv = 1, t from 0 to 2π */
    g3_mesh_t c = {G3K_CURVE, 101, 1, 0, 2 * M_PI, 0, 0, mw, mn, mp, 0, 0, 0};
    g3_box_t cb = {{-1, -1, 0}, {1, 1, 2 * M_PI}};
    g3_mesh_sample(&c, f_helix, NULL, &cb);
    CHECK(c.defined == 101);
    NEAR(mw[100].z, 2 * M_PI, 0);
    NEAR(mn[100].z, 1, 1e-7);
    NEAR(mn[0].z, -1, 1e-7);
    /* normalisation of a single point */
    g3v_t n;
    g3_normalize(&box, &(g3w_t){1, -1, 2}, &n);
    NEAR(n.x, 0.5, 1e-7);
    NEAR(n.y, -0.5, 1e-7);
    NEAR(n.z, 0.5, 1e-7);
    g3_normalize(&box, &(g3w_t){INFINITY, 0, 0}, &n);
    CHECK(isnan(n.x));
}

/* ------------------------------------------------------------ rasterizer */
#define TW 40
#define TH 40
#define BG 0x0000
static uint16_t tpx[TW * TH];
static float tzb[TW * TH];
static const g3_target_t tgt = {tpx, tzb, 0, 0, TW, TH, TW};

static int count(uint16_t c)
{
    int n = 0;
    for (int i = 0; i < TW * TH; i++)
        n += tpx[i] == c;
    return n;
}
static g3p_t P(float x, float y, float d) { return (g3p_t){x, y, d}; }

/* Z-buffering is order independent; shared edges leave no gaps; coverage
 * follows the pixel-centre rule; box clipping per pixel; z-tested lines are
 * hidden behind surfaces and visible on them; undefined points draw nothing. */
static void test_raster(void)
{
    const uint16_t RED = 0xF800, BLUE = 0x001F, GREEN = 0x07E0;
    /* a square of two triangles, 10..30 in x and y: exactly 20 x 20 pixels, no gaps */
    g3r_clear(&tgt, BG);
    g3p_t a = P(10, 10, 0.5f), b = P(30, 10, 0.5f), c = P(30, 30, 0.5f), d = P(10, 30, 0.5f);
    g3r_tri(&tgt, &a, &b, &c, NULL, NULL, NULL, RED);
    g3r_tri(&tgt, &a, &c, &d, NULL, NULL, NULL, RED);
    CHECK(count(RED) == 400);
    CHECK(tpx[10 * TW + 10] == RED && tpx[29 * TW + 29] == RED && tpx[9 * TW + 10] == BG && tpx[30 * TW + 30] == BG);
    /* the same with an irregular shared diagonal (fractional vertices) */
    g3r_clear(&tgt, BG);
    g3p_t e = P(3.3f, 4.7f, 0.5f), f = P(35.1f, 2.2f, 0.5f), h = P(31.7f, 36.4f, 0.5f), k = P(5.9f, 33.3f, 0.5f);
    g3r_tri(&tgt, &e, &f, &h, NULL, NULL, NULL, RED);
    g3r_tri(&tgt, &e, &h, &k, NULL, NULL, NULL, BLUE);
    int gaps = 0;
    for (int y = 8; y < 30; y++)
        for (int x = 8; x < 30; x++)
            gaps += tpx[y * TW + x] == BG;
    CHECK(gaps == 0);

    /* z-buffer: near red over far blue, in both drawing orders */
    for (int order = 0; order < 2; order++) {
        g3r_clear(&tgt, BG);
        g3p_t n0 = P(5, 5, 0.3f), n1 = P(35, 5, 0.3f), n2 = P(5, 35, 0.3f);   /* near */
        g3p_t f0 = P(15, 15, 0.2f), f1 = P(35, 15, 0.2f), f2 = P(15, 35, 0.2f); /* far */
        if (order == 0) {
            g3r_tri(&tgt, &n0, &n1, &n2, NULL, NULL, NULL, RED);
            g3r_tri(&tgt, &f0, &f1, &f2, NULL, NULL, NULL, BLUE);
        } else {
            g3r_tri(&tgt, &f0, &f1, &f2, NULL, NULL, NULL, BLUE);
            g3r_tri(&tgt, &n0, &n1, &n2, NULL, NULL, NULL, RED);
        }
        CHECK(tpx[15 * TW + 16] == RED);  /* overlap: the near one */
        CHECK(tpx[30 * TW + 17] == BLUE); /* far only */
        CHECK(tpx[6 * TW + 6] == RED);
        NEAR(tzb[15 * TW + 16], 0.3, 1e-6);
    }
    /* interpenetrating triangles: depth is interpolated per pixel */
    g3r_clear(&tgt, BG);
    g3p_t l0 = P(0, 0, 0.1f), l1 = P(40, 0, 0.9f), l2 = P(0, 40, 0.1f);   /* nearer to the right */
    g3p_t r0 = P(0, 0, 0.5f), r1 = P(40, 0, 0.5f), r2 = P(0, 40, 0.5f);   /* constant 0.5 */
    g3r_tri(&tgt, &l0, &l1, &l2, NULL, NULL, NULL, RED);
    g3r_tri(&tgt, &r0, &r1, &r2, NULL, NULL, NULL, BLUE);
    CHECK(tpx[2 * TW + 5] == BLUE);  /* x = 5: d = 0.2 behind */
    CHECK(tpx[2 * TW + 30] == RED);  /* x = 30: d = 0.7 in front */

    /* box clipping: world z from -1 (left) to 3 (right): only z <= 1 drawn (the left half) */
    g3r_clear(&tgt, BG);
    g3p_t c0 = P(0, 0, 0.5f), c1 = P(40, 0, 0.5f), c2 = P(40, 40, 0.5f), c3 = P(0, 40, 0.5f);
    g3v_t w0 = {0, 0, -1}, w1 = {0, 0, 3}, w2 = {0, 0, 3}, w3 = {0, 0, -1};
    g3r_tri(&tgt, &c0, &c1, &c2, &w0, &w1, &w2, GREEN);
    g3r_tri(&tgt, &c0, &c2, &c3, &w0, &w2, &w3, GREEN);
    CHECK(count(GREEN) == 20 * 40);
    CHECK(tpx[20 * TW + 19] == GREEN && tpx[20 * TW + 20] == BG);

    /* lines: hidden behind a near surface, visible on it (bias), visible in front */
    g3r_clear(&tgt, BG);
    g3p_t s0 = P(0, 0, 0.5f), s1 = P(40, 0, 0.5f), s2 = P(0, 40, 0.5f), s3 = P(40, 40, 0.5f);
    g3r_tri(&tgt, &s0, &s1, &s2, NULL, NULL, NULL, BLUE);
    g3r_tri(&tgt, &s1, &s3, &s2, NULL, NULL, NULL, BLUE);
    g3p_t la = P(2.5f, 5.5f, 0.4f), lb = P(37.5f, 5.5f, 0.4f);
    g3r_line(&tgt, &la, &lb, NULL, NULL, RED, true, false);
    CHECK(tpx[5 * TW + 20] == BLUE); /* behind */
    la.d = lb.d = 0.5f;
    g3r_line(&tgt, &la, &lb, NULL, NULL, RED, true, false);
    CHECK(tpx[5 * TW + 20] == RED); /* on the surface */
    la.d = lb.d = 0.6f, la.sy = lb.sy = 10.5f;
    g3r_line(&tgt, &la, &lb, NULL, NULL, GREEN, true, true);
    CHECK(tpx[10 * TW + 20] == GREEN && tpx[11 * TW + 21] == GREEN); /* thick: 2 x 2 */
    CHECK(tzb[10 * TW + 20] == 0.5f); /* lines never write depth */
    /* endpoints inclusive, untested lines ignore depth */
    g3r_clear(&tgt, BG);
    g3p_t e0 = P(3.5f, 3.5f, 0), e1 = P(3.5f, 12.5f, 0);
    g3r_line(&tgt, &e0, &e1, NULL, NULL, RED, false, false);
    CHECK(count(RED) == 10);
    /* undefined vertex: nothing */
    g3r_clear(&tgt, BG);
    g3p_t u0 = P(0, 0, NAN);
    g3r_tri(&tgt, &u0, &s1, &s2, NULL, NULL, NULL, RED);
    g3r_line(&tgt, &u0, &s1, NULL, NULL, RED, false, false);
    CHECK(count(RED) == 0);

    /* colours */
    CHECK(g3_shade(0xFFFF, 1) == 0xFFFF && g3_shade(0xFFFF, 0) == 0);
    CHECK(g3_shade(RGB565(208, 104, 48), 0.5f) == RGB565(104, 52, 24));
    CHECK(g3_heat(-1) == RGB565(30, 70, 230) && g3_heat(1) == RGB565(225, 45, 30) && g3_heat(5) == g3_heat(1));
    CHECK(g3_heat(NAN) == g3_heat(-1));
}

/* ------------------------------------------------------------ scene */
static uint16_t img[LCD_WIDTH * LCD_HEIGHT];
static float zb[LCD_WIDTH * BODY_H];

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* A saddle on a 25 x 25 grid: shaded and wireframe renders draw it, the
 * shaded render is opaque (the background colour does not show through the
 * centre), a coarse render works; prints the time per frame. */
static void test_scene(void)
{
    g3_box_t box = {{-1, -1, -1}, {1, 1, 1}};
    g3_mesh_t m = {G3K_FUNC, 25, 25, -1, 1, -1, 1, mw, mn, mp, 0, 0, 0};
    g3_mesh_sample(&m, f_plane, NULL, &box); /* z = x + 2y, clipped at |z| = 1 */
    g3_view_t v;
    g3_view_setup(&v, -60, 25, 160, 121, 52);
    g3_scene_t s;
    memset(&s, 0, sizeof s);
    s.view = &v;
    s.mesh[0] = &m;
    s.color[0] = 0x001F;
    s.style = G3S_SHADE;
    s.box = s.axes = true;
    s.stride = 1;
    g3_target_t t = {img, zb, 0, BODY_Y, LCD_WIDTH, BODY_H, LCD_WIDTH};
    g3_render(&t, &s);
    int lit = 0;
    for (int i = BODY_Y * LCD_WIDTH; i < (BODY_Y + BODY_H) * LCD_WIDTH; i++)
        lit += img[i] != C_WHITE;
    CHECK(lit > 3000);
    CHECK(img[121 * LCD_WIDTH + 160] != C_WHITE); /* the plane passes through the centre */
    CHECK(!isinf(zb[(121 - BODY_Y) * LCD_WIDTH + 160]));
    s.style = G3S_WIRE;
    g3_render(&t, &s);
    int wire = 0;
    for (int i = BODY_Y * LCD_WIDTH; i < (BODY_Y + BODY_H) * LCD_WIDTH; i++)
        wire += img[i] != C_WHITE;
    CHECK(wire > 500 && wire < lit);
    /* timing (informational) */
    g3_mesh_t big = {G3K_FUNC, 33, 33, -1, 1, -1, 1, mw, mn, mp, 0, 0, 0};
    g3_mesh_sample(&big, f_plane, NULL, &box);
    s.mesh[0] = &big;
    s.style = G3S_SHADE;
    int n = 50;
    double t0 = now_s();
    for (int k = 0; k < n; k++) {
        g3_view_setup(&v, -60 + k, 25, 160, 121, 52);
        g3_render(&t, &s);
    }
    double dt = (now_s() - t0) / n;
    printf("test_g3d: shaded 32x32 grid render %.0f us per frame on this host\n", dt * 1e6);
}

/* ------------------------------------------------------------ contours */
static bool f_half(void *u, double x, double y, double o[3])
{
    (void)u;
    o[0] = x, o[1] = y, o[2] = x / 2;
    return true;
}
static bool f_cone(void *u, double x, double y, double o[3])
{
    (void)u;
    o[0] = x, o[1] = y, o[2] = sqrt(x * x + y * y) - 0.5;
    return true;
}

/* Level curves: the plane z = x/2 on a 25 x 25 grid (normalised z -0.5..0.5)
 * meets 5 of 9 levels (-0.4 .. 0.4), each a line across the 24 cell rows:
 * one segment per cell in the contour map, one per triangle in 3D; the map's
 * filled bands differ left and right of a line, and its pixels show no
 * background; parametric surfaces get no contours; a cone's level curves are
 * circles centred on the map's centre. */
static void test_contours(void)
{
    g3_box_t box = {{-1, -1, -1}, {1, 1, 1}};
    g3_mesh_t m = {G3K_FUNC, 25, 25, -1, 1, -1, 1, mw, mn, mp, 0, 0, 0};
    g3_mesh_sample(&m, f_half, NULL, &box);
    g3_view_t v;
    g3_view_setup(&v, -60, 25, 160, 121, 52);
    g3_scene_t s;
    memset(&s, 0, sizeof s);
    s.view = &v;
    s.mesh[0] = &m;
    s.color[0] = 0x001F;
    s.style = G3S_SHADE;
    s.stride = 1;
    s.win = box;
    g3_target_t t = {img, zb, 0, BODY_Y, LCD_WIDTH, BODY_H, LCD_WIDTH};
    CHECK(g3_render(&t, &s) == 0); /* no levels: no contours */
    s.levels = 9;
    CHECK(g3_render(&t, &s) == 5 * 24 * 2);
    s.style = G3S_WIRE;
    CHECK(g3_render(&t, &s) == 5 * 24 * 2);
    m.kind = G3K_PSURF; /* (same nodes) */
    CHECK(g3_render(&t, &s) == 0);
    m.kind = G3K_FUNC;
    /* the map: top view, 80 px per half-width */
    g3_view_setup(&v, 0, 90, 140, 114, 80);
    s.contour_map = true;
    s.style = G3S_SHADE;
    CHECK(g3_render(&t, &s) == 5 * 24);
    int bg = 0;
    for (int y = 40; y < 188; y++)
        for (int x = 66; x < 214; x++)
            bg += img[y * LCD_WIDTH + x] == C_WHITE;
    CHECK(bg == 0);
    /* x = 0.2 (16 px right of the centre, z = 0.1) and x = 0.6 (48 px, z = 0.3) lie on either side of
     * the level z = 0.2: different bands */
    uint16_t c1 = img[114 * LCD_WIDTH + 140 + 16], c2 = img[114 * LCD_WIDTH + 140 + 48];
    CHECK(c1 != c2 && c1 != C_WHITE && c2 != C_WHITE);
    s.style = G3S_WIRE; /* lines only: the background shows between them */
    CHECK(g3_render(&t, &s) == 5 * 24);
    CHECK(img[114 * LCD_WIDTH + 140 + 16] == C_WHITE);
    /* a cone z = r - 0.5: the level z = 0 is the circle r = 0.5 (40 px) about the centre */
    g3_mesh_t c = {G3K_FUNC, 33, 33, -1, 1, -1, 1, mw, mn, mp, 0, 0, 0};
    g3_mesh_sample(&c, f_cone, NULL, &box);
    s.mesh[0] = &c;
    s.box = s.axes = false;
    CHECK(g3_render(&t, &s) > 0);
    for (int a = 0; a < 360; a += 45) {
        int x = 140 + (int)lround(40 * cos(a * M_PI / 180)), y = 114 - (int)lround(40 * sin(a * M_PI / 180)), hit = 0;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
                hit += img[(y + dy) * LCD_WIDTH + x + dx] != C_WHITE;
        CHECK(hit > 0);
    }
    CHECK(img[114 * LCD_WIDTH + 140] == C_WHITE); /* (inside, between the circles) */
}

/* ------------------------------------------------------------ model */
static uint8_t arena[8u << 20];
static uint8_t g3mem[2u << 20];
static uint16_t fb[LCD_WIDTH * LCD_HEIGHT];

/* Entries of all three kinds through the CAS, errors, the saved state. */
static void test_model(void)
{
    memset(&g, 0, sizeof g);
    tok_init();
    gfx_init(fb);
    g.cas = cas_init(arena, sizeof arena);
    CHECK(g.cas != NULL);
    cas_get_settings(g.cas, &g.cs);
    yvars_invalidate();
    graphx_init();
    CHECK(g3d_mem_init(g3mem, true) <= sizeof g3mem);
    CHECK(g3d_have_mem());
    /* z = x*y over [-5,5]^2, 24 x 24 cells */
    g3_set_text(0, 0, "x*y");
    g3_evaluate();
    g3_mesh_t *m = g3_mesh(0);
    CHECK(m && m->nu == 25 && m->nv == 25);
    if (m) {
        NEAR(m->w[0].z, 25, 1e-12);            /* (-5, -5) */
        NEAR(m->w[24].z, -25, 1e-12);          /* (5, -5) */
        NEAR(m->w[12 * 25 + 12].z, 0, 1e-12);  /* centre */
        CHECK(isnan(m->n[0].x));               /* z = 25 is 5 half-widths out: dropped */
    }
    CHECK(g3_mesh(1) == NULL); /* empty */
    /* a parametric surface with a missing part is an error; complete, it is drawn */
    g3.ent[1].kind = G3K_PSURF;
    g3_set_text(1, 0, "cos(u)");
    g3_evaluate();
    CHECK(g3_mesh(1) == NULL && g3_error(1)[0]);
    g3_set_text(1, 1, "sin(u)");
    g3_set_text(1, 2, "v");
    g3.vmax = 2;
    g3_evaluate();
    m = g3_mesh(1);
    CHECK(m && !g3_error(1)[0]);
    if (m) {
        NEAR(m->w[24].x, 1, 1e-12); /* u = 2π */
        NEAR(m->w[24 * 25].z, 2, 1e-12); /* v = vmax */
    }
    /* a curve, and an expression that does not compile */
    g3.ent[2].kind = G3K_CURVE;
    g3_set_text(2, 0, "t");
    g3_set_text(2, 1, "t^2");
    g3_set_text(2, 2, "1");
    g3.tsteps = 10;
    g3_set_text(3, 0, "x+*");
    g3_evaluate();
    m = g3_mesh(2);
    CHECK(m && m->nu == 11 && m->nv == 1);
    if (m)
        NEAR(m->w[10].y, 4 * M_PI * M_PI, 1e-9);
    CHECK(g3_mesh(3) == NULL && g3_error(3)[0]);
    /* a disabled entry is not drawn */
    g3.ent[0].enabled = false;
    g3_evaluate();
    CHECK(g3_mesh(0) == NULL);
    /* CAS variables are used (and re-read by the next evaluation) */
    char out[64];
    cas_eval(g.cas, "k:=3", out, sizeof out);
    g3.ent[0].enabled = true;
    g3_set_text(0, 0, "k*x");
    g3_evaluate();
    m = g3_mesh(0);
    CHECK(m != NULL);
    if (m)
        NEAR(m->w[24].z, 15, 1e-12);

    /* saved state round trip */
    static uint8_t buf[8192];
    pw_t w = {buf, sizeof buf, 0, false};
    g3.azim = 33, g3.elev = -12, g3.style = G3S_WIRE, g3.colormode = G3C_HEIGHT, g3.zmax = 7;
    persist_graph3d.save(&w);
    CHECK(!w.overflow);
    g3_defaults();
    pr_t r = {buf, w.len, 0, false};
    persist_graph3d.load(&r, persist_graph3d.version);
    CHECK(!r.err);
    CHECK(g3.azim == 33 && g3.elev == -12 && g3.style == G3S_WIRE && g3.colormode == G3C_HEIGHT && g3.zmax == 7);
    CHECK(g3.ent[1].kind == G3K_PSURF && g3.ent[2].kind == G3K_CURVE && g3_parts(1) == 3);
    gtext_to_editor(&g3.ent[0].ex[0], &gx_tmp_ed);
    ed_display(&gx_tmp_ed, out, sizeof out);
    CHECK(!strcmp(out, "k*x"));
    /* contour settings (version 2); a version 1 blob (without them) keeps their defaults */
    g3.contours = true, g3.map = true, g3.levels = 14;
    w.len = 0;
    persist_graph3d.save(&w);
    size_t v1len = w.len - 2 - 8; /* without u8 contours, u8 map, f64 levels */
    g3_defaults();
    pr_t r3 = {buf, w.len, 0, false};
    persist_graph3d.load(&r3, 2);
    CHECK(!r3.err && g3.contours && g3.map && g3_levels() == 14 && g3.azim == 33);
    g3_defaults();
    g3.azim = 1;
    pr_t r4 = {buf, v1len, 0, false};
    persist_graph3d.load(&r4, 1);
    CHECK(!r4.err && !g3.contours && !g3.map && g3_levels() == G3_LEVELS_DEF && g3.azim == 33);
    NEAR(g3_level(1), g3.zmin + (g3.zmax - g3.zmin) / 10, 1e-12);
    /* a truncated blob keeps the defaults for what is missing */
    g3_defaults();
    pr_t r2 = {buf, 10, 0, false};
    persist_graph3d.load(&r2, 1);
    CHECK(g3.azim == G3_AZIM_DEF && g3.xmax == 5);
}

int main(void)
{
    test_view();
    test_mesh();
    test_raster();
    test_scene();
    test_contours();
    test_model();
    printf("test_g3d: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
