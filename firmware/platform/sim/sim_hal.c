/* Simulator implementation of hal.h (macOS / POSIX).
 *
 * The whole app, console and CAS run unchanged on the host; this file stands in
 * for the hardware:
 *   - display: the framebuffer is a static array; hal_display_flush() copies the
 *     dirty rectangle into a separate `gram` array that plays the panel's GRAM,
 *     so `lcdcheck` can verify that dirty-rectangle tracking (core/gfx.c) left
 *     the panel identical to the framebuffer;
 *   - console: stdin/stdout, or a --script file;
 *   - time: wall clock when interactive; a virtual clock otherwise, advanced only
 *     by hal_delay_ms() and while the console runs `wait` (see hal_idle), so a
 *     test script produces the same screens however fast the host is;
 *   - storage: 16 MB of RAM with NOR semantics (erase to 0xFF, writes can only
 *     clear bits), optionally mirrored to a file (--flash), preloaded with this
 *     build's QSPI content image unless --no-content, with injectable faults
 *     (platform/hostdev: console `fault`);
 *   - telemetry: fixed plausible values (USB powered, fresh batteries).
 * Keypad and touch input arrive through console commands (core/console.c). */
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "console.h"
#include "gfx.h"
#include "hal.h"
#include "hostdev.h"
#include "keyq.h"
#include "usbhost.h"
#include "png.h"
#include "sim.h"

sim_options_t sim_opt = {.input_fd = 0};

static uint16_t framebuffer[LCD_WIDTH * LCD_HEIGHT];
static uint16_t gram[LCD_WIDTH * LCD_HEIGHT]; /* what the "LCD" shows */
static bool display_on = true;
static int backlight = HAL_BACKLIGHT_DEFAULT_PCT;
static uint64_t vtime_ms;    /* virtual time */
static uint64_t real_start_ns;
static bool input_eof;       /* console input reached EOF: main loop exits */
static uint8_t *flash;       /* simulated NOR, allocated on first storage call */
static int flash_fd = -1;    /* --flash backing file, written through on every change */
static bool live_dirty;      /* panel changed since the last --live PNG */
static uint64_t live_last_ns;
static unsigned long flush_count; /* hal_display_flush calls, reported by lcdcheck */

#define SIM_ARENA_SIZE (24u * 1024 * 1024) /* app + CAS arena (hardware: ~32 MB SDRAM) */
#define SIM_FLASH_SIZE (16u * 1024 * 1024) /* same as the W25Q128 QSPI NOR */

/* Monotonic wall-clock time in ns. */
static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* ------------------------------------------------------------- system --- */
void hal_init(void)
{
    real_start_ns = now_ns();
    /* Scripted or piped input = a test: use the deterministic clock. */
    if (sim_opt.input_fd != 0 || !isatty(0))
        sim_opt.virtual_time = !sim_opt.realtime;
    hostdev_wdg_model = sim_opt.virtual_time; /* the watchdog model needs the virtual clock */
    memset(gram, 0, sizeof gram);
}

const char *hal_platform_name(void) { return "sim"; }

void hal_reset(void)
{
    con_log("sim: reset requested (exiting)");
    exit(0);
}

void hal_enter_bootloader(void) { con_log("sim: bootloader requested (ignored)"); }

uint16_t *hal_framebuffer(void) { return framebuffer; }

void *hal_arena(size_t *size)
{
    static void *arena;
    if (!arena)
        arena = calloc(1, SIM_ARENA_SIZE);
    *size = SIM_ARENA_SIZE;
    return arena;
}

uint32_t hal_irq_save(void) { return 0; }
void hal_irq_restore(uint32_t s) { (void)s; }

/* -------------------------------------------------------------- timer --- */
uint32_t hal_millis(void)
{
    if (sim_opt.virtual_time)
        return (uint32_t)vtime_ms;
    return (uint32_t)((now_ns() - real_start_ns) / 1000000u);
}

void hal_delay_ms(uint32_t ms)
{
    if (sim_opt.virtual_time)
        vtime_ms += ms;
    else
        usleep(ms * 1000u);
}

/* Pretend to be a 400 MHz core (the STM32H743's maximum) so profiling output
 * reads in the same units as on hardware; derived from wall-clock time. */
uint32_t hal_cycles(void) { return (uint32_t)(now_ns() / 1000u * 400u); }
uint32_t hal_cycles_per_us(void) { return 400; }

/* ------------------------------------------------------------ display --- */
/* Rewrite the --live PNG if the panel changed, at most every 100 ms unless
 * `force`. Written to FILE.tmp and renamed, so a viewer never sees a partial
 * file. */
static void live_update(bool force)
{
    if (!sim_opt.live_png || !live_dirty)
        return;
    uint64_t t = now_ns();
    if (!force && t - live_last_ns < 100000000ull)
        return;
    live_last_ns = t;
    live_dirty = false;
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s.tmp", sim_opt.live_png);
    if (sim_write_lcd_png(tmp) == 0)
        rename(tmp, sim_opt.live_png);
}

/* "Send" a rectangle to the panel: copy it into gram (no clipping; core/gfx.c
 * only passes on-screen rectangles). */
void hal_display_flush(const uint16_t *fb, int x, int y, int w, int h)
{
    for (int j = 0; j < h; j++)
        memcpy(&gram[(y + j) * LCD_WIDTH + x], &fb[(y + j) * LCD_WIDTH + x], (size_t)w * 2);
    flush_count++;
    live_dirty = true;
}

void hal_display_enable(bool on)
{
    display_on = on;
    live_dirty = true;
}

/* Backlight level is only stored (it feeds the fake telemetry); screenshots
 * are not dimmed. */
void hal_backlight_set(int percent) { backlight = percent < 0 ? 0 : percent > 100 ? 100 : percent; }
int hal_backlight_get(void) { return backlight; }

/* Panel contents as a PNG; an all-black image while the display is off. */
int sim_write_lcd_png(const char *path)
{
    static uint16_t shown[LCD_WIDTH * LCD_HEIGHT];
    if (!display_on) {
        memset(shown, 0, sizeof shown);
    } else {
        memcpy(shown, gram, sizeof shown);
    }
    return png_write_rgb565(path, shown, LCD_WIDTH, LCD_HEIGHT, LCD_WIDTH);
}

/* ------------------------------------------------------------- keypad --- */
/* No physical keypad: always 0 (keys held with the console `press` command
 * are tracked by core/console.c and shown by `keys`). */
uint64_t hal_keypad_raw(void) { return 0; }

/* -------------------------------------------------------------- power --- */
/* Wait up to timeout_ms (-1 = forever, 0 = poll) for console input. True if
 * input is readable or already at EOF (so callers do not block on a closed
 * stream). */
static bool fd_ready(int timeout_ms)
{
    struct pollfd p = {.fd = sim_opt.input_fd, .events = POLLIN};
    int r = poll(&p, 1, timeout_ms);
    return r > 0;
}

/* Input read ahead while a computation runs (the CAS / Python poll hook,
 * hal_usb_poll): a Ctrl-C (0x03) in it is a break at once, as on the
 * calculator's USB serial port (platform/stm32h743/usb.c); the other bytes
 * wait here for the console. */
static char ahead[4096];
static int ahead_len, ahead_pos;

static bool input_ready(int timeout_ms)
{
    if (input_eof || ahead_pos < ahead_len)
        return true;
    return fd_ready(timeout_ms);
}

void hal_idle(void)
{
    live_update(false);
    if (sim_opt.virtual_time) {
        /* Time only advances while the console is executing `wait`
         * (deterministic regardless of how fast the driver sends input). */
        extern bool console_waiting(void);
        if (console_waiting()) {
            vtime_ms += 10;
            return;
        }
        input_ready(-1 /* block until input */);
        return;
    }
    input_ready(10); /* interactive: ~10 ms tick keeps cursor blink and key repeat running */
}

/* Hibernate as far as a simulator can: save the session (app/session.c, into
 * the --flash file if given) and blank the panel, then return. The main loop
 * stays in its "off" state until ON is pressed. */
void hal_power_off(void)
{
    platform_hibernate_save();
    display_on = false;
    live_dirty = true;
    live_update(true);
}

/* Fake but self-consistent telemetry for the `power` console command and the
 * app's battery indicator (app/app.c): 5.1 V USB-powered VSYS, current rising
 * with the backlight, 5.6 V battery pack, SD rail off. */
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
    t->v3v3_sd_v = 0.0f;
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
/* Touch input comes from the console `touch` command (core/console.c). */
bool hal_touch_available(void) { return true; }
void hal_service(void) {}
size_t hal_cas_stack_limit(void) { return 0; } /* CAS default (2 MB) < 8 MB main-thread stack */

/* ------------------------------------------------------------ storage --- */
static void flash_sync(uint32_t off, size_t n);

/* Allocate the simulated NOR (erased = 0xFF) on first use and load the
 * --flash file over it, if any. A missing or short file leaves the rest
 * erased; a file that cannot be opened is silently ignored (RAM only). */
static int flash_init(void)
{
    if (flash)
        return 0;
    flash = malloc(SIM_FLASH_SIZE);
    if (!flash)
        return -1;
    memset(flash, 0xFF, SIM_FLASH_SIZE);
    if (sim_opt.flash_file) {
        flash_fd = open(sim_opt.flash_file, O_RDWR | O_CREAT, 0644);
        if (flash_fd >= 0) {
            ssize_t n = pread(flash_fd, flash, SIM_FLASH_SIZE, 0);
            (void)n;
        }
    }
    hostdev_flash_attach(flash, SIM_FLASH_SIZE, flash_sync);
    return 0;
}

/* Write the changed range through to the backing file, so the file always
 * holds the flash state at the moment the process dies (a killed simulator
 * behaves like a power cut for core/pstore.c). */
static void flash_sync(uint32_t off, size_t n)
{
    if (flash_fd >= 0) {
        ssize_t r = pwrite(flash_fd, flash + off, n, off);
        (void)r;
    }
}

size_t hal_storage_size(void) { return hostdev_flash_size(SIM_FLASH_SIZE); }

int hal_storage_read(uint32_t off, void *buf, size_t n)
{
    if (flash_init() || off + n > hal_storage_size())
        return -1;
    if (hostdev_flash_read(buf, n))
        return 0;
    memcpy(buf, flash + off, n);
    return 0;
}

/* Like the NOR, only whole HAL_STORAGE_SECTOR-aligned sectors can be erased. */
int hal_storage_erase(uint32_t off, size_t n)
{
    if (flash_init() || off % HAL_STORAGE_SECTOR || n % HAL_STORAGE_SECTOR || off + n > hal_storage_size() ||
        hostdev_flash_modify())
        return -1;
    memset(flash + off, 0xFF, n);
    flash_sync(off, n);
    return 0;
}

/* Program: AND into the existing bytes, as NOR programming can only turn 1s
 * into 0s. Writing over unerased data therefore corrupts it just as on the
 * chip. No page-boundary limit is simulated. */
int hal_storage_reset(void) { return 0; } /* (a file: nothing to reset) */
int hal_storage_write(uint32_t off, const void *buf, size_t n)
{
    if (flash_init() || off + n > hal_storage_size() || hostdev_flash_modify())
        return -1;
    const uint8_t *b = buf;
    for (size_t i = 0; i < n; i++)
        flash[off + i] &= b[i]; /* NOR semantics: can only clear bits */
    flash_sync(off, n);
    return 0;
}

/* ------------------------------------------------------------ console --- */
void sim_console_readahead(void)
{
    if (input_eof || ahead_len == (int)sizeof ahead || !fd_ready(0))
        return;
    if (ahead_pos == ahead_len)
        ahead_pos = ahead_len = 0;
    char b[sizeof ahead];
    ssize_t n = read(sim_opt.input_fd, b, sizeof ahead - (size_t)ahead_len);
    if (n == 0)
        input_eof = true;
    for (ssize_t i = 0; i < n; i++) {
        if (b[i] == 3)
            keyq_break();
        else
            ahead[ahead_len++] = b[i];
    }
}

/* Non-blocking read of console input: bytes read, 0 if none pending, -1 at
 * EOF (calc_main then returns). */
int hal_console_read(char *buf, int max)
{
    if (ahead_pos < ahead_len && max > 0) {
        buf[0] = ahead[ahead_pos++];
        return 1;
    }
    if (input_eof)
        return -1;
    if (!fd_ready(0))
        return 0;
    ssize_t n = read(sim_opt.input_fd, buf, (size_t)max);
    if (n == 0) {
        input_eof = true;
        return -1;
    }
    if (n < 0)
        return errno == EAGAIN || errno == EINTR ? 0 : -1;
    return (int)n;
}

void hal_console_write(const char *s, size_t n)
{
    fwrite(s, 1, n, stdout);
    if (memchr(s, '\n', n))
        fflush(stdout);
}

/* --------------------------------------------- platform console cmds --- */
/* Relative screenshot names are placed under --shots DIR. */
static int resolve_path(const char *name, char *out, size_t n)
{
    if (name[0] == '/' || !sim_opt.shot_dir)
        snprintf(out, n, "%s", name);
    else
        snprintf(out, n, "%s/%s", sim_opt.shot_dir, name);
    return 0;
}

/* screenshot FILE.png [fb]: the panel (default) or, with `fb`, the app
 * framebuffer, which may hold drawing not yet flushed. */
static int cmd_screenshot(int argc, char **argv)
{
    if (argc < 2)
        return con_error("usage: screenshot FILE.png [fb]");
    char path[1024];
    resolve_path(argv[1], path, sizeof path);
    int rc;
    if (argc > 2 && !strcmp(argv[2], "fb"))
        rc = png_write_rgb565(path, framebuffer, LCD_WIDTH, LCD_HEIGHT, LCD_WIDTH);
    else
        rc = sim_write_lcd_png(path);
    if (rc)
        return con_error("cannot write %s", path);
    con_printf("file=%s\n", path);
    return 0;
}

static int cmd_lcdcheck(int argc, char **argv)
{
    /* Verify that dirty-rectangle flushing kept the panel identical to the framebuffer. */
    int bad = 0, fx = -1, fy = -1;
    for (int i = 0; i < LCD_WIDTH * LCD_HEIGHT; i++)
        if (gram[i] != framebuffer[i]) {
            if (!bad)
                fx = i % LCD_WIDTH, fy = i / LCD_WIDTH;
            bad++;
        }
    con_printf("mismatched_pixels=%d\nflushes=%lu\n", bad, flush_count);
    if (bad)
        return con_error("panel differs from framebuffer (first at %d,%d)", fx, fy);
    return 0;
}

static int cmd_simtime(int argc, char **argv)
{
    con_printf("virtual=%d\nms=%lu\n", sim_opt.virtual_time, (unsigned long)hal_millis());
    return 0;
}

const console_cmd hal_console_cmds[] = {
    {"screenshot", "FILE.png [fb]", "write the simulated LCD (or framebuffer) as PNG", cmd_screenshot},
    {"lcdcheck", "", "check panel == framebuffer (dirty-rect test)", cmd_lcdcheck},
    {"simtime", "", "simulator clock info", cmd_simtime},
    {"usb", "host on|off|vbus on|off|ls|cat|expect|lacks|drop ...", "the USB model and a computer (platform/hostdev)",
     hostdev_cmd_usb},
    {"fault", "qspi ok|absent|dead ff|dead 00|cut N | sd ok|dead", "inject device faults (platform/hostdev)",
     hostdev_cmd_fault},
    {0, 0, 0, 0},
};
