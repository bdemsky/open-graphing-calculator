/*
 * Math Quest: what to do, and help when it is hard -- all in the round, all in character.
 *
 *   - the first-time demo: the first order of every game (each game once per player) a ghost hand
 *     shows the gesture (tap a bubble, drag the corner, slide the card, ...) while the place's guide
 *     says the goal; any input ends it. After that the goal banner is the short reminder;
 *   - idle hints: nothing done for five seconds -- every target pulses (all the answers, never just
 *     the right one) and the guide pops up in the corner pointing at them;
 *   - "what do I do?": the soft key (F3) or the "?" in the top line replays the demo;
 *   - the lightbulb (F1, "show me"): the story acted again with its operation shown (the groups
 *     counted off one by one, a pile dealt out to the plates, two piles pushed together, a part
 *     taken away) and the operation's little tag ("3 bags, 4 each"); never the answer;
 *   - the helper friend: after two misses on an order (or a third slip of the same skill within a
 *     few orders) a friend pops in ("I'll help!"), walks through a similar order with fresh numbers
 *     step by step (a tap goes on), then she solves her own order with the friend cheering. Right
 *     after help: "Teamwork!" and hearts. Missed again: they do it together -- the duds fade and the
 *     friend points at it, so the order always ends in a success. The favourite friend (cast slot
 *     0) comes most often; each friend has an entrance and a line of their own.
 *
 * Coordinates are logical (the 320 x 240 screen).
 */
#include "mq.h"

#define FY (MQ_Y0 + 58)
#define FB (MQ_Y0 + MQ_H)
#define FH (FB - FY)
#define DEMO_MS 3200

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int guide_of(int w) { return w >= G_SKY ? 8 : w % MQ_NCAST; }
/* a demo lasts its words' reading time (never less than DEMO_MS): the game's goal; the arcade's and the action
 * games' captions (up to 40 characters, the time of the longest: the demo and its words end together) */
static uint32_t demo_ms(const mq_t *q)
{
    mq_t *w = (mq_t *)q;
    return (uint32_t)imax(DEMO_MS, mq_read_ms(w, mq_mech_goal(q->gm.mech)));
}
static int cap_ms(mq_t *q) { return imax(DEMO_MS, mq_read_ms_len(q, 40)); }

/* ------------------------------------------------------------------ the first-time demo */
static bool demo_seen(mq_t *q, int bit) { return (mq_pace(q)->demo_seen >> (bit & 31)) & 1; }
static void demo_mark(mq_t *q, int bit)
{
    mq_pace(q)->demo_seen |= 1u << (bit & 31);
    q->dirty = true;
}

/* an order begins (after its acted story): the demo the first time she plays this game */
void mq_guide_order(mq_t *q)
{
    mq_game_t *g = &q->gm;
    if (g->mech < ME_COUNT && !demo_seen(q, g->mech) && !q->preview) {
        g->demo_t = q->anim_t + 1;
        mq_speak(q, guide_of(q->world), mq_mech_goal(g->mech), MODX_SPEAK_NOW);
    }
}
bool mq_guide_demo_on(const mq_t *q) { return q->gm.demo_t && q->anim_t + 1 - q->gm.demo_t < demo_ms(q); }
/* she did something: a demo running is over (seen) */
void mq_guide_input(mq_t *q)
{
    mq_game_t *g = &q->gm;
    if (g->demo_t) {
        if (g->mech < ME_COUNT)
            demo_mark(q, g->mech);
        g->demo_t = 0;
    }
}
/* "what do I do?": the demo again, now */
void mq_guide_help(mq_t *q)
{
    mq_game_t *g = &q->gm;
    if (q->scene != SC_PLAY || q->sub != GS_PLAY)
        return;
    g->demo_t = q->anim_t + 1;
    q->last_input = q->now;
    mq_speak(q, guide_of(q->world), mq_mech_goal(g->mech), MODX_SPEAK_NOW);
    mq_sfx(q, MODX_SFX_TAP, 0);
    q->redraw = true;
}
/* the frame: a demo that ran to its end counts as seen */
void mq_guide_frame(mq_t *q)
{
    mq_game_t *g = &q->gm;
    uint32_t idle = q->now - (q->last_input > q->prob_start ? q->last_input : q->prob_start);
    if (q->sub == GS_PLAY && !g->scene_on && idle > 5000u && !g->idle_said) { /* the guide's reminder, once */
        g->idle_said = 1;
        if (!g->helped)
            mq_speak(q, guide_of(q->world), mq_mech_goal(g->mech), 0);
    } else if (idle < 5000u)
        g->idle_said = 0;
    if (g->demo_t && q->anim_t + 1 - g->demo_t >= demo_ms(q)) {
        mq_read_note(q, q->anim_t + 1 - g->demo_t, (int)demo_ms(q), false);
        if (g->mech < ME_COUNT)
            demo_mark(q, g->mech);
        g->demo_t = 0;
    }
}

/* The first-time demo of a game outside the round (the arcade's fact games, the action games): a ghost
 * hand doing the gesture and the guide saying the controls, for the first 3.2 s of its first time */
void mq_demo_seen(mq_t *q, int bit)
{
    if (!demo_seen(q, bit))
        demo_mark(q, bit);
}
bool mq_demo_overlay(mq_t *q, int bit, uint32_t t0, int x0, int y0, int x1, int y1, bool drag, const char *cap)
{
    if (demo_seen(q, bit) || q->preview)
        return false;
    int el = (int)(q->anim_t - t0);
    if (el >= cap_ms(q)) {
        demo_mark(q, bit);
        return false;
    }
    if (el < 40)
        mq_speak(q, 8, cap, MODX_SPEAK_NOW);
    int ph = el % 1600, x = x0, y = y0, press = 0;
    if (drag) {
        int t = ph < 300 ? 0 : ph > 1300 ? 1000 : ph - 300;
        x = x0 + (x1 - x0) * t / 1000, y = y0 + (y1 - y0) * t / 1000, press = ph > 200 && ph < 1400 ? 16 : 0;
    } else {
        press = ph > 500 && ph < 800 ? 16 : 0;
        if (ph > 600 && ph < 1200)
            mq_ring(q, x, y, 4 + (ph - 600) / 75, Q_WHITE);
    }
    mq_hand(q, x, y, press);
    /* the guide's words: in the screen's lane (the header's middle, the game's top line: never over what she
     * plays -- the render audit), a toast waiting meanwhile; a screen without one: at the bottom as before */
    int lx = 46, ly = MQ_Y0 + MQ_H - 40, room = 268;
    if (q->lane_on)
        lx = q->lane_x0, ly = q->lane_y, room = q->lane_x1 - q->lane_x0, q->lane_on = false;
    int w = mq_text_w(q, q->api->font_small, cap) + 26;
    w = w < room ? w : room;
    mq_rrect(q, lx, ly, w, 16, 7, Q_LAV);
    mq_cast(q, 8, lx + 2, ly, 16, AN_TALK);
    mq_text_fit(q, q->api->font_small, lx + 20, ly + 1, w - 24, cap, Q_INK, false);
    return true;
}
bool mq_demo_running(mq_t *q, int bit, uint32_t t0)
{
    if (demo_seen(q, bit) || q->preview)
        return false;
    if ((int)(q->anim_t - t0) < cap_ms(q))
        return true;
    mq_demo_seen(q, bit); /* watched to its end: seen (a fact game marked it only at her first answer) */
    return false;
}

/* a ghost hand at (x, y): a soft white mitten with a pointing finger; press 0..16 squashes it */
void mq_hand(mq_t *q, int x, int y, int press)
{
    int d = press / 4;
    mq_circle(q, x + 6, y + 13 + d, 9, Q_INK);
    mq_circle(q, x + 6, y + 13 + d, 8, Q_WHITE);
    mq_rrect(q, x - 2, y - 1 + d, 7, 16, 3, Q_INK);
    mq_rrect(q, x - 1, y + d, 5, 15, 2, Q_WHITE);
    mq_circle(q, x + 1, y + 1 + d, 1, Q_PINK);
}

/* the gesture of a game: from (x0, y0) to (x1, y1); drag or tap */
static void gesture(mq_t *q, int *x0, int *y0, int *x1, int *y1, bool *drag)
{
    mq_game_t *g = &q->gm;
    *drag = false;
    *x0 = 160, *y0 = FY + 70, *x1 = 160, *y1 = FY + 70;
    switch (g->mech) {
    case ME_POP: case ME_ZAP: { /* across the answers, then a tap in the air between them */
        int lo = 400, hi = -1, yy = FY + 70;
        for (int i = 0; i < GM_NOBJ; i++)
            if (g->o[i].kind == 1 && g->mech == ME_POP) {
                lo = imin(lo, g->o[i].x / 16), hi = imax(hi, g->o[i].x / 16), yy = g->o[i].y / 16;
            }
        if (hi < 0)
            lo = 60, hi = 260;
        *x0 = lo, *x1 = hi, *y0 = *y1 = g->mech == ME_ZAP ? FY + 52 : imax(FY + 20, imin(FB - 16, yy + 14)); /* (Peekaboo: above the holes, clear of the signs and the friends numbers) */
        *drag = true; /* (a sweep that points at every one, its fingertip at their lower rims: the numbers stay in
                       * sight -- the render audit's M6) */
        break;
    }
    case ME_CATCH: *x0 = g->px, *y0 = FB - 18, *x1 = g->px > 160 ? g->px - 70 : g->px + 70, *y1 = FB - 18, *drag = true; break;
    case ME_GATES: case ME_KART: *x0 = *x1 = 236, *y0 = *y1 = FY + 74; break;
    case ME_SORT: *x0 = 224, *y0 = FY + 70, *x1 = bin_x(g, 0), *y1 = FB - 12, *drag = true; break; /* (from the card's corner: its words show) */
    case ME_JUMP:
        if (g->mode == 1 && !g->rstep) /* rounding, first: across the three pairs (low on them: their numbers show) */
            *x0 = 40, *x1 = 280, *y0 = *y1 = FY + 62, *drag = true;
        else if (g->mode == 1) /* rounding: from one landing pad to the other (never pointing at the answer) */
            *x0 = 40, *x1 = 280, *y0 = *y1 = FY + 100, *drag = true;
        else
            *x0 = 60, *y0 = *y1 = FY + 92, *x1 = 200, *drag = true;
        break;
    case ME_LAUNCH: *x0 = 140, *y0 = FB - 20, *x1 = 60, *y1 = FY + 56, *drag = true; break; /* (to the arc's outside: the degrees show) */
    case ME_BAKE: *x0 = 120, *y0 = FY + 42, *x1 = 150, *y1 = FY + 72, *drag = true; break;
    case ME_BUILD: {
        int cell = mq_build_cell(g);
        *x0 = 20 + g->bw * cell, *y0 = FY + 10 + g->bh * cell;
        *x1 = g->fixh ? *x0 + 3 * cell : *x0, *y1 = g->fixh ? *y0 : *y0 + 3 * cell;
        *drag = true;
        break;
    }
    case ME_CLOCK: *x0 = 110, *y0 = FY + 34, *x1 = 148, *y1 = FY + 74, *drag = true; break;
    case ME_TILL: case ME_PETS: *x0 = *x1 = 30, *y0 = *y1 = FB - 9; break; /* (low on the coin: its value shows) */
    case ME_DIG: *x0 = *x1 = 150, *y0 = *y1 = FY + 70; break;
    case ME_VAULT: *x0 = *x1 = 140, *y0 = *y1 = FY + 56; break;
    case ME_COOK: *x0 = *x1 = 48, *y0 = *y1 = FB - 24; break; /* (low on the first amount: its label shows) */
    default: break;
    }
}

/* the targets that pulse when she has been idle: every answer, or the game's go button */
static void pulse_targets(mq_t *q, int el)
{
    mq_game_t *g = &q->gm;
    int r = 20 + (el / 120) % 4;
    int n = 0;
    if (g->mech == ME_POP || g->mech == ME_CATCH)
        for (int i = 0; i < GM_NOBJ; i++)
            if (g->o[i].kind == 1 && !((g->gone >> g->o[i].opt) & 1)) {
                mq_ring(q, g->o[i].x / 16, g->o[i].y / 16, r, Q_YELLOW);
                n++;
            }
    if (!n && g->mech != ME_ZAP && g->mech != ME_GATES && g->mech != ME_KART) {
        mq_rrect(q, 250, FB - 36, 68, 34, 10, (el / 200) % 2 ? Q_YELLOW : Q_PEACH); /* the go button's glow */
        mq_rrect(q, 252, FB - 34, 64, 30, 8, Q_MINT);
    }
}

/* the overlay of the round: the demo, or the idle hint (called after the field is drawn) */
void mq_guide_draw(mq_t *q)
{
    mq_game_t *g = &q->gm;
    if (q->sub != GS_PLAY || g->scene_on || g->bulb_t)
        return;
    if (mq_guide_demo_on(q)) {
        int el = (int)(q->anim_t + 1 - g->demo_t), x0, y0, x1, y1;
        if (g->mech == ME_CATCH) { /* (backlog (h)) the hand slides a ghost cloud across and a ghost star, no number on
                                    * it (never the answer), drops into its basket: move under a star to catch it */
            int ph = el % 2200, t = ph < 300 ? 0 : ph > 1500 ? 1000 : (ph - 300) * 1000 / 1200;
            int cx0 = g->px, cx1 = g->px > 160 ? g->px - 100 : g->px + 100, cx = cx0 + (cx1 - cx0) * t / 1000;
            for (int k = -1; k <= 1; k++)
                mq_ring(q, cx + k * 20, FB - 14 - (k ? 0 : 4), k ? 11 : 15, Q_WHITE);
            mq_rrect(q, cx - 15, FB - 58, 30, 10, 4, Q_WHITE);
            int sy = FY + 20 + (FB - 80 - FY) * (ph < 700 ? 0 : ph > 1900 ? 1000 : (ph - 700) * 1000 / 1200) / 1000;
            if (ph < 1900)
                mq_star(q, cx1, sy, 12, Q_WHITE), mq_star(q, cx1, sy, 9, Q_YELLOW);
            else
                mq_ring(q, cx1, FB - 54, 6 + (ph - 1900) / 30, Q_YELLOW); /* caught: a sparkle */
            mq_hand(q, cx, FB - 8, ph > 200 && ph < 1600 ? 16 : 0);
            return;
        }
        int w = mq_game_main_widget(q);
        if (w >= 0) { /* the game's own control shows its gesture (mq_widget.c) */
            mq_wg_demo(q, w, el);
            return;
        }
        bool drag;
        gesture(q, &x0, &y0, &x1, &y1, &drag);
        int ph = el % 1600, x = x0, y = y0, press = 0;
        if (drag) { /* press, move, lift */
            int t = ph < 300 ? 0 : ph > 1300 ? 1000 : (ph - 300);
            x = x0 + (x1 - x0) * t / 1000, y = y0 + (y1 - y0) * t / 1000;
            press = ph > 200 && ph < 1400 ? 16 : 0;
        } else {
            press = ph > 500 && ph < 800 ? 16 : 0;
            if (ph > 600 && ph < 1200)
                mq_ring(q, x, y, 4 + (ph - 600) / 75, Q_WHITE);
        }
        mq_hand(q, x, y, press); /* (the guide says the goal in the top line: mq_play.c, the audit's O1) */
        return;
    }
    uint32_t idle = q->now - (q->last_input > q->prob_start ? q->last_input : q->prob_start);
    bool mash = q->mash_t && q->now - q->mash_t < 2 * MASH_PAUSE_MS;
    if ((idle > 5000u || mash) && !g->blowing) { /* still a while (or mashing): everything she can use pulses */
        int el = mash ? (int)(q->now - q->mash_t) : (int)(idle - 5000u);
        pulse_targets(q, el); /* (the guide's "psst" is in the top line: nothing over the field) */
    }
}

/* ------------------------------------------------------------------ the operation's tag */
/* the little tag that says what the story does, in its own words and numbers (never the answer):
 * "3 bags, 4 each", "24 shared by 4", "put together", "take away", "side by side" */
int mq_op_cue(mq_t *q, const mq_prob_t *p, char *out, int n)
{
    const modx_api_t *api = q->api;
    out[0] = 0;
    long a = p->stg_n[0], b = p->stg_n[1];
    switch (p->stg - 1) {
    case ST_GROUPS: case ST_MUL: api->snprintf(out, (size_t)n, "%ld groups, %ld each", a, b); break;
    case ST_RECIPE: api->snprintf(out, (size_t)n, "%ld friends, a part each", a); break;
    case ST_SHARE: case ST_PERONE: api->snprintf(out, (size_t)n, "%ld shared by %ld", a, b); break;
    case ST_DIVR: api->snprintf(out, (size_t)n, "%ld shared by %ld, extras?", a, b); break;
    case ST_HOWMANY: case ST_FULL: case ST_BUS: api->snprintf(out, (size_t)n, "%ld in groups of %ld", a, b); break;
    case ST_LEFT: api->snprintf(out, (size_t)n, "groups of %ld, extras?", b); break;
    case ST_TIMES: api->snprintf(out, (size_t)n, "%ld times as many", b); break;
    case ST_TAKE: case ST_MARBLES: api->snprintf(out, (size_t)n, "groups, then some go"); break;
    case ST_SHAREADD: api->snprintf(out, (size_t)n, "share, then more each"); break;
    case ST_ADD: case ST_FAR: api->snprintf(out, (size_t)n, "put together"); break;
    case ST_SUB: case ST_JUG: api->snprintf(out, (size_t)n, "take away"); break;
    case ST_READY: api->snprintf(out, (size_t)n, "start, then later"); break;
    case ST_HOWLONG: api->snprintf(out, (size_t)n, "from start to end"); break;
    case ST_CHANGE: api->snprintf(out, (size_t)n, "pay, get change back"); break;
    case ST_FENCE: api->snprintf(out, (size_t)n, "all the way around"); break;
    case ST_RUG: api->snprintf(out, (size_t)n, "squares inside"); break;
    default: break;
    }
    return out[0] != 0;
}

/* ------------------------------------------------------------------ the lightbulb */
void mq_bulb_start(mq_t *q)
{
    mq_game_t *g = &q->gm;
    if (q->scene != SC_PLAY || q->sub != GS_PLAY || g->scene_on)
        return;
    g->bulb_t = q->anim_t + 1;
    q->last_input = q->now;
    char cue[40];
    if (mq_op_cue(q, &q->prob, cue, sizeof cue))
        mq_speak(q, q->prob.ca % MQ_NCAST, cue, MODX_SPEAK_NOW);
    else if (q->prob.cap[0])
        mq_speak(q, q->prob.ca % MQ_NCAST, q->prob.cap, MODX_SPEAK_NOW);
    mq_sfx(q, MODX_SFX_WHOOSH, 1);
    q->redraw = true;
}
#define BULB_MS 3200
/* the lightbulb's scene: its words' reading time (no stage: the order's lines in it), never less than BULB_MS */
static uint32_t bulb_ms(mq_t *q)
{
    const mq_prob_t *p = &q->prob;
    if (p->stg)
        return BULB_MS;
    return (uint32_t)imax(BULB_MS, mq_read_ms_len(q, mq_read_chars(p->text[0]) + mq_read_chars(p->text[1]) +
                                                         mq_read_chars(p->text[2])));
}
bool mq_bulb_frame(mq_t *q)
{
    mq_game_t *g = &q->gm;
    if (g->bulb_t && q->anim_t + 1 - g->bulb_t >= bulb_ms(q)) {
        mq_read_note(q, q->anim_t + 1 - g->bulb_t, (int)bulb_ms(q), false);
        g->bulb_t = 0, g->hud_ok = 0;
    }
    return g->bulb_t != 0;
}
static void idea_btn(mq_t *q, int x, int y, int w, int h)
{
    mq_rrect(q, x, y, w, h, 10, Q_YELLOW);
    mq_rect(q, x, y, w, h, Q_DIM);
    mq_text_c(q, q->api->font_small, x + w / 2, y + h / 2 - 5, "the big idea", Q_INK);
}
/* the lightbulb's way to work it: "Pip's trick" (mq_strat.c: the way that fits her order best) */
static void strat_btn(mq_t *q)
{
    int bx, by, bw, bh;
    int32_t x[6];
    if (!mq_strat_bulb_btn(q, &bx, &by, &bw, &bh))
        return;
    int s = mq_strat_fit(q, &q->prob, 0, x);
    char l[24];
    q->api->snprintf(l, sizeof l, "%s's trick", mq_cast_name(q, s >= 0 ? q->prob.ca % MQ_NCAST : 0));
    if (s >= 0) {
        char nm[40];
        mq_strat_name(q, s, nm, sizeof nm);
        int k = 0;
        while (nm[k] && nm[k] != ' ' && k < 20)
            k++;
        q->api->snprintf(l, sizeof l, "%.*s trick", k, nm);
    }
    mq_rrect(q, bx, by, bw, bh, 10, Q_MINT);
    mq_rect(q, bx, by, bw, bh, Q_DIM);
    mq_text_fit(q, q->api->font_small, bx + 3, by + bh / 2 - 5, bw - 6, l, Q_INK, true);
}
/* the situation again, acted with its operation (in the field, instead of the game for a moment) */
void mq_bulb_draw(mq_t *q)
{
    mq_game_t *g = &q->gm;
    const modx_api_t *api = q->api;
    const mq_prob_t *p = &q->prob;
    int el = (int)(q->anim_t + 1 - g->bulb_t);
    int bx, by, bw, bh;
    if (p->stg) {
        mq_stage_scene(q, p, 420 + el, FY, FB, false);
        if (mq_bulb_idea_btn(q, &bx, &by, &bw, &bh))
            idea_btn(q, bx, by, bw, bh);
        strat_btn(q);
        return;
    }
    /* no story stage: its picture as asked (nothing given away) and its words */
    mq_rrect(q, 8, FY + 6, 304, FH - 12, 12, Q_WHITE);
    mq_rect(q, 8, FY + 6, 304, FH - 12, Q_YELLOW);
    bool vq = q->vis_q;
    q->vis_q = true;
    if (p->vis != V_NONE && !p->vq)
        mq_visual(q, p, 14, FY + 10, 180, FH - 56, imin(1000, el));
    q->vis_q = vq;
    for (int i = 0; i < 3; i++)
        mq_text(q, api->font_small, 16, FB - 44 + i * 12, p->text[i], Q_INK);
    if (!mq_strat_bulb_btn(q, &bx, &by, &bw, &bh))
        mq_cast(q, p->ca % MQ_NCAST, 236, FY + 40, 52, AN_TALK);
    if (mq_bulb_idea_btn(q, &bx, &by, &bw, &bh))
        idea_btn(q, bx, by, bw, bh);
    strat_btn(q);
}
/* the lightbulb's "big idea" button (an idea she has met: its teaching moment again) */
bool mq_bulb_idea_btn(mq_t *q, int *x, int *y, int *w, int *h)
{
    *x = 216, *y = FY + 8, *w = 96, *h = 28;
    return mq_teach_has(q);
}

/* ------------------------------------------------------------------ the helper friend */
/* who comes: her favourite (cast slot 0) about half the time, else the others in turn */
int mq_helper_pick(mq_t *q)
{
    mq_pace_t *pc = mq_pace(q);
    int k = pc->helper_rot++;
    q->dirty = true;
    if (k % 2 == 0)
        return 0;
    return 1 + (k / 2) % (MQ_NCAST - 1);
}
/* each friend's way of arriving and first line */
static const char help_line[MQ_NCAST][28] = {"I'll help!", "Special delivery: help!", "Let's mix it up together!",
                                             "Hoo! Let's look closely.", "Leave it to me!", "I'll paint it for you!",
                                             "Let's go step by step.", "I'm here! Zoom!", "Twinkle! I'll help!"};
static const uint8_t help_entry[MQ_NCAST] = {1, 0, 2, 1, 0, 2, 0, 0, 1}; /* 0 hop in, 1 fly in, 2 pop up */

void mq_helper_start(mq_t *q)
{
    mq_game_t *g = &q->gm;
    mq_profile_t *p = mq_prof(q);
    int skill = q->prob.skill;
    g->help_slot = (uint8_t)mq_helper_pick(q);
    g->help_t = q->anim_t + 1;
    g->help_step = 0;
    g->helped = 1;
    /* a similar order, fresh numbers, one step easier: the one they walk through */
    int d = p->sk[skill].diff;
    mq_generate_fresh(q, &q->ex, skill, d > 0 ? d - 1 : 0);
    if (q->prob.way) { /* a way's order: the similar one is the same way's, fresh numbers */
        int32_t x[6] = {q->prob.wx[0], q->prob.wx[1], q->prob.wx[2], 0, 0, 0};
        q->ex = q->prob;
        if (x[0] > 2)
            x[0]--;
        mq_strat_make(q, &q->ex, q->prob.way - 1, x);
    }
    char out[3][MQ_TXT];
    mq_prob_t vis;
    q->ex_n = (uint8_t)mq_steps(q, &q->ex, 0, out, &vis);
    q->ex_step = 0;
    /* the grown-ups' log, and more spaced practice of this skill */
    mq_pace_t *pc = mq_pace(q);
    if (pc->helped[skill] < 255)
        pc->helped[skill]++;
    mq_teach_helper(q); /* (twice for the same big idea: its moment is offered again) */
    mq_learn_t *L = mq_learn(q);
    if (!L->rev[skill].box)
        mq_review_join(q, skill, false);
    L->rev[skill].due = (uint16_t)(mq_play_min(q) + 3);
    q->sub = GS_HELP;
    g->hud_ok = 0;
    mq_sfx(q, MODX_SFX_FANFARE, 1);
    mq_speak(q, g->help_slot, help_line[g->help_slot], MODX_SPEAK_NOW);
    if (mq_strat_helper(q)) /* the way that fits the order best, shown with the similar one (mq_strat.c) */
        g->help_step = 1;
    q->redraw = true;
}

#define HELP_IN 900
/* what the friend's moment says now: arriving, the line and the similar order; a step, its lines */
static int helper_chars(mq_t *q)
{
    mq_game_t *g = &q->gm;
    char out[3][MQ_TXT];
    mq_prob_t vis;
    if (g->help_step == 0)
        return mq_read_chars(help_line[g->help_slot % MQ_NCAST]) + mq_read_chars(q->ex.text[0]) +
               mq_read_chars(q->ex.text[1]) + mq_read_chars(q->ex.text[2]);
    mq_steps(q, &q->ex, g->help_step - 1, out, &vis);
    return mq_read_chars(out[0]) + mq_read_chars(out[1]) + mq_read_chars(out[2]);
}
/* how long the moment stays before it moves on by itself: its reading time (never quicker than before) */
static uint32_t helper_ms(mq_t *q)
{
    uint32_t old = q->gm.help_step ? 6000u : 2400u, need = (uint32_t)mq_read_ms_len(q, helper_chars(q));
    return need > old ? need : old;
}
/* a tap: a step still typing is shown whole; else the next step */
void mq_helper_tap(mq_t *q)
{
    mq_game_t *g = &q->gm;
    if (q->scene == SC_PLAY && q->sub == GS_HELP && !mq_strat_on(q) && g->help_step &&
        mq_type_tap(q, g->help_t, helper_chars(q)))
        return;
    mq_helper_next(q);
}
/* the next step; after the last, back to her own order (the friend stays to cheer) */
void mq_helper_next(mq_t *q)
{
    mq_game_t *g = &q->gm;
    if (q->scene != SC_PLAY || q->sub != GS_HELP)
        return;
    if (mq_strat_help_next(q)) /* a way being shown: its next step */
        return;
    if (q->anim_t + 1 - g->help_t < HELP_IN && g->help_step == 0) { /* (still arriving) */
        g->help_t = q->anim_t - HELP_IN + 1;
        return;
    }
    g->help_t = q->anim_t + 1;
    char out[3][MQ_TXT];
    mq_prob_t vis;
    if (g->help_step < q->ex_n) {
        mq_steps(q, &q->ex, g->help_step, out, &vis);
        q->ex_step = g->help_step;
        g->help_step++;
        mq_speak(q, g->help_slot, out[0], MODX_SPEAK_NOW);
        q->redraw = true;
        return;
    }
    /* back to her own order: "Your turn! You can do it!" */
    q->sub = GS_PLAY;
    g->hud_ok = 0;
    g->typed[0] = 0, g->typed_n = 0;
    q->prob_start = q->now, q->last_input = q->now;
    mq_speak(q, g->help_slot, "Your turn! You can do it!", MODX_SPEAK_NOW);
    q->redraw = true;
}
/* the moment moves on by itself if she waits (a calm pace: 6 s a step) */
void mq_helper_frame(mq_t *q)
{
    mq_game_t *g = &q->gm;
    if (q->sub == GS_HELP && mq_strat_on(q)) { /* a way being shown keeps its own pace */
        mq_strat_frame(q);
        return;
    }
    if (q->sub == GS_HELP && q->anim_t + 1 - g->help_t > helper_ms(q)) {
        mq_read_note(q, q->anim_t + 1 - g->help_t, (int)helper_ms(q), false);
        mq_helper_next(q);
    }
}

/* where the helper friend is drawn now (the entrance: a hop, a flight or a pop) */
static void helper_pos(mq_t *q, int *x, int *y, int *s)
{
    mq_game_t *g = &q->gm;
    int el = (int)(q->anim_t + 1 - g->help_t), k = help_entry[g->help_slot % MQ_NCAST];
    *s = 56, *x = 240, *y = FY + 14;
    if (g->help_step || el >= HELP_IN)
        return;
    if (k == 0) /* hops in from the right */
        *x = 330 - (330 - 240) * el / HELP_IN, *y = FY + 14 - (int)(mq_isin((el * 540 / HELP_IN) % 180) * 18 / 1024);
    else if (k == 1) /* flies down from above */
        *y = FY - 60 + (74 * el / HELP_IN);
    else /* pops up, growing */
        *s = 56 * el / HELP_IN + 4, *x = 240 + (56 - *s) / 2, *y = FY + 14 + (56 - *s);
}

void mq_helper_draw(mq_t *q)
{
    mq_game_t *g = &q->gm;
    const modx_api_t *api = q->api;
    int x, y, s;
    if (mq_strat_help_draw(q)) /* a way being shown */
        return;
    helper_pos(q, &x, &y, &s);
    mq_rrect(q, 4, FY + 2, 312, FH - 4, 12, Q_CREAM);
    char out[3][MQ_TXT];
    mq_prob_t vis;
    int el = (int)(q->anim_t + 1 - g->help_t);
    if (g->help_step == 0) { /* arriving: "I'll help!" -- the similar order already there behind (audit H1) */
        mq_steps(q, &q->ex, 0, out, &vis);
        if (q->ex.stg)
            mq_stage_scene(q, &q->ex, 0, FY + 2, FY + 96, false);
        else if (vis.vis != V_NONE)
            mq_visual(q, &vis, 8, FY + 4, 200, 92, 0);
        mq_rrect(q, 8, FY + 98, 256, 44, 8, Q_WHITE);
        for (int i = 0; i < 3; i++)
            mq_text(q, api->font_small, 14, FY + 100 + i * 13, q->ex.text[i], Q_INK);
        mq_cast(q, g->help_slot % MQ_NCAST, x, y, s, el < HELP_IN ? AN_FLY : AN_HAPPY);
        if (el > HELP_IN / 2) {
            const char *l = help_line[g->help_slot % MQ_NCAST];
            int w = mq_text_w(q, api->font_medium, l) + 20;
            mq_rrect(q, 230 - w, FY + 24, w, 24, 10, Q_WHITE);
            mq_text(q, api->font_medium, 240 - w, FY + 28, l, Q_HOTPINK);
            for (int k = 0; k < 3; k++)
                mq_star(q, 40 + k * 60, FY + 80 + (int)((q->anim_t / 150 + (uint32_t)k) % 3) * 3, 5, Q_YELLOW);
        }
        return;
    }
    /* a step of the walkthrough with the order's things, the friend telling it */
    int st = g->help_step - 1;
    mq_steps(q, &q->ex, st, out, &vis);
    if (vis.kind == K_SHADE)
        vis.v[5] = (int16_t)((1 << vis.ans) - 1), vis.v[4] = -1;
    if (vis.kind == K_LINE)
        vis.v[3] = (int16_t)(st + 1 >= q->ex_n ? vis.ans : -1), vis.v[5] = -1;
    int stage = imin(1000, el * 2);
    if (st + 1 < q->ex_n)
        stage = stage * (st + 1) / q->ex_n;
    if (q->ex.stg && st == 0) /* the story first, acted */
        mq_stage_scene(q, &q->ex, 420 + el, FY + 2, FY + 96, false);
    else
        mq_visual(q, &vis, 8, FY + 4, 200, 92, stage);
    bool typing = q->say_t && q->anim_t - q->say_t < 1600;
    mq_rrect(q, 8, FY + 98, 256, 44, 8, Q_WHITE);
    mq_cast(q, g->help_slot % MQ_NCAST, 266, FY + 96, 46, typing ? AN_TALK : AN_HAPPY); /* the friend telling it */
    int shown = mq_type_shown(q, g->help_t); /* a typewriter at her reading pace (a tap: the whole line) */
    for (int i = 0; i < 3; i++) {
        char l[MQ_TXT];
        int k = 0;
        for (; out[i][k] && k < shown && k < MQ_TXT - 1; k++)
            l[k] = out[i][k];
        l[k] = 0;
        shown -= k;
        mq_text(q, api->font_small, 14, FY + 100 + i * 13, l, Q_INK);
    }
    for (int i = 0; i < q->ex_n; i++)
        mq_circle(q, 212 + i * 10, FY + 92, 3, i <= st ? Q_HOTPINK : Q_GREY);
}

/* her own order again with the friend beside her: cheering, or (together) pointing at it */
void mq_helper_cheer_draw(mq_t *q, bool behind)
{
    mq_game_t *g = &q->gm;
    if (!g->helped || q->sub != GS_PLAY || g->scene_on)
        return;
    int bob = (int)(q->anim_t / 180 % 2);
    if (behind) { /* (under the answers: an answer rising past never hides behind the friend -- the render audit) */
        mq_cast(q, g->help_slot % MQ_NCAST, 276, FY + 2 - bob, 40, g->hoh ? AN_TALK : AN_HAPPY);
        return;
    }
    if (g->hoh) { /* together: the friend points, the target glows */
        for (int i = 0; i < GM_NOBJ; i++)
            if (g->o[i].kind == 1 && !((g->gone >> g->o[i].opt) & 1))
                mq_ring(q, g->o[i].x / 16, g->o[i].y / 16, 22 + (int)(q->anim_t / 120 % 3), Q_HOTPINK);
        if (g->hoh == 2) /* a game that is set: the go button glows */
            mq_ring(q, 284, FB - 19, 24 + (int)(q->anim_t / 120 % 3), Q_HOTPINK);
        mq_rrect(q, 186, FY + 4, 88, 16, 8, Q_WHITE);
        mq_text_c(q, q->api->font_small, 230, FY + 5, "Together: this one!", Q_HOTPINK);
    }
}
