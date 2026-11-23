/* Main loop shared by the simulator and the firmware (see calc.h). */
#include "calc.h"
#include <stdio.h>
#include "app.h"
#include "console.h"
#include "evlog.h"
#include "gfx.h"
#include "hal.h"
#include "keyq.h"
#include "patterns.h"
#include "safemode.h"
#include "touch.h"
#include "version.h"

static volatile bool off_requested; /* set by calc_request_power_off(), or from the app's request */
static bool is_off;                 /* simulators only: dark, waiting for ON */

const char *calc_version(void)
{
    static char v[64];
    if (!v[0])
        snprintf(v, sizeof v, "%s (CAS=" CAS_IMPL ")", fw_version);
    return v;
}

void calc_request_power_off(void) { off_requested = true; }
bool calc_is_off(void) { return is_off; }

/* Hibernate. The framebuffer is flushed first so the panel shows the final
 * state; hal_power_off() saves the session (platform_hibernate_save) before
 * switching anything off. */
static void power_off(void)
{
    off_requested = false;
    con_log("power off");
    safemode_clean_shutdown(); /* a deliberate power-off is not a failed boot */
    gfx_flush();
    is_off = true;
    hal_power_off(); /* hardware: Standby, does not return (wake = reset) */
    /* simulator: stay dark until ON is pressed; Standby loses the RAM that
     * survives resets, so the event log comes back from the session */
    evlog_power_lost();
}

/* One-time start-up after hal_init(): the app gets the platform's RAM arena
 * (SDRAM on hardware, a small internal-RAM fallback if the SDRAM failed its
 * boot check) as its working memory. */
void calc_setup(void)
{
    size_t arena_size;
    void *arena = hal_arena(&arena_size);
    gfx_init(hal_framebuffer());
    console_init();
    app_init(arena, arena_size);
    gfx_flush();
    hal_watchdog_start(); /* from here a hang resets (and counts towards safe mode) */
    con_log("calc %s on %s ready", calc_version(), hal_platform_name());
}

/* One main-loop iteration. Order matters:
 *   0. the watchdog: kicked once per iteration (hal.h; in the simulators an
 *      expired model restarts the app first, as the IWDG reset would);
 *   1. all queued keys, then all queued touch events, each routed to the
 *      "off" state (only ON wakes), an open test pattern (any press / tap
 *      closes it), or the app; a power-off request stops event processing so
 *      later events are not handled by an app that is going away;
 *   2. power off if requested (on hardware this does not return);
 *   3. the console runs only when no input is pending, after a flush, so a
 *      console command (e.g. `screen`) sees the screen as of the last input;
 *   4. the app's periodic tick (key auto-repeat, cursor blink, battery
 *      status, session auto-save, auto power-down), skipped while off or
 *      while a pattern is shown;
 *   5. a final flush of whatever the tick drew.
 * Returns true if any input or console command was processed. */
bool calc_step(void)
{
    bool busy = false;
    key_event_t ev;
    if (hal_watchdog_expired()) { /* simulators: what the IWDG reset does on hardware */
        con_log("watchdog: reset");
        app_crash_restart(HAL_RESET_WATCHDOG);
    }
    hal_watchdog_kick(); /* the main loop is alive */
    hal_service(); /* deferred driver work (touch controller reads, alerts) */
    while (keyq_pop(&ev)) {
        busy = true;
        if (is_off) {
            if (ev.key == KEY_ON && ev.pressed) {
                is_off = false;
                touch_reset();
                hal_display_enable(true);
                con_log("power on");
                app_resume();
            }
            continue;
        }
        if (pattern_active) {
            if (ev.pressed) {
                pattern_active = false;
                app_redraw();
            }
            continue;
        }
        app_key_event(ev.key, ev.pressed);
        if (app_power_off_requested())
            off_requested = true;
        if (off_requested)
            break;
    }
    touch_event_t tev;
    while (!off_requested && touch_pop(&tev)) {
        busy = true;
        if (is_off)
            continue; /* only ON wakes the calculator */
        if (pattern_active) {
            if (tev.type == TEV_TAP) {
                pattern_active = false;
                app_redraw();
            }
            continue;
        }
        app_touch_event(&tev);
        if (app_power_off_requested())
            off_requested = true;
    }
    if (off_requested)
        power_off();
    gfx_flush(); /* panel is current before any console command runs */
    if (keyq_empty() && touch_queue_empty() && console_poll())
        busy = true;
    if (!is_off && !pattern_active)
        app_tick(hal_millis());
    safemode_tick(hal_millis()); /* the boot counts as successful after SAFEMODE_OK_MS */
    if (app_power_off_requested())
        off_requested = true;
    gfx_flush();
    return busy;
}

int calc_exit_status(void) { return console_expect_failures ? 1 : 0; }

/* Blocking main loop: idle (WFI on hardware) only when an iteration found
 * nothing to do, so bursts of input are drained at full speed. */
int calc_main(void)
{
    calc_setup();
    while (!console_quit) {
        if (!calc_step())
            hal_idle(); /* wakes on interrupt (hw) or input/10 ms (sim) */
    }
    return calc_exit_status();
}
