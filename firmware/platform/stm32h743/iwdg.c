/*
 * Independent watchdog IWDG1 (RM0433 "Independent watchdog (IWDG)") and the
 * option bytes that decide how it behaves in low-power modes (RM0433
 * "Embedded flash memory (FLASH)", FLASH_OPTSR_CUR / FLASH_OPTSR_PRG).
 *
 * Timeout: LSI (32 kHz typ., 29.4-33.6 kHz over temperature, DS12110) / 64 =
 * 500 Hz, reload 999 -> 1000 counts = 2.0 s (1.9-2.2 s). A hang of the main
 * loop (no hal_watchdog_kick for that long) resets the MCU; RCC_RSR then has
 * IWDG1RSTF (power.c: "last reset: watchdog"), and since the boot-attempt
 * record in .noinit DTCM survives the reset, the failed boot counts towards
 * safe mode (core/safemode.h) like a crash does.
 *
 * Low-power modes. The IWDG is clocked by the LSI and keeps counting in Stop
 * and Standby unless the option bytes freeze it there (FZ_IWDG_STOP,
 * FZ_IWDG_SDBY: 1 = runs, the factory default; 0 = frozen). Our power
 * management (power.c):
 *   - between key presses the core only sleeps (WFI in hal_idle), woken by the
 *     1 kHz SysTick, so the main loop runs and kicks every millisecond: no
 *     issue;
 *   - Standby is "off" and its only exit is a reset, so "wake up to refresh"
 *     is impossible there: an unfrozen IWDG would reset the switched-off
 *     calculator ~2 s after SHIFT ON, i.e. switch it back on. Hence
 *     FZ_IWDG_SDBY = 0 is required, and iwdg_start() refuses to start the
 *     watchdog without it (About and the boot log say why);
 *   - Stop (console `pwr stop`, bring-up and current measurements only) keeps
 *     RAM and state, so it could wake on the RTC to refresh; with
 *     FZ_IWDG_STOP = 0 it does not have to (power_enter_stop slices a Stop
 *     into <= 1.5 s RTC wake-ups only when the option byte is not set).
 * Decision: freeze in both via the option bytes, a one-time board set-up step
 * (`optbytes iwdg-freeze` on the console, or STM32CubeProgrammer, see the
 * bring-up checklist in README.md); the firmware checks the setting at every
 * boot and reports it (`optbytes`, `wdg`, MEM > About "Watchdog").
 *
 * IWDG1_SW (option byte): 1 = software watchdog, started by iwdg_start()
 * (factory default); 0 = hardware watchdog, running from reset with a ~0.5 s
 * default timeout, which iwdg_early() stretches to 2 s first thing in hal_init.
 * DBGMCU_APB4FZ1.DBG_IWDG1 stops it while a debugger halts the core.
 * Once started it cannot be stopped; any reset stops a software watchdog, so
 * the ROM bootloader (console `bootloader`) and the bring-up image never see it.
 */
#include <stdio.h>
#include <string.h>
#include "console.h"
#include "hal.h"
#include "platform.h"

#define IWDG_KEY_RELOAD 0xAAAAu
#define IWDG_KEY_ENABLE 0xCCCCu
#define IWDG_KEY_ACCESS 0x5555u
#define IWDG_PR_DIV64 4u
#define IWDG_RELOAD (HAL_WATCHDOG_MS / 2u - 1u) /* 500 Hz counts */
#define OPT_KEY1 0x08192A3Bu                    /* FLASH_OPTKEYR unlock sequence */
#define OPT_KEY2 0x4C5D6E7Fu
#define STOP_SLICE_MS 1500u                     /* Stop with an unfrozen IWDG: wake this often */

static bool g_running;
static bool g_started_once; /* iwdg_start ran (so "off" has a reason) */

bool iwdg_frozen_in_stop(void) { return !(FLASH->OPTSR_CUR & FLASH_OPTSR_FZ_IWDG_STOP); }
bool iwdg_frozen_in_standby(void) { return !(FLASH->OPTSR_CUR & FLASH_OPTSR_FZ_IWDG_SDBY); }
static bool hw_mode(void) { return !(FLASH->OPTSR_CUR & FLASH_OPTSR_IWDG1_SW); }
bool iwdg_running(void) { return g_running; }

void iwdg_kick(void)
{
    if (g_running)
        IWDG1->KR = IWDG_KEY_RELOAD;
}

/* Prescaler and reload: writable after the access key; the new values are
 * taken over within ~5 LSI cycles (PVU/RVU clear), bounded at 10 ms here. */
static void configure(void)
{
    DBGMCU->APB4FZ1 |= DBGMCU_APB4FZ1_DBG_IWDG1; /* frozen while a debugger halts the core */
    IWDG1->KR = IWDG_KEY_ACCESS;
    IWDG1->PR = IWDG_PR_DIV64;
    IWDG1->RLR = IWDG_RELOAD;
    for (uint32_t i = 0; i < 1000u && (IWDG1->SR & (IWDG_SR_PVU | IWDG_SR_RVU)); i++)
        delay_us(10);
    IWDG1->KR = IWDG_KEY_RELOAD;
}

void iwdg_early(void)
{
    if (!hw_mode())
        return;
    configure(); /* already counting since reset (~0.5 s default): give it the 2 s */
    g_running = true;
}

void iwdg_start(void)
{
    g_started_once = true;
#if !defined(CONFIG_WATCHDOG)
    if (!g_running)
        return; /* disabled in this build (a hardware-mode watchdog still runs) */
#endif
    if (g_running) {
        iwdg_kick();
        return;
    }
    if (!iwdg_frozen_in_standby())
        return; /* it would wake the switched-off calculator: iwdg_status says so */
    IWDG1->KR = IWDG_KEY_ENABLE; /* starts the LSI and the counter */
    configure();
    g_running = true;
}

void iwdg_status(char *out, size_t n)
{
    if (g_running)
        snprintf(out, n, "on (2 s%s; %s)", hw_mode() ? ", hardware mode" : "",
                 iwdg_frozen_in_stop() ? "frozen in Stop and Standby" : "frozen in Standby, Stop refreshes it");
    else if (!g_started_once)
        snprintf(out, n, "off (not started yet)");
#if !defined(CONFIG_WATCHDOG)
    else
        snprintf(out, n, "off (disabled in this build: CONFIG_WATCHDOG)");
#else
    else
        snprintf(out, n, "off: option bytes let it run in Standby (console `optbytes iwdg-freeze`)");
#endif
}

/* Stop with a running, unfrozen IWDG: wake on the RTC every STOP_SLICE_MS,
 * kick, go back. Used by power_enter_stop(). */
uint32_t iwdg_stop_slice_ms(void) { return g_running && !iwdg_frozen_in_stop() ? STOP_SLICE_MS : 0; }

/* Program FZ_IWDG_STOP = FZ_IWDG_SDBY = 0, leaving every other option (RDP,
 * BOR, boot addresses, IWDG1_SW) as it is (RM0433 "Option byte modification":
 * unlock OPTCR, write OPTSR_PRG, OPTSTART, wait for OPT_BUSY, lock). */
static int freeze_program(void)
{
    if (FLASH->OPTCR & FLASH_OPTCR_OPTLOCK) {
        FLASH->OPTKEYR = OPT_KEY1;
        FLASH->OPTKEYR = OPT_KEY2;
    }
    if (FLASH->OPTCR & FLASH_OPTCR_OPTLOCK)
        return -1;
    FLASH->OPTCCR = FLASH_OPTCCR_CLR_OPTCHANGEERR;
    FLASH->OPTSR_PRG = FLASH->OPTSR_CUR & ~(FLASH_OPTSR_FZ_IWDG_STOP | FLASH_OPTSR_FZ_IWDG_SDBY);
    FLASH->OPTCR |= FLASH_OPTCR_OPTSTART;
    int r = 0;
    for (uint32_t t0 = g_ticks_ms; FLASH->OPTSR_CUR & FLASH_OPTSR_OPT_BUSY;) {
        iwdg_kick();
        if (g_ticks_ms - t0 > 3000u) {
            r = -2;
            break;
        }
    }
    if (!r && (FLASH->OPTSR_CUR & FLASH_OPTSR_OPTCHANGEERR))
        r = -3;
    FLASH->OPTCR |= FLASH_OPTCR_OPTLOCK;
    return r;
}

/* console `optbytes [iwdg-freeze]`: the watchdog-related option bytes and
 * RDP; with iwdg-freeze, the one-time board set-up of the bring-up checklist. */
int iwdg_console(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "iwdg-freeze")) {
        if (iwdg_frozen_in_stop() && iwdg_frozen_in_standby()) {
            con_printf("already set: the IWDG is frozen in Stop and Standby\n");
            return 0;
        }
        int r = freeze_program();
        if (r)
            return con_error("option byte programming failed (%d)", r);
        con_printf("programmed: FZ_IWDG_STOP=0 FZ_IWDG_SDBY=0; `reset` to start the watchdog\n");
    } else if (argc > 1) {
        return con_error("usage: optbytes [iwdg-freeze]");
    }
    uint32_t o = FLASH->OPTSR_CUR;
    char st[96];
    iwdg_status(st, sizeof st);
    con_printf("optsr_cur=%08lx rdp=%02lx iwdg1_sw=%d fz_iwdg_stop=%d fz_iwdg_sdby=%d (0 = frozen)\nwatchdog=%s\n",
               (unsigned long)o, (unsigned long)((o >> 8) & 0xFF), (int)((o >> FLASH_OPTSR_IWDG1_SW_Pos) & 1),
               (int)((o >> FLASH_OPTSR_FZ_IWDG_STOP_Pos) & 1), (int)((o >> FLASH_OPTSR_FZ_IWDG_SDBY_Pos) & 1), st);
    return 0;
}
