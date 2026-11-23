/* Sparkle Town (modules/mathquest): every choice a child is shown can be answered (the pink-cake report:
 * "How much of the cake is pink?" with no pink on the cake and no 0/4 to pick):
 *   - in EVERY game that tells an order with options (bubbles, clouds, gates, karts, holes, bins, pads,
 *     cards ...), for every skill, rung and several seeds: the right answer is one of the options, exactly
 *     one option is right (an equivalent fraction counts as right), and no two options read the same;
 *   - a fraction picture (pies, bars, the hundredths grid) shows its coloured parts from the first frame of
 *     the question: the coloured parts drawn are the numerator, the parts drawn the denominator (times the
 *     pies), in every game that draws the question's picture.
 * Built and run by `make test`. */
#include "mq_test_util.h"

static bool order_in(int skill, int d, int mech)
{
    char b[32];
    snprintf(b, sizeof b, "skill %d %d", skill, d);
    con(b);
    q->fun_ms = 0, q->rapid = 0, q->guess_n = 0;
    for (int w = 0; w < WS_COUNT; w++)
        mq_pace(q)->ws_level[w] = 2;
    memset(mq_pace(q)->recent, 0, sizeof mq_pace(q)->recent), mq_pace(q)->recent_miss = 0;
    for (int i = 0; i < 200; i++) {
        mq_generate_fresh(q, &q->prob, skill, d);
        if (mq_mech_fits(mech, &q->prob)) {
            q->gm.round_mech = (uint8_t)mech;
            q->tried_wrong = false, q->used_help = false;
            mq_game_order(q);
            return q->gm.mech == mech && q->sub == GS_PLAY;
        }
    }
    return false;
}

static bool is_right(int32_t v)
{
    return mq_correct(&q->prob, v) || (q->prob.kind == K_CHOICE && v == q->prob.ans);
}

static void test_options(void)
{
    con("new Op 2");
    con("engine stop");
    int orders = 0, bad = 0, mechs_seen = 0;
    for (int m = 0; m < ME_COUNT; m++) {
        int seen = 0;
        for (int sk = 0; sk < MQ_NSKILL; sk++)
            for (int r = 0; r < mq_rungs(sk); r++)
                for (int s = 0; s < 3; s++) {
                    q->rng = (uint32_t)(m * 7919 + sk * 131 + r * 17 + s * 104729) | 1;
                    if (!order_in(sk, r, m))
                        break;
                    if (!q->gm.nopt || q->gm.make)
                        continue; /* (typed, made or dialled: no options to pick from) */
                    seen++, orders++;
                    int nr = 0, dup = 0;
                    for (int i = 0; i < q->gm.nopt; i++) {
                        nr += is_right(q->gm.opt[i]);
                        for (int j = 0; j < i; j++)
                            dup += strcmp(q->gm.lab[i], q->gm.lab[j]) == 0;
                    }
                    if (nr != 1 || dup || q->gm.nopt < 2 || mq_game_right_opt(q) < 0) {
                        if (bad++ < 12)
                            fprintf(stderr, "game %d skill %d (%s) rung %d: %d options, %d right, %d alike: %s | %s\n", m, sk,
                                    mq_skill[sk].name, r, q->gm.nopt, nr, dup, q->prob.text[0], q->prob.text[1]);
                    }
                }
        mechs_seen += seen > 0;
        if (seen)
            printf("  game %d: %d orders with options\n", m, seen);
    }
    printf("options: %d orders in %d games, %d bad\n", orders, mechs_seen, bad);
    CHECK(bad == 0 && orders > 2000 && mechs_seen >= 7);
}

/* the coloured parts a fraction picture should show (-1: not a coloured-part picture) */
static int want_shaded(const mq_prob_t *p, int *parts)
{
    switch (p->vis) {
    case V_PIE:
        if (p->kind == K_SHADE)
            return -1; /* (she colours it: the shading is hers) */
        *parts = p->v[1] * (p->v[2] > 0 ? p->v[2] : 1);
        return p->v[0];
    case V_BAR: *parts = p->v[1]; return p->v[0];
    case V_BAR2: *parts = p->v[1] + p->v[3]; return p->v[0] + p->v[2];
    case V_GRID:
        if (p->v[2] != 5)
            return -1;
        *parts = p->v[0] * p->v[1];
        return p->v[3];
    default: return -1;
    }
}

static void test_coloured_parts(void)
{
    con("new Pk 2");
    con("engine stop");
    int checked = 0, bad = 0, needed = 0;
    for (int m = 0; m < ME_COUNT; m++)
        for (int sk = 0; sk < MQ_NSKILL; sk++)
            for (int r = 0; r < mq_rungs(sk); r++)
                for (int s = 0; s < 3; s++) {
                    q->rng = (uint32_t)(m * 31 + sk * 7 + r * 101 + s * 13) | 1;
                    mq_prob_t t;
                    mq_generate(q, &t, sk, r);
                    int parts = 0;
                    if (want_shaded(&t, &parts) < 0)
                        break;
                    if (!order_in(sk, r, m))
                        break;
                    int want = want_shaded(&q->prob, &parts);
                    if (want < 0)
                        continue;
                    for (int f = 0; f < 2; f++) { /* the first frames of the question: nothing animated in */
                        q->vis_parts = q->vis_shaded = 0;
                        con("step 1");
                        if (!q->vis_parts) {
                            if (mq_vis_needed(&q->prob) && f == 1 && bad++ < 12)
                                fprintf(stderr, "game %d skill %d rung %d: the picture the question needs is not drawn\n", m, sk, r);
                            continue;
                        }
                        checked++, needed += mq_vis_needed(&q->prob);
                        bool vq = q->vis_q;
                        q->vis_q = true;
                        bool hidden = mq_visual_hides(q, &q->prob);
                        q->vis_q = vq;
                        if (q->vis_parts != parts || (q->vis_shaded != want && !(hidden && q->vis_shaded == 0))) {
                            if (bad++ < 12)
                                fprintf(stderr, "game %d skill %d (%s) rung %d frame %d: %d of %d parts coloured, want %d of %d: %s | %s\n",
                                        m, sk, mq_skill[sk].name, r, f, q->vis_shaded, q->vis_parts, want, parts,
                                        q->prob.text[0], q->prob.text[1]);
                        }
                    }
                }
    printf("coloured parts: %d frames (%d where the question reads the colours), %d bad\n", checked, needed, bad);
    CHECK(bad == 0 && checked > 50 && needed > 10);
}

/* the user's report itself: 1/4 of a cake pink, asked in Bubble Pop and Cloud Catch */
static void test_pink_cake(void)
{
    con("new Pc 2");
    con("engine stop");
    for (int m = 0; m < 2; m++) {
        int mech = m ? ME_CATCH : ME_POP, ok = 0;
        for (int s = 0; s < 40 && !ok; s++) {
            q->rng = (uint32_t)(s * 2654435761u) | 1;
            if (!order_in(S_UNITFRAC, 0, mech) || q->prob.ans / 1000 != 1)
                continue;
            q->vis_parts = q->vis_shaded = 0;
            con("step 1");
            CHECK(q->vis_shaded == 1 && q->vis_parts == q->prob.ans % 1000);
            CHECK(mq_game_right_opt(q) >= 0);
            ok = 1;
        }
        CHECK(ok);
    }
}

/* a twisted field (gravity flip, mirror world): the right option, tapped where it is drawn, is taken; the
 * debug hooks list it there (the bot tapped the unflipped box and popped a dud: its 'answer shown' S1) */
static void test_twist_taps(void)
{
    con("new Tw 2");
    con("engine stop");
    static const int mods[2] = {MOD_FLIP, MOD_MIRROR};
    for (int m = 0; m < 2; m++)
        for (int s = 0; s < 4; s++) {
            q->rng = (uint32_t)(s * 7 + m * 101) | 1;
            CHECK(order_in(S_GROUPS, 1, ME_POP));
            char b[16];
            snprintf(b, sizeof b, "twist %d", mods[m]);
            con(b);
            con("step 30");
            mq_game_widgets(q);
            int r = mq_game_right_opt(q), hit = 0;
            for (int i = 0; i < q->wg.n; i++) {
                const mq_wg_t *g = &q->wg.w[i];
                if (g->kind != WG_CHOICE || g->arg != r)
                    continue;
                int x = g->x, y = g->y;
                mq_game_screen_box(q, &x, &y, g->w, g->h);
                ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(1, x + g->w / 2, y + g->h / 2));
                hit = 1;
                break;
            }
            CHECK(hit);
            for (int k = 0; k < 40 && q->sub == GS_PLAY && !q->tried_wrong; k++)
                con("step 1");
            CHECK(q->sub == GS_CHEER && !q->tried_wrong);
        }
}

/* an order that needs its picture shows it in every game it goes to (the bot: a graph's order in Kart Race
 * drew no graph -- the road covered the card) */
static void test_picture_shown(void)
{
    con("new Ps 2");
    con("engine stop");
    int orders = 0, bad = 0;
    for (int m = 0; m < ME_COUNT; m++)
        for (int k = 0; k < MQ_NSKILL; k++)
            for (int r = 0; r < mq_rungs(k); r++) {
                int sks[MQ_NSKILL];
                sks[k] = k; /* (every skill whose order needs its picture: a graph, a clock, coins, "what did Dab paint?") */
                q->rng = (uint32_t)(m * 17 + k * 5 + r) | 1;
                if (!order_in(sks[k], r, m) || !mq_vis_needed(&q->prob))
                    continue;
                orders++;
                q->frame_units = 0;
                con("step 1");
                int r4[4], white = 0;
                ops.draw(q, 0, r4);
                for (int y = MQ_Y0 + 66; y < MQ_Y0 + 196; y++) /* the card on the left (the picking games) */
                    for (int x = 8; x < 110; x++)
                        white += fb[y * 320 + x] == 0xFFFF;
                bool card = m == ME_POP || m == ME_CATCH || m == ME_GATES || m == ME_KART || m == ME_ZAP;
                if (card && !mq_guide_demo_on(q)) { /* the card is a plain, still panel: nothing of the field (lane lines, clouds, sparkles) runs
                             * through it -- the same pixels ten frames later (the user: lines through a bar chart) */
                    static uint16_t was[130][102];
                    for (int y = 0; y < 130; y++)
                        for (int x = 0; x < 102; x++)
                            was[y][x] = fb[(MQ_Y0 + 66 + y) * 320 + 8 + x];
                    q->last_input = q->now;
                    con("step 10");
                    ops.draw(q, 0, r4);
                    int moved = 0;
                    for (int y = 0; y < 130; y++)
                        for (int x = 0; x < 102; x++)
                            moved += was[y][x] != fb[(MQ_Y0 + 66 + y) * 320 + 8 + x];
                    if (moved > 40 && q->sub == GS_PLAY && !mq_guide_demo_on(q) && bad++ < 8)
                        fprintf(stderr, "picture: game %d skill %d rung %d: %d pixels of the card changed (the field over it)\n", m,
                                sks[k], r, moved);
                }
                bool units = q->prob.vis == V_BARGRAPH || q->prob.vis == V_COINS || q->prob.vis == V_GRID; /* (counted things) */
                if (((units && !q->frame_units) || (card && white < 102 * 130 / 4)) && bad++ < 8)
                    fprintf(stderr, "picture: game %d skill %d rung %d: the picture the order needs is not drawn (%s | %s)\n", m,
                            sks[k], r, q->prob.text[0], q->prob.text[1]);
            }
    printf("pictures: %d orders that need their picture, %d without it\n", orders, bad);
    CHECK(bad == 0 && orders > 20);
}

/* while an order waits, nothing but its answers (and the game's controls) can be tapped: no extra bubble, star,
 * coin, friend or power-up drifts by (the user: "rising bubbles you can click on that are not the maths") */
static void test_no_extras(void)
{
    enum { W_FUN_ID = 3 }; /* (mq_play.c W_FUN: an extra as a widget) */
    con("new Nx 2");
    con("engine stop");
    static const int mechs[] = {ME_POP, ME_CATCH, ME_GATES, ME_KART, ME_ZAP};
    int frames = 0, extras = 0;
    for (size_t m = 0; m < sizeof mechs / sizeof mechs[0]; m++)
        for (int s = 0; s < 6; s++) {
            q->rng = (uint32_t)(s * 977 + (int)m) | 1;
            if (!order_in(S_GROUPS, 1, mechs[m]))
                continue;
            for (int f = 0; f < 600 && q->sub == GS_PLAY; f++) {
                q->last_input = q->now;
                con("step 1");
                mq_game_widgets(q);
                frames++;
                for (int i = 0; i < q->wg.n; i++)
                    extras += q->wg.w[i].id == W_FUN_ID;
            }
        }
    printf("extras: %d frames of waiting orders, %d tappable extras\n", frames, extras);
    CHECK(frames > 5000 && extras == 0);
}

/* squishies (the user's daughter: "it's weird to call a green squishy a strawberry"): a strawberry is red in
 * every variant, a peach peachy; the colour-free kinds keep their four pastels; the name follows the look */
static void test_squishy_colours(void)
{
    for (int k = 0; k < SQ_KINDS; k++)
        for (int c = 0; c < SQ_COLS; c++)
            for (int sh = 0; sh < 2; sh++) {
                uint16_t v = mq_squish_colour(k, c, sh);
                int r = (v >> 11) << 3, g = ((v >> 5) & 63) << 2, b = (v & 31) << 3;
                if (k == SQ_BERRY)
                    CHECK(r >= 120 && r > g + 40 && r > b + 20); /* red, deep red, pink-red, light red: never green */
                else if (k == SQ_PEACH)
                    CHECK(r >= 230 && g >= 120 && g <= 220 && b <= 190); /* peach shades */
            }
    char nm[40];
    mq_squish_name(SQ_BERRY, 2, false, nm, sizeof nm);
    CHECK(!strcmp(nm, "pink strawberry"));
    mq_squish_name(SQ_BERRY, 1, true, nm, sizeof nm);
    CHECK(!strcmp(nm, "sparkly deep red strawberry"));
    mq_squish_name(SQ_CAT, 2, false, nm, sizeof nm);
    CHECK(!strcmp(nm, "blue cat"));
    for (int c = 0; c < SQ_COLS; c++) /* (no kind's variant is out of its palette, whatever an old save holds) */
        CHECK(mq_squish_colour(SQ_BERRY, c + SQ_COLS, false) == mq_squish_colour(SQ_BERRY, c, false));
}

/* an order stays until she answers it: "show me" (the trick, a worked example) and the time passing bring her back to
 * the SAME order -- never another one (the user: "it shows me a problem, then flips to another one") */
static void test_order_stays(void)
{
    con("new Os 2");
    con("engine stop");
    static const int mechs[] = {ME_POP, ME_CATCH, ME_GATES, ME_VAULT, ME_SORT};
    int checked = 0, flips = 0;
    for (size_t m = 0; m < sizeof mechs / sizeof mechs[0]; m++)
        for (int k = 0; k < 3; k++) { /* (the third time the trick fades: the last step was hers -- on another order) */
            if (!order_in(S_GROUPS, 1, mechs[m]))
                continue;
            mq_prof(q)->sk[S_GROUPS].mastery = (uint8_t)(k ? 50 : 0); /* (k 1, 2: she half knows it -- the trick fades) */
            uint32_t id = mq_prob_ident(&q->prob);
            con("showme");
            for (int f = 0; f < 600 && q->sub != GS_PLAY; f++)
                con("step 1"), con("next");
            for (int f = 0; f < 300 && q->sub == GS_PLAY && !q->tried_wrong; f++) { /* she thinks a while */
                q->last_input = q->now;
                con("step 1");
            }
            checked++;
            if ((q->sub != GS_PLAY || mq_prob_ident(&q->prob) != id) && flips++ < 4)
                fprintf(stderr, "order: game %d try %d: after 'show me' the order is %s (sub %d)\n", mechs[m], k,
                        mq_prob_ident(&q->prob) == id ? "the same" : "ANOTHER", q->sub);
        }
    for (size_t m = 0; m < sizeof mechs / sizeof mechs[0]; m++) { /* idle for a minute and a half: the same order */
        if (!order_in(S_GROUPS, 1, mechs[m]))
            continue;
        uint32_t id = mq_prob_ident(&q->prob);
        for (int f = 0; f < 2200 && q->sub == GS_PLAY; f++)
            con("step 1");
        checked++;
        if ((q->sub != GS_PLAY || mq_prob_ident(&q->prob) != id) && flips++ < 4)
            fprintf(stderr, "order: game %d idle: the order changed (sub %d)\n", mechs[m], q->sub);
    }
    printf("orders: %d 'show me's and idles, %d flips\n", checked, flips);
    CHECK(checked >= 10 && flips == 0);
}

int main(void)
{
    harness_init();
    test_order_stays();
    test_squishy_colours();
    test_no_extras();
    test_picture_shown();
    test_twist_taps();
    test_pink_cake();
    test_options();
    test_coloured_parts();
    printf("test_mq_options: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
