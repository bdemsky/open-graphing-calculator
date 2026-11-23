/*
 * Math Quest: scenery -- every game is played in a place, never on a blank panel.
 *
 * mq_backdrop draws a world's place behind a game's field, in layers: the sky or the wall, far
 * things (hills, rooftops, a ferris wheel, the sea), near things (flowers, shelves, bunting), and
 * small animated details at two speeds for a gentle parallax: clouds drifting, flowers swaying,
 * steam curling from cups, gears turning, flags waving, boats bobbing, a lighthouse beam. All
 * from shapes in logical pixels, timed by the animation clock (deterministic under `mq step`);
 * a frame of it costs about 60-160 rectangles (docs/MATHQUEST.md, "Performance").
 */
#include "mq.h"

static int tri_wave(uint32_t t, int period, int amp) /* -amp .. amp, a triangle wave */
{
    int p = (int)(t % (uint32_t)period), h = period / 2;
    int v = p < h ? p : period - p;
    return v * 2 * amp / h - amp;
}

static void cloud(mq_t *q, int x, int y, int s, uint16_t c)
{
    mq_circle(q, x, y, 5 * s / 4, c);
    mq_circle(q, x + 6 * s / 4, y - 3 * s / 4, 7 * s / 4, c);
    mq_circle(q, x + 13 * s / 4, y, 5 * s / 4, c);
    mq_fill(q, x, y, 13 * s / 4, 5 * s / 4 + 1, c);
}

static void flower(mq_t *q, int x, int y, int sway, uint16_t c)
{
    mq_line(q, x, y, x + sway, y - 12, RGB(92, 170, 110));
    mq_fill(q, x + 1, y - 5, 3, 2, RGB(120, 200, 130));
    int fx = x + sway, fy = y - 14;
    mq_circle(q, fx - 2, fy, 2, c), mq_circle(q, fx + 2, fy, 2, c);
    mq_circle(q, fx, fy - 2, 2, c), mq_circle(q, fx, fy + 2, 2, c);
    mq_fill(q, fx - 1, fy - 1, 2, 2, Q_YELLOW);
}

static void gear(mq_t *q, int cx, int cy, int r, int ang, uint16_t c, uint16_t hole)
{
    mq_circle(q, cx, cy, r, c);
    for (int k = 0; k < 8; k++) {
        int a = ang + k * 45, tx = cx + mq_icos(a) * (r + 2) / 1024, ty = cy + mq_isin(a) * (r + 2) / 1024;
        mq_fill(q, tx - 2, ty - 2, 5, 5, c);
    }
    mq_circle(q, cx, cy, r / 3, hole);
}

static void steam(mq_t *q, int x, int y, uint32_t t, int k)
{
    for (int i = 0; i < 3; i++) {
        int ph = (int)((t / 30 + (uint32_t)(i * 40 + k * 23)) % 120); /* rises 0..120 */
        int sx = x + tri_wave(t / 10 + (uint32_t)(i * 37), 60, 3), sy = y - ph / 6;
        int r = 1 + ph / 50;
        mq_circle(q, sx, sy, r, RGB(255, 255, 255));
    }
}

static void cup(mq_t *q, int x, int y, uint16_t c, uint32_t t, int k)
{
    mq_rrect(q, x - 6, y - 8, 12, 9, 3, c);
    mq_ring(q, x + 7, y - 4, 2, c);
    mq_fill(q, x - 8, y, 16, 2, Q_WHITE);
    steam(q, x, y - 10, t, k);
}

static void flag_line(mq_t *q, int y, uint32_t t)
{
    static const uint16_t c[5] = {Q_HOTPINK, Q_YELLOW, Q_SKY, Q_MINT, Q_LAV};
    mq_fill(q, 0, y, MQ_W, 1, Q_DIM);
    for (int i = 0; i < 12; i++) {
        int x = 6 + i * 27, w = tri_wave(t / 8 + (uint32_t)(i * 29), 80, 2);
        int16_t px[3] = {(int16_t)(x - 6), (int16_t)(x + 6), (int16_t)(x + w)}, py[3] = {(int16_t)y, (int16_t)y, (int16_t)(y + 11)};
        mq_poly(q, px, py, 3, c[i % 5]);
    }
}

/* the world's place behind a game's field, y0 .. y1 */
void mq_backdrop(mq_t *q, int world, int y0, int y1)
{
    uint32_t t = q->anim_t;
    int h = y1 - y0;
    { /* the place's colours for the sides of a wide window: sky, ground, accent */
        static const uint16_t sc[MQ_NWORLD][3] = {
            {RGB(200, 236, 250), RGB(186, 228, 196), Q_PINK},    {RGB(214, 234, 255), RGB(190, 214, 244), Q_BLUE},
            {RGB(255, 232, 240), RGB(250, 214, 220), Q_HOTPINK}, {RGB(236, 230, 255), RGB(214, 204, 244), Q_PURPLE},
            {RGB(255, 240, 224), RGB(214, 170, 130), Q_ORANGE},  {RGB(255, 250, 232), RGB(255, 236, 190), Q_YELLOW},
            {RGB(224, 246, 255), RGB(190, 236, 214), Q_HOTPINK}, {RGB(226, 222, 255), RGB(240, 240, 255), Q_LAV},
            {RGB(220, 236, 252), RGB(170, 210, 240), Q_SKY}};
        int w = world >= 0 && world < MQ_NWORLD ? world : 0;
        mq_hr_scene(q, 1 + w, mq_tint(q, sc[w][0]), mq_tint(q, sc[w][1]), mq_tint(q, sc[w][2])); /* (night: the sides too) */
    }
    /* (a high-resolution platform may paint the place from its own art: MODX_HR_SPRITE) */
    static const char bid[MQ_NWORLD][12] = {"bg/garden", "bg/post", "bg/bakery", "bg/clock", "bg/shop", "bg/art", "bg/fair",
                                            "bg/sky", "bg/star"};
    int hr = mq_hr_sprite(q, bid[world >= 0 && world < MQ_NWORLD ? world : 0], 0, y0, MQ_W, h, 0, 0);
    switch (world) {
    case G_GARDEN: default: {
        mq_fill(q, 0, y0, MQ_W, h / 3, RGB(200, 236, 250));
        mq_fill(q, 0, y0 + h / 3, MQ_W, h - h / 3, RGB(214, 244, 226));
        for (int i = 0; i < 3; i++) /* clouds: the slow layer */
            cloud(q, ((i * 130 + 360) - (int)(t / 90 % 400)) - 40, y0 + 14 + (i % 2) * 14, 4, Q_WHITE);
        mq_oval(q, 60, y1 - 4, 110, 26, RGB(186, 228, 196));    /* far hills */
        mq_oval(q, 260, y1 - 2, 120, 34, RGB(196, 234, 204));
        static const uint16_t fc[4] = {Q_PINK, Q_LAV, Q_HOTPINK, Q_YELLOW};
        for (int i = 0; i < 9; i++) /* flowers sway in the breeze: the near layer */
            flower(q, 14 + i * 37, y1 - 3, tri_wave(t / 12 + (uint32_t)(i * 31), 90, 2), fc[i % 4]);
        int bx = (int)(t / 25 % 400) - 40, by = y0 + 40 + tri_wave(t / 6, 80, 8); /* a butterfly */
        int wing = (t / 120) % 2 ? 3 : 1;
        mq_oval(q, bx - 3, by, wing, 3, Q_LAV), mq_oval(q, bx + 3, by, wing, 3, Q_LAV);
        mq_fill(q, bx, by - 2, 1, 5, Q_INK);
        break;
    }
    case G_POST: {
        mq_fill(q, 0, y0, MQ_W, h, RGB(214, 234, 255));
        for (int i = 0; i < 2; i++)
            cloud(q, ((i * 200 + 380) - (int)(t / 70 % 420)) - 40, y0 + 16 + i * 10, 4, Q_WHITE);
        static const int16_t rx[6] = {0, 52, 110, 168, 220, 272};
        for (int i = 0; i < 6; i++) { /* rooftops */
            int bh = 26 + (i * 13) % 20, x = rx[i];
            mq_fill(q, x, y1 - bh, 48, bh, i % 2 ? RGB(190, 214, 244) : RGB(200, 222, 248));
            int16_t px[3] = {(int16_t)(x - 2), (int16_t)(x + 50), (int16_t)(x + 24)},
                    py[3] = {(int16_t)(y1 - bh), (int16_t)(y1 - bh), (int16_t)(y1 - bh - 12)};
            mq_poly(q, px, py, 3, RGB(170, 196, 236));
            mq_fill(q, x + 10, y1 - bh + 8, 6, 6, RGB(255, 246, 200));
            mq_fill(q, x + 30, y1 - bh + 8, 6, 6, RGB(255, 246, 200));
        }
        int px0 = (int)(t / 12 % 480) - 60, py0 = y0 + 30 + tri_wave(t / 4, 140, 10); /* a paper plane */
        int16_t ax[3] = {(int16_t)px0, (int16_t)(px0 + 14), (int16_t)px0}, ay[3] = {(int16_t)(py0 - 5), (int16_t)py0, (int16_t)(py0 + 3)};
        mq_poly(q, ax, ay, 3, Q_WHITE);
        mq_line(q, px0, py0, px0 + 14, py0, Q_SKY);
        break;
    }
    case G_BAKERY: {
        mq_fill(q, 0, y0, MQ_W, h, RGB(255, 232, 240));
        for (int i = 0; i < 11; i++) /* striped wallpaper */
            mq_fill(q, i * 30, y0, 12, h - 30, RGB(255, 240, 246));
        mq_fill(q, 0, y1 - 30, MQ_W, 4, RGB(222, 170, 140)); /* the shelf */
        mq_fill(q, 0, y1 - 26, MQ_W, 26, RGB(250, 214, 220));
        static const uint16_t cc[4] = {Q_SKY, Q_MINT, Q_LAV, Q_PEACH};
        for (int i = 0; i < 4; i++) /* steaming cups */
            cup(q, 30 + i * 84, y1 - 31, cc[i], t, i);
        for (int i = 0; i < 3; i++) { /* cakes on the shelf */
            int x = 72 + i * 84;
            mq_rrect(q, x - 9, y1 - 42, 18, 12, 3, Q_PEACH);
            mq_rrect(q, x - 9, y1 - 44, 18, 5, 2, Q_PINK);
            mq_circle(q, x, y1 - 46, 2, RGB(236, 70, 90));
        }
        for (int i = 0; i < 2; i++) { /* hanging lamps swing a little */
            int lx = 90 + i * 140, sw = tri_wave(t / 14 + (uint32_t)(i * 50), 120, 3);
            mq_line(q, lx, y0, lx + sw, y0 + 14, Q_DIM);
            mq_circle(q, lx + sw, y0 + 17, 5, Q_YELLOW);
        }
        break;
    }
    case G_CLOCK: {
        mq_fill(q, 0, y0, MQ_W, h, RGB(236, 230, 255));
        for (int i = 0; i < 7; i++) /* twinkles */
            if ((t / 300 + (uint32_t)i * 3) % 5)
                mq_fill(q, 20 + i * 46, y0 + 6 + (i * 17) % 30, 2, 2, Q_WHITE);
        int a = (int)(t / 30 % 360);
        gear(q, 26, y1 - 30, 22, a, RGB(214, 204, 244), RGB(236, 230, 255));
        gear(q, 62, y1 - 8, 14, -a * 3 / 2 + 20, RGB(220, 210, 248), RGB(236, 230, 255));
        gear(q, 296, y0 + 34, 18, -a, RGB(214, 204, 244), RGB(236, 230, 255));
        int sw = tri_wave(t / 6, 200, 16); /* a pendulum */
        mq_line(q, 300, y0, 300 + sw, y1 - 40, RGB(200, 186, 236));
        mq_circle(q, 300 + sw, y1 - 38, 6, RGB(200, 186, 236));
        break;
    }
    case G_SHOP: {
        mq_fill(q, 0, y0, MQ_W, h, RGB(255, 240, 224));
        for (int i = 0; i < 16; i++) /* the awning's scallops */
            mq_circle(q, 10 + i * 20, y0 + 4, 10, i % 2 ? Q_WHITE : RGB(255, 196, 160));
        for (int s = 0; s < 2; s++) { /* shelves with jars */
            int y = y1 - 20 - s * 34;
            mq_fill(q, 0, y, MQ_W, 3, RGB(214, 170, 130));
            for (int i = 0; i < 7; i++) {
                int x = 20 + i * 46 + s * 20;
                mq_rrect(q, x - 6, y - 14, 12, 14, 3, s ? RGB(206, 236, 250) : RGB(255, 226, 236));
                mq_fill(q, x - 5, y - 16, 10, 3, Q_BROWN);
            }
        }
        if ((t / 400) % 3 == 0) /* a coin glints */
            mq_star(q, 40 + (int)(t / 1200 % 6) * 48, y1 - 40, 3, Q_YELLOW);
        break;
    }
    case G_ART: {
        mq_fill(q, 0, y0, MQ_W, h, RGB(255, 250, 232));
        static const uint16_t pc[5] = {Q_PINK, Q_SKY, Q_MINT, Q_YELLOW, Q_LAV};
        for (int i = 0; i < 6; i++) /* splats on the wall */
            mq_circle(q, 24 + i * 56, y0 + 22 + (i * 23) % 40, 6 + i % 3, pc[i % 5]);
        for (int i = 0; i < 6; i++) { /* colour drops float up */
            int ph = (int)((t / 20 + (uint32_t)(i * 53)) % (uint32_t)(h + 20));
            mq_circle(q, 30 + i * 52 + tri_wave(t / 9 + (uint32_t)(i * 17), 90, 4), y1 - ph, 3, pc[(i + 2) % 5]);
        }
        mq_line(q, 272, y1, 286, y1 - 46, Q_BROWN), mq_line(q, 306, y1, 290, y1 - 46, Q_BROWN); /* an easel */
        mq_fill(q, 274, y1 - 46, 30, 22, Q_WHITE), mq_circle(q, 289, y1 - 36, 5, Q_PINK);
        break;
    }
    case G_FAIR: {
        mq_fill(q, 0, y0, MQ_W, h, RGB(224, 246, 255));
        int cx = 250, cy = y0 + 56, r = 42, a = (int)(t / 40 % 360); /* the ferris wheel turns */
        mq_ring(q, cx, cy, r, RGB(196, 220, 240));
        for (int k = 0; k < 8; k++) {
            int gx = cx + mq_icos(a + k * 45) * r / 1024, gy = cy + mq_isin(a + k * 45) * r / 1024;
            mq_line(q, cx, cy, gx, gy, RGB(206, 226, 244));
            mq_rrect(q, gx - 4, gy, 8, 6, 2, k % 2 ? Q_PINK : Q_YELLOW);
        }
        mq_line(q, cx, cy, cx - 16, y1, RGB(196, 220, 240)), mq_line(q, cx, cy, cx + 16, y1, RGB(196, 220, 240));
        flag_line(q, y0 + 2, t);
        for (int i = 0; i < 3; i++) { /* balloons drift up */
            int ph = (int)((t / 18 + (uint32_t)(i * 97)) % (uint32_t)(h + 30));
            int bx = 30 + i * 70 + tri_wave(t / 11 + (uint32_t)(i * 13), 100, 5), by = y1 + 10 - ph;
            mq_oval(q, bx, by, 5, 6, i == 1 ? Q_SKY : Q_HOTPINK);
            mq_line(q, bx, by + 6, bx, by + 14, Q_DIM);
        }
        break;
    }
    case G_SKY: {
        mq_fill(q, 0, y0, MQ_W, h, RGB(226, 222, 255));
        for (int i = 0; i < 10; i++)
            if ((t / 250 + (uint32_t)i * 7) % 4)
                mq_star(q, 14 + i * 31, y0 + 8 + (i * 29) % 46, 2 + i % 2, Q_WHITE);
        int x = 230; /* the castle on its cloud */
        mq_fill(q, x, y1 - 50, 50, 34, RGB(206, 200, 244));
        mq_fill(q, x - 8, y1 - 64, 14, 48, RGB(214, 208, 248)), mq_fill(q, x + 44, y1 - 64, 14, 48, RGB(214, 208, 248));
        int16_t ax[3] = {(int16_t)(x - 10), (int16_t)(x + 8), (int16_t)(x - 1)}, ay[3] = {(int16_t)(y1 - 64), (int16_t)(y1 - 64), (int16_t)(y1 - 78)};
        mq_poly(q, ax, ay, 3, RGB(255, 196, 220));
        int16_t bx[3] = {(int16_t)(x + 42), (int16_t)(x + 60), (int16_t)(x + 51)}, by[3] = {(int16_t)(y1 - 64), (int16_t)(y1 - 64), (int16_t)(y1 - 78)};
        mq_poly(q, bx, by, 3, RGB(255, 196, 220));
        for (int i = 0; i < 3; i++)
            cloud(q, ((i * 150 + 380) - (int)(t / 60 % 440)) - 50, y1 - 10 - (i % 2) * 18, 5, Q_WHITE);
        break;
    }
    case G_STAR: {
        mq_fill(q, 0, y0, MQ_W, h * 2 / 3, RGB(220, 236, 252));
        mq_fill(q, 0, y0 + h * 2 / 3, MQ_W, h - h * 2 / 3, RGB(170, 210, 240));
        int wy = y0 + h * 2 / 3;
        for (int i = 0; i < 12; i++) /* wave crests roll */
            mq_oval(q, (int)((uint32_t)(i * 30) + t / 40 % 30) - 10, wy + 1 + (i % 2) * 8, 7, 1, Q_WHITE);
        mq_fill(q, 26, wy - 54, 12, 54, Q_WHITE), mq_fill(q, 26, wy - 40, 12, 8, Q_HOTPINK); /* the lighthouse */
        mq_fill(q, 24, wy - 60, 16, 6, Q_INK);
        int beam = tri_wave(t / 5, 400, 1000);
        if (beam > 0)
            mq_poly(q, (int16_t[3]){38, (int16_t)(38 + beam / 6), (int16_t)(38 + beam / 6)},
                    (int16_t[3]){(int16_t)(wy - 57), (int16_t)(wy - 64), (int16_t)(wy - 50)}, 3, RGB(255, 250, 200));
        for (int i = 0; i < 2; i++) { /* boats bob */
            int bx = 150 + i * 110, by = wy + 6 + tri_wave(t / 8 + (uint32_t)(i * 60), 100, 2);
            int16_t hx[4] = {(int16_t)(bx - 14), (int16_t)(bx + 14), (int16_t)(bx + 9), (int16_t)(bx - 9)},
                    hy[4] = {(int16_t)(by - 5), (int16_t)(by - 5), (int16_t)(by + 3), (int16_t)(by + 3)};
            mq_poly(q, hx, hy, 4, i ? Q_HOTPINK : Q_ORANGE);
            mq_fill(q, bx, by - 22, 1, 17, Q_INK);
            int16_t sx[3] = {(int16_t)(bx + 1), (int16_t)(bx + 1), (int16_t)(bx + 12)}, sy[3] = {(int16_t)(by - 22), (int16_t)(by - 6), (int16_t)(by - 6)};
            mq_poly(q, sx, sy, 3, Q_WHITE);
        }
        int gx = (int)(t / 16 % 420) - 50, gy = y0 + 20 + tri_wave(t / 5, 120, 6); /* a gull */
        mq_line(q, gx - 5, gy - 2, gx, gy, Q_INK), mq_line(q, gx, gy, gx + 5, gy - 2, Q_INK);
        break;
    }
    }
    if (hr)
        mq_hr_end(q);
}
