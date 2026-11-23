/*
 * Bring-up firmware: the first image to flash on a new board.
 *
 * Minimal on purpose: clocks (HSE -> PLL, HSI fallback), the PB8 LED blinking
 * at 1 Hz, the USART1 console (115200 8N1, same protocol as the full firmware),
 * the INA226, the ADC and the QSPI flash. Nothing on 3V3_BUS is powered at
 * boot: SDRAM, LCD and touch are switched on only by `rail bus on` (or
 * `selftest`), so a short on that rail can be found with `power` before it is
 * loaded. No app and no CAS. The framebuffer is in internal SRAM (D2), so the
 * LCD can be tested even if the SDRAM is broken.
 *
 * Commands: everything under `help` (the hardware commands of the full
 * firmware: clocks, memtest, rail, lcd, tp, backlight, qspi, i2c, ina226, adc,
 * kpd, pwr, ...) plus `selftest` below. Keys and touches are logged as
 * "# key ..." / "# touch ..." lines.
 */
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "calc.h"
#include "version.h"
#include "console.h"
#include "gfx.h"
#include "hal.h"
#include "keyq.h"
#include "memtest.h"
#include "patterns.h"
#include "platform.h"
#include "touch.h"

/* Stubs for the symbols the core/platform code normally gets from the
 * calculator app (calc.c / app), which is not linked into this image. */
const char *calc_version(void)
{
    static char v[64];
    if (!v[0])
        snprintf(v, sizeof v, "%s (BRINGUP)", fw_version);
    return v;
}
void calc_request_power_off(void) { power_enter_standby(); }
bool calc_is_off(void) { return false; }
void app_redraw(void) { gfx_clear(C_BLACK); }

/* Print one selftest result line ("name PASS|FAIL detail"); returns 1 on
 * failure so callers can sum the failures. */
static int check(const char *what, bool ok, const char *detail)
{
    con_printf("%-10s %s  %s\n", what, ok ? "PASS" : "FAIL", detail);
    return ok ? 0 : 1;
}

/* ------------------------------------------------------- LCD bus margin */
/* GRAM write/readback at a range of write pulse widths (DATAST) with SDRAM
 * stores hitting the shared bus after every LCD write (lcd_ram_test). Setup
 * margin shows as the shortest DATAST that still passes; the data-hold side is
 * fixed by the 1k series resistors and is exercised by every run (plus one run
 * with the shortest gap between writes, ADDSET 1). Production: LCD_WR_DATAST. */
#define LCDBUS_N 256
static uint16_t g_lcdpat[LCDBUS_N];

/* Fill g_lcdpat with classic bus-test patterns: walking 1/0 (stuck or shorted
 * data lines), alternating 5555/AAAA (adjacent-line crosstalk), all-toggle
 * (maximum simultaneous switching) and a 16-bit Galois LFSR (taps 0xB400,
 * maximal length) for everything in between. */
static void lcdbus_patterns(void)
{
    int k = 0;
    for (int b = 0; b < 16; b++) g_lcdpat[k++] = (uint16_t)(1u << b);          /* walking 1 */
    for (int b = 0; b < 16; b++) g_lcdpat[k++] = (uint16_t)~(1u << b);         /* walking 0 */
    for (int i = 0; i < 32; i++) g_lcdpat[k++] = (i & 1) ? 0xAAAA : 0x5555;    /* adjacent bits */
    for (int i = 0; i < 32; i++) g_lcdpat[k++] = (i & 1) ? 0xFFFF : 0x0000;    /* all bits toggle */
    uint32_t x = 0xACE1u;
    while (k < LCDBUS_N) {                                                     /* LFSR noise */
        x = (x >> 1) ^ (-(x & 1u) & 0xB400u);
        g_lcdpat[k++] = (uint16_t)x;
    }
}

typedef struct {
    int min_pass;        /* shortest passing DATAST (sweep stops at the first failure), or -1 */
    int prod_err;        /* errors at the production setting */
    int stress_err;      /* errors at ADDSET 1 / production DATAST */
    int ref_err;         /* errors at the slow reference (DATAST 30): readback sanity */
    const char *fmt;
    uint16_t raw[6];
} lcdbus_result;

/* Sweep the LCD write pulse (DATAST, FMC kernel clocks) from slow to fast,
 * then one back-to-back run (ADDSET 1), and restore the production timing.
 * The DATAST 30 run doubles as the readback-format reference. */
static void lcdbus_run(lcdbus_result *r, bool verbose)
{
    static const uint8_t sweep[] = {30, 20, 15, 12, 10, 8, 6, 5, 4, 3, 2};
    /* 252 KB into SDRAM: past the 150 KB framebuffer, still inside the 256 KB
     * write-through MPU region, so each store goes straight onto the bus */
    volatile uint16_t *scratch = (volatile uint16_t *)(SDRAM_BASE + 0x3F000u); /* write-through region */
    lcdbus_patterns();
    r->min_pass = -1;
    r->prod_err = r->stress_err = r->ref_err = -1;
    bool failed = false;
    for (unsigned i = 0; i < sizeof sweep; i++) {
        lcd_bank_write_timing(LCD_WR_ADDSET, sweep[i]);
        const char *fmt;
        uint16_t raw[6];
        int e = lcd_ram_test(g_lcdpat, LCDBUS_N, scratch, &fmt, raw);
        if (sweep[i] == 30) {
            r->ref_err = e;
            r->fmt = fmt;
            for (int k = 0; k < 6; k++) r->raw[k] = raw[k];
        }
        if (sweep[i] == LCD_WR_DATAST) r->prod_err = e;
        if (verbose)
            con_printf("  DATAST %2u (%3lu ns WRX low): %d/%d pixels wrong (%s)\n", sweep[i],
                       (unsigned long)(sweep[i] * 1000000000ull / fmc_kernel_hz()), e, LCDBUS_N, fmt);
        if (e == 0 && !failed) r->min_pass = sweep[i];
        if (e != 0) failed = true;
    }
    lcd_bank_write_timing(1, LCD_WR_DATAST);
    const char *fmt;
    uint16_t raw[6];
    r->stress_err = lcd_ram_test(g_lcdpat, LCDBUS_N, scratch, &fmt, raw);
    if (verbose)
        con_printf("  ADDSET 1 / DATAST %u (back-to-back writes): %d/%d wrong\n", LCD_WR_DATAST, r->stress_err,
                   LCDBUS_N);
    lcd_bank_write_timing(LCD_WR_ADDSET, LCD_WR_DATAST);
}

/* Judge a sweep: writes a one-line summary into d and returns 1 on failure
 * (unrecognised readback, errors at production or back-to-back timing, or
 * less than 5 clocks of margin). */
static int lcdbus_check(const lcdbus_result *r, char *d, size_t n)
{
    if (r->ref_err != 0) {
        snprintf(d, n, "readback not recognised at DATAST 30 (%d wrong, best %s); raw %04x %04x %04x %04x %04x %04x",
                 r->ref_err, r->fmt, r->raw[0], r->raw[1], r->raw[2], r->raw[3], r->raw[4], r->raw[5]);
        return 1;
    }
    snprintf(d, n, "min DATAST %d, production %u (%s), back-to-back %s, readback %s", r->min_pass, LCD_WR_DATAST,
             r->prod_err == 0 ? "ok" : "FAILS", r->stress_err == 0 ? "ok" : "FAILS", r->fmt);
    /* want >= 5 clocks (25 ns) of setup margin below the production pulse */
    return !(r->prod_err == 0 && r->stress_err == 0 && r->min_pass >= 0 && r->min_pass <= (int)LCD_WR_DATAST - 5);
}

/* `lcdbus`: verbose margin sweep; GRAM is overwritten, so the screen is
 * cleared afterwards. */
static int cmd_lcdbus(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (!bus_powered()) return con_error("3V3_BUS is off: run `selftest` or `rail bus on` first");
    lcdbus_result r;
    con_printf("LCD bus margin: %d-pixel GRAM write/readback, SDRAM store after every LCD write\n", LCDBUS_N);
    lcdbus_run(&r, true);
    char d[160];
    int bad = lcdbus_check(&r, d, sizeof d);
    con_printf("lcdbus %s  %s\n", bad ? "FAIL" : "PASS", d);
    app_redraw();
    return bad ? con_error("LCD bus margin check failed") : 0;
}

/* selftest: power 3V3_BUS and check everything that can be checked without a person.
 * Order matters: the always-on parts (INA226, 3V3, HSE, QSPI) are checked
 * with 3V3_BUS still off, so a supply fault is seen before the bus is
 * loaded; then the bus is powered in the same order as hal_init() (touch
 * reset release first, FMC banks, SDRAM, LCD) and each block is tested.
 * "sdclk" and "current" are informational and never count as failures.
 * Takes roughly a second (LCD reset/sleep-out waits, 250 ms TE count). */
static int cmd_selftest(int argc, char **argv)
{
    char d[96];
    int fails = 0;
    hal_telemetry_t t;
    hal_telemetry(&t);
    snprintf(d, sizeof d, "vsys=%.3f V isys=%.1f mA (bus off)", (double)t.vsys_v, (double)t.isys_ma);
    fails += check("ina226", t.valid_pmon, d);
    snprintf(d, sizeof d, "3V3=%.3f V (VREFINT)", (double)t.v3v3_v);
    fails += check("3v3", t.v3v3_v > 3.1f && t.v3v3_v < 3.5f, d);
    snprintf(d, sizeof d, "clock from %s", hse_ready() ? "HSE 25 MHz" : "HSI (crystal failed)");
    fails += check("hse", hse_ready(), d);
    snprintf(d, sizeof d, "SDCLK %lu MHz, REV_ID 0x%04lx (rev %s; V -> 100 MHz, else 90 MHz)",
             (unsigned long)(sdram_sdclk_hz() / 1000000u), (unsigned long)silicon_rev(), silicon_rev_name());
    check("sdclk", true, d);
    uint32_t id = qspi_read_id();
    qspi_memory_mapped();
    snprintf(d, sizeof d, "jedec=%06lx (expect ef4018)", (unsigned long)id);
    fails += check("qspi", id == 0xEF4018u, d);

    bus_power(true);
    touch_reset_release();
    lcd_bank_init();
    sdram_init();
    hal_telemetry(&t);
    snprintf(d, sizeof d, "3V3_BUS=%.3f V isys=%.1f mA", (double)t.v3v3_bus_v, (double)t.isys_ma);
    fails += check("3v3_bus", t.v3v3_bus_v > 3.0f && t.v3v3_bus_v < 3.6f, d);
    fails += check("sdram", sdram_ok(), "boot pattern check");
    if (sdram_ok()) {
        extern uint8_t __sdram_arena_start[];
        memtest_result_t r;
        uint32_t e = memtest_all((volatile uint32_t *)__sdram_arena_start, 1u << 20, 0x1234567u, &r);
        snprintf(d, sizeof d, "1 MB: %lu errors%s%s", (unsigned long)e, e ? " first test " : "",
                 e && r.failed_test ? r.failed_test : "");
        fails += check("memtest", e == 0, d);
    }
    lcd_init();
    uint32_t lid = lcd_read_id();
    snprintf(d, sizeof d, "RDDID=%06lx", (unsigned long)lid);
    fails += check("lcd_id", lid != 0 && lid != 0xFFFFFF, d);
    uint32_t c0 = lcd_te_count(), t0 = g_ticks_ms;
    while (g_ticks_ms - t0 < 250) {
    }
    uint32_t te = lcd_te_count() - c0;
    snprintf(d, sizeof d, "%lu edges in 250 ms (~%lu Hz)", (unsigned long)te, (unsigned long)(te * 4));
    fails += check("lcd_te", te >= 10 && te <= 20, d); /* 40-80 Hz around the nominal 60 Hz */
    {
        lcdbus_result lr;
        char ld[160];
        lcdbus_run(&lr, false);
        fails += check("lcd_bus", lcdbus_check(&lr, ld, sizeof ld) == 0, ld);
    }
    pattern_draw("bars");
    gfx_flush();
    int bl = backlight_calibrate();
    snprintf(d, sizeof d, "gain=%.2f at 20 mA (anode 3V3_BUS)", (double)backlight_gain());
    fails += check("backlight", bl == 0, d);
    backlight_set_ma(20);
    int tp = touch_init();
    fails += check("touch", tp == 0, tp == 0 ? touch_chip_name() : "no answer at 0x38");
    hal_telemetry(&t);
    snprintf(d, sizeof d, "isys=%.1f mA with bus, LCD, 20 mA backlight", (double)t.isys_ma);
    check("current", true, d);
    con_printf("selftest: %d failure(s)\n", fails);
    return fails ? con_error("%d check(s) failed", fails) : 0;
}

/* Takes the place of the calculator app's command table (core/console.c
 * searches core, app, then hal_console_cmds from cmds.c). */
const console_cmd app_console_cmds[] = {
    {"selftest", "", "power 3V3_BUS and test INA226, 3V3, HSE, QSPI, SDRAM, LCD, TE, backlight, touch", cmd_selftest},
    {"lcdbus", "", "LCD write-timing margin: DATAST sweep with GRAM readback under SDRAM traffic", cmd_lcdbus},
    {0, 0, 0, 0},
};

/* Main loop: service drivers, log key/touch events to the console, push any
 * drawing to the LCD, run console commands, and sleep (WFI) until the next
 * interrupt when idle; SysTick wakes it at least every 1 ms. */
int main(void)
{
    /* Keep the debug port usable while the main loop sleeps in WFI. After a
     * power-on reset DBGMCU_CR is 0, and the core then answers SWD only
     * between WFIs (rev A board 1: OpenOCD read Cortex-M PARTNO 0x0 and could
     * not flash). The calculator image leaves it off: it costs Sleep/Stop power. */
    DBGMCU->CR |= DBGMCU_CR_DBG_SLEEPD1 | DBGMCU_CR_DBG_STOPD1 | DBGMCU_CR_DBG_STANDBYD1;
    hal_init();
    heartbeat_enable(true);
    gfx_init(hal_framebuffer());
    console_init();
    con_log("bringup %s ready: `help`, `selftest`, `rail bus on`", calc_version());
    for (;;) {
        iwdg_kick(); /* runs only if the option bytes select the hardware watchdog (iwdg.c) */
        hal_service();
        key_event_t ev;
        while (keyq_pop(&ev))
            con_log("key %s %s", key_names[ev.key], ev.pressed ? "down" : "up");
        touch_event_t te;
        while (touch_pop(&te))
            con_log("touch %s %d,%d", touch_event_name(te.type), te.x, te.y);
        gfx_flush();
        bool ran = console_poll();
        if (!ran && !console_waiting()) __WFI();
    }
}
