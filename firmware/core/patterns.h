/* LCD test patterns for bring-up (drawn into the framebuffer).
 *
 * Shown by the console `pattern NAME` command (core/console.c), which sets
 * pattern_active; the main loop (core/calc.c) then routes the next key press
 * or touch tap to closing the pattern and redrawing the app instead of the
 * app itself. The patterns check the ST7789 panel: colour order and gamma
 * (bars, gradient), geometry and scan direction/MADCTL (grid, orient),
 * pixel-level timing artefacts (checker), and dead pixels (solid colours). */
#ifndef PATTERNS_H
#define PATTERNS_H
#include <stdbool.h>

extern bool pattern_active; /* a pattern is shown; the next key restores the app */
/* Draw pattern `name` over the whole screen (soft-key bar included) with the
 * theme colour map bypassed. Returns false for an unknown name (nothing drawn).
 * Does not set pattern_active; the caller does. */
bool pattern_draw(const char *name);
/* Space-separated list of the pattern names, for console help. */
const char *patterns_list(void);
/* Defined in core/console.c: true while a console `wait MS` command is
 * pending (its OK has not been printed yet). */
bool console_waiting(void);

#endif
