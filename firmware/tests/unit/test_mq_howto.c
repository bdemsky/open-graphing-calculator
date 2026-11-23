/* Sparkle Town (modules/mathquest): every game teaches itself and mashing never pays (backlog (e); the bot's
 * 16: "mashing still pays a little"). For each of the 16 games:
 *   - how to play: the first order in it opens the game's own demo (the hand and its words) for a new
 *     player, and only the first time;
 *   - anti-mash: a kid answering at machine speed (a quick miss, then the right one) earns nothing once the
 *     guess detector has seen two (orders 2-6: at most a quarter of what a calm kid earns in six).
 * The results table is printed (and kept in docs/MATHQUEST.md "How to play and mashing, per game").
 * Built and run by `make test`. */
#include "mq_test_util.h"

static bool order_in(int skill, int d, int mech)
{
    char b[32];
    snprintf(b, sizeof b, "skill %d %d", skill, d);
    con(b);
    q->fun_ms = 0;
    for (int w = 0; w < WS_COUNT; w++)
        mq_pace(q)->ws_level[w] = 2;
    for (int i = 0; i < 200; i++) {
        mq_generate_fresh(q, &q->prob, skill, d);
        if (mq_mech_fits(mech, &q->prob)) {
            q->gm.round_mech = (uint8_t)mech;
            q->tried_wrong = false, q->used_help = false;
            mq_game_order(q);
            return q->gm.mech == mech && q->sub == GS_PLAY;
        }
    }
    return false;
}

/* a skill and rung that this game tells (the first that fits) */
static bool find_order(int m, int *sk, int *r)
{
    for (int s = 0; s < MQ_NSKILL; s++)
        for (int d = 0; d < mq_rungs(s) && d < 2; d++) {
            mq_prob_t p;
            q->rng = (uint32_t)(s * 31 + d + 7) | 1;
            for (int k = 0; k < 20; k++) {
                mq_generate(q, &p, s, d);
                if (mq_mech_fits(m, &p)) {
                    *sk = s, *r = d;
                    return true;
                }
            }
        }
    return false;
}

static long earned(void) { return (long)mq_prof(q)->stars + mq_home(q)->coins; }

/* one order answered: `mash` at machine speed (a quick miss first), else calmly and right */
static void answer(bool mash)
{
    int guard = 0;
    while (q->sub == GS_PLAY && mq_guide_demo_on(q) && guard++ < 400)
        con("step 1");
    if (q->sub != GS_PLAY)
        return;
    if (mash) {
        q->now += 200, q->last_input = q->now;
        con("try");
        for (int j = 0; j < 200 && q->sub == GS_PLAY && !q->tried_wrong; j++)
            con("step 1"); /* (a flung star lands) */
        if (q->sub == GS_PLAY) {
            q->now += 200, q->last_input = q->now;
            con("correct");
        }
    } else {
        q->now += 4000, q->last_input = q->now;
        con("correct");
    }
    for (int j = 0; j < 200 && q->scene == SC_PLAY && q->sub == GS_PLAY && !q->tried_wrong; j++)
        con("step 1"); /* (a flung star or a kart takes a moment to arrive) */
    for (int j = 0; j < 80 && q->scene == SC_PLAY && q->sub != GS_PLAY; j++)
        con("next");
}

static void test_games(void)
{
    static const char *names[ME_COUNT];
    int bad = 0;
    printf("| game | demo the first time | demo again | calm: earned in 6 | mashing: earned in orders 2-6 |\n|---|---|---|---|---|\n");
    for (int m = 0; m < ME_COUNT; m++) {
        int sk, r;
        names[m] = mq_mech_name(m);
        con("new Ho 2");
        con("engine stop");
        q->scenes_off = true;
        if (!find_order(m, &sk, &r)) {
            printf("| %s | (no order fits) | | | |\n", names[m]);
            bad++;
            continue;
        }
        /* how to play: the demo the first time, not the second */
        CHECK(order_in(sk, r, m));
        bool demo1 = mq_guide_demo_on(q);
        answer(false);
        CHECK(order_in(sk, r, m));
        bool demo2 = mq_guide_demo_on(q);
        /* calm */
        long e0 = earned();
        for (int k = 0; k < 6; k++) {
            if (!order_in(sk, r, m))
                break;
            answer(false);
        }
        long calm = earned() - e0;
        /* mashing */
        q->rapid = 0, q->guess_n = 0;
        e0 = earned();
        long first_mash = 0;
        for (int k = 0; k < 6; k++) {
            if (!order_in(sk, r, m))
                break;
            answer(true);
            if (k == 0) /* (the first quick miss is not mashing yet: the detector needs two) */
                first_mash = earned() - e0;
            if (getenv("MQ_TRACE"))
                fprintf(stderr, "  %s mash %d: stars %d coins %d guess %d rapid %d sub %d tried %d\n", names[m], k, mq_prof(q)->stars,
                        mq_home(q)->coins, q->guess_n, q->rapid, q->sub, q->tried_wrong);
        }
        long mash = earned() - e0 - first_mash;
        printf("| %s | %s | %s | %ld | %ld |\n", names[m], demo1 ? "yes" : "NO", demo2 ? "YES" : "no", calm, mash);
        bool ok = demo1 && !demo2 && calm > 0 && mash * 4 <= calm;
        if (!ok)
            fprintf(stderr, "how-to / mash: %s (skill %d rung %d): demo %d/%d calm %ld mash %ld\n", names[m], sk, r, demo1, demo2,
                    calm, mash);
        bad += !ok;
    }
    CHECK(bad == 0);
}

/* Sky Fling: a second fling while a star is in the air changes nothing -- the star is judged at what it was
 * flung (a quick wrong fling and then the right one at once paid twice what a calm kid earns) */
static void test_double_fling(void)
{
    con("new Df 2");
    con("engine stop");
    int sk, r;
    CHECK(find_order(ME_LAUNCH, &sk, &r));
    int wrongs = 0, n = 0;
    for (int k = 0; k < 4; k++) {
        if (!order_in(sk, r, ME_LAUNCH))
            continue;
        for (int j = 0; j < 400 && mq_guide_demo_on(q); j++)
            con("step 1");
        con("try");
        con("correct"); /* (while the first is in the air) */
        for (int j = 0; j < 200 && q->sub == GS_PLAY && !q->tried_wrong; j++)
            con("step 1");
        n++, wrongs += q->tried_wrong || q->sub == GS_WHY;
    }
    CHECK(n >= 3 && wrongs == n);
}

/* the arcade's action games and fact games: their demo the first time (not the second), and answers typed or
 * picked at machine speed and wrong earn nothing */
static void test_arcade(void)
{
    static const char *fg_names[FG_COUNT] = {"Star Catch", "Cafe Rush", "Blooming Garden", "Snail Race"};
    con("new Ar 2");
    for (int g = 0; g < FG_COUNT; g++) {
        con("go arcade");
        mq_fg_start(q, g);
        bool d1 = mq_demo_running(q, SPD_FG + g, q->fg_t0);
        for (int k = 0; k < 300 && mq_demo_running(q, SPD_FG + g, q->fg_t0); k++)
            con("step 1"); /* (watched to its end: seen) */
        long e0 = earned();
        for (int k = 0; k < 8; k++) {
            q->now += 150, q->last_input = q->now;
            con("wrong");
            con("step 2");
        }
        long mash = earned() - e0;
        con("go arcade");
        mq_fg_start(q, g);
        con("step 2");
        bool d2 = mq_demo_running(q, SPD_FG + g, q->fg_t0);
        printf("| %s (arcade) | %s | %s | - | %ld (8 wrong at once) |\n", fg_names[g], d1 ? "yes" : "NO", d2 ? "YES" : "no", mash);
        CHECK(d1 && !d2 && mash == 0);
    }
    for (int g = 0; g < ACT_COUNT; g++) {
        con("go arcade");
        mq_action_start(q, g);
        con("step 2");
        bool d1 = mq_demo_running(q, SPD_ACT + g, q->act.t0);
        for (int k = 0; k < 300 && mq_demo_running(q, SPD_ACT + g, q->act.t0); k++)
            con("step 1");
        con("go arcade");
        mq_action_start(q, g);
        con("step 2");
        bool d2 = mq_demo_running(q, SPD_ACT + g, q->act.t0);
        printf("| %s (arcade) | %s | %s | - | - |\n", mq_action_name(g), d1 ? "yes" : "NO", d2 ? "YES" : "no");
        CHECK(d1 && !d2);
    }
}

int main(void)
{
    harness_init();
    test_double_fling();
    test_games();
    test_arcade();
    printf("test_mq_howto: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
