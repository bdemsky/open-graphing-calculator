/*
 * Math Quest: fun beats, squishies and juice.
 *
 * The rhythm. Playing the maths fills an invisible budget of pure fun: about 10 s for every
 * 90 s of maths (the grown-ups set the share, 10 % by default), never more than FUN_CAP so it
 * cannot be hoarded. At natural moments (a streak, the end of a run, a boss won, or when play
 * gets hard: several misses or slow answers) the scheduler slips in a short fun beat if the
 * budget allows and the last beat was a while ago (never two in a row): a
 * bubble pop, a catch burst, a dance to tap along to, a story moment, a squishy to squish, a
 * new outfit to try or a new thing to place. Each lasts 8-10 s, then the story carries on
 * into the next maths moment. Rooms and dress-up that she opens herself spend the same budget
 * (no more beats until maths fills it again); a long stay at home is the home story's
 * (mq_homestory.c: a friend who needs her) -- there is no meter, no label and no blocking message.
 *
 * Squishies: kawaii squishy toys (a bun, a peach, a cat, a cloud, a mochi, a bear, a
 * strawberry, a star) in four colours and a rare sparkly variant, collected on a shelf.
 * Pressing one squishes it: a ring of spring-mass points (integer Q8) deforms under the
 * finger, drag stretches it, a pinch squeezes it, and it slowly rises back with a little face
 * reacting. Streaks give squishy dough; the squishy maker turns dough into a new one.
 *
 * Juice: coins that fly to the counter, praise that never repeats back to back, a curtain
 * between scenes. All state is in mq_t (RW) and mq_home_t (saved).
 */
#include "mq.h"

#define FUN_CAP 20000u           /* the budget never holds more than this (ms) */
#define FUN_MIN 6000u            /* a beat needs at least this much */
#define BEAT_GAP 3               /* maths moments between two beats, at least */

enum { HK_LEFT = 13, HK_RIGHT = 14, HK_UP = 12, HK_DOWN = 15, HK_ENTER = 49, HK_DEL = 8 };
enum { TV_DOWN = 1, TV_DRAG, TV_UP, TV_TAP, TV_PINCH, TV_PINCH_END, TV_CANCEL };

static const char beat_name[BEAT_COUNT][10] = {"", "bubbles", "catch", "dance", "story", "squishy", "dress",
                                               "place", "maker", "jam"};
static const char sq_name[SQ_KINDS][12] = {"bun", "peach", "cat", "cloud", "mochi", "bear", "strawberry",
                                           "star"};
/* Each kind's four colours (body, shade). The ones that are real food keep their own colour, in shades of it
 * (the user's daughter: "it's weird to call a green squishy a strawberry"): a strawberry is red (with black seeds
 * and a green leafy top), a peach is peach; the others -- a bun, a cat, a cloud, a mochi, a bear, a star -- may be
 * any of the four pastels. A colour is a variant of its kind (the collection's bits stay kind x variant: an old
 * save's "green strawberry" is now a light red one). */
static const uint16_t sq_free[SQ_COLS][2] = {
    {RGB(255, 236, 214), RGB(240, 200, 160)}, {RGB(255, 196, 214), RGB(240, 150, 180)},
    {RGB(206, 236, 255), RGB(150, 200, 240)}, {RGB(220, 246, 214), RGB(160, 214, 150)}};
static const uint16_t sq_berry[SQ_COLS][2] = { /* red, deep red, pink-red, light red */
    {RGB(240, 70, 86), RGB(196, 40, 60)}, {RGB(204, 36, 56), RGB(150, 20, 40)},
    {RGB(255, 112, 140), RGB(220, 70, 104)}, {RGB(255, 150, 150), RGB(232, 104, 110)}};
static const uint16_t sq_peach[SQ_COLS][2] = { /* peach, pink peach, golden peach, blush */
    {RGB(255, 200, 160), RGB(240, 160, 120)}, {RGB(255, 180, 170), RGB(236, 136, 128)},
    {RGB(255, 214, 140), RGB(236, 176, 96)}, {RGB(255, 190, 180), RGB(232, 150, 150)}};
static const char sq_free_name[SQ_COLS][8] = {"cream", "pink", "blue", "mint"};
static const char sq_berry_name[SQ_COLS][10] = {"red", "deep red", "pink", "light red"};
static const char sq_peach_name[SQ_COLS][10] = {"peachy", "pink", "golden", "rosy"};
uint16_t mq_squish_colour(int kind, int col, bool shade)
{
    const uint16_t(*p)[2] = kind == SQ_BERRY ? sq_berry : kind == SQ_PEACH ? sq_peach : sq_free;
    return p[(col % SQ_COLS + SQ_COLS) % SQ_COLS][shade ? 1 : 0];
}
/* "sparkly pink strawberry", "blue cat": the name follows the look */
void mq_squish_name(int kind, int col, bool sparkly, char *out, int n)
{
    const char *c = kind == SQ_BERRY ? sq_berry_name[col % SQ_COLS] : kind == SQ_PEACH ? sq_peach_name[col % SQ_COLS]
                                                                                      : sq_free_name[col % SQ_COLS];
    int k = 0;
    for (const char *parts[4] = {sparkly ? "sparkly " : "", c, " ", sq_name[kind % SQ_KINDS]}, **pp = parts; pp < parts + 4; pp++)
        for (const char *t = *pp; *t && k < n - 1; t++)
            out[k++] = *t;
    out[k] = 0;
}

static int imin(int a, int b) { return a < b ? a : b; }
static uint16_t light(uint16_t c) { return (uint16_t)(((c & 0xF7DE) >> 1) + (0xF7DE >> 1)); } /* half way to white */
static int imax(int a, int b) { return a > b ? a : b; }
static int iabs(int a) { return a < 0 ? -a : a; }
static int isqrt(int v)
{
    int r = 0, b = 1 << 30;
    if (v <= 0)
        return 0;
    while (b > v)
        b >>= 2;
    while (b) {
        if (v >= r + b)
            v -= r + b, r = (r >> 1) + b;
        else
            r >>= 1;
        b >>= 2;
    }
    return r;
}

const char *mq_beat_name(int b) { return b >= 0 && b < BEAT_COUNT ? beat_name[b] : ""; }

/* ------------------------------------------------------------------ the budget */
int mq_fun_pct(const mq_profile_t *p) { return p->pad[0] ? p->pad[0] - 1 : MQ_FUN_PCT; } /* 0 stored = the default */
#define fun_pct mq_fun_pct

static bool maths_scene(int s)
{
    return s == SC_PLAY || s == SC_FEEDBACK || s == SC_EXAMPLE || s == SC_OFFER || s == SC_FACTGAME ||
           s == SC_INTRO; /* (the action games count their own moments: mq_action.c) */
}
/* the scenes that spend the budget: the beats and her home. The shop and the book are not (shopping with the coins
 * she earned and looking at her book are hers: the user, "a star fell from the sky ... keeps me from shopping"),
 * nor the Sparkle Kitchen (its orders are fractions to make: maths, booked like a round's) */
static bool fun_scene(int s)
{
    return s == SC_FUN || s == SC_ROOM || s == SC_WARDROBE || s == SC_SHOWOFF;
}

/* Time passes: maths fills the budget, fun spends it. */
void mq_fun_time(mq_t *q, uint32_t dt, bool maths)
{
    int pct = fun_pct(mq_prof(q));
    if (maths) {
        q->fun_acc += dt * (uint32_t)pct;
        uint32_t earn = q->fun_acc / (uint32_t)(100 - (pct < 100 ? pct : 99));
        q->fun_acc -= earn * (uint32_t)(100 - (pct < 100 ? pct : 99));
        q->fun_ms = q->fun_ms + earn > FUN_CAP ? FUN_CAP : q->fun_ms + earn;
        q->maths_ms += dt;
    } else
        q->fun_ms = q->fun_ms > dt ? q->fun_ms - dt : 0;
}

/* A maths moment happened (an answer): beats need a few of them in between. */
void mq_fun_maths_moment(mq_t *q)
{
    if (q->since_beat < 255)
        q->since_beat++;
}

/* A beat now? reason: FUN_STREAK, FUN_RUN_END, FUN_BOSS, FUN_DIP. Picks the beat (rotating,
 * never the same twice running, only beats that make sense now) and starts it; -> true if one
 * started (the caller then waits for mq_fun_resume). */
bool mq_fun_maybe(mq_t *q, int reason, int ret)
{
    if (q->fun_off || q->fun_ms < FUN_MIN || q->since_beat < BEAT_GAP)
        return false;
    mq_home_t *h = mq_home(q);
    bool can[BEAT_COUNT];
    for (int b = 0; b < BEAT_COUNT; b++)
        can[b] = true;
    can[BEAT_DRESS] = q->new_outfit != 0;   /* only when an outfit just arrived */
    can[BEAT_PLACE] = q->new_item != 0 && ret != FR_RUN; /* something new to place (not in the middle of a run) */
    can[BEAT_MAKER] = h->dough >= 2;        /* enough dough for a squishy */
    can[BEAT_GONE0] = false; /* (pet time, removed: "kind of lame") */
    can[BEAT_STORY] = reason == FUN_RUN_END || reason == FUN_BOSS;
    /* the rotation: the beats that carry numbers (bubbles to count by or make ten, stars to catch by a
     * times table) come round three times as often as each of the others */
    static const uint8_t rot[] = {BEAT_BUBBLES, BEAT_CATCH, BEAT_JAM,   BEAT_BUBBLES, BEAT_SQUISH, BEAT_CATCH,
                                  BEAT_DANCE,   BEAT_BUBBLES, BEAT_STORY, BEAT_CATCH,   BEAT_MAKER};
    int pick = -1;
    if (can[BEAT_DRESS] && q->last_beat != BEAT_DRESS)
        pick = BEAT_DRESS;
    else if (can[BEAT_PLACE] && q->last_beat != BEAT_PLACE)
        pick = BEAT_PLACE;
    else
        for (int k = 1; k <= (int)sizeof rot && pick < 0; k++) { /* the next in the rotation */
            int i = (q->beat_rot + k) % (int)sizeof rot, b = rot[i];
            if (can[b] && b != q->last_beat)
                pick = b, q->beat_rot = (uint8_t)i;
        }
    if (pick < 0)
        return false;
    mq_fun_start(q, pick, ret);
    return true;
}

static void sq_arrive(mq_t *q);
static void sq_init(mq_squish_t *s, int kind, int cx, int cy);
static void catch_begin(mq_t *q);

/* the story beat: a line more every reading time of a line (3 s at least) */
static uint32_t story_line_ms(mq_t *q)
{
    int ms = mq_read_ms_len(q, 90) / 3; /* (its three lines: up to 30 characters each, read as one text) */
    return (uint32_t)(ms > 3000 ? ms : 3000);
}
void mq_fun_start(mq_t *q, int beat, int ret)
{
    q->beat = (uint8_t)beat, q->last_beat = (uint8_t)beat, q->beat_ret = (uint8_t)ret;
    q->beat_t = q->anim_t, q->beat_score = 0, q->since_beat = 0;
    q->beat_len = beat == BEAT_STORY ? 3 * story_line_ms(q) : beat == BEAT_SQUISH || beat == BEAT_MAKER ? 10000 : 8000; /* short beats */
    for (int i = 0; i < 12; i++)
        q->fo[i].on = 0;
    if (beat == BEAT_BUBBLES)
        mq_bubbles_begin(q);
    if (beat == BEAT_CATCH) /* the stars carry a count by a step: catch the next number, a long run of them */
        catch_begin(q);
    q->basket_x = 140;
    q->maker_kind = 0, q->maker_col = 0;
    if (beat == BEAT_SQUISH)
        sq_arrive(q);
    if (beat == BEAT_DRESS) {
        q->new_outfit = 0;
        mq_home_enter(q, SC_WARDROBE);
        q->hook_t = 0;
        return;
    }
    if (beat == BEAT_PLACE) {
        q->new_item = 0;
        mq_home_enter(q, SC_ROOM);
        return;
    }
    if (beat == BEAT_JAM) { /* drum along with Puff (the toy box's jam) */
        mq_toys_jam(q, ret, q->beat_len);
        return;
    }
    mq_go(q, SC_FUN);
}

/* the beat is over: on with the story */
static void beat_end(mq_t *q)
{
    q->beat = 0xFF;
    mq_fun_resume(q, q->beat_ret);
}

/* ------------------------------------------------------------------ squishies */
/* rest radius (Q8 px) of point i of n of a squishy kind */
static int32_t sq_rest(int kind, int i, int n)
{
    int a = i * 360 / n; /* 0 = up, clockwise */
    int r = 32;
    int up = mq_icos(a); /* 1024 at the top */
    switch (kind) {
    case SQ_BUN: r = 32 - (up < -600 ? (-up - 600) / 60 : 0); break; /* a flat bottom */
    case SQ_PEACH: r = 33 - (iabs(a - 0) < 18 || iabs(a - 360) < 18 ? 7 : 0); break; /* the dip */
    case SQ_CAT: r = 30 + ((iabs(a - 40) < 14 || iabs(a - 320) < 14) ? 14 : 0); break; /* ears */
    case SQ_CLOUD: r = 30 + mq_icos(a * 5) * 4 / 1024; break;
    case SQ_MOCHI: r = 30 + iabs(mq_isin(a)) * 8 / 1024; break; /* wide */
    case SQ_BEAR: r = 30 + ((iabs(a - 45) < 18 || iabs(a - 315) < 18) ? 9 : 0); break; /* round ears */
    case SQ_BERRY: r = 26 + (up < 0 ? -up * 10 / 1024 : 0) - (up > 0 ? up * 2 / 1024 : 0); break;
    default: r = 24 + (mq_icos(a * 5) + 1024) * 10 / 2048; break; /* a soft star */
    }
    return (int32_t)r << 8;
}

static void sq_init(mq_squish_t *s, int kind, int cx, int cy)
{
    s->kind = (uint8_t)kind, s->cx = (int16_t)cx, s->cy = (int16_t)cy;
    s->squeeze = 256, s->press = 0, s->grab = -1, s->pokes = 0;
    for (int i = 0; i < SQ_N; i++) {
        int a = i * 360 / SQ_N;
        int32_t r = sq_rest(kind, i, SQ_N);
        s->rx[i] = (int32_t)mq_isin(a) * r / 1024, s->ry[i] = -(int32_t)mq_icos(a) * r / 1024;
        s->px[i] = s->rx[i], s->py[i] = s->ry[i], s->vx[i] = s->vy[i] = 0;
    }
}

/* One step of the soft body (40 ms, 4 substeps): every point is pulled to its rest place
 * (slowly: the slow rise), smoothed with its neighbours, damped; a finger pushes the points
 * under it towards the middle, a grabbed point follows the finger, a pinch scales the rest
 * shape. Integer Q8; stable for any input (velocities and offsets are clamped). */
void mq_squish_step(mq_squish_t *s)
{
    /* pressed: the whole squishy squashes (wider, lower), then slowly rises back */
    int sqx = s->squeeze + (s->press ? 34 : 0), sqy = s->squeeze - (s->press ? 52 : 0);
    for (int sub = 0; sub < 4; sub++) {
        for (int i = 0; i < SQ_N; i++) {
            int j = (i + SQ_N - 1) % SQ_N, k = (i + 1) % SQ_N;
            /* d = offset from the rest place; smoothing acts on the offsets, so rest is the only calm state */
            int32_t dxi = s->px[i] - s->rx[i] * sqx / 256, dyi = s->py[i] - s->ry[i] * sqy / 256;
            int32_t dxj = s->px[j] - s->rx[j] * sqx / 256, dyj = s->py[j] - s->ry[j] * sqy / 256;
            int32_t dxk = s->px[k] - s->rx[k] * sqx / 256, dyk = s->py[k] - s->ry[k] * sqy / 256;
            int32_t fx = -dxi / 24 + (dxj + dxk - 2 * dxi) / 8, fy = -dyi / 24 + (dyj + dyk - 2 * dyi) / 8;
            if (!fx && dxi) /* integer steps still reach rest */
                fx = dxi > 0 ? -1 : 1;
            if (!fy && dyi)
                fy = dyi > 0 ? -1 : 1;
            s->vx[i] = (s->vx[i] + fx) * 13 / 16, s->vy[i] = (s->vy[i] + fy) * 13 / 16; /* well damped */
            s->vx[i] = s->vx[i] > 4096 ? 4096 : s->vx[i] < -4096 ? -4096 : s->vx[i];
            s->vy[i] = s->vy[i] > 4096 ? 4096 : s->vy[i] < -4096 ? -4096 : s->vy[i];
        }
        for (int i = 0; i < SQ_N; i++) {
            s->px[i] += s->vx[i], s->py[i] += s->vy[i];
            if (s->press) { /* the finger: points inside its circle are pushed in */
                int32_t dx = s->px[i] - s->fx, dy = s->py[i] - s->fy;
                int d = isqrt((int)((dx / 16) * (dx / 16) + (dy / 16) * (dy / 16))) * 16, R = 22 * 256;
                if (d >= R && d < 3 * R) { /* further away: the squish spreads out a little */
                    int32_t len = isqrt((int)((s->px[i] / 16) * (s->px[i] / 16) + (s->py[i] / 16) * (s->py[i] / 16))) * 16;
                    if (len > 0) {
                        int32_t out = (3 * R - d) / 12;
                        s->px[i] += s->px[i] * out / len / 4, s->py[i] += s->py[i] * out / len / 4;
                    }
                }
                if (d < R) {
                    int32_t len = isqrt((int)((s->px[i] >> 4) * (s->px[i] >> 4) + (s->py[i] >> 4) * (s->py[i] >> 4))) << 4;
                    if (len > 0) {
                        int32_t in = R - d;
                        s->px[i] -= s->px[i] * in / len, s->py[i] -= s->py[i] * in / len;
                    }
                }
            }
            if (s->grab == i) /* a stretch: the grabbed point follows the finger */
                s->px[i] += (s->fx - s->px[i]) / 3, s->py[i] += (s->fy - s->py[i]) / 3;
            int32_t lim = 70 << 8;
            s->px[i] = s->px[i] > lim ? lim : s->px[i] < -lim ? -lim : s->px[i];
            s->py[i] = s->py[i] > lim ? lim : s->py[i] < -lim ? -lim : s->py[i];
        }
    }
}

/* how far from rest (Q8 px, the largest point offset) */
int32_t mq_squish_strain(const mq_squish_t *s)
{
    int32_t m = 0;
    for (int i = 0; i < SQ_N; i++) {
        int32_t d = iabs(s->px[i] - s->rx[i] * s->squeeze / 256) + iabs(s->py[i] - s->ry[i] * s->squeeze / 256);
        m = d > m ? d : m;
    }
    return m;
}

/* finger events on the squishy (screen coordinates); pinch: scale in 1/256 */
void mq_squish_touch(mq_squish_t *s, int type, int x, int y, int scale)
{
    int32_t fx = (int32_t)(x - s->cx) * 256, fy = (int32_t)(y - s->cy) * 256;
    switch (type) {
    case TV_DOWN:
        s->press = 1, s->fx = fx, s->fy = fy, s->grab = -1, s->pokes++;
        break;
    case TV_DRAG: { /* a drag from the edge grabs the nearest point and stretches */
        if (s->grab < 0) {
            int best = 0;
            int32_t bd = 0x7FFFFFFF;
            for (int i = 0; i < SQ_N; i++) {
                int32_t d = iabs(s->px[i] - s->fx) + iabs(s->py[i] - s->fy);
                if (d < bd)
                    bd = d, best = i;
            }
            s->grab = (int8_t)best;
            s->press = 0;
        }
        s->fx = fx, s->fy = fy;
        break;
    }
    case TV_PINCH: s->squeeze = imax(128, imin(320, scale)); break;
    case TV_UP: case TV_TAP: case TV_CANCEL: case TV_PINCH_END:
        s->press = 0, s->grab = -1, s->squeeze = 256;
        break;
    default: break;
    }
}

static void sq_draw(mq_t *q, const mq_squish_t *s, int col, bool sparkly, int scale_div)
{
    int16_t xs[SQ_N], ys[SQ_N];
    int32_t mx = 0, my = 0;
    for (int i = 0; i < SQ_N; i++) {
        xs[i] = (int16_t)(s->cx + (s->px[i] >> 8) / scale_div), ys[i] = (int16_t)(s->cy + (s->py[i] >> 8) / scale_div);
        mx += s->px[i] - s->rx[i], my += s->py[i] - s->ry[i];
    }
    /* the face follows the squish */
    int fx = s->cx + (int)(mx / SQ_N >> 8) / scale_div, fy = s->cy + (int)(my / SQ_N >> 8) / scale_div + 4 / scale_div;
    int strain = (int)(mq_squish_strain(s) >> 8);
    int g = 9 / scale_div + 1;
    /* (a high-resolution platform may draw it from its own art, the points as they are: MODX_HR_SPRITE) */
    int32_t sv[8 + 2 * SQ_N] = {s->kind, mq_squish_colour(s->kind, col, false), sparkly, strain > 6, (q->anim_t / 40) % 60 == 0,
                                fx - s->cx, fy - s->cy, g};
    for (int i = 0; i < SQ_N; i++)
        sv[8 + 2 * i] = xs[i] - s->cx, sv[9 + 2 * i] = ys[i] - s->cy;
    int hr = mq_hr_sprite(q, "squishy", s->cx - 72 / scale_div, s->cy - 72 / scale_div, 144 / scale_div, 144 / scale_div, sv,
                          8 + 2 * SQ_N);
    uint16_t body = mq_squish_colour(s->kind, col, false), shade = mq_squish_colour(s->kind, col, true);
    /* the rise's wobble: just after it settles from a squash, a soft decaying jiggle (drawn, not simulated) */
    int wob = 0;
    if (scale_div == 1 && q->sq_rise_t && q->anim_t - q->sq_rise_t < 700) {
        int el = (int)(q->anim_t - q->sq_rise_t);
        wob = mq_isin(el * 360 / 230 % 360) * (700 - el) / 700 * 3 / 1024;
    }
    for (int i = 0; i < SQ_N; i++) /* (the jiggle: the top bobs, the bottom stays on the shelf) */
        ys[i] = (int16_t)(ys[i] - (ys[i] < s->cy ? wob : 0));
    /* soft shading: the body in its shade, a lighter body over it nudged up-left (the light), a glossy
     * highlight, an outline that follows the soft body all round */
    mq_poly(q, xs, ys, SQ_N, shade);
    int16_t ls[SQ_N], lt[SQ_N];
    for (int i = 0; i < SQ_N; i++) {
        int dx = xs[i] - s->cx, dy = ys[i] - s->cy;
        ls[i] = (int16_t)(s->cx + dx * 7 / 8 - 2 / scale_div), lt[i] = (int16_t)(s->cy + dy * 7 / 8 - 2 / scale_div);
    }
    mq_poly(q, ls, lt, SQ_N, body);
    int top = s->cy, left = s->cx;
    for (int i = 0; i < SQ_N; i++)
        top = ys[i] < top ? ys[i] : top, left = xs[i] < left ? xs[i] : left;
    int hx = left + (s->cx - left) * 2 / 3, hy = top + (s->cy - top) / 2;
    mq_oval(q, hx, hy, 7 / scale_div + 1, 4 / scale_div + 1, light(body));
    mq_circle(q, hx - 2 / scale_div, hy - 1 / scale_div, 2 / scale_div + 1, Q_WHITE);
    for (int i = 0; i < SQ_N; i++) {
        mq_line(q, xs[i], ys[i], xs[(i + 1) % SQ_N], ys[(i + 1) % SQ_N], Q_INK);
        if (scale_div == 1) /* (a soft, thicker outline when big) */
            mq_line(q, xs[i], ys[i] + 1, xs[(i + 1) % SQ_N], ys[(i + 1) % SQ_N] + 1, Q_INK);
    }
    /* each kind's own little touch */
    int u = scale_div;
    switch (s->kind) {
    case SQ_BUN: /* a cinnamon swirl */
        mq_ring(q, fx + 12 / u, fy - 12 / u, 4 / u + 1, shade), mq_ring(q, fx + 12 / u, fy - 12 / u, 2 / u + 1, shade);
        break;
    case SQ_PEACH: /* a leaf on top */
        mq_oval(q, s->cx + 5 / u, top + 2 / u, 5 / u + 1, 2 / u + 1, Q_GREEN);
        break;
    case SQ_CAT: /* whiskers */
        for (int k = -1; k <= 1; k += 2) {
            mq_line(q, fx + k * (g + 4 / u), fy + 3 / u, fx + k * (g + 12 / u), fy + 1 / u, Q_INK);
            mq_line(q, fx + k * (g + 4 / u), fy + 5 / u, fx + k * (g + 12 / u), fy + 6 / u, Q_INK);
        }
        break;
    case SQ_CLOUD: /* a little rainbow tuft */
        mq_fill(q, s->cx - 10 / u, top + 3 / u, 20 / u + 1, 2 / u + 1, Q_PINK), mq_fill(q, s->cx - 8 / u, top + 5 / u, 16 / u + 1, 1, Q_YELLOW);
        break;
    case SQ_MOCHI: /* a dusting of flour */
        for (int k = 0; k < 4; k++)
            mq_fill(q, s->cx - 14 / u + k * 9 / u, top + 6 / u + (k % 2) * 3 / u, 1, 1, Q_WHITE);
        break;
    case SQ_BEAR: /* a muzzle */
        mq_oval(q, fx, fy + 4 / u, 6 / u + 1, 4 / u + 1, light(body));
        break;
    case SQ_BERRY: /* black seeds (she asked for black spots) and a leafy green top */
        for (int k = 0; k < 5; k++)
            mq_fill(q, s->cx - 12 / u + k * 6 / u, s->cy + 10 / u + (k % 2) * 5 / u, 1, 2 / u + 1, Q_INK);
        mq_oval(q, s->cx, top + 1, 8 / u + 1, 3 / u + 1, Q_GREEN);
        break;
    default: /* the star: a twinkle in its point */
        mq_star(q, s->cx, top + 6 / u, 2 / u + 1, Q_WHITE);
        break;
    }
    /* the face: big shiny eyes and blush; squeezed: > < and an "o"; settled after a squash: a happy ^ ^ */
    bool happy = scale_div == 1 && q->sq_rise_t && q->anim_t - q->sq_rise_t < 900;
    if (strain > 6) {
        mq_line(q, fx - g - 2, fy - 3, fx - g + 1, fy - 1, Q_INK), mq_line(q, fx - g + 1, fy - 1, fx - g - 2, fy + 1, Q_INK);
        mq_line(q, fx + g + 2, fy - 3, fx + g - 1, fy - 1, Q_INK), mq_line(q, fx + g - 1, fy - 1, fx + g + 2, fy + 1, Q_INK);
        mq_circle(q, fx, fy + 5 / scale_div + 1, 2, Q_HOTPINK);
    } else if (happy) {
        for (int k = -1; k <= 1; k += 2) {
            mq_line(q, fx + k * g - 2, fy, fx + k * g, fy - 2, Q_INK), mq_line(q, fx + k * g, fy - 2, fx + k * g + 2, fy, Q_INK);
        }
        mq_line(q, fx - 3, fy + 4, fx, fy + 6, Q_INK), mq_line(q, fx, fy + 6, fx + 3, fy + 4, Q_INK);
    } else {
        bool blink = (q->anim_t / 40) % 60 == 0;
        int eh = scale_div == 1 ? 5 : 3;
        mq_fill(q, fx - g - 1, fy - (blink ? 0 : eh / 2), 3, blink ? 1 : eh, Q_INK);
        mq_fill(q, fx + g - 1, fy - (blink ? 0 : eh / 2), 3, blink ? 1 : eh, Q_INK);
        if (!blink && scale_div == 1) /* shines in the eyes */
            mq_fill(q, fx - g, fy - eh / 2, 1, 1, Q_WHITE), mq_fill(q, fx + g, fy - eh / 2, 1, 1, Q_WHITE);
        mq_line(q, fx - 2, fy + 4, fx, fy + 5, Q_INK), mq_line(q, fx, fy + 5, fx + 2, fy + 4, Q_INK);
    }
    bool giggling = scale_div == 1 && q->sq_gig_t && q->anim_t - q->sq_gig_t < 700;
    int bl = giggling ? 2 + q->sq_gig_lvl : 0; /* the blush deepens with the giggle */
    mq_oval(q, fx - g - 4, fy + 4, 3 / scale_div + 1 + bl / 2, 2 / scale_div + 1 + bl / 3, giggling ? Q_HOTPINK : Q_BLUSH);
    mq_oval(q, fx + g + 4, fy + 4, 3 / scale_div + 1 + bl / 2, 2 / scale_div + 1 + bl / 3, giggling ? Q_HOTPINK : Q_BLUSH);
    if (sparkly)
        for (int i = 0; i < 3; i++)
            mq_star(q, s->cx + (i - 1) * 22 / scale_div, s->cy - 30 / scale_div + (int)((q->anim_t / 160 + (uint32_t)i) % 3),
                    3, Q_YELLOW);
    if (hr)
        mq_hr_end(q);
    /* (outside the squishy's own drawing: a platform with art of its own for it keeps these) */
    if (giggling) { /* "hee hee" and hearts float up: more for a bigger squeeze */
        int el = (int)(q->anim_t - q->sq_gig_t), up = el / 20;
        mq_text_c(q, q->api->font_small, s->cx + 34, s->cy - 46 - up, q->sq_gig_lvl >= 2 ? "hee hee hee!" : "hee hee", Q_HOTPINK);
        for (int k = 0; k <= q->sq_gig_lvl; k++)
            mq_heart(q, s->cx - 30 + k * 20, s->cy - 40 - up - (k % 2) * 6, 4, Q_HOTPINK);
    }
    if (happy) /* risen all the way: a sparkle */
        for (int k = 0; k < 4; k++) {
            int el = (int)(q->anim_t - q->sq_rise_t), a = k * 90 + el / 4, r = 30 + el / 30;
            mq_star(q, s->cx + mq_icos(a) * r / 1024, s->cy - 8 + mq_isin(a) * r / 1024, 5 + (el / 120 + k) % 2 * 2, Q_YELLOW);
        }
}

/* the big squishy's moments: squashed hard, then settled (its wobble, its sparkle, a happy face) */
#define GIGGLE_GAP 450 /* ms between giggles at least: rapid squishing never spams them */
static void sq_watch(mq_t *q)
{
    int strain = (int)(mq_squish_strain(&q->sq) >> 8);
    { /* ticklish giggles: a poke a tiny one, a big squash a longer laugh, a slow press one that builds; a
       * happy sigh (or a boing) on letting go -- every kind in its own voice (the param's kind) */
        int lvl = strain > 24 ? 3 : strain > 14 ? 2 : strain > 6 ? 1 : 0, kind = q->sq.kind % SQ_KINDS;
        bool press = q->sq.press != 0 || q->sq.grab >= 0, gap = !q->sq_gig_t || q->anim_t - q->sq_gig_t >= GIGGLE_GAP; /* (a stretch too) */
        if (press && !q->sq_was_press)
            q->sq_press_t = q->anim_t;
        bool slow = press && q->anim_t - q->sq_press_t > 700;
        if (press && gap && (!q->sq_was_press || lvl > q->sq_gig_lvl || slow)) {
            mq_sfx(q, MODX_SFX_GIGGLE, kind | lvl << 4 | (slow ? 1 << 6 : 0));
            q->sq_gig_t = mq_mark(q), q->sq_gig_lvl = (uint8_t)lvl;
        }
        if (!press && q->sq_was_press) {
            mq_sfx(q, MODX_SFX_SIGH, kind | (q->sq_gig_lvl >= 2 ? 1 << 4 : 0));
            q->sq_gig_lvl = 0;
        }
        q->sq_was_press = press;
    }
    if (strain > 8 || q->sq.press)
        q->sq_squashed = 1;
    else if (q->sq_squashed && strain <= 1) {
        q->sq_squashed = 0;
        q->sq_rise_t = mq_mark(q);
        mq_sfx(q, MODX_SFX_UNLOCK, 0);
    }
}

/* a small squishy at rest (the shelf, the book) */
void mq_squish_icon(mq_t *q, int kind, int col, bool sparkly, int cx, int cy)
{
    mq_squish_t s;
    sq_init(&s, kind, cx, cy);
    sq_draw(q, &s, col, sparkly, 4);
}

static int sq_bit(int kind, int col, bool sparkly) { return (sparkly ? 32 : 0) + kind * SQ_COLS + col; }
bool mq_squish_owned(const mq_home_t *h, int kind, int col, bool sparkly)
{
    int b = sq_bit(kind, col, sparkly);
    return (h->squish[b / 32] >> (b % 32)) & 1;
}
void mq_squish_give(mq_home_t *h, int kind, int col, bool sparkly)
{
    int b = sq_bit(kind, col, sparkly);
    h->squish[b / 32] |= 1u << (b % 32);
    if (!h->own[IT_SQUISHY_SHELF])
        h->own[IT_SQUISHY_SHELF] = 1; /* the first squishy brings its shelf */
}
int mq_squish_count(const mq_home_t *h)
{
    int n = 0;
    for (int b = 0; b < 64; b++)
        n += (h->squish[b / 32] >> (b % 32)) & 1;
    return n;
}

/* a new squishy arrives: one not owned yet when there is one, now and then a sparkly one */
static void sq_arrive(mq_t *q)
{
    mq_home_t *h = mq_home(q);
    int kind = 0, col = 0, tries = 0;
    bool sparkly = mq_rnd(q, 0, 7) == 0;
    do {
        kind = mq_rnd(q, 0, SQ_KINDS - 1), col = mq_rnd(q, 0, SQ_COLS - 1);
    } while (mq_squish_owned(h, kind, col, sparkly) && ++tries < 40);
    q->sq_col = (uint8_t)col, q->sq_sparkle = sparkly;
    q->sq_new = !mq_squish_owned(h, kind, col, sparkly);
    mq_squish_give(h, kind, col, sparkly);
    sq_init(&q->sq, kind, 160, MQ_Y0 + 108);
    q->dirty = true;
}

/* ------------------------------------------------------------------ the beats */
/* The bubbles beat carries a little maths, as play (no score kept, a wrong tap only wobbles):
 *   count by -- the bubbles hold the multiples of 2, 3, 4, 5 or 10 (and a few others): pop them in order;
 *   make ten -- pop two whose numbers make 10 (or 100): a pair, then the next;
 *   the bigger slice -- bubbles come two by two with fractions: pop the bigger one. */
static int bub_free(mq_t *q)
{
    for (int i = 0; i < 12; i++)
        if (!q->fo[i].on)
            return i;
    return -1;
}
static void bub_spawn(mq_t *q, int x, int v, int v2)
{
    int i = bub_free(q);
    if (i < 0)
        return;
    q->fo[i] = (mq_fobj_t){(int16_t)x, (int16_t)(MQ_Y0 + MQ_H + 14), (int16_t)mq_rnd(q, 16, 19), (uint8_t)mq_rnd(q, 0, 4), 1,
                           (int16_t)v, (int16_t)v2, 0};
}
static bool bub_has(mq_t *q, int v)
{
    for (int i = 0; i < 12; i++)
        if (q->fo[i].on && q->fo[i].v == v && !q->fo[i].v2)
            return true;
    return false;
}
void mq_bubbles_begin(mq_t *q)
{
    static const uint8_t steps[5] = {2, 5, 10, 3, 4};
    q->bub_mode = (uint8_t)((q->bub_mode + 1) % BB_COUNT);
    q->bub_step = steps[mq_rnd(q, 0, 4)];
    q->bub_next = q->bub_step;
    q->bub_target = mq_rnd(q, 0, 2) ? 10 : 100;
    q->bub_pick = 0;
}
static void bubbles_step(mq_t *q)
{
    uint32_t every = q->bub_mode == BB_BIGGER ? 1600 : 640; /* (a pair needs room: never overlapping) */
    if (q->anim_t % every < 40) { /* a new bubble (or a pair) */
        int x = mq_rnd(q, 34, 286);
        switch (q->bub_mode) {
        case BB_SKIP: { /* the next of the count is always on its way; the others its neighbours */
            int v = !bub_has(q, q->bub_next) ? q->bub_next : q->bub_next + q->bub_step * mq_rnd(q, 1, 2);
            if (mq_rnd(q, 0, 3) == 0)
                v = q->bub_next + mq_rnd(q, 1, q->bub_step > 2 ? q->bub_step - 1 : 1);
            bub_spawn(q, x, v, 0);
            break;
        }
        case BB_PAIRS: { /* numbers whose partners come too */
            int t = q->bub_target, a = t == 10 ? mq_rnd(q, 1, 9) : mq_rnd(q, 1, 9) * 10;
            bub_spawn(q, x, a, 0);
            if (mq_rnd(q, 0, 1))
                bub_spawn(q, x > 160 ? x - 110 : x + 110, t - a, 0);
            break;
        }
        default: { /* two fractions side by side, never the same size */
            int b1 = mq_rnd(q, 2, 8), b2 = mq_rnd(q, 2, 8), a1 = mq_rnd(q, 1, b1 - 1 > 0 ? b1 - 1 : 1), a2 = mq_rnd(q, 1, b2 - 1 > 0 ? b2 - 1 : 1);
            if (a1 * b2 == a2 * b1)
                a2 = a2 + 1 < b2 ? a2 + 1 : a2 - 1 > 0 ? a2 - 1 : a2;
            if (a1 * b2 == a2 * b1 || bub_free(q) < 0)
                break;
            bub_spawn(q, 90, a1, b1);
            bub_spawn(q, 230, a2, b2);
            break;
        }
        }
    }
    for (int i = 0; i < 12; i++)
        if (q->fo[i].on) {
            q->fo[i].y = (int16_t)(q->fo[i].y - 1 - (q->anim_t / 40 + (uint32_t)i) % 2);
            if (q->fo[i].wob)
                q->fo[i].wob--;
            if (q->fo[i].y < MQ_Y0 - 20)
                q->fo[i].on = 0;
        }
}
/* the bigger of fraction bubble i and its partner at the same height */
static int bub_partner(mq_t *q, int i)
{
    for (int j = 0; j < 12; j++)
        if (j != i && q->fo[j].on && q->fo[j].v2 && iabs(q->fo[j].y - q->fo[i].y) < 4)
            return j;
    return -1;
}
/* a tap on bubble i of the beat: it pops when it fits the little game, else it wobbles */
static void bub_tap(mq_t *q, int i)
{
    mq_fobj_t *b = &q->fo[i];
    bool pop = false;
    if (q->bub_mode == BB_SKIP) {
        if (b->v == q->bub_next)
            pop = true, q->bub_next = (int16_t)(q->bub_next + q->bub_step);
    } else if (q->bub_mode == BB_PAIRS) {
        if (!q->bub_pick)
            q->bub_pick = (uint8_t)(i + 1), b->wob = 6; /* picked: it glows */
        else if (q->bub_pick - 1 != i && q->fo[q->bub_pick - 1].on && q->fo[q->bub_pick - 1].v + b->v == q->bub_target) {
            q->fo[q->bub_pick - 1].on = 0, pop = true;
            q->bub_pick = 0;
        } else
            q->bub_pick = (uint8_t)(i + 1);
        if (!pop) {
            mq_sfx(q, MODX_SFX_TAP, 0);
            return;
        }
    } else {
        int j = bub_partner(q, i);
        if (j < 0 || b->v * q->fo[j].v2 > q->fo[j].v * b->v2) {
            pop = true;
            if (j >= 0)
                q->fo[j].on = 0;
        }
    }
    if (pop) {
        b->on = 0, q->beat_score++;
        mq_spark_burst(q, b->x, b->y, 8);
        mq_sfx(q, MODX_SFX_POP, q->beat_score);
    } else {
        b->wob = 10; /* not that one: a wobble, nothing lost */
        mq_sfx(q, MODX_SFX_TAP, 1);
    }
}

/* Catch the Stars: counting by the beat's step -- the next number of the count is the star to catch (3, 6, 9,
 * ...): skip-counting, the root of the times table, with the answer as the target; other numbers of the count
 * (the one just passed, the ones further on) and the numbers between bounce off gently (nothing lost). The stars
 * fall slowly at a speed that never changes and the next one always comes soon: it is the counting that is the
 * game, not the speed of a hand (the user: "what is its educational benefit?"). A round is a long run of the
 * count -- 10 to 12 catches from a varied start, well past ten times the step, now and then counted back (from
 * grade 3) -- so she really practises it (the user: "ends too quickly to really practice the skill"); it is maths
 * time, not the fun budget's, and ends when the count is done (or she has left it alone a while). */
#define CATCH_IDLE 25000u /* no move of the basket this long: the round ends (she has stopped playing it) */
static void catch_begin(mq_t *q)
{
    int g = mq_start_grade(q), tb = mq_fact_run_table(q);
    static const uint8_t low[] = {2, 5, 10, 3, 4}, high[] = {3, 4, 6, 7, 8, 9};
    int k = tb >= 2 && tb <= 10 && mq_rnd(q, 0, 1) ? tb /* (the table she is learning, half the time) */
            : g >= 4 ? high[mq_rnd(q, 0, (int)sizeof high - 1)] : low[mq_rnd(q, 0, (int)sizeof low - 1)];
    q->bub_step = (uint8_t)k;
    q->catch_len = (uint8_t)mq_rnd(q, 10, 12);
    q->catch_dir = (int8_t)(g >= 3 && mq_rnd(q, 0, 2) == 0 ? -1 : 1);
    int a = mq_rnd(q, q->catch_dir < 0 ? 1 : 0, 3); /* the lowest multiple of the count (never 0 counted back to) */
    q->catch_from = (int16_t)(q->catch_dir > 0 ? a : a + q->catch_len + 1);
    q->catch_t = q->anim_t, q->catch_end_t = 0;
    q->beat_len = 150000; /* (a cap: the count's end, or her leaving it, ends it) */
}
/* the k-th number of the count (0, 1: the two shown; 2...: the ones to catch) */
static int catch_term(const mq_t *q, int k) { return imax(2, q->bub_step) * (q->catch_from + q->catch_dir * k); }
int mq_catch_next(const mq_t *q) { return q->beat_score >= q->catch_len ? 0 : catch_term(q, q->beat_score + 2); }
static int16_t catch_num(mq_t *q)
{
    int k = imax(2, q->bub_step), next = mq_catch_next(q);
    bool falling = false;
    for (int i = 0; i < 12; i++)
        falling |= q->fo[i].on && !q->fo[i].wob && q->fo[i].v == next;
    if (!falling && mq_rnd(q, 0, 99) < 70)
        return (int16_t)next;
    int sg = mq_rnd(q, 0, 1) ? 1 : -1, v;
    if (mq_rnd(q, 0, 2) == 0) /* another number of the count: the one just passed, or further on */
        v = next + sg * k * mq_rnd(q, 1, 2);
    else /* a number between */
        v = next + sg * mq_rnd(q, 1, imax(1, k - 1));
    return (int16_t)(v <= 0 || v == next ? next + 1 : v);
}

static void catch_step(mq_t *q)
{
    int speed = 2; /* (a gentle fall that never speeds up: counting, not a race of hands) */
    bool done = !mq_catch_next(q);
    if (q->anim_t % 640 < 40 && !done)
        for (int i = 0; i < 12; i++)
            if (!q->fo[i].on) {
                q->fo[i] = (mq_fobj_t){(int16_t)mq_rnd(q, 24, 296), (int16_t)(MQ_Y0 + 46), 11, (uint8_t)mq_rnd(q, 0, 4), 1};
                q->fo[i].v = catch_num(q);
                break;
            }
    for (int i = 0; i < 12; i++)
        if (q->fo[i].on) {
            q->fo[i].y = (int16_t)(q->fo[i].y + speed);
            if (q->fo[i].y > MQ_Y0 + 164 && iabs(q->fo[i].x - q->basket_x) < 30 && !q->fo[i].wob) {
                if (!done && q->fo[i].v == mq_catch_next(q)) { /* the next number of the count: a sparkle */
                    q->fo[i].on = 0;
                    q->beat_score++;
                    mq_spark_burst(q, q->fo[i].x, MQ_Y0 + 168, 6);
                    mq_sfx(q, MODX_SFX_POP, q->beat_score);
                    if (!mq_catch_next(q)) { /* the whole count: a cheer, then on with the story */
                        done = true, q->catch_end_t = mq_mark(q);
                        q->beat_len = q->anim_t - q->beat_t + 3000;
                        mq_spark_burst(q, 160, MQ_Y0 + 60, 24);
                        mq_sfx(q, MODX_SFX_CHEER, 5);
                        for (int j = 0; j < 12; j++)
                            q->fo[j].wob = q->fo[j].on ? 1 : 0; /* (the rest float away) */
                    }
                } else
                    q->fo[i].wob = 1; /* not the next: it bounces off (nothing lost, no miss) */
            } else if (q->fo[i].y > MQ_Y0 + MQ_H || q->fo[i].x < -20 || q->fo[i].x > MQ_W + 20)
                q->fo[i].on = 0;
            if (q->fo[i].on && q->fo[i].wob)
                q->fo[i].x = (int16_t)(q->fo[i].x + (q->fo[i].x < q->basket_x ? -4 : 4));
        }
}

void mq_fun_frame(mq_t *q, uint32_t dt)
{
    bool maths = maths_scene(q->scene) || (q->scene == SC_FUN && q->beat == BEAT_CATCH); /* (the count is maths) */
    if (maths || fun_scene(q->scene))
        mq_fun_time(q, dt, maths);
    q->hook_t = 0; /* (no friend at the door any more: never a dialog over a screen she opened, never moved on) */
    /* (her home with the budget used up: the home story takes it from there, mq_homestory.c) */
    if (q->scene != SC_FUN)
        return;
    uint32_t el = q->anim_t - q->beat_t;
    if (q->beat == BEAT_BUBBLES)
        bubbles_step(q);
    if (q->beat == BEAT_CATCH) {
        uint32_t ms = 0;
        int k = q->api->held_key(&ms);
        if (k == HK_LEFT)
            q->basket_x = (int16_t)imax(24, q->basket_x - 8), q->catch_t = q->anim_t;
        if (k == HK_RIGHT)
            q->basket_x = (int16_t)imin(296, q->basket_x + 8), q->catch_t = q->anim_t;
        catch_step(q);
        if (!q->catch_end_t && q->anim_t - q->catch_t > CATCH_IDLE)
            q->beat_len = 0; /* (left alone: on with the story) */
    }
    if (q->beat == BEAT_SQUISH || q->beat == BEAT_MAKER) {
        mq_squish_step(&q->sq);
        sq_watch(q);
    }
    if (el > q->beat_len || (!q->fun_ms && el > 5000 && q->beat != BEAT_CATCH && q->beat != BEAT_STORY)) {
        /* (a story: told to its end) */
        if (q->beat == BEAT_STORY)
            mq_read_note(q, el, (int)story_line_ms(q), false);
        beat_end(q);
    }
}

bool mq_fun_animating(mq_t *q) { return q->scene == SC_FUN; }

/* the friend at the door when the budget is used up (drawn over rooms and dress-up) */
void mq_fun_hook_draw(mq_t *q)
{
    if (!q->hook_t || q->scene == SC_FUN || !fun_scene(q->scene))
        return; /* (never now: the friend's word is a toast, mq_fun_frame) */
    q->nbtn = 0;
    int w = q->world < MQ_NWORLD ? q->world : 0;
    static const char need[MQ_NWORLD][30] = {"The garden show starts soon!", "Parcels need rescuing!",
                                             "Customers at the bakery!", "The tower clock stopped!",
                                             "Tango's shop is opening!", "The mural needs paint!",
                                             "The fair is starting!", "A star fell in the sky!",
                                             "Ships at Star Harbor!"};
    int slot = w >= G_SKY ? 8 : w;
    /* one card, everything on it (a modal: the friend, the words, go!): nothing half over the room behind */
    mq_rrect(q, 40, MQ_Y0 + 26, 272, 86, 12, Q_LAV);
    mq_rrect(q, 42, MQ_Y0 + 28, 268, 82, 11, Q_WHITE);
    mq_sprite(q, slot, 50, MQ_Y0 + 36, 2, 1);
    mq_text_fit(q, q->api->font_small, 96, MQ_Y0 + 36, 208, mq_cast_name(q, slot), Q_HOTPINK, false);
    mq_text_fit(q, q->api->font_small, 96, MQ_Y0 + 52, 208, need[w], Q_INK, false);
    mq_button(q, 240, MQ_Y0 + 80, 64, 24, HB_HOOK, 0, "go!", Q_MINT);
}

void mq_fun_draw(mq_t *q)
{
    const modx_api_t *api = q->api;
    mq_profile_t *p = mq_prof(q);
    uint32_t el = q->anim_t - q->beat_t;
    int left = (int)((q->beat_len > el ? q->beat_len - el : 0) / 1000);
    (void)left;
    switch (q->beat) {
    case BEAT_BUBBLES: {
        mq_fill(q, 0, MQ_Y0, MQ_W, MQ_H, RGB(225, 245, 255));
        char hd[40];
        if (q->bub_mode == BB_SKIP)
            api->snprintf(hd, sizeof hd, "Count by %ds: %d, %d, ?", q->bub_step, q->bub_next - 2 * q->bub_step > 0 ? q->bub_next - 2 * q->bub_step : 0,
                          q->bub_next - q->bub_step);
        else if (q->bub_mode == BB_PAIRS)
            api->snprintf(hd, sizeof hd, "Pop two that make %d!", q->bub_target);
        else
            api->snprintf(hd, sizeof hd, "Pop the bigger slice!");
        mq_text_c(q, api->font_medium, 160, MQ_Y0 + 6, hd, Q_BLUE);
        for (int i = 0; i < 12; i++)
            if (q->fo[i].on) {
                static const uint16_t c[5] = {Q_PINK, Q_SKY, Q_LAV, Q_MINT, Q_YELLOW};
                const mq_fobj_t *b = &q->fo[i];
                int wx = b->wob ? ((b->wob / 2) % 2 ? 2 : -2) : 0, x = b->x + wx;
                bool picked = q->bub_mode == BB_PAIRS && q->bub_pick == i + 1;
                mq_circle(q, x, b->y, b->r, picked ? Q_YELLOW : RGB(240, 250, 255));
                mq_ring(q, x, b->y, b->r, c[b->kind % 5]);
                mq_ring(q, x, b->y, b->r - 1, Q_WHITE);
                mq_fill(q, x - b->r / 2, b->y - b->r / 2, 3, 3, Q_WHITE);
                char s[10];
                if (b->v2)
                    api->snprintf(s, sizeof s, "%d/%d", b->v, b->v2);
                else
                    api->snprintf(s, sizeof s, "%d", b->v);
                if (b->y - 6 + api->font_small->h <= MQ_Y0 + MQ_H) /* (its number once it is up in the play area, never
                                                                     * over the soft keys: the render audit) */
                    mq_text_c(q, api->font_small, x, b->y - 6, s, Q_INK);
            }
        mq_add_btn(q, 0, MQ_Y0 + 20, MQ_W, MQ_H - 20, HB_FUNTAP, 0);
        break;
    }
    case BEAT_CATCH:
        mq_fill(q, 0, MQ_Y0, MQ_W, MQ_H, RGB(236, 236, 255));
        {
            int k = imax(2, q->bub_step), n = q->beat_score;
            const char *by = q->catch_dir < 0 ? "Count back by" : "Count by";
            if (q->catch_end_t) /* the whole count: what she did */
                api->snprintf(q->tmp, sizeof q->tmp, "%s %ds to %d!", q->catch_dir < 0 ? "You counted back by" : "You counted by",
                              k, catch_term(q, q->catch_len + 1));
            else /* the count so far (its last two) and the next one to find */
                api->snprintf(q->tmp, sizeof q->tmp, "%s %ds: %d, %d, ?", by, k, catch_term(q, n), catch_term(q, n + 1));
            mq_text_fit(q, api->font_medium, 160, MQ_Y0 + 4, 312, q->tmp, Q_PURPLE, true);
            /* how far the count goes: a star a catch, lit when caught */
            int w = q->catch_len * 12, x0 = 160 - w / 2 + 6;
            for (int i = 0; i < q->catch_len; i++)
                if (i < n)
                    mq_star(q, x0 + i * 12, MQ_Y0 + 28, 5, Q_YELLOW);
                else
                    mq_ring(q, x0 + i * 12, MQ_Y0 + 28, 3, Q_LAV);
        }
        for (int i = 0; i < 12; i++)
            if (q->fo[i].on) {
                mq_star(q, q->fo[i].x, q->fo[i].y, 18, q->fo[i].kind % 2 ? Q_YELLOW : Q_PINK);
                mq_circle(q, q->fo[i].x, q->fo[i].y, 10, Q_WHITE);
                char s[8];
                api->snprintf(s, sizeof s, "%d", q->fo[i].v);
                mq_text_fit(q, api->font_small, q->fo[i].x, q->fo[i].y - 6, 22, s, Q_INK, true);
            }
        mq_rrect(q, q->basket_x - 24, MQ_Y0 + 172, 48, 18, 6, Q_BROWN);
        mq_buddy_draw(q, p->buddy % MQ_NCAST, q->basket_x - 20, MQ_Y0 + 134, 2, 0);
        mq_add_btn(q, 0, MQ_Y0 + 20, MQ_W, MQ_H - 20, HB_FUNTAP, 0);
        break;
    case BEAT_DANCE: {
        mq_fill(q, 0, MQ_Y0, MQ_W, MQ_H, RGB(255, 236, 250));
        for (int i = 0; i < 8; i++) /* disco lights */
            mq_circle(q, 20 + i * 40, MQ_Y0 + 20 + (int)((q->anim_t / 200 + (uint32_t)i) % 3) * 4, 6,
                      i % 3 ? Q_LAV : Q_YELLOW);
        int beat = (int)(q->anim_t / 500 % 4); /* lean left, up, lean right, squash */
        int dx = beat == 0 ? -10 : beat == 2 ? 10 : 0, dy = beat == 1 ? -12 : beat == 3 ? 4 : 0;
        mq_buddy_draw(q, p->buddy % MQ_NCAST, 120 + dx, MQ_Y0 + 60 + dy, 4, 0);
        bool on = q->anim_t % 500 < 160; /* the beat: tap on it */
        mq_circle(q, 160, MQ_Y0 + 178, on ? 16 : 11, on ? Q_HOTPINK : Q_PINK);
        api->snprintf(q->tmp, sizeof q->tmp, "%d", q->beat_score);
        mq_text_c(q, api->font_medium, 160, MQ_Y0 + 171, q->tmp, Q_WHITE);
        mq_text_c(q, api->font_small, 160, MQ_Y0 + 6, "Dance party! Tap on the beat!", Q_INK);
        mq_add_btn(q, 0, MQ_Y0 + 20, MQ_W, MQ_H - 20, HB_FUNTAP, 0);
        break;
    }
    case BEAT_STORY: {
        int w = q->world < MQ_NWORLD ? q->world : 0, slot = w >= G_SKY ? 8 : w;
        static const char lines[MQ_NWORLD][3][40] = {
            {"The garden show is coming!", "My tulips need lots of love.", "Will you help me win?"},
            {"Oh no, the parcels are lost!", "Every one has a home to go to.", "Let's rescue them together!"},
            {"The bakery opens tomorrow!", "Cakes, pies and cookies...", "We need a super baker!"},
            {"The tower clock is stuck!", "The train can't be late.", "Let's fix the gears!"},
            {"My shop is almost ready!", "Customers are coming soon.", "Can you help at the till?"},
            {"The big mural is blank!", "Rugs, frames and shapes...", "Let's make it beautiful!"},
            {"The fun fair is here!", "Prizes, rides and balloons!", "Let's run the stalls!"},
            {"A star fell from the sky!", "The Sky Castle is up there.", "Let's climb the clouds!"},
            {"Star ships are landing!", "Their cargo is all mixed up.", "Let's sort the harbor!"}};
        mq_fill(q, 0, MQ_Y0, MQ_W, MQ_H, RGB(250, 244, 255));
        int k = (int)imin(2, (int)(el / story_line_ms(q)));
        mq_sprite(q, slot, 20, MQ_Y0 + 60 - (int)(q->anim_t / 120 % 2) * 2, 4, 1);
        mq_rrect(q, 110, MQ_Y0 + 40, 200, 70, 12, Q_WHITE);
        for (int i = 0; i <= k; i++)
            mq_text(q, api->font_small, 118, MQ_Y0 + 48 + i * 18, lines[w][i], Q_INK);
        mq_buddy_draw(q, p->buddy % MQ_NCAST, 230, MQ_Y0 + 120, 3, 1);
        mq_add_btn(q, 0, MQ_Y0, MQ_W, MQ_H, HB_FUNTAP, 0);
        break;
    }
    case BEAT_SQUISH:
    case BEAT_MAKER:
        mq_fill(q, 0, MQ_Y0, MQ_W, MQ_H, RGB(255, 246, 236));
        mq_fill(q, 0, MQ_Y0 + 160, MQ_W, 46, Q_PEACH);
        if (q->beat == BEAT_MAKER && !q->sq_made) {
            mq_text_c(q, api->font_medium, 160, MQ_Y0 + 6, "Squishy maker!", Q_HOTPINK);
            for (int k = 0; k < SQ_KINDS; k++) {
                mq_button(q, 8 + k * 38, MQ_Y0 + 120, 36, 36, HB_MAKER, k, NULL, k == q->maker_kind ? Q_YELLOW : Q_WHITE);
                mq_squish_icon(q, k, q->maker_col, false, 26 + k * 38, MQ_Y0 + 138);
            }
            for (int c = 0; c < SQ_COLS; c++) /* (the chosen kind's own colours: no green strawberry) */
                mq_button(q, 70 + c * 46, MQ_Y0 + 164, 40, 26, HB_MAKER, 16 + c, NULL, mq_squish_colour(q->maker_kind, c, false));
            mq_button(q, 260, MQ_Y0 + 164, 56, 26, HB_MAKER, 32, "make!", Q_MINT);
            mq_squish_icon(q, q->maker_kind, q->maker_col, false, 160, MQ_Y0 + 70);
            break;
        }
        mq_text_c(q, api->font_medium, 160, MQ_Y0 + 6, q->sq_new ? "A new squishy!" : "Squish squish!", Q_HOTPINK);
        mq_squish_name(q->sq.kind, q->sq_col, q->sq_sparkle, q->tmp, (int)sizeof q->tmp);
        mq_text_c(q, api->font_small, 160, MQ_Y0 + 24, q->tmp, Q_INK);
        sq_draw(q, &q->sq, q->sq_col, q->sq_sparkle, 1);
        mq_text_c(q, api->font_small, 160, MQ_Y0 + 186, "press, stretch or pinch it", Q_DIM);
        break;
    default: break;
    }
}

/* taps and keys in a beat */
void mq_fun_press(mq_t *q, int id, int arg, int x, int y)
{
    mq_home_t *h = mq_home(q);
    if (id == HB_HOOK) {
        q->hook_t = 0;
        mq_fun_resume(q, FR_WORLD);
        return;
    }
    if (id == HB_MAKER) {
        if (arg < 16)
            q->maker_kind = (uint8_t)arg;
        else if (arg < 32)
            q->maker_col = (uint8_t)(arg - 16);
        else if (h->dough >= 2) {
            h->dough = (uint8_t)(h->dough - 2);
            q->sq_new = !mq_squish_owned(h, q->maker_kind, q->maker_col, false);
            mq_squish_give(h, q->maker_kind, q->maker_col, false);
            q->sq_col = q->maker_col, q->sq_sparkle = 0, q->sq_made = 1;
            sq_init(&q->sq, q->maker_kind, 160, MQ_Y0 + 108);
            mq_spark_burst(q, 160, MQ_Y0 + 100, 20);
            q->dirty = true;
        }
        q->redraw = true;
        return;
    }
    if (id != HB_FUNTAP)
        return;
    switch (q->beat) {
    case BEAT_BUBBLES:
        for (int i = 0; i < 12; i++)
            if (q->fo[i].on && iabs(q->fo[i].x - x) <= q->fo[i].r + 6 && iabs(q->fo[i].y - y) <= q->fo[i].r + 6) {
                bub_tap(q, i);
                break;
            }
        break;
    case BEAT_CATCH: q->basket_x = (int16_t)imax(24, imin(296, x)), q->catch_t = q->anim_t; break;
    case BEAT_DANCE:
        if (q->anim_t % 500 < 200 || q->anim_t % 500 > 440)
            q->beat_score++, mq_spark_burst(q, 160, MQ_Y0 + 170, 8);
        break;
    case BEAT_STORY:
        if (q->anim_t - q->beat_t > 1500)
            q->beat_t -= story_line_ms(q); /* tap: the next line (and the end) sooner */
        break;
    default: break;
    }
    q->redraw = true;
}

bool mq_fun_touch(mq_t *q, int type, int x, int y, int scale)
{
    if (q->scene != SC_FUN)
        return false;
    if ((q->beat == BEAT_SQUISH || (q->beat == BEAT_MAKER && q->sq_made)) && y > MQ_Y0 + 30 && y < MQ_Y0 + 176) {
        mq_squish_touch(&q->sq, type, x, y, scale);
        q->redraw = true;
        return true;
    }
    if (q->beat == BEAT_CATCH && (type == TV_DOWN || type == TV_DRAG)) {
        q->basket_x = (int16_t)imax(24, imin(296, x)), q->catch_t = q->anim_t;
        return true;
    }
    if (q->beat == BEAT_BUBBLES && type == TV_DOWN) { /* pop on touch-down: snappier */
        mq_fun_press(q, HB_FUNTAP, 0, x, y);
        q->fun_down = true;
        return true;
    }
    if (q->beat == BEAT_BUBBLES && (type == TV_TAP || type == TV_UP) && q->fun_down) { /* its release: the DOWN popped
                                                                         * (never the field's button again: CR2-06) */
        q->fun_down = false;
        return true;
    }
    return false;
}

/* keys in a beat: number keys pop bubbles by column, arrows steer, ENTER taps the beat */
bool mq_fun_key(mq_t *q, int k, int c)
{
    if (q->scene != SC_FUN)
        return false;
    if (q->beat == BEAT_BUBBLES && c >= '1' && c <= '9') {
        int col = (c - '1') * 320 / 9 + 18, best = -1;
        for (int i = 0; i < 12; i++)
            if (q->fo[i].on && iabs(q->fo[i].x - col) < 30 && (best < 0 || q->fo[i].y < q->fo[best].y))
                best = i;
        if (best >= 0)
            mq_fun_press(q, HB_FUNTAP, 0, q->fo[best].x, q->fo[best].y);
        return true;
    }
    if (q->beat == BEAT_CATCH && (k == HK_LEFT || k == HK_RIGHT)) {
        q->basket_x = (int16_t)imax(24, imin(296, q->basket_x + (k == HK_LEFT ? -16 : 16))), q->catch_t = q->anim_t;
        return true;
    }
    if (q->beat == BEAT_SQUISH && k == HK_ENTER) { /* a poke by key */
        mq_squish_touch(&q->sq, TV_DOWN, 160, MQ_Y0 + 100, 256);
        mq_squish_step(&q->sq);
        mq_squish_touch(&q->sq, TV_UP, 160, MQ_Y0 + 100, 256);
        return true;
    }
    if (k == HK_ENTER && (q->beat == BEAT_DANCE || q->beat == BEAT_STORY)) {
        mq_fun_press(q, HB_FUNTAP, 0, 160, MQ_Y0 + 100);
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------ juice */
/* praise that never repeats back to back */
const char *mq_praise(mq_t *q)
{
    static const char pr[12][16] = {"Yay!", "Amazing!", "Super!", "You did it!", "Sparkly!", "Wow!", "Perfect!",
                                    "Brilliant!", "Woohoo!", "So cool!", "Fantastic!", "Hooray!"};
    int k = mq_rnd(q, 0, 10);
    if (k >= q->last_praise)
        k++;
    q->last_praise = (uint8_t)k;
    return pr[k % 12];
}

/* coins fly from (x, y) to the counter in the header */
void mq_fly_coins(mq_t *q, int x, int y, int n)
{
    for (int i = 0; i < 8 && n > 0; i++)
        if (!q->fly[i].t0) {
            q->fly[i].x = (int16_t)(x + mq_rnd(q, -12, 12)), q->fly[i].y = (int16_t)(y + mq_rnd(q, -8, 8));
            q->fly[i].t0 = q->anim_t + (uint32_t)(i * 60) + 1;
            n--;
        }
}

void mq_juice_draw(mq_t *q)
{
    for (int i = 0; i < 8; i++) /* flying coins: 600 ms, eased towards the counter */
        if (q->fly[i].t0) {
            if (q->anim_t < q->fly[i].t0)
                continue;
            int t = (int)(q->anim_t - q->fly[i].t0);
            if (t > 600) {
                q->fly[i].t0 = 0;
                continue;
            }
            int k = t * t / 600, tx = q->coin_ix ? q->coin_ix : MQ_W - 6, ty = q->coin_ix ? q->coin_iy : MQ_Y0 + 2;
            int x = q->fly[i].x + (tx - q->fly[i].x) * k / 600, y = q->fly[i].y + (ty - q->fly[i].y) * k / 600;
            mq_money_draw(q, q->pay.fly_dn[i], x, y, 1); /* a coin of the reward, into the purse */
        }
    uint32_t el = q->anim_t - q->trans_t; /* an iris between scenes: opens from the middle in 240 ms */
    if (q->trans_t && el > 0 && el < 240) { /* (el 0: not a frame yet; `mq step` settles it for tests) */
        int cx = q->tap_x > 0 && q->tap_x < MQ_W ? q->tap_x : MQ_W / 2; /* from where she tapped */
        int cy = q->tap_y > MQ_Y0 && q->tap_y < MQ_Y0 + MQ_H ? q->tap_y : MQ_Y0 + MQ_H / 2;
        int r = (int)el * 400 / 240, r2 = r * r;
        for (int y = MQ_Y0; y < MQ_Y0 + MQ_H; y++) {
            int dy = y - cy, d2 = r2 - dy * dy;
            if (d2 <= 0) {
                mq_fill(q, 0, y, MQ_W, 1, Q_PURPLE);
                continue;
            }
            int dx = r; /* the square root, by Newton */
            for (int k = 0; k < 8 && dx > 0; k++)
                dx = (dx + d2 / dx) / 2;
            mq_fill(q, 0, y, cx - dx, 1, Q_PURPLE);
            mq_fill(q, cx + dx, y, MQ_W - cx - dx, 1, Q_PURPLE);
            mq_fill(q, cx - dx - 2, y, 2, 1, Q_PINK), mq_fill(q, cx + dx, y, 2, 1, Q_PINK); /* a soft pink rim */
        }
    }
}

bool mq_juice_animating(mq_t *q)
{
    for (int i = 0; i < 8; i++)
        if (q->fly[i].t0)
            return true;
    return q->trans_t && q->anim_t - q->trans_t < 240;
}

/* console `mq fun ...` (tests): fun BUDGET_MS | fun beat NAME | fun pct N */
int mq_fun_console(mq_t *q, int argc, char **argv)
{
    const modx_api_t *api = q->api;
    long n;
    if (argc > 2 && (!api->strcmp(argv[2], "off") || !api->strcmp(argv[2], "on"))) { /* scripted tests */
        q->fun_off = !api->strcmp(argv[2], "off");
        return 0;
    }
    if (argc > 2 && api->con_parse_int(argv[2], &n)) {
        q->fun_ms = (uint32_t)(n < 0 ? 0 : n);
        return 0;
    }
    if (argc > 3 && !api->strcmp(argv[2], "beat")) {
        for (int b = 0; b < BEAT_COUNT; b++)
            if (!api->strcmp(argv[3], beat_name[b])) {
                mq_fun_start(q, b, FR_MAP);
                return 0;
            }
        return api->con_error("no beat %s", argv[3]);
    }
    if (argc > 3 && !api->strcmp(argv[2], "pct") && api->con_parse_int(argv[3], &n)) {
        mq_prof(q)->pad[0] = (uint8_t)(n < 0 ? 1 : n > 50 ? 51 : n + 1);
        return 0;
    }
    if (argc > 2 && !api->strcmp(argv[2], "dough") && argc > 3 && api->con_parse_int(argv[3], &n)) {
        mq_home(q)->dough = (uint8_t)n;
        return 0;
    }
    return api->con_error("usage: mq fun MS | fun on|off | fun beat NAME | fun pct N | fun dough N");
}
