/* Portable entry point shared by the simulator and the firmware.
 *
 * Data flow of one main-loop iteration (calc_step): driver work deferred by
 * the platform (hal_service), key events from core/keyq, touch events from
 * core/touch, then the debug console (core/console), the app's periodic tick
 * and a framebuffer flush to the panel (gfx_flush). Everything above the HAL
 * (include/hal.h) is the same code on the board, the desktop simulator and
 * the WebAssembly build. */
#ifndef CALC_H
#define CALC_H
#include <stdbool.h>
#include <stdint.h>

/* Run the calculator: init gfx/console/app, then the main loop.
 * Returns only when the console requests exit (simulator `quit` / EOF).
 * The platform's main() calls hal_init() first, then calc_main(). */
int calc_main(void);

/* The same loop in pieces, for hosts that cannot block (the WebAssembly build):
 * calc_setup() once, then calc_step() repeatedly; calc_step() runs one main-loop
 * iteration and returns true if it did work (call it again before idling). */
void calc_setup(void);
bool calc_step(void);
/* Process exit code: 1 if a console `expect` check failed, else 0. */
int calc_exit_status(void);

/* Version string, e.g. "0.2.1 (CAS=src)" or "0.2.1+3-gabc1234 (CAS=src)" (core/version.h). */
const char *calc_version(void);

/* Ask the main loop to power off (hibernate). On hardware this does not return
 * (wake = reset); in the simulator the screen goes dark until ON is pressed. */
void calc_request_power_off(void);
bool calc_is_off(void); /* simulator: the screen is dark, waiting for ON */

#endif
