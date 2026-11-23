/*
 * Math Quest: the town -- Sparkle Town itself, a little street she walks along.
 *
 * The map is a scene, not a menu: a street about four screens long with her house, the notice
 * board (the daily quests), a building for every place (the Garden's greenhouse, the Post
 * Office, the Bakery with its cafe tables and the tip jar, the Clock Tower, the Shop, the Art
 * Studio, the Fair's big tent, the beanstalk up to the Sky Castle, the Star Harbor pier) and the
 * funfair corner (the arcade). Her buddy walks along it with the pet that follows her
 * everywhere; the friends who moved into town stroll about behind. Progress is the town
 * changing: a place that is not open yet is wrapped in scaffolding; an open one gets flower
 * boxes and bunting as its stars fill up, and a golden star, balloons and twinkling lights
 * when its finale is won; houses, trees and fountains fill the gaps as the unlock ladder
 * climbs. Clouds and hills drift by slower than the street (parallax).
 *
 * Input: tap a building (she walks there and goes in), drag or swipe to look along the
 * street, LEFT / RIGHT walk to the next place, ENTER or UP goes in. Everything is in logical
 * pixels of the street (x) and the screen (y); the camera is q->town_cam.
 */
#include "mq.h"

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int iabs(int a) { return a < 0 ? -a : a; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

#define TOWN_W 1420
#define GROUND (MQ_Y0 + 122)   /* the buildings stand here */
#define WALK (MQ_Y0 + 180)     /* her feet on the pavement */
#define GOIN_DX 14              /* the "go in!" bubble: right of her head */
#define GOIN_DY 38
#define GOIN_W 44

/* the places along the street: what, where (the middle of the building) */
enum { PL_HOME = 0, PL_BOARD = 1, PL_WORLD = 2, PL_ARCADE = PL_WORLD + MQ_NWORLD, PL_KITCHEN, PL_COUNT };
/* (the Sparkle Kitchen came later: it stands first on the street, its index last) */
static const int16_t place_x[PL_COUNT] = {146, 236, 326, 436, 546, 656, 766, 876, 986, 1096, 1206, 1332, 50};

int mq_town_place_x(int pl) { return place_x[clampi(pl, 0, PL_COUNT - 1)]; }

/* the pet that follows her: the first pet she owns (a puppy if she has none yet) */
int mq_pet_id(mq_t *q)
{
    mq_home_t *h = mq_home(q);
    for (int id = 1; id <= MQ_NITEM; id++)
        if (mq_item[id].cat == HC_PET && h->own[id])
            return id;
    return 0;
}
/* a companion from the start: the Garden's cheapest pet, a bunny or a snail (once) */
void mq_pet_give(mq_t *q)
{
    if (mq_pet_id(q))
        return;
    int best = 0;
    for (int id = 1; id <= MQ_NITEM; id++)
        if (mq_item[id].cat == HC_PET && mq_item[id].set == HS_GARDEN && (!best || mq_item[id].price < mq_item[best].price))
            best = id;
    if (best) {
        mq_home(q)->own[best]++;
        q->dirty = true;
    }
}
/* the pet at x, its feet on y; it hops when happy */
void mq_pet_draw(mq_t *q, int x, int y, int happy, int face)
{
    int id = mq_pet_id(q);
    if (!id)
        return;
    int hop = happy ? (int)(q->anim_t / 90 % 4 == 1 ? 4 : q->anim_t / 90 % 4 == 2 ? 6 : 0) : (int)(q->anim_t / 300 % 2);
    mq_item_draw(q, id, x - 8, y - 16 - hop, face ? 4 : 0, (int)(q->anim_t / 400 % 2), 1, -1);
}

/* ------------------------------------------------------------------ life in town */
/* the town is never still, even before any friend has moved in: birds cross the sky by day (in
 * the back), butterflies flutter over the pavement (in front); at night fireflies blink instead */
static void town_life(mq_t *q, int cam, bool front)
{
    uint32_t t = q->anim_t;
    bool night = mq_town_time(q) == TINT_NIGHT;
    if (!front) {
        if (night)
            return;
        for (int k = 0; k < 3; k++) { /* birds: a little "v" that flaps, slower than the street (parallax) */
            int x = MQ_W + 30 - (int)((t / (22 + (uint32_t)k * 6) + (uint32_t)k * 170 + (uint32_t)(cam / 3)) % 420);
            int y = MQ_Y0 + 32 + k * 11 + mq_isin((int)((t / 10 + (uint32_t)k * 80) % 360)) * 3 / 1024, up = (t / 160 + (uint32_t)k) % 2 ? 3 : 0;
            mq_line(q, x - 6, y - up, x, y, Q_INK), mq_line(q, x, y, x + 6, y - up, Q_INK);
        }
        return;
    }
    static const uint16_t wing[3] = {Q_HOTPINK, Q_YELLOW, Q_LAV};
    for (int k = 0; k < 3; k++) {
        int span = 520, wx = (int)((t / (34 + (uint32_t)k * 9) + (uint32_t)k * 190) % (uint32_t)(2 * span));
        wx = (wx < span ? wx : 2 * span - wx) + 60 + k * 300; /* street x: back and forth along a stretch */
        int x = wx - cam, y = WALK - 34 - k * 9 + mq_isin((int)((t / 7 + (uint32_t)k * 100) % 360)) * 7 / 1024;
        if (x < -10 || x > MQ_W + 10)
            continue;
        if (night) { /* a firefly */
            if ((t / 250 + (uint32_t)k) % 3)
                mq_circle(q, x, y, 2, RGB(255, 250, 150));
            continue;
        }
        int flap = (int)(t / 90 + (uint32_t)k) % 2; /* a butterfly: two wings that open and close */
        mq_oval(q, x - 3, y, flap ? 3 : 1, 3, wing[k]), mq_oval(q, x + 3, y, flap ? 3 : 1, 3, wing[k]);
        mq_fill(q, x, y - 2, 1, 5, Q_INK);
    }
}

/* ------------------------------------------------------------------ the buildings */
/* a place not open yet: its colours washed out (a quarter of the colour, the rest pale grey) */
static uint16_t grey(uint16_t c, bool on)
{
    if (on)
        return c;
    uint16_t g = RGB(226, 226, 232), m = (uint16_t)(((c & 0xF7DE) >> 1) + ((g & 0xF7DE) >> 1));
    return (uint16_t)(((m & 0xF7DE) >> 1) + ((g & 0xF7DE) >> 1));
}

/* the time of day in town, by the minutes she has played (no clock: play time): a long day, then a
 * dusk, a night with the windows lit and stars, a dusk again (lantern night: night all along) */
int mq_town_time(mq_t *q)
{
    if (mq_events_night(q))
        return TINT_NIGHT;
    uint32_t m = mq_prof(q)->secs / 60 % 20;
    return m < 12 ? TINT_NONE : m < 14 ? TINT_DUSK : m < 19 ? TINT_NIGHT : TINT_DUSK;
}

/* a window: a frame, panes, little curtains, a sill; lit warm at dusk and at night (the light is
 * not tinted: it glows) */
static void window_(mq_t *q, int x, int y, bool on, bool box)
{
    int tint = q->tint;
    bool lit = on && tint;
    mq_fill(q, x - 1, y - 1, 12, 12, on ? Q_WHITE : RGB(236, 236, 240));
    if (lit) {
        q->tint = 0;
        mq_fill(q, x - 3, y - 3, 16, 16, RGB(255, 236, 170)); /* the glow */
        mq_fill(q, x - 1, y - 1, 12, 12, Q_WHITE);
    }
    mq_fill(q, x, y, 10, 10, lit ? RGB(255, 222, 120) : on ? RGB(206, 236, 255) : RGB(226, 226, 234));
    mq_fill(q, x + 4, y, 2, 10, on ? Q_WHITE : RGB(240, 240, 244));
    mq_fill(q, x, y + 4, 10, 1, on ? Q_WHITE : RGB(240, 240, 244));
    if (on) { /* curtains */
        int16_t lx[3] = {(int16_t)x, (int16_t)(x + 3), (int16_t)x}, ly[3] = {(int16_t)y, (int16_t)y, (int16_t)(y + 6)};
        int16_t rx[3] = {(int16_t)(x + 10), (int16_t)(x + 7), (int16_t)(x + 10)};
        mq_poly(q, lx, ly, 3, Q_PINK), mq_poly(q, rx, ly, 3, Q_PINK);
    }
    q->tint = (uint8_t)tint;
    mq_fill(q, x - 2, y + 10, 14, 2, on ? RGB(214, 190, 170) : RGB(220, 220, 228)); /* the sill */
    if (box) { /* a flower box */
        mq_fill(q, x - 1, y + 11, 12, 3, Q_BROWN);
        for (int k = 0; k < 3; k++)
            mq_circle(q, x + 1 + k * 4, y + 10, 1, k % 2 ? Q_PINK : Q_YELLOW);
    }
}

/* a door with a rounded top, a knob and a step */
static void door(mq_t *q, int cx, int b, int w, int h, uint16_t c, bool on)
{
    mq_fill(q, cx - w / 2 - 2, b - 2, w + 4, 2, on ? RGB(200, 180, 170) : RGB(220, 220, 228));
    mq_rrect(q, cx - w / 2, b - h - 2, w, h, w / 2, c);
    mq_fill(q, cx - w / 2, b - h / 2 - 2, w, h / 2, c);
    mq_circle(q, cx + w / 4, b - h / 2 - 2, 1, on ? Q_YELLOW : Q_GREY);
}

/* shingles on a roof triangle */
static void shingles(mq_t *q, int x0, int x1, int y, int h, uint16_t c)
{
    for (int r = 1; r < 4; r++) {
        int yy = y - h + h * r / 4, half = (x1 - x0) * r / 8;
        mq_fill(q, (x0 + x1) / 2 - half, yy, 2 * half, 1, c);
    }
}

/* a chimney with smoke curling up */
static void chimney(mq_t *q, int x, int y, uint32_t t)
{
    mq_fill(q, x, y, 8, 14, RGB(200, 130, 120));
    mq_fill(q, x - 1, y, 10, 3, RGB(180, 110, 100));
    for (int i = 0; i < 3; i++) {
        int ph = (int)((t / 40 + (uint32_t)i * 33) % 100);
        mq_circle(q, x + 4 + (ph / 12) % 3 - 1 + ph / 25, y - 3 - ph / 4, 2 + ph / 40, RGB(240, 240, 248));
    }
}

static void roof_tri(mq_t *q, int x0, int x1, int y, int h, uint16_t c)
{
    int16_t px[3] = {(int16_t)x0, (int16_t)x1, (int16_t)((x0 + x1) / 2)}, py[3] = {(int16_t)y, (int16_t)y, (int16_t)(y - h)};
    mq_poly(q, px, py, 3, c);
}

static void sign(mq_t *q, int cx, int y, const char *s, bool on)
{
    int w = mq_text_w(q, q->api->font_small, s) + 8;
    w = w > 88 ? 88 : w; /* (a long name: the house's width) */
    if (cx < 0 || cx > MQ_W) /* its place is off the screen: no half a word at the edge */
        return;
    cx = cx < w / 2 + 1 ? w / 2 + 1 : cx > MQ_W - w / 2 - 1 ? MQ_W - w / 2 - 1 : cx; /* (half off: held at the edge) */
    mq_rrect(q, cx - w / 2, y, w, 13, 4, on ? Q_WHITE : RGB(236, 236, 240));
    mq_text_fit(q, q->api->font_small, cx, y - 1, w - 6, s, on ? Q_INK : Q_DIM, true);
}

static void bunting(mq_t *q, int x0, int x1, int y)
{
    static const uint16_t c[4] = {Q_HOTPINK, Q_YELLOW, Q_SKY, Q_MINT};
    mq_line(q, x0, y, x1, y, Q_DIM);
    for (int x = x0 + 3, k = 0; x < x1 - 3; x += 8, k++) {
        int16_t px[3] = {(int16_t)(x - 3), (int16_t)(x + 3), (int16_t)x}, py[3] = {(int16_t)y, (int16_t)y, (int16_t)(y + 6)};
        mq_poly(q, px, py, 3, c[k % 4]);
    }
}

/* place w's building at sx (its middle), standing on GROUND; stage 0 closed, 1 open, 2 half its
 * stars, 3 its finale won */
static void building(mq_t *q, int w, int sx, int stage)
{
    bool on = stage > 0;
    int b = GROUND;
    uint32_t t = q->anim_t;
    /* (a high-resolution platform may draw the building from its own art: MODX_HR_SPRITE) */
    static const char bid[MQ_NWORLD][12] = {"town/garden", "town/post", "town/bakery", "town/clock", "town/shop",
                                            "town/art", "town/fair", "town/castle", "town/star"};
    int32_t sv[1] = {stage};
    int hr = mq_hr_sprite(q, bid[w % MQ_NWORLD], sx - 50, b - 120, 100, 120, sv, 1);
    switch (w) {
    case G_GARDEN: /* a greenhouse */
        mq_rrect(q, sx - 40, b - 50, 80, 50, 4, grey(RGB(200, 240, 214), on));
        mq_oval(q, sx, b - 50, 40, 16, grey(RGB(214, 246, 226), on));
        for (int k = -30; k <= 30; k += 15)
            mq_fill(q, sx + k, b - 62, 1, 62, grey(RGB(150, 210, 170), on));
        door(q, sx, b, 16, 22, grey(Q_GREEN, on), on);
        if (on) /* plants inside the glass */
            for (int k = 0; k < 4; k++)
                mq_circle(q, sx - 26 + k * 17 + (k > 1 ? 6 : 0), b - 30 + (k % 2) * 6, 5, RGB(130, 200, 150));
        for (int k = 0; k < 6; k++)
            mq_circle(q, sx - 36 + k * 14 + (k > 2 ? 4 : 0), b - 4, 3, k % 2 ? Q_PINK : Q_YELLOW);
        break;
    case G_POST: /* the post office */
        mq_fill(q, sx - 40, b - 64, 80, 64, grey(RGB(176, 206, 244), on));
        roof_tri(q, sx - 46, sx + 46, b - 64, 18, grey(Q_BLUE, on));
        window_(q, sx - 30, b - 54, on, stage >= 2), window_(q, sx + 20, b - 54, on, stage >= 2);
        shingles(q, sx - 46, sx + 46, b - 64, 18, grey(RGB(70, 140, 210), on));
        door(q, sx, b, 16, 26, grey(Q_WHITE, on), on);
        mq_fill(q, sx - 3, b - 20, 6, 4, grey(Q_SKY, on)); /* a letter slot */
        mq_rrect(q, sx + 22, b - 22, 12, 22, 4, grey(RGB(236, 80, 90), on)); /* the mailbox */
        mq_fill(q, sx + 24, b - 16, 8, 2, Q_INK);
        break;
    case G_BAKERY: /* the bakery and its cafe */
        mq_fill(q, sx - 40, b - 58, 80, 58, grey(RGB(255, 214, 228), on));
        for (int k = 0; k < 8; k++) /* the awning */
            mq_fill(q, sx - 44 + k * 11, b - 62, 11, 10, k % 2 ? grey(Q_WHITE, on) : grey(Q_HOTPINK, on));
        window_(q, sx - 30, b - 40, on, stage >= 2), window_(q, sx + 20, b - 40, on, stage >= 2);
        door(q, sx, b, 14, 26, grey(Q_BROWN, on), on);
        mq_circle(q, sx, b - 70, 7, grey(Q_PEACH, on)), mq_circle(q, sx, b - 74, 4, grey(Q_PINK, on)); /* a cupcake sign */
        if (on)
            chimney(q, sx + 26, b - 76, t);
        break;
    case G_CLOCK: { /* the clock tower */
        mq_fill(q, sx - 22, b - 84, 44, 84, grey(RGB(214, 200, 250), on));
        roof_tri(q, sx - 28, sx + 28, b - 84, 18, grey(Q_PURPLE, on));
        mq_circle(q, sx, b - 64, 14, grey(Q_WHITE, on));
        int a = (int)(t / 25 % 360);
        mq_line(q, sx, b - 64, sx + mq_isin(a) * 11 / 1024, b - 64 - mq_icos(a) * 11 / 1024, Q_INK);
        mq_line(q, sx, b - 64, sx + mq_isin(a / 12) * 7 / 1024, b - 64 - mq_icos(a / 12) * 7 / 1024, Q_INK);
        window_(q, sx - 5, b - 42, on, stage >= 2);
        shingles(q, sx - 28, sx + 28, b - 84, 18, grey(RGB(130, 100, 210), on));
        door(q, sx, b, 14, 24, grey(Q_BROWN, on), on);
        break;
    }
    case G_SHOP: /* the shop */
        mq_fill(q, sx - 38, b - 54, 76, 54, grey(RGB(255, 222, 190), on));
        for (int k = 0; k < 7; k++)
            mq_circle(q, sx - 36 + k * 12, b - 54, 6, k % 2 ? grey(Q_WHITE, on) : grey(Q_ORANGE, on));
        mq_fill(q, sx - 30, b - 40, 26, 18, grey(RGB(255, 246, 200), on)); /* the shop window */
        mq_fill(q, sx - 31, b - 23, 28, 2, grey(RGB(214, 170, 130), on));
        mq_circle(q, sx - 22, b - 30, 4, grey(RGB(240, 196, 80), on));
        mq_rrect(q, sx - 14, b - 36, 7, 9, 2, grey(Q_PINK, on)); /* things for sale */
        door(q, sx + 16, b, 16, 28, grey(Q_BROWN, on), on);
        if (on) { /* an OPEN sign on the door */
            mq_rrect(q, sx + 10, b - 22, 12, 6, 2, Q_WHITE);
            mq_fill(q, sx + 12, b - 20, 8, 2, Q_HOTPINK);
        }
        break;
    case G_ART: /* the art studio */
        mq_fill(q, sx - 38, b - 60, 76, 60, grey(RGB(255, 246, 196), on));
        roof_tri(q, sx - 44, sx + 44, b - 60, 14, grey(Q_ORANGE, on));
        mq_oval(q, sx, b - 74, 12, 7, grey(Q_WHITE, on)); /* a palette */
        for (int k = 0; k < 4; k++)
            mq_circle(q, sx - 7 + k * 5, b - 75 + (k % 2) * 3, 2, on ? (k == 0 ? Q_PINK : k == 1 ? Q_SKY : k == 2 ? Q_MINT : Q_HOTPINK) : Q_GREY);
        window_(q, sx - 28, b - 46, on, stage >= 2), window_(q, sx + 18, b - 46, on, stage >= 2);
        door(q, sx, b, 14, 24, grey(Q_SKY, on), on);
        if (on) /* paint drips by the door */
            mq_fill(q, sx - 14, b - 30, 2, 6, Q_PINK), mq_fill(q, sx + 12, b - 28, 2, 5, Q_MINT);
        break;
    case G_FAIR: /* the fair's big tent */
        for (int k = 0; k < 6; k++) {
            int16_t px[3] = {(int16_t)(sx - 42 + k * 14), (int16_t)(sx - 28 + k * 14), (int16_t)sx},
                    py[3] = {(int16_t)b, (int16_t)b, (int16_t)(b - 70)};
            mq_poly(q, px, py, 3, k % 2 ? grey(Q_WHITE, on) : grey(RGB(120, 214, 190), on));
        }
        mq_fill(q, sx - 1, b - 84, 2, 14, Q_DIM);
        mq_fill(q, sx + 1, b - 84, 9, 6, on ? Q_HOTPINK : Q_GREY);
        mq_fill(q, sx - 8, b - 18, 16, 18, grey(Q_INK, on));
        break;
    case G_SKY: { /* the beanstalk up to the castle on its cloud */
        for (int y = b; y > b - 70; y -= 6)
            mq_circle(q, sx - 20 + mq_isin((b - y) * 6) * 6 / 1024, y, 3, grey(Q_GREEN, on));
        mq_oval(q, sx + 4, b - 70, 34, 9, grey(Q_WHITE, on));
        mq_fill(q, sx - 6, b - 96, 30, 24, grey(RGB(214, 208, 248), on));
        roof_tri(q, sx - 10, sx + 4, b - 96, 10, grey(Q_PINK, on)), roof_tri(q, sx + 14, sx + 28, b - 96, 10, grey(Q_PINK, on));
        mq_fill(q, sx + 5, b - 86, 8, 12, grey(Q_PURPLE, on));
        break;
    }
    case G_STAR: /* the harbor: a pier and its lighthouse */
        mq_fill(q, sx - 44, b - 10, 88, 10, grey(RGB(150, 200, 240), on));
        mq_fill(q, sx - 40, b - 14, 60, 4, grey(Q_BROWN, on));
        mq_fill(q, sx + 20, b - 84, 14, 74, grey(Q_WHITE, on));
        mq_fill(q, sx + 20, b - 64, 14, 8, grey(Q_HOTPINK, on)), mq_fill(q, sx + 20, b - 40, 14, 8, grey(Q_HOTPINK, on));
        mq_fill(q, sx + 18, b - 92, 18, 8, grey(Q_INK, on));
        if (on && (t / 500) % 2)
            mq_circle(q, sx + 27, b - 88, 3, Q_YELLOW);
        mq_fill(q, sx - 26, b - 26, 2, 14, Q_INK), mq_fill(q, sx - 24, b - 26, 10, 8, grey(Q_WHITE, on)); /* a boat's sail */
        break;
    }
    if (hr)
        mq_hr_end(q);
    /* how far along it is: closed -- scaffolding and a little lock sign */
    if (!on) {
        hr = mq_hr_sprite(q, "town/scaffold", sx - 46, b - 72, 92, 72, 0, 0);
        for (int k = 0; k < 4; k++)
            mq_fill(q, sx - 44 + k * 29, b - 70, 3, 70, RGB(206, 176, 140));
        for (int y = b - 66; y < b; y += 22)
            mq_fill(q, sx - 44, y, 90, 3, RGB(206, 176, 140));
        mq_rrect(q, sx - 9, b - 40, 18, 14, 3, Q_DIM);
        mq_ring(q, sx, b - 41, 5, Q_DIM);
        if (hr)
            mq_hr_end(q);
    }
    if (stage >= 2) /* half the stars: bunting */
        bunting(q, sx - 40, sx + 40, b - 66);
    if (stage >= 3) { /* the finale won: a golden star, balloons, twinkling lights */
        int y = w == G_CLOCK ? b - 108 : w == G_SKY ? b - 112 : b - 88;
        mq_star(q, sx, y - (int)(t / 300 % 2), 9, Q_YELLOW);
        for (int k = 0; k < 2; k++) {
            int bx = sx + (k ? 44 : -44), by = b - 54 - (int)((t / 200 + (uint32_t)k * 3) % 4);
            mq_line(q, bx, by + 6, bx + (k ? -2 : 2), b - 34, Q_DIM);
            mq_oval(q, bx, by, 5, 6, k ? Q_SKY : Q_HOTPINK);
        }
        for (int k = 0; k < 6; k++)
            if ((t / 250 + (uint32_t)k) % 3)
                mq_circle(q, sx - 35 + k * 14, b - 2, 1, k % 2 ? Q_YELLOW : Q_WHITE);
    }
}

/* her house */
static void house(mq_t *q, int sx)
{
    int b = GROUND;
    int hr = mq_hr_sprite(q, "town/house", sx - 40, b - 72, 80, 72, 0, 0);
    mq_fill(q, sx - 30, b - 48, 60, 48, Q_PEACH);
    roof_tri(q, sx - 36, sx + 36, b - 48, 22, Q_HOTPINK);
    window_(q, sx - 22, b - 38, true, true), window_(q, sx + 12, b - 38, true, true);
    shingles(q, sx - 36, sx + 36, b - 48, 22, RGB(240, 100, 150));
    chimney(q, sx + 16, b - 66, q->anim_t);
    door(q, sx, b, 14, 24, Q_PURPLE, true);
    mq_heart(q, sx, b - 60, 4, Q_WHITE);
    if (hr)
        mq_hr_end(q);
    /* what she owns shows outside: a plant, a lamp, a toy by the door (half size), a pet house */
    mq_home_t *h = mq_home(q);
    static const uint8_t cats[3] = {HC_PLANT, HC_LIGHT, HC_TOY};
    static const int8_t at[3] = {12, -30, 24}; /* beside the door, by the corner, in front */
    for (int c = 0; c < 3; c++)
        for (int id = 1; id <= MQ_NITEM; id++)
            if (mq_item[id].cat == cats[c] && h->own[id] && mq_item[id].aw && mq_item[id].aw <= 24 && mq_item[id].ah <= 24) {
                mq_item_draw(q, id, sx + at[c] - mq_item[id].aw / 4, b - mq_item[id].ah / 2, 0, 0, 0, -1);
                break;
            }
    if (mq_pet_id(q)) { /* a little pet house */
        hr = mq_hr_sprite(q, "town/pethouse", sx + 30, b - 20, 22, 20, 0, 0);
        mq_fill(q, sx + 34, b - 10, 14, 10, Q_SKY);
        roof_tri(q, sx + 32, sx + 50, b - 10, 7, Q_HOTPINK);
        mq_circle(q, sx + 41, b - 3, 3, Q_INK);
        if (hr)
            mq_hr_end(q);
    }
}

/* the notice board with the day's three quests */
static void board(mq_t *q, int sx)
{
    mq_home_t *h = mq_home(q);
    int b = GROUND;
    mq_fill(q, sx - 26, b - 20, 3, 20, Q_BROWN), mq_fill(q, sx + 23, b - 20, 3, 20, Q_BROWN);
    mq_rrect(q, sx - 30, b - 60, 60, 42, 4, RGB(214, 170, 120));
    mq_rrect(q, sx - 27, b - 57, 54, 36, 3, RGB(236, 206, 160));
    for (int i = 0; i < 3; i++) { /* three scrolls */
        int have, need, y = b - 54 + i * 11;
        (void)mq_quest_text(q, i, &have, &need);
        bool done = (h->quest_done >> i) & 1;
        mq_rrect(q, sx - 22, y, 44, 9, 3, done ? Q_MINT : Q_WHITE);
        mq_fill(q, sx - 20, y + 2, 40 * have / (need ? need : 1), 5, done ? Q_GREEN : Q_PEACH);
        if (done)
            mq_star(q, sx + 24, y + 4, 4, Q_YELLOW);
    }
}

/* the funfair corner: the arcade (a carousel and a little ferris wheel) */
static void funfair(mq_t *q, int sx)
{
    int b = GROUND, a = (int)(q->anim_t / 30 % 360);
    int cx = sx + 26, cy = b - 56;
    int hr = mq_hr_sprite(q, "town/funfair", sx - 46, b - 90, 104, 90, 0, 0);
    mq_ring(q, cx, cy, 30, RGB(206, 196, 240));
    for (int k = 0; k < 6; k++) {
        int gx = cx + mq_icos(a + k * 60) * 30 / 1024, gy = cy + mq_isin(a + k * 60) * 30 / 1024;
        mq_line(q, cx, cy, gx, gy, RGB(214, 206, 244));
        mq_rrect(q, gx - 4, gy, 9, 7, 2, k % 2 ? Q_PINK : Q_SKY);
    }
    mq_line(q, cx, cy, cx - 12, b, Q_DIM), mq_line(q, cx, cy, cx + 12, b, Q_DIM);
    /* the carousel */
    mq_fill(q, sx - 40, b - 12, 44, 12, Q_PINK);
    roof_tri(q, sx - 44, sx + 8, b - 40, 16, Q_HOTPINK);
    mq_fill(q, sx - 40, b - 40, 44, 4, Q_YELLOW);
    for (int k = 0; k < 4; k++) {
        int px = sx - 34 + k * 12, up = mq_isin((int)(q->anim_t / 6 % 360) + k * 90) * 4 / 1024;
        mq_fill(q, px + 2, b - 36, 1, 24, Q_DIM);
        mq_rrect(q, px - 3, b - 26 + up, 10, 6, 3, k % 2 ? Q_WHITE : Q_LAV);
    }
    if (hr)
        mq_hr_end(q);
}

/* the Sparkle Kitchen (the cake counter): a candy cart with a striped awning, a cake on top,
 * sparkles over it while there is energy to spend */
static void kitchen(mq_t *q, int sx)
{
    int b = GROUND;
    uint32_t t = q->anim_t;
    int hr = mq_hr_sprite(q, "town/kitchen", sx - 40, b - 56, 80, 56, 0, 0);
    mq_rrect(q, sx - 34, b - 40, 68, 34, 6, RGB(255, 228, 238));
    mq_fill(q, sx - 34, b - 14, 68, 4, Q_HOTPINK);
    for (int k = 0; k < 6; k++) /* the awning */
        mq_rrect(q, sx - 40 + k * 14, b - 54, 14, 14, 5, k % 2 ? Q_WHITE : Q_MINT);
    mq_fill(q, sx - 30, b - 34, 60, 14, RGB(255, 250, 240)); /* the counter, treats on it */
    if (hr)
        mq_hr_end(q);
    mq_merge_item(q, CK_CODE(CK_PINK, 2), sx - 18, b - 28, 6), mq_merge_item(q, CK_WHOLE(CK_PINK), sx, b - 28, 6),
        mq_merge_item(q, CK_CODE(CK_LEMON, 3), sx + 18, b - 28, 6);
    hr = mq_hr_sprite(q, "town/kitchen_wheels", sx - 30, b - 11, 60, 11, 0, 0);
    mq_circle(q, sx - 22, b - 4, 6, Q_INK), mq_circle(q, sx + 22, b - 4, 6, Q_INK); /* wheels */
    mq_circle(q, sx - 22, b - 4, 3, Q_GREY), mq_circle(q, sx + 22, b - 4, 3, Q_GREY);
    if (hr)
        mq_hr_end(q);
    mq_merge_item(q, CK_WHOLE(CK_PINK), sx, b - 68 - (int)(t / 400 % 2), 11); /* the cake sign */
    int en = mq_extra(q)->energy;
    if (en > 0)
        for (int k = 0; k < 3; k++)
            if ((t / 250 + (uint32_t)k) % 3)
                mq_star(q, sx - 26 + k * 26, b - 80 + (k % 2) * 6, 3, Q_YELLOW);
}

/* the town's little things that come with the unlock ladder: in the gaps between buildings */
static void deco(mq_t *q, int i, int x)
{
    int y = GROUND, hr = 0;
    int32_t dv[1] = {(int)(q->anim_t / 600) % 2 || q->tint ? 1 : 0};
    static const char did[5][18] = {"town/cottage", "town/lamp", "town/fountain", "town/tree", "town/cottage_sky"};
    static const int8_t dbox[4][4] = {{-12, -26, 24, 26}, {-10, -42, 20, 42}, {-10, -20, 20, 20}, {-10, -36, 24, 36}};
    hr = mq_hr_sprite(q, did[i % 4 == 0 && i >= 4 ? 4 : i % 4], x + dbox[i % 4][0], y + dbox[i % 4][1], dbox[i % 4][2],
                      dbox[i % 4][3], dv, 1);
    switch (i % 4) {
    case 0: { /* a little house */
        mq_fill(q, x - 9, y - 14, 18, 14, i < 4 ? Q_PEACH : Q_SKY);
        roof_tri(q, x - 11, x + 11, y - 14, 10, Q_HOTPINK);
        mq_fill(q, x - 2, y - 6, 5, 6, Q_BROWN);
        break;
    }
    case 1: { /* a lamp post: lit at dusk and at night */
        mq_fill(q, x - 1, y - 30, 2, 30, Q_INK);
        int tint = q->tint;
        if (tint)
            q->tint = 0, mq_circle(q, x, y - 32, 8, RGB(255, 236, 170));
        mq_circle(q, x, y - 32, 4, (q->anim_t / 600) % 2 || tint ? Q_YELLOW : RGB(255, 240, 170));
        q->tint = (uint8_t)tint;
        break;
    }
    case 2: /* a fountain */
        mq_circle(q, x, y - 3, 9, Q_SKY);
        mq_circle(q, x, y - 3, 6, Q_BLUE);
        mq_fill(q, x - 1, y - 14 - (int)(q->anim_t / 200 % 3), 2, 10, Q_SKY);
        break;
    default: /* a tree with a balloon */
        mq_fill(q, x - 2, y - 10, 4, 10, Q_BROWN);
        mq_circle(q, x, y - 16, 9, Q_GREEN);
        mq_line(q, x + 6, y - 12, x + 9, y - 26, Q_DIM);
        mq_circle(q, x + 9, y - 29 - (int)(q->anim_t / 300 % 2), 4, Q_HOTPINK);
        break;
    }
    if (hr)
        mq_hr_end(q);
}

static int world_stage(mq_t *q, int w)
{
    mq_profile_t *p = mq_prof(q);
    if (!mq_world_open(p, w))
        return 0;
    if (mq_boss_won(p, w))
        return 3;
    int n = mq_world_levels(w), got = 0;
    for (int l = 0; l < n; l++)
        got += p->sk[mq_levels[w][l] - 1].stars;
    return got * 2 >= n * 3 ? 2 : 1;
}

/* ------------------------------------------------------------------ the scene */
static int cam(mq_t *q) { return q->town_cam; }

void mq_town_enter(mq_t *q)
{
    if (!q->town_x)
        q->town_x = place_x[PL_WORLD + (q->world < MQ_NWORLD ? q->world : 0)];
    q->town_tx = q->town_x, q->town_go = 0, q->town_drag = 0, q->town_free = 0;
    q->pet_x = (int16_t)(q->town_x - 24);
    q->town_cam = (int16_t)clampi(q->town_x - 150, 0, TOWN_W - MQ_W);
    if (mq_prof(q)->used) {
        mq_home_t *h = mq_home(q);
        mq_events_enter(q); /* a new day's special: announced */
        if (!h->quest_day)
            mq_quest_roll(q); /* the notice board's quests */
        mq_pet_give(q);
        if (h->town_seen < h->ladder) { /* something new in town since she last looked */
            q->api->snprintf(q->toast_in, sizeof q->toast_in, "The town grew!");
            mq_toast(q, q->toast_in);
            q->meta_t = mq_mark(q);
            h->town_seen = h->ladder;
            q->dirty = true;
            mq_spark_burst(q, 160, MQ_Y0 + 100, 18);
        }
    }
}

/* where she stands: the place in front of her (-1 none) */
static int place_at(int x)
{
    for (int i = 0; i < PL_COUNT; i++)
        if (iabs(x - place_x[i]) < 40)
            return i;
    return -1;
}

static void go_in(mq_t *q, int pl)
{
    if (pl == PL_HOME)
        mq_press(q, B_GOHOME, 0);
    else if (pl == PL_BOARD) {
        for (int i = 0; i < 3; i++) /* the board: the first quest not done yet */
            if (!((mq_home(q)->quest_done >> i) & 1)) {
                mq_press(q, B_QUEST, i);
                return;
            }
        mq_press(q, B_QUEST, 0);
    } else if (pl == PL_ARCADE)
        mq_press(q, B_ARCADE, 0);
    else if (pl == PL_KITCHEN)
        mq_merge_open(q);
    else if (pl >= PL_WORLD && pl < PL_WORLD + MQ_NWORLD)
        mq_press(q, B_WORLD, pl - PL_WORLD);
}

/* walk to place pl and go in when there */
static void walk_to(mq_t *q, int pl, bool enter)
{
    q->town_tx = place_x[pl];
    q->town_go = enter ? (uint8_t)(pl + 1) : 0;
    q->town_free = 0;
    if (iabs(q->town_tx - q->town_x) < 4 && enter) { /* already there */
        q->town_go = 0;
        go_in(q, pl);
    }
}

void mq_town_frame(mq_t *q)
{
    if (q->scene != SC_MAP)
        return;
    int d = q->town_tx - q->town_x;
    if (d) {
        int v = imax(5, iabs(d) / 10);
        q->town_x = (int16_t)(q->town_x + (d > 0 ? imin(v, d) : imax(-v, d)));
        q->town_face = d < 0;
    } else if (q->town_go) {
        int pl = q->town_go - 1;
        q->town_go = 0;
        go_in(q, pl);
        return;
    }
    int pd = q->town_x - (q->town_face ? -22 : 22) - q->pet_x; /* the pet trots after her */
    if (iabs(pd) > 2)
        q->pet_x = (int16_t)(q->pet_x + pd / 4 + (pd > 0 ? 1 : -1));
    if (!q->town_free) { /* the camera follows her, gently */
        int want = clampi(q->town_x - 150, 0, TOWN_W - MQ_W), c = q->town_cam;
        q->town_cam = (int16_t)(c + (want - c) / 5 + (want > c ? 1 : want < c ? -1 : 0));
        if (iabs(want - q->town_cam) <= 1)
            q->town_cam = (int16_t)want;
    }
}



void mq_town_scene(mq_t *q)
{
    mq_profile_t *p = mq_prof(q);
    mq_home_t *h = mq_home(q);
    int c = cam(q);
    uint32_t t = q->anim_t;
    int tod = mq_town_time(q); /* day, dusk, night: the whole street tinted, the lights glowing */
    q->tint = (uint8_t)tod;
    mq_hr_scene(q, 20, mq_tint(q, RGB(206, 236, 255)), mq_tint(q, RGB(170, 226, 186)), Q_HOTPINK); /* the town (a wide window's sides) */
    /* the sky and the far layer (slow: parallax) */
    int32_t tv[2] = {tod, c};
    int hr = mq_hr_sprite(q, "town/sky", 0, MQ_Y0 + 18, MQ_W, GROUND - MQ_Y0 - 18, tv, 2);
    mq_fill(q, 0, MQ_Y0 + 18, MQ_W, 50, tod == TINT_DUSK ? RGB(255, 200, 190) : RGB(206, 236, 255));
    mq_fill(q, 0, MQ_Y0 + 68, MQ_W, GROUND - MQ_Y0 - 68, tod == TINT_DUSK ? RGB(255, 220, 200) : RGB(220, 242, 255));
    if (hr)
        mq_hr_end(q);
    if (tod == TINT_NIGHT) { /* stars and the moon */
        q->tint = 0;
        for (int i = 0; i < 14; i++)
            if ((t / 300 + (uint32_t)i * 7) % 5)
                mq_star(q, (i * 47 - c / 6 + 400) % 330 - 5, MQ_Y0 + 24 + (i * 19) % 50, 2 + (i % 3 == 0), Q_WHITE);
        mq_circle(q, 270 - c / 10 % 40, MQ_Y0 + 36, 10, RGB(255, 246, 200));
        mq_circle(q, 275 - c / 10 % 40, MQ_Y0 + 32, 9, RGB(70, 82, 140));
        q->tint = (uint8_t)tod;
    }
    for (int i = 0; i < 5; i++) { /* clouds drift and move a little with the street */
        int x = ((i * 170 - c / 4 - (int)(t / 120)) % 600 + 600) % 600 - 60;
        int y = MQ_Y0 + 30 + (i % 3) * 14;
        int32_t cv[1] = {i};
        hr = mq_hr_sprite(q, "town/cloud", x - 7, y - 13, 33, 21, cv, 1);
        mq_circle(q, x, y, 6, Q_WHITE), mq_circle(q, x + 9, y - 4, 8, Q_WHITE), mq_circle(q, x + 19, y, 6, Q_WHITE);
        mq_fill(q, x, y, 19, 7, Q_WHITE);
        if (hr)
            mq_hr_end(q);
    }
    mq_events_town(q, c, 0); /* (a rainbow, kites, shooting stars: behind the hills) */
    town_life(q, c, false);
    hr = mq_hr_sprite(q, "town/hills", 0, GROUND - 28, MQ_W, 52, tv, 2);
    for (int i = 0; i < 6; i++) /* far hills */
        mq_oval(q, i * 120 - (c / 3) % 120 + 20, GROUND - 2, 80, 26, i % 2 ? RGB(190, 230, 200) : RGB(176, 222, 192));
    if (hr)
        mq_hr_end(q);
    /* the street layer */
    int n = (h->ladder + 1) / 2 > 10 ? 10 : (h->ladder + 1) / 2;
    for (int i = 0; i < n; i++) { /* the little things of the ladder, in the gaps */
        int x = (place_x[1 + i % 10] + place_x[2 + i % 10]) / 2 - c;
        if (x > -30 && x < MQ_W + 30)
            deco(q, i, x);
    }
    /* the places; their signs are put up last, over the friends strolling by (a name always reads) */
    struct { int16_t x, y; const char *s; bool on; } sg[PL_COUNT];
    int nsg = 0;
    char home_s[24];
    q->api->snprintf(home_s, sizeof home_s, "%s's", mq_name(q));
#define SIGN(x_, y_, s_, on_) (sg[nsg].x = (int16_t)(x_), sg[nsg].y = (int16_t)(y_), sg[nsg].s = (s_), sg[nsg++].on = (on_))
    for (int pl = 0; pl < PL_COUNT; pl++) {
        int sx = place_x[pl] - c;
        if (sx < -70 || sx > MQ_W + 70)
            continue;
        if (pl == PL_HOME) {
            house(q, sx);
            SIGN(sx, GROUND - 82, home_s, true); /* (no wider than the house: sign() fits it) */
        } else if (pl == PL_BOARD) {
            board(q, sx);
            SIGN(sx, GROUND - 76, "quests", true);
        } else if (pl == PL_ARCADE) {
            funfair(q, sx);
            SIGN(sx - 18, GROUND - 74, "Arcade", true);
        } else if (pl == PL_KITCHEN) {
            kitchen(q, sx);
            SIGN(sx, GROUND + 1, "Kitchen", true);
        } else {
            int w = pl - PL_WORLD, st = world_stage(q, w);
            building(q, w, sx, st);
            SIGN(sx, GROUND + 1, mq_world_name[w], st > 0);
            if (w == G_BAKERY) { /* the cafe's table and its tip jar */
                int jx = sx + 52, jy = GROUND - 2;
                mq_fill(q, jx - 10, jy - 12, 20, 3, Q_WHITE), mq_fill(q, jx - 1, jy - 10, 2, 10, Q_DIM);
                mq_rrect(q, jx - 6, jy - 24, 12, 12, 3, RGB(200, 230, 245));
                for (int k = 0; k < h->tips && k < 6; k++)
                    mq_circle(q, jx - 3 + (k % 3) * 3, jy - 15 - (k / 3) * 3, 1, Q_YELLOW);
                if (h->tips >= 5 && (t / 400) % 2)
                    mq_star(q, jx + 8, jy - 26, 4, Q_YELLOW);
                for (int k = 0, m = 0; k < 6; k++) /* treats from the Sparkle Kitchen on the cafe's table */
                    if (mq_extra(q)->cafe[k])
                        mq_merge_item(q, mq_extra(q)->cafe[k], jx - 30 + (m % 3) * 9, jy - 16 - (m / 3) * 7, 4), m++;
            }
        }
    }
    mq_events_town(q, c, 1); /* today's special: a visitor, lanterns, a parade, a picnic, a party */
    /* the street (the friends stroll on its far side), the pavement she walks on, the lawn */
    hr = mq_hr_sprite(q, "town/street", 0, GROUND + 14, MQ_W, MQ_Y0 + MQ_H - GROUND - 14, tv, 2);
    mq_fill(q, 0, GROUND + 14, MQ_W, 18, RGB(236, 226, 240));
    mq_fill(q, 0, GROUND + 32, MQ_W, WALK - GROUND - 26, RGB(250, 232, 220));
    mq_fill(q, 0, GROUND + 32, MQ_W, 2, RGB(226, 200, 190));
    for (int k = 0; k < 12; k++) /* paving stones */
        mq_fill(q, k * 32 - c % 32, WALK - 6, 16, 2, RGB(236, 214, 204));
    mq_fill(q, 0, WALK + 6, MQ_W, MQ_Y0 + MQ_H - WALK - 6, RGB(170, 226, 186));
    for (int k = 0; k < 16; k++) { /* flowers in the lawn, the near layer: they move faster */
        int fx = ((k * 47 - c * 5 / 4) % 752 + 752) % 752 - 20;
        if (fx < MQ_W + 10)
            mq_circle(q, fx, WALK + 10 + (k % 3) * 2, 2, k % 3 == 0 ? Q_PINK : k % 3 == 1 ? Q_YELLOW : Q_WHITE);
    }
    if (hr)
        mq_hr_end(q);
    q->tint = 0; /* the friends, her and her pet: bright (as if in the lamplight) */
    /* friends who moved into town stroll about (behind her); best friends walk together; one
     * of them talks now and then about what she did (they remember) */
    int news = mq_chapter_news(q), buddy = p->buddy % MQ_NCAST;
    int fx[MQ_NCAST], nw = 0;
    for (int k = 0; k < MQ_NCAST; k++) {
        fx[k] = -9999;
        if (!((p->met >> k) & 1) || k == buddy || k == news || nw >= 5 || k == mq_events_birthday(q)) /* (the birthday friend is at the party) */
            continue;
        int bf = mq_best_friend(k);
        if (bf < k && fx[bf] > -9999) /* with a best friend: side by side */
            fx[k] = fx[bf] + 30;
        else {
            int span = TOWN_W - 100, x = (int)((t / 30 + (uint32_t)k * 331) % (uint32_t)(2 * span));
            fx[k] = (x < span ? x : 2 * span - x) + 50;
        }
        nw++;
    }
    int who = -1, best = 1000;
    uint32_t say_ms = (uint32_t)mq_read_ms_len(q, 44); /* (a line stays its reading time: the longest's, 3.5 s at least) */
    if (say_ms < 3500u)
        say_ms = 3500u;
    if ((t / say_ms) % 2 == 0) /* every other few seconds the friend nearest the middle says something */
        for (int k = 0; k < MQ_NCAST; k++) {
            int x = fx[k] - c;
            if (fx[k] > -9999 && x > 40 && x < MQ_W - 40 && iabs(x - 160) < best)
                best = iabs(x - 160), who = k;
        }
    for (int k = 0; k < MQ_NCAST; k++) {
        int x = fx[k] - c;
        if (fx[k] > -9999 && x > -24 && x < MQ_W + 24)
            mq_cast(q, k, x - 20, GROUND - 8 - (k == who ? 0 : (int)(t / 200 % 2)), 40, k == who ? AN_TALK : AN_IDLE);
    }
    if (who >= 0) {
        char line[48];
        mq_chatter(q, who, (int)(t / (2 * say_ms)) + who, line, sizeof line);
        uint16_t said = (uint16_t)(who * 4096 + (t / say_ms) % 4096);
        if (said != q->town_said) { /* a new line: their voice */
            q->town_said = said;
            mq_voice(q, who, line);
        }
        int x = fx[who] - c;
        const font_t *f = q->api->font_small;
        int w = mq_text_w(q, f, line) + 10;
        w = w > MQ_W - 4 ? MQ_W - 4 : w; /* (a long name in the line: it fits the screen) */
        int bx = clampi(x - w / 2, 2, MQ_W - w - 2), by = GROUND - 30;
        mq_rrect(q, bx, by, w, 14, 6, Q_WHITE);
        mq_fill(q, x - 2, by + 14, 4, 3, Q_WHITE);
        mq_text_fit(q, f, bx + 5, by, w - 10, line, Q_INK, false);
    }
    /* her pet and her */
    int px = q->pet_x - c, hx = q->town_x - c;
    bool walking = q->town_tx != q->town_x;
    mq_pet_draw(q, px, WALK + 2, walking, q->town_face);
    q->buddy_anim = walking ? AN_DANCE : AN_IDLE;
    mq_buddy_draw(q, p->buddy % MQ_NCAST, hx - 20, WALK - 40 - (walking ? (int)(t / 80 % 2) * 2 : 0), 2, 1);
    q->buddy_anim = 0;
    int here = place_at(q->town_x);
    if (!walking && here >= 0 && news < 0) { /* stopped at a door: a "go in!" bubble beside her (a tap on it, ENTER or UP) */
        int gx = imin(hx + GOIN_DX, MQ_W - GOIN_W - 2), gy = WALK - GOIN_DY + ((t / 400) % 2);
        mq_rrect(q, gx, gy, GOIN_W, 15, 7, Q_YELLOW);
        mq_text_c(q, q->api->font_small, gx + GOIN_W / 2, gy + 1, "go in!", Q_INK);
    }
    if (news >= 0) { /* a friend with news runs up to her: a "!" bubble, a tap (or DOWN) to hear it */
        int nx = news == buddy ? hx : hx + 34 + (walking ? 0 : (int)(t / 300 % 2)), ny = WALK - 40;
        if (news != buddy)
            mq_cast(q, news, nx - 20, ny, 40, (t / 600) % 2 ? AN_WOW : AN_HAPPY);
        /* (beside the head, under the signs' line: never over a building's name) */
        int bx = nx + 25, by = GROUND + 15 - (int)(t / 150 % 4 == 1 ? 2 : 0);
        mq_rrect(q, bx - 9, by, 18, 16, 7, Q_HOTPINK);
        mq_poly(q, (int16_t[3]){(int16_t)(bx - 8), (int16_t)(bx - 8), (int16_t)(bx - 13)}, (int16_t[3]){(int16_t)(by + 9), (int16_t)(by + 14), (int16_t)(by + 14)}, 3, Q_HOTPINK);
        mq_text_c(q, q->api->font_medium, bx, by, "!", Q_WHITE);
    }
    town_life(q, c, true);
    for (int i = 0; i < nsg; i++)
        sign(q, sg[i].x, sg[i].y, sg[i].s, sg[i].on);
#undef SIGN
    mq_events_town(q, c, 2); /* (bubble day's bubbles, snow in front) */
    /* the street's edges: arrows that say there is more */
    if (c > 4)
        mq_poly(q, (int16_t[3]){4, 12, 12}, (int16_t[3]){(int16_t)(WALK - 10), (int16_t)(WALK - 16), (int16_t)(WALK - 4)}, 3, Q_DIM);
    if (c < TOWN_W - MQ_W - 4)
        mq_poly(q, (int16_t[3]){316, 308, 308}, (int16_t[3]){(int16_t)(WALK - 10), (int16_t)(WALK - 16), (int16_t)(WALK - 4)}, 3, Q_DIM);
}

/* ------------------------------------------------------------------ input */
/* a tap on the street: the place under it (walk there and go in), the tip jar */
bool mq_town_touch(mq_t *q, int type, int x, int y)
{
    if (q->scene != SC_MAP || y < MQ_Y0 + 18 || y > MQ_Y0 + MQ_H - 22)
        return false;
    int wx = x + q->town_cam;
    if (type == 1) { /* down: maybe a drag */
        q->drag_x0 = (int16_t)x, q->drag_cam0 = q->town_cam, q->town_drag = 0;
        return true;
    }
    if (type == 2) { /* drag: look along the street */
        if (iabs(x - q->drag_x0) > 6 || q->town_drag) {
            q->town_drag = 1, q->town_free = 1;
            q->town_cam = (int16_t)clampi(q->drag_cam0 - (x - q->drag_x0), 0, TOWN_W - MQ_W);
        }
        return true;
    }
    if (type == 3)
        return true;
    if (type != 4 || q->town_drag)
        return true;
    if (mq_events_tap(q, wx, y)) /* today's visitor: their wish */
        return true;
    int news = mq_chapter_news(q); /* a friend with news beside her: a tap hears it */
    if (news >= 0) {
        int nx = q->town_x + (news == mq_prof(q)->buddy % MQ_NCAST ? 0 : 34);
        if (iabs(wx - nx) < 22 && y > WALK - 64 && y < WALK + 4) {
            mq_chapter_open(q, news);
            return true;
        }
    }
    int jx = place_x[PL_WORLD + G_BAKERY] + 52; /* the tip jar */
    if (iabs(wx - jx) < 14 && y > GROUND - 32 && y < GROUND + 4) {
        mq_press(q, B_TIPS, 0);
        return true;
    }
    { /* the "go in!" bubble beside her: in */
        int here = place_at(q->town_x), hx = q->town_x - q->town_cam, gx = imin(hx + GOIN_DX, MQ_W - GOIN_W - 2);
        if (here >= 0 && q->town_tx == q->town_x && x >= gx - 4 && x < gx + GOIN_W + 4 && y >= WALK - GOIN_DY - 6 &&
            y < WALK - GOIN_DY + 20) {
            go_in(q, here);
            return true;
        }
    }
    /* a tap on a building itself (its walls and door, not the street in front of it): she walks there and goes in.
     * Walking never goes in: a tap on the street or the pavement walks her there and no further, whatever she
     * passes (the user's report: walking along the street went into a building by itself) */
    for (int pl = 0; pl < PL_COUNT; pl++)
        if (iabs(wx - place_x[pl]) < 40 && y > GROUND - 120 && y < GROUND + 14) {
            walk_to(q, pl, true);
            mq_sfx(q, MODX_SFX_STEP, pl);
            return true;
        }
    q->town_tx = (int16_t)clampi(wx, 30, TOWN_W - 30), q->town_go = 0, q->town_free = 0; /* walk there */
    return true;
}

bool mq_town_swipe(mq_t *q, int dir)
{
    if (q->scene != SC_MAP || dir > 1)
        return false;
    if (q->town_drag) /* the drag already moved the street */
        return true;
    q->town_free = 1;
    q->town_cam = (int16_t)clampi(q->town_cam + (dir == 0 ? 200 : -200), 0, TOWN_W - MQ_W);
    return true;
}

bool mq_town_key(mq_t *q, int k)
{
    enum { HK_UP = 12, HK_LEFT = 13, HK_RIGHT = 14, HK_ENTER = 49 };
    if (q->scene != SC_MAP)
        return false;
    if (k == HK_LEFT || k == HK_RIGHT) { /* walk to the next place */
        int here = 0, best = -1;
        for (int i = 0; i < PL_COUNT; i++)
            if (iabs(place_x[i] - q->town_tx) < iabs(place_x[here] - q->town_tx))
                here = i;
        for (int i = 0; i < PL_COUNT; i++) /* the next place along the street that way */
            if (k == HK_RIGHT ? place_x[i] > place_x[here] && (best < 0 || place_x[i] < place_x[best])
                              : place_x[i] < place_x[here] && (best < 0 || place_x[i] > place_x[best]))
                best = i;
        if (best < 0)
            best = here;
        walk_to(q, best, false);
        mq_sfx(q, MODX_SFX_STEP, best);
        q->kbd = true;
        return true;
    }
    if (k == 15 /* DOWN */ && mq_chapter_news(q) >= 0) { /* a friend's news */
        mq_chapter_open(q, mq_chapter_news(q));
        return true;
    }
    if (k == HK_ENTER || k == HK_UP) {
        int here = place_at(q->town_tx);
        if (here >= 0)
            walk_to(q, here, true);
        return true;
    }
    return false;
}

/* `mq town X`: stand at x of the street (tests) */
void mq_town_set(mq_t *q, int x)
{
    q->town_x = q->town_tx = (int16_t)clampi(x, 20, TOWN_W - 20);
    q->pet_x = (int16_t)(q->town_x - 22);
    q->town_cam = (int16_t)clampi(q->town_x - 150, 0, TOWN_W - MQ_W), q->town_free = 0;
}
