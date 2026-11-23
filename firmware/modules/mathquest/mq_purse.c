/*
 * Math Quest: the purse -- coins are the one currency she earns, and paying at the shop's counter is
 * money practice (docs/MATHQUEST.md "Home: coins, the purse, the shop").
 *
 * The balance. Kept in cents (mq_home_t.purse; `coins` mirrors it, capped, so an older module still
 * reads it, and what an older module earned or spent since is taken over: purse_mirror). A coin earned
 * is a cent: a right answer 3c, a skill mastered 25c, the shop's things 4c..50c, its dear things $1..$10. An older save's coins
 * and a quarter of its stars (stars were a score, never spent) become the balance once (mq_purse_migrate).
 *
 * The handful. The balance is made of pennies, nickels, dimes, quarters, $1 and $5 bills
 * (mq_home_t.hand, always summing to the balance). Before every purchase it is composed afresh, quietly
 * (never while she pays), to suit the price and what she practises (mq_purse_compose):
 *   - the price can always be paid exactly, in the coins of her rung of "Count coins" (dimes and
 *     pennies; with nickels; with quarters; with dollars), now and then with a coin broken into smaller
 *     ones so there is something to think about;
 *   - for paying over (making change) the bill or coin to pay with is there too (a $1 bill for a price
 *     under a dollar, a $5 bill above);
 *   - the rest of the balance as the fewest pieces: so a handful has at most MQ_PURSE_COINS coins
 *     (never 200 pennies), the $5 bills apart (a stack).
 * Earnings drop into it as coins that make the reward (they fly into the purse); too many coins
 * and it is tidied (the fewest pieces) between purchases.
 *
 * Paying (mq_pay_mode, the counter):
 *   PM_TAP     before she counts coins (Count coins not reached: not played, not placed above): "buy!"
 *              is a tap, as always;
 *   PM_COUNT   she drags (or taps) coins from her purse onto the counter ("That's 37c!"), taps a
 *              counter coin to take it back, the arrows and ENTER do it too; "pay!" checks. The counter
 *              never shows a running total (that would count for her). A miss: a hint; the second:
 *              "help me pay" by itself; the third: the cashier counts it out with her and it is bought
 *              anyway (never blocked). "Help me pay" rings a working set and slides the wrong coins back;
 *   PM_CHANGE  ("Making change" reached, taught at school, quarters solid) she pays over -- the $1 bill
 *              for a 37c thing -- and types the change on the counter's keys ($0.63); a miss: "count up
 *              from 37c"; the second: the cashier counts the change out. The change drops into her purse;
 *   PM_PICKY   (exact paying solid: four right first time in a row, quarters) the picky cashier wants the
 *              fewest coins, at most N coins, or "no pennies today!". Paid right but with too many: he
 *              grumbles (cute, never cross), offers a swap from her purse (5 pennies for a nickel ...)
 *              and she still gets it; the fewest coins earn a bonus sparkle (kitchen energy).
 * The modes take turns between purchases. Each purchase is one answer of its skill (Count coins or
 * Making change) for the engine: the problem's history and review (mq_learn.c), the success window
 * and her rung (mq_coach.c, mq_rung.c), the skill's mastery; help or a miss counts as one, so a rung
 * never climbs on help (no give-aways).
 *
 * The fewest coins: US coins and bills are a canonical system, so the greedy way is the fewest when
 * there are enough of each (mq_coins_greedy); from a given handful it is a small exact search
 * (mq_coins_fewest). tests/unit/test_mq_purse.c checks both against brute force.
 */
#include "mq.h"

_Static_assert(sizeof(mq_home_t) == 1024, "the home block's size is part of the save format");

enum { HK_DEL = 8, HK_UP = 12, HK_LEFT = 13, HK_RIGHT = 14, HK_DOWN = 15, HK_ENTER = 49 };
enum { TV_DOWN = 1, TV_DRAG, TV_UP, TV_TAP, TV_PINCH, TV_PINCH_END, TV_CANCEL };
/* the counter's widgets (ids) */
enum { PW_COIN = 1, PW_CCOIN, PW_PAY, PW_HELP, PW_KEY, PW_DEL, PW_OK, PW_SWAP, PW_NOSWAP, PW_DONE };

const uint16_t mq_dn_value[DN_COUNT] = {1, 5, 10, 25, 100, 500};
static const char dn_sprite[DN_COUNT][14] = {"money/penny", "money/nickel", "money/dime", "money/quarter", "money/bill1",
                                             "money/bill5"};
static const char dn_name[DN_COUNT][8] = {"penny", "nickel", "dime", "quarter", "dollar", "five"};
static const char dn_names[DN_COUNT][10] = {"pennies", "nickels", "dimes", "quarters", "dollars", "fives"};

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static void zero(uint8_t c[DN_COUNT])
{
    for (int k = 0; k < DN_COUNT; k++)
        c[k] = 0;
}
static void copy(uint8_t d[DN_COUNT], const uint8_t s[DN_COUNT])
{
    for (int k = 0; k < DN_COUNT; k++)
        d[k] = s[k];
}

/* ------------------------------------------------------------------ counting and the fewest coins */
int mq_coins_sum(const uint8_t c[DN_COUNT])
{
    int s = 0;
    for (int k = 0; k < DN_COUNT; k++)
        s += c[k] * mq_dn_value[k];
    return s;
}
int mq_coins_count(const uint8_t c[DN_COUNT])
{
    int n = 0;
    for (int k = 0; k < DN_COUNT; k++)
        n += c[k];
    return n;
}
int mq_coins_only(const uint8_t c[DN_COUNT]) { return c[DN_PENNY] + c[DN_NICKEL] + c[DN_DIME] + c[DN_QUARTER]; }

/* the fewest pieces with any number of each: the greedy way (US coins are canonical) */
void mq_coins_greedy(int cents, uint8_t out[DN_COUNT])
{
    zero(out);
    if (cents < 0)
        cents = 0;
    for (int k = DN_COUNT - 1; k >= 0; k--) {
        int n = cents / mq_dn_value[k];
        out[k] = (uint8_t)imin(n, 255);
        cents -= out[k] * mq_dn_value[k];
    }
}

/* the fewest of `have` that make exactly `cents`: a search from the biggest piece down, cut off as soon as
 * it cannot beat the best so far (at most a few thousand steps for a handful) */
static void fewest_dfs(int k, int rem, int used, const uint8_t *have, uint8_t *cur, int *best, uint8_t *out)
{
    if (rem == 0) {
        if (used < *best) {
            *best = used;
            for (int i = 0; i < DN_COUNT; i++)
                out[i] = i > k ? cur[i] : 0;
        }
        return;
    }
    if (k < 0)
        return;
    int v = mq_dn_value[k];
    if (used + (rem + v - 1) / v >= *best) /* even all of this size could not beat it */
        return;
    for (int n = imin(have[k], rem / v); n >= 0; n--) {
        cur[k] = (uint8_t)n;
        fewest_dfs(k - 1, rem - n * v, used + n, have, cur, best, out);
    }
    cur[k] = 0;
}
int mq_coins_fewest(int cents, const uint8_t have[DN_COUNT], uint8_t out[DN_COUNT])
{
    uint8_t cur[DN_COUNT], o[DN_COUNT];
    zero(cur), zero(o);
    int best = 0x7FFF;
    if (cents < 0)
        return -1;
    fewest_dfs(DN_COUNT - 1, cents, 0, have, cur, &best, o);
    if (best == 0x7FFF)
        return -1;
    if (out)
        copy(out, o);
    return best;
}
bool mq_coins_payable(int cents, const uint8_t have[DN_COUNT]) { return mq_coins_fewest(cents, have, NULL) >= 0; }

/* ------------------------------------------------------------------ composing the handful */
/* the price exactly, in the coins of her rung (0 dimes and pennies, 1 + nickels, 2 + quarters, 3 + dollars),
 * now and then with a coin broken into smaller ones (seed) */
static void price_coins(int price, int rung, uint32_t seed, uint8_t w[DN_COUNT])
{
    zero(w);
    int d = price / 100, c = price % 100;
    w[DN_FIVE] = (uint8_t)(d / 5), w[DN_DOLLAR] = (uint8_t)(d % 5);
    if (rung <= 0) {
        w[DN_DIME] = (uint8_t)(c / 10), w[DN_PENNY] = (uint8_t)(c % 10);
        return;
    }
    if (rung == 1) {
        int r = c % 10;
        w[DN_DIME] = (uint8_t)(c / 10), w[DN_NICKEL] = (uint8_t)(r >= 5), w[DN_PENNY] = (uint8_t)(r % 5);
        if ((seed & 1) && w[DN_DIME]) /* a dime as two nickels */
            w[DN_DIME]--, w[DN_NICKEL] = (uint8_t)(w[DN_NICKEL] + 2);
        return;
    }
    w[DN_QUARTER] = (uint8_t)(c / 25), c %= 25;
    w[DN_DIME] = (uint8_t)(c / 10), c %= 10;
    w[DN_NICKEL] = (uint8_t)(c / 5), w[DN_PENNY] = (uint8_t)(c % 5);
    int v = (int)(seed % 3);
    if (v == 1 && w[DN_QUARTER]) /* a quarter as two dimes and a nickel */
        w[DN_QUARTER]--, w[DN_DIME] = (uint8_t)(w[DN_DIME] + 2), w[DN_NICKEL]++;
    else if (v && w[DN_DIME]) /* a dime as two nickels */
        w[DN_DIME]--, w[DN_NICKEL] = (uint8_t)(w[DN_NICKEL] + 2);
    else if (v && w[DN_NICKEL]) /* a nickel as five pennies */
        w[DN_NICKEL]--, w[DN_PENNY] = (uint8_t)(w[DN_PENNY] + 5);
}

/* The handful for a purchase: sums to balance; the price payable exactly in the rung's coins; in
 * PM_CHANGE the piece `over` to pay over with as well (when the balance has room); for the picky cashier
 * two ways to pay (a coin broken into smaller ones, and the fewest coins, when the balance has room);
 * the rest as the fewest pieces. A price above the balance: just the fewest pieces. */
void mq_purse_compose(int balance, int price, int mode, int rung, int over, uint32_t seed, uint8_t out[DN_COUNT])
{
    zero(out);
    if (balance <= 0)
        return;
    if (price <= 0 || price > balance) {
        mq_coins_greedy(balance, out);
        return;
    }
    uint8_t w[DN_COUNT], r[DN_COUNT];
    if (mode == PM_PICKY) /* (a broken coin, with quarters: paying with more coins than needed is possible) */
        seed = seed - seed % 3 + 1, rung = imax(rung, 2);
    price_coins(price, rung, seed, w);
    int rest = balance - price;
    if (mode == PM_PICKY && rest >= price % 100) { /* ... and the fewest coins for it too (room allowing) */
        uint8_t g[DN_COUNT];
        mq_coins_greedy(price % 100, g);
        mq_coins_greedy(rest - price % 100, r);
        if (mq_coins_only(w) + mq_coins_only(g) + mq_coins_only(r) <= MQ_PURSE_COINS) {
            for (int k = 0; k < DN_COUNT; k++)
                out[k] = (uint8_t)(out[k] + g[k]);
            rest -= price % 100;
        }
    }
    if (mode == PM_CHANGE && over >= 0 && over < DN_COUNT && rest >= mq_dn_value[over])
        out[over]++, rest -= mq_dn_value[over];
    mq_coins_greedy(rest, r);
    for (int k = 0; k < DN_COUNT; k++)
        out[k] = (uint8_t)imin(255, out[k] + w[k] + r[k]);
}

/* ------------------------------------------------------------------ the balance */
static void set_bal(mq_home_t *h, uint32_t cents)
{
    if (cents > HM_COINS_MAX)
        cents = HM_COINS_MAX;
    h->purse = cents;
    h->coins = (uint16_t)cents, h->purse_mirror = h->coins; /* (an older module reads coins) */
    h->purse_ver = h->purse_ver ? h->purse_ver : 1;
}
static void tidy(mq_home_t *h) /* the handful sums to the balance and is not a heap of coins */
{
    if (mq_coins_sum(h->hand) != (int)h->purse || mq_coins_only(h->hand) > MQ_PURSE_COINS)
        mq_coins_greedy((int)h->purse, h->hand);
}

uint32_t mq_purse_cents(mq_t *q)
{
    mq_home_t *h = mq_home(q);
    if (!h->purse_ver) /* (not migrated yet: a profile made by an older path) */
        mq_purse_migrate(q, q->save.cur & 1);
    return h->purse;
}

void mq_purse_set(mq_t *q, uint32_t cents)
{
    mq_home_t *h = mq_home(q);
    set_bal(h, cents);
    mq_coins_greedy((int)h->purse, h->hand);
    q->dirty = true;
}

void mq_purse_add(mq_t *q, int cents, const uint8_t *pieces)
{
    mq_home_t *h = mq_home(q);
    if (cents <= 0)
        return;
    uint32_t was = mq_purse_cents(q);
    set_bal(h, was + (uint32_t)cents);
    uint8_t g[DN_COUNT];
    if (!pieces || mq_coins_sum(pieces) != cents)
        mq_coins_greedy(cents, g), pieces = g;
    for (int k = 0; k < DN_COUNT; k++)
        h->hand[k] = (uint8_t)imin(255, h->hand[k] + pieces[k]);
    tidy(h);
    q->dirty = true;
}

bool mq_purse_spend(mq_t *q, int cents)
{
    mq_home_t *h = mq_home(q);
    if (cents < 0 || mq_purse_cents(q) < (uint32_t)cents)
        return false;
    set_bal(h, h->purse - (uint32_t)cents);
    mq_coins_greedy((int)h->purse, h->hand);
    q->dirty = true;
    return true;
}

/* An older save (no purse yet): its coins and a quarter of its stars (stars were only ever a score) into
 * the balance, once. A save an older module played since: what it earned or spent is taken over. */
void mq_purse_migrate(mq_t *q, int prof)
{
    mq_home_t *h = &q->save.home[prof & 1];
    const mq_profile_t *p = &q->save.prof[prof & 1];
    if (!h->purse_ver) {
        set_bal(h, (uint32_t)h->coins + p->stars / 4u);
        mq_coins_greedy((int)h->purse, h->hand);
    } else if (h->coins != h->purse_mirror) {
        int32_t b = (int32_t)h->purse + (int32_t)h->coins - (int32_t)h->purse_mirror;
        set_bal(h, (uint32_t)(b < 0 ? 0 : b));
    }
    tidy(h);
}

/* earnings fly into the purse as coins that make them (the biggest first, at most eight on the way) */
void mq_purse_fly(mq_t *q, int x, int y, int cents)
{
    uint8_t c[DN_COUNT];
    mq_coins_greedy(cents, c);
    int i = 0;
    for (int k = DN_COUNT - 1; k >= 0; k--)
        for (int n = 0; n < c[k]; n++) {
            while (i < 8 && q->fly[i].t0)
                i++;
            if (i >= 8)
                return;
            q->fly[i].x = (int16_t)(x + mq_rnd(q, -12, 12)), q->fly[i].y = (int16_t)(y + mq_rnd(q, -8, 8));
            q->fly[i].t0 = q->anim_t + (uint32_t)(i * 60) + 1;
            q->pay.fly_dn[i] = (uint8_t)k;
            i++;
        }
}

/* ------------------------------------------------------------------ drawing money */
static void coin_look(int dn, int scale, int *r, uint16_t *face, uint16_t *rim)
{
    static const uint8_t rad[4] = {10, 11, 9, 12};
    *r = scale >= 2 ? rad[dn & 3] : (rad[dn & 3] + 1) / 2 + 1;
    *face = dn == DN_PENNY ? RGB(222, 150, 100) : RGB(214, 218, 230);
    *rim = dn == DN_PENNY ? RGB(170, 100, 62) : RGB(150, 150, 172);
}

/* a coin or a bill centred at (cx, cy): scale 2 the counter's size (the value on it), 1 small (flying) */
static void money_draw(mq_t *q, int dn, int cx, int cy, int scale, bool label);
void mq_money_draw(mq_t *q, int dn, int cx, int cy, int scale) { money_draw(q, dn, cx, cy, scale, scale >= 2); }
/* (label: the value on it -- a pile labels only its top one, so no coin covers another's value) */
static void money_draw(mq_t *q, int dn, int cx, int cy, int scale, bool label)
{
    dn = dn < 0 ? 0 : dn >= DN_COUNT ? DN_COUNT - 1 : dn;
    const modx_api_t *api = q->api;
    if (dn >= DN_DOLLAR) {
        int w = scale >= 2 ? 36 : 14, h = scale >= 2 ? 20 : 8, x = cx - w / 2, y = cy - h / 2;
        int hr = mq_hr_sprite(q, dn_sprite[dn], x, y, w, h, 0, 0);
        mq_rrect(q, x, y, w, h, 3, RGB(120, 186, 130));
        mq_rrect(q, x + 2, y + 2, w - 4, h - 4, 2, RGB(196, 236, 200));
        if (scale >= 2)
            mq_circle(q, cx, cy, 5, RGB(150, 206, 160));
        if (hr)
            mq_hr_end(q);
        if (label)
            mq_text_c(q, api->font_small, cx, cy - 6, dn == DN_DOLLAR ? "$1" : "$5", Q_INK);
        return;
    }
    int r;
    uint16_t face, rim;
    coin_look(dn, scale, &r, &face, &rim);
    int hr = mq_hr_sprite(q, dn_sprite[dn], cx - r, cy - r, 2 * r, 2 * r, 0, 0);
    mq_circle(q, cx, cy, r, rim);
    mq_circle(q, cx, cy, r - (scale >= 2 ? 2 : 1), face);
    if (scale >= 2)
        mq_fill(q, cx - r / 2, cy - r + 3, 2, 2, Q_WHITE); /* a shine */
    if (hr)
        mq_hr_end(q);
    if (label) {
        static const char lab[4][3] = {"1", "5", "10", "25"};
        mq_text_c(q, api->font_small, cx, cy - 6, lab[dn], Q_INK);
    }
}

/* the cent sign, drawn (the fonts have none): a "c" with a stroke through it */
static int cent_sign(mq_t *q, const font_t *f, int x, int y, uint16_t c, bool draw)
{
    int w = q->api->text_width(f, "c");
    if (draw) {
        mq_text(q, f, x, y, "c", c);
        int a = f->ascent, xh = f->h * 4 / 10;
        mq_line(q, x + w / 2, y + a - xh - 2, x + w / 2, y + a + 1, c);
    }
    return w;
}
static int cents_fmt(char *b, int n, const modx_api_t *api, int cents)
{
    if (cents >= 100)
        return api->snprintf(b, (size_t)n, "$%d.%02d", cents / 100, cents % 100);
    return api->snprintf(b, (size_t)n, "%d", cents);
}
int mq_cents_width(mq_t *q, const font_t *f, int cents)
{
    char b[16];
    cents_fmt(b, sizeof b, q->api, cents < 0 ? 0 : cents);
    return q->api->text_width(f, b) + (cents < 100 ? cent_sign(q, f, 0, 0, 0, false) : 0);
}
/* "37c" (the cent sign drawn) or "$1.37" at (x, y) */
static int cents_raw(mq_t *q, const font_t *f, int x, int y, int cents, uint16_t c)
{
    char b[16];
    cents_fmt(b, sizeof b, q->api, cents);
    int tw = mq_text(q, f, x, y, b, c);
    if (cents < 100)
        cent_sign(q, f, x + tw, y, c, true);
    return mq_cents_width(q, f, cents);
}
/* "+37c" or "$1.37" (pre: a word before it, or NULL) at (x, y), or centred on x: the width. On a platform
 * with a font of its own (the Mac page) it goes over as one run with a real cent sign, measured and centred
 * on the 1x run (MODX_HR_MEASURE): no gap between the "+", the digits and the sign */
int mq_cents_run(mq_t *q, const font_t *f, int x, int y, const char *pre, int cents, uint16_t c, bool centre)
{
    const modx_api_t *api = q->api;
    cents = cents < 0 ? 0 : cents;
    pre = pre ? pre : "";
    int wp = *pre ? api->text_width(f, pre) : 0, w = wp + mq_cents_width(q, f, cents);
    if (centre)
        x -= w / 2;
    char s[40];
    if (cents >= 100)
        api->snprintf(s, sizeof s, "%s$%d.%02d", pre, cents / 100, cents % 100);
    else
        api->snprintf(s, sizeof s, "%s%d\xC2\xA2", pre, cents);
    int mw = mq_hr_measure(q, f, s);
    int hr = mw ? mq_hr_text_at(q, f, x * 16 + w * 8 - mw / 2, y, mw, s, c) : 0;
    if (*pre)
        mq_text(q, f, x, y, pre, c);
    cents_raw(q, f, x + wp, y, cents, c);
    if (hr)
        mq_hr_end(q);
    return w;
}
int mq_cents_text(mq_t *q, const font_t *f, int x, int y, int cents, uint16_t c, bool centre)
{
    return mq_cents_run(q, f, x, y, NULL, cents, c, centre);
}

/* her purse: a little pink pouch with a gold clasp and handle, in the square of half side r round (cx, cy) */
void mq_purse_icon(mq_t *q, int cx, int cy, int r)
{
    int hd = imax(2, r / 3), x = cx - r, w = 2 * r, y = cy - r + hd, h = cy + r - y;
    int hr = mq_hr_sprite(q, "money/purse", x, cy - r, w, 2 * r, 0, 0);
    mq_ring(q, cx, y, hd, RGB(240, 196, 80));                       /* the handle */
    mq_rrect(q, x, y + 1, w, h - 1, imax(2, r / 2), RGB(236, 120, 160)); /* the pouch */
    mq_rrect(q, x + 1, y + 2, w - 2, h - 3 > 1 ? h - 3 : 1, imax(2, r / 2), Q_PINK);
    mq_fill(q, x + 1, y + h / 3, w - 2, 1, RGB(236, 120, 160));       /* the flap */
    mq_circle(q, cx, y + h / 3 + 1, imax(1, r / 4), RGB(240, 196, 80)); /* the clasp */
    if (hr)
        mq_hr_end(q);
}

/* the purse and the balance, right-aligned at right_x: every title line. The coins fly to the purse. */
int mq_purse_badge(mq_t *q, int right_x, int y)
{
    const modx_api_t *api = q->api;
    int cents = (int)mq_purse_cents(q), w = mq_cents_width(q, api->font_medium, cents);
    mq_cents_text(q, api->font_medium, right_x - w, y, cents, Q_INK, false);
    mq_purse_icon(q, right_x - w - 11, y + 7, 8);
    q->coin_ix = (int16_t)(right_x - w - 11), q->coin_iy = (int16_t)(y + 7); /* (coins fly into it, not over the count) */
    if (q->pop_coins && q->now - q->pop_t < 1500u) { /* "+3" floating up */
        int dy = (int)(q->now - q->pop_t) / 100;
        char s[12];
        api->snprintf(s, sizeof s, "+%d", q->pop_coins);
        mq_text(q, api->font_small, right_x - w - 44, y + 4 - dy / 2, s, RGB(220, 150, 30));
    }
    return w + 20;
}

/* ------------------------------------------------------------------ a game's end: coins, not points */
/* A game (the arcade, the action games, Bongo Beats) ends on what it earned: the coins shower into her
 * purse and "+12c" is the one number shown. A new best is a trophy and "New best!", never a score. */
void mq_game_coins_start(mq_t *q) { q->earned0 = mq_home(q)->earned; }
int mq_game_coins(mq_t *q)
{
    uint32_t e = mq_home(q)->earned;
    return e >= q->earned0 ? (int)(e - q->earned0 > 99999u ? 99999u : e - q->earned0) : 0;
}

/* a little gold cup in the square of half side r round (cx, cy) */
void mq_trophy(mq_t *q, int cx, int cy, int r)
{
    int hr = mq_hr_sprite(q, "money/trophy", cx - r, cy - r, 2 * r, 2 * r, 0, 0);
    int hw = imax(2, r * 6 / 10), top = cy - r + r / 4, cup = imax(4, r);
    mq_ring(q, cx - hw, top + cup / 3, imax(1, r / 4), RGB(240, 196, 80)); /* the handles */
    mq_ring(q, cx + hw, top + cup / 3, imax(1, r / 4), RGB(240, 196, 80));
    mq_rrect(q, cx - hw, top, 2 * hw, cup, imax(2, r / 3), RGB(240, 196, 80)); /* the cup */
    mq_star(q, cx, top + cup / 2 - 1, imax(2, r / 3), RGB(255, 236, 160));
    mq_fill(q, cx - 1, top + cup, 3, imax(1, r / 4), RGB(214, 160, 60)); /* the stem, the foot */
    mq_rrect(q, cx - hw + 1, cy + r - imax(2, r / 3), 2 * hw - 2, imax(2, r / 3), 1, RGB(150, 100, 150));
    if (hr)
        mq_hr_end(q);
}

/* the end card's middle, its top at y round cx: the coins (at most eight) fall into the purse one by one
 * from t0, then "+12c" (nothing earned: a cheer); a new best below it, as a trophy */
void mq_coins_end(mq_t *q, int cx, int y, int cents, uint32_t t0, bool new_best)
{
    const modx_api_t *api = q->api;
    const font_t *f = api->font_xlarge;
    int el = (int)(q->anim_t - t0), w = cents ? api->text_width(f, "+") + mq_cents_width(q, f, cents) : 0;
    int px = cx - (w + 34) / 2 + 13, py = y + 16; /* the purse, left of the amount */
    if (cents > 0) {
        uint8_t c[DN_COUNT];
        mq_coins_greedy(cents, c);
        int n = 0, landed = 0;
        for (int k = DN_COUNT - 1; k >= 0; k--)
            for (int i = 0; i < c[k] && n < 8; i++, n++) {
                int at = el - n * 140; /* each falls for 420 ms, 140 ms after the one before */
                if (at >= 420) {
                    landed++;
                    continue;
                }
                if (at < 0)
                    continue;
                int sx = px + (n % 3 - 1) * 22, sy = y - 26;
                mq_money_draw(q, k, sx + (px - sx) * at / 420, sy + (py - sy) * at / 420, 2);
            }
        int bob = landed && landed < n && el - (landed - 1) * 140 - 420 < 120 ? 2 : 0; /* the purse bobs as each lands */
        mq_purse_icon(q, px, py + bob, 13);
        mq_cents_run(q, f, px + 21, y + 2, "+", cents, Q_HOTPINK, false);

        if (n && landed == n && el - n * 140 - 280 < 200)
            mq_spark_burst(q, px, py - 6, 2);
    } else
        mq_text_c(q, f, cx, y + 2, "Great run!", Q_HOTPINK);
    if (new_best) {
        int tw = api->text_width(api->font_medium, "New best!"), bx = cx - (tw + 26) / 2;
        mq_trophy(q, bx + 9, y + 48, 9);
        mq_text(q, api->font_medium, bx + 24, y + 40, "New best!", Q_PURPLE);
        if ((q->anim_t / 200) % 3 == 0)
            mq_spark_burst(q, bx + 9, y + 42, 2);
    }
}

/* ------------------------------------------------------------------ how she pays now */
static bool reached(mq_t *q, int s)
{
    const mq_skrec_t *k = &mq_prof(q)->sk[s];
    return k->tries || (k->flags & SK_PASSED) || mq_skill_assumed(q, s);
}
static int rung_of(mq_t *q, int s) { return imin(mq_prof(q)->sk[s].diff, mq_rungs(s) - 1); }
/* the piece to pay over with: a $1 bill under a dollar on the first rungs of making change, else a $5 bill */
static int over_piece(mq_t *q, int price) { return price < 100 && rung_of(q, S_CHANGE) < 2 ? DN_DOLLAR : DN_FIVE; }

int mq_pay_mode(mq_t *q, int price)
{
    const mq_profile_t *p = mq_prof(q);
    mq_home_t *h = mq_home(q);
    if (price <= 0 || !reached(q, S_COINS) || mq_purse_cents(q) < (uint32_t)price)
        return PM_TAP;
    const mq_skrec_t *c = &p->sk[S_COINS];
    bool quarters = c->diff >= 2 || (c->flags & SK_PASSED);
    bool change = reached(q, S_CHANGE) && mq_school_taught(q, S_CHANGE) && quarters;
    if (change && rung_of(q, S_CHANGE) == 0 && price % 5) /* (the first rung: prices of whole nickels) */
        change = false;
    if (change && mq_purse_cents(q) < (uint32_t)price + mq_dn_value[over_piece(q, price)])
        change = false;
    if (price >= 500) /* (no bill to pay over with: a $5 thing or dearer is counted out) */
        change = false;
    bool picky = h->pay_streak >= 4 && quarters;
    int n = h->pay_n;
    if (change && picky)
        return n % 3 == 0 ? PM_CHANGE : n % 3 == 1 ? PM_PICKY : PM_COUNT;
    if (change)
        return n % 2 == 0 ? PM_CHANGE : PM_COUNT;
    if (picky)
        return n % 2 ? PM_PICKY : PM_COUNT;
    return PM_COUNT;
}

bool mq_pay_on(const mq_t *q) { return q->pay.on != 0; }

/* ------------------------------------------------------------------ the cashier talks */
/* what the cashier says: '#' is an amount (say_v), drawn as money and said in words */
static void say(mq_t *q, const char *s, int v)
{
    mq_pay_t *P = &q->pay;
    q->api->snprintf(P->say, sizeof P->say, "%s", s);
    P->say_v = v, P->say_t = mq_mark(q);
    char spoken[96], amt[32];
    if (v >= 100 && v % 100 == 0)
        q->api->snprintf(amt, sizeof amt, "%d dollar%s", v / 100, v >= 200 ? "s" : "");
    else if (v >= 100)
        q->api->snprintf(amt, sizeof amt, "%d dollar%s %d cent%s", v / 100, v >= 200 ? "s" : "", v % 100, v % 100 == 1 ? "" : "s");
    else
        q->api->snprintf(amt, sizeof amt, "%d cent%s", v, v == 1 ? "" : "s");
    int o = 0;
    for (const char *c = s; *c && o < (int)sizeof spoken - 1; c++)
        if (*c == '#')
            for (const char *a = amt; *a && o < (int)sizeof spoken - 1; a++)
                spoken[o++] = *a;
        else
            spoken[o++] = *c;
    spoken[o] = 0;
    mq_speak(q, P->cashier, spoken, 0);
    q->redraw = true;
}

/* the line (with its amount) at x, y, at most w wide (centred) */
static void say_draw(mq_t *q, int cx, int y, int w)
{
    const modx_api_t *api = q->api;
    const font_t *f = api->font_medium;
    mq_pay_t *P = &q->pay;
    char a[48], b[48];
    int na = 0, nb = 0;
    bool amt = false;
    for (const char *c = P->say; *c; c++) {
        if (*c == '#' && !amt) {
            amt = true;
            continue;
        }
        if (!amt && na < 47)
            a[na++] = *c;
        else if (amt && nb < 47)
            b[nb++] = *c;
    }
    a[na] = b[nb] = 0;
    int wa = api->text_width(f, a), wv = amt ? mq_cents_width(q, f, P->say_v) : 0, wb = api->text_width(f, b);
    if (wa + wv + wb > w)
        f = api->font_small, wa = api->text_width(f, a), wv = amt ? mq_cents_width(q, f, P->say_v) : 0, wb = api->text_width(f, b);
    int x = cx - (wa + wv + wb) / 2;
    if (x < cx - w / 2)
        x = cx - w / 2;
    /* A platform with a font of its own (the Mac page) sets the runs side by side as its font has them
     * (MODX_HR_MEASURE), centred the same way -- laid out by the 1x widths, each run sat in a box of its
     * own and a gap opened ("That's   12c !"). The amount goes over as one run with a real cent sign. */
    char vs[16];
    if (P->say_v >= 100)
        api->snprintf(vs, sizeof vs, "$%d.%02d", P->say_v / 100, P->say_v % 100);
    else
        api->snprintf(vs, sizeof vs, "%d\xC2\xA2", P->say_v < 0 ? 0 : P->say_v);
    int ma = a[0] ? mq_hr_measure(q, f, a) : 0, mv = amt ? mq_hr_measure(q, f, vs) : 0, mb = b[0] ? mq_hr_measure(q, f, b) : 0;
    bool fit = (ma || !a[0]) && (mv || !amt) && (mb || !b[0]) && (ma || mv || mb);
    int x16 = cx * 16 - (ma + mv + mb) / 2;
    if (x16 < (cx - w / 2) * 16)
        x16 = (cx - w / 2) * 16;
    int hr = fit && a[0] ? mq_hr_text_at(q, f, x16, y, ma, a, Q_INK) : 0;
    mq_text(q, f, x, y, a, Q_INK);
    if (hr)
        mq_hr_end(q);
    if (amt) {
        hr = fit ? mq_hr_text_at(q, f, x16 + ma, y, mv, vs, Q_HOTPINK) : 0;
        mq_cents_text(q, f, x + wa, y, P->say_v, Q_HOTPINK, false);
        if (hr)
            mq_hr_end(q);
    }
    hr = fit && b[0] ? mq_hr_text_at(q, f, x16 + ma + mv, y, mb, b, Q_INK) : 0;
    mq_text(q, f, x + wa + wv, y, b, Q_INK);
    if (hr)
        mq_hr_end(q);
}


/* ------------------------------------------------------------------ the engine: one answer a purchase */
static uint32_t pay_ident(const mq_pay_t *P)
{
    uint32_t h = 2166136261u;
    uint32_t v[4] = {P->skill, P->price, P->skill == S_CHANGE ? P->paid : 0u, P->mode};
    for (int i = 0; i < 4; i++)
        h = (h ^ v[i]) * 16777619u;
    return h | 1u;
}
static void record(mq_t *q, bool right)
{
    mq_pay_t *P = &q->pay;
    if (P->recorded)
        return;
    P->recorded = 1;
    int s = P->skill;
    uint32_t ms = q->now - P->t0;
    mq_prob_t pr = {0};
    pr.skill = (uint8_t)s, pr.diff = P->rung, pr.ans = s == S_CHANGE ? P->paid - P->price : P->price;
    pr.ident = pay_ident(P);
    mq_prof(q)->problems++;
    mq_seen_record(q, &pr, right, ms > 15000u); /* the problem's own history */
    mq_rev_t *rv = &mq_learn(q)->rev[s];
    if (!rv->box)
        mq_review_join(q, s, false); /* played: it comes back now and then */
    else if ((int16_t)(uint16_t)(rv->due - mq_play_min(q)) <= 0)
        mq_review_answer(q, s, right); /* due in review: this was its review */
    /* the success window (not the round's rung: q->prob is the round's order), then the counter's rung */
    uint8_t keep = q->prob.skill;
    q->prob.skill = 0xFF;
    mq_coach_answer(q, s, right, ms);
    q->prob.skill = keep;
    mq_rung_answer(q, s, P->rung, right, right && ms < 8000u);
    mq_skill_answer(q, s, right, right);
    mq_engine_answered(q, s, !right);
    mq_home_t *h = mq_home(q);
    if (s == S_COINS)
        h->pay_streak = (uint8_t)(right ? imin(250, h->pay_streak + 1) : 0);
}

/* ------------------------------------------------------------------ the counter: open, pay, close */
static const mq_item_t *ITEM(int id) { return &mq_item[id >= 1 && id <= MQ_NITEM ? id : 0]; }

void mq_pay_open(mq_t *q, int item)
{
    mq_pay_t *P = &q->pay;
    mq_home_t *h = mq_home(q);
    for (size_t i = 0; i < offsetof(mq_pay_t, fly_dn); i++) /* (the coins in flight stay) */
        ((uint8_t *)P)[i] = 0;
    int price = ITEM(item)->price;
    int mode = mq_pay_mode(q, price);
    if (mode == PM_TAP || mq_buy_check(q, item))
        return;
    P->item = (uint8_t)item, P->price = (uint16_t)price, P->mode = (uint8_t)mode;
    P->cashier = (uint8_t)(mq_skill[S_COINS].world % MQ_NCAST);
    P->skill = mode == PM_CHANGE ? S_CHANGE : S_COINS;
    P->rung = (uint8_t)rung_of(q, P->skill);
    P->over = (uint8_t)(mode == PM_CHANGE ? over_piece(q, price) : 0xFF);
    P->t0 = q->now;
    /* the handful, composed quietly now (between purchases) */
    mq_purse_compose((int)mq_purse_cents(q), price, mode, rung_of(q, S_COINS), mode == PM_CHANGE ? P->over : -1,
                     (uint32_t)mq_rnd(q, 0, 0x7FFF), h->hand);
    if (!mq_coins_payable(price, h->hand)) /* (never: the composition pays it) -- a tap then */
        return;
    copy(P->hand, h->hand);
    P->on = 1, P->phase = PP_PAY;
    mq_wgs_t *s = &q->wg; /* (a fresh set of widgets: nothing pressed, carried or in focus from another screen) */
    s->n = 0, s->press = s->carry = s->picked = s->hover = s->focus = s->dragged = -1, s->flash_t = 0, s->kbd = false;
    s->focus_id = 0;
    mq_softkeys_update(q);
    q->dirty = q->redraw = true;
    if (mode == PM_PICKY) {
        uint8_t nop[DN_COUNT];
        copy(nop, P->hand), nop[DN_PENNY] = 0;
        int best = mq_coins_fewest(price, P->hand, NULL), r = mq_rnd(q, 0, 2);
        if (r == 0 && price % 5 == 0 && mq_coins_payable(price, nop) && P->hand[DN_PENNY])
            P->rule = PK_NOPENNY, say(q, "That's #! No pennies today!", price);
        else if (r == 1 && best >= 2) {
            P->rule = PK_MAXN, P->maxn = (uint8_t)best;
            char b[48];
            q->api->snprintf(b, sizeof b, "That's #! %d coins at most!", best);
            say(q, b, price);
        } else
            P->rule = PK_FEWEST, say(q, "That's #! The fewest coins, please!", price);
    } else if (mode == PM_CHANGE)
        say(q, P->over == DN_DOLLAR ? "That's #! A dollar bill is fine!" : "That's #! A $5 bill is fine!", price);
    else
        say(q, "That's #!", price);
    mq_sfx(q, MODX_SFX_TAP, 0);
}

static void close_counter(mq_t *q)
{
    q->pay.on = 0;
    q->wg.n = 0, q->wg.picked = -1, q->wg.carry = -1;
    mq_softkeys_update(q);
    q->redraw = true;
}

/* CLEAR / back: the coins on the counter go back into her purse; with none there, the counter closes
 * (nothing bought, nothing lost). After paying: closes. */
bool mq_pay_back(mq_t *q)
{
    mq_pay_t *P = &q->pay;
    if (!P->on)
        return false;
    if (P->phase == PP_PAY && mq_coins_count(P->counter)) {
        for (int k = 0; k < DN_COUNT; k++)
            P->back[k] = P->counter[k], P->hand[k] = (uint8_t)(P->hand[k] + P->counter[k]), P->counter[k] = 0;
        P->back_t = mq_mark(q);
        q->redraw = true;
        return true;
    }
    if (P->phase == PP_CHANGE) /* (paid over, the change not said yet: the money back, nothing bought) */
        for (int k = 0; k < DN_COUNT; k++)
            P->hand[k] = (uint8_t)(P->hand[k] + P->counter[k]), P->counter[k] = 0;
    if (P->phase == PP_PAY || P->phase == PP_CHANGE)
        copy(mq_home(q)->hand, P->hand);
    close_counter(q);
    return true;
}

/* the purchase made: the price out of the balance, the change into her purse, the item hers */
static void complete(mq_t *q)
{
    mq_pay_t *P = &q->pay;
    mq_home_t *h = mq_home(q);
    int paid = mq_coins_sum(P->counter), change = paid - P->price;
    uint8_t ch[DN_COUNT];
    mq_coins_greedy(change > 0 ? change : 0, ch);
    for (int k = 0; k < DN_COUNT; k++)
        P->hand[k] = (uint8_t)imin(255, P->hand[k] + ch[k]);
    set_bal(h, h->purse - P->price);
    copy(h->hand, P->hand);
    tidy(h);
    copy(P->hand, h->hand);
    P->paid = (uint16_t)paid;
    mq_buy_give(q, P->item);
    h->pay_n++;
    P->phase = PP_THANKS, P->done_t = mq_mark(q);
    q->api->snprintf(q->toast_in, sizeof q->toast_in, "You got a %s!", ITEM(P->item)->name);
    mq_toast(q, q->toast_in);
    if (q->new_bonus)
        mq_toast(q, "Set complete! A bonus prize!");
    q->new_bonus = 0;
    mq_spark_burst(q, 286, MQ_Y0 + 76, 16);
    mq_sfx(q, MODX_SFX_COIN, 0);
    mq_keep(q);
}

/* a swap from her purse the cashier can offer: some small coins for one bigger (5 pennies for a nickel ...) */
static bool find_swap(const uint8_t hand[DN_COUNT], uint8_t *from, uint8_t *n, uint8_t *to)
{
    static const uint8_t sw[4][3] = {{DN_PENNY, 5, DN_NICKEL}, {DN_NICKEL, 2, DN_DIME}, {DN_QUARTER, 4, DN_DOLLAR},
                                     {DN_DOLLAR, 5, DN_FIVE}};
    for (int i = 0; i < 4; i++)
        if (hand[sw[i][0]] >= sw[i][1]) {
            *from = sw[i][0], *n = sw[i][1], *to = sw[i][2];
            return true;
        }
    return false;
}

/* the working set from what she has (purse and counter): the coins she put down that fit, then the
 * fewest of the rest; paying over, the piece to pay with */
static bool working_set(mq_t *q, uint8_t ws[DN_COUNT])
{
    mq_pay_t *P = &q->pay;
    uint8_t all[DN_COUNT];
    for (int k = 0; k < DN_COUNT; k++)
        all[k] = (uint8_t)(P->hand[k] + P->counter[k]);
    zero(ws);
    if (P->mode == PM_CHANGE && P->over < DN_COUNT && all[P->over]) {
        ws[P->over] = 1;
        return true;
    }
    int on = mq_coins_sum(P->counter);
    uint8_t rest[DN_COUNT];
    if (P->mode != PM_PICKY && on <= P->price && mq_coins_fewest(P->price - on, P->hand, rest) >= 0) {
        for (int k = 0; k < DN_COUNT; k++)
            ws[k] = (uint8_t)(P->counter[k] + rest[k]);
        return true;
    }
    return mq_coins_fewest(P->price, all, ws) >= 0;
}

/* "help me pay": the working set ringed in her purse, the coins on the counter that are not in it slide back */
static void help(mq_t *q)
{
    mq_pay_t *P = &q->pay;
    uint8_t ws[DN_COUNT];
    if (P->phase != PP_PAY || !working_set(q, ws))
        return;
    P->helped = 1;
    bool slid = false;
    for (int k = 0; k < DN_COUNT; k++) {
        int extra = P->counter[k] - ws[k];
        P->back[k] = 0;
        if (extra > 0)
            P->counter[k] = (uint8_t)(P->counter[k] - extra), P->hand[k] = (uint8_t)(P->hand[k] + extra),
            P->back[k] = (uint8_t)extra, slid = true;
        P->help[k] = (uint8_t)imax(0, ws[k] - P->counter[k]);
    }
    if (slid)
        P->back_t = mq_mark(q);
    P->mood = 0;
    if (P->mode == PM_CHANGE)
        say(q, "Let's pay with the big one! Tap it.", 0);
    else
        say(q, "Let's do it together: the ringed ones!", 0);
    P->say_v = -1;
    mq_sfx(q, MODX_SFX_WHOOSH, 0);
}

/* a wrong payment: a hint, then help by itself, then the cashier counts it out with her (never stuck) */
static void miss(mq_t *q, const char *why, int v)
{
    mq_pay_t *P = &q->pay;
    P->misses++;
    P->mood = 2;
    if (P->mode != PM_CHANGE)
        record(q, false);
    mq_sfx(q, MODX_SFX_DUD, 0);
    if (P->misses == 1) {
        say(q, why, v);
        return;
    }
    if (P->misses == 2 || !P->helped) {
        help(q);
        return;
    }
    uint8_t ws[DN_COUNT]; /* the third: counted out together, and bought */
    if (!working_set(q, ws))
        mq_coins_fewest(P->price, P->hand, ws);
    for (int k = 0; k < DN_COUNT; k++)
        P->hand[k] = (uint8_t)(P->hand[k] + P->counter[k] - ws[k]), P->counter[k] = ws[k], P->help[k] = 0;
    if (mq_coins_sum(P->counter) > P->price && P->mode == PM_CHANGE) {
        P->paid = (uint16_t)mq_coins_sum(P->counter), P->phase = PP_CHANGE, P->change_in = 0, P->change_n = 0;
        say(q, "Here we go! How much change?", 0);
        P->say_v = -1;
        return;
    }
    complete(q);
    say(q, "We counted # together. Thank you!", P->price);
}

static void pay(mq_t *q)
{
    mq_pay_t *P = &q->pay;
    int sum = mq_coins_sum(P->counter), n = mq_coins_count(P->counter);
    if (!n) {
        say(q, "Put some coins on the counter!", 0);
        P->say_v = -1;
        return;
    }
    if (sum < P->price) {
        miss(q, "Hmm, that's not # yet.", P->price);
        return;
    }
    if (sum > P->price) {
        if (P->mode != PM_CHANGE) {
            miss(q, "Ooh, that's more than #!", P->price);
            return;
        }
        int least = 0x7FFF; /* paying over: no piece on the counter that is not needed */
        for (int k = 0; k < DN_COUNT; k++)
            if (P->counter[k] && mq_dn_value[k] < least)
                least = mq_dn_value[k];
        if (sum - least >= P->price) {
            miss(q, "That's lots! Fewer will do for #.", P->price);
            return;
        }
        P->paid = (uint16_t)sum, P->phase = PP_CHANGE, P->change_in = 0, P->change_n = 0, P->mood = 0;
        say(q, "You gave #. How much change?", sum);
        mq_sfx(q, MODX_SFX_COIN, 0);
        return;
    }
    /* exactly the price */
    if (P->mode == PM_CHANGE && !P->recorded) /* paid exactly instead of over: counting coins, right */
        P->skill = S_COINS, P->rung = (uint8_t)rung_of(q, S_COINS);
    record(q, !P->helped && !P->misses);
    if (P->mode != PM_PICKY) {
        complete(q);
        say(q, P->helped ? "We did it! Thank you!" : "That's just right! Thank you!", 0);
        P->say_v = -1;
        return;
    }
    /* the picky cashier */
    mq_home_t *h = mq_home(q);
    uint8_t all[DN_COUNT];
    for (int k = 0; k < DN_COUNT; k++)
        all[k] = (uint8_t)(P->hand[k] + P->counter[k]);
    int best = mq_coins_fewest(P->price, all, NULL);
    bool ok = P->rule == PK_FEWEST ? n == best : P->rule == PK_MAXN ? n <= P->maxn : P->counter[DN_PENNY] == 0;
    uint8_t paid[DN_COUNT];
    copy(paid, P->counter);
    complete(q);
    if (ok) {
        h->picky_ok = (uint8_t)imin(250, h->picky_ok + 1);
        if (n == best && !P->helped) { /* the fewest coins: a bonus sparkle */
            P->bonus = 1;
            mq_extra_t *e = mq_extra(q);
            e->energy = (uint8_t)imin(60, e->energy + 3);
            mq_spark_burst(q, 160, MQ_Y0 + 76, 28);
            mq_sfx(q, MODX_SFX_UNLOCK, 3);
            say(q, "The fewest coins! A bonus sparkle!", 0);
        } else
            say(q, "Perfect! Thank you!", 0);
        P->say_v = -1;
        P->mood = 0;
        return;
    }
    /* right, but too many coins: a grumpy-cute grumble, a swap, and she still gets it */
    h->picky_ok = 0;
    static const char grumble[4][44] = {"Hmph! So... many... coins! *pout*", "Clink clink clink! My poor drawer!",
                                        "Oof! My till is SO full now!", "So many coins! My ears are ringing!"};
    P->mood = 1;
    say(q, grumble[mq_rnd(q, 0, 3)], 0);
    P->say_v = -1;
    mq_sfx(q, MODX_SFX_SIGH, 0);
    if (find_swap(P->hand, &P->swap_from, &P->swap_n, &P->swap_to))
        P->phase = PP_SWAP;
    (void)paid;
}

static void change_ok(mq_t *q)
{
    mq_pay_t *P = &q->pay;
    int want = P->paid - P->price;
    if (P->change_in == want) {
        record(q, !P->change_misses && !P->helped);
        complete(q);
        say(q, "Yes! # back. Thank you!", want);
        return;
    }
    P->change_misses++;
    record(q, false);
    mq_sfx(q, MODX_SFX_DUD, 0);
    P->mood = 2;
    if (P->change_misses == 1) {
        say(q, "Hmm, not quite. Count up from #!", P->price);
        P->change_in = 0, P->change_n = 0;
        return;
    }
    P->change_in = want; /* the second: counted out together */
    complete(q);
    say(q, "Let's count it: # back. Thank you!", want);
}

/* ------------------------------------------------------------------ layout and drawing */
#define CY0 (MQ_Y0 + 52)    /* the counter */
#define CH 50
#define PY0 (MQ_Y0 + 106)   /* her purse */
#define PH 64
#define BY (MQ_Y0 + 174)    /* the buttons */
static int col_of(int dn) { return DN_FIVE - dn; } /* big to small, left to right */
static int counter_x(int dn) { return 30 + col_of(dn) * 40; }
static int purse_x(int dn) { return 34 + col_of(dn) * 51; }
static bool in_counter(int x, int y) { return x >= 4 && x < 256 && y >= CY0 - 6 && y < CY0 + CH + 4; }
static bool in_purse(int x, int y) { return y >= PY0 - 2 && y < PY0 + PH + 4; }

static void layout(mq_t *q)
{
    mq_pay_t *P = &q->pay;
    mq_wg_begin(q);
    int i;
    if (P->phase == PP_PAY) {
        for (int dn = DN_FIVE; dn >= 0; dn--) {
            i = mq_wg_add(q, WG_DRAG, PW_COIN, dn, purse_x(dn) - 22, PY0 + 8, 44, 50);
            mq_wg_flags(q, i, WF_CUSTOM | (P->hand[dn] ? 0 : WF_DISABLED));
            mq_wg_label(q, i, dn_name[dn], 0);
        }
        for (int dn = DN_FIVE; dn >= 0; dn--)
            if (P->counter[dn]) {
                i = mq_wg_add(q, WG_DRAG, PW_CCOIN, dn, counter_x(dn) - 18, CY0 + 6, 36, 40);
                mq_wg_flags(q, i, WF_CUSTOM);
                mq_wg_label(q, i, dn_name[dn], 0);
            }
        i = mq_wg_add(q, WG_BUTTON, PW_HELP, 0, 8, BY, 120, 28);
        mq_wg_label(q, i, "help me pay", Q_LAV);
        i = mq_wg_add(q, WG_BUTTON, PW_PAY, 0, 228, BY, 84, 28);
        mq_wg_label(q, i, "pay!", Q_MINT);
    } else if (P->phase == PP_CHANGE) {
        for (int k = 0; k < 10; k++) {
            int d = (k + 1) % 10;
            char s[2] = {(char)('0' + d), 0};
            i = mq_wg_add(q, WG_KEY, PW_KEY, d, 8 + k * 31, PY0 + 28, 28, 32);
            mq_wg_range(q, i, d, 0, 9, 1);
            mq_wg_label(q, i, s, Q_WHITE);
        }
        i = mq_wg_add(q, WG_BUTTON, PW_DEL, 0, 8, BY, 60, 28);
        mq_wg_label(q, i, "<", Q_PEACH);
        i = mq_wg_add(q, WG_BUTTON, PW_OK, 0, 228, BY, 84, 28);
        mq_wg_label(q, i, "ok!", Q_MINT);
    } else if (P->phase == PP_SWAP) {
        i = mq_wg_add(q, WG_BUTTON, PW_NOSWAP, 0, 8, BY, 120, 28);
        mq_wg_label(q, i, "no thanks", Q_WHITE);
        i = mq_wg_add(q, WG_BUTTON, PW_SWAP, 0, 228, BY, 84, 28);
        mq_wg_label(q, i, "swap!", Q_MINT);
    } else {
        i = mq_wg_add(q, WG_BUTTON, PW_DONE, 0, 228, BY, 84, 28);
        mq_wg_label(q, i, "yay!", Q_MINT);
    }
}

/* a pile of n of a kind: a little stack (at most five drawn), "x7" under it when more than one */
static void pile(mq_t *q, int dn, int cx, int cy, int n, bool ring)
{
    if (n <= 0)
        return;
    int show = imin(n, 5);
    for (int k = 0; k < show; k++)
        money_draw(q, dn, cx + (dn >= DN_DOLLAR ? k * 2 : 0), cy - k * 3, 2, k == show - 1);
    if (n > 1) {
        char s[8];
        q->api->snprintf(s, sizeof s, "x%d", n);
        mq_text_c(q, q->api->font_small, cx, cy + 13, s, Q_INK);
    }
    if (ring)
        for (int t = 0; t < 2; t++)
            mq_rect(q, cx - 22 - t, cy - 22 - t, 44 + 2 * t, 48 + 2 * t, Q_HOTPINK);
}

void mq_pay_draw(mq_t *q)
{
    const modx_api_t *api = q->api;
    mq_pay_t *P = &q->pay;
    layout(q);
    mq_rrect(q, 2, MQ_Y0 + 20, 316, 186, 10, RGB(255, 246, 230));
    /* the cashier and what he says */
    int an = P->mood == 1 ? AN_POUT : P->mood == 2 ? AN_THINK : P->bonus ? AN_DANCE
           : P->say_t && q->anim_t - P->say_t < 1200 ? AN_TALK : AN_HAPPY;
    mq_cast(q, P->cashier, 6, MQ_Y0 + 21, 40, an);
    mq_rrect(q, 50, MQ_Y0 + 23, 264, 26, 10, Q_WHITE);
    mq_rect(q, 56, MQ_Y0 + 23, 252, 1, Q_LAV);
    say_draw(q, 182, MQ_Y0 + 29, 256);
    /* the counter: wood, the item and its price tag at the end */
    mq_rrect(q, 4, CY0, 312, CH, 8, RGB(222, 184, 140));
    mq_fill(q, 10, CY0 + 2, 300, 3, RGB(240, 210, 170));
    int id = P->item, cx = 286;
    const mq_item_t *t = ITEM(id);
    int s = t->aw && t->aw <= 22 && t->ah <= 18 ? 2 : t->aw <= 44 && t->ah <= 36 ? 1 : 0;
    int dw = s ? t->aw * s : t->aw / 2, dh = s ? t->ah * s : t->ah / 2;
    if (t->aw)
        mq_item_draw(q, id, cx - dw / 2, CY0 + 30 - dh, 0, (int)(q->anim_t / 400 % 2), s, -1);
    mq_rrect(q, cx - 22, CY0 + 32, 44, 16, 5, Q_YELLOW); /* the tag */
    mq_cents_text(q, api->font_small, cx, CY0 + 34, P->price, Q_INK, true);
    int carry_i = -1, fx = 0, fy = 0;
    bool carrying = mq_wg_carrying(q, &carry_i, &fx, &fy);
    if (carrying && in_counter(fx, fy) && mq_wg(q, carry_i) && mq_wg(q, carry_i)->id == PW_COIN)
        for (int k = 0; k < 2; k++) /* (where it would land) */
            mq_rect(q, 4 - k, CY0 - k, 252 + 2 * k, CH + 2 * k, Q_YELLOW);
    uint32_t bk = P->back_t ? q->anim_t - P->back_t : 1000; /* coins sliding back into her purse (400 ms) */
    for (int dn = DN_FIVE; dn >= 0; dn--) {
        int n = P->counter[dn];
        if (carrying && mq_wg(q, carry_i)->id == PW_CCOIN && mq_wg(q, carry_i)->arg == dn)
            n--;
        pile(q, dn, counter_x(dn), CY0 + 26, n, false);
    }
    /* her purse (or, for the change, the keys) */
    mq_rrect(q, 4, PY0, 312, PH, 12, RGB(255, 214, 228));
    mq_rrect(q, 6, PY0 + 2, 308, 6, 3, RGB(255, 236, 242));
    if (P->phase == PP_PAY) {
        mq_purse_icon(q, 14, PY0 - 2, 7);
        for (int dn = DN_FIVE; dn >= 0; dn--) {
            int n = P->hand[dn];
            if (carrying && mq_wg(q, carry_i)->id == PW_COIN && mq_wg(q, carry_i)->arg == dn)
                n--;
            pile(q, dn, purse_x(dn), PY0 + 36, n, P->help[dn] > 0);
            if (P->help[dn] > 0) { /* how many of the ringed ones */
                char b[8];
                api->snprintf(b, sizeof b, "%d", P->help[dn]);
                mq_circle(q, purse_x(dn) + 18, PY0 + 12, 7, Q_HOTPINK);
                mq_text_c(q, api->font_small, purse_x(dn) + 18, PY0 + 6, b, Q_WHITE);
            }
        }
    } else if (P->phase == PP_CHANGE) {
        mq_rrect(q, 70, PY0 + 4, 180, 20, 6, Q_WHITE);
        /* "Your change: $0.75": the amount right after the words, by the 1x widths here and by the page's own font
         * widths on the Mac (MODX_HR_MEASURE: a fixed column left a gap there) */
        static const char lab[] = "Your change:";
        api->snprintf(q->tmp, sizeof q->tmp, "$%d.%02d", (int)(P->change_in / 100), (int)(P->change_in % 100));
        int lw = api->text_width(api->font_small, lab), ml = mq_hr_measure(q, api->font_small, lab),
            ma = mq_hr_measure(q, api->font_medium, q->tmp);
        int hr = ml && ma ? mq_hr_text_at(q, api->font_small, 76 * 16, PY0 + 7, ml, lab, Q_DIM) : 0;
        mq_text(q, api->font_small, 76, PY0 + 7, lab, Q_DIM);
        if (hr)
            mq_hr_end(q);
        hr = ml && ma ? mq_hr_text_at(q, api->font_medium, 76 * 16 + ml + 5 * 16, PY0 + 7, ma, q->tmp, Q_HOTPINK) : 0;
        mq_text(q, api->font_medium, 76 + lw + 5, PY0 + 7, q->tmp, Q_HOTPINK);
        if (hr)
            mq_hr_end(q);
    } else if (P->phase == PP_SWAP) { /* "5 pennies for a nickel?" */
        char b[40];
        int x = 40;
        for (int k = 0; k < P->swap_n; k++)
            mq_money_draw(q, P->swap_from, x + k * 26, PY0 + 30, 2); /* (apart: every value seen) */
        x += P->swap_n * 26 + 8;
        mq_text(q, api->font_large, x, PY0 + 20, "=", Q_INK);
        mq_money_draw(q, P->swap_to, x + 40, PY0 + 30, 2);
        api->snprintf(b, sizeof b, "%d %s for a %s?", P->swap_n, dn_names[P->swap_from], dn_name[P->swap_to]);
        mq_text_c(q, api->font_small, 160, PY0 + 48, b, Q_INK);
    } else {
        mq_purse_icon(q, 160, PY0 + 30, 16);
        mq_cents_text(q, api->font_medium, 160, PY0 + 46, (int)mq_purse_cents(q), Q_INK, true);
        if (P->bonus)
            mq_star(q, 200, PY0 + 24, 9 + (int)(q->anim_t / 200 % 2), Q_YELLOW);
    }
    if (bk < 400) /* the wrong ones sliding back */
        for (int dn = 0; dn < DN_COUNT; dn++)
            if (P->back[dn]) {
                int x0 = counter_x(dn), y0 = CY0 + 26, x1 = purse_x(dn), y1 = PY0 + 36;
                mq_money_draw(q, dn, x0 + (x1 - x0) * (int)bk / 400, y0 + (y1 - y0) * (int)bk / 400, 2);
            }
    mq_wg_draw(q);
    if (carrying) /* the coin under her finger */
        mq_money_draw(q, mq_wg(q, carry_i)->arg, fx, fy - 6, 2);
}

/* ------------------------------------------------------------------ input */
static void put(mq_t *q, int dn)
{
    mq_pay_t *P = &q->pay;
    if (P->phase != PP_PAY || dn < 0 || dn >= DN_COUNT || !P->hand[dn])
        return;
    P->hand[dn]--, P->counter[dn]++;
    if (P->help[dn])
        P->help[dn]--;
    P->mood = 0;
    mq_sfx(q, MODX_SFX_COIN, dn);
}
static void take(mq_t *q, int dn)
{
    mq_pay_t *P = &q->pay;
    if (P->phase != PP_PAY || dn < 0 || dn >= DN_COUNT || !P->counter[dn])
        return;
    P->counter[dn]--, P->hand[dn]++;
    mq_sfx(q, MODX_SFX_TAP, 0);
}
static void digit(mq_t *q, int d)
{
    mq_pay_t *P = &q->pay;
    if (P->phase != PP_CHANGE || P->change_n >= 4)
        return;
    P->change_in = P->change_in * 10 + d; /* (the till's way: the digits shift in, cents first) */
    P->change_n = (uint8_t)(P->change_n + (P->change_in > 0));
    mq_sfx(q, MODX_SFX_CLICK, d);
}

static void act(mq_t *q, const mq_wev_t *ev)
{
    mq_pay_t *P = &q->pay;
    switch (ev->id) {
    case PW_COIN:
        if (ev->why == WHY_PRESS || (ev->why == WHY_RELEASE && q->wg.moved && in_counter(ev->x, ev->y)))
            put(q, ev->arg);
        break;
    case PW_CCOIN:
        if (ev->why == WHY_PRESS || (ev->why == WHY_RELEASE && q->wg.moved && in_purse(ev->x, ev->y)))
            take(q, ev->arg);
        break;
    case PW_PAY: pay(q); break;
    case PW_HELP: help(q); break;
    case PW_KEY: digit(q, ev->value); break;
    case PW_DEL:
        if (P->phase == PP_CHANGE)
            P->change_in /= 10, P->change_n = (uint8_t)(P->change_n ? P->change_n - 1 : 0);
        break;
    case PW_OK:
        if (P->phase == PP_CHANGE)
            change_ok(q);
        break;
    case PW_SWAP:
        if (P->phase == PP_SWAP) {
            mq_home_t *h = mq_home(q);
            if (P->hand[P->swap_from] >= P->swap_n) {
                P->hand[P->swap_from] = (uint8_t)(P->hand[P->swap_from] - P->swap_n), P->hand[P->swap_to]++;
                copy(h->hand, P->hand);
                q->dirty = true;
            }
            P->phase = PP_THANKS, P->done_t = mq_mark(q), P->mood = 0;
            say(q, "Much better! Thank you!", 0);
            P->say_v = -1;
            mq_sfx(q, MODX_SFX_COIN, 0);
        }
        break;
    case PW_NOSWAP:
        if (P->phase == PP_SWAP) {
            P->phase = PP_THANKS, P->done_t = mq_mark(q), P->mood = 0;
            say(q, "Okay! Thank you anyway!", 0);
            P->say_v = -1;
        }
        break;
    case PW_DONE: close_counter(q); break;
    default: break;
    }
    q->wg.picked = -1; /* (a tap moves a coin at once: nothing stays picked) */
    q->redraw = true;
}

bool mq_pay_touch(mq_t *q, int type, int x, int y)
{
    if (!q->pay.on)
        return false;
    layout(q);
    mq_wev_t ev;
    if (mq_wg_touch(q, type, x, y, &ev) && ev.idx >= 0 && ev.why != WHY_NONE)
        act(q, &ev);
    else if (type == TV_TAP && q->pay.phase == PP_THANKS)
        close_counter(q);
    q->redraw = true;
    return true; /* (the shop under the counter takes nothing) */
}

bool mq_pay_key(mq_t *q, int k, int c)
{
    mq_pay_t *P = &q->pay;
    if (!P->on)
        return false;
    layout(q);
    if (P->phase == PP_CHANGE && k == HK_DEL) {
        mq_wev_t e = {0};
        e.id = PW_DEL;
        act(q, &e);
        return true;
    }
    mq_wev_t ev;
    if (mq_wg_key(q, k, c, &ev)) {
        if (ev.idx >= 0 && ev.why != WHY_NONE)
            act(q, &ev);
        q->redraw = true;
        return true;
    }
    if (k == HK_ENTER) { /* ENTER with nothing in focus: the phase's main button */
        mq_wev_t e = {0};
        e.why = WHY_KEY;
        e.id = P->phase == PP_PAY ? PW_PAY : P->phase == PP_CHANGE ? PW_OK : P->phase == PP_SWAP ? PW_SWAP : PW_DONE;
        act(q, &e);
    }
    q->redraw = true;
    return true;
}

void mq_pay_frame(mq_t *q)
{
    mq_pay_t *P = &q->pay;
    uint32_t need = (uint32_t)mq_read_ms(q, P->say) > 4000u ? (uint32_t)mq_read_ms(q, P->say) : 4000u; /* (its reading time) */
    if (P->on && P->phase == PP_THANKS && P->done_t && q->anim_t - P->done_t > need) {
        mq_read_note(q, q->anim_t - P->done_t, (int)need, false);
        close_counter(q);
    }
    if (P->back_t && q->anim_t - P->back_t > 400u)
        P->back_t = 0;
}
bool mq_pay_animating(mq_t *q) { return q->pay.on != 0; }

/* ------------------------------------------------------------------ console and state (tests, the bot) */
static bool streq(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}
static int dn_parse(const char *s)
{
    static const char ab[DN_COUNT][3] = {"p", "n", "d", "q", "1", "5"};
    for (int k = 0; k < DN_COUNT; k++)
        if (streq(s, ab[k]) || streq(s, dn_name[k]))
            return k;
    return -1;
}
static int item_parse(mq_t *q, const char *s)
{
    long n;
    if (q->api->con_parse_int(s, &n))
        return (int)n;
    for (int id = 1; id <= MQ_NITEM; id++) { /* a name, '_' for a space */
        const char *a = mq_item[id].name, *b = s;
        while (*a && (*a == *b || (*a == ' ' && *b == '_')))
            a++, b++;
        if (!*a && !*b)
            return id;
    }
    return 0;
}
static void hand_str(char *b, int n, const modx_api_t *api, const uint8_t c[DN_COUNT])
{
    api->snprintf(b, (size_t)n, "%d %d %d %d %d %d", c[0], c[1], c[2], c[3], c[4], c[5]);
}

/* mq purse [compose BAL PRICE MODE RUNG [OVER] | fewest C P N D Q B1 B5]
 * mq pay open ITEM | put DN | take DN | go | help | key D | del | ok | change N | swap | noswap | done | back |
 *        mode [PRICE] | setup COINS_RUNG STREAK [CHANGE_RUNG [N]] */
int mq_purse_console(mq_t *q, int argc, char **argv)
{
    const modx_api_t *api = q->api;
    mq_pay_t *P = &q->pay;
    char b[64];
    long v[7] = {0};
    const char *sub = argc > 2 ? argv[2] : "";
    if (streq(argv[1], "purse")) {
        if (streq(sub, "compose") && argc > 6) {
            for (int i = 0; i < 5 && 3 + i < argc; i++)
                api->con_parse_int(argv[3 + i], &v[i]);
            uint8_t c[DN_COUNT];
            mq_purse_compose((int)v[0], (int)v[1], (int)v[2], (int)v[3], argc > 7 ? (int)v[4] : DN_DOLLAR, (uint32_t)mq_rnd(q, 0, 0x7FFF), c);
            hand_str(b, sizeof b, api, c);
            api->con_printf("%s (%d coins, %d c)\n", b, mq_coins_only(c), mq_coins_sum(c));
            return 0;
        }
        if (streq(sub, "fewest") && argc > 9) {
            uint8_t have[DN_COUNT], out[DN_COUNT];
            for (int i = 0; i < 7; i++)
                api->con_parse_int(argv[3 + i], &v[i]);
            for (int k = 0; k < DN_COUNT; k++)
                have[k] = (uint8_t)v[1 + k];
            int n = mq_coins_fewest((int)v[0], have, out);
            hand_str(b, sizeof b, api, out);
            api->con_printf("%d: %s\n", n, n >= 0 ? b : "-");
            return 0;
        }
        hand_str(b, sizeof b, api, mq_home(q)->hand);
        api->con_printf("purse %lu c: %s\n", (unsigned long)mq_purse_cents(q), b);
        return 0;
    }
    if (streq(sub, "open") && argc > 3) {
        int id = item_parse(q, argv[3]);
        if (mq_buy_check(q, id))
            return api->con_error("cannot buy %s (%d)", argv[3], mq_buy_check(q, id));
        if (q->scene != SC_SHOP && !(q->scene == SC_WARDROBE && mq_item[id].cat == HC_OUTFIT)) /* (clothes: dress-up's too) */
            mq_home_enter(q, SC_SHOP);
        q->shop_pick = (uint8_t)id;
        mq_home_press(q, HB_BUY, id);
        return 0;
    }
    if (streq(sub, "setup") && argc > 4) { /* (tests, the bot) her coins rung, exact payments in a row, [her change rung
                                               * (-1 none), purchases so far (the modes' turn)] */
        for (int i = 0; i < 4 && 3 + i < argc; i++)
            api->con_parse_int(argv[3 + i], &v[i]);
        mq_profile_t *p = mq_prof(q);
        p->sk[S_COINS].tries = (uint8_t)imax(1, p->sk[S_COINS].tries), p->sk[S_COINS].diff = (uint8_t)v[0];
        mq_home(q)->pay_streak = (uint8_t)v[1];
        if (argc > 5 && v[2] >= 0)
            p->sk[S_CHANGE].tries = (uint8_t)imax(1, p->sk[S_CHANGE].tries), p->sk[S_CHANGE].diff = (uint8_t)v[2];
        mq_home(q)->pay_n = (uint8_t)v[3];
        q->dirty = true;
        return 0;
    }
    if (streq(sub, "mode")) {
        long price = 30;
        if (argc > 3)
            api->con_parse_int(argv[3], &price);
        api->con_printf("%d\n", mq_pay_mode(q, (int)price));
        return 0;
    }
    if (!P->on)
        return api->con_error("not paying");
    mq_wev_t e = {0};
    e.why = WHY_PRESS;
    if ((streq(sub, "put") || streq(sub, "take")) && argc > 3) {
        int dn = dn_parse(argv[3]);
        if (dn < 0)
            return api->con_error("no such coin %s", argv[3]);
        e.id = sub[0] == 'p' ? PW_COIN : PW_CCOIN, e.arg = (uint8_t)dn;
    } else if (streq(sub, "key") && argc > 3 && api->con_parse_int(argv[3], &v[0]))
        e.id = PW_KEY, e.value = (int16_t)v[0];
    else if (streq(sub, "change") && argc > 3 && api->con_parse_int(argv[3], &v[0])) {
        P->change_in = (int32_t)v[0], P->change_n = 1;
        e.id = PW_OK;
    } else if (streq(sub, "back")) {
        mq_pay_back(q);
        return 0;
    } else {
        static const char nm[][8] = {"go", "help", "del", "ok", "swap", "noswap", "done"};
        static const uint8_t id[] = {PW_PAY, PW_HELP, PW_DEL, PW_OK, PW_SWAP, PW_NOSWAP, PW_DONE};
        for (int i = 0; i < 7; i++)
            if (streq(sub, nm[i]))
                e.id = id[i];
        if (!e.id)
            return api->con_error("pay: open|put|take|go|help|key|del|ok|change|swap|noswap|done|back|mode");
    }
    act(q, &e);
    return 0;
}

void mq_purse_state(mq_t *q, void (*kv)(const char *, const char *, void *), void *user)
{
    const modx_api_t *api = q->api;
    mq_pay_t *P = &q->pay;
    char b[64];
    int c = (int)mq_purse_cents(q);
    api->snprintf(b, sizeof b, "%d", c);
    kv("mq_coins", b, user); /* (cents in her purse) */
    cents_fmt(b, sizeof b, api, c);
    kv("mq_purse_shown", b, user); /* the badge's text (its cent sign is drawn) */
    hand_str(b, sizeof b, api, mq_home(q)->hand);
    kv("mq_purse_hand", b, user);
    static const char mn[4][8] = {"tap", "count", "change", "picky"}, ph[4][8] = {"pay", "change", "swap", "thanks"};
    if (P->on)
        api->snprintf(b, sizeof b, "%s %s", mn[P->mode & 3], ph[P->phase & 3]);
    else
        api->snprintf(b, sizeof b, "off");
    kv("mq_pay", b, user);
    hand_str(b, sizeof b, api, P->hand);
    kv("mq_pay_hand", b, user);
    hand_str(b, sizeof b, api, P->counter);
    kv("mq_pay_counter", b, user);
    hand_str(b, sizeof b, api, P->help);
    kv("mq_pay_help", b, user);
    api->snprintf(b, sizeof b, "%d %d %ld", P->price, P->paid, (long)P->change_in);
    kv("mq_pay_price", b, user);
    {
        char o[64];
        int n = 0;
        for (const char *s = P->say; *s && n < 60; s++)
            if (*s == '#')
                n += api->snprintf(o + n, sizeof o - (size_t)n, "%dc", (int)P->say_v);
            else
                o[n++] = *s;
        o[n < 63 ? n : 63] = 0;
        kv("mq_pay_say", o, user);
    }
    mq_home_t *h = mq_home(q);
    api->snprintf(b, sizeof b, "%d %d %d %d %d %d", P->rule, P->maxn, P->bonus, h->pay_streak, h->picky_ok, h->pay_n);
    kv("mq_pay_rec", b, user);
    api->snprintf(b, sizeof b, "%d %d %d %d", P->swap_n, P->swap_from, P->swap_to, P->misses);
    kv("mq_pay_swap", b, user);
}
