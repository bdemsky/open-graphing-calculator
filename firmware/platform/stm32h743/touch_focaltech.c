/*
 * FocalTech capacitive touch controller: FT6236 (FT6x36 family) on the
 * EastRising ER-TPC032-3.1 touch panel, which shares the LCD's 40-pin FPC
 * (J3 pins 1-4: TP_INT, TP_RST, TP_SCL, TP_SDA). The FT5x06/FT5x26 family
 * (up to 5 points, same register layout) is accepted too.
 *
 *   I2C4: TP_SCL PD12 / TP_SDA PD13 (AF4, open drain, 4.7k pull-ups to 3V3_BUS), address 0x38,
 *   100 kHz by default (FT6x36 DS table 2-2 timing; `tp speed fast` tries ~340 kHz, i2c.c)
 *   TP_INT PC1 (EXTI1, active low, 10k pull-up to 3V3_BUS)
 *   TP_RST PA7 (active low, 10k pull-up to 3V3_BUS)
 *   Supply: the panel's VCI = 3V3_BUS (BUS_EN). The controller and all its
 *   pull-ups are unpowered while the rail is off: then PD12/PD13/PC1 are analog
 *   and PA7 is driven LOW (never high), see touch_pins_bus_off() called by
 *   bus_power(false).
 *
 * Register map (FocalTech "Application Note for FT6x06 CTPM" working-mode map,
 * which the FT6x36 shares; FT5x06 AN for the 5-point layout):
 *   0x00 DEV_MODE, 0x01 GEST_ID, 0x02 TD_STATUS (point count in 3:0), then
 *   6 bytes per point from 0x03: XH (event flag 7:6, X 11:8), XL, YH (id 7:4,
 *   Y 11:8), YL, weight, misc (area 7:4). FT6x36: 2 points (0x03-0x0E);
 *   FT5x06/FT5x26: 5 points (0x03-0x1E).
 *   0xA3 CIPHER (chip id), 0xA4 G_MODE (0 = INT polling/level, 1 = trigger,
 *   default 1), 0xA5 PWR_MODE (3 = hibernate), 0xA6 FIRMID, 0xA8 FOCALTECH_ID
 *   (panel/CTPM vendor id; FT6x06 AN default 0x11, but panel makers program
 *   their own value, so it is only reported, never checked).
 *   CIPHER values: 0x06 FT6206 (FT6x06 AN default), 0x36 FT6236, 0x64
 *   FT6236U/FT6336U (Adafruit_FT6206 library, not a FocalTech document),
 *   0x54 FT5x26 and 0x55 FT5x06 (FocalTech FT5x06-style CTPM AN): 5 points.
 *   Other values: treated as an unknown FT6x36-compatible controller (2 points).
 *
 * Timing (FT6x36 datasheet v0.3, table 3-5): reset low >= 5 ms (Trst) and low
 * >= 1 ms after power-on (Tprt); reporting starts ~300 ms after reset (Trsi)
 * or power-on (Tpon); after power-off VDD must stay < 0.3 V for >= 5 ms.
 * Power: ~4 mA active, 1.5 mA monitor, ~50 uA sleep (FT6x36 DS table 3-2);
 * hibernate is left only by a reset (or an INT low pulse, not used here).
 *
 * Reads are interrupt-driven but done in main context: the EXTI1 ISR only sets
 * a flag (and wakes WFI); touch_service() (from hal_service) reads all points in
 * one burst (15 bytes for 2 points, 31 for 5), maps them to screen coordinates
 * and calls touch_report(). While any finger is down it also polls every 15 ms,
 * so a missed INT pulse (or a controller in level/"polling" INT mode) cannot
 * leave a finger stuck down. Everything degrades gracefully if the controller
 * is absent: probe failure -> touch disabled, keys keep working; `tp init`
 * retries.
 */
#include <stdio.h>
#include <string.h>
#include "console.h"
#include "hal.h"
#include "platform.h"
#include "touch.h"

#define TP_I2C I2C4
#define REG_GEST 0x01
#define REG_CIPHER 0xA3
#define REG_G_MODE 0xA4
#define REG_PMODE 0xA5
#define REG_FIRMID 0xA6
#define REG_VENDOR 0xA8
#define PMODE_HIBERNATE 0x03
#define REPORT_BYTES_MAX 31 /* 0x00 .. 0x1E: 5 points (FT5x06 family) */
#define POLL_MS 15      /* re-read period while a finger is down */
#define READY_MS 320    /* Trsi / Tpon ~300 ms (FT6x36 DS table 3-5) + margin */
#define RESET_LOW_MS 6  /* Trst >= 5 ms */
#define MAX_ERRORS 8    /* consecutive failed reads before the controller is declared lost */

static volatile bool g_irq;  /* set by EXTI1 (and by touch_init): a read is due */
static bool g_present, g_asleep;
static uint32_t g_ready_at;  /* g_ticks_ms after which the controller answers (reset + READY_MS) */
static uint32_t g_last_read;
static int g_down;          /* contacts in the last report */
static int g_errors;        /* consecutive read errors */
static int g_map = TOUCH_ORIENTATION;
static uint8_t g_ids[4];    /* CIPHER (chip id), firmware, FocalTech/CTPM vendor id, G_MODE */
static int g_max_pts = 2;   /* points the controller reports: 2 (FT6x36) or 5 (FT5x06) */
static uint8_t g_last_raw[REPORT_BYTES_MAX];
static uint32_t g_irq_count, g_reads, g_read_errors;

/* true while touch input can actually arrive (hal_touch_available). */
bool touch_hw_present(void) { return g_present && !g_asleep; }

/* Burst length from register 0x00: DEV_MODE, GEST_ID, TD_STATUS + 6 per point. */
static int report_bytes(void) { return 3 + 6 * g_max_pts; }

/* Chip name and point count from ID_G_CIPHER (0xA3). */
static const char *chip_name(uint8_t cipher, int *max_pts)
{
    int n = 2;
    const char *name;
    switch (cipher) {
    case 0x36: name = "FT6236"; break;
    case 0x64: name = "FT6336U/FT6236U"; break;
    case 0x06: name = "FT6206"; break;
    case 0x54: name = "FT5x26"; n = 5; break;
    case 0x55: name = "FT5x06"; n = 5; break;
    default: name = "unknown FT6x36-compatible"; break;
    }
    if (max_pts) *max_pts = n;
    return name;
}

const char *touch_chip_name(void) { return g_present ? chip_name(g_ids[0], NULL) : "none"; }
int touch_get_map(void) { return g_map; }
void touch_set_map(int m) { g_map = m & 7; }

/* Raw controller coordinates (portrait, TOUCH_RAW_W x TOUCH_RAW_H) to screen
 * pixels (landscape LCD_WIDTH x LCD_HEIGHT) using the current map bits
 * (TOUCH_SWAP_XY / FLIP_X / FLIP_Y, board.h): clamp, optionally swap, scale
 * the raw range onto the screen, then mirror. */
void touch_map_point(int rx, int ry, int *sx, int *sy)
{
    int w = TOUCH_RAW_W, h = TOUCH_RAW_H;
    if (rx < 0) rx = 0;
    if (ry < 0) ry = 0;
    if (rx >= w) rx = w - 1;
    if (ry >= h) ry = h - 1;
    int a = rx, b = ry;
    if (g_map & TOUCH_SWAP_XY) {
        a = ry, b = rx;
        int t = w;
        w = h, h = t;
    }
    /* scale the (possibly swapped) raw ranges onto the screen */
    a = a * LCD_WIDTH / w;
    b = b * LCD_HEIGHT / h;
    if (g_map & TOUCH_FLIP_X) a = LCD_WIDTH - 1 - a;
    if (g_map & TOUCH_FLIP_Y) b = LCD_HEIGHT - 1 - b;
    *sx = a, *sy = b;
}

/* ------------------------------------------------------------- pins */
void touch_pins_bus_off(void)
{
    /* Called with 3V3_BUS about to go (or already) off. */
    EXTI_D1->IMR1 &= ~(1u << EXTI_LINE_TP_INT);
    gpio_set_analog(P_TP_SCL);
    gpio_set_analog(P_TP_SDA);
    gpio_set_analog(P_TP_INT);
    gpio_write(P_TP_RST, 0);
    gpio_config(P_TP_RST, PIN_OUT, PULL_NONE); /* low: holds the controller in reset at power-up */
    g_present = false;
    g_down = 0;
}

/* Drive TP_RST low (push-pull; safe with the rail on or off). */
void touch_reset_assert(void)
{
    gpio_write(P_TP_RST, 0);
    gpio_config(P_TP_RST, PIN_OUT, PULL_NONE);
}

/* Release reset (3V3_BUS must be up); the controller reports after ~300 ms. */
void touch_reset_release(void)
{
    if (!bus_powered()) return;
    gpio_write(P_TP_RST, 1);
    g_ready_at = g_ticks_ms + READY_MS;
    g_asleep = false;
}

/* 9 clocks with SDA released frees a slave stuck mid-byte (e.g. after an MCU reset).
 * (I2C-bus specification UM10204 "Bus clear".) Bit-banged on the pins as
 * open-drain GPIOs at ~100 kHz; pins_i2c() hands them back to I2C4. */
static void bus_recover(void)
{
    gpio_write(P_TP_SCL, 1);
    gpio_write(P_TP_SDA, 1);
    gpio_config(P_TP_SCL, PIN_OD, PULL_NONE);
    gpio_config(P_TP_SDA, PIN_OD, PULL_NONE);
    delay_us(10);
    for (int i = 0; i < 9 && !gpio_read(P_TP_SDA); i++) {
        gpio_write(P_TP_SCL, 0);
        delay_us(5);
        gpio_write(P_TP_SCL, 1);
        delay_us(5);
    }
    /* STOP: SDA low -> high while SCL high */
    gpio_write(P_TP_SDA, 0);
    delay_us(5);
    gpio_write(P_TP_SDA, 1);
    delay_us(5);
}

/* PD12/PD13 to I2C4 (AF4), open drain, medium speed (fast-mode edges are
 * set by the 4.7k pull-ups, not by the driver). */
static void pins_i2c(void)
{
    gpio_config(P_TP_SCL, 4, PULL_NONE);
    gpio_config(P_TP_SDA, 4, PULL_NONE);
    gpio_port(P_TP_SCL)->OTYPER |= 1u << (P_TP_SCL & 15); /* open drain */
    gpio_port(P_TP_SDA)->OTYPER |= 1u << (P_TP_SDA & 15);
    gpio_set_speed(P_TP_SCL, 1);
    gpio_set_speed(P_TP_SDA, 1);
}

/* Register access: write the start address, then burst-read (auto-increment)
 * or write one byte. 0 = ok. */
static int reg_read(uint8_t reg, uint8_t *buf, int n) { return i2c_bus_read(TP_I2C, TOUCH_I2C_ADDR, reg, buf, n); }

static int reg_write(uint8_t reg, uint8_t v)
{
    uint8_t b[2] = {reg, v};
    return i2c_bus_write(TP_I2C, TOUCH_I2C_ADDR, b, 2);
}

/* ------------------------------------------------------------- init */
/* Probe and arm the interrupt. Waits for the controller's start-up time if
 * reset was released less than READY_MS ago. Returns 0 if the controller answered,
 * -1 if 3V3_BUS is off, -2 if it did not answer. The point count is capped at
 * TOUCH_MAX_CONTACTS (core/touch.h) even for 5-point controllers. */
int touch_init(void)
{
    g_present = false;
    if (!bus_powered()) return -1;
    /* busy-wait (SysTick keeps counting); at boot this overlaps with the LCD
     * init, so usually little or nothing is left to wait */
    while ((int32_t)(g_ticks_ms - g_ready_at) < 0) {
    }
    bus_recover();
    pins_i2c();
    i2c_bus_init(TP_I2C);
    gpio_config(P_TP_INT, PIN_IN, PULL_NONE); /* 10k pull-up on the board */

    uint8_t id[4] = {0};
    int r = reg_read(REG_CIPHER, &id[0], 1);
    if (!r) r = reg_read(REG_FIRMID, &id[1], 1);
    if (!r) r = reg_read(REG_VENDOR, &id[2], 1);
    if (!r) r = reg_read(REG_G_MODE, &id[3], 1);
    if (r) return -2;
    memcpy(g_ids, id, sizeof id);
    (void)chip_name(id[0], &g_max_pts);
    if (g_max_pts > TOUCH_MAX_CONTACTS) g_max_pts = TOUCH_MAX_CONTACTS;
    g_present = true;
    g_errors = 0;
    g_down = 0;

    /* EXTI1 <- PC1, falling edge (INT is active low) */
    RCC->APB4ENR |= RCC_APB4ENR_SYSCFGEN;
    SYSCFG->EXTICR[EXTI_LINE_TP_INT >> 2] = (SYSCFG->EXTICR[EXTI_LINE_TP_INT >> 2] & ~(0xFu << ((EXTI_LINE_TP_INT & 3) * 4))) |
                                             ((uint32_t)(P_TP_INT >> 4) << ((EXTI_LINE_TP_INT & 3) * 4));
    EXTI->FTSR1 |= 1u << EXTI_LINE_TP_INT;
    EXTI->RTSR1 &= ~(1u << EXTI_LINE_TP_INT);
    EXTI_D1->PR1 = 1u << EXTI_LINE_TP_INT;
    EXTI_D1->IMR1 |= 1u << EXTI_LINE_TP_INT;
    NVIC_SetPriority(EXTI1_IRQn, 5);
    NVIC_EnableIRQ(EXTI1_IRQn);
    g_irq = true; /* read once: picks up a finger already down */
    return 0;
}

/* TP_INT falling edge: only flags the read (I2C is main-context only, i2c.c). */
void EXTI1_IRQHandler(void)
{
    EXTI_D1->PR1 = 1u << EXTI_LINE_TP_INT;
    g_irq = true;
    g_irq_count++;
}

/* --------------------------------------------------------- service */
/* Give up on the controller after repeated I2C failures: stop interrupts,
 * release any contact the app still sees, tell the user. */
static void lost(const char *why)
{
    g_present = false;
    EXTI_D1->IMR1 &= ~(1u << EXTI_LINE_TP_INT);
    if (g_down) {
        touch_report(NULL, 0); /* release any finger the app thinks is down */
        g_down = 0;
    }
    con_log("touch: controller lost (%s); keys still work, `tp init` retries", why);
}

/* One burst read of the report registers from 0x00 into buf
 * (report_bytes() bytes); counts reads and errors for `tp`. */
static int touch_read_raw(uint8_t *buf)
{
    if (!g_present) return -1;
    int r = reg_read(0x00, buf, report_bytes());
    g_reads++;
    if (r) g_read_errors++;
    return r;
}

/* Main-loop step (hal_service): read the controller when INT fired or, while
 * a finger is down, every POLL_MS; decode the points, drop "up" events and
 * invalid ids, map to screen coordinates and pass the set of current contacts
 * to touch_report() (core/touch.c, gesture recognition). A report with zero
 * contacts is sent once when the last finger lifts. */
void touch_service(void)
{
    if (!g_present || g_asleep) return;
    uint32_t now = g_ticks_ms;
    bool due = g_irq || (g_down && now - g_last_read >= POLL_MS);
    if (!due || (int32_t)(now - g_ready_at) < 0) return;
    g_irq = false;
    g_last_read = now;

    uint8_t b[REPORT_BYTES_MAX];
    if (touch_read_raw(b)) {
        if (++g_errors >= MAX_ERRORS) lost("i2c errors");
        return;
    }
    g_errors = 0;
    memcpy(g_last_raw, b, (size_t)report_bytes());
    int n = b[2] & 0x0F;
    if (n > g_max_pts) n = 0; /* 0x0F etc: invalid frame */
    touch_point_t pts[TOUCH_MAX_CONTACTS];
    int k = 0;
    for (int i = 0; i < n; i++) {
        const uint8_t *p = &b[3 + 6 * i];
        int ev = p[0] >> 6;          /* 0 down, 1 up, 2 contact */
        int id = p[2] >> 4;
        if (ev == 1 || id > 0x0E) continue;
        int rx = ((p[0] & 0x0F) << 8) | p[1];
        int ry = ((p[2] & 0x0F) << 8) | p[3];
        int sx, sy;
        touch_map_point(rx, ry, &sx, &sy);
        pts[k++] = (touch_point_t){(int16_t)sx, (int16_t)sy, (uint8_t)id};
    }
    if (k || g_down) touch_report(pts, k);
    g_down = k;
}

/* --------------------------------------------------------- power */
/* Hibernate the controller (display off). INT is masked first so the
 * last pulses are ignored; any contact still down is released. */
void touch_sleep(void)
{
    if (!g_present || g_asleep) return;
    EXTI_D1->IMR1 &= ~(1u << EXTI_LINE_TP_INT);
    reg_write(REG_PMODE, PMODE_HIBERNATE); /* ~50 uA; wake needs a reset */
    g_asleep = true;
    if (g_down) {
        touch_report(NULL, 0);
        g_down = 0;
    }
}

/* Leave hibernate with a reset pulse (blocks RESET_LOW_MS). Reads resume
 * by themselves once g_ready_at has passed (touch_service checks it). */
void touch_wake(void)
{
    if (!g_asleep || !bus_powered()) return;
    touch_reset_assert();
    delay_us(RESET_LOW_MS * 1000); /* Trst >= 5 ms */
    touch_reset_release();         /* reports again after ~300 ms */
    EXTI_D1->PR1 = 1u << EXTI_LINE_TP_INT;
    if (g_present) EXTI_D1->IMR1 |= 1u << EXTI_LINE_TP_INT;
}

/* --------------------------------------------------------- console */
/* Decode and print a raw report (console `tp raw` / `tp watch`). */
static void print_points(const uint8_t *b)
{
    int n = b[2] & 0x0F;
    con_printf("gesture=0x%02x points=%d\n", b[1], n);
    for (int i = 0; i < n && i < g_max_pts; i++) {
        const uint8_t *p = &b[3 + 6 * i];
        int rx = ((p[0] & 0x0F) << 8) | p[1], ry = ((p[2] & 0x0F) << 8) | p[3], sx, sy;
        touch_map_point(rx, ry, &sx, &sy);
        con_printf("p%d id=%d event=%d raw=%d,%d screen=%d,%d weight=%d area=%d\n", i, p[2] >> 4, p[0] >> 6, rx, ry,
                   sx, sy, p[4], p[5] >> 4);
    }
}

/* Console command `tp [info|init|sleep|wake|map N|raw|reg R [N]|watch [ms]|speed [std|fast]]`
 * (dispatched from cmds.c). Returns 0 or a con_error() code. */
int touch_console(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "info";
    if (!strcmp(sub, "init")) {
        if (!bus_powered()) return con_error("3V3_BUS is off (rail bus on)");
        touch_reset_assert();
        delay_us(RESET_LOW_MS * 1000);
        touch_reset_release();
        int r = touch_init();
        con_printf("touch: %s\n", r ? "no answer at 0x38" : touch_chip_name());
        return r ? con_error("touch controller not responding (check the LCD FPC in J3 (touch is on pins 1-4), "
                             "3V3_BUS, I2C4/INT/RST pull-ups)")
                 : 0;
    }
    if (!strcmp(sub, "speed")) {
        /* I2C4 clock: std = 100 kHz (FT6x36 DS timing table), fast = ~340 kHz */
        if (argc > 2) {
            if (!strcmp(argv[2], "fast")) i2c4_set_fast(true);
            else if (!strcmp(argv[2], "std")) i2c4_set_fast(false);
            else return con_error("usage: tp speed [std|fast]");
            if (bus_powered()) i2c_bus_init(TP_I2C);
        }
        con_printf("i2c4=%s\n", i2c4_fast() ? "fast (~340 kHz)" : "std (100 kHz)");
        return 0;
    }
    if (!strcmp(sub, "sleep")) {
        touch_sleep();
        return 0;
    }
    if (!strcmp(sub, "wake")) {
        touch_wake();
        return 0;
    }
    if (!strcmp(sub, "map")) {
        unsigned long m;
        if (argc > 2) {
            if (!con_parse_u32(argv[2], &m) || m > 7) return con_error("usage: tp map 0-7 (1 swap, 2 flip y, 4 flip x)");
            touch_set_map((int)m);
        }
        con_printf("map=%d (swap=%d flip_x=%d flip_y=%d, default %d)\n", g_map, !!(g_map & TOUCH_SWAP_XY),
                   !!(g_map & TOUCH_FLIP_X), !!(g_map & TOUCH_FLIP_Y), TOUCH_ORIENTATION);
        return 0;
    }
    if (!strcmp(sub, "raw")) {
        uint8_t b[REPORT_BYTES_MAX];
        if (touch_read_raw(b)) return con_error("read failed (controller absent or asleep?)");
        print_points(b);
        return 0;
    }
    if (!strcmp(sub, "reg")) {
        unsigned long reg, n = 1;
        if (argc < 3 || !con_parse_u32(argv[2], &reg) || reg > 255) return con_error("usage: tp reg R [N]");
        if (argc > 3 && (!con_parse_u32(argv[3], &n) || n < 1 || n > 64)) return con_error("N 1..64");
        uint8_t b[64];
        if (!g_present || reg_read((uint8_t)reg, b, (int)n)) return con_error("read failed");
        for (unsigned long i = 0; i < n; i++) con_printf("%02lx: %02x\n", reg + i, b[i]);
        return 0;
    }
    if (!strcmp(sub, "watch")) {
        /* print raw points for N ms (default 5000) — for checking the mapping */
        unsigned long ms = 5000;
        if (argc > 2) con_parse_u32(argv[2], &ms);
        uint32_t t0 = g_ticks_ms, last = 0;
        while (g_ticks_ms - t0 < ms) {
            iwdg_kick();
            if (g_ticks_ms - last < 100) continue;
            last = g_ticks_ms;
            uint8_t b[REPORT_BYTES_MAX];
            if (touch_read_raw(b)) return con_error("read failed");
            if (b[2] & 0x0F) print_points(b);
        }
        return 0;
    }
    con_printf("present=%d asleep=%d bus=%d int_pin=%d map=%d\n", g_present, g_asleep, bus_powered(),
               gpio_read(P_TP_INT), g_map);
    con_printf("chip=%s cipher=0x%02x (FT6236 0x36, FT6336U 0x64) firmware=0x%02x vendor_id=0x%02x "
               "g_mode=%d (%s) max_points=%d\n",
               touch_chip_name(), g_ids[0], g_ids[1], g_ids[2], g_ids[3],
               g_ids[3] == 0 ? "level" : g_ids[3] == 1 ? "trigger" : "?", g_max_pts);
    con_printf("i2c4=%s irqs=%lu reads=%lu read_errors=%lu down=%d\n", i2c4_fast() ? "fast" : "std",
               (unsigned long)g_irq_count, (unsigned long)g_reads,
               (unsigned long)g_read_errors, g_down);
    return 0;
}
