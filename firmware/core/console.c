/* Debug console: line protocol, core commands. See console.h and README.md. */
#include "console.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "calc.h"
#include "version.h"
#include "gfx.h"
#include "hal.h"
#include "keyq.h"
#include "patterns.h"
#include "rle.h"
#include "safemode.h"
#include "touch.h"

#define LINE_MAX 384 /* input line buffer, including the NUL */
#define MAX_ARGS 24  /* more tokens: the line is rejected */

int console_expect_failures;
bool console_quit;

static char line[LINE_MAX];  /* line being assembled by console_poll */
static int line_len;
static bool line_overflow;   /* current line exceeded LINE_MAX - 1: reject it at EOL */
static bool echo;
static char errmsg[160];     /* set by con_error for the running command */
static bool have_error;
static uint32_t wait_until;  /* hal_millis() deadline of a pending `wait` */
static bool waiting;

/* ------------------------------------------------------------ output */
void con_write(const char *s, size_t n) { hal_console_write(s, n); }

void con_puts(const char *s)
{
    con_write(s, strlen(s));
    con_write("\n", 1);
}

/* Format prefix + fmt into a 256-byte stack buffer and write it; output that
 * does not fit is truncated to 255 bytes. */
static void vprint(const char *prefix, const char *fmt, va_list ap)
{
    char buf[256];
    int n = 0;
    if (prefix)
        n = snprintf(buf, sizeof buf, "%s", prefix);
    int m = vsnprintf(buf + n, sizeof buf - (size_t)n, fmt, ap);
    if (m < 0)
        return;
    size_t len = (size_t)n + (size_t)m;
    if (len >= sizeof buf)
        len = sizeof buf - 1;
    con_write(buf, len);
}

void con_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprint(NULL, fmt, ap);
    va_end(ap);
}

void con_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprint("# ", fmt, ap);
    va_end(ap);
    con_write("\n", 1);
}

int con_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(errmsg, sizeof errmsg, fmt, ap);
    va_end(ap);
    have_error = true;
    return -1;
}

/* streaming base64: bytes are buffered 57 at a time, and each full buffer is
 * printed as one 76-character line (57 bytes = 19 groups of 3 -> 76 chars), so
 * padding can only occur on the final, partial line. This lets the RLE
 * encoder in `screen` stream straight into base64 without holding the
 * encoded image in memory. */
typedef struct {
    uint8_t in[57];
    int n;
} b64_t;

/* Encode n <= 57 bytes as one line of base64 plus '\n'. */
static void b64_line(const uint8_t *in, int n)
{
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char out[80];
    int o = 0;
    for (int i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < n) v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < n) v |= in[i + 2];
        out[o++] = tbl[(v >> 18) & 63];
        out[o++] = tbl[(v >> 12) & 63];
        out[o++] = i + 1 < n ? tbl[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < n ? tbl[v & 63] : '=';
    }
    out[o++] = '\n';
    con_write(out, (size_t)o);
}

/* Sink callback (same signature as rle_encode's output callback). */
static void b64_feed(const uint8_t *d, size_t n, void *user)
{
    b64_t *b = user;
    while (n--) {
        b->in[b->n++] = *d++;
        if (b->n == 57) {
            b64_line(b->in, 57);
            b->n = 0;
        }
    }
}

/* Flush the final partial line (padded with '='). */
static void b64_end(b64_t *b)
{
    if (b->n)
        b64_line(b->in, b->n);
    b->n = 0;
}

void con_base64(const void *data, size_t n)
{
    b64_t b = {.n = 0};
    b64_feed(data, n, &b);
    b64_end(&b);
}

/* ------------------------------------------------------------ parsing */
/* Number base of s (after an optional sign): 16 with a 0x prefix, else 10. */
static int num_base(const char *s)
{
    if (*s == '-' || *s == '+')
        s++;
    return s[0] == '0' && (s[1] == 'x' || s[1] == 'X') ? 16 : 10;
}

bool con_parse_u32(const char *s, unsigned long *out)
{
    char *end;
    if (!s || !*s || *s == '-')
        return false;
    errno = 0;
    *out = strtoul(s, &end, num_base(s));
    return *end == 0 && errno != ERANGE && *out <= 0xFFFFFFFFul;
}

bool con_parse_int(const char *s, long *out)
{
    char *end;
    if (!s || !*s)
        return false;
    errno = 0;
    *out = strtol(s, &end, num_base(s));
    return *end == 0 && errno != ERANGE;
}

/* Split s in place into at most MAX_ARGS arguments (-1 if there are more).
 * Arguments are separated by whitespace; a double-quoted argument
 * may contain spaces, and inside quotes a backslash escapes the next
 * character ("\n" becomes a newline, any other "\X" becomes X). The closing
 * quote is optional at end of line. Text directly after a closing quote
 * starts a new argument. Returns argc; argv points into s. */
static int tokenize(char *s, char **argv)
{
    int argc = 0;
    while (*s) {
        while (isspace((unsigned char)*s))
            s++;
        if (!*s)
            break;
        if (argc == MAX_ARGS)
            return -1;
        if (*s == '"') {
            s++;
            argv[argc++] = s;
            char *d = s;
            while (*s && *s != '"') {
                if (*s == '\\' && s[1]) {
                    s++;
                    *d++ = *s == 'n' ? '\n' : *s;
                    s++;
                } else {
                    *d++ = *s++;
                }
            }
            if (*s)
                s++;
            *d = 0;
        } else {
            argv[argc++] = s;
            while (*s && !isspace((unsigned char)*s))
                s++;
            if (*s)
                *s++ = 0;
        }
    }
    return argc;
}

/* ------------------------------------------------------------ commands */
static const console_cmd core_cmds[];

/* Linear search of core, app, then platform tables (exact, case-sensitive). */
static const console_cmd *find_cmd(const char *name)
{
    const console_cmd *tables[] = {core_cmds, app_console_cmds, hal_console_cmds};
    for (size_t t = 0; t < sizeof tables / sizeof tables[0]; t++)
        for (const console_cmd *c = tables[t]; c->name; c++)
            if (!strcmp(c->name, name))
                return c;
    return NULL;
}

static int cmd_help(int argc, char **argv)
{
    const console_cmd *tables[] = {core_cmds, app_console_cmds, hal_console_cmds};
    const char *titles[] = {"core", "app", "platform"};
    for (size_t t = 0; t < 3; t++) {
        if (argc < 2)
            con_printf("[%s]\n", titles[t]);
        for (const console_cmd *c = tables[t]; c->name; c++)
            if (argc < 2 || !strcmp(argv[1], c->name))
                con_printf("  %-10s %-28s %s\n", c->name, c->usage, c->help);
    }
    return 0;
}

static int cmd_version(int argc, char **argv)
{
    con_printf("version=%s\nplatform=%s\nbuilt=%s %s\n", calc_version(), hal_platform_name(), fw_build_date,
               fw_build_time);
    return 0;
}

static int cmd_echo(int argc, char **argv)
{
    if (argc > 1)
        echo = !strcmp(argv[1], "on");
    con_printf("echo=%s\n", echo ? "on" : "off");
    return 0;
}

/* Key code for a key name/alias, or KEY_NONE (0) after setting the error. */
static int key_arg(const char *name)
{
    int k = key_from_name(name);
    if (k == KEY_NONE)
        con_error("unknown key '%s' (see 'keys list')", name);
    return k;
}

/* key NAME...: queue press+release for each key. All names are checked
 * first, so an unknown name queues nothing. */
static int cmd_key(int argc, char **argv)
{
    if (argc < 2)
        return con_error("usage: key NAME...");
    for (int i = 1; i < argc; i++)
        if (!key_arg(argv[i]))
            return -1;
    for (int i = 1; i < argc; i++) {
        int k = key_from_name(argv[i]);
        keyq_push((uint8_t)k, true);
        keyq_push((uint8_t)k, false);
    }
    return 0;
}

/* Raw-state bit of key k (see cmd_keys). */
static int key_bit(int k) { return key_pos[k] == 0xFF ? 49 : (key_pos[k] >> 4) * 7 + (key_pos[k] & 15); }

/* Keys held through `press` (not yet `release`d), as hal_keypad_raw() bits;
 * `keys` ORs them in, since injected events never reach the platform's
 * raw keypad state. */
static uint64_t g_injected_raw;
/* `hold MS NAME...`: keys pressed now and released by themselves at
 * g_hold_until (hal_millis), even while the app is busy (a boot polling the
 * safe-mode keys), so tests can let go "in the middle" of a start-up. */
static uint64_t g_hold_raw;
static uint8_t g_hold_keys[8];
static int g_nhold;
static uint32_t g_hold_until;

static void hold_expire(void)
{
    if (!g_nhold || (int32_t)(hal_millis() - g_hold_until) < 0)
        return;
    for (int i = 0; i < g_nhold; i++)
        keyq_push(g_hold_keys[i], false);
    g_injected_raw &= ~g_hold_raw;
    g_hold_raw = 0;
    g_nhold = 0;
}

uint64_t console_held_keys(void)
{
    hold_expire();
    return g_injected_raw;
}
uint64_t console_key_raw_mask(int key) { return 1ULL << key_bit(key); }

/* press NAME... / release NAME... (one handler; argv[0] selects the edge). */
static int cmd_press(int argc, char **argv)
{
    bool down = !strcmp(argv[0], "press");
    if (argc < 2)
        return con_error("usage: %s NAME", argv[0]);
    for (int i = 1; i < argc; i++)
        if (!key_arg(argv[i]))
            return -1;
    for (int i = 1; i < argc; i++) {
        int k = key_from_name(argv[i]);
        keyq_push((uint8_t)k, down);
        if (down)
            g_injected_raw |= 1ULL << key_bit(k);
        else
            g_injected_raw &= ~(1ULL << key_bit(k));
    }
    return 0;
}

/* hold MS NAME...: press the keys now and release them MS ms later. */
static int cmd_hold(int argc, char **argv)
{
    unsigned long ms;
    if (argc < 3 || !con_parse_u32(argv[1], &ms) || argc - 2 > (int)sizeof g_hold_keys)
        return con_error("usage: hold MS NAME... (at most %u keys)", (unsigned)sizeof g_hold_keys);
    for (int i = 2; i < argc; i++)
        if (!key_arg(argv[i]))
            return -1;
    g_nhold = 0;
    g_hold_raw = 0;
    for (int i = 2; i < argc; i++) {
        int k = key_from_name(argv[i]);
        keyq_push((uint8_t)k, true);
        g_hold_keys[g_nhold++] = (uint8_t)k;
        g_hold_raw |= 1ULL << key_bit(k);
    }
    g_injected_raw |= g_hold_raw;
    g_hold_until = hal_millis() + (uint32_t)ms;
    return 0;
}

/* keys [list]. key_pos[] (generated from keys/layout.py) packs the matrix
 * position as row << 4 | col, or 0xFF for the ON key, which is not in the
 * matrix but on its own wake-up pin (PA0/WKUP1). The raw bitmask from
 * hal_keypad_raw() uses bit row*7+col (7 columns) and bit 49 for ON. */
static int cmd_keys(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "list")) {
        for (int k = 1; k < KEY_COUNT; k++) {
            if (key_pos[k] != 0xFF)
                con_printf("%-7s row=%d col=%d\n", key_names[k], key_pos[k] >> 4, key_pos[k] & 15);
            else
                con_printf("%-7s (PA0/WKUP1)\n", key_names[k]);
        }
        return 0;
    }
    uint64_t raw = hal_keypad_raw() | g_injected_raw;
    con_printf("raw=0x%06lx%08lx\n", (unsigned long)(raw >> 32), (unsigned long)(raw & 0xFFFFFFFFu));
    for (int k = 1; k < KEY_COUNT; k++) {
        if (raw & (1ULL << key_bit(k)))
            con_printf("down=%s\n", key_names[k]);
    }
    return 0;
}

/* screen [raw]: dump the framebuffer as
 *   BEGIN SCREEN w= h= fmt=RGB565LE enc=rle|raw bytes=N crc32=XXXXXXXX
 *   <base64 lines>
 *   END
 * The CRC covers the decoded pixel bytes so the host can verify the transfer.
 * RLE (core/rle.c) shrinks a typical UI screen from 150 KB to a few KB, which
 * matters at 115200 baud. The RLE path encodes twice: once with no sink just
 * to learn the byte count for the header, then again streaming into base64,
 * so no encode buffer is needed. */
static int cmd_screen(int argc, char **argv)
{
    const uint16_t *fb = gfx_framebuffer();
    size_t n = (size_t)LCD_WIDTH * LCD_HEIGHT;
    bool raw = argc > 1 && !strcmp(argv[1], "raw");
    uint32_t crc = crc32_update(0, fb, n * 2); /* little-endian hosts/targets */
    if (raw) {
        con_printf("BEGIN SCREEN w=%d h=%d fmt=RGB565LE enc=raw bytes=%u crc32=%08lx\n", LCD_WIDTH,
                   LCD_HEIGHT, (unsigned)(n * 2), (unsigned long)crc);
        con_base64(fb, n * 2);
    } else {
        size_t len = rle_encode(fb, n, NULL, NULL);
        con_printf("BEGIN SCREEN w=%d h=%d fmt=RGB565LE enc=rle bytes=%u crc32=%08lx\n", LCD_WIDTH,
                   LCD_HEIGHT, (unsigned)len, (unsigned long)crc);
        b64_t b = {.n = 0};
        rle_encode(fb, n, b64_feed, &b);
        b64_end(&b);
    }
    con_puts("END");
    return 0;
}

static int cmd_fbinfo(int argc, char **argv)
{
    con_printf("addr=0x%08lx\nwidth=%d\nheight=%d\nstride_bytes=%d\nformat=RGB565LE\nbytes=%d\n",
               (unsigned long)(uintptr_t)gfx_framebuffer(), LCD_WIDTH, LCD_HEIGHT, LCD_WIDTH * 2,
               LCD_WIDTH * LCD_HEIGHT * 2);
    gfx_stats_t s;
    gfx_get_stats(&s);
    con_printf("flushes=%lu\nrects=%lu\npixels=%lu\n", (unsigned long)s.flushes, (unsigned long)s.rects,
               (unsigned long)s.pixels);
    return 0;
}

static int cmd_fbcrc(int argc, char **argv)
{
    con_printf("crc32=%08lx\n",
               (unsigned long)crc32_update(0, gfx_framebuffer(), (size_t)LCD_WIDTH * LCD_HEIGHT * 2));
    return 0;
}

static int cmd_text(int argc, char **argv)
{
    for (int i = 0; i < gfx_textlog_count(); i++) {
        const gfx_textrun_t *t = gfx_textlog_get(i);
        if (argc > 1 && !strcmp(argv[1], "wh")) /* (tests/bot: each run's box too) */
            con_printf("%3d %3d %3d %3d %s\n", t->y, t->x, t->w, t->h, t->text);
        else
            con_printf("%3d %3d %s\n", t->y, t->x, t->text);
    }
    return 0;
}

static int cmd_pixel(int argc, char **argv)
{
    long x, y;
    if (argc < 3 || !con_parse_int(argv[1], &x) || !con_parse_int(argv[2], &y))
        return con_error("usage: pixel X Y");
    uint16_t c = gfx_get_pixel((int)x, (int)y);
    /* components expanded to 8 bits by shifting (no bit replication: white
     * reads back as 248,252,248) */
    con_printf("rgb565=0x%04x r=%d g=%d b=%d\n", c, (c >> 11) << 3, ((c >> 5) & 63) << 2, (c & 31) << 3);
    return 0;
}

/* key=value with 3 decimals; NaN (quantity not measurable on this platform or
 * sensor absent) prints as "nan". */
static void print_f(const char *k, float v, const char *unit)
{
    if (isnan(v))
        con_printf("%s=nan\n", k);
    else
        con_printf("%s=%.3f%s\n", k, (double)v, unit);
}

/* power [off]: telemetry snapshot (units are in the key names: V, mA, mW,
 * degC). `power off` only requests the power-off; the main loop performs it
 * on its next iteration, after this command's OK has been printed. */
static int cmd_power(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "off")) {
        calc_request_power_off();
        return 0;
    }
    hal_telemetry_t t;
    hal_telemetry(&t);
    con_printf("pmon=%s\n", t.valid_pmon ? "ok" : "absent");
    print_f("vsys_v", t.vsys_v, "");
    print_f("isys_ma", t.isys_ma, "");
    print_f("psys_mw", t.psys_mw, "");
    print_f("vbat_v", t.vbat_v, "");
    print_f("v3v3_v", t.v3v3_v, "");
    print_f("v3v3_bus_v", t.v3v3_bus_v, "");
    print_f("v3v3_sd_v", t.v3v3_sd_v, "");
    print_f("bl_ma", t.bl_ma, "");
    print_f("temp_c", t.temp_c, "");
    con_printf("usb=%d\nbacklight_pct=%d\n", t.usb_present, hal_backlight_get());
    return 0;
}

static int cmd_bl(int argc, char **argv)
{
    long v;
    if (argc > 1) {
        if (!con_parse_int(argv[1], &v) || v < 0 || v > 100)
            return con_error("usage: bl [0..100]");
        hal_backlight_set((int)v);
    }
    con_printf("backlight_pct=%d\n", hal_backlight_get());
    return 0;
}

/* pattern [NAME|off]: draw a test pattern (core/patterns.c). While
 * pattern_active is set the main loop stops calling app_tick, so the pattern
 * stays on screen until `pattern off` or the next key/tap redraws the app. */
static int cmd_pattern(int argc, char **argv)
{
    if (argc < 2) {
        con_printf("patterns: %s off\n", patterns_list());
        return 0;
    }
    if (!strcmp(argv[1], "off")) {
        pattern_active = false;
        app_redraw();
        return 0;
    }
    if (!pattern_draw(argv[1]))
        return con_error("unknown pattern '%s'", argv[1]);
    pattern_active = true;
    return 0;
}

/* wait MS: defer the OK until MS milliseconds of main-loop time have passed.
 * console_poll reads no further input meanwhile, so the app keeps ticking
 * (animations, timeouts, auto-repeat) before the next command. */
static int cmd_wait(int argc, char **argv)
{
    unsigned long ms;
    if (argc < 2 || !con_parse_u32(argv[1], &ms))
        return con_error("usage: wait MS");
    wait_until = hal_millis() + (uint32_t)ms;
    waiting = true;
    return 1; /* OK is printed when the wait completes */
}

/* reset / bootloader: print OK first (the command never returns), then wait
 * 20 ms so the last bytes have left the UART before the core resets ("OK\n"
 * takes ~0.3 ms at 115200 baud; the rest is margin). */
static int cmd_reset(int argc, char **argv)
{
    safemode_clean_shutdown(); /* a deliberate reset is not a failed boot */
    con_puts("OK");
    hal_delay_ms(20);
    hal_reset();
    return 1;
}

static int cmd_bootloader(int argc, char **argv)
{
    con_puts("OK");
    hal_delay_ms(20);
    hal_enter_bootloader();
    return 1;
}

/* dfu: save the session, then restart into the ROM bootloader's USB DFU
 * (hal_enter_bootloader leaves the USB bus first, so the computer sees this
 * device go and the bootloader's appear). tools/calcctl.py flash uses it. */
static int cmd_dfu(int argc, char **argv)
{
    platform_hibernate_save();
    safemode_clean_shutdown(); /* not a failed boot */
    con_puts("OK");
    hal_delay_ms(20);
    hal_enter_bootloader();
    return 1;
}

static int cmd_uptime(int argc, char **argv)
{
    con_printf("ms=%lu\n", (unsigned long)hal_millis());
    return 0;
}

static int cmd_quit(int argc, char **argv)
{
    console_quit = true;
    return 0;
}

/* Parse argv[0..n-1] as integers into out[]; false on the first failure. */
static bool parse_ints(char **argv, int n, long *out)
{
    for (int i = 0; i < n; i++)
        if (!con_parse_int(argv[i], &out[i]))
            return false;
    return true;
}

/* touch                                  status: contacts, last gesture, controller
 * touch X Y down|move|up [ID]            one contact update (ID 0..4, default 0)
 * touch tap X Y                          down + up
 * touch drag X1 Y1 X2 Y2 [STEPS]         down, STEPS moves, up
 * touch pinch CX CY D1 D2 [STEPS]        two fingers on a horizontal line, spacing D1 -> D2
 * touch swipe X Y L|R|U|D [DIST]         a quick straight 120 px (or DIST) drag from X Y (a swipe)
 * Events go through the same recognizer as the hardware controller and are
 * processed by the app before the next command runs. Coordinates are screen
 * pixels. drag/pinch interpolate linearly in STEPS equal moves. All frames of
 * one command are injected back to back (nearly the same hal_millis()), so
 * `tap` is always shorter than TOUCH_TAP_MS, and a `drag` shorter than
 * TOUCH_SLOP_PX is recognized as a tap too. */
static int cmd_touch(int argc, char **argv)
{
    long v[6];
    if (argc == 1) {
        touch_point_t p[TOUCH_MAX_CONTACTS];
        int n = touch_contacts(p, TOUCH_MAX_CONTACTS);
        const touch_event_t *e = touch_last_event();
        con_printf("available=%d\ncontacts=%d\n", hal_touch_available(), n);
        for (int i = 0; i < n; i++)
            con_printf("contact%d=%d,%d id=%d\n", i, p[i].x, p[i].y, p[i].id);
        con_printf("events=%lu\nlast=%s %d,%d start=%d,%d scale=%.3f\n", (unsigned long)touch_event_count(),
                   touch_event_name(e->type), e->x, e->y, e->x0, e->y0, (double)e->scale);
        return 0;
    }
    if (!strcmp(argv[1], "tap")) {
        if (argc < 4 || !parse_ints(argv + 2, 2, v))
            return con_error("usage: touch tap X Y");
        touch_inject(0, (int)v[0], (int)v[1], 1);
        touch_inject(0, (int)v[0], (int)v[1], 0);
        return 0;
    }
    if (!strcmp(argv[1], "drag")) {
        long steps = 8;
        if (argc < 6 || !parse_ints(argv + 2, 4, v) || (argc > 6 && !con_parse_int(argv[6], &steps)) || steps < 1)
            return con_error("usage: touch drag X1 Y1 X2 Y2 [STEPS]");
        touch_inject(0, (int)v[0], (int)v[1], 1);
        for (long i = 1; i <= steps; i++)
            touch_inject(0, (int)(v[0] + (v[2] - v[0]) * i / steps), (int)(v[1] + (v[3] - v[1]) * i / steps), 2);
        touch_inject(0, (int)v[2], (int)v[3], 0);
        return 0;
    }
    if (!strcmp(argv[1], "swipe")) {
        long dist = 120;
        if (argc < 5 || !parse_ints(argv + 2, 2, v) || (argc > 5 && !con_parse_int(argv[5], &dist)) || dist < 1)
            return con_error("usage: touch swipe X Y L|R|U|D [DIST]");
        char d = argv[4][0] & ~32;
        int ux = d == 'L' ? -1 : d == 'R' ? 1 : 0, uy = d == 'U' ? -1 : d == 'D' ? 1 : 0;
        if (!ux && !uy)
            return con_error("direction must be L, R, U or D");
        touch_inject(0, (int)v[0], (int)v[1], 1);
        for (long i = 1; i <= 4; i++) /* back to back: fast */
            touch_inject(0, (int)(v[0] + ux * dist * i / 4), (int)(v[1] + uy * dist * i / 4), 2);
        touch_inject(0, (int)(v[0] + ux * dist), (int)(v[1] + uy * dist), 0);
        return 0;
    }
    if (!strcmp(argv[1], "pinch")) {
        long steps = 8;
        if (argc < 6 || !parse_ints(argv + 2, 4, v) || (argc > 6 && !con_parse_int(argv[6], &steps)) || steps < 1)
            return con_error("usage: touch pinch CX CY D1 D2 [STEPS]");
        long cx = v[0], cy = v[1];
        /* fingers 0 and 1 at cx -/+ d/2; finger 1 lifts first, so the
         * recognizer emits PINCH_END with the final scale (about D2 / D1) */
        touch_inject(0, (int)(cx - v[2] / 2), (int)cy, 1);
        touch_inject(1, (int)(cx + v[2] / 2), (int)cy, 1);
        for (long i = 1; i <= steps; i++) {
            long d = v[2] + (v[3] - v[2]) * i / steps;
            touch_inject(0, (int)(cx - d / 2), (int)cy, 2);
            touch_inject(1, (int)(cx + d / 2), (int)cy, 2);
        }
        touch_inject(1, (int)(cx + v[3] / 2), (int)cy, 0);
        touch_inject(0, (int)(cx - v[3] / 2), (int)cy, 0);
        return 0;
    }
    long id = 0;
    if (argc < 4 || !parse_ints(argv + 1, 2, v) || (argc > 4 && !con_parse_int(argv[4], &id)) || id < 0 ||
        id >= TOUCH_MAX_CONTACTS)
        return con_error("usage: touch X Y down|move|up [ID] | tap X Y | drag X1 Y1 X2 Y2 [N] | pinch CX CY D1 D2 [N]");
    int st = !strcmp(argv[3], "down") ? 1 : !strcmp(argv[3], "move") ? 2 : !strcmp(argv[3], "up") ? 0 : -1;
    if (st < 0)
        return con_error("state must be down, move or up");
    touch_inject((int)id, (int)v[0], (int)v[1], st);
    return 0;
}

/* ifcas IMPL COMMAND...: run COMMAND only when the linked CAS is IMPL (stub,
 * src or lib); otherwise just answer OK. Lets one test script cover both. */
static int cmd_ifcas(int argc, char **argv)
{
    if (argc < 3)
        return con_error("usage: ifcas stub|src|lib COMMAND...");
    if (strcmp(argv[1], CAS_IMPL))
        return 0;
    const console_cmd *c = find_cmd(argv[2]);
    if (!c)
        return con_error("unknown command '%s'", argv[2]);
    return c->fn(argc - 2, argv + 2);
}

/* Build configuration (generated $(BUILD)/gen/config_gen.c, tools/kconfig.py):
 * the enabled bool Kconfig options, NULL-terminated. */
extern const char *const build_config[];

bool build_has_config(const char *name)
{
#ifdef PLATFORM_WEB
    /* the web pages leave out the Python runtime (py_core.c, modx.c) and the notebook-page module
     * (Makefile WEB_MODULE_FILES) although the .config they are built from has them: tests that need
     * them skip there, as on a tier without them */
    if (!strcmp(name, "PYTHON") || !strcmp(name, "MOD_PYTHON") || !strcmp(name, "MOD_WEBUI"))
        return false;
#endif
    for (int i = 0; build_config[i]; i++)
        if (!strcmp(build_config[i], name))
            return true;
    return false;
}

/* ifcfg [!]NAME COMMAND...: run COMMAND only if the Kconfig option CONFIG_NAME
 * is (with !: is not) enabled in this build, so one UI test serves every tier. */
static int cmd_ifcfg(int argc, char **argv)
{
    if (argc < 3)
        return con_error("usage: ifcfg [!]OPTION COMMAND...");
    bool neg = argv[1][0] == '!';
    if (build_has_config(argv[1] + neg) == neg)
        return 0;
    const console_cmd *c = find_cmd(argv[2]);
    if (!c)
        return con_error("unknown command '%s'", argv[2]);
    return c->fn(argc - 2, argv + 2);
}

/* requirecfg [!]NAME: a test script that needs CONFIG_NAME (or its absence)
 * stops here in other builds: prints "SKIP ..." and ends the session, which
 * tests/run_ui.py reports as skipped. */
static int cmd_requirecfg(int argc, char **argv)
{
    if (argc < 2)
        return con_error("usage: requirecfg [!]OPTION");
    bool neg = argv[1][0] == '!';
    if (build_has_config(argv[1] + neg) != neg)
        return 0;
    con_printf("SKIP: this build has %sCONFIG_%s\n", neg ? "" : "no ", argv[1] + neg);
    console_quit = true;
    return 0;
}

static const console_cmd core_cmds[] = {
    {"help", "[cmd]", "list commands", cmd_help},
    {"version", "", "firmware version, platform, build date", cmd_version},
    {"echo", "on|off", "echo input characters (for humans)", cmd_echo},
    {"key", "NAME...", "press+release keys in order", cmd_key},
    {"press", "NAME...", "press (hold) keys", cmd_press},
    {"release", "NAME...", "release keys", cmd_press},
    {"hold", "MS NAME...", "press keys, release them MS ms later", cmd_hold},
    {"keys", "[list]", "raw keypad state, or list key names", cmd_keys},
    {"screen", "[raw]", "dump framebuffer (base64 RLE or raw RGB565)", cmd_screen},
    {"fbinfo", "", "framebuffer address/format + flush stats", cmd_fbinfo},
    {"fbcrc", "", "CRC32 of the framebuffer", cmd_fbcrc},
    {"text", "[wh]", "text runs on screen: y x [w h] text", cmd_text},
    {"pixel", "X Y", "framebuffer pixel colour", cmd_pixel},
    {"power", "[off]", "power telemetry (or hibernate)", cmd_power},
    {"bl", "[0..100]", "backlight level", cmd_bl},
    {"pattern", "[NAME|off]", "LCD test pattern", cmd_pattern},
    {"wait", "MS", "let the main loop run for MS milliseconds", cmd_wait},
    {"uptime", "", "milliseconds since boot", cmd_uptime},
    {"reset", "", "software reset", cmd_reset},
    {"bootloader", "", "enter the ROM bootloader (USB DFU / USART1)", cmd_bootloader},
    {"dfu", "", "save the session, restart into the ROM USB DFU bootloader (calcctl flash)", cmd_dfu},
    {"quit", "", "exit (simulator)", cmd_quit},
    {"touch", "[X Y down|move|up [ID] | tap|drag|pinch|swipe ...]", "inject touch input / touch status", cmd_touch},
    {"ifcas", "stub|src|lib CMD...", "run CMD only with that CAS linked (tests)", cmd_ifcas},
    {"ifcfg", "[!]OPTION CMD...", "run CMD only if Kconfig option CONFIG_OPTION is (not) set (tests)", cmd_ifcfg},
    {"requirecfg", "[!]OPTION", "end the script (SKIP) unless CONFIG_OPTION is (not) set (tests)", cmd_requirecfg},
    {0, 0, 0, 0},
};

/* ------------------------------------------------------------ engine */
/* Execute one line and print exactly one terminator (unless the command
 * returned 1). A command that returns 0 but called con_error still gets ERR. */
int console_exec(const char *text)
{
    char buf[LINE_MAX];
    char *argv[MAX_ARGS];
    strncpy(buf, text, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    char *s = buf;
    while (isspace((unsigned char)*s))
        s++;
    if (!*s || *s == '#')
        return 0; /* blank or comment: no response */
    int argc = tokenize(s, argv);
    if (argc == 0)
        return 0;
    const console_cmd *c = argc > 0 ? find_cmd(argv[0]) : NULL;
    have_error = false;
    int rc;
    if (argc < 0) {
        rc = con_error("too many arguments (max %d)", MAX_ARGS);
    } else if (!c) {
        rc = con_error("unknown command '%s' (try help)", argv[0]);
    } else {
        hal_watchdog_kick(); /* long commands kick it themselves (core/memtest.c, qspi, sd) */
        rc = c->fn(argc, argv);
    }
    if (rc == 1)
        return 0; /* command printed its own terminator (or deferred it) */
    if (rc != 0 || have_error) {
        con_printf("ERR %s\n", have_error ? errmsg : "failed");
        return -1;
    }
    con_puts("OK");
    return 0;
}

void console_init(void)
{
    line_len = 0;
    echo = false;
}

/* Byte-at-a-time line assembly. '\r' and '\n' both end a line, so "\r\n"
 * yields one line plus an empty one (ignored). Backspace/DEL edit the line
 * only with echo on (interactive use); otherwise they are stored like any
 * other byte. An over-long line is swallowed up to its end and answered with
 * a single ERR. */
bool console_poll(void)
{
    hold_expire();
    if (waiting) {
        /* signed difference: correct across the 32-bit millisecond wrap */
        if ((int32_t)(hal_millis() - wait_until) < 0)
            return false;
        waiting = false;
        con_puts("OK");
    }
    char c;
    for (;;) {
        int n = hal_console_read(&c, 1);
        if (n < 0) { /* end of input (simulator stdin EOF): stop the main loop */
            console_quit = true;
            return false;
        }
        if (n == 0)
            return false;
        if (c == 3)
            continue; /* Ctrl-C: a break, already given to the ON hook by the link (keyq_break) */
        if (echo && c != '\n')
            con_write(c == '\r' ? "\r\n" : &c, c == '\r' ? 2 : 1);
        if (c == '\r' || c == '\n') {
            line[line_len] = 0;
            bool ran = line_len > 0;
            if (line_overflow)
                con_puts("ERR line too long");
            else if (ran)
                console_exec(line);
            line_len = 0;
            line_overflow = false;
            if (ran)
                return true;
            continue;
        }
        if ((c == 8 || c == 127) && echo) {
            if (line_len)
                line_len--;
            continue;
        }
        if (line_len < LINE_MAX - 1)
            line[line_len++] = c;
        else
            line_overflow = true;
    }
}

bool console_waiting(void) { return waiting; }
