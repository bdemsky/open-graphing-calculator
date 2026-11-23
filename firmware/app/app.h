/*
 * Calculator application: the public interface between the portable main loop
 * (core/calc.c), the debug console and the application layer in app/.
 *
 * The main loop owns the hardware/simulator event queues and calls in here:
 *   - app_init() once at boot with the memory arena (SDRAM on hardware) that
 *     the app carves into its work areas (see app.c) and the CAS heap;
 *   - app_key_event() for every key press/release popped from core/keyq.c;
 *   - app_touch_event() for every gesture recognised by core/touch.c;
 *   - app_tick() on every pass of the loop (cursor blink, key repeat, idle
 *     autosave, auto power-off, per-screen animations);
 *   - app_power_off_requested() after each event to learn whether the app
 *     wants to switch off (SHIFT ON, or five minutes without input).
 * Everything runs in the main-loop (thread) context; the only code the app
 * runs from an interrupt is the ON-key hook installed with keyq_set_on_hook()
 * (it interrupts a long CAS evaluation).
 */
#ifndef APP_H
#define APP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cas.h"
#include "touch.h"

/* Start (or restart) the application: carve `arena` (size bytes, not freed,
 * owned by the app from now on) into the session buffer, help workspace, RPN
 * stack, statistics lists, graph cache and CAS heap, restore the saved
 * session and show the home screen. */
void app_init(void *arena, size_t size);
/* A key went down (pressed) or up. Presses run the key's action at once;
 * releases only end the auto-repeat of arrows and backspace. */
void app_key_event(int key, bool pressed);
/* The key held down now (0: none) and, in *ms, for how long (code modules: hold gates). */
int app_held_key(uint32_t *ms);
void app_touch_event(const touch_event_t *ev); /* gestures from core/touch.c */
void app_tick(uint32_t now_ms); /* every main-loop pass; now_ms = hal_millis() */
void app_redraw(void);              /* full redraw of the current screen */
const char *app_screen_name(void);  /* "home", "mode", "yeq", "window", "graph", ... */
uint32_t app_last_activity(void);    /* the last key or touch (ms) */
cas_ctx *app_cas(void);
/* Called by the main loop after a simulated/real power cycle. */
void app_resume(void);
/* The same with another reset cause (HAL_RESET_*, hal.h), e.g. the console
 * `reboot` (HAL_RESET_SOFTWARE); not a failed boot. */
void app_restart(int cause);
/* Simulators: restart as after a crash or a watchdog reset (cause
 * HAL_RESET_FAULT / HAL_RESET_WATCHDOG; counts towards safe mode,
 * core/safemode.h). */
void app_crash_restart(int cause);
/* The CAS poll hook the app installs (cas_set_poll): kicks the watchdog
 * during long evaluations and never interrupts. Helpers that install their
 * own time box restore this one afterwards (not NULL), and kick themselves. */
int app_cas_poll(void *user);
/* inside app_cas_poll: a long CAS evaluation (or Python) is below on the stack (the USB stack runs
 * from there): no module may be bound or called (app/usb_app.c, the notebooks folder) */
bool app_in_cas_poll(void);
/* true when the app asked to power off (SHIFT ON / auto power down). */
bool app_power_off_requested(void);

/* Introspection for the debug console / tests: key=value pairs. */
typedef void (*app_kv_fn)(const char *key, const char *value, void *user);
void app_state_foreach(app_kv_fn fn, void *user);

#endif
