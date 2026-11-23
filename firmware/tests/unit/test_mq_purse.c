/* Sparkle Town (modules/mathquest) the purse and paying at the counter (mq_purse.c):
 *   - the fewest coins: greedy is the fewest for US coins (brute force, every amount to $10); from a
 *     handful, the search is the fewest (brute force over every sub-handful);
 *   - the handful composed for a purchase sums to the balance, pays the price exactly, has the piece to
 *     pay over with when making change, and never holds more than MQ_PURSE_COINS coins;
 *   - the balance is conserved: earnings, a tap purchase, a counter purchase with change, the swap;
 *   - the level gates: a tap before coins; counting coins; making change (taught, quarters, room for it);
 *     the picky cashier once exact paying is solid;
 *   - the flows: exact pay, under / over (a hint, then help by itself, then counted out together and
 *     bought: never stuck), "help me pay" (the working set ringed, wrong coins slide back), change
 *     right and wrong, the picky cashier (the fewest: a bonus sparkle; too many: a grumble, a swap, and
 *     still bought), CLEAR putting the coins back;
 *   - the engine: one answer a purchase (the skill's tries, the problem's history, the review, the rung);
 *   - the save: an older save's coins and stars into the purse, an older module's spending taken over;
 *   - the widgets: a tap puts a coin down, a tap on the counter takes it back, a drag, the arrows and
 *     ENTER, the change keys typed; every hit area at least MQ_WG_HIT; nothing said in school words. */
#include "mq_test_util.h"

enum { K_ENTER = 49, K_DEL = 8, K_LEFT = 13, K_RIGHT = 14, K_CLEAR_KEY = 20, K_NINE = 34 };
static const char *const banned[] = {"math", "learn", "lesson", "homework", "grade", "quiz", "test", "skill", "problem",
                                     "level", "mistake", "worksheet", "correct", "fact", "practice", "review", NULL};
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

static char st_key[48], st_val[96];
static void st_kv(const char *k, const char *v, void *u)
{
    (void)u;
    if (!strcmp(k, st_key))
        snprintf(st_val, sizeof st_val, "%s", v);
}
static const char *state(const char *k)
{
    snprintf(st_key, sizeof st_key, "%s", k);
    st_val[0] = 0;
    ops.state(q, st_kv, NULL);
    return st_val;
}
static void draw(void)
{
    int r[4];
    ops.draw(q, 0, r);
}
static void tap(int x, int y) { ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(4, x, y)); }
static void touch(int type, int x, int y) { ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(type, x, y)); }
static void key(int k) { ops.event(q, MODX_EV_KEY, k); }

static int sum(const uint8_t *c) { return mq_coins_sum(c); }

/* ------------------------------------------------------------------ the solvers */
static void test_solvers(void)
{
    /* unlimited: the fewest by brute force (every count of every piece) against greedy, to $10 */
    static int16_t best[1001];
    best[0] = 0;
    for (int c = 1; c <= 1000; c++) {
        best[c] = 9999;
        for (int k = 0; k < DN_COUNT; k++)
            if (mq_dn_value[k] <= c && best[c - mq_dn_value[k]] + 1 < best[c])
                best[c] = (int16_t)(best[c - mq_dn_value[k]] + 1);
    }
    for (int c = 0; c <= 1000; c++) {
        uint8_t g[DN_COUNT];
        mq_coins_greedy(c, g);
        CHECK(sum(g) == c);
        CHECK(mq_coins_count(g) == best[c]);
    }
    /* from a handful: every sub-handful of random handfuls */
    uint32_t r = 12345;
    for (int t = 0; t < 400; t++) {
        uint8_t have[DN_COUNT], out[DN_COUNT];
        for (int k = 0; k < DN_COUNT; k++)
            r = r * 1103515245u + 12345u, have[k] = (uint8_t)((r >> 16) % (k >= DN_DOLLAR ? 3 : 6));
        r = r * 1103515245u + 12345u;
        int target = (int)((r >> 16) % 300);
        int bf = 9999;
        uint8_t c[DN_COUNT];
        for (c[5] = 0; c[5] <= have[5]; c[5]++)
            for (c[4] = 0; c[4] <= have[4]; c[4]++)
                for (c[3] = 0; c[3] <= have[3]; c[3]++)
                    for (c[2] = 0; c[2] <= have[2]; c[2]++)
                        for (c[1] = 0; c[1] <= have[1]; c[1]++)
                            for (c[0] = 0; c[0] <= have[0]; c[0]++)
                                if (sum(c) == target && mq_coins_count(c) < bf)
                                    bf = mq_coins_count(c);
        int n = mq_coins_fewest(target, have, out);
        if (bf == 9999)
            CHECK(n == -1 && !mq_coins_payable(target, have));
        else {
            CHECK(n == bf);
            CHECK(sum(out) == target && mq_coins_count(out) == n);
            for (int k = 0; k < DN_COUNT; k++)
                CHECK(out[k] <= have[k]);
        }
    }
    /* greedy fails from a handful, the search does not: 30c from a quarter and three dimes */
    uint8_t h30[DN_COUNT] = {0, 0, 3, 1, 0, 0}, o[DN_COUNT];
    CHECK(mq_coins_fewest(30, h30, o) == 3 && o[DN_DIME] == 3 && o[DN_QUARTER] == 0);
}

/* ------------------------------------------------------------------ composing the handful */
static void test_compose(void)
{
    uint32_t r = 777;
    int most = 0;
    for (int t = 0; t < 20000; t++) {
        r = r * 1103515245u + 12345u;
        int bal = (int)((r >> 8) % 3000), mode = (int)((r >> 4) % 4), rung = (int)((r >> 20) % 4);
        r = r * 1103515245u + 12345u;
        int price = 1 + (int)((r >> 8) % 120), over = price < 100 ? DN_DOLLAR : DN_FIVE;
        if (t % 7 == 0)
            price = 1 + (int)((r >> 12) % 600);
        uint8_t c[DN_COUNT];
        mq_purse_compose(bal, price, mode, rung, over, r >> 3, c);
        CHECK(sum(c) == bal);                              /* conserves the balance */
        CHECK(mq_coins_only(c) <= MQ_PURSE_COINS);         /* bounded: never a heap of pennies */
        if (mq_coins_only(c) > most)
            most = mq_coins_only(c);
        if (price <= bal) {
            CHECK(mq_coins_payable(price, c));             /* exact payment always possible */
            if (mode == PM_CHANGE && bal >= price + mq_dn_value[over] && mq_dn_value[over] > price) /* the piece to pay over with */
                CHECK(c[over] >= 1 && mq_dn_value[over] > price);
            /* the price in the rung's coins: no quarters below rung 2, no nickels at rung 0 (with no rest) */
            if (bal == price && price < 100) {
                if (rung < 2)
                    CHECK(c[DN_QUARTER] == 0);
                if (rung == 0)
                    CHECK(c[DN_NICKEL] == 0);
            }
        }
    }
    CHECK(most > 10); /* (and a real handful) */
}

/* ------------------------------------------------------------------ a player at the shop */
static void player(void)
{
    fresh_module();
    con("new Ada 3");
    q->fun_off = true;
    con("fun 36000");
    mq_prof(q)->days = mq_prof(q)->days ? mq_prof(q)->days : 1; /* (the day's quests all done: a purchase pays nothing extra) */
    mq_quest_roll(q);
    mq_home(q)->quest_done = 7;
}
/* an item on sale she can buy, priced lo..hi (0 none) */
static int item_priced(int lo, int hi, bool nickels)
{
    for (int id = 1; id <= MQ_NITEM; id++)
        if (mq_item[id].price >= lo && mq_item[id].price <= hi && !mq_buy_check(q, id) && !mq_item_owned(q, id) &&
            (!nickels || mq_item[id].price % 5 == 0) && !(mq_item[id].flags & (F_GIFT | F_SPECIAL | F_RETIRED)))
            return id;
    return 0;
}
static void coins_on(int rung)
{
    mq_skrec_t *s = &mq_prof(q)->sk[S_COINS];
    s->tries = s->tries ? s->tries : 1, s->diff = (uint8_t)rung;
}
static void open_item(int id)
{
    char b[32];
    snprintf(b, sizeof b, "pay open %d", id);
    CHECK(con(b) == 0);
}
static void conserved(void)
{
    mq_home_t *h = mq_home(q);
    CHECK(sum(h->hand) == (int)h->purse);
    CHECK(h->coins == (h->purse > HM_COINS_MAX ? HM_COINS_MAX : h->purse));
    if (q->pay.on && q->pay.phase == PP_PAY) /* while paying: her purse and the counter are the balance */
        CHECK(sum(q->pay.hand) + sum(q->pay.counter) == (int)h->purse);
}
/* put exactly the price on the counter, the fewest coins (fewest) or the most (not) */
static void put_exact(bool fewest)
{
    uint8_t ws[DN_COUNT];
    if (fewest)
        CHECK(mq_coins_fewest(q->pay.price, q->pay.hand, ws) >= 0);
    else { /* the most coins: small ones first */
        int rem = q->pay.price;
        for (int k = 0; k < DN_COUNT; k++)
            ws[k] = 0;
        for (int k = 0; k < DN_COUNT; k++)
            while (ws[k] < q->pay.hand[k] && rem >= mq_dn_value[k] &&
                   mq_coins_payable(rem - mq_dn_value[k], (uint8_t[DN_COUNT]){
                       (uint8_t)(q->pay.hand[0] - (k == 0 ? ws[0] + 1 : ws[0])), (uint8_t)(q->pay.hand[1] - (k == 1 ? ws[1] + 1 : ws[1])),
                       (uint8_t)(q->pay.hand[2] - (k == 2 ? ws[2] + 1 : ws[2])), (uint8_t)(q->pay.hand[3] - (k == 3 ? ws[3] + 1 : ws[3])),
                       (uint8_t)(q->pay.hand[4] - (k == 4 ? ws[4] + 1 : ws[4])), (uint8_t)(q->pay.hand[5] - (k == 5 ? ws[5] + 1 : ws[5]))}))
                ws[k]++, rem -= mq_dn_value[k];
        CHECK(rem == 0);
    }
    static const char ab[DN_COUNT][3] = {"p", "n", "d", "q", "1", "5"};
    for (int k = 0; k < DN_COUNT; k++)
        for (int n = 0; n < ws[k]; n++) {
            char b[16];
            snprintf(b, sizeof b, "pay put %s", ab[k]);
            con(b);
        }
    CHECK(sum(q->pay.counter) == q->pay.price);
}

static void test_gates(void)
{
    player();
    con("coins 900");
    int id = item_priced(20, 40, true);
    CHECK(id > 0);
    int price = mq_item[id].price;
    CHECK(mq_pay_mode(q, price) == PM_TAP); /* a new player: a tap */
    coins_on(0);
    CHECK(mq_pay_mode(q, price) == PM_COUNT); /* counting coins reached */
    mq_home_t *h = mq_home(q);
    h->pay_streak = 4;
    CHECK(mq_pay_mode(q, price) == PM_COUNT); /* (no quarters yet: no picky cashier) */
    coins_on(2);
    h->pay_n = 1;
    CHECK(mq_pay_mode(q, price) == PM_PICKY); /* exact paying solid, quarters: the picky cashier takes turns */
    h->pay_n = 2;
    CHECK(mq_pay_mode(q, price) == PM_COUNT);
    h->pay_streak = 0;
    mq_skrec_t *ch = &mq_prof(q)->sk[S_CHANGE];
    ch->tries = 1;
    h->pay_n = 0;
    CHECK(mq_pay_mode(q, price) == PM_CHANGE); /* making change reached */
    h->pay_n = 1;
    CHECK(mq_pay_mode(q, price) == PM_COUNT);
    h->pay_n = 0;
    con("school 3 10"); /* school has not taught it yet (grade 4): not asked */
    CHECK(mq_pay_mode(q, price) == PM_COUNT);
    con("school 5 1");
    CHECK(mq_pay_mode(q, price) == PM_CHANGE);
    con("coins 60"); /* no room to pay over */
    CHECK(mq_pay_mode(q, price) == PM_COUNT);
    con("coins 900");
    coins_on(1); /* change wants quarters solid */
    CHECK(mq_pay_mode(q, price) == PM_COUNT);
    coins_on(2);
    CHECK(mq_pay_mode(q, price + 1000) == PM_TAP); /* (more than she has: the card says how much more) */
}

static void test_count_flow(void)
{
    player();
    con("coins 300");
    coins_on(0);
    int id = item_priced(12, 40, false);
    CHECK(id > 0);
    int price = mq_item[id].price;
    uint32_t bal0 = mq_purse_cents(q);
    int tries0 = mq_prof(q)->sk[S_COINS].tries;
    open_item(id);
    CHECK(q->pay.on && q->pay.mode == PM_COUNT && q->pay.phase == PP_PAY);
    CHECK(q->scene == SC_SHOP);
    CHECK(!strcmp(state("mq_pay"), "count pay"));
    CHECK(strstr(state("mq_pay_say"), "That's") != NULL);
    CHECK(q->pay.hand[DN_QUARTER] == 0 || q->pay.price > 25 || 1); /* (rung 0: the price in dimes and pennies) */
    conserved();
    draw();
    /* every hit area big enough; the counter shows no running total */
    for (int i = 0; i < q->wg.n; i++) {
        int hx, hy, hw, hh;
        mq_wg_hitbox(&q->wg.w[i], &hx, &hy, &hw, &hh);
        CHECK(hw >= MQ_WG_HIT && hh >= MQ_WG_HIT);
    }
    put_exact(false);
    conserved();
    con("pay go");
    CHECK(q->pay.phase == PP_THANKS);
    CHECK(mq_item_owned(q, id) == 1);
    CHECK(mq_purse_cents(q) == bal0 - (uint32_t)price);
    conserved();
    CHECK(mq_prof(q)->sk[S_COINS].tries == tries0 + 1); /* one answer of counting coins */
    CHECK(mq_learn(q)->rev[S_COINS].box > 0);            /* it joined the review */
    CHECK(mq_home(q)->pay_streak == 1);
    con("pay done");
    CHECK(!q->pay.on && q->shop_pick == id); /* the card again: place it */

    /* a miss: a hint; the second: help by itself (the wrong coins slide back); the third: counted out, bought */
    id = item_priced(12, 40, false);
    price = mq_item[id].price;
    bal0 = mq_purse_cents(q);
    open_item(id);
    con("pay put p");
    con("pay go");
    CHECK(q->pay.misses == 1 && q->pay.phase == PP_PAY && !q->pay.helped);
    CHECK(mq_home(q)->pay_streak == 0); /* (the miss was the purchase's answer) */
    for (int k = 0; k < DN_COUNT; k++) /* everything she has on the counter: too much */
        while (q->pay.hand[k])
            con(k == 0 ? "pay put p" : k == 1 ? "pay put n" : k == 2 ? "pay put d" : k == 3 ? "pay put q" : k == 4 ? "pay put 1" : "pay put 5");
    conserved();
    con("pay go");
    CHECK(q->pay.misses == 2 && q->pay.helped);
    CHECK(sum(q->pay.counter) <= price);                    /* the wrong ones went back */
    CHECK(sum(q->pay.counter) + sum(q->pay.help) == price); /* the ringed ones make the rest */
    conserved();
    con("pay take p"); /* (and still wrong) */
    con("pay go");
    CHECK(q->pay.misses == 3);
    CHECK(q->pay.phase == PP_THANKS && mq_item_owned(q, id) == 1); /* never stuck: bought */
    CHECK(mq_purse_cents(q) == bal0 - (uint32_t)price);
    conserved();
    con("pay done");

    /* "help me pay" at once: the working set; then paying it is right but not a first try (no give-away) */
    id = item_priced(12, 40, false);
    open_item(id);
    con("pay help");
    CHECK(q->pay.helped && sum(q->pay.help) == q->pay.price);
    int streak = mq_home(q)->pay_streak;
    for (int k = 0; k < DN_COUNT; k++)
        while (q->pay.help[k])
            con(k == 0 ? "pay put p" : k == 1 ? "pay put n" : k == 2 ? "pay put d" : k == 3 ? "pay put q" : k == 4 ? "pay put 1" : "pay put 5");
    con("pay go");
    CHECK(q->pay.phase == PP_THANKS && mq_item_owned(q, id) == 1);
    CHECK(mq_home(q)->pay_streak == 0 && streak == 0);
    con("pay done");

    /* CLEAR: the coins back, then the counter closes, nothing bought, nothing lost */
    id = item_priced(12, 40, false);
    bal0 = mq_purse_cents(q);
    open_item(id);
    con("pay put d");
    key(K_CLEAR_KEY);
    CHECK(q->pay.on && sum(q->pay.counter) == 0);
    key(K_CLEAR_KEY);
    CHECK(!q->pay.on && q->scene == SC_SHOP && mq_item_owned(q, id) == 0 && mq_purse_cents(q) == bal0);
    conserved();
}

static void test_change_flow(void)
{
    player();
    con("coins 500");
    coins_on(2);
    mq_prof(q)->sk[S_CHANGE].tries = 1;
    int id = item_priced(12, 45, true);
    CHECK(id > 0);
    int price = mq_item[id].price;
    uint32_t bal0 = mq_purse_cents(q);
    open_item(id);
    CHECK(q->pay.mode == PM_CHANGE && q->pay.over == DN_DOLLAR && q->pay.hand[DN_DOLLAR] >= 1);
    int ctries = mq_prof(q)->sk[S_CHANGE].tries;
    con("pay put 1");
    con("pay put 1"); /* two dollars for less than one: too much */
    con("pay go");
    CHECK(q->pay.phase == PP_PAY && q->pay.misses == 1);
    con("pay take 1");
    con("pay go");
    CHECK(q->pay.phase == PP_CHANGE && q->pay.paid == 100);
    /* the change typed on the keys: wrong first ("count up"), then right */
    draw();
    key(K_NINE); /* (a digit through the widget keys: the keyboard) */
    CHECK(q->pay.change_in == 9);
    key(K_DEL);
    CHECK(q->pay.change_in == 0);
    con("pay change 1");
    CHECK(q->pay.change_misses == 1 && q->pay.phase == PP_CHANGE);
    CHECK(strstr(state("mq_pay_say"), "Count up") != NULL);
    char b[32];
    snprintf(b, sizeof b, "pay change %d", 100 - price);
    con(b);
    CHECK(q->pay.phase == PP_THANKS && mq_item_owned(q, id) == 1);
    CHECK(mq_purse_cents(q) == bal0 - (uint32_t)price);
    CHECK(mq_prof(q)->sk[S_CHANGE].tries == ctries + 1); /* one answer of making change (the first: wrong) */
    conserved();
    con("pay done");

    /* right first time; and the second wrong change is counted out (bought anyway) */
    mq_home(q)->pay_n = 0;
    id = item_priced(12, 45, true);
    price = mq_item[id].price;
    open_item(id);
    CHECK(q->pay.mode == PM_CHANGE);
    con("pay put 1");
    con("pay go");
    con("pay change 3");
    con("pay change 4");
    CHECK(q->pay.phase == PP_THANKS && mq_item_owned(q, id) == 1);
    conserved();
    con("pay done");
    /* paying exactly in change mode is fine too: counting coins, right */
    mq_home(q)->pay_n = 0;
    id = item_priced(12, 45, true);
    open_item(id);
    put_exact(true);
    con("pay go");
    CHECK(q->pay.phase == PP_THANKS && q->pay.skill == S_COINS);
    conserved();
}

/* the dear things ($1 to $10): there, on sale from the start, paid with bills and coins at every rung,
 * a $1..$4.99 thing paid over with a $5 (change), a $5 or dearer one counted out */
static void test_dear_things(void)
{
    player();
    int dear = 0, five = 0, top = 0;
    for (int id = 1; id <= MQ_NITEM; id++) {
        int p = mq_item[id].price;
        if (p < 100 || (mq_item[id].flags & (F_GIFT | F_SPECIAL | F_RETIRED)))
            continue;
        dear++, five += p >= 500, top = p > top ? p : top;
        CHECK(p <= 1000 && mq_item_unlocked(mq_prof(q), id)); /* (reachable, on sale from the start) */
        for (int rung = 0; rung < 4; rung++)
            for (int mode = 0; mode < 4; mode++) {
                uint8_t c[DN_COUNT];
                int bal = p + 37 + rung * 211, over = p < 100 ? DN_DOLLAR : DN_FIVE;
                mq_purse_compose(bal, p, mode, rung, over, (uint32_t)(id * 7 + rung), c);
                CHECK(sum(c) == bal && mq_coins_only(c) <= MQ_PURSE_COINS && mq_coins_payable(p, c));
                if (mode == PM_CHANGE && p < 500 && bal >= p + 500)
                    CHECK(c[DN_FIVE] >= 1);
            }
    }
    CHECK(dear >= 12 && five >= 2 && top == 1000);
    /* paying for a $4.50 bed: four dollars and two quarters, counted out (no total shown) */
    con("coins 2000");
    coins_on(3);
    open_item(IT_CANOPY_BED);
    CHECK(q->pay.on && q->pay.mode == PM_COUNT && q->pay.price == 450);
    draw();
    put_exact(true);
    con("pay go");
    CHECK(q->pay.phase == PP_THANKS && mq_item_owned(q, IT_CANOPY_BED) == 1 && mq_purse_cents(q) == 1550);
    conserved();
    con("pay done");
    /* making change from $5 for a $3.75 piano; a $10 castle is never paid over (no bill for it) */
    mq_prof(q)->sk[S_CHANGE].tries = 1, mq_prof(q)->sk[S_CHANGE].diff = 3;
    mq_home(q)->pay_n = 0;
    CHECK(mq_pay_mode(q, mq_item[IT_CASTLE_TOY].price) != PM_CHANGE);
    open_item(IT_PIANO_PINK);
    CHECK(q->pay.mode == PM_CHANGE && q->pay.over == DN_FIVE && q->pay.hand[DN_FIVE] >= 1);
    con("pay put 5");
    con("pay go");
    CHECK(q->pay.phase == PP_CHANGE && q->pay.paid == 500);
    con("pay change 125");
    CHECK(q->pay.phase == PP_THANKS && mq_item_owned(q, IT_PIANO_PINK) == 1 && mq_purse_cents(q) == 1550 - 375);
    CHECK(strstr(state("mq_pay_say"), "0 cents") == NULL);
    conserved();
    con("pay done");
}

static void test_picky(void)
{
    player();
    con("coins 400");
    coins_on(3);
    mq_home_t *h = mq_home(q);
    int bonus = 0, grumble = 0, swaps = 0;
    for (int t = 0; t < 24; t++) {
        h->pay_streak = 5, h->pay_n = 1;
        int id = item_priced(10, 50, false);
        if (!id) {
            con("coins 400");
            for (int k = 1; k <= MQ_NITEM; k++)
                h->own[k] = 0;
            id = item_priced(10, 50, false);
        }
        open_item(id);
        CHECK(q->pay.mode == PM_PICKY);
        CHECK(!school(q->pay.say));
        uint8_t e0 = mq_extra(q)->energy;
        if (t % 2 == 0) { /* the fewest: a bonus sparkle */
            put_exact(true);
            con("pay go");
            CHECK(q->pay.phase == PP_THANKS && q->pay.bonus == 1);
            CHECK(mq_extra(q)->energy == (e0 + 3 > 60 ? 60 : e0 + 3));
            bonus++;
        } else { /* as many coins as she can: (most often) a grumble, a swap, and she still gets it */
            put_exact(false);
            int n = mq_coins_count(q->pay.counter), best;
            uint8_t all[DN_COUNT];
            for (int k = 0; k < DN_COUNT; k++)
                all[k] = (uint8_t)(q->pay.hand[k] + q->pay.counter[k]);
            best = mq_coins_fewest(q->pay.price, all, NULL);
            bool nopenny = q->pay.rule == PK_NOPENNY && q->pay.counter[DN_PENNY] == 0;
            con("pay go");
            CHECK(mq_item_owned(q, id) >= 1);
            if (n > best && !nopenny && !(q->pay.rule == PK_MAXN && n <= q->pay.maxn)) {
                grumble++;
                CHECK(q->pay.mood == 1 && q->pay.bonus == 0);
                CHECK(h->picky_ok == 0);
                if (q->pay.phase == PP_SWAP) {
                    uint32_t bal = mq_purse_cents(q);
                    int from = q->pay.swap_from, to = q->pay.swap_to, nf = h->hand[from], nt = h->hand[to];
                    CHECK(q->pay.swap_n * mq_dn_value[from] == mq_dn_value[to]); /* a fair swap */
                    con("pay swap");
                    CHECK(h->hand[from] == nf - q->pay.swap_n && h->hand[to] == nt + 1);
                    CHECK(mq_purse_cents(q) == bal); /* the balance is the same */
                    swaps++;
                }
            }
        }
        conserved();
        con("pay done");
    }
    printf("picky: %d bonus, %d grumbles, %d swaps\n", bonus, grumble, swaps);
    CHECK(bonus >= 10 && grumble >= 3 && swaps >= 1);
}

/* the widgets: taps, a drag, the arrows and ENTER */
static void test_widgets(void)
{
    player();
    con("coins 300");
    coins_on(0);
    int id = item_priced(12, 40, false);
    open_item(id);
    draw();
    int dime = -1;
    for (int i = 0; i < q->wg.n; i++)
        if (q->wg.w[i].kind == WG_DRAG && !strcmp(q->wg.w[i].label, "dime") && !(q->wg.w[i].flags & WF_DISABLED))
            dime = i;
    CHECK(dime >= 0);
    mq_wg_t g = q->wg.w[dime];
    int n0 = q->pay.hand[DN_DIME];
    tap(g.x + g.w / 2, g.y + g.h / 2); /* a tap: onto the counter */
    CHECK(q->pay.counter[DN_DIME] == 1 && q->pay.hand[DN_DIME] == n0 - 1);
    CHECK(q->wg.picked == -1);
    draw();
    int back = -1;
    for (int i = 0; i < q->wg.n; i++)
        if (q->wg.w[i].kind == WG_DRAG && q->wg.w[i].y < MQ_Y0 + 106 && !strcmp(q->wg.w[i].label, "dime"))
            back = i;
    CHECK(back >= 0);
    g = q->wg.w[back];
    tap(g.x + g.w / 2, g.y + g.h / 2); /* a tap on the counter's dime: back in the purse */
    CHECK(q->pay.counter[DN_DIME] == 0 && q->pay.hand[DN_DIME] == n0);
    draw();
    g = q->wg.w[dime];
    touch(1, g.x + g.w / 2, g.y + g.h / 2); /* a drag up onto the counter */
    for (int k = 1; k <= 6; k++)
        touch(2, g.x + g.w / 2, g.y + g.h / 2 - k * 12);
    draw();
    touch(3, g.x + g.w / 2, g.y + g.h / 2 - 72);
    CHECK(q->pay.counter[DN_DIME] == 1);
    /* keys: the arrows walk the coins, ENTER puts the one in focus down */
    draw();
    int c0 = mq_coins_count(q->pay.counter);
    key(K_RIGHT);
    draw();
    key(K_ENTER);
    CHECK(mq_coins_count(q->pay.counter) == c0 + 1 || mq_coins_count(q->pay.counter) == c0 - 1 || q->pay.misses);
    conserved();
    con("pay back");
    con("pay back");
    CHECK(!q->pay.on);
    /* a tap purchase below the money level: the old way, one tap */
    player();
    con("coins 300");
    id = item_priced(12, 40, false);
    q->scene = SC_SHOP, q->shop_pick = (uint8_t)id;
    mq_press(q, HB_BUY, id);
    CHECK(!q->pay.on && mq_item_owned(q, id) == 1);
    conserved();
}

/* earnings: the coins that fly in make the reward; the handful stays a handful */
static void test_earn(void)
{
    player();
    uint32_t b0 = mq_purse_cents(q);
    for (int i = 0; i < 300; i++)
        mq_earn(q, 1 + i % 7, NULL);
    int tot = 0;
    for (int i = 0; i < 300; i++)
        tot += 1 + i % 7;
    CHECK(mq_purse_cents(q) == b0 + (uint32_t)tot);
    CHECK(mq_coins_only(mq_home(q)->hand) <= MQ_PURSE_COINS);
    conserved();
    for (int i = 0; i < 8; i++)
        q->fly[i].t0 = 0;
    mq_earn(q, 37, NULL); /* a quarter, a dime and two pennies fly in */
    int fly = 0;
    for (int i = 0; i < 8; i++)
        if (q->fly[i].t0)
            fly += mq_dn_value[q->pay.fly_dn[i]];
    CHECK(fly >= 37);
    mq_purse_set(q, 59990);
    mq_earn(q, 50, NULL); /* (capped) */
    CHECK(mq_purse_cents(q) == HM_COINS_MAX);
    conserved();
    CHECK(!strcmp(state("mq_coins"), "60000") && !strcmp(state("mq_purse_shown"), "$600.00"));
}

/* the save: an older save's coins and stars into the purse; an older module's spending taken over */
static void test_migrate(void)
{
    player();
    mq_home_t *h = mq_home(q);
    mq_profile_t *p = mq_prof(q);
    h->purse_ver = 0, h->purse = 0, h->purse_mirror = 0;
    for (int k = 0; k < DN_COUNT; k++)
        h->hand[k] = 0;
    h->coins = 123, p->stars = 41;
    mq_purse_migrate(q, q->save.cur);
    CHECK(h->purse == 123 + 10 && h->purse_ver == 1 && h->coins == 133 && h->purse_mirror == 133);
    conserved();
    mq_purse_migrate(q, q->save.cur); /* once */
    CHECK(h->purse == 133);
    h->coins = 100; /* an older module spent 33 */
    mq_purse_migrate(q, q->save.cur);
    CHECK(h->purse == 100 && h->purse_mirror == 100);
    h->coins = 160; /* ... and earned 60 */
    mq_purse_migrate(q, q->save.cur);
    CHECK(h->purse == 160);
    conserved();
    /* through the save itself: a version-6 save from before the purse loads into the purse */
    h->purse_ver = 0, h->coins = 77, p->stars = 8;
    con("save");
    fresh_module();
    CHECK(mq_home(q)->purse == 79 && mq_home(q)->purse_ver == 1);
    CHECK(mq_coins_sum(mq_home(q)->hand) == 79);
    CHECK(sizeof(mq_home_t) == 1024);
}

int main(void)
{
    harness_init();
    test_solvers();
    test_compose();
    test_gates();
    test_count_flow();
    test_change_flow();
    test_dear_things();

    test_picky();
    test_widgets();
    test_earn();
    test_migrate();
    printf("test_mq_purse: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
