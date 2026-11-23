/* Sparkle Town (modules/mathquest): drawing invariants over every skill, rung and many seeds.
 *   - labels never overlap (the playtest: "the bar chart's labels run into each other"): every text a
 *     picture draws stays in its box and clear of every other text it draws -- graphs, plots, tables, maps,
 *     rulers, and every other picture -- in each box size a game gives it, with the built-in names and with
 *     a theme pack's longest names (the Mac draws each text inside the same box, so this holds there too);
 *   - every number of a scale or an animated picture stays visible in every frame (the playtest: "a number
 *     disappears on the scales"): a frame-by-frame replay of each such order in its games checks that each
 *     operand the picture shows at the start is still drawn, frame after frame, until she answers.
 * Built and run by `make test`. */
#include "mq_test_util.h"

typedef struct {
    int x, y, w, h;
    char s[24];
} tbox_t;
static tbox_t tb[96];
static int ntb, logging;

static int log_text(const font_t *f, int x, int y, const char *s, uint16_t fg, int bg)
{
    if (logging && ntb < 96 && s[0]) {
        tbox_t *t = &tb[ntb++];
        t->x = x, t->y = y, t->w = modx_api.text_width(f, s), t->h = f->h;
        snprintf(t->s, sizeof t->s, "%s", s);
    }
    return modx_api.text(f, x, y, s, fg, bg);
}

static bool overlap(const tbox_t *a, const tbox_t *b)
{
    /* (one pixel of a cell's blank edge may touch) */
    return a->x + a->w - 1 > b->x && b->x + b->w - 1 > a->x && a->y + a->h - 1 > b->y && b->y + b->h - 1 > a->y;
}

static const int boxes[][2] = {{192, 82}, {222, 82}, {200, 92}, {180, 128}, {110, 138}, {132, 68}, {124, 128}, {192, 140}};
#define NBOX ((int)(sizeof boxes / sizeof boxes[0]))

static void long_names(bool on)
{
    q->theme.loaded = on;
    if (!on)
        return;
    q->theme.nspr = MQ_NCAST;
    for (int i = 0; i < MQ_NCAST; i++) {
        q->theme.role[i] = (uint8_t)i;
        snprintf(q->theme.names[i], sizeof q->theme.names[i], "%s", i % 2 ? "WWWWWWWWWWW" : "Annabellemm");
    }
}

static void test_labels(void)
{
    static modx_api_t api;
    api = modx_api, api.text = log_text;
    const modx_api_t *api0 = q->api;
    con("new La 2");
    int pics = 0, bad = 0, graphs = 0;
    for (int names = 0; names < 2; names++) {
        long_names(names == 1);
        for (int sk = 0; sk < MQ_NSKILL; sk++)
            for (int r = 0; r < mq_rungs(sk); r++)
                for (int seed = 0; seed < 24; seed++) {
                    mq_prob_t p;
                    q->rng = (uint32_t)(seed * 7919 + r * 31 + sk * 131 + names) | 1;
                    mq_generate(q, &p, sk, r);
                    if (p.vis == V_NONE)
                        continue;
                    bool graph = p.vis == V_BARGRAPH || p.vis == V_LINEPLOT || p.vis == V_TABLE || p.vis == V_COORD ||
                                 p.vis == V_RULER;
                    for (int b = 0; b < NBOX; b++)
                        for (int asked = 0; asked < 2; asked++) {
                            int x0 = 6, y0 = MQ_Y0 + 60, w = boxes[b][0], h = boxes[b][1];
                            ntb = 0, logging = 1;
                            q->api = &api;
                            q->vis_q = asked;
                            mq_visual(q, &p, x0, y0, w, h, 1000);
                            q->vis_q = false;
                            q->api = api0;
                            logging = 0;
                            pics++, graphs += graph;
                            for (int i = 0; i < ntb; i++) {
                                const tbox_t *t = &tb[i];
                                bool out = t->x < x0 - 1 || t->x + t->w > x0 + w + 2 || t->y < y0 - 2 || t->y + t->h > y0 + h + 3;
                                int hit = -1;
                                for (int j = 0; j < i && hit < 0; j++)
                                    if (overlap(t, &tb[j]))
                                        hit = j;
                                if ((out || hit >= 0) && bad++ < 100000)
                                    fprintf(stderr, "labels: skill %d (%s) rung %d seed %d box %dx%d%s%s: \"%s\" at %d,%d %dx%d %s%s\n", sk,
                                            mq_skill[sk].name, r, seed, w, h, asked ? " (asked)" : "", names ? " (long names)" : "",
                                            t->s, t->x, t->y, t->w, t->h, out ? "leaves the box" : "over ",
                                            hit >= 0 ? tb[hit].s : "");
                            }
                        }
                }
    }
    long_names(false);
    printf("labels: %d pictures (%d graphs, plots, tables, maps, rulers), %d bad\n", pics, graphs, bad);
    CHECK(bad == 0 && graphs > 5000);
}

/* ---- numbers that stay: every frame of a scale or an animated picture */
static char seen_txt[64][80];
static int nseen;
static int seen_text(const font_t *f, int x, int y, const char *s, uint16_t fg, int bg)
{
    if (logging && nseen < 64)
        snprintf(seen_txt[nseen++], sizeof seen_txt[0], "%s", s);
    return modx_api.text(f, x, y, s, fg, bg);
}
/* the number s as a whole in text h ("5" in "0 x 5 = ?", not in "15") */
static bool has_tok(const char *h, const char *s)
{
    size_t n = strlen(s);
    for (const char *t = strstr(h, s); t; t = strstr(t + 1, s))
        if ((t == h || t[-1] < '0' || t[-1] > '9') && (t[n] < '0' || t[n] > '9'))
            return true;
    return false;
}
static bool drawn(const char *s)
{
    for (int i = 0; i < nseen; i++)
        if (has_tok(seen_txt[i], s))
            return true;
    return false;
}

static bool order_in(int skill, int d, int mech)
{
    char b[32];
    snprintf(b, sizeof b, "skill %d %d", skill, d);
    con(b);
    q->fun_ms = 0, q->rapid = 0, q->guess_n = 0;
    for (int w = 0; w < WS_COUNT; w++)
        mq_pace(q)->ws_level[w] = 2;
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

static void test_operands_stay(void)
{
    static modx_api_t api;
    api = modx_api, api.text = seen_text;
    const modx_api_t *api0 = q->api;
    con("new Sc 2");
    con("engine stop");
    int orders = 0, bad = 0, frames = 0, scales = 0;
    for (int m = 0; m < ME_COUNT; m++)
        for (int sk = 0; sk < MQ_NSKILL; sk++)
            for (int r = 0; r < mq_rungs(sk); r++)
                for (int s = 0; s < 4; s++) {
                    q->scenes_off = s < 2; /* (and with the acted story first: its stage animates the picture) */
                    mq_prob_t t;
                    q->rng = (uint32_t)(m * 977 + sk * 31 + r * 7 + s * 1009) | 1;
                    mq_generate(q, &t, sk, r);
                    if (t.vis != V_SCALE && t.vis != V_JUG && t.vis != V_GROUPS && t.vis != V_ARRAY)
                        break;
                    if (!order_in(sk, r, m))
                        break;
                    /* the operands: the numbers the first frame draws that are in the order's text */
                    char ops_[8][16];
                    int nops = 0;
                    q->api = &api, nseen = 0, logging = 1;
                    con("step 1");
                    logging = 0, q->api = api0;
                    for (int i = 0; i < nseen && nops < 8; i++) {
                        const char *v = seen_txt[i];
                        bool num = v[0] && strspn(v, "0123456789") == strlen(v);
                        if (!num)
                            continue;
                        bool in_text = false;
                        for (int l = 0; l < 3 && !in_text; l++)
                            in_text = has_tok(q->prob.text[l], v);
                        if (in_text)
                            snprintf(ops_[nops++], sizeof ops_[0], "%s", v);
                    }
                    if (!nops)
                        continue;
                    orders++, scales += q->prob.vis == V_SCALE || q->prob.vis == V_JUG;
                    for (int f = 0; f < 150 && q->sub == GS_PLAY && !q->tried_wrong; f++) {
                        q->api = &api, nseen = 0, logging = 1;
                        q->last_input = q->now; /* (she is there: no idle hints) */
                        con("step 1");
                        logging = 0, q->api = api0;
                        frames++;
                        if (q->sub != GS_PLAY)
                            break;
                        if (mq_teach_on(q)) /* (a teaching moment's panel: a modal, the order behind it on purpose) */
                            continue;
                        for (int k = 0; k < nops; k++)
                            if (!drawn(ops_[k])) {
                                if (bad++ < 12)
                                    fprintf(stderr, "operands: game %d skill %d (%s) rung %d frame %d: \"%s\" is gone (%s | %s)\n", m,
                                            sk, mq_skill[sk].name, r, f, ops_[k], q->prob.text[0], q->prob.text[1]);
                                for (int z = 0; z < nseen && getenv("MQ_TRACE"); z++)
                                    fprintf(stderr, "  [%s]", seen_txt[z]);
                                f = 999;
                                break;
                            }
                    }
                }
    q->scenes_off = true;
    printf("operands: %d orders (%d on scales and jugs), %d frames, %d with a number gone\n", orders, scales, frames, bad);
    CHECK(bad == 0 && orders > 20 && scales > 10);
}

/* ---- the sequence render test (backlog (g)): every game's round drawn frame by frame from the order's start
 * (with and without the acted story) to a few seconds in: no text flickers -- gone for one frame and back the
 * next (a half-drawn frame, a draw order that changes with the frame) -- and no frame is blank */
static char seq_t[3][64][40];
static int seq_n[3];
static int seq_text(const font_t *f, int x, int y, const char *s, uint16_t fg, int bg)
{
    if (logging && seq_n[2] < 64 && s[0])
        snprintf(seq_t[2][seq_n[2]++], sizeof seq_t[0][0], "%s", s);
    return modx_api.text(f, x, y, s, fg, bg);
}
static bool seq_has(int k, const char *s)
{
    for (int i = 0; i < seq_n[k]; i++)
        if (!strcmp(seq_t[k][i], s))
            return true;
    return false;
}

static void test_sequence(void)
{
    static modx_api_t api;
    api = modx_api, api.text = seq_text;
    const modx_api_t *api0 = q->api;
    con("new Sq 2");
    con("engine stop");
    int rounds = 0, frames = 0, flick = 0, blank = 0;
    for (int m = 0; m < ME_COUNT; m++)
        for (int sk = 0; sk < MQ_NSKILL; sk += 5)
            for (int sc = 0; sc < 2; sc++) {
                q->rng = (uint32_t)(m * 13 + sk * 7 + sc) | 1;
                q->scenes_off = !sc;
                if (!order_in(sk, 1 < mq_rungs(sk) ? 1 : 0, m))
                    continue;
                rounds++;
                seq_n[0] = seq_n[1] = 0;
                for (int f = 0; f < 120 && q->scene == SC_PLAY; f++) {
                    q->last_input = q->now;
                    seq_n[2] = 0;
                    q->api = &api, logging = 1;
                    con("step 1");
                    q->api = api0, logging = 0;
                    frames++;
                    if (!seq_n[2] && blank++ < 6)
                        fprintf(stderr, "sequence: game %d skill %d frame %d: no text at all\n", m, sk, f);
                    if (f >= 2)
                        for (int i = 0; i < seq_n[0]; i++)
                            if (!seq_has(1, seq_t[0][i]) && seq_has(2, seq_t[0][i]) && flick++ < 12)
                                fprintf(stderr, "sequence: game %d skill %d%s frame %d: \"%s\" flickers (gone one frame)\n", m, sk,
                                        sc ? " (story)" : "", f, seq_t[0][i]);
                    memcpy(seq_t[0], seq_t[1], sizeof seq_t[0]), seq_n[0] = seq_n[1];
                    memcpy(seq_t[1], seq_t[2], sizeof seq_t[0]), seq_n[1] = seq_n[2];
                }
            }
    q->scenes_off = true;
    printf("sequence: %d rounds, %d frames, %d flickers, %d blank frames\n", rounds, frames, flick, blank);
    CHECK(rounds > 100 && flick == 0 && blank == 0);
}

/* Bubble Pop and Cloud Catch at the slowest speed, in every twist: no answer ever waits under the order board
 * with its number hidden, frame after frame (seeds 11, 13 and 15 of 49_mathquest_float.txt had a bubble crawling
 * through the top band at the floor speed for seconds) */
static void test_float(void)
{
    int bad = 0, frames = 0;
    for (int seed = 1; seed <= 24; seed++)
        for (int m = 0; m < 2; m++)
            for (int tw = 0; tw < 4; tw++) {
                static const int twists[4] = {MOD_NONE, MOD_FLIP, MOD_GIANT, MOD_MIRROR};
                char b[40];
                con("new Fl 3");
                con("engine stop");
                snprintf(b, sizeof b, "seed %d", seed);
                con(b);
                snprintf(b, sizeof b, "speed %d 0", m ? ME_CATCH : ME_POP);
                con(b);
                if (!order_in(S_GROUPS, 1, m ? ME_CATCH : ME_POP))
                    continue;
                snprintf(b, sizeof b, "twist %d", twists[tw]);
                con(b);
                for (int f = 0; f < 700 && q->sub == GS_PLAY; f++) {
                    con("step 1");
                    frames++;
                    if (mq_game_hidden(q) && bad++ < 6)
                        fprintf(stderr, "float: seed %d %s twist %d frame %d: an answer under the order board\n", seed,
                                m ? "Cloud Catch" : "Bubble Pop", twists[tw], f);
                }
            }
    printf("float: %d frames, %d with an answer hidden under the board\n", frames, bad);
    CHECK(bad == 0 && frames > 50000);
}

/* the user: "pie questions: sometimes the pieces that should be pink aren't" -- every fraction picture whose
 * coloured parts the question names or needs ("what part is pink?", "frost 3/4") shows them while it is asked,
 * at every rung and in every frame (a question that hides its model never hides the data it asks about) */
static bool names_colour(const mq_prob_t *p)
{
    for (int l = 0; l < 3; l++) {
        char w[MQ_TXT + 1];
        snprintf(w, sizeof w, "%s", p->text[l]);
        for (char *c = w; *c; c++)
            *c = (char)(*c | 32);
        if (strstr(w, "pink") || strstr(w, "colou") || strstr(w, "shaded") || strstr(w, "frosted"))
            return true;
    }
    char c[41];
    snprintf(c, sizeof c, "%s", p->cap);
    for (char *k = c; *k; k++)
        *k = (char)(*k | 32);
    return strstr(c, "pink") || strstr(c, "colou");
}
static void test_pink_shown(void)
{
    con("new Pk 3");
    int pics = 0, bad = 0;
    for (int sk = 0; sk < MQ_NSKILL; sk++)
        for (int r = 0; r < mq_rungs(sk); r++)
            for (int seed = 0; seed < 40; seed++) {
                mq_prob_t p;
                q->rng = (uint32_t)(seed * 7919 + r * 31 + sk * 131) | 1;
                mq_generate(q, &p, sk, r);
                if ((p.vis != V_PIE && p.vis != V_BAR) || p.vq || p.kind == K_SHADE || p.v[0] <= 0)
                    continue;
                if (!mq_vis_needed(&p) && !names_colour(&p))
                    continue;
                for (int rev = 0; rev <= 1000; rev += 250) {
                    q->vis_q = true;
                    mq_visual(q, &p, 6, MQ_Y0 + 60, 192, 82, rev);
                    q->vis_q = false;
                    pics++;
                    int d = p.v[1] > 1 ? p.v[1] : 1, pies = p.v[2] < 1 ? 1 : p.v[2] > 4 ? 4 : p.v[2];
                    int want = p.v[0] < (p.vis == V_PIE ? d * pies : d) ? p.v[0] : (p.vis == V_PIE ? d * pies : d);
                    if (q->vis_shaded != want && bad++ < 400 && rev == 1000)
                        fprintf(stderr, "pink: skill %d rung %d seed %d reveal %d: %d of %d coloured (\"%s %s\")\n", sk, r,
                                seed, rev, q->vis_shaded, want, p.text[0], p.text[1]);
                }
            }
    printf("pink: %d pictures, %d without their coloured parts\n", pics, bad);
    CHECK(pics > 100);
    CHECK(bad == 0);
}

/* CR-01 (the code review): a question picture whose data she reads (a picture graph's stars, a grid's
 * pink squares, a bar, a ribbon) is the whole picture -- never one unit short of the shown-answer picture */
static void test_data_whole(void)
{
    con("new Dw 3");
    int pics = 0, bad = 0;
    for (int sk = 0; sk < MQ_NSKILL; sk++)
        for (int r = 0; r < mq_rungs(sk); r++)
            for (int seed = 0; seed < 16; seed++) {
                mq_prob_t p;
                q->rng = (uint32_t)(seed * 104729 + r * 37 + sk * 17) | 1;
                mq_generate(q, &p, sk, r);
                if (!mq_vis_needed(&p) || p.vis == V_PATTERN || p.vis == V_ANGLE)
                    continue;
                q->vis_q = true;
                if (mq_visual_hides(q, &p)) {
                    q->vis_q = false;
                    continue;
                }
                mq_visual(q, &p, 6, MQ_Y0 + 60, 192, 82, 1000);
                int asked = q->vis_units, ash = q->vis_shaded;
                q->vis_q = false;
                mq_visual(q, &p, 6, MQ_Y0 + 60, 192, 82, 1000);
                pics++;
                if ((asked != q->vis_units || ash != q->vis_shaded) && bad++ < 8)
                    fprintf(stderr, "data: skill %d rung %d seed %d vis %d: asked %d/%d units, shown %d/%d\n", sk, r, seed,
                            p.vis, asked, ash, q->vis_units, q->vis_shaded);
            }
    printf("data: %d question pictures, %d not whole\n", pics, bad);
    CHECK(pics > 100);
    CHECK(bad == 0);
}

/* the user: "characters' heads are sometimes chopped off" -- in every scene and every game, over many frames, no
 * friend is drawn with the top of the head above the body's top (under the title line) */
static void test_heads(void)
{
    int topm = 99;
    for (int sl = 0; sl < MQ_NCAST; sl++) {
        const uint8_t *px = mq_cast_px(q, sl, EX_IDLE, false);
        for (int r = 0; r < ART_BIG; r++) {
            int on = 0;
            for (int c = 0; c < ART_BIG; c++)
                on |= px[r * ART_BIG + c];
            if (on) {
                if (r < topm)
                    topm = r;
                break;
            }
        }
    }
    printf("heads: the art's first row %d of %d\n", topm, ART_BIG);
    con("new He 3");
    con("engine stop");
    int frames = 0, bad = 0;
    for (int sc = 0; sc < SC_COUNT; sc++) {
        if (sc == SC_PARENT)
            continue;
        char b[48];
        if (sc == SC_ACTION) /* (an action game as it starts: its own command) */
            snprintf(b, sizeof b, "act %d", frames % 3);
        else
            snprintf(b, sizeof b, "go %s", mq_scene_name_of(sc));
        con(b);
        for (int f = 0; f < 60; f++) {
            con("step 7");
            int r[4];
            q->head_clip = 0, q->head_n = 0, q->head_watch = 1;
            ops.draw(q, 0, r);
            q->head_watch = 0;
            frames++;
            if (q->head_clip && bad++ < 30)
                fprintf(stderr, "heads: scene %s frame %d: slot %d's head at y %d (cover %d %d %d %d)\n",
                        mq_scene_name_of(q->scene), f, q->head_clip_slot, q->head_clip_y, q->head_cover[0],
                        q->head_cover[1], q->head_cover[2], q->head_cover[3]);
        }
    }
    for (int m = 0; m < ME_COUNT; m++) {
        con("skill 16 1");
        char b[48];
        snprintf(b, sizeof b, "mech %d", m);
        if (con(b) != 0)
            continue;
        for (int f = 0; f < 80; f++) {
            con(f % 20 == 10 ? "correct" : "step 7");
            int r[4];
            q->head_clip = 0, q->head_n = 0, q->head_watch = 1;
            ops.draw(q, 0, r);
            q->head_watch = 0;
            frames++;
            if (q->head_clip && bad++ < 30)
                fprintf(stderr, "heads: game %d scene %s frame %d: slot %d's head at y %d (cover %d %d %d %d)\n", m,
                        mq_scene_name_of(q->scene), f, q->head_clip_slot, q->head_clip_y, q->head_cover[0],
                        q->head_cover[1], q->head_cover[2], q->head_cover[3]);
        }
    }
    for (int g = 0; g < 4; g++) { /* the fact games, played to their end (the finish card over the friends) */
        char b[24];
        snprintf(b, sizeof b, "game %d", g);
        con(b);
        for (int f = 0; f < 160; f++) {
            con(f % 3 == 0 ? "correct" : "step 5");
            int r[4];
            q->head_clip = 0, q->head_n = 0, q->head_watch = 1;
            ops.draw(q, 0, r);
            q->head_watch = 0;
            frames++;
            if (q->head_clip && bad++ < 30)
                fprintf(stderr, "heads: fact game %d frame %d: slot %d's head at y %d (cover %d %d %d %d)\n", g, f,
                        q->head_clip_slot, q->head_clip_y, q->head_cover[0], q->head_cover[1], q->head_cover[2],
                        q->head_cover[3]);
        }
    }
    printf("heads: %d frames, %d with a head above the top or under a panel\n", frames, bad);
    CHECK(frames > 1000);
    CHECK(bad == 0);
}

/* the user: "as things get more advanced, ask fraction comparisons where the exact answer is NOT shown visually" --
 * past the first rungs a comparison is told with the numbers only (the bars come with the feedback) */
static void test_cmp_symbolic(void)
{
    con("new Cf 4");
    int top = 0, top_told = 0;
    for (int seed = 0; seed < 60; seed++)
        for (int r = 0; r < mq_rungs(S_CMPDIFF); r++) {
            mq_prob_t p;
            q->rng = (uint32_t)(seed * 977 + r * 13) | 1;
            mq_generate(q, &p, S_CMPDIFF, r);
            CHECK(!p.vq == (strstr(p.text[2], "half") != NULL)); /* (the half benchmark keeps its picture) */
            if (r == mq_rungs(S_CMPDIFF) - 1)
                top++, top_told += p.vq;
            mq_generate(q, &p, S_CMPFRAC, r < mq_rungs(S_CMPFRAC) ? r : 0);
            if (strstr(p.text[0], "slice") && p.v[0] >= 2) /* the same top number (2 or 3): reasoned */
                CHECK(p.vq);
            if (p.v[1] >= 10 || p.v[3] >= 10) /* tenths and twelfths */
                CHECK(p.vq);
        }
    CHECK(top_told * 10 >= top * 6); /* the top rung: mostly with the numbers only */
}

int main(void)
{
    harness_init();
    test_cmp_symbolic();
    test_heads();
    test_pink_shown();
    test_data_whole();
    test_float();
    test_sequence();
    test_labels();
    test_operands_stay();
    printf("test_mq_invariants: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
