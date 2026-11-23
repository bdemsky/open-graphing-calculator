/* Sparkle Town (modules/mathquest): the widgets (mq_widget.c) and the gesture harness (the audit's C1, C2,
 * C5 and its widget proposal):
 *   - every kind in a blank scene, driven with every gesture: a tap on its centre and its edges, one
 *     pixel outside its hit area, a press-drag-lift, a flick, a scroll of +-1 and +-5, the keys (arrows,
 *     digits, ENTER, DEL): the value changes as it should, the event says why, the hit area is at least
 *     MQ_WG_HIT, the pressed state is drawn, the focus moves with the arrows and is drawn, and the demo's
 *     ghost hand starts inside the widget;
 *   - every game of the round with an order it tells: its widgets all have big enough hit areas, a fuzz of
 *     taps, drags, flicks and scrolls breaks nothing, and the right answer can be given by touch only, by
 *     the keys only, and with a mouse and its wheel (clicks and scrolls) only;
 *   - Treasure Heist's wheels: tap the arrows, tap above or below, drag, flick, scroll, keys; the value
 *     entered stays after a wrong one.
 * Built and run by `make test`. */
#include "mq_test_util.h"

#define FY (MQ_Y0 + 58)
#define FB (MQ_Y0 + MQ_H)
enum { K_DEL = 8, K_UP = 12, K_LEFT = 13, K_RIGHT = 14, K_DOWN = 15, K_ENTER = 49 };
enum { T_DOWN = 1, T_DRAG, T_UP, T_TAP };

static uint32_t fz = 99;
static int rnd(int n)
{
    fz = fz * 1103515245u + 12345u;
    return (int)((fz >> 8) % (uint32_t)(n > 0 ? n : 1));
}

/* the pixels of a box differ between two frames */
static uint16_t snap[320 * 240];
static int changed(int x, int y, int w, int h)
{
    int n = 0;
    for (int j = y < 0 ? 0 : y; j < y + h && j < 240; j++)
        for (int i = x < 0 ? 0 : x; i < x + w && i < 320; i++)
            n += snap[j * 320 + i] != fb[j * 320 + i];
    return n;
}

static void test_kinds(void)
{
    con("new Wil 2");
    mq_wev_t ev;
    /* ---- a button: tap anywhere in its hit area (bigger than its drawing), never one pixel outside */
    mq_wg_begin(q);
    int b = mq_wg_add(q, WG_BUTTON, 1, 0, 100, 100, 30, 20);
    mq_wg_label(q, b, "go", Q_MINT);
    int hx, hy, hw, hh;
    mq_wg_hitbox(mq_wg(q, b), &hx, &hy, &hw, &hh);
    CHECK(hw >= MQ_WG_HIT && hh >= MQ_WG_HIT);
    CHECK(mq_wg_touch(q, T_TAP, 115, 110, &ev) && ev.why == WHY_TAP && ev.id == 1);
    CHECK(mq_wg_touch(q, T_TAP, hx, hy, &ev) && ev.why == WHY_TAP);
    CHECK(mq_wg_touch(q, T_TAP, hx + hw - 1, hy + hh - 1, &ev) && ev.why == WHY_TAP);
    CHECK(!mq_wg_touch(q, T_TAP, hx - 1, hy, &ev) && ev.why == WHY_NONE);
    CHECK(!mq_wg_touch(q, T_TAP, hx, hy + hh, &ev));
    /* pressed: drawn differently while the finger is on it */
    gfx_fill_rect(0, 0, 320, 240, 0), mq_wg_draw(q), memcpy(snap, fb, sizeof snap);
    mq_wg_touch(q, T_DOWN, 115, 110, &ev);
    gfx_fill_rect(0, 0, 320, 240, 0), mq_wg_draw(q);
    CHECK(changed(100, 100, 30, 24) > 10);
    mq_wg_touch(q, T_UP, 115, 110, &ev);
    /* ENTER on the focused one; the demo's hand starts on it */
    mq_wg_focus(q, b);
    CHECK(mq_wg_key(q, K_ENTER, 0, &ev) && ev.why == WHY_KEY && ev.id == 1);

    /* ---- two buttons side by side: the nearest centre wins where the hit areas overlap */
    mq_wg_begin(q);
    int l = mq_wg_add(q, WG_BUTTON, 1, 0, 100, 100, 30, 30), r = mq_wg_add(q, WG_BUTTON, 2, 0, 132, 100, 30, 30);
    CHECK(mq_wg_at(q, 120, 115) == l && mq_wg_at(q, 140, 115) == r && mq_wg_at(q, 131, 92) >= 0);
    /* the arrows move the focus, which is drawn */
    mq_wg_focus(q, l);
    CHECK(mq_wg_key(q, K_RIGHT, 0, &ev) && mq_wg_focused(q) == r);
    CHECK(mq_wg_key(q, K_LEFT, 0, &ev) && mq_wg_focused(q) == l);
    mq_wgs(q)->kbd = false;
    gfx_fill_rect(0, 0, 320, 240, 0), mq_wg_draw(q), memcpy(snap, fb, sizeof snap);
    mq_wgs(q)->kbd = true;
    gfx_fill_rect(0, 0, 320, 240, 0), mq_wg_draw(q);
    CHECK(changed(94, 94, 42, 42) > 20); /* the focus ring */

    /* ---- a wheel: tap above / below, drag, flick, scroll, keys, digits move on, DEL */
    mq_wg_begin(q);
    int w0 = mq_wg_add(q, WG_WHEEL, 3, 0, 100, 80, 36, 64), w1 = mq_wg_add(q, WG_WHEEL, 3, 1, 140, 80, 36, 64);
    mq_wg_range(q, w0, 5, 0, 9, 1), mq_wg_range(q, w1, 0, 0, 9, 1);
    CHECK(mq_wg_touch(q, T_TAP, 118, 86, &ev) && ev.why == WHY_TAP && ev.value == 6);  /* its up arrow */
    CHECK(mq_wg_touch(q, T_TAP, 118, 140, &ev) && ev.value == 5);                     /* its down arrow */
    CHECK(mq_wg_touch(q, T_TAP, 96, 86, &ev) && ev.value == 6);                       /* beside it (its hit area) */
    mq_wg(q, w0)->value = 5;
    mq_wg_touch(q, T_DOWN, 118, 112, &ev);
    CHECK(mq_wg_touch(q, T_DRAG, 118, 84, &ev) && ev.why == WHY_DRAG && ev.value == 7); /* 28 px up: two */
    mq_wg_touch(q, T_UP, 118, 84, &ev);
    mq_wg(q, w0)->value = 5;
    CHECK(mq_wg_swipe(q, 2, 118, 112, &ev) && ev.why == WHY_FLICK && ev.value == 8);   /* a flick up: three */
    mq_wg(q, w0)->value = 5;
    CHECK(mq_wg_scroll(q, -1, 118, 112, &ev) && ev.why == WHY_SCROLL && ev.value == 6);
    CHECK(mq_wg_scroll(q, 5, 118, 112, &ev) && ev.value == 1);
    CHECK(mq_wg_scroll(q, -5, 118, 112, &ev) && ev.value == 6);
    mq_wg_focus(q, w0);
    CHECK(mq_wg_key(q, K_UP, 0, &ev) && ev.value == 7 && ev.why == WHY_KEY);
    CHECK(mq_wg_key(q, K_DOWN, 0, &ev) && ev.value == 6);
    CHECK(mq_wg_key(q, 0, '3', &ev) && ev.value == 3 && mq_wg_focused(q) == w1); /* a digit, then the next wheel */
    CHECK(mq_wg_key(q, K_DEL, 0, &ev) && ev.value == 0);
    CHECK(mq_wg_key(q, K_LEFT, 0, &ev) && mq_wg_focused(q) == w0);
    gfx_fill_rect(0, 0, 320, 240, 0), mq_wg_draw(q);
    CHECK(fb[(80 + 8) * 320 + 118] != 0); /* its drawn arrows */

    /* ---- a slider: tap a spot, drag (its swipe no step more), scroll, LEFT / RIGHT; a dial by its angle */
    mq_wg_begin(q);
    int s = mq_wg_add(q, WG_SLIDER, 4, 0, 40, 100, 200, 20);
    mq_wg_range(q, s, 0, 0, 10, 1);
    CHECK(mq_wg_touch(q, T_TAP, 140, 110, &ev) && ev.value == 5);
    mq_wg(q, s)->value = 5;
    mq_wg_touch(q, T_DOWN, 140, 110, &ev);
    CHECK(mq_wg_touch(q, T_DRAG, 220, 110, &ev) && ev.value == 9);
    mq_wg_touch(q, T_UP, 220, 110, &ev);
    CHECK(!mq_wg_swipe(q, 1, 140, 110, &ev)); /* the drag set it: the swipe after its UP is no extra step (Hop Along) */
    CHECK(mq_wg_scroll(q, 2, 140, 110, &ev) && ev.value == 7);
    mq_wg_focus(q, s);
    CHECK(mq_wg_key(q, K_RIGHT, 0, &ev) && ev.value == 6 && mq_wg_key(q, K_LEFT, 0, &ev) && ev.value == 4);
    mq_wg_begin(q);
    int d = mq_wg_add(q, WG_SLIDER, 5, 0, 60, 60, 200, 100);
    mq_wg_range(q, d, 0, 0, 180, 1), mq_wg_flags(q, d, WF_DIAL);
    CHECK(mq_wg_touch(q, T_TAP, 160, 62, &ev) && ev.value >= 85 && ev.value <= 95); /* straight up: 90 */
    CHECK(mq_wg_touch(q, T_TAP, 250, 158, &ev) && ev.value <= 5);                   /* to the right: 0 */

    /* ---- carrying: press-drag-lift onto a target (it glows), or tap it then tap the target, or the keys */
    mq_wg_begin(q);
    int it = mq_wg_add(q, WG_DRAG, 6, 0, 20, 40, 60, 40), t0 = mq_wg_add(q, WG_DROP, 7, 0, 20, 160, 80, 48),
        t1 = mq_wg_add(q, WG_DROP, 7, 1, 200, 160, 80, 48);
    (void)t0;
    mq_wg_touch(q, T_DOWN, 50, 60, &ev);
    mq_wg_touch(q, T_DRAG, 150, 120, &ev);
    CHECK(mq_wg_touch(q, T_DRAG, 240, 180, &ev) && ev.why == WHY_DRAG && ev.target == t1);
    int cidx, cx, cy;
    CHECK(mq_wg_carrying(q, &cidx, &cx, &cy) && cidx == it && cx == 240 && mq_wg_hover(q) == t1);
    CHECK(mq_wg_touch(q, T_UP, 240, 180, &ev) && ev.why == WHY_DROP && ev.value == 1 && ev.idx == it);
    mq_wg_touch(q, T_DOWN, 50, 60, &ev);
    CHECK(mq_wg_touch(q, T_UP, 52, 61, &ev) && ev.why == WHY_RELEASE); /* (not moved: no drop) */
    CHECK(mq_wg_touch(q, T_TAP, 50, 60, &ev) && ev.why == WHY_PRESS && mq_wg_picked(q) == it);
    CHECK(mq_wg_touch(q, T_TAP, 60, 180, &ev) && ev.why == WHY_DROP && ev.value == 0);
    mq_wg_focus(q, it);
    CHECK(mq_wg_key(q, K_ENTER, 0, &ev) && ev.why == WHY_PRESS);
    CHECK(mq_wg_key(q, K_RIGHT, 0, &ev) && mq_wg_key(q, K_ENTER, 0, &ev) && ev.why == WHY_DROP);

    /* ---- keys of a pad: a tap, or the same key on the keyboard */
    mq_wg_begin(q);
    for (int k = 0; k < 10; k++) {
        char lb[2] = {(char)('0' + k), 0};
        int i = mq_wg_add(q, WG_KEY, 8, '0' + k, 4 + (k % 5) * 50, 100 + (k / 5) * 40, 46, 36);
        mq_wg_label(q, i, lb, Q_WHITE);
    }
    CHECK(mq_wg_touch(q, T_TAP, 4 + 2 * 50 + 20, 118, &ev) && ev.arg == '2');
    CHECK(mq_wg_key(q, 0, '7', &ev) && ev.why == WHY_KEY && ev.arg == '7');

    /* ---- clock hands: the hand nearest the finger's angle moves; scroll and keys step */
    mq_wg_begin(q);
    int mh = mq_wg_add(q, WG_CLOCKHAND, 9, 1, 50, 50, 140, 140), hh2 = mq_wg_add(q, WG_CLOCKHAND, 9, 2, 50, 50, 140, 140);
    mq_wg_range(q, mh, 0, 0, 59, 1), mq_wg_range(q, hh2, 3, 0, 11, 1); /* 3:00: the minute hand up, the hour to the right */
    mq_wg_touch(q, T_DOWN, 120, 60, &ev);
    CHECK(ev.idx == mh && ev.why == WHY_DRAG);
    mq_wg_touch(q, T_DOWN, 185, 120, &ev);
    CHECK(ev.idx == hh2 && ev.value == 3);
    mq_wg_touch(q, T_UP, 185, 120, &ev);
    CHECK(mq_wg_scroll(q, 5, 120, 60, &ev) && ev.value == 5 && ev.idx == mh);

    /* ---- steering: drag, tap a side steps, flick to the far side, keys (not the scroll wheel) */
    mq_wg_begin(q);
    int st = mq_wg_add(q, WG_STEER, 10, 0, 20, 100, 280, 60);
    mq_wg_range(q, st, 160, 20, 300, 18);
    CHECK(mq_wg_touch(q, T_TAP, 280, 130, &ev) && ev.value == 178);
    CHECK(mq_wg_swipe(q, 0, 160, 130, &ev) && ev.value == 20);
    CHECK(!mq_wg_scroll(q, 1, 160, 130, &ev)); /* the wheel never steers (the Mac playtest) */
    mq_wg_focus(q, st);
    CHECK(mq_wg_key(q, K_LEFT, 0, &ev) && ev.value == 142);
    mq_wg_touch(q, T_DOWN, 100, 130, &ev);
    CHECK(ev.value == 100);

    /* ---- every kind's demo: the ghost hand starts inside its hit area */
    for (int k = 0; k < WG_COUNT; k++) {
        mq_wg_begin(q);
        int i = mq_wg_add(q, k, 1, 0, 100, 100, 60, 60);
        mq_wg_hitbox(mq_wg(q, i), &hx, &hy, &hw, &hh);
        gfx_fill_rect(0, 0, 320, 240, 0);
        memcpy(snap, fb, sizeof snap);
        mq_wg_demo(q, i, 0);
        CHECK(changed(hx - 12, hy - 4, hw + 24, hh + 30) > 10);
    }
}

/* ------------------------------------------------------------ every game */
static bool order_in(int skill, int rung, int mech)
{
    char b[48];
    snprintf(b, sizeof b, "skill %d %d", skill, rung);
    con(b);
    q->fun_ms = 0, q->rapid = 0, q->guess_n = 0, q->mash_t = 0; /* (a test answers at machine speed) */
    for (int w = 0; w < WS_COUNT; w++) /* (the games' own controls: the notebook waits for its button) */
        mq_pace(q)->ws_level[w] = 2;
    for (int i = 0; i < 200; i++) {
        mq_generate_fresh(q, &q->prob, skill, rung);
        if (mq_mech_fits(mech, &q->prob)) {
            q->gm.round_mech = (uint8_t)mech;
            q->tried_wrong = false, q->used_help = false;
            q->run_i = 0; /* (an order to pick from, not one she makes) */
            mq_game_order(q);
            q->gm.mod = 0, q->gm.demo_t = 0;
            return q->gm.mech == mech && q->sub == GS_PLAY;
        }
    }
    return false;
}
static void tev(int type, int x, int y) { ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(type, x, y)); }
static void key(int k) { ops.event(q, MODX_EV_KEY, k); }
static void wheel(int dy, int x, int y) { ops.event(q, MODX_EV_SCROLL, MODX_SCROLL(dy, x, y)); }
static void typ(const char *s)
{
    for (; *s; s++)
        mq_game_key(q, 0, *s);
}
/* the widget with this id and arg now (-1 none) */
static int find(int id, int arg)
{
    mq_game_widgets(q);
    for (int i = 0; i < q->wg.n; i++)
        if (q->wg.w[i].id == id && (arg < 0 || q->wg.w[i].arg == arg))
            return i;
    return -1;
}
/* (a widget not laid out -- find's -1 -- is no crash: a point off the screen, and the play-through tries again. The
 * module's events set its clock from the wall clock, so under load a moment can pass between two of the test's
 * events -- a demo, the mash pause -- and a widget may not be there yet: mq_wg(q, -1) is NULL) */
static void centre(int i, int *x, int *y)
{
    const mq_wg_t *g = mq_wg(q, i);
    if (!g) {
        *x = -100, *y = -100;
        return;
    }
    *x = g->x + g->w / 2, *y = g->y + g->h / 2;
}
/* (the round's widget ids, mq_play.c) */
enum { W_GO = 1, W_ANS, W_FUN, W_STEER, W_LANE, W_CARD, W_BIN, W_LINE, W_PAD, W_AIM, W_PIE, W_PLOT, W_HAND, W_COIN,
       W_TRAY, W_TRAYROW, W_KEY, W_MAP, W_WHEEL, W_SPOT, W_TILE, W_MAKE, W_HELP, W_POWER, W_BOARD, W_FRIEND, W_HOLE, W_NOTE,
       W_PAIR };

static const struct {
    int mech, skill, rung;
} games[] = {{ME_POP, S_GROUPS, 1},  {ME_CATCH, S_GROUPS, 1},  {ME_GATES, S_PV3, 1},    {ME_KART, S_MUL10, 1},
             {ME_ZAP, S_DIVFACT, 1}, {ME_SORT, S_CMP3, 1},     {ME_JUMP, S_FRACLINE, 1}, {ME_JUMP, S_ROUND10, 1},
             {ME_LAUNCH, S_PROTRACTOR, 1}, {ME_LAUNCH, S_FRACLINE, 0}, {ME_BAKE, S_PARTITION, 1}, {ME_BUILD, S_AREAMUL, 2},
             {ME_CLOCK, S_TIME5, 2}, {ME_TILL, S_COINS, 1},  {ME_TILL, S_CHANGE, 1},   {ME_DIG, S_COORD, 1},
             {ME_VAULT, S_ADDSUB, 5}, {ME_COOK, S_GROUPS, 1}, {ME_PETS, S_CHANGE, 1}};
#define NGAMES (int)(sizeof games / sizeof games[0])

/* the right answer by touch only (taps and drags on the widgets) */
static bool by_touch(int gi)
{
    mq_game_t *g = &q->gm;
    const mq_prob_t *p = &q->prob;
    int x, y, i;
    char a[24];
    switch (g->mech) {
    case ME_POP: case ME_ZAP: case ME_CATCH: case ME_GATES: case ME_KART: {
        int r = mq_game_right_opt(q);
        if (g->mech == ME_ZAP) {
            for (int k = 0; k < GM_NOBJ; k++)
                if (g->o[k].kind == 1 && g->o[k].opt == r)
                    i = find(W_HOLE, g->o[k].hp), centre(i, &x, &y), tev(T_DOWN, x, y);
        } else {
            if (g->mech == ME_POP) /* (in view: the bubbles float, a tap needs one to see) */
                for (int k = 0; k < GM_NOBJ; k++)
                    if (g->o[k].kind == 1)
                        g->o[k].y = (int16_t)((FY + 60) * 16), g->o[k].vy = 0;
            i = find(W_ANS, r);
            if (getenv("WG_TRACE"))
                fprintf(stderr, "pick: mech %d make %d right %d widget %d of %d nopt %d sub %d scene %d\n", g->mech, g->make, r, i,
                        q->wg.n, g->nopt, q->sub, g->scene_on);
            if (i < 0)
                return false;
            centre(i, &x, &y);
            tev(T_DOWN, x, y); /* (Bubble Pop: popped the moment the finger lands) */
            if (q->sub == GS_PLAY)
                tev(T_TAP, x, y);
            for (int k = 0; k < 300 && q->sub == GS_PLAY; k++)
                con("step 1");
        }
        break;
    }
    case ME_SORT: /* carry the card to its bin */
        i = find(W_CARD, -1), centre(i, &x, &y);
        tev(T_DOWN, x, y);
        i = find(W_BIN, p->ans);
        int bx, by;
        centre(i, &bx, &by);
        tev(T_DRAG, (x + bx) / 2, (y + by) / 2), tev(T_DRAG, bx, by), tev(T_UP, bx, by);
        con("step 20");
        break;
    case ME_JUMP:
        if (g->mode == 1) {
            if (!g->rstep) /* (rounding with the help faded: the two round numbers first) */
                i = find(W_PAIR, g->rright), centre(i, &x, &y), tev(T_TAP, x, y);
            CHECK(g->rstep == 1);
            i = find(W_PAD, p->ans == g->opt[0] ? 0 : 1), centre(i, &x, &y), tev(T_TAP, x, y);
        } else {
            i = find(W_LINE, -1);
            const mq_wg_t *w = mq_wg(q, i);
            tev(T_TAP, w->x + w->w * (int)p->ans / (w->hi ? w->hi : 1), w->y + w->h / 2);
        }
        i = find(W_GO, -1), centre(i, &x, &y), tev(T_TAP, x, y);
        break;
    case ME_LAUNCH:
        if (g->mode == 3) {
            i = find(W_AIM, -1);
            const mq_wg_t *w = mq_wg(q, i);
            tev(T_DOWN, w->x + w->w * (int)p->ans / (w->hi ? w->hi : 1), w->y + w->h / 2);
        } else { /* the dial: point at the angle */
            int ang = (int)p->ans;
            const mq_wg_t *w = mq_wg(q, find(W_AIM, -1)); /* (its centre: the box's bottom middle) */
            tev(T_DOWN, w->x + w->w / 2 + mq_icos(ang) * 90 / 1024, w->y + w->h - mq_isin(ang) * 90 / 1024);
        }
        tev(T_UP, 0, 0);
        i = find(W_GO, -1), centre(i, &x, &y), tev(T_TAP, x, y);
        for (int k = 0; k < 80 && q->sub == GS_PLAY; k++) /* (the star flies, then lands) */
            con("step 1");
        break;
    case ME_BAKE:
        for (int k = 0; k < p->ans; k++) {
            int am = k * 360 / p->v[1] + 180 / p->v[1];
            tev(T_DOWN, 120 + mq_isin(am) * 40 / 1024, FY + 72 - mq_icos(am) * 40 / 1024);
            tev(T_UP, 0, 0);
        }
        i = find(W_GO, -1), centre(i, &x, &y), tev(T_TAP, x, y);
        break;
    case ME_BUILD: {
        int cell = mq_build_cell(g);
        x = 20 + (g->fixw ? g->fixw : (int)p->ans) * cell + 1, y = FY + 10 + (g->fixh ? g->fixh : (int)p->ans) * cell + 1;
        tev(T_DOWN, x, y), tev(T_UP, x, y);
        i = find(W_GO, -1), centre(i, &x, &y), tev(T_TAP, x, y);
        break;
    }
    case ME_CLOCK: { /* the hour hand, then the minute hand, dragged round */
        int h = p->ans / 100 % 12, m = p->ans % 100;
        int ha = h * 30, ma = m * 6;
        tev(T_DOWN, 110 + mq_isin(g->value / 100 % 12 * 30) * 30 / 1024, FY + 74 - mq_icos(g->value / 100 % 12 * 30) * 30 / 1024);
        tev(T_DRAG, 110 + mq_isin(ha) * 30 / 1024, FY + 74 - mq_icos(ha) * 30 / 1024), tev(T_UP, 0, 0);
        tev(T_DOWN, 110 + mq_isin(g->value % 100 * 6) * 60 / 1024, FY + 74 - mq_icos(g->value % 100 * 6) * 60 / 1024);
        tev(T_DRAG, 110 + mq_isin(ma) * 60 / 1024, FY + 74 - mq_icos(ma) * 60 / 1024), tev(T_UP, 0, 0);
        if (g->value != p->ans) /* (the hour moved with the minutes round the top: set it again) */
            tev(T_DOWN, 110 + mq_isin(g->value / 100 % 12 * 30 + 15) * 30 / 1024, FY + 74 - mq_icos(g->value / 100 % 12 * 30 + 15) * 30 / 1024),
                tev(T_DRAG, 110 + mq_isin(ha) * 30 / 1024, FY + 74 - mq_icos(ha) * 30 / 1024), tev(T_UP, 0, 0);
        i = find(W_GO, -1), centre(i, &x, &y), tev(T_TAP, x, y);
        break;
    }
    case ME_TILL: case ME_PETS:
        if (g->mode == 0) { /* coins carried to the tray (dollars, quarters, dimes, nickels, pennies) */
            static const int val[5] = {100, 25, 10, 5, 1};
            int32_t left = p->ans;
            for (int c = 0; c < 5; c++)
                while (left >= val[c]) {
                    i = find(W_COIN, c), centre(i, &x, &y);
                    tev(T_DOWN, x, y), tev(T_DRAG, 100, FY + 50), tev(T_DRAG, 100, FY + 40), tev(T_UP, 100, FY + 40);
                    left -= val[c];
                }
        } else {
            snprintf(a, sizeof a, "%ld", (long)p->ans);
            for (const char *c = a; *c; c++)
                i = find(W_KEY, *c), centre(i, &x, &y), tev(T_TAP, x, y);
        }
        i = find(W_GO, -1), centre(i, &x, &y), tev(T_TAP, x, y);
        break;
    case ME_DIG:
        tev(T_TAP, 30 + (int)(p->ans / 100) * 20, FB - 20 - (int)(p->ans % 100) * 18); /* (the map: columns 20 wide) */
        i = find(W_GO, -1), centre(i, &x, &y), tev(T_TAP, x, y);
        break;
    case ME_VAULT: { /* each wheel tapped up (its arrow) to its digit */
        mq_answer_text(q, p, a, sizeof a);
        int len = (int)strlen(a), n = g->lock_n;
        for (int k = 0; k < len; k++) {
            int wi = n - len + k;
            i = find(W_WHEEL, wi);
            if (i < 0)
                return false;
            const mq_wg_t *w = mq_wg(q, i);
            while (g->lock[wi] != a[k])
                tev(T_TAP, w->x + w->w / 2, w->y + 6);
        }
        i = find(W_GO, -1), centre(i, &x, &y), tev(T_TAP, x, y);
        break;
    }
    case ME_COOK: /* a tap on the right amount */
        i = find(W_ANS, mq_game_right_opt(q));
        if (i < 0)
            return false;
        centre(i, &x, &y), tev(T_TAP, x, y);
        break;
    default: return false;
    }
    (void)gi;
    return q->sub == GS_CHEER;
}

/* the right answer with the keys only */
static bool by_keys(void)
{
    mq_game_t *g = &q->gm;
    const mq_prob_t *p = &q->prob;
    char a[24];
    mq_answer_text(q, p, a, sizeof a);
    switch (g->mech) {
    case ME_SORT: {
        int r = mq_game_right_opt(q);
        while (g->sel > r)
            key(K_LEFT);
        while (g->sel < r)
            key(K_RIGHT);
        key(K_ENTER);
        con("step 20");
        break;
    }
    case ME_JUMP:
        if (g->mode == 1) {
            if (!g->rstep) { /* (the two round numbers first) */
                while (g->rsel != g->rright)
                    key(g->rsel < g->rright ? K_RIGHT : K_LEFT);
                key(K_ENTER);
            }
            key(p->ans == g->opt[0] ? K_LEFT : K_RIGHT);
        } else
            while (g->aim < p->ans)
                key(K_RIGHT);
        key(K_ENTER);
        break;
    case ME_LAUNCH:
        if (g->mode == 3)
            while (g->aim < p->ans)
                key(K_RIGHT);
        else
            typ(a);
        key(K_ENTER);
        for (int k = 0; k < 80 && q->sub == GS_PLAY; k++)
            con("step 1");
        break;
    case ME_BAKE:
        for (int k = 0; k < p->ans; k++)
            mq_game_key(q, 0, '1' + k);
        key(K_ENTER);
        break;
    case ME_BUILD:
        for (int k = 0; k < 20 && (g->fixh ? g->bw : g->bh) != p->ans; k++)
            key(g->fixh ? ((g->bw < p->ans) ? K_RIGHT : K_LEFT) : ((g->bh < p->ans) ? K_DOWN : K_UP));
        key(K_ENTER);
        break;
    case ME_TILL: case ME_PETS:
        snprintf(a, sizeof a, "%ld", (long)p->ans);
        typ(a);
        key(K_ENTER);
        break;
    case ME_DIG:
        while (g->px < p->ans / 100)
            key(K_RIGHT);
        while (g->py < p->ans % 100)
            key(K_UP);
        key(K_ENTER);
        break;
    case ME_ZAP: { /* the keypad is the holes: 7 8 9 / 4 5 6 */
        int r = mq_game_right_opt(q);
        for (int k = 0; k < GM_NOBJ; k++)
            if (g->o[k].kind == 1 && g->o[k].opt == r)
                mq_game_key(q, 0, "789456"[g->o[k].hp % 6]);
        break;
    }
    case ME_GATES: case ME_KART: case ME_POP: case ME_CATCH: case ME_CLOCK: case ME_VAULT: case ME_COOK:
        typ(a); /* (a picking game takes it as soon as only one answer fits what is typed) */
        if (q->sub == GS_PLAY)
            key(K_ENTER);
        for (int k = 0; k < 300 && q->sub == GS_PLAY; k++)
            con("step 1");
        break;
    default: return false;
    }
    return q->sub == GS_CHEER;
}

/* with a mouse and its wheel only: clicks (taps) and scrolls */
static bool by_mouse(int gi)
{
    mq_game_t *g = &q->gm;
    const mq_prob_t *p = &q->prob;
    int x, y, i;
    switch (g->mech) {
    case ME_VAULT: { /* the wheel under the pointer, scrolled to each digit */
        char a[24];
        mq_answer_text(q, p, a, sizeof a);
        int len = (int)strlen(a), n = g->lock_n;
        for (int k = 0; k < len; k++) {
            int wi = n - len + k;
            i = find(W_WHEEL, wi), centre(i, &x, &y);
            int want = a[k] - '0', have = g->lock[wi] - '0';
            wheel(-(want - have), x, y); /* (up is more) */
        }
        i = find(W_GO, -1), centre(i, &x, &y), tev(T_TAP, x, y);
        return q->sub == GS_CHEER;
    }
    case ME_JUMP:
        if (g->mode == 0) {
            i = find(W_LINE, -1), centre(i, &x, &y);
            wheel((int)p->ans - g->aim, x, y);
            i = find(W_GO, -1), centre(i, &x, &y), tev(T_TAP, x, y);
            return q->sub == GS_CHEER;
        }
        break;
    case ME_CLOCK: { /* the minute hand scrolled round, the hour by a click on the face */
        int h = p->ans / 100 % 12, m = p->ans % 100, ha = h * 30;
        for (int k = 0; k < 200 && g->value % 100 != m; k++)
            wheel(1, 110 + mq_isin(g->value % 100 * 6) * 60 / 1024, FY + 74 - mq_icos(g->value % 100 * 6) * 60 / 1024);
        for (int k = 0; k < 24 && g->value / 100 % 12 != h; k++)
            tev(T_TAP, 110 + mq_isin(ha) * 30 / 1024, FY + 74 - mq_icos(ha) * 30 / 1024);
        i = find(W_GO, -1), centre(i, &x, &y), tev(T_TAP, x, y);
        return q->sub == GS_CHEER;
    }
    default: break;
    }
    return by_touch(gi); /* (everything else is clicks) */
}

static void test_games(void)
{
    con("new Gia 2");
    con("grant bosses");
    q->fun_off = true;
    int small = 0;
    for (int gi = 0; gi < NGAMES; gi++) {
        for (int way = 0; way < 3; way++) {
            int ok = 0;
            for (int t = 0; t < 4 && !ok; t++) {
                q->rng = (uint32_t)(gi * 97 + way * 13 + t * 7) | 1;
                if (!order_in(games[gi].skill, games[gi].rung, games[gi].mech))
                    continue;
                mq_game_widgets(q);
                for (int i = 0; i < q->wg.n; i++) { /* every hit area at least 10 mm */
                    int hx, hy, hw, hh;
                    mq_wg_hitbox(&q->wg.w[i], &hx, &hy, &hw, &hh);
                    small += hw < MQ_WG_HIT || hh < MQ_WG_HIT;
                }
                ok = way == 0 ? by_touch(gi) : way == 1 ? by_keys() : by_mouse(gi);
                if (!ok)
                    fprintf(stderr, "%s (skill %d): not answered %s (moment %s)\n", mq_mech_name(games[gi].mech),
                            games[gi].skill, way == 0 ? "by touch" : way == 1 ? "by the keys" : "with the mouse",
                            mq_game_moment(q));
            }
            CHECK(ok);
        }
        /* a fuzz: taps, drags, flicks and scrolls anywhere; nothing breaks, the order stays playable */
        q->rng = (uint32_t)gi * 31 + 3;
        if (!order_in(games[gi].skill, games[gi].rung, games[gi].mech))
            continue;
        for (int k = 0; k < 300 && q->scene == SC_PLAY; k++) {
            int x = rnd(320), y = MQ_Y0 + rnd(MQ_H), what = rnd(6);
            if (what == 0)
                tev(T_DOWN, x, y), tev(T_TAP, x, y);
            else if (what == 1)
                tev(T_DOWN, x, y), tev(T_DRAG, rnd(320), MQ_Y0 + rnd(MQ_H)), tev(T_UP, x, y);
            else if (what == 2)
                ops.event(q, MODX_EV_SWIPE, MODX_SWIPE(rnd(4), x, y));
            else if (what == 3)
                wheel(rnd(11) - 5, x, y);
            else if (what == 4)
                key(rnd(2) ? K_UP + rnd(4) : K_ENTER);
            else
                con("step 3");
            if (q->sub != GS_PLAY)
                con("next");
            int r[4];
            ops.draw(q, 0, r);
        }
        CHECK(q->scene == SC_PLAY || q->scene == SC_DONE_LEVEL || q->scene == SC_WORLD); /* (or the round played out) */
    }
    CHECK(small == 0);
}

/* Treasure Heist: the value dialled stays on the wheels after a wrong one (and is said) */
static void test_heist_keeps(void)
{
    con("new Hal 2");
    q->fun_off = true;
    CHECK(order_in(S_ADDSUB, 5, ME_VAULT));
    char a[24];
    mq_answer_text(q, &q->prob, a, sizeof a);
    a[strlen(a) - 1] = a[strlen(a) - 1] == '9' ? '0' : (char)(a[strlen(a) - 1] + 1); /* one off */
    typ(a);
    char before[16];
    snprintf(before, sizeof before, "%s", q->gm.lock);
    key(K_ENTER);
    CHECK(q->tried_wrong && q->sub == GS_PLAY && !strcmp(q->gm.lock, before));
}

/* the audit's O1: the goal, the demo's words and the guide's psst are in the top line (the HUD strip): with
 * or without them the field (the answers, the coins, the keys, the bins' labels) is drawn the same */
static void test_hud_zone(void)
{
    con("new Oda 2");
    q->fun_off = true;
    int bad = 0;
    for (int gi = 0; gi < NGAMES; gi++) {
        q->rng = (uint32_t)gi * 5 + 1;
        if (!order_in(games[gi].skill, games[gi].rung, games[gi].mech))
            continue;
        for (int o = 0; o < 2; o++) { /* the goal (a new round), then the idle psst */
            mq_game_t *g = &q->gm;
            int r[4];
            mq_prof(q)->problems = 50, q->anim_t += 10000, g->order_t = 1, g->demo_t = 0;
            g->t0 = 1, q->last_input = q->now, q->prob_start = q->now, g->hud_ok = 0;
            q->earn_msg[0] = 0, q->earn_qn = 0; /* (no toast waiting for the top line) */
            uint32_t at = q->anim_t;
            ops.draw(q, 0, r);
            memcpy(snap, fb, sizeof snap);
            if (o == 0)
                g->t0 = (q->anim_t - 100) | 1;
            else
                q->last_input = q->prob_start = q->now - 6000;
            q->anim_t = at, g->hud_ok = 0;
            ops.draw(q, 0, r);
            int top = changed(0, MQ_Y0, 320, 16), field = 0;
            if (o == 0)
                field = changed(0, FY, 320, FB - FY);
            if (!top || field) {
                if (bad++ < 4)
                    fprintf(stderr, "%s: the %s top %d, field %d\n", mq_mech_name(games[gi].mech), o ? "psst" : "goal", top, field);
            }
        }
    }
    CHECK(bad == 0);
}

/* ------------------------------------------------------------------ a real tap is DOWN then TAP (never UP)
 * The core's recogniser sends a short press as DOWN then TAP (core/touch.c); a handler that acts on both acts
 * twice (code review 2: the room's tap deselected, the gate's tap held, a bongo drummed twice; CR-06 and the
 * pie before). This is the library's contract for one finger landing and lifting on each kind, so a handler can
 * be checked against it:
 *   BUTTON, KEY, CHOICE, WHEEL, DROP: nothing on the DOWN, one WHY_TAP on the TAP;
 *   CHOICE with WF_ONDOWN: one WHY_TAP on the DOWN, nothing on the TAP (the 1.5 s guard);
 *   SLIDER, NUMLINE, STEER, CLOCKHAND: WHY_DRAG on the DOWN and WHY_TAP on the TAP with the SAME value once the
 *     handler applied the DOWN's (so acting on both is harmless only if the action is idempotent);
 *   DRAG: nothing on the DOWN (lifted), WHY_PRESS on the TAP (tap, then tap where it goes);
 *   AREA: WHY_PRESS on the DOWN and WHY_TAP on the TAP -- a handler must act on exactly one of them. */
static void test_real_tap(void)
{
    con("new Wil 2");
    static const struct {
        int kind, flags, lo, hi, value;
        int down_why, tap_why;
    } k[] = {
        {WG_BUTTON, 0, 0, 0, 0, WHY_NONE, WHY_TAP},     {WG_KEY, 0, 0, 0, 7, WHY_NONE, WHY_TAP},
        {WG_CHOICE, 0, 0, 0, 3, WHY_NONE, WHY_TAP},     {WG_CHOICE, WF_ONDOWN, 0, 0, 3, WHY_TAP, WHY_NONE},
        {WG_WHEEL, 0, 0, 9, 4, WHY_NONE, WHY_TAP},      {WG_DROP, 0, 0, 0, 0, WHY_NONE, WHY_TAP},
        {WG_SLIDER, 0, 0, 20, 2, WHY_DRAG, WHY_TAP},    {WG_NUMLINE, 0, 0, 12, 1, WHY_DRAG, WHY_TAP},
        {WG_STEER, 0, 40, 280, 60, WHY_DRAG, WHY_TAP},  {WG_SLIDER, WF_VERT, 0, 10, 0, WHY_DRAG, WHY_TAP},
        {WG_CLOCKHAND, 0, 0, 59, 0, WHY_DRAG, WHY_TAP}, {WG_DRAG, 0, 0, 0, 0, WHY_NONE, WHY_PRESS},
        {WG_AREA, 0, 0, 0, 0, WHY_PRESS, WHY_TAP},
    };
    /* twice: the DOWN and the TAP a frame apart (the usual), and both in one frame (a quick tap; the console's
     * `touch tap`). The one-frame case on an even tick was CR2-05 (docs/audit/CODE_REVIEW_2.md): press_t was set to
     * anim_t | 1, a moment in the future on an even anim_t, so the "its DOWN, just now" guard's unsigned
     * difference wrapped and the on-down choice was picked again by the TAP; mq_mark / mq_since (B2) fixed it. */
    for (int pass = 0; pass < 2; pass++)
        for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
            q->anim_t = pass ? 4000 : 4001;
            mq_wg_begin(q);
            int w = k[i].kind == WG_CLOCKHAND ? 120 : k[i].kind == WG_STEER ? 240 : 160, h = k[i].kind == WG_CLOCKHAND ? 120 : 40;
            int g = mq_wg_add(q, k[i].kind, 5, 1, 60, 80, w, h);
            mq_wg_range(q, g, k[i].value, k[i].lo, k[i].hi, 1);
            mq_wg_flags(q, g, k[i].flags);
            q->wg.picked = -1, q->wg.carry = -1, q->wg.press = -1, q->wg.downpick = false;
            int x = 60 + w * 3 / 4, y = 80 + h / 2; /* (off the centre: a slider's value and a wheel's half are defined) */
            if (k[i].kind == WG_WHEEL)
                y = 80 + 6;
            mq_wev_t d, t;
            mq_wg_touch(q, T_DOWN, x, y, &d);
            if (d.why != k[i].down_why && fails < 40)
                fprintf(stderr, "kind %d flags %d: DOWN gave why %d, expected %d\n", k[i].kind, k[i].flags, d.why, k[i].down_why);
            CHECK(d.why == k[i].down_why);
            if (d.why == WHY_DRAG)
                mq_wg(q, g)->value = d.value; /* (the handler applied it and laid out again) */
            if (!pass)
                q->anim_t += 40; /* (the next frame) */
            mq_wg_touch(q, T_TAP, x, y, &t);
            if (t.why != k[i].tap_why && fails < 40)
                fprintf(stderr, "kind %d flags %d (pass %d): TAP gave why %d, expected %d\n", k[i].kind, k[i].flags, pass, t.why,
                        k[i].tap_why);
            CHECK(t.why == k[i].tap_why);
            if (d.why == WHY_DRAG && t.why == WHY_TAP)
                CHECK(t.value == d.value);
            if (k[i].kind == WG_DRAG)
                CHECK(mq_wg_picked(q) == g);
            q->wg.picked = -1;
        }
}

int main(void)
{
    harness_init();
    test_kinds();
    test_real_tap();
    test_games();
    test_heist_keeps();
    test_hud_zone();
    printf("test_mq_widgets: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
