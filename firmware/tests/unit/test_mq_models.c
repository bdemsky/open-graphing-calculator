/* Sparkle Town (modules/mathquest): every picture fits its numbers (the audit's P1, P2, T1):
 *   - mq_vis_check, the validator of each kind of picture, passes for every skill, rung and seed;
 *   - base-ten blocks are drawn whole, every block of the number (or the place chart when they would not
 *     fit: never a partial model), and a comparison draws both numbers;
 *   - a square is drawn square, a rectangle not, in the card of the picking games and in Sort Slide's;
 *   - an order whose question needs its picture (a graph, a plot, an L-shape, a clock to read ...) only
 *     goes to games that draw it: never the Heist, the kitchen or the pet shop.
 * Built and run by `make test`. */
#include "mq_test_util.h"

#define FY (MQ_Y0 + 58)
#define FB (MQ_Y0 + MQ_H)

static void test_validator(void)
{
    con("new Vi 2");
    mq_prob_t p;
    char why[64];
    int bad = 0, n = 0;
    for (int sk = 0; sk < MQ_NSKILL; sk++)
        for (int r = 0; r < mq_rungs(sk); r++)
            for (int i = 0; i < 120; i++) {
                q->rng = (uint32_t)(sk * 131 + r * 17 + i * 7919) | 1;
                mq_generate(q, &p, sk, r);
                n++;
                if (mq_vis_check(q, &p, why, sizeof why)) {
                    if (bad++ < 10)
                        fprintf(stderr, "skill %d (%s) rung %d: %s: %s | %s\n", sk, mq_skill[sk].name, r, why, p.text[0],
                                p.text[1]);
                }
            }
    CHECK(bad == 0 && n > 30000);
    /* the validator does catch a picture that does not fit */
    q->rng = 5;
    mq_generate(q, &p, S_CMP3, 0);
    p.v[2] = 0;
    CHECK(mq_vis_check(q, &p, why, sizeof why) != 0);
    mq_generate(q, &p, S_COINS, 2);
    p.v[3]++;
    CHECK(mq_vis_check(q, &p, why, sizeof why) != 0);
    mq_generate(q, &p, S_SYMMETRY, 3);
    p.v[0] = p.v[0] == 0 ? 1 : 0; /* the text says one shape, the picture another */
    CHECK(strstr(p.text[1], "square") || strstr(p.text[1], "rectangle") || strstr(p.text[1], "trapezoid") ||
          strstr(p.text[1], "rhombus") || strstr(p.text[1], "parallelo") ? mq_vis_check(q, &p, why, sizeof why) != 0 : 1);
}

static int digsum(int32_t n)
{
    int s = 0;
    for (; n; n /= 10)
        s += (int)(n % 10);
    return s;
}

static void test_blocks(void)
{
    mq_prob_t p;
    int whole = 0, chart = 0;
    static const int16_t box[3][2] = {{110, 138}, {132, 68}, {180, 128}}; /* the card, Sort Slide's card, the why */
    for (int i = 0; i < 400; i++) {
        q->rng = (uint32_t)i * 2654435761u | 1;
        mq_generate(q, &p, i % 2 ? S_PV3 : S_CMP3, i % 5);
        for (int b = 0; b < 3; b++) {
            memset(fb, 0, sizeof fb);
            q->vis_q = false;
            mq_visual(q, &p, 4, FY + 4, box[b][0], box[b][1], 1000);
            int want = digsum(p.big) + (p.skill == S_CMP3 ? digsum(p.v[2]) : 0);
            if (q->vis_units) {
                CHECK(q->vis_units == want); /* every block, never a partial model */
                whole++;
            } else
                chart++;
        }
    }
    CHECK(whole > 600 && whole + chart == 1200);
    /* 685 in the picking games' card: six flats, eight rods, five ones (the audit's three flats) */
    memset(&p, 0, sizeof p);
    p.skill = S_PV3, p.vis = V_BASE10, p.big = 685, p.kind = K_NUM, p.ans = 685;
    mq_visual(q, &p, 4, FY + 4, 110, FB - FY - 10, 1000);
    CHECK(q->vis_units == 19 || q->vis_units == 0);
    CHECK(mq_base10_unit(q, 685, 110, FB - FY - 24) > 0);
}

/* the box of the pixels of colour c */
static void bbox(uint16_t c, int *w, int *h)
{
    int x0 = 999, y0 = 999, x1 = -1, y1 = -1;
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 320; x++)
            if (fb[y * 320 + x] == c) {
                x0 = x < x0 ? x : x0, x1 = x > x1 ? x : x1;
                y0 = y < y0 ? y : y0, y1 = y > y1 ? y : y1;
            }
    *w = x1 - x0 + 1, *h = y1 - y0 + 1;
}

static void test_shapes(void)
{
    mq_prob_t p;
    memset(&p, 0, sizeof p);
    p.skill = S_SYMMETRY, p.vis = V_SHAPE, p.kind = K_NUM;
    static const int16_t box[2][2] = {{110, 138}, {132, 68}};
    for (int b = 0; b < 2; b++) {
        int w, h;
        gfx_fill_rect(0, 0, 320, 240, 0);
        p.v[0] = 0; /* a square */
        mq_visual(q, &p, 4, FY + 4, box[b][0], box[b][1], 1000);
        bbox(Q_LAV, &w, &h);
        CHECK(w > 20 && h > 20 && abs(w - h) <= 2);
        gfx_fill_rect(0, 0, 320, 240, 0);
        p.v[0] = 1; /* a rectangle */
        mq_visual(q, &p, 4, FY + 4, box[b][0], box[b][1], 1000);
        bbox(Q_LAV, &w, &h);
        CHECK(abs(w - h) > 6);
    }
}

static void test_routing(void)
{
    con("new Ro 2");
    mq_prob_t p;
    int needed = 0, wrong = 0;
    for (int sk = 0; sk < MQ_NSKILL; sk++)
        for (int r = 0; r < mq_rungs(sk); r++)
            for (int i = 0; i < 20; i++) {
                q->rng = (uint32_t)(sk * 7 + r * 101 + i * 13) | 1;
                mq_generate(q, &p, sk, r);
                if (!mq_vis_needed(&p))
                    continue;
                needed++;
                wrong += mq_mech_fits(ME_VAULT, &p) || mq_mech_fits(ME_COOK, &p) || mq_mech_fits(ME_PETS, &p);
                for (int t = 0; t < 4; t++) {
                    int m = mq_mech_for(&p, ME_VAULT, t); /* a Heist round: the order goes to a game that draws it */
                    wrong += m == ME_VAULT || !mq_mech_fits(m, &p);
                }
            }
    CHECK(needed > 300 && wrong == 0);
}

int main(void)
{
    harness_init();
    test_validator();
    test_blocks();
    test_shapes();
    test_routing();
    printf("test_mq_models: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
