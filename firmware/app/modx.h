/*
 * modx: code modules (phase 2 of docs/QSPI_CONTENT.md, section 12).
 *
 * A code module is machine code in a content module (core/modstore.h, kind
 * MOD_KIND_CODE_XIP, interface "modx"). Its payload is a position-independent
 * image linked at address 0 (modules/modx_image.ld): it starts with a
 * descriptor (modx_desc_t) and contains only read-only code and constants:
 * no .data, no .bss, no GOT (the link fails otherwise). The module calls the
 * core only through the versioned API table (modx_api_t) passed to its init
 * function, and keeps its state in RW memory the core allocates for it from
 * the app arena (desc.rw_size bytes, zeroed at every bind).
 *
 * Execution. On the hardware the image runs in place (XIP) from the
 * memory-mapped QSPI window: the entry is the window address of the payload
 * plus desc.init (Thumb bit included). Every call into module code goes
 * through the modx_* trampolines below, which count the module as busy;
 * a QSPI write (session save, install, compaction) never happens while a
 * module is busy (session.c defers a save, modstore writes come only from
 * the Memory menus and the console, which no module calls), interrupt
 * handlers never call a module, and qspi.c makes the window execute-never
 * while it is out of memory-mapped mode. A store change (install, remove,
 * revert, format, remount) unbinds every module first (modx_unbind_all).
 *
 * Simulators (native and web): the module sources are linked into the host
 * binary and registered in modx_host[] (modx.c), but binding takes exactly
 * the same path: the module must be served by the store (state ok: the
 * factory content holds its .MOD file, or it was installed), its descriptor
 * is read from the stored payload and checked (magic, ABI, name, sizes),
 * the RW block comes from the same pool and init gets the same API table.
 * Only the entry address differs (the linked function instead of window +
 * offset). The console `modx off NAME` emulates "not installed" on every
 * platform (tests), as does `module remove NAME`.
 *
 * Fallback: a feature asks modx_get(name); NULL means not available and
 * modx_why(name) gives the sentence to show ("Install the Animation module
 * (Memory > Modules)."). Nothing else in the core depends on a module.
 *
 * Versioning (QSPI_CONTENT.md 5.1): MODX_ABI is the interface ABI of "modx"
 * (core_ifaces in modules_ui.c, checked by the store at install and boot).
 * Bump it when the API table, the descriptor, the ops table or the meaning of
 * any entry changes incompatibly. New API entries are appended (modx_api_t
 * .size grows; a module declares the size it needs in desc.api_size).
 */
#ifndef MODX_H
#define MODX_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cas.h"
#include "font.h"

#define MODX_MAGIC 0x584D4443u /* "CDMX" */
#define MODX_ABI 1
#define MODX_IFACE "modx"
#define MODX_MAX 8             /* modules known (bound or failed) at the same time */
/* RW memory for all bound modules (app arena, big arena only): 512 KB, plus the Python module's
 * heap where the firmware has Python (CONFIG_PYTHON_HEAP_KB, app/py_core.h) */
#if defined(CONFIG_PYTHON)
#define MODX_POOL ((512u << 10) + ((unsigned)CONFIG_PYTHON_HEAP_KB << 10))
#else
#define MODX_POOL (512u << 10)
#endif

/* The first bytes of a code module's image (offset 0 of the payload). */
typedef struct {
    uint32_t magic;     /* MODX_MAGIC */
    uint16_t abi;       /* MODX_ABI it was built for */
    uint16_t desc_size; /* sizeof(modx_desc_t) when built */
    char name[16];      /* module name, as in the .MOD header */
    uint32_t rw_size;   /* bytes of RW state it needs (zeroed at bind) */
    uint32_t init;      /* entry: image offset of the init function (Thumb bit set) */
    uint32_t image_size;/* bytes of code + constants (<= payload length) */
    uint32_t api_size;  /* bytes of modx_api_t it uses (<= the core's) */
} modx_desc_t;

struct modx_api;
struct modx_ops;
/* Entry point: fill *ops (functions of the module; they receive rw back),
 * return 0, or a negative number to refuse (e.g. the API is too old). */
typedef int (*modx_init_fn)(const struct modx_api *api, void *rw, struct modx_ops *ops);

/* What a bound module offers the core (filled by its init). Every function
 * gets the module's RW block. NULL entries are not called. */
typedef struct modx_ops {
    uint16_t size;
    void (*tick)(void *rw, uint32_t now, int scr);             /* main loop, while a hooked screen is shown */
    int (*event)(void *rw, int ev, int arg);                   /* MODX_EV_*; result per event */
    bool (*draw)(void *rw, int scr, int rect[4]);              /* overlay; rect: x, y, w, h drawn */
    int (*console)(void *rw, int argc, char **argv);           /* console command of the module */
    void (*state)(void *rw, void (*kv)(const char *, const char *, void *), void *user);
} modx_ops_t;

/* Events (modx_ops_t.event). */
enum {
    MODX_EV_QUERY = 0, /* -> MODX_ST_* flags */
    MODX_EV_INPUT,     /* a key or touch arrived: -> 1 if it was used to pause */
    MODX_EV_LEAVE,     /* the hooked screen was left */
    MODX_EV_SLEEP,     /* power-off / Standby is next */
    MODX_EV_TOGGLE,    /* play / pause */
    MODX_EV_PLAY,
    MODX_EV_PAUSE,
    MODX_EV_STOP,      /* stop and forget the progress (target changed) */
    MODX_EV_RECORD,    /* record the animation to the microSD card */
    /* modules with a screen of their own (app/mquest_core.c): input and life cycle */
    MODX_EV_ENTER,     /* the module's screen is shown (arg 1: returning from a message) */
    MODX_EV_KEY,       /* arg = key | mod << 8 (KEY_*, MOD_*) */
    MODX_EV_TOUCH,     /* arg = MODX_TOUCH(type, x, y) (TEV_* of core/touch.h) */
    /* appended (ABI 1): a swipe after the UP of its drag (core/touch.h TEV_SWIPE); older modules
     * ignore unknown events */
    MODX_EV_SWIPE,     /* arg = MODX_SWIPE(dir, x0, y0): dir 0 left, 1 right, 2 up, 3 down; where it began */
    /* appended (ABI 1): a scroll wheel or a trackpad's two-finger scroll (the web pages); older modules
     * ignore it */
    MODX_EV_SCROLL,    /* arg = MODX_SCROLL(dy, x, y): dy notches (+ down, -127..127) at (x, y) */
    /* appended (ABI 1): a typed character (arg: its ASCII, 0x20..0x7E, in the case it was typed: a
     * computer keyboard's printable key, the calculator's ALPHA letter with SHIFT ALPHA's upper case).
     * -> 1 if a text field took it; 0 (an older module too: unknown events return 0) and the core
     * sends the key as MODX_EV_KEY instead */
    MODX_EV_TEXT,
};
#define MODX_SCROLL(dy, x, y) ((int)(((unsigned)((dy) & 0xFF) << 20) | ((unsigned)(x) << 10) | (unsigned)(y)))
#define MODX_SCROLL_DY(a) ((int)(int8_t)(((unsigned)(a) >> 20) & 0xFFu))
#define MODX_SWIPE(dir, x0, y0) MODX_TOUCH(dir, x0, y0)
#define MODX_SWIPE_DIR(a) MODX_TOUCH_TYPE(a)
#define MODX_TOUCH(type, x, y) ((int)(((unsigned)(type) << 20) | ((unsigned)(x) << 10) | (unsigned)(y)))
#define MODX_TOUCH_TYPE(a) (((unsigned)(a) >> 20) & 0xFu)
#define MODX_TOUCH_X(a) ((int)(((unsigned)(a) >> 10) & 0x3FFu))
#define MODX_TOUCH_Y(a) ((int)((unsigned)(a) & 0x3FFu))
enum { MODX_ST_PLAYING = 1, MODX_ST_RECORDING = 2 };

/* The core's API table (ABI 1). Data pointers are in the core's memory;
 * strings are UTF-8. Functions may only be called from module code that the
 * core called (main loop). */
typedef struct anim_target anim_target_t; /* app/anim.h */
typedef struct modx_api {
    uint16_t abi, size;            /* MODX_ABI, sizeof(modx_api_t) */
    /* system */
    uint32_t (*millis)(void);
    uint32_t (*cycles)(void);
    uint32_t (*cycles_per_us)(void);
    void (*watchdog_kick)(void);
    bool (*input_pending)(void);   /* a key press is waiting (abort long work) */
    /* text and numbers */
    int (*snprintf)(char *out, size_t n, const char *fmt, ...);
    void (*fmt_num)(double v, int width, char *out, size_t n);
    int (*strcmp)(const char *a, const char *b);
    double (*floor)(double v);
    /* drawing (core/gfx.h), screen pixels, RGB565 (theme-mapped by gfx) */
    void (*fill_rect)(int x, int y, int w, int h, uint16_t c);
    void (*rect)(int x, int y, int w, int h, uint16_t c);
    int (*text)(const font_t *f, int x, int y, const char *s, uint16_t fg, int bg);
    int (*text_width)(const font_t *f, const char *s);
    const uint16_t *(*framebuffer)(void);  /* LCD_WIDTH x LCD_HEIGHT, what the panel shows */
    int (*flush)(void);                    /* push drawn pixels to the panel */
    const font_t *font_small, *font_medium;
    /* user interface */
    void (*toast)(const char *text);
    void (*message)(const char *title, const char *text);
    void (*softkeys_changed)(void);
    void (*con_printf)(const char *fmt, ...);
    int (*con_error)(const char *fmt, ...);
    bool (*con_parse_int)(const char *s, long *out);
    /* CAS (cas/include/cas.h): compile an expression in named variables and
     * evaluate it numerically */
    cas_ctx *(*cas)(void);
    cas_status (*cas_compile_vars)(cas_ctx *ctx, const char *expr, const char *const *vars, int nvars, cas_fn **out);
    double (*cas_fn_eval_vars)(const cas_fn *fn, const double *vals);
    void (*cas_fn_free)(cas_ctx *ctx, cas_fn *fn);
    /* graphing hooks (app/anim_core.c) */
    int (*anim_target)(anim_target_t *t);                 /* what the shown graph animates */
    uint32_t (*anim_apply)(int kind, double value, int grid); /* set it, draw a frame; -> us */
    void (*anim_finish)(int kind, bool reset);            /* paused/stopped: store, redraw */
    int (*g3_grid)(void);                                 /* the 3D grid setting */
    /* files on the microSD card: one file at a time */
    int (*file_create)(const char *prefix, const char *ext, char *path, size_t n); /* /CALC/PREFnnn.EXT */
    int (*file_write)(const void *buf, size_t n);         /* 0 or FS_E* */
    int (*file_close)(bool keep);                         /* publish (keep) or drop it */
    void (*card_error)(int err, char *msg, size_t n);
    /* ---- appended (ABI 1, size 2): full-screen modules such as Math Quest ---- */
    const font_t *font_large, *font_xlarge;
    void (*line)(int x0, int y0, int x1, int y1, uint16_t c);
    int (*held_key)(uint32_t *ms);                 /* key held down now and for how long (0: none) */
    int (*key_char)(int key, int mod);             /* ASCII the key types in that layer (digits, letters), 0 */
    /* saved bytes of a module (blob "modx.save", kept by the core across binds and power-off; one
     * module at a time, up to MODX_SAVE_MAX bytes) */
    size_t (*save_get)(const char *name, void *buf, size_t n);       /* -> bytes copied (0: none) */
    int (*save_put)(const char *name, const void *buf, size_t n);    /* 0, or -1 (too big / slot taken) */
    /* reading microSD files (the same one-file slot as file_create; close with file_close(false)) */
    int (*file_open)(const char *path, uint32_t *size);  /* 0 or FS_E* */
    int (*file_read)(void *buf, size_t n);               /* bytes read, 0 at the end, <0 FS_E* */
    int (*dir_list)(const char *dir, int (*cb)(const char *name, uint32_t size, void *user), void *user);
    /* the module's own screen (MODX_SCR_SCREEN, app/mquest_core.c) */
    void (*softkey)(int i, const char *label);     /* label of F1..F5 (i = 0..4; NULL: blank) */
    void (*leave)(void);                           /* back to the home screen */
    /* ---- appended (ABI 1, size 3): Math Quest's home (rooms and a photo of them) ---- */
    size_t (*save_max)(void);                      /* bytes save_put takes (MODX_SAVE_MAX of this core) */
    int (*screenshot)(char *msg, size_t n);        /* the screen as /CALC/SHOTnnn.PNG: 0 or <0, msg says */
    /* ---- appended (ABI 1, size 4): CAS extension modules (cas/src/casx.h; modules/casx) ---- */
    const void *(*casx_api)(void);                 /* the CAS extension table (casx_api_t), NULL: none */
    /* ---- appended (ABI 1, size 5): games -- pinch scale, sound hooks, the display scale ---- */
    int (*touch_scale)(void);                      /* the scale of the pinch being reported (TEV_PINCH /
                                                      TEV_PINCH_END), x 256 (256 = no change) */
    void (*sfx)(int id, int param);                /* a one-shot sound effect (MODX_SFX_*): no-op on the
                                                      calculator, logged by the simulator, forwarded to the
                                                      page's calcSound(id, param) by the web build */
    void (*music)(int track, int intensity);       /* a music loop (MODX_MUS_*, 0 = silence), intensity 0..3 */
    int (*display_scale)(void);                    /* physical pixels per logical pixel (1 on the calculator) */
    /* ---- appended (ABI 1, size 6): smooth drawing on a high-resolution display ---- */
    int (*hr)(int op, const int32_t *a, int n, const void *p); /* MODX_HR_*: 0 = not available */
    /* ---- appended (ABI 1, size 7): bigger saves (MODX_SAVE_MAX 96 KB; save_max() says it) ---- */
    size_t (*save_size)(const char *name);         /* bytes kept for `name` (0: none), before save_get */
    /* ---- appended (ABI 1, size 8): talking characters ---- */
    void (*speak)(int who, const char *text);      /* a character says `text` (a short line; the caption is the
                                                      module's): who = cast slot (0..8; visitors 9..16) |
                                                      MODX_SPEAK_WORDS to read it aloud in words (else a cute
                                                      babble) | MODX_SPEAK_NOW to interrupt. No-op on the
                                                      calculator, logged by the simulator (state key
                                                      modx_speak), forwarded to the page's calcSpeak(who, text) */
    /* ---- appended (ABI 1, size 9): progress worth keeping now (a round won, a purchase): the core writes the
     * session at its next quiet moment, debounced (app/session.h session_save_soon) ---- */
    void (*save_soon)(void);
    /* ---- appended (ABI 1, size 10): the Geometry app (modules/geometry; its own table and version,
     * app/geo_core.h). Since this size the core also keeps MODX_RW (below) while it calls a module. ---- */
    const struct geo_core *(*geo_core)(void);
    /* ---- appended (ABI 1, size 11): Python (modules/python; its own table and version, app/py_core.h) ---- */
    const struct py_core *(*py_core)(void);
} modx_api_t;
#define MODX_SPEAK_WORDS 0x100
#define MODX_SPEAK_NOW 0x200
/* High-resolution drawing (modx_api_t.hr; the standalone Sparkle Town page, web/page/sparkle_*):
 * at display_scale() > 1 a module brackets a shape, a text or a picture that it is about to draw
 * with rectangles: hr(MODX_HR_SHAPE / TEXT / IMAGE, ...) before, hr(MODX_HR_END, 0, 0, 0) after.
 * A result of 1 means the platform drew it smoothly itself (anti-aliased shape, a web font, the
 * high-resolution picture of the same art) and keeps the bracketed rectangles out of the
 * high-resolution image (they still reach the 1x framebuffer: tests, screenshots); 0 means draw it
 * as usual (no END needed). Every call is optional; the calculator returns 0 (or has no entry).
 * a[]: a[0] the kind / style, a[1] the colour (RGB565; -1 none), a[2], a[3] a clip band in physical
 * rows (y0, y1; y1 = 0: none), then coordinates in physical pixels x 16 (sub-pixel). */
enum {
    MODX_HR_END = 0,
    MODX_HR_SHAPE,  /* a[0] MODX_SH_*; a[4..]: CIRCLE cx cy r | RING cx cy r w | RRECT x y w h r |
                       LINE x0 y0 x1 y1 w | POLY n points x y (even-odd) */
    MODX_HR_TEXT,   /* a[0] the style (0 small .. 3 big), a[4..7] x y w h of the 1x layout box; p: UTF-8 */
    MODX_HR_IMAGE,  /* p: modx_hr_img_t; a[4..] x y (top-left), the size of an art pixel, w h, flags
                       (bits 0-1 a quarter turn, bit 2 mirrored), shear (top row, x16), height x16 / 16 */
    MODX_HR_SCENE,  /* a[0] a scene id of the module, a[1..3] sky, ground and accent colours: what the
                       platform may extend beside the 4:3 play area (no END) */
    /* appended: a whole thing the module draws from many shapes (a building, a background, a
     * bubble, a squishy...), named so a platform with art of its own for it (the Mac page's vector
     * set) can draw that instead. p: its id (a C string, "town/bakery"); a[4..7] x y w h of its
     * box; a[8] bit 0 mirrored, bit 1 upside down; a[9] the module's animation clock (ms); a[10]
     * the tint (0, 1 dusk, 2 night); a[11] n; a[12], a[13] the box's width and height in the
     * module's logical pixels, a[14..12+n) its own parameters. 1: drawn (END
     * after), 0: no art for it here, draw it as usual. */
    MODX_HR_SPRITE,
};
enum { MODX_SH_CIRCLE = 1, MODX_SH_RING, MODX_SH_RRECT, MODX_SH_LINE, MODX_SH_POLY };
typedef struct {
    const uint8_t *px;    /* indices, row by row (stride bytes a row) */
    const uint16_t *pal;  /* index -> RGB565 (index 0 transparent) */
    int16_t stride;
    uint8_t bpp;          /* 8, or 4 (low nibble first) */
} modx_hr_img_t;
/* Sound ids (stable: never renumbered; docs/MATHQUEST.md "Sound hooks"). */
enum {
    MODX_SFX_POP = 1, MODX_SFX_COIN, MODX_SFX_COMBO, MODX_SFX_WHOOSH, MODX_SFX_SQUISH, MODX_SFX_UNLOCK,
    MODX_SFX_GIFT, MODX_SFX_CHEER, MODX_SFX_DUD, MODX_SFX_STEP, MODX_SFX_JUMP, MODX_SFX_BOSS, MODX_SFX_TAP,
    MODX_SFX_LAUNCH, MODX_SFX_LAND, MODX_SFX_BOOST, MODX_SFX_BONGO_LO, MODX_SFX_BONGO_HI, MODX_SFX_COWBELL,
    MODX_SFX_CLAP,
    /* appended (Sparkle Town's characters and new games): a friend's voice (param: cast slot x 16 +
     * syllables, 1..15), two things merging (param: the new level), a fanfare (a chapter's payoff, a
     * festival), a dial's click (param: the digit) */
    MODX_SFX_VOICE, MODX_SFX_MERGE, MODX_SFX_FANFARE, MODX_SFX_CLICK,
    /* appended (Bongo Beats' cat band): a soft mew (param: the MIDI note | eighths << 7 | 1 << 12 a held
     * "meeeow", 2 << 12 a pickup "mrrp"),
     * a happy trill (a combo; param: the combo), a big meow (the song's finale), a paw's tap on a
     * bongo (param: 0 the low drum, 1 the high one) */
    MODX_SFX_MEW, MODX_SFX_TRILL, MODX_SFX_MEOW, MODX_SFX_PAW,
    /* the songs' band: a bass note, a chord (param: the note | eighths << 7 | minor << 14), a tambourine */
    MODX_SFX_BASS, MODX_SFX_CHORD, MODX_SFX_TAMB,
    /* appended (the squishies): a ticklish giggle (param: the squishy kind | the squeeze 0..3 << 4 | 1 << 6 a
     * slow press building), a happy sigh on letting go (param: the kind | 1 << 4 a wobble-boing) */
    MODX_SFX_GIGGLE, MODX_SFX_SIGH,
    MODX_SFX_COUNT
};
enum { MODX_MUS_OFF = 0, MODX_MUS_TITLE, MODX_MUS_MAP, MODX_MUS_CAFE, MODX_MUS_ARCADE, MODX_MUS_BOSS, MODX_MUS_WINDDOWN,
       MODX_MUS_PLAY, MODX_MUS_RHYTHM, MODX_MUS_COUNT }; /* RHYTHM: intensity = the tempo step, 90/110/130 bpm,
                                                    from the moment it is asked for (the beat clock) */
/* the sound log of the simulators (state keys modx_sfx, modx_music; console `modx sfx [clear]`) */
const char *modx_sfx_name(int id);
void modx_set_touch_scale(int scale256); /* app/mquest_core.c: the pinch being forwarded */
/* A high-resolution display (the standalone Sparkle Town web page, platform/web/web_sparkle.c): it
 * takes the modules' drawing calls (physical pixels at `scale`) and answers hr(). Without one (the
 * calculator, the simulators) the calls go to the framebuffer and display_scale() is 1. Set it
 * before a module binds: a module reads the scale when it starts. */
typedef struct {
    int scale;
    void (*fill_rect)(int x, int y, int w, int h, uint16_t c);
    void (*rect)(int x, int y, int w, int h, uint16_t c);
    int (*text)(const font_t *f, int x, int y, const char *s, uint16_t fg, int bg);
    void (*line)(int x0, int y0, int x1, int y1, uint16_t c);
    int (*hr)(int op, const int32_t *a, int n, const void *p);
} modx_display_t;
void modx_set_display(const modx_display_t *d);
/* Bytes of saved state the core keeps for a module: 6 KB up to API size 2; since size 3
 * save_max() reports it (16 KB up to size 6, 96 KB since size 7). A module that needs more
 * than 6 KB requires size 3 and refuses to start when save_max() is less than it needs, so
 * a save is never cut. The session blob "modx.save" is version 2 (u32 length) since 96 KB;
 * version 1 (u16 length) still loads. */
#define MODX_SAVE_MAX (96u << 10)

extern const modx_api_t modx_api;

/* Code-load modules (module kind MOD_KIND_CODE_LOAD, the "python" module): the
 * image is not run in place but copied into an executable SDRAM area (the
 * last CONFIG_MODX_CODE_KB of the arena, MPU region 5 on the hardware),
 * relocated and run there, so it may have ordinary static data and tables of
 * pointers. Its descriptor is longer: image_size covers code, constants and
 * initialised data (copied); bss_size zeroed bytes follow; the payload ends
 * with reloc_count words at reloc_off, each the image offset of a 32-bit word
 * that holds a link address (the image is linked at 0): the loader adds the
 * load address. modules/modx_load.ld links such an image, tools/modreloc.py
 * writes the list. An older core refuses the kind (MOD_ABI). */
typedef struct {
    modx_desc_t d;        /* d.desc_size = sizeof(modx_load_desc_t) */
    uint32_t bss_size;    /* zeroed after the image */
    uint32_t reloc_off;   /* payload offset of the relocation list (u32 image offsets) */
    uint32_t reloc_count;
    uint32_t flags;       /* 0 */
} modx_load_desc_t;

/* Image build (modules/NAME/, -DMODX_IMAGE): the descriptor at offset 0. The
 * image is linked at address 0, so a function's link address is its offset
 * (the R_ARM_ABS32 relocations of the descriptor are the only absolute ones
 * the image may contain: tools/modlink.py). Host builds: nothing; the module
 * is listed in modx_host[] (modx.c). */
#if defined(MODX_IMAGE)
extern const uint8_t __modx_end[];
#define MODX_DESCRIPTOR(nm, rwsize, fn)                                                                 \
    __attribute__((section(".modx_desc"), used)) const modx_desc_t modx_descriptor = {                \
        MODX_MAGIC, MODX_ABI, sizeof(modx_desc_t), nm, rwsize, (uint32_t)(uintptr_t)&fn,               \
        (uint32_t)(uintptr_t)__modx_end, sizeof(modx_api_t)}
#else
#define MODX_DESCRIPTOR(nm, rwsize, fn) extern int modx_descriptor_unused
#endif

/* The RW block of the module being called (since API size 10): every call into
 * a module (the trampolines below, init at bind, modx_call_begin) points one
 * word at its RW block and restores the previous value afterwards, so a module
 * whose code cannot pass its state through every function (the geometry
 * module) finds it with MODX_RW(). On the hardware the word is at a fixed
 * address, the first word of DTCM (the linker script puts section .modx_ctx
 * there): an image has no other way to reach core data, and a constant
 * address needs no relocation. Host builds and modules linked into the core
 * tier read the variable itself. */
#define MODX_CTX_ADDR 0x20000000u
#if defined(MODX_IMAGE)
#define MODX_RW() (*(void *const *)MODX_CTX_ADDR)
#else
extern void *modx_ctx;
#define MODX_RW() (modx_ctx)
#endif

/* ---- core side ---- */
size_t modx_mem_init(void *mem, bool big); /* app_init: the RW pool (bytes taken) */
/* app_init: the code area of code-load modules, taken from the end of the arena
 * [.., end) when `room` bytes are left there (0: none); returns the bytes taken */
size_t modx_code_init(void *end, size_t room);
/* The bound module `name`, binding it on first use; NULL if it is not
 * available (modx_why says why). */
typedef struct modx_mod modx_mod_t;
modx_mod_t *modx_get(const char *name);
modx_mod_t *modx_peek(const char *name);  /* bound already? (never binds) */
const char *modx_why(const char *name);   /* "Install the Animation module (Memory > Modules)." */
const char *modx_title(const char *name); /* display name: "Animation" */
void modx_unbind_all(void);               /* before any change of the module store */
void modx_forget_all(void);               /* the same without calling any module (boot) */
bool modx_busy(void);                     /* module code is on the stack */
/* the module screen (app/mquest_core.c): API softkey / leave */
void modx_screen_softkey(int i, const char *label);
void modx_screen_leave(void);
/* the Notebook app's card files (app/py_core.c NBIO_SD_*): 0 create, 1 remove, 2 list (long names) */
int modx_nb_sd(int op, void *p);
#define MQ_MODULE "mathquest"
#define CASX_MODULE "casx" /* the advanced CAS extension (modules/casx, cas/ext/adv) */
/* Calls into a bound module through its own tables (app/geo_core.c): count it busy and point
 * MODX_RW at its RW block; begin returns what modx_call_end restores (calls nest). */
void *modx_call_begin(modx_mod_t *m);
void modx_call_end(void *prev);
/* trampolines (count the module busy around the call) */
void modx_tick(modx_mod_t *m, uint32_t now, int scr);
int modx_event(modx_mod_t *m, int ev, int arg);
bool modx_draw(modx_mod_t *m, int scr, int rect[4]);
void modx_state(modx_mod_t *m, void (*kv)(const char *, const char *, void *), void *user);
int modx_console_mod(modx_mod_t *m, int argc, char **argv);
int modx_console(int argc, char **argv);  /* console `modx list|off N|on N|bind N` */
void modx_status(void (*kv)(const char *, const char *, void *), void *user); /* state keys */
#if !defined(PLATFORM_STM32) && defined(CONFIG_CONTENT_QSPI)
/* host: link a module's init under its name (unit tests add their own) */
void modx_host_register(const char *name, modx_init_fn init, uint32_t rw_size);
#endif

/* appended: more words of the high-resolution hooks (modx_api_t.hr), read only when n has them.
 *   MODX_HR_IMAGE a[12] (n >= 13): the tint the module draws the picture's pixels with (0, 1 dusk,
 *     2 night), as for MODX_HR_SPRITE a[10];
 *   MODX_HR_TEXT a[0] bits 4-5: where the text sits in its 1x layout box when the platform's font
 *     is wider or narrower (MODX_HR_ALIGN_*); the style is a[0] & 15. */
enum { MODX_HR_ALIGN_LEFT, MODX_HR_ALIGN_CENTRE, MODX_HR_ALIGN_RIGHT };
#define MODX_HR_TEXT_STYLE(a0) ((a0) & 15)
#define MODX_HR_TEXT_ALIGN(a0) (((a0) >> 4) & 3)

/* appended: how wide the platform draws a text (an hr op after MODX_HR_SPRITE; no END).
 *   a[0] the style (0 small .. 3 big), a[1] the height of its 1x layout box (logical pixels: the 1x
 *   font's); p: UTF-8. Returns the width the platform's font has at its natural size in such a box,
 *   in logical pixels x 16 (0: not known -- lay out by the 1x font). A sentence the module builds from
 *   runs ("That's " "12c" "!": a colour or a drawn sign of its own each) is then set side by side as
 *   the platform's font has it, so no gap opens between the runs. */
enum { MODX_HR_MEASURE = MODX_HR_SPRITE + 1 };

#endif

