/* WebAssembly (Emscripten) implementation of hal.h, plus the exported API the
 * browser page and the node tests call (see web_api.h / README "Web simulator").
 *
 * Nothing here blocks: the host (requestAnimationFrame in the browser, a loop
 * in node) calls web_frame(dt_ms), which advances a virtual clock and runs
 * main-loop iterations (calc_step) until the app is idle. Time is virtual and
 * fully host-driven, so the same key sequence always draws the same pixels.
 *
 * Like the macOS simulator (platform/sim/sim_hal.c), the panel is modelled by
 * a separate `gram` array filled by hal_display_flush(), storage is RAM with
 * NOR semantics, and telemetry is fixed. Differences: no files (the flash and
 * the SD card are lost on reload), the console is a pair of in-memory buffers,
 * and `screenshot` is carried out by the host (cmd_screenshot below).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "calc.h"
#include "console.h"
#include "fat.h"
#include "gfx.h"
#include "session.h"
#include "hal.h"
#include "hostdev.h"
#include "usbhost.h"
#include "keyq.h"
#include "touch.h"
#include "web_api.h"

#define WEB_ARENA_SIZE (24u * 1024 * 1024)
#define WEB_FLASH_SIZE (16u * 1024 * 1024)
#define WEB_MAX_STEPS 4096 /* safety bound for one web_frame (never hang the browser tab) */

static uint16_t framebuffer[LCD_WIDTH * LCD_HEIGHT];
static uint16_t gram[LCD_WIDTH * LCD_HEIGHT];          /* what the "LCD" shows */
static uint8_t rgba[LCD_WIDTH * LCD_HEIGHT * 4];        /* gram as RGBA8888 for <canvas> */
static bool display_on = true;
static int backlight = HAL_BACKLIGHT_DEFAULT_PCT;
static uint32_t vtime_ms;
static uint64_t keyraw;
static uint32_t lcd_serial; /* bumps whenever the visible panel changes */
static uint8_t *flash;
static bool started;

/* console: input ring fed by web_console_input(), output buffer read by web_console_take() */
static char con_in[16384];
static size_t con_in_head, con_in_tail; /* free-running indices, used modulo sizeof con_in */
static char *con_out;
static size_t con_out_len, con_out_cap;

/* ------------------------------------------------------------- system --- */
void hal_init(void) { memset(gram, 0, sizeof gram); }
const char *hal_platform_name(void) { return "web"; }
void hal_reset(void) { con_log("web: reset requested (ignored; reload the page)"); }
void hal_enter_bootloader(void) { con_log("web: bootloader requested (ignored)"); }
uint16_t *hal_framebuffer(void) { return framebuffer; }

void *hal_arena(size_t *size)
{
    static void *arena;
    if (!arena)
        arena = calloc(1, WEB_ARENA_SIZE);
    *size = WEB_ARENA_SIZE;
    return arena;
}

uint32_t hal_irq_save(void) { return 0; }
void hal_irq_restore(uint32_t s) { (void)s; }

/* -------------------------------------------------------------- timer --- */
uint32_t hal_millis(void) { return vtime_ms; }
void hal_delay_ms(uint32_t ms) { vtime_ms += ms; } /* virtual: never spins */
/* Virtual 400 MHz cycle counter with only ms resolution; wraps like the real
 * 32-bit counter (every ~10.7 s), so only short differences are meaningful. */
uint32_t hal_cycles(void) { return vtime_ms * 400000u; }
uint32_t hal_cycles_per_us(void) { return 400; }

/* ------------------------------------------------------------ display --- */
/* Copy the rectangle into the simulated panel and bump lcd_serial so the page
 * knows to repaint its <canvas>. */
void hal_display_flush(const uint16_t *fb, int x, int y, int w, int h)
{
    for (int j = 0; j < h; j++)
        memcpy(&gram[(y + j) * LCD_WIDTH + x], &fb[(y + j) * LCD_WIDTH + x], (size_t)w * 2);
    lcd_serial++;
}

void hal_display_enable(bool on)
{
    display_on = on;
    lcd_serial++;
}

void hal_backlight_set(int percent)
{
    backlight = percent < 0 ? 0 : percent > 100 ? 100 : percent;
    lcd_serial++;
}
int hal_backlight_get(void) { return backlight; }

/* ------------------------------------------------------------- keypad --- */
uint64_t hal_keypad_raw(void) { return keyraw; }

/* -------------------------------------------------------------- power --- */
void hal_idle(void) {} /* the host's frame loop is the idle */

/* Save the session (to the RAM flash) and blank the panel; the main loop then
 * stays off until ON (web_is_off() reports it). */
void hal_power_off(void)
{
    platform_hibernate_save();
    display_on = false;
    lcd_serial++;
}

/* Same fixed values as the macOS simulator (USB powered, fresh batteries). */
void hal_telemetry(hal_telemetry_t *t)
{
    memset(t, 0, sizeof *t);
    t->valid_pmon = true;
    t->vsys_v = 5.10f;
    t->isys_ma = display_on ? 95.0f + (float)backlight * 0.6f : 0.035f;
    t->psys_mw = t->vsys_v * t->isys_ma;
    t->vbat_v = 5.6f;
    t->v3v3_v = 3.30f;
    t->v3v3_bus_v = display_on ? 3.29f : 0.0f;
    t->bl_ma = display_on ? (float)backlight * 0.9f : 0.0f;
    t->temp_c = 31.0f;
    t->usb_present = true;
}

/* hal_usb_*: platform/hostdev/usbhost.c */

bool hal_crash_text(char *out, size_t n)
{
    (void)out, (void)n;
    return false;
}

/* -------------------------------------------------------------- touch --- */
bool hal_touch_available(void) { return true; }
void hal_service(void) {}
/* Emscripten stack is 1 MB (-sSTACK_SIZE in the Makefile); the CAS default for
 * non-ARM builds (2 MB) would overflow it silently. */
size_t hal_cas_stack_limit(void) { return 768u << 10; }

/* ------------------------------------------------------------ storage --- */
/* RAM-backed NOR (lost on reload), allocated on first use. */
static int flash_init(void)
{
    if (!flash) {
        flash = malloc(WEB_FLASH_SIZE);
        if (!flash)
            return -1;
        memset(flash, 0xFF, WEB_FLASH_SIZE);
        hostdev_flash_attach(flash, WEB_FLASH_SIZE, NULL); /* the full manual for the demo */
    }
    return 0;
}

size_t hal_storage_size(void) { return hostdev_flash_size(WEB_FLASH_SIZE); }

int hal_storage_read(uint32_t off, void *buf, size_t n)
{
    if (flash_init() || off + n > hal_storage_size())
        return -1;
    if (hostdev_flash_read(buf, n))
        return 0;
    memcpy(buf, flash + off, n);
    return 0;
}

int hal_storage_erase(uint32_t off, size_t n)
{
    if (flash_init() || off % HAL_STORAGE_SECTOR || n % HAL_STORAGE_SECTOR || off + n > hal_storage_size() ||
        hostdev_flash_modify())
        return -1;
    memset(flash + off, 0xFF, n);
    return 0;
}

int hal_storage_reset(void) { return 0; } /* (a file: nothing to reset) */
int hal_storage_write(uint32_t off, const void *buf, size_t n)
{
    if (flash_init() || off + n > hal_storage_size() || hostdev_flash_modify())
        return -1;
    const uint8_t *b = buf;
    for (size_t i = 0; i < n; i++)
        flash[off + i] &= b[i]; /* NOR semantics: programming only clears bits */
    return 0;
}

/* ------------------------------------------------------------ console --- */
int hal_console_read(char *buf, int max)
{
    int n = 0;
    while (n < max && con_in_tail != con_in_head) {
        buf[n++] = con_in[con_in_tail % sizeof con_in];
        con_in_tail++;
    }
    return n; /* never -1: the page never "ends" */
}

/* Append to the output buffer (grown by doubling, kept NUL-terminated) until
 * the host collects it with web_console_take(). On out-of-memory the text is
 * dropped. */
void hal_console_write(const char *s, size_t n)
{
    if (con_out_len + n + 1 > con_out_cap) {
        size_t cap = con_out_cap ? con_out_cap : 4096;
        while (cap < con_out_len + n + 1)
            cap *= 2;
        char *p = realloc(con_out, cap);
        if (!p)
            return;
        con_out = p;
        con_out_cap = cap;
    }
    memcpy(con_out + con_out_len, s, n);
    con_out_len += n;
    con_out[con_out_len] = 0;
}

/* --------------------------------------------- platform console cmds --- */
static int cmd_lcdcheck(int argc, char **argv)
{
    int bad = 0;
    for (int i = 0; i < LCD_WIDTH * LCD_HEIGHT; i++)
        bad += gram[i] != framebuffer[i];
    con_printf("mismatched_pixels=%d\n", bad);
    return bad ? con_error("panel differs from framebuffer") : 0;
}

static int cmd_simtime(int argc, char **argv)
{
    con_printf("virtual=1\nms=%lu\n", (unsigned long)hal_millis());
    return 0;
}

static int cmd_screenshot(int argc, char **argv)
{
    /* The host saves the image: it runs scripts one line at a time through
     * web_console_run(), recognises `screenshot` lines itself and reads the
     * panel (web_lcd_rgba / web_framebuffer) after the line has run
     * (web/test/run_web_test.js). Here the command only echoes the name. */
    if (argc < 2)
        return con_error("usage: screenshot FILE.png [fb]");
    con_printf("file=%s\n", argv[1]);
    return 0;
}

const console_cmd hal_console_cmds[] = {
    {"screenshot", "FILE.png [fb]", "(web) host saves the LCD; see web_console_run", cmd_screenshot},
    {"lcdcheck", "", "check panel == framebuffer (dirty-rect test)", cmd_lcdcheck},
    {"simtime", "", "simulator clock info", cmd_simtime},
    {"usb", "host on|off|vbus on|off|ls|cat|expect|drop ...", "the USB model and a computer (platform/hostdev)",
     hostdev_cmd_usb},
    {"fault", "qspi ok|absent|dead ff|dead 00|cut N | sd ok|dead", "inject device faults (platform/hostdev)",
     hostdev_cmd_fault},
    {0, 0, 0, 0},
};

/* ================================================================ web API */
/* Run main-loop iterations until one does no work (or WEB_MAX_STEPS). Virtual
 * time does not advance here, so a pending console `wait` only progresses
 * across web_frame() calls (or inside web_console_run). */
static void run_until_idle(void)
{
    for (int i = 0; i < WEB_MAX_STEPS; i++)
        if (!calc_step())
            break;
}

WEB_EXPORT int web_init(void)
{
    if (started)
        return 0;
    started = true;
    hal_init();
    calc_setup();
    run_until_idle();
    return 0;
}

WEB_EXPORT void web_frame(uint32_t dt_ms)
{
    if (!started)
        web_init();
    if (dt_ms > 1000)
        dt_ms = 1000; /* tab was in the background: don't fast-forward minutes */
    vtime_ms += dt_ms;
    run_until_idle();
}

WEB_EXPORT int web_key_from_name(const char *name) { return key_from_name(name); }
WEB_EXPORT int web_key_count(void) { return KEY_COUNT; }
WEB_EXPORT const char *web_key_name(int key) { return key > 0 && key < KEY_COUNT ? key_names[key] : ""; }

/* Press or release a key as the keypad scanner would: update the raw matrix
 * bits (key_pos is row << 4 | col; 0xFF = the ON key, bit 49) and queue the
 * event. */
WEB_EXPORT void web_key(int key, int down)
{
    if (key <= KEY_NONE || key >= KEY_COUNT)
        return;
    int bit = key_pos[key] == 0xFF ? 49 : (key_pos[key] >> 4) * 7 + (key_pos[key] & 15);
    if (down)
        keyraw |= 1ull << bit;
    else
        keyraw &= ~(1ull << bit);
    keyq_push((uint8_t)key, down != 0); /* ON press also runs the CAS-interrupt hook */
    run_until_idle();
}

WEB_EXPORT const uint16_t *web_framebuffer(void) { return framebuffer; }
WEB_EXPORT const uint16_t *web_panel(void) { return gram; }
WEB_EXPORT uint32_t web_lcd_serial(void) { return lcd_serial; }
WEB_EXPORT int web_display_on(void) { return display_on; }
WEB_EXPORT int web_backlight(void) { return backlight; }
WEB_EXPORT uint32_t web_millis(void) { return vtime_ms; }
WEB_EXPORT int web_is_off(void) { return calc_is_off(); }
WEB_EXPORT const char *web_version(void) { return calc_version(); }

WEB_EXPORT const uint8_t *web_lcd_rgba(void)
{
    /* RGB565 -> RGBA8888 with bit replication (same expansion as png.c). The
     * panel is black while the display is off. */
    for (int i = 0; i < LCD_WIDTH * LCD_HEIGHT; i++) {
        uint16_t p = display_on ? gram[i] : 0;
        uint8_t r = (uint8_t)((p >> 11) & 31), g = (uint8_t)((p >> 5) & 63), b = (uint8_t)(p & 31);
        rgba[i * 4 + 0] = (uint8_t)((r << 3) | (r >> 2));
        rgba[i * 4 + 1] = (uint8_t)((g << 2) | (g >> 4));
        rgba[i * 4 + 2] = (uint8_t)((b << 3) | (b >> 2));
        rgba[i * 4 + 3] = 255;
    }
    return rgba;
}

/* Queue console input; all-or-nothing (-1 if it does not fit in the ring). */
WEB_EXPORT int web_console_input(const char *text)
{
    size_t n = strlen(text);
    if (n > sizeof con_in - (con_in_head - con_in_tail))
        return -1;
    for (size_t i = 0; i < n; i++)
        con_in[con_in_head++ % sizeof con_in] = text[i];
    return 0;
}

WEB_EXPORT const char *web_console_take(void)
{
    /* Returns everything written since the last call (valid until the next call). */
    static char *taken;
    free(taken);
    taken = con_out ? con_out : calloc(1, 1);
    con_out = NULL;
    con_out_len = con_out_cap = 0;
    return taken;
}

extern bool console_waiting(void);

WEB_EXPORT int web_console_run(const char *line)
{
    /* Run one console line to completion with the native simulator's
     * deterministic semantics: virtual time advances only while `wait` is
     * pending, 10 ms per idle iteration. Output via web_console_take().
     * Lines longer than ~1 KB are truncated. Returns 0 once the line (and
     * anything queued before it) has run and the loop is idle; -1 if it could
     * not be queued or the iteration guard ran out. */
    char buf[1100];
    snprintf(buf, sizeof buf, "%s\n", line);
    if (web_console_input(buf))
        return -1;
    for (int guard = 0; guard < 10000000; guard++) {
        if (calc_step())
            continue;
        if (console_waiting()) {
            vtime_ms += 10;
            continue;
        }
        if (con_in_head == con_in_tail)
            return 0;
    }
    return -1;
}

/* Touch on the display (the capacitive-touch panel variant): pointer events
 * from the page go through the same gesture recognizer as the hardware touch
 * controller (core/touch.c). state: 1 = down, 2 = move, 0 = up. web_touch is
 * the primary pointer (contact 0); web_touch_id adds more fingers (pinch). */
static int touch_x = -1, touch_y = -1, touch_state;
WEB_EXPORT void web_touch_id(int id, int x, int y, int state)
{
    x = x < 0 ? 0 : x >= LCD_WIDTH ? LCD_WIDTH - 1 : x;
    y = y < 0 ? 0 : y >= LCD_HEIGHT ? LCD_HEIGHT - 1 : y;
    if (id == 0) {
        touch_x = x, touch_y = y;
        touch_state = state;
    }
    touch_inject(id, x, y, state);
    run_until_idle();
}
WEB_EXPORT void web_touch(int x, int y, int state) { web_touch_id(0, x, y, state); }
WEB_EXPORT int web_touch_state(void) { return touch_state ? (touch_y << 16 | touch_x) : -1; }

WEB_EXPORT int web_expect_failures(void) { return console_expect_failures; }

/* A file onto the simulated microSD card (the card must be inserted and formatted: console
 * `fs insert MB`, `fs format`); its folders are made as needed. 0 or an FS_E* error. */
WEB_EXPORT int web_write_file(const char *path, const uint8_t *data, uint32_t len)
{
    char dir[96];
    int r = 0;
    for (size_t i = 1; path[i] && i < sizeof dir; i++) /* the folders on the way */
        if (path[i] == '/') {
            memcpy(dir, path, i), dir[i] = 0;
            if ((r = fs_mkdir(dir)) != 0)
                break;
        }
    fs_file_t f;
    if (!r)
        r = fs_open(&f, path, FS_WRITE);
    for (uint32_t o = 0; !r && o < len;) {
        uint32_t n = len - o > 4096 ? 4096 : len - o;
        if (fs_write(&f, data + o, n) != (int)n)
            r = -1;
        o += n;
    }
    if (!r)
        r = fs_close(&f);
    fs_unmount();
    run_until_idle();
    return r;
}

/* The saved session (what a power cycle keeps: settings, variables, saved module bytes) as
 * of now, for the page to keep in localStorage. *len its bytes; the pointer stays valid until
 * the next call that runs the calculator. */
WEB_EXPORT const uint8_t *web_session_get(uint32_t *len)
{
    session_save(true);
    size_t n = 0;
    const uint8_t *p = session_snapshot(&n);
    *len = (uint32_t)n;
    return p;
}

/* Where the page puts a kept session before web_session_put (cap: its size). */
WEB_EXPORT uint8_t *web_session_buffer(uint32_t *cap)
{
    size_t c = 0;
    uint8_t *b = session_buffer(&c);
    *cap = (uint32_t)c;
    return b;
}

/* Install the session in the buffer (len bytes) and restart the app with it, as after a
 * power cycle. 0, or <0 if it is not a session. */
WEB_EXPORT int web_session_put(uint32_t len)
{
    int r = session_install(len);
    if (r) {
        session_save(true); /* the buffer was overwritten: keep the current session */
        return r;
    }
    app_resume();
    run_until_idle();
    return 0;
}
