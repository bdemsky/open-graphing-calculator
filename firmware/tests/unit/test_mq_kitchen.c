/* Sparkle Town (modules/mathquest) the Sparkle Kitchen (mq_merge.c): a cake counter where cutting and serving are
 * fractions.
 *   - the pieces: 1/d of a cake for d in 1, 2, 3, 4, 6, 8, the names, the knives at each rung, the fewest pieces cut
 *     from one cake that make an amount;
 *   - cutting: a whole in 2, a half in 2 (two quarters), re-cutting, a third in 2, a half in 3; too tiny at the rung;
 *     as small as it gets; the amount on the counter never changes; pieces never join on the counter;
 *   - the counter never blocks play: a full counter still bakes (a spare piece to the cafe, rewarded), a cut makes its
 *     room the same way, a new order keeps at most KC_KEEP leftovers;
 *   - the orders: made by the generator for S_PARTITION at her rung, at every rung (its fractions, its new
 *     complication in most orders, a whole cake from the oven that the rung's knives can cut into the order's
 *     slices), before school's fractions no further than thirds and sixths;
 *   - judging: the plate's amount -- any mix of slices that make it (2/4 for 1/2; a third, a quarter and a sixth for
 *     3/4) --, a miss with one more try, a second miss shown how and finished; the first answer booked once (the
 *     skill, the problem's history, the review, the rung's window, the strand, the error patterns -- three eighths
 *     for 3/4 --, the grown-ups' recent orders), never the demo's; the plate holds one cake;
 *   - the plate: slices snap in one after another round from 12 o'clock (drawn there), a slice taken out and the
 *     later ones close up, a tap or a drag on a slice takes that slice;
 *   - the first-time flow: Pip and 1/2 of a cake, the guide's step for each moment (the oven, the knife, the cut, the
 *     plate, Give), the free cake, by touch (a tap is a DOWN then a TAP: one act), never again after;
 *   - no give-away: nothing drawn changes with what is on the plate (no total, no fraction of it);
 *   - fair and calm: no clock brings sparkles, nothing spoils, an order waits for her;
 *   - the rewards: coins and hearts, the bonus sparkle for the fewest pieces, a present every third order, the cafe,
 *     the visitor's whole cake;
 *   - the keys and the widgets (every hit area at least MQ_WG_HIT), the knife by key, tap and drag; nothing said in
 *     school words;
 *   - the save: the old merge board's things migrated once (sparkles back, the cafe's treats as cakes), the merging
 *     cake counter's pieces as the same pieces (its last row moved up, the plate up to one cake, nothing lost), the
 *     counter kept through a save and a load, the block's size unchanged. */
#include "mq_test_util.h"

enum { TD = 1, TM, TU, TT };
enum { K_ENTER = 49, K_UP = 12, K_RIGHT = 14, K_DOWN = 15, K_CLEAR = 20, K_F3 = 3 };
static const char *const banned[] = {"math", "learn", "lesson", "homework", "grade", "quiz", "test", "skill", "problem",
                                     "level", "mistake", "worksheet", "correct", "fact", "practice", "review", "fraction",
                                     "numerator", "denominator", NULL};
static bool school(const char *s)
{
    char w[200];
    snprintf(w, sizeof w, "%s", s);
    for (char *c = w; *c; c++)
        *c = (char)(*c | 32);
    for (int i = 0; banned[i]; i++)
        if (strstr(w, banned[i]))
            return true;
    return false;
}

/* every text drawn, for the screens' checks */
static char texts[200][64];
static int ntext;
static int log_text(const font_t *f, int x, int y, const char *s, uint16_t fg, int bg)
{
    if (ntext < 200 && s && s[0])
        snprintf(texts[ntext++], sizeof texts[0], "%s", s);
    return modx_api.text(f, x, y, s, fg, bg);
}
static modx_api_t cap_api;
static void draw(void)
{
    int r[4];
    ntext = 0;
    ops.draw(q, 0, r);
}
static bool drawn(const char *sub) /* (in any text, or across a wrapped line's end) */
{
    static char all[200 * 65];
    int k = 0;
    for (int i = 0; i < ntext; i++) {
        int n = (int)strlen(texts[i]);
        memcpy(all + k, texts[i], (size_t)n);
        k += n;
        all[k++] = ' ';
    }
    all[k] = 0;
    return strstr(all, sub) != NULL;
}
static int drawn_exact(const char *s)
{
    int n = 0;
    for (int i = 0; i < ntext; i++)
        n += !strcmp(texts[i], s);
    return n;
}
static void touch(int type, int x, int y) { ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(type, x, y)); }
static void tap(int x, int y) { touch(TD, x, y), touch(TT, x, y); } /* a real tap: DOWN, then TAP */
static void drag(int x0, int y0, int x1, int y1)
{
    touch(TD, x0, y0);
    for (int k = 1; k <= 6; k++)
        touch(TM, x0 + (x1 - x0) * k / 6, y0 + (y1 - y0) * k / 6);
    touch(TU, x1, y1);
}
static void key(int k) { ops.event(q, MODX_EV_KEY, k); }

/* the screen's geometry (mq_merge.c) */
#define CELL 30
#define BX 4
#define BY (MQ_Y0 + 22)
static int cx(int c) { return BX + (c % KC_COLS) * CELL + CELL / 2; }
static int cy(int c) { return BY + (c / KC_COLS) * CELL + CELL / 2; }
static const int OVEN_X = 29, OVEN_Y = MQ_Y0 + 168, GIVE_X = 284, GIVE_Y = MQ_Y0 + 114, PLATE_X = 220, PLATE_Y = MQ_Y0 + 114;
enum { W_PLATE = 2, W_GIVE = 3, W_OVEN = 4, W_KNIFE = 9 };
/* a point on the plate's slice at angle a (degrees clockwise from 12 o'clock) */
static int px(int a) { return PLATE_X + mq_isin(a) * 14 / 1024; }
static int py(int a) { return PLATE_Y - mq_icos(a) * 14 / 1024; }
/* the knife that cuts in k: its button's middle (after a draw: the widgets) */
static bool knife_xy(int k, int *x, int *y)
{
    for (int i = 0; i < q->wg.n; i++)
        if (q->wg.w[i].id == W_KNIFE && q->wg.w[i].arg == k) {
            *x = q->wg.w[i].x + q->wg.w[i].w / 2, *y = q->wg.w[i].y + q->wg.w[i].h / 2;
            return true;
        }
    return false;
}
static int focused_id(void) { return q->wg.focus >= 0 ? q->wg.w[q->wg.focus].id : -1; }

static mq_extra_t *E(void) { return mq_extra(q); }
static int count(int code)
{
    int n = 0;
    for (int i = 0; i < KC_CELLS; i++)
        n += E()->board[i] == code;
    return n;
}
static int counter_v(void)
{
    int v = 0;
    for (int i = 0; i < KC_CELLS; i++)
        v += mq_merge_value(E()->board[i]);
    return v;
}
static int counter_n(void)
{
    int n = 0;
    for (int i = 0; i < KC_CELLS; i++)
        n += E()->board[i] != 0;
    return n;
}
static void clear_counter(void)
{
    memset(E()->board, 0, sizeof E()->board);
    memset(E()->plate, 0, sizeof E()->plate);
}
static void player(void)
{
    memset(q, 0, mq_module_rw_size);
    memset(&ops, 0, sizeof ops);
    CHECK(mq_module_init(&cap_api, q, &ops) == 0);
    q->fun_ms = 0, q->rng = 0x2545F491u, q->scenes_off = true;
    con("new Zoe 2");
    q->fun_off = true;
}
/* an order of the engine's at rung r, the demo seen */
static void real_order(int r)
{
    mq_demo_seen(q, MQ_DEMO_KITCHEN);
    mq_prof(q)->sk[S_PARTITION].diff = (uint8_t)r;
    E()->ck_who = 0;
    mq_merge_order_new(q);
}
static void plate_of(const int *codes, int n)
{
    memset(E()->plate, 0, sizeof E()->plate);
    for (int i = 0; i < n; i++)
        E()->plate[i] = (uint8_t)codes[i];
}
/* the pieces: P(d) 1/d of the pink cake, L(d) of the lemon one */
#define P(d) CK_CODE(CK_PINK, d)
#define L(d) CK_CODE(CK_LEMON, d)

/* ------------------------------------------------------------------ the pieces */
static void test_pieces(void)
{
    static const int ds[6] = {1, 2, 3, 4, 6, 8};
    for (int i = 0; i < 6; i++)
        CHECK(mq_merge_value(P(ds[i])) == 24 / ds[i] && mq_merge_value(L(ds[i])) == 24 / ds[i]);
    CHECK(mq_merge_value(0) == 0 && mq_merge_value(P(5)) == 0 && mq_merge_value(P(7)) == 0 && mq_merge_value(P(0)) == 0 &&
          mq_merge_value(P(9)) == 0 && mq_merge_value(0x37) == 0);
    CHECK(!strcmp(mq_merge_name(P(4)), "quarter") && !strcmp(mq_merge_name(P(1)), "whole cake") &&
          !strcmp(mq_merge_name(L(3)), "third") && !strcmp(mq_merge_name(P(8)), "eighth") && !mq_merge_name(0x77)[0]);
    /* the knives: halves and quarters; then thirds and sixths; eighths at the top rung */
    int k[5];
    CHECK(mq_merge_knives(0, k) == 2 && k[0] == 2 && k[1] == 4);
    CHECK(mq_merge_knives(1, k) == 4 && k[1] == 3 && k[3] == 6 && mq_merge_knives(2, k) == 4);
    CHECK(mq_merge_knives(3, k) == 5 && k[4] == 8);
    /* the fewest pieces cut from one cake: a half and a quarter for 3/4, two thirds for 2/3, a half and two sixths
     * (or two thirds and a sixth) for 5/6, a half, a quarter and an eighth for 7/8; none for 7/8 without eighths */
    CHECK(mq_merge_fewest(0, 12) == 1 && mq_merge_fewest(0, 18) == 2 && mq_merge_fewest(0, 6) == 1);
    CHECK(mq_merge_fewest(2, 16) == 2 && mq_merge_fewest(2, 20) == 3 && mq_merge_fewest(2, 18) == 2);
    CHECK(mq_merge_fewest(3, 21) == 3 && mq_merge_fewest(3, 15) == 2 && mq_merge_fewest(3, 9) == 2);
    CHECK(mq_merge_fewest(0, 21) == 0 && mq_merge_fewest(0, 0) == 0);
}

/* ------------------------------------------------------------------ cutting */
static void test_cutting(void)
{
    player();
    con("merge open");
    real_order(2); /* (the knives: 2, 3, 4, 6) */
    clear_counter();
    E()->board[0] = (uint8_t)P(1);
    /* a whole in 2: two halves; a half in 2: two quarters (a half IS two quarters) */
    CHECK(mq_merge_cut(q, 0, 2) == 1 && count(P(2)) == 2 && E()->board[0] == P(2) && strstr(q->mb_say, "Cut in 2: two halves"));
    CHECK(counter_v() == 24);
    CHECK(mq_merge_cut(q, 0, 2) == 1 && count(P(4)) == 2 && count(P(2)) == 1 && strstr(q->mb_say, "A half cut in 2: two quarters"));
    CHECK(counter_v() == 24);
    /* re-cutting: a quarter in 2 would be eighths -- too tiny before the top rung; a quarter in 3 never */
    int c4 = -1;
    for (int i = 0; i < KC_CELLS && c4 < 0; i++)
        if (E()->board[i] == P(4))
            c4 = i;
    CHECK(mq_merge_cut(q, c4, 2) == 0 && strstr(q->mb_say, "as small as it gets") && count(P(4)) == 2);
    CHECK(mq_merge_cut(q, c4, 3) == 0 && count(P(4)) == 2);
    /* the other half in 3: three sixths; the slices land next to it */
    int c2 = -1;
    for (int i = 0; i < KC_CELLS && c2 < 0; i++)
        if (E()->board[i] == P(2))
            c2 = i;
    CHECK(mq_merge_cut(q, c2, 3) == 1 && count(P(6)) == 3 && counter_v() == 24 && strstr(q->mb_say, "three sixths"));
    /* the lemon cake in 3: three thirds; a third in 2: two sixths; a sixth: as small as it gets */
    clear_counter();
    E()->board[7] = (uint8_t)L(1);
    CHECK(mq_merge_cut(q, 7, 3) == 1 && count(L(3)) == 3 && E()->board[7] == L(3));
    for (int i = 0; i < KC_CELLS; i++) /* (next to it: the places on either side or under it) */
        if (E()->board[i] == L(3))
            CHECK(abs(i % KC_COLS - 7 % KC_COLS) + abs(i / KC_COLS - 7 / KC_COLS) <= 1);
    CHECK(mq_merge_cut(q, 7, 2) == 1 && count(L(6)) == 2 && count(L(3)) == 2);
    CHECK(mq_merge_cut(q, 7, 2) == 0 && strstr(q->mb_say, "as small as it gets"));
    CHECK(mq_merge_cut(q, 9, 2) < 0); /* nothing there */
    /* pieces never join on the counter: a quarter onto a quarter is a swap */
    clear_counter();
    E()->board[0] = E()->board[1] = (uint8_t)P(4);
    CHECK(mq_merge_move(q, 0, 1) == 0 && E()->board[0] == P(4) && E()->board[1] == P(4));
    E()->board[2] = (uint8_t)L(3);
    CHECK(mq_merge_move(q, 2, 0) == 0 && E()->board[0] == L(3) && E()->board[2] == P(4));
    CHECK(mq_merge_move(q, 2, 9) == 0 && E()->board[9] == P(4) && !E()->board[2]); /* a move to a free place */
    /* the top rung: eighths; an eighth is as small as it gets; a knife the rung has not: nothing */
    con("merge order 5 8");
    clear_counter();
    E()->board[0] = (uint8_t)P(4);
    CHECK(mq_merge_cut(q, 0, 2) == 1 && count(P(8)) == 2);
    CHECK(mq_merge_cut(q, 0, 2) == 0 && strstr(q->mb_say, "as small"));
    real_order(0); /* (the knives: 2, 4) */
    clear_counter();
    E()->board[0] = (uint8_t)P(1);
    CHECK(mq_merge_cut(q, 0, 3) == 0 && E()->board[0] == P(1));
    CHECK(mq_merge_cut(q, 0, 4) == 1 && count(P(4)) == 4);
    /* a cut with no room: spare pieces go to the cafe (each a treat there), never lost */
    clear_counter();
    memset(E()->cafe, 0, sizeof E()->cafe);
    for (int i = 0; i < KC_CELLS; i++)
        E()->board[i] = (uint8_t)P(4);
    E()->board[0] = (uint8_t)P(1);
    CHECK(mq_merge_cut(q, 0, 4) == 1 && count(P(4)) == KC_CELLS - 1 - 3 + 4 && mq_cafe_treats(q) == 3);
    CHECK(strstr(q->mb_say, "Cut in 4")); /* (the cut said last; the cafe's line before it) */
}

/* ------------------------------------------------------------------ the counter never blocks play */
static void test_full_counter(void)
{
    player();
    con("merge open");
    real_order(2);
    clear_counter();
    memset(E()->cafe, 0, sizeof E()->cafe);
    /* a full counter: a whole cake at the end and slices */
    for (int i = 0; i < KC_CELLS; i++)
        E()->board[i] = (uint8_t)(i % 2 ? P(4) : L(6));
    E()->board[KC_CELLS - 1] = (uint8_t)P(1);
    E()->energy = 3;
    int coins = (int)mq_home(q)->coins, before = counter_v();
    int c = mq_merge_gen(q, 0);
    CHECK(c >= 0 && E()->energy == 2 && mq_merge_value(E()->board[c]) == 24); /* baked */
    CHECK(counter_n() == KC_CELLS && mq_cafe_treats(q) == 1 && E()->cafe[0] == P(1)); /* the spare whole: the cafe */
    CHECK((int)mq_home(q)->coins == coins + 5 && counter_v() == before - 24 + 24);   /* and a whole pays */
    CHECK(strstr(q->mb_say, "cafe"));
    /* again and again: it always bakes, each spare piece a cafe treat */
    for (int k = 0; k < 8; k++) {
        E()->energy = 1;
        int n0 = mq_cafe_treats(q);
        CHECK(mq_merge_gen(q, 0) >= 0 && counter_n() == KC_CELLS && (mq_cafe_treats(q) == n0 + 1 || n0 == 6));
    }
    /* by touch too: the oven tapped on a full counter */
    E()->energy = 1;
    draw();
    tap(OVEN_X, OVEN_Y);
    CHECK(E()->energy == 0);
    /* a new order keeps at most KC_KEEP leftovers: the rest go to the cafe */
    memset(E()->cafe, 0, sizeof E()->cafe);
    for (int i = 0; i < KC_CELLS; i++)
        E()->board[i] = (uint8_t)P(8);
    real_order(2);
    CHECK(counter_n() == KC_KEEP && mq_cafe_treats(q) == 6);
    CHECK(KC_KEEP + 8 <= KC_CELLS); /* (room for a whole cake cut in eighths beside the leftovers) */
}

/* ------------------------------------------------------------------ the orders at every rung */
static void test_orders(void)
{
    player();
    con("merge open");
    int rungs = mq_rungs(S_PARTITION);
    CHECK(rungs == 4);
    for (int r = 0; r < rungs; r++) {
        int seen_den[9] = {0}, more = 0, newc = 0, N = 80;
        for (int k = 0; k < N; k++) {
            real_order(r);
            int n, dn, who = mq_merge_order(q, &n, &dn);
            const mq_prob_t *p = &q->mb_prob;
            CHECK(who >= 0 && who < MQ_NCAST && !(E()->ck_flags & KF_TUTOR));
            CHECK(p->skill == S_PARTITION && p->diff == r && p->ident && p->kind == K_FRAC && p->ans == n * 1000 + dn);
            CHECK(dn >= 2 && dn <= 8 && n >= 1 && n < dn);
            seen_den[dn]++;
            more += n > 1;
            /* the rung's levels are never passed: thirds and sixths from rung 1, more than one slice from 2, eighths from 3 */
            CHECK(r >= 1 || (dn == 2 || dn == 4));
            CHECK(r >= 3 || dn != 8);
            CHECK(r >= 2 || n == 1);
            newc += r == 0 || (r == 1 && (dn == 3 || dn == 6)) || (r == 2 && n > 1) || (r == 3 && dn == 8);
            /* the oven: one whole cake (one sparkle), and the rung's knives cut it into the order's slices */
            clear_counter();
            E()->energy = 5;
            int c = mq_merge_gen(q, 0);
            CHECK(c >= 0 && E()->energy == 4 && counter_v() == 24 && counter_n() == 1);
            CHECK(mq_merge_cut(q, c, dn) == 1 && counter_n() == dn);
            CHECK(mq_merge_fewest(r, n * 24 / dn) > 0);
        }
        CHECK(newc * 2 > N); /* most orders show the rung's new complication */
        if (r >= 1)
            CHECK(seen_den[3] + seen_den[6] > 0);
        if (r >= 2)
            CHECK(more > 0);
        if (r >= 3)
            CHECK(seen_den[8] > 0);
        printf("test_mq_kitchen: rung %d: halves %d, quarters %d, thirds %d, sixths %d, eighths %d, more than one slice %d of %d\n",
               r, seen_den[2], seen_den[4], seen_den[3], seen_den[6], seen_den[8], more, N);
    }
    /* before school's fractions (the grown-ups said grade 3, week 8): no further than thirds and sixths */
    mq_set_school(q, 3, 8);
    CHECK(!mq_school_taught(q, S_PARTITION));
    for (int k = 0; k < 40; k++) {
        real_order(3);
        int n, dn;
        mq_merge_order(q, &n, &dn);
        CHECK(dn != 8 && q->mb_prob.diff <= 2);
        int kk[5];
        CHECK(mq_merge_knives(E()->ck_rung, kk) <= 4); /* (no eighths knife either) */
    }
    mq_set_school(q, 3, 30);
    int eighths = 0;
    for (int k = 0; k < 40; k++) {
        real_order(3);
        int n, dn;
        mq_merge_order(q, &n, &dn);
        eighths += dn == 8;
    }
    CHECK(eighths > 0);
    /* no repeats: the same order not twice in a row at a rung with many */
    real_order(3);
    int last = E()->ck_nd, same = 0;
    for (int k = 0; k < 30; k++) {
        real_order(3);
        same += E()->ck_nd == last;
        last = E()->ck_nd;
    }
    CHECK(same < 3);
}

/* ------------------------------------------------------------------ judging, booked once */
static void test_judging(void)
{
    player();
    con("merge open");
    mq_profile_t *p = mq_prof(q);
    real_order(0);
    con("merge order 3 4");
    E()->ck_flags = 0;
    int tries = p->sk[S_PARTITION].tries, right = p->sk[S_PARTITION].right, probs = (int)p->problems;
    /* nothing on the plate: nothing judged */
    CHECK(mq_merge_give(q) < 0 && p->sk[S_PARTITION].tries == tries && !(E()->ck_flags & KF_TRIED));
    /* a quarter: a little more, please (one more try); booked once */
    int one[1] = {P(4)};
    plate_of(one, 1);
    CHECK(mq_merge_give(q) == 0 && strstr(q->mb_say, "a little more"));
    CHECK(p->sk[S_PARTITION].tries == tries + 1 && p->sk[S_PARTITION].right == right && (int)p->problems == probs + 1);
    CHECK(mq_learn(q)->rev[S_PARTITION].box > 0); /* played: in the review now */
    CHECK(!strcmp(q->recent_o[(q->recent_i + MQ_NRECENT - 1) % MQ_NRECENT].her, "1/4") &&
          q->recent_o[(q->recent_i + MQ_NRECENT - 1) % MQ_NRECENT].why == PW_KITCHEN);
    /* a whole cake: too much -- the second miss: shown how, nothing booked again */
    int whole[1] = {P(1)};
    plate_of(whole, 1);
    CHECK(mq_merge_give(q) < 0 && (E()->ck_flags & KF_SHOWN) && strstr(q->mb_say, "3/4 is 3 of 4 pieces"));
    CHECK(p->sk[S_PARTITION].tries == tries + 1 && (int)p->problems == probs + 1);
    /* finished with three quarters (equivalent: right) */
    int three[3] = {P(4), P(4), P(4)};
    plate_of(three, 3);
    int coins = (int)mq_home(q)->coins;
    CHECK(mq_merge_give(q) == 1 && !E()->ck_who && (int)mq_home(q)->coins == coins + 2);
    CHECK(p->sk[S_PARTITION].tries == tries + 1 && p->sk[S_PARTITION].right == right);
    /* right the first time: 2/4 for 1/2 -- booked right, six coins */
    con("merge order 1 2");
    E()->ck_flags = 0;
    int two[2] = {P(4), P(4)};
    plate_of(two, 2);
    coins = (int)mq_home(q)->coins;
    uint16_t win = mq_pace(q)->rung_win[S_PARTITION];
    CHECK(mq_merge_give(q) == 1 && (int)mq_home(q)->coins == coins + 6);
    CHECK(p->sk[S_PARTITION].tries == tries + 2 && p->sk[S_PARTITION].right == right + 1);
    CHECK(mq_pace(q)->rung_win[S_PARTITION] != win); /* the rung's own window has it */
    CHECK(mq_seen_find(q, q->mb_prob.ident) != NULL); /* the problem's history */
    /* any mix of slices that makes the order is right, each kind of cake: */
    struct { int n, d, k, c[6]; } ok[] = {
        {3, 4, 2, {P(2), P(4)}},                  /* a half and a quarter */
        {3, 4, 3, {L(3), P(4), L(6)}},            /* a third, a quarter and a sixth */
        {3, 4, 5, {P(8), P(8), P(4), P(8), P(8)}}, /* eighths and a quarter */
        {2, 3, 2, {P(2), L(6)}},                  /* a half and a sixth (the pink cake serves a lemon order) */
        {2, 3, 4, {L(6), L(6), L(6), L(6)}},      /* four sixths */
        {1, 2, 3, {L(6), L(6), L(6)}},            /* three sixths */
        {5, 6, 3, {P(2), L(3)}},                  /* a half and a third */
        {5, 8, 2, {P(2), P(8)}},                  /* a half and an eighth */
    };
    for (unsigned i = 0; i < sizeof ok / sizeof ok[0]; i++) {
        char b[32];
        con("step 60");
        snprintf(b, sizeof b, "merge order %d %d", ok[i].n, ok[i].d);
        con(b);
        E()->ck_flags = 0;
        plate_of(ok[i].c, ok[i].k);
        CHECK(mq_merge_give(q) == 1);
    }
    /* and a mix that misses is not: a third and a quarter for 3/4 */
    con("step 60");
    con("merge order 3 4");
    E()->ck_flags = 0;
    int near[2] = {L(3), P(4)};
    plate_of(near, 2);
    CHECK(mq_merge_give(q) == 0 && strstr(q->mb_say, "a little more"));
    /* the error pattern: three eighths for 3/4 (the bigger bottom number taken for the bigger piece), again and again */
    mq_diag_rec_t *d = mq_diag_rec(q, PT_BIG_DENOM);
    int hits = d->hits;
    for (int k = 0; k < 4; k++) {
        con("merge order 3 4");
        E()->ck_flags = 0;
        int e3[3] = {P(8), P(8), P(8)};
        plate_of(e3, 3);
        CHECK(mq_merge_give(q) == 0);
    }
    CHECK(d->hits == hits + 4);
    /* "Give!" mashed with whatever is at hand: nothing paid, one answer booked */
    con("merge order 5 8");
    E()->ck_flags = 0;
    coins = (int)mq_home(q)->coins;
    int t0 = p->sk[S_PARTITION].tries;
    static const int some[4] = {P(8), P(4), P(2), P(1)};
    for (int k = 0; k < 12; k++) {
        int any[1] = {some[k % 4]};
        plate_of(any, 1);
        CHECK(mq_merge_give(q) <= 0);
    }
    CHECK((int)mq_home(q)->coins == coins && p->sk[S_PARTITION].tries == t0 + 1);
    /* the plate holds one cake: a whole, then nothing more; eight eighths fill it */
    con("merge order 7 8");
    clear_counter();
    E()->board[0] = (uint8_t)P(1), E()->board[1] = (uint8_t)P(8);
    CHECK(mq_merge_put(q, 0) == 0 && mq_merge_put(q, 1) < 0 && strstr(q->mb_say, "one cake") && E()->board[1]);
    clear_counter();
    for (int i = 0; i < 10; i++)
        E()->board[i] = (uint8_t)P(8);
    for (int i = 0; i < 8; i++)
        CHECK(mq_merge_put(q, i) == i);
    CHECK(mq_merge_put(q, 8) < 0 && mq_merge_plate(q) == 24);
    CHECK(mq_merge_take(q, 0) >= 0 && mq_merge_plate(q) == 21);
}

/* ------------------------------------------------------------------ the plate: slices snap in */
static uint16_t at(int x, int y) { return fb[y * 320 + x]; }
static void test_plate(void)
{
    player();
    con("merge open");
    real_order(2);
    con("merge order 3 4");
    clear_counter();
    E()->board[0] = (uint8_t)P(2), E()->board[1] = (uint8_t)P(4), E()->board[2] = (uint8_t)L(6);
    draw();
    uint16_t empty = at(px(225), py(225));
    /* a half, a quarter, a sixth: from 12 o'clock round, each after the last (0-180, 180-270, 270-330) */
    drag(cx(0), cy(0), PLATE_X, PLATE_Y);
    drag(cx(1), cy(1), PLATE_X + 5, PLATE_Y - 5);
    drag(cx(2), cy(2), 230, MQ_Y0 + 50); /* (onto the friend: the plate too) */
    CHECK(mq_merge_plate(q) == 22 && E()->plate[0] == P(2) && E()->plate[1] == P(4) && E()->plate[2] == L(6));
    draw();
    CHECK(at(px(90), py(90)) != empty && at(px(225), py(225)) != empty && at(px(300), py(300)) != empty);
    CHECK(at(px(345), py(345)) == empty); /* (the rest of the cake's place: empty) */
    CHECK(at(px(300), py(300)) != at(px(225), py(225))); /* (the lemon sixth beside the pink quarter) */
    /* a tap on the quarter (at 225) takes it back; the sixth snaps back to 180-240 */
    tap(px(225), py(225));
    CHECK(mq_merge_plate(q) == 16 && E()->plate[1] == L(6) && count(P(4)) == 1);
    draw();
    CHECK(at(px(200), py(200)) != empty && at(px(300), py(300)) == empty);
    /* a drag of the sixth (now at 210) back onto the counter: there */
    drag(px(210), py(210), cx(10), cy(10));
    CHECK(mq_merge_plate(q) == 12 && E()->board[10] == L(6));
    /* a drag of the half within the plate: it stays */
    drag(px(90), py(90), PLATE_X - 4, PLATE_Y + 2);
    CHECK(mq_merge_plate(q) == 12 && E()->plate[0] == P(2));
    /* nothing on the screen says the plate's amount: no 1/2, 11/12, 22 */
    draw();
    CHECK(!drawn("1/2") && !drawn("11/12") && !drawn_exact("22"));
}

/* ------------------------------------------------------------------ the first visit */
static void test_first_visit(void)
{
    player();
    mq_profile_t *p = mq_prof(q);
    int tries = p->sk[S_PARTITION].tries, en, kx, ky;
    con("go map");
    con("merge open");
    CHECK(q->scene == SC_MERGE && E()->ck_who == 1 && (E()->ck_flags & KF_TUTOR) && E()->ck_nd == (1 | 2 << 4));
    en = E()->energy;
    con("step 60");
    draw();
    CHECK(drawn("Tap the oven") && drawn("1/2 of a cake")); /* step 1: Pip wants 1/2 of a cake; the hand on the oven */
    CHECK(drawn_exact("1") >= 1);
    /* a tap on the oven (DOWN, then TAP): one whole cake, on the house */
    tap(OVEN_X, OVEN_Y);
    CHECK(count(P(1)) == 1 && E()->energy == en);
    con("step 80");
    draw();
    CHECK(drawn("Pick the knife that cuts in 2")); /* step 2 */
    CHECK(knife_xy(2, &kx, &ky) && knife_xy(4, &kx, &ky) && !knife_xy(3, &kx, &ky)); /* (the demo's knives: 2, 4) */
    knife_xy(2, &kx, &ky);
    touch(TD, kx, ky); /* its DOWN only lifts; its TAP picks it (once) */
    CHECK(q->mb_knife == 0);
    touch(TT, kx, ky);
    CHECK(q->mb_knife == 2);
    con("step 80");
    draw();
    CHECK(drawn("tap the cake to cut it")); /* step 3 */
    int c = -1;
    for (int i = 0; i < KC_CELLS && c < 0; i++)
        if (E()->board[i] == P(1))
            c = i;
    tap(cx(c), cy(c));
    CHECK(count(P(2)) == 2 && !count(P(1)));
    con("step 80");
    draw();
    CHECK(drawn("Drag a half onto Pip's plate")); /* step 4 */
    drag(cx(c), cy(c), PLATE_X, PLATE_Y);
    CHECK(mq_merge_plate(q) == 12);
    con("step 10");
    draw();
    CHECK(drawn("Now tap Give")); /* step 5 */
    tap(GIVE_X, GIVE_Y);
    CHECK(!E()->ck_who && ((mq_pace(q)->demo_seen >> MQ_DEMO_KITCHEN) & 1));
    CHECK(p->sk[S_PARTITION].tries == tries); /* the demo's order is never booked as hers */
    con("step 60"); /* the next friend: a real order, no demo */
    CHECK(E()->ck_who && !(E()->ck_flags & KF_TUTOR) && q->mb_prob.skill == S_PARTITION);
    con("go map");
    con("merge open");
    clear_counter();
    E()->board[0] = (uint8_t)P(1);
    con("step 60");
    draw();
    CHECK(!(E()->ck_flags & KF_TUTOR) && !drawn("Pick the knife that cuts in 2")); /* (never again: the goal only) */
    /* the demo with quarters (she picked the other knife): a quarter, then a little more */
    player();
    con("merge open");
    clear_counter();
    E()->board[0] = (uint8_t)P(1);
    CHECK(mq_merge_cut(q, 0, 4) == 1);
    CHECK(mq_merge_put(q, 0) == 0);
    con("step 80");
    draw();
    CHECK(drawn("A little more"));
    /* too much in the demo: the step says take one back; a wrong give in the demo is not booked */
    player();
    con("merge open");
    clear_counter();
    int big[1] = {P(1)};
    plate_of(big, 1);
    con("step 80");
    draw();
    CHECK(drawn("Drag a piece back"));
    CHECK(mq_merge_give(q) == 0 && mq_prof(q)->sk[S_PARTITION].tries == 0);
}

/* ------------------------------------------------------------------ no give-away */
static void texts_now(char out[][64], int *n)
{
    q->mb_say_t = 0, q->mb_hint_t = 0; /* (the words as they settle: the goal) */
    draw();
    *n = ntext;
    for (int i = 0; i < ntext; i++)
        memcpy(out[i], texts[i], 64);
}
static void test_no_giveaway(void)
{
    player();
    con("merge open");
    real_order(2);
    con("merge order 3 4");
    E()->energy = 12;
    clear_counter();
    static char base[200][64], now[200][64];
    int nb, nn;
    texts_now(base, &nb);
    static const int plates[6][3] = {{P(2), P(8), 0}, {P(2), P(4), 0}, {P(8), P(8), P(8)}, {P(1), 0, 0}, {P(4), 0, 0}, {L(3), L(6), P(4)}};
    for (int k = 0; k < 6; k++) {
        int c[3], m = 0;
        for (int i = 0; i < 3; i++)
            if (plates[k][i])
                c[m++] = plates[k][i];
        plate_of(c, m);
        texts_now(now, &nn);
        /* the same words whatever is on the plate (Give's label aside: it is a widget's own) */
        int a = 0, b = 0;
        for (int i = 0; i < nb; i++)
            a += strcmp(base[i], "Give!") && strcmp(base[i], "plate");
        for (int i = 0; i < nn; i++)
            b += strcmp(now[i], "Give!") && strcmp(now[i], "plate");
        CHECK(a == b);
        for (int i = 0, j = 0; i < nn; i++) {
            if (!strcmp(now[i], "Give!") || !strcmp(now[i], "plate"))
                continue;
            while (j < nb && (!strcmp(base[j], "Give!") || !strcmp(base[j], "plate")))
                j++;
            CHECK(j < nb && !strcmp(now[i], base[j]));
            j++;
        }
        CHECK(!drawn("5/8") && !drawn("10/16") && !drawn_exact("5") && !drawn_exact("8")); /* (a half and an eighth) */
        /* the order's 3 over 4, and the knives' own 3 and 4 (cut in 2, 3, 4, 6): nothing else */
        CHECK(drawn_exact("3") == 2 && drawn_exact("4") == 2 && drawn_exact("6") == 1 && drawn_exact("2") == 1);
    }
    /* a picked piece says its own size, never what the plate makes */
    clear_counter();
    E()->board[0] = (uint8_t)P(4);
    q->mb_pick = -1;
    tap(cx(0), cy(0));
    CHECK(strstr(q->mb_say, "1/4") && !strstr(q->mb_say, "plate"));
}

/* ------------------------------------------------------------------ fair and calm, the rewards */
static void test_fair_and_rewards(void)
{
    player();
    con("merge open");
    real_order(0);
    E()->energy = 0;
    clear_counter();
    E()->board[3] = (uint8_t)P(4);
    uint8_t nd = E()->ck_nd, who = E()->ck_who;
    CHECK(mq_merge_gen(q, 0) < 0 && strstr(q->mb_say, "Play a game"));
    con("step 3000"); /* two minutes: no sparkles from a clock, nothing spoils, the friend waits */
    CHECK(E()->energy == 0 && E()->board[3] == P(4) && E()->ck_nd == nd && E()->ck_who == who);
    /* the fewest pieces: a bonus sparkle and "So neat!"; hearts; the next friend after the thank-you */
    E()->energy = 4;
    con("merge order 3 4");
    E()->ck_flags = 0;
    int s = E()->ck_who - 1, h = E()->heart[s], neat = E()->made[1];
    int hq[2] = {P(2), P(4)};
    plate_of(hq, 2);
    CHECK(mq_merge_give(q) == 1 && E()->energy == 5 && E()->made[1] == neat + 1 && strstr(q->mb_say, "So neat"));
    CHECK(E()->heart[s] >= h + 2 && mq_more(q)->mem[0].kind == MEM_MERGE);
    /* three quarters, or a third, a quarter and a sixth: right, no bonus */
    con("step 60");
    con("merge order 3 4");
    E()->ck_flags = 0;
    int tq[3] = {P(4), P(4), P(4)};
    plate_of(tq, 3);
    CHECK(mq_merge_give(q) == 1 && E()->energy == 5);
    con("step 60");
    con("merge order 3 4");
    E()->ck_flags = 0;
    int mx[3] = {L(3), P(4), L(6)};
    plate_of(mx, 3);
    CHECK(mq_merge_give(q) == 1 && E()->energy == 5);
    /* two thirds for 2/3: neat */
    con("step 60");
    con("merge order 2 3");
    E()->ck_flags = 0;
    int tt[2] = {L(3), L(3)};
    plate_of(tt, 2);
    CHECK(mq_merge_give(q) == 1 && E()->energy == 6);
    /* a present every third order: a squishy, then a plant */
    int sq = mq_squish_count(mq_home(q)), served = E()->made[0];
    for (int k = 0; k < 6; k++) {
        con("step 60");
        con("merge order 1 2");
        E()->ck_flags = 0;
        int hf[1] = {P(2)};
        plate_of(hf, 1);
        CHECK(mq_merge_give(q) == 1);
    }
    CHECK(E()->made[0] == served + 6 && E()->made[3] >= 2 && mq_squish_count(mq_home(q)) >= sq + 1);
    /* the cafe: a spare piece sits there (tips faster); a whole cake pays; seven: the oldest eaten */
    clear_counter();
    memset(E()->cafe, 0, sizeof E()->cafe);
    E()->board[0] = (uint8_t)P(8);
    CHECK(mq_merge_cafe(q, 0) == 0 && mq_cafe_treats(q) == 1 && !E()->board[0]);
    int coins = (int)mq_home(q)->coins;
    E()->board[0] = (uint8_t)P(1);
    CHECK(mq_merge_cafe(q, 0) == 0 && (int)mq_home(q)->coins == coins + 5);
    for (int k = 0; k < 6; k++)
        E()->board[k] = (uint8_t)L(6), mq_merge_cafe(q, k);
    CHECK(mq_cafe_treats(q) == 6 && E()->cafe[5] == L(6));
    /* by a drag onto the cafe */
    E()->board[4] = (uint8_t)P(4);
    draw();
    drag(cx(4), cy(4), 260, MQ_Y0 + 180);
    CHECK(!E()->board[4] && E()->cafe[5] == P(4));
    /* sparkles come from playing */
    E()->energy = 0;
    mq_prob_t pr;
    memset(&pr, 0, sizeof pr);
    pr.skill = S_GROUPS, pr.ca = 1, pr.cb = 1;
    mq_friends_right(q, &pr, true);
    CHECK(E()->energy == 1);
}

/* ------------------------------------------------------------------ the keys and the widgets */
static void test_keys_widgets(void)
{
    player();
    con("merge open");
    real_order(0);
    con("merge order 1 2");
    clear_counter();
    E()->energy = 3;
    con("step 5");
    draw();
    for (int i = 0; i < q->wg.n; i++) { /* every hit area at least MQ_WG_HIT */
        int x, y, w, h;
        mq_wg_hitbox(&q->wg.w[i], &x, &y, &w, &h);
        CHECK(w >= MQ_WG_HIT && h >= MQ_WG_HIT);
    }
    /* the knife by keys: DOWN off the counter's last row to the knives, ENTER picks the knife, UP back to the
     * counter, ENTER on the cake cuts it */
    E()->board[14] = (uint8_t)P(1);
    q->mb_cur = 14;
    key(K_DOWN);
    CHECK(focused_id() == W_KNIFE && q->wg.w[q->wg.focus].arg == 2);
    key(K_ENTER);
    CHECK(q->mb_knife == 2);
    key(K_UP);
    CHECK(focused_id() == 1 && q->mb_cur == 14);
    key(K_ENTER);
    CHECK(count(P(2)) == 2 && q->mb_knife == 2);
    /* CLEAR puts the knife down (she stays) */
    key(K_CLEAR);
    CHECK(q->mb_knife == 0 && q->scene == SC_MERGE);
    /* the oven by keys: DOWN from the first column */
    clear_counter();
    q->mb_cur = 12;
    key(K_DOWN);
    CHECK(focused_id() == W_OVEN);
    key(K_ENTER);
    CHECK(count(P(1)) == 1 && E()->energy == 2);
    key(K_UP);
    CHECK(focused_id() == 1);
    /* ENTER picks, an arrow moves, ENTER puts: a move (pieces never join) */
    clear_counter();
    E()->board[0] = E()->board[1] = (uint8_t)P(4);
    q->mb_cur = 0;
    key(K_ENTER);
    CHECK(q->mb_pick == 0);
    key(K_RIGHT);
    key(K_ENTER);
    CHECK(E()->board[0] == P(4) && E()->board[1] == P(4) && q->mb_pick == -1);
    /* CLEAR lets go of a picked piece */
    key(K_ENTER);
    CHECK(q->mb_pick == 1);
    key(K_CLEAR);
    CHECK(q->mb_pick == -1 && q->scene == SC_MERGE);
    /* picked, then RIGHT off the counter's edge to the plate, ENTER puts it there (two quarters); RIGHT to Give */
    for (int k = 0; k < 2; k++) {
        q->wg.focus = -1, q->wg.focus_id = 1; /* (back on the counter) */
        draw();
        int c = E()->board[0] ? 0 : 1;
        q->mb_cur = (uint8_t)c;
        key(K_ENTER);
        CHECK(q->mb_pick == c);
        for (int j = c; j < KC_COLS; j++)
            key(K_RIGHT);
        CHECK(focused_id() == W_PLATE);
        key(K_ENTER);
    }
    CHECK(mq_merge_plate(q) == 12 && !E()->board[0] && !E()->board[1]);
    con("step 2");
    draw();
    key(K_RIGHT);
    CHECK(focused_id() == W_GIVE);
    key(K_ENTER);
    CHECK(!E()->ck_who); /* served */
    /* the help soft key: the goal, with the hand */
    con("step 60");
    q->mb_hint_t = 0;
    key(K_F3);
    CHECK(q->mb_hint_t != 0);
    draw();
    CHECK(drawn("tap Give") || drawn("Tap the oven"));
    /* a whole cake and nothing else: the hint points at the knives */
    clear_counter();
    E()->board[3] = (uint8_t)P(1);
    q->mb_hint_t = 0;
    key(K_F3);
    draw();
    CHECK(drawn("Pick a knife"));
    /* the knife by touch: a drag from its button onto a piece cuts it; a tap picks it, a tap on a piece cuts, a tap on
     * the knife puts it down */
    int kx, ky;
    CHECK(knife_xy(4, &kx, &ky));
    drag(kx, ky, cx(3), cy(3));
    CHECK(count(P(4)) == 4 && q->mb_knife == 0);
    clear_counter();
    E()->board[3] = (uint8_t)P(1), E()->board[5] = (uint8_t)P(1);
    tap(kx, ky);
    CHECK(q->mb_knife == 4);
    draw();
    CHECK(drawn("cut it in 4") || drawn("cuts in 4"));
    tap(cx(3), cy(3));
    tap(cx(5), cy(5));
    CHECK(count(P(4)) == 8 && q->mb_knife == 4);
    tap(kx, ky);
    CHECK(q->mb_knife == 0);
    /* a tap on a piece with no knife picks it (a DOWN alone does nothing) */
    touch(TD, cx(3), cy(3));
    CHECK(q->mb_pick == -1);
    touch(TT, cx(3), cy(3));
    CHECK(q->mb_pick == 3);
    q->mb_pick = -1;
    /* a drag of a slice off the plate puts it back on the counter */
    clear_counter();
    int one[1] = {P(4)};
    plate_of(one, 1);
    draw();
    drag(px(45), py(45), cx(5), cy(5));
    CHECK(!mq_merge_plate(q) && E()->board[5] == P(4));
    /* a tap on the plate's slice takes it back too */
    plate_of(one, 1);
    draw();
    tap(px(45), py(45));
    CHECK(!mq_merge_plate(q));
    /* a drag onto the friend puts it on the plate */
    clear_counter();
    E()->board[2] = (uint8_t)P(2);
    drag(cx(2), cy(2), 230, MQ_Y0 + 50);
    CHECK(mq_merge_plate(q) == 12 && !E()->board[2]);
    /* the words: never school words */
    static const char *lines[] = {"merge order 3 4", "merge order 2 3", "merge order 5 8"};
    for (int k = 0; k < 3; k++) {
        con(lines[k]);
        draw();
        for (int i = 0; i < ntext; i++)
            CHECK(!school(texts[i]));
        CHECK(!school(q->mb_say));
        E()->board[0] = (uint8_t)P(1);
        mq_merge_cut(q, 0, 2);
        CHECK(!school(q->mb_say));
        mq_merge_cut(q, 0, 3);
        CHECK(!school(q->mb_say));
    }
}

/* ------------------------------------------------------------------ every friend at the counter: whole heads */
static void test_heads(void)
{
    player();
    con("merge open");
    real_order(2);
    int frames = 0, bad = 0;
    for (int sl = 0; sl < MQ_NCAST; sl++)
        for (int phase = 0; phase < 3; phase++) { /* waiting, thinking after a miss, the thank-you */
            con("merge order 3 4");
            E()->ck_who = (uint8_t)(sl + 1), E()->ck_flags = 0;
            if (phase == 1) {
                int one[1] = {P(4)};
                plate_of(one, 1);
                mq_merge_give(q);
            } else if (phase == 2) {
                int hq[2] = {P(2), P(4)};
                plate_of(hq, 2);
                mq_merge_give(q);
            }
            for (int f = 0; f < 30; f++) {
                con("step 3");
                if (!E()->ck_who && phase == 2 && f > 20)
                    break;
                int r[4];
                q->head_clip = 0, q->head_n = 0, q->head_watch = 1;
                ops.draw(q, 0, r);
                q->head_watch = 0;
                frames++;
                if (q->head_clip && bad++ < 10)
                    fprintf(stderr, "kitchen heads: slot %d phase %d frame %d: head at y %d\n", sl, phase, f, q->head_clip_y);
            }
        }
    printf("test_mq_kitchen: heads: %d frames, %d with a head cut off or covered\n", frames, bad);
    CHECK(frames > 200 && bad == 0);
}

/* ------------------------------------------------------------------ the save */
static void test_save(void)
{
    CHECK(sizeof(mq_extra_t) == 384);
    CHECK(offsetof(mq_extra_t, plate) + KC_PLATE + sizeof E()->reserved == 384); /* (the plate's bytes: as before) */
    player();
    mq_extra_t *e = E();
    /* an old merge board: a royal cake's worth and more, wishes, treats at the cafe */
    e->mb_seen = 1, e->energy = 7;
    e->board[0] = 0x11, e->board[1] = 0x23, e->board[29] = 0x35; /* 1 + 4 + 16 sparkles */
    e->ck_who = 3, e->ck_nd = 0x14, e->ck_rung = 0x21, e->ck_flags = 0x33; /* (the old wishes' bytes) */
    e->cafe[0] = 0x15, e->cafe[1] = 0x17;
    e->made[0] = 4;
    mq_merge_migrate(q, q->save.cur);
    CHECK(e->mb_seen == 3 && e->energy == 7 + 21 && !e->ck_who && !e->ck_nd && !e->made[0]);
    for (int i = 0; i < MB_CELLS; i++)
        CHECK(!e->board[i]);
    CHECK(e->cafe[0] == P(1) && e->cafe[1] == P(1) && mq_cafe_treats(q) == 2);
    e->energy = 9;
    mq_merge_migrate(q, q->save.cur); /* once */
    CHECK(e->energy == 9);
    /* at most 60 sparkles back */
    e->mb_seen = 1;
    for (int i = 0; i < MB_CELLS; i++)
        e->board[i] = 0x17;
    mq_merge_migrate(q, q->save.cur);
    CHECK(e->energy == 60);
    /* through the save itself: an old board loads migrated */
    e->mb_seen = 1, e->energy = 2, e->board[4] = 0x12;
    con("save");
    fresh_module();
    e = E();
    CHECK(e->mb_seen == 3 && e->energy == 4 && !e->board[4]);
    /* the merging cake counter (mb_seen 2; its codes: pink 1..4 an eighth .. a whole, lemon 1..2 a sixth, a third):
     * the same pieces of cake, its fourth row moved up, the plate up to one cake (the rest back on the counter) */
    memset(e->board, 0, sizeof e->board);
    memset(e->cafe, 0, sizeof e->cafe);
    e->mb_seen = 2, e->energy = 5;
    e->board[0] = 0x12, e->board[1] = 0x14, e->board[5] = 0x21, e->board[20] = 0x13, e->board[23] = 0x22;
    e->ck_who = 2, e->ck_nd = 3 | 4 << 4, e->ck_rung = 2, e->ck_flags = 0;
    e->plate[0] = 0x14, e->plate[1] = 0x13, e->plate[2] = 0x11; /* (a whole, a half, an eighth: too much for a plate) */
    e->cafe[0] = 0x22, e->cafe[1] = 0x14;
    con("save");
    fresh_module();
    e = E();
    CHECK(e->mb_seen == 3 && e->energy == 5 && e->ck_who == 2 && e->ck_nd == (3 | 4 << 4));
    CHECK(e->board[0] == P(4) && e->board[1] == P(1) && e->board[5] == L(6));
    CHECK(count(P(2)) == 2 && count(L(3)) == 1 && count(P(8)) == 1); /* (the row moved up, the plate's spares back) */
    for (int i = KC_CELLS; i < MB_CELLS; i++)
        CHECK(!e->board[i]);
    CHECK(e->plate[0] == P(1) && !e->plate[1] && mq_merge_plate(q) == 24);
    CHECK(e->cafe[0] == L(3) && e->cafe[1] == P(1));
    /* a full merging counter: what finds no place goes to the cafe (nothing lost) */
    memset(e->board, 0, sizeof e->board);
    memset(e->plate, 0, sizeof e->plate);
    memset(e->cafe, 0, sizeof e->cafe);
    e->mb_seen = 2;
    for (int i = 0; i < 24; i++)
        e->board[i] = 0x12;
    mq_merge_migrate(q, q->save.cur);
    CHECK(count(P(4)) == KC_CELLS && mq_cafe_treats(q) == 24 - KC_CELLS);
    /* the counter kept through a save: the order, the plate, the pieces */
    con("merge open");
    real_order(1);
    con("merge order 2 3");
    e->board[7] = (uint8_t)L(6);
    int two[2] = {L(3), L(6)};
    plate_of(two, 2);
    con("save");
    fresh_module();
    e = E();
    CHECK(e->ck_nd == (2 | 3 << 4) && e->board[7] == L(6) && e->plate[0] == L(3) && e->plate[1] == L(6));
    con("merge open");
    CHECK(q->scene == SC_MERGE && mq_merge_plate(q) == 12);
    /* its problem made again with the same identity, booked as S_PARTITION at its rung */
    int tries = mq_prof(q)->sk[S_PARTITION].tries;
    e->plate[2] = (uint8_t)L(6);
    CHECK(mq_merge_give(q) == 1 && mq_prof(q)->sk[S_PARTITION].tries == tries + 1);
    /* a damaged save never draws or keeps what no path makes */
    e->board[3] = 0x7F, e->board[25] = (uint8_t)P(2), e->plate[0] = 0x55, e->cafe[2] = 0x99, e->ck_who = 99;
    e->plate[1] = (uint8_t)P(1), e->plate[2] = (uint8_t)P(1);
    con("go map");
    con("merge open");
    CHECK(!e->board[3] && !e->board[25] && !e->cafe[2] && e->ck_who <= MQ_NCAST && mq_merge_plate(q) <= 24);
    draw();
}

int main(void)
{
    harness_init();
    cap_api = modx_api;
    cap_api.text = log_text;
    test_pieces();
    test_cutting();
    test_full_counter();
    test_orders();
    test_judging();
    test_plate();
    test_first_visit();
    test_no_giveaway();
    test_fair_and_rewards();
    test_keys_widgets();
    test_heads();
    test_save();
    printf("test_mq_kitchen: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
