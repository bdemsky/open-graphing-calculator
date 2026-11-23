/* Sparkle Town (modules/mathquest) the ways to work it (mq_strat.c): Common Core strategies as hands-on models.
 *   - coverage: every strategy the curriculum lists has a model (its steps plan for the example and for
 *     orders), a teaching segment (opening lines and steps the friend shows) and an order type (a "use this
 *     way" order whose answer the model ends with); the test fails when one is missing;
 *   - models: worked to the end with every move right, the picture shows the numbers (blocks, dots, hops,
 *     pieces ...: mq_strat_check), for many random orders of every way; wrong moves are refused with a why;
 *   - the turn-around trick draws both rectangles, with the same count, before and after the turn;
 *   - the pacing gates: school's grade and week open the ways (grade 3, week 10: the number line, not yet
 *     partial sums or fraction strips), the standard algorithms only at their grade (4 and 5);
 *   - gestures: a hop dragged onto the path by touch, a chip carried with keys, a slider dragged, a tap, a
 *     pick by a digit key; the teaching segment's "try one" skips to her own;
 *   - the helper friend and the lightbulb pick the way that fits the order (and the helper a second one);
 *   - orders: at school's week the engine introduces a way, then asks for it now and then; solving it in the
 *     model answers the order in the game;
 *   - kid words: nothing a way says or shows uses school words; the grown-ups' list shows Common Core names. */
#include "mq_test_util.h"

static const char *const banned[] = {"math", "maths", "learn", "lesson", "homework", "grade", "quiz", "test", "skill", "problem",
                                     "level", "mistake", "worksheet", "borrowing", "correct", "remember", "fact", "facts",
                                     "practice", "smart", "review", "strategy", "strategies", "array", "algorithm", "partial",
                                     "product", "quotient", "commutative", "associative", "distributive", "property",
                                     "expanded", "compensation", "benchmark", "equation", "diagram", "decompose", "denominator",
                                     "numerator", "standard", "number line", NULL};
static int kid_bad;
static bool kid_on;
static bool school(const char *s)
{
    char w[220];
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
static void kid(const char *s)
{
    if (kid_on && s && school(s) && kid_bad++ < 8)
        fprintf(stderr, "a way says \"%s\"\n", s);
}
static modx_api_t tapi;
static char shown[64][80];
static int nshown;
static int t_text(const font_t *f, int x, int y, const char *s, uint16_t fg, int bg)
{
    kid(s);
    if (nshown < 64)
        snprintf(shown[nshown++], sizeof shown[0], "%s", s);
    return modx_api.text(f, x, y, s, fg, bg);
}
static int said_n;
static void t_speak(int who, const char *s)
{
    (void)who;
    said_n++;
    kid(s);
}
static void key(int k) { ops.event(q, MODX_EV_KEY, k); }
static void touch(int type, int x, int y) { ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(type, x, y)); }
static void draw(void)
{
    int r[4];
    nshown = 0;
    q->redraw = true;
    ops.draw(q, 0, r);
}
static void frames(int n)
{
    char b[24];
    snprintf(b, sizeof b, "step %d", n);
    con(b);
}
static void reinit(void)
{
    memset(q, 0, mq_module_rw_size);
    memset(&ops, 0, sizeof ops);
    CHECK(mq_module_init(&tapi, q, &ops) == 0);
    q->fun_ms = 0;
    q->rng = 0x2545F491u;
    con("new Ada 3");
    q->fun_off = true;
    q->scenes_off = true;
    mq_pace(q)->voice = VO_WORDS;
    for (int w = 0; w < WS_COUNT; w++)
        mq_pace(q)->ws_level[w] = 2;
}
static void in_play(void)
{
    con("engine stop");
    con("skill 16 0");
    CHECK(q->scene == SC_PLAY);
}

/* ------------------------------------------------------------------ coverage */
/* the strategies of the curriculum (the grown-ups' names): each one must be a way with a model, a teaching
 * segment and an order type */
static const char *const required[] = {
    "Open number line", "Partial sums", "Expanded form", "Making ten", "Compensation", "Counting up to subtract",
    "Standard algorithm (add", "Equal groups", "Arrays", "Area model", "Partial products", "Doubling and halving",
    "Using known facts", "Standard algorithm (multiply", "Commutative", "Associative", "Distributive", "Identity and zero",
    "Friendly numbers", "Equal sharing", "Equal grouping", "Think multiplication", "Partial quotients",
    "Area model for division", "Tape diagram: part-part-whole", "Tape diagram: comparison", "Tape diagram: equal groups",
    "Fraction strips", "Area models (circles", "Fractions on a number line", "Benchmark fractions", "Unit fractions",
    "Equivalent fractions", "Place-value chart", "Base-ten blocks", "Money model", "elapsed time", "Line plots", NULL};

static void test_coverage(void)
{
    reinit();
    in_play();
    int fam[SF_COUNT] = {0};
    for (int i = 0; required[i]; i++) {
        int found = -1;
        for (int s = 0; s < STG_COUNT; s++)
            if (strstr(mq_strat_cc(s), required[i]))
                found = s;
        if (found < 0)
            fprintf(stderr, "no way for \"%s\"\n", required[i]);
        CHECK(found >= 0);
    }
    for (int s = 0; s < STG_COUNT; s++) {
        int32_t x[6] = {0};
        fam[mq_strat_family(s)]++;
        /* a model: its plan for the example ... */
        mq_strat_start(q, s, SM_SHOW, NULL);
        CHECK(mq_strat_on(q) && q->strat.n >= 1);
        /* ... a teaching segment: opening lines, steps the friend shows, then done */
        CHECK(mq_strat_teach_n(s) >= 1 && q->strat.phase == SP_TEACH);
        int beats = 0;
        while (mq_strat_on(q) && q->strat.phase == SP_TEACH && beats++ < 40)
            con("strat next");
        CHECK(q->strat.phase == SP_DONE && q->strat.cur == q->strat.n);
        draw();
        char why[160];
        int bad = mq_strat_check(q, why, sizeof why);
        if (bad)
            fprintf(stderr, "example of %s: %s\n", mq_strat_cc(s), why);
        CHECK(!bad);
        mq_strat_close(q);
        /* ... and an order type: a "use this way" order, its answer what the model ends with */
        int32_t e[6];
        for (int k = 0; k < 20; k++) {
            mq_prob_t o = q->prob;
            mq_strat_numbers(q, s, k % 3, e);
            CHECK(mq_strat_make(q, &o, s, e));
            CHECK(o.way == s + 1 && o.skill == mq_strat_skill(s) && o.text[0][0]);
            CHECK(mq_correct(&o, o.ans));
            for (int w = 0; w < o.nwrong; w++)
                CHECK(o.wrong[w] != o.ans);
            CHECK(mq_strat_plan(q, s, e));
            const mq_sstep_t *last = &q->strat.st[q->strat.n - 1];
            if (s != STG_ALGO_AS && s != STG_ALGO_M && s != STG_TURN && s != STG_BENCH)
                CHECK(last->g == GE_PICK && last->want == (o.kind == K_DEC ? o.ans : o.ans));
            for (int k2 = 0; k2 < q->strat.n; k2++) /* (the friend's words fit their bubble) */
                CHECK(strlen(q->strat.st[k2].say) > 3 && strlen(q->strat.st[k2].say) < 71);
            for (int ln = 0; ln < 3; ln++)
                CHECK(strlen(o.text[ln]) < MQ_TXT - 1);
        }
        (void)x;
    }
    for (int f = 0; f < SF_COUNT; f++)
        CHECK(fam[f] >= 2);
}

/* ------------------------------------------------------------------ the models match their numbers */
static void test_models(void)
{
    reinit();
    in_play();
    char why[160];
    for (int s = 0; s < STG_COUNT; s++) {
        mq_more(q)->strat_seen[s] = 1; /* (straight to her own) */
        for (int k = 0; k < 25; k++) {
            int32_t e[6];
            mq_strat_numbers(q, s, k % 3, e);
            if (s == STG_ALGO_AS || s == STG_ALGO_M) { /* (her own goes to the notebook: the friend's columns here) */
                mq_strat_start(q, s, SM_SHOW, e);
                while (mq_strat_on(q) && q->strat.phase == SP_TEACH)
                    con("strat next");
            } else {
                mq_strat_start(q, s, SM_BULB, e);
                CHECK(q->strat.phase == SP_DO);
                con("strat auto");
                CHECK(q->strat.phase == SP_DONE);
            }
            draw();
            int bad = mq_strat_check(q, why, sizeof why);
            if (bad)
                fprintf(stderr, "%s (%ld %ld %ld): %s\n", mq_strat_cc(s), (long)e[0], (long)e[1], (long)e[2], why);
            CHECK(!bad);
            mq_strat_close(q);
        }
    }
    /* a wrong move: refused, its why said, the step stays */
    mq_strat_start(q, STG_JUMPS, SM_BULB, (int32_t[6]){38, 25, 0, 0, 0, 0});
    CHECK(q->strat.cur == 0 && q->strat.st[0].g == GE_JUMP);
    int w0 = q->strat.wrongs;
    CHECK(!mq_strat_act(q, 100)); /* a hop past the spot */
    CHECK(q->strat.wrongs == w0 + 1 && q->strat.cur == 0 && q->strat.pos == 38);
    CHECK(mq_strat_act(q, 10) && mq_strat_act(q, 10) && q->strat.cur == 1 && q->strat.pos == 58);
    mq_strat_close(q);
    mq_strat_start(q, STG_EXPAND, SM_BULB, (int32_t[6]){352, 1, 0, 0, 0, 0});
    CHECK(!mq_strat_act(q, 0 * 16 + 0)); /* the 3 into the ones house */
    CHECK(q->strat.put[0] == 0 && q->strat.wrongs == 1);
    mq_strat_close(q);
    /* the house of places tells the number to build, in its parts (the user: it didn't show it) */
    mq_strat_start(q, STG_PVCHART, SM_BULB, (int32_t[6]){306, 0, 0, 0, 0, 0});
    CHECK(q->strat.st[0].g == GE_DRAG && strstr(q->strat.st[0].say, "3 hundreds, 0 tens, 6 ones"));
    mq_strat_close(q);
    mq_strat_start(q, STG_PVCHART, SM_BULB, (int32_t[6]){247, 1, 0, 0, 0, 0});
    CHECK(strstr(q->strat.st[0].say, "2 ones, 4 tenths, 7 hundredths"));
    mq_strat_close(q);
}

/* the turn-around trick: both rectangles, the same count, before and after the turn */
static void test_turn(void)
{
    reinit();
    in_play();
    mq_more(q)->strat_seen[STG_TURN] = 1;
    int32_t pairs[4][2] = {{2, 4}, {3, 5}, {4, 6}, {5, 2}};
    for (int i = 0; i < 4; i++) {
        int32_t e[6] = {pairs[i][0], pairs[i][1], 1, 0, 0, 0};
        mq_strat_start(q, STG_TURN, SM_BULB, e);
        draw();
        CHECK(q->strat.units[0] == pairs[i][0] * pairs[i][1] && q->strat.units[1] == q->strat.units[0]);
        bool rows1 = false, rows2 = false, ex1 = false, ex2 = false;
        char r1[32], r2[32], x1[32], x2[32];
        snprintf(r1, sizeof r1, "%ld rows of %ld", (long)pairs[i][0], (long)pairs[i][1]);
        snprintf(r2, sizeof r2, "%ld rows of %ld", (long)pairs[i][1], (long)pairs[i][0]);
        snprintf(x1, sizeof x1, "%ld \xC3\x97 %ld", (long)pairs[i][0], (long)pairs[i][1]);
        snprintf(x2, sizeof x2, "%ld \xC3\x97 %ld", (long)pairs[i][1], (long)pairs[i][0]);
        for (int k = 0; k < nshown; k++)
            rows1 |= !strcmp(shown[k], r1), rows2 |= !strcmp(shown[k], r2), ex1 |= !strcmp(shown[k], x1), ex2 |= !strcmp(shown[k], x2);
        CHECK(rows1 && rows2 && ex1 && ex2);
        CHECK(mq_strat_act(q, 45) && q->strat.cur == 0); /* half way round: not yet */
        draw();
        CHECK(q->strat.units[0] == q->strat.units[1]);
        CHECK(mq_strat_act(q, 90) && q->strat.cur == 1); /* turned: it covers the other */
        frames(20);
        draw();
        CHECK(q->strat.units[0] == pairs[i][0] * pairs[i][1] && q->strat.units[1] == q->strat.units[0]);
        CHECK(mq_strat_act(q, 0) && mq_strat_act(q, pairs[i][0]));
        CHECK(q->strat.phase == SP_DONE);
        mq_strat_close(q);
    }
    /* the friend shows it turning smoothly: the turn takes several frames */
    mq_more(q)->strat_seen[STG_TURN] = 0;
    mq_strat_start(q, STG_TURN, SM_SHOW, NULL);
    con("strat next"), con("strat next"); /* (the opening lines) */
    frames(30);
    CHECK(q->strat.cur == 1);
    mq_strat_close(q);
}

/* ------------------------------------------------------------------ school's pacing */
static void test_pacing(void)
{
    reinit();
    con("school 3 10");
    CHECK(mq_strat_open(q, STG_JUMPS) && mq_strat_open(q, STG_GROUPS) && mq_strat_open(q, STG_TURN) && mq_strat_open(q, STG_SPLIT));
    CHECK(mq_strat_open(q, STG_TIMELINE) && mq_strat_open(q, STG_MAKE10) && mq_strat_open(q, STG_THINKX));
    CHECK(!mq_strat_open(q, STG_PSUMS) && !mq_strat_open(q, STG_STRIP) && !mq_strat_open(q, STG_FLINE) && !mq_strat_open(q, STG_PLOT));
    CHECK(!mq_strat_open(q, STG_AREA) && !mq_strat_open(q, STG_PEEL) && !mq_strat_open(q, STG_ALGO_AS) && !mq_strat_open(q, STG_ALGO_M));
    con("school 3 22");
    CHECK(mq_strat_open(q, STG_STRIP) && mq_strat_open(q, STG_PIE) && !mq_strat_open(q, STG_FLINE) && !mq_strat_open(q, STG_UNITS));
    con("school 3 40");
    CHECK(!mq_strat_open(q, STG_ALGO_AS) && !mq_strat_open(q, STG_PEEL));
    con("school 4 3");
    CHECK(!mq_strat_open(q, STG_ALGO_AS) && mq_strat_open(q, STG_PLOT));
    con("school 4 4");
    CHECK(mq_strat_open(q, STG_ALGO_AS) && !mq_strat_open(q, STG_ALGO_M) && !mq_strat_open(q, STG_AREA));
    con("school 4 40");
    CHECK(mq_strat_open(q, STG_PEEL) && mq_strat_open(q, STG_MONEY) && !mq_strat_open(q, STG_ALGO_M));
    con("school 5 5");
    CHECK(!mq_strat_open(q, STG_ALGO_M));
    con("school 5 6");
    CHECK(mq_strat_open(q, STG_ALGO_M));
    for (int s = 0; s < STG_COUNT; s++) /* (every week its grade says) */
        CHECK(mq_strat_open(q, s));
    /* school not said: her starting grade, all its year */
    reinit();
    CHECK(mq_strat_open(q, STG_PLOT) && mq_strat_open(q, STG_STRIP) && !mq_strat_open(q, STG_AREA) && !mq_strat_open(q, STG_ALGO_AS));
}

/* ------------------------------------------------------------------ gestures */
static int wg_find(int id, int arg)
{
    for (int i = 0; i < q->wg.n; i++)
        if (q->wg.w[i].id == id && q->wg.w[i].arg == arg)
            return i;
    return -1;
}
static void test_gestures(void)
{
    reinit();
    in_play();
    mq_more(q)->strat_seen[STG_JUMPS] = 0;
    mq_strat_start(q, STG_JUMPS, SM_BULB, (int32_t[6]){38, 25, 0, 0, 0, 0});
    CHECK(q->strat.phase == SP_TEACH); /* new to her: the friend shows it first */
    draw();
    int t = wg_find(SW_TRY, 0);
    CHECK(t >= 0);
    if (t >= 0)
        touch(4, q->wg.w[t].x + 10, q->wg.w[t].y + 10); /* "try one" */
    CHECK(q->strat.phase == SP_DO && q->strat.x[0] == 38);
    /* drag a hop of ten onto the path */
    draw();
    int c = wg_find(SW_CHIP, 0), sl = wg_find(SW_SLOT, 0);
    CHECK(c >= 0 && sl >= 0);
    if (c >= 0 && sl >= 0) {
        const mq_wg_t *a = &q->wg.w[c], *b = &q->wg.w[sl];
        touch(1, a->x + a->w / 2, a->y + a->h / 2);
        touch(2, a->x + a->w / 2, a->y - 20);
        touch(2, b->x + b->w / 2, b->y + b->h / 2);
        touch(3, b->x + b->w / 2, b->y + b->h / 2);
        CHECK(q->strat.pos == 48);
    }
    /* keys: ENTER picks the chip up, an arrow chooses the path, ENTER drops it */
    draw();
    q->wg.focus = (int8_t)wg_find(SW_CHIP, 0);
    key(49), key(14), key(49);
    CHECK(q->strat.pos == 58);
    con("strat act 1"), con("strat act 1"), con("strat act 1"), con("strat act 1"), con("strat act 1");
    CHECK(q->strat.cur == 2 && q->strat.st[2].g == GE_PICK);
    /* a pick by its digit key */
    int right = 0;
    for (int i = 0; i < 3; i++)
        if (q->strat.st[2].ch[i] == 63)
            right = i;
    mq_strat_key(q, 0, '1' + right);
    CHECK(q->strat.phase == SP_DONE);
    /* the answer goes into the game */
    for (int k = 0; k < 600 && mq_strat_on(q); k++) /* (the done line: its reading time) */
        frames(1);
    CHECK(!mq_strat_on(q) && q->sub == GS_CHEER);
    /* a slider dragged: the garden's rows */
    con("skill 16 0");
    mq_more(q)->strat_seen[STG_ARRAY] = 1;
    mq_strat_start(q, STG_ARRAY, SM_BULB, (int32_t[6]){3, 7, 0, 0, 0, 0});
    draw();
    int sd = wg_find(SW_SLIDE, 0);
    CHECK(sd >= 0);
    if (sd >= 0) {
        const mq_wg_t *g = &q->wg.w[sd];
        int x = g->x + g->w / 2, y3 = g->y + 3 * g->h / 10;
        touch(1, x, g->y + 2);
        touch(2, x, y3);
        CHECK(q->strat.val == 3 && q->strat.cur == 0); /* (still held) */
        touch(3, x, y3);
        CHECK(q->strat.cur == 1);
    }
    mq_strat_close(q);
    /* a tap: a bag filled */
    mq_more(q)->strat_seen[STG_GROUPS] = 1;
    mq_strat_start(q, STG_GROUPS, SM_BULB, (int32_t[6]){4, 6, 0, 0, 0, 0});
    draw();
    touch(4, 160, MQ_Y0 + 60 + 40);
    CHECK(q->strat.cnt == 1);
    mq_strat_close(q);
}

/* ------------------------------------------------------------------ the helper and the lightbulb */
static void test_fit(void)
{
    reinit();
    in_play();
    con("school 4 40");
    int32_t x[6];
    mq_prob_t p = q->prob;
    p.way = 0, p.skill = S_ADDSUB, p.ws = WS_ADD, p.op[0] = 299, p.op[1] = 45, p.kind = K_NUM;
    CHECK(mq_strat_fit(q, &p, 0, x) == STG_COMP && x[0] == 299 && x[1] == 45);
    CHECK(mq_strat_fit(q, &p, 1, x) == STG_JUMPS);
    p.op[0] = 47, p.op[1] = 38;
    CHECK(mq_strat_fit(q, &p, 0, x) == STG_JUMPS && mq_strat_fit(q, &p, 1, x) == STG_PSUMS);
    p.ws = WS_SUB, p.op[0] = 62, p.op[1] = 37;
    CHECK(mq_strat_fit(q, &p, 0, x) == STG_COUNTUP);
    p.ws = WS_MUL, p.op[0] = 23, p.op[1] = 4, p.skill = S_MULTI;
    CHECK(mq_strat_fit(q, &p, 0, x) == STG_AREA && mq_strat_fit(q, &p, 1, x) == STG_PPROD);
    p.ws = WS_DIV, p.op[0] = 96, p.op[1] = 4, p.skill = S_DIVIDE;
    CHECK(mq_strat_fit(q, &p, 0, x) == STG_PEEL && mq_strat_fit(q, &p, 1, x) == STG_AREADIV);
    p.ws = WS_NONE, p.skill = S_FACTS, p.a[0] = (int16_t)mq_fact_index(7, 6, false);
    int f0 = mq_strat_fit(q, &p, 0, x);
    CHECK(f0 == STG_KNOWN || f0 == STG_SPLIT);
    /* at grade 3, week 10: the number line, never the columns */
    con("school 3 10");
    p.ws = WS_ADD, p.op[0] = 47, p.op[1] = 38, p.skill = S_ADDSUB;
    CHECK(mq_strat_fit(q, &p, 0, x) == STG_JUMPS && mq_strat_fit(q, &p, 1, x) == -1);
    /* the helper friend shows the way with the similar order, then a second way, then her own */
    con("school 4 40");
    for (int t = 0; t < 40; t++) {
        con("skill 16 3");
        if (q->prob.ws == WS_ADD && q->prob.op[1] >= 10 && q->prob.op[0] + q->prob.op[1] < 1000)
            break;
    }
    CHECK(q->prob.ws == WS_ADD);
    con("wrong");
    CHECK(q->sub == GS_HELP);
    CHECK(mq_strat_on(q) && q->strat.mode == SM_HELP);
    int st = q->strat.id;
    draw();
    for (int i = 0; i < 60 && q->sub == GS_HELP; i++)
        frames(20);
    CHECK(q->sub == GS_PLAY && !mq_strat_on(q));
    CHECK(mq_more(q)->strat_help[st] >= 1);
    /* the lightbulb: its trick button opens her own order's best way */
    con("bulb");
    int bx, by, bw, bh;
    CHECK(mq_strat_bulb_btn(q, &bx, &by, &bw, &bh));
    draw();
    touch(4, bx + bw / 2, by + bh / 2);
    CHECK(mq_strat_on(q) && q->strat.mode == SM_BULB);
    mq_strat_close(q);
}

/* ------------------------------------------------------------------ orders from the engine */
static void test_orders(void)
{
    reinit();
    con("school 3 10");
    con("fun off");
    int ways = 0, intro = 0, worked = 0;
    kid_on = true;
    for (int r = 0; r < 300; r++) {
        if (q->scene != SC_PLAY)
            con("level 1 3");
        if (q->scene != SC_PLAY)
            continue;
        if (q->prob.way) {
            int s = q->prob.way - 1;
            ways++;
            CHECK(mq_strat_open(q, s));
            if (mq_strat_on(q) && q->strat.phase == SP_TEACH)
                intro++;
            draw();
            if (mq_strat_on(q)) {
                con("strat try");
                draw();
                con("strat auto");
                for (int k = 0; k < 600 && mq_strat_on(q); k++) /* (the done line: its reading time) */
                    frames(1);
                CHECK(!mq_strat_on(q));
                worked += q->sub == GS_CHEER;
            }
        }
        draw();
        if (q->scene == SC_PLAY && q->sub == GS_PLAY)
            con("correct");
        con("next");
        frames(4);
    }
    kid_on = false;
    printf("  ways in 300 orders: %d (%d introduced), worked in the model %d\n", ways, intro, worked);
    CHECK(ways >= 3 && intro >= 1 && worked >= 1);
    for (int s = 0; s < STG_COUNT; s++) /* (nothing school has not reached) */
        if (mq_more(q)->strat_seen[s])
            CHECK(mq_strat_open(q, s));
}

/* ------------------------------------------------------------------ kid words, the grown-ups */
static void test_words(void)
{
    reinit();
    in_play();
    kid_on = true;
    for (int s = 0; s < STG_COUNT; s++) {
        mq_strat_start(q, s, SM_SHOW, NULL);
        for (int b = 0; b < 30 && mq_strat_on(q) && q->strat.phase == SP_TEACH; b++) {
            draw();
            con("strat next");
        }
        mq_strat_close(q);
        mq_more(q)->strat_seen[s] = 1;
        int32_t e[6];
        mq_strat_numbers(q, s, 1, e);
        mq_prob_t o = q->prob;
        mq_strat_make(q, &o, s, e);
        for (int l = 0; l < 3; l++)
            kid(o.text[l]);
        for (int c = 0; c < o.nchoice; c++)
            kid(o.choice[c]);
        if (s != STG_ALGO_AS && s != STG_ALGO_M) {
            mq_strat_start(q, s, SM_BULB, e);
            for (int k = 0; k < 16 && q->strat.phase == SP_DO; k++) {
                draw();
                con("strat act -12345"); /* (a wrong move: its why) */
                draw();
                int c0 = q->strat.cur;
                (void)c0;
                con("strat auto");
            }
            mq_strat_close(q);
        }
    }
    kid_on = false;
    CHECK(kid_bad == 0);
    CHECK(said_n > 100);
    /* the grown-ups: the Common Core names */
    mq_more(q)->strat_used[STG_JUMPS] = 3;
    con("go parent");
    q->parent_pat = 2;
    q->tab = 0;
    draw();

    bool cc = false, nm = false;
    for (int k = 0; k < nshown; k++)
        cc |= strstr(shown[k], "Open number line") != NULL, nm |= strstr(shown[k], "jumping trick") != NULL;
    CHECK(cc && nm);
}

/* ------------------------------------------------------------------ lessons for the help paths */
static void test_lessons(void)
{
    reinit();
    in_play();
    con("school 4 40");
    int skills = 0;
    for (int sk = 0; sk < S_COUNT; sk++) {
        int has = -1;
        for (int s = 0; s < STG_COUNT; s++)
            if (mq_strat_skill(s) == sk && s != STG_ALGO_AS && s != STG_ALGO_M)
                has = s;
        if (has < 0)
            continue;
        skills++;
        for (int t = 0; t < 6; t++) {
            char b[32];
            snprintf(b, sizeof b, "skill %d %d", sk, t % 3);
            con(b);
            if (q->scene != SC_PLAY)
                continue;
            mq_prob_t before = q->prob;
            int32_t ex[6], ox[6];
            int s = mq_lesson_for(q, sk, t % 3, &q->prob, ex);
            CHECK(s >= 0 && mq_strat_open(q, s));
            int f = mq_strat_fit(q, &q->prob, 0, ox);
            if (f == s) /* a similar example: other numbers */
                CHECK(ex[0] != ox[0] || ex[1] != ox[1] || ex[2] != ox[2]);
            CHECK(mq_lesson_start(q, sk, t % 3, &q->prob));
            CHECK(mq_strat_on(q) && q->strat.mode == SM_LESSON && q->strat.phase == SP_TEACH);
            for (int k = 0; k < 40 && mq_strat_on(q); k++)
                frames(20);
            CHECK(!mq_strat_on(q) && q->sub == GS_PLAY && q->prob.ans == before.ans); /* her own order, still hers */
        }
    }
    CHECK(skills >= 15);
}

/* ------------------------------------------------------------------ the error patterns' pictures */
static void test_diag(void)
{
    reinit();
    con("school 4 40");
    int nshow = 0;
    for (int t = 0; t < 30; t++) {
        con("skill 22 0"); /* (multiplying: 2-4 digits by one) */
        if (q->scene != SC_PLAY || q->prob.ws != WS_MUL || q->prob.op[0] > 99)
            continue;
        int32_t lost = q->prob.op[0] % 10 * q->prob.op[1]; /* only the ones' piece */
        CHECK(mq_diag_detect(&q->prob, lost, PT_LOST_PARTIAL, NULL) == 1);
        CHECK(mq_diag_detect(&q->prob, q->prob.ans, PT_LOST_PARTIAL, NULL) == 0);
        draw();
        CHECK(mq_strat_diag_picture(q, PT_LOST_PARTIAL));
        CHECK(q->strat.id == STG_AREA && q->strat.units[0] == q->prob.ans && !mq_strat_on(q));
        nshow++;
    }
    CHECK(nshow >= 3);
    CHECK(!mq_strat_diag_picture(q, PT_XY_SWAP));
    con("skill 0 1"); /* equal groups: adding for times, in bags */
    if (q->prob.skill == S_GROUPS) {
        CHECK(mq_diag_detect(&q->prob, q->prob.v[0] + q->prob.v[1], PT_ADD_FOR_TIMES, NULL) == 1);
        CHECK(mq_strat_diag_picture(q, PT_ADD_FOR_TIMES) && q->strat.units[0] == q->prob.ans);
    }
}

/* ------------------------------------------------------------------ the screen names the numbers she works with
 * (code review 2, CR2-01: the house of places never said the number to build). For every way and its orders:
 * once she is at her own order (SP_DO), the numbers the order gives her are on the screen as whole numbers -- in
 * the title line, the friend's words or the model's labels. Which of x[] are "givens" is the way's own
 * (THINKX gives 42 and 6, never the 7 it asks for; MONEY gives the coins' counts; a time line formats its
 * times and a line plot packs its lengths: not checked). */
static int givens(int s, const int32_t *x, int32_t out[3])
{
    int32_t a = x[0], b = x[1], c = x[2];
    switch (s) {
    case STG_EXPAND: out[0] = a; return 1;
    case STG_PVCHART: out[0] = a / 100, out[1] = a / 10 % 10, out[2] = a % 10; return 3; /* (in its parts: the number's
                                                                                          * name is the last question) */
    case STG_REGROUP: case STG_FRIENDLY: case STG_EQUIV: case STG_BLOCKS: out[0] = a, out[1] = b, out[2] = c; return 3;
    case STG_SHARE: case STG_BAGS: case STG_THINKX: out[0] = a * b, out[1] = a; return 2;
    case STG_TAPE_EQ: out[0] = c ? a * b : b; return 1; /* (the pieces are drawn on the ribbon, to be counted) */
    case STG_MONEY: out[0] = a / 100, out[1] = a / 10 % 10, out[2] = a % 10; return 3;
    case STG_ALGO_AS: case STG_ALGO_M: case STG_TIMELINE: case STG_PLOT: return 0;
    default: out[0] = a, out[1] = b; return 2;
    }
}
/* v as a whole number somewhere in the texts drawn (not a part of a longer number or a decimal) */
static bool drawn_number(int32_t v)
{
    char d[16];
    snprintf(d, sizeof d, "%ld", (long)v);
    size_t n = strlen(d);
    for (int k = 0; k < nshown; k++)
        for (const char *p = strstr(shown[k], d); p; p = strstr(p + 1, d)) {
            bool l = p > shown[k] && ((p[-1] >= '0' && p[-1] <= '9') || p[-1] == '.');
            bool r = (p[n] >= '0' && p[n] <= '9') || (p[n] == '.' && p[n + 1] >= '0' && p[n + 1] <= '9');
            if (!l && !r)
                return true;
        }
    return false;
}
static void test_named(void)
{
    reinit();
    in_play();
    static const int known_gap[] = {-1}; /* (a way whose fix is pending: a warning, not a failure; CR2-01 fixed in
                                          * 6857b39: the house of places names the number in its parts) */
    int bad = 0, warned = 0, checked = 0;
    for (int s = 0; s < STG_COUNT; s++) {
        if (s == STG_ALGO_AS || s == STG_ALGO_M)
            continue;
        for (int k = 0; k < 6; k++) {
            int32_t e[6], g[3];
            mq_strat_numbers(q, s, k % 3, e);
            int ng = givens(s, e, g);
            if (!ng)
                continue;
            mq_strat_start(q, s, SM_BULB, e);
            con("strat try");
            if (!mq_strat_on(q) || q->strat.phase != SP_DO) {
                mq_strat_close(q);
                continue;
            }
            draw();
            checked++;
            bool gap = false;
            for (int i = 0; known_gap[i] >= 0; i++)
                gap |= known_gap[i] == s;
            for (int i = 0; i < ng; i++)
                if (!drawn_number(g[i])) {
                    if (gap)
                        warned++;
                    else
                        bad++;
                    if ((gap ? warned : bad) <= 3)
                        fprintf(stderr, "%s: %s does not show %ld (x = %ld %ld %ld)\n", gap ? "known gap" : "BAD",
                                mq_strat_cc(s), (long)g[i], (long)e[0], (long)e[1], (long)e[2]);
                }
            mq_strat_close(q);
        }
    }
    CHECK(checked >= 30 * 6);
    CHECK(bad == 0);
    CHECK(warned == 0);
    printf("named: %d screens, %d known gaps\n", checked, warned);
}

int main(void)
{
    harness_init();
    tapi = modx_api;
    tapi.speak = t_speak;
    tapi.text = t_text;
    test_coverage();
    test_models();
    test_turn();
    test_pacing();
    test_gestures();
    test_fit();
    test_orders();
    test_words();
    test_lessons();
    test_diag();
    test_named();
    printf("test_mq_strat: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
