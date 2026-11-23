/* Colour themes and the large-font mode (see theme.h).
 *
 * Owns two MODE rows (THEME, FONT, registered in app/registry.c) and the
 * "theme" persist blob. The dark theme is a pure function from light-palette
 * RGB565 colours to dark ones, installed as gfx_color_map; no screen has
 * dark-mode code. The large-font mode only swaps the ui_font pointer that
 * UI_FONT/UI_CW/UI_CH read. State: cur_theme, large, ui_font (statics). */
#include "theme.h"
#include "registry.h"
#include "session.h"

static int cur_theme = THEME_LIGHT;
static bool large;
const font_t *ui_font = &font_large;

/* ------------------------------------------------------------ dark map */
#define DARK_BG 18   /* grey level of the dark background */
#define DARK_FG 232  /* grey level of normal text */

/* RGB565 -> 8-bit components (0..255). */
static void unpack(uint16_t c, int *r, int *gr, int *b)
{
    *r = ((c >> 11) & 31) * 255 / 31;
    *gr = ((c >> 5) & 63) * 255 / 63;
    *b = (c & 31) * 255 / 31;
}

/* Approximate luma 0..255 with integer weights 2:5:1 / 8 (close to the
 * Rec. 601 weights 0.299, 0.587, 0.114). */
static int lum(uint16_t c)
{
    int r, gr, b;
    unpack(c, &r, &gr, &b);
    return (2 * r + 5 * gr + b) / 8;
}

/* Dark-mode mapping of a fill/line/pixel colour:
 *   - the status bar, selection and soft-key bar colours are kept;
 *   - greys (max - min channel < 40) have their lightness inverted into the
 *     range DARK_BG..DARK_FG (white -> near-black background, black -> light
 *     grey), dark results get a slight blue tint (+4 on blue);
 *   - saturated dark colours (luma < 120) move 40 % towards white with
 *     their hue kept, so blue/red curves stay readable on black;
 *   - light saturated colours are unchanged.
 * A one-entry memo (last input/output) makes runs of the same colour cheap;
 * gfx calls this for every primitive. Main-loop only (static memo). */
static uint16_t dark_fill(uint16_t c)
{
    static uint16_t last_in = 0xFFFF, last_out = RGB565(DARK_BG, DARK_BG, DARK_BG);
    if (c == last_in)
        return last_out;
    uint16_t out = c;
    if (c != C_STATUS_BG && c != C_SEL_BG && c != C_SOFT_BG) {
        int r, gr, b;
        unpack(c, &r, &gr, &b);
        int mx = r > gr ? (r > b ? r : b) : (gr > b ? gr : b);
        int mn = r < gr ? (r < b ? r : b) : (gr < b ? gr : b);
        if (mx - mn < 40) { /* grey: invert the lightness */
            int v = (r + gr + b) / 3;
            int nv = DARK_BG + (255 - v) * (DARK_FG - DARK_BG) / 255;
            out = RGB565(nv, nv, nv + (nv < 128 ? 4 : 0));
        } else if (lum(c) < 120) { /* dark colour: lighten, keep the hue */
            out = RGB565(r + (255 - r) * 2 / 5, gr + (255 - gr) * 2 / 5, b + (255 - b) * 2 / 5);
        }
    }
    last_in = c;
    last_out = out;
    return out;
}

/* gfx_color_map in dark mode. role GFX_ROLE_FILL: dark_fill(). Text ink
 * (GFX_ROLE_INK) is chosen against the colour `under` it: on a light
 * background (a badge or highlight that stayed light) dark ink is kept (light
 * ink becomes the dark background grey); white ink is kept (text on the
 * selection); other ink is mapped like a fill, and forced to the light text
 * grey if it would still be too dark to read. */
uint16_t theme_dark_map(uint16_t c, int role, uint16_t under)
{
    if (role != GFX_ROLE_INK)
        return dark_fill(c);
    if (lum(under) >= 140) /* light background under the text (badge, highlight): keep dark ink */
        return lum(c) < 140 ? c : RGB565(DARK_BG, DARK_BG, DARK_BG);
    if (c == C_WHITE)
        return c;
    uint16_t m = dark_fill(c);
    return lum(m) < 100 ? RGB565(DARK_FG, DARK_FG, DARK_FG) : m;
}

int theme_get(void) { return cur_theme; }
bool theme_is_dark(void) { return cur_theme == THEME_DARK; }

/* Select the theme and invalidate everything drawn with the old one (the
 * graph cache and the display); the caller redraws the screen. */
void theme_set(int t)
{
    cur_theme = t == THEME_DARK ? THEME_DARK : THEME_LIGHT;
    gfx_color_map = cur_theme == THEME_DARK ? theme_dark_map : NULL;
    g.graph_valid = false; /* the graph cache holds mapped pixels */
    gfx_invalidate();
}

/* ------------------------------------------------------------ fonts */
bool theme_large(void) { return large; }

void theme_set_large(bool on)
{
    large = on;
    ui_font = on ? &font_xlarge : &font_large;
}

/* 2D math fonts by nesting level (clamped to 0..2): normal 10x20 / 7x14 /
 * 6x13, large mode one size up (14x28 / 10x20 / 7x14). */
const font_t *theme_math_font(int level)
{
    static const font_t *const normal[3] = {&font_large, &font_medium, &font_small};
    static const font_t *const big[3] = {&font_xlarge, &font_large, &font_medium};
    if (level < 0)
        level = 0;
    if (level > 2)
        level = 2;
    return large ? big[level] : normal[level];
}

/* ------------------------------------------------------------ MODE rows */
static int theme_row_get(void) { return cur_theme; }
static void theme_row_set(int c) { theme_set(c); } /* the MODE screen redraws everything */
const mode_row_t mode_theme = {"THEME", {"LIGHT", "DARK"}, 2, theme_row_get, theme_row_set, "mode-theme"};

static int font_row_get(void) { return large; }
static void font_row_set(int c) { theme_set_large(c != 0); }
const mode_row_t mode_font = {"FONT", {"NORMAL", "LARGE"}, 2, font_row_get, font_row_set, "mode-font"};

/* ------------------------------------------------------------ persistence */
/* "theme" blob, version 1: u8 theme, u8 large font. */
static void theme_save(pw_t *w)
{
    pw_u8(w, (uint32_t)cur_theme);
    pw_u8(w, large);
}

static void theme_load(pr_t *r, unsigned version)
{
    unsigned t = pr_u8(r), l = pr_u8(r);
    if (r->err)
        return;
    theme_set(t == THEME_DARK ? THEME_DARK : THEME_LIGHT);
    theme_set_large(l != 0);
}

static void theme_reset(void)
{
    theme_set(THEME_LIGHT);
    theme_set_large(false);
}

const persist_def_t persist_theme = {"theme", 1, theme_save, theme_load, theme_reset};
