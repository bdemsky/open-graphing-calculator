/*
 * Framebuffer graphics. The framebuffer is RGB565 in SDRAM (hardware) and is
 * pushed to the LCD as dirty rectangles by gfx_flush().
 *
 * The text log records what text is on screen (position + string) so the debug
 * console can report screen contents as text for automated tests.
 *
 * Data flow: the app (and core/patterns.c) draws into a LCD_WIDTH x LCD_HEIGHT
 * (320 x 240, landscape) RGB565 buffer with the calls below. Every call clips
 * to the current clip rectangle and records the touched area in a small list
 * of dirty rectangles. The main loop (core/calc.c calc_step) calls gfx_flush()
 * after processing input and again after app_tick(); it hands each dirty
 * rectangle to hal_display_flush():
 *   - hardware: platform/stm32h743/lcd.c writes it to the ST7789 over the FMC
 *     8080 bus (CASET/RASET window + RAMWR), synchronously, by the CPU;
 *   - simulators: copied into the host-side image (PNG / browser canvas).
 * Because the hardware flush is synchronous, the framebuffer may be redrawn as
 * soon as gfx_flush() returns.
 *
 * Coordinates are pixels, origin top-left, x to the right, y down. Rectangles
 * are (x, y, w, h) with the far edges exclusive. The framebuffer stride is
 * always LCD_WIDTH pixels. Not thread/ISR-safe: main context only.
 */
#ifndef GFX_H
#define GFX_H
#include <stdbool.h>
#include <stdint.h>
#include "font.h"
#include "hal.h"

/* Pack 8-bit-per-channel r, g, b (0..255) into RGB565: rrrrrggg gggbbbbb.
 * Usable in constant expressions. */
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
/* Text background value meaning "leave background pixels untouched". Text
 * backgrounds are passed as int so that this sentinel cannot collide with any
 * RGB565 colour (0..0xFFFF). */
#define GFX_TRANSPARENT (-1)

/* Palette */
#define C_BLACK   RGB565(0, 0, 0)
#define C_WHITE   RGB565(255, 255, 255)
#define C_GRAY    RGB565(128, 128, 128)
#define C_LTGRAY  RGB565(220, 220, 220)
#define C_DKGRAY  RGB565(64, 64, 64)
#define C_BLUE    RGB565(0, 80, 220)
#define C_RED     RGB565(220, 30, 30)
#define C_GREEN   RGB565(0, 150, 60)
#define C_MAGENTA RGB565(200, 0, 160)
#define C_ORANGE  RGB565(240, 130, 0)
#define C_BROWN   RGB565(140, 80, 30)
#define C_NAVY    RGB565(0, 0, 128)
#define C_LTBLUE  RGB565(100, 170, 255)
#define C_YELLOW  RGB565(230, 200, 0)
#define C_AMBER   RGB565(240, 160, 48)  /* SHIFT (keycap colour) */
#define C_TEAL    RGB565(43, 179, 168)  /* ALPHA (keycap colour) */
#define C_STATUS_BG RGB565(40, 44, 52)
#define C_SOFT_BG RGB565(70, 76, 88) /* soft-key labels (kept in dark mode, like the status bar) */
#define C_SEL_BG  RGB565(0, 90, 200)
#define C_GRID    RGB565(200, 200, 200)
#define C_AXES    RGB565(40, 40, 40)

/* Colour theme hook (installed by app/theme.c for dark mode; NULL = identity).
 * Every drawing call passes its colour through it, so screens draw with the
 * light palette above and need no theme code. Roles: FILL for rectangles,
 * lines, pixels and opaque text backgrounds; INK for text, with `under` = the
 * (mapped) background the text is drawn on (the opaque background, or the
 * framebuffer pixel at the text's top-left corner). Code that writes the
 * framebuffer directly maps its colours with gfx_map(). */
enum { GFX_ROLE_FILL = 0, GFX_ROLE_INK = 1 };
typedef uint16_t (*gfx_color_map_fn)(uint16_t c, int role, uint16_t under);
extern gfx_color_map_fn gfx_color_map;
uint16_t gfx_map(uint16_t c); /* FILL mapping of one colour */

/* Attach the framebuffer (LCD_WIDTH * LCD_HEIGHT pixels, provided by the
 * platform: SDRAM at 0xD0000000 on hardware), reset the clip to the base clip,
 * empty the text log and mark the whole screen dirty. Does not clear pixels. */
void gfx_init(uint16_t *fb);
uint16_t *gfx_framebuffer(void);
/* Draw into buf (LCD_WIDTH x LCD_HEIGHT pixels, the framebuffer's layout) instead of the screen
 * (offscreen images: notebook graph cells); NULL: the screen again. Nothing is marked dirty
 * and the text log is left alone meanwhile. */
void gfx_target(uint16_t *buf);

/* Clipping (all drawing honours it). The clip is intersected with the screen;
 * it is not intersected with the previous clip, so nested clips must be
 * computed by the caller. */
void gfx_set_clip(int x, int y, int w, int h);
void gfx_reset_clip(void);   /* to the base clip (default: the whole screen) */
void gfx_get_clip(int r[4]); /* the clip now: x0, y0, x1, y1 (far edges exclusive) */
/* Base clip: gfx_reset_clip() restores [0, y1) (the app keeps the soft-key bar
 * below y1 out of the screens' reach); gfx_set_clip() may still go below. */
void gfx_set_base_clip(int y1);
int gfx_base_clip(void);

/* Primitives. Colours are given in the light palette and pass through
 * gfx_color_map (FILL role) before being stored. */
void gfx_pixel(int x, int y, uint16_t c);
/* Raw (already mapped) framebuffer pixel, ignoring the clip; 0 off-screen. */
uint16_t gfx_get_pixel(int x, int y);
/* Solid rectangle. Also drops text-log runs that overlap the filled area,
 * since that text has been painted over. */
void gfx_fill_rect(int x, int y, int w, int h, uint16_t c);
void gfx_rect(int x, int y, int w, int h, uint16_t c); /* outline */
void gfx_hline(int x, int y, int w, uint16_t c);
void gfx_vline(int x, int y, int h, uint16_t c);
/* Line from (x0, y0) to (x1, y1), both endpoints inclusive (Bresenham). */
void gfx_line(int x0, int y0, int x1, int y1, uint16_t c);
/* Fill the whole screen (ignoring the clip, including the area below the base
 * clip), empty the text log, and restore the base clip. */
void gfx_clear(uint16_t c);
/* Copy a rectangle from/to a full-screen-sized buffer (stride LCD_WIDTH; the
 * rectangle sits at the same (x, y) in both). The graph screen uses this to
 * cache the plotted curves and restore them under moving overlays (trace
 * cursor). gfx_save_rect clips to the base clip; gfx_restore_rect clips to the
 * current clip and the base clip (so it never writes pixels that were not
 * saved), marks the area dirty, and does not touch the text log. */
void gfx_save_rect(uint16_t *dst, int x, int y, int w, int h);
void gfx_restore_rect(const uint16_t *src, int x, int y, int w, int h);

/* Text. bg = GFX_TRANSPARENT or an RGB565 colour. Returns width in pixels.
 * (x, y) is the top-left corner of the first character cell; each codepoint
 * advances by f->w. No wrapping: text past the clip is simply clipped.
 * gfx_text records the string in the text log; gfx_char does not. */
int gfx_text(const font_t *f, int x, int y, const char *utf8, uint16_t fg, int bg);
int gfx_char(const font_t *f, int x, int y, uint32_t cp, uint16_t fg, int bg);
/* Width in pixels of a UTF-8 string (codepoints * f->w). */
int gfx_text_width(const font_t *f, const char *utf8);

/* Dirty rectangles. Drawing calls mark automatically. Up to 16 rectangles are
 * kept; touching rectangles are merged when that adds little area, and once the
 * list is full a new rectangle is merged into the one that grows least. */
void gfx_mark_dirty(int x, int y, int w, int h);
/* Called with every rectangle marked dirty (clamped to the screen; NULL: none). The standalone
 * Sparkle Town page (platform/web/web_sparkle.c) copies what the core draws into its
 * high-resolution image this way. */
extern void (*gfx_dirty_hook)(int x, int y, int w, int h);
/* Called by gfx_text before it draws (colours mapped); true: the hook shows the text itself, so
 * its rectangle is not reported to gfx_dirty_hook (the pixels are still drawn). */
extern bool (*gfx_text_hook)(const font_t *f, int x, int y, const char *s, uint16_t fg, int bg);
void gfx_invalidate(void);  /* whole screen */
/* Push dirty rectangles to the display (hal_display_flush, blocking) and clear
 * the list; returns number of pixels sent. */
int gfx_flush(void);

/* ---- text log (what text is visible where) ----
 * A list of text runs, kept sorted by (y, x). A new run replaces every run it
 * overlaps, and gfx_fill_rect / gfx_clear remove runs they paint over, so the
 * log approximates the text currently visible. It is what the console `text`
 * and `expect text` commands inspect. Runs are truncated to
 * GFX_TEXTLOG_LEN - 1 bytes; when the log is full, the oldest run is dropped. */
#define GFX_TEXTLOG_MAX 192
#define GFX_TEXTLOG_LEN 64
typedef struct {
    int16_t x, y, w, h;
    uint32_t seq; /* insertion order (eviction of the oldest run) */
    char text[GFX_TEXTLOG_LEN];
} gfx_textrun_t;
void gfx_textlog_enable(bool on); /* default on; off for transient overlays (cursor) */
int gfx_textlog_count(void);
const gfx_textrun_t *gfx_textlog_get(int i); /* sorted by y, then x; NULL if out of range */
/* true if any single run contains `needle` as a substring (runs are not
 * joined, so a needle spanning two runs is not found). */
bool gfx_textlog_contains(const char *needle);
/* Record text drawn by other means (2D math drawn piecewise with the log
 * disabled): its linear form at the rectangle it occupies. */
void gfx_textlog_note(int x, int y, int w, int h, const char *s);

/* Stats for the console (`fbinfo`): cumulative since boot. flushes counts
 * gfx_flush calls that sent anything, rects the rectangles sent, pixels the
 * pixels sent. */
typedef struct { uint32_t flushes, pixels, rects; } gfx_stats_t;
void gfx_get_stats(gfx_stats_t *s);

#endif
