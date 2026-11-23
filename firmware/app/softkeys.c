/*
 * Soft-key bar (ui.h softkeys_t): the labels of F1..F5 at the bottom of the
 * display, the dispatch of F1..F5 (and of taps on the bar), and the MENU key's
 * context menu. Screens only declare their labels and actions. While the
 * context menu of a screen with an editor is open, the bar is the edit bar
 * of app/input_clip.c (undo redo copy paste hist), so MENU works as a prefix.
 *
 * Layout: the bar is SOFT_H (16) px high at SOFT_Y, split into five 64 px
 * cells (CELL_W) with labels centred in font_small; a key whose `screen` is
 * the current screen is highlighted (C_SEL_BG). While SHIFT is pending the
 * bar shows the SHIFT row in amber. The bar lies below the base clip that
 * gfx_reset_clip() restores, so screens cannot overwrite it; this file sets
 * its own clip to draw it.
 *
 * Redrawing is lazy: ui_draw_softkeys() runs after every key and touch event
 * (app.c) but repaints only when the labels, highlight or SHIFT state differ
 * from what was last drawn (`drawn`), or after ui_softkeys_invalidate().
 */
#include <stdio.h>
#include <string.h>
#include "g3d.h"
#include "input.h"
#include "registry.h"
#include "ui.h"

#define CELL_W (LCD_WIDTH / 5)

static const softkeys_t empty_bar; /* screens without soft keys */

/* ------------------------------------------------------------ graph screens */
/* "trace": go to the graph (if not there) and let it start TRACE, which the
 * graph screen does on its own F4. */
static void trace(void)
{
    if (ui_current() != &scr_graph)
        ui_switch(&scr_graph);
    scr_graph.key(KEY_F4, MOD_NONE); /* the graph screen starts TRACE */
}
static void zoom(void) { menu_open(&menu_zoom); }
/* "graph": the graph screen; graphing from the home screen puts the graph into its notebook */
/* "graph"; on the GRAPH screen itself the key reads "back" and returns to the home notebook
 * (writing a bound graph cell back, as HOME does: home.c home_enter). */
static void graph(void)
{
    if (ui_current() == &scr_graph) {
        ui_switch(ui_home());
        return;
    }
    if (!home_graph_embed(false) && ui_current() != &scr_graph)
        ui_switch(&scr_graph);
}
static void open_app(const char *id)
{
    const app_def_t *a = app_find(id);
    if (a)
        a->open();
}
static void plots(void) { open_app("statplot"); }
static void calc(void) { open_app("calc"); }
static void format(void) { mode_open_at("GRID"); } /* SETTINGS at the graph rows (GRID, AXES) */

/* Shared by Y=, WINDOW, GRAPH, TABLE, TBLSET and the slider settings, so the
 * graphing screens are always one key apart. "plots" and "calc" open the
 * registered apps "statplot" (stat_menu.c) and "calc" (gr_calc.c). */
const softkeys_t softkeys_graph = {
    {{"y=", NULL, &scr_yeq}, {"window", NULL, &scr_window}, {"zoom", zoom}, {"trace", trace},
     {"graph", graph, &scr_graph}},
    {{"plots", plots}, {"tblset", NULL, &scr_tblset}, {"format", format}, {"calc", calc},
     {"table", NULL, &scr_table}},
};

/* ------------------------------------------------------------ menus */
static void menu_help(void) { help_context(); }
/* Default bar of every menu: the page keys have no action, so they reach
 * menu.c's key() as KEY_F1 / KEY_F2. */
static const softkeys_t menu_bar = {
    {{"▲ page"}, {"▼ page"}, {NULL}, {"help", menu_help}, {"back", menu_close}},
};

/* The label a soft key shows: `label`, except that the page keys ("▲ page",
 * "▼ page", in every menu bar and in help) show only when they can scroll
 * (menu_page_room, help_page_room): a list that fits on one page has neither. */
const char *ui_softkey_label(const char *label)
{
    if (!label)
        return NULL;
    if (!strcmp(label, "graph") && (ui_current() == &scr_graph || ui_current() == &scr_g3d))
        return "back"; /* on the graph screens themselves: back to the notebook (ui_softkey) */
    bool pg_up = !strcmp(label, "▲ page"), pg_down = !strcmp(label, "▼ page");
    if (!pg_up && !pg_down)
        return label;
    bool up = true, down = true;
    if (ui_current() == &scr_menu)
        menu_page_room(&up, &down);
    else if (ui_current() == &scr_help)
        help_page_room(&up, &down);
    return (pg_up ? up : down) ? label : NULL;
}

static const menu_def_t *ctx_shown; /* the menu the MENU key opened last */

/* The bar in effect now: an open menu's own bar; for the MENU key's context
 * menu on a screen with an editor the edit bar (undo redo copy paste hist,
 * app/input_clip.c); else the default menu bar; on other screens their
 * softkeys_fn() / softkeys, else an empty bar. Never NULL. */
const softkeys_t *ui_softkeys(void)
{
    const screen_t *s = ui_current();
    if (!s)
        return &empty_bar;
    if (s == &scr_menu) {
        const menu_def_t *m = menu_current();
        if (m && m->softkeys)
            return m->softkeys;
        const softkeys_t *eb = m && m == ctx_shown ? input_context_bar(menu_origin()) : NULL;
        return eb ? eb : &menu_bar;
    }
    const softkeys_t *sk = s->softkeys_fn ? s->softkeys_fn() : s->softkeys;
    return sk ? sk : &empty_bar;
}

/* ------------------------------------------------------------ drawing */
/* What the bar shows now. Labels are compared by pointer: they are string
 * constants of the softkeys_t tables, so a different table with the same
 * text still counts as a change (one harmless extra repaint). */
static struct {
    bool valid;
    bool shift;
    const char *label[5];
    bool active[5];
    bool mark[5];
} drawn;

void ui_softkeys_invalidate(void) { drawn.valid = false; }

/* The game style (SCREEN_GAME, Sparkle Town): no calculator chrome, the keys are candy buttons.
 * A label whose first byte is 1..15 has an icon (bulb, wand, house, book, star, friends, key,
 * bag, door, undo, sparkle, camera, heart, bow, tick) before its word; F1..F5 work as always. */
static const char game_icon[15][12][13] = {
    /* 1 bulb */ {"....kkkk....", "...kyyyyk...", "..kywyyyyk..", "..kwyyyyyk..", "..kyyyyyyk..", "..kyyyyyyk..", "...kyyyyk...", "....kyyk....", "....kllk....", "....kllk....", ".....kk.....", "............"},
    /* 2 wand */ {".........y..", "........yyy.", ".....y...y..", "....yky.....", ".....k......", "....k.......", "...k........", "..k.....y...", ".k.....yyy..", "k.......y...", "............", "............"},
    /* 3 house */ {".....kk.....", "....kppk....", "...kppppk...", "..kppppppk..", ".kppppppppk.", "kkkkkkkkkkkk", ".kwwwwwwwwk.", ".kwwwkkwwwk.", ".kwwwkbkwwk.", ".kwwwkbkwwk.", ".kkkkkkkkkk.", "............"},
    /* 4 book */ {"............", ".kkkkkkkkkk.", ".kllllklllk.", ".klwwlklwwk.", ".kllllklllk.", ".klwwlklwwk.", ".kllllklllk.", ".kllllklllk.", ".kkkkkkkkkk.", "............", "............", "............"},
    /* 5 star */ {".....kk.....", ".....yy.....", "....kyyk....", "kkkkyyyykkkk", ".kyyyyyyyyk.", "..kyyyyyyk..", "...kyyyyk...", "..kyyyyyyk..", "..kyykkyyk..", ".kyk....kyk.", ".kk......kk.", "............"},
    /* 6 friends */ {"............", "..kk....kk..", ".kppk..kssk.", ".kppk..kssk.", "..kk....kk..", ".kppk..kssk.", "kppppkksssk.", "kppppkksssk.", "kkkkkkkkkkk.", "............", "............", "............"},
    /* 7 key */ {"............", "..kkk.......", ".kyyyk......", "kyy.yyk.....", "kyy.yykkkkk.", "kyyyyyyyyyk.", ".kyyykkkyky.", "..kkk..kk.k.", "............", "............", "............", "............"},
    /* 8 bag */ {"....kkkk....", "...k....k...", "...k....k...", ".kkkkkkkkkk.", ".kppppppppk.", ".kppwwppppk.", ".kppppppppk.", ".kppppppppk.", ".kppppppppk.", ".kkkkkkkkkk.", "............", "............"},
    /* 9 door */ {"...kkkkkk...", "..kbbbbbbk..", "..kbbbbbbk..", "..kbbbbbbk..", "..kbbbbbyk..", "..kbbbbbbk..", "..kbbbbbbk..", "..kbbbbbbk..", ".kkkkkkkkkk.", "............", "............", "............"},
    /* 10 undo */ {"............", "...k........", "..kk........", ".kkkkkkkk...", "..kk.....k..", "...k......k.", "..........k.", ".........k..", "....kkkkk...", "............", "............", "............"},
    /* 11 sparkle */ {".....y......", ".....y......", "....yyy.....", "yyyyywyyyy..", "....yyy...p.", ".....y...ppp", ".....y....p.", "..p.........", ".ppp...y....", "..p...yyy...", ".......y....", "............"},
    /* 12 camera */ {"............", "....kkk.....", ".kkkkkkkkkk.", ".kllllllllk.", ".kllkkklllk.", ".kllkwkllyk.", ".kllkkklllk.", ".kllllllllk.", ".kkkkkkkkkk.", "............", "............", "............"},
    /* 13 heart */ {"............", ".kkk..kkk...", "kpppkkpppk..", "kpwpppppppk.", "kppppppppk..", ".kppppppk...", "..kppppk....", "...kppk.....", "....kk......", "............", "............", "............"},
    /* 14 bow */ {"............", "kk......kk..", "kpk....kpk..", "kppk..kppk..", "kpppkkpppk..", "kpppkkpppk..", "kppk..kppk..", "kpk....kpk..", "kk......kk..", "............", "............", "............"},
    /* 15 tick */ {"............", "..........k.", ".........kk.", "........kk..", ".k.....kk...", ".kk...kk....", "..kk.kk.....", "...kkk......", "....k.......", "............", "............", "............"},
};
static uint16_t game_icon_col(char c)
{
    switch (c) {
    case 'k': return GAME_BAR_INK;
    case 'y': return 0xFF30; /* RGB(255, 230, 128) */
    case 'w': return 0xFFFF;
    case 'p': return 0xFD98; /* RGB(255, 179, 199) */
    case 'b': return 0xB42C; /* RGB(176, 133, 96) */
    case 'l': return 0xCDBF; /* RGB(201, 182, 255) */
    case 's': return 0x9EDF; /* RGB(159, 216, 255) */
    default: return 0;
    }
}
static void game_icon_draw(int icon, int x, int y)
{
    const char (*rows)[13] = game_icon[(icon - 1) % 15];
    for (int r = 0; r < 12; r++)
        for (int c = 0; c < 12;) {
            char v = rows[r][c];
            int c1 = c + 1;
            while (c1 < 12 && rows[r][c1] == v)
                c1++;
            if (v != '.')
                gfx_fill_rect(x + c, y + r, c1 - c, 1, game_icon_col(v));
            c = c1;
        }
}
static void game_button(int i, const char *label)
{
    static const uint16_t cand[5] = {0xFDDB, 0xAF39, 0xAEDF, 0xD63F, 0xFEB5}; /* pink, mint, sky, lavender, peach */
    static const uint16_t edge[5] = {0xF3F4, 0x5DF1, 0x5D5C, 0x9BFC, 0xF50B};
    const font_t *f = &font_small;
    int x = i * CELL_W + 1, w = CELL_W - 2, icon = (unsigned char)label[0] >= 1 && (unsigned char)label[0] <= 15 ? label[0] : 0;
    const char *t = icon ? label + 1 : label;
    /* a candy button: rounded, a darker edge below, a shine on top */
    gfx_fill_rect(x + 2, SOFT_Y + 1, w - 4, SOFT_H - 1, edge[i]);
    gfx_fill_rect(x, SOFT_Y + 3, w, SOFT_H - 5, edge[i]);
    gfx_fill_rect(x + 2, SOFT_Y + 1, w - 4, SOFT_H - 3, cand[i]);
    gfx_fill_rect(x, SOFT_Y + 3, w, SOFT_H - 7, cand[i]);
    gfx_fill_rect(x + 4, SOFT_Y + 2, w - 8, 1, 0xFFFF);
    int tw = gfx_text_width(f, t), iw = icon ? 12 : 0, x0 = x + (w - tw - iw) / 2;
    if (icon)
        game_icon_draw(icon, x0, SOFT_Y + 2);
    gfx_text(f, x0 + iw, SOFT_Y + 1, t, GAME_BAR_INK, GFX_TRANSPARENT);
}

/* Repaint the bar if what it should show changed (see the file comment). */
void ui_draw_softkeys(void)
{
    const softkeys_t *sk = ui_softkeys();
    bool shift = g.shift != 0;
    const softkey_t *row = shift ? sk->shift : sk->key;
    const char *label[5];
    bool active[5], mark[5];
    for (int i = 0; i < 5; i++) {
        label[i] = ui_softkey_label(row[i].label);
        active[i] = (row[i].screen && row[i].screen == ui_current()) || (row[i].on && row[i].on());
        /* amber corner: this key also has a SHIFT function (shown while SHIFT is pending) */
        mark[i] = !shift && sk->shift[i].label && sk->shift[i].label[0];
    }
    if (drawn.valid && drawn.shift == shift && !memcmp(drawn.label, label, sizeof label) &&
        !memcmp(drawn.active, active, sizeof active) && !memcmp(drawn.mark, mark, sizeof mark))
        return;
    drawn.valid = true;
    drawn.shift = shift;
    memcpy(drawn.label, label, sizeof label);
    memcpy(drawn.active, active, sizeof active);
    memcpy(drawn.mark, mark, sizeof mark);

    gfx_set_clip(0, SOFT_Y, LCD_WIDTH, SOFT_H);
    const font_t *f = &font_small;
    if (ui_current() && ui_current()->style == SCREEN_GAME) { /* a game: pastel pills, no calculator chrome */
        gfx_fill_rect(0, SOFT_Y, LCD_WIDTH, SOFT_H, GAME_BAR_BG);
        for (int i = 0; i < 5; i++)
            if (label[i] && label[i][0])
                game_button(i, label[i]);
        gfx_reset_clip();
        return;
    }
    gfx_fill_rect(0, SOFT_Y, LCD_WIDTH, SOFT_H, C_STATUS_BG);
    for (int i = 0; i < 5; i++) {
        int x = i * CELL_W;
        if (!label[i] || !label[i][0])
            continue;
        uint16_t bg = active[i] ? C_SEL_BG : C_SOFT_BG;
        gfx_fill_rect(x + 1, SOFT_Y + 1, CELL_W - 2, SOFT_H - 1, bg);
        int w = gfx_text_width(f, label[i]);
        gfx_text(f, x + (CELL_W - w) / 2, SOFT_Y + 2, label[i], shift ? C_AMBER : C_WHITE, GFX_TRANSPARENT);
    }
    for (int i = 0; i < 5; i++) /* 4 px amber triangle in the top-right corner of the cell */
        if (mark[i])
            for (int d = 0; d < 4; d++)
                gfx_fill_rect((i + 1) * CELL_W - 2 - (4 - d), SOFT_Y + 1 + d, 4 - d, 1, C_AMBER);
    gfx_reset_clip();
}

/* ------------------------------------------------------------ dispatch */
/* Press soft key i (0..4 = F1..F5) in layer mod (MOD_SHIFT: the SHIFT row;
 * anything else: the plain row). Runs the key's fn, or switches to its screen
 * (nothing if already there), or passes KEY_F1 + i with mod to the current
 * screen's key(). Returns false for a blank (NULL-label) key, which does
 * nothing. Called for F1..F5 key presses, taps on the bar and the MENU
 * context menu. */
bool ui_softkey(int i, int mod)
{
    if (i < 0 || i > 4)
        return false;
    const softkeys_t *sk = ui_softkeys();
    const softkey_t *k = mod == MOD_SHIFT ? &sk->shift[i] : &sk->key[i];
    if (!k->label)
        return false;
    const screen_t *cur = ui_current();
    if (k->fn)
        k->fn();
    else if (k->screen) {
        if (k->screen != cur)
            ui_switch(k->screen);
        else if (cur == &scr_g3d) /* "graph" on the 3D graph itself reads "back" */
            ui_switch(ui_home());
    } else if (cur && cur->key)
        cur->key(KEY_F1 + i, mod);
    return true;
}

/* ------------------------------------------------------------ MENU
 * The generated context menu: what can be done on this screen.
 *
 * On a screen with an editor the bar of the open menu is the edit bar
 * (input_context_bar: undo redo copy paste history; SHIFT cut clips), so the
 * list starts with those actions and their keys as they work now ("undo  F1"
 * = press F1), lined up with the bar below. The screen's own soft keys follow
 * without key names: inside the menu F1..F5 are the edit bar, so "y=  F1"
 * would be wrong there (they are on the screen's bar after leaving, and an
 * amber mark there shows which have a SHIFT function). On screens without an
 * editor the bar is the plain menu bar and the soft keys keep their names
 * ("trace  F4", "plots  SHIFT F1"). "Help  SHIFT MENU" is always last.
 *
 * It is a dynamic menu over a snapshot of the bars taken when MENU was
 * pressed (ctx_sk, ctx_eb, ctx_map). Choosing a soft-key item closes the menu
 * first so the key acts on the screen underneath; an edit item runs the edit
 * bar's action, which works on the origin and closes the menu itself. */
enum { CTX_HELP = 10, CTX_EDIT = 20 }; /* ctx_map: 0..4 plain, 5..9 SHIFT, 10 help, 20 + 0..9 edit bar */
static const softkeys_t *ctx_sk; /* bar of the screen the menu was opened on */
static const softkeys_t *ctx_eb; /* its edit bar, NULL if the screen has no editor */
static int ctx_n;                /* number of items */
static uint8_t ctx_map[21];

static int ctx_count(void) { return ctx_n; }

static void ctx_get(int i, menu_item_t *out)
{
    static char b[48];
    int k = ctx_map[i];
    const char *help = "softkeys";
    if (k == CTX_HELP) {
        snprintf(b, sizeof b, "Help      SHIFT MENU");
        help = "help";
    } else if (k >= CTX_EDIT) {
        int e = k - CTX_EDIT;
        const char *l = e < 5 ? ctx_eb->key[e].label : ctx_eb->shift[e - 5].label;
        int pad = 10 - utf8_len(l);
        snprintf(b, sizeof b, "%s%*s%sF%d", l, pad > 1 ? pad : 1, "", e < 5 ? "" : "SHIFT ", e % 5 + 1);
        help = "edit-bar";
    } else {
        const char *l = k < 5 ? ctx_sk->key[k].label : ctx_sk->shift[k - 5].label;
        int pad = 10 - utf8_len(l); /* align the key names (labels may be UTF-8) */
        if (ctx_eb)
            snprintf(b, sizeof b, "%s", l);
        else
            snprintf(b, sizeof b, "%s%*s%sF%d", l, pad > 1 ? pad : 1, "", k < 5 ? "" : "SHIFT ", k % 5 + 1);
    }
    *out = (menu_item_t){b, NULL, NULL, k, help};
}

static void ctx_select(const menu_item_t *it)
{
    int k = it->action;
    if (k >= CTX_EDIT) { /* the edit bar is the open menu's bar: press its key */
        ui_softkey((k - CTX_EDIT) % 5, k - CTX_EDIT < 5 ? MOD_NONE : MOD_SHIFT);
        return;
    }
    menu_close();
    if (k == CTX_HELP)
        help_context();
    else
        ui_softkey(k % 5, k < 5 ? MOD_NONE : MOD_SHIFT);
}

static const menu_def_t menu_context = {"MENU", NULL, 0, ctx_select, false, "softkeys", ctx_count, ctx_get};

/* MENU key: close an open menu, else open the bar's own menu (softkeys_t.menu)
 * or the generated one. */
void ui_context_menu(void)
{
    if (ui_current() == &scr_menu) { /* MENU again: close the menu */
        menu_close();
        return;
    }
    const softkeys_t *sk = ui_softkeys();
    if (sk->menu) {
        ctx_shown = sk->menu;
        menu_open(sk->menu);
        return;
    }
    ctx_sk = sk;
    ctx_eb = input_context_bar(ui_current());
    ctx_n = 0;
    for (int k = 0; ctx_eb && k < 10; k++) { /* the edit bar first, without its help and back */
        const softkey_t *s = k < 5 ? &ctx_eb->key[k] : &ctx_eb->shift[k - 5];
        if (s->label && s->label[0] && s->fn != menu_close && strcmp(s->label, "help"))
            ctx_map[ctx_n++] = (uint8_t)(CTX_EDIT + k);
    }
    for (int k = 0; k < 10; k++) {
        const softkey_t *s = k < 5 ? &sk->key[k] : &sk->shift[k - 5];
        if (s->label && s->label[0] && strcmp(s->label, "help")) /* "Help" is the last item anyway */
            ctx_map[ctx_n++] = (uint8_t)k;
    }
    ctx_map[ctx_n++] = CTX_HELP;
    ctx_shown = &menu_context;
    menu_open(&menu_context);
}
