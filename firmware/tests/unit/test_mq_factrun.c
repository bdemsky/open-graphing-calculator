/* Sparkle Town (modules/mathquest): the facts move on (the playtest: "lots of 0 x and 1 x, never 6 x 5";
 * "her facts never get to fluent"). A competent player plays the game as it comes -- the engine's orders in
 * the places she likes, the facts among them, answered right in a game's time (about 2-4 s from when an
 * answer can be given for a fact she knows, longer for a new one) -- over many play days:
 *   - the first rules check asks only two x0 / x1 facts;
 *   - x0 / x1 facts stay at most 5 % of her fact orders throughout (after that check);
 *   - she reaches the x6, x7 and x8 tables within a sensible time (play days, not months);
 *   - box 4 (fluent, held on two days) is reached in game play;
 * from a fresh save and from an old one (the stalled boxes: x0 / x1 and a few x2 facts stuck in box 1).
 * Built and run by `make test`. */
#include "mq_test_util.h"

static uint32_t rr = 3;
static int rnd(int n)
{
    rr = rr * 1103515245u + 12345u;
    return (int)((rr >> 16) % (uint32_t)n);
}

typedef struct {
    int facts, trivial, rule_check, first_day[13], fluent, worst_window;
} run_t;

static bool has_factor(int f, int x)
{
    int a, b;
    bool div;
    mq_fact_decode(f, &a, &b, &div);
    return !div && ((a == x && b >= 2) || (b == x && a >= 2));
}

static void play_days(int days, run_t *r)
{
    static const int worlds[] = {G_GARDEN, G_GARDEN, G_BAKERY, G_POST, G_SHOP};
    mq_profile_t *p = mq_prof(q);
    int win[60] = {0}, wi = 0;
    for (int d = 0; d < days; d++) {
        p->days = (uint16_t)(p->days + 1);
        char b[24];
        snprintf(b, sizeof b, "day %d", p->days);
        con(b);
        for (int round = 0; round < 8; round++) { /* about 20 minutes a day */
            snprintf(b, sizeof b, "level %d 0", worlds[rnd(5)]);
            con(b);
            for (int k = 0; k < 12 && q->scene == SC_PLAY; k++) {
                if (q->sub != GS_PLAY) {
                    con("step 40");
                    continue;
                }
                int ms = 6000;
                if (q->prob.skill == S_FACTS) {
                    int f = q->prob.a[0], a, bb;
                    bool div;
                    mq_fact_decode(f, &a, &bb, &div);
                    bool rules = mq_fact_rules_known(p), tr = mq_fact_trivial(f);
                    if (!rules)
                        r->rule_check++;
                    else {
                        r->facts++, r->trivial += tr;
                        win[wi % 60] = tr, wi++;
                        if (wi >= 60) {
                            int n = 0;
                            for (int i = 0; i < 60; i++)
                                n += win[i];
                            r->worst_window = n > r->worst_window ? n : r->worst_window;
                        }
                    }
                    for (int x = 2; x <= 12; x++)
                        if (has_factor(f, x) && r->first_day[x] < 0)
                            r->first_day[x] = d;
                    /* she knows it (box 2+): 2-4 s; new or shaky: 4-7 s */
                    ms = p->fact[f].box >= 2 ? 2000 + rnd(2000) : 4000 + rnd(3000);
                }
                q->now += (uint32_t)ms, q->last_input = q->now;
                q->fun_ms = 0;
                con("correct");
                for (int j = 0; j < 60 && q->scene == SC_PLAY && (q->sub != GS_PLAY || q->gm.helped); j++) {
                    if (q->sub == GS_PLAY && q->gm.helped) {
                        con("correct");
                        if (q->sub == GS_CHEER)
                            q->gm.helped = 0;
                        continue;
                    }
                    con("next");
                }
            }
            mq_go(q, SC_MAP);
        }
    }
    r->fluent = mq_facts_fluent(p, NULL);
}

static void report(const char *what, const run_t *r)
{
    printf("%s: rules check %d facts; then %d fact orders, %d x0/x1 (%d.%d %%, worst 60-window %d); first day of x6 %d, "
           "x7 %d, x8 %d, x9 %d; %d facts fluent\n",
           what, r->rule_check, r->facts, r->trivial, r->facts ? r->trivial * 100 / r->facts : 0,
           r->facts ? r->trivial * 1000 / r->facts % 10 : 0, r->worst_window, r->first_day[6], r->first_day[7],
           r->first_day[8], r->first_day[9], r->fluent);
}

static void check_run(const run_t *r, int days)
{
    CHECK(r->rule_check <= 2);
    CHECK(r->facts > 200 && r->trivial * 100 <= r->facts * 5 && r->worst_window <= 3);
    for (int x = 6; x <= 8; x++)
        CHECK(r->first_day[x] >= 0 && r->first_day[x] <= days * 2 / 3);
    CHECK(r->fluent >= 20);
}

static run_t fresh_run(void)
{
    run_t r = {0};
    for (int x = 0; x < 13; x++)
        r.first_day[x] = -1;
    return r;
}

static void test_fresh(void)
{
    con("new Fr 2");
    q->fun_off = true;
    rr = 3;
    run_t r = fresh_run();
    play_days(30, &r);
    report("fresh save, 30 play days", &r);
    check_run(&r, 30);
}

/* a save from before the fix: x0 / x1 facts and a few x2 / x5 facts seen again and again, stuck in box 1,
 * due every round; the schedule's version 0 (mq_fact_migrate runs at the load) */
static void test_old_save(void)
{
    fresh_module();
    con("new Old 2");
    mq_profile_t *p = mq_prof(q);
    for (int f = 0; f < 169; f++) {
        int a = f / 13, b = f % 13;
        if (a > 10 || b > 10)
            continue;
        if (mq_fact_trivial(f) || ((a == 2 || b == 2 || a == 5 || b == 5) && a <= 5 && b <= 5)) {
            p->fact[f].seen = 9, p->fact[f].right = 6, p->fact[f].box = 1, p->fact[f].due = 0, p->fact[f].t10 = 55;
        }
    }
    p->round = 40, p->days = 12, p->problems = 900;
    mq_pace(q)->fact_ver = 0, mq_pace(q)->fact_t10 = 0;
    con("save");
    mq_save_now(q);
    fresh_module(); /* the load: the migration */
    q->fun_off = true;
    CHECK(mq_pace(q)->fact_ver == 1 && mq_prof(q)->fact[mq_fact_index(0, 7, false)].box >= 2);
    rr = 77;
    run_t r = fresh_run();
    r.rule_check = 0;
    play_days(30, &r);
    report("old save, 30 play days", &r);
    check_run(&r, 30);
}

/* box 4 in a game's time: a fact answered at her usual pace (5 s from when it could be answered, the answers
 * flying in) is quick for her; at a third over it is not */
static void test_quick(void)
{
    fresh_module();
    con("new Qk 2");
    CHECK(mq_fact_quick_ms(q) == 3000);
    for (int i = 0; i < 40; i++)
        mq_fact_record(q, mq_fact_index(3, 4, false), true, 5000);
    CHECK(mq_fact_quick_ms(q) >= 4400 && mq_fact_quick_ms(q) <= 4600);
    mq_profile_t *p = mq_prof(q);
    int f = mq_fact_index(6, 7, false);
    mq_fact_record(q, f, true, 4000); /* right the first time, quick for her: box 3 */
    CHECK(p->fact[f].box == 3);
    mq_fact_record(q, f, true, 6500); /* slow for her: it stays */
    CHECK(p->fact[f].box == 3);
    mq_fact_record(q, f, true, 4200);
    CHECK(p->fact[f].box == 4);
    for (int i = 0; i < 200; i++)
        mq_fact_record(q, mq_fact_index(3, 4, false), true, 9000);
    CHECK(mq_fact_quick_ms(q) == 6000); /* never slower than 6 s */
}

/* the user: "fact review in fixed-factor sets, as schools do (3 x the numbers)" -- times-table runs: a forced
 * run is one table with the other factor shuffled and never repeated; in play, runs come now and then (for the
 * newest table she is learning), and only with facts she has met */
static void test_table_runs(void)
{
    fresh_module();
    con("new Tr 3");
    mq_profile_t *p = mq_prof(q);
    p->fact[mq_fact_index(0, 5, false)].right = 1, p->fact[mq_fact_index(0, 5, false)].seen = 1;
    p->fact[mq_fact_index(1, 5, false)].right = 1, p->fact[mq_fact_index(1, 5, false)].seen = 1;
    static const int met[4] = {2, 5, 10, 3};
    for (int i = 0; i < 4; i++)
        for (int k = 2; k <= 10; k++) {
            mq_fact_t *ft = &p->fact[mq_fact_index(met[i], k, false)];
            ft->seen = 3, ft->right = 2, ft->box = met[i] == 3 ? 1 : 3, ft->due = 0;
        }
    CHECK(mq_fact_run_start(q, 3));
    CHECK(!mq_fact_run_start(q, 8)); /* (the 8s not met yet: no run) */
    CHECK(mq_fact_run_start(q, 3));
    uint16_t seen_k = 0;
    for (int i = 0; i < 6; i++) {
        int a, b;
        bool dv;
        mq_fact_decode(mq_fact_next(q), &a, &b, &dv);
        CHECK(a == 3 && !dv && b >= 2 && b <= 10 && !((seen_k >> b) & 1));
        seen_k |= (uint16_t)(1u << b);
    }
    /* in play: runs start now and then, each one table (her newest: the 3s), never a fact she has not met */
    int runs = 0, inrun = 0;
    for (int i = 0; i < 600; i++) {
        int a, b;
        bool dv;
        int f = mq_fact_next(q);
        mq_fact_decode(f, &a, &b, &dv);
        if (q->trun_t) {
            runs += !inrun;
            inrun++;
            CHECK(a == q->trun_t - 1 && !dv && p->fact[f].seen);
        } else
            inrun = 0;
        p->round++;
    }
    printf("table runs: %d runs in 600 fact picks\n", runs);
    CHECK(runs >= 5 && runs <= 60);
}

int main(void)
{
    harness_init();
    test_quick();
    fresh_module();
    test_fresh();
    test_old_save();
    test_table_runs();
    printf("test_mq_factrun: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
