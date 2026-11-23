/* Sparkle Town (modules/mathquest): regressions for the second code review's bugs (docs/audit/CODE_REVIEW_2.md)
 * that are checked best from inside the module -- each test names its CR2 item. The ones a screen shows are UI
 * scripts (tests/ui/49_mathquest_cr2_*.txt). */
#include "mq_test_util.h"
#include "gfx.h"

enum { TV_DOWN = 1, TV_DRAG, TV_UP, TV_TAP };

static void touch(int type, int x, int y) { ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(type, x, y)); }
static void draw(void)
{
    int r[4];
    q->redraw = true;
    ops.draw(q, 0, r);
}

static void player(const char *name)
{
    fresh_module();
    char b[40];
    snprintf(b, sizeof b, "new %s", name);
    con(b);
    q->fun_off = true;
    con("engine stop");
}

/* CR2-21 (B8): a grown-up answering the question preview is never booked as the child -- no problems, skill record,
 * stars, coins, recent orders or facts; the preview still celebrates a right answer and gives a typed one a retry */
static void test_preview_not_hers(void)
{
    player("Mia 2");
    con("seed 3");
    mq_profile_t *p = mq_prof(q);
    int sk = 1;
    unsigned problems = p->problems, stars = p->stars, coins = mq_home(q)->coins, recent = q->recent_n;
    mq_skrec_t before = p->sk[sk];
    con("preview 1 1");
    CHECK(q->preview && q->scene == SC_QUIZ);
    con("wrong");
    con("step 5");
    con("preview 1 1");
    con("correct");
    CHECK(q->sub == 1); /* (celebrating) */
    for (int i = 0; i < 40; i++) /* (on to the next, and past a round's length: never a round won) */
        con("step 40"), con("correct");
    CHECK(q->scene == SC_QUIZ || q->scene == SC_FEEDBACK);
    CHECK(p->problems == problems && p->stars == stars && mq_home(q)->coins == coins && q->recent_n == recent);
    CHECK(!memcmp(&before, &p->sk[sk], sizeof before));
}

/* a friend's way, at her own order: the jumping trick (STG 0) on a "use this way" order */
static void way_order(void)
{
    player("Noa 2");
    con("seed 3");
    con("notebook 2");
    con("skill 16 1");
    con("step 5");
    con("strat order 0");
    con("step 5");
    con("strat 0 use");
    con("step 5");
    con("strat try");
    con("step 5");
}

/* CR2-09 (B8): the first wrong move in a friend's model is a slip, never a made-up answer of hers: the recent
 * orders say "a slip", the error patterns are not fed */
static void test_way_slip(void)
{
    way_order();
    CHECK(mq_strat_on(q));
    uint8_t ri = q->recent_i;
    unsigned problems = mq_prof(q)->problems;
    con("strat act 0"); /* (a hop of 0: wrong) */
    con("step 5");
    CHECK(mq_prof(q)->problems == problems + 1); /* (the order missed first time, for the coach) */
    const mq_recent_t *r = &q->recent_o[ri % MQ_NRECENT];
    CHECK(!strcmp(r->her, "a slip"));
    CHECK(q->diag_hit == 0);
    CHECK(!q->judge_slip);
}

/* CR2-24: an order worked through a friend's model is timed from the order, never a 0 ms answer */
static void test_way_time(void)
{
    way_order();
    uint32_t start = q->prob_start;
    con("step 100");
    con("strat auto");
    con("step 5");
    con("strat done");
    CHECK(q->sub == GS_CHEER);
    CHECK(q->prob_start == start && q->now - q->prob_start > 4000);
}

/* CR2-07 (B3): a thing picked in a friend's model (a chip, tap-then-tap) is not left picked on the game's widgets
 * after "back"; and a widget set's picked index never drops on a WG_DROP unless it is still a thing to carry */
static void test_stale_picked(void)
{
    player("Noa 3");
    con("seed 4");
    con("school 4 40");
    con("notebook 2");
    con("skill 16 0");
    con("strat 0 bulb 38 25");
    con("step 4");
    con("strat try");
    con("step 4");
    draw();
    int chip = -1;
    for (int i = 0; i < q->wg.n; i++)
        if (q->wg.w[i].kind == WG_DRAG && chip < 0)
            chip = i;
    CHECK(chip >= 0);
    if (chip >= 0) {
        const mq_wg_t *g = &q->wg.w[chip];
        touch(TV_TAP, g->x + g->w / 2, g->y + g->h / 2);
        CHECK(q->wg.picked == chip);
    }
    mq_strat_close(q);
    CHECK(q->wg.picked == -1 && q->wg.carry == -1 && q->wg.press == -1);
    /* the library: a stale pick (index 0, now a button) is not dropped on a WG_DROP */
    mq_wg_begin(q);
    int b = mq_wg_add(q, WG_BUTTON, 5, 0, 10, 10, 40, 30);
    int d = mq_wg_add(q, WG_DROP, 6, 0, 100, 10, 60, 60);
    q->wg.picked = (int8_t)b;
    mq_wev_t ev;
    CHECK(mq_wg_touch(q, TV_TAP, 130, 40, &ev));
    CHECK(ev.why == WHY_TAP && ev.idx == d);
    /* a new order clears what was picked on the last */
    q->wg.picked = 0;
    con("skill 14 1");
    CHECK(q->wg.picked == -1);
}

/* any run of the screen's text holding s */
static bool on_screen(const char *s) { return gfx_textlog_contains(s); }
static bool order_says(const char *s)
{
    for (int l = 0; l < 3; l++)
        if (strstr(q->prob.text[l], s))
            return true;
    return strstr(q->prob.cap, s) != NULL;
}
/* the lightbulb of skill sk at rung d (seed n): drawn "as asked" */
static void bulb_of(int sk, int d, int n)
{
    char b[40];
    snprintf(b, sizeof b, "seed %d", n);
    con(b);
    snprintf(b, sizeof b, "skill %d %d", sk, d);
    con(b);
    con("bulb");
    con("step 20");
    draw();
}

/* CR2-11, 12, 13, 14, 17: the pictures never print the answer or the wrong thing. The odd/even and factor orders' array box
 * prints neither the total ("N in all") nor rows the order does not say; the equal-fractions bars label the second
 * bar with its unknown, never the first fraction's label or the answer; the glue order draws no place chart */
static void test_pictures(void)
{
    player("Mia 2");
    con("scene off");
    int bad11 = 0, bad13 = 0, n13 = 0, bad12 = 0, n12 = 0, bad14 = 0, n14 = 0, bad17 = 0, n17 = 0;
    char b19[24];
    for (int seed = 1; seed <= 12; seed++) {
        for (int d = 3; d <= 4; d++) { /* number patterns: odd or even */
            bulb_of(S_PATTERN, d, seed);
            if (q->prob.kind == K_CHOICE && q->prob.vis == V_ARRAY && on_screen(" in all"))
                bad11++, fprintf(stderr, "odd/even: \"in all\" printed (%s)\n", q->prob.text[0]);
        }
        for (int d = 2; d <= 4; d++) { /* factors */
            bulb_of(S_FACTORS, d, seed);
            char r[24];
            snprintf(r, sizeof r, "%d rows of %d", q->prob.v[0], q->prob.v[1]);
            if (q->prob.kind == K_CHOICE && q->prob.vis == V_ARRAY && (on_screen(" in all") || (on_screen(r) && !order_says(r))))
                bad11++, fprintf(stderr, "factors: the box prints the answer (%s)\n", q->prob.text[0]);
        }
        for (int d = 0; d <= 3; d++) { /* the same amount */
            bulb_of(S_EQUIV, d, seed);
            const mq_prob_t *p = &q->prob;
            if (p->vis != V_BAR2 || (p->v[2] != p->ans && p->v[3] != p->ans))
                continue;
            n13++;
            char a[16], f[16];
            snprintf(a, sizeof a, "%d/%d", p->v[2], p->v[3]);
            snprintf(f, sizeof f, "%d/%d", p->v[0], p->v[1]);
            int nf = 0; /* (the first fraction's label once, the second bar's never the same) */
            for (int i = 0; i < gfx_textlog_count(); i++)
                nf += !strcmp(gfx_textlog_get(i)->text, f);
            if (on_screen(a) || nf > 1 || (!on_screen("?/") && !on_screen("/?")))
                bad13++, fprintf(stderr, "same amount: %s %s labelled wrong (%d %d)\n", p->text[0], p->text[1], on_screen(a), nf);
        }
        bulb_of(S_RULE, 2, seed); /* CR2-14: "which number does the 6th frog wear?" -- the 5th is shown, the "?" is the 6th */
        if (strstr(q->prob.text[1], "6th")) {
            char t5[12], an[12];
            snprintf(t5, sizeof t5, "%d", q->prob.v[0] + 4 * q->prob.v[1]);
            snprintf(an, sizeof an, "%d", (int)q->prob.ans);
            n14++;
            if (!on_screen(t5) || !on_screen("?") || on_screen(an))
                bad14++, fprintf(stderr, "the 6th frog: %s\n", q->prob.text[0]);
        }
        bulb_of(S_TWOPAT, 0, seed); /* CR2-14: two patterns -- the second one's "?" where the first has the number said */
        if (q->prob.vis == V_PATTERN) {
            n14++;
            char an[12];
            snprintf(an, sizeof an, "%d", (int)q->prob.ans);
            int b2 = q->prob.v[4], ask = q->prob.v[3];
            if (!b2 || ask < 2 || !on_screen("?") || on_screen(an) || q->prob.ans != b2 * (ask - 1))
                bad14++, fprintf(stderr, "two patterns: %s\n", q->prob.text[0]);
        }
        bulb_of(S_FRACMUL, 2, seed); /* CR2-17: a fraction of a fraction -- the bulb shades no square of the answer's overlap */
        if (q->prob.vis == V_GRID) {
            n17++;
            if (q->vis_shaded)
                bad17++, fprintf(stderr, "fraction of a fraction: %d squares pink (%s)\n", q->vis_shaded, q->prob.cap);
        }
        snprintf(b19, sizeof b19, "skill %d 0", S_CMPBIG);
        con(b19); /* compare big numbers: the glue */
        if (!strncmp(q->prob.text[0], "Glue", 4)) {
            n12++;
            if (q->prob.vis == V_PV || q->prob.vis == V_BASE10)
                bad12++;
        }
    }
    printf("pictures: %d same-amount bulbs, %d glue orders, %d patterns, %d fractions of fractions\n", n13, n12, n14, n17);
    CHECK(bad11 == 0);
    CHECK(n13 > 0 && bad13 == 0);
    CHECK(n12 > 0 && bad12 == 0);
    CHECK(n14 > 4 && bad14 == 0);
    CHECK(n17 > 4 && bad17 == 0);
}

static bool run_is(const char *s)
{
    for (int i = 0; i < gfx_textlog_count(); i++)
        if (!strcmp(gfx_textlog_get(i)->text, s))
            return true;
    return false;
}
/* CR2-15: the grown-ups' question preview never labels a division's rows with the quotient (a leftover order's answer
 * is q * 1000 + r) or "0" (a quotient over 99), and a big sum shows its true second number */
static void test_preview_pictures(void)
{
    player("Mia 2");
    int ndiv = 0, nsum = 0, bad = 0;
    for (int seed = 1; seed <= 8; seed++) {
        char b[32];
        for (int k = 0; k < 3; k++) {
            int sk = k < 2 ? S_DIVIDE : S_ADDSUBBIG, d = k == 0 ? 2 : 4;
            snprintf(b, sizeof b, "seed %d", seed);
            con(b);
            snprintf(b, sizeof b, "preview %d %d", sk, d);
            con(b);
            draw();
            const mq_prob_t *p = &q->prob;
            if (p->vis == V_GROUPS) {
                ndiv++;
                long qt = p->kind == K_REM ? p->ans / 1000 : p->ans;
                snprintf(b, sizeof b, "%ld", qt);
                int nq = 0; /* (a quotient over 99: every row a "?", never "0" -- the keypad's 0 is a key) */
                for (int i = 0; i < gfx_textlog_count(); i++)
                    nq += !strcmp(gfx_textlog_get(i)->text, "?");
                if (run_is(b) || (!p->v[1] && p->v[0] <= 12 && nq < p->v[0]))
                    bad++, fprintf(stderr, "preview: %s labels the rows with %s\n", p->text[0], run_is(b) ? b : "0");
            }
            if (sk == S_ADDSUBBIG && p->op[1] > 30000) {
                nsum++;
                snprintf(b, sizeof b, "+ %ld", (long)p->op[1]);
                if (!on_screen(b))
                    bad++, fprintf(stderr, "preview: %s %s does not show %s\n", p->text[0], p->text[1], b);
            }
        }
    }
    con("go parent");
    printf("preview pictures: %d divisions, %d big sums\n", ndiv, nsum);
    CHECK(ndiv > 4 && bad == 0);
}

/* CR2-16: a teaching moment's try draws what its question names: "how many tens?" with the carried ten counts to the
 * answer (the carried one in yellow); "is this a square corner?" without the mark the show then points to */
static void test_teach_pictures(void)
{
    int carry = 0, corner = 0;
    for (int idea = IDEA_NONE + 1; idea < IDEA_COUNT; idea++)
        for (int seg = 0; seg < 5; seg++) {
            const char *right = "", *ask = mq_teach_try_ask(idea, seg, &right);
            if (strstr(ask, "carried") && strstr(ask, "How many tens")) {
                carry++;
                CHECK(mq_teach_try_tens(idea, seg) == atoi(right));
            }
            if (strstr(ask, "square corner")) {
                corner++;
                CHECK(!mq_teach_try_mark(idea, seg));
            }
        }
    CHECK(carry == 1 && corner == 1);
}

/* CR2-19, CR2-20 (B9): the unlock ladder stops at its top (it wrapped after the 256th unlock and then unlocked coins
 * and a save every few seconds forever); her star total holds at 65535 (it wrapped to 0) */
static void test_counters(void)
{
    player("Mia 2");
    mq_profile_t *p = mq_prof(q);
    mq_home_t *h = mq_home(q);
    p->stars = 65000;
    h->ladder = 250;
    con("go map");
    for (int i = 0; i < 40; i++) { /* every unlock still due, then the top */
        q->earn_msg[0] = 0, q->earn_qn = 0;
        mq_meta_frame(q);
    }
    CHECK(h->ladder == 255);
    unsigned coins = h->coins;
    for (int i = 0; i < 40; i++) {
        q->earn_msg[0] = 0, q->earn_qn = 0;
        mq_meta_frame(q);
    }
    CHECK(h->ladder == 255 && h->coins == coins);
    CHECK(mq_meta_next(q, NULL, NULL, NULL) > 0);
    /* stars: a right answer at the top holds */
    p->stars = 65534;
    con("skill 1 0");
    con("correct");
    con("settle");
    con("step 2");
    con("correct");
    CHECK(p->stars == 65535);
}

/* CR2-18: a merge-board cell no game path makes (chain 0, a chain or level past the tables) is cleared when the board
 * opens, and never indexes the drawing's tables (UBSan: plate[-1]) */
static void test_merge_cells(void)
{
    player("Mia 2");
    con("merge open");
    mq_extra(q)->board[0] = 0x01;  /* chain 0 */
    mq_extra(q)->board[1] = 0x4F;  /* chain 4, level 15 */
    mq_extra(q)->board[2] = 0x12;  /* a real one */
    draw(); /* (drawn before it is opened again: skipped) */
    con("merge open");
    CHECK(mq_extra(q)->board[0] == 0 && mq_extra(q)->board[1] == 0 && mq_extra(q)->board[2] == 0x12);
    draw();
}

/* CR2-23: in Bubble Pop's cheer the fun that floats up is hers to pop (a real tap: DOWN, a frame, TAP); a tap
 * elsewhere still goes on */
static void test_cheer_fun(void)
{
    player("Mia 2");
    con("seed 3");
    con("scene off");
    con("skill 24 0");
    con("mech 0");
    con("step 30");
    CHECK(q->gm.mech == ME_POP);
    con("correct");
    con("step 2");
    CHECK(q->sub == GS_CHEER);
    mq_game_t *g = &q->gm;
    int k = 0;
    while (k < GM_NOBJ - 1 && g->o[k].kind)
        k++;
    memset(&g->o[k], 0, sizeof g->o[k]);
    g->o[k].kind = 6 /* GO_PLAIN (mq_play.c) */, g->o[k].hp = 1, g->o[k].x = 230 * 16, g->o[k].y = 150 * 16;
    unsigned stars = g->stars;
    touch(TV_DOWN, 230, 150);
    q->anim_t += 40;
    touch(TV_TAP, 230, 150);
    CHECK(q->sub == GS_CHEER && g->stars > stars);
    q->anim_t += 400;
    touch(TV_TAP, 300, 200); /* (nothing there: on with the next order) */
    CHECK(q->sub != GS_CHEER);
}

/* CR2-06: in the bubbles beat a tap pops once (its DOWN pops; its TAP fell through to the field's button and popped
 * a second bubble under the finger) */
static void test_beat_pop_once(void)
{
    player("Mia 2");
    con("go map");
    mq_fun_start(q, BEAT_BUBBLES, FR_MAP);
    CHECK(q->scene == SC_FUN && q->beat == BEAT_BUBBLES);
    q->bub_mode = BB_SKIP, q->bub_next = 4, q->bub_step = 2;
    memset(q->fo, 0, sizeof q->fo);
    q->fo[0] = (mq_fobj_t){.x = 160, .y = 130, .r = 14, .on = 1, .v = 4};
    q->fo[1] = (mq_fobj_t){.x = 162, .y = 131, .r = 14, .on = 1, .v = 6};
    draw();
    touch(TV_DOWN, 161, 130);
    q->anim_t += 40;
    touch(TV_TAP, 161, 130);
    CHECK(!q->fo[0].on && q->fo[1].on);
}

/* CR2-10: on the calculator the grown-ups' standards list scrolls by a drag (it only redrew) */
static void test_standards_drag(void)
{
    player("Mia 2");
    con("go parent");
    q->gate.shown = false;
    q->tab = 0;
    q->parent_scroll = 0;
    draw();
    touch(TV_DOWN, 160, 150);
    touch(TV_DRAG, 160, 110);
    touch(TV_DRAG, 160, 60);
    CHECK(q->parent_scroll >= 5);
    touch(TV_UP, 160, 60);
    touch(TV_DOWN, 160, 60);
    touch(TV_DRAG, 160, 200);
    CHECK(q->parent_scroll == 0);
    touch(TV_UP, 160, 200);
}

/* CR2-05 (B2): a time mark is never in the future, so "within N ms of it" holds in its own tick */
static void test_mark(void)
{
    q->anim_t = 4000;
    uint32_t m = mq_mark(q);
    CHECK(m == 4000 && mq_since(q, m) == 0);
    q->anim_t = 0;
    CHECK(mq_mark(q) == 1); /* (0 means "none") */
    CHECK(mq_since(q, 1) < 0 && mq_since(q, 1) > -10);
}

int main(void)
{
    harness_init();
    test_preview_not_hers();
    test_way_slip();
    test_way_time();
    test_stale_picked();
    test_mark();
    test_standards_drag();
    test_beat_pop_once();
    test_cheer_fun();
    test_counters();
    test_merge_cells();
    test_pictures();
    test_teach_pictures();
    test_preview_pictures();
    printf("test_mq_cr2: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
