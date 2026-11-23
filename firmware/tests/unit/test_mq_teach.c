/* Sparkle Town (modules/mathquest) teaching moments (mq_teach.c):
 *   - every idea has 3..5 parts, each a try with 2..3 answers (the right one among them) and 1..3 lines;
 *   - no school words in anything a moment says or shows;
 *   - the first order with a big idea starts its moment (once); a right try says "You already know this!"
 *     and skips the part (2), a miss shows it (1), skip jumps it (3), done goes back to the game;
 *   - the game waits while it is on; at its end the notebook opens for column work she is still learning;
 *   - the helper friend twice for the same idea: the moment is offered again; the lightbulb replays it;
 *   - taps and keys work through the widgets; every line is said aloud (the speak hook);
 *   - the grown-ups' record per idea (shown, knew, skipped, replays) and the play log's lines. */
#include "mq_test_util.h"

enum { K_ENTER = 49, K_DEL = 8 };
static const char *const banned[] = {"math", "learn", "lesson", "homework", "grade", "quiz", "test", "skill", "problem",
                                     "level", "mistake", "worksheet", "borrowing", "correct", "remember", "fact",
                                     "practice", "smart", "review", NULL};
static bool school(const char *s)
{
    char w[200];
    snprintf(w, sizeof w, "%s", s);
    for (char *c = w; *c; c++)
        *c = (char)(*c | 32);
    for (int i = 0; banned[i]; i++)
        for (char *p = strstr(w, banned[i]); p; p = strstr(p + 1, banned[i])) {
            size_t n = strlen(banned[i]);
            bool l = p > w && p[-1] >= 'a' && p[-1] <= 'z', r = p[n] >= 'a' && p[n] <= 'z';
            if (!l && (!r || p[n] == 's'))
                return true;
        }
    return false;
}

/* the speak hook: what was said */
static modx_api_t tapi;
static int said_n;
static char said[160];
static void t_speak(int who, const char *s)
{
    (void)who;
    said_n++;
    snprintf(said, sizeof said, "%s", s);
}
static void key(int k) { ops.event(q, MODX_EV_KEY, k); }
static void tap(int x, int y) { ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(4, x, y)); }

static void reinit(void)
{
    memset(q, 0, mq_module_rw_size);
    memset(&ops, 0, sizeof ops);
    CHECK(mq_module_init(&tapi, q, &ops) == 0);
    q->fun_ms = 0;
    q->rng = 0x2545F491u;
    con("new Ada 3");
    q->fun_off = true;
    mq_pace(q)->voice = VO_WORDS; /* (the words said) */
    for (int w = 0; w < WS_COUNT; w++)
        mq_pace(q)->ws_level[w] = 2;
}

/* an order of this skill with this idea, in play (its story acted first unless scenes are off) */
static bool order_with(int skill, int rung, int idea)
{
    char b[48];
    snprintf(b, sizeof b, "skill %d %d", skill, rung);
    for (int i = 0; i < 40; i++) {
        q->scenes_off = true;
        con(b);
        if (q->prob.idea == idea && q->scene == SC_PLAY && q->sub == GS_PLAY)
            return true;
    }
    return false;
}
/* the widget with this label (its centre) */
static bool tap_label(const char *l)
{
    int r[4];
    q->wg.n = 0;
    ops.draw(q, 0, r);
    for (int i = 0; i < q->wg.n; i++)
        if (!strcmp(q->wg.w[i].label, l)) {
            tap(q->wg.w[i].x + q->wg.w[i].w / 2, q->wg.w[i].y + q->wg.w[i].h / 2);
            return true;
        }
    return false;
}
static int segv(int idea, int s) { return (mq_pace(q)->idea_seg[idea] >> (2 * s)) & 3; }

static void test_table(void)
{
    for (int i = 1; i < IDEA_COUNT; i++) {
        int n = mq_teach_nseg(i);
        CHECK(n >= 3 && n <= 5);
        CHECK(mq_idea_name(i)[0]);
        q->scene = SC_PLAY, q->sub = GS_PLAY;
        mq_teach_start(q, i, 2);
        CHECK(mq_teach_on(q));
        CHECK(q->teach.nseg == n);
        for (int s = 0; s < n && mq_teach_on(q); s++) {
            CHECK(q->teach.seg == s && q->teach.phase == TP_TRY);
            CHECK(!school(q->teach.say));
            CHECK(!school(said));
            /* its answers: 2 or 3, buttons big enough, one of them right */
            int r[4], nch = 0;
            q->wg.n = 0;
            ops.draw(q, 0, r);
            for (int k = 0; k < q->wg.n; k++) {
                const mq_wg_t *g = &q->wg.w[k];
                if (g->kind == WG_CHOICE) {
                    nch++;
                    CHECK(!school(g->label));
                }
                int hx, hy, hw, hh;
                mq_wg_hitbox(g, &hx, &hy, &hw, &hh);
                CHECK(hw >= MQ_WG_HIT && hh >= MQ_WG_HIT);
            }
            CHECK(nch >= 2 && nch <= 3);
            /* a wrong pick first (when there is one): shown, every line said */
            int right = -1;
            for (int k = 0; k < nch; k++) { /* (find the right one: a fresh copy of the part) */
                mq_teach_t save = q->teach;
                uint16_t sv = mq_pace(q)->idea_seg[i];
                mq_teach_pick(q, k);
                if (q->teach.phase == TP_KNEW)
                    right = k;
                q->teach = save, mq_pace(q)->idea_seg[i] = sv;
            }
            CHECK(right >= 0);
            int wrong = right == 0 ? 1 : 0;
            mq_teach_pick(q, wrong);
            CHECK(q->teach.phase == TP_SHOW && segv(i, s) == 1);
            for (int b = 0; b < 4 && mq_teach_on(q) && q->teach.seg == s; b++) {
                CHECK(!school(q->teach.say) && !school(said));
                CHECK(!strcmp(said, q->teach.say));
                mq_teach_next(q);
            }
        }
        CHECK(!mq_teach_on(q));
    }
}

static void test_first_meeting(void)
{
    reinit();
    /* a fraction order: the moment comes with it (its story acted first: after it) */
    CHECK(order_with(S_UNITFRAC, 0, IDEA_FRACTION));
    CHECK(!mq_teach_on(q)); /* (scenes off: the console's orders go straight to the game) */
    q->scenes_off = false;
    CHECK(mq_teach_due(q));
    mq_teach_start(q, IDEA_FRACTION, 0);
    CHECK(mq_teach_on(q) && q->teach.why == 0);
    CHECK(strstr(said, "fractions") && strstr(said, "?")); /* (the friend's line and the try, said) */
    /* the game waits */
    int32_t ans = q->prob.ans;
    uint32_t ps = q->prob_start;
    con("step 100");
    CHECK(mq_teach_on(q) && q->prob.ans == ans && q->sub == GS_PLAY);
    (void)ps;
    /* fair pieces: "not fair", tapped -- she knew it */
    CHECK(tap_label("not fair"));
    CHECK(q->teach.phase == TP_KNEW && segv(IDEA_FRACTION, 0) == 2 && strstr(said, "already know"));
    con("step 40"); /* (its reading time: still up) */
    CHECK(q->teach.phase == TP_KNEW);
    ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(1, 160, MQ_Y0 + 120)), tap(160, MQ_Y0 + 120); /* a tap: on */
    CHECK(q->teach.seg == 1 && q->teach.phase == TP_TRY);
    /* the bottom number: key 1 is a miss, shown; ENTER through its lines */
    key('1');
    CHECK(q->teach.phase == TP_SHOW && segv(IDEA_FRACTION, 1) == 1);
    key(K_ENTER);
    CHECK(q->teach.beat == 1);
    key(K_ENTER);
    CHECK(q->teach.seg == 2);
    /* skip: by its button, then DEL */
    CHECK(tap_label("skip"));
    CHECK(segv(IDEA_FRACTION, 2) == 3 && q->teach.seg == 3);
    key(K_DEL);
    CHECK(segv(IDEA_FRACTION, 3) == 3 && q->teach.seg == 4);
    /* "show me" on the last: shown without a pick, then it moves on by itself (a calm pace) */
    CHECK(tap_label("show me"));
    CHECK(q->teach.phase == TP_SHOW && segv(IDEA_FRACTION, 4) == 1);
    con("step 400");
    CHECK(!mq_teach_on(q));
    int sh, kn, sk;
    CHECK(mq_teach_summary(q, IDEA_FRACTION, &sh, &kn, &sk) == 5 && sh == 2 && kn == 1 && sk == 2);
    /* met: not again on the next fraction order */
    CHECK(order_with(S_UNITFRAC, 0, IDEA_FRACTION));
    q->scenes_off = false;
    CHECK(!mq_teach_due(q));
    /* the game goes on: the right answer cheers */
    con("correct");
    CHECK(q->sub == GS_CHEER);
}

static void test_auto_start(void)
{
    reinit();
    /* a new idea's order arriving (scenes on, story acted first): the moment after the story */
    q->scenes_off = false;
    mq_pace(q)->idea_offered[IDEA_AREA] = 0;
    bool seen = false;
    for (int i = 0; i < 40 && !seen; i++) {
        char b[32];
        snprintf(b, sizeof b, "skill %d 1", S_AREAMUL);
        con(b);
        q->scenes_off = false;
        if (q->prob.idea != IDEA_AREA)
            continue;
        mq_game_order(q);
        for (int f = 0; f < 600 && !mq_teach_on(q); f++)
        {
            con("step 1");
            if (q->gm.scene_on)
                tap(160, 120);
        }
        seen = mq_teach_on(q);
    }
    CHECK(seen);
    CHECK(q->teach.idea == IDEA_AREA && q->teach.why == 0);
    /* done: the rest skipped, back to the game */
    CHECK(tap_label("done"));
    CHECK(!mq_teach_on(q) && q->sub == GS_PLAY);
    int sh, kn, sk;
    mq_teach_summary(q, IDEA_AREA, &sh, &kn, &sk);
    CHECK(sk == mq_teach_nseg(IDEA_AREA));
}

static void test_helper_offer_and_bulb(void)
{
    reinit();
    CHECK(order_with(S_UNITFRAC, 0, IDEA_FRACTION));
    mq_pace(q)->idea_offered[IDEA_FRACTION] = 0x80; /* met before */
    q->scenes_off = false;
    CHECK(!mq_teach_due(q));
    /* the helper friend comes twice for fraction orders: the moment is offered on the next one */
    mq_helper_start(q);
    CHECK(!mq_teach_due(q));
    q->sub = GS_PLAY;
    mq_helper_start(q);
    q->sub = GS_PLAY;
    CHECK(mq_teach_due(q));
    mq_teach_start(q, IDEA_FRACTION, 1);
    CHECK(strstr(q->teach.say, "once more") && strstr(q->teach.say, "skip"));
    mq_teach_close(q);
    CHECK(!mq_teach_due(q)); /* (counted again from here) */
    mq_pace(q)->idea_offered[IDEA_FRACTION] = 0x82; /* (twice more at once: the game first, three orders) */
    CHECK(!mq_teach_due(q));
    mq_prof(q)->problems += 3;
    CHECK(mq_teach_due(q));
    mq_pace(q)->idea_offered[IDEA_FRACTION] = 0x80;
    /* the lightbulb: its big idea button replays it */
    CHECK(mq_teach_has(q));
    con("bulb");
    CHECK(q->gm.bulb_t);
    int bx, by, bw, bh;
    CHECK(mq_bulb_idea_btn(q, &bx, &by, &bw, &bh));
    CHECK(bw >= 44 && bh >= 24);
    tap(bx + bw / 2, by + bh / 2);
    CHECK(mq_teach_on(q) && q->teach.why == 2 && mq_pace(q)->idea_replays[IDEA_FRACTION] == 1);
    con("teach done");
    CHECK(!mq_teach_on(q));
    /* ENTER in the lightbulb's scene: the same */
    con("bulb");
    key(K_ENTER);
    CHECK(mq_teach_on(q) && mq_pace(q)->idea_replays[IDEA_FRACTION] == 2);
    con("teach done");
    /* an order without an idea: no button */
    q->prob.idea = IDEA_NONE;
    CHECK(!mq_teach_has(q) && !mq_bulb_idea_btn(q, &bx, &by, &bw, &bh));
}

static void test_notebook_after(void)
{
    reinit();
    /* adding with carries (an idea and column work): the moment first, then the notebook */
    for (int w = 0; w < WS_COUNT; w++)
        mq_pace(q)->ws_level[w] = 0;
    bool ok = false;
    for (int i = 0; i < 60 && !ok; i++) {
        q->scenes_off = true;
        char b[32];
        snprintf(b, sizeof b, "skill %d 4", S_ADDSUB);
        con(b);
        snprintf(b, sizeof b, "mech %d", ME_VAULT);
        con(b);
        ok = q->prob.idea == IDEA_CARRY && q->gm.mech == ME_VAULT;
        if (!ok) {
            q->gm.round_mech = ME_VAULT;
            mq_game_order(q);
            ok = q->prob.idea == IDEA_CARRY && q->gm.mech == ME_VAULT;
        }
    }
    CHECK(ok);
    mq_cols_close(q);
    q->scenes_off = false;
    CHECK(mq_teach_due(q));
    mq_teach_start(q, IDEA_CARRY, 0);
    CHECK(mq_teach_on(q) && !mq_cols_on(q));
    con("teach done");
    CHECK(!mq_teach_on(q) && mq_cols_on(q));
    /* the console's right answer goes through both */
    q->scenes_off = false;
    mq_pace(q)->idea_offered[IDEA_CARRY] = 0;
    mq_cols_close(q);
    mq_teach_start(q, IDEA_CARRY, 0);
    con("correct");
    CHECK(!mq_teach_on(q) && q->sub == GS_CHEER);
}

static int log_lines;
static bool log_idea;
static void log_out(void *u, const char *l)
{
    (void)u;
    log_lines++;
    if (!strncmp(l, "big_idea,fractions,5,", 21))
        log_idea = true;
}
static void test_grownups(void)
{
    reinit();
    CHECK(order_with(S_UNITFRAC, 0, IDEA_FRACTION));
    mq_teach_start(q, IDEA_FRACTION, 0);
    con("teach skip");
    con("teach pick 2");
    con("teach done");
    mq_coach_log(q, log_out, NULL);
    CHECK(log_idea);
    int sh, kn, sk;
    CHECK(mq_teach_summary(q, IDEA_FRACTION, &sh, &kn, &sk) == 5 && kn == 1 && sk == 4);
    /* the grown-ups' Time tab says it */
    CHECK(con("teach") == 0);
}

/* the user: "sharing problems: when asking how many each gets, don't show the number of items in each part" --
 * the sharing tries show the treats in one pile and the plates empty (no treat drawn right of the pile) */
static void test_share_plates_empty(void)
{
    reinit();
    q->scene = SC_PLAY, q->sub = GS_PLAY;
    mq_teach_start(q, IDEA_SHARE, 2);
    CHECK(mq_teach_on(q));
    int tries = 0;
    for (int s = 0; s < q->teach.nseg && mq_teach_on(q); s++) {
        CHECK(q->teach.phase == TP_TRY);
        con("step 60");
        int r[4], pile = 0, plates = 0;
        ops.draw(q, 0, r);
        for (int y = 80; y < 175; y++)
            for (int x = 0; x < 320; x++)
                if (fb[y * 320 + x] == Q_HOTPINK)
                    *(x < 76 ? &pile : &plates) += 1;
        CHECK(pile > 0);
        CHECK(plates == 0);
        tries++;
        mq_teach_pick(q, 0); /* (the right one: "you already know") */
        for (int b = 0; b < 4 && mq_teach_on(q) && q->teach.seg == s; b++)
            mq_teach_next(q);
    }
    CHECK(tries >= 3);
}

int main(void)
{
    harness_init();
    tapi = modx_api;
    tapi.speak = t_speak;
    reinit();
    test_table();
    test_share_plates_empty();
    test_first_meeting();
    test_auto_start();
    test_helper_offer_and_bulb();
    test_notebook_after();
    test_grownups();
    printf("test_mq_teach: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
