/*
 * Math Quest: the meta game -- what pulls her back for one more round, without pressure.
 *
 *   The unlock ladder  something new every few stars (about every 1-3 minutes of play early on,
 *                      then every few minutes): a present for her room, a friend moving into
 *                      town, a coin bag, a new shop on the map, squishy dough, a sticker. The
 *                      next one is always shown as a filling bar with its picture ("Soon: a new
 *                      friend!"; never a count of points), on the map and at the end of a round.

 *   Daily quests       three small ones each play day ("Help 5 friends", "Pop 15 bubbles",
 *                      "Decorate a room"), from a pool, each paying coins; all three a bonus.
 *                      A missed day costs nothing.
 *   The town grows     the map gets houses, shops, a fountain and friends walking about as the
 *                      ladder climbs: her world visibly fills up.
 *   The cafe's tips    the cafe earns a coin now and then while she plays anything (play time,
 *                      not real time: no pressure to come back), collected from the tip jar.
 *
 * Saved in mq_home_t (fields taken from its reserved bytes). No timers that punish, no fake
 * scarcity; the grown-ups' daily limit stays in charge.
 */
#include "mq.h"

static int imax(int a, int b) { return a > b ? a : b; }
#define MQ_LADDER_TOP 255 /* (h->ladder is a uint8_t) */

/* stars needed for the k-th unlock: quick at first, then every 15 */
static int ladder_at(int k)
{
    static const uint8_t first[14] = {3, 6, 10, 15, 21, 28, 36, 45, 55, 66, 78, 91, 105, 120};
    return k < 14 ? first[k] : 120 + (k - 13) * 15;
}
enum { UL_GIFT, UL_FRIEND, UL_COINS, UL_SHOP, UL_DOUGH, UL_STICKER, UL_KINDS };
static const char ul_name[UL_KINDS][20] = {"a room present", "a new friend", "a coin bag", "a new shop", "squishy dough",
                                           "a sticker"};

static int ul_kind(int k) { return k % UL_KINDS; }

/* the next unlock: stars still needed (0: due now) and what it is */
int mq_meta_next(mq_t *q, int *need, int *have, const char **what)
{
    mq_home_t *h = mq_home(q);
    int k = h->ladder, at = ladder_at(k), prev = k ? ladder_at(k - 1) : 0, s = mq_prof(q)->stars;
    if (k >= MQ_LADDER_TOP) { /* the ladder climbed to its top (3750 stars): nothing more, never a wrap to the first
                               * rung that unlocked forever (CR2-19) */
        if (need)
            *need = 1;
        if (have)
            *have = 1;
        if (what)
            *what = ul_name[ul_kind(k)];
        return 1;
    }
    if (need)
        *need = at - prev;
    if (have)
        *have = s - prev < 0 ? 0 : s - prev > at - prev ? at - prev : s - prev;
    if (what)
        *what = ul_name[ul_kind(k)];
    return at - s > 0 ? at - s : 0;
}

static void unlock(mq_t *q)
{
    mq_home_t *h = mq_home(q);
    mq_profile_t *p = mq_prof(q);
    if (h->ladder >= MQ_LADDER_TOP)
        return;
    int k = h->ladder++;
    char m[40];
    switch (ul_kind(k)) {
    case UL_GIFT: { /* the cheapest thing for her room she does not have yet */
        int best = 0;
        for (int id = 1; id <= MQ_NITEM; id++)
            if (mq_item_unlocked(p, id) && !h->own[id] && mq_item[id].w && mq_item[id].price &&
                (!best || mq_item[id].price < mq_item[best].price))
                best = id;
        if (best) {
            h->own[best]++;
            q->new_item = (uint8_t)best; /* placed in her room the next time she is there */
            q->api->snprintf(m, sizeof m, "New! %s", mq_item[best].name);
            break;
        }
        mq_earn(q, 15, NULL);
        q->api->snprintf(m, sizeof m, "New! A coin bag");
        break;
    }
    case UL_FRIEND: {
        int c = -1;
        for (int i = 0; i < MQ_NCAST && c < 0; i++)
            if (!((p->met >> i) & 1))
                c = i;
        if (c >= 0) {
            p->met |= (uint16_t)(1 << c);
            q->api->snprintf(m, sizeof m, "%s moved into town!", mq_cast_name(q, c));
        } else
            q->api->snprintf(m, sizeof m, "A party in town!");
        break;
    }
    case UL_COINS: mq_earn(q, 15, NULL); q->api->snprintf(m, sizeof m, "New! A coin bag"); break;
    case UL_SHOP: q->api->snprintf(m, sizeof m, "A new shop opened in town!"); break;
    case UL_DOUGH:
        if (h->dough < 99)
            h->dough++;
        q->api->snprintf(m, sizeof m, "New! Squishy dough");
        break;
    default: {
        int s = -1;
        for (int i = 0; i < MQ_NSTICKER && s < 0; i++)
            if (!((p->stickers >> i) & 1))
                s = i;
        if (s >= 0) {
            p->stickers |= 1u << s;
            q->api->snprintf(m, sizeof m, "New sticker: %s", mq_art_sticker_name[s]);
        } else {
            mq_earn(q, 10, NULL);
            q->api->snprintf(m, sizeof m, "New! A coin bag");
        }
        break;
    }
    }
    mq_toast(q, m);
    mq_keep(q); /* (something new: kept) */
    q->meta_t = mq_mark(q);
    mq_spark_burst(q, 160, MQ_Y0 + 70, 16);
    mq_sfx(q, MODX_SFX_UNLOCK, k);
    q->dirty = true;
}

/* ------------------------------------------------------------------ daily quests */
typedef struct {
    uint8_t ev, n;
    char say[24];
} mq_quest_def_t;
/* (a saved quest is its index here: entries keep their places, new ones go at the end; a quest is a thing
 * to do in the town -- never a score to collect) */
static const mq_quest_def_t qdef[] = {
    {QE_HELP, 5, "Help 5 friends"},       {QE_HELP, 10, "Help 10 friends"},  {QE_POP, 15, "Pop 15 bubbles"},
    {QE_CATCH, 10, "Catch 10 goodies"},  {QE_DECORATE, 1, "Decorate a room"}, {QE_FINALE, 1, "Win a big finale"},
    {QE_ARCADE, 1, "Play in the arcade"}, {QE_HELP, 3, "Help 3 friends"},    {QE_TOY, 1, "Play with a toy"},
    {QE_POP, 20, "Pop 20 bubbles"},       {QE_SHOP, 1, "Buy something nice"}, {QE_CATCH, 15, "Catch 15 goodies"},
};
#define NQDEF ((int)(sizeof qdef / sizeof qdef[0]))

const char *mq_quest_text(mq_t *q, int i, int *have, int *need)
{
    mq_home_t *h = mq_home(q);
    const mq_quest_def_t *d = &qdef[h->quest[i] % NQDEF];
    if (have)
        *have = h->quest_n[i] > d->n ? d->n : h->quest_n[i];
    if (need)
        *need = d->n;
    return d->say;
}

/* a new play day: three new quests (enter_map) */
void mq_quest_roll(mq_t *q)
{
    mq_home_t *h = mq_home(q);
    mq_profile_t *p = mq_prof(q);
    if (h->quest_day == p->days && h->quest_day)
        return;
    h->quest_day = p->days;
    int used = 0;
    /* (its own little generator, from the day and what she has earned: the game's random numbers are not
     * drawn here, so the pool can grow without changing anything else) */
    uint32_t r = (uint32_t)p->days * 2654435761u ^ h->earned * 40503u ^ 0x5A17u;
    for (int i = 0; i < 3; i++) {
        int k, tries = 0;
        do {
            r = r * 1103515245u + 12345u;
            k = (int)((r >> 16) % NQDEF);
        } while (((used >> qdef[k].ev) & 1) && ++tries < 40);

        used |= 1 << qdef[k].ev;
        h->quest[i] = (uint8_t)k, h->quest_n[i] = 0;
    }
    h->quest_done = 0;
    q->dirty = true;
}

void mq_quest_event(mq_t *q, int ev, int n)
{
    mq_home_t *h = mq_home(q);
    if (!mq_prof(q)->used || q->preview)
        return;
    if (!h->quest_day)
        mq_quest_roll(q);
    for (int i = 0; i < 3; i++) {
        const mq_quest_def_t *d = &qdef[h->quest[i] % NQDEF];
        if (d->ev != ev || ((h->quest_done >> i) & 1))
            continue;
        int v = h->quest_n[i] + n;
        h->quest_n[i] = (uint8_t)(v > 250 ? 250 : v);
        if (h->quest_n[i] >= d->n) {
            h->quest_done |= (uint8_t)(1 << i);
            mq_earn(q, 10, "Quest done!");
            mq_sfx(q, MODX_SFX_GIFT, i);
            if (h->quest_done == 7) { /* all three: a bonus */
                mq_earn(q, 20, "All quests done!");
                if (h->dough < 99)
                    h->dough++;
            }
        }
        q->dirty = true;
    }
}

/* ------------------------------------------------------------------ the cafe's tips, the frame */
/* a second of play went by (mq_main.c) */
void mq_meta_second(mq_t *q)
{
    mq_home_t *h = mq_home(q);
    int town = h->ladder > 6 ? 6 : h->ladder;
    int need = 90 - 10 * town - 6 * mq_cafe_treats(q); /* treats on the counter: faster (never below 15 s: CR-02) */
    if (need < 15)
        need = 15;
    if (++h->tip_secs >= need) {
        h->tip_secs = 0;
        if (h->tips < 40)
            h->tips++;
    }
}

int mq_meta_collect(mq_t *q)
{
    mq_home_t *h = mq_home(q);
    int n = h->tips;
    if (n) {
        h->tips = 0;
        mq_earn(q, n, "Cafe tips!");
        mq_sfx(q, MODX_SFX_COIN, n);
    }
    return n;
}

/* safe moments only: not while an order waits for her */
void mq_meta_frame(mq_t *q)
{
    if (!mq_prof(q)->used || q->preview)
        return;
    if (q->scene == SC_TITLE || q->scene == SC_NAME || q->scene == SC_PARENT || q->scene == SC_QUIZ ||
        (q->scene == SC_PLAY && q->sub == GS_PLAY) || q->scene == SC_FACTGAME || q->scene == SC_ACTION)
        return;
    if (mq_toast_busy(q))
        return; /* one banner at a time */
    if (mq_meta_next(q, NULL, NULL, NULL) == 0)
        unlock(q);
}

/* ------------------------------------------------------------------ drawing */
/* the next unlock's picture, in the square of half side r round (cx, cy) */
static void ul_icon(mq_t *q, int kind, int cx, int cy, int r)
{
    switch (kind) {
    case UL_GIFT: /* a present */
        mq_rrect(q, cx - r, cy - r + 3, 2 * r, 2 * r - 3, 2, Q_HOTPINK);
        mq_fill(q, cx - 1, cy - r + 3, 3, 2 * r - 3, Q_YELLOW), mq_fill(q, cx - r, cy, 2 * r, 2, Q_YELLOW);
        mq_heart(q, cx, cy - r + 2, imax(2, r / 2), Q_YELLOW);
        break;
    case UL_FRIEND: mq_heart(q, cx, cy, r, Q_HOTPINK); break; /* a new friend: a heart */
    case UL_COINS: mq_purse_icon(q, cx, cy, r); break;          /* a coin bag */
    case UL_SHOP: /* a little shop: a roof and a door */
        mq_rrect(q, cx - r + 1, cy - 1, 2 * r - 2, r + 1, 2, Q_PEACH);
        mq_fill(q, cx - r, cy - r + 2, 2 * r, 4, Q_HOTPINK);
        mq_fill(q, cx - 2, cy + 2, 4, r - 2, Q_BROWN);
        break;
    case UL_DOUGH: mq_circle(q, cx, cy + 1, r - 1, Q_LAV), mq_circle(q, cx - r / 3, cy - r / 4, imax(1, r / 4), Q_WHITE); break;
    default: mq_star(q, cx, cy, r, Q_YELLOW); break; /* a sticker */
    }
}

/* the ladder's bar: the next unlock as a picture at the end of a filling bar ("a new friend!"), never a
 * count of points, at x, y, w wide */
void mq_meta_bar(mq_t *q, int x, int y, int w)
{
    int need, have;
    const char *what;
    int left = mq_meta_next(q, &need, &have, &what);
    mq_lane(q, x, y - 1, x + w); /* a toast takes the bar's place for a moment (it says what she just won) */
    if (q->earn_msg[0] && (!q->earn_msg_t || q->now - q->earn_msg_t < mq_toast_ms(q)))
        return;
    int bw = w - 18; /* the bar, then the picture of what comes */
    mq_rrect(q, x, y, bw, 14, 7, Q_WHITE);
    mq_rrect(q, x + 1, y + 1, 2 + (bw - 4) * have / (need ? need : 1), 12, 6, (q->anim_t / 300) % 2 && left <= 2 ? Q_YELLOW : Q_PINK);
    int bob = left <= 2 && (q->anim_t / 300) % 2 ? 1 : 0; /* almost there: it wiggles */
    mq_circle(q, x + w - 8, y + 7, 9, Q_WHITE);
    ul_icon(q, ul_kind(mq_home(q)->ladder), x + w - 8, y + 7 - bob, 6);
    q->api->snprintf(q->tmp, sizeof q->tmp, left ? "Soon: %s!" : "Now: %s!", what);
    mq_text_fit(q, q->api->font_small, x + 8, y + 1, bw - 12, q->tmp, Q_INK, false);
}

void mq_meta_state(mq_t *q, void (*kv)(const char *, const char *, void *), void *user)
{
    mq_home_t *h = mq_home(q);
    char b[48];
    q->api->snprintf(b, sizeof b, "%d", h->ladder);
    kv("mq_unlocks", b, user);
    q->api->snprintf(b, sizeof b, "%d", h->tips);
    kv("mq_tips", b, user);
    q->api->snprintf(b, sizeof b, "%d%d%d %d/%d/%d", h->quest_done & 1, (h->quest_done >> 1) & 1, (h->quest_done >> 2) & 1,
                     h->quest_n[0], h->quest_n[1], h->quest_n[2]);
    kv("mq_quests", b, user);
    int need = mq_meta_next(q, NULL, NULL, NULL);
    q->api->snprintf(b, sizeof b, "%d", need);
    kv("mq_next_unlock", b, user);
}
