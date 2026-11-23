/* MODE screen: rows from the registry (registry.c: mode_registry), scrolling,
 * plus the built-in rows and the saved settings blob.
 *
 * The settings screen (SHIFT HOME) is data-driven: each mode_row_t
 * (app/registry.h) has a label, up to 12 option names and get()/set()
 * callbacks, so features add rows without touching this file. UP/DOWN pick a
 * row (the focus column jumps to the row's current option), LEFT/RIGHT move
 * the orange focus box and apply the option at once (so leaving the screen
 * never drops a choice that looked made; ENTER applies it too); a tap on an
 * option applies it at once. The current option of each row is drawn highlighted. After every
 * change app_apply_settings() pushes the settings into the CAS and the
 * editor. Soft keys: about, help (the highlighted row's topic, through
 * mode_current_row()), done.
 *
 * The built-in rows below edit fields of the global app state `g` (CAS
 * settings g.cs, number format, graph grid/axes) and the backlight; they are
 * saved by the "settings" persist blob at the end of this file. Rows added by
 * other features save their own values. Screen state (row, col, top) is
 * static and kept between visits. */
#include <stdio.h>
#include <string.h>
#include "hal.h"
#include "registry.h"
#include "ui.h"

/* ------------------------------------------------------------ built-in rows */
static int angle_get(void) { return g.cs.angle == CAS_ANGLE_DEG; }
static void angle_set(int c) { g.cs.angle = c ? CAS_ANGLE_DEG : CAS_ANGLE_RAD; }
const mode_row_t mode_angle = {"ANGLE", {"RADIAN", "DEGREE"}, 2, angle_get, angle_set, "mode-angle"};

static int result_get(void) { return !g.cs.exact; }
static void result_set(int c) { g.cs.exact = !c; }
const mode_row_t mode_result = {"RESULT", {"EXACT", "APPROX"}, 2, result_get, result_set, "mode-result"};

/* DIGITS row: significant digits of approximate results (g.cs.digits). A
 * stored value between the options shows as the next larger option. */
static const int digit_vals[] = {6, 8, 10, 12, 15};
static int digits_get(void)
{
    for (int i = 0; i < 5; i++)
        if (g.cs.digits <= digit_vals[i])
            return i;
    return 4;
}
static void digits_set(int c) { g.cs.digits = digit_vals[c]; }
const mode_row_t mode_digits = {"DIGITS", {"6", "8", "10", "12", "15"}, 5, digits_get, digits_set, "mode-digits"};

static int format_get(void) { return g.fmt; }
static void format_set(int c) { g.fmt = (uint8_t)c; }
const mode_row_t mode_format = {"FORMAT", {"NORMAL", "SCI", "ENG"}, 3, format_get, format_set, "mode-format"};

/* DECIMAL row: option 0 = FLOAT (g.fix = -1), option k = FIX k-1. */
static int decimals_get(void) { return g.fix + 1; }
static void decimals_set(int c) { g.fix = (int8_t)(c - 1); }
const mode_row_t mode_decimals = {
    "DECIMAL", {"FLOAT", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"}, 11, decimals_get, decimals_set,
    "mode-format"};

static int complex_get(void) { return g.cplx; }
static void complex_set(int c) { g.cplx = (uint8_t)c; }
const mode_row_t mode_complex = {"COMPLEX", {"REAL", "a+bi", "r∠θ"}, 3, complex_get, complex_set, "mode-complex"};

/* GRID / AXES change the graph picture: invalidate the graph cache. */
static int grid_get(void) { return g.grid; }
static void grid_set(int c)
{
    g.grid = c;
    g.graph_valid = false;
}
const mode_row_t mode_grid = {"GRID", {"OFF", "ON"}, 2, grid_get, grid_set, "mode-graph"};

static int axes_get(void) { return !g.axes; }
static void axes_set(int c)
{
    g.axes = !c;
    g.graph_valid = false;
}
const mode_row_t mode_axes = {"AXES", {"ON", "OFF"}, 2, axes_get, axes_set, "mode-graph"};

/* LIGHT row: backlight 20..100 %. SHIFT ▲/▼ change the backlight in 10 %
 * steps, so get() shows the option the current level is nearest to (rounding
 * halves up). */
static int light_get(void)
{
    int b = hal_backlight_get(), best = 0;
    for (int i = 0; i < 5; i++)
        if (b >= (i + 1) * 20 - 10)
            best = i;
    return best;
}
static void light_set(int c)
{
    g.backlight = (c + 1) * 20;
    hal_backlight_set(g.backlight);
}
const mode_row_t mode_light = {"LIGHT", {"20", "40", "60", "80", "100"}, 5, light_get, light_set, "mode-light"};

/* On USB power the idle auto power-off only darkens the screen (STAY ON): the CPU, the
 * USB link, the browser page and the drive keep running; SLEEP switches off as on
 * batteries (app.c app_tick). */
static int usbpwr_get(void) { return g.usb_awake ? 0 : 1; }
static void usbpwr_set(int c) { g.usb_awake = c == 0; }
const mode_row_t mode_usbpwr = {"ON USB", {"STAY ON", "SLEEP"}, 2, usbpwr_get, usbpwr_set, "mode-usb"};

/* ------------------------------------------------------------ screen */
/* Layout: one row per setting, ROW_H px high; label column LABEL_W px wide in
 * font_medium, options after it in the large font. NVIS rows fit in the body
 * (8); `top` is the first visible row, adjusted in mode_draw() so `row` (the
 * focused row) is visible; `col` is the focused option of that row. */
#define ROW_H (CH + 4)
#define LABEL_W 64
#define NVIS (BODY_H / ROW_H)
static int row, col, top;

static int nrows(void) { return mode_registry_count; }

/* option x positions for a row; the spacing shrinks so every option fits.
 * The gap is (available width - total text width) / (options - 1), clamped to
 * 4..14 px; with 11 options (DECIMAL) the minimum gap is used. Returns the
 * gap (used for the focus box margin and the touch hit areas). */
static int layout(const mode_row_t *r, int *xs)
{
    int total = 0;
    for (int c = 0; c < r->nopts; c++)
        total += gfx_text_width(FONT, r->opts[c]);
    int avail = LCD_WIDTH - LABEL_W - 6;
    int gap = r->nopts > 1 ? (avail - total) / (r->nopts - 1) : 0;
    if (gap > 14)
        gap = 14;
    if (gap < 4)
        gap = 4;
    int x = LABEL_W;
    for (int c = 0; c < r->nopts; c++) {
        xs[c] = x;
        x += gfx_text_width(FONT, r->opts[c]) + gap;
    }
    return gap;
}

/* Draw the visible rows (scrolling `top` first), the selected options, the
 * focus box and ↑/↓ marks when rows are hidden above/below. */
static void mode_draw(void)
{
    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, C_WHITE);
    if (row < top)
        top = row;
    if (row >= top + NVIS)
        top = row - NVIS + 1;
    for (int v = 0; v < NVIS && top + v < nrows(); v++) {
        int ri = top + v;
        const mode_row_t *r = mode_registry[ri];
        int y = BODY_Y + 3 + v * ROW_H;
        gfx_text(&font_medium, 4, y + 3, r->label, C_DKGRAY, GFX_TRANSPARENT);
        int xs[12];
        int gap = layout(r, xs);
        int cur = r->get();
        for (int c = 0; c < r->nopts; c++) {
            const char *o = r->opts[c];
            int w = gfx_text_width(FONT, o);
            if (c == cur) {
                gfx_fill_rect(xs[c] - 2, y, w + 4, CH, C_SEL_BG);
                gfx_text(FONT, xs[c], y, o, C_WHITE, GFX_TRANSPARENT);
            } else {
                gfx_text(FONT, xs[c], y, o, C_BLACK, GFX_TRANSPARENT);
            }
            if (ri == row && c == col) {
                int m = gap < 8 ? gap / 2 : 4;
                gfx_rect(xs[c] - m, y - 2, w + 2 * m, CH + 4, C_ORANGE);
            }
        }
    }
    if (top > 0)
        gfx_text(&font_small, LCD_WIDTH - 8, BODY_Y + 2, "↑", C_GRAY, GFX_TRANSPARENT);
    if (top + NVIS < nrows())
        gfx_text(&font_small, LCD_WIDTH - 8, UI_BOTTOM - 13, "↓", C_GRAY, GFX_TRANSPARENT);
}

/* Set option c of row r and apply all settings (CAS, editor angle marks...). */
static void apply(int r, int c)
{
    mode_registry[r]->set(c);
    app_apply_settings();
}

static void mode_key(int key, int mod)
{
    const mode_row_t *r = mode_registry[row];
    switch (key) {
    case KEY_UP:
        row = (row + nrows() - 1) % nrows();
        col = mode_registry[row]->get();
        break;
    case KEY_DOWN:
        row = (row + 1) % nrows();
        col = mode_registry[row]->get();
        break;
    case KEY_LEFT:
    case KEY_RIGHT:
        /* the choice takes effect at once (like a tap): leaving with done,
         * HOME or CLEAR never loses it */
        col = (col + (key == KEY_LEFT ? r->nopts - 1 : 1)) % r->nopts;
        apply(row, col);
        ui_draw_status();
        break;
    case KEY_ENTER:
        apply(row, col);
        ui_draw_status();
        break;
    case KEY_CLEAR:
        ui_switch(ui_home());
        return;
    default:
        return;
    }
    mode_draw();
}

/* Keep the last row between visits (tests use `setmode` instead of counting
 * DOWN presses); the focus starts on the row's current option. */
static void mode_enter(void)
{
    if (row >= nrows())
        row = 0;
    col = mode_registry[row]->get();
}

/* Touch: tap an option to select and apply it; drag up/down scrolls the rows
 * (as in menus), keeping the focused row on screen. */
static void mode_touch(const touch_event_t *ev)
{
    if (ev->type == TEV_DRAG) {
        int r = row;
        if (ui_drag_list(ev, ROW_H, &top, nrows(), NVIS, &row)) {
            if (row != r)
                col = mode_registry[row]->get();
            mode_draw();
        }
        return;
    }
    if (ev->type != TEV_TAP)
        return;
    for (int v = 0; v < NVIS && top + v < nrows(); v++) {
        const mode_row_t *r = mode_registry[top + v];
        int y = BODY_Y + 3 + v * ROW_H;
        if (ev->y < y - 2 || ev->y >= y + CH + 2)
            continue;
        int xs[12];
        int gap = layout(r, xs);
        for (int c = 0; c < r->nopts; c++) {
            int w = gfx_text_width(FONT, r->opts[c]);
            if (ev->x >= xs[c] - gap / 2 - 1 && ev->x < xs[c] + w + gap / 2 + 1) {
                row = top + v, col = c;
                apply(row, c);
                ui_draw_status();
                mode_draw();
                return;
            }
        }
    }
}

/* The focused row (help.c uses its help topic for SHIFT HELP). */
const mode_row_t *mode_current_row(void) { return row < nrows() ? mode_registry[row] : NULL; }

/* Open SETTINGS with the row labelled `label` focused (the graph screens'
 * format key opens it at GRID); an unknown label keeps the last row. */
void mode_open_at(const char *label)
{
    for (int i = 0; i < nrows(); i++)
        if (!strcmp(mode_registry[i]->label, label))
            row = i;
    ui_switch(&scr_mode);
}

static void sk_done(void) { ui_switch(ui_home()); }
static const softkeys_t mode_soft = {{{"about", app_about}, {NULL}, {NULL}, {"help", help_context}, {"done", sk_done}}};
const screen_t scr_mode = {"mode", "SETTINGS", mode_enter, mode_draw, mode_key, NULL, NULL, mode_touch, "mode",
                           &mode_soft};

/* ------------------------------------------------------------ persistence */
/* "settings" blob: ten bytes in this order: angle, exact, digits, fmt, fix
 * (signed), cplx, grid, axes, backlight %, insert mode; version 2 adds ON USB
 * (1 = stay on). A version 1 blob has no ON USB byte: STAY ON. (The session pads
 * every blob to a multiple of 4 bytes with zeros, so a version 1 blob does have an
 * eleventh byte, 0: a field added at the end needs a new version, not a check for
 * the end of the blob.) */
static void settings_save(pw_t *w)
{
    pw_u8(w, g.cs.angle);
    pw_u8(w, (uint32_t)g.cs.exact);
    pw_u8(w, (uint32_t)g.cs.digits);
    pw_u8(w, g.fmt);
    pw_u8(w, (uint32_t)(int32_t)g.fix);
    pw_u8(w, g.cplx);
    pw_u8(w, g.grid);
    pw_u8(w, g.axes);
    pw_u8(w, (uint32_t)g.backlight);
    pw_u8(w, ed_insert_mode);
    pw_u8(w, g.usb_awake);
}

/* Restore the settings; a short blob is ignored entirely, out-of-range values
 * fall back to defaults field by field. */
static void settings_load(pr_t *r, unsigned version)
{
    unsigned angle = pr_u8(r), exact = pr_u8(r), digits = pr_u8(r), fmt = pr_u8(r);
    int fix = (int8_t)pr_u8(r);
    unsigned cplx = pr_u8(r), grid = pr_u8(r), axes = pr_u8(r), bl = pr_u8(r), ins = pr_u8(r);
    if (r->err)
        return;
    g.cs.angle = angle ? CAS_ANGLE_DEG : CAS_ANGLE_RAD;
    g.cs.exact = exact != 0;
    if (digits >= 1 && digits <= 15)
        g.cs.digits = (int)digits;
    g.fmt = fmt <= FMT_ENG ? (uint8_t)fmt : FMT_NORMAL;
    g.fix = fix >= -1 && fix <= 9 ? (int8_t)fix : -1;
    g.cplx = cplx <= CPLX_POLAR ? (uint8_t)cplx : CPLX_RECT;
    g.grid = grid != 0;
    g.axes = axes != 0;
    if (bl >= 5 && bl <= 100) {
        g.backlight = (int)bl;
        hal_backlight_set(g.backlight);
    }
    ed_insert_mode = ins != 0;
    unsigned ua = version >= 2 ? pr_u8(r) : 1;
    g.usb_awake = r->err || ua != 0;
}

/* Factory defaults (MEM > Reset settings / Reset all): RAD, EXACT, 12 digits,
 * NORMAL FLOAT, a+bi, no grid, axes on, default backlight, insert mode. */
static void settings_reset(void)
{
    g.cs.angle = CAS_ANGLE_RAD;
    g.cs.exact = 1;
    g.cs.digits = 12;
    g.fmt = FMT_NORMAL;
    g.fix = -1;
    g.cplx = CPLX_RECT;
    g.grid = false;
    g.axes = true;
    g.backlight = HAL_BACKLIGHT_DEFAULT_PCT;
    hal_backlight_set(g.backlight);
    ed_insert_mode = true;
    g.usb_awake = true;
}

const persist_def_t persist_settings = {"settings", 2, settings_save, settings_load, settings_reset};
