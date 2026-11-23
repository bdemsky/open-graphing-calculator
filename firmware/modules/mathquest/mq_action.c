/*
 * Math Quest: action games for the keypad (and touch).
 *
 *   Cloud Dash   a runner in three lanes: UP / DOWN (or a tap on a lane) to switch lanes,
 *                dodge the rocks, grab hearts and stars; now and then a row of three answer
 *                bubbles comes: run through the right one (or type its number: the runner
 *                jumps to its lane at once);
 *   Star Sled    LEFT / RIGHT (or drag) to steer a sled and catch falling stars; now and
 *                then three numbers fall: catch the right one;
 *   Pop-up Pals  nine holes on the number keys as they lie on the keypad (7 8 9 / 4 5 6 /
 *                1 2 3) or by touch: bop the pals that pop up; now and then numbers pop up:
 *                bop the right one.
 *
 * Both kinds of moment are mixed in one game: pure action (dodging, collecting, bopping)
 * and "grab the right number" moments, with no label telling them apart. The answer
 * moments are maths time and the pure stretches are fun time for the invisible budget
 * (mq_fun.c), which also sets the rhythm: pure stretches last about a third of the fun
 * budget (3-8 s), so maths comes more often when the budget runs low. The questions are the
 * x / / facts from their scheduler (mq_facts.c: Leitner boxes, fluency, no repeats). The
 * action speed adapts on its own (a crash: a step slower; a clean stretch: a step faster),
 * apart from the maths. Wrong answers and crashes never end a game; a game lasts 45 s.
 * Personal bests are saved (mq_home_t.best[4..6]); the games open with the story.
 */
#include "mq.h"

enum { HK_UP = 12, HK_LEFT = 13, HK_RIGHT = 14, HK_DOWN = 15, HK_ENTER = 49 };
enum { TV_DOWN = 1, TV_DRAG, TV_UP, TV_TAP };
enum { OB_COIN = 1, OB_STAR, OB_ROCK, OB_NUM, OB_PAL };

#define GAME_MS 45000u
static const char act_name[ACT_COUNT][14] = {"Cloud Dash", "Star Sled", "Pop-up Pals"};

const char *mq_action_name(int g) { return act_name[g >= 0 && g < ACT_COUNT ? g : 0]; }
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int iabs(int a) { return a < 0 ? -a : a; }

/* open with the story: Cloud Dash from the start, Star Sled with the Post Office, Pop-up Pals
 * with the Bakery */
bool mq_action_open(const mq_profile_t *p, int g) { (void)p; return g >= 0 && g < ACT_COUNT; } /* all open */

/* axis-aligned boxes overlap */
bool mq_hit(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

int mq_lane_y(int lane) { return MQ_Y0 + 44 + lane * 52; }

/* ------------------------------------------------------------------ questions */
/* a fact's answer and two near misses, all three different (c[0] the answer) */
void mq_act_options(int ans, int x, int y, bool div, int c[3])
{
    c[0] = ans, c[1] = ans + (div ? 1 : y ? y : 1), c[2] = ans > 0 ? ans - (div ? 1 : x ? x : 1) : ans + 2;
    if (c[2] < 0 || c[2] == c[1] || c[2] == c[0])
        c[2] = ans + 2;
    if (c[1] == c[0])
        c[1] = ans + 3;
    for (int k = 1; c[2] == c[1] || c[2] == c[0]; k++) /* (0 x 2: two 2s -- CR-11) */
        c[2] = ans + 3 + k;
}

/* a new answer moment: a fact, its answer and two near misses */
static void new_question(mq_t *q)
{
    mq_action_t *a = &q->act;
    int f = mq_fact_next(q), x, y;
    bool div;
    mq_fact_decode(f, &x, &y, &div);
    int ans = mq_fact_answer(f);
    a->fact = (int16_t)f, a->ans = (int16_t)ans;
    if (div)
        q->api->snprintf(a->qtext, sizeof a->qtext, "%d \xC3\xB7 %d = ?", x * y, y);
    else
        q->api->snprintf(a->qtext, sizeof a->qtext, "%d \xC3\x97 %d = ?", x, y);
    int c[3];
    mq_act_options(ans, x, y, div, c);
    for (int i = 2; i > 0; i--) { /* shuffle */
        int j = mq_rnd(q, 0, i), t = c[i];
        c[i] = c[j], c[j] = t;
    }
    for (int i = 0; i < 3; i++)
        a->opt[i] = (int16_t)c[i];
    a->asked_t = q->anim_t;
    a->moment = 1;
    a->typed = 0;
}

static void answered(mq_t *q, int value)
{
    mq_action_t *a = &q->act;
    bool right = value == a->ans;
    mq_fact_record(q, a->fact, right, q->anim_t - a->asked_t);
    if (right) {
        a->score += 5, a->streak++, a->rights++;
        mq_spark_burst(q, 160, MQ_Y0 + 60, 14);
        if (a->streak % 3 == 0)
            mq_earn(q, 1, NULL); /* a coin now and then (the maths earns it) */
    } else
        a->streak = 0;
    a->moment = 0, a->right_last = right;
    a->flash_t = q->anim_t;
    /* the next pure stretch: a third of the fun budget, 3..8 s */
    uint32_t pure = q->fun_ms / 3;
    a->pure_until = q->anim_t + (pure < 3000 ? 3000 : pure > 8000 ? 8000 : pure);
}

/* ------------------------------------------------------------------ starting */
void mq_action_start(mq_t *q, int game)
{
    mq_action_t *a = &q->act;
    uint8_t *b = (uint8_t *)a;
    for (size_t i = 0; i < sizeof *a; i++)
        b[i] = 0;
    a->game = (uint8_t)game, a->t0 = q->anim_t, a->speed = 2, a->lane = 1;
    a->y = (int16_t)mq_lane_y(1), a->x = 160;
    a->pure_until = q->anim_t + 4000;
    mq_game_coins_start(q); /* the end card shows the coins of this game */
    mq_go(q, SC_ACTION);
}

static mq_aobj_t *spawn(mq_t *q, int kind, int x, int y, int lane, int val)
{
    mq_action_t *a = &q->act;
    for (int i = 0; i < ACT_NOBJ; i++)
        if (!a->o[i].kind) {
            a->o[i] = (mq_aobj_t){(uint8_t)kind, (uint8_t)lane, (int16_t)x, (int16_t)y, (int16_t)val, 0};
            return &a->o[i];
        }
    return NULL;
}

static bool any_kind(const mq_action_t *a, int kind)
{
    for (int i = 0; i < ACT_NOBJ; i++)
        if (a->o[i].kind == kind)
            return true;
    return false;
}

/* room for an answer row (three numbers)? */
static bool room3(const mq_action_t *a)
{
    int n = 0;
    for (int i = 0; i < ACT_NOBJ; i++)
        n += !a->o[i].kind;
    return n >= 3;
}

/* the number row's speed (px a frame): the game's, but slow enough that the numbers reach her no sooner
 * than 1.5 x her think time for the fact (Star Sled: they fall 140 px; Cloud Dash: they run 300 px) */
int mq_action_num_v(mq_t *q, int v, int dist)
{
    const mq_fact_t *t = q->act.fact >= 0 && q->act.fact < MQ_NFACT ? &mq_prof(q)->fact[q->act.fact] : NULL;
    int think = t && t->t10 ? t->t10 * 100 : 4000;
    think = think < 1500 ? 1500 : think;
    int frames = think * 3 / 2 / MQ_FRAME_MS, cap = dist / (frames > 0 ? frames : 1);
    return imax(1, imin(v, cap));
}
#define ACT_SPEED_MAX 4 /* a comfortable cruise: dodging well speeds it up a little, never toward failure */

static void crash(mq_t *q)
{
    mq_action_t *a = &q->act;
    a->speed = (uint8_t)imax(1, a->speed - 1), a->clean = 0, a->crash_t = q->anim_t;
}

/* ------------------------------------------------------------------ one frame (40 ms) */
static void run_step(mq_t *q)
{
    mq_action_t *a = &q->act;
    int v = 3 + a->speed * 2;
    a->y = (int16_t)(a->y + (mq_lane_y(a->lane) - a->y + (mq_lane_y(a->lane) > a->y ? 1 : -1)) / 2);
    if (!a->moment && q->anim_t >= a->pure_until && !any_kind(a, OB_NUM) && room3(a)) { /* an answer row */
        new_question(q);
        for (int l = 0; l < 3; l++)
            spawn(q, OB_NUM, 340, mq_lane_y(l), l, a->opt[l]);
    } else if (!a->moment && mq_rnd(q, 0, 99) < 6 + a->speed) { /* rocks, coins, stars */
        int l = mq_rnd(q, 0, 2), k = mq_rnd(q, 0, 9);
        spawn(q, k < 4 ? OB_ROCK : k < 8 ? OB_COIN : OB_STAR, 340, mq_lane_y(l), l, 0);
    }
    for (int i = 0; i < ACT_NOBJ; i++) {
        mq_aobj_t *o = &a->o[i];
        if (!o->kind)
            continue;
        o->x = (int16_t)(o->x - (o->kind == OB_NUM ? mq_action_num_v(q, v, 300) : v));
        if (mq_hit(40, a->y - 12, 26, 24, o->x - 12, o->y - 12, 24, 24) && iabs(a->y - o->y) < 20) {
            if (o->kind == OB_NUM) {
                answered(q, o->val);
                for (int k = 0; k < ACT_NOBJ; k++) /* the row is done */
                    if (a->o[k].kind == OB_NUM)
                        a->o[k].kind = 0;
                continue;
            }
            if (o->kind == OB_ROCK)
                crash(q);
            else
                a->score += o->kind == OB_STAR ? 2 : 1, mq_spark_burst(q, o->x, o->y, 4);
            o->kind = 0;
        } else if (o->x < -20) {
            if (o->kind == OB_NUM) { /* a row missed: it counts as not answered, comes back later */
                for (int k = 0; k < ACT_NOBJ; k++)
                    if (a->o[k].kind == OB_NUM)
                        a->o[k].kind = 0;
                a->moment = 0, a->pure_until = q->anim_t + 2000;
            }
            if (o->kind == OB_ROCK && ++a->clean % 12 == 0 && a->speed < ACT_SPEED_MAX)
                a->speed++; /* dodging well: a little faster */
            o->kind = 0;
        }
    }
}

static void sled_step(mq_t *q)
{
    mq_action_t *a = &q->act;
    uint32_t ms = 0;
    int k = q->api->held_key(&ms);
    if (k == HK_LEFT)
        a->x = (int16_t)imax(20, a->x - 7);
    if (k == HK_RIGHT)
        a->x = (int16_t)imin(300, a->x + 7);
    int fall = 2 + a->speed;
    if (!a->moment && q->anim_t >= a->pure_until && !any_kind(a, OB_NUM) && room3(a)) {
        new_question(q);
        for (int i = 0; i < 3; i++)
            spawn(q, OB_NUM, 60 + i * 100, MQ_Y0 + 36, i, a->opt[i]);
    } else if (!a->moment && mq_rnd(q, 0, 99) < 8)
        spawn(q, mq_rnd(q, 0, 4) ? OB_STAR : OB_ROCK, mq_rnd(q, 20, 300), MQ_Y0 + 30, 0, 0);
    for (int i = 0; i < ACT_NOBJ; i++) {
        mq_aobj_t *o = &a->o[i];
        if (!o->kind)
            continue;
        o->y = (int16_t)(o->y + (o->kind == OB_NUM ? mq_action_num_v(q, fall - 1, 140) : fall));
        if (mq_hit(a->x - 22, MQ_Y0 + 168, 44, 16, o->x - 10, o->y - 10, 20, 20)) {
            if (o->kind == OB_NUM) {
                answered(q, o->val);
                for (int k2 = 0; k2 < ACT_NOBJ; k2++)
                    if (a->o[k2].kind == OB_NUM)
                        a->o[k2].kind = 0;
                continue;
            }
            if (o->kind == OB_ROCK)
                crash(q);
            else
                a->score++, mq_spark_burst(q, o->x, o->y, 4);
            o->kind = 0;
        } else if (o->y > MQ_Y0 + MQ_H) {
            if (o->kind == OB_NUM) {
                for (int k2 = 0; k2 < ACT_NOBJ; k2++)
                    if (a->o[k2].kind == OB_NUM)
                        a->o[k2].kind = 0;
                a->moment = 0, a->pure_until = q->anim_t + 2000;
            }
            /* a missed star: nothing lost */
            if (o->kind == OB_ROCK && ++a->clean % 10 == 0 && a->speed < ACT_SPEED_MAX)
                a->speed++;
            o->kind = 0;
        }
    }
}

/* holes 0..8: row 0 at the top (keys 7 8 9), row 2 at the bottom (keys 1 2 3) */
int mq_hole_of_key(int c) { return c >= '1' && c <= '9' ? (2 - (c - '1') / 3) * 3 + (c - '1') % 3 : -1; }
static void hole_xy(int h, int *x, int *y) { *x = 70 + (h % 3) * 90, *y = MQ_Y0 + 62 + (h / 3) * 48; }

static void pals_step(mq_t *q)
{
    mq_action_t *a = &q->act;
    uint32_t life = (uint32_t)imax(500, 1400 - a->speed * 150);
    if (!a->moment && q->anim_t >= a->pure_until && !any_kind(a, OB_NUM)) {
        for (int i = 0; i < ACT_NOBJ; i++)
            a->o[i].kind = 0;
        new_question(q);
        int used = 0;
        for (int i = 0; i < 3; i++) {
            int h;
            do
                h = mq_rnd(q, 0, 8);
            while ((used >> h) & 1);
            used |= 1 << h;
            mq_aobj_t *o = spawn(q, OB_NUM, 0, 0, h, a->opt[i]);
            if (o)
                o->t = (uint16_t)(q->anim_t / 10);
        }
    } else if (!a->moment && mq_rnd(q, 0, 99) < 10) {
        int h = mq_rnd(q, 0, 8);
        bool busy = false;
        for (int i = 0; i < ACT_NOBJ; i++)
            busy |= a->o[i].kind && a->o[i].lane == h;
        mq_aobj_t *o = busy ? NULL : spawn(q, OB_PAL, 0, 0, h, mq_rnd(q, 0, MQ_NCAST - 1));
        if (o)
            o->t = (uint16_t)(q->anim_t / 10);
    }
    for (int i = 0; i < ACT_NOBJ; i++) /* pals go back down; numbers wait */
        if (a->o[i].kind == OB_PAL && (uint32_t)(uint16_t)((uint16_t)(q->anim_t / 10) - a->o[i].t) > life / 10)
            a->o[i].kind = 0;
}

static void bop(mq_t *q, int h)
{
    mq_action_t *a = &q->act;
    for (int i = 0; i < ACT_NOBJ; i++)
        if (a->o[i].kind && a->o[i].lane == h) {
            int x, y;
            hole_xy(h, &x, &y);
            if (a->o[i].kind == OB_NUM) {
                answered(q, a->o[i].val);
                for (int k = 0; k < ACT_NOBJ; k++)
                    if (a->o[k].kind == OB_NUM)
                        a->o[k].kind = 0;
            } else {
                a->score++, a->o[i].kind = 0;
                if (++a->clean % 8 == 0 && a->speed < ACT_SPEED_MAX)
                    a->speed++;
                mq_spark_burst(q, x, y, 5);
            }
            return;
        }
    if (!a->moment)
        a->clean = 0; /* a bop at an empty hole: nothing lost */
}

void mq_action_frame(mq_t *q)
{
    mq_action_t *a = &q->act;
    if (q->scene != SC_ACTION || a->done)
        return;
    mq_fun_time(q, MQ_FRAME_MS, a->moment != 0); /* answer moments are maths, the rest is fun */
    if (a->game == ACT_RUN)
        run_step(q);
    else if (a->game == ACT_SLED)
        sled_step(q);
    else
        pals_step(q);
    if (q->anim_t - a->t0 >= GAME_MS) { /* the end: the score, maybe a new best */
        mq_home_t *h = mq_home(q);
        a->done = 1;
        if (a->rights) /* playing to the end: a little more (the maths earned it) */
            mq_earn(q, 2, NULL);
        mq_quest_event(q, QE_ARCADE, 1);
        mq_sfx(q, MODX_SFX_CHEER, 0);
        a->new_best = a->score > h->best[4 + a->game];
        if (a->new_best)
            mq_sfx(q, MODX_SFX_UNLOCK, 3);
        if (a->new_best)
            h->best[4 + a->game] = a->score, q->dirty = true;
        mq_spark_burst(q, 160, MQ_Y0 + 80, 24);
    }
}

/* ------------------------------------------------------------------ input */
bool mq_action_key(mq_t *q, int k, int c)
{
    mq_action_t *a = &q->act;
    if (q->scene != SC_ACTION || a->done)
        return false;
    mq_demo_seen(q, SPD_ACT + a->game);
    if (a->game == ACT_RUN) {
        if (k == HK_UP || k == HK_DOWN) {
            a->lane = (uint8_t)imax(0, imin(2, a->lane + (k == HK_UP ? -1 : 1)));
            return true;
        }
        if (c >= '0' && c <= '9' && a->moment) { /* typed: the runner jumps to that number's lane */
            a->typed = (int16_t)(a->typed * 10 + (c - '0'));
            for (int l = 0; l < 3; l++)
                if (a->opt[l] == a->typed)
                    a->lane = (uint8_t)l, a->typed = 0;
            if (a->typed > 999)
                a->typed = 0;
            return true;
        }
    }
    if (a->game == ACT_SLED && (k == HK_LEFT || k == HK_RIGHT)) {
        a->x = (int16_t)imax(20, imin(300, a->x + (k == HK_LEFT ? -24 : 24)));
        return true;
    }
    if (a->game == ACT_SLED && c >= '0' && c <= '9' && a->moment) { /* typed: steer under that number */
        a->typed = (int16_t)(a->typed * 10 + (c - '0'));
        for (int i = 0; i < ACT_NOBJ; i++)
            if (a->o[i].kind == OB_NUM && a->o[i].val == a->typed)
                a->x = a->o[i].x, a->typed = 0;
        if (a->typed > 999)
            a->typed = 0;
        return true;
    }
    if (a->game == ACT_PALS && mq_hole_of_key(c) >= 0) {
        bop(q, mq_hole_of_key(c));
        return true;
    }
    return false;
}

bool mq_action_touch(mq_t *q, int type, int x, int y)
{
    mq_action_t *a = &q->act;
    if (q->scene != SC_ACTION || a->done)
        return false;
    mq_demo_seen(q, SPD_ACT + a->game);
    if (a->game == ACT_RUN && (type == TV_DOWN || type == TV_TAP)) {
        int l = (y - (MQ_Y0 + 18)) / 52;
        a->lane = (uint8_t)imax(0, imin(2, l));
        return true;
    }
    if (a->game == ACT_SLED && (type == TV_DOWN || type == TV_DRAG)) {
        a->x = (int16_t)imax(20, imin(300, x));
        return true;
    }
    if (a->game == ACT_PALS && type == TV_DOWN) {
        for (int h = 0; h < 9; h++) {
            int hx, hy;
            hole_xy(h, &hx, &hy);
            if (iabs(x - hx) < 40 && iabs(y - hy) < 22)
                bop(q, h);
        }
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------ drawing */
void mq_action_draw(mq_t *q)
{
    const modx_api_t *api = q->api;
    mq_action_t *a = &q->act;
    mq_profile_t *p = mq_prof(q);
    static const uint16_t sky[ACT_COUNT] = {RGB(214, 238, 255), RGB(236, 232, 255), RGB(232, 250, 238)};
    mq_fill(q, 0, MQ_Y0, MQ_W, MQ_H, sky[a->game % ACT_COUNT]);
    char s[24];
    if (a->game == ACT_RUN) {
        for (int l = 0; l < 3; l++) /* three cloud lanes */
            mq_rrect(q, 0, mq_lane_y(l) + 12, MQ_W, 10, 5, Q_WHITE);
        for (int i = 0; i < 6; i++) /* puffs drifting by */
            mq_circle(q, (int)(330 - (q->anim_t / 20 + (uint32_t)i * 60) % 360), MQ_Y0 + 30 + (i % 3) * 52, 6, Q_WHITE);
    } else if (a->game == ACT_SLED)
        mq_fill(q, 0, MQ_Y0 + 180, MQ_W, 26, Q_WHITE);
    else
        for (int h = 0; h < 9; h++) {
            int x, y;
            hole_xy(h, &x, &y);
            mq_rrect(q, x - 30, y + 6, 60, 14, 7, Q_BROWN);
            char k[2] = {(char)('7' - (h / 3) * 3 + h % 3), 0};
            mq_text(q, api->font_small, x + 30, y + 6, k, Q_DIM);
        }
    for (int i = 0; i < ACT_NOBJ; i++) {
        const mq_aobj_t *o = &a->o[i];
        int x = o->x, y = o->y;
        if (a->game == ACT_PALS && o->kind)
            hole_xy(o->lane, &x, &y);
        switch (o->kind) {
        case OB_COIN: mq_heart(q, x, y, 7, Q_HOTPINK); break; /* (hearts: sparkles to grab, never money -- the maths earns the coins) */
        case OB_STAR: mq_star(q, x, y, 9, Q_YELLOW); break;
        case OB_ROCK: mq_rrect(q, x - 11, y - 9, 22, 18, 6, Q_DIM), mq_fill(q, x - 5, y - 4, 3, 3, Q_GREY); break;
        case OB_PAL: mq_cast(q, o->val, x - 15, y - 26, 30, AN_IDLE | AN_STILL); break;
        case OB_NUM:
            mq_circle(q, x, y - (a->game == ACT_PALS ? 6 : 0), 15, Q_WHITE);
            mq_ring(q, x, y - (a->game == ACT_PALS ? 6 : 0), 15, Q_PURPLE);
            api->snprintf(s, sizeof s, "%d", o->val);
            mq_text_c(q, api->font_medium, x, y - 7 - (a->game == ACT_PALS ? 6 : 0), s, Q_INK);
            break;
        default: break;
        }
    }
    /* the player */
    bool hurt = q->anim_t - a->crash_t < 500 && a->crash_t;
    if (a->game == ACT_RUN)
        mq_buddy_draw(q, p->buddy % MQ_NCAST, 40, a->y - 22 + (hurt ? 3 : 0), 2, 0);
    else if (a->game == ACT_SLED) {
        mq_rrect(q, a->x - 22, MQ_Y0 + 176, 44, 8, 4, Q_HOTPINK);
        mq_buddy_draw(q, p->buddy % MQ_NCAST, a->x - 20, MQ_Y0 + 136 + (hurt ? 3 : 0), 2, 0);
    }
    /* the top bar: the question while it is asked, the coins of this game (never a score) */
    mq_fill(q, 0, MQ_Y0, MQ_W, 18, Q_WHITE);
    if (mq_demo_running(q, SPD_ACT + a->game, a->t0)) /* the guide's words take the bar for a moment (the lane) */
        mq_lane(q, 2, MQ_Y0 + 1, 276);
    else
        mq_text(q, api->font_small, 6, MQ_Y0 + 3, mq_action_name(a->game), Q_INK);
    if (q->lane_on)
        ;
    else if (a->moment && !a->done)
        mq_text_c(q, api->font_medium, 160, MQ_Y0 + 2, a->qtext, Q_HOTPINK);
    else if (a->flash_t && q->anim_t - a->flash_t < 900)
        mq_text_c(q, api->font_medium, 160, MQ_Y0 + 2, a->right_last ? "Yes!" : "Oops!", a->right_last ? Q_GREEN : Q_PURPLE);
    {
        int c = mq_game_coins(q), w = mq_cents_width(q, api->font_medium, c);
        mq_purse_icon(q, MQ_W - 16 - w, MQ_Y0 + 9, 7);
        mq_cents_text(q, api->font_medium, MQ_W - 6 - w, MQ_Y0 + 2, c, Q_INK, false);
    }
    if (a->done) {
        mq_rrect(q, 50, MQ_Y0 + 40, 220, 110, 12, Q_WHITE);
        mq_coins_end(q, 160, MQ_Y0 + 50, mq_game_coins(q), a->t0 + GAME_MS, a->new_best);

        mq_button(q, 70, MQ_Y0 + 116, 80, 28, HB_ACTION, a->game, "again!", Q_MINT);
        mq_button(q, 170, MQ_Y0 + 116, 80, 28, HB_ACTION, 100, "back", Q_PEACH);
    }
    mq_sparks_draw(q);
    { /* the first time: the guide shows the controls */
        static const char cap[ACT_COUNT][40] = {"Up and down: run through the right one!", "Left and right: catch the right one!",
                                                "Tap a friend (or keys 7-9, 4-6, 1-3)!"};
        static const int16_t ges[ACT_COUNT][5] = {{60, MQ_Y0 + 40, 60, MQ_Y0 + 140, 1}, {80, MQ_Y0 + 176, 240, MQ_Y0 + 176, 1},
                                                  {160, MQ_Y0 + 110, 160, MQ_Y0 + 110, 0}};
        const int16_t *gg = ges[a->game % ACT_COUNT];
        mq_demo_overlay(q, SPD_ACT + a->game, a->t0, gg[0], gg[1], gg[2], gg[3], gg[4], cap[a->game % ACT_COUNT]);
    }
}

/* console `mq act GAME` / `mq act lane L` / `mq act x X` (tests) */
int mq_action_console(mq_t *q, int argc, char **argv)
{
    long n;
    if (argc > 3 && !q->api->strcmp(argv[2], "lane") && q->api->con_parse_int(argv[3], &n)) {
        q->act.lane = (uint8_t)imax(0, imin(2, (int)n));
        return 0;
    }
    if (argc > 3 && !q->api->strcmp(argv[2], "x") && q->api->con_parse_int(argv[3], &n)) {
        q->act.x = (int16_t)n;
        return 0;
    }
    if (argc > 2 && q->api->con_parse_int(argv[2], &n) && n >= 0 && n < ACT_COUNT) {
        mq_action_start(q, (int)n);
        return 0;
    }
    return q->api->con_error("usage: mq act GAME | act lane L | act x X");
}
