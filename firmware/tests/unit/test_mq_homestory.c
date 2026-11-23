/* Sparkle Town (modules/mathquest) the home story and the clothes rack (mq_homestory.c, mq_home.c):
 *   - home time escalates gently: restless (the buddy at the window, a thought in dress-up), the friend's card
 *     ("go see!" / "later": later only closes it), then the walk-out at a calm moment, into the friend's place and
 *     its game; nothing on screen is a timer or a lock, and she can go home right after;
 *   - never mid-action: not while a finger is down or a thing is dragged, not while paying (CR-12) in the store or
 *     dress-up, not over a card of hers; any touch calls the walk-out off;
 *   - the home time refills with orders outside, and the errand done fills it (the thank-you present: a
 *     decoration shining in her room, or clothes); the grown-ups' setting changes it or turns it off;
 *   - dress-up sells: today's rack (new pieces each play day, none she has), paid at the same counter as the store
 *     (the price out of her purse, the piece hers and on), a tap before she counts coins; the store's Wear tab sells
 *     the rack only;
 *   - the save: an older save (the new bytes zero) keeps her wardrobe and rooms, and starts the story afresh. */
#include "mq_test_util.h"

enum { TV_DOWN = 1, TV_DRAG, TV_UP, TV_TAP };

static void player(void)
{
    fresh_module();
    con("new Ada 3");
    q->fun_off = true;
    con("engine stop");
    mq_prof(q)->days = mq_prof(q)->days ? mq_prof(q)->days : 1;
    mq_quest_roll(q);
    mq_home(q)->quest_done = 7;
}
static void touch(int type, int x, int y) { ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(type, x, y)); }
/* she is there (a touch a moment ago), then n frames */
static void there(int n)
{
    q->user_t = q->now;
    char b[24];
    snprintf(b, sizeof b, "step %d", n);
    con(b);
}
/* frames with her hands still (the calm moments) */
static void still(int n)
{
    char b[24];
    snprintf(b, sizeof b, "step %d", n);
    con(b);
}
static void coins_on(int rung)
{
    mq_skrec_t *s = &mq_prof(q)->sk[S_COINS];
    s->tries = s->tries ? s->tries : 1, s->diff = (uint8_t)rung;
}
static bool card_button(int arg, int *x, int *y)
{
    int r[4];
    ops.draw(q, 0, r);
    for (int i = 0; i < q->nbtn; i++)
        if (q->btn[i].id == HB_STORY && q->btn[i].arg == arg) {
            *x = q->btn[i].x + q->btn[i].w / 2, *y = q->btn[i].y + q->btn[i].h / 2;
            return true;
        }
    return false;
}
static void finish_round(void)
{
    for (int i = 0; i < 14 && q->scene != SC_DONE_LEVEL; i++) {
        con("correct");
        con("settle"), con("step 40");
    }
}

/* restless, the card, later, the walk-out, out to the errand; the errand done; home again */
static void test_escalate(void)
{
    player();
    mq_home_t *h = mq_home(q);
    con("go room");
    CHECK(mq_hstory_budget(q) == 360 && mq_hstory_stage(q) == 0);
    /* the clock runs while she is there (and not when she is gone: no touch for a minute) */
    con("story time 100");
    for (int i = 0; i < 10; i++)
        there(25); /* 10 x 1 s */
    CHECK(h->home_s >= 109 && h->home_s <= 111);
    q->user_t = q->now - 70000u;
    still(100);
    CHECK(h->home_s >= 109 && h->home_s <= 111); /* (gone: it waits) */
    /* (a) restless: the buddy goes to the window and looks out (a thought of what is out there) */
    con("story time 360");
    there(150);
    CHECK(mq_hstory_stage(q) == 1 && h->errand >= 1 && h->errand <= MQ_NWORLD);
    CHECK(q->scene == SC_ROOM && !q->story_card);
    CHECK(q->wk_tx[0] == mq_hstory_walk_x(q) && q->wk_x[0] == q->wk_tx[0]);
    bool looked = false;
    for (int i = 0; i < 100 && !looked; i++)
        there(1), looked = q->wk_react[0] >= 51;
    CHECK(looked);
    /* (b) the hook: a card at a calm moment, with "go see!" and "later" */
    con("story time 400");
    CHECK(mq_hstory_stage(q) == 2);
    there(10);
    CHECK(!q->story_card); /* (her hands were just busy) */
    still(50);
    CHECK(q->story_card == 1 && (h->story_fl & HSF_KNOCKED));
    int x, y;
    CHECK(card_button(1, &x, &y) && card_button(0, &x, &y));
    touch(TV_TAP, x, y); /* "later": only closes it */
    CHECK(q->scene == SC_ROOM && !q->story_card && (h->story_fl & HSF_DISMISSED));
    still(100);
    CHECK(q->scene == SC_ROOM && !q->story_card);
    /* (c) the walk-out, a minute on: at a calm moment; a touch calls it off */
    con("story time 470");
    q->user_t = q->now;
    still(70);
    CHECK(!q->out_t && q->scene == SC_ROOM); /* (2.8 s: not calm yet) */
    still(10);
    CHECK(q->out_t && q->story_card == 2 && q->scene == SC_ROOM);
    touch(TV_TAP, 160, MQ_Y0 + 4); /* (a touch on the title line) */
    still(1);
    CHECK(!q->out_t && q->scene == SC_ROOM);
    still(80); /* (calm again: 3 s) */
    CHECK(q->out_t);
    int w = h->errand - 1;
    still(70);
    CHECK(q->scene == SC_PLAY || q->scene == SC_QUIZ);
    CHECK(q->world == w && h->errand == w + 1);
    /* the errand: a round in the friend's place; the thank-you; the home time full again; home right after */
    int own0 = 0;
    for (int id = 1; id <= MQ_NITEM; id++)
        own0 += h->own[id];
    finish_round();
    CHECK(q->scene == SC_DONE_LEVEL);
    CHECK(q->errand_gift && h->errand == 0 && h->home_s == 0 && h->errands == 1 && mq_hstory_stage(q) == 0);
    int own1 = 0;
    for (int id = 1; id <= MQ_NITEM; id++)
        own1 += h->own[id];
    CHECK(q->errand_gift == 0xFF || (own1 >= own0 + 1 && h->own[q->errand_gift] == 1));
    int gift = q->errand_gift;
    bool home_btn = false;
    {
        int r[4];
        ops.draw(q, 0, r);
        for (int i = 0; i < q->wg.n; i++) /* (the done screen's ways on are widgets) */
            home_btn |= q->wg.w[i].id == B_GOHOME;
    }
    CHECK(home_btn);
    mq_press(q, B_GOHOME, 0);
    CHECK(q->scene == SC_ROOM);
    if (gift != 0xFF && mq_item[gift].cat != HC_OUTFIT) { /* a decoration: already in her room, shining */
        const mq_room_t *rm = &h->room[q->hm_room];
        bool placed = false;
        for (int k = 0; k < rm->n; k++)
            placed |= rm->it[k].id == gift;
        CHECK(placed);
    }
    /* the next errand: the other kind of present (clothes after a decoration) */
    con("story time 500");
    still(80);
    CHECK(q->scene == SC_PLAY || q->scene == SC_QUIZ);
    finish_round();
    CHECK(q->errand_gift && h->errands == 2);
    if (gift != 0xFF && q->errand_gift != 0xFF)
        CHECK((mq_item[gift].cat == HC_OUTFIT) != (mq_item[q->errand_gift].cat == HC_OUTFIT));
}

/* "go see!" goes; an errand left half done stays (nothing locked: she can come home any time) */
static void test_go_see(void)
{
    player();
    mq_home_t *h = mq_home(q);
    con("go wardrobe");
    con("story time 365");
    there(20);
    CHECK(mq_hstory_stage(q) == 1 && !q->story_card);
    con("story time 410");
    still(50);
    CHECK(q->story_card == 1 && q->scene == SC_WARDROBE);
    int x, y, w = h->errand - 1;
    CHECK(card_button(1, &x, &y));
    touch(TV_TAP, x, y);
    CHECK((q->scene == SC_PLAY || q->scene == SC_QUIZ) && q->world == w);
    /* two orders, then home: the home time came back down, the errand still asked */
    con("correct"), con("settle"), con("step 40"), con("correct"), con("settle"), con("step 40");
    CHECK(h->home_s >= 410 - 120 && h->home_s <= 410 + 5 - 120 && h->errand == w + 1); /* (a few seconds more at home) */
    con("go room");
    CHECK(q->scene == SC_ROOM && mq_hstory_stage(q) == 0);
    there(30);
    CHECK(q->scene == SC_ROOM && !q->story_card);
    /* a round in that place later on: the errand done */
    con("go map");
    char b[24];
    snprintf(b, sizeof b, "world %d", w);
    con(b);
    mq_errand_go(q, w);
    finish_round();
    CHECK(q->errand_gift && h->errand == 0);
}

/* never mid-drag, never while paying, never over a card of hers */
static void test_never_mid_action(void)
{
    player();
    mq_home_t *h = mq_home(q);
    con("go room");
    con("story time 500");
    /* a finger down on the bed and dragging it: nothing, however long */
    touch(TV_DOWN, 24, MQ_Y0 + 20 + 6 * 16 + 8);
    CHECK(q->hm_drag == 1);
    touch(TV_DRAG, 40, MQ_Y0 + 20 + 6 * 16 + 8);
    still(400);
    CHECK(q->scene == SC_ROOM && !q->out_t && !q->story_card && q->hm_drag == 1);
    touch(TV_UP, 40, MQ_Y0 + 20 + 6 * 16 + 8);
    CHECK(q->hm_drag == 0);
    still(30);
    CHECK(q->scene == SC_ROOM && !q->out_t); /* (just let go: not yet) */
    still(60);
    CHECK(q->out_t || q->scene != SC_ROOM); /* (calm now: off she goes) */
    /* paying in dress-up: the counter stays, nothing comes over it */
    player();
    h = mq_home(q);
    con("coins 900");
    coins_on(1);
    con("pay setup 1 0");
    con("go wardrobe");
    mq_rack_fill(q);
    int id = h->rack[0];
    CHECK(id && !h->own[id]);
    mq_home_press(q, HB_CARD, id);
    mq_home_press(q, HB_BUY, id);
    CHECK(q->pay.on && q->scene == SC_WARDROBE);
    con("pay put p");
    con("story time 600");
    still(600);
    CHECK(q->scene == SC_WARDROBE && q->pay.on && !q->story_card && !q->out_t && q->pay.counter[DN_PENNY] == 1);
    CHECK(h->home_s == 600); /* (paying is maths: the clock stops) */
    /* the store's counter too (CR-12) */
    con("pay back"), con("pay back");
    CHECK(!q->pay.on);
    con("go shop");
    q->shop_pick = 0;
    for (int i = 1; i <= MQ_NITEM; i++)
        if (!mq_buy_check(q, i) && mq_item[i].cat != HC_OUTFIT && mq_item[i].price >= 20) {
            id = i;
            break;
        }
    q->shop_pick = (uint8_t)id;
    mq_home_press(q, HB_BUY, id);
    CHECK(q->pay.on);
    still(600);
    CHECK(q->scene == SC_SHOP && q->pay.on && !q->story_card);
    /* a rack piece up close in dress-up (her card): no friend's card over it, no walk-out */
    con("pay back"), con("pay back");
    con("go wardrobe");
    mq_home_press(q, HB_CARD, h->rack[1] ? h->rack[1] : h->rack[0]);
    CHECK(q->shop_pick);
    still(300);
    CHECK(q->scene == SC_WARDROBE && !q->story_card && !q->out_t);
}

/* orders outside take the home time back down; the errand fills it; the grown-ups' setting */
static void test_budget(void)
{
    player();
    mq_home_t *h = mq_home(q);
    con("story time 420");
    con("level 0 0");
    con("correct"), con("settle"), con("step 40");
    CHECK(h->home_s == 360);
    con("wrong"), con("settle"), con("step 40"); /* (a miss is an order played too: never a punishment) */
    CHECK(h->home_s == 300);
    for (int i = 0; i < 12; i++) {
        if (q->scene == SC_DONE_LEVEL)
            con("level 0 0");
        con("correct"), con("settle"), con("step 40");
    }
    CHECK(h->home_s == 0);
    /* the grown-ups: 3, 10 minutes, off */
    mq_press(q, B_HOMEMIN, 3);
    CHECK(mq_hstory_budget(q) == 180);
    con("story time 200");
    CHECK(mq_hstory_stage(q) == 1);
    mq_press(q, B_HOMEMIN, 10);
    CHECK(mq_hstory_budget(q) == 600 && mq_hstory_stage(q) == 0);
    mq_press(q, B_HOMEMIN, 0);
    CHECK(mq_hstory_budget(q) == 0 && h->home_min == MQ_HOME_OFF);
    con("go room");
    con("story time 5000");
    still(300);
    CHECK(q->scene == SC_ROOM && mq_hstory_stage(q) == 0 && !q->story_card && !q->out_t);
    CHECK(con("story min default") == 0 && mq_hstory_budget(q) == 360);
    /* a new play day starts afresh */
    mq_prof(q)->days++;
    still(2);
    CHECK(h->home_s == 0);
    /* the parent screen draws the setting */
    con("go parent");
    q->tab = 2;
    int r[4], n = 0;
    ops.draw(q, 0, r);
    for (int i = 0; i < q->nbtn; i++)
        n += q->btn[i].id == B_HOMEMIN;
    CHECK(n == 5);
}

/* dress-up sells: the rack, the counter, the coins, the wardrobe */
static void test_rack(void)
{
    player();
    mq_home_t *h = mq_home(q);
    con("go wardrobe");
    CHECK(mq_rack_fill(q) == MQ_RACK_N);
    for (int k = 0; k < MQ_RACK_N; k++) {
        int id = h->rack[k];
        CHECK(id && mq_item[id].cat == HC_OUTFIT && !h->own[id] && mq_item[id].price >= 1 && mq_item[id].price <= 1000);
        for (int j = 0; j < k; j++)
            CHECK(h->rack[j] != id);
    }
    /* not on the rack: not sold (in dress-up or the store); the starter outfit stays hers */
    int off = 0, starters = 0;
    for (int id = 1; id <= MQ_NITEM; id++) {
        if (mq_item[id].cat != HC_OUTFIT)
            continue;
        starters += h->own[id] > 0;
        if (!h->own[id] && !mq_rack_has(q, id) && mq_item[id].price)
            off = id;
    }
    CHECK(starters >= 3 && off && mq_buy_check(q, off) == -2);
    /* a tap buys before she counts coins */
    con("coins 300");
    int a = h->rack[0];
    uint32_t c0 = mq_purse_cents(q);
    mq_home_press(q, HB_CARD, a);
    CHECK(q->shop_pick == a);
    mq_home_press(q, HB_BUY, a);
    CHECK(!q->pay.on && h->own[a] == 1 && mq_purse_cents(q) == c0 - mq_item[a].price);
    CHECK(h->wear[mq_item[a].extra % HM_NSLOT] == a); /* (bought in dress-up: on at once) */
    CHECK(mq_buy_check(q, a) == -4);
    /* at the counter: the exact coins, the price out of her purse, the piece hers */
    coins_on(1);
    con("pay setup 1 0");
    int b = h->rack[1];
    q->shop_pick = 0;
    mq_home_press(q, HB_CARD, b);
    mq_home_press(q, HB_BUY, b);
    CHECK(q->pay.on && q->pay.price == mq_item[b].price && q->scene == SC_WARDROBE);
    uint8_t ws[DN_COUNT];
    CHECK(mq_coins_fewest(q->pay.price, q->pay.hand, ws) >= 0);
    static const char ab[DN_COUNT][3] = {"p", "n", "d", "q", "1", "5"};
    for (int k = 0; k < DN_COUNT; k++)
        for (int n = 0; n < ws[k]; n++) {
            char s[16];
            snprintf(s, sizeof s, "pay put %s", ab[k]);
            con(s);
        }
    c0 = mq_purse_cents(q);
    con("pay go");
    CHECK(h->own[b] == 1 && mq_purse_cents(q) == c0 - mq_item[b].price);
    CHECK(mq_coins_sum(h->hand) == (int)h->purse);
    CHECK(mq_prof(q)->sk[S_COINS].tries >= 1); /* (one answer of counting coins) */
    con("pay done");
    CHECK(!q->pay.on && q->scene == SC_WARDROBE);
    /* hers in the wardrobe's "yours" */
    int r[4], seen = 0;
    q->shop_pick = 0;
    ops.draw(q, 0, r);
    for (int i = 0; i < q->nbtn; i++)
        seen += q->btn[i].id == HB_WEAR && (q->btn[i].arg == a || q->btn[i].arg == b);
    CHECK(seen == 2);
    /* not enough coins: the card says how much more, buy does nothing */
    con("coins 1");
    int c = h->rack[2];
    mq_home_press(q, HB_CARD, c);
    mq_home_press(q, HB_BUY, c);
    CHECK(!q->pay.on && !h->own[c] && mq_purse_cents(q) == 1);
    /* the store's Wear tab sells the rack too, the rest is "soon" */
    con("coins 300");
    con("go shop");
    CHECK(mq_buy_check(q, c) == 0 && mq_buy_check(q, off) == -2);
    /* the next play day: new pieces, none she has */
    mq_prof(q)->days++;
    CHECK(mq_rack_fill(q) >= 1);
    for (int k = 0; k < MQ_RACK_N; k++)
        CHECK(!h->rack[k] || !h->own[h->rack[k]]);
    /* everything hers: an empty rack, "tomorrow" */
    for (int id = 1; id <= MQ_NITEM; id++)
        if (mq_item[id].cat == HC_OUTFIT && mq_item[id].price)
            h->own[id] = 1;
    mq_prof(q)->days++;
    CHECK(mq_rack_fill(q) == 0);
    con("go wardrobe");
    ops.draw(q, 0, r);
}

/* an older save: the story's and the rack's bytes zero -- her wardrobe and rooms kept, the story fresh */
static void test_save(void)
{
    player();
    mq_home_t *h = mq_home(q);
    CHECK(sizeof(mq_home_t) == 1024);
    con("coins 300");
    con("buy Teddy");
    con("place Teddy 9 6");
    h->own[IT_CHEFHAT] = 1, h->wear[OS_HEAD] = IT_CHEFHAT;
    mq_home_t before = *h;
    /* the bytes an older save had as reserved: zero */
    h->home_s = h->home_day = h->rack_day = 0;
    h->home_min = h->errand = h->story_fl = h->errands = 0;
    for (int k = 0; k < MQ_RACK_N; k++)
        h->rack[k] = 0;
    con("save");
    fresh_module();
    h = mq_home(q);
    CHECK(!memcmp(h->own, before.own, sizeof h->own) && !memcmp(h->wear, before.wear, sizeof h->wear));
    CHECK(!memcmp(h->room, before.room, sizeof h->room));
    CHECK(h->purse == before.purse && mq_coins_sum(h->hand) == (int)h->purse);
    CHECK(mq_hstory_budget(q) == 360 && h->home_s == 0 && mq_hstory_stage(q) == 0);
    CHECK(mq_rack_fill(q) == MQ_RACK_N);
    /* the new bytes are kept by a save */
    h->home_min = 10, h->home_s = 123, h->errand = 3, h->errands = 2;
    uint8_t rk = h->rack[0];
    con("save");
    fresh_module();
    h = mq_home(q);
    CHECK(h->home_min == 10 && h->home_s == 123 && h->errand == 3 && h->errands == 2 && h->rack[0] == rk);
    printf("save: %u bytes (mq_home_t %u, home story and rack %u of its reserved bytes)\n", (unsigned)sizeof(mq_save_t),
           (unsigned)sizeof(mq_home_t), (unsigned)(10 + MQ_RACK_N));
}

int main(void)
{
    harness_init();
    test_escalate();
    test_go_see();
    test_never_mid_action();
    test_budget();
    test_rack();
    test_save();
    printf("test_mq_homestory: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
