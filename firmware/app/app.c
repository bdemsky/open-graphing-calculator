/*
 * Application core: the screen manager, key and touch dispatch, the SHIFT /
 * ALPHA state machine, the status bar, the blinking text cursor, the modal
 * message box, start-up (arena layout) and the `state` introspection used by
 * the console tests.
 *
 * Structure. The UI is a set of screens (screen_t in ui.h). Exactly one is
 * current (`cur`); ui_switch() makes another one current, calls its enter()
 * and draw() and redraws the status and soft-key bars. Menus (scr_menu,
 * menu.c), message boxes (scr_msg, below) and help (scr_help) are screens too;
 * they return to the screen below with `ui_returning` set so that screen keeps
 * its state.
 *
 * Event flow. core/calc.c pops keys and gestures and calls app_key_event() /
 * app_touch_event(); handle_key() resolves SHIFT/ALPHA into a modifier and
 * offers the key, in this order, to the soft-key bar (F1..F5, softkeys.c), the
 * global key actions (keys_dispatch(), keys.c) and finally the current
 * screen's key(). app_tick() runs every main-loop pass.
 *
 * State. All shared application state is the global `g` (app_t, ui.h):
 * the CAS context and settings, number format, Y= functions, WINDOW/TBLSET,
 * the graph cache and the SHIFT/ALPHA state. Module statics below hold the
 * screen stack, key repeat, cursor and message box. Persistent settings are
 * saved by other modules' persist_def_t blobs (mode.c, graph_state.c, ...),
 * not here.
 */
#include "app.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hal.h"
#include "anim.h"
void mq_state(void (*kv)(const char *, const char *, void *), void *user); /* app/mquest_core.c */
#include "py_core.h" /* Python: the break key, state */
#include "input.h"
#include "forms_ui.h" /* alternate forms browser: arena, state */
#include "modx.h" /* undo, clipboard, catalog: arena, events, state */
#include "nbdoc.h" /* the home screen's notebook: arena */
#include "m2d.h"
#include "registry.h"
#include "rpn.h"
#include "session.h"
#include "g3d.h"  /* 3D graphing: work memory, state */
#include "stat.h" /* statistics app: arena, state */
#include "sheet.h" /* spreadsheet: arena, state */
#include "sdbk.h"  /* microSD tools: ALPHA MENU = screenshot, state */
#include "console.h"    /* console_held_keys: the safe-mode key in the simulators */
#include "modules_ui.h" /* QSPI content modules: boot, notices, state */
#include "usb_app.h"    /* USB drive: installs from the computer, state */
#include "evlog.h"
#include "safemode.h"   /* boot recovery */
#include "ui.h"

app_t g; /* the application state shared by all screens (ui.h) */

/* Colours of the Y= functions (func_t.color indexes these), in the order the
 * Y= swatch cycles through them; the names are for `state yN_color`. */
const uint16_t func_colors[NCOLORS] = {C_BLUE, C_RED, C_BLACK, C_MAGENTA, C_GREEN, C_ORANGE, C_BROWN, C_NAVY};
const char *const func_color_names[NCOLORS] = {"blue", "red", "black", "magenta", "green", "orange", "brown",
                                               "navy"};

static const screen_t *cur, *prev; /* current screen; the last non-menu/message screen before it */
bool ui_returning;
static int held_key;                /* key held down (auto-repeat), 0 = none */
static uint32_t held_since, last_repeat, last_activity; /* ms: press time, last repeat, last key/touch */
static bool screen_dark;   /* idle on USB power with ON USB = STAY ON: display off, CPU running */
static bool touch_swallow; /* the gesture that woke the screen does nothing else */

/* Light the screen again after an idle dark period (any key or touch). */
static void screen_wake(void)
{
    screen_dark = false;
    hal_display_enable(true);
    hal_backlight_set(g.backlight);
    app_redraw();
}
static bool off_request;            /* set by SHIFT ON / auto power-off, read by the main loop */
#define APD_MS (5u * 60u * 1000u)   /* auto power-down after 5 minutes without input */

/* ------------------------------------------------------------ cursor
 * One blinking text cursor for the whole UI. A screen that draws an editor
 * calls ui_cursor_at() with the cell under the cursor (position, the
 * character there and its colours); app_tick() then toggles it every 500 ms by
 * repainting that one cell, so the screen does not have to redraw. Switching
 * screens hides it. The cursor shape shows the input state:
 *   SHIFT pending  orange arrow on dark grey;
 *   ALPHA          teal 'a' (lowercase once/lock) or 'A' (uppercase) on dark grey;
 *   overwrite      the character in inverse video;
 *   insert         the character with an underline and a bar on its left.
 * Only FONT-sized (10x20) cells are supported. */
static struct {
    bool active, on;
    int x, y;
    uint32_t ch;
    uint16_t fg, bg;
    uint32_t last;  /* ms of the last blink toggle */
} cursor;

/* Paint the cursor cell in its visible (on) or hidden (off: the plain
 * character) phase. The text log is disabled so the cursor glyphs never show
 * up in `expect text`. */
static void cursor_paint(bool on)
{
    if (!cursor.active)
        return;
    gfx_textlog_enable(false);
    if (!on) {
        gfx_char(FONT, cursor.x, cursor.y, cursor.ch, cursor.fg, cursor.bg);
    } else {
        uint32_t c = cursor.ch;
        uint16_t fg = cursor.bg, bg = cursor.fg;
        if (g.shift) {
            c = 0x2191; /* ↑ */
            bg = C_DKGRAY;
            fg = C_ORANGE;
        } else if (g.alpha) {
            c = (g.alpha == 1 || g.alpha == 3) ? 'a' : 'A';
            bg = C_DKGRAY;
            fg = C_TEAL;
        }
        if (!ed_insert_mode || g.shift || g.alpha) {
            gfx_char(FONT, cursor.x, cursor.y, c, fg, bg);
        } else {
            /* insert mode: draw char with an underline bar */
            gfx_char(FONT, cursor.x, cursor.y, cursor.ch, cursor.fg, cursor.bg);
            gfx_fill_rect(cursor.x, cursor.y + CH - 3, CW, 3, cursor.fg);
            gfx_fill_rect(cursor.x, cursor.y + 2, 2, CH - 4, cursor.fg);
        }
    }
    gfx_textlog_enable(true);
    cursor.on = on;
}

/* Place the cursor on the FONT cell at pixel (x, y), which shows character
 * `ch` in fg on bg, and show it at once (restarting the blink phase). */
void ui_cursor_at(int x, int y, uint32_t ch, uint16_t fg, uint16_t bg)
{
    cursor.active = true;
    cursor.x = x;
    cursor.y = y;
    cursor.ch = ch;
    cursor.fg = fg;
    cursor.bg = bg;
    cursor.last = hal_millis();
    cursor_paint(true);
}

/* Stop blinking. Nothing is repainted: the caller redraws the area anyway. */
void ui_cursor_hide(void) { cursor.active = false; }

/* Show the cursor now and restart its 500 ms phase (after every key, so the
 * cursor is visible while the user types). */
void ui_cursor_reset_blink(void)
{
    cursor.last = hal_millis();
    if (cursor.active)
        cursor_paint(true);
}

/* ------------------------------------------------------------ status bar
 * The 18 px bar at the top: angle and result mode plus the number format on
 * the left, the current screen's title in the middle, then the SHIFT/ALPHA
 * badge, "OVR" in overwrite mode and the battery gauge on the right. The
 * battery is sampled every 30 s from app_tick(), not on every redraw; a USB
 * plug or unplug (VBUS sense, polled every tick) triggers a reading at once. */
static float battery_v = NAN; /* last battery voltage (NAN: unknown) */
static bool on_usb;
static uint32_t last_batt;    /* ms of the last battery reading */

static void read_battery(void)
{
    hal_telemetry_t t;
    hal_telemetry(&t);
    battery_v = t.vbat_v;
    on_usb = t.usb_present;
}

/* Toast: a short notice in the middle of the status bar (where the screen
 * title is), e.g. "copied" after MENU F3. It is not time based (screenshots
 * and tests stay deterministic): it stays until the next key or touch event,
 * which clears it before that event is handled, so a toast set while handling
 * an event survives exactly until the user's next input. */
static char toast[40];

void ui_toast(const char *text)
{
    snprintf(toast, sizeof toast, "%s", text ? text : "");
    ui_draw_status();
}

/* Clear the toast at the start of an input event (redraws the bar if one was shown). */
static void toast_clear(void)
{
    if (toast[0]) {
        toast[0] = 0;
        ui_draw_status();
    }
}

/* Redraw the whole status bar from `g` and the current screen. Cheap enough to
 * call after every state change (SHIFT, ALPHA, MODE settings, insert mode). */
void ui_draw_status(void)
{
    const font_t *f = &font_small;
    if (cur && cur->style == SCREEN_GAME) { /* a game: no calculator state, a toast, a low battery */
        gfx_fill_rect(0, 0, LCD_WIDTH, STATUS_H, GAME_BAR_BG);
        if (toast[0]) {
            int w = gfx_text_width(f, toast), x0 = (LCD_WIDTH - w) / 2;
            gfx_fill_rect(x0 - 6, 1, w + 12, STATUS_H - 2, GAME_BAR_PILL);
            gfx_text(f, x0, 3, toast, GAME_BAR_INK, GFX_TRANSPARENT);
        }
        if (!on_usb && !isnan(battery_v) && battery_v < HAL_BATT_WARN_V) { /* a little battery, only when low */
            int bx = LCD_WIDTH - 24, by = 5;
            gfx_rect(bx, by, 16, 9, GAME_BAR_INK);
            gfx_fill_rect(bx + 16, by + 3, 2, 3, GAME_BAR_INK);
            gfx_fill_rect(bx + 2, by + 2, 3, 5, C_RED);
        }
        return;
    }
    gfx_fill_rect(0, 0, LCD_WIDTH, STATUS_H, C_STATUS_BG);
    char buf[64];
    char fmt[12] = ""; /* " FIX2", " SCI3", " ENG", ...: empty for NORMAL/FLOAT */
    if (g.fmt != FMT_NORMAL || g.fix >= 0) {
        if (g.fix >= 0)
            snprintf(fmt, sizeof fmt, " %s%d", g.fmt == FMT_SCI ? "SCI" : g.fmt == FMT_ENG ? "ENG" : "FIX", g.fix);
        else
            snprintf(fmt, sizeof fmt, " %s", g.fmt == FMT_SCI ? "SCI" : "ENG");
    }
    snprintf(buf, sizeof buf, "%s %s%s", g.cs.angle == CAS_ANGLE_DEG ? "DEG" : "RAD",
             g.cs.exact ? "EXACT" : "APPROX", fmt);
    gfx_text(f, 4, 3, buf, C_WHITE, GFX_TRANSPARENT);
    if (toast[0]) { /* a short notice (ui_toast) replaces the title until the next key or tap */
        int w = gfx_text_width(f, toast);
        int x0 = (LCD_WIDTH - w) / 2;
        gfx_fill_rect(x0 - 4, 2, w + 8, STATUS_H - 4, C_SEL_BG);
        gfx_text(f, x0, 3, toast, C_WHITE, GFX_TRANSPARENT);
    } else if (cur && cur->title) {
        int w = gfx_text_width(f, cur->title);
        gfx_text(f, (LCD_WIDTH - w) / 2, 3, cur->title, C_YELLOW, GFX_TRANSPARENT);
    }
    /* shift indicator */
    const char *sh = NULL;
    uint16_t shc = C_AMBER;
    if (g.shift)
        sh = "SHIFT";
    else if (g.alpha == 1)
        sh = "a", shc = C_TEAL;
    else if (g.alpha == 2)
        sh = "A", shc = C_TEAL;
    else if (g.alpha == 3)
        sh = "a-LK", shc = C_TEAL;
    else if (g.alpha == 4)
        sh = "A-LK", shc = C_TEAL;
    int x = LCD_WIDTH - 40; /* right edge of the badge, left of the battery */
    if (sh) {
        int w = gfx_text_width(f, sh);
        gfx_fill_rect(x - w - 8, 2, w + 6, STATUS_H - 4, shc);
        gfx_text(f, x - w - 5, 3, sh, C_BLACK, GFX_TRANSPARENT);
    }
    if (!ed_insert_mode)
        gfx_text(f, x - 80, 3, "OVR", C_WHITE, GFX_TRANSPARENT);
    /* battery: 4xAAA, HAL_BATT_EMPTY_V (3.6 V, power-off) .. HAL_BATT_FULL_V (6.0 V);
     * red below HAL_BATT_WARN_V (4.0 V). The outline is 26x11 px with a 2 px
     * terminal; the fill is up to 22 px wide, proportional to the voltage
     * between empty and full. On USB power the gauge shows "USB" instead. */
    int bx = LCD_WIDTH - 34, by = 4;
    gfx_rect(bx, by, 26, 11, C_WHITE);
    gfx_fill_rect(bx + 26, by + 3, 2, 5, C_WHITE);
    float frac = isnan(battery_v) ? 0.0f
                                  : (battery_v - HAL_BATT_EMPTY_V) / (HAL_BATT_FULL_V - HAL_BATT_EMPTY_V);
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    if (on_usb) {
        gfx_text(f, bx + 4, 3, "USB", C_GREEN, GFX_TRANSPARENT);
    } else {
        int w = (int)(frac * 22.0f + 0.5f);
        gfx_fill_rect(bx + 2, by + 2, w, 7, !(battery_v >= HAL_BATT_WARN_V) ? C_RED : C_GREEN);
    }
}

/* ------------------------------------------------------------ screens */
static const screen_t *home_screen; /* replacement home screen (RPN mode), NULL = scr_home */
const screen_t *ui_home(void) { return home_screen ? home_screen : &scr_home; }
void ui_set_home(const screen_t *s) { home_screen = s; }
const screen_t *ui_current(void) { return cur; }
const screen_t *ui_previous(void) { return prev; }

/* Make `s` the current screen: remember the one we leave in `prev` (unless it
 * is a menu or a message box, which are transient overlays: `prev` then keeps
 * the real screen underneath, so ui_message() from a menu and menu_open() from
 * a message still know where to return; the first switch away from one of
 * them sets `prev` only when there is none yet), hide the cursor, reset the
 * clip to the body, call enter() (which sees ui_returning) and draw() and
 * redraw the status and soft-key bars. Switching to the current screen is
 * allowed and re-enters / redraws it. */
void ui_switch(const screen_t *s)
{
    if (s != cur && cur != &scr_menu && cur != &scr_msg)
        prev = cur;
    else if (s != cur && !prev)
        prev = cur;
    cur = s;
    home_graph_flow_check(s);
    ui_cursor_hide();
    gfx_reset_clip();
    if (s->enter)
        s->enter();
    ui_draw_status();
    if (s->draw)
        s->draw();
    ui_softkeys_invalidate();
    ui_draw_softkeys();
}

/* Repaint everything (after a test pattern, a theme change or the console
 * `redraw`) without calling the screen's enter(). */
void app_redraw(void)
{
    gfx_clear(C_WHITE);
    ui_cursor_hide();
    ui_draw_status();
    if (cur && cur->draw)
        cur->draw();
    ui_softkeys_invalidate();
    ui_draw_softkeys();
}

const char *app_screen_name(void) { return cur ? cur->name : "none"; }
uint32_t app_last_activity(void) { return last_activity; }
cas_ctx *app_cas(void) { return g.cas; }

/* ------------------------------------------------------------ key text
 * What a key types comes from keys/layout.py (generated key_info[]). */

/* The text key `key` types in layer `mod` (key_info[].text / .shift_text /
 * .alpha), copied into disp (n bytes, UTF-8, truncated). `alpha` is the ALPHA
 * state the key was pressed in (g.alpha values): in the uppercase states 2 and
 * 4 a single lowercase letter is upper-cased. Returns false when the key types
 * nothing in that layer (navigation keys, actions). */
bool ui_key_text(int key, int mod, int alpha, char *disp, size_t n)
{
    if (key <= 0 || key >= KEY_COUNT)
        return false;
    const key_info_t *ki = &key_info[key];
    const char *s;
    char a[8];
    if (mod == MOD_ALPHA) {
        s = ki->alpha;
        if (s && (alpha == 2 || alpha == 4) && s[0] >= 'a' && s[0] <= 'z' && !s[1]) {
            a[0] = (char)(s[0] - 'a' + 'A');
            a[1] = 0;
            s = a;
        }
    } else if (mod == MOD_SHIFT) {
        s = ki->shift_text;
    } else {
        s = ki->text;
    }
    if (!s)
        return false;
    snprintf(disp, n, "%s", s);
    return true;
}

/* Text that goes after the cursor when the key is typed (the closing part of
 * templates such as `abs(` `)`), or NULL. ALPHA letters never have one. */
const char *ui_key_suffix(int key, int mod)
{
    if (key <= 0 || key >= KEY_COUNT)
        return NULL;
    return mod == MOD_SHIFT ? key_info[key].shift_suffix : mod == MOD_NONE ? key_info[key].suffix : NULL;
}

/* Standard single-line editing for any editor_t: LEFT/RIGHT move by one token
 * (SHIFT: to the start/end), DEL deletes the token before the cursor (SHIFT DEL
 * toggles the global insert/overwrite mode and redraws the status bar's OVR),
 * and any key that types text inserts it (with its template suffix). The
 * ALPHA case comes from g.alpha_snapshot_for_key because handle_key() has
 * already cleared a one-shot ALPHA by the time the screen sees the key.
 * Returns false for keys it does not handle (ENTER, CLEAR, UP/DOWN, ...), which
 * the caller then interprets. Does not redraw. */
bool ui_editor_key(editor_t *e, int key, int mod)
{
    switch (key) {
    case KEY_LEFT:
        if (mod == MOD_SHIFT)
            ed_home(e);
        else
            ed_left(e);
        return true;
    case KEY_RIGHT:
        if (mod == MOD_SHIFT)
            ed_end(e);
        else
            ed_right(e);
        return true;
    case KEY_DEL:
        if (mod == MOD_SHIFT) {
            ed_insert_mode = !ed_insert_mode;
            ui_draw_status();
        } else {
            ed_backspace(e);
        }
        return true;
    default:
        break;
    }
    char t[16];
    int alpha_case = g.alpha_snapshot_for_key;
    if (ui_key_text(key, mod, alpha_case, t, sizeof t)) {
        ed_insert_text(e, t, ui_key_suffix(key, mod));
        return true;
    }
    return false;
}

/* Touch: walk the cursor from the start of the line token by token until it
 * reaches display column `col` (or the end), so a tap never lands inside a
 * multi-character token such as `sin(`. */
void ui_editor_tap(editor_t *e, int col)
{
    ed_home(e);
    while (e->cur < e->len && ed_cursor_col(e) < col)
        ed_right(e);
}

/* Draw editor `e` as one line of `cols` FONT cells at pixel (x, y), scrolled
 * horizontally so the cursor stays visible: e->scroll (the first visible
 * display column, in code points) is adjusted here and kept in the editor
 * between draws. With `focus` the blinking cursor is placed on the cursor
 * cell. ED_MAX * 4 bytes hold any line, since a code point is <= 4 UTF-8 bytes. */
void ui_draw_editor_line(editor_t *e, int x, int y, int cols, uint16_t fg, uint16_t bg, bool focus)
{
    char buf[ED_MAX * 4];
    ed_display(e, buf, sizeof buf);
    int ccol = ed_cursor_col(e);
    /* scroll just enough to keep the cursor column in [scroll, scroll + cols) */
    if (ccol < e->scroll)
        e->scroll = ccol;
    if (ccol >= e->scroll + cols)
        e->scroll = ccol - cols + 1;
    /* skip `scroll` codepoints */
    const char *p = buf;
    for (int i = 0; i < e->scroll && *p; i++)
        utf8_next(&p);
    char vis[ED_MAX * 4];
    const char *q = p;
    for (int i = 0; i < cols && *q; i++)
        utf8_next(&q);
    size_t l = (size_t)(q - p);
    memcpy(vis, p, l);
    vis[l] = 0;
    gfx_fill_rect(x, y, cols * CW, CH, bg);
    gfx_text(FONT, x, y, vis, fg, bg);
    if (focus)
        ui_cursor_at(x + (ccol - e->scroll) * CW, y, ed_char_at_cursor(e), fg, bg);
}

/* ------------------------------------------------------------ message box
 * Modal message (errors, confirmations): a grey box with the title (red for
 * errors: titles with FAIL, ERR, NOT, INVALID; else blue) and word-wrapped
 * text; any key, a tap or the "ok" soft key returns to the
 * screen it was opened from. The strings are not copied: the caller keeps them
 * alive (string literals or static buffers) until the box is closed. */
static const char *msg_title, *msg_text;
static const screen_t *msg_return; /* where any key goes back to (NULL: home) */

/* Box at (20,58) 280x126 px: the title at y = 66, then the text in
 * font_medium at a 16 px pitch from y = 92 while y < 172 (at most 5 lines
 * of 26 code points; the line buffer holds 26 four-byte characters). The
 * "press any key" hint at y = 166 is drawn only when the text needs at most
 * 4 lines (a 5th line would overlap it; the "ok" soft key says the same). */
static void msg_draw(void)
{
    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, C_WHITE);
    gfx_fill_rect(20, 58, LCD_WIDTH - 40, 126, C_LTGRAY);
    gfx_rect(20, 58, LCD_WIDTH - 40, 126, C_DKGRAY);
    /* errors in red, other notices (EXPORTED, MEM, SCREENSHOT) in the accent colour */
    bool err = strstr(msg_title, "FAIL") || strstr(msg_title, "ERR") || strstr(msg_title, "NOT ") ||
               strstr(msg_title, "INVALID");
    gfx_text(FONT, 30, 66, msg_title, err ? C_RED : C_BLUE, GFX_TRANSPARENT);
    /* wrap the text at 26 columns (code points), at the last space that fits;
     * a word longer than a line (a path) is cut */
    char line[26 * 4 + 1];
    const char *s = msg_text;
    int y = 92;
    while (*s && y < 172) {
        int n = 0;
        const char *p = s, *brk = NULL;
        while (*p && n < 26) {
            if (*p == ' ')
                brk = p;
            utf8_next(&p);
            n++;
        }
        const char *next = p;
        if (*p && *p != ' ' && brk) /* mid-word: break at the last space */
            p = brk, next = brk + 1;
        else if (*p == ' ')
            next = p + 1;
        size_t l = (size_t)(p - s);
        if (l > sizeof line - 1)
            l = sizeof line - 1;
        memcpy(line, s, l);
        line[l] = 0;
        gfx_text(&font_medium, 30, y, line, C_BLACK, GFX_TRANSPARENT);
        y += 16;
        s = next;
    }
    if (y <= 156)
        gfx_text(&font_small, 30, 166, "press any key", C_DKGRAY, GFX_TRANSPARENT);
}

/* Any key closes the box: return to the saved screen as a "return" (it keeps
 * its state). Global keys such as HOME are handled before this by keys.c. */
static void msg_key(int key, int mod)
{
    const screen_t *r = msg_return ? msg_return : ui_home();
    ui_returning = true;
    ui_switch(r);
    ui_returning = false;
}

static void msg_touch(const touch_event_t *ev)
{
    if (ev->type == TEV_TAP)
        msg_key(KEY_ENTER, MOD_NONE);
}

static const softkeys_t msg_soft = {{{NULL}, {NULL}, {NULL}, {NULL}, {"ok"}}};
const screen_t scr_msg = {"message", NULL, NULL, msg_draw, msg_key, NULL, NULL, msg_touch, NULL, &msg_soft};

/* Show a modal message. Opened from a menu or another message, it returns to
 * the screen below those (`prev`), not to the menu. */
void ui_message(const char *title, const char *text)
{
    msg_title = title;
    msg_text = text;
    msg_return = (cur == &scr_msg || cur == &scr_menu) ? prev : cur;
    ui_switch(&scr_msg);
}

/* ------------------------------------------------------------ settings */
/* Push the MODE settings in `g` to their consumers after a change: the token
 * editor's degree display flag, the CAS settings (with 15 significant digits
 * while FIX/SCI/ENG is active, so numfmt.c rounds a full-precision value, and
 * cs.real set from COMPLEX = REAL, so odd roots of negative numbers are real:
 * (-8)^(1/3) = -2) and the compiled graph functions (recompiled lazily: angle
 * and complex mode change them). */
void app_apply_settings(void)
{
    ed_angle_deg = g.cs.angle == CAS_ANGLE_DEG;
    cas_settings cs = g.cs;
    if (g.fmt != FMT_NORMAL || g.fix >= 0)
        cs.digits = 15; /* full precision for FIX/SCI/ENG rounding (numfmt.c) */
    cs.real = g.cplx == CPLX_REAL; /* real odd roots in the CAS (cas.h) */
    cas_set_settings(g.cas, &cs);
    funcs_invalidate();
}

/* ------------------------------------------------------------ dispatch */
/* ON-key hook (keyq_set_on_hook): runs inside keyq_push(), i.e. in the key
 * scanning interrupt on hardware, while the main loop may be stuck in a long
 * CAS evaluation. cas_interrupt() only sets a flag the evaluator polls, which
 * is safe from an ISR; the evaluation then fails with CAS_ERR_INTERRUPTED. */
static void cas_break(void)
{
    py_break_isr(); /* and Python's KeyboardInterrupt (app/py_core.c) */
    if (g.cas)
        cas_interrupt(g.cas);
}

/* Global action "off" (SHIFT ON): the main loop powers off after this event. */
void app_request_off(void) { off_request = true; }

/* One key press (also every auto-repeat).
 *
 * SHIFT and ALPHA are prefix keys handled here; they only change state:
 *   SHIFT toggles g.shift (one-shot: the next key gets MOD_SHIFT);
 *   g.alpha is a small state machine driven by ALPHA:
 *       0 none --ALPHA--> 1 lower once --ALPHA--> 2 upper once --ALPHA--> 0
 *       SHIFT ALPHA --> 3 lower lock --ALPHA--> 4 upper lock --ALPHA--> 0
 *   One-shot states (shift, alpha 1/2) end after the next key; locks end with
 *   ALPHA (from 4) or CLEAR.
 * Every other key is turned into (key, mod): mod is MOD_SHIFT if SHIFT was
 * pending, else MOD_ALPHA if an ALPHA state is active and the key has a letter,
 * else MOD_NONE. It is then offered to the soft keys (F1..F5), the global
 * actions (keys.c) and the current screen, in that order. ON without SHIFT
 * does nothing here: its job (interrupting the CAS) was done by cas_break()
 * when it was queued. Afterwards the cursor and soft-key bar are refreshed. */
/* Number of home history items, read through home_state() ("history" key) so
 * the framework needs no accessor in home.c. */
static void hist_kv(const char *k, const char *v, void *user)
{
    if (!strcmp(k, "history"))
        *(int *)user = atoi(v);
}
static int home_history_count(void)
{
    int n = 0;
    home_state(hist_kv, &n);
    return n;
}

static void handle_key(int key)
{
    last_activity = hal_millis();
    session_activity();
    if (screen_dark) { /* the key only wakes the screen */
        screen_wake();
        return;
    }
    if (anim_input()) /* a playing animation pauses: the key does only that */
        return;
    toast_clear();
    if (key == KEY_SHIFT) {
        g.shift = !g.shift;
        ui_draw_status();
        ui_cursor_reset_blink();
        ui_draw_softkeys(); /* the bar shows the SHIFT F1..F5 labels */
        return;
    }
    if (key == KEY_ALPHA) {
        if (g.shift) {
            g.shift = 0;
            g.alpha = 3; /* lowercase lock */
        } else if (g.alpha == 0) {
            g.alpha = 1;
        } else if (g.alpha == 1) {
            g.alpha = 2;
        } else if (g.alpha == 3) {
            g.alpha = 4;
        } else {
            g.alpha = 0;
        }
        ui_draw_status();
        ui_cursor_reset_blink();
        ui_draw_softkeys();
        return;
    }
    int mod = g.shift ? MOD_SHIFT : g.alpha ? MOD_ALPHA : MOD_NONE;
    g.alpha_snapshot_for_key = g.alpha; /* for the letter case in ui_editor_key() */
    /* consume the one-shot states; redraw the badge if one ended */
    bool status_changed = g.shift || g.alpha == 1 || g.alpha == 2;
    g.shift = 0;
    if (g.alpha == 1 || g.alpha == 2)
        g.alpha = 0;
    /* ALPHA (once) then MENU: screenshot to the microSD card (app/sdbk.c) */
    bool shot = mod == MOD_ALPHA && key == KEY_MENU && g.alpha_snapshot_for_key <= 2;
    if (mod == MOD_ALPHA && !key_info[key].alpha)
        mod = MOD_NONE; /* keys without a letter (navigation, soft keys, ...) ignore ALPHA */
    if (key == KEY_CLEAR && g.alpha >= 3)
        g.alpha = 0, status_changed = true;
    if (status_changed)
        ui_draw_status();
    if (shot) {
        sdbk_hotkey();
        ui_draw_softkeys();
        return;
    }
    if (key == KEY_ON && mod == MOD_NONE) {
        ui_draw_softkeys();
        return; /* break: handled by the on-hook */
    }
    /* CLEAR on an empty home line wipes the history: snapshot it first so
     * MENU F1 (undo) can bring it back (app/input.h state guard). Any other
     * key on the home screen except MENU (which leads to undo) forgets it. */
    bool guard = cur == &scr_home && key == KEY_CLEAR && mod == MOD_NONE && home_history_count() > 0 &&
                 scr_home.editor && ed_empty(scr_home.editor());
    if (guard)
        input_state_guard("home", &scr_home);
    else if (cur == &scr_home && key != KEY_MENU)
        input_state_guard_forget();
    input_event_begin(key, mod); /* editor changes from here on are undo steps (app/input_undo.c) */
    if (KEY_IS_SOFT(key))
        ui_softkey(KEY_SOFT_INDEX(key), mod);
    else if (!keys_dispatch(key, mod) && cur && cur->key)
        cur->key(key, mod);
    input_event_end();
    if (guard) {
        bool wiped = home_history_count() == 0;
        input_state_guard_keep(wiped);
        if (wiped)
            ui_toast("history cleared: MENU F1 undoes");
    }
    ui_cursor_reset_blink();
    ui_draw_softkeys();
}

/* Key event from the main loop. A press is handled at once and starts the
 * auto-repeat timer; a release of the held key stops it. */
void app_key_event(int key, bool pressed)
{
    if (!pressed) {
        if (key == held_key)
            held_key = 0;
        return;
    }
    held_key = key;
    held_since = last_repeat = hal_millis();
    handle_key(key);
}

int app_held_key(uint32_t *ms)
{
    if (ms)
        *ms = held_key ? hal_millis() - held_since : 0;
    return held_key;
}

/* Periodic work, every main-loop pass (also while idle: the loop wakes at
 * least every 10 ms in the simulators). Time arithmetic is unsigned, so the
 * millisecond counter may wrap. */
void app_tick(uint32_t now)
{
    /* auto-repeat for arrows and ⌫: first repeat after 450 ms, then every 70 ms */
    if (held_key == KEY_LEFT || held_key == KEY_RIGHT || held_key == KEY_UP || held_key == KEY_DOWN ||
        held_key == KEY_DEL) {
        if (now - held_since > 450 && now - last_repeat >= 70) {
            last_repeat = now;
            handle_key(held_key);
        }
    }
    if (cursor.active && now - cursor.last >= 500) {
        cursor.last = now;
        cursor_paint(!cursor.on);
    }
    /* plugging or unplugging USB switches the gauge at once, not at the next 30 s reading */
    if (now - last_batt > 30000 || hal_usb_present() != on_usb) {
        last_batt = now;
        read_battery();
        ui_draw_status();
        if (!on_usb && battery_v < HAL_BATT_WARN_V)
            py_nb_flush(); /* a low battery: the notebook is saved before it runs out */
    }
    if (cur && cur->tick)
        cur->tick(now);
    session_tick(now); /* autosave one minute after the last input */
    usb_app_tick(now); /* modules dropped on the USB drive (app/usb_app.c) */
    py_tick(now);      /* the Notebook: saving, the browser page (app/py_core.c) */
    /* Idle: on USB power with ON USB = STAY ON only the screen goes dark (the CPU, the USB
     * link, the browser page and the drive keep running; a key or touch lights it); otherwise
     * auto power-down, but never while a computer uses the USB link (it would cut it). */
    if (now - last_activity > APD_MS) {
        if (g.usb_awake && hal_usb_present()) {
            if (!screen_dark) {
                screen_dark = true;
                hal_display_enable(false);
            }
        } else if (!hal_usb_configured()) {
            if (screen_dark)
                screen_wake(); /* unplugged while dark: switch off normally */
            off_request = true;
        }
    }
}

static char touch_last[48] = "none"; /* last gesture, for `state touch_last` */

/* Touch gesture from core/touch.c (screen coordinates; x0/y0 is where the
 * gesture started). Gestures that start in the soft-key bar belong to the bar
 * (a tap presses the soft key under it: 64 px per key, with SHIFT if pending);
 * pinches are always the screen's (two fingers may straddle the bar).
 * Everything else goes to the current screen's touch(). */
void app_touch_event(const touch_event_t *ev)
{
    last_activity = hal_millis();
    session_activity();
    if (screen_dark) {
        screen_wake();
        touch_swallow = true;
        return;
    }
    if (touch_swallow) { /* the rest of the waking gesture */
        if (ev->type != TEV_DOWN)
            return;
        touch_swallow = false;
    }
    snprintf(touch_last, sizeof touch_last, "%s %d,%d", touch_event_name(ev->type), ev->x, ev->y);
    if ((ev->type == TEV_DOWN || ev->type == TEV_TAP) && anim_input())
        return; /* touching the screen pauses a playing animation (and does nothing else) */
    if (ev->type == TEV_DOWN)
        ui_drag_reset(); /* a new gesture: drag-to-scroll starts from zero */
    if (ev->type == TEV_TAP)
        toast_clear();
    if (ev->type == TEV_TAP && ev->y0 < SOFT_Y && (g.shift || g.alpha == 1 || g.alpha == 2)) {
        /* a tap acts like a key: one-shot SHIFT/ALPHA states end */
        g.shift = 0;
        if (g.alpha < 3)
            g.alpha = 0;
        ui_draw_status();
    }
    input_event_begin(0, MOD_NONE); /* undo steps, as for keys */
    if (ev->y0 >= SOFT_Y && ev->type != TEV_PINCH && ev->type != TEV_PINCH_END) {
        /* the soft-key bar: a tap presses the key under it; other gestures that
         * start there are not the screen's -- but a game sees the finger go down and up
         * there too) */
        if (cur && cur->style == SCREEN_GAME && cur->touch &&
            (ev->type == TEV_DOWN || ev->type == TEV_UP || ev->type == TEV_TAP))
            cur->touch(ev);
        if (ev->type == TEV_TAP) {
            int mod = g.shift ? MOD_SHIFT : MOD_NONE;
            g.shift = 0;
            ui_draw_status();
            ui_softkey(ev->x0 * 5 / LCD_WIDTH, mod);
            ui_cursor_reset_blink();
        }
        input_event_end();
        ui_draw_softkeys();
        return;
    }
    if (cur && cur->touch)
        cur->touch(ev);
    input_event_end();
    ui_cursor_reset_blink();
    ui_draw_softkeys();
}

/* Read and clear the power-off request (the main loop then saves the session
 * and switches off). */
bool app_power_off_requested(void)
{
    bool r = off_request;
    off_request = false;
    return r;
}

static void *app_arena; /* kept for app_resume() */
static size_t app_arena_size;

static int boot_cause = -1; /* HAL_RESET_* for the next app_init (-1: ask the platform) */

/* Wake from power-off. On hardware a wake is a reset, so this only runs in
 * the simulators, which emulate that: the app restarts from the saved session
 * (exactly what a real power cycle does). A deliberate restart, so it does
 * not count as a failed boot (core/safemode.h). */
void app_resume(void) { app_restart(HAL_RESET_WAKE); }

void app_restart(int cause)
{
    safemode_clean_shutdown();
    boot_cause = cause;
    app_init(app_arena, app_arena_size);
}

/* Simulators (console `reboot crash`, the simulated watchdog): restart as
 * after a crash, i.e. without clearing the boot-attempt counter; three in a
 * row start safe mode. */
void app_crash_restart(int cause)
{
    boot_cause = cause;
    app_init(app_arena, app_arena_size);
}

static uint8_t in_cas_poll;
bool app_in_cas_poll(void) { return in_cas_poll != 0; }
int app_cas_poll(void *user)
{
    (void)user;
    hal_watchdog_kick();
    in_cas_poll++;
    hal_usb_poll(); /* a long evaluation does not starve the USB link */
    in_cas_poll--;
    return 0;
}

/* An unexpected start goes into the event log (core/evlog.h), once per boot:
 * a crash (with its pc / lr), the watchdog, a brown-out or the reset pin, and
 * safe mode. Power-on, switch-on and a deliberate restart are not events. */
static void log_start(void)
{
    int c = safemode_reset_cause();
    if (c != HAL_RESET_POWERON && c != HAL_RESET_WAKE && c != HAL_RESET_SOFTWARE)
        evlog_add("start: %s", safemode_reset_text());
    char crash[224];
    if (c == HAL_RESET_FAULT && hal_crash_text(crash, sizeof crash)) {
        const char *at = strstr(crash, "pc "); /* "N since power-on, last at pc X lr Y (...)" */
        char *end = at ? strstr(at, " (") : NULL;
        if (end)
            *end = 0;
        evlog_add("crash: %s", at ? at : crash);
    }
    if (safemode_active()) {
        int r = safemode_reason();
        evlog_add("safe mode: %s", r == SAFEMODE_KEY ? "ON + CLEAR held" : r == SAFEMODE_CRASHLOOP ? "crash loop"
                                                                                                   : "requested");
    }
}

/* The raw key state for the safe-mode check: the keypad plus keys held
 * through the console (`press`, `hold`). */
static uint64_t boot_keys(void) { return hal_keypad_raw() | console_held_keys(); }

/* Boot the application in `arena` (all of the app's large memory; nothing is
 * malloc'ed). The arena is carved front to back:
 *
 *   session buffer   SESSION_MAX (256 KB), 32 KB in a small arena
 *   help workspace   HELP_WORK (128 KB), 16 KB in a small arena
 *   RPN stack + undo RPN_WORK (256 KB), RPN_WORK_SMALL (12 KB) in a small arena
 *   statistics       lists + scratch, size chosen by stat_mem_init()
 *   input comforts   INPUT_WORK (84 KB), INPUT_WORK_SMALL (4 KB) in a small arena
 *   forms browser    16 KB, 4 KB in a small arena (app/forms_ui.c)
 *   notebook         the home notebook (18 KB), its strings (256 KB) and a copy of both, graph cell
 *                    images (about 1 MB); none in a small arena (app/nbdoc.h)
 *   USB drive        2 MB staging + ~40 KB of FAT/directory (app/usb_app.c), none in a small arena
 *   graph cache      one 320x240 RGB565 frame (150 KB), only if >= 1 MB is
 *                    still left for the CAS after it
 *   CAS heap         everything that is left (cas_init)
 *
 * "Big" means the full layout fits with 1 MB to spare; the small layout is for
 * the hardware's fallback arena in AXI SRAM when the SDRAM fails its test.
 * Then all of `g` gets its defaults (RAD, EXACT, 12 digits, NORMAL/FLOAT,
 * a+bi, standard window, Y1..Y6 enabled in the first six colours), the
 * per-feature modules are reset, the saved session overrides whatever it
 * contains, and the home screen (or its RPN replacement) is shown.
 *
 * Also called again by app_resume(): every module must tolerate re-init with
 * its statics from the previous run (see EXTENDING.md). */
void app_init(void *arena, size_t size)
{
    app_arena = arena;
    app_arena_size = size;
    /* boot recovery first: ON + CLEAR held together at power-on, or a crash
     * loop (faults, watchdog resets), starts safe mode (no content modules,
     * no saved session) */
    evlog_boot(); /* a new boot number for the event log (core/evlog.h) */
    bool sm_keys = safemode_keys_held(boot_keys, console_key_raw_mask(KEY_ON) | console_key_raw_mask(KEY_CLEAR));
    safemode_boot(sm_keys, boot_cause >= 0 ? boot_cause : hal_reset_cause());
    boot_cause = -1;
    if (sm_keys)
        keyq_clear(); /* the held ON and CLEAR are not input for the home screen */
    memset(&g, 0, sizeof g);
    tok_init();
    m2d_init(); /* 2D template tokens + editor hooks, before the session is restored */
    uint8_t *a = arena;
    size_t fbbytes = (size_t)LCD_WIDTH * LCD_HEIGHT * 2;
    /* The graph cache is an optimisation: skip it when the arena is small
     * (hardware fallback arena after an SDRAM failure). The session buffer
     * (serialization of the saved state) is SESSION_MAX normally, 32 KB in
     * the fallback arena; the help viewer's workspace HELP_WORK (128 KB; 16 KB
     * in the fallback arena). */
    bool big = size >= fbbytes + SESSION_MAX + HELP_WORK + (1024u << 10);
    size_t sess = big ? SESSION_MAX : 32u << 10;
    size_t helpw = big ? HELP_WORK : 16u << 10;
    void *sess_buf = a;
    a += sess;
    help_init(a, helpw);
    modules_boot(); /* the manual module (QSPI), unless safe mode / core build */
    a += helpw;
    size -= sess + helpw;
    size_t rpnw = big ? RPN_WORK : RPN_WORK_SMALL; /* RPN stack + undo (rpn.c) */
    rpn_init(a, rpnw);
    rpn_set_renderer(&m2d_rpn_renderer); /* stack levels in textbook form (app/m2d_home.c) */
    a += rpnw;
    size -= rpnw;
    size_t statw = stat_mem_init(a, big); /* statistics lists + scratch (app/stat_lists.c) */
    a += statw;
    size -= statw;
    size_t inw = input_init(a, big); /* undo log, clipboard, catalog index (app/input.h) */
    a += inw;
    size -= inw;
    size_t g3w = g3d_mem_init(a, big); /* 3D graphing meshes + z-buffer + image (app/g3d_model.c) */
    a += g3w;
    size -= g3w;
    size_t shw = sheet_mem_init(a, big); /* spreadsheet cells + buffers (app/sheet_core.c) */
    a += shw;
    size -= shw;
    size_t mxw = modx_mem_init(a, big); /* RW state of code modules (app/modx.c) */
    a += mxw;
    size -= mxw;
    size_t usbw = usb_mem_init(a, big); /* the USB CALC drive's staging buffer (app/usb_app.c) */
    a += usbw;
    size -= usbw;
    size_t fmw = forms_mem_init(a, big); /* alternate forms browser (app/forms_ui.c) */
    a += fmw;
    size -= fmw;
    size_t nbw = nbdoc_mem_init(a, big); /* the notebook: document, strings, graph cell images (app/nbdoc.h) */
    a += nbw;
    size -= nbw;
    size -= modx_code_init(a + size, big ? size : 0); /* code-load modules: the end of the arena (app/modx.c) */
    if (size >= fbbytes + (1024u << 10)) {
        g.graph_cache = (uint16_t *)a;
        a += fbbytes;
        size -= fbbytes;
    } else {
        g.graph_cache = NULL;
    }
    g.cas = cas_init(a, size);
    /* cas_init needs >= 256 KB; every platform arena is larger (the smallest is
     * the hardware's SDRAM-failure fallback, 320 KB of AXI SRAM). */
    size_t stack = hal_cas_stack_limit();
    if (stack)
        cas_set_stack_limit(g.cas, stack);
    cas_set_poll(g.cas, app_cas_poll, NULL); /* long evaluations keep the watchdog fed */
    /* defaults (the saved session below overrides them) */
    cas_get_settings(g.cas, &g.cs);
    g.cs.angle = CAS_ANGLE_RAD;
    g.cs.exact = 1;
    if (g.cs.digits <= 0)
        g.cs.digits = 12;
    g.fmt = FMT_NORMAL;
    g.fix = -1;
    g.cplx = CPLX_RECT;
    ed_insert_mode = true;
    keyq_set_on_hook(cas_break); /* ON interrupts the CAS (from the key ISR) */
    g.axes = true;
    g.grid = false;
    g.xmin = -10, g.xmax = 10, g.xscl = 1, g.ymin = -10, g.ymax = 10, g.yscl = 1;
    g.tblstart = 0, g.tbldelta = 1;
    for (int i = 0; i < NFUNC; i++) {
        g.f[i].enabled = true;
        g.f[i].color = (uint8_t)(i % NCOLORS);
    }
    g.backlight = HAL_BACKLIGHT_DEFAULT_PCT;
    hal_backlight_set(g.backlight);
    g.usb_awake = true;
    home_reset();
    yvars_invalidate();
    vars_init();
    graphx_init(); /* graph modes, styles, sliders (app/gr_model.c) */
    /* restore the saved session (settings, Y=, window, history, CAS variables, apps) */
    session_init(sess_buf, sess);
    if (safemode_active())
        session_disable("safe mode");
    bool first_run = session_load() == 0; /* nothing saved yet: a new calculator or after Reset all */
    log_start(); /* after the load: the saved event log is merged, the boot numbered */
    app_apply_settings();
    read_battery();
    last_batt = last_activity = hal_millis();
    off_request = false;
    g.shift = 0;
    g.alpha = 0;
    gfx_set_base_clip(UI_BOTTOM); /* the soft-key bar is drawn by app/softkeys.c only */
    gfx_clear(C_WHITE);
    cur = prev = NULL;
    toast[0] = 0;
    ui_switch(ui_home());
    if (first_run) /* first power-on: where help is (the toast ends at the first key) */
        ui_toast("new? SHIFT then MENU: help");
    modules_boot_notice(); /* safe mode / flash fault toast, or the offer to install the manual */
}

/* Report the application state as key=value pairs (console `state`, `expect
 * state`): the screen, MODE settings, input state, Y= functions, WINDOW,
 * then each module's own keys (home, RPN, graph, table, menu, help, stat,
 * session, touch, CAS memory). A module with new keys adds one call here.
 * Values passed to fn are only valid during the call. */
void app_state_foreach(app_kv_fn fn, void *user)
{
    char b[64];
    fn("screen", app_screen_name(), user);
    fn("angle", g.cs.angle == CAS_ANGLE_DEG ? "deg" : "rad", user);
    fn("exact", g.cs.exact ? "1" : "0", user);
    snprintf(b, sizeof b, "%d", g.cs.digits);
    fn("digits", b, user);
    fn("format", g.fmt == FMT_SCI ? "sci" : g.fmt == FMT_ENG ? "eng" : "normal", user);
    snprintf(b, sizeof b, "%d", g.fix);
    fn("fix", b, user);
    fn("complex", g.cplx == CPLX_REAL ? "real" : g.cplx == CPLX_POLAR ? "polar" : "rect", user);
    {
        cas_settings cs; /* what the CAS really uses (app_apply_settings) */
        cas_get_settings(g.cas, &cs);
        fn("cas_real", cs.real ? "1" : "0", user);
    }
    fn("grid", g.grid ? "1" : "0", user);
    fn("axes", g.axes ? "1" : "0", user);
    fn("shift", g.shift ? "shift" : g.alpha ? "alpha" : "none", user);
    snprintf(b, sizeof b, "%d", g.alpha);
    fn("alpha", b, user);
    fn("insert", ed_insert_mode ? "1" : "0", user);
    fn("autosimp", autosimp_on() ? "1" : "0", user); /* MODE SIMPLIFY (app/result_actions.c) */
    snprintf(b, sizeof b, "%d", hal_backlight_get());
    fn("backlight", b, user);
    fn("screen_dark", screen_dark ? "1" : "0", user);
    fn("usb_awake", g.usb_awake ? "1" : "0", user);
    home_state(fn, user);
    rpn_state(fn, user);
    forms_state(fn, user);
    char disp[ED_MAX * 4];
    for (int i = 0; i < NFUNC; i++) {
        char k[16];
        ed_display(&g.f[i].ed, disp, sizeof disp);
        snprintf(k, sizeof k, "y%d", i + 1);
        fn(k, disp, user);
        snprintf(k, sizeof k, "y%d_on", i + 1);
        fn(k, g.f[i].enabled ? "1" : "0", user);
        snprintf(k, sizeof k, "y%d_color", i + 1);
        fn(k, func_color_names[g.f[i].color], user);
    }
    const struct { const char *k; double v; } w[] = {
        {"xmin", g.xmin}, {"xmax", g.xmax}, {"xscl", g.xscl}, {"ymin", g.ymin}, {"ymax", g.ymax},
        {"yscl", g.yscl}, {"tblstart", g.tblstart}, {"tbldelta", g.tbldelta},
    };
    for (size_t i = 0; i < sizeof w / sizeof w[0]; i++) {
        snprintf(b, sizeof b, "%.10g", w[i].v);
        fn(w[i].k, b, user);
    }
    graph_state(fn, user);
    table_state(fn, user);
    if (cur == &scr_menu) {
        fn("menu", menu_current_title(), user);
        snprintf(b, sizeof b, "%d", menu_current_sel());
        fn("menu_sel", b, user);
    }
    { /* the soft-key bar's labels, F1..F5, "|"-separated (empty: no key) */
        const softkeys_t *sk = ui_softkeys();
        char bar[96] = "";
        for (int i = 0; i < 5; i++)
            snprintf(bar + strlen(bar), sizeof bar - strlen(bar), "%s%s", i ? "|" : "",
                     ui_softkey_label(sk->key[i].label) ? ui_softkey_label(sk->key[i].label) : "");
        fn("softkeys", bar, user);
    }
    if (cur == &scr_menu) {
        menu_item_t it;
        if (menu_current_item(&it))
            fn("menu_item", it.label, user);
    }
    help_state(fn, user);
    stat_state(fn, user);
    sheet_state(fn, user);
    sdbk_state(fn, user);
    input_state(fn, user);
    g3d_state(fn, user);
    session_state(fn, user);
    mq_state(fn, user);      /* Math Quest (app/mquest_core.c + the module) */
    py_state(fn, user);      /* Python (app/py_core.c + the module) */
    modules_state(fn, user); /* safe mode, boot attempts, manual module (app/modules_ui.c) */
    usb_app_state(fn, user); /* USB drive and card (app/usb_app.c) */
    modx_status(fn, user);   /* code modules (app/modx.c) */
    anim_state(fn, user);    /* animation (app/anim_core.c + the module) */
    fn("touch_last", touch_last, user);
    fn("toast", toast, user);
    fn("touch_available", hal_touch_available() ? "1" : "0", user);
    cas_mem_stats ms;
    cas_mem(g.cas, &ms);
    snprintf(b, sizeof b, "%lu/%lu", (unsigned long)ms.used, (unsigned long)ms.arena_size);
    fn("cas_mem", b, user);
}
