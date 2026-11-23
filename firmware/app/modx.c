/*
 * modx: binding and calling code modules (design in modx.h and
 * docs/QSPI_CONTENT.md section 12).
 *
 * Binding (modx_get, on first use after boot or after a store change):
 *   1. the module must be served by the store (mod_state == MOD_OK: installed,
 *      committed, active, interface "modx" with a compatible ABI) and not
 *      switched off for tests (`modx off NAME`);
 *   2. its descriptor is read from offset 0 of the payload (mod_read, which
 *      checks the payload CRC on first use) and checked: magic, ABI, size,
 *      name, an API size this core provides, an RW size the pool can give;
 *   3. the entry: hardware, mod_map() gives the payload's address in the
 *      memory-mapped QSPI window; the
 *      entry is base + desc.init, which must be a Thumb address inside the
 *      image; the I-cache is invalidated (hal_code_sync) because the image
 *      may have been written since the last fetch from that address. Host
 *      builds: the linked init function from modx_host[];
 *   4. RW memory: desc.rw_size bytes (8-aligned, zeroed) from the pool
 *      carved from the app arena at boot (modx_mem_init); the pool is reset
 *      when every module is unbound;
 *   5. init(&modx_api, rw, &ops) runs (counted busy); a negative result or
 *      an ops table that is too small refuses the module.
 * A failed bind is remembered (with its reason for modx_why) until the
 * next store change, so a missing module costs one check, not one per frame.
 * The core tier has no store: a module linked into the firmware
 * (CONFIG_GEOMETRY_BUILTIN) binds from steps 4-5 directly; any other says
 * it is not part of this firmware.
 *
 * MODX_RW (modx.h): every call into a module points modx_ctx at its RW block
 * for the duration of the call (modx_call_begin / modx_call_end; nested
 * calls restore the outer value).
 *
 * Busy count: every trampoline increments g_busy around the call into the
 * module, so core code reached from a module (through the API table) can
 * tell (modx_busy) and refuse what must not happen then: session.c defers a
 * session save, because a QSPI write takes the window away from the code
 * that is executing.
 *
 * State: statics below; nothing is saved. Main loop only.
 */
#include "modx.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "anim.h"
#include "console.h"
#include "fat.h"
#include "g3d.h"
#include "gfx.h"
#include "hal.h"
#include "keyq.h"
#include "modstore.h"
#include "sdbk.h"
#include "geo_core.h"
#include "py_core.h"
#include "sections.h"
#include "session.h"
#include "ui.h"
#include "app.h"
#include "gen/keymap_gen.h"

#if defined(CONFIG_CONTENT_QSPI)
#define HAVE_MODULES 1
#define POOL_SIZE MODX_POOL /* (with the Python heap: modx.h) */
#elif defined(CONFIG_GEOMETRY_BUILTIN)
#define HAVE_MODULES 0
#define POOL_SIZE (64u << 10) /* core tier: the RW block of the geometry module linked in (~17 KB) */
#else
#define HAVE_MODULES 0
#define POOL_SIZE 0u
#endif
int geo_module_init(const modx_api_t *api, void *rw, modx_ops_t *ops); /* modules/geometry/geo_io.c */
extern const uint32_t geo_module_rw_size;
int py_module_init(const modx_api_t *api, void *rw, modx_ops_t *ops); /* modules/python/py_entry.c */
extern const uint32_t py_module_rw_size;

/* ------------------------------------------------------------ API table */
static cas_ctx *api_cas(void) { return g.cas; }
static bool api_input_pending(void) { return !keyq_empty(); }
static const uint16_t *api_framebuffer(void) { return gfx_framebuffer(); }
static void api_softkeys_changed(void)
{
    ui_softkeys_invalidate();
    ui_draw_softkeys();
}

/* one microSD file at a time for modules (GIF recording) */
static fs_file_t mfile;
static bool mfile_open, mfile_read; /* open, and opened for reading (api_file_open) */

/* Create /CALC/PREFIXnnn.EXT with the first unused number (001..999). */
static int api_file_create(const char *prefix, const char *ext, char *path, size_t n)
{
    if (mfile_open)
        return FS_EBUSY;
    int r = fs_mkdir("/CALC");
    for (int k = 1; !r && k <= 999; k++) {
        snprintf(path, n, "/CALC/%s%03d.%s", prefix, k, ext);
        fs_file_t f;
        int e = fs_open(&f, path, FS_READ);
        if (e == 0) {
            fs_close(&f);
            continue;
        }
        if (e != FS_ENOENT)
            r = e;
        break;
    }
    if (!r)
        r = fs_open(&mfile, path, FS_WRITE);
    if (r) {
        fs_unmount();
        return r;
    }
    mfile_open = true;
    mfile_read = false;
    return 0;
}

/* Open a card file for reading (theme packs); *size = its length. */
static int api_file_open(const char *path, uint32_t *size)
{
    if (mfile_open)
        return FS_EBUSY;
    int r = fs_open(&mfile, path, FS_READ);
    if (r) {
        fs_unmount();
        return r;
    }
    if (size)
        *size = mfile.size;
    mfile_open = mfile_read = true;
    return 0;
}

static int api_file_read(void *buf, size_t n)
{
    if (!mfile_open || !mfile_read)
        return FS_EINVAL;
    return fs_read(&mfile, buf, n);
}

/* List a card directory for a module: cb(name, size, user) per file
 * (directories skipped); cb returns nonzero to stop. */
typedef struct {
    int (*cb)(const char *name, uint32_t size, void *user);
    void *user;
} dl_ctx_t;
static int dl_cb(const fs_dirent_t *e, void *u)
{
    dl_ctx_t *c = u;
    return e->is_dir ? 0 : c->cb(e->short_name[0] ? e->short_name : e->name, e->size, c->user);
}
static int api_dir_list(const char *dir, int (*cb)(const char *name, uint32_t size, void *user), void *user)
{
    if (mfile_open)
        return FS_EBUSY;
    dl_ctx_t c = {cb, user};
    int r = fs_list(dir, dl_cb, &c);
    fs_unmount();
    return r;
}

/* ASCII of what `key` types in layer `mod` (MOD_ALPHA: the letter), 0 if
 * it is not a single printable character. */
static int api_key_char(int key, int mod)
{
    if (key <= 0 || key >= KEY_COUNT)
        return 0;
    const char *t = mod == MOD_ALPHA ? key_info[key].alpha : mod == MOD_NONE ? key_info[key].text : NULL;
    return t && t[0] > ' ' && (unsigned char)t[0] < 0x7F && !t[1] ? t[0] : 0;
}

/* ---- saved bytes of a module (blob "modx.save") ----
 * One record (name + up to MODX_SAVE_MAX bytes) in a buffer carved from the
 * arena next to the RW pool, so a module's progress survives unbinding,
 * store changes and power-off without the module being bound at save time.
 * Blob versions: 1 (cores up to 16 KB) has a u16 length, 2 a u32 length;
 * both load (a version-1 session migrates on its next save). */
static uint8_t *save_buf;
static size_t save_len;
static char save_name[16];

static size_t api_save_get(const char *name, void *buf, size_t n)
{
    if (!save_buf || !save_len || strcmp(save_name, name))
        return 0;
    size_t k = save_len < n ? save_len : n;
    memcpy(buf, save_buf, k);
    return k;
}

static int api_save_put(const char *name, const void *buf, size_t n)
{
    if (!save_buf || n > MODX_SAVE_MAX || (save_len && strcmp(save_name, name)))
        return -1;
    snprintf(save_name, sizeof save_name, "%s", name);
    memcpy(save_buf, buf, n);
    save_len = n;
    return 0;
}

static void ms_save(pw_t *w)
{
    if (!save_buf || !save_len)
        return;
    pw_str(w, save_name);
    pw_u32(w, (uint32_t)save_len);
    pw_bytes(w, save_buf, save_len);
}
static void ms_reset(void) { save_len = 0, save_name[0] = 0; }
static void ms_load(pr_t *r, unsigned version)
{
    ms_reset();
    char nm[16];
    pr_str(r, nm, sizeof nm);
    size_t n = version >= 2 ? pr_u32(r) : pr_u16(r);
    if (r->err || !save_buf || n > MODX_SAVE_MAX || n > pr_left(r))
        return;
    pr_bytes(r, save_buf, n);
    if (r->err)
        return;
    snprintf(save_name, sizeof save_name, "%s", nm);
    save_len = n;
}
/* Blob "modx.save" v2: str name, u32 length, bytes (v1: a u16 length). */
const persist_def_t persist_modx_save = {"modx.save", 2, ms_save, ms_load, ms_reset};

static size_t api_save_max(void) { return MODX_SAVE_MAX; }
/* bytes kept for module `name` (0: none), so a module can tell an older layout before reading it */
static size_t api_save_size(const char *name) { return save_buf && save_len && !strcmp(save_name, name) ? save_len : 0; }
static int api_screenshot(char *msg, size_t n) { return sdbk_screenshot(msg, n); }

/* games: the pinch scale, sound hooks (no sound on the calculator: the simulators log them, the web
 * build forwards them to the page's calcSound(id, param)), the display scale */
static int touch_scale256 = 256;
void modx_set_touch_scale(int scale256) { touch_scale256 = scale256 > 0 ? scale256 : 256; }
static int api_touch_scale(void) { return touch_scale256; }
static uint64_t sfx_seen;        /* ids fired since `modx sfx clear` (bit per id) */
static uint32_t sfx_count;
static int music_now, music_level;
static const char sfx_names[MODX_SFX_COUNT][8] = {"", "pop", "coin", "combo", "whoosh", "squish", "unlock", "gift",
                                                 "cheer", "dud", "step", "jump", "boss", "tap", "launch", "land",
                                                 "boost", "bongo", "bongo2", "cowbell", "clap", "voice", "merge",
                                                 "fanfare", "click", "mew", "trill", "meow", "paw", "bass",
                                                 "chord", "tamb", "giggle", "sigh"};
const char *modx_sfx_name(int id) { return id > 0 && id < MODX_SFX_COUNT ? sfx_names[id] : "?"; }
#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
EM_JS(void, web_sound, (int kind, int id, int param), {
    var f = (typeof globalThis !== 'undefined' && globalThis.calcSound) || (Module && Module.calcSound);
    if (typeof f === 'function') { try { f(kind ? 'music' : 'sfx', id, param); } catch (e) {} }
});
#endif
static void api_sfx(int id, int param)
{
    if (id > 0 && id < MODX_SFX_COUNT)
        sfx_seen |= (uint64_t)1 << id, sfx_count++;
#if defined(__EMSCRIPTEN__)
    web_sound(0, id, param);
#endif
    (void)param;
}
static void api_music(int track, int intensity)
{
    music_now = track, music_level = intensity;
#if defined(__EMSCRIPTEN__)
    web_sound(1, track, intensity);
#endif
}
/* talking characters: the page speaks (calcSpeak), the simulators log the last line */
static char speak_last[64];
static uint32_t speak_count;
#if defined(__EMSCRIPTEN__)
EM_JS(void, web_speak, (int who, const char *text), {
    var f = (typeof globalThis !== 'undefined' && globalThis.calcSpeak) || (Module && Module.calcSpeak);
    if (typeof f === 'function') { try { f(who, UTF8ToString(text)); } catch (e) {} }
});
#endif
static void api_speak(int who, const char *text)
{
    if (!text)
        return;
    snprintf(speak_last, sizeof speak_last, "%d %s", who & 0xFF, text);
    speak_count++;
#if defined(__EMSCRIPTEN__)
    web_speak(who, text);
#endif
}
/* the display: the framebuffer, or a high-resolution one (modx_set_display) */
static const modx_display_t *display;
void modx_set_display(const modx_display_t *d) { display = d && d->scale > 1 ? d : NULL; }
static int api_display_scale(void) { return display ? display->scale : 1; }
static void api_fill_rect(int x, int y, int w, int h, uint16_t c)
{
    if (display)
        display->fill_rect(x, y, w, h, c);
    else
        gfx_fill_rect(x, y, w, h, c);
}
static void api_rect(int x, int y, int w, int h, uint16_t c)
{
    if (display)
        display->rect(x, y, w, h, c);
    else
        gfx_rect(x, y, w, h, c);
}
static int api_text(const font_t *f, int x, int y, const char *s, uint16_t fg, int bg)
{
    return display ? display->text(f, x, y, s, fg, bg) : gfx_text(f, x, y, s, fg, bg);
}
static void api_line(int x0, int y0, int x1, int y1, uint16_t c)
{
    if (display)
        display->line(x0, y0, x1, y1, c);
    else
        gfx_line(x0, y0, x1, y1, c);
}
static int api_hr(int op, const int32_t *a, int n, const void *p) { return display ? display->hr(op, a, n, p) : 0; }

/* the module's own screen: soft-key labels and leaving (app/mquest_core.c) */
static void api_softkey(int i, const char *label) { modx_screen_softkey(i, label); }
static void api_leave(void) { modx_screen_leave(); }

static int api_file_write(const void *buf, size_t n)
{
    if (!mfile_open)
        return FS_EINVAL;
    if (fs_write(&mfile, buf, n) != (int)n) {
        mfile_open = false; /* fs_write dropped the file */
        fs_unmount();
        return FS_EIO;
    }
    return 0;
}

static int api_file_close(bool keep)
{
    if (!mfile_open)
        return FS_EINVAL;
    mfile_open = false;
    int r = 0;
    if (mfile_read)
        r = fs_close(&mfile), mfile_read = false;
    else if (keep)
        r = fs_close(&mfile);
    else
        fs_abort(&mfile);
    fs_unmount();
    return r;
}

/* The notebook's card files (app/py_core.c NBIO_SD_*): 0 create `p` (a path; long names allowed)
 * for writing in the one-file slot (then file_write / file_close), 1 remove it, 2 list a directory
 * with long names (p: nbio_list_t; -> count). */
typedef struct {
    int (*cb)(const char *name, uint32_t size, void *user);
    void *user;
    int n;
} nl_ctx_t;
static int nl_cb(const fs_dirent_t *e, void *u)
{
    nl_ctx_t *c = u;
    return e->is_dir ? 0 : (c->n++, c->cb(e->name, e->size, c->user));
}
int modx_nb_sd(int op, void *p)
{
    if (mfile_open)
        return FS_EBUSY;
    int r;
    if (op == 0) {
        char dir[64];
        const char *sl = strrchr(p, '/');
        snprintf(dir, sizeof dir, "%.*s", sl ? (int)(sl - (const char *)p) : 0, (const char *)p);
        r = fs_mkdir("/CALC");
        if (!r && strcmp(dir, "/CALC"))
            r = fs_mkdir(dir);
        if (!r && !(r = fs_open(&mfile, p, FS_WRITE))) {
            mfile_open = true, mfile_read = false;
            return 0;
        }
    } else if (op == 1) {
        r = fs_remove(p);
    } else {
        const nbio_list_t *l = p;
        nl_ctx_t c = {l->cb, l->user, 0};
        r = fs_list(l->dir, nl_cb, &c);
        if (!r)
            r = c.n;
    }
    fs_unmount();
    return r;
}

static void api_con_printf(const char *fmt, ...)
{
    char b[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    con_printf("%s", b);
}

static int api_con_error(const char *fmt, ...)
{
    char b[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    return con_error("%s", b);
}

/* The table every module gets (ABI 1); entries are appended, never reordered. */
const modx_api_t modx_api = {
    MODX_ABI, sizeof(modx_api_t),
    hal_millis, hal_cycles, hal_cycles_per_us, hal_watchdog_kick, api_input_pending,
    snprintf, fmt_num, strcmp, floor,
    api_fill_rect, api_rect, api_text, gfx_text_width, api_framebuffer, gfx_flush, &font_small, &font_medium,
    ui_toast, ui_message, api_softkeys_changed, api_con_printf, api_con_error, con_parse_int,
    api_cas, cas_compile_vars, cas_fn_eval_vars, cas_fn_free,
    anim_target, anim_apply, anim_finish, g3_grid,
    api_file_create, api_file_write, api_file_close, sdbk_card_error,
    &font_large, &font_xlarge, api_line, app_held_key, api_key_char, api_save_get, api_save_put,
    api_file_open, api_file_read, api_dir_list, api_softkey, api_leave,
    api_save_max, api_screenshot,
    cas_ext_api,
    api_touch_scale, api_sfx, api_music, api_display_scale,
    api_hr,
    api_save_size,
    api_speak,
    session_save_soon,
    geo_core_table,
    py_core_table,
};

/* ------------------------------------------------------------ registry */
/* display names of the known code modules (fallback messages, About) */
static const struct {
    const char *name, *title;
} known[] = {
    {ANIM_MODULE, "Animation"},
    {MQ_MODULE, "Math Quest"},
    {CASX_MODULE, "advanced CAS"},
    {GEO_MODULE, "Geometry"},
    {PY_MODULE, "Python"},
    {"webui", "Notebook page"}, /* a data module: CALCULATOR.HTML on the CALC drive (app/usb_app.c) */
};

const char *modx_title(const char *name)
{
    for (size_t i = 0; i < sizeof known / sizeof known[0]; i++)
        if (!strcmp(known[i].name, name))
            return known[i].title;
    return name;
}

#if !defined(PLATFORM_STM32) && HAVE_MODULES
/* Host builds link the module sources; their entry points by name. */
typedef struct {
    char name[16];
    modx_init_fn init;
    uint32_t rw_size;
} host_mod_t;
extern int anim_module_init(const modx_api_t *api, void *rw, modx_ops_t *ops);
extern const uint32_t anim_module_rw_size;
extern int mq_module_init(const modx_api_t *api, void *rw, modx_ops_t *ops);
extern const uint32_t mq_module_rw_size;
extern int casx_module_init(const modx_api_t *api, void *rw, modx_ops_t *ops);
static host_mod_t modx_host[MODX_MAX];
static int nhost;

void modx_host_register(const char *name, modx_init_fn init, uint32_t rw_size)
{
    for (int i = 0; i < nhost; i++)
        if (!strcmp(modx_host[i].name, name)) {
            modx_host[i].init = init, modx_host[i].rw_size = rw_size;
            return;
        }
    if (nhost < MODX_MAX) {
        snprintf(modx_host[nhost].name, sizeof modx_host[nhost].name, "%s", name);
        modx_host[nhost].init = init, modx_host[nhost].rw_size = rw_size;
        nhost++;
    }
}

static const host_mod_t *host_find(const char *name)
{
    static bool builtin_done;
    if (!builtin_done) { /* the modules of modules/ (linked into this binary) */
        builtin_done = true;
        modx_host_register(ANIM_MODULE, anim_module_init, anim_module_rw_size);
        modx_host_register(MQ_MODULE, mq_module_init, mq_module_rw_size);
        modx_host_register(CASX_MODULE, casx_module_init, 0);
        modx_host_register(GEO_MODULE, geo_module_init, geo_module_rw_size);
#if defined(CONFIG_PYTHON) && !defined(PLATFORM_WEB)
        modx_host_register(PY_MODULE, py_module_init, py_module_rw_size);
#endif
    }
    for (int i = 0; i < nhost; i++)
        if (!strcmp(modx_host[i].name, name))
            return &modx_host[i];
    return NULL;
}
#endif

/* ------------------------------------------------------------ binding */
struct modx_mod {
    char name[16];
    bool tried;          /* bind attempted since the last store change */
    bool bound;
    char why[96];        /* reason when not bound */
    void *rw;
    uint32_t rw_size;
    const void *base;    /* hardware: image address (XIP) */
    modx_ops_t ops;
    uint32_t calls;      /* trampoline calls (console `modx list`) */
};
static modx_mod_t mods[MODX_MAX];
static char off_names[MODX_MAX][16]; /* `modx off NAME`: emulate "not installed" */
static uint8_t *pool;
static size_t pool_size, pool_used;
static uint8_t *code_area; /* code-load modules (modx_code_init) */
static size_t code_size, code_used;
static int g_busy;
/* MODX_RW (modx.h): the first word of DTCM on the hardware (section .modx_ctx, linker script) */
#if defined(PLATFORM_STM32)
__attribute__((section(".modx_ctx")))
#endif
void *modx_ctx;

#if HAVE_MODULES
/* The CAS asks for an extension module when one of its commands is used
 * (cas_ext_set_loader): binding it runs its init, which registers them. */
static int casx_load(const char *name) { return modx_get(name) ? 0 : -1; }
#endif

size_t modx_mem_init(void *mem, bool big)
{
    memset(mods, 0, sizeof mods);
    g_busy = 0;
    modx_ctx = NULL;
    pool_used = 0;
#if HAVE_MODULES
    cas_ext_set_loader(casx_load);
#endif
    if (!big || POOL_SIZE == 0) {
        pool = NULL, pool_size = 0;
        return 0;
    }
    pool = (uint8_t *)(((uintptr_t)mem + 7) & ~(uintptr_t)7);
    pool_size = POOL_SIZE;
    if (!HAVE_MODULES)
        return (size_t)(pool - (uint8_t *)mem) + POOL_SIZE;
    save_buf = pool + POOL_SIZE; /* the saved bytes follow the pool (kept: the session loads them later) */
    return (size_t)(pool - (uint8_t *)mem) + POOL_SIZE + MODX_SAVE_MAX;
}

bool modx_busy(void) { return g_busy > 0 || cas_ext_busy(); }

void modx_forget_all(void)
{
    /* no call into the modules here: at boot the statics may be the previous
     * run's (the simulators restart the app); the core resets what they had
     * set (anim_module_changed) */
    geo_core_unbind(false);
    memset(mods, 0, sizeof mods);
    pool_used = code_used = 0;
    cas_ext_unload(); /* the CAS forgets the commands extension modules registered */
    if (mfile_open)
        api_file_close(false);
}

/* Before a store write (and when a module is switched off): the module code
 * is still in place, so the core first takes back what it keeps for a module
 * (the geometry construction), then forgets every binding. After the write
 * nothing is bound any more, so the second call that follows it (and the one
 * at boot, modx_forget_all) never calls into a module. */
void modx_unbind_all(void)
{
    py_nb_flush(); /* (the notebook lives in the Python module's memory) */
    geo_core_unbind(true);
    modx_forget_all();
}

void *modx_call_begin(modx_mod_t *m)
{
    void *prev = modx_ctx;
    g_busy++;
    if (m)
        m->calls++, modx_ctx = m->rw;
    return prev;
}

void modx_call_end(void *prev)
{
    modx_ctx = prev;
    g_busy--;
}

static bool is_off(const char *name)
{
    for (int i = 0; i < MODX_MAX; i++)
        if (off_names[i][0] && !strcmp(off_names[i], name))
            return true;
    return false;
}

/* The "not installed" sentence for module `name` in store state s. */
static void state_why(char *out, size_t n, const char *name, mod_state_t s)
{
    const char *t = modx_title(name);
    switch (s) {
    case MOD_ABI:
        snprintf(out, n, "The %s module is for another firmware: install the one of this release (Memory > Modules).", t);
        break;
    case MOD_CORRUPT:
        snprintf(out, n, "The %s module is damaged: install it again (Memory > Modules).", t);
        break;
    case MOD_NODEV:
        snprintf(out, n, "The QSPI flash is not responding, so the %s module cannot run.", t);
        break;
    case MOD_DISABLED:
        snprintf(out, n, "Modules are not used in safe mode: switch off and on.");
        break;
    case MOD_DEPS:
        snprintf(out, n, "The %s module needs another module: install the release's modules (Memory > Modules).", t);
        break;
    default:
        snprintf(out, n, "Install the %s module (Memory > Modules).", t);
        break;
    }
}

/* The code area: the last CONFIG_MODX_CODE_KB (a power of two) of the arena,
 * aligned to its size, so one MPU region makes it executable (hal_code_region). */
FLASH2_TEXT size_t modx_code_init(void *end, size_t room)
{
    code_area = NULL, code_size = code_used = 0;
#if defined(CONFIG_MODX_CODE_KB) && HAVE_MODULES
    size_t sz = (size_t)CONFIG_MODX_CODE_KB << 10;
    uintptr_t e = (uintptr_t)end, base = (e - sz) & ~(uintptr_t)(sz - 1);
    if (!room || e < sz || e - base > room / 2)
        return 0;
    code_area = (uint8_t *)base, code_size = sz;
    hal_code_region(code_area, sz);
    return (size_t)(e - base);
#else
    (void)end, (void)room;
    return 0;
#endif
}

#if HAVE_MODULES
/* Copy a code-load module's image into the code area, relocate it, zero its
 * bss and make it visible to instruction fetches: the load address, or NULL
 * (m->why says why). bind() checked the descriptor's first part. */
__attribute__((noinline)) FLASH2_TEXT static uint8_t *load_image(modx_mod_t *m, const modx_desc_t *d, uint32_t plen)
{
    modx_load_desc_t ld;
    const char *name = m->name;
    mod_state_t bad = MOD_CORRUPT;
    if (d->desc_size < sizeof ld || mod_read(name, 0, &ld, sizeof ld) || ld.d.image_size < sizeof ld ||
        ld.d.image_size > plen || ld.reloc_off < ld.d.image_size || ld.reloc_count > (plen - ld.reloc_off) / 4 ||
        !(ld.d.init & 1u) || ld.d.init >= ld.d.image_size)
        goto fail;
    size_t need = ((size_t)ld.d.image_size + ld.bss_size + 31u) & ~(size_t)31u;
    if (!code_area || need > code_size - code_used) {
        snprintf(m->why, sizeof m->why, "The %s module needs %lu KB of the SDRAM, which is not available.",
                 modx_title(name), (unsigned long)(need >> 10));
        return NULL;
    }
    uint8_t *dst = code_area + code_used;
    if (mod_read(name, 0, dst, ld.d.image_size))
        goto fail;
    uint32_t off[64];
    for (uint32_t i = 0; i < ld.reloc_count; i += 64) {
        uint32_t k = ld.reloc_count - i < 64 ? ld.reloc_count - i : 64;
        if (mod_read(name, ld.reloc_off + i * 4, off, k * 4))
            goto fail;
        for (uint32_t j = 0; j < k; j++) {
            uint32_t w;
            if ((off[j] & 3u) || off[j] > ld.d.image_size - 4)
                goto fail;
            memcpy(&w, dst + off[j], 4);
            w += (uint32_t)(uintptr_t)dst; /* (the simulators relocate too, which tests the list; they run host code) */
            memcpy(dst + off[j], &w, 4);
        }
    }
    memset(dst + ld.d.image_size, 0, need - ld.d.image_size);
    hal_code_sync(); /* written back from the D-cache, I-cache invalidated */
    code_used += need;
    return dst;
fail:
    state_why(m->why, sizeof m->why, name, bad);
    return NULL;
}
#endif

/* Try to bind slot m (see the file comment); fills m->why on failure. */
static void bind(modx_mod_t *m)
{
    m->tried = true;
    const char *name = m->name;
    uint32_t rw = 0;
    modx_init_fn init = NULL;
    size_t code_mark = code_used; /* given back if a loaded image does not bind */
    if (is_off(name)) {
        state_why(m->why, sizeof m->why, name, MOD_ABSENT);
        return;
    }
#if !HAVE_MODULES
    /* core tier: no module store; only the modules linked into the firmware */
#if defined(CONFIG_GEOMETRY_BUILTIN)
    if (!strcmp(name, GEO_MODULE))
        init = geo_module_init, rw = geo_module_rw_size;
#endif
    if (!init) {
        snprintf(m->why, sizeof m->why, "%s is not part of this firmware (core tier).", modx_title(name));
        return;
    }
#else
    mod_state_t s = mod_state(name);
    if (s != MOD_OK) {
        state_why(m->why, sizeof m->why, name, s);
        return;
    }
    modx_desc_t d;
    if (mod_read(name, 0, &d, sizeof d)) {
        state_why(m->why, sizeof m->why, name, MOD_CORRUPT);
        return;
    }
    const mod_hdr_t *h = mod_header(name);
    uint32_t plen = h ? h->payload_len : 0;
    bool load = h && h->kind == MOD_KIND_CODE_LOAD;
    if (d.magic != MODX_MAGIC || d.abi != MODX_ABI || d.desc_size < sizeof d || strncmp(d.name, name, sizeof d.name) ||
        d.api_size > sizeof(modx_api_t) || (h && h->kind != MOD_KIND_CODE_XIP && !load)) {
        state_why(m->why, sizeof m->why, name, MOD_ABI);
        return;
    }
    rw = d.rw_size;
    uint8_t *img = NULL;
    if (load && !(img = load_image(m, &d, plen)))
        return;
#if defined(PLATFORM_STM32)
    if (img) {
        m->base = img;
        init = (modx_init_fn)(uintptr_t)(img + d.init);
    } else {
        uint32_t len = 0;
        const uint8_t *base = mod_map(name, &len);
        if (!base || len != plen || d.image_size > len || d.image_size < sizeof d || !(d.init & 1u) ||
            d.init >= d.image_size) {
            state_why(m->why, sizeof m->why, name, base ? MOD_CORRUPT : MOD_NODEV);
            return;
        }
        hal_code_sync();
        m->base = base;
        init = (modx_init_fn)(uintptr_t)(base + d.init);
    }
#else
    (void)plen;
    const host_mod_t *hm = host_find(name);
    if (!hm) {
        state_why(m->why, sizeof m->why, name, MOD_ABI);
        return;
    }
    init = hm->init;
    rw = hm->rw_size; /* the host's own layout (pointer sizes differ from the image's) */
#endif
#endif
    size_t need = ((size_t)rw + 7u) & ~(size_t)7u;
    if (need && (!pool || need > pool_size - pool_used)) {
        snprintf(m->why, sizeof m->why, "The %s module needs %lu KB of the SDRAM, which is not available.",
                 modx_title(name), (unsigned long)(need >> 10));
        code_used = code_mark;
        return;
    }
    m->rw = pool ? pool + pool_used : NULL;
    m->rw_size = rw;
    if (need)
        memset(m->rw, 0, need);
    memset(&m->ops, 0, sizeof m->ops);
    void *prev = modx_call_begin(m);
    m->calls = 0;
    int r = init(&modx_api, m->rw, &m->ops);
    modx_call_end(prev);
    if (r < 0 || m->ops.size < sizeof m->ops) {
        snprintf(m->why, sizeof m->why, "The %s module refused to start (error %d): install the release's version.",
                 modx_title(name), r);
        memset(&m->ops, 0, sizeof m->ops);
        code_used = code_mark;
        return;
    }
    pool_used += need;
    m->bound = true;
    m->why[0] = 0;
}

static modx_mod_t *slot(const char *name)
{
    modx_mod_t *free_slot = NULL;
    for (int i = 0; i < MODX_MAX; i++) {
        if (mods[i].name[0] && !strcmp(mods[i].name, name))
            return &mods[i];
        if (!mods[i].name[0] && !free_slot)
            free_slot = &mods[i];
    }
    for (int i = 0; i < MODX_MAX && !free_slot; i++)
        if (!mods[i].bound) /* full: forget a failed attempt (it is retried when asked again) */
            free_slot = &mods[i];
    if (free_slot) {
        memset(free_slot, 0, sizeof *free_slot);
        snprintf(free_slot->name, sizeof free_slot->name, "%s", name);
    }
    return free_slot;
}

modx_mod_t *modx_get(const char *name)
{
    modx_mod_t *m = slot(name);
    if (!m)
        return NULL;
    if (!m->tried)
        bind(m);
    return m->bound ? m : NULL;
}

modx_mod_t *modx_peek(const char *name)
{
    for (int i = 0; i < MODX_MAX; i++)
        if (mods[i].bound && !strcmp(mods[i].name, name))
            return &mods[i];
    return NULL;
}

const char *modx_why(const char *name)
{
    modx_mod_t *m = slot(name);
    if (!m)
        return "Too many modules.";
    if (!m->tried)
        bind(m);
    return m->bound ? "" : m->why;
}

/* ------------------------------------------------------------ trampolines */
void modx_tick(modx_mod_t *m, uint32_t now, int scr)
{
    if (!m || !m->ops.tick)
        return;
    void *c = modx_call_begin(m);
    m->ops.tick(m->rw, now, scr);
    modx_call_end(c);
}

int modx_event(modx_mod_t *m, int ev, int arg)
{
    if (!m || !m->ops.event)
        return 0;
    void *c = modx_call_begin(m);
    int r = m->ops.event(m->rw, ev, arg);
    modx_call_end(c);
    return r;
}

bool modx_draw(modx_mod_t *m, int scr, int rect[4])
{
    if (!m || !m->ops.draw)
        return false;
    void *c = modx_call_begin(m);
    bool r = m->ops.draw(m->rw, scr, rect);
    modx_call_end(c);
    return r;
}

void modx_state(modx_mod_t *m, void (*kv)(const char *, const char *, void *), void *user)
{
    if (!m || !m->ops.state)
        return;
    void *c = modx_call_begin(m);
    m->ops.state(m->rw, kv, user);
    modx_call_end(c);
}

int modx_console_mod(modx_mod_t *m, int argc, char **argv)
{
    if (!m || !m->ops.console)
        return con_error("no console in this module");
    void *c = modx_call_begin(m);
    int r = m->ops.console(m->rw, argc, argv);
    modx_call_end(c);
    return r;
}

/* ------------------------------------------------------------ console / state */
/* `state` keys: modx_<name> = bound / the store state / off, modx_pool (bytes used). */
void modx_status(void (*kv)(const char *, const char *, void *), void *user)
{
    char k[32], b[24];
    for (size_t i = 0; i < sizeof known / sizeof known[0]; i++) {
        const char *nm = known[i].name;
        snprintf(k, sizeof k, "modx_%s", nm);
        modx_mod_t *m = slot(nm);
        const char *v = "unbound";
        if (m && m->bound)
            v = "bound";
        else if (is_off(nm))
            v = "off";
        else if (m && m->tried)
            v = "unavailable";
        kv(k, v, user);
    }
    snprintf(b, sizeof b, "%lu", (unsigned long)pool_used);
    kv("modx_pool", b, user);
    char sb[16 * MODX_SFX_COUNT] = ""; /* (room for every name; and never written past: a toy box mashed through every
                                       * sound overran the old 128 bytes -- snprintf's would-be length was added on) */
    size_t n = 0;
    for (int i = 1; i < MODX_SFX_COUNT && n < sizeof sb - 1; i++)
        if ((sfx_seen >> i) & 1) {
            int w = snprintf(sb + n, sizeof sb - n, " %s", sfx_names[i]);
            n = w < 0 ? n : n + (size_t)w >= sizeof sb ? sizeof sb - 1 : n + (size_t)w;
        }
    kv("modx_sfx", sb, user);
    snprintf(b, sizeof b, "%d %d", music_now, music_level);
    kv("modx_music", b, user);
    kv("modx_speak", speak_last, user);
    snprintf(b, sizeof b, "%lu", (unsigned long)speak_count);
    kv("modx_speaks", b, user);
}

/* modx list | off NAME | on NAME | bind NAME | unbind */
int modx_console(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "list";
    if (!strcmp(sub, "sfx")) { /* the sound log: modx sfx [clear] */
        if (argc > 2 && !strcmp(argv[2], "clear"))
            sfx_seen = 0, sfx_count = 0;
        con_printf("%lu effects, music %d (%d):", (unsigned long)sfx_count, music_now, music_level);
        for (int i = 1; i < MODX_SFX_COUNT; i++)
            if ((sfx_seen >> i) & 1)
                con_printf(" %s", sfx_names[i]);
        con_printf("\n");
        return 0;
    }
    if (!strcmp(sub, "list")) {
        con_printf("api abi %d size %u, pool %lu of %lu bytes used, code %lu of %lu\n", MODX_ABI,
                   (unsigned)sizeof(modx_api_t), (unsigned long)pool_used, (unsigned long)pool_size,
                   (unsigned long)code_used, (unsigned long)code_size);
        for (int i = 0; i < MODX_MAX; i++)
            if (mods[i].name[0])
                con_printf("%s: %s rw %lu calls %lu%s%s\n", mods[i].name,
                           mods[i].bound ? "bound" : mods[i].tried ? "not bound" : "unbound",
                           (unsigned long)mods[i].rw_size, (unsigned long)mods[i].calls, mods[i].why[0] ? ": " : "",
                           mods[i].why);
        return 0;
    }
    if ((!strcmp(sub, "off") || !strcmp(sub, "on")) && argc > 2) {
        bool off = !strcmp(sub, "off");
        for (int i = 0; i < MODX_MAX; i++)
            if (!strcmp(off_names[i], argv[2]))
                off_names[i][0] = 0;
        for (int i = 0; off && i < MODX_MAX; i++)
            if (!off_names[i][0]) {
                snprintf(off_names[i], sizeof off_names[i], "%s", argv[2]);
                break;
            }
        modx_unbind_all();
        anim_module_changed();
        return 0;
    }
    if (!strcmp(sub, "bind") && argc > 2) {
        if (modx_get(argv[2]))
            return 0;
        return con_error("%s", modx_why(argv[2]));
    }
    if (!strcmp(sub, "unbind")) {
        modx_unbind_all();
        anim_module_changed();
        return 0;
    }
    return con_error("usage: modx list|off NAME|on NAME|bind NAME|unbind");
}
