/* Sparkle Town (modules/mathquest): every strand of her grade comes round (the playtest: "shapes never
 * appear"). A simulated grade-3 player whose school is in week 10 plays her first two weeks (ten play
 * days, six rounds a day) in the places she likes best -- never the Mural, the Fair or the Clock Tower:
 *   - the low-prerequisite strands school has not reached yet (shapes 3.G, time, simple data) still
 *     appear early, in a light share (a taste of each, never crowding out the year's work);
 *   - the grade-4 geometry (4.G: lines, sorting shapes, mirror lines) comes once she is past grade 3 there;
 *   - the bulk of the orders stays on what school has taught.
 * Built and run by `make test`. */
#include "mq_test_util.h"

static uint32_t sim_rng = 11;
static int sim_rnd(int n)
{
    sim_rng = sim_rng * 1103515245u + 12345u;
    return (int)((sim_rng >> 16) % (uint32_t)n);
}

enum { C_SHAPES, C_TIME, C_DATA, C_TAUGHT, C_ALL, C_N };

static bool geometry(int s) { return s == S_SHAPES || s == S_PARTITION || s == S_LINES || s == S_CLASSIFY || s == S_SYMMETRY; }
static bool time_skill(int s) { return s == S_TIME5 || s == S_TIME1 || s == S_ELAPSED; }
static bool data_skill(int s) { return s == S_GRAPH1 || s == S_SCALED || s == S_RULER; }

/* plays `days` play days of `rounds` rounds in the places of `worlds`; counts the orders (cnt) and the
 * first play day each kind came (first) */
static void play(int days, int rounds, const int *worlds, int nw, int pr_right, int cnt[C_N], int first[C_N])
{
    mq_profile_t *p = mq_prof(q);
    for (int d = 0; d < days; d++) {
        p->days = (uint16_t)(p->days + 1);
        for (int r = 0; r < rounds; r++) {
            char b[24];
            snprintf(b, sizeof b, "level %d 0", worlds[sim_rnd(nw)]);
            con(b);
            for (int k = 0; k < 12 && q->scene == SC_PLAY; k++) {
                if (q->sub != GS_PLAY) {
                    con("step 40");
                    continue;
                }
                int s = q->prob.skill;
                int kinds[C_N] = {geometry(s), time_skill(s), data_skill(s), mq_school_taught(q, s) || s == S_FACTS, 1};
                for (int c = 0; c < C_N; c++)
                    if (kinds[c]) {
                        cnt[c]++;
                        if (first[c] < 0)
                            first[c] = d;
                    }
                if (getenv("MQ_TRACE"))
                    fprintf(stderr, "day %d round %d: skill %d (%s) g%d taught %d\n", d, r, s, mq_skill[s].name,
                            mq_skill[s].grade, mq_school_taught(q, s));
                q->now += 3000 + (uint32_t)sim_rnd(3000);
                q->fun_ms = 0;
                con(sim_rnd(100) < pr_right ? "correct" : "wrong");
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
}

static void test_grade3_week10(void)
{
    static const int fav[] = {G_GARDEN, G_BAKERY, G_POST, G_SHOP};
    for (int seed = 0; seed < 3; seed++) {
        con("new Wen 2");
        con("school 3 10");
        q->fun_off = true;
        q->rng = 0x9E3779B9u * (uint32_t)(seed + 1) | 1;
        sim_rng = 11 + (uint32_t)seed;
        int cnt[C_N] = {0}, first[C_N];
        for (int c = 0; c < C_N; c++)
            first[c] = -1;
        play(10, 6, fav, 4, 88, cnt, first);
        printf("grade 3, week 10, seed %d: %d orders; shapes %d (day %d), time %d (day %d), data %d (day %d); taught %d\n",
               seed, cnt[C_ALL], cnt[C_SHAPES], first[C_SHAPES], cnt[C_TIME], first[C_TIME], cnt[C_DATA], first[C_DATA],
               cnt[C_TAUGHT]);
        CHECK(cnt[C_ALL] > 300);
        for (int c = C_SHAPES; c <= C_DATA; c++) { /* each strand early (in the first week), and only a taste */
            CHECK(first[c] >= 0 && first[c] <= 4);
            CHECK(cnt[c] >= 3 && cnt[c] * 100 <= cnt[C_ALL] * 12);
        }
        CHECK((cnt[C_SHAPES] + cnt[C_TIME] + cnt[C_DATA]) * 100 <= cnt[C_ALL] * 25); /* a light share in all */
        CHECK(cnt[C_TAUGHT] * 100 >= cnt[C_ALL] * 60); /* most of it: what school has taught */
        /* the grown-ups' recent orders: the last 30, each with why it came (tastes and goals among them) */
        int why[PW_COUNT] = {0};
        for (int i = 0; i < q->recent_n; i++) {
            why[q->recent_o[i].why % PW_COUNT]++;
            CHECK(q->recent_o[i].q[0] && q->recent_o[i].her[0]);
        }
        CHECK(q->recent_n == MQ_NRECENT && why[PW_TASTE] >= 1 && why[PW_GOAL] + why[PW_NEW] + why[PW_REVIEW] + why[PW_CHECK] >= 10);
    }
}

/* grade 4 (week 20): the 4.G strand (lines, sorting shapes, mirror lines) comes round too */
static void test_grade4_geometry(void)
{
    static const int fav[] = {G_GARDEN, G_BAKERY, G_POST, G_SHOP, G_CLOCK};
    con("new Gia 2");
    con("school 4 20");
    q->fun_off = true;
    q->rng = 0x51ED27u | 1;
    sim_rng = 99;
    int cnt[C_N] = {0}, first[C_N];
    for (int c = 0; c < C_N; c++)
        first[c] = -1;
    play(10, 6, fav, 5, 90, cnt, first);
    printf("grade 4, week 20: %d orders; geometry %d (day %d)\n", cnt[C_ALL], cnt[C_SHAPES], first[C_SHAPES]);
    CHECK(cnt[C_SHAPES] >= 3 && first[C_SHAPES] >= 0 && first[C_SHAPES] <= 5);
}

/* no school week said (the default): shapes, time and data come round all the same */
static void test_no_school(void)
{
    static const int fav[] = {G_GARDEN, G_BAKERY, G_POST};
    con("new Nos 2");
    q->fun_off = true;
    q->rng = 0xC0FFEEu | 1;
    sim_rng = 5;
    int cnt[C_N] = {0}, first[C_N];
    for (int c = 0; c < C_N; c++)
        first[c] = -1;
    play(10, 6, fav, 3, 88, cnt, first);
    printf("no school said: %d orders; shapes %d (day %d), time %d, data %d\n", cnt[C_ALL], cnt[C_SHAPES], first[C_SHAPES],
           cnt[C_TIME], cnt[C_DATA]);
    for (int c = C_SHAPES; c <= C_DATA; c++)
        CHECK(first[c] >= 0 && first[c] <= 4 && cnt[c] >= 3 && cnt[c] * 100 <= cnt[C_ALL] * 12);
}

/* the grown-ups change the grade of an EXISTING player (the user: "I set 4th grade ... not much difference"): from
 * the next orders on, the new grade's topics; kept over a reload; and back down the same way */
static int grade_share(int n, int want, bool down)
{
    static const int fav[] = {G_GARDEN, G_BAKERY, G_POST, G_SHOP};
    int hit = 0, all = 0;
    mq_profile_t *p = mq_prof(q);
    for (int r = 0; all < n && r < 40; r++) {
        char b[24];
        snprintf(b, sizeof b, "level %d 0", fav[sim_rnd(4)]);
        con(b);
        for (int k = 0; k < 12 && all < n && q->scene == SC_PLAY; k++) {
            if (q->sub != GS_PLAY) {
                con("step 40");
                continue;
            }
            int s = q->prob.skill;
            if (s != S_FACTS && !q->diag_item) { /* (the facts are a thread through every grade; an order aimed at an
                                                  * error pattern she showed is about her, whatever its grade: at most
                                                  * one in three, mq_diag_pick) */
                all++;
                hit += down ? mq_skill[s].grade <= want : mq_skill[s].grade >= want;
                if (getenv("MQ_TRACE"))
                    fprintf(stderr, "grade %d: %s (g%d) why %d rev %d\n", want, mq_skill[s].name, mq_skill[s].grade, q->pick_why, q->rev_item);
            }
            q->now += 4000;
            q->fun_ms = 0;
            con(sim_rnd(100) < 85 ? "correct" : "wrong");
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
    (void)p;
    return all ? hit * 100 / all : 0;
}
static void test_grade_change(void)
{
    /* (three players, other dice each: one player's 20 orders were a coin toss away from the bar -- an unlucky run of
     * misses steps a strand down, as it should; the shares are the three's) */
    static const unsigned seeds[3] = {21, 55, 89};
    int up = 0, up2 = 0, down = 0, wk = 0, g = 0;
    for (int k = 0; k < 3; k++) {
        if (k)
            fresh_module();
        con("new Gc 2");
        con("school 3 10");
        q->fun_off = true;
        q->rng = (0xBADC0DEu ^ seeds[k]) | 1;
        sim_rng = seeds[k];
        int cnt[C_N] = {0}, first[C_N];
        play(2, 6, (const int[]){G_GARDEN, G_BAKERY, G_POST}, 3, 85, cnt, first); /* her first days at grade 3 */
        con("school 4 10");                                                            /* the grown-ups: grade 4 */
        up += grade_share(20, 4, false);
        mq_save_now(q);
        fresh_module(); /* a reload */
        q->fun_off = true;
        q->rng = (0xBADC0DEu ^ seeds[k] * 2654435761u) | 1; /* (the module's dice after it: the player's own too) */
        g = mq_school(q, &wk);
        up2 += grade_share(20, 4, false);
        con("school 3 10"); /* and back down */
        down += grade_share(20, 3, true);
        CHECK(g == 4 && wk == 10);
    }
    up /= 3, up2 /= 3, down /= 3;
    printf("grade change: to 4: %d %% grade 4, after a reload %d %% (school %d week %d); back to 3: %d %% grade 3 or below\n", up,
           up2, g, wk, down);
    CHECK(up >= 70 && up2 >= 70 && down >= 70);
}

int main(void)
{
    harness_init();
    test_grade_change();
    test_no_school();
    test_grade3_week10();
    test_grade4_geometry();
    printf("test_mq_coverage: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
