/*
 * Generic list menu (CAS, MATH, ANGLE, ZOOM, CATALOG, APPS, VARS, MEM, the MENU
 * key's context menu, ...) and the built-in menu definitions.
 *
 * One screen, scr_menu, shows whichever menu_def_t (ui.h) was opened last: a
 * title line, then MROWS items. UP/DOWN move the selection (wrapping), F1/F2
 * page, ENTER chooses, CLEAR closes; digits 1..9, 0 choose the first ten
 * items directly (their labels are prefixed "1:".."0:"), ALPHA letters A..Z
 * the next 26 (prefixed "A:".."Z:"); in an `alpha_jump` menu (CATALOG) letters jump to the
 * first item starting with that letter instead, with ALPHA lock switched on
 * while it is open. Touch: tap an item to choose it, drag to scroll.
 * Type-to-filter menus (menu_def_t.filter_key, CATALOG and HISTORY in
 * app/input_*.c) get every other key first and show their query at the right
 * of the title line; letters then never jump and digits never choose.
 *
 * Choosing an item calls the menu's on_select(), or (on_select NULL) inserts
 * the item's insert/suffix text into the editor of the screen the menu was
 * opened from (`origin`) and returns there. Menus opened from menus, messages
 * or help keep the original `origin`, so a sub-menu still inserts into the
 * screen below all of them.
 *
 * State: the open menu `m`, the selection `sel` and the first visible row
 * `top` are module statics; nothing is saved.
 */
#include <stdio.h>
#include <string.h>
#include "stat.h"
#include "theme.h"
#include "ui.h"

static const menu_def_t *m;     /* the open (or last opened) menu */
static int sel, top;            /* selected item; first visible item */
static const screen_t *origin;  /* screen the menu (chain) was opened from */
/* Rows below the title line: the title takes UI_CH + 4 px of the 206 px body,
 * so 9 rows with the 20 px font and 6 with the 28 px large font. */
#define MROWS ((BODY_H - UI_CH - 4) / UI_CH) /* rows below the title line (UI_CH: large-font mode) */
#define QUERY_SHOW 48 /* bytes of a filter query shown in the title line (+ cursor) */

/* ------------------------------------------------------------ definitions */
/* CAS commands. Every template was checked against the real CAS (firmware/cas,
 * console `eval`) and the numeric stub: factor(x^2-1), expand((x+1)^2),
 * simplify(sin(x)^2+cos(x)^2), solve(x^2=2,x), diff(x^3,x), integrate(sin(x)^2,x),
 * integrate(x^2,x,0,1), limit(sin(x)/x,x,0), approx(pi). */
static const menu_item_t cas_items[] = {
    {"factor(", "factor(", ")", 0},
    {"expand(", "expand(", ")", 0},
    {"simplify(", "simplify(", ")", 0},
    {"solve( ,x)", "solve(", ",x)", 0},
    {"d/dx  diff( ,x)", "diff(", ",x)", 0},
    {"∫ integrate( ,x)", "integrate(", ",x)", 0},
    {"∫ integrate( ,x,a,b)", "integrate(", ",x,0,1)", 0},
    {"limit( ,x,a)", "limit(", ",x,0)", 0},
    {"approx(", "approx(", ")", 0},
    /* del: grad, divergence, curl, Laplacian in x, y, z (∇(x*y), ∇·[x,y,z], ...) */
    {"∇f  gradient", "∇(", ")", 0, "nabla"},
    {"∇·F  divergence", "∇·[", "]", 0, "nabla"},
    {"∇×F  curl", "∇×[", "]", 0, "nabla"},
    {"∇²f  Laplacian", "∇²(", ")", 0, "nabla"},
};
const menu_def_t menu_cas = {"CAS", cas_items, sizeof cas_items / sizeof cas_items[0], NULL, false, "menu-cas"};

static const menu_item_t math_items[] = {
    {"abs(", "abs(", NULL, 0},     {"round(", "round(", NULL, 0}, {"floor(", "floor(", NULL, 0},
    {"ceil(", "ceil(", NULL, 0},   {"³", "³", NULL, 0, "powers"},
    /* The real CAS has no cbrt()/nthroot(): use powers, which both CAS builds
     * accept. ∛ wraps the argument; ˣ√ is infix (8^(1/3)). */
    {"∛  ( )^(1/3)", "(", ")^(1/3)", 0, "nth-roots"},
    {"ˣ√  ^(1/n)", "^(1/", ")", 0, "nth-roots"}, {"min(", "min(", NULL, 0}, {"max(", "max(", NULL, 0},
    {"mod(", "mod(", NULL, 0},     {"!", "!", NULL, 0, "factorial"},
};
const menu_def_t menu_math = {"MATH", math_items, sizeof math_items / sizeof math_items[0], NULL, false, "menu-math"};

static const menu_item_t angle_items[] = {
    {"° degrees", "°", NULL, 0, "menu-angle"}, {"ʳ radians", "ʳ", NULL, 0, "menu-angle"},
};
const menu_def_t menu_angle = {"ANGLE", angle_items, sizeof angle_items / sizeof angle_items[0], NULL, false,
                               "menu-angle"};

/* ZOOM (F3 on the graph screens): the actions are graph_zoom() codes; its
 * switch in app/graph.c uses the literal numbers 1..7, so keep this order. */
enum { Z_IN = 1, Z_OUT, Z_STD, Z_DEC, Z_SQUARE, Z_TRIG, Z_FIT };
/* Labels say what each zoom does (the classic names come first, for users
 * who know them); Z_STAT is handled here (stat_zoomstat, app/stat.h). */
#define Z_STAT 100
static const menu_item_t zoom_items[] = {
    {"Zoom In  ×2", NULL, NULL, Z_IN, "zoom"},
    {"Zoom Out ÷2", NULL, NULL, Z_OUT, "zoom"},
    {"ZStandard  x,y −10..10", NULL, NULL, Z_STD, "zoom"},
    {"ZDecimal  0.1 per pixel", NULL, NULL, Z_DEC, "zoom"},
    {"ZSquare  round circles", NULL, NULL, Z_SQUARE, "zoom"},
    {"ZTrig  x −2π..2π", NULL, NULL, Z_TRIG, "zoom"},
    {"ZoomFit  fit y to graphs", NULL, NULL, Z_FIT, "zoom"},
    {"ZoomStat  fit stat plots", NULL, NULL, Z_STAT, "stat-plots"},
};
/* Apply the zoom and show the graph (also when ZOOM was opened elsewhere). */
static void zoom_select(const menu_item_t *it)
{
    if (it->action == Z_STAT) {
        if (stat_zoomstat()) {
            ui_message("ZOOMSTAT", "No stat plot with data is on: switch one on in Stat plots.");
            return;
        }
    } else
        graph_zoom(it->action);
    if (!home_graph_embed(false)) /* (graphing from the home screen: into its notebook) */
        ui_switch(&scr_graph);
}
const menu_def_t menu_zoom = {"ZOOM", zoom_items, sizeof zoom_items / sizeof zoom_items[0], zoom_select, false,
                              "zoom"};

/* ------------------------------------------------------------ screen */
/* ui_confirm (ui.h): a No/Yes menu. "No" comes first so that ENTER, or the
 * same key pressed twice by accident, never confirms. */
static void (*confirm_yes)(void);
static const menu_item_t confirm_items[] = {
    {"No", NULL, NULL, 0, NULL},
    {"Yes", NULL, NULL, 1, NULL},
};
static void confirm_select(const menu_item_t *it)
{
    void (*yes)(void) = confirm_yes;
    menu_close(); /* back to the screen the question was asked on */
    if (it->action == 1 && yes)
        yes();
}
static void confirm_sk_yes(void) { confirm_select(&confirm_items[1]); }
static void confirm_sk_help(void) { help_context(); }
/* yes on F1, no on F5 (the leave slot), so the two are far apart */
static const softkeys_t confirm_soft = {
    {{"yes", confirm_sk_yes}, {NULL}, {NULL}, {"help", confirm_sk_help}, {"no", menu_close}},
};
static menu_def_t menu_confirm_def = {NULL, confirm_items, 2, confirm_select, false, "mem", NULL, NULL,
                                      &confirm_soft};

void ui_confirm(const char *question, void (*on_yes)(void))
{
    confirm_yes = on_yes;
    menu_confirm_def.title = question;
    menu_open(&menu_confirm_def);
}

const char *menu_current_title(void) { return m ? m->title : ""; }
int menu_current_sel(void) { return sel; }
const menu_def_t *menu_current(void) { return ui_current() == &scr_menu ? m : NULL; }
const screen_t *menu_origin(void) { return origin; }

/* Number of items of the open menu: static (items/n) or dynamic (count()). */
static int mcount(void) { return m->items ? m->n : m->count ? m->count() : 0; }

/* Item i of the open menu (a copy; a dynamic menu's strings are valid until
 * the next item_at()). */
static void item_at(int i, menu_item_t *out)
{
    if (m->items)
        *out = m->items[i];
    else
        m->get(i, out);
}

bool menu_current_item(menu_item_t *out)
{
    if (ui_current() != &scr_menu || !m || sel < 0 || sel >= mcount())
        return false;
    item_at(sel, out);
    return true;
}

/* Open `def` with the first item selected. The origin (insertion target and
 * return screen) is the current screen, unless that is a menu, message or
 * help page: then the existing origin is kept (or the previous screen is used
 * if there is none). An alpha_jump menu turns on lowercase ALPHA lock so
 * letter keys arrive as letters. */
void menu_open(const menu_def_t *def)
{
    const screen_t *cur = ui_current();
    if (cur != &scr_menu && cur != &scr_msg && cur != &scr_help)
        origin = cur;
    else if (!origin)
        origin = ui_previous();
    m = def;
    sel = top = 0;
    if (def->alpha_jump)
        g.alpha = 3; /* catalog: letters jump */
    ui_switch(&scr_menu);
}

/* Back to the origin (or home) as a return, so it keeps its state; ends the
 * ALPHA lock an alpha_jump menu switched on. */
void menu_close(void)
{
    if (m && m->alpha_jump)
        g.alpha = 0;
    ui_returning = true;
    ui_switch(origin ? origin : ui_home());
    ui_returning = false;
}

/* Display text of item i: "1:label".."9:label", "0:label" for the tenth,
 * "A:".."Z:" for items 11..36 (chosen with the ALPHA letters), plain labels
 * after that and in alpha_jump menus. */
static void label_for(int i, char *b, size_t n)
{
    char k = i < 9 ? (char)('1' + i) : i == 9 ? '0' : (char)('A' + i - 10);
    menu_item_t it;
    item_at(i, &it);
    if (m->alpha_jump || i >= 36)
        snprintf(b, n, "%s", it.label);
    else
        snprintf(b, n, "%c:%s", k, it.label);
}

/* Title bar (UI_CH + 2 px, light grey), then rows of UI_CH px starting at
 * BODY_Y + UI_CH + 4; the selected row is inverted. `top` is first scrolled so
 * `sel` is visible. ↑ / ↓ at the right mark more items above / below. */
static void menu_draw(void)
{
    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, C_WHITE);
    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, UI_CH + 2, C_LTGRAY);
    gfx_text(UI_FONT, 4, BODY_Y + 1, m->title, C_BLACK, GFX_TRANSPARENT);
    if (m->query) { /* type-to-filter menus: the query, right-aligned, with a block cursor */
        char q[QUERY_SHOW];
        snprintf(q, sizeof q, "%s█", m->query());
        int qx = LCD_WIDTH - gfx_text_width(UI_FONT, q) - 4;
        gfx_text(UI_FONT, qx, BODY_Y + 1, q, C_BLUE, GFX_TRANSPARENT);
        if (!m->query()[0]) /* nothing typed yet: say that typing searches */
            gfx_text(&font_small, qx - gfx_text_width(&font_small, "type to search") - 4, BODY_Y + 5,
                     "type to search", C_GRAY, GFX_TRANSPARENT);
    }
    if (sel < top)
        top = sel;
    if (sel >= top + MROWS)
        top = sel - MROWS + 1;
    char b[64];
    int n = mcount();
    if (sel >= n)
        sel = n ? n - 1 : 0;
    for (int r = 0; r < MROWS && top + r < n; r++) {
        int i = top + r;
        int y = BODY_Y + UI_CH + 4 + r * UI_CH;
        label_for(i, b, sizeof b);
        if (i == sel) {
            gfx_fill_rect(0, y, LCD_WIDTH, UI_CH, C_SEL_BG);
            gfx_text(UI_FONT, 4, y, b, C_WHITE, GFX_TRANSPARENT);
        } else {
            gfx_text(UI_FONT, 4, y, b, C_BLACK, GFX_TRANSPARENT);
        }
    }
    if (top > 0)
        gfx_text(UI_FONT, LCD_WIDTH - UI_CW - 2, BODY_Y + UI_CH + 4, "↑", C_GRAY, GFX_TRANSPARENT);
    if (top + MROWS < n)
        gfx_text(UI_FONT, LCD_WIDTH - UI_CW - 2, UI_BOTTOM - UI_CH, "↓", C_GRAY, GFX_TRANSPARENT);
    if (!n) { /* an empty list says why, and how to get entries back */
        const char *why = "(empty)";
        if (m->query && m->query()[0])
            why = "No match: backspace erases a letter.";
        gfx_text(UI_FONT == FONT ? &font_medium : FONT, 8, BODY_Y + UI_CH + 8, why, C_GRAY, GFX_TRANSPARENT);
    }
}

/* Choose item i: end the catalog's ALPHA lock, then either call on_select()
 * (which decides where to go next) or insert the item's text into the origin
 * screen's editor and return there. If the origin has no editor, the text
 * goes to the home screen's entry line and the home screen is shown. */
static void choose(int i)
{
    if (i < 0 || i >= mcount())
        return;
    menu_item_t item;
    item_at(i, &item);
    const menu_item_t *it = &item;
    if (m->alpha_jump)
        g.alpha = 0;
    if (m->on_select) {
        m->on_select(it);
        return;
    }
    const screen_t *dest = origin && origin->editor ? origin : ui_home();
    editor_t *e = dest->editor ? dest->editor() : NULL;
    if (e && it->insert)
        ed_insert_text(e, it->insert, it->suffix);
    ui_returning = true;
    ui_switch(dest);
    ui_returning = false;
}

/* Whether a page key can scroll the open menu: items above / below the visible
 * rows (`top` as menu_draw will clamp it to keep `sel` visible). The menu
 * bars show "▲ page" / "▼ page" only then (ui_softkey_label). */
void menu_page_room(bool *up, bool *down)
{
    *up = *down = false;
    if (ui_current() != &scr_menu || !m)
        return;
    int n = mcount(), t = top;
    if (sel < t)
        t = sel;
    if (sel >= t + MROWS)
        t = sel - MROWS + 1;
    *up = t > 0;
    *down = t + MROWS < n;
}

/* Keys of the menu screen (MENU, HELP and the other global keys were handled
 * before; F1/F2 arrive here because the default menu bar has no actions for
 * them). */
static void menu_key(int key, int mod)
{
    int n = mcount();
    if (key == KEY_UP) {
        sel = sel > 0 ? sel - 1 : (n ? n - 1 : 0);
    } else if (key == KEY_DOWN) {
        sel = sel < n - 1 ? sel + 1 : 0;
    } else if (key == KEY_ENTER) {
        choose(sel);
        return;
    } else if (key == KEY_F1 || key == KEY_F2) { /* soft keys: page up / down, when shown */
        bool up, down;
        menu_page_room(&up, &down);
        if (!(key == KEY_F1 ? up : down))
            return;
        sel += key == KEY_F1 ? -MROWS : MROWS;
        if (sel >= n)
            sel = n - 1;
        if (sel < 0)
            sel = 0;
    } else if (key == KEY_CLEAR) {
        menu_close();
        return;
    } else if (m->filter_key && m->filter_key(key, mod)) {
        sel = top = 0; /* the list was filtered again: best match first */
    } else if (m->filter_key) {
        return; /* a key the filter does not use: no letter jump, no digit choice */
    } else if (m->alpha_jump) {
        /* jump to the first item whose label starts with the key's letter
         * (case-insensitive); the menu is not wrapped around */
        char t[8];
        if (ui_key_text(key, MOD_ALPHA, 1, t, sizeof t) && t[0] >= 'a' && t[0] <= 'z') {
            for (int i = 0; i < n; i++) {
                menu_item_t it;
                item_at(i, &it);
                char c = it.label[0];
                if (c >= 'A' && c <= 'Z')
                    c = (char)(c - 'A' + 'a');
                if (c == t[0]) {
                    sel = i;
                    break;
                }
            }
        }
    } else {
        int idx = -1;
        static const uint8_t digit_keys[10] = {KEY_1, KEY_2, KEY_3, KEY_4, KEY_5,
                                               KEY_6, KEY_7, KEY_8, KEY_9, KEY_0};
        for (int i = 0; i < 10; i++)
            if (key == digit_keys[i])
                idx = i;
        /* ALPHA A..Z: items 11..36 (the "A:".."Z:" labels) */
        char t[8];
        if (idx < 0 && mod == MOD_ALPHA && ui_key_text(key, MOD_ALPHA, 1, t, sizeof t) && !t[1] &&
            ((t[0] >= 'a' && t[0] <= 'z') || (t[0] >= 'A' && t[0] <= 'Z')))
            idx = 10 + (t[0] | 0x20) - 'a';
        if (idx >= 0 && idx < n) {
            sel = idx;
            choose(idx);
            return;
        }
    }
    menu_draw();
}

/* Touch: tap an item to choose it; drag vertically to scroll one row per
 * UI_CH px of movement (ui_drag_list: finger up = later items); the
 * selection is pulled along to stay visible. */
static void menu_touch(const touch_event_t *ev)
{
    int list_y = BODY_Y + UI_CH + 4;
    switch (ev->type) {
    case TEV_TAP: {
        if (ev->y < list_y)
            break;
        int r = (ev->y - list_y) / UI_CH;
        if (r >= MROWS || top + r >= mcount())
            break;
        sel = top + r;
        choose(sel);
        return;
    }
    case TEV_DRAG:
        if (ui_drag_list(ev, UI_CH, &top, mcount(), MROWS, &sel))
            menu_draw();
        break;
    default:
        break;
    }
}

const screen_t scr_menu = {"menu", NULL, NULL, menu_draw, menu_key, NULL, NULL, menu_touch, NULL};
