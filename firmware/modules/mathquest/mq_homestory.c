/*
 * Math Quest: the home story -- a friend who needs her (design: docs/MATHQUEST.md, "The home story").
 *
 * Her room and dress-up are hers, and they are lovely; but a kid who only moves furniture never meets the
 * maths. So the home keeps a quiet clock: the seconds she spends in her home (the room, dress-up, show-off)
 * since she was last outside (`mq_home_t.home_s`, saved). Orders played outside take it back down (a minute
 * of home time for each order at the default), and finishing a friend's errand sets it to nothing. There is
 * never a timer, a meter or a lock on the screen; when the home time is used up (the grown-ups' setting,
 * 6 minutes by default, or off) the story moves, gently and inside the story:
 *
 *   (a) restless: her buddy wanders to the window and looks out, a little picture of what is going on
 *       out there in a thought bubble (a droopy flower, a letter, a parcel, a stopped clock, a star);
 *   (b) the hook (40 s later): a knock, a letter under the door, a friend at the window -- a small card
 *       with the friend and what they need, a "go see!" button and a "later" one (later only closes it);
 *   (c) the walk-out (a minute after that): at a calm moment (no finger down, nothing being dragged, not
 *       paying, no card of hers open, three seconds without a touch) her buddy heads for the door, "Come
 *       on, let's go!"; any touch or key calls it off (it tries again at the next calm moment); else she
 *       is taken to the friend's place and its game, straight to the orders.
 *
 * Finishing a round in the errand's place (that one, or any later round there) is the errand done: the
 * friend's thank-you present (a decoration for her room, shining there when she gets home, or a piece of
 * clothing; a tip when she has everything), the home time is full again, and the done screen's "home"
 * button takes her straight back. Nothing is ever locked; the shop and the book stop the clock (shopping
 * is paying, and paying is maths); paying at a counter never sees the story (CR-12).
 */
#include "mq.h"

#define HOME_DEFAULT_MIN 6
#define HOOK_S 40          /* restless -> the friend's card */
#define OUT_S 100          /* restless -> the buddy takes her out, at a calm moment */
#define CALM_CARD_MS 1500u /* the card comes when nothing has been touched for this long */
#define CALM_OUT_MS 3000u  /* ... and the walk-out after this long */
#define OUT_MS 2500u       /* the walk to the door (any input calls it off) */
#define CARD_X 44
#define CARD_Y (MQ_Y0 + 22)
#define CARD_W 272
#define CARD_H 62

enum { TV_DOWN = 1, TV_DRAG, TV_UP, TV_TAP };
enum { IC_FLOWER, IC_LETTER, IC_PARCEL, IC_CLOCK, IC_STAR, IC_CAKE, IC_COUNT };

/* what each place's friend needs (a story, never a task list), how it reaches her, and its picture */
static const char hook_open[MQ_NWORLD][22] = {"%s waves outside!", "A letter from %s!", "Knock! It's %s!",
                                              "Ding! It's %s!", "Knock! It's %s!", "A note from %s!",
                                              "Knock! It's %s!", "%s waves outside!", "Toot! It's %s!"};
static const char hook_need[MQ_NWORLD][22] = {"The flowers droop!", "Parcels to deliver!", "Lots of customers!",
                                              "The clock stopped!", "The shop is opening!", "Paint the mural!",
                                              "The fair is on!", "A star fell down!", "A ship came in!"};
static const uint8_t hook_icon[MQ_NWORLD] = {IC_FLOWER, IC_LETTER, IC_CAKE, IC_CLOCK, IC_PARCEL, IC_LETTER,
                                             IC_STAR, IC_STAR, IC_PARCEL};

static int friend_of(int w) { return w >= G_SKY ? 8 : w; }

bool mq_hstory_home(int s) { return s == SC_ROOM || s == SC_WARDROBE || s == SC_SHOWOFF; }

/* the home time in seconds (0: off) */
int mq_hstory_budget(mq_t *q)
{
    int m = mq_home(q)->home_min;
    if (m == MQ_HOME_OFF)
        return 0;
    return (m ? m : HOME_DEFAULT_MIN) * 60;
}

/* 0 calm, 1 restless, 2 the hook, 3 the walk-out (when calm) */
int mq_hstory_stage(mq_t *q)
{
    mq_home_t *h = mq_home(q);
    int b = mq_hstory_budget(q);
    if (!b || !mq_prof(q)->used)
        return 0;
    return h->home_s < b ? 0 : h->home_s < b + HOOK_S ? 1 : h->home_s < b + OUT_S ? 2 : 3;
}

static int errand_world(mq_t *q)
{
    mq_home_t *h = mq_home(q);
    return h->errand ? (h->errand - 1) % MQ_NWORLD : 0;
}

int mq_hstory_icon(mq_t *q) { return hook_icon[errand_world(q)]; }

/* she is in the middle of something: never then (a finger down, a drag, paying, a card of hers open, the gate) */
static bool busy(mq_t *q)
{
    bool beat = q->beat != 0xFF && q->anim_t - q->beat_t < q->beat_len + 5000u; /* (a fun beat placing or dressing) */
    return q->finger || q->hm_drag || mq_pay_on(q) || q->shop_pick || q->gate.shown || q->scene == SC_FUN || beat;
}
static bool calm(mq_t *q, uint32_t ms) { return !busy(q) && q->now - q->user_t >= ms; }

/* a new play day starts afresh */
static void day_check(mq_t *q)
{
    mq_home_t *h = mq_home(q);
    uint16_t d = (uint16_t)(mq_prof(q)->days + 1);
    if (h->home_day != d) {
        h->home_day = d, h->home_s = 0;
        h->story_fl &= (uint8_t)~HSF_DISMISSED;
        q->dirty = true;
    }
}

void mq_hstory_frame(mq_t *q, uint32_t dt)
{
    if (!mq_prof(q)->used || q->scene == SC_TITLE || q->scene == SC_PARENT)
        return;
    day_check(q);
    mq_home_t *h = mq_home(q);
    bool home = mq_hstory_home(q->scene);
    if (!home) {
        q->story_card = 0, q->out_t = 0;
        return;
    }
    if (!mq_pay_on(q) && q->now - q->user_t < 60000u) { /* (she is there: a touch or a key in the last minute) */
        uint32_t acc = q->home_acc + dt;
        for (; acc >= 1000u; acc -= 1000u)
            if (h->home_s < 60000u)
                h->home_s++;
        q->home_acc = (uint16_t)acc;
    }
    int st = mq_hstory_stage(q);
    if (st >= 1 && !h->errand) { /* the friend who needs her: the next place in turn */
        h->errand = (uint8_t)((h->errands * 5u + mq_prof(q)->days) % MQ_NWORLD + 1);
        q->dirty = true;
    }
    if (st < 2) {
        q->story_card = 0, q->out_t = 0;
        return;
    }
    /* the hook: a card in the room or dress-up (not over a show-off photo), when nothing is going on */
    if (!q->story_card && !(h->story_fl & HSF_DISMISSED) && q->scene != SC_SHOWOFF && calm(q, CALM_CARD_MS)) {
        q->story_card = 1;
        if (!(h->story_fl & HSF_KNOCKED))
            mq_sfx(q, MODX_SFX_CLICK, 2), mq_sfx(q, MODX_SFX_CLICK, 2); /* knock knock */
        h->story_fl |= HSF_KNOCKED;
        q->dirty = q->redraw = true;
    }
    if (q->story_card == 1 && (h->story_fl & HSF_DISMISSED))
        q->story_card = 0;
    if (st < 3)
        return;
    /* the walk-out: at a calm moment her buddy heads for the door; any touch or key calls it off */
    if (q->out_t && (q->user_t > q->out_t || busy(q))) {
        q->out_t = 0;
        q->story_card = (uint8_t)(h->story_fl & HSF_DISMISSED ? 0 : 1);
        q->redraw = true;
    }
    if (!q->out_t && calm(q, CALM_OUT_MS)) {
        q->out_t = mq_mark_now(q);
        q->story_card = 2;
        h->story_fl |= HSF_KNOCKED;
        mq_speak(q, mq_prof(q)->buddy % MQ_NCAST, "Come on, let's go!", 0);
        q->redraw = true;
    } else if (q->out_t && q->now - q->out_t >= OUT_MS)
        mq_errand_go(q, errand_world(q));
}

/* an order played outside: home time back (a minute for each at the default; a third for a quick fact in the arcade) */
void mq_hstory_order(mq_t *q, int part)
{
    if (mq_hstory_home(q->scene) || q->scene == SC_SHOP || q->scene == SC_BOOK)
        return;
    mq_home_t *h = mq_home(q);
    int b = mq_hstory_budget(q) ? mq_hstory_budget(q) : HOME_DEFAULT_MIN * 60, back = (b / 6 > 30 ? b / 6 : 30) / (part > 0 ? part : 1);
    h->home_s = (uint16_t)(h->home_s > back ? h->home_s - back : 0);
    if (h->home_s < b) {
        h->story_fl &= (uint8_t)~HSF_DISMISSED;
        if (!(h->story_fl & HSF_KNOCKED))
            h->errand = 0; /* (never asked: another friend next time) */
    }
}

/* the friend's thank-you: a decoration (shining in her room when she gets home) or clothes, in turn; a tip
 * when she has everything. -> the item (0xFF: the tip) */
static int thank_you(mq_t *q, int w)
{
    mq_home_t *h = mq_home(q);
    const mq_profile_t *p = mq_prof(q);
    for (int pass = 0; pass < 2; pass++) {
        bool clothes = ((h->errands + pass) & 1) != 0;
        int best = 0, bs = 1 << 30;
        for (int id = 1; id <= MQ_NITEM; id++) {
            const mq_item_t *t = &mq_item[id];
            if (h->own[id] || !t->price || t->price > 50 || (t->flags & (F_GIFT | F_SPECIAL | F_RETIRED)) ||
                !mq_item_unlocked(p, id))
                continue;
            if (clothes ? t->cat != HC_OUTFIT : (t->w == 0 || t->cat == HC_PET))
                continue;
            int sc = (t->set == w ? 0 : 1000) + t->price;
            if (sc < bs)
                bs = sc, best = id;
        }
        if (best) {
            h->own[best]++;
            if (!clothes)
                q->gift_place = (uint8_t)best; /* (in her room when she gets home, shining) */
            mq_check_sets(q);
            return best;
        }
    }
    mq_earn(q, 25, NULL);
    return 0xFF;
}

/* a round finished: in the errand's place, the errand is done */
void mq_hstory_level_done(mq_t *q)
{
    mq_home_t *h = mq_home(q);
    q->errand_gift = 0;
    if (q->preview || !h->errand || !(h->story_fl & HSF_KNOCKED) || errand_world(q) != q->world)
        return;
    int w = errand_world(q);
    q->errand_gift = (uint8_t)thank_you(q, w);
    h->errand = 0, h->story_fl = 0, h->home_s = 0;
    if (h->errands < 255)
        h->errands++;
    char b[64];
    if (q->errand_gift != 0xFF)
        q->api->snprintf(b, sizeof b, "%s: Thank you! A %s for you!", mq_cast_name(q, friend_of(w)), mq_item[q->errand_gift].name);
    else
        q->api->snprintf(b, sizeof b, "%s: Thank you! A tip for you!", mq_cast_name(q, friend_of(w)));
    mq_toast(q, b);
    mq_remember(q, MEM_HELP, friend_of(w), 0, w, 1);
    q->dirty = true;
}

/* ------------------------------------------------------------------ drawing */
void mq_hstory_icon_draw(mq_t *q, int icon, int cx, int cy)
{
    switch (icon) {
    case IC_FLOWER: /* a droopy flower */
        mq_line(q, cx, cy + 5, cx + 1, cy - 1, Q_GREEN);
        mq_line(q, cx + 1, cy - 1, cx + 4, cy, Q_GREEN);
        mq_circle(q, cx + 5, cy + 2, 3, Q_PINK);
        mq_circle(q, cx + 5, cy + 2, 1, Q_YELLOW);
        break;
    case IC_LETTER:
        mq_fill(q, cx - 5, cy - 3, 11, 8, Q_WHITE);
        mq_rect(q, cx - 5, cy - 3, 11, 8, Q_INK);
        mq_line(q, cx - 5, cy - 3, cx, cy + 1, Q_INK);
        mq_line(q, cx, cy + 1, cx + 5, cy - 3, Q_INK);
        mq_heart(q, cx, cy + 2, 2, Q_HOTPINK);
        break;
    case IC_PARCEL:
        mq_fill(q, cx - 5, cy - 3, 11, 9, RGB(214, 168, 112));
        mq_fill(q, cx - 1, cy - 3, 2, 9, Q_HOTPINK);
        mq_fill(q, cx - 5, cy, 11, 2, Q_HOTPINK);
        break;
    case IC_CLOCK:
        mq_circle(q, cx, cy + 1, 5, Q_WHITE);
        mq_ring(q, cx, cy + 1, 5, Q_INK);
        mq_line(q, cx, cy + 1, cx, cy - 2, Q_INK);
        mq_line(q, cx, cy + 1, cx + 3, cy + 1, Q_INK);
        break;
    case IC_CAKE:
        mq_fill(q, cx - 5, cy, 11, 6, Q_PINK);
        mq_fill(q, cx - 5, cy - 1, 11, 2, Q_WHITE);
        mq_circle(q, cx, cy - 3, 1, Q_HOTPINK);
        break;
    default: mq_star(q, cx, cy + 1, 5, Q_YELLOW); break;
    }
}

/* where the restless buddy wanders in the room: the window if one is up, else the door at the right; -1 none */
int mq_hstory_walk_x(mq_t *q)
{
    if (q->out_t)
        return HM_COLS * HM_CELL - 40; /* off to the door */
    if (mq_hstory_stage(q) < 1)
        return -1;
    const mq_room_t *rm = &mq_home(q)->room[q->hm_room % HM_NROOM];
    for (int k = rm->n - 1; k >= 0; k--)
        if (mq_item[rm->it[k].id].cat == HC_WINDOW)
            return rm->it[k].x * HM_CELL + mq_item[rm->it[k].id].w * HM_CELL / 2 - 20;
    return HM_COLS * HM_CELL - 56;
}

static void thought(mq_t *q, int cx, int cy, int icon)
{
    mq_circle(q, cx - 15, cy + 15, 3, Q_LAV);
    mq_circle(q, cx - 15, cy + 15, 2, Q_WHITE);
    mq_circle(q, cx - 10, cy + 9, 4, Q_LAV);
    mq_circle(q, cx - 10, cy + 9, 3, Q_WHITE);
    mq_rrect(q, cx - 13, cy - 11, 27, 21, 8, Q_LAV);
    mq_rrect(q, cx - 12, cy - 10, 25, 19, 7, Q_WHITE);
    mq_hstory_icon_draw(q, icon, cx, cy - 1);
}

void mq_hstory_draw(mq_t *q)
{
    if (!mq_hstory_home(q->scene) || mq_pay_on(q))
        return;
    const modx_api_t *api = q->api;
    int w = errand_world(q);
    if (mq_hstory_stage(q) >= 1 && q->scene == SC_WARDROBE && !q->story_card) /* the buddy's mind is outside */
        thought(q, 96 + (int)(q->anim_t / 500 % 2), MQ_Y0 + 44, hook_icon[w]);
    if (!q->story_card)
        return;
    bool out = q->story_card == 2;
    int nb = 0; /* the screen's buttons under the card are not there while it is up (one target under a finger) */
    for (int i = 0; i < q->nbtn; i++) {
        const mq_btn_t *t = &q->btn[i];
        if (t->x < CARD_X + CARD_W && CARD_X < t->x + t->w && t->y < CARD_Y + CARD_H && CARD_Y < t->y + t->h)
            continue;
        q->btn[nb++] = *t;
    }
    q->nbtn = (uint8_t)nb;
    if (q->focus >= q->nbtn)
        q->focus = 0;
    mq_rrect(q, CARD_X, CARD_Y, CARD_W, CARD_H, 10, out ? Q_MINT : Q_LAV);
    mq_rrect(q, CARD_X + 2, CARD_Y + 2, CARD_W - 4, CARD_H - 4, 9, Q_WHITE);
    int slot = out ? mq_prof(q)->buddy % MQ_NCAST : friend_of(w);
    mq_cast(q, slot, CARD_X + 5, CARD_Y + 5, 34, (q->anim_t / 600) % 2 ? AN_TALK : AN_HAPPY); /* who it is, big */
    mq_hstory_icon_draw(q, hook_icon[w], CARD_X + 22, CARD_Y + 48);
    char b[48];
    const int tx = CARD_X + 44, tw = 142;
    if (out) {
        mq_text_fit(q, api->font_small, tx, CARD_Y + 12, tw, "Come on, let's go!", Q_HOTPINK, false);
        api->snprintf(b, sizeof b, "%s needs us!", mq_cast_name(q, friend_of(w)));
        mq_text_fit(q, api->font_small, tx, CARD_Y + 32, tw, b, Q_INK, false);
        mq_button(q, CARD_X + 190, CARD_Y + 18, 76, 28, HB_STORY, 1, "go!", Q_MINT);
        return;
    }
    api->snprintf(b, sizeof b, hook_open[w], mq_cast_name(q, friend_of(w)));
    mq_text_fit(q, api->font_small, tx, CARD_Y + 8, tw, b, Q_HOTPINK, false);
    mq_text_fit(q, api->font_small, tx, CARD_Y + 24, tw, hook_need[w], Q_INK, false);
    mq_text_fit(q, api->font_small, tx, CARD_Y + 40, tw, "Can you help?", Q_PURPLE, false);
    mq_button(q, CARD_X + 190, CARD_Y + 4, 76, 26, HB_STORY, 1, "go see!", Q_MINT);
    mq_button(q, CARD_X + 190, CARD_Y + 32, 76, 26, HB_STORY, 0, "later", Q_WHITE);
}

/* a touch on the card is the card's (its buttons take the tap); -> true if it was on the card */
bool mq_hstory_touch(mq_t *q, int type, int x, int y)
{
    (void)type;
    return q->story_card && mq_hstory_home(q->scene) && x >= CARD_X && x < CARD_X + CARD_W && y >= CARD_Y &&
           y < CARD_Y + CARD_H;
}

void mq_hstory_press(mq_t *q, int arg)
{
    if (arg == 1) { /* "go see!": her friend's place */
        mq_errand_go(q, errand_world(q));
        return;
    }
    mq_home(q)->story_fl |= HSF_DISMISSED; /* "later": closed, nothing else */
    q->story_card = 0, q->out_t = 0;
    q->dirty = q->redraw = true;
}

/* ------------------------------------------------------------------ console and state (tests, the bot) */
/* mq story | story time S | story min M|off|default | story go | story later */
int mq_hstory_console(mq_t *q, int argc, char **argv)
{
    const modx_api_t *api = q->api;
    mq_home_t *h = mq_home(q);
    long n;
    const char *sub = argc > 2 ? argv[2] : "";
    if (!api->strcmp(sub, "time") && argc > 3 && api->con_parse_int(argv[3], &n)) {
        day_check(q);
        h->home_s = (uint16_t)(n < 0 ? 0 : n > 60000 ? 60000 : n);
        if (h->home_s < mq_hstory_budget(q)) /* (as a refill would: a card closed with "later" can come again) */
            h->story_fl &= (uint8_t)~HSF_DISMISSED;
    } else if (!api->strcmp(sub, "min") && argc > 3) {
        if (!api->strcmp(argv[3], "off"))
            h->home_min = MQ_HOME_OFF;
        else if (!api->strcmp(argv[3], "default"))
            h->home_min = 0;
        else if (api->con_parse_int(argv[3], &n) && n >= 1 && n <= 60)
            h->home_min = (uint8_t)n;
        else
            return api->con_error("story min M|off|default");
        q->dirty = true;
    } else if (!api->strcmp(sub, "go") || !api->strcmp(sub, "later")) {
        if (!q->story_card)
            return api->con_error("no card");
        mq_hstory_press(q, sub[0] == 'g');
    } else if (sub[0])
        return api->con_error("story [time S | min M|off|default | go | later]");
    api->con_printf("story stage %d home %u budget %d errand %d fl %d card %d out %d\n", mq_hstory_stage(q),
                    (unsigned)h->home_s, mq_hstory_budget(q), h->errand, h->story_fl, q->story_card, q->out_t ? 1 : 0);
    return 0;
}

void mq_hstory_state(mq_t *q, void (*kv)(const char *, const char *, void *), void *user)
{
    mq_home_t *h = mq_home(q);
    char b[64];
    /* stage, home seconds, the home time (s, 0 off), the errand (world + 1), the card (1 the hook, 2 the walk-out) */
    q->api->snprintf(b, sizeof b, "%d %u %d %d %d", mq_hstory_stage(q), (unsigned)h->home_s, mq_hstory_budget(q), h->errand,
                     q->story_card);
    kv("mq_story", b, user);
    q->api->snprintf(b, sizeof b, "%d", q->errand_gift);
    kv("mq_errand_gift", b, user);
}
