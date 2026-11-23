/*
 * hal_stm32.c — hal.h implementation for the STM32H743 board.
 *
 * This is the seam between the portable core/app/CAS code (which only sees
 * include/hal.h) and the platform drivers in this directory (platform.h).
 * It owns the boot sequence (hal_init), the framebuffer/arena placement with
 * an on-chip SRAM fallback when the SDRAM fails, and thin wrappers for the
 * display, backlight, keypad, telemetry, QSPI storage and console.
 *
 * Boot order (hal_init, production image) and why:
 *   1. power_early_init: record the reset/wake reason before anything clears
 *      the RCC/PWR flags.
 *   2. clock_init, mpu_cache_init, systick_init: 400 MHz clock tree, MPU
 *      regions (FMC/SDRAM/QSPI attributes) before caches, 1 kHz tick.
 *   3. gpio_config_from_table, keypad_init, uart_init: safe pin states, the
 *      key scanner (from here keys held through the reset are tracked: the
 *      safe-mode key ON + CLEAR), then the console so every later step can log
 *      ("# ..." lines).
 *   4. bus_power(true): 3V3_BUS (SDRAM + LCD + touch) with the LCD and touch
 *      resets held low; the touch reset is released immediately because the
 *      FT6236 needs ~300 ms before it answers, which overlaps the LCD init.
 *   5. FMC banks: LCD (NOR/SRAM bank 1) and SDRAM, then the panel itself.
 *   6. I2C2 + INA226 + its alert line, ADC, backlight (calibrated
 *      against BL_SENSE before the first non-zero setpoint), touch, QSPI
 *      (left memory-mapped), SD card detect.
 * The BRINGUP variant stops short of powering 3V3_BUS so the board can be
 * checked rail by rail from the console (bringup_main.c).
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "console.h"
#include "evlog.h"
#include "hal.h"
#include "platform.h"

/* The framebuffer lives at the very start of SDRAM (0xD0000000), see the
 * linker script; it can be read over SWD (`openocd/readfb.sh`). */
__attribute__((section(".sdram_fb"), aligned(32), used)) static uint16_t g_fb[LCD_WIDTH * LCD_HEIGHT];
extern uint8_t __sdram_arena_start[], __sdram_arena_end[];

/* Fallback when SDRAM fails: framebuffer in D2 SRAM, app/CAS arena in AXI SRAM
 * (cas_init needs >= 256 KB; the app then runs without its graph cache), so
 * the calculator and the console still work for debugging. */
#define FALLBACK_ARENA (320u * 1024u)
__attribute__((section(".sram_d2"), aligned(32))) static uint16_t g_fb_fallback[LCD_WIDTH * LCD_HEIGHT];
__attribute__((section(".axi_fallback"), aligned(32))) static uint8_t g_arena_fallback[FALLBACK_ARENA];

static int g_backlight_pct = HAL_BACKLIGHT_DEFAULT_PCT; /* user setting, kept while the display is off */
static bool g_display_on;

/* printf-style boot message on the UART console, prefixed "# " (comment
 * lines for host tools such as calcctl.py) and truncated to ~158 chars. */
static void boot_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void boot_log(const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof buf - 2) n = sizeof buf - 2;
    uart_write("# ", 2);
    uart_write(buf, (size_t)n);
    uart_write("\n", 1);
}

/* Log the SDRAM clock choice: the FMC kernel clock depends on the silicon
 * revision (fmc.c / system.c), so the boot log records which one was used. */
static void log_sdram_clock(void)
{
    boot_log("sdram clock: SDCLK %lu MHz (FMC kernel %lu MHz from %s), REV_ID 0x%04lx (rev %s), refresh %lu",
             (unsigned long)(sdram_sdclk_hz() / 1000000u), (unsigned long)(fmc_kernel_hz() / 1000000u),
             fmc_kernel_hz() == HCLK_HZ ? "HCLK3" : "PLL2R", (unsigned long)silicon_rev(), silicon_rev_name(),
             (unsigned long)sdram_refresh_count());
}

#ifdef BRINGUP
/* Bring-up image (platform/stm32h743/bringup): no 3V3_BUS at boot, see bringup_main.c. */
void hal_init(void)
{
    power_early_init();
    iwdg_early(); /* hardware-watchdog option byte only; the bring-up image never starts it */
    clock_init();
    mpu_cache_init();
    systick_init();
    gpio_config_from_table();
    uart_init(115200);
    boot_log("BRINGUP firmware: boot %s", power_boot_reason());
    boot_log("clock: SYSCLK %lu MHz from %s, LSE %s, silicon rev %s (0x%04lx), io-comp %s",
             (unsigned long)(SystemCoreClock / 1000000u), hse_ready() ? "HSE" : "HSI (HSE FAILED)",
             lse_ready() ? "running" : "starting", silicon_rev_name(), (unsigned long)silicon_rev(),
             io_compensation_ready() ? "ready" : "NOT READY");
    log_sdram_clock();
    keypad_init();
    i2c_init();
    boot_log("ina226: %s", ina226_init() == 0 ? "ok" : "not found");
    adc_init();
    backlight_init();
    int qr = qspi_init(); /* first: it enables the QUADSPI clock */
    boot_log("qspi: JEDEC %06lx", (unsigned long)qspi_read_id());
    if (qr == 0) qspi_memory_mapped(); /* qspi_read_id leaves indirect mode */
    sd_init();
    g_display_on = true;
    boot_log("3V3_BUS is OFF: `power` to check currents, then `rail bus on` or `selftest`");
}
#else
void hal_init(void)
{
    power_early_init();
    iwdg_early(); /* only if the option bytes select the hardware watchdog (iwdg.c) */
    clock_init();
    mpu_cache_init();
    systick_init();
    gpio_config_from_table();
    keypad_init(); /* scan from now on: the safe-mode keys held through the reset count (keypad.c) */
    uart_init(115200);
    boot_log("boot: %s", power_boot_reason());
    boot_log("clock: SYSCLK %lu MHz from %s, LSE %s, silicon rev %s (0x%04lx), io-comp %s",
         (unsigned long)(SystemCoreClock / 1000000u), hse_ready() ? "HSE" : "HSI (HSE FAILED)",
         lse_ready() ? "running" : "starting", silicon_rev_name(), (unsigned long)silicon_rev(),
         io_compensation_ready() ? "ready" : "NOT READY");
    log_sdram_clock();
    if (g_crash.magic == CRASH_MAGIC)
        boot_log("previous crash: pc=%08lx lr=%08lx cfsr=%08lx (console `crash`)", (unsigned long)g_crash.pc,
             (unsigned long)g_crash.lr, (unsigned long)g_crash.cfsr);

    /* 3V3_BUS: SDRAM + LCD + touch. Reset lines are held low through power-up. */
    bus_power(true);
    touch_reset_release(); /* FT6236 reports ~300 ms after reset, overlapping the LCD init below */
    lcd_bank_init();
    int r = sdram_init();
    boot_log("sdram: %s", r == 0 ? "ok (32 MB at 0xD0000000)" : "FAILED quick check - using SRAM fallback");
    lcd_init();
    boot_log("lcd: RDDID %06lx, MADCTL %02x", (unsigned long)lcd_read_id(), lcd_get_madctl());
    i2c_init();
    r = ina226_init();
    boot_log("ina226: %s", r == 0 ? "ok" : "not found");
    pmon_alert_init();
    adc_init();
    backlight_init();
    r = backlight_calibrate();
    boot_log("backlight: %s (gain %.2f)", r == 0 ? "ok" : "NO CURRENT - panel/backlight missing?",
             (double)backlight_gain());
    hal_backlight_set(g_backlight_pct);
    g_display_on = true;
    r = touch_init();
    boot_log("touch: %s%s", r == 0 ? touch_chip_name() : "not found (keys only)", r == 0 ? " ok" : "");
    r = qspi_init();
    uint32_t qid = qspi_read_id();
    if (r == 0) qspi_memory_mapped(); /* qspi_read_id leaves indirect mode */
    boot_log("qspi: JEDEC %06lx %s", (unsigned long)qid, r == 0 ? "ok, memory-mapped" : "FAILED");
    sd_init();
    boot_log("sd: card %s", sd_card_present() ? "present" : "absent");
    boot_log("watchdog option bytes: %s in Stop, %s in Standby (`optbytes`)",
             iwdg_frozen_in_stop() ? "frozen" : "RUNS", iwdg_frozen_in_standby() ? "frozen" : "RUNS");
}

#endif

/* Main-loop housekeeping (hal.h): work that interrupts only flag, done in
 * thread context because it needs I2C or ADC transfers. */
void hal_service(void)
{
    usb_service();
    touch_service();
    pmon_service();
    battery_service();
    backlight_service();
}

bool hal_touch_available(void) { return touch_hw_present(); }

size_t hal_cas_stack_limit(void)
{
    /* Everything but 24 KB: frames below cas_eval (main loop, console, app
     * evaluate buffers ~8 KB) and nested interrupt frames. */
    uint32_t total = stack_size();
    return total > (40u << 10) ? total - (24u << 10) : (16u << 10);
}

const char *hal_platform_name(void) { return "stm32h743"; }

/* Software reset; the empty polled write waits for TC, i.e. for console
 * output already queued to finish before the reset cuts it off. */
void hal_reset(void)
{
    uart_write_raw_polled("");
    NVIC_SystemReset();
}

/* Reboot into the ST system-memory bootloader: g_boot_magic lives in .noinit
 * RAM that survives a reset, and the reset handler jumps to the ROM when it
 * sees the magic (startup.c). DSB makes sure the store has completed first. */
void hal_enter_bootloader(void)
{
    hal_usb_detach(); /* the host sees our device leave before the ROM's appears */
    g_boot_magic = BOOT_MAGIC_DFU;
    __DSB();
    uart_write_raw_polled("# entering ROM bootloader (USB DFU / USART1)\r\n");
    NVIC_SystemReset();
}

#ifdef BRINGUP
uint16_t *hal_framebuffer(void) { return g_fb_fallback; } /* LCD tests independent of the SDRAM */
#else
uint16_t *hal_framebuffer(void) { return sdram_ok() ? g_fb : g_fb_fallback; }
#endif

/* Heap arena for the app/CAS: the linker-reserved SDRAM region, or the
 * 320 KB AXI SRAM fallback when sdram_init() failed. */
void *hal_arena(size_t *size)
{
    if (!sdram_ok()) {
        *size = sizeof g_arena_fallback;
        return g_arena_fallback;
    }
    *size = (size_t)(__sdram_arena_end - __sdram_arena_start);
    return __sdram_arena_start;
}

/* Nestable critical section: mask all configurable interrupts (PRIMASK) and
 * return the previous state for hal_irq_restore(). */
uint32_t hal_irq_save(void)
{
    uint32_t p = __get_PRIMASK();
    __disable_irq();
    return p;
}

void hal_irq_restore(uint32_t state) { __set_PRIMASK(state); }

uint32_t hal_millis(void) { return g_ticks_ms; }

/* Sleep-wait on the 1 kHz tick (WFI between ticks); resolution 1 ms, so the
 * actual delay is between ms-1 and ms. Must not be called with interrupts
 * masked (g_ticks_ms would never advance). */
void hal_delay_ms(uint32_t ms)
{
    uint32_t t0 = g_ticks_ms;
    while (g_ticks_ms - t0 < ms) __WFI();
}

/* DWT cycle counter (core clock, wraps every ~10.7 s at 400 MHz). */
uint32_t hal_cycles(void) { return DWT->CYCCNT; }
uint32_t hal_cycles_per_us(void) { return SystemCoreClock / 1000000u; }

void hal_display_flush(const uint16_t *fb, int x, int y, int w, int h)
{
    if (!g_display_on) return;
    lcd_flush(fb, x, y, w, h);
}

/* Display on/off (screen blanking, not power-off). Off order: backlight to
 * 0 mA first so the panel's sleep-in transition is never visible, then touch
 * to hibernate, then the ST7789 to sleep. On order is the reverse. 3V3_BUS
 * stays powered, so the framebuffer (SDRAM) is retained. */
void hal_display_enable(bool on)
{
    if (on == g_display_on) return;
    if (on) {
        lcd_sleep(false);
        hal_backlight_set(g_backlight_pct);
        touch_wake();
    } else {
        backlight_set_ma(0);
        touch_sleep(); /* 8-13 mA -> 25-50 uA */
        lcd_sleep(true);
    }
    g_display_on = on;
}

/* Brightness in percent of BL_LIMIT_MA (90 mA, the panel's nominal LED
 * current), linear in current. */
void hal_backlight_set(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    g_backlight_pct = percent;
    backlight_set_ma(BL_LIMIT_MA * (float)percent / 100.0f);
}

int hal_backlight_get(void) { return g_backlight_pct; }

uint64_t hal_keypad_raw(void) { return keypad_state(); }
uint32_t hal_keypad_held_ms(uint64_t mask) { return keypad_held_ms(mask); }

/* ---- watchdog and reset cause (hal.h; iwdg.c, power.c) ---- */
void hal_watchdog_start(void)
{
    iwdg_start();
    char b[96];
    iwdg_status(b, sizeof b);
    boot_log("watchdog: %s", b);
}
void hal_watchdog_kick(void) { iwdg_kick(); }
bool hal_watchdog_running(void) { return iwdg_running(); }
bool hal_watchdog_expired(void) { return false; } /* the IWDG resets by itself */
void hal_watchdog_status(char *out, size_t n) { iwdg_status(out, n); }
int hal_reset_cause(void) { return power_reset_cause(); }

void hal_idle(void) { __WFI(); }

void hal_power_off(void) { power_enter_standby(); }

/* Snapshot of the power telemetry (status screen / console): VSYS from the
 * INA226 (NaN if absent), 3V3 = VDDA from VREFINT, switched rails and the
 * backlight current through their ADC dividers/sense resistor, die
 * temperature, battery voltage (switched divider, ~16 ms) and USB VBUS.
 * Blocking: several I2C and ADC conversions, main context only. */
void hal_telemetry(hal_telemetry_t *t)
{
    memset(t, 0, sizeof *t);
    t->valid_pmon = ina226_present() && ina226_read(&t->vsys_v, &t->isys_ma, &t->psys_mw, NULL) == 0;
    if (!t->valid_pmon) t->vsys_v = t->isys_ma = t->psys_mw = NAN;
    t->v3v3_v = adc_vdda();
    t->v3v3_bus_v = adc_read_volts(ADC_CH_3V3_BUS) * RAIL_DIV_RATIO;
    t->v3v3_sd_v = adc_read_volts(ADC_CH_3V3_SD) * RAIL_DIV_RATIO;
    t->bl_ma = adc_read_volts(ADC_CH_BL) / BL_SENSE_OHMS * 1000.0f;
    t->temp_c = adc_temp_c();
    t->vbat_v = adc_vbat();
    t->usb_present = hal_usb_present();
}

bool hal_usb_present(void) { return gpio_read(P_USB_VBUS_SENSE) != 0; }

/* Persistent storage = the whole 16 MB W25Q128 QSPI flash (byte offsets from
 * its start); 0 if the chip did not initialise. Writes and erases go through
 * qspi.c's indirect mode, which re-enters memory-mapped mode and invalidates
 * the D-cache lines of the modified range afterwards, so reads through the
 * mapped window never return stale data. */
bool hal_crash_text(char *out, size_t n)
{
    if (g_crash.magic != CRASH_MAGIC)
        return false;
    snprintf(out, n,
             "%lu since power-on, last at pc %08lx lr %08lx (cfsr %08lx hfsr %08lx bfar %08lx; "
             "sp %08lx, stack %lu of %lu bytes used)",
             (unsigned long)g_crash.count, (unsigned long)g_crash.pc, (unsigned long)g_crash.lr,
             (unsigned long)g_crash.cfsr, (unsigned long)g_crash.hfsr, (unsigned long)g_crash.bfar,
             (unsigned long)g_crash.sp, (unsigned long)g_crash.stack_used, (unsigned long)stack_size());
    return true;
}

size_t hal_storage_size(void) { return qspi_is_ok() ? QSPI_SIZE : 0; }

int hal_storage_read(uint32_t off, void *buf, size_t n)
{
    if (off > QSPI_SIZE || n > QSPI_SIZE - off || !qspi_is_ok()) return -1;
    /* memory-mapped read is fastest; the region is write-through cached */
    memcpy(buf, (const void *)(QSPI_MMAP_BASE + off), n);
    return 0;
}

/* XIP view for code modules (hal.h): the memory-mapped window, which is the
 * normal state of the QUADSPI (every indirect operation restores it). */
const void *hal_storage_xip(uint32_t off, size_t n)
{
    if (off > QSPI_SIZE || n > QSPI_SIZE - off || !qspi_is_ok()) return NULL;
    return (const void *)(QSPI_MMAP_BASE + off);
}

/* After installing / moving code in the QSPI: the writes went around the
 * caches (indirect mode), and the D-cache lines of the range were already
 * invalidated by qspi.c; drop stale instruction lines too (global
 * invalidate: cheap, nothing dirty in the I-cache). */
void hal_code_sync(void)
{
    SCB_CleanDCache(); /* code-load modules: the copy in the SDRAM (write-back cached) */
    __DSB();
    SCB_InvalidateICache();
    __DSB();
    __ISB();
}

/* The code area of code-load modules (app/modx.c modx_code_init): MPU region 5
 * over it, Normal write-back read/write-allocate like region 1, but executable
 * (the default memory map makes 0xC0000000-0xDFFFFFFF execute-never; an MPU
 * region's attributes replace it). Region 5 wins over regions 1 and 2. */
__attribute__((section(".flash2.text"))) void hal_code_region(void *base, size_t size)
{
    if (size < 32 || (size & (size - 1)) || ((uintptr_t)base & (size - 1)))
        return;
    uint32_t log2 = 31u - __CLZ((uint32_t)size);
    __DMB();
    ARM_MPU_SetRegion(ARM_MPU_RBAR(5, (uint32_t)(uintptr_t)base),
                      ARM_MPU_RASR(0, ARM_MPU_AP_FULL, 1, 0, 1, 1, 0, log2 - 1u));
    __DSB();
    __ISB();
}

/* Erase whole 4 KB sectors covering [off, off+n); off must be sector aligned.
 * 64 KB-aligned runs of 64 KB use the block erase (one D8h instead of
 * sixteen 20h: ~150 ms instead of ~0.7 s typical). */
/* Seen on a board, rarely: a module install whose small writes failed until the next boot,
 * after many QSPI operations; a boot cures it, so a failed erase or write resets the
 * controller and the chip as qspi_init() does at boot, logs it, and tries once more.
 * Repeating an erase or a program of the same bytes is harmless on NOR flash. */
int hal_storage_reset(void) { return qspi_init(); }

static int erase_range(uint32_t off, size_t n)
{
    for (size_t a = 0; a < n;) {
        uint32_t at = off + (uint32_t)a;
        if (at % 65536u == 0 && n - a >= 65536u) {
            if (qspi_erase_block64(at)) return -1;
            a += 65536u;
        } else {
            if (qspi_erase_sector(at)) return -1;
            a += HAL_STORAGE_SECTOR;
        }
    }
    return 0;
}

int hal_storage_erase(uint32_t off, size_t n)
{
    if (off % HAL_STORAGE_SECTOR || off > QSPI_SIZE || n > QSPI_SIZE - off) return -1;
    if (!erase_range(off, n)) return 0;
    evlog_add("qspi: erase %06lx+%lu failed: reset and retry", (unsigned long)off, (unsigned long)n);
    if (hal_storage_reset() || erase_range(off, n)) {
        evlog_add("qspi: erase %06lx+%lu retry failed", (unsigned long)off, (unsigned long)n);
        return -1;
    }
    return 0;
}

int hal_storage_write(uint32_t off, const void *buf, size_t n)
{
    if (off > QSPI_SIZE || n > QSPI_SIZE - off) return -1;
    int r = qspi_write(off, buf, n);
    if (!r) return 0;
    evlog_add("qspi: write %06lx+%lu failed (%d): reset and retry", (unsigned long)off, (unsigned long)n, r);
    if (hal_storage_reset() || (r = qspi_write(off, buf, n)) != 0) {
        evlog_add("qspi: write %06lx+%lu retry failed (%d)", (unsigned long)off, (unsigned long)n, r);
        return -1;
    }
    return 0;
}

/* ---- recovery and optional content (hal.h) ---- */
/* Boot-attempt record for core/safemode.c: .noinit DTCM survives a reset
 * (crash, NVIC_SystemReset) but not Standby or a power loss, so only failed
 * boots in a row count. */
__attribute__((section(".noinit"))) static uint32_t g_boot_record[2];
uint32_t *hal_boot_record(void) { return g_boot_record; }


/* How the optional devices answered at boot (MEM > About, console `devices`). */
void hal_device_report(void (*kv)(const char *dev, const char *status, void *user), void *user)
{
    char b[64];
    uint32_t id = qspi_boot_id();
    if (qspi_is_ok())
        snprintf(b, sizeof b, "ok (W25Q128, JEDEC %06lx)", (unsigned long)id);
    else
        snprintf(b, sizeof b, "not responding (JEDEC %06lx)", (unsigned long)id);
    kv("QSPI", b, user);
    kv("microSD", sd_card_present() ? "card present" : "no card", user);
    snprintf(b, sizeof b, "%s", touch_hw_present() ? touch_chip_name() : "not found (keys only)");
    kv("Touch", b, user);
    kv("Power monitor", ina226_present() ? "ok (INA226)" : "not found (no current readings)", user);
    kv("SDRAM", sdram_ok() ? "ok (32 MB)" : "FAILED its test (small memory mode)", user);
    iwdg_status(b, sizeof b);
    kv("Watchdog", b, user);
}

/* The console runs on the UART and, while a computer has the USB serial port
 * open, on USB too: input comes from either. A line is taken whole from the
 * link it started on (the other waits, at most LINE_LOCK_MS of silence on the
 * first), so lines typed on both at once never mix. Output goes to both,
 * except that the reply to a line that came over USB goes to USB only: the
 * UART writes wait for every byte at 115200 baud, so copying a screen dump
 * (tens of KB) there stalled the calculator for seconds per dump, long enough
 * for the 2 s watchdog when a page mirrored the screen. */
#define LINE_LOCK_MS 1000u
enum { SRC_ANY, SRC_UART, SRC_USB };
static int g_reply_src = SRC_ANY; /* link of the last complete input line */
int hal_console_read(char *buf, int max)
{
    static int src;
    static uint32_t src_t;
    int n = 0, from = SRC_ANY;
    if (max <= 0)
        return 0;
    if (src != SRC_ANY && g_ticks_ms - src_t > LINE_LOCK_MS)
        src = SRC_ANY; /* a half line left on one link does not block the other */
    if (src != SRC_USB && (n = uart_read(buf, 1)) > 0)
        from = SRC_UART;
    else if (src != SRC_UART && (n = usb_console_read(buf, 1)) > 0)
        from = SRC_USB;
    if (n <= 0)
        return 0;
    if (buf[0] == '\n' || buf[0] == '\r')
        g_reply_src = from, src = SRC_ANY;
    else
        src = from;
    src_t = g_ticks_ms;
    return n;
}

void hal_console_write(const char *s, size_t n)
{
    usb_console_write(s, n);
    if (g_reply_src != SRC_USB || !hal_usb_configured())
        uart_write(s, n);
}
