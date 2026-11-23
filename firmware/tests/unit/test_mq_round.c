/* Sparkle Town (modules/mathquest) Hop Along's rounding teaches the method (the user: "teach rounding as opposed
 * to just picking the closer side"; "show the number, but not on the scale"):
 *   - the number is on its card and never on the line before she answers (the line shows its real spot after a
 *     right hop, with why: past the middle up, before it down, on it up);
 *   - the help fades: a new player sees both round numbers and the middle (0); with the skill half known she finds
 *     the two round numbers herself first (1: three pairs, a wrong one only wobbles with a hint, nothing judged);
 *     known well, the numbers only (2: no line); an error pattern of rounding being practised brings the most help
 *     back;
 *   - two ends: no second guess after a miss (it would be no thinking), and what the friend says then is the
 *     method's question, never the answer;
 *   - the error patterns: rounding to the farther ten, a 5 rounded down (test_mq_diag has their followers). */
#include "mq_test_util.h"
#include "../../core/gen/keymap_gen.h"

enum { K_LEFT = 13, K_RIGHT = 14, K_ENTER = 49 };
#define LY (MQ_Y0 + 58 + 74)

static void frame(void)
{
    q->redraw = true;
    int r[4];
    ops.draw(q, 0, r);
}
/* pixels of the parcel's colour in the band over the line (where its real spot is drawn) */
static int spot_pixels(void)
{
    frame();
    int n = 0;
    for (int y = LY - 16; y < LY - 2; y++)
        for (int x = 16; x < 304; x++)
            n += fb[y * 320 + x] == Q_PEACH;
    return n;
}
static void order(int skill, int rung)
{
    char b[40];
    snprintf(b, sizeof b, "skill %d %d", skill, rung);
    con(b);
    snprintf(b, sizeof b, "mech %d", ME_JUMP);
    con(b);
    con("step 2");
}
static void tap(int x, int y)
{
    ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(1, x, y));
    ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(4, x, y));
}

int main(void)
{
    harness_init();
    con("new Ada 3"), con("engine stop"), con("fun off"), con("scene off"), con("grant bosses");
    mq_game_t *g = &q->gm;
    mq_profile_t *pr = mq_prof(q);
    /* the help fades with what she knows */
    CHECK(mq_round_level(q, S_ROUND10) == 0);
    pr->sk[S_ROUND10].mastery = 50;
    CHECK(mq_round_level(q, S_ROUND10) == 1);
    pr->sk[S_ROUND10].mastery = 80;
    CHECK(mq_round_level(q, S_ROUND10) == 2);
    mq_diag_rec(q, PT_ROUND_HALF_DOWN)->state = PS_PRACTICE; /* (a pattern being practised: the most help again) */
    CHECK(mq_round_level(q, S_ROUND10) == 0);
    mq_diag_rec(q, PT_ROUND_HALF_DOWN)->state = PS_NONE;
    pr->sk[S_ROUND10].mastery = 0;

    int orders = 0, revealed = 0;
    for (int lv = 0; lv < 3; lv++)
        for (int sk = 0; sk < 2; sk++)
            for (int rung = 0; rung < 4; rung++)
                for (int rep = 0; rep < 3; rep++) {
                    char b[24];
                    snprintf(b, sizeof b, "roundhelp %d", lv);
                    con(b);
                    order(sk ? S_ROUNDBIG : S_ROUND10, rung);
                    if (q->scene != SC_PLAY || g->mech != ME_JUMP || g->mode != 1)
                        continue;
                    orders++;
                    CHECK(g->rlevel == lv && g->rstep == (lv == 1 ? 0 : 1));
                    int32_t n = g->value, lo = g->opt[0], hi = g->opt[1];
                    CHECK(lo < n && n < hi && (hi - lo == 10 || hi - lo == 100 || hi - lo == 1000 || hi - lo == 10000));
                    int before = spot_pixels(); /* (not on the line: compared with after) */
                    if (lv == 1) { /* the three pairs: never below 0, the right one among them; a wrong one wobbles */
                        for (int k = 0; k < 3; k++)
                            CHECK(lo + (k - g->rright) * (hi - lo) >= 0);
                        int tries = pr->sk[g->rright ? S_ROUND10 : S_ROUND10].tries;
                        int wrongk = (g->rright + 1) % 3;
                        tap(8 + wrongk * 103 + 49, MQ_Y0 + 58 + 34 + 17);
                        CHECK(g->rstep == 0 && g->rwob == wrongk + 1 && q->sub == GS_PLAY);
                        CHECK(pr->sk[S_ROUND10].tries == tries); /* (nothing judged) */
                        CHECK(!strstr(g->say, "and"));
                        if (rep == 0) { /* by touch */
                            tap(8 + g->rright * 103 + 49, MQ_Y0 + 58 + 34 + 17);
                        } else { /* by keys */
                            while (g->rsel != g->rright)
                                ops.event(q, MODX_EV_KEY, g->rsel < g->rright ? K_RIGHT : K_LEFT);
                            ops.event(q, MODX_EV_KEY, K_ENTER);
                        }
                        CHECK(g->rstep == 1 && q->sub == GS_PLAY);
                        before = spot_pixels();
                    }
                    /* the hop: a tap on the right pad, then land! */
                    int e = q->prob.ans == hi;
                    tap(e ? MQ_W - 32 : 32, MQ_Y0 + 58 + 76);
                    CHECK(g->aim == e);
                    ops.event(q, MODX_EV_KEY, K_ENTER);
                    CHECK(q->sub != GS_PLAY && g->given == q->prob.ans);
                    revealed += spot_pixels() >= before + 60; /* (now its real spot: the parcel, 16 x 14) */
                    con("step 80");
                }
    printf("test_mq_round: %d orders, %d revealed after\n", orders, revealed);
    CHECK(orders >= 50 && revealed == orders);
    /* a miss: judged at once (two ends: no second guess), and the friend asks the method's question */
    con("roundhelp 0");
    for (int t = 0; t < 10; t++) {
        order(S_ROUND10, 0);
        if (g->mech != ME_JUMP || g->mode != 1)
            continue;
        int32_t n = g->value, lo = g->opt[0], hi = g->opt[1], ans = q->prob.ans;
        int tries = pr->sk[S_ROUND10].tries;
        q->last_input = q->now;
        g->aim = ans == lo ? 1 : 0;
        ops.event(q, MODX_EV_KEY, K_ENTER);
        CHECK(pr->sk[S_ROUND10].tries == tries + 1);
        char ns[12], as[12];
        snprintf(ns, sizeof ns, "%ld", (long)n), snprintf(as, sizeof as, "to %ld", (long)ans);
        if (strstr(g->say, "middle")) { /* (the method's question; the helper may have come instead) */
            CHECK(strstr(g->say, ns) != NULL && !strstr(g->say, as));
            CHECK(strstr(g->say, "past the middle") || strstr(g->say, "on the middle"));
        }
        con("step 200");
        break;
    }
    con("roundhelp -1");
    printf("test_mq_round: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
