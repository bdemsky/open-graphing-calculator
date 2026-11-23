/*
 * Colour themes (MODE > THEME: Light / Dark) and the large-font mode (MODE >
 * FONT: Normal / Large). See app/EXTENDING.md, "Themes and fonts".
 *
 * Screens draw with the light palette. The TH_* tokens below name the roles
 * of the palette colours; they ARE the light values, so a screen written with
 * them (or with the plain C_* colours) looks exactly as before in Light mode.
 * Dark mode is a colour map installed in the graphics layer (gfx_color_map,
 * core/gfx.h): every fill, line, pixel and text colour passes through it, so
 * every screen, including ones written before themes existed, is themed
 * without code of its own. Text ink is chosen against what is under it (white
 * text on a selection stays white, black text on a yellow badge stays black).
 *
 * Both settings are MODE rows (mode_theme, mode_font) saved in the "theme"
 * persist blob (app/theme.c). Code that writes the framebuffer directly
 * bypasses the map and must call gfx_map(c) itself.
 */
#ifndef THEME_H
#define THEME_H
#include <stdbool.h>
#include "ui.h"

/* ---- role tokens (light values; mapped at draw time in dark mode) ---- */
#define TH_BG        C_WHITE     /* screen / body background */
#define TH_FG        C_BLACK     /* normal text and lines */
#define TH_DIM       C_GRAY      /* secondary text: hints, live preview, scroll arrows */
#define TH_LABEL     C_DKGRAY    /* labels (MODE row names) */
#define TH_PANEL     C_LTGRAY    /* title bars, panels, message boxes */
#define TH_SEL_BG    C_SEL_BG    /* selection highlight (kept in dark mode) */
#define TH_SEL_FG    C_WHITE     /* text on the selection */
#define TH_ERR       C_RED       /* errors */
#define TH_FOCUS     C_ORANGE    /* focus frames */
#define TH_LINK      C_BLUE      /* links, accents */
#define TH_GRID      C_GRID
#define TH_AXES      C_AXES
#define TH_STATUS_BG C_STATUS_BG /* status bar (kept in dark mode) */

enum { THEME_LIGHT = 0, THEME_DARK = 1 };
int theme_get(void);
void theme_set(int theme); /* installs/removes the colour map; redraw afterwards */
bool theme_is_dark(void);
/* The dark map itself (exposed for unit tests and code that maps by hand).
 * role is GFX_ROLE_FILL or GFX_ROLE_INK (core/gfx.h); `under` is the pixel
 * colour below text ink, used to keep text readable on light badges. */
uint16_t theme_dark_map(uint16_t c, int role, uint16_t under);

/* ---- fonts ----
 * ui_font is FONT (10x20) normally and font_xlarge (14x28) in large-font
 * mode. Screens that can lay out a bigger font use UI_FONT / UI_CW / UI_CH
 * instead of FONT / CW / CH (home, menus); the others keep FONT. */
extern const font_t *ui_font;
#define UI_FONT (ui_font)
#define UI_CW ((int)ui_font->w)
#define UI_CH ((int)ui_font->h)
bool theme_large(void);
void theme_set_large(bool on); /* switch ui_font; screens re-layout on their next draw */
/* 2D math: font for nesting level 0 (main), 1 (scripts), 2+ (scripts of scripts) */
const font_t *theme_math_font(int level);

#endif
