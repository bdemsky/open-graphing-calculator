/* LCD bring-up test patterns (see patterns.h). */
#include "patterns.h"
#include <string.h>
#include "gfx.h"

bool pattern_active;

/* Eight vertical colour bars (white, yellow, cyan, green, magenta, red, blue,
 * black): a wrong RGB/BGR order or a missing colour bit is obvious. */
static void bars(void)
{
    static const uint16_t c[8] = {C_WHITE, RGB565(255, 255, 0), RGB565(0, 255, 255), RGB565(0, 255, 0),
                                  RGB565(255, 0, 255), RGB565(255, 0, 0), RGB565(0, 0, 255), C_BLACK};
    for (int i = 0; i < 8; i++)
        gfx_fill_rect(i * LCD_WIDTH / 8, 0, LCD_WIDTH / 8, LCD_HEIGHT, c[i]);
}

/* 10 px grid, every 5th line bright, red border on the outermost pixels:
 * shows offsets, missing rows/columns at the edges and scaling errors. */
static void grid(void)
{
    gfx_clear(C_BLACK);
    for (int x = 0; x < LCD_WIDTH; x += 10)
        gfx_vline(x, 0, LCD_HEIGHT, x % 50 ? C_DKGRAY : C_WHITE);
    for (int y = 0; y < LCD_HEIGHT; y += 10)
        gfx_hline(0, y, LCD_WIDTH, y % 50 ? C_DKGRAY : C_WHITE);
    gfx_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, RGB565(255, 0, 0));
}

/* Horizontal ramps 0..255 in red, green, blue and grey bands. RGB565 keeps 5
 * bits of red/blue and 6 of green, so the ramps show 32 or 64 steps; a stuck
 * data line shows up as a missing or repeated half of a ramp. */
static void gradient(void)
{
    int band = LCD_HEIGHT / 4;
    for (int x = 0; x < LCD_WIDTH; x++) {
        int v = x * 256 / LCD_WIDTH;
        gfx_vline(x, 0, band, RGB565(v, 0, 0));
        gfx_vline(x, band, band, RGB565(0, v, 0));
        gfx_vline(x, 2 * band, band, RGB565(0, 0, v));
        gfx_vline(x, 3 * band, LCD_HEIGHT - 3 * band, RGB565(v, v, v));
    }
}

/* 8x8 black/white checkerboard: maximum-contrast edges show smearing,
 * crosstalk and pixel misalignment. */
static void checker(void)
{
    for (int y = 0; y < LCD_HEIGHT; y += 8)
        for (int x = 0; x < LCD_WIDTH; x += 8)
            gfx_fill_rect(x, y, 8, 8, ((x ^ y) & 8) ? C_WHITE : C_BLACK);
}

/* Orientation: labelled corners, an arrow pointing up, and R/G/B squares
 * (top-left red, top-right green, bottom-left blue) to check MADCTL and RGB order. */
static void orient(void)
{
    gfx_clear(C_WHITE);
    gfx_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, C_BLACK);
    gfx_fill_rect(1, 1, 30, 30, RGB565(255, 0, 0));
    gfx_fill_rect(LCD_WIDTH - 31, 1, 30, 30, RGB565(0, 255, 0));
    gfx_fill_rect(1, LCD_HEIGHT - 31, 30, 30, RGB565(0, 0, 255));
    gfx_text(&font_medium, 36, 4, "TOP-LEFT (red)", C_BLACK, GFX_TRANSPARENT);
    gfx_text(&font_medium, LCD_WIDTH - 36 - 7 * 15, 4, "TOP-RIGHT (grn)", C_BLACK, GFX_TRANSPARENT);
    gfx_text(&font_medium, 36, LCD_HEIGHT - 18, "BOTTOM-LEFT (blue)", C_BLACK, GFX_TRANSPARENT);
    int cx = LCD_WIDTH / 2, cy = LCD_HEIGHT / 2;
    for (int i = 0; i < 40; i++)
        gfx_hline(cx - i / 2, cy - 40 + i, i + 1, C_BLACK); /* arrow head */
    gfx_fill_rect(cx - 6, cy, 13, 50, C_BLACK);
    gfx_text(&font_large, cx - 5 * 10 / 2, cy + 60, "UP", C_BLACK, GFX_TRANSPARENT);
}

static void solid(uint16_t c) { gfx_clear(c); }

const char *patterns_list(void) { return "bars grid gradient checker orient white black red green blue"; }

/* Dispatch by name; false if the name is unknown. Keep in sync with
 * patterns_list(). */
static bool draw_named(const char *name)
{
    if (!strcmp(name, "bars")) bars();
    else if (!strcmp(name, "grid")) grid();
    else if (!strcmp(name, "gradient")) gradient();
    else if (!strcmp(name, "checker")) checker();
    else if (!strcmp(name, "orient")) orient();
    else if (!strcmp(name, "white")) solid(C_WHITE);
    else if (!strcmp(name, "black")) solid(C_BLACK);
    else if (!strcmp(name, "red")) solid(RGB565(255, 0, 0));
    else if (!strcmp(name, "green")) solid(RGB565(0, 255, 0));
    else if (!strcmp(name, "blue")) solid(RGB565(0, 0, 255));
    else return false;
    return true;
}

bool pattern_draw(const char *name)
{
    /* test patterns show the true colours, also in the dark theme */
    gfx_color_map_fn saved = gfx_color_map;
    gfx_color_map = NULL;
    int base = gfx_base_clip(); /* the whole screen, also the app's soft-key bar */
    gfx_set_base_clip(LCD_HEIGHT);
    bool ok = draw_named(name);
    gfx_set_base_clip(base);
    gfx_color_map = saved;
    return ok;
}
