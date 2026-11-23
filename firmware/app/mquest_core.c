/*
 * Math Quest, core side: the screen that hosts the "mathquest" code module
 * (modules/mathquest/, educational math games for grades 3 and 4; design in
 * docs/MATHQUEST.md), its APPS entry, the console `mq` and the fallback when
 * the module is not installed.
 *
 * Everything the player sees is drawn by the module, which runs in place from
 * the QSPI; this file only forwards: enter / key / touch / tick go to the
 * module as MODX_EV_ENTER, MODX_EV_KEY, MODX_EV_TOUCH and ops.tick, and the
 * soft-key labels come from it through the API (modx_api_t.softkey). The
 * labelled soft keys have no action of their own, so the framework delivers
 * them to key() as KEY_F1..KEY_F5 and they reach the module like any key.
 * The module asks to leave with modx_api_t.leave; the switch to the home screen
 * happens after the module call returned (never from inside module code).
 *
 * Without the module (not installed, core tier, `modx off mathquest`) APPS >
 * Math Quest shows "Install the Math Quest module (Memory > Modules)." and
 * nothing else changes. Saved progress lives in the core's "modx.save" blob
 * (app/modx.c), so it is kept while the module is absent.
 *
 * State: the soft-key labels and a pending leave request. Main loop only.
 */
#include <stdio.h>
#include <string.h>
#include "console.h"
#include "modx.h"
#include "registry.h"
#include "theme.h"
#include "ui.h"

int mq_console(int argc, char **argv);
void mq_state(void (*kv)(const char *, const char *, void *), void *user);

#if defined(CONFIG_CONTENT_QSPI)
#define HAVE_MODULES 1
#else
#define HAVE_MODULES 0
#endif

extern const screen_t scr_mquest;
static char sk_label[5][12];
static softkeys_t sk_bar;
static bool leave_req;

static modx_mod_t *mq(void) { return HAVE_MODULES ? modx_get(MQ_MODULE) : NULL; }

void modx_screen_softkey(int i, const char *label)
{
    if (i < 0 || i > 4)
        return;
    snprintf(sk_label[i], sizeof sk_label[i], "%s", label ? label : "");
    ui_softkeys_invalidate();
}

void modx_screen_leave(void) { leave_req = true; }

/* After every call into the module: a requested leave happens here. */
static void after(void)
{
    if (leave_req && ui_current() == &scr_mquest) {
        leave_req = false;
        ui_switch(ui_home());
    }
}

static const softkeys_t *mq_softkeys(void)
{
    for (int i = 0; i < 5; i++)
        sk_bar.key[i] = (softkey_t){sk_label[i][0] ? sk_label[i] : NULL};
    return &sk_bar;
}

static void mq_enter(void)
{
    leave_req = false;
    modx_mod_t *m = mq();
    if (m)
        modx_event(m, MODX_EV_ENTER, ui_returning ? 1 : 0);
}

static void mq_draw(void)
{
    modx_mod_t *m = mq();
    int rect[4];
    if (!m || !modx_draw(m, 0, rect)) { /* module gone while shown: say why */
        gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, TH_BG);
        gfx_text(&font_small, 8, BODY_Y + 8, modx_why(MQ_MODULE), TH_FG, -1);
    }
    after();
}

static void mq_key(int key, int mod)
{
    modx_mod_t *m = mq();
    if (!m) {
        ui_switch(ui_home());
        return;
    }
    char t[8];
    if (mod == MOD_ALPHA && ui_key_text(key, mod, g.alpha_snapshot_for_key, t, sizeof t) && t[0] >= ' ' &&
        (unsigned char)t[0] < 0x7F && !t[1] && modx_event(m, MODX_EV_TEXT, t[0])) { /* a text field took the letter */
        after();
        return;
    }
    modx_event(m, MODX_EV_KEY, key | mod << 8);
    after();
}

/* A typed character (the web pages: a computer keyboard's printable key): MODX_EV_TEXT; true if the
 * game's text field took it (else the page sends the key as a calculator key) */
bool mquest_text(int c);
bool mquest_text(int c)
{
    modx_mod_t *m = mq();
    if (!m || ui_current() != &scr_mquest || c < ' ' || c > 0x7E)
        return false;
    int r = modx_event(m, MODX_EV_TEXT, c);
    if (r)
        after();
    return r != 0;
}

static void mq_touch(const touch_event_t *ev)
{
    modx_mod_t *m = mq();
    if (m && ev->type == TEV_SWIPE) { /* MODX_EV_SWIPE: 0 left, 1 right, 2 up, 3 down */
        int d = touch_swipe_dir(ev);
        modx_event(m, MODX_EV_SWIPE, MODX_SWIPE(d == SWIPE_LEFT ? 0 : d == SWIPE_RIGHT ? 1 : d == SWIPE_UP ? 2 : 3,
                                                 ev->x0 < 0 ? 0 : ev->x0, ev->y0 < 0 ? 0 : ev->y0));
        after();
        return;
    }
    modx_set_touch_scale(ev->type == TEV_PINCH || ev->type == TEV_PINCH_END ? (int)(ev->scale * 256.0f + 0.5f) : 256);
    if (m)
        modx_event(m, MODX_EV_TOUCH, MODX_TOUCH(ev->type, ev->x0 < 0 ? 0 : ev->type == TEV_TAP ? ev->x0 : ev->x,
                                               ev->type == TEV_TAP ? ev->y0 : ev->y));
    after();
}

/* A scroll wheel over the game (the web pages: web_scroll): MODX_EV_SCROLL, dy notches at logical (x, y) */
void mquest_scroll(int dy, int x, int y);
void mquest_scroll(int dy, int x, int y)
{
    modx_mod_t *m = mq();
    if (!m || ui_current() != &scr_mquest || !dy)
        return;
    dy = dy < -127 ? -127 : dy > 127 ? 127 : dy;
    modx_event(m, MODX_EV_SCROLL, MODX_SCROLL(dy, x < 0 ? 0 : x > 1023 ? 1023 : x, y < 0 ? 0 : y > 1023 ? 1023 : y));
    after();
}

static void mq_tick(uint32_t now)
{
    modx_mod_t *m = mq();
    if (m)
        modx_tick(m, now, 0);
    after();
}

const screen_t scr_mquest = {"mathquest", "SPARKLE TOWN", mq_enter, mq_draw, mq_key, mq_tick, NULL, mq_touch,
                             "mathquest", NULL, mq_softkeys, SCREEN_GAME};

/* APPS > Math Quest: the game, or the message saying how to get it. */
static void mq_open(void)
{
    if (!mq()) {
        ui_message("MATH QUEST NOT INSTALLED", modx_why(MQ_MODULE));
        return;
    }
    ui_switch(&scr_mquest);
}
/* the kids see a game ("Sparkle Town"); "Math Quest" is the name for grown-ups (Memory > Modules, help) */
const app_def_t app_mathquest = {"mathquest", "Sparkle Town", mq_open, {{0}}, "mathquest"};

/* console `mq ...`: everything goes to the module (tests: seed, step, answer, ...). */
int mq_console(int argc, char **argv)
{
    modx_mod_t *m = mq();
    if (!m)
        return con_error("%s", modx_why(MQ_MODULE));
    int r = modx_console_mod(m, argc, argv);
    if (ui_current() == &scr_mquest)
        ui_draw_softkeys();
    after();
    return r;
}

/* `state` keys: mq_module (ok, or why not) and the module's own (mq_*). */
void mq_state(void (*kv)(const char *, const char *, void *), void *user)
{
    modx_mod_t *m = HAVE_MODULES ? modx_peek(MQ_MODULE) : NULL;
    kv("mq_module", m ? "ok" : HAVE_MODULES ? modx_why(MQ_MODULE) : "core tier", user);
    if (m)
        modx_state(m, kv, user);
}
