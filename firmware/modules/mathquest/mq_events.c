/*
 * Math Quest: surprises -- a special thing on most play days, never the same one twice in a row.
 *
 * Whether a day brings something is a little hash of the play day and her name (a surprise, but
 * the same all day): about two play days in three have something special, the first two days stay
 * calm. WHICH one is the one that has been away longest (mq_more_t.vis_day, .fest_day), so all
 * eight visitors and all nine festivals come round before any comes back -- weeks of play before
 * a repeat. Today's special is kept once announced (mq_more_t.ev_day / ev_kind / ev_arg).
 *
 *   A visitor     a traveller comes to town with a wish from the Sparkle Kitchen: Sir Sprinkles the
 *                 unicorn, Luna the moon bunny, Captain Puff the sheep, Coco the koala, Biscuit the
 *                 corgi, Ziggy the little dragon, Pearl the jellyfish, Bao the panda. In the kitchen
 *                 the wish is a golden one; granting it brings a big thank-you, a keepsake and
 *                 coins. Tomorrow the visitor travels on (nothing is lost if she never gets to it).
 *   Lantern night the town at dusk with lanterns strung along the street and fireworks; the games
 *                 are played at night (night mode).
 *   Bubble day    golden bubbles float about everywhere: many more of them in Bubble Pop, and soap
 *                 bubbles drifting over the town.
 *   Parade        every friend she has met walks along the street in a line with balloons; hearts
 *                 count double.
 *   Snow day      snow falls over the town and every game; a snowman stands by the street.
 *   Rainbow day   a rainbow over the town and behind every game.
 *   Picnic day    a picnic on the grass with two friends and a basket; orders bring double
 *                 sparkle energy for the kitchen.
 *   Kite day      kites with ribbon tails dance in the sky, over the town and the games.
 *   A birthday    one of her friends has a birthday party by the street (a cake with candles,
 *                 balloons, a banner); hearts with them count double.
 *   Star shower   shooting stars all evening: the town and the games at night.
 *
 * Everything is drawn from shapes (resolution independent). Nothing here has a timer.
 */
#include "mq.h"

static int iabs(int a) { return a < 0 ? -a : a; }

static const char vis_name[MQ_NVISITOR][14] = {"Sir Sprinkles", "Luna", "Captain Puff", "Coco",
                                               "Biscuit",       "Ziggy", "Pearl",       "Bao"};
static const char vis_from[MQ_NVISITOR][22] = {"Rainbow Meadow", "the Moon",     "the Cloud Sea", "the Gum Tree Hills",
                                               "the seaside",    "Dragon Peak",  "the Coral Reef", "the Bamboo Woods"};
const char *mq_visitor_name(int v) { return vis_name[((v % MQ_NVISITOR) + MQ_NVISITOR) % MQ_NVISITOR]; }

/* the hash of a play day and her name */
static uint32_t day_hash(const mq_profile_t *p)
{
    uint32_t h = (uint32_t)p->days * 2654435761u;
    for (const char *s = p->name; *s; s++)
        h = (h ^ (uint8_t)*s) * 16777619u;
    return h ^ (h >> 15);
}

/* what a day brings, before it is announced: a visitor (the one away longest), a festival (the one
 * not seen for longest), or nothing; arg: the visitor, the friend with a birthday */
static int roll(mq_t *q, int *arg)
{
    mq_profile_t *p = mq_prof(q);
    mq_more_t *m = mq_more(q);
    *arg = 0;
    if (!p->used || p->days < 3)
        return EV_NONE;
    uint32_t h = day_hash(p);
    int r = (int)(h % 20u), rot = (int)(h / 20u % 64u);
    if (r < 5) { /* a visitor */
        int best = -1;
        for (int i = 0; i < MQ_NVISITOR; i++) {
            int v = (i + rot) % MQ_NVISITOR;
            if (best < 0 || m->vis_day[v] < m->vis_day[best])
                best = v;
        }
        *arg = best;
        return EV_VISITOR;
    }
    if (r < 14) { /* a festival */
        int best = -1;
        for (int i = 0; i < EV_NFEST; i++) {
            int f = (i + rot) % EV_NFEST;
            if (best < 0 || m->fest_day[f] < m->fest_day[best])
                best = f;
        }
        int ev = EV_FEST0 + best;
        if (ev == EV_BIRTHDAY) /* a friend she has met (Pip is there from the start) */
            for (int i = 0; i < MQ_NCAST; i++) {
                int s = (p->days + i) % MQ_NCAST;
                if (((p->met >> s) & 1) || s == 0) {
                    *arg = s;
                    break;
                }
            }
        return ev;
    }
    return EV_NONE;
}

static int today(mq_t *q, int *arg)
{
    mq_more_t *m = mq_more(q);
    mq_profile_t *p = mq_prof(q);
    if (p->used && m->ev_day && m->ev_day == p->days && m->ev_kind < EV_COUNT) {
        *arg = m->ev_arg;
        return m->ev_kind;
    }
    return roll(q, arg);
}

/* today's special (EV_*) */
int mq_event_today(mq_t *q)
{
    int a;
    return today(q, &a);
}
int mq_event_arg(mq_t *q)
{
    int a = 0;
    today(q, &a);
    return a;
}

/* tests: today is KIND (with ARG); a negative kind forgets it */
void mq_event_force(mq_t *q, int kind, int arg)
{
    mq_more_t *m = mq_more(q);
    if (kind < 0 || kind >= EV_COUNT) {
        m->ev_day = 0;
        return;
    }
    m->ev_day = mq_prof(q)->days, m->ev_kind = (uint8_t)kind, m->ev_arg = (uint8_t)arg;
    mq_extra(q)->visit_day = 0; /* (announced again) */
}

bool mq_events_night(mq_t *q)
{
    int e = mq_event_today(q);
    return e == EV_LANTERN || e == EV_STARS;
}
int mq_events_birthday(mq_t *q)
{
    int a = 0;
    return today(q, &a) == EV_BIRTHDAY ? a % MQ_NCAST : -1;
}
int mq_events_energy(mq_t *q) { return mq_event_today(q) == EV_PICNIC ? 2 : 1; }

static const char ev_label[EV_COUNT][16] = {"",         "A visitor!",  "Lantern night", "Bubble day", "Parade day", "Snow day",
                                            "Rainbow day", "Picnic day", "Kite day",      "A birthday!", "Star shower"};
const char *mq_event_label(mq_t *q)
{
    int e = mq_event_today(q);
    return e > EV_NONE && e < EV_COUNT ? ev_label[e] : mq_event_name(mq_event(mq_prof(q)));
}

/* what a friend says about today's special (the town's chatter) */
const char *mq_event_chat(mq_t *q)
{
    static const char chat[EV_COUNT][32] = {"",
                                            "Did you see who came to town?",
                                            "The lanterns are so pretty!",
                                            "Bubbles everywhere! Pop!",
                                            "March with us! Hooray!",
                                            "Let's build a snowman!",
                                            "Look! A rainbow!",
                                            "Come to our picnic!",
                                            "My kite flies the highest!",
                                            "It's a party! Cake time!",
                                            "Make a wish on a star!"};
    int e = mq_event_today(q);
    return e > EV_NONE && e < EV_COUNT ? chat[e] : "";
}

/* the visitor of today and their wish (chain x 16 + level); -1: no visitor or the wish was granted */
int mq_visitor(mq_t *q, int *wish)
{
    mq_extra_t *e = mq_extra(q);
    int a = 0;
    if (today(q, &a) != EV_VISITOR)
        return -1;
    int d = mq_prof(q)->days;
    if (e->visit_day == d && e->visit_got)
        return -1;
    if (wish)
        *wish = CK_WHOLE(CK_PINK); /* a whole cake from the Sparkle Kitchen (fresh from its oven, uncut) */
    return a % MQ_NVISITOR;
}

/* a new play day's special: announced when she first sees the town that day (and remembered: who
 * came, which festival, for the rotation) */
void mq_events_enter(mq_t *q)
{
    mq_extra_t *e = mq_extra(q);
    mq_more_t *m = mq_more(q);
    int d = mq_prof(q)->days, arg = 0, ev = today(q, &arg);
    if (e->visit_day == d || !ev)
        return;
    e->visit_day = (uint16_t)d, e->visit_got = 0, e->visitor = (uint8_t)(ev == EV_VISITOR ? arg + 1 : 0);
    m->ev_day = (uint16_t)d, m->ev_kind = (uint8_t)ev, m->ev_arg = (uint8_t)arg;
    m->fests_seen = (uint16_t)(m->fests_seen | 1u << ev);
    if (ev == EV_VISITOR) {
        m->vis_day[arg % MQ_NVISITOR] = (uint16_t)d;
        m->visitors_met = (uint16_t)(m->visitors_met | 1u << (arg % MQ_NVISITOR));
        q->api->snprintf(q->toast_in, sizeof q->toast_in, "%s came from %s!", vis_name[arg % MQ_NVISITOR],
                         vis_from[arg % MQ_NVISITOR]);
    } else {
        m->fest_day[ev - EV_FEST0] = (uint16_t)d;
        static const char msg[EV_COUNT][32] = {"", "", "Lantern night tonight!", "It's bubble day!", "A parade of friends today!",
                                               "It's snowing in town!", "A rainbow over the town!", "A picnic today!",
                                               "It's kite day!", "", "Shooting stars tonight!"};
        if (ev == EV_BIRTHDAY)
            q->api->snprintf(q->toast_in, sizeof q->toast_in, "It's %s's birthday!", mq_cast_name(q, arg % MQ_NCAST));
        else
            q->api->snprintf(q->toast_in, sizeof q->toast_in, "%s", msg[ev]);
    }
    mq_toast(q, q->toast_in);
    mq_sfx(q, MODX_SFX_FANFARE, ev);
    q->dirty = true;
}

/* the wish granted: a keepsake from far away, coins, a happy visitor */
void mq_visitor_thanks(mq_t *q)
{
    mq_extra_t *e = mq_extra(q);
    mq_home_t *h = mq_home(q);
    e->visit_got = 1;
    int best = 0; /* a gift-only keepsake she does not have yet */
    for (int id = 1; id <= MQ_NITEM; id++)
        if ((mq_item[id].flags & F_GIFT) && !h->own[id] && (!best || mq_rnd(q, 0, 2) == 0))
            best = id;
    if (best)
        h->own[best]++;
    char m[40];
    q->api->snprintf(m, sizeof m, best ? "A %s from far away!" : "A big thank-you! +50", best ? mq_item[best].name : "");
    mq_earn(q, best ? 30 : 50, m);
    mq_remember(q, MEM_GIFT, mq_prof(q)->buddy % MQ_NCAST, 0, 0, 1);
    mq_sfx(q, MODX_SFX_FANFARE, 9);
    q->dirty = true;
}

/* ------------------------------------------------------------------ drawing */
#define DARK RGB(60, 56, 70)
/* a visitor (from shapes) in a size x size box, its feet on the box's bottom */
void mq_visitor_draw(mq_t *q, int v, int x, int y, int size, bool happy)
{
    int z = size, cx = x + z / 2, by = y + z, hop = happy ? (int)(q->anim_t / 120 % 4 == 1 ? z / 10 : 0) : 0;
    by -= hop;
#define U(n) ((n) * z / 40)
    int blink = (q->anim_t / 150 + (uint32_t)v * 7) % 24 == 0; /* they blink now and then */
    switch (((v % MQ_NVISITOR) + MQ_NVISITOR) % MQ_NVISITOR) {
    case 0: { /* Sir Sprinkles: a white unicorn with a rainbow mane and a golden horn */
        mq_oval(q, cx, by - U(10), U(14), U(9), Q_WHITE);
        mq_fill(q, cx - U(10), by - U(6), U(3), U(6), Q_WHITE), mq_fill(q, cx + U(7), by - U(6), U(3), U(6), Q_WHITE);
        mq_circle(q, cx + U(10), by - U(22), U(9), Q_WHITE);
        static const uint16_t mane[4] = {Q_HOTPINK, Q_YELLOW, Q_SKY, Q_LAV};
        for (int i = 0; i < 4; i++)
            mq_circle(q, cx + U(3) - i * U(3), by - U(28) + i * U(4), U(3) + 1, mane[i]);
        int16_t hx[3] = {(int16_t)(cx + U(10)), (int16_t)(cx + U(16)), (int16_t)(cx + U(14))},
                hy[3] = {(int16_t)(by - U(30)), (int16_t)(by - U(30)), (int16_t)(by - U(40))};
        mq_poly(q, hx, hy, 3, RGB(240, 196, 80));
        mq_fill(q, cx + U(12), by - U(24), U(2) + 1, blink ? 1 : U(2) + 1, Q_INK);
        mq_fill(q, cx + U(14), by - U(19), U(3), 1, Q_BLUSH);
        break;
    }
    case 1: /* Luna: a lavender moon bunny */
        mq_oval(q, cx - U(5), by - U(34), U(3), U(9), Q_LAV), mq_oval(q, cx + U(5), by - U(34), U(3), U(9), Q_LAV);
        mq_oval(q, cx - U(5), by - U(33), U(1) + 1, U(6), Q_PINK), mq_oval(q, cx + U(5), by - U(33), U(1) + 1, U(6), Q_PINK);
        mq_circle(q, cx, by - U(13), U(13), Q_LAV);
        mq_circle(q, cx, by - U(20), U(3), Q_YELLOW), mq_circle(q, cx + U(1) + 1, by - U(21), U(3), Q_LAV); /* the moon mark */
        mq_fill(q, cx - U(5), by - U(13), U(2) + 1, blink ? 1 : U(3), Q_INK), mq_fill(q, cx + U(4), by - U(13), U(2) + 1, blink ? 1 : U(3), Q_INK);
        mq_fill(q, cx - U(8), by - U(9), U(3), 1 + U(1), Q_BLUSH), mq_fill(q, cx + U(6), by - U(9), U(3), 1 + U(1), Q_BLUSH);
        break;
    case 2: /* Captain Puff: a fluffy sheep in a captain's hat */
        for (int i = 0; i < 6; i++)
            mq_circle(q, cx + mq_icos(i * 60) * U(9) / 1024, by - U(14) + mq_isin(i * 60) * U(8) / 1024, U(7), Q_WHITE);
        mq_circle(q, cx, by - U(14), U(9), Q_WHITE);
        mq_oval(q, cx, by - U(16), U(7), U(6), RGB(90, 80, 100));
        mq_fill(q, cx - U(3), by - U(17), U(2) + 1, blink ? 1 : U(2) + 1, Q_WHITE), mq_fill(q, cx + U(2), by - U(17), U(2) + 1, blink ? 1 : U(2) + 1, Q_WHITE);
        mq_rrect(q, cx - U(9), by - U(28), U(18), U(6), U(2), RGB(64, 72, 140)); /* the hat */
        mq_fill(q, cx - U(11), by - U(23), U(22), U(2) + 1, Q_INK);
        mq_star(q, cx, by - U(25), U(2) + 1, Q_YELLOW);
        break;
    case 3: { /* Coco: a grey koala with big fluffy ears and a leaf on her head */
        uint16_t gr = RGB(170, 172, 190);
        mq_oval(q, cx, by - U(9), U(11), U(9), gr);
        mq_oval(q, cx, by - U(8), U(6), U(6), RGB(228, 228, 236));
        mq_circle(q, cx - U(11), by - U(31), U(7), gr), mq_circle(q, cx + U(11), by - U(31), U(7), gr);
        mq_circle(q, cx - U(11), by - U(31), U(4), Q_PINK), mq_circle(q, cx + U(11), by - U(31), U(4), Q_PINK);
        mq_circle(q, cx, by - U(24), U(11), gr);
        mq_oval(q, cx, by - U(22), U(3), U(4), DARK); /* the big nose */
        mq_fill(q, cx - U(6), by - U(27), U(2) + 1, blink ? 1 : U(2) + 1, Q_INK), mq_fill(q, cx + U(5), by - U(27), U(2) + 1, blink ? 1 : U(2) + 1, Q_INK);
        mq_fill(q, cx - U(9), by - U(21), U(3), 1 + U(1), Q_BLUSH), mq_fill(q, cx + U(7), by - U(21), U(3), 1 + U(1), Q_BLUSH);
        mq_oval(q, cx + U(4), by - U(36), U(4), U(2), Q_GREEN), mq_fill(q, cx + U(4), by - U(36), U(3), 1, RGB(60, 150, 100));
        break;
    }
    case 4: { /* Biscuit: an orange corgi puppy with a pink scarf, tongue out */
        mq_oval(q, cx, by - U(9), U(13), U(8), Q_ORANGE);
        mq_fill(q, cx - U(9), by - U(4), U(4), U(4), Q_WHITE), mq_fill(q, cx + U(6), by - U(4), U(4), U(4), Q_WHITE);
        int wag = (int)(q->anim_t / 150 % 2) * U(2); /* the tail wags */
        mq_circle(q, cx + U(13), by - U(14) - wag, U(3), Q_ORANGE);
        int16_t lx[3] = {(int16_t)(cx - U(10)), (int16_t)(cx - U(3)), (int16_t)(cx - U(9))},
                ly[3] = {(int16_t)(by - U(26)), (int16_t)(by - U(30)), (int16_t)(by - U(39))};
        int16_t rx[3] = {(int16_t)(cx + U(10)), (int16_t)(cx + U(3)), (int16_t)(cx + U(9))};
        mq_poly(q, lx, ly, 3, Q_ORANGE), mq_poly(q, rx, ly, 3, Q_ORANGE);
        mq_circle(q, cx, by - U(22), U(10), Q_ORANGE);
        mq_oval(q, cx, by - U(18), U(6), U(5), Q_WHITE);
        mq_circle(q, cx, by - U(21), U(1) + 1, Q_INK);
        mq_fill(q, cx - U(5), by - U(26), U(2) + 1, blink ? 1 : U(2) + 1, Q_INK), mq_fill(q, cx + U(4), by - U(26), U(2) + 1, blink ? 1 : U(2) + 1, Q_INK);
        mq_fill(q, cx - U(1), by - U(17), U(2) + 1, U(3), Q_HOTPINK); /* the tongue */
        mq_rrect(q, cx - U(9), by - U(14), U(18), U(3) + 1, U(1), Q_HOTPINK); /* the scarf */
        mq_fill(q, cx + U(4), by - U(12), U(3), U(5), Q_HOTPINK);
        break;
    }
    case 5: { /* Ziggy: a little mint dragon with lavender wings that flap, and golden horns */
        int flap = (int)(q->anim_t / 200 % 2) * U(3);
        int16_t wx[3] = {(int16_t)(cx - U(8)), (int16_t)(cx - U(20)), (int16_t)(cx - U(12))},
                wy[3] = {(int16_t)(by - U(16)), (int16_t)(by - U(26) - flap), (int16_t)(by - U(8))};
        int16_t vx[3] = {(int16_t)(cx + U(8)), (int16_t)(cx + U(20)), (int16_t)(cx + U(12))};
        mq_poly(q, wx, wy, 3, Q_LAV), mq_poly(q, vx, wy, 3, Q_LAV);
        int16_t tx[3] = {(int16_t)(cx + U(6)), (int16_t)(cx + U(18)), (int16_t)(cx + U(8))},
                ty[3] = {(int16_t)(by - U(6)), (int16_t)(by - U(2)), (int16_t)(by - U(1))};
        mq_poly(q, tx, ty, 3, Q_MINT);
        mq_oval(q, cx, by - U(10), U(10), U(10), Q_MINT);
        mq_oval(q, cx, by - U(9), U(6), U(7), RGB(255, 240, 190)); /* the belly */
        int16_t h1[3] = {(int16_t)(cx - U(7)), (int16_t)(cx - U(3)), (int16_t)(cx - U(7))},
                hy[3] = {(int16_t)(by - U(30)), (int16_t)(by - U(32)), (int16_t)(by - U(38))};
        int16_t h2[3] = {(int16_t)(cx + U(7)), (int16_t)(cx + U(3)), (int16_t)(cx + U(7))};
        mq_poly(q, h1, hy, 3, Q_YELLOW), mq_poly(q, h2, hy, 3, Q_YELLOW);
        mq_circle(q, cx, by - U(25), U(9), Q_MINT);
        mq_fill(q, cx - U(5), by - U(28), U(2) + 1, blink ? 1 : U(3), Q_INK), mq_fill(q, cx + U(4), by - U(28), U(2) + 1, blink ? 1 : U(3), Q_INK);
        mq_fill(q, cx - U(8), by - U(23), U(3), 1 + U(1), Q_BLUSH), mq_fill(q, cx + U(6), by - U(23), U(3), 1 + U(1), Q_BLUSH);
        mq_fill(q, cx - U(2), by - U(21), U(4) + 1, 1, Q_INK); /* a smile */
        break;
    }
    case 6: { /* Pearl: a pink jellyfish who floats, her ribbons waving */
        int fl = mq_isin((int)(q->anim_t / 8 % 360)) * U(3) / 1024, top = by - U(30) + fl;
        for (int i = 0; i < 4; i++) { /* the ribbons */
            int rx0 = cx - U(7) + i * U(5);
            for (int k = 0; k < 4; k++) {
                int wv = mq_isin((int)((q->anim_t / 5 + (uint32_t)(i * 60 + k * 50)) % 360)) * U(2) / 1024;
                mq_line(q, rx0 + wv, top + U(10) + k * U(4), rx0 - wv, top + U(14) + k * U(4), i % 2 ? Q_LAV : Q_HOTPINK);
            }
        }
        mq_oval(q, cx, top + U(6), U(12), U(10), Q_PINK);
        mq_fill(q, cx - U(12), top + U(9), U(24) + 1, U(3), Q_PINK);
        mq_circle(q, cx - U(5), top + U(1), U(2), Q_WHITE), mq_circle(q, cx + U(6), top + U(3), U(1) + 1, Q_WHITE);
        mq_fill(q, cx - U(4), top + U(6), U(2) + 1, blink ? 1 : U(2) + 1, Q_INK), mq_fill(q, cx + U(3), top + U(6), U(2) + 1, blink ? 1 : U(2) + 1, Q_INK);
        mq_fill(q, cx - U(8), top + U(9), U(3), 1 + U(1), Q_BLUSH), mq_fill(q, cx + U(6), top + U(9), U(3), 1 + U(1), Q_BLUSH);
        break;
    }
    default: { /* Bao: a panda with a basket of buns */
        mq_oval(q, cx, by - U(10), U(11), U(10), Q_WHITE);
        mq_oval(q, cx - U(9), by - U(11), U(4), U(6), DARK), mq_oval(q, cx + U(9), by - U(11), U(4), U(6), DARK);
        mq_circle(q, cx - U(9), by - U(33), U(4), DARK), mq_circle(q, cx + U(9), by - U(33), U(4), DARK);
        mq_circle(q, cx, by - U(25), U(10), Q_WHITE);
        mq_oval(q, cx - U(4), by - U(26), U(3), U(4), DARK), mq_oval(q, cx + U(4), by - U(26), U(3), U(4), DARK);
        mq_fill(q, cx - U(4), by - U(27), U(1) + 1, blink ? 1 : U(1) + 1, Q_WHITE), mq_fill(q, cx + U(4), by - U(27), U(1) + 1, blink ? 1 : U(1) + 1, Q_WHITE);
        mq_fill(q, cx - U(1), by - U(21), U(2) + 1, U(1) + 1, DARK);
        mq_fill(q, cx - U(8), by - U(21), U(3), 1 + U(1), Q_BLUSH), mq_fill(q, cx + U(6), by - U(21), U(3), 1 + U(1), Q_BLUSH);
        mq_rrect(q, cx - U(8), by - U(9), U(16), U(7), U(2), Q_BROWN); /* the basket */
        mq_circle(q, cx - U(4), by - U(10), U(3), RGB(255, 246, 236)), mq_circle(q, cx + U(3), by - U(10), U(3), RGB(255, 246, 236));
        break;
    }
    }
#undef U
}

#define VIS_X 276   /* where a visitor stands in town (street x, on the near pavement) */
#define SNOW_X 196  /* the snowman */
#define PIC_X 381   /* the picnic */
#define BDAY_X 601  /* the birthday party */
#define T_GROUND (MQ_Y0 + 122)

static const uint16_t fest_col[6] = {Q_HOTPINK, Q_ORANGE, Q_YELLOW, Q_MINT, Q_SKY, Q_LAV};

/* a rainbow: arcs of `w` px around cx, cy from radius r (outside) inwards */
static void rainbow(mq_t *q, int cx, int cy, int r, int w)
{
    for (int b = 0; b < 6; b++) {
        int16_t px[26], py[26];
        int ro = r - b * w, ri = ro - w;
        for (int i = 0; i < 13; i++) {
            px[i] = (int16_t)(cx + mq_icos(180 - i * 15) * ro / 1024), py[i] = (int16_t)(cy - mq_isin(180 - i * 15) * ro / 1024);
            px[25 - i] = (int16_t)(cx + mq_icos(180 - i * 15) * ri / 1024), py[25 - i] = (int16_t)(cy - mq_isin(180 - i * 15) * ri / 1024);
        }
        mq_poly(q, px, py, 26, fest_col[b]);
    }
}
/* a kite: a diamond with a ribbon tail that waves */
static void kite(mq_t *q, int x, int y, int k, uint32_t t)
{
    uint16_t c = fest_col[k % 6], c2 = fest_col[(k + 3) % 6];
    int16_t px[4] = {(int16_t)x, (int16_t)(x + 8), (int16_t)x, (int16_t)(x - 8)}, py[4] = {(int16_t)(y - 11), (int16_t)y, (int16_t)(y + 13), (int16_t)y};
    mq_poly(q, px, py, 4, c);
    int16_t hx[3] = {(int16_t)x, (int16_t)(x + 8), (int16_t)x}, hy[3] = {(int16_t)(y - 11), (int16_t)y, (int16_t)y};
    mq_poly(q, hx, hy, 3, c2);
    for (int i = 0; i < 4; i++) {
        int wv = mq_isin((int)((t / 4 + (uint32_t)(i * 70 + k * 40)) % 360)) * 3 / 1024;
        mq_line(q, x + wv, y + 13 + i * 5, x - wv, y + 17 + i * 5, Q_INK);
        mq_fill(q, x - wv - 1, y + 16 + i * 5, 3, 2, i % 2 ? c : c2);
    }
}
/* a shooting star: a head and a fading tail, crossing every few seconds */
static void shooting(mq_t *q, int k, uint32_t t, int y0)
{
    int el = (int)((t + (uint32_t)k * 1300) % 3900);
    if (el > 900)
        return;
    int sx = 40 + (k * 97 + (int)(t / 3900) * 53) % 240, sy = y0 + 4 + (k * 13) % 30, x = sx + el / 6, y = sy + el / 14;
    for (int i = 1; i < 5; i++)
        mq_circle(q, x - i * 6, y - i * 2, 2 - i / 3, i < 3 ? RGB(255, 246, 200) : Q_LAV);
    mq_star(q, x, y, 4, Q_YELLOW);
}

/* the specials in town by layer: 0 the sky (a rainbow, kites, shooting stars), 1 by the buildings
 * (lanterns, the parade, the snowman, the picnic, the party), 2 in front (the visitor, bubbles, snow) */
void mq_events_town(mq_t *q, int cam, int layer)
{
    int ev = mq_event_today(q);
    uint32_t t = q->anim_t;
    if (layer == 0) {
        if (ev == EV_RAINBOW)
            rainbow(q, 220 - cam / 5, T_GROUND + 6, 108, 6); /* (its top just under the title line) */
        if (ev == EV_STARS)
            for (int k = 0; k < 3; k++)
                shooting(q, k, t, MQ_Y0 + 20);
        return;
    }
    if (layer == 1 && ev == EV_KITES) /* kites over the roofs, their strings down to the street */
        for (int k = 0; k < 4; k++) {
            int x = ((k * 100 + 30 - cam / 2) % 400 + 400) % 400 - 40 + mq_isin((int)((t / 9 + (uint32_t)k * 90) % 360)) * 8 / 1024;
            int y = MQ_Y0 + 26 + (k % 2) * 14 + mq_isin((int)((t / 6 + (uint32_t)k * 70) % 360)) * 5 / 1024;
            mq_line(q, x, y + 13, x + 24 + k * 6, T_GROUND + 12, Q_DIM);
            kite(q, x, y, k, t);
        }
    if (layer == 1 && ev == EV_LANTERN) { /* lanterns strung along the street, fireworks above */
        static const uint16_t lc[4] = {Q_HOTPINK, Q_YELLOW, Q_ORANGE, Q_LAV};
        mq_fill(q, 0, T_GROUND - 92, MQ_W, 1, Q_DIM);
        for (int i = 0; i < 12; i++) {
            int x = ((i * 34 - cam) % 408 + 408) % 408 - 30;
            mq_oval(q, x, T_GROUND - 84 + (i % 2) * 3, 5, 7, lc[i % 4]);
            mq_fill(q, x - 1, T_GROUND - 92, 2, 2, Q_DIM);
        }
        for (int k = 0; k < 2; k++) {
            int el = (int)((t + (uint32_t)k * 900) % 1800), fx = 80 + k * 160 + (int)((t / 1800) % 3) * 20, fy = MQ_Y0 + 36;
            if (el < 900)
                for (int i = 0; i < 10; i++)
                    mq_circle(q, fx + mq_icos(i * 36) * el / 30 / 1024, fy + mq_isin(i * 36) * el / 30 / 1024 + el * el / 120000, 1 + (el < 500),
                              lc[(i + k) % 4]);
        }
    }
    if (layer == 2 && ev == EV_BUBBLES) /* soap bubbles drifting over the town */
        for (int i = 0; i < 7; i++) {
            int x = (int)((uint32_t)(i * 53) + t / 30) % 340 - 10, y = MQ_Y0 + 180 - (int)((t / 25 + (uint32_t)i * 61) % 170);
            mq_ring(q, x, y, 5 + i % 3, i % 2 ? Q_SKY : RGB(240, 196, 80));
            mq_fill(q, x - 2, y - 2, 2, 2, Q_WHITE);
        }
    if (layer == 2 && ev == EV_SNOW) /* snowflakes drifting down */
        for (int i = 0; i < 16; i++) {
            int x = ((i * 61 - cam / 3 + mq_isin((int)((t / 12 + (uint32_t)i * 40) % 360)) * 6 / 1024) % 340 + 340) % 340 - 10;
            int y = MQ_Y0 + 18 + (int)((t / 28 + (uint32_t)i * 37) % 186);
            mq_circle(q, x, y, 1 + i % 2, Q_WHITE);
        }
    if (layer == 1 && ev == EV_SNOW) { /* a snowman by the street, with a carrot nose and a scarf */
        int x = SNOW_X - cam;
        if (x > -30 && x < MQ_W + 30) {
            mq_circle(q, x, T_GROUND + 2, 11, Q_WHITE), mq_circle(q, x, T_GROUND - 15, 8, Q_WHITE);
            mq_fill(q, x - 7, T_GROUND - 29, 14, 3, DARK), mq_fill(q, x - 4, T_GROUND - 36, 8, 8, DARK);
            mq_fill(q, x - 3, T_GROUND - 17, 2, 2, Q_INK), mq_fill(q, x + 2, T_GROUND - 17, 2, 2, Q_INK);
            int16_t nx[3] = {(int16_t)x, (int16_t)(x + 7), (int16_t)x}, ny[3] = {(int16_t)(T_GROUND - 14), (int16_t)(T_GROUND - 12), (int16_t)(T_GROUND - 11)};
            mq_poly(q, nx, ny, 3, Q_ORANGE);
            mq_fill(q, x - 7, T_GROUND - 9, 14, 3, Q_HOTPINK), mq_fill(q, x + 3, T_GROUND - 8, 3, 7, Q_HOTPINK);
            mq_fill(q, x - 1, T_GROUND - 2, 2, 2, DARK), mq_fill(q, x - 1, T_GROUND + 3, 2, 2, DARK);
        }
    }
    if (layer == 1 && ev == EV_PICNIC) { /* a checked blanket on the grass, a basket, two friends */
        int x = PIC_X - cam;
        if (x > -60 && x < MQ_W + 60) {
            for (int i = 0; i < 6; i++)
                for (int j = 0; j < 2; j++)
                    mq_fill(q, x - 30 + i * 10, T_GROUND + 2 + j * 5, 10, 5, (i + j) % 2 ? Q_WHITE : Q_PINK);
            mq_rrect(q, x - 6, T_GROUND - 6, 14, 10, 3, Q_BROWN), mq_ring(q, x + 1, T_GROUND - 6, 5, Q_BROWN);
            mq_circle(q, x + 16, T_GROUND, 3, RGB(255, 246, 236)), mq_circle(q, x + 22, T_GROUND + 1, 3, Q_PINK);
            mq_profile_t *p = mq_prof(q);
            int n = 0;
            for (int k = 0; k < 2 * MQ_NCAST && n < 2; k++) { /* friends she has met (or Pip and Momo) */
                int s = (k + p->days) % MQ_NCAST;
                if (k < MQ_NCAST ? ((p->met >> s) & 1) && s != p->buddy % MQ_NCAST : (s == 0 || s == 2)) {
                    mq_cast(q, s, x - 50 + n * 66, T_GROUND - 24, 32, (t / 1500 + (uint32_t)n) % 2 ? AN_HAPPY : AN_TALK);
                    n++;
                }
            }
        }
    }
    if (layer == 1 && ev == EV_BIRTHDAY) { /* the party: a banner, balloons, a cake with candles, the friend dancing */
        int x = BDAY_X - cam, who = mq_events_birthday(q);
        if (x > -70 && x < MQ_W + 70) {
            mq_line(q, x - 40, T_GROUND - 58, x + 40, T_GROUND - 58, Q_DIM);
            for (int i = 0; i < 9; i++) { /* the bunting */
                int16_t fx[3] = {(int16_t)(x - 40 + i * 10), (int16_t)(x - 32 + i * 10), (int16_t)(x - 36 + i * 10)},
                        fy[3] = {(int16_t)(T_GROUND - 58), (int16_t)(T_GROUND - 58), (int16_t)(T_GROUND - 50)};
                mq_poly(q, fx, fy, 3, fest_col[i % 6]);
            }
            for (int i = 0; i < 3; i++) { /* balloons */
                int bx = x - 36 + i * 7, byy = T_GROUND - 44 - (i % 2) * 6 + mq_isin((int)((t / 6 + (uint32_t)i * 100) % 360)) * 2 / 1024;
                mq_line(q, bx, byy + 6, x - 30, T_GROUND - 6, Q_DIM);
                mq_oval(q, bx, byy, 5, 6, fest_col[(i * 2) % 6]);
            }
            mq_fill(q, x - 14, T_GROUND - 4, 28, 3, Q_WHITE), mq_fill(q, x - 1, T_GROUND - 1, 2, 12, Q_DIM); /* the table */
            mq_rrect(q, x - 10, T_GROUND - 16, 20, 12, 3, Q_PINK), mq_fill(q, x - 10, T_GROUND - 12, 20, 2, Q_WHITE);
            for (int i = 0; i < 3; i++) {
                mq_fill(q, x - 5 + i * 5, T_GROUND - 22, 2, 6, Q_SKY);
                if ((t / 150 + (uint32_t)i) % 3)
                    mq_circle(q, x - 4 + i * 5, T_GROUND - 24, 1, Q_ORANGE);
            }
            if (who >= 0) { /* the birthday friend dances by the cake under sparkles */
                int hop = (int)(t / 180 % 4 == 1 ? 4 : 0);
                mq_cast(q, who, x + 14, T_GROUND - 34 - hop, 40, AN_DANCE);
                for (int i = 0; i < 3; i++)
                    if ((t / 200 + (uint32_t)i) % 3)
                        mq_star(q, x + 22 + i * 12, T_GROUND - 40 - (i % 2) * 6, 3, fest_col[(i * 2 + 1) % 6]);
            }
        }
    }
    if (layer == 1 && ev == EV_PARADE) { /* friends in a line with balloons */
        mq_profile_t *p = mq_prof(q);
        int n = 0;
        for (int k = 0; k < MQ_NCAST; k++)
            if (((p->met >> k) & 1) || k == 0 || k == 7) { /* (Pip and Shelly always march) */
                int x = (int)((t / 25 + (uint32_t)n * 36) % 1520) - 50 - cam;
                if (x > -30 && x < MQ_W + 30) {
                    /* (down the street, under the places' signs: their names stay readable) */
                    mq_cast(q, k, x - 16, T_GROUND + 12, 32, AN_DANCE);
                    mq_line(q, x + 8, T_GROUND + 14, x + 10, T_GROUND - 6, Q_DIM);
                    mq_oval(q, x + 10, T_GROUND - 11, 4, 5, n % 2 ? Q_SKY : Q_HOTPINK);
                }
                n++;
            }
    }
    int wish = 0, v = mq_visitor(q, &wish);
    if (layer == 2 && v >= 0) { /* the visitor and their wish (on the near side of the street) */
        int x = VIS_X - cam;
        if (x > -40 && x < MQ_W + 40) {
            mq_visitor_draw(q, v, x - 20, T_GROUND - 6, 40, false);
            int by = T_GROUND - 46 - (int)(t / 300 % 2);
            mq_rrect(q, x - 14, by, 28, 24, 9, Q_YELLOW);
            mq_merge_item(q, wish, x, by + 12, 7);
            mq_poly(q, (int16_t[3]){(int16_t)(x - 3), (int16_t)(x + 3), (int16_t)x}, (int16_t[3]){(int16_t)(by + 23), (int16_t)(by + 23), (int16_t)(by + 28)}, 3,
                    Q_YELLOW);
        }
    }
}

/* today's special over a game's field (cheap: a few shapes): snow, a rainbow, kites, shooting stars */
void mq_events_field(mq_t *q)
{
    int ev = mq_event_today(q);
    uint32_t t = q->anim_t;
    int fy = MQ_Y0 + 58, fb = MQ_Y0 + MQ_H;
    if (ev == EV_RAINBOW)
        rainbow(q, 250, fy + 70, 66, 4);
    else if (ev == EV_KITES)
        for (int k = 0; k < 2; k++)
            kite(q, 70 + k * 190 + mq_isin((int)((t / 9 + (uint32_t)k * 120) % 360)) * 10 / 1024, fy + 18 + k * 6, k + 1, t);
    else if (ev == EV_STARS)
        for (int k = 0; k < 2; k++)
            shooting(q, k, t, fy);
    else if (ev == EV_SNOW)
        for (int i = 0; i < 10; i++) {
            int x = (i * 37 + mq_isin((int)((t / 12 + (uint32_t)i * 40) % 360)) * 5 / 1024 + 340) % 340 - 10;
            int y = fy + (int)((t / 30 + (uint32_t)i * 41) % (uint32_t)(fb - fy));
            mq_circle(q, x, y, 1 + i % 2, Q_WHITE);
        }
}

/* a tap on the visitor (street x wx): they say their wish and point to the kitchen */
bool mq_events_tap(mq_t *q, int wx, int y)
{
    int wish = 0, v = mq_visitor(q, &wish);
    if (v < 0 || iabs(wx - VIS_X) > 22 || y < T_GROUND - 52 || y > T_GROUND + 36)
        return false;
    q->api->snprintf(q->toast_in, sizeof q->toast_in, "%s would love a %s!", vis_name[v], mq_merge_name(wish));
    mq_toast(q, q->toast_in);
    mq_sfx(q, MODX_SFX_VOICE, (9 + v) * 16 + 5);
    return true;
}

int mq_events_bubbles(mq_t *q) { return mq_event_today(q) == EV_BUBBLES; }
int mq_events_hearts(mq_t *q, int slot)
{
    int e = mq_event_today(q);
    return e == EV_PARADE || (e == EV_BIRTHDAY && slot == mq_events_birthday(q)) ? 2 : 1;
}

/* `mq event` (tests): today's special */
const char *mq_event_key(mq_t *q)
{
    static const char k[EV_COUNT][10] = {"none", "visitor", "lantern", "bubbles", "parade", "snow",
                                         "rainbow", "picnic", "kites", "birthday", "stars"};
    int e = mq_event_today(q);
    return k[e >= 0 && e < EV_COUNT ? e : 0];
}
