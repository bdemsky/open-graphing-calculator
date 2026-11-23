/*
 * Geometry app, core side (interface in app/geo_core.h): what stays in
 * internal flash when the app itself is the "geometry" code module
 * (modules/geometry, runs in place from the QSPI).
 *
 *   - scr_geo, the APPS entry, the soft-key bar and the menus: each callback
 *     forwards to the module's ops between modx_call_begin / modx_call_end;
 *     without the module, APPS > Geometry says why ("Install the Geometry
 *     module (Memory > Modules).") like the other modules;
 *   - the session blob "geo": the bytes are kept here while the module is not
 *     bound (geo_core_unbind takes them back from the module before every
 *     store change) and given to it when it attaches, so the construction
 *     survives installs, removals and the time before the first use;
 *   - the GRAPH overlay (View > Show on GRAPH), read from the kept bytes
 *     whether it is on, so a construction shown on GRAPH binds the module
 *     only when GRAPH draws it;
 *   - the prompt line's editor (the screen's editor for menus and the
 *     clipboard), files on the microSD card (/CALC/GEO/NAME.GEO: "GEO1" + the
 *     blob payload) and File > Open;
 *   - geo_core_table(): the services the module calls.
 *
 * The core tier can link the module's sources into the firmware
 * (CONFIG_GEOMETRY_BUILTIN, off by default: the rollback image has USB
 * instead) and binds them the same way (app/modx.c). Without the app the
 * blob is still kept as bytes, so a construction survives a rollback.
 *
 * State: the statics below (the kept blob and the file image in DTCM on the
 * STM32, where the module's 17 KB used to be). Main loop only.
 */
#include "geo_core.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "console.h"
#include "editor.h"
#include "fat.h"
#include "gfx.h"
#include "graph_overlay.h"
#include "modx.h"
#include "registry.h"
#include "theme.h"
#include "ui.h"

#if defined(CONFIG_CONTENT_QSPI) || defined(CONFIG_GEOMETRY_BUILTIN)
#define HAVE_GEO 1
#else
#define HAVE_GEO 0
#endif
#if defined(PLATFORM_STM32)
#define GEO_DTCM __attribute__((section(".dtcm_bss")))
#else
#define GEO_DTCM
#endif

#define GEO_DIR "/CALC/GEO"
/* blob bound: view (25 bytes) + count + 96 objects of at most 4 + 12 + 2 + 2 + 5 + 16 bytes */
#define BLOB_MAX (26 + 96 * 41)
#define GEO_OPT_ONGRAPH 16 /* the view option "Show on GRAPH" (modules/geometry/geo.h GO_ONGRAPH) */

static const geo_ops_t *ops;                 /* the attached module's entry points, NULL: none */
static GEO_DTCM uint8_t blob[BLOB_MAX];      /* the kept session blob */
static size_t blob_len;
static bool blob_valid;
#if HAVE_GEO
static GEO_DTCM uint8_t fbuf[4 + BLOB_MAX];  /* file image */
#endif
static editor_t ed;                          /* the prompt line */
static bool ed_on;

/* The bound module with its ops attached (binding it on first use), or NULL. */
static modx_mod_t *geo_mod(void)
{
    if (!HAVE_GEO)
        return NULL;
    modx_mod_t *m = modx_get(GEO_MODULE);
    return m && ops ? m : NULL;
}
/* bound already (never binds) */
static modx_mod_t *geo_peek(void)
{
    modx_mod_t *m = HAVE_GEO ? modx_peek(GEO_MODULE) : NULL;
    return m && ops ? m : NULL;
}
#define CALL(m, stmt)                         \
    do {                                      \
        void *c_ = modx_call_begin(m);        \
        stmt;                                 \
        modx_call_end(c_);                    \
    } while (0)

/* ------------------------------------------------------------ the kept construction */

static void load_blob(modx_mod_t *m)
{
    pr_t r = {blob, blob_len, 0, false};
    if (blob_valid && blob_len)
        CALL(m, ops->load(&r));
    else
        CALL(m, ops->reset());
}

#if HAVE_GEO
/* The module's init: keep its ops and give it the construction. Runs inside
 * the bind (MODX_RW is the module's block already). */
static void core_attach(const geo_ops_t *o)
{
    ops = o && o->size >= sizeof(geo_ops_t) ? o : NULL;
    if (!ops)
        return;
    pr_t r = {blob, blob_len, 0, false};
    if (blob_valid && blob_len)
        ops->load(&r);
    else
        ops->reset();
}
#endif

void geo_core_unbind(bool keep)
{
    modx_mod_t *m = keep ? geo_peek() : NULL;
    if (m) { /* the code is still in place: every store write unbinds first */
        pw_t w = {blob, sizeof blob, 0, false};
        CALL(m, ops->save(&w));
        blob_len = w.overflow ? 0 : w.len;
        blob_valid = !w.overflow;
    }
    ops = NULL;
    ed_on = false;
}

static void gp_save(pw_t *w)
{
    modx_mod_t *m = geo_peek();
    if (m)
        CALL(m, ops->save(w));
    else if (blob_valid)
        pw_bytes(w, blob, blob_len);
}
static void gp_load(pr_t *r, unsigned version)
{
    (void)version;
    size_t n = pr_left(r);
    blob_len = n < sizeof blob ? n : 0; /* too big: not ours */
    blob_valid = pr_bytes(r, blob, blob_len) && blob_len;
    modx_mod_t *m = geo_peek();
    if (m)
        load_blob(m);
}
static void gp_reset(void)
{
    blob_valid = false, blob_len = 0;
    modx_mod_t *m = geo_peek();
    if (m)
        CALL(m, ops->reset());
}
/* Blob "geo" v1: u8 view options, f64 origin x, y, scale, the construction (modules/geometry/geo_io.c). */
const persist_def_t persist_geo = {"geo", 1, gp_save, gp_load, gp_reset};

/* ------------------------------------------------------------ screen */

#if HAVE_GEO
static void geo_back(void)
{
    ui_returning = true;
    ui_switch(&scr_geo);
    ui_returning = false;
}
#endif

static void sc_enter(void)
{
    modx_mod_t *m = geo_mod();
    if (m)
        CALL(m, ops->enter(ui_returning));
}
static void sc_draw(void)
{
    modx_mod_t *m = geo_mod();
    if (m) {
        CALL(m, ops->draw());
        return;
    }
    ui_cursor_hide(); /* the module went away while shown: say why */
    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, TH_BG);
    gfx_text(&font_small, 8, BODY_Y + 8, modx_why(GEO_MODULE), TH_FG, -1);
}
static void sc_key(int key, int mod)
{
    modx_mod_t *m = geo_mod();
    if (m)
        CALL(m, ops->key(key, mod));
    else
        ui_switch(ui_home());
}
static void sc_touch(const touch_event_t *ev)
{
    modx_mod_t *m = geo_mod();
    geo_touch_t t = {ev->type, ev->x, ev->y, ev->x0, ev->y0, ev->dx, ev->dy, (double)ev->scale};
    if (m)
        CALL(m, ops->touch(&t));
}
static editor_t *sc_editor(void) { return ed_on && geo_peek() ? &ed : NULL; }

static void softkey(int i)
{
    modx_mod_t *m = geo_mod();
    if (m)
        CALL(m, ops->softkey(i));
}
static void sk0(void) { softkey(0); }
static void sk1(void) { softkey(1); }
static void sk2(void) { softkey(2); }
static void sk3(void) { softkey(3); }
static void sk4(void) { softkey(4); }
static void sk5(void) { softkey(5); }
static void sk6(void) { softkey(6); }
static void sk7(void) { softkey(7); }
static void sk8(void) { softkey(8); }
static void sk9(void) { softkey(9); }
static const softkeys_t geo_soft = {
    {{"move", sk0}, {"points", sk1}, {"draw", sk2}, {"constr", sk3}, {"measure", sk4}},
    {{"undo", sk5}, {"delete", sk6}, {"view", sk7}, {"file", sk8}, {"hide", sk9}},
};

const screen_t scr_geo = {"geometry", "GEOMETRY", sc_enter, sc_draw, sc_key, NULL, sc_editor, sc_touch,
                          "geometry", &geo_soft};

static void geo_open(void)
{
    if (!geo_mod()) {
        ui_message("GEOMETRY NOT INSTALLED", modx_why(GEO_MODULE));
        return;
    }
    ui_switch(&scr_geo);
}
const app_def_t app_geometry = {"geometry", "Geometry", geo_open, {{0}}, "geometry"};

#if HAVE_GEO
/* ------------------------------------------------------------ menus */
/* One geometry menu is open at a time: its items come from the module. */
static int cur_menu;
static char item_label[40], item_help[24];

static int mn_count(void)
{
    modx_mod_t *m = geo_mod();
    int n = 0;
    if (m)
        CALL(m, n = ops->menu_count(cur_menu));
    return n;
}
static void mn_get(int i, menu_item_t *out)
{
    modx_mod_t *m = geo_mod();
    int a = -1;
    item_label[0] = item_help[0] = 0;
    if (m)
        CALL(m, a = ops->menu_item(cur_menu, i, item_label, sizeof item_label, item_help, sizeof item_help));
    *out = (menu_item_t){item_label, NULL, NULL, a, item_help[0] ? item_help : NULL};
}
static void mn_select(const menu_item_t *it)
{
    modx_mod_t *m = geo_mod();
    if (m)
        CALL(m, ops->menu_select(cur_menu, it->action));
    else
        geo_back();
}
#define GEO_MENU(title, help) {title, NULL, 0, mn_select, false, help, mn_count, mn_get}
static const menu_def_t geo_menus[GEO_NMENU] = {
    GEO_MENU("POINTS", "geometry-tools"),  GEO_MENU("DRAW", "geometry-tools"), GEO_MENU("CONSTRUCT", "geometry-tools"),
    GEO_MENU("MEASURE", "geometry-measure"), GEO_MENU("VIEW", "geometry"),     GEO_MENU("FILE", "geometry-files"),
};
static void core_menu_open(int menu)
{
    if (menu < 0 || menu >= GEO_NMENU)
        return;
    cur_menu = menu;
    menu_open(&geo_menus[menu]);
}

/* ------------------------------------------------------------ files */

/* "/CALC/GEO/NAME.GEO" from a typed name: letters and digits, upper case,
 * at most 8; false if nothing is left. */
static bool file_path(const char *name, char *out, size_t n)
{
    char b[9];
    int k = 0;
    for (const char *s = name; *s && k < 8; s++) {
        char c = *s;
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 32);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
            b[k++] = c;
    }
    b[k] = 0;
    if (!k)
        return false;
    snprintf(out, n, GEO_DIR "/%s.GEO", b);
    return true;
}

/* Message box for a microSD error r, worded like the other apps' ("microSD: no card."). */
static void card_error(const char *title, int r)
{
    static char msg[48];
    snprintf(msg, sizeof msg, "microSD: %s.", fs_strerror(r));
    ui_message(title, msg);
}

static int core_file_save(const char *name)
{
    modx_mod_t *m = geo_mod();
    char path[40];
    if (!m)
        return FS_EINVAL;
    if (!file_path(name, path, sizeof path)) {
        ui_message("SAVE", "Type a name (letters, digits).");
        return FS_ENAME;
    }
    memcpy(fbuf, "GEO1", 4);
    pw_t w = {fbuf + 4, sizeof fbuf - 4, 0, false};
    CALL(m, ops->save(&w));
    fs_file_t f;
    int r = w.overflow ? FS_EFULL : fs_mkdir("/CALC");
    if (!r)
        r = fs_mkdir(GEO_DIR);
    if (!r)
        r = fs_open(&f, path, FS_WRITE);
    if (!r && fs_write(&f, fbuf, w.len + 4) != (int)(w.len + 4))
        r = FS_EIO;
    if (!r)
        r = fs_close(&f);
    fs_unmount();
    if (r)
        card_error("SAVE FAILED", r);
    return r;
}

static int core_file_load(const char *name)
{
    modx_mod_t *m = geo_mod();
    char path[40];
    if (!m)
        return FS_EINVAL;
    if (!file_path(name, path, sizeof path))
        return FS_ENAME;
    fs_file_t f;
    int r = fs_open(&f, path, FS_READ), n = 0;
    if (!r) {
        int k;
        while ((k = fs_read(&f, fbuf + n, sizeof fbuf - (size_t)n)) > 0)
            n += k;
        if (k < 0)
            r = k;
        fs_close(&f);
    }
    fs_unmount();
    if (!r && (n < 4 || memcmp(fbuf, "GEO1", 4)))
        r = FS_EINVAL;
    if (!r) {
        pr_t rd = {fbuf + 4, (size_t)n - 4, 0, false};
        bool ok = false;
        CALL(m, ok = ops->load(&rd));
        if (!ok)
            r = FS_EINVAL;
    }
    return r;
}

/* File > Open: the *.GEO files of /CALC/GEO as a menu. */
#define NFILES 24
static char files[NFILES][13];
static int nfiles;
static int file_cb(const fs_dirent_t *e, void *user)
{
    const char *dot = strrchr(e->short_name, '.');
    if (!e->is_dir && dot && !strcmp(dot, ".GEO") && nfiles < NFILES) {
        snprintf(files[nfiles], sizeof files[0], "%.*s", (int)(dot - e->short_name), e->short_name);
        nfiles++;
    }
    return 0;
}
static int fl_count(void) { return nfiles; }
static void fl_get(int i, menu_item_t *out) { *out = (menu_item_t){files[i], NULL, NULL, i, "geometry-files"}; }
static void fl_select(const menu_item_t *it)
{
    int r = core_file_load(files[it->action]);
    geo_back();
    if (r == FS_EINVAL)
        ui_message("OPEN FAILED", "Not a geometry file.");
    else if (r)
        card_error("OPEN FAILED", r);
}
static const menu_def_t menu_files = {"OPEN", NULL, 0, fl_select, false, "geometry-files", fl_count, fl_get};

static void core_files_menu(void)
{
    nfiles = 0;
    int r = fs_list(GEO_DIR, file_cb, NULL);
    fs_unmount();
    if (r || !nfiles) {
        geo_back();
        if (r && r != FS_ENOENT)
            card_error("OPEN FAILED", r);
        else
            ui_message("OPEN", "No constructions in /CALC/GEO.");
        return;
    }
    menu_open(&menu_files);
}

#endif

/* ------------------------------------------------------------ GRAPH overlay */

static bool ov_active(void)
{
    modx_mod_t *m = geo_peek();
    bool on = false;
    /* not bound yet: the kept blob says whether there is anything to show (u8 options, 3 x f64, u8 count) */
    if (!m && blob_valid && blob_len > 25 && (blob[0] & GEO_OPT_ONGRAPH) && blob[25])
        m = geo_mod();
    if (m)
        CALL(m, on = ops->ov_active());
    return on;
}
static void ov_draw(void)
{
    modx_mod_t *m = geo_mod();
    if (m)
        CALL(m, ops->ov_draw());
}
static int ov_nsets(void) { return 1; }
static int ov_npoints(int set)
{
    modx_mod_t *m = geo_peek();
    int n = 0;
    if (m)
        CALL(m, n = ops->ov_npoints());
    return n;
}
static bool ov_point(int set, int i, double *x, double *y)
{
    modx_mod_t *m = geo_peek();
    bool ok = false;
    if (m)
        CALL(m, ok = ops->ov_point(i, x, y));
    return ok;
}
static const char *ov_label(int set, char *b, size_t n)
{
    snprintf(b, n, "Geometry points");
    return b;
}
static uint16_t ov_color(int set) { return C_BLUE; }
static bool ov_bounds(double *x0, double *x1, double *y0, double *y1)
{
    modx_mod_t *m = geo_peek();
    bool ok = false;
    if (m)
        CALL(m, ok = ops->ov_bounds(x0, x1, y0, y1));
    return ok;
}
const graph_overlay_t graph_overlay_geo = {"geo", ov_active, ov_draw, ov_nsets, ov_npoints, ov_point,
                                           ov_label, ov_color, ov_bounds};

/* ------------------------------------------------------------ console, clipboard */

int geo_console(int argc, char **argv)
{
    modx_mod_t *m = geo_mod();
    if (!m)
        return con_error("%s", modx_why(GEO_MODULE));
    return modx_console_mod(m, argc, argv);
}

bool geo_clip_text(char *out, size_t n)
{
    modx_mod_t *m = geo_peek();
    bool ok = false;
    out[0] = 0;
    if (m)
        CALL(m, ok = ops->clip_text(out, n));
    return ok;
}

/* ------------------------------------------------------------ services for the module */
#if HAVE_GEO

static bool pr_failed(const pr_t *r) { return r->err; }
static void core_home(void) { ui_switch(ui_home()); }
static bool core_shown(void) { return ui_current() == &scr_geo; }
static void core_ed_open(const char *init)
{
    ed_clear(&ed);
    ed_insert_text(&ed, init, NULL);
    ed_on = true;
}
static void core_ed_close(void) { ed_on = false; }
static bool core_ed_key(int key, int mod) { return ui_editor_key(&ed, key, mod); }
static void core_ed_draw(int x, int y, int cols) { ui_draw_editor_line(&ed, x, y, cols, TH_FG, TH_BG, true); }
static void core_ed_clear(void) { ed_clear(&ed); }
static bool core_ed_empty(void) { return ed_empty(&ed); }
static void core_ed_text(char *out, size_t n) { ed_display(&ed, out, n); }
static int core_ed_number(double *v, char *err, size_t n)
{
    char cas[ED_MAX * 4];
    ed_cas(&ed, cas, sizeof cas);
    return eval_number(cas, v, err, n);
}
static bool func_on(int f) { return f >= 0 && f < NFUNC && g.f[f].enabled && g.f[f].fn; }
static double core_func_eval(int f, double x) { return f >= 0 && f < NFUNC ? func_eval(f, x) : (double)NAN; }
static uint16_t func_color(int f) { return f >= 0 && f < NFUNC ? func_colors[g.f[f].color % NCOLORS] : TH_FG; }
static void expect_failed(void) { console_expect_failures++; }
/* libm entries the compiler might otherwise inline differently per target: plain functions */
static long core_lround(double v) { return lround(v); }

static const geo_core_t geo_core = {
    GEO_CORE_ABI, sizeof(geo_core_t), core_attach,
    sin, cos, tan, acos, atan2, sqrt, hypot, fmod, pow, log10, floor, ceil, round, fmin, fmax, core_lround,
    strlen, strstr, atof,
    pw_u8, pw_f64, pw_str, pr_u8, pr_f64, pr_str, pr_failed,
    gfx_pixel, gfx_hline, gfx_vline, gfx_line, gfx_fill_rect, gfx_text, gfx_text_width, gfx_set_clip, gfx_reset_clip,
    &font_small, &font_medium,
    ui_cursor_hide, ui_message, core_menu_open, core_files_menu, geo_back, core_home, core_shown, app_redraw,
    core_ed_open, core_ed_close, core_ed_key, core_ed_draw, core_ed_clear, core_ed_empty, core_ed_text, core_ed_number,
    funcs_compile, func_on, core_func_eval, func_color, graph_invalidate, gov_window, gov_line, gov_mark,
    core_file_save, core_file_load, fs_strerror,
    expect_failed,
};
const geo_core_t *geo_core_table(void) { return &geo_core; }
#else
const geo_core_t *geo_core_table(void) { return NULL; } /* core tier without the app */
#endif
