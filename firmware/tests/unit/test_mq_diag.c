/* Sparkle Town (modules/mathquest) error patterns (mq_diag.c): the library's detectors on worked examples,
 * synthetic players who follow a pattern (it is diagnosed, quickly, and nothing else is), random slippers
 * by the thousand (nothing is diagnosed), the friend's moment, the aimed orders and the re-checks until it
 * is fixed, the grown-ups' list and the play log. */
#include "mq_test_util.h"

static mq_prob_t col_prob(int ws, int32_t a, int32_t b)
{
    mq_prob_t p;
    memset(&p, 0, sizeof p);
    p.skill = S_ADDSUB, p.kind = K_NUM, p.ws = (uint8_t)ws, p.op[0] = a, p.op[1] = b;
    p.ans = ws == WS_ADD ? a + b : a - b;
    return p;
}

/* ------------------------------------------------------------------ the detectors */
static void test_detect(void)
{
    int col;
    mq_prob_t p = col_prob(WS_ADD, 47, 38); /* 85 */
    CHECK(mq_diag_detect(&p, 75, PT_CARRY_TENS, &col) == 1 && col == 1);
    CHECK(mq_diag_detect(&p, 85, PT_CARRY_TENS, NULL) == 0);
    CHECK(mq_diag_detect(&p, 84, PT_CARRY_TENS, NULL) == 0);
    CHECK(mq_diag_detect(&p, 75, PT_CARRY_HUNDREDS, NULL) == -1); /* (no carry into the hundreds: no chance) */
    p = col_prob(WS_ADD, 276, 238);                                /* 514: carries into the tens and the hundreds */
    CHECK(mq_diag_detect(&p, 404, PT_CARRY_TENS, NULL) == 1 && mq_diag_detect(&p, 404, PT_CARRY_HUNDREDS, NULL) == 1);
    CHECK(mq_diag_detect(&p, 414, PT_CARRY_HUNDREDS, &col) == 1 && col == 2); /* only the hundreds' carry lost */
    CHECK(mq_diag_detect(&p, 414, PT_CARRY_TENS, NULL) == 0);
    CHECK(mq_diag_detect(&p, 504, PT_CARRY_TENS, NULL) == 1 && mq_diag_detect(&p, 504, PT_CARRY_HUNDREDS, NULL) == 0);
    p = col_prob(WS_ADD, 5678, 4567);                              /* carries everywhere, into the thousands too */
    CHECK(mq_diag_detect(&p, p.ans - 1000, PT_CARRY_THOUSANDS, &col) == 1 && col == 3);
    p = col_prob(WS_SUB, 402, 157); /* 245; the small digit from the big one: 355 */
    CHECK(mq_diag_detect(&p, 355, PT_SMALL_FROM_BIG, NULL) == 1 && mq_diag_detect(&p, 245, PT_SMALL_FROM_BIG, NULL) == 0);
    CHECK(mq_diag_detect(&p, 355, PT_CARRY_TENS, NULL) == -1);
    p = col_prob(WS_SUB, 603, 278); /* 325; across the zero, the 0 made 10, not 9: 335 */
    CHECK(mq_diag_detect(&p, 335, PT_ZERO_TRADE, &col) == 1 && col == 1);
    CHECK(mq_diag_detect(&p, 335, PT_SMALL_FROM_BIG, NULL) == 0);
    p = col_prob(WS_SUB, 645, 213); /* nothing to trade: no chance for either */
    CHECK(mq_diag_detect(&p, 422, PT_SMALL_FROM_BIG, NULL) == -1 && mq_diag_detect(&p, 422, PT_ZERO_TRADE, NULL) == -1);
    /* place value: a zero lost or added, x 10 */
    memset(&p, 0, sizeof p);
    p.skill = S_PV3, p.kind = K_NUM, p.ans = 3045;
    CHECK(mq_diag_detect(&p, 345, PT_PLACE_SLIP, NULL) == 1 && mq_diag_detect(&p, 30450, PT_PLACE_SLIP, NULL) == 1);
    CHECK(mq_diag_detect(&p, 30045, PT_PLACE_SLIP, NULL) == 1 && mq_diag_detect(&p, 3046, PT_PLACE_SLIP, NULL) == 0);
    /* rounding (the user: "teach rounding, not just picking the closer side"): 47 to the nearest 10 -- 40 is the
     * farther ten; 45 -- 40 is a 5 rounded down; 450 to the nearest 100 -- 400 too; a round number shows neither */
    memset(&p, 0, sizeof p);
    p.skill = S_ROUND10, p.kind = K_NUM, p.a[0] = 47, p.a[1] = 10, p.ans = 50;
    CHECK(mq_diag_detect(&p, 40, PT_ROUND_FAR, NULL) == 1 && mq_diag_detect(&p, 50, PT_ROUND_FAR, NULL) == 0);
    CHECK(mq_diag_detect(&p, 40, PT_ROUND_HALF_DOWN, NULL) == -1);
    p.a[0] = 42, p.ans = 40;
    CHECK(mq_diag_detect(&p, 50, PT_ROUND_FAR, NULL) == 1 && mq_diag_detect(&p, 40, PT_ROUND_FAR, NULL) == 0);
    p.a[0] = 45, p.ans = 50;
    CHECK(mq_diag_detect(&p, 40, PT_ROUND_HALF_DOWN, NULL) == 1 && mq_diag_detect(&p, 40, PT_ROUND_FAR, NULL) == -1);
    p.a[0] = 450, p.a[1] = 100, p.ans = 500;
    CHECK(mq_diag_detect(&p, 400, PT_ROUND_HALF_DOWN, NULL) == 1);
    p.a[0] = 400;
    CHECK(mq_diag_detect(&p, 400, PT_ROUND_FAR, NULL) == -1 && mq_diag_detect(&p, 400, PT_ROUND_HALF_DOWN, NULL) == -1);
    memset(&p, 0, sizeof p);
    p.skill = S_ROUNDBIG, p.kind = K_NUM, p.big = 3750, p.v[0] = 2, p.ans = 3800;
    CHECK(mq_diag_detect(&p, 3700, PT_ROUND_HALF_DOWN, NULL) == 1);
    CHECK(mq_diag_rec(q, PT_ROUND_HALF_DOWN) == &mq_more(q)->diag2[PT_ROUND_HALF_DOWN - MQ_NPATTERN]); /* (appended) */
    /* the rest: the generator's own wrong answers, exactly (fractions: an equivalent one too), never its fallback */
    int found[PT_COUNT] = {0};
    for (int sk = 0; sk < MQ_NSKILL; sk++)
        for (int k = 0; k < 6; k++) {
            mq_generate(q, &p, sk, k % 4);
            for (int pt = PT_ADD_DENOMS; pt < PT_COUNT; pt++) {
                int32_t v = mq_diag_example(&p, pt);
                if (v == 0x7FFFFFFF)
                    continue;
                found[pt]++;
                CHECK(mq_diag_detect(&p, v, pt, NULL) == 1);
                CHECK(!mq_correct(&p, v));
                CHECK(mq_diag_detect(&p, p.ans, pt, NULL) == 0);
                if (p.kind == K_NUM || p.kind == K_MONEY) { /* a slip far off is not the pattern (no fallback guess) */
                    int32_t far = p.ans * 3 + 7777;
                    CHECK(mq_diag_detect(&p, far, pt, NULL) == 0);
                }
            }
        }
    for (int pt = PT_ADD_DENOMS; pt < PT_COUNT; pt++) {
        if (!found[pt])
            fprintf(stderr, "no order shows pattern %s\n", mq_pattern_name(pt));
        CHECK(found[pt] > 0); /* every pattern of the library has orders that can show it */
    }
    { /* the coins: a nickel taken for more than a dime is one of them */
        bool seen = false;
        for (int t = 0; t < 60 && !seen; t++) {
            mq_generate(q, &p, S_COINS, 1);
            int ni = p.v[2], di = p.v[1];
            if (ni && di && ni != di) {
                int32_t v = p.v[4] * 100 + p.v[0] * 25 + di * 5 + ni * 10 + p.v[3];
                seen = mq_diag_detect(&p, v, PT_COIN_VALUE, NULL) == 1;
            }
        }
        CHECK(seen);
    }
}

/* ------------------------------------------------------------------ synthetic players */
static uint32_t rs = 1;
static int rnd(int n)
{
    rs = rs * 1103515245u + 12345u;
    return (int)((rs >> 16) % (uint32_t)n);
}
static void fresh_player(void)
{
    fresh_module();
    con("new Ada 4");
    mq_prof(q)->problems = 100;
}
static int spotted_list(int *which)
{
    int n = 0;
    for (int pt = 0; pt < PT_COUNT; pt++)
        if (mq_diag_rec(q, pt)->state != PS_NONE) {
            if (which)
                which[n] = pt;
            n++;
        }
    return n;
}
/* the wrong options of a pick game: up to three of the generator's wrong answers, in its order */
static int options(const mq_prob_t *p, int32_t *opt)
{
    int n = 0;
    opt[n++] = p->ans;
    for (int i = 0; i < p->nwrong && n < 4; i++)
        opt[n++] = p->wrong[i];
    return n;
}

/* a player who follows pattern pt on its chances (follow % of the time) in skill sk (rung diff), and is
 * right 90 % elsewhere (a random typed slip otherwise); picks: answers are picked from options. Returns
 * the chances it had before it was diagnosed (-1 never); *others: other patterns diagnosed */
static int follower(int pt, int sk, int diff, int follow, bool picks, int *others)
{
    fresh_player();
    int orders = 0;
    rs = 77u + (uint32_t)pt * 31u + (uint32_t)picks;
    for (int t = 0; t < 400; t++) {
        mq_prob_t p;
        mq_generate(q, &p, sk, diff);
        int32_t opt[4];
        int nopt = picks ? options(&p, opt) : 0;
        int32_t v = mq_diag_example(&p, pt), g = p.ans;
        bool chance = v != 0x7FFFFFFF;
        if (chance && picks) { /* (the pattern's answer is one of the options in a pick game) */
            bool in = false;
            for (int i = 0; i < nopt; i++)
                in |= opt[i] == v;
            if (!in && nopt < 4)
                opt[nopt++] = v;
            else if (!in)
                opt[3] = v;
        }
        if (chance && rnd(100) < follow)
            g = v;
        else if (rnd(100) >= 90)
            g = picks && nopt > 1 ? opt[1 + rnd(nopt - 1)] : p.ans + 1 + rnd(40);
        mq_diag_feed(q, &p, g, mq_correct(&p, g), true, opt, nopt);
        orders += chance;
        mq_prof(q)->problems++;
        if (mq_diag_rec(q, pt)->state == PS_SPOTTED)
            break;
    }
    *others = 0;
    for (int k = 0; k < PT_COUNT; k++)
        *others += k != pt && mq_diag_rec(q, k)->state != PS_NONE;
    return mq_diag_rec(q, pt)->state == PS_SPOTTED ? orders : -1;
}

static void test_followers(void)
{
    static const struct {
        int pt, sk, diff;
    } who[] = {
        {PT_CARRY_TENS, S_ADDSUB, 4},     {PT_CARRY_HUNDREDS, S_ADDSUB, 5}, {PT_CARRY_HUNDREDS, S_ADDSUBBIG, 2},
        {PT_SMALL_FROM_BIG, S_ADDSUB, 5}, {PT_ZERO_TRADE, S_ADDSUB, 6},     {PT_ADD_DENOMS, S_ADDFRAC, 1},
        {PT_HANDS_SWAP, S_TIME5, 1},      {PT_COIN_VALUE, S_COINS, 2},      {PT_LEFTOVER, S_REMWORD, 1},
        {PT_AREA_PERIM, S_PERIM, 1},      {PT_XY_SWAP, S_COORD, 1},         {PT_GRAPH_KEY, S_SCALED, 1},
        {PT_ROUND_FAR, S_ROUND10, 0},     {PT_ROUND_HALF_DOWN, S_ROUND10, 3}, {PT_ROUND_FAR, S_ROUNDBIG, 1},
    };
    for (size_t i = 0; i < sizeof who / sizeof who[0]; i++)
        for (int picks = 0; picks < 2; picks++) {
            int others, n = follower(who[i].pt, who[i].sk, who[i].diff, 75, picks, &others);
            if (n < 0 || n > (picks ? 25 : 12) || others)
                fprintf(stderr, "pattern %s (%s, %s): diagnosed after %d of its chances, %d others\n",
                        mq_pattern_name(who[i].pt), mq_skill[who[i].sk].name, picks ? "picked" : "typed", n, others);
            /* found, in a handful of its chances (never on fewer than 3); picked from options that all show it, a
             * random pick looks the same, so it takes more of them */
            CHECK(n >= 3 && n <= (picks ? 25 : 12));
            CHECK(others == 0);      /* and nothing else */
        }
    /* a carry into the tens is fine, into the hundreds forgotten: the hundreds only */
    fresh_player();
    for (int t = 0; t < 200 && mq_diag_rec(q, PT_CARRY_HUNDREDS)->state == PS_NONE; t++) {
        mq_prob_t p;
        mq_generate(q, &p, S_ADDSUB, 5);
        int32_t v = mq_diag_example(&p, PT_CARRY_HUNDREDS);
        int32_t g = v != 0x7FFFFFFF && rnd(100) < 80 ? v : p.ans;
        mq_diag_feed(q, &p, g, g == p.ans, true, NULL, 0);
    }
    CHECK(mq_diag_rec(q, PT_CARRY_HUNDREDS)->state == PS_SPOTTED && mq_diag_rec(q, PT_CARRY_HUNDREDS)->col == 2);
    CHECK(mq_diag_rec(q, PT_CARRY_TENS)->state == PS_NONE);
}

/* random slippers: right most of the time, wrong at random -- typed (any number) or picked (any of the
 * wrong options, which are the generator's misconceptions) -- across every skill: nothing is diagnosed */
static void test_random_slips(void)
{
    int diagnosed = 0, orders = 0, shown = 0;
    for (int run = 0; run < 24; run++) {
        fresh_player();
        rs = 1000u + (uint32_t)run * 7919u;
        int acc = 70 + run % 4 * 8; /* 70 .. 94 % right */
        bool picks = run % 2;
        for (int t = 0; t < 600; t++) {
            mq_prob_t p;
            int sk = rnd(MQ_NSKILL);
            if (sk == S_FACTS)
                continue;
            mq_generate(q, &p, sk, rnd(4));
            int32_t opt[4];
            int nopt = picks ? options(&p, opt) : 0;
            int32_t g = p.ans;
            if (rnd(100) >= acc) {
                if (p.kind == K_CHOICE)
                    g = (p.ans + 1 + rnd(p.nchoice - 1)) % p.nchoice;
                else if (picks && nopt > 1)
                    g = opt[1 + rnd(nopt - 1)];
                else
                    g = p.ans + 1 + rnd(30);
            }
            mq_diag_feed(q, &p, g, mq_correct(&p, g), true, opt, nopt);
            orders++;
            mq_prof(q)->problems++;
        }
        int which[PT_COUNT], n = spotted_list(which);
        for (int k = 0; k < n; k++)
            fprintf(stderr, "random slipper %d (%d%% right, %s): %s diagnosed\n", run, acc, picks ? "picks" : "types",
                    mq_pattern_name(which[k]));
        diagnosed += n;
        for (int pt = 0; pt < PT_COUNT; pt++)
            shown += mq_diag_rec(q, pt)->hits;
    }
    printf("random slippers: %d orders, %d diagnoses, %d wrong answers that looked like one\n", orders, diagnosed, shown);
    CHECK(diagnosed == 0);
}

/* ------------------------------------------------------------------ the moment, the aimed orders, the re-checks */
static void test_lifecycle(void)
{
    fresh_player();
    con("engine stop"), con("fun off"), con("seed 5");
    mq_diag_rec_t *d = mq_diag_rec(q, PT_CARRY_TENS);
    /* in the game: her answers forget the tens' carry, again and again */
    int tries = 0;
    while (d->state == PS_NONE && tries++ < 40) {
        con("skill 16 4");
        q->fixed_sk1 = 0;
        if (mq_diag_example(&q->prob, PT_CARRY_TENS) == 0x7FFFFFFF)
            continue;
        q->now += 6000, q->last_input = q->now; /* (she thought about it: not a guess) */
        CHECK(con("diag slip 0") == 0);
        con("step 40");
    }
    CHECK(d->state == PS_SPOTTED || d->state == PS_PRACTICE);
    /* the friend shows her (the miss that showed it), or does at the next one */
    for (int t = 0; t < 20 && !mq_diag_on(q) && d->state == PS_SPOTTED; t++) {
        con("skill 16 4");
        q->now += 6000, q->last_input = q->now;
        if (mq_diag_example(&q->prob, PT_CARRY_TENS) != 0x7FFFFFFF)
            con("diag slip 0");

    }
    CHECK(mq_diag_on(q) && q->diag.pt == PT_CARRY_TENS && q->diag.nparts == 4);
    char *s = q->diag.say[0];
    CHECK(strstr(s, "Yours") != NULL); /* her answer next to the right one */
    int draws = 0;
    while (mq_diag_on(q) && draws++ < 8) {
        int r[4];
        ops.draw(q, 0, r);
        con("diag next");
    }
    CHECK(!mq_diag_on(q) && d->state == PS_PRACTICE && d->step == 3 && d->told);
    CHECK(q->scene == SC_PLAY && q->gm.hoh); /* and they finish the order together */
    /* the aimed orders: the engine asks the skill where it showed, an order that can show it */
    q->fixed_sk1 = 0, q->engine_stop = 0, q->diag_alt = 0;
    mq_prof(q)->problems += 5;
    int sk = mq_diag_pick(q);
    CHECK(sk == S_ADDSUB && q->diag_item == PT_CARRY_TENS + 1);
    CHECK(mq_diag_pick(q) < 0 && mq_diag_pick(q) < 0); /* (at most one aimed order in three) */
    for (int k = 0; k < 3; k++) { /* three aimed orders right: re-checks next */
        mq_prob_t p;
        do
            mq_generate(q, &p, S_ADDSUB, 4);
        while (mq_diag_detect(&p, p.ans, PT_CARRY_TENS, NULL) < 0);
        q->diag_item = PT_CARRY_TENS + 1;
        mq_diag_feed(q, &p, p.ans, true, true, NULL, 0);
        mq_prof(q)->problems++;
    }
    CHECK(d->state == PS_RECHECK && d->step == 0);
    q->diag_alt = 0;
    CHECK(mq_diag_pick(q) < 0); /* not due yet */
    for (int k = 0; k < 3; k++) {
        mq_prof(q)->problems += 10u << k;
        q->diag_alt = 0;
        CHECK(mq_diag_pick(q) == S_ADDSUB);
        mq_prob_t p;
        do
            mq_generate(q, &p, S_ADDSUB, 4);
        while (mq_diag_detect(&p, p.ans, PT_CARRY_TENS, NULL) < 0);
        mq_diag_feed(q, &p, p.ans, true, true, NULL, 0);
    }
    CHECK(d->state == PS_FIXED);
    /* showing it again later: spotted again, after fresh evidence */
    for (int k = 0; k < 30 && d->state == PS_FIXED; k++) {
        mq_prob_t p;
        do
            mq_generate(q, &p, S_ADDSUB, 4);
        while (mq_diag_detect(&p, p.ans, PT_CARRY_TENS, NULL) < 0);
        mq_diag_feed(q, &p, mq_diag_example(&p, PT_CARRY_TENS), false, true, NULL, 0);
    }
    CHECK(d->state == PS_SPOTTED);
    /* a re-check missed the same way: back to practice */
    d->state = PS_RECHECK, d->step = 1;
    {
        mq_prob_t p;
        do
            mq_generate(q, &p, S_ADDSUB, 4);
        while (mq_diag_detect(&p, p.ans, PT_CARRY_TENS, NULL) < 0);
        q->diag_item = PT_CARRY_TENS + 1;
        mq_diag_feed(q, &p, mq_diag_example(&p, PT_CARRY_TENS), false, true, NULL, 0);
    }
    CHECK(d->state == PS_PRACTICE);
}

/* every pattern's moment draws, with no school words, and the grown-ups' list and the log have it */
static modx_api_t kapi;
static int bad;
static int k_text(const font_t *f, int x, int y, const char *s, uint16_t fg, int bg)
{
    static const char *const banned[] = {"mistake", "wrong", "correct", "borrow", "problem", "skill", "math", "learn",
                                         "test", "practice", "level", NULL};
    char w[200];
    snprintf(w, sizeof w, "%s", s);
    for (char *c = w; *c; c++)
        *c = (char)(*c | 32);
    for (int i = 0; banned[i]; i++)
        if (strstr(w, banned[i]) && bad++ < 6)
            fprintf(stderr, "the friend's moment says \"%s\"\n", s);
    return modx_api.text(f, x, y, s, fg, bg);
}
static char logbuf[4096];
static void log_line(void *u, const char *l)
{
    (void)u;
    strncat(logbuf, l, sizeof logbuf - strlen(logbuf) - 2);
    strcat(logbuf, "\n");
}
static void test_moments(void)
{
    kapi = modx_api;
    kapi.text = k_text;
    memset(q, 0, mq_module_rw_size);
    memset(&ops, 0, sizeof ops);
    CHECK(mq_module_init(&kapi, q, &ops) == 0);
    q->rng = 0x2545F491u, q->scenes_off = true;
    con("new Ada 4"), con("engine stop"), con("fun off");
    int shown = 0;
    for (int pt = 0; pt < PT_COUNT; pt++) {
        bool ok = false;
        for (int sk = 0; sk < MQ_NSKILL && !ok; sk++) {
            if (sk == S_FACTS)
                continue;
            for (int d = 0; d < 7 && !ok; d++) {
                char b[24];
                snprintf(b, sizeof b, "skill %d %d", sk, d);
                if (con(b) || q->scene != SC_PLAY || q->sub != GS_PLAY)
                    continue;
                if (mq_diag_example(&q->prob, pt) == 0x7FFFFFFF)
                    continue;
                char c[24];
                snprintf(c, sizeof c, "diag show %d", pt);
                ok = con(c) == 0 && mq_diag_on(q);
            }
        }
        CHECK(ok);
        if (!ok)
            continue;
        shown++;
        for (int k = 0; k < 6 && mq_diag_on(q); k++) {
            int r[4];
            q->redraw = true;
            ops.draw(q, 0, r);
            con("diag next");
        }
    }
    CHECK(shown == PT_COUNT && bad == 0);
    /* the grown-ups: the list (a tap on "patterns"), and the play log */
    mq_diag_rec(q, PT_HANDS_SWAP)->hits = 4, mq_diag_rec(q, PT_HANDS_SWAP)->chances = 6;
    mq_diag_rec(q, PT_HANDS_SWAP)->state = PS_PRACTICE, mq_diag_rec(q, PT_HANDS_SWAP)->step = 2;
    snprintf(mq_diag_rec(q, PT_HANDS_SWAP)->ex, 24, "8:15, not 3:40");
    logbuf[0] = 0;
    mq_coach_log(q, log_line, NULL);
    CHECK(strstr(logbuf, "pattern,status") && strstr(logbuf, "Hour and minute hands swapped,practising,4,6"));
    CHECK(mq_diag_spotted(q) >= 1);
}

int main(void)
{
    harness_init();
    test_detect();
    test_followers();
    test_random_slips();
    test_lifecycle();
    test_moments();
    printf("test_mq_diag: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
