/*
 * 3D graphing: surfaces z = f(x,y), parametric surfaces (x,y,z)(u,v) and
 * parametric space curves (x,y,z)(t), drawn by a software renderer.
 *
 * Modules (all app/g3d_*.c):
 *   g3d_view.c    camera: rotation (azimuth, elevation), weak perspective,
 *                 projection of normalised points to screen pixels + depth;
 *                 deterministic sin/cos so the WebAssembly build draws the
 *                 same pixels as the native simulator
 *   g3d_mesh.c    meshing: sampling an evaluator on a (u, v) grid or a t
 *                 range, normalisation into the view box [-1,1]^3
 *   g3d_raster.c  rasterizer: z-buffered flat-shaded triangles, z-tested and
 *                 plain lines, both clipped per pixel to the view box;
 *                 lighting and the height colour map; the scene renderer
 *                 (surfaces, mesh lines, curves, box, axes)
 *   g3d_model.c   entries, 3D window, view settings (global `g3`), CAS
 *                 compilation + evaluation, saved state (persist_graph3d),
 *                 work memory carved from the arena (g3d_mem_init)
 *   g3d_screen.c  the 3D graph screen (rotate, zoom, trace, VIEW menu), the
 *                 app entry, console command `g3d`
 *   g3d_edit.c    the entry editor (scr_g3d_eq) and the 3D WINDOW form
 *                 (scr_g3d_win)
 *
 * Coordinates. World values (x, y, z) are mapped to the normalised view box
 * [-1,1]^3 by the 3D window (n = (w - centre) / half-range); the box is drawn
 * as a cube whatever the window's aspect. The camera turns the box about the
 * world z axis by the azimuth and tilts it by the elevation (g3d_view.c), then
 * projects with a weak perspective (eye at distance G3_EYE). Depth values are
 * 1 / (G3_EYE - z_eye), which are affine in screen space (so they can be
 * interpolated linearly across a triangle) and grow towards the viewer.
 *
 * Contours. N levels (g3.levels) split the window's z range into N + 1 equal
 * bands; level k (1..N) is at normalised height k * 2 / (N + 1) - 1. The
 * renderer traces them per grid cell (marching squares) from the cached
 * nodes, for z = f(x,y) entries only: over the 3D surfaces instead of
 * the grid lines (g3.contours), and in the contour map (g3.map): the surfaces
 * seen from above, flat, with filled height-colour bands (shaded style) or the
 * curves alone (wireframe), a frame and a key of the level values.
 *
 * Memory. The meshes (per entry: world values in double for the trace,
 * normalised floats for rendering, projected points), the z-buffer and a
 * full-screen colour buffer for the rendered image live in one block carved
 * from the app arena at boot (g3d_mem_init, ~1.7 MB of the SDRAM). In the
 * hardware's small fallback arena (no SDRAM) the block is not allocated and
 * the screen says so.
 */
#ifndef G3D_H
#define G3D_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "graph.h"

/* ------------------------------------------------------------ entries + settings */
#define G3_NENT 6           /* entries Z1..Z6 */
#define G3_GRID_MIN 4       /* grid cells per side */
#define G3_GRID_MAX 64
#define G3_GRID_DEF 24
#define G3_TSTEPS_MIN 8     /* curve segments */
#define G3_TSTEPS_MAX 1000
#define G3_TSTEPS_DEF 200
#define G3_NODES_MAX ((G3_GRID_MAX + 1) * (G3_GRID_MAX + 1)) /* >= G3_TSTEPS_MAX + 1 */
#define G3_LEVELS_MIN 1     /* contour levels */
#define G3_LEVELS_MAX 30
#define G3_LEVELS_DEF 9

enum { G3K_FUNC = 0, G3K_PSURF, G3K_CURVE, G3K_N };   /* entry kinds */
enum { G3S_WIRE = 0, G3S_SHADE };                     /* rendering style */
enum { G3C_ENTRY = 0, G3C_HEIGHT };                   /* colour: entry colour / by height */

/* One entry: z(x,y) (ex[0]), or x/y/z of (u,v) or of t (ex[0..2]). */
typedef struct {
    gtext_t ex[3];
    uint8_t kind;   /* G3K_* */
    uint8_t color;  /* index into func_colors */
    bool enabled;
} g3_entry_t;

/* Everything the user sets (global `g3`, saved by persist_graph3d). */
typedef struct {
    g3_entry_t ent[G3_NENT];
    double xmin, xmax, ymin, ymax, zmin, zmax;  /* 3D window = the view box */
    double umin, umax, vmin, vmax;              /* parametric surface ranges */
    double tmin, tmax;                          /* space curve range */
    double grid, tsteps;  /* grid cells per side, curve segments (edited as numbers; integers) */
    double azim, elev;    /* view angles in degrees (elevation -90..90) */
    double zoom;          /* view scale (1 = box fits the screen) */
    uint8_t style;        /* G3S_* */
    uint8_t colormode;    /* G3C_* */
    bool axes, box;       /* draw the axes through the origin / the labelled box */
    bool contours;        /* level curves on the z = f(x,y) surfaces (3D view) */
    bool map;             /* the contour map view instead of the 3D view */
    double levels;        /* contour levels (edited as a number; an integer) */
} g3_state_t;
extern g3_state_t g3;

#define G3_AZIM_DEF (-60.0)
#define G3_ELEV_DEF 25.0

/* ------------------------------------------------------------ view (g3d_view.c) */
typedef struct { float x, y, z; } g3v_t;    /* normalised world point (NAN x: undefined) */
typedef struct { float sx, sy, d; } g3p_t;  /* screen pixel position and depth (NAN d: undefined) */

#define G3_EYE 6.0f /* eye distance from the box centre, in box half-widths */

typedef struct {
    float m[3][3];  /* rows: screen right, screen up, towards the viewer (world -> eye) */
    float cx, cy;   /* screen position of the box centre (pixels) */
    float scale;    /* pixels per normalised unit at depth 0 */
} g3_view_t;

double g3_sin_deg(double deg);  /* deterministic (no libm) sine / cosine of degrees */
double g3_cos_deg(double deg);
/* Set up the camera: azimuth about +z, then elevation; centre and scale in pixels. */
void g3_view_setup(g3_view_t *v, double azim_deg, double elev_deg, float cx, float cy, float scale);
/* world -> eye coordinates (right, up, towards viewer) */
void g3_view_eye(const g3_view_t *v, const g3v_t *w, float e[3]);
/* Project one point; an undefined point gives d = NAN. */
void g3_project(const g3_view_t *v, const g3v_t *w, g3p_t *p);
void g3_project_all(const g3_view_t *v, const g3v_t *w, g3p_t *p, int n);

/* ------------------------------------------------------------ meshes (g3d_mesh.c) */
/* Window of the view box: lo/hi per axis. */
typedef struct { double lo[3], hi[3]; } g3_box_t;
typedef struct { double x, y, z; } g3w_t;

/* A sampled entry. Surfaces: nu x nv nodes, node (i, j) at index j * nu + i for
 * parameter (u0 + i (u1-u0)/(nu-1), v0 + j (v1-v0)/(nv-1)); curves: nu nodes, nv = 1. */
typedef struct {
    uint8_t kind;
    int nu, nv;
    double u0, u1, v0, v1;
    g3w_t *w;    /* world values (NAN where undefined) */
    g3v_t *n;    /* normalised (x = NAN where undefined or far outside the box) */
    g3p_t *p;    /* projected (filled by the renderer) */
    double zlo, zhi; /* range of the finite z values (zlo > zhi if none) */
    int defined;     /* nodes kept for rendering (finite and within G3_FAR) */
} g3_mesh_t;

/* Evaluator: the point for parameters (u, v) (curves: v = 0); false = undefined. */
typedef bool (*g3_eval_fn)(void *user, double u, double v, double out[3]);
/* Sample f over the mesh's grid (m->kind, nu, nv, u0..v1 and the arrays set by
 * the caller), normalise with `box`; fills w, n, zlo/zhi, defined. */
void g3_mesh_sample(g3_mesh_t *m, g3_eval_fn f, void *user, const g3_box_t *box);
/* (part of g3_mesh_sample) undefined nodes next to defined ones moved onto the edge of f's domain */
void g3_mesh_edges(g3_mesh_t *m, g3_eval_fn f, void *user, const g3_box_t *box);
/* normalised coordinates of a world point (NAN x if undefined or |n| > G3_FAR) */
void g3_normalize(const g3_box_t *box, const g3w_t *w, g3v_t *n);
#define G3_FAR 3.0 /* points farther out than this (in box half-widths) are dropped; keeps every
                    * kept point in front of the eye (3 * sqrt 3 < G3_EYE) */

/* ------------------------------------------------------------ rasterizer (g3d_raster.c) */
/* A render target: rectangle (x0, y0, w, h) of a pixel buffer with `stride`
 * pixels per row (px[y * stride + x]), and a w*h z-buffer (NULL: no depth). */
typedef struct {
    uint16_t *px;
    float *zb;
    int x0, y0, w, h, stride;
} g3_target_t;

#define G3_ZBIAS 2e-3f /* depth slack of z-tested lines (lines on a surface win) */

void g3r_clear(const g3_target_t *t, uint16_t bg);
/* Filled triangle, z-buffered (if t->zb), clipped per pixel to the view box
 * when the world points are given (wa NULL: no clip). colour already mapped. */
void g3r_tri(const g3_target_t *t, const g3p_t *a, const g3p_t *b, const g3p_t *c, const g3v_t *wa,
             const g3v_t *wb, const g3v_t *wc, uint16_t color);
/* Line, endpoints inclusive; ztest: drawn where depth >= z-buffer - G3_ZBIAS
 * (the z-buffer is not written); thick: 2x2 pixels. Clipped like g3r_tri. */
void g3r_line(const g3_target_t *t, const g3p_t *a, const g3p_t *b, const g3v_t *wa, const g3v_t *wb,
              uint16_t color, bool ztest, bool thick);
uint16_t g3_shade(uint16_t c, float k); /* scale an RGB565 colour's brightness by k (0..1) */
uint16_t g3_heat(float h);              /* height colour map, h in [-1, 1]: blue..red */

/* Scene: what g3_render draws. */
typedef struct {
    const g3_view_t *view;
    g3_mesh_t *mesh[G3_NENT];  /* NULL: not drawn */
    uint16_t color[G3_NENT];   /* entry colours (unmapped) */
    uint8_t style, colormode;
    bool axes, box;
    int stride;                /* draw every stride-th grid line (coarse preview while dragging) */
    double origin[3];          /* normalised position of the world origin (axes) */
    uint16_t (*map)(uint16_t); /* colour mapping (theme), NULL = identity */
    int levels;                /* contour levels on z = f(x,y) surfaces (0: none; > 0 in the map) */
    bool contour_map;          /* the contour map (view: the top view, azimuth 0, elevation 90) */
    g3_box_t win;              /* the 3D window (the map places nodes by their world x, y) */
} g3_scene_t;
/* Render the scene into t (clears it first). Projects every mesh node into m->p
 * (in the map: the z = f(x,y) meshes only, flat). Returns the contour segments drawn. */
int g3_render(const g3_target_t *t, const g3_scene_t *s);

/* ------------------------------------------------------------ model (g3d_model.c) */
size_t g3d_mem_init(void *mem, bool big); /* from app_init: bytes taken from the arena */
bool g3d_have_mem(void);
void g3_defaults(void);
void g3_invalidate(void);        /* entries or window changed: re-evaluate before the next render */
bool g3_valid(void);
void g3_evaluate(void);          /* compile, sample and free every enabled entry */
g3_mesh_t *g3_mesh(int i);       /* entry i's mesh if it is drawn (enabled, compiled), else NULL */
const char *g3_error(int i);     /* compile/evaluation error of entry i ("" if none) */
bool g3_entry_empty(int i);
int g3_parts(int i);             /* expressions of entry i: 1 (z) or 3 (x, y, z) */
const char *g3_part_label(int i, int part, char *buf, size_t n); /* "z1(x,y)", "x2(u,v)", "y3(t)" */
void g3_box(g3_box_t *b);        /* the 3D window as a box */
int g3_grid(void);               /* grid cells per side (rounded, clamped) */
int g3_tsteps(void);
int g3_levels(void);             /* contour levels (rounded, clamped) */
double g3_level(int k);          /* world z of level k (1..g3_levels()) in the window's z range */
void g3_set_text(int i, int part, const char *text); /* tests / console */
void g3d_state(void (*kv)(const char *, const char *, void *), void *user);
extern const struct persist_def persist_graph3d; /* saved state (g3d_model.c) */
uint32_t g3_eval_us(void);       /* duration of the last g3_evaluate (us, hal_cycles) */

extern float *g3d_zbuf;     /* LCD_WIDTH x BODY_H depths (NULL without the work memory) */
extern uint16_t *g3d_image; /* LCD_WIDTH x LCD_HEIGHT render image (rows BODY_Y.. used) */

/* ------------------------------------------------------------ screens */
extern const screen_t scr_g3d, scr_g3d_eq, scr_g3d_win;
void g3d_open(void);
int g3d_console(int argc, char **argv);  /* console `g3d ...` */
void g3d_trace_state(void (*kv)(const char *, const char *, void *), void *user);
void g3d_view_menu(void);   /* soft key "view": the VIEW menu */
void g3d_trace_key(void);   /* soft key "trace" from the other 3D screens */
void g3d_redraw(void);      /* settings changed: re-render on the next draw */
extern const softkeys_t g3d_softkeys; /* graph screen bar (the other screens share its first row) */
void g3d_eq_state(void (*kv)(const char *, const char *, void *), void *user);

#endif
