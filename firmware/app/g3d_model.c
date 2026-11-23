/*
 * 3D graphing model (see g3d.h): the entries Z1..Z6, the 3D window and the
 * view settings (global `g3`), their evaluation into meshes through the CAS,
 * the saved state, and the work memory.
 *
 * Entries. Each entry is z = f(x,y) (one expression), a parametric surface
 * x(u,v), y(u,v), z(u,v) or a space curve x(t), y(t), z(t) (three), stored as
 * compact token strings (gtext_t, like the 2D graph's PAR entries) and edited
 * through an editor_t by the entry screen (g3d_edit.c).
 *
 * Evaluation (g3_evaluate). For every enabled entry the expressions are
 * compiled with cas_compile_vars in their parameters ({x, y}, {u, v} or {t};
 * other names take their stored CAS values), sampled on the grid
 * (g3_mesh_sample: (grid + 1)^2 nodes, curves tsteps + 1) with
 * cas_fn_eval_vars, and freed again right away. Nothing compiled is kept
 * between calls, so a CAS reset (MEM, casreset) or a simulator resume can
 * never leave a dangling cas_fn here; the graph screen re-evaluates on every
 * fresh visit instead, which also picks up changed CAS variables. Rotating,
 * zooming and tracing only use the cached nodes.
 *
 * Memory (g3d_mem_init, from app_init): one block from the arena holding, per
 * entry, G3_NODES_MAX nodes of world values (double), normalised values and
 * projected points (48 bytes per node, ~200 KB per entry), then the z-buffer
 * (float per body pixel, 258 KB) and a full-screen RGB565 image (150 KB) the
 * screen renders into; 1.6 MB in all. Not allocated in the small fallback
 * arena: g3d_have_mem() is false and the screen shows a message.
 *
 * Saved state: blob "graph3d" (persist_graph3d, layout at g3_save).
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "anim.h"
#include "g3d.h"
#include "hal.h"
#include "session.h"

g3_state_t g3;

static g3_mesh_t meshes[G3_NENT];
static bool drawn[G3_NENT];         /* entry evaluated and drawable */
static char errs[G3_NENT][48];      /* compile errors */
static bool valid;                  /* meshes match the entries and window */
static bool have_mem;
static uint32_t eval_us;
/* Animation (anim.h): the free letter of the entries that is compiled as an
 * extra variable (var_cache, found by g3_anim_var), its value, and a coarser
 * grid for animation frames (anim_grid, 0 = the setting). */
static char var_cache;
static bool var_valid;
static double anim_val;
static bool anim_val_set;
static char anim_val_of;        /* the letter anim_val belongs to */
static int anim_grid;
float *g3d_zbuf;                    /* LCD_WIDTH x BODY_H depths (g3d_screen.c) */
uint16_t *g3d_image;                /* LCD_WIDTH x LCD_HEIGHT pixels (rows BODY_Y.. used) */

/* ------------------------------------------------------------ memory */
/* Carve the meshes, z-buffer and image from `mem` (8-byte aligned pieces);
 * returns the bytes used (0 in the small arena: 3D graphing unavailable). */
size_t g3d_mem_init(void *mem, bool big)
{
    have_mem = false;
    g3d_zbuf = NULL;
    g3d_image = NULL;
    memset(meshes, 0, sizeof meshes);
    g3_defaults();
    if (!big)
        return 0;
    uint8_t *a = (uint8_t *)(((uintptr_t)mem + 7) & ~(uintptr_t)7);
    for (int i = 0; i < G3_NENT; i++) {
        meshes[i].w = (g3w_t *)a;
        a += sizeof(g3w_t) * G3_NODES_MAX;
        meshes[i].n = (g3v_t *)a;
        a += sizeof(g3v_t) * G3_NODES_MAX;
        meshes[i].p = (g3p_t *)a;
        a += sizeof(g3p_t) * G3_NODES_MAX;
    }
    g3d_zbuf = (float *)a;
    a += sizeof(float) * LCD_WIDTH * BODY_H;
    g3d_image = (uint16_t *)a;
    a += sizeof(uint16_t) * LCD_WIDTH * LCD_HEIGHT;
    have_mem = true;
    return (size_t)(a - (uint8_t *)mem);
}

bool g3d_have_mem(void) { return have_mem; }

/* ------------------------------------------------------------ settings */
/* Defaults: empty entries (z = f(x,y), enabled, colours in Y= order), window
 * [-5,5]^3, u, v and t over [0, 2π], a 24 x 24 grid, 200 curve segments, the
 * standard view (azimuth -60°, elevation 25°), shaded, entry colours, axes
 * and box shown. */
void g3_defaults(void)
{
    memset(&g3, 0, sizeof g3);
    for (int i = 0; i < G3_NENT; i++) {
        g3.ent[i].kind = G3K_FUNC;
        g3.ent[i].color = (uint8_t)(i % NCOLORS);
        g3.ent[i].enabled = true;
    }
    g3.xmin = g3.ymin = g3.zmin = -5;
    g3.xmax = g3.ymax = g3.zmax = 5;
    g3.umin = g3.vmin = g3.tmin = 0;
    g3.umax = g3.vmax = g3.tmax = 2 * M_PI;
    g3.grid = G3_GRID_DEF;
    g3.tsteps = G3_TSTEPS_DEF;
    g3.azim = G3_AZIM_DEF;
    g3.elev = G3_ELEV_DEF;
    g3.zoom = 1;
    g3.style = G3S_SHADE;
    g3.colormode = G3C_ENTRY;
    g3.axes = g3.box = true;
    g3.contours = g3.map = false;
    g3.levels = G3_LEVELS_DEF;
    valid = false;
}

void g3_invalidate(void) { valid = false, var_valid = false; }
bool g3_valid(void) { return valid; }
uint32_t g3_eval_us(void) { return eval_us; }

static int clampi(double v, int lo, int hi)
{
    if (!(v >= lo))
        return lo;
    if (v > hi)
        return hi;
    return (int)lround(v);
}
int g3_grid(void) { return clampi(g3.grid, G3_GRID_MIN, G3_GRID_MAX); }
/* grid of the next evaluation: the setting, or the animation's coarser one */
static int eval_grid(void)
{
    int n = g3_grid();
    return anim_grid >= G3_GRID_MIN && anim_grid < n ? anim_grid : n;
}
int g3_tsteps(void) { return clampi(g3.tsteps, G3_TSTEPS_MIN, G3_TSTEPS_MAX); }
int g3_levels(void) { return clampi(g3.levels, G3_LEVELS_MIN, G3_LEVELS_MAX); }
double g3_level(int k) { return g3.zmin + (g3.zmax - g3.zmin) * k / (g3_levels() + 1); }

void g3_box(g3_box_t *b)
{
    b->lo[0] = g3.xmin, b->hi[0] = g3.xmax;
    b->lo[1] = g3.ymin, b->hi[1] = g3.ymax;
    b->lo[2] = g3.zmin, b->hi[2] = g3.zmax;
}

/* ------------------------------------------------------------ entries */
int g3_parts(int i) { return g3.ent[i].kind == G3K_FUNC ? 1 : 3; }

bool g3_entry_empty(int i)
{
    for (int p = 0; p < g3_parts(i); p++)
        if (!gtext_empty(&g3.ent[i].ex[p]))
            return false;
    return true;
}

/* Label of expression `part` of entry i: z1(x,y); x2(u,v) y2(u,v) z2(u,v);
 * x3(t) y3(t) z3(t). */
const char *g3_part_label(int i, int part, char *buf, size_t n)
{
    static const char xyz[] = "xyz";
    const g3_entry_t *e = &g3.ent[i];
    if (e->kind == G3K_FUNC)
        snprintf(buf, n, "z%d(x,y)", i + 1);
    else
        snprintf(buf, n, "%c%d(%s)", xyz[part % 3], i + 1, e->kind == G3K_PSURF ? "u,v" : "t");
    return buf;
}

/* Set an expression from display text (console `g3d set`, unit tests). */
void g3_set_text(int i, int part, const char *text)
{
    if (i < 0 || i >= G3_NENT || part < 0 || part > 2)
        return;
    ed_set_text(&gx_tmp_ed, text);
    if (!gtext_from_editor(&g3.ent[i].ex[part], &gx_tmp_ed))
        g3.ent[i].ex[part].len = 0;
    valid = false, var_valid = false;
}

g3_mesh_t *g3_mesh(int i) { return i >= 0 && i < G3_NENT && valid && drawn[i] ? &meshes[i] : NULL; }
const char *g3_error(int i) { return i >= 0 && i < G3_NENT ? errs[i] : ""; }

/* ------------------------------------------------------------ evaluation */
typedef struct {
    const cas_fn *fn[3];
    uint8_t kind;
    int ai;        /* index of the animation variable in the values (-1: none) */
    double aval;   /* its value */
} evctx_t;

/* g3_eval_fn of an entry: z(x,y) gives (u, v, f(u,v)); the others evaluate
 * their three compiled parts at (u, v) or t = u. NAN results are filtered by
 * g3_mesh_sample. */
static bool eval_entry(void *user, double u, double v, double out[3])
{
    const evctx_t *c = user;
    double vals[3] = {u, v, 0};
    if (c->ai >= 0)
        vals[c->ai] = c->aval;
    if (c->kind == G3K_FUNC) {
        out[0] = u, out[1] = v;
        out[2] = cas_fn_eval_vars(c->fn[0], vals);
    } else {
        for (int k = 0; k < 3; k++)
            out[k] = cas_fn_eval_vars(c->fn[k], vals);
    }
    return true;
}

/* Compile, sample and free entry i (see the file comment); false with errs[i]
 * set if an expression is missing or does not compile. */
static bool eval_one(int i, const g3_box_t *box)
{
    static const char *const vxy[] = {"x", "y"}, *const vuv[] = {"u", "v"}, *const vt[] = {"t"};
    g3_entry_t *e = &g3.ent[i];
    g3_mesh_t *m = &meshes[i];
    evctx_t c = {{NULL, NULL, NULL}, e->kind, -1, 0};
    cas_fn *fn[3] = {NULL, NULL, NULL};
    int np = g3_parts(i);
    const char *const *own = e->kind == G3K_FUNC ? vxy : e->kind == G3K_PSURF ? vuv : vt;
    int nv = e->kind == G3K_CURVE ? 1 : 2;
    /* the animation variable (if any, and not one of the entry's own) */
    const char *vars[3] = {own[0], nv > 1 ? own[1] : NULL, NULL};
    char av[2] = {g3_anim_var(), 0};
    if (av[0] && strcmp(av, own[0]) && (nv < 2 || strcmp(av, own[1]))) {
        vars[nv] = av;
        c.ai = nv++;
        c.aval = g3_anim_value();
    }
    char cas[GTOK * 32];
    bool ok = true;
    for (int p = 0; p < np && ok; p++) {
        if (gtext_empty(&e->ex[p])) {
            snprintf(errs[i], sizeof errs[i], "%s", e->kind == G3K_FUNC ? "empty" : "x, y and z needed");
            ok = false;
            break;
        }
        gtext_to_editor(&e->ex[p], &gx_tmp_ed);
        ed_cas(&gx_tmp_ed, cas, sizeof cas);
        if (cas_compile_vars(g.cas, cas, vars, nv, &fn[p]) != CAS_OK) {
            const char *msg = cas_last_error(g.cas);
            snprintf(errs[i], sizeof errs[i], "%s", msg && *msg ? msg : "error");
            fn[p] = NULL;
            ok = false;
        }
        c.fn[p] = fn[p];
    }
    if (ok) {
        m->kind = e->kind;
        if (e->kind == G3K_CURVE) {
            int ts = g3_tsteps(); /* animation frames: as coarse as the grid */
            if (anim_grid >= G3_GRID_MIN && anim_grid < g3_grid())
                ts = ts * anim_grid / g3_grid() < G3_TSTEPS_MIN ? G3_TSTEPS_MIN : ts * anim_grid / g3_grid();
            m->nu = ts + 1, m->nv = 1;
            m->u0 = g3.tmin, m->u1 = g3.tmax, m->v0 = m->v1 = 0;
        } else {
            m->nu = m->nv = eval_grid() + 1;
            if (e->kind == G3K_FUNC)
                m->u0 = g3.xmin, m->u1 = g3.xmax, m->v0 = g3.ymin, m->v1 = g3.ymax;
            else
                m->u0 = g3.umin, m->u1 = g3.umax, m->v0 = g3.vmin, m->v1 = g3.vmax;
        }
        g3_mesh_sample(m, eval_entry, &c, box);
    }
    for (int p = np - 1; p >= 0; p--)
        if (fn[p])
            cas_fn_free(g.cas, fn[p]);
    return ok;
}

/* Evaluate every enabled, non-empty entry into its mesh (Y1..Y6 are defined
 * in the CAS first, so entries may call them). Records the time taken. */
void g3_evaluate(void)
{
    if (!have_mem)
        return;
    uint32_t t0 = hal_cycles();
    g3_box_t box;
    g3_box(&box);
    yvars_sync();
    for (int i = 0; i < G3_NENT; i++) {
        errs[i][0] = 0;
        drawn[i] = false;
        if (!g3.ent[i].enabled || g3_entry_empty(i))
            continue;
        drawn[i] = eval_one(i, &box);
    }
    valid = true;
    eval_us = (hal_cycles() - t0) / hal_cycles_per_us();
}

/* ------------------------------------------------------------ animation */
static void add_free(char c, void *u)
{
    uint32_t *mask = u;
    if (c >= 'a' && c <= 'z')
        *mask |= 1u << (c - 'a');
}

/* anim.h: the letter animated in 3D: a single lower-case letter in the
 * enabled entries that is not that entry's own variable (x y, u v, t), not e
 * or i; t if it qualifies, else the first in alphabetical order. 0 if none.
 * Like a 2D slider parameter it is an input of the evaluation: it starts at
 * the value stored in the CAS (a 2D animation of t leaves t:=value), else at
 * the animation's start (0), so the surface is drawn even before playing.
 * Cached until the entries change (g3_invalidate). */
char g3_anim_var(void)
{
    if (var_valid)
        return var_cache;
    var_cache = 0;
    uint32_t all = 0;
    char cas[GTOK * 32];
    for (int i = 0; i < G3_NENT; i++) {
        const g3_entry_t *e = &g3.ent[i];
        if (!e->enabled || g3_entry_empty(i))
            continue;
        uint32_t m = 0;
        for (int p = 0; p < g3_parts(i); p++) {
            gtext_to_editor(&e->ex[p], &gx_tmp_ed);
            ed_cas(&gx_tmp_ed, cas, sizeof cas);
            gx_scan_letters(cas, add_free, &m);
        }
        const char *own = e->kind == G3K_FUNC ? "xy" : e->kind == G3K_PSURF ? "uv" : "t";
        for (const char *o = own; *o; o++)
            m &= ~(1u << (*o - 'a'));
        all |= m;
    }
    all &= ~(1u << ('e' - 'a') | 1u << ('i' - 'a'));
    for (int k = -1; k < 26 && !var_cache; k++) {
        char c = k < 0 ? 't' : (char)('a' + k);
        if ((all & 1u << (c - 'a')) && !(k >= 0 && c == 't'))
            var_cache = c;
    }
    var_valid = true;
    if (var_cache && (!anim_val_set || anim_val_of != var_cache)) {
        char nm[2] = {var_cache, 0}, err[32];
        double v;
        const anim_cfg_t *c = anim_cfg(var_cache, false, 0, 0);
        anim_val = eval_number(nm, &v, err, sizeof err) == 0 ? v : c ? c->start : 0;
        anim_val_of = var_cache;
        anim_val_set = true;
    }
    return var_cache;
}

double g3_anim_value(void) { return anim_val; }

/* anim.h: value of the animated variable for the next evaluation, and the
 * grid of animation frames (0: the setting). Invalidates the meshes. */
void g3_anim_set(const char *name, double value, int grid)
{
    anim_val = value;
    anim_val_set = true;
    anim_val_of = name && name[0] ? name[0] : anim_val_of;
    anim_grid = grid;
    valid = false;
}

/* the animated variable's value kept aside (a graph cell drawn at its own t) and back */
void g3_anim_keep(bool restore)
{
    static double v;
    static char of;
    static bool set;
    if (!restore) {
        v = anim_val, of = anim_val_of, set = anim_val_set;
        return;
    }
    anim_val = v, anim_val_of = of, anim_val_set = set, anim_grid = 0;
    valid = var_valid = false;
}

/* ------------------------------------------------------------ console state */
/* `state` keys: g3d_eN_P (expression P of entry N, display text), g3d_eN_kind
 * (z/surf/curve), g3d_eN_on, g3d_errN, g3d_azim, g3d_elev, g3d_zoom,
 * g3d_style (wire/shade), g3d_color (entry/height), g3d_axes, g3d_box,
 * g3d_grid, g3d_contours, g3d_map, g3d_levels, g3d_window (xmin,xmax,ymin,ymax,zmin,zmax), g3d_valid, g3d_drawn
 * (number of drawn entries), plus the screens' keys. */
void g3d_state(void (*kv)(const char *, const char *, void *), void *user)
{
    static const char *const kinds[G3K_N] = {"z", "surf", "curve"};
    char b[ED_MAX * 4], k[24];
    int nd = 0;
    for (int i = 0; i < G3_NENT; i++) {
        for (int p = 0; p < g3_parts(i); p++) {
            gtext_to_editor(&g3.ent[i].ex[p], &gx_tmp_ed);
            ed_display(&gx_tmp_ed, b, sizeof b);
            snprintf(k, sizeof k, "g3d_e%d_%d", i + 1, p + 1);
            kv(k, b, user);
        }
        snprintf(k, sizeof k, "g3d_e%d_kind", i + 1);
        kv(k, kinds[g3.ent[i].kind % G3K_N], user);
        snprintf(k, sizeof k, "g3d_e%d_on", i + 1);
        kv(k, g3.ent[i].enabled ? "1" : "0", user);
        snprintf(k, sizeof k, "g3d_err%d", i + 1);
        kv(k, errs[i], user);
        nd += g3_mesh(i) != NULL;
    }
    snprintf(b, sizeof b, "%g", g3.azim);
    kv("g3d_azim", b, user);
    snprintf(b, sizeof b, "%g", g3.elev);
    kv("g3d_elev", b, user);
    snprintf(b, sizeof b, "%g", g3.zoom);
    kv("g3d_zoom", b, user);
    kv("g3d_style", g3.style == G3S_WIRE ? "wire" : "shade", user);
    kv("g3d_color", g3.colormode == G3C_HEIGHT ? "height" : "entry", user);
    kv("g3d_axes", g3.axes ? "1" : "0", user);
    kv("g3d_box", g3.box ? "1" : "0", user);
    snprintf(b, sizeof b, "%d", g3_grid());
    kv("g3d_grid", b, user);
    kv("g3d_contours", g3.contours ? "1" : "0", user);
    kv("g3d_map", g3.map ? "1" : "0", user);
    snprintf(b, sizeof b, "%d", g3_levels());
    kv("g3d_levels", b, user);
    snprintf(b, sizeof b, "%g,%g,%g,%g,%g,%g", g3.xmin, g3.xmax, g3.ymin, g3.ymax, g3.zmin, g3.zmax);
    kv("g3d_window", b, user);
    kv("g3d_valid", valid ? "1" : "0", user);
    snprintf(b, sizeof b, "%d", nd);
    kv("g3d_drawn", b, user);
    g3d_eq_state(kv, user);
    g3d_trace_state(kv, user);
}

/* ------------------------------------------------------------ persistence */
#define NVALS 17
static double *vals_ptr(int k)
{
    double *v[NVALS] = {&g3.xmin, &g3.xmax, &g3.ymin, &g3.ymax, &g3.zmin, &g3.zmax, &g3.umin, &g3.umax, &g3.vmin,
                        &g3.vmax, &g3.tmin, &g3.tmax, &g3.grid, &g3.tsteps, &g3.azim, &g3.elev, &g3.zoom};
    return v[k];
}

/* Blob "graph3d" version 1:
 *   u8 entries (6), per entry: u8 kind, u8 colour, u8 enabled, u8 parts (3), parts x str
 *       (ed_serialize text, so token ids may change between firmware versions)
 *   u8 count (17), f64 xmin xmax ymin ymax zmin zmax umin umax vmin vmax tmin tmax
 *       grid tsteps azim elev zoom
 *   u8 style, u8 colour mode, u8 axes, u8 box
 * version 2 appends: u8 contours, u8 contour map, f64 levels (version 1 blobs
 * load with the contours off and the default levels) */
static void g3_save(pw_t *w)
{
    char b[GTOK * 24];
    pw_u8(w, G3_NENT);
    for (int i = 0; i < G3_NENT; i++) {
        pw_u8(w, g3.ent[i].kind);
        pw_u8(w, g3.ent[i].color);
        pw_u8(w, g3.ent[i].enabled);
        pw_u8(w, 3);
        for (int p = 0; p < 3; p++) {
            gtext_to_editor(&g3.ent[i].ex[p], &gx_tmp_ed);
            ed_serialize(&gx_tmp_ed, b, sizeof b);
            pw_str(w, b);
        }
    }
    pw_u8(w, NVALS);
    for (int k = 0; k < NVALS; k++)
        pw_f64(w, *vals_ptr(k));
    pw_u8(w, g3.style);
    pw_u8(w, g3.colormode);
    pw_u8(w, g3.axes);
    pw_u8(w, g3.box);
    pw_u8(w, g3.contours);
    pw_u8(w, g3.map);
    pw_f64(w, g3.levels);
}

/* Restore from the defaults: entries only if they read without error, the
 * numbers only if all are finite and the ranges non-empty, enumerations
 * reduced to their ranges. */
static void g3_load(pr_t *r, unsigned version)
{
    g3_defaults();
    char b[GTOK * 24];
    unsigned ne = pr_u8(r);
    for (unsigned i = 0; i < ne && !r->err; i++) {
        g3_entry_t e;
        memset(&e, 0, sizeof e);
        e.kind = (uint8_t)(pr_u8(r) % G3K_N);
        e.color = (uint8_t)(pr_u8(r) % NCOLORS);
        e.enabled = pr_u8(r) != 0;
        unsigned np = pr_u8(r);
        for (unsigned p = 0; p < np; p++) {
            pr_str(r, b, sizeof b);
            ed_deserialize(&gx_tmp_ed, b);
            if (p < 3 && !gtext_from_editor(&e.ex[p], &gx_tmp_ed))
                e.ex[p].len = 0;
        }
        if (!r->err && i < G3_NENT)
            g3.ent[i] = e;
    }
    unsigned nv = pr_u8(r);
    double v[NVALS];
    for (unsigned k = 0; k < nv; k++) {
        double x = pr_f64(r);
        if (k < NVALS)
            v[k] = x;
    }
    if (!r->err && nv >= NVALS) {
        bool ok = true;
        for (int k = 0; k < NVALS; k++)
            ok &= isfinite(v[k]) != 0;
        if (ok && v[0] < v[1] && v[2] < v[3] && v[4] < v[5] && v[6] < v[7] && v[8] < v[9] && v[10] < v[11] &&
            v[16] > 0)
            for (int k = 0; k < NVALS; k++)
                *vals_ptr(k) = v[k];
    }
    unsigned st = pr_u8(r), cm = pr_u8(r), ax = pr_u8(r), bx = pr_u8(r);
    if (!r->err) {
        g3.style = st == G3S_WIRE ? G3S_WIRE : G3S_SHADE;
        g3.colormode = cm == G3C_HEIGHT ? G3C_HEIGHT : G3C_ENTRY;
        g3.axes = ax != 0;
        g3.box = bx != 0;
    }
    if (version >= 2) {
        unsigned co = pr_u8(r), mp = pr_u8(r);
        double lv = pr_f64(r);
        if (!r->err) {
            g3.contours = co != 0;
            g3.map = mp != 0;
            g3.levels = lv >= G3_LEVELS_MIN && lv <= G3_LEVELS_MAX ? round(lv) : G3_LEVELS_DEF;
        }
    }
    if (g3.elev > 90)
        g3.elev = 90;
    if (g3.elev < -90)
        g3.elev = -90;
    valid = false;
}

/* MEM > Reset all */
static void g3_reset(void) { g3_defaults(); }

const persist_def_t persist_graph3d = {"graph3d", 2, g3_save, g3_load, g3_reset};
