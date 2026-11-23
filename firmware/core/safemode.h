/*
 * safemode — boot recovery (docs/QSPI_CONTENT.md, "Safe mode").
 *
 * Safe mode boots plain: no QSPI content (help shows the built-in essentials,
 * phase 2: the numeric stub CAS), no saved session (defaults; the saved one
 * stays untouched in the flash and comes back at the next normal boot), no
 * offer to install content from the microSD card. It is the way out of a
 * content image or a saved session that crashes the firmware at boot.
 *
 * Entered when
 *   - ON and CLEAR are held together while the calculator starts, both down
 *     for SAFEMODE_KEY_MS (hold CLEAR, press ON, keep both down until the home
 *     screen shows SAFE MODE). A plain ON press never does it, nor does CLEAR
 *     pressed later: the keys are checked once, at boot, and must be down
 *     together without a break (safemode_keys_held), or
 *   - SAFEMODE_CRASHES boots in a row did not reach SAFEMODE_OK_MS of uptime
 *     (a crash loop: the fault handler resets, and so does the watchdog when
 *     the firmware hangs; every such reset counts).
 * The boot-attempt counter lives in memory that survives a reset but not a
 * power loss (hal_boot_record: .noinit DTCM on the hardware, a static in
 * the simulators). Standby (power off) and a deliberate reset (console
 * `reset`, `reboot`) clear it, so only failed boots count.
 *
 * Leaving safe mode: switch off and on again (the counter starts from 0).
 */
#ifndef SAFEMODE_H
#define SAFEMODE_H
#include <stdbool.h>
#include <stdint.h>

#ifdef CONFIG_SAFEMODE_CRASHES     /* Kconfig: Recovery */
#define SAFEMODE_CRASHES CONFIG_SAFEMODE_CRASHES
#else
#define SAFEMODE_CRASHES 3     /* consecutive unfinished boots that trigger safe mode */
#endif
#define SAFEMODE_OK_MS 10000u  /* uptime after which a boot counts as successful */

#define SAFEMODE_KEY_MS 500u   /* ON + CLEAR held together this long at power-on */

enum { SAFEMODE_OFF = 0, SAFEMODE_KEY = 1, SAFEMODE_CRASHLOOP = 2, SAFEMODE_FORCED = 3 };

/* The safe-mode key check, once at boot: true if every key of `mask` (ON and
 * CLEAR, hal_keypad_raw() bits) is down now and stays down until they have
 * been held together for SAFEMODE_KEY_MS. The time they were already held
 * before the call counts (hal_keypad_held_ms: on hardware the scanner runs
 * from reset, and the boot takes longer than that, so normally no wait);
 * otherwise it polls `held` every 10 ms, at most SAFEMODE_KEY_MS, and gives up
 * as soon as one key is released. `held` returns the raw state (the keypad,
 * plus keys held through the console in the simulators). */
bool safemode_keys_held(uint64_t (*held)(void), uint64_t mask);
/* Called once at the start of every boot (app_init): counts the attempt and
 * decides. `keys`: the result of safemode_keys_held. `reset_cause`: why this
 * boot happened (HAL_RESET_*, hal.h), kept for MEM > About. */
void safemode_boot(bool keys, int reset_cause);
/* Why the last (re)start happened: HAL_RESET_* and as text ("watchdog"). */
int safemode_reset_cause(void);
const char *safemode_reset_text(void);
bool safemode_active(void);
int safemode_reason(void);           /* SAFEMODE_* */
const char *safemode_reason_text(void);
/* Main loop: once SAFEMODE_OK_MS have passed since boot, the boot was a
 * success and the attempt counter returns to 0. */
void safemode_tick(uint32_t now_ms);
/* A deliberate restart or power-off: clear the counter. */
void safemode_clean_shutdown(void);
/* Attempts counted so far (console `safemode`). */
uint32_t safemode_attempts(void);
/* Test hook (console `safemode force|off`): the next boot enters / skips safe mode. */
void safemode_test_force(int mode);

#endif
