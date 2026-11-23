/*
 * Math Quest: the story stage -- an order told as a little scene, not as a word problem.
 *
 * The friend says a few words ("Bracelets for friends!", p->cap) and the board shows the story's
 * things with their numbers as in-world labels: three bracelets with a "4" tag on each and a bead
 * jar marked "?"; a pile of 24 cookies and four plates marked "?"; a clock at 8:15, a timer of
 * 55 minutes and a clock marked "?". The full story is a tap away (and comes back on a second
 * miss). Nothing is countable for the answer: groups are drawn only when their number is given
 * (more than five become one with an "x7" badge), the things being asked about are a labelled
 * pile, and the unknown is a "?" tag. When the order is solved the scene fills in: the beads on
 * the bracelets, the answer on the tag (the concrete picture after the answer).
 *
 * While an order is asked the stage draws no loose things at all: a heap carries one picture as
 * its sign, groups are given numbers; the filled-in scene comes only after the answer.
 *
 * Everything is drawn from shapes in logical pixels (resolution independent).
 */
#include "mq.h"

static int imin(int a, int b) { return a < b ? a : b; }

/* an ellipse (filled), rx x ry */
void mq_oval(mq_t *q, int cx, int cy, int rx, int ry, uint16_t c)
{
    if (rx <= 0 || ry <= 0)
        return;
    int16_t pv[48];
    for (int i = 0; i < 24; i++) /* (a high-resolution platform draws it smooth: mq_hr.c) */
        pv[i] = (int16_t)(cx + mq_icos(i * 15) * rx / 1024), pv[24 + i] = (int16_t)(cy + mq_isin(i * 15) * ry / 1024);
    int hr = mq_hr_shape(q, MODX_SH_POLY, c, pv, 24);
    for (int dy = -ry; dy <= ry; dy++) {
        /* dx = rx * sqrt(1 - dy^2 / ry^2) */
        int v = (ry * ry - dy * dy) * 256 / (ry * ry), r = 0, b = 1 << 14; /* sqrt of v (0..256) x 16 */
        v *= 256;
        while (b > v)
            b >>= 2;
        while (b) {
            if (v >= r + b)
                v -= r + b, r = (r >> 1) + b;
            else
                r >>= 1;
            b >>= 2;
        }
        int dx = rx * r / 256;
        mq_fill(q, cx - dx, cy + dy, 2 * dx + 1, 1, c);
    }
    if (hr)
        mq_hr_end(q);
}

/* ------------------------------------------------------------------ things */
static const uint16_t bead_col[5] = {Q_HOTPINK, Q_BLUE, Q_GREEN, Q_PURPLE, Q_ORANGE};

/* an item (what is counted in the story), about 12 px, centred at cx, cy */
void mq_stage_item(mq_t *q, int kind, int cx, int cy, int k)
{
    /* (a high-resolution platform may draw it from its own art: MODX_HR_SPRITE) */
    static const char sid[SI_COUNT][14] = {"stage/bead", "stage/cookie", "stage/star", "stage/flower", "stage/friend",
                                           "stage/seed", "stage/candle", "stage/balloon", "stage/shell", "stage/letter",
                                           "stage/marble", "stage/apple", "stage/leaf", "stage/card", "stage/light",
                                           "stage/bug", "stage/slice", "stage/egg", "stage/drop", "stage/coin",
                                           "stage/chair", "stage/carrot"};
    int32_t sv[1] = {k};
    int hr = kind >= 0 && kind < SI_COUNT ? mq_hr_sprite(q, sid[kind], cx - 8, cy - 9, 16, 17, sv, 1) : 0;
    switch (kind) {
    case SI_BEAD: mq_circle(q, cx, cy, 4, bead_col[k % 5]), mq_fill(q, cx - 1, cy - 1, 2, 2, Q_WHITE); break;
    case SI_COOKIE:
        mq_circle(q, cx, cy, 5, RGB(214, 160, 100));
        mq_fill(q, cx - 2, cy - 2, 2, 2, Q_INK), mq_fill(q, cx + 2, cy, 2, 2, Q_INK), mq_fill(q, cx - 1, cy + 2, 2, 2, Q_INK);
        break;
    case SI_STAR: mq_star(q, cx, cy, 6, Q_YELLOW); break;
    case SI_FLOWER:
        for (int i = 0; i < 5; i++)
            mq_circle(q, cx + mq_icos(i * 72) * 3 / 1024, cy + mq_isin(i * 72) * 3 / 1024, 2, k % 2 ? Q_PINK : Q_LAV);
        mq_circle(q, cx, cy, 1, Q_YELLOW);
        break;
    case SI_FRIEND:
        mq_circle(q, cx - 4, cy - 4, 2, Q_PEACH), mq_circle(q, cx + 4, cy - 4, 2, Q_PEACH);
        mq_circle(q, cx, cy, 5, Q_PEACH);
        mq_fill(q, cx - 2, cy - 1, 1, 2, Q_INK), mq_fill(q, cx + 2, cy - 1, 1, 2, Q_INK);
        mq_fill(q, cx - 1, cy + 2, 3, 1, Q_HOTPINK);
        break;
    case SI_SEED: mq_oval(q, cx, cy, 2, 4, Q_BROWN); break;
    case SI_CANDLE:
        mq_fill(q, cx - 2, cy - 2, 4, 8, Q_SKY);
        mq_circle(q, cx, cy - 5, 2, Q_ORANGE);
        break;
    case SI_BALLOON:
        mq_oval(q, cx, cy - 2, 4, 5, bead_col[k % 5]);
        mq_fill(q, cx, cy + 3, 1, 4, Q_DIM);
        break;
    case SI_SHELL:
        mq_oval(q, cx, cy, 6, 4, Q_PEACH);
        for (int i = -1; i <= 1; i++)
            mq_line(q, cx, cy + 4, cx + i * 4, cy - 3, Q_ORANGE);
        break;
    case SI_LETTER:
        mq_fill(q, cx - 6, cy - 4, 12, 8, Q_WHITE), mq_rect(q, cx - 6, cy - 4, 12, 8, Q_DIM);
        mq_line(q, cx - 6, cy - 4, cx, cy, Q_DIM), mq_line(q, cx + 5, cy - 4, cx, cy, Q_DIM);
        break;
    case SI_MARBLE: mq_circle(q, cx, cy, 4, k % 2 ? Q_SKY : Q_LAV), mq_fill(q, cx - 2, cy - 2, 2, 2, Q_WHITE); break;
    case SI_APPLE:
        mq_circle(q, cx, cy, 5, RGB(240, 90, 100));
        mq_fill(q, cx, cy - 7, 1, 3, Q_BROWN), mq_fill(q, cx + 1, cy - 7, 3, 2, Q_GREEN);
        break;
    case SI_LEAF: mq_oval(q, cx, cy, 6, 3, Q_GREEN), mq_fill(q, cx - 5, cy, 10, 1, RGB(60, 150, 100)); break;
    case SI_CARD: mq_rrect(q, cx - 4, cy - 6, 9, 12, 2, bead_col[k % 5]), mq_fill(q, cx - 2, cy - 2, 5, 2, Q_WHITE); break;
    case SI_LIGHT: mq_circle(q, cx, cy - 1, 4, Q_YELLOW), mq_fill(q, cx - 2, cy + 3, 4, 3, Q_DIM); break;
    case SI_BUG:
        mq_circle(q, cx, cy, 5, RGB(236, 70, 80));
        mq_circle(q, cx, cy - 5, 2, Q_INK);
        mq_fill(q, cx, cy - 3, 1, 8, Q_INK), mq_fill(q, cx - 3, cy - 1, 2, 2, Q_INK), mq_fill(q, cx + 2, cy + 1, 2, 2, Q_INK);
        break;
    case SI_SLICE: {
        int16_t px[3] = {(int16_t)(cx - 6), (int16_t)(cx + 6), (int16_t)cx}, py[3] = {(int16_t)(cy - 5), (int16_t)(cy - 5), (int16_t)(cy + 6)};
        mq_poly(q, px, py, 3, Q_YELLOW);
        mq_fill(q, cx - 6, cy - 6, 13, 2, RGB(214, 150, 90));
        mq_fill(q, cx - 2, cy - 2, 2, 2, Q_HOTPINK);
        break;
    }
    case SI_EGG: mq_oval(q, cx, cy, 4, 5, Q_CREAM), mq_fill(q, cx - 2, cy - 2, 1, 2, Q_WHITE); break;
    case SI_DROP: mq_circle(q, cx, cy + 1, 4, Q_SKY), mq_fill(q, cx - 1, cy - 5, 3, 3, Q_SKY); break;
    case SI_COIN: mq_circle(q, cx, cy, 5, RGB(240, 196, 80)), mq_circle(q, cx, cy, 3, Q_YELLOW); break;
    case SI_CHAIR:
        mq_fill(q, cx - 4, cy - 6, 2, 12, Q_BROWN), mq_fill(q, cx - 4, cy, 9, 2, Q_BROWN);
        mq_fill(q, cx + 3, cy, 2, 6, Q_BROWN);
        break;
    case SI_CARROT: {
        int16_t px[3] = {(int16_t)(cx - 3), (int16_t)(cx + 3), (int16_t)cx}, py[3] = {(int16_t)(cy - 4), (int16_t)(cy - 4), (int16_t)(cy + 7)};
        mq_poly(q, px, py, 3, Q_ORANGE);
        mq_fill(q, cx - 2, cy - 7, 1, 3, Q_GREEN), mq_fill(q, cx + 1, cy - 7, 1, 3, Q_GREEN);
        break;
    }
    default: mq_star(q, cx, cy, 5, Q_YELLOW); break;
    }
    if (hr)
        mq_hr_end(q);
}

/* a little face on a prop (two eyes, a smile, blush): the stage's things are friends too */
static void cute(mq_t *q, int cx, int cy)
{
    mq_fill(q, cx - 3, cy, 1, 2, Q_INK), mq_fill(q, cx + 3, cy, 1, 2, Q_INK);
    mq_fill(q, cx - 1, cy + 2, 3, 1, Q_INK);
    mq_fill(q, cx - 5, cy + 2, 2, 1, Q_BLUSH), mq_fill(q, cx + 4, cy + 2, 2, 1, Q_BLUSH);
}

/* a container (what the things are in), about 22 x 18, centred at cx, its bottom at by */
void mq_stage_prop(mq_t *q, int kind, int cx, int by)
{
    int y = by - 18;
    static const char pid[SP_COUNT][14] = {"prop/bag", "prop/tray", "prop/box", "prop/jar", "prop/bus", "prop/tent",
                                           "prop/ring", "prop/row", "prop/basket", "prop/cake", "prop/pot", "prop/blanket",
                                           "prop/page", "prop/leaf", "prop/chair", "prop/boat", "prop/cup"};
    int hr = kind >= 0 && kind < SP_COUNT ? mq_hr_sprite(q, pid[kind], cx - 13, by - 20, 26, 21, 0, 0) : 0;
    switch (kind) {
    case SP_BAG:
        mq_ring(q, cx, y + 4, 4, Q_BROWN);
        mq_rrect(q, cx - 9, y + 5, 18, 13, 5, RGB(236, 196, 150));
        mq_fill(q, cx - 7, y + 7, 14, 1, RGB(250, 226, 196));
        cute(q, cx, y + 10);
        break;
    case SP_TRAY:
        mq_oval(q, cx, by - 3, 13, 5, RGB(190, 180, 205));
        mq_oval(q, cx, by - 4, 12, 4, Q_WHITE);
        mq_oval(q, cx, by - 4, 9, 2, Q_GREY);
        break;
    case SP_BOX:
        mq_fill(q, cx - 9, y + 6, 18, 12, Q_PINK);
        mq_fill(q, cx - 10, y + 3, 20, 4, Q_HOTPINK);
        mq_fill(q, cx - 1, y + 3, 3, 15, Q_YELLOW);
        cute(q, cx - 4, y + 10);
        break;
    case SP_JAR:
        mq_rrect(q, cx - 8, y + 4, 16, 14, 4, RGB(206, 236, 250));
        mq_fill(q, cx - 7, y + 1, 14, 4, Q_BROWN);
        mq_fill(q, cx - 5, y + 6, 2, 8, Q_WHITE);
        cute(q, cx + 1, y + 11);
        break;
    case SP_BUS:
        mq_rrect(q, cx - 12, y + 3, 24, 12, 3, Q_YELLOW);
        for (int i = 0; i < 3; i++)
            mq_fill(q, cx - 9 + i * 7, y + 5, 5, 4, Q_SKY);
        mq_circle(q, cx - 7, by - 2, 2, Q_INK), mq_circle(q, cx + 7, by - 2, 2, Q_INK);
        break;
    case SP_TENT: {
        int16_t px[3] = {(int16_t)(cx - 11), (int16_t)(cx + 11), (int16_t)cx}, py[3] = {(int16_t)by, (int16_t)by, (int16_t)(y + 1)};
        mq_poly(q, px, py, 3, Q_ORANGE);
        int16_t dx[3] = {(int16_t)(cx - 3), (int16_t)(cx + 3), (int16_t)cx}, dy[3] = {(int16_t)by, (int16_t)by, (int16_t)(y + 9)};
        mq_poly(q, dx, dy, 3, Q_INK);
        break;
    }
    case SP_RING:
        mq_circle(q, cx, y + 10, 8, RGB(236, 200, 120));
        mq_circle(q, cx, y + 10, 5, q->clip_y1 ? RGB(255, 255, 255) : Q_WHITE);
        break;
    case SP_ROW:
        mq_rrect(q, cx - 12, by - 6, 24, 6, 3, Q_BROWN);
        for (int i = 0; i < 3; i++)
            mq_fill(q, cx - 8 + i * 8, by - 9, 1, 3, Q_GREEN);
        break;
    case SP_BASKET:
        mq_ring(q, cx, y + 7, 7, Q_BROWN);
        mq_rrect(q, cx - 10, y + 8, 20, 10, 4, RGB(214, 170, 110));
        mq_fill(q, cx - 9, y + 11, 18, 1, Q_BROWN);
        cute(q, cx, y + 13);
        break;
    case SP_CAKE:
        mq_rrect(q, cx - 10, y + 7, 20, 11, 3, Q_PEACH);
        mq_rrect(q, cx - 10, y + 5, 20, 5, 2, Q_PINK);
        cute(q, cx, y + 11);
        break;
    case SP_POT: {
        int16_t px[4] = {(int16_t)(cx - 9), (int16_t)(cx + 9), (int16_t)(cx + 6), (int16_t)(cx - 6)},
                py[4] = {(int16_t)(y + 6), (int16_t)(y + 6), (int16_t)by, (int16_t)by};
        mq_poly(q, px, py, 4, RGB(222, 130, 90));
        mq_fill(q, cx - 10, y + 4, 20, 4, RGB(236, 150, 110));
        cute(q, cx, y + 10);
        break;
    }
    case SP_BLANKET:
        mq_rrect(q, cx - 12, by - 8, 24, 8, 2, Q_PINK);
        for (int i = 0; i < 4; i++)
            mq_fill(q, cx - 10 + i * 6, by - 8, 3, 8, Q_WHITE);
        break;
    case SP_PAGE:
        mq_fill(q, cx - 8, y + 1, 16, 17, Q_WHITE);
        mq_rect(q, cx - 8, y + 1, 16, 17, Q_LAV);
        break;
    case SP_LEAF: mq_oval(q, cx, by - 6, 11, 5, Q_GREEN), mq_fill(q, cx - 9, by - 6, 18, 1, RGB(60, 150, 100)); break;
    case SP_CHAIR:
        mq_fill(q, cx - 6, y + 1, 3, 17, Q_BROWN);
        mq_fill(q, cx - 6, y + 9, 13, 3, Q_BROWN);
        mq_fill(q, cx + 5, y + 9, 2, 9, Q_BROWN);
        break;
    case SP_BOAT: {
        int16_t px[4] = {(int16_t)(cx - 12), (int16_t)(cx + 12), (int16_t)(cx + 8), (int16_t)(cx - 8)},
                py[4] = {(int16_t)(by - 7), (int16_t)(by - 7), (int16_t)by, (int16_t)by};
        mq_poly(q, px, py, 4, Q_BLUE);
        int16_t sx[3] = {(int16_t)cx, (int16_t)cx, (int16_t)(cx + 9)}, sy[3] = {(int16_t)(y + 1), (int16_t)(by - 8), (int16_t)(by - 8)};
        mq_poly(q, sx, sy, 3, Q_WHITE);
        break;
    }
    case SP_CUP:
        mq_rrect(q, cx - 7, y + 5, 14, 13, 3, Q_WHITE);
        mq_ring(q, cx + 8, y + 10, 3, Q_WHITE);
        mq_fill(q, cx - 6, y + 6, 12, 3, Q_PEACH);
        cute(q, cx, y + 11);
        break;
    default: mq_rrect(q, cx - 9, y + 4, 18, 14, 4, Q_PEACH); break;
    }
    if (hr)
        mq_hr_end(q);
}

/* ------------------------------------------------------------------ labels */
/* a tag with s on it, centred at cx (its top at y); goal: the pink "?" of what is asked */
static int tag(mq_t *q, int cx, int y, const char *s, int goal)
{
    const font_t *f = q->api->font_small;
    int w = mq_text_w(q, f, s) + 6;
    if (w < 12)
        w = 12;
    uint16_t bg = goal == 1 ? Q_HOTPINK : goal == 2 ? Q_GREEN : Q_WHITE;
    mq_rrect(q, cx - w / 2, y + 1, w, 12, 4, Q_DIM);
    mq_rrect(q, cx - w / 2, y, w, 12, 4, bg);
    mq_text_c(q, f, cx, y - 1, s, goal ? Q_WHITE : Q_INK);
    return w;
}
static int tag_w(mq_t *q, const char *s)
{
    int w = mq_text_w(q, q->api->font_small, s) + 6;
    return w < 12 ? 12 : w;
}

/* ------------------------------------------------------------------ the scene of a story */
enum { TK_GROUP, TK_PILE, TK_OP, TK_ARROW, TK_GOAL, TK_FACE, TK_CLOCK, TK_TIMER, TK_PRICE, TK_JUG, TK_PATH, TK_FLAG,
       TK_RECT, TK_BADGE };
typedef struct {
    uint8_t t, a, b, n;  /* kind, prop / cast slot, item, copies */
    char lab[14];        /* its number, as a label */
    uint8_t goal;        /* the label is the unknown */
} mq_tok_t;
#define NTOK 9

static void put(mq_t *q, mq_tok_t *k, int *n, int t, int a, int b, int copies, const char *lab, int goal)
{
    if (*n >= NTOK)
        return;
    mq_tok_t *o = &k[(*n)++];
    o->t = (uint8_t)t, o->a = (uint8_t)a, o->b = (uint8_t)b, o->n = (uint8_t)copies, o->goal = (uint8_t)goal;
    q->api->snprintf(o->lab, sizeof o->lab, "%s", lab ? lab : "");
}

/* the scene's tokens; ans: the answer's label ("?" while it is asked) */
static int scene(mq_t *q, const mq_prob_t *p, mq_tok_t *k, const char *ans)
{
    const modx_api_t *api = q->api;
    int n = 0, c = p->stg_c, it = p->stg_i;
    int32_t a = p->stg_n[0], b = p->stg_n[1], d = p->stg_n[2];
    char s0[14], s1[14], s2[14];
    api->snprintf(s0, sizeof s0, "%ld", (long)a);
    api->snprintf(s1, sizeof s1, "%ld", (long)b);
    api->snprintf(s2, sizeof s2, "%ld", (long)d);
    bool solved = ans[0] != '?';
    switch (p->stg - 1) {
    case ST_GROUPS: /* a groups of b: all of them? */
        put(q, k, &n, TK_GROUP, c, it, (int)a, s1, 0);
        put(q, k, &n, TK_ARROW, 0, 0, 0, NULL, 0);
        put(q, k, &n, TK_GOAL, SP_JAR, it, 0, ans, 1);
        break;
    case ST_MUL: /* big groups: one with its count */
        put(q, k, &n, TK_GROUP, c, it, 1, s1, 0);
        api->snprintf(s0, sizeof s0, "x%ld", (long)a);
        put(q, k, &n, TK_BADGE, 0, 0, 0, s0, 0);
        put(q, k, &n, TK_ARROW, 0, 0, 0, NULL, 0);
        put(q, k, &n, TK_GOAL, SP_JAR, it, 0, ans, 1);
        break;
    case ST_SHARE: case ST_PERONE: /* n shared by b: on each? */
        put(q, k, &n, TK_PILE, 0, it, 0, s0, 0);
        put(q, k, &n, TK_ARROW, 0, 0, 0, NULL, 0);
        put(q, k, &n, TK_GROUP, c, it, (int)b, ans, 1);
        break;
    case ST_HOWMANY: case ST_FULL: case ST_BUS: /* n in groups of b: how many groups? */
        put(q, k, &n, TK_PILE, 0, it, 0, s0, 0);
        put(q, k, &n, TK_ARROW, 0, 0, 0, NULL, 0);
        put(q, k, &n, TK_GROUP, c, it, 1, s1, 0);
        api->snprintf(s2, sizeof s2, "x%s", ans);
        put(q, k, &n, TK_BADGE, 0, 0, 0, s2, 1);
        break;
    case ST_LEFT: /* n in groups of b: left over? */
        put(q, k, &n, TK_PILE, 0, it, 0, s0, 0);
        put(q, k, &n, TK_ARROW, 0, 0, 0, NULL, 0);
        put(q, k, &n, TK_GROUP, c, it, 1, s1, 0);
        put(q, k, &n, TK_OP, '+', 0, 0, NULL, 0);
        put(q, k, &n, TK_FACE, p->cb, it, 0, ans, 1);
        break;
    case ST_TIMES: /* B has a; A has b times as many */
        put(q, k, &n, TK_FACE, p->cb, it, 0, s0, 0);
        put(q, k, &n, TK_ARROW, 0, 0, 0, NULL, 0);
        put(q, k, &n, TK_FACE, p->ca, it, 0, ans, 1);
        api->snprintf(s1, sizeof s1, "x%ld", (long)b);
        put(q, k, &n, TK_BADGE, 0, 0, 0, s1, 0);
        break;
    case ST_TAKE: case ST_MARBLES: /* a groups of b, c go: left? */
        put(q, k, &n, TK_GROUP, c, it, (int)a, s1, 0);
        put(q, k, &n, TK_OP, '-', 0, 0, NULL, 0);
        put(q, k, &n, TK_PILE, 0, it, 0, s2, 0);
        put(q, k, &n, TK_ARROW, 0, 0, 0, NULL, 0);
        put(q, k, &n, TK_GOAL, SP_JAR, it, 0, ans, 1);
        break;
    case ST_SHAREADD: /* a friends share b, then d more each: each? */
        put(q, k, &n, TK_PILE, 0, it, 0, s1, 0);
        put(q, k, &n, TK_ARROW, 0, 0, 0, NULL, 0);
        put(q, k, &n, TK_GROUP, c, it, (int)a, ans, 1);
        api->snprintf(s0, sizeof s0, "+%ld", (long)d);
        put(q, k, &n, TK_BADGE, 0, it, 0, s0, 0);
        break;
    case ST_ADD: case ST_SUB:
        put(q, k, &n, TK_PILE, 0, it, 0, s0, 0);
        put(q, k, &n, TK_OP, p->stg == ST_ADD + 1 ? '+' : '-', 0, 0, NULL, 0);
        put(q, k, &n, TK_PILE, 0, it, 0, s1, 0);
        put(q, k, &n, TK_ARROW, 0, 0, 0, NULL, 0);
        put(q, k, &n, TK_GOAL, SP_JAR, it, 0, ans, 1);
        break;
    case ST_DIVR: /* n into b: each, and left over */
        put(q, k, &n, TK_PILE, 0, it, 0, s0, 0);
        put(q, k, &n, TK_ARROW, 0, 0, 0, NULL, 0);
        put(q, k, &n, TK_GROUP, c, it, (int)b, solved ? ans : "?", 1);
        break;
    case ST_READY: { /* starts at s, d minutes: done when? */
        api->snprintf(s1, sizeof s1, "%ld min", (long)a);
        put(q, k, &n, TK_CLOCK, 0, 0, 0, p->stg_s, 0);
        put(q, k, &n, TK_OP, '+', 0, 0, NULL, 0);
        put(q, k, &n, TK_TIMER, 0, 0, 0, s1, 0);
        put(q, k, &n, TK_ARROW, 0, 0, 0, NULL, 0);
        put(q, k, &n, TK_CLOCK, 0, 0, 0, ans, 1);
        break;
    }
    case ST_HOWLONG: { /* from s1 to s2: how long? */
        char t1[8], t2[8];
        int i = 0, j = 0;
        while (p->stg_s[i] && p->stg_s[i] != ' ' && i < 7)
            t1[i] = p->stg_s[i], i++;
        t1[i] = 0;
        while (p->stg_s[i] && (p->stg_s[i] < '0' || p->stg_s[i] > '9'))
            i++;
        while (p->stg_s[i] && j < 7)
            t2[j++] = p->stg_s[i++];
        t2[j] = 0;
        put(q, k, &n, TK_CLOCK, 0, 0, 0, t1, 0);
        put(q, k, &n, TK_ARROW, 0, 0, 0, NULL, 0);
        put(q, k, &n, TK_CLOCK, 0, 0, 0, t2, 0);
        put(q, k, &n, TK_OP, '=', 0, 0, NULL, 0);
        api->snprintf(s0, sizeof s0, solved ? "%s" : "? min", ans);
        put(q, k, &n, TK_TIMER, 0, 0, 0, s0, 1);
        break;
    }
    case ST_CHANGE: /* price, paid with $a: change? */
        put(q, k, &n, TK_PRICE, 0, 0, 0, p->stg_s, 0);
        api->snprintf(s0, sizeof s0, "$%ld", (long)a);
        put(q, k, &n, TK_OP, '<', 0, 0, NULL, 0);
        put(q, k, &n, TK_PRICE, 1, 0, 0, s0, 0);
        put(q, k, &n, TK_ARROW, 0, 0, 0, NULL, 0);
        put(q, k, &n, TK_GOAL, SP_JAR, SI_COIN, 0, ans, 1);
        break;
    case ST_JUG: /* a liters, b poured: left? */
        api->snprintf(s0, sizeof s0, "%ld L", (long)a);
        api->snprintf(s1, sizeof s1, "%ld L", (long)b);
        put(q, k, &n, TK_JUG, 0, 0, 0, s0, 0);
        put(q, k, &n, TK_OP, '-', 0, 0, NULL, 0);
        put(q, k, &n, TK_JUG, 1, 0, 0, s1, 0);
        put(q, k, &n, TK_ARROW, 0, 0, 0, NULL, 0);
        put(q, k, &n, TK_JUG, 0, 0, 0, ans, 1);
        break;
    case ST_FAR: /* a m, then b m: how far? */
        api->snprintf(s0, sizeof s0, "%ld m", (long)a);
        api->snprintf(s1, sizeof s1, "%ld m", (long)b);
        put(q, k, &n, TK_PATH, 0, 0, 0, s0, 0);
        put(q, k, &n, TK_OP, '+', 0, 0, NULL, 0);
        put(q, k, &n, TK_PATH, 0, 0, 0, s1, 0);
        put(q, k, &n, TK_ARROW, 0, 0, 0, NULL, 0);
        put(q, k, &n, TK_FLAG, 0, 0, 0, ans, 1);
        break;
    case ST_FENCE: case ST_RUG: /* a by b: around (fence) or inside (rug) */
        put(q, k, &n, TK_RECT, p->stg == ST_FENCE + 1, 0, 0, ans, 1);
        break;
    case ST_RECIPE: /* a friends, b/d each */
        api->snprintf(s1, sizeof s1, "%ld/%ld", (long)b, (long)d);
        put(q, k, &n, TK_GROUP, c, it, (int)a, s1, 0);
        put(q, k, &n, TK_ARROW, 0, 0, 0, NULL, 0);
        put(q, k, &n, TK_GOAL, SP_TRAY, it, 0, ans, 1);
        break;
    default: break;
    }
    return n;
}


/* Layout: a labelled thing is its picture (IW wide) with its label hanging off its right side,
 * like a price tag; groups repeat that. */
#define IW 22
static int group_n(const mq_tok_t *t) { return t->n >= 1 && t->n <= 5 ? t->n : 1; }
static int lab_w(mq_t *q, const char *s) { return IW + tag_w(q, s) - 7; }

static int tok_w(mq_t *q, const mq_tok_t *t)
{
    switch (t->t) {
    case TK_GROUP: return group_n(t) * (lab_w(q, t->lab) + 2) - 2 + (t->n > 5 ? tag_w(q, "x99") + 2 : 0);
    case TK_PILE: case TK_GOAL: case TK_FACE: case TK_CLOCK: case TK_TIMER: case TK_PRICE: case TK_JUG: case TK_PATH:
    case TK_FLAG:
        return lab_w(q, t->lab);
    case TK_OP: return 9;
    case TK_ARROW: return 13;
    case TK_RECT: return 150;
    case TK_BADGE: return tag_w(q, t->lab);
    default: return 0;
    }
}

/* Draw the stage of p in the row x .. x + w, its things standing on y + 20 (about 22 tall);
 * solved: the scene filled in. Returns false when the order has no stage (its text is shown). */
bool mq_stage_draw(mq_t *q, const mq_prob_t *p, int x, int y, int w, bool solved)
{
    if (!p->stg)
        return false;
    mq_tok_t tk[NTOK];
    char ans[16];
    if (solved)
        mq_format(q, p, p->ans, ans, sizeof ans);
    else
        q->api->snprintf(ans, sizeof ans, "?");
    int n = scene(q, p, tk, ans), tw = 0;
    if (!n)
        return false;
    for (int i = 0; i < n; i++)
        tw += tok_w(q, &tk[i]) + 5;
    int cx = x + 4, by = y + 20; /* the things stand on by; their labels hang off their right */
    if (tw < w - 8)
        cx = x + (w - tw) / 2 > x + 24 ? x + 24 : x + (w - tw) / 2;
    int bounce = (int)(q->anim_t / 140 % 4), up = bounce == 1 ? -1 : bounce == 3 ? 1 : 0;
    const modx_api_t *api = q->api;
    int by0 = by;
    /* the operation acted (the scene being acted, once everything is on): the groups counted off one
     * by one, a pile dealt out to its plates, two piles pushed together, a part taken away -- never the
     * answer (nothing countable is added) */
    int op = p->stg - 1, ph = q->stg_rv > 1300 ? (q->stg_rv - 1300) % 1800 : -1;
    int mids[NTOK];
    for (int i = 0; i < n; i++) {
        mq_tok_t *t = &tk[i];
        int tw1 = tok_w(q, t), mid = cx + IW / 2;
        mids[i] = mid;
        if (ph >= 0 && i == 2 && (op == ST_ADD || op == ST_FAR)) /* together: the second pile slides over */
            cx -= imin(ph, 700) * (cx - mids[0] - 14) / 700 * (ph < 1500 ? 1 : 0);
        if (ph >= 0 && i == 2 && (op == ST_SUB || op == ST_JUG || op == ST_TAKE || op == ST_MARBLES) && ph < 1400)
            by = by0 - imin(ph, 700) / 70; /* taken away: the part lifts off */
        if (q->stg_rv) { /* the scene being acted: the things pop in one by one, with a little drop */
            int at = 300 + i * 150, el = q->stg_rv - at;
            if (el < 0) {
                cx += tw1 + 5;
                continue;
            }
            by = by0 - (el < 160 ? (160 - el) / 20 : 0);
        }
        int gl = t->goal && !solved ? 1 : t->goal ? 2 : 0;
        int ty = by - 12 + (gl == 1 ? up : 0), tx = cx + IW - 7; /* the label's left edge */
#define LABEL(x0) tag(q, (x0) + IW - 7 + tag_w(q, t->lab) / 2, ty, t->lab, gl)
        switch (t->t) {
        case TK_GROUP: {
            int m = group_n(t), step = lab_w(q, t->lab) + 2;
            for (int j = 0; j < m; j++) {
                int gx = cx + j * step;
                int hop = ph >= 0 && !t->goal && ph / 300 == j ? 3 : 0; /* counted off, one group a beat */
                mq_stage_prop(q, t->a, gx + IW / 2, by - hop);
                if (solved && t->goal == 0 && p->stg - 1 != ST_SHAREADD && p->stg - 1 != ST_RECIPE) {
                    int per = imin(6, (int)p->stg_n[1]); /* filled in: the things in each */
                    for (int s = 0; s < per; s++)
                        mq_circle(q, gx + 3 + (s % 3) * 7, by - 16 + (s / 3) * 5, 2, bead_col[(s + j) % 5]);
                }
                LABEL(gx);
            }
            if (t->n > 5) {
                char b[8];
                api->snprintf(b, sizeof b, "x%d", t->n);
                tag(q, cx + m * step + tag_w(q, b) / 2, by - 20, b, 0);
            }
            break;
        }
        case TK_PILE:
            /* a heap with the thing on it as its sign (one picture: nothing to count) */
            mq_oval(q, mid, by - 5, 11, 5, RGB(240, 214, 190));
            mq_oval(q, mid, by - 7, 7, 3, RGB(255, 240, 226));
            mq_stage_item(q, t->b, mid, by - 13, 0);
            LABEL(cx);
            break;
        case TK_GOAL:
            mq_stage_prop(q, t->a, mid, by);
            mq_stage_item(q, t->b, mid, by - 11, 0);
            LABEL(cx);
            break;
        case TK_OP: {
            uint16_t c = Q_PURPLE;
            int m = cx + 4;
            if (t->a == '+')
                mq_fill(q, m - 3, by - 11, 7, 2, c), mq_fill(q, m - 1, by - 14, 2, 8, c);
            else if (t->a == '-')
                mq_fill(q, m - 3, by - 11, 7, 2, c);
            else if (t->a == '=')
                mq_fill(q, m - 3, by - 13, 7, 2, c), mq_fill(q, m - 3, by - 9, 7, 2, c);
            else /* '<': paid with */
                mq_star(q, m, by - 11, 4, Q_YELLOW);
            break;
        }
        case TK_ARROW: {
            int m = cx + 6;
            int16_t px[3] = {(int16_t)(m + 1), (int16_t)(m + 6), (int16_t)(m + 1)},
                    py[3] = {(int16_t)(by - 16), (int16_t)(by - 11), (int16_t)(by - 6)};
            mq_fill(q, m - 6, by - 12, 8, 3, Q_LAV);
            mq_poly(q, px, py, 3, Q_LAV);
            break;
        }
        case TK_FACE:
            mq_sprite(q, t->a % MQ_NCAST, cx, by - 21, 1, 0);
            LABEL(cx);
            break;
        case TK_CLOCK:
            mq_circle(q, mid, by - 10, 9, Q_PURPLE);
            mq_circle(q, mid, by - 10, 7, Q_WHITE);
            mq_fill(q, mid, by - 16, 1, 6, Q_INK), mq_fill(q, mid, by - 10, 4, 1, Q_INK);
            LABEL(cx);
            break;
        case TK_TIMER:
            mq_circle(q, mid, by - 9, 8, Q_ORANGE);
            mq_circle(q, mid, by - 9, 6, Q_WHITE);
            mq_fill(q, mid - 2, by - 19, 5, 2, Q_ORANGE);
            mq_fill(q, mid, by - 14, 1, 5, Q_INK);
            LABEL(cx);
            break;
        case TK_PRICE:
            if (t->a) /* a banknote */
                mq_rrect(q, mid - 10, by - 17, 20, 12, 2, Q_MINT), mq_circle(q, mid, by - 11, 3, Q_GREEN);
            else
                mq_stage_item(q, SI_STAR, mid, by - 12, 0), mq_ring(q, mid - 6, by - 17, 2, Q_DIM);
            LABEL(cx);
            break;
        case TK_JUG:
            if (t->a) /* a glass */
                mq_fill(q, mid - 5, by - 17, 10, 14, RGB(206, 236, 250)), mq_fill(q, mid - 4, by - 10, 8, 6, Q_YELLOW);
            else {
                mq_rrect(q, mid - 8, by - 19, 16, 17, 4, RGB(206, 236, 250));
                mq_ring(q, mid - 9, by - 11, 3, RGB(170, 210, 230));
                mq_fill(q, mid - 7, by - 11, 14, 8, Q_YELLOW);
            }
            LABEL(cx);
            break;
        case TK_PATH:
            for (int s = 0; s < 3; s++)
                mq_oval(q, cx + 4 + s * 7, by - 6 - (s % 2) * 4, 3, 2, Q_BROWN);
            LABEL(cx);
            break;
        case TK_FLAG:
            mq_fill(q, mid - 4, by - 21, 2, 19, Q_INK);
            mq_fill(q, mid - 2, by - 21, 10, 8, Q_HOTPINK);
            LABEL(cx);
            break;
        case TK_RECT: { /* the garden / rug with its sides labelled, and what is asked */
            int rw = 64, rh = 16, rx = cx + 4, ry = by - 18;
            char s0[12], s1[12];
            api->snprintf(s0, sizeof s0, "%ld", (long)p->stg_n[0]);
            api->snprintf(s1, sizeof s1, "%ld", (long)p->stg_n[1]);
            if (t->a) { /* a fence round a garden */
                mq_fill(q, rx, ry, rw, rh, Q_MINT);
                mq_rect(q, rx, ry, rw, rh, Q_BROWN), mq_rect(q, rx + 1, ry + 1, rw - 2, rh - 2, Q_BROWN);
                for (int s = 0; s <= 6; s++)
                    mq_fill(q, rx + s * (rw - 2) / 6, ry - 2, 3, 3, Q_BROWN), mq_fill(q, rx + s * (rw - 2) / 6, ry + rh - 1, 3, 3, Q_BROWN);
            } else {
                mq_fill(q, rx, ry, rw, rh, Q_LAV);
                for (int s = 0; s < 4; s++)
                    mq_fill(q, rx + 4 + s * 15, ry + 3, 8, rh - 6, Q_PINK);
            }
            tag(q, rx + rw / 2, ry + rh - 6, s0, 0);
            tag(q, rx + rw + tag_w(q, s1) / 2 + 1, ry + 1, s1, 0);
            int gx = rx + rw + tag_w(q, s1) + 10;
            if (t->a) /* the fence all around, or the squares inside */
                mq_ring(q, gx + 8, by - 10, 6, Q_BROWN), mq_ring(q, gx + 8, by - 10, 5, Q_BROWN);
            else
                mq_fill(q, gx + 2, by - 16, 12, 12, Q_PINK), mq_rect(q, gx + 2, by - 16, 12, 12, Q_HOTPINK);
            tag(q, gx + 16 + tag_w(q, t->lab) / 2, ty, t->lab, gl);
            break;
        }
        case TK_BADGE:
            tag(q, cx + tw1 / 2, by - 20 + (t->goal && !solved ? up : 0), t->lab, t->goal ? (solved ? 2 : 1) : 0);
            break;
        default: break;
        }
#undef LABEL
        (void)tx;
        cx += tw1 + 5;
        by = by0;
    }
    /* sharing: one of the pile's things hops to each plate in turn (a deal, not a count) */
    if (ph >= 0 && n >= 3 && tk[0].t == TK_PILE && tk[2].t == TK_GROUP &&
        (op == ST_SHARE || op == ST_PERONE || op == ST_DIVR || op == ST_SHAREADD || op == ST_HOWMANY || op == ST_FULL ||
         op == ST_BUS || op == ST_LEFT)) {
        int m = group_n(&tk[2]), step = lab_w(q, tk[2].lab) + 2, j = (ph / 450) % m, f = ph % 450;
        int x0 = mids[0], x1 = mids[2] + j * step, hx = x0 + (x1 - x0) * f / 450;
        int hy = by0 - 14 - (f * (450 - f)) / 8000;
        mq_stage_item(q, tk[0].b, hx, hy, 0);
    }
    return true;
}

/* the width of p's stage row (logical, unzoomed); 0: no stage */
int mq_stage_width(mq_t *q, const mq_prob_t *p)
{
    if (!p->stg)
        return 0;
    mq_tok_t tk[NTOK];
    int n = scene(q, p, tk, "?"), tw = 0;
    for (int i = 0; i < n; i++)
        tw += tok_w(q, &tk[i]) + 5;
    return tw;
}

/* The story acted out in the field when an order arrives (el: ms into it): the friend walks in
 * and tells it in a speech bubble, the story's things pop onto a little stage at twice the size,
 * with their numbers as tags and the "?" bouncing; a friend it is for pouts on the other side.
 * Then it shrinks to the order board's row and the game begins (mq_play.c). */
void mq_stage_scene(mq_t *q, const mq_prob_t *p, int el, int y0, int y1, bool solved)
{
    const modx_api_t *api = q->api;
    bool sad = mq_story_sad(p) && p->cb != p->ca;
    /* the stage: a little wooden stage with a rug on it */
    mq_oval(q, 168, y1 - 10, 152, 12, RGB(214, 170, 130));
    mq_oval(q, 168, y1 - 13, 148, 10, RGB(236, 204, 166));
    mq_oval(q, 172, y1 - 14, 112, 6, RGB(232, 214, 250));
    /* the friend telling it, holding up one of the story's things */
    int fx = el < 420 ? -60 + el * 68 / 420 : 8, size = 58;
    int anim = el < 420 ? AN_DANCE : el < 1500 ? AN_TALK : AN_HAPPY;
    mq_cast(q, p->ca % MQ_NCAST, fx, y1 - 14 - size, size, anim);
    if (el >= 420 && el < 1500 && p->stg_i < SI_COUNT) {
        int hx = fx + size - 4, hy = y1 - 14 - size / 2 - (int)(q->anim_t / 160 % 2) * 2;
        mq_zoom(q, 24, hx, hy);
        mq_stage_item(q, p->stg_i, hx, hy, 0);
        mq_zoom(q, 16, 0, 0);
    }
    if (sad) /* the friend it is for, pouting */
        mq_cast(q, p->cb % MQ_NCAST, 272, y1 - 12 - 42, 42, AN_POUT);
    /* what the friend says */
    const char *say = p->cap[0] ? p->cap : p->text[0];
    if (el > 250 && say[0]) {
        const font_t *f = api->text_width(api->font_medium, say) <= 228 ? api->font_medium : api->font_small;
        int w = api->text_width(f, say) + 16, bx = 70, by = y0 + 6;
        if (w > 244)
            w = 244;
        mq_rrect(q, bx, by + 2, w, 22, 10, RGB(220, 210, 228));
        mq_rrect(q, bx, by, w, 22, 10, Q_WHITE);
        int16_t tx[3] = {(int16_t)(bx + 2), (int16_t)(bx + 14), (int16_t)(bx - 8)}, ty[3] = {(int16_t)(by + 12), (int16_t)(by + 20),
                                                                                         (int16_t)(by + 26)};
        mq_poly(q, tx, ty, 3, Q_WHITE);
        mq_text(q, f, bx + 8, by + (f == api->font_small ? 5 : 3), say, Q_INK);
    }
    /* the stage's things, twice the size (or as big as fits) */
    int tw = mq_stage_width(q, p);
    if (!tw)
        return;
    int left = 72, avail = (sad ? 266 : 312) - left, z = tw ? avail * 16 / tw : 32;
    z = z > 40 ? 40 : z < 16 ? 16 : z;
    int sx = left + (avail - tw * z / 16) / 2, sy = y1 - 16 - 22 * z / 16;
    q->stg_rv = (uint16_t)(el < 1 ? 1 : el > 60000 ? 60000 : el);
    mq_zoom(q, z, sx, sy);
    mq_stage_draw(q, p, sx, sy, tw + 8, solved);
    mq_zoom(q, 16, 0, 0);
    q->stg_rv = 0;
    /* the operation's little tag ("3 groups, 4 each", "put together"): what the story does, in words */
    char cue[40];
    if (el > 700 && mq_op_cue(q, p, cue, sizeof cue)) {
        int w = api->text_width(api->font_small, cue) + 14, cx = 72 + w / 2;
        mq_rrect(q, cx - w / 2, y0 + 32, w, 15, 7, Q_LAV);
        mq_text_c(q, api->font_small, cx, y0 + 33, cue, Q_INK);
    }
}
