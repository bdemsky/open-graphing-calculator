/* Math Quest (modules/mathquest): the curriculum and the game logic, run on
 * the host with the real API table (app/modx.c).
 *   - coverage: every Common Core standard of grades 3 and 4 is in the table
 *     with a description and at least one skill, plus the grade-2 foundations
 *     and the grade-5 challenge items; every skill is on a level; every Star
 *     Math domain is covered; the data skills have levels of their own. A
 *     missing standard fails `make test`;
 *   - generators: thousands of problems per skill and difficulty: the answer
 *     typed the way a player types it parses back and is right, choices are
 *     distinct, every known wrong answer is wrong and the classifier names its
 *     misconception, and spot checks recompute answers independently;
 *   - the misconception classifier on classic mistakes (no carrying, bigger
 *     denominator, area/perimeter, hands swapped, ignored remainder, longer
 *     decimal, place-value shift);
 *   - fact fluency: teaching order, fact families, Leitner boxes, re-asking a
 *     missed fact, fluency (box 4 and 3 s), the required set, strategies;
 *   - finishing the game needs every standard and every fact;
 *   - saving and loading the profiles through the core's save area;
 *   - theme packs from the (simulated) microSD card: loaded, damaged, missing;
 *     version 2 packs with home item pictures;
 *   - the home: the coin economy (learning pays more than grinding, diminishing
 *     returns on mastered skills, mastery / boss / persistence bonuses), the shop
 *     (prices, locked sets, set bonuses, daily gifts), rooms (placing, layering,
 *     turning, undo, drag and drop by touch), saving and migrating version-1 saves;
 *   - a scripted level through the console with a fixed seed.
 * Built and run by `make test`. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "../../modules/mathquest/mq.h"
#include "fat.h"
#include "gfx.h"
#include "hal.h"
#include "modx.h"

static int fails, checks;
#define CHECK(c)                                                                  \
    do {                                                                          \
        checks++;                                                                 \
        if (!(c)) {                                                               \
            fails++;                                                              \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
        }                                                                         \
    } while (0)

extern int mq_module_init(const modx_api_t *api, void *rw, modx_ops_t *ops);
const char *mq_scene_name(mq_t *q);
bool mq_fg_finished(mq_t *q);
extern const uint32_t mq_module_rw_size;

static mq_t *q;
static modx_ops_t ops;
static uint16_t fb[320 * 240];

static void fresh_module(void)
{
    memset(q, 0, mq_module_rw_size);
    memset(&ops, 0, sizeof ops);
    CHECK(mq_module_init(&modx_api, q, &ops) == 0);
    q->fun_ms = 0; /* scripted runs: no fun beats unless a test asks for them */
    q->rng = 0x2545F491u; /* deterministic: the module seeds from the clock */
    q->scenes_off = true;  /* orders start at once (test_scene acts them) */
}

static int con(const char *line)
{
    char buf[128], *argv[8];
    int argc = 0;
    snprintf(buf, sizeof buf, "mq %s", line);
    for (char *t = strtok(buf, " "); t && argc < 8; t = strtok(NULL, " "))
        argv[argc++] = t;
    return ops.console(q, argc, argv);
}
/* after a right answer: the cheer stays its reading time (mq_read.c): the frames to its end (at least the 40 the
 * old 1.5 s cheer took) */
static void cheer_out(void)
{
    int n = 0;
    for (; n < 600 && q->scene == SC_PLAY && q->sub == GS_CHEER; n++)
        con("step 1");
    for (; n < 40; n++)
        con("step 1");
}

/* ------------------------------------------------------------ coverage */
static const char *const required[] = {
    "3.OA.1", "3.OA.2", "3.OA.3", "3.OA.4", "3.OA.5", "3.OA.6", "3.OA.7", "3.OA.8", "3.OA.9", "3.NBT.1", "3.NBT.2",
    "3.NBT.3", "3.NF.1", "3.NF.2", "3.NF.3", "3.MD.1", "3.MD.2", "3.MD.3", "3.MD.4", "3.MD.5", "3.MD.6", "3.MD.7",
    "3.MD.8", "3.G.1", "3.G.2", "4.OA.1", "4.OA.2", "4.OA.3", "4.OA.4", "4.OA.5", "4.NBT.1", "4.NBT.2", "4.NBT.3",
    "4.NBT.4", "4.NBT.5", "4.NBT.6", "4.NF.1", "4.NF.2", "4.NF.3", "4.NF.4", "4.NF.5", "4.NF.6", "4.NF.7", "4.MD.1",
    "4.MD.2", "4.MD.3", "4.MD.4", "4.MD.5", "4.MD.6", "4.MD.7", "4.G.1", "4.G.2", "4.G.3",
    /* grade-2 foundations and grade-5 challenge items seen in adaptive (Star Math) assessments */
    "2.NBT.1", "2.NBT.3", "2.NBT.4", "2.NBT.7", "2.MD.7", "2.MD.8", "2.MD.10",
    /* grade 5 in full: a grade ahead (Sky Castle, Star Harbor) */
    "5.OA.1", "5.OA.2", "5.OA.3", "5.NBT.1", "5.NBT.2", "5.NBT.3", "5.NBT.4", "5.NBT.5", "5.NBT.6", "5.NBT.7",
    "5.NF.1", "5.NF.2", "5.NF.3", "5.NF.4", "5.NF.5", "5.NF.6", "5.NF.7", "5.MD.1", "5.MD.2", "5.MD.3", "5.MD.4",
    "5.MD.5", "5.G.1", "5.G.2", "5.G.3", "5.G.4"};

static int find_std(const char *id)
{
    for (int s = 0; s < MQ_NSTD; s++)
        if (!strcmp(mq_std[s].id, id))
            return s;
    return -1;
}

static bool on_level(int skill, int world)
{
    for (int l = 0; l < MQ_MAXLV; l++)
        if (mq_levels[world][l] == skill + 1)
            return true;
    return false;
}

static void test_coverage(void)
{
    char err[64];
    int bad = mq_coverage_check(err, sizeof err, &modx_api);
    if (bad)
        fprintf(stderr, "coverage: %s\n", err);
    CHECK(bad == 0);
    int n = (int)(sizeof required / sizeof required[0]);
    CHECK(n == 53 + 7 + 26);
    for (int i = 0; i < n; i++) {
        int s = find_std(required[i]);
        if (s < 0)
            fprintf(stderr, "standard %s is missing from mq_std[]\n", required[i]);
        CHECK(s >= 0);
        if (s < 0)
            continue;
        CHECK(mq_std[s].desc[0] && mq_std[s].skills[0]);
        CHECK(mq_std[s].domain < MQ_NDOM);
        CHECK(mq_std[s].grade == required[i][0] - '0');
    }
    /* every Star Math domain has grade-3/4 standards; grade-2 and grade-5 rows exist */
    int dom[MQ_NDOM] = {0}, g2 = 0, g5 = 0;
    for (int s = 0; s < MQ_NSTD; s++) {
        dom[mq_std[s].domain] += mq_std[s].grade == 3 || mq_std[s].grade == 4;
        g2 += mq_std[s].grade == 2, g5 += mq_std[s].grade == 5;
    }
    for (int d = 0; d < MQ_NDOM; d++)
        CHECK(dom[d] >= 3);
    CHECK(g2 == 7 && g5 == 26);
    for (int s = 0; s < MQ_NSTD; s++) /* grade 5 is played in its two worlds */
        for (int j = 0; j < 4 && mq_std[s].grade == 5; j++)
            if (mq_std[s].skills[j])
                CHECK(mq_skill[mq_std[s].skills[j] - 1].world >= G_SKY);
    /* the data skills are real levels of the Fair; the challenge track is the Sky Castle */
    CHECK(on_level(S_GRAPH1, G_FAIR) && on_level(S_SCALED, G_FAIR) && on_level(S_RULER, G_FAIR) &&
          on_level(S_LINEPLOT, G_FAIR) && on_level(S_TABLE, G_FAIR));
    CHECK(on_level(S_MULBIG, G_SKY) && on_level(S_FRUNLIKE, G_STAR) && on_level(S_DEC1000, G_SKY) &&
          on_level(S_VOLUME, G_STAR) && on_level(S_COORD, G_SKY) && on_level(S_SHAPES5, G_STAR));
    for (int sk = 0; sk < MQ_NSKILL; sk++)
        CHECK(mq_skill[sk].name[0] && mq_skill[sk].world < MQ_NWORLD);
    for (int m = 1; m < MC_COUNT; m++)
        CHECK(mq_mc_name[m][0] && mq_mc_help[m][0][0]);
}

/* ------------------------------------------------------------ generators */
static void test_generators(void)
{
    char s[32];
    for (int sk = 0; sk < MQ_NSKILL; sk++)
        for (int d = 0; d < mq_rungs(sk); d++) /* every rung of the ladder */
            for (int it = 0; it < 150; it++) {
                q->rng = (uint32_t)(sk * 7919 + d * 104729 + it * 31 + 1);
                mq_prob_t p;
                mq_generate(q, &p, sk, d);
                bool ok = true;
                ok &= p.skill == sk && p.text[0][0] &&
                      (p.vis != V_NONE || (p.kind == K_CHOICE && p.mc_default == MC_WRONG_OP) || /* pick the operation */
                       (sk == S_TWOSTEP && mq_rung_cx(&p, 0) == 0) || /* add, then add more: the numbers */
                       (sk == S_CMPBIG && !strncmp(p.text[0], "Glue", 4)) || /* (its place chart was the answer: CR2-12) */
                       sk >= S_POW10); /* (grade 5: the numbers are the picture) */
                ok &= p.diff == d;
                if (p.kind == K_CHOICE) {
                    ok &= p.nchoice >= 2 && p.ans >= 0 && p.ans < p.nchoice;
                    for (int i = 0; i < p.nchoice; i++) {
                        ok &= p.choice[i][0] != 0;
                        for (int j = 0; j < i; j++)
                            ok &= strcmp(p.choice[i], p.choice[j]) != 0;
                        if (i != p.ans)
                            ok &= mq_classify(&p, i) != MC_NONE && !mq_correct(&p, i);
                    }
                    ok &= mq_correct(&p, p.ans);
                } else if (p.kind == K_SHADE || p.kind == K_LINE) {
                    ok &= p.ans > 0 && mq_correct(&p, p.ans);
                } else {
                    int32_t v = -1;
                    mq_answer_text(q, &p, s, sizeof s);
                    ok &= mq_parse(&p, s, &v) == 0 && mq_correct(&p, v);
                    ok &= p.ans >= 0;
                }
                for (int i = 0; i < p.nwrong; i++) {
                    ok &= !mq_correct(&p, p.wrong[i]);
                    int mc = mq_classify(&p, p.wrong[i]);
                    /* the first known wrong answer that matches decides (fractions: an equivalent one) */
                    ok &= mc != MC_NONE;
                    bool earlier = false;
                    for (int j = 0; j < i; j++)
                        earlier |= p.wrong[j] == p.wrong[i];
                    if (p.kind != K_FRAC && !earlier)
                        ok &= mc == p.wrong_mc[i];
                }
                { /* the order fits the order board's bubble: three lines of 262 pixels */
                    char all[3 * MQ_TXT + 4], l[3][MQ_TXT];
                    snprintf(all, sizeof all, "%s %s %s", p.text[0], p.text[1], p.text[2]);
                    if (mq_story_wrap(q, all, l, 262) > 3) {
                        fprintf(stderr, "too long: %s\n", all);
                        ok = false;
                    }
                }
                /* independent spot checks */
                if (sk == S_GROUPS || (sk == S_AREAMUL && !strstr(p.text[1], "wide")) || (sk == S_AREACOUNT && p.vis == V_GRID))
                    ok &= p.ans == p.v[0] * p.v[1];
                if (sk == S_AREAMUL && strstr(p.text[1], "wide"))
                    ok &= p.ans == p.v[1] && p.ans * p.v[0] < 100;
                if ((sk == S_AREACOUNT || sk == S_AREAL) && p.vis == V_LSHAPE)
                    ok &= p.ans == p.v[0] * p.v[1] + p.v[2] * p.v[3] && p.v[2] < p.v[0];
                if (sk == S_PERIM && p.v[2] == 2 && strstr(p.text[1], "ribbon"))
                    ok &= p.ans == 2 * (p.v[0] + p.v[1]);
                if (sk == S_VOLUME)
                    ok &= p.ans == p.v[0] * p.v[1] * p.v[2] + p.v[3] * p.v[4] * p.v[5];
                if (sk == S_DIVIDE && p.kind == K_REM)
                    ok &= p.big == (p.ans / 1000) * p.a[1] + p.ans % 1000 && p.ans % 1000 < p.a[1] && p.ans % 1000;
                if (sk == S_DIVIDE && p.kind == K_NUM)
                    ok &= p.big == p.ans * p.a[1];
                if (sk == S_MULTI || sk == S_MULBIG)
                    ok &= p.ans == p.op[0] * p.op[1] && p.ws == WS_MUL;
                if (p.ws == WS_ADD)
                    ok &= p.ans == p.op[0] + p.op[1];
                if (p.ws == WS_SUB && p.kind == K_NUM)
                    ok &= p.ans == p.op[0] - p.op[1];
                if (p.ws == WS_DIV)
                    ok &= p.op[0] == (p.kind == K_REM ? p.ans / 1000 * p.op[1] + p.ans % 1000 : p.ans * p.op[1]);
                if (sk == S_ADDSUB || sk == S_ADDSUBBIG) {
                    int a = 0, b = 0;
                    char op = 0;
                    if (sscanf(p.text[2], "%d %c", &a, &op) == 2) {
                        const char *t = strstr(p.text[2], op == '+' ? "+ " : "\xE2\x88\x92 ");
                        b = t ? atoi(t + (op == '+' ? 2 : 4)) : 0;
                        ok &= p.ans == (op == '+' ? a + b : a - b);
                    }
                }
                if (sk == S_TIME1 || sk == S_TIME5)
                    ok &= p.ans / 100 == p.v[0] && p.ans % 100 == p.v[1] && p.ans % 100 < 60;
                if (sk == S_COORD)
                    ok &= p.ans == p.v[0] * 100 + p.v[1];
                if (sk == S_COINS)
                    ok &= p.ans == p.v[4] * 100 + p.v[0] * 25 + p.v[1] * 10 + p.v[2] * 5 + p.v[3];
                if (!ok) {
                    fprintf(stderr, "skill %d (%s) diff %d: %s | %s | ans %d kind %d\n", sk, mq_skill[sk].name, d,
                            p.text[0], p.text[1], (int)p.ans, p.kind);
                    CHECK(ok);
                    it = 1000; /* one report per skill and difficulty */
                }
                checks++;
            }
    /* worked examples exist for every skill and end with the answer */
    for (int sk = 0; sk < MQ_NSKILL; sk++) {
        mq_prob_t p, vis;
        char out[3][MQ_TXT];
        q->rng = (uint32_t)sk + 99;
        mq_generate(q, &p, sk, 1);
        int n = mq_steps(q, &p, 0, out, &vis);
        CHECK(n >= 2);
        CHECK(out[0][0] != 0);
        mq_steps(q, &p, n - 1, out, &vis);
        CHECK(strstr(out[0], "answer") != NULL);
    }
}

/* find a problem of `skill` satisfying cond (fresh numbers each time) */
#define FIND(p, skill, d, cond)                                    \
    do {                                                           \
        for (int k_ = 0; k_ < 5000; k_++) {                        \
            mq_generate(q, &(p), skill, d);                        \
            if (cond)                                              \
                break;                                             \
        }                                                          \
    } while (0)

static void test_classifier(void)
{
    mq_prob_t p;
    q->rng = 12345;
    /* 47 + 38 written as 75: the ten was not carried */
    FIND(p, S_ADDSUB, 3, p.a[2] == 0 && p.a[0] % 10 + p.a[1] % 10 >= 10);
    int a = p.a[0], b = p.a[1];
    int32_t nc = 0, m = 1;
    for (int x = a, y = b; x || y; x /= 10, y /= 10, m *= 10)
        nc += ((x % 10 + y % 10) % 10) * m;
    CHECK(p.ans == a + b && nc != a + b);
    CHECK(mq_classify(&p, nc) == MC_NO_REGROUP);
    /* subtraction: the smaller digit taken from the bigger */
    FIND(p, S_ADDSUB, 3, p.a[2] == 1 && p.a[0] % 10 < p.a[1] % 10);
    a = p.a[0], b = p.a[1];
    int32_t fl = 0;
    m = 1;
    for (int x = a, y = b; x || y; x /= 10, y /= 10, m *= 10)
        fl += abs(x % 10 - y % 10) * m;
    CHECK(mq_classify(&p, fl) == MC_SUB_FLIP || mq_classify(&p, fl) == MC_NO_BORROW_ZERO);
    /* 1/8 > 1/4? bigger denominator, bigger slice */
    FIND(p, S_CMPFRAC, 1, p.mc_default == MC_BIG_DENOM);
    CHECK(p.kind == K_CHOICE && p.v[0] == p.v[2]);
    CHECK(mq_classify(&p, 2 - p.ans) == MC_BIG_DENOM);
    /* area answered with the perimeter */
    FIND(p, S_AREAMUL, 1, 1);
    CHECK(mq_classify(&p, 2 * (p.v[0] + p.v[1])) == MC_AREA_PERIM);
    FIND(p, S_PERIM, 0, p.v[2] == 2);
    CHECK(mq_classify(&p, p.v[0] * p.v[1]) == MC_AREA_PERIM || p.ans != 2 * (p.v[0] + p.v[1]));
    /* clock hands swapped: 3:40 read as 8:15 */
    FIND(p, S_TIME5, 2, p.v[1] >= 5);
    int h = p.v[0], mm = p.v[1];
    CHECK(mq_classify(&p, (mm / 5) * 100 + (h % 12) * 5) == MC_HANDS_SWAP);
    /* the leftover forgotten */
    FIND(p, S_DIVIDE, 2, p.kind == K_REM && p.ans % 1000 != 0);
    CHECK(mq_classify(&p, p.ans / 1000 * 1000) == MC_REM_IGNORED);
    int32_t v;
    CHECK(mq_parse(&p, "17R3", &v) == 0 && v == 17003);
    /* 0.5 < 0.45? a longer decimal is bigger */
    FIND(p, S_CMPDEC, 1, p.a[0] * 10 > p.a[1] && p.a[1] / 10 < p.a[0]);
    CHECK(p.ans == 0 && mq_classify(&p, 2) == MC_DEC_LONGER); /* (the bins: the left one, the same, the right one) */
    /* place value: the 7 in 4,732 worth 7 */
    FIND(p, S_PV3, 3, p.wrong_mc[0] == MC_PLACE_SHIFT);
    CHECK(mq_classify(&p, p.wrong[0]) == MC_PLACE_SHIFT);
    /* fractions: added the denominators */
    FIND(p, S_ADDFRAC, 0, strstr(p.text[1], "in all") != NULL);
    int n1 = (int)(p.ans / 1000), d1 = (int)(p.ans % 1000);
    CHECK(mq_classify(&p, n1 * 1000 + 2 * d1) == MC_ADD_DENOMS);
    CHECK(mq_correct(&p, 2 * n1 * 1000 + 2 * d1)); /* an equivalent fraction is right */
    /* a near miss and an unknown mistake */
    FIND(p, S_ADDSUBBIG, 2, p.ans > 1000);
    CHECK(mq_classify(&p, p.ans + 1) == MC_CLOSE);
    /* parsing */
    p.kind = K_MONEY;
    CHECK(mq_parse(&p, "2.65", &v) == 0 && v == 265);
    CHECK(mq_parse(&p, "3", &v) == 0 && v == 300);
    CHECK(mq_parse(&p, ".7", &v) == 0 && v == 70);
    p.kind = K_TIME;
    CHECK(mq_parse(&p, "345", &v) == 0 && v == 345);
    CHECK(mq_parse(&p, "12:05", &v) == 0 && v == 1205);
    p.kind = K_FRAC;
    CHECK(mq_parse(&p, "3/0", &v) < 0 && mq_parse(&p, "3/4", &v) == 0 && v == 3004);
    p.kind = K_DEC;
    CHECK(mq_parse(&p, "0.045", &v) == 0 && v == 45);
}


/* grade-3 gaps (Star Math check): borrowing across zeros within 1000, "how many
 * fewer" on picture / bar graphs, elapsed time that crosses the hour */
static void test_grade3_gaps(void)
{
    mq_prob_t p, vis;
    char out[3][MQ_TXT];
    q->rng = 777;
    int top = mq_rungs(S_ADDSUB) - 1;
    for (int d = 0; d <= top; d++) {
        int sub = 0, zero = 0;
        for (int i = 0; i < 600; i++) {
            mq_generate(q, &p, S_ADDSUB, d);
            if (p.a[2] == 0)
                continue;
            sub++;
            if (p.a[2] != 2)
                continue;
            zero++;
            int a = p.a[0], b = p.a[1];
            CHECK(a >= 200 && a < 1000 && a / 10 % 10 == 0 && a % 10 < b % 10 && b < a && p.ans == a - b);
            CHECK(mq_classify(&p, a - b + 10) == MC_NO_BORROW_ZERO);
            int32_t fl = 0, m = 1;
            for (int x = a, y = b; x || y; x /= 10, y /= 10, m *= 10)
                fl += abs(x % 10 - y % 10) * m;
            CHECK(fl == p.ans || mq_classify(&p, fl) == MC_NO_BORROW_ZERO);
            CHECK(p.mc_default == MC_NO_BORROW_ZERO);
        }
        /* across a zero only on the top rung (never in her first ones: the audit's L2), and there often */
        CHECK(sub > 100 && (d < top ? zero == 0 : zero * 3 >= sub));
    }
    /* the worked example walks through the regrouping chain: 603 -> 5 hundreds, 9 tens, 13 ones */
    FIND(p, S_ADDSUB, mq_rungs(S_ADDSUB) - 1, p.a[2] == 2 && p.a[0] % 10 == 3);
    int n = mq_steps(q, &p, 0, out, &vis);
    CHECK(n == 5);
    mq_steps(q, &p, 1, out, &vis);
    CHECK(strstr(out[1], "trade 1 hundred for 10 tens") != NULL);
    mq_steps(q, &p, 2, out, &vis);
    char want[48];
    snprintf(want, sizeof want, "%d hundreds, 9 tens, 13 ones.", p.a[0] / 100 - 1);
    CHECK(!strcmp(out[1], want));
    mq_steps(q, &p, n - 1, out, &vis);
    CHECK(strstr(out[0], "answer") != NULL);
    /* how many fewer: single-unit and scaled graphs; skipping the key on the scaled one */
    for (int sk = S_GRAPH1; sk <= S_SCALED; sk++) {
        FIND(p, sk, 2, strstr(p.text[1], "fewer") != NULL);
        CHECK(strstr(p.text[1], "fewer") != NULL);
        int lo = -1, hi = -1;
        for (int i = 0; i < 4; i++) {
            static const char *fr[4] = {"apples", "pears", "plums", "grapes"};
            if (strstr(p.text[1], fr[i]))
                lo = i;
            if (strstr(p.text[2], fr[i]))
                hi = i;
        }
        CHECK(lo >= 0 && hi >= 0 && p.ans == p.v[2 + hi] - p.v[2 + lo] && p.ans > 0);
        if (sk == S_SCALED) {
            CHECK(p.v[1] > 1 && mq_classify(&p, p.ans / p.v[1]) == MC_SCALE_IGNORED);
            FIND(p, sk, 2, strstr(p.text[1], "fewer") != NULL && p.v[1] == 5);
            CHECK(p.v[1] == 5); /* each symbol = 5 */
        }
    }
    int fewer = 0;
    for (int i = 0; i < 400; i++) {
        mq_generate(q, &p, S_SCALED, 1);
        fewer += strstr(p.text[1], "fewer") != NULL;
    }
    CHECK(fewer >= 60);
    /* elapsed time: whole hours first, then minutes in the hour, then across the hour (2:45 + 50 min = 3:35) */
    for (int d = 0; d < mq_rungs(S_ELAPSED); d++) {
        int cross = 0;
        for (int i = 0; i < 300; i++) {
            mq_generate(q, &p, S_ELAPSED, d);
            int start = p.a[0], dur = p.a[1];
            cross += start % 60 + dur % 60 >= 60;
            if (p.kind == K_TIME) {
                int e = start + dur;
                int hh = e / 60 % 12;
                CHECK(p.ans == (hh ? hh : 12) * 100 + e % 60);
            } else
                CHECK(p.ans == dur);
            if (d == 0)
                CHECK(dur % 60 == 0);
        }
        CHECK(d < 3 ? cross == 0 : d == 3 ? cross >= 150 : 1);
    }
}

/* ------------------------------------------------------------ facts */
static void test_facts(void)
{
    con("new Ava 2");
    mq_profile_t *p = mq_prof(q);
    int tot;
    CHECK(mq_facts_fluent(p, &tot) == 0 && tot == 121 + 110);
    p->facts12 = 1;
    mq_facts_fluent(p, &tot);
    CHECK(tot == MQ_NFACT);
    p->facts12 = 0;
    /* teaching order: a quick check of the x0 / x1 rules first, then the tables */
    q->nqueue = 0, q->new_today = 0;
    int f = mq_fact_next(q), a, b;
    bool div;
    mq_fact_decode(f, &a, &b, &div);
    CHECK(!div && (a <= 1 || b <= 1) && mq_fact_trivial(f));
    /* the family of 3 x 7: its twin at once; its division only once the multiplication is solid */
    q->nqueue = 0;
    int f37 = mq_fact_index(3, 7, false);
    mq_fact_record(q, f37, false, 5000); /* missed: box 1, asked again soon, with its family */
    CHECK(p->fact[f37].box == 1);
    bool seen73 = false, seen21_3 = false, seen21_7 = false, seen37 = false;
    for (int i = 0; i < 6; i++) {
        int g = mq_fact_next(q);
        seen37 |= g == f37;
        seen73 |= g == mq_fact_index(7, 3, false);
        seen21_3 |= g == mq_fact_index(7, 3, true); /* 21 / 3 = 7 */
        seen21_7 |= g == mq_fact_index(3, 7, true); /* 21 / 7 = 3 */
    }
    CHECK(seen37 && seen73 && !seen21_3 && !seen21_7);
    CHECK(!mq_fact_div_ready(p, mq_fact_index(7, 3, true)));
    for (int x = 2; x <= 10; x++) /* the x3 and x7 tables solid: their division is ready */
        p->fact[mq_fact_index(x, 3, false)].box = p->fact[mq_fact_index(x, 7, false)].box = 3;
    CHECK(mq_fact_div_ready(p, mq_fact_index(7, 3, true)) && mq_fact_div_ready(p, mq_fact_index(3, 7, true)));
    q->nqueue = 0;
    mq_fact_record(q, f37, false, 5000);
    for (int i = 0; i < 8; i++) {
        int g = mq_fact_next(q);
        seen21_3 |= g == mq_fact_index(7, 3, true);
        seen21_7 |= g == mq_fact_index(3, 7, true);
    }
    CHECK(seen21_3 && seen21_7);
    for (int x = 2; x <= 10; x++)
        p->fact[mq_fact_index(x, 3, false)].box = p->fact[mq_fact_index(x, 7, false)].box = 0;
    p->fact[f37].box = 1;
    /* Leitner: fast right answers climb; slow ones stay; fluent at box 4 and <= 3 s */
    mq_fact_t *t = &p->fact[f37];
    for (int i = 0; i < 3; i++) {
        p->round = t->due; /* it is due again */
        mq_fact_record(q, f37, true, 2000);
    }
    CHECK(t->box == 4);
    CHECK(!mq_fact_fluent(t) || t->t10 <= 30);
    mq_fact_record(q, f37, true, 2000);
    CHECK(t->box == 5 && !mq_fact_fluent(t)); /* box 5, but all on one play day: not yet */
    p->days++; /* the next play day: still quick, now it is known for good */
    mq_fact_record(q, f37, true, 2000);
    CHECK(t->box == 5 && mq_fact_fluent(t));
    CHECK(t->due == p->round + 12);
    uint8_t before = t->box;
    mq_fact_record(q, f37, true, 8000); /* right but slow: the box stays */
    CHECK(t->box == before);
    mq_fact_record(q, f37, false, 1000);
    CHECK(t->box == 1 && !mq_fact_fluent(t));
    /* due facts come back by box (nothing asked just before) */
    q->nqueue = 0;
    memset(q->fhist, 0, sizeof q->fhist);
    p->round = 1000;
    CHECK(mq_fact_next(q) == f37);
    /* strategies */
    char st[3][MQ_TXT];
    mq_prob_t vis;
    memset(&vis, 0, sizeof vis);
    mq_fact_strategy(q, mq_fact_index(9, 7, false), st, &vis);
    CHECK(strstr(st[0], "Nines") && strstr(st[1], "70") && strstr(st[1], "63"));
    mq_fact_strategy(q, mq_fact_index(4, 6, false), st, &vis);
    CHECK(strstr(st[0], "Double") != NULL);
    mq_fact_strategy(q, mq_fact_index(7, 8, false), st, &vis);
    CHECK(strstr(st[1], "56") != NULL && vis.v[2] != 0); /* 7 x 8: split 5 + 2, or doubles */
    mq_fact_strategy(q, mq_fact_index(6, 8, true), st, &vis); /* 48 / 8 */
    CHECK(strstr(st[0], "multiplication") && strstr(st[2], "48"));
}

/* ------------------------------------------------------------ the whole game */
static void test_completion(void)
{
    con("new Zoe 3");
    mq_profile_t *p = mq_prof(q);
    CHECK(!mq_quest_complete(p));
    con("grant all");
    CHECK(mq_quest_complete(p));
    /* one required fact not fluent: not finished */
    p->fact[mq_fact_index(6, 7, false)].box = 2;
    CHECK(!mq_quest_complete(p));
    p->fact[mq_fact_index(6, 7, false)].box = 5;
    /* a 12s fact does not count unless the parent asks for 11 and 12 */
    p->fact[mq_fact_index(12, 12, false)].box = 0;
    CHECK(mq_quest_complete(p));
    /* one standard not passed: not finished */
    p->sk[S_SYMMETRY].flags = 0;
    CHECK(!mq_quest_complete(p));
    int ok, total;
    mq_dom_progress(p, DOM_GE, &ok, &total);
    CHECK(ok == total - 1);
}

static void test_save_load(void)
{
    con("new Lily 4");
    mq_profile_t *p = mq_prof(q);
    p->stars = 321;
    p->fact[mq_fact_index(8, 7, false)].box = 3;
    p->sk[S_GROUPS].mastery = 77;
    mq_save_now(q);
    int cur = q->save.cur;
    fresh_module(); /* a new bind: the RW block is zeroed, the save comes back from the core */
    CHECK(q->save.cur == cur);
    p = mq_prof(q);
    CHECK(p->used && !strcmp(p->name, "Lily") && p->buddy == 4);
    CHECK(p->stars == 321 && p->fact[mq_fact_index(8, 7, false)].box == 3 && p->sk[S_GROUPS].mastery == 77);
    CHECK(sizeof(mq_save_t) <= MODX_SAVE_MAX);
}

/* ------------------------------------------------------------ theme packs */
static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static size_t build_thm(uint8_t *b, bool skins)
{
    /* 2 sprites of 8 x 8, 3 colours, slots 2 and 7 */
    uint8_t *p = b + 64;
    uint16_t pal[3] = {0, 0xF800, 0x07E0};
    memcpy(p, pal, 6), p += 6;
    const char *names[2] = {"Berry", "Comet"};
    uint8_t slots[2] = {2, 7};
    for (int i = 0; i < 2; i++) {
        memset(p, 0, 16);
        memcpy(p, names[i], strlen(names[i]));
        p[12] = slots[i];
        p += 16;
    }
    if (skins)
        for (int i = 0; i < THM_NWORLD * 3; i++)
            p[0] = (uint8_t)(i + 1), p[1] = 0x12, p += 2;
    for (int i = 0; i < 2 * 64; i++)
        *p++ = (uint8_t)((i % 8 + i / 8) % 3);
    size_t plen = (size_t)(p - (b + 64));
    memset(b, 0, 64);
    memcpy(b, "MQTH", 4);
    b[4] = 1, b[6] = 64;
    memcpy(b + 8, "Test pack", 9);
    b[32] = 8, b[33] = 8, b[34] = 2, b[35] = skins ? 1 : 0, b[36] = 3;
    b[40] = (uint8_t)plen, b[41] = (uint8_t)(plen >> 8);
    uint32_t c = crc32(b + 64, plen);
    for (int i = 0; i < 4; i++)
        b[44 + i] = (uint8_t)(c >> (8 * i));
    return 64 + plen;
}

static int put_file(const char *path, const uint8_t *d, size_t n)
{
    fs_file_t f;
    int r = fs_open(&f, path, FS_WRITE);
    if (!r && fs_write(&f, d, n) != (int)n)
        r = -1;
    if (!r)
        r = fs_close(&f);
    fs_unmount();
    return r;
}

static void test_themes(void)
{
    static uint8_t b[2048];
    CHECK(hal_sd_sim_insert(64) == 0);
    CHECK(fs_mkfs() == 0);
    CHECK(fs_mkdir("/CALC") == 0 && fs_mkdir("/CALC/THEMES") == 0);
    size_t n = build_thm(b, true);
    CHECK(put_file("/CALC/THEMES/TEST.THM", b, n) == 0);
    b[100] ^= 0x55; /* one damaged byte */
    CHECK(put_file("/CALC/THEMES/BAD.THM", b, n) == 0);
    fs_unmount();
    mq_theme_scan(q);
    CHECK(q->ntheme_files == 2);
    CHECK(mq_theme_load(q, "TEST.THM") == 0 && q->theme.loaded);
    CHECK(!strcmp(q->theme.title, "Test pack") && q->theme.nspr == 2 && q->theme.cw == 8);
    CHECK(!strcmp(mq_cast_name(q, 2), "Berry") && !strcmp(mq_cast_name(q, 7), "Comet"));
    CHECK(!strcmp(mq_cast_name(q, 0), "Pip")); /* slots the pack leaves out keep the built-in cast */
    CHECK(q->theme.has_skin && q->theme.skin[0][0] == 0x1201);
    CHECK(mq_theme_px(q, 1)[9] < 3 && q->theme.anim[1] == AN_IDLE && q->theme.frame[1] == 0); /* an old pack: idle */
    CHECK(mq_theme_load(q, "BAD.THM") == -3 && !q->theme.loaded);
    CHECK(!strcmp(mq_cast_name(q, 2), "Momo"));
    CHECK(mq_theme_load(q, "NONE.THM") == -1 && !q->theme.loaded);
    n = build_thm(b, false);
    b[4] = 4; /* another version (a newer one) */
    CHECK(put_file("/CALC/THEMES/NEW.THM", b, n) == 0);
    CHECK(mq_theme_load(q, "NEW.THM") == -2);
    /* a pack made by tools/make_theme.py from the built-in cast (make mq-tools-test sets
     * MQ_SAMPLE_THM): it loads, and every pixel shows the built-in art's colour */
    const char *sample = getenv("MQ_SAMPLE_THM");
    if (sample) {
        static uint8_t big[256 * 1024];
        FILE *f = fopen(sample, "rb");
        CHECK(f != NULL);
        size_t len = f ? fread(big, 1, sizeof big, f) : 0;
        if (f)
            fclose(f);
        CHECK(put_file("/CALC/THEMES/PASTEL.THM", big, len) == 0);
        CHECK(mq_theme_load(q, "PASTEL.THM") == 0 && q->theme.nspr == MQ_NCAST * EX_COUNT && q->theme.cw == ART_BIG);
        /* the sheet's faces as animations (gen_mq_art.py SHEET_ANIM): idle, blink, look 0/1, talk, happy, pout, wow, think */
        static const uint8_t an_of[EX_COUNT][2] = {{AN_IDLE, 0}, {AN_BLINK, 0}, {AN_LOOK, 0}, {AN_LOOK, 1}, {AN_TALK, 0},
                                                   {AN_HAPPY, 0}, {AN_POUT, 0}, {AN_WOW, 0}, {AN_THINK, 0}};
        int diff = 0, seen = 0;
        for (int i = 0; i < q->theme.nspr; i++) {
            int slot = q->theme.role[i], ex = -1;
            for (int e = 0; e < EX_COUNT; e++)
                if (an_of[e][0] == q->theme.anim[i] && an_of[e][1] == q->theme.frame[i])
                    ex = e;
            CHECK(ex >= 0 && slot < MQ_NCAST);
            if (ex < 0 || slot >= MQ_NCAST)
                continue;
            seen++;
            if (ex == EX_IDLE)
                CHECK(!strcmp(q->theme.names[i], mq_art_cast_name[slot]));
            uint8_t tp[ART_BIG * ART_BIG];
            memcpy(tp, mq_theme_px(q, i), sizeof tp);
            const uint8_t *ap = mq_cast_px(q, slot, ex, false);
            for (int k = 0; k < ART_BIG * ART_BIG; k++) {
                int a = ap[k], t = tp[k];
                diff += (a == 0) != (t == 0) || (a && mq_art_pal[a] != q->theme.pal[t]);
            }
        }
        CHECK(diff == 0 && seen == MQ_NCAST * EX_COUNT);
        printf("test_mathquest: sample theme %s checked\n", sample);
    }
    /* the setting survives a new bind, and a missing card falls back */
    con("theme TEST.THM");
    mq_save_now(q);
    fresh_module();
    CHECK(q->theme.loaded && !strcmp(q->save.theme, "TEST.THM"));
    hal_sd_sim_insert(0);
    fresh_module();
    CHECK(!q->theme.loaded && q->theme_err == -1 && !strcmp(mq_cast_name(q, 2), "Momo"));
}



/* ------------------------------------------------------------ no repeats, no give-aways */
static void test_no_repeats(void)
{
    con("new Rae 2");
    mq_prob_t p;
    /* 1000 problems per skill and difficulty: never the same problem twice in a row, and
     * (where the skill has room for it) never one of the last 3 */
    for (int sk = 0; sk < MQ_NSKILL; sk++) {
        if (sk == S_FACTS)
            continue;
        for (int d = 0; d < mq_rungs(sk); d += mq_rungs(sk) > 1 ? mq_rungs(sk) - 1 : 1) {
            q->rng = (uint32_t)sk * 977 + (uint32_t)d + 5;
            memset(q->hist_id, 0, sizeof q->hist_id);
            memset(q->hist_look, 0, sizeof q->hist_look);
            uint32_t last[3] = {0, 0, 0}, seen[16];
            int back2back = 0, near = 0, nseen = 0;
            for (int i = 0; i < 1000; i++) {
                mq_generate_fresh(q, &p, sk, d);
                uint32_t id = mq_prob_ident(&p);
                bool known = false;
                for (int k = 0; k < nseen; k++)
                    known |= seen[k] == id;
                if (!known && nseen < 16)
                    seen[nseen++] = id;
                back2back += id == last[0];
                near += id == last[1] || id == last[2];
                last[2] = last[1], last[1] = last[0], last[0] = id;
            }
            bool tiny = nseen < 6; /* a handful of possible problems: they alternate */
            if (back2back || (near > 20 && !tiny))
                fprintf(stderr, "skill %d (%s) d%d: %d back to back, %d within 3\n", sk, mq_skill[sk].name, d, back2back,
                        near);
            CHECK(back2back == 0);
            CHECK(near <= 20 || tiny);
        }
    }
    /* 6 x 7 then 7 x 6 count as the same */
    mq_prob_t a, b;
    memset(&a, 0, sizeof a), memset(&b, 0, sizeof b);
    a.skill = b.skill = S_GROUPS, a.ans = b.ans = 42;
    strcpy(a.text[0], "6 rows of 7"), strcpy(b.text[0], "7 rows of 6");
    a.v[0] = 6, a.v[1] = 7, b.v[0] = 7, b.v[1] = 6;
    CHECK(mq_prob_ident(&a) == mq_prob_ident(&b));
    /* the similar problem after a mistake is similar, never the same */
    for (int k = 0; k < 40; k++) { /* (the helper friend's walkthrough: a similar order with fresh numbers) */
        con("skill 0");
        uint32_t before = mq_prob_ident(&q->prob);
        con("wrong");
        CHECK(q->scene == SC_PLAY && q->sub == GS_HELP && q->ex.skill == S_GROUPS && mq_prob_ident(&q->ex) != before);
    }
    /* facts: a fact (or its twin 7 x 3) never comes back within the next 3, even when missed */
    con("new Sol 2");
    mq_profile_t *pr = mq_prof(q);
    int keys[2000], n = 0;
    for (int i = 0; i < 2000; i++) {
        int f = mq_fact_next(q), fa, fb2;
        bool div;
        mq_fact_decode(f, &fa, &fb2, &div);
        keys[n++] = div ? 1000 + f : (fa < fb2 ? fa * 13 + fb2 : fb2 * 13 + fa);
        mq_fact_record(q, f, i % 4 != 0, i % 3 ? 1500 : 4000); /* every 4th missed */
        if (i % 20 == 19)
            pr->round++;
    }
    int close = 0;
    for (int i = 1; i < n; i++)
        close += keys[i] == keys[i - 1] || (i > 1 && keys[i] == keys[i - 2]) || (i > 2 && keys[i] == keys[i - 3]);
    CHECK(close == 0);
}

/* the question's model never makes the answer countable, except where counting or reading
 * the model is the skill (VR_MAY) or at the ladder's concrete rungs (mq_rung_concrete: rungs 0-1,
 * not where counting replaces the skill); and it never prints the answer itself */
static char ga_ans[24];
static int ga_said;
static int ga_text(const font_t *f, int x, int y, const char *s, uint16_t fg, int bg)
{
    if (ga_ans[0] && !strcmp(s, ga_ans))
        ga_said++;
    return modx_api.text(f, x, y, s, fg, bg);
}
static void test_no_giveaway(void)
{
    con("new Uma 2");
    mq_prob_t p;
    int bad = 0, may = 0, said = 0;
    static modx_api_t ga_api;
    ga_api = modx_api, ga_api.text = ga_text;
    for (int sk = 0; sk < MQ_NSKILL; sk++) {
        int vr = mq_skill[sk].visual_may_reveal;
        may += vr == VR_MAY;
        for (int d = 0; d < mq_rungs(sk); d++)
            for (int i = 0; i < 60; i++) {
                q->rng = (uint32_t)(sk * 4 + d) * 131 + (uint32_t)i;
                mq_generate(q, &p, sk, d);
                q->vis_q = true;
                bool hides = mq_visual_hides(q, &p);
                const modx_api_t *api0 = q->api;
                ga_said = 0, ga_ans[0] = 0;
                if (!hides && vr != VR_MAY && p.kind == K_NUM && p.vis != V_COINS) { /* (a countable picture: never the answer printed; coin faces carry their values) */
                    snprintf(ga_ans, sizeof ga_ans, "%ld", (long)p.ans);
                    for (int t = 0; t < 3; t++) /* (unless the order itself says that number: a given) */
                        for (const char *c = p.text[t]; *c; c++)
                            if ((c == p.text[t] || c[-1] < '0' || c[-1] > '9') && !strncmp(c, ga_ans, strlen(ga_ans)) &&
                                (c[strlen(ga_ans)] < '0' || c[strlen(ga_ans)] > '9'))
                                ga_ans[0] = 0;
                }
                q->api = &ga_api;
                mq_visual(q, &p, 6, MQ_Y0 + 68, 192, 82, 1000);
                q->api = api0;
                q->vis_q = false;
                if (ga_said && said++ < 5)
                    fprintf(stderr, "skill %d (%s) d%d: the question's picture prints the answer %s\n", sk, mq_skill[sk].name, d, ga_ans);
                CHECK(hides == !(vr == VR_MAY || mq_rung_concrete(sk, d)));
                if (!hides || p.kind == K_CHOICE)
                    continue;
                if (q->vis_units && q->vis_units == p.ans) {
                    if (bad++ < 5)
                        fprintf(stderr, "skill %d (%s) d%d: the question shows %d countable things = the answer\n", sk,
                                mq_skill[sk].name, d, q->vis_units);
                }
                /* and the full model (feedback, worked example) still shows everything */
            }
    }
    CHECK(bad == 0 && said == 0);
    CHECK(may >= 20 && mq_skill[S_GROUPS].visual_may_reveal == VR_NO);
    /* the concrete rungs: 0 and 1 (not the facts, area by multiplying, the multi-digit operations) */
    CHECK(mq_rung_concrete(S_GROUPS, 0) && mq_rung_concrete(S_GROUPS, 1) && !mq_rung_concrete(S_GROUPS, 2));
    CHECK(!mq_rung_concrete(S_FACTS, 0) && !mq_rung_concrete(S_AREAMUL, 0) && !mq_rung_concrete(S_MULTI, 0));
    /* the classic give-aways, one by one: sharing seeds into pots (a division) */
    FIND(p, S_SHARE, 2, 1);
    q->vis_q = true;
    mq_visual(q, &p, 6, MQ_Y0 + 68, 192, 82, 1000);
    CHECK(q->vis_units == 0);
    q->vis_q = false;
    mq_visual(q, &p, 6, MQ_Y0 + 68, 192, 82, 1000);
    CHECK(q->vis_units == p.v[0] * p.v[1]); /* after the answer: the seeds in the pots */
    /* area by multiplying: no unit squares; counting squares stays a counting skill */
    FIND(p, S_AREAMUL, 1, 1);
    q->vis_q = true;
    mq_visual(q, &p, 6, MQ_Y0 + 68, 192, 82, 1000);
    CHECK(q->vis_units == 0);
    FIND(p, S_AREACOUNT, 1, 1);
    mq_visual(q, &p, 6, MQ_Y0 + 68, 192, 82, 1000);
    CHECK(q->vis_units == p.ans);
    /* multiplication: dots at the first rungs (single digits: counting is how it starts), never
     * past them (the stage shows the groups with labels) */
    FIND(p, S_GROUPS, 0, 1);
    mq_visual(q, &p, 6, MQ_Y0 + 68, 192, 82, 1000);
    CHECK(q->vis_units > 0);
    FIND(p, S_GROUPS, 2, 1);
    mq_visual(q, &p, 6, MQ_Y0 + 68, 192, 82, 1000);
    CHECK(q->vis_units == 0);
    q->vis_q = false;
}


/* ------------------------------------------------------------ kid screens: a game, not school */
/* Everything a kid screen shows goes through these wrappers of the API table: text, toasts,
 * soft keys, message boxes; drawing above the body (the status bar) is counted. */
static modx_api_t kid_api;
static int kid_bad, kid_above, kid_strings;
static const char *const banned[] = {"math", "maths", "learn", "learning", "lesson", "lessons", "homework", "grade",
                                     "grades", "score", "correct", "incorrect", "practice", "quiz", "test", "tests",
                                     "fact", "facts", "skill", "skills", "problem", "problems", "level", "levels",
                                     "standard", "standards", "mistake", "mistakes", "drill", "smart", "worksheet",
                                     "review", "reviews", "revise", "refresh", "remember", "prime", "composite",
                                     "borrowing", "misconception", "cuddle", "cuddles", "cuddling"}; /* (cuddle: the user's word list) */

static void kid_check(const char *s)
{
    if (!s || !s[0])
        return;
    kid_strings++;
    for (const char *p = s; *p; p++) {
        if (p > s && ((p[-1] | 32) >= 'a' && (p[-1] | 32) <= 'z'))
            continue; /* words start at a boundary */
        for (size_t b = 0; b < sizeof banned / sizeof banned[0]; b++) {
            size_t n = strlen(banned[b]), k = 0;
            while (k < n && p[k] && (p[k] | 32) == banned[b][k])
                k++;
            if (k == n && !((p[n] | 32) >= 'a' && (p[n] | 32) <= 'z')) {
                if (kid_bad++ < 8)
                    fprintf(stderr, "kid screen %s says \"%s\" (%s)\n", mq_scene_name(q), s, banned[b]);
            }
        }
        /* nothing is ever locked: no gate words */
        static const char *const gate[] = {"locked", "opens with", "not open", "unlock it", "win the big finale",
                                           "visit every", "visit the stone", "first win"};
        for (size_t b = 0; b < sizeof gate / sizeof gate[0]; b++) {
            size_t n = strlen(gate[b]), k = 0;
            while (k < n && p[k] && (p[k] | 32) == gate[b][k])
                k++;
            if (k == n && kid_bad++ < 8)
                fprintf(stderr, "kid screen %s shows a lock: \"%s\" (%s)\n", mq_scene_name(q), s, gate[b]);
        }
        /* coins, not points: no score shown ("12 stars!", "Best: 40", "best 7", "30 points") -- a game
         * ends on the coins it earned, a best is a trophy */
        { /* (a story's stars are things to count: "3 stars, then 1 more" is fine; "12 stars!" alone is a score) */
            if (!strncasecmp(p, "best", 4)) {
                const char *t = p + 4;
                while (*t == ':' || *t == ' ')
                    t++;
                if (*t >= '0' && *t <= '9' && kid_bad++ < 8)
                    fprintf(stderr, "kid screen %s shows a best as a number: \"%s\"\n", mq_scene_name(q), s);
            }
            if (*p >= '0' && *p <= '9' && (p == s || p[-1] < '0' || p[-1] > '9')) {
                const char *t = p;
                while (*t >= '0' && *t <= '9')
                    t++;
                bool score = false;
                if (!strncasecmp(t, " stars", 6))
                    score = p == s && (!t[6] || !strcmp(t + 6, "!"));
                if (!strncasecmp(t, " point", 6) || !strncasecmp(t, " pts", 4))
                    score = true;
                if (score && kid_bad++ < 8)
                    fprintf(stderr, "kid screen %s shows a score: \"%s\"\n", mq_scene_name(q), s);

            }
        }
        /* a version-like number (1.2.3) or a build id */
        if (*p >= '0' && *p <= '9') {
            int dots = 0;
            const char *t = p;
            while ((*t >= '0' && *t <= '9') || (*t == '.' && t[1] >= '0' && t[1] <= '9'))
                dots += *t++ == '.';
            if (dots >= 2 && kid_bad++ < 8)
                fprintf(stderr, "kid screen %s shows a version-like number: \"%s\"\n", mq_scene_name(q), s);
        }
    }
}
static int kid_below;
static int kid_text(const font_t *f, int x, int y, const char *s, uint16_t fg, int bg)
{
    if (y < MQ_Y0 && kid_above++ < 4)
        fprintf(stderr, "kid screen %s writes above the status bar: \"%s\" at %d\n", mq_scene_name(q), s, y);
    if (y + f->h > MQ_Y0 + MQ_H && kid_below++ < 4) /* (the soft keys' bar is the core's: the user, "the bottom buttons are painted over") */
        fprintf(stderr, "kid screen %s writes over the soft keys: \"%s\" at %d\n", mq_scene_name(q), s, y);
    kid_check(s);
    return modx_api.text(f, x, y, s, fg, bg);
}
static void kid_fill(int x, int y, int w, int h, uint16_t c)
{
    if (y + h > MQ_Y0 + MQ_H && h > 0 && w > 0 && kid_below++ < 4)
        fprintf(stderr, "kid screen %s fills over the soft keys: %d,%d %dx%d\n", mq_scene_name(q), x, y, w, h);
    if (y < MQ_Y0 && h > 0 && w > 0 && kid_above++ < 4)
        fprintf(stderr, "kid screen %s fills above the status bar: %d,%d %dx%d (%d/%d/%d)\n", mq_scene_name(q), x, y, w, h, q->chap_slot, q->chap_step, q->chap_line);
    modx_api.fill_rect(x, y, w, h, c);
}
static void kid_toast(const char *s) { kid_check(s); }
static void kid_message(const char *t, const char *s) { kid_check(t), kid_check(s); }
static void kid_softkey(int i, const char *s) { kid_check(s), modx_api.softkey(i, s); }

static void kid_draw(void)
{
    int r[4];
    ops.draw(q, 0, r);
}

static void test_kid_screens(void)
{
    kid_api = modx_api;
    kid_api.text = kid_text, kid_api.fill_rect = kid_fill, kid_api.toast = kid_toast;
    kid_api.message = kid_message, kid_api.softkey = kid_softkey;
    memset(q, 0, mq_module_rw_size);
    memset(&ops, 0, sizeof ops);
    CHECK(mq_module_init(&kid_api, q, &ops) == 0);
    kid_bad = kid_above = kid_strings = 0;
    con("seed 3");
    con("go title");
    kid_draw();
    con("new Kai 2");
    con("grant bosses");
    static const char *const scenes[] = {"name", "map", "arcade", "room", "shop", "book", "wardrobe", "showoff",
                                         "stickers", "surprise", "complete", "break", "offer"};
    for (size_t i = 0; i < sizeof scenes / sizeof scenes[0]; i++) {
        char b[32];
        snprintf(b, sizeof b, "go %s", scenes[i]);
        con(b);
        kid_draw();
    }
    for (int t = 0; t < 6; t++) { /* every shop tab and an item up close */
        mq_home_press(q, HB_TAB, t);
        kid_draw();
    }
    mq_home_press(q, HB_CARD, IT_OVEN);
    kid_draw();
    mq_home_press(q, HB_BUY, IT_OVEN); /* "not enough coins" */
    for (int s = 0; s < 11; s++) { /* every book page */
        con("go book");
        q->book_set = (uint8_t)s;
        kid_draw();
    }
    con("gift");
    con("go surprise");
    kid_draw();
    for (int w = 0; w < MQ_NWORLD; w++) { /* worlds, their stones, the finale's intro */
        char b[32];
        snprintf(b, sizeof b, "world %d", w);
        con(b);
        kid_draw();
        mq_prof(q)->sk[mq_levels[w][0] - 1].plays = 0;
        q->level = 0, q->sub = 0;
        con("go intro");
        kid_draw();
        q->sub = 1;
        kid_draw();
    }
    for (int sk = 0; sk < MQ_NSKILL; sk++) { /* every kind of question, a miss, the trick, the celebration */
        char b[32];
        snprintf(b, sizeof b, "skill %d 1", sk);
        con(b);
        kid_draw();
        con("correct");
        kid_draw();
        con("step 40");
        con("wrong");
        kid_draw();
        con("next");
        con("wrong");
        con("next");
        kid_draw(); /* the offer */
        con("showme");
        for (int i = 0; i < 8 && q->sub == GS_TRICK; i++) {
            kid_draw();
            con("next");
        }
        for (int m = 0; m < ME_COUNT; m++) { /* the order told in every game it fits */
            char g[16];
            snprintf(b, sizeof b, "skill %d %d", sk, m % 4);
            con(b);
            snprintf(g, sizeof g, "mech %d", m);
            if (con(g) == 0) {
                con("step 30");
                kid_draw();
                con("try"); /* a dud: the friend's line */
                kid_draw();
                con("try");
                kid_draw();
            }
        }
    }
    CHECK(q->scene == SC_PLAY);
    con("level 0 0");
    for (int i = 0; i < 12 && q->scene == SC_PLAY; i++)
        con("correct"), cheer_out();
    kid_draw(); /* the celebration */
    for (int g = 0; g < FG_COUNT; g++) { /* the arcade games, to the end */
        char b[32];
        snprintf(b, sizeof b, "game %d", g);
        con(b);
        for (int i = 0; i < 60 && q->scene == SC_FACTGAME; i++) {
            con("correct");
            con("step 5");
            kid_draw();
            if (mq_fg_finished(q))
                break;
        }
        kid_draw();
    }
    for (int t = 0; t < TOY_COUNT; t++) { /* the toy box: every toy, its celebrations */
        mq_toys_open(q, t);
        con("step 60");
        kid_draw();
        mq_toys_press(q, 4), mq_toys_press(q, 5), mq_toys_press(q, 4);
        kid_draw();
    }
    for (int g = 0; g < ACT_COUNT; g++) { /* the action games: playing, their end (a new best, then not) */
        char b[16];
        snprintf(b, sizeof b, "act %d", g);
        for (int k = 0; k < 2; k++) {
            con(b);
            con("step 300");
            kid_draw();
            con("step 1000");
            kid_draw();
        }
    }
    for (int k = 0; k < 12; k++) { /* every daily quest there is */
        mq_home(q)->quest[0] = (uint8_t)k;
        kid_check(mq_quest_text(q, 0, NULL, NULL));
    }

    mq_prof(q)->stars = 200; /* the town, the quests, the unlock banners */
    con("go map");
    for (int i = 0; i < 12; i++) {
        q->earn_msg[0] = 0;
        con("step 2");
        kid_draw();
    }
    for (int i = 0; i < 3; i++)
        kid_check(mq_quest_text(q, i, NULL, NULL));
    for (int s = 0; s < MQ_NCAST; s++) /* every friend's story, every line, the payoffs */
        for (int k = 0; k < 4; k++) {
            char b[24];
            snprintf(b, sizeof b, "chapter %d", s);
            con(b);
            for (int l = 0; l < mq_chapter_lines() && q->scene == SC_CHAPTER; l++) {
                con("step 40");
                kid_draw();
                mq_chapter_next(q);
            }
        }
    for (int d = 3; d < 30; d++) { /* every special in town, every twist in a game */
        char b[16];
        snprintf(b, sizeof b, "day %d", d);
        con(b);
        con("go map");
        con("step 20");
        kid_draw();
    }
    { /* every special by name: in town (each visitor too), and over a game */
        static const char *evs[] = {"visitor 3", "visitor 4", "visitor 5", "visitor 6", "visitor 7", "lantern", "bubbles", "parade",
                                    "snow", "rainbow", "picnic", "kites", "birthday 1", "stars"};
        for (size_t i = 0; i < sizeof evs / sizeof evs[0]; i++) {
            char b[32];
            con("go map");
            snprintf(b, sizeof b, "event %s", evs[i]);
            CHECK(con(b) == 0);
            con("town 200"), con("step 20"), kid_draw();
            con("town 560"), con("step 20"), kid_draw();
            con("level 0 0"), con("step 30"), kid_draw();
        }
        con("event none");
    }
    con("opengames"); /* the new games, played */
    con("skill 0 1"), con("mech 14"), con("step 10"), kid_draw(), con("correct"), con("step 5"), kid_draw();
    con("skill 45 1"), con("mech 15"), con("step 10"), kid_draw(), con("correct"), con("step 5"), kid_draw();
    for (int m = 1; m < MOD_COUNT; m++) {
        char b[16];
        con("level 0 0");
        snprintf(b, sizeof b, "twist %d", m);
        con(b);
        con("step 10");
        kid_draw();
    }
    con("merge open"); /* the Sparkle Kitchen, its hints and thank-yous */
    kid_draw();
    con("merge gen"), con("step 10");
    kid_draw();
    mq_merge_put(q, 0), mq_merge_give(q), mq_merge_give(q), mq_merge_give(q), con("step 5");
    kid_draw();
    mq_merge_cafe(q, 1), con("merge demo off"), con("merge order 5 8"), mq_merge_gen(q, 0);
    for (int c = 0; c < KC_CELLS; c++)
        mq_extra(q)->board[c] = (uint8_t)(c % 2 ? CK_CODE(CK_PINK, 1 << c % 4) : CK_CODE(CK_LEMON, 3 + 3 * (c % 2)));
    kid_draw();
    mq_merge_gen(q, 0), mq_merge_move(q, 0, 2), mq_merge_move(q, 1, 3), mq_merge_put(q, 5), mq_merge_give(q);
    kid_draw();
    mq_merge_help(q);
    kid_draw();
    for (int s = 0; s < MQ_NCAST; s++) /* and what they say in town */
        for (int k = 0; k < 12; k++) {
            char line[48];
            mq_chatter(q, s, k, line, sizeof line);
            kid_check(line);
            CHECK(q->api->text_width(q->api->font_small, line) <= 300);
        }
    int b0 = kid_bad; /* the scanner itself (quietly): two school words and a version number */
    kid_bad = 100;
    kid_check("Mistakes help us learn");
    kid_check("module 0.1.0");
    kid_check("Pip's facetious factory"); /* inside other words: fine */
    CHECK(kid_bad == 103);
    kid_bad = b0;
    CHECK(kid_strings > 500);
    CHECK(kid_bad == 0);
    CHECK(kid_above == 0); /* the game draws nothing over the status bar */
    CHECK(kid_below == 0); /* ... nor over the soft keys */
    fresh_module();
}


/* ------------------------------------------------------------ fun beats and squishies */
/* Catch the Stars: skip-counting with the next number as the star to catch. A round is a long run of the count
 * (the user: "ends too quickly to really practice the skill"): 10-12 catches from a varied start, past ten times
 * the step, counted back now and then from grade 3; it never speeds up, a wrong star bounces off gently (nothing
 * lost), the next number is falling most of the time, and the round is maths time: it lasts until the count is
 * done whatever the fun budget, and ends by itself when left alone. */
static void test_catch_stars(void)
{
    int grade0 = mq_pace(q)->start_grade;
    con("fun beat catch");
    CHECK(q->scene == SC_FUN && q->beat == BEAT_CATCH && q->bub_step >= 2);
    CHECK(q->catch_len >= 10 && q->catch_len <= 12 && q->beat_len >= 60000);
    { /* only the next number of the count is caught; the one passed, the ones on and those between bounce off */
        q->beat_score = 2;
        int next = mq_catch_next(q), k = q->bub_step;
        q->fo[0] = (mq_fobj_t){160, MQ_Y0 + 160, 11, 0, 1}, q->fo[0].v = (int16_t)next;
        q->fo[1] = (mq_fobj_t){160, MQ_Y0 + 160, 11, 0, 1}, q->fo[1].v = (int16_t)(next + 1);
        q->fo[2] = (mq_fobj_t){160, MQ_Y0 + 160, 11, 0, 1}, q->fo[2].v = (int16_t)(next + 2 * k);
        for (int i = 3; i < 12; i++)
            q->fo[i].on = 0;
        q->basket_x = 160;
        con("step 3");
        CHECK(!q->fo[0].on && q->beat_score == 3 && q->fo[1].wob && q->fo[2].wob);
        CHECK(mq_catch_next(q) == next + q->catch_dir * k); /* (the count goes on from where she is) */
    }
    { /* the next number is never long in coming (a star with it falling most of the time) */
        for (int i = 0; i < 12; i++)
            q->fo[i].on = 0;
        int with = 0, frames = 0;
        for (int f = 0; f < 150 && q->scene == SC_FUN; f++) {
            con("step 1");
            bool on = false;
            for (int i = 0; i < 12; i++)
                on |= q->fo[i].on && !q->fo[i].wob && q->fo[i].v == mq_catch_next(q);
            if (f >= 30)
                frames++, with += on;
        }
        CHECK(frames > 50 && with * 10 >= frames * 6);
    }
    con("go map");
    /* whole rounds played by a steady catcher, the fun budget empty: every round counts on (or back) through
     * 10-12 numbers past ten times the step, at one fall speed, and ends when the count is done */
    int back = 0, starts[16] = {0}, nstart = 0, secs_min = 999;
    for (int g = 2; g <= 3; g++) {
        mq_pace(q)->start_grade = (uint8_t)g;
        for (int r = 0; r < 12; r++) {
            q->fun_ms = 0;
            con("fun beat catch");
            int k = q->bub_step, len = q->catch_len, dir = q->catch_dir, from = q->catch_from;
            CHECK(len >= 10 && len <= 12 && (dir == 1 || dir == -1));
            CHECK(dir > 0 ? from + len + 1 >= 11 : from >= 11 && from - len - 1 >= 1); /* (past ten times; never 0) */
            CHECK(g >= 3 || dir > 0); /* (counting back from grade 3) */
            back += dir < 0;
            bool seen = false;
            for (int i = 0; i < nstart; i++)
                seen |= starts[i] == from * dir;
            if (!seen && nstart < 16)
                starts[nstart++] = from * dir;
            int frames = 0, caught = 0, wrong_tries = 0;
            int16_t y0[12];
            uint8_t on0[12];
            for (; frames < 4000 && q->scene == SC_FUN; frames++) {
                int next = mq_catch_next(q), best = -1;
                for (int i = 0; i < 12; i++)
                    if (q->fo[i].on && !q->fo[i].wob && q->fo[i].v == next && (best < 0 || q->fo[i].y > q->fo[best].y))
                        best = i;
                int score = q->beat_score;
                if (frames % 97 == 50 && next) { /* now and then a wrong star under the basket: gentle */
                    for (int i = 0; i < 12; i++)
                        if (q->fo[i].on && !q->fo[i].wob && q->fo[i].v != next && q->fo[i].y > MQ_Y0 + 140) {
                            ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(2, q->fo[i].x, MQ_Y0 + 150)), wrong_tries++;
                            break;
                        }
                } else if (best >= 0) /* (her finger on the basket) */
                    ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(2, q->fo[best].x, MQ_Y0 + 150));
                for (int i = 0; i < 12; i++)
                    y0[i] = q->fo[i].y, on0[i] = q->fo[i].on;
                con("step 1");
                for (int i = 0; i < 12; i++)
                    if (on0[i] && q->fo[i].on && q->fo[i].y != y0[i])
                        CHECK(q->fo[i].y - y0[i] == 2); /* (one speed: it never speeds up) */
                CHECK(q->beat_score == score || q->beat_score == score + 1);
                caught += q->beat_score > score;
            }
            CHECK(caught == len && q->scene != SC_FUN);
            int secs = frames * MQ_FRAME_MS / 1000;
            secs_min = secs < secs_min ? secs : secs_min;
            CHECK(secs >= 20 && secs <= 90);
            (void)k, (void)wrong_tries;
            con("go map");
        }
        CHECK(g < 3 || back >= 2);
    }
    CHECK(nstart >= 5); /* (the start varies) */
    printf("test_mathquest: Catch the Stars -- a steady catcher's round at least %d s, %d starts, %d counted back\n", secs_min,
           nstart, back);
    { /* left alone, the round ends by itself (even with stars falling into the still basket now and then) */
        con("fun beat catch");
        int f = 0;
        for (; f < 2000 && q->scene == SC_FUN; f++)
            con("step 1");
        CHECK(q->scene != SC_FUN && f * MQ_FRAME_MS >= 24000 && f * MQ_FRAME_MS <= 30000);
        con("go map");
    }
    mq_pace(q)->start_grade = (uint8_t)grade0;
}

static void test_fun(void)
{
    con("new Fay 2");
    q->fun_ms = 0, q->fun_acc = 0;
    /* about 10 s of fun per 90 s of maths (the default 10 %: the playtest, "too many games that are not
     * maths"), capped, spent by fun scenes */
    CHECK(mq_fun_pct(mq_prof(q)) == 10 && MQ_FUN_PCT == 10);
    for (int i = 0; i < 90; i++)
        mq_fun_time(q, 1000, true);
    CHECK(q->fun_ms >= 9900 && q->fun_ms <= 10100);
    for (int i = 0; i < 1000; i++)
        mq_fun_time(q, 1000, true);
    CHECK(q->fun_ms == 20000); /* never hoarded */
    mq_fun_time(q, 6000, false);
    CHECK(q->fun_ms == 14000);
    con("fun pct 50"); /* the grown-ups' share */
    q->fun_ms = 0, q->fun_acc = 0;
    mq_fun_time(q, 10000, true);
    CHECK(q->fun_ms == 10000);
    con("fun pct 10");
    /* the beats are short (8-10 s) and most carry numbers (bubbles, the stars of a times table) */
    {
        int maths = 0, all = 0;
        for (int k = 0; k < 24; k++) {
            q->fun_ms = 20000, q->since_beat = 3;
            if (!mq_fun_maybe(q, FUN_RUN_END, FR_MAP))
                continue;
            all++, maths += q->beat == BEAT_BUBBLES || q->beat == BEAT_CATCH;
            CHECK(q->beat_len <= 10000 || q->scene != SC_FUN || q->beat == BEAT_CATCH); /* (the catch: a long count) */
            con("go map");
        }
        CHECK(all >= 20 && maths * 2 >= all);
        con("go map");
    }
    test_catch_stars();
    /* beats: not without budget, never two in a row, rotating */
    q->fun_ms = 5000, q->since_beat = 9;
    CHECK(!mq_fun_maybe(q, FUN_STREAK, FR_MAP));
    q->fun_ms = 30000, q->since_beat = 0;
    CHECK(!mq_fun_maybe(q, FUN_STREAK, FR_MAP));
    int seen[BEAT_COUNT] = {0}, last = -1, repeats = 0;
    for (int k = 0; k < 30; k++) {
        q->fun_ms = 30000, q->since_beat = 3;
        CHECK(mq_fun_maybe(q, k % 2 ? FUN_STREAK : FUN_RUN_END, FR_MAP));
        CHECK(q->scene == SC_FUN || q->scene == SC_WARDROBE || q->scene == SC_ROOM || q->scene == SC_TOYS);
        repeats += q->beat == last;
        last = q->beat;
        seen[q->beat]++;
        CHECK(!mq_fun_maybe(q, FUN_STREAK, FR_MAP)); /* right after a beat: no second one */
        con("go map");
    }
    int kinds = 0;
    for (int b = 0; b < BEAT_COUNT; b++)
        kinds += seen[b] > 0;
    CHECK(repeats == 0 && kinds >= 5);
    CHECK(con("fun beat pet") != 0); /* (and no console way to it) */
    CHECK(seen[BEAT_GONE0] == 0); /* pet time is gone (the user: "kind of lame") */
    /* in a run: a streak of 5 with budget brings a beat; it ends by itself and the run goes on */
    con("level 0 0");
    q->fun_ms = 30000;
    for (int i = 0; i < 5; i++) {
        con("correct");
        cheer_out();
    }
    CHECK(q->scene == SC_FUN || (q->scene == SC_ROOM && q->beat == BEAT_PLACE)); /* (the ladder's present: placing it) */
    int guard = 0;
    while ((q->scene == SC_FUN || q->scene == SC_ROOM) && guard++ < 1000)
        con("step 10");
    CHECK(q->scene == SC_PLAY && q->run_i == 5);
    /* a room she opened herself with the fun budget used up: nothing comes over it and she is not moved on (a long
     * stay at home is the home story's: test_mq_homestory.c) */
    con("go room");
    q->fun_ms = 2000, q->earn_msg[0] = 0, q->earn_qn = 0;
    con("step 300");
    CHECK(q->scene == SC_ROOM && !q->hook_t && !q->story_card && !q->earn_msg[0]);
    /* the shop: entered with no budget, it is the shop, buying works, nothing comes over it */
    q->fun_ms = 0;
    con("coins 300"), con("go shop"), con("step 300");
    CHECK(q->scene == SC_SHOP && !q->hook_t && q->fun_ms == 0);

    /* the squishy: stable, squishes under a finger, stretches, and slowly comes back */
    for (int kind = 0; kind < SQ_KINDS; kind++) {
        mq_squish_t s;
        memset(&s, 0, sizeof s);
        mq_squish_touch(&s, 3, 0, 0, 256);
        q->sq.kind = 0;
        con("fun beat squishy");
        s = q->sq;
        (void)kind;
        mq_squish_touch(&s, 1, s.cx + 12, s.cy, 256);
        for (int i = 0; i < 8; i++)
            mq_squish_step(&s);
        CHECK(mq_squish_strain(&s) > (4 << 8));
        mq_squish_touch(&s, 3, s.cx + 12, s.cy, 256);
        int32_t prev = 0x7FFFFFFF;
        bool calm = true;
        for (int i = 0; i < 400; i++) {
            mq_squish_step(&s);
            int32_t st = mq_squish_strain(&s);
            if (i > 40 && st > prev + 64)
                calm = false; /* settles without bouncing back up */
            prev = st;
        }
        CHECK(calm && mq_squish_strain(&s) < 256);
        /* a stretch, a pinch, then 1000 random pokes: always bounded, and it rests again */
        mq_squish_touch(&s, 1, s.cx + 30, s.cy, 256);
        mq_squish_touch(&s, 2, s.cx + 80, s.cy + 10, 256);
        for (int i = 0; i < 10; i++)
            mq_squish_step(&s);
        CHECK(mq_squish_strain(&s) > (10 << 8));
        mq_squish_touch(&s, 5, s.cx, s.cy, 160);
        for (int i = 0; i < 1000; i++) {
            mq_squish_touch(&s, i % 3 == 0 ? 1 : i % 3 == 1 ? 2 : 3, s.cx + mq_rnd(q, -60, 60), s.cy + mq_rnd(q, -60, 60),
                            256);
            mq_squish_step(&s);
            for (int k = 0; k < SQ_N; k++)
                CHECK(s.px[k] <= (70 << 8) && s.px[k] >= -(70 << 8));
        }
        mq_squish_touch(&s, 3, 0, 0, 256);
        for (int i = 0; i < 400; i++)
            mq_squish_step(&s);
        CHECK(mq_squish_strain(&s) < 256);
        con("go map");
    }
    /* the collection is saved; the first squishy brings its shelf */
    mq_home_t *h = mq_home(q);
    memset(h->squish, 0, sizeof h->squish);
    h->own[IT_SQUISHY_SHELF] = 0;
    mq_squish_give(h, SQ_CAT, 2, false);
    mq_squish_give(h, SQ_MOCHI, 0, true);
    CHECK(mq_squish_count(h) == 2 && h->own[IT_SQUISHY_SHELF] == 1 && mq_squish_owned(h, SQ_CAT, 2, false));
    CHECK(!mq_squish_owned(h, SQ_CAT, 2, true));
    /* the bubbles beat's light maths: count by (in order), make ten (a pair), the bigger slice; a tap
     * out of turn only wobbles */
    for (int mode = 0; mode < BB_COUNT; mode++) {
        con("fun beat bubbles");
        q->bub_mode = (uint8_t)mode, q->bub_step = 3, q->bub_next = 6, q->bub_target = 10, q->bub_pick = 0;
        for (int i = 0; i < 12; i++)
            q->fo[i].on = 0;
        q->fo[0] = (mq_fobj_t){60, MQ_Y0 + 100, 18, 0, 1, 6, 0, 0};
        q->fo[1] = (mq_fobj_t){160, MQ_Y0 + 100, 18, 1, 1, 7, 0, 0};
        q->fo[2] = (mq_fobj_t){260, MQ_Y0 + 100, 18, 2, 1, 4, 0, 0};
        if (mode == BB_BIGGER)
            q->fo[0] = (mq_fobj_t){90, MQ_Y0 + 60, 18, 0, 1, 1, 4, 0}, q->fo[1] = (mq_fobj_t){230, MQ_Y0 + 60, 18, 1, 1, 2, 3, 0},
            q->fo[2].on = 0;
        int sc = q->beat_score;
        if (mode == BB_SKIP) { /* 7 is not next: a wobble; 6 is: a pop */
            mq_fun_press(q, HB_FUNTAP, 0, 160, MQ_Y0 + 100);
            CHECK(q->fo[1].on && q->fo[1].wob && q->beat_score == sc);
            mq_fun_press(q, HB_FUNTAP, 0, 60, MQ_Y0 + 100);
            CHECK(!q->fo[0].on && q->beat_score == sc + 1 && q->bub_next == 9);
        } else if (mode == BB_PAIRS) { /* 6 then 4 make ten */
            mq_fun_press(q, HB_FUNTAP, 0, 60, MQ_Y0 + 100);
            CHECK(q->fo[0].on && q->bub_pick == 1);
            mq_fun_press(q, HB_FUNTAP, 0, 260, MQ_Y0 + 100);
            CHECK(!q->fo[0].on && !q->fo[2].on && q->beat_score == sc + 1);
        } else { /* 1/4 or 2/3: the bigger pops both */
            mq_fun_press(q, HB_FUNTAP, 0, 90, MQ_Y0 + 60);
            CHECK(q->fo[0].on && q->fo[0].wob);
            mq_fun_press(q, HB_FUNTAP, 0, 230, MQ_Y0 + 60);
            CHECK(!q->fo[0].on && !q->fo[1].on && q->beat_score == sc + 1);
        }
        for (int k = 0; k < 80; k++) /* the beat spawns its own bubbles of the game */
            con("step 1");
        int with = 0;
        for (int i = 0; i < 12; i++)
            with += q->fo[i].on && q->fo[i].v > 0;
        CHECK(q->scene != SC_FUN || with > 0);
        kid_check("Count by 3s"), kid_check("Pop two that make 10!"), kid_check("Pop the bigger slice!");
    }
    con("fun beat squishy"); /* a squishy arrives in a beat */
    CHECK(mq_squish_count(mq_home(q)) == 3 && q->scene == SC_FUN);
    /* squashed hard, let go: it rises back slowly, then wobbles and sparkles once it has risen */
    q->sq_rise_t = 0;
    ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(1, 160, MQ_Y0 + 100));
    con("step 4");
    CHECK(q->sq_squashed && !q->sq_rise_t);
    ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(3, 160, MQ_Y0 + 100));
    for (int k = 0; k < 120 && !q->sq_rise_t; k++)
        con("step 1");
    CHECK(q->sq_rise_t != 0 && mq_squish_strain(&q->sq) >> 8 <= 1);
    kid_draw();
    con("fun dough 2");
    con("fun beat maker");
    mq_fun_press(q, HB_MAKER, SQ_STAR, 0, 0);
    mq_fun_press(q, HB_MAKER, 16 + 3, 0, 0);
    mq_fun_press(q, HB_MAKER, 32, 0, 0);
    CHECK(mq_squish_owned(mq_home(q), SQ_STAR, 3, false) && mq_home(q)->dough == 0);
    int cur = q->save.cur, n = mq_squish_count(mq_home(q));
    mq_save_now(q);
    fresh_module();
    q->save.cur = (uint8_t)cur;
    CHECK(mq_squish_count(mq_home(q)) == n);
    /* the daily gift grows with days in a row; a missed day only starts again */
    h = mq_home(q);
    mq_profile_t *p = mq_prof(q);
    h->last_gift_day = 0, h->gift_streak = 0;
    for (int d = 1; d <= 7; d++) {
        p->days = (uint16_t)d;
        mq_open_gift(q);
    }
    CHECK(h->gift_streak == 7);
    p->days = 10;
    mq_open_gift(q);
    CHECK(h->gift_streak == 1 && q->gift_coins >= 10);
}


/* ------------------------------------------------------------ the learning engine */
static void test_learning(void)
{
    con("new Lia 2");
    mq_profile_t *p = mq_prof(q);
    mq_learn_t *L = mq_learn(q);
    memset(L, 0, sizeof *L);
    p->secs = 600; /* 10 minutes of play */
    /* the expiry math: a long clean streak puts a problem away for long, one right after many misses briefly */
    CHECK(mq_seen_ttl(0, 125, 1, false) == 25);
    CHECK(mq_seen_ttl(0, 125, 5, false) == 400);
    CHECK(mq_seen_ttl(100, 125, 1, false) > mq_seen_ttl(0, 125, 1, false)); /* mastery lengthens it */
    CHECK(mq_seen_ttl(0, 125, 1, true) < mq_seen_ttl(0, 125, 1, false));    /* a slow right counts less */
    CHECK(mq_seen_ttl(100, 150, 30, false) == 30000);
    /* CR-03: a long TTL still blocks (a 16-bit minute compared wrap-safe: never past 32767 ahead) */
    {
        mq_prob_t pr;
        mq_generate(q, &pr, S_ADDSUB, 1);
        mq_seen_t *e = mq_seen_record(q, &pr, true, false);
        CHECK(e != NULL);
        if (e) {
            e->exp = (uint16_t)(mq_play_min(q) + mq_seen_ttl(100, 150, 30, false));
            CHECK(mq_seen_blocked(q, mq_prob_ident(&pr)));
        }
    }
    mq_prob_t a, b;
    q->rng = 5;
    mq_generate(q, &a, S_ADDSUB, 2);
    mq_generate(q, &b, S_ADDSUB, 2);
    for (int i = 0; i < 6; i++) /* missed many times ... */
        mq_seen_record(q, &a, false, false);
    mq_seen_t *ea = mq_seen_record(q, &a, true, false); /* ... then right once */
    int ttl_a = (int16_t)(ea->exp - mq_play_min(q));
    mq_seen_t *eb = NULL;
    for (int i = 0; i < 5; i++) /* always right */
        eb = mq_seen_record(q, &b, true, false);
    int ttl_b = (int16_t)(eb->exp - mq_play_min(q));
    CHECK(ttl_a > 0 && ttl_a < 30 && ttl_b > 10 * ttl_a);
    CHECK(ea->res == 0x01 && ea->streak == 1 && eb->streak == 5 && eb->res == 0x1F);
    /* ease bounds */
    for (int i = 0; i < 40; i++)
        mq_seen_record(q, &a, false, false);
    CHECK(ea->ease == 65 && (int16_t)(ea->exp - mq_play_min(q)) == 2);
    for (int i = 0; i < 100; i++)
        mq_seen_record(q, &a, true, false);
    CHECK(ea->ease == 150);
    /* a problem answered right is not asked again until its time has come (by play time) */
    memset(L->seen, 0, sizeof L->seen);
    memset(q->hist_id, 0, sizeof q->hist_id);
    q->rng = 77;
    mq_generate_fresh(q, &a, S_ADDSUB, 2);
    mq_seen_t *e = mq_seen_record(q, &a, true, false);
    uint32_t ida = mq_prob_ident(&a);
    CHECK(mq_seen_blocked(q, ida));
    int again = 0;
    for (int i = 0; i < 400; i++) {
        mq_generate_fresh(q, &b, S_ADDSUB, 2);
        again += mq_prob_ident(&b) == ida;
    }
    CHECK(again == 0);
    p->secs += (uint32_t)((int16_t)(e->exp - mq_play_min(q)) + 1) * 60u;
    CHECK(!mq_seen_blocked(q, ida));
    /* a wrong answer does not put a problem away: it comes back soon */
    mq_seen_record(q, &b, false, false);
    CHECK((int16_t)(mq_seen_find(q, mq_prob_ident(&b))->exp - mq_play_min(q)) <= 2);
    /* a skill with only a few problems, all answered right: generation still works and alternates */
    memset(q->hist_id, 0, sizeof q->hist_id);
    uint32_t last = 0;
    int same = 0;
    for (int i = 0; i < 60; i++) {
        mq_generate_fresh(q, &a, S_UNITFRAC, 0);
        mq_seen_record(q, &a, true, false);
        same += mq_prob_ident(&a) == last;
        last = mq_prob_ident(&a);
    }
    CHECK(same == 0);
    /* eviction: room for MQ_NSEEN; the one seen longest ago goes */
    memset(L->seen, 0, sizeof L->seen);
    mq_prob_t first;
    for (int i = 0; i < MQ_NSEEN + 40; i++) {
        memset(&a, 0, sizeof a);
        a.skill = S_ADDSUB, a.ans = i;
        snprintf(a.text[0], MQ_TXT, "%d + 1", i);
        if (i == 0)
            first = a;
        p->secs += 60;
        mq_seen_record(q, &a, true, false);
    }
    int used = 0;
    for (int i = 0; i < MQ_NSEEN; i++)
        used += L->seen[i].ease != 0;
    CHECK(used == MQ_NSEEN && mq_seen_find(q, mq_prob_ident(&first)) == NULL && mq_seen_find(q, mq_prob_ident(&a)));
    /* the history survives a new bind */
    int cur = q->save.cur;
    mq_save_now(q);
    fresh_module();
    q->save.cur = (uint8_t)cur;
    CHECK(mq_seen_find(q, mq_prob_ident(&a)) != NULL && mq_learn(q)->seen[0].ease);
    p = mq_prof(q);
    L = mq_learn(q);

    /* interleaved review: the share adapts within 10..40 % */
    L->share = 0;
    CHECK(mq_review_share(q) == 15);
    mq_review_join(q, S_GROUPS, false);
    for (int i = 0; i < 20; i++)
        mq_review_answer(q, S_GROUPS, true);
    CHECK(mq_review_share(q) == 10 && L->rev[S_GROUPS].box == 7);
    p->sk[S_GROUPS].mastery = 100, p->sk[S_GROUPS].flags |= SK_PASSED;
    for (int i = 0; i < 20; i++)
        mq_review_answer(q, S_GROUPS, false);
    CHECK(mq_review_share(q) == 40 && L->rev[S_GROUPS].box == 1 && L->rev[S_GROUPS].slips == 20);
    CHECK(p->sk[S_GROUPS].mastery == 0 && !(p->sk[S_GROUPS].flags & SK_PASSED)); /* demoted step by step */
    /* due-time order: the most overdue first; not yet due: none */
    memset(L->rev, 0, sizeof L->rev);
    uint16_t now = mq_play_min(q);
    L->rev[S_PV3] = (mq_rev_t){2, 0, (uint16_t)(now - 30)};
    L->rev[S_TIME5] = (mq_rev_t){2, 0, (uint16_t)(now - 90)};
    L->rev[S_COINS] = (mq_rev_t){2, 0, (uint16_t)(now + 50)};
    CHECK(mq_review_pick(q, S_AREAMUL, false) == S_TIME5);
    L->rev[S_TIME5].due = (uint16_t)(now + 10);
    CHECK(mq_review_pick(q, S_AREAMUL, false) == S_PV3);
    L->rev[S_PV3].due = (uint16_t)(now + 10);
    CHECK(mq_review_pick(q, S_AREAMUL, false) == -1);
    /* prerequisites: struggling with division brings multiplication back, even before it is due */
    L->rev[S_GROUPS] = (mq_rev_t){3, 0, (uint16_t)(now + 200)};
    CHECK(mq_review_pick(q, S_SHARE, false) == -1);
    CHECK(mq_review_pick(q, S_SHARE, true) == S_GROUPS);
    L->rev[S_EQUIV] = (mq_rev_t){3, 0, (uint16_t)(now + 200)};
    CHECK(mq_review_pick(q, S_ADDFRAC, true) == S_EQUIV);
    /* weak problems that are due raise their skill */
    L->rev[S_COINS].due = (uint16_t)(now - 1), L->rev[S_PV3].due = (uint16_t)(now - 5);
    memset(L->seen, 0, sizeof L->seen);
    mq_generate(q, &a, S_COINS, 1);
    mq_seen_record(q, &a, false, false);
    p->secs += 300;
    CHECK(mq_review_pick(q, S_AREAMUL, false) == S_COINS);
    /* in a run: earlier skills appear among the items, about the share */
    memset(L, 0, sizeof *L);
    for (int s = 0; s < 12; s++)
        if (s != S_FACTS)
            L->rev[s] = (mq_rev_t){2, 0, (uint16_t)(mq_play_min(q) - 1)};
    int other = 0, total = 0;
    con("engine stop"); /* the stop's own skill: the review's share alone */
    for (int k = 0; k < 40; k++) {
        con("level 1 0");
        for (int i = 0; i < 5 && q->scene == SC_PLAY; i++) {
            other += q->prob.skill != S_PV3, total++;
            q->fun_ms = 0;
            con("correct");
            cheer_out();
        }
        for (int s = 0; s < 12; s++) /* keep them due */
            if (s != S_FACTS)
                L->rev[s].due = (uint16_t)(mq_play_min(q) - 1);
        L->share = 15;
    }
    CHECK(other * 100 >= total * 6 && other * 100 <= total * 30);
    con("engine on");
}

/* the save from before the learning engine (version 2) is migrated */
static void test_learn_migrate(void)
{
    con("new Mo 2");
    mq_prof(q)->sk[S_GROUPS].plays = 2, mq_prof(q)->sk[S_GROUPS].flags |= SK_PASSED;
    mq_prof(q)->sk[S_SHARE].plays = 1;
    mq_home(q)->coins = 321;
    static mq_save_v3_t s2; /* the old layout (71 skills) */
    memset(&s2, 0, sizeof s2);
    memcpy(&s2, &q->save, offsetof(mq_save_t, prof));
    for (int k = 0; k < 2; k++) {
        memcpy(&s2.prof[k], &q->save.prof[k], offsetof(mq_profile_t, sk));
        memcpy(s2.prof[k].sk, q->save.prof[k].sk, sizeof s2.prof[k].sk);
        memcpy(s2.prof[k].fact, q->save.prof[k].fact, sizeof s2.prof[k].fact);
        s2.home[k] = q->save.home[k];
    }
    s2.version = 2, s2.size = (uint16_t)MQ_SAVE_V2_SIZE;
    int cur = s2.cur;
    CHECK(modx_api.save_put("mathquest", &s2, MQ_SAVE_V2_SIZE) == 0);
    fresh_module();
    CHECK(q->save.version == MQ_SAVE_VERSION && q->save.cur == cur && mq_home(q)->coins == 321);
    CHECK(mq_prof(q)->sk[S_GROUPS].plays == 2 && !mq_prof(q)->sk[S_SHAPES5].plays && mq_prof(q)->used);
    mq_learn_t *L = mq_learn(q);
    CHECK(L->rev[S_GROUPS].box == 3 && L->rev[S_SHARE].box == 1 && !L->rev[S_PV3].box && !L->seen[0].ease);
    printf("test_mathquest: the save is %u bytes (of %u): profiles %u, homes %u, learning %u; RW block %u bytes\n",
           (unsigned)sizeof(mq_save_t), (unsigned)MODX_SAVE_MAX, (unsigned)(2 * sizeof(mq_profile_t)),
           (unsigned)(2 * sizeof(mq_home_t)), (unsigned)(2 * sizeof(mq_learn_t)), (unsigned)mq_module_rw_size);
    CHECK(sizeof(mq_save_t) <= MODX_SAVE_MAX && sizeof(mq_seen_t) == 12);
}


/* A simulated new player -- a typical 9-year-old: good at the facts and grade-3 multiplication, shaky on
 * fractions and decimals -- over her first three sessions: the placement starts at mid-grade 3 and never
 * jumps, the first orders are gentle (difficulty 0-1, grade 3 and below), the ramp is slow, and no
 * decimal or grade-5 order (no "5.4 kg into 6 baskets") comes early. */
static uint32_t np_rng = 7;
static int np_rnd(int n)
{
    np_rng = np_rng * 1103515245u + 12345u;
    return (int)((np_rng >> 16) % (uint32_t)n);
}
static void test_new_player(void)
{
    con("new Nia 2");
    q->fun_off = true;
    mq_profile_t *p = mq_prof(q);
    int late = 0, hard = 0, orders = 0, g5 = 0, first_hi = 0, sess_diff[3] = {0}, sess_n[3] = {0};
    for (int sess = 0; sess < 3; sess++) {
        p->days = (uint16_t)(sess + 1); /* a play day each */
        for (int round = 0; round < 4; round++) { /* (a session: four rounds of MQ_RUN_N orders, about 40 orders) */
            con(round % 3 == 0 ? "level 2 0" : round % 3 == 1 ? "level 0 0" : "level 6 0");
            for (int k = 0; k < 12 && q->scene == SC_PLAY; k++) {
                if (q->sub != GS_PLAY) {
                    con("step 40");
                    continue;
                }
                int s = q->prob.skill, g = mq_skill[s].grade, st = mq_strand(s);
                orders++;
                late += mq_decimal_skill(s) || g == 5;
                g5 += g == 5;
                hard += mq_rung_band(s, q->prob.diff) >= 2 && sess == 0; /* (the upper half of its ladder) */
                if (orders <= 30 && !q->placing_item) /* her first orders after the placement's probes */
                    first_hi = g > first_hi ? g : first_hi;
                if (getenv("MQ_TRACE") && orders <= 30)
                    fprintf(stderr, "new player: order %d skill %d (%s) grade %d d%d placing %d review %d\n", orders, s,
                            mq_skill[s].name, g, q->prob.diff, q->placing_item, q->review_mode);
                sess_diff[sess] += mq_rung_band(s, q->prob.diff), sess_n[sess]++;
                /* good at facts and multiplication; shaky on fractions (60 %) and decimals (40 %) */
                int pr = s == S_FACTS || st == STR_MULDIV ? (g <= 3 ? 95 : 75) : st == STR_FRAC ? 60 : g <= 3 ? 88 : 70;
                if (mq_decimal_skill(s))
                    pr = 40;
                q->now += 2500 + (uint32_t)np_rnd(4000);
                q->fun_ms = 0;
                con(np_rnd(100) < pr ? "correct" : "wrong");
                for (int j = 0; j < 60 && q->scene == SC_PLAY && (q->sub != GS_PLAY || q->gm.helped); j++) {
                    if (q->sub == GS_PLAY && q->gm.helped) {
                        con(np_rnd(100) < 70 || q->gm.hoh ? "correct" : "wrong");
                        if (q->sub == GS_CHEER)
                            q->gm.helped = 0;
                        continue;
                    }
                    con("next");
                }
            }
            mq_go(q, SC_MAP);
        }
    }
    if (getenv("MQ_TRACE"))
        fprintf(stderr, "new player: %d orders, late %d, hard in the first session %d, first-orders top grade %d, "
                        "mean difficulty by session %d/%d %d/%d %d/%d, strands %d %d %d %d\n", orders, late, hard, first_hi,
                sess_diff[0], sess_n[0], sess_diff[1], sess_n[1], sess_diff[2], sess_n[2], mq_pace(q)->strand[0],
                mq_pace(q)->strand[1], mq_pace(q)->strand[2], mq_pace(q)->strand[3]);
    CHECK(orders > 90);
    CHECK(late == 0 && g5 == 0);      /* no decimals, no grade 5 in her first three sessions */
    CHECK(hard == 0);                 /* the first session: the lower half of every ladder only */
    CHECK(first_hi <= 3);             /* her first orders: grade 3 at most */
    CHECK(sess_diff[0] * sess_n[2] <= sess_diff[2] * sess_n[0] + sess_n[0] * sess_n[2] / 2); /* a slow ramp, not a drop */
    for (int k = 0; k < STR_COUNT; k++)
        CHECK(mq_pace(q)->strand[k] <= 3);
    /* the 5.4 kg into 6 baskets kind (a decimal division) is not reachable for her */
    CHECK(!mq_skill_late_ok(q, S_DECOPS) && !mq_skill_late_ok(q, S_DECIMAL));
    /* the strand's own step down: struggling in fractions (most of the last answers there missed) lowers
     * the strand a grade at once, whatever each skill's own controller says; sailing raises it again
     * (after the first sessions) */
    mq_pace_t *pc = mq_pace(q);
    p->problems = 400;
    pc->strand[STR_FRAC] = 3, pc->str_hist[STR_FRAC] = 0;
    for (int i = 0; i < 5; i++)
        mq_engine_answered(q, S_WHOLES, i == 2 ? false : true);
    CHECK(pc->strand[STR_FRAC] == 2);
    for (int i = 0; i < 7; i++)
        mq_engine_answered(q, S_WHOLES, false);
    CHECK(pc->strand[STR_FRAC] == 3);
    /* never two grades above her strand: a grade-5 fraction skill waits */
    CHECK(mq_skill[S_FRUNLIKE].grade == 5);
    /* an assumed skill is checked gently for a new player (difficulty 1, not 2) */
    p->problems = 10;
    pc->strand[STR_PLACE] = 3, p->sk[S_PV3].tries = 0, p->sk[S_PV3].diff = 0;
    CHECK(mq_skill_assumed(q, S_PV3) && p->sk[S_PV3].tries < 3 && mq_engine_diff(q, S_PV3) <= 1);
}

/* ------------------------------------------------------------ the coach */
static void test_coach(void)
{
    con("new Cy 2");
    mq_profile_t *p = mq_prof(q);
    mq_learn_t *L = mq_learn(q);
    /* success near 85 %: a player who finds the rungs of a ladder harder and harder (right 99 % on its
     * first rung .. 60 % on its last): she settles where she is right about 85 % of the time */
    static const int pr[7] = {99, 97, 95, 92, 85, 72, 60};
    int right = 0, n = 0;
    q->rng = 9;
    p->problems = 1000; /* (a player past her first sessions: a new one errs easy, test_new_player) */
    for (int i = 0; i < 800; i++) {
        q->prob.skill = S_ADDSUB, q->prob.diff = p->sk[S_ADDSUB].diff;
        bool ok = mq_rnd(q, 0, 99) < pr[p->sk[S_ADDSUB].diff];
        mq_coach_answer(q, S_ADDSUB, ok, 3000);
        if (i >= 200)
            right += ok, n++;
    }
    if (right * 100 < n * 78 || right * 100 > n * 92)
        fprintf(stderr, "coach: %d of %d right on the ladder\n", right, n);
    CHECK(right * 100 >= n * 78 && right * 100 <= n * 92);
    CHECK(L->t10[S_ADDSUB] == 30);
    /* try again once: a wrong typed answer gets a second try with a hint, a wrong choice does not */
    con("skill 16 1");
    CHECK(q->prob.kind != K_CHOICE);
    con("try");
    CHECK(q->scene == SC_PLAY && q->sub == GS_PLAY && q->retrying && q->tried_wrong);
    ops.draw(q, 0, (int[4]){0}); /* the friend's line is drawn */
    con("try");
    CHECK(q->sub == GS_HELP && !q->retrying); /* the second miss: a helper friend */
    con("skill 16 1");
    q->rapid = 0; /* (the console answers instantly: not guessing here) */
    con("try");
    con("correct"); /* right on the second try: not a first try */
    CHECK(q->sub == GS_CHEER && q->run_right == 0);
    /* a sort into two bins: no second try (it would be no thinking at all) */
    con("skill 11 3");
    for (int i = 0; i < 40 && !(q->prob.kind == K_CHOICE && q->prob.nchoice == 2 && q->gm.mech == ME_SORT); i++)
        q->sub = GS_PLAY, q->tried_wrong = false, mq_generate(q, &q->prob, S_FACTORS, 3), mq_game_order(q);
    CHECK(q->gm.mech == ME_SORT && q->gm.nopt == 2);
    q->tried_wrong = false, q->rapid = 0, q->guess_n = 0;
    con("try");
    CHECK(q->sub == GS_HELP);
    /* the preview of the question screen for grown-ups: choices go straight to the feedback */
    FIND(q->prob, S_CMP3, 1, q->prob.kind == K_CHOICE);
    q->scene = SC_QUIZ, q->preview = true, q->tried_wrong = false, q->sub = 0, q->rapid = 0, q->guess_n = 0;
    con("try");
    CHECK(q->scene == SC_FEEDBACK);
    q->preview = false;
    /* faded scaffolding: full example, then a completion problem, then none */
    mq_skrec_t *s = &p->sk[S_ADDSUB];
    s->mastery = 10, s->flags = 0;
    CHECK(mq_coach_fade(q, S_ADDSUB) == 0);
    s->mastery = 50;
    CHECK(mq_coach_fade(q, S_ADDSUB) == 1);
    con("skill 16 1");
    q->rapid = 0, q->guess_n = 0;
    uint32_t own = mq_prob_ident(&q->prob);
    con("showme");
    CHECK(q->sub == GS_TRICK && q->ex_fade && mq_prob_ident(&q->ex) != own); /* (a similar example, other numbers) */
    for (int i = 0; i < 8 && q->sub == GS_TRICK; i++)
        con("next");
    /* then back to HER order, the same numbers, to solve herself (never swapped for the example: the user's "it
     * flips to another problem"; help teaches, never answers); a helped order is no evidence of mastery */
    CHECK(q->scene == SC_PLAY && q->sub == GS_PLAY && mq_prob_ident(&q->prob) == own && q->used_help);
    s->flags |= SK_PASSED;
    CHECK(mq_coach_fade(q, S_ADDSUB) == 2);
    /* mastery needs evidence on two play days */
    s = &p->sk[S_PERIM];
    memset(s, 0, sizeof *s);
    L->pass_day[S_PERIM] = 0;
    p->days = 4;
    con("skill 52 0");
    for (int i = 0; i < 7; i++) {
        q->prob.skill = S_PERIM;
        con("correct");
        cheer_out();
        if (q->scene != SC_PLAY)
            con("skill 52 0");
    }
    CHECK(s->mastery == 100 && s->right >= 5 && !(s->flags & SK_PASSED) && L->pass_day[S_PERIM] == 4);
    p->days = 5;
    con("skill 52 0");
    con("correct");
    CHECK(s->flags & SK_PASSED);
    /* guessing: quick wrong first answers earn nothing next and numbers become typed */
    q->guess_n = 0, q->rapid = 0;
    mq_coach_guess(q, false, 400);
    CHECK(!q->guess_n);
    mq_coach_guess(q, false, 300);
    CHECK(q->guess_n == 5);
    mq_prob_t c;
    memset(&c, 0, sizeof c);
    c.kind = K_CHOICE, c.nchoice = 3, c.ans = 1;
    strcpy(c.choice[0], "12"), strcpy(c.choice[1], "15"), strcpy(c.choice[2], "18");
    c.choice_mc[0] = MC_NEIGHBOR, c.choice_mc[2] = MC_NEIGHBOR;
    mq_coach_typed(q, &c);
    CHECK(c.kind == K_NUM && c.ans == 15 && c.nwrong == 2 && mq_correct(&c, 15) && !mq_correct(&c, 12));
    strcpy(c.choice[0], "<");
    c.kind = K_CHOICE, c.nchoice = 3, c.ans = 1;
    mq_coach_typed(q, &c);
    CHECK(c.kind == K_CHOICE); /* tiles that are not numbers stay */
    con("skill 16 1");
    mq_home(q)->quest_day = mq_prof(q)->days, mq_home(q)->quest_done = 7, mq_home(q)->ladder = 200; /* (quests, unlocks pay on their own) */
    int c0 = mq_home(q)->coins;
    con("correct");
    if (getenv("MQ_TRACE"))
        fprintf(stderr, "guess: coins %d -> %d, guess_n %d, sub %d mech %d\n", c0, mq_home(q)->coins, q->guess_n, q->sub, q->gm.mech);
    CHECK(mq_home(q)->coins == c0 && q->guess_n == 4);
    q->guess_n = 0;
    /* the play log: console and a CSV file on the card */
    CHECK(con("log") == 0);
    CHECK(hal_sd_sim_insert(64) == 0 && fs_mkfs() == 0);
    char path[40];
    CHECK(mq_coach_export(q, path, sizeof path) == 0 && !strcmp(path, "/CALC/MQLOG001.CSV"));
    fs_file_t f;
    char buf[256] = {0};
    CHECK(fs_open(&f, path, FS_READ) == 0);
    int got = fs_read(&f, buf, sizeof buf - 1);
    fs_close(&f);
    fs_unmount();
    CHECK(got > 100 && !strncmp(buf, "player,Cy", 9) && strstr(buf, "skill,world,grade,mastery"));
    hal_sd_sim_insert(0);
    /* weekly events by play day: the seasonal item is sold only in its week */
    p->days = 1;
    CHECK(mq_event(p) == 0 && mq_item_unlocked(p, IT_BLOSSOM_TREE) && !mq_item_unlocked(p, IT_SNOWMAN));
    p->days = 22;
    CHECK(mq_event(p) == 3 && mq_item_unlocked(p, IT_SNOWMAN));
    /* a prompt for grown-ups, a greeting with her name */
    char g[64];
    mq_coach_greet(q, g, sizeof g);
    CHECK(strstr(g, "Cy") != NULL && mq_coach_prompt(q)[0]);
}

/* colours that tell things apart stay apart for colour-blind players: the simulated
 * protanope / deuteranope views (Vienot, Brettel & Mollon 1999) of each pair still differ in
 * lightness or colour; meaning never rests on hue alone (shapes, text and positions too) */
static void cvd(const int in[3], int type, double out[3])
{
    double r = in[0] / 255.0, g = in[1] / 255.0, b = in[2] / 255.0;
    r = r * r, g = g * g, b = b * b; /* about linear */
    double L = 17.8824 * r + 43.5161 * g + 4.11935 * b, M = 3.45565 * r + 27.1554 * g + 3.86714 * b,
           S = 0.0299566 * r + 0.184309 * g + 1.46709 * b;
    if (type == 0)
        L = 2.02344 * M - 2.52581 * S;
    else
        M = 0.494207 * L + 1.24827 * S;
    out[0] = 0.0809444479 * L - 0.130504409 * M + 0.116721066 * S;
    out[1] = -0.0102485335 * L + 0.0540193266 * M - 0.113614708 * S;
    out[2] = -0.000365296938 * L - 0.00412161469 * M + 0.693511405 * S;
}
static void test_colours(void)
{
    static const int pal[][3] = {{92, 191, 138}, {255, 179, 199}, {255, 230, 128}, {217, 217, 224},
                                 {255, 122, 162}, {90, 169, 230},   {155, 127, 230}, {255, 164, 92}};
    /* fact grid: fluent green / almost yellow / learning pink / not yet grey; right green vs miss pink;
     * the price you can afford (ink) vs not (hot pink); world tokens done vs now */
    static const int pairs[][2] = {{0, 1}, {0, 2}, {1, 3}, {2, 3}, {0, 3}, {4, 5}, {6, 7}, {4, 3}};
    for (size_t k = 0; k < sizeof pairs / sizeof pairs[0]; k++)
        for (int t = 0; t < 2; t++) {
            double a[3], b[3];
            cvd(pal[pairs[k][0]], t, a);
            cvd(pal[pairs[k][1]], t, b);
            double d = sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
            if (d < 0.08)
                fprintf(stderr, "colours %zu: %d/%d too close for colour-blind type %d (%.3f)\n", k, pairs[k][0],
                        pairs[k][1], t, d);
            CHECK(d >= 0.08);
        }
}


/* ------------------------------------------------------------ the action games */
static int lane_of(int val)
{
    for (int i = 0; i < ACT_NOBJ; i++)
        if (q->act.o[i].kind && q->act.o[i].val == val)
            return q->act.o[i].lane;
    return -1;
}

static void test_action(void)
{
    /* CR-11: three different answers for every fact (0 x 2 gave two 2s) */
    for (int x = 0; x <= 12; x++)
        for (int y = 0; y <= 12; y++)
            for (int dv = 0; dv < 2; dv++) {
                if (dv && !y)
                    continue;
                int c[3];
                mq_act_options(dv ? x : x * y, x, y, dv, c);
                CHECK(c[0] == (dv ? x : x * y) && c[0] != c[1] && c[1] != c[2] && c[0] != c[2] && c[1] >= 0 && c[2] >= 0);
            }
    con("new Ivy 2");
    con("fun on");
    con("seed 31");
    /* collisions and the keypad map */
    CHECK(mq_hit(0, 0, 10, 10, 5, 5, 10, 10) && !mq_hit(0, 0, 10, 10, 10, 0, 5, 5) && !mq_hit(0, 0, 4, 4, 0, 4, 4, 4));
    CHECK(mq_hole_of_key('7') == 0 && mq_hole_of_key('9') == 2 && mq_hole_of_key('5') == 4 && mq_hole_of_key('1') == 6 &&
          mq_hole_of_key('3') == 8 && mq_hole_of_key('0') == -1);
    /* Cloud Dash: lanes, an answer row run through the right bubble, a crash slows down */
    con("act 0");
    CHECK(q->scene == SC_ACTION && q->act.lane == 1);
    ops.event(q, MODX_EV_KEY, 12 /* UP */);
    con("step 6");
    CHECK(q->act.lane == 0 && q->act.y == mq_lane_y(0));
    for (int i = 0; i < ACT_NOBJ; i++) /* a clear run for the answer row */
        q->act.o[i].kind = 0;
    q->act.pure_until = 0;
    con("step 1");
    CHECK(q->act.moment && lane_of(q->act.ans) >= 0);
    int f = q->act.fact, seen = mq_prof(q)->fact[f].seen, score = q->act.score;
    char d[8];
    snprintf(d, sizeof d, "%d", q->act.ans);
    con("act lane 0");
    int want = lane_of(q->act.ans);
    for (const char *c = d; *c; c++) /* typing the answer jumps to its lane */
        mq_action_key(q, 0, *c);
    if (q->act.lane != want) /* (a shorter option that is a prefix of it took the jump) */
        q->act.lane = (uint8_t)want;
    CHECK(q->act.lane == want);
    for (int i = 0; i < 240 && q->act.moment; i++)
        con("step 1");
    CHECK(!q->act.moment && q->act.right_last && q->act.score >= score + 5 && mq_prof(q)->fact[f].seen == seen + 1);
    /* a wrong bubble: no points, nothing lost */
    for (int i = 0; i < ACT_NOBJ; i++)
        q->act.o[i].kind = 0;
    q->act.pure_until = 0;
    con("step 1");
    int wrong = q->act.opt[0] == q->act.ans ? q->act.opt[1] : q->act.opt[0];
    q->act.lane = (uint8_t)lane_of(wrong);
    score = q->act.score;
    for (int i = 0; i < 240 && q->act.moment; i++)
        con("step 1");
    CHECK(!q->act.right_last && q->act.score < score + 5 && q->scene == SC_ACTION);
    /* a rock in her lane: a step slower, the game goes on */
    for (int i = 0; i < ACT_NOBJ; i++)
        q->act.o[i].kind = 0;
    q->act.speed = 4, q->act.pure_until = q->anim_t + 100000;
    q->act.o[0] = (mq_aobj_t){3 /* rock */, q->act.lane, 60, (int16_t)mq_lane_y(q->act.lane), 0, 0};
    con("step 3");
    CHECK(q->act.speed == 3 && q->scene == SC_ACTION);
    /* the fun budget: pure stretches spend it, answer moments fill it */
    q->act.moment = 0, q->fun_ms = 20000;
    con("step 10");
    CHECK(q->fun_ms < 20000);
    /* 45 s: the end, a personal best kept */
    q->act.score = 77;
    con("step 1200");
    CHECK(q->act.done && q->act.new_best && q->act.score >= 77 && mq_home(q)->best[4] == q->act.score);
    mq_home(q)->best[4] = 9999;
    con("act 0");
    con("step 1200");
    CHECK(q->act.done && !q->act.new_best && mq_home(q)->best[4] == 9999);
    /* Star Sled: typed numbers steer under them; catching the right one */
    mq_prof(q)->bosses = 0x3;
    CHECK(mq_action_open(mq_prof(q), ACT_SLED) && mq_action_open(mq_prof(q), ACT_PALS));
    con("act 1");
    q->act.pure_until = 0;
    con("step 1");
    CHECK(q->act.moment);
    score = q->act.score;
    for (int i = 0; i < ACT_NOBJ; i++)
        if (q->act.o[i].kind && q->act.o[i].val == q->act.ans)
            q->act.x = q->act.o[i].x;
    for (int i = 0; i < 200 && q->act.moment; i++)
        con("step 1");
    CHECK(q->act.right_last && q->act.score >= score + 5);
    /* Pop-up Pals: the key of the hole with the answer */
    con("act 2");
    q->act.pure_until = 0;
    con("step 1");
    CHECK(q->act.moment);
    int hole = -1;
    for (int i = 0; i < ACT_NOBJ; i++)
        if (q->act.o[i].kind && q->act.o[i].val == q->act.ans)
            hole = q->act.o[i].lane;
    CHECK(hole >= 0);
    static const char keyc[9] = {'7', '8', '9', '4', '5', '6', '1', '2', '3'};
    CHECK(mq_action_key(q, 0, keyc[hole]) && q->act.right_last && !q->act.moment);
    con("go arcade");
}

/* ------------------------------------------------------------ the home: coins, shop, rooms */
static void play_level(int w, int l) /* every answer right */
{
    char b[32];
    snprintf(b, sizeof b, "level %d %d", w, l);
    con(b);
    for (int i = 0; i < 12 && q->scene == SC_PLAY; i++) {
        q->fun_ms = 0; /* the economy alone here (fun beats: test_fun) */
        con("correct");
        cheer_out();
    }
}

/* Longer visits (the user: "pushed around shops too quickly...maybe a little longer in each?"): a round at a stop is
 * MQ_RUN_N orders, a finale MQ_RUN_BOSS, Bubble Pop MQ_RUN_POP, all in the game she picked; a long round marks its
 * halfway point (the cheer, the facts' clock) and pays no more coins than the old short one; it still ends on a win;
 * the done screen offers "Stay & play again!" beside the way on (widgets: tap, the arrows and ENTER), which plays
 * another round of the same game in the same place, its coins and history kept as any round's; a tap elsewhere goes
 * on as ever, but not in the first moment (her taps in the game never wave it away). */
static int lr_widget(int id)
{
    for (int i = 0; i < q->wg.n; i++)
        if (q->wg.w[i].id == id)
            return i;
    return -1;
}
static void lr_tap_widget(int id)
{
    con("widgets");
    int i = lr_widget(id);
    CHECK(i >= 0);
    if (i < 0)
        return;
    const mq_wg_t *g = &q->wg.w[i];
    ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(4, g->x + g->w / 2, g->y + g->h / 2));
}
/* every answer right (up to `upto` orders); the orders played, each in the round's game; the halfway cheer seen */
static int lr_play(int mech, int upto, int *halfway, int *games_changed)
{
    int orders = 0;
    for (int i = 0; i < 120 && q->scene == SC_PLAY && orders < upto; i++) {
        q->fun_ms = 0;
        if (q->sub == GS_PLAY && !q->gm.scene_on) {
            if (mech >= 0 && q->gm.mech != mech)
                (*games_changed)++;
            con("correct");
            orders++;
            cheer_out(); /* (the cheer: its reading time) */
        }
        con("step 2");
        if (q->scene == SC_PLAY && q->gm.half_t && !*halfway)
            (*halfway)++;
    }
    return orders;
}
static int lr_play_round(int mech, int *halfway, int *games_changed) { return lr_play(mech, 1000, halfway, games_changed); }
static void test_long_rounds(void)
{
    fresh_module();
    con("new Lia 3");
    q->fun_off = true;
    mq_set_start_grade(q, 3); /* (placed: no placement round) */
    mq_profile_t *p = mq_prof(q);
    mq_home_t *h = mq_home(q);
    CHECK(MQ_RUN_N == 10 && MQ_RUN_BOSS == 12 && MQ_RUN_POP == 8 && MQ_RUN_PLACE == 8);
    /* a picked game at the Post Office: ten orders, all in it, halfway marked */
    mq_press(q, B_WORLD, G_POST);
    CHECK(q->scene == SC_WORLD);
    mq_press(q, B_WGAME, 0);
    CHECK(q->scene == SC_PLAY && q->gm.chosen && q->gm.round_mech < ME_COUNT);
    int mech = q->gm.round_mech, world = q->world, level = q->level;
    int n0 = q->gm.round_mech == ME_POP ? MQ_RUN_POP : MQ_RUN_N;
    CHECK(q->run_n == n0);
    unsigned round0 = p->round;
    int half = 0, changed = 0, orders = lr_play_round(mech, &half, &changed);
    CHECK(q->scene == SC_DONE_LEVEL && orders == n0 && changed == 0);
    CHECK(half == 1 || n0 < MQ_RUN_N);
    CHECK(p->round == round0 + (n0 >= MQ_RUN_N ? 1 : 0)); /* (the facts' clock ticked at the half) */
    CHECK(q->run_coins > 0); /* (the rate: test_home_economy) */
    /* the done screen: four ways on, as widgets; the focus on the usual way on */
    con("widgets");
    CHECK(q->wg.n == 4 && lr_widget(B_STAY) >= 0 && lr_widget(B_CONT) >= 0 && lr_widget(B_GOHOME) >= 0);
    CHECK(q->wg.focus == lr_widget(B_CONT));
    for (int i = 0; i < q->wg.n; i++) /* (all on the screen, apart) */
        CHECK(q->wg.w[i].x >= 0 && q->wg.w[i].x + q->wg.w[i].w <= MQ_W && q->wg.w[i].y + q->wg.w[i].h <= MQ_Y0 + MQ_H &&
              (i == 0 || q->wg.w[i].x >= q->wg.w[i - 1].x + q->wg.w[i - 1].w));
    /* a tap at once (her last tap in the game) does nothing, on a button or beside it */
    lr_tap_widget(B_STAY);
    CHECK(q->scene == SC_DONE_LEVEL);
    ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(4, 160, MQ_Y0 + 60));
    CHECK(q->scene == SC_DONE_LEVEL);
    /* then "Stay & play again!" by a tap: the same place, the same game, a new round */
    con("step 40");
    uint32_t earned = h->earned;
    int skill = mq_levels[world][level] - 1;
    int plays = p->sk[skill].plays, recent = q->recent_i, probs = (int)p->problems;
    lr_tap_widget(B_STAY);
    CHECK(q->scene == SC_PLAY && q->world == world && q->level == level && !q->boss);
    CHECK(q->gm.round_mech == mech && q->gm.chosen && q->run_i == 0 && q->run_coins == 0 && q->run_n == n0);
    half = changed = 0;
    orders = lr_play_round(mech, &half, &changed);
    CHECK(q->scene == SC_DONE_LEVEL && orders == n0 && changed == 0);
    /* its coins and history recorded, as any round's */
    CHECK(q->run_coins > 0 && h->earned == earned + (uint32_t)q->run_coins);
    CHECK(p->sk[skill].plays == plays + 1 && (int)p->problems == probs + n0);
    CHECK(q->recent_i == (recent + n0) % MQ_NRECENT);
    /* the keys: the arrows choose, ENTER presses -- LEFT from the way on is "Stay & play again!" */
    con("step 40");
    ops.event(q, MODX_EV_KEY, 13 /* LEFT */);
    CHECK(q->scene == SC_DONE_LEVEL && q->wg.focus == lr_widget(B_STAY) && q->wg.kbd);
    ops.event(q, MODX_EV_KEY, 49 /* ENTER */);
    CHECK(q->scene == SC_PLAY && q->world == world && q->gm.round_mech == mech);
    /* it still ends on a win: a miss on the last order brings one more */
    orders = lr_play(mech, n0 - 1, &half, &changed);
    for (int i = 0; i < 20 && q->scene == SC_PLAY && q->sub != GS_PLAY; i++)
        con("step 40");
    CHECK(orders == n0 - 1 && q->run_i == n0 - 1);
    con("wrong");
    for (int i = 0; i < 6 && q->scene == SC_PLAY && q->sub == GS_PLAY && !q->gm.helped && q->run_i == n0 - 1; i++)
        con("wrong");
    for (int i = 0; i < 30 && q->scene == SC_PLAY && (q->sub != GS_PLAY || q->gm.scene_on); i++)
        con("next"), con("step 10");
    if (q->gm.helped) /* (the helper friend: her own order again, together) */
        con("correct");
    else
        CHECK(q->extra == 1);
    lr_play_round(-1, &half, &changed);
    CHECK(q->scene == SC_DONE_LEVEL && q->run_i == n0 + q->extra);
    con("step 40");
    ops.event(q, MODX_EV_KEY, 49 /* ENTER: the usual way on */);
    CHECK(q->scene == SC_WORLD && q->world == world);
    /* a tap anywhere else goes on, as ever (a swipe too) */
    mq_press(q, B_WGAME, 0);
    lr_play_round(-1, &half, &changed);
    CHECK(q->scene == SC_DONE_LEVEL);
    con("step 40");
    ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(4, 160, MQ_Y0 + 60));
    CHECK(q->scene == SC_WORLD);
    mq_press(q, B_WGAME, 0);
    lr_play_round(-1, &half, &changed);
    con("step 40");
    ops.event(q, MODX_EV_SWIPE, MODX_SWIPE(0, 160, MQ_Y0 + 100));
    CHECK(q->scene == SC_WORLD);
    /* the finale: twelve orders; staying plays the finale again */
    con("boss 0");
    CHECK(q->scene == SC_PLAY && q->boss && q->run_n == MQ_RUN_BOSS && q->gm.grump_max == MQ_RUN_BOSS);
    half = changed = 0;
    orders = lr_play_round(-1, &half, &changed);
    CHECK(q->scene == SC_DONE_LEVEL && orders == MQ_RUN_BOSS && half == 1);
    con("step 40");
    con("stay");
    CHECK(q->scene == SC_PLAY && q->boss && q->world == 0 && q->run_n == MQ_RUN_BOSS && q->run_i == 0);
    /* Bubble Pop: eight */
    mq_go(q, SC_MAP);
    q->want_mech1 = ME_POP + 1;
    con("level 0 0");
    q->want_mech1 = 0;
    CHECK(q->gm.round_mech == ME_POP && q->run_n == MQ_RUN_POP);
    q->fun_off = false;
}

static void test_home_economy(void)
{
    con("seed 11");
    con("new Ava 2");
    con("engine stop"); /* the economy of one stop's skill (the engine's choice: test_engine) */
    mq_profile_t *p = mq_prof(q);
    mq_home_t *h = mq_home(q);
    CHECK(h->coins == 20 && h->room[0].n == 5); /* the starter bedroom */
    /* the rate: learning pays more than mastered; bouncing back never beats a first try */
    CHECK(mq_answer_coins(p, S_GROUPS, true, false, 1, 0) == 3);
    CHECK(mq_answer_coins(p, S_GROUPS, false, true, 1, 0) == 2);
    CHECK(mq_answer_coins(p, S_GROUPS, false, false, 1, 0) == 0);
    CHECK(mq_answer_coins(p, S_GROUPS, true, false, 5, 0) == 5); /* a combo of 5 */
    p->sk[S_SHARE].flags |= SK_PASSED;
    CHECK(mq_answer_coins(p, S_SHARE, true, false, 1, 0) == 1);
    CHECK(mq_answer_coins(p, S_SHARE, false, true, 1, 0) == 1);
    /* diminishing returns: a mastered skill played over and over pays a third */
    p->sk[S_SHARE].plays = 20;
    int learn = 0, grind = 0;
    for (uint32_t k = 0; k < 30; k++)
        learn += mq_answer_coins(p, S_GROUPS, true, false, 1, k), grind += mq_answer_coins(p, S_SHARE, true, false, 1, k);
    CHECK(learn == 90 && grind == 10);
    p->sk[S_SHARE].flags = 0, p->sk[S_SHARE].plays = 0;
    /* a first level, all right: answers, the combo, the first finish (+10) and three stars (+5); not mastered yet:
     * mastery needs a second play day (spaced evidence) */
    h->ladder = 200, h->quest_day = 1, h->quest_done = 7; /* (the unlocks and quests pay on their own: test_meta) */
    int c0 = h->coins;
    play_level(0, 0);
    CHECK(q->scene == SC_DONE_LEVEL && !(p->sk[S_GROUPS].flags & SK_PASSED));
    int first = h->coins - c0;
    if (getenv("MQ_TRACE"))
        fprintf(stderr, "economy: first %d (mech %d, run %d, orders %d)\n", first, q->gm.round_mech, q->run_n, q->run_i);
    /* (Bubble Pop: rounds of MQ_RUN_POP orders, paid as the old five: 3 an answer and +2 at a combo of 5, x 5/8,
     * rounded; the first finish +10, three stars +5 -- the old round's 32, never more) */
    CHECK(q->run_n == MQ_RUN_POP && first == ((3 * MQ_RUN_POP + 2) * 5 + MQ_RUN_POP / 2) / MQ_RUN_POP + 10 + 5);
    CHECK(first <= 3 * 5 + 2 + 10 + 5);
    CHECK(q->run_coins == first && h->earned >= (uint32_t)first);
    /* the next play day: mastered on the first answer (+25), then mastered rates */
    p->days = 2;
    c0 = h->coins;
    play_level(0, 0);
    CHECK(p->sk[S_GROUPS].flags & SK_PASSED);
    /* (a round of MQ_RUN_N in the next game: 3, nine mastered 1s, +1 at combos of 5 and 10, x 6/10 rounded; +25 --
     * the old six-order round paid 3 + 25 + 5 * 1 + 1) */
    CHECK(q->run_n == MQ_RUN_N && h->coins - c0 == 25 + ((3 + (MQ_RUN_N - 1) + 2) * 6 + MQ_RUN_N / 2) / MQ_RUN_N);
    CHECK(h->coins - c0 <= 3 + 25 + 5 * 1 + 1);
    /* the same level again pays little; after many plays even less */
    c0 = h->coins;
    play_level(0, 0);
    int again = h->coins - c0;
    CHECK(again > 0 && again <= 8 && again * 4 < first);
    for (int k = 0; k < 8; k++)
        play_level(0, 0);
    c0 = h->coins;
    play_level(0, 0);
    int tired = h->coins - c0;
    CHECK(tired <= 3 && tired < again);
    /* a boss: 50 coins the first time, 5 after */
    con("grant world 0");
    p->bosses = 0;
    for (int k = 0; k < mq_world_levels(0); k++)
        p->sk[mq_levels[0][k] - 1].plays = 1;
    q->new_set = 0xFF;
    c0 = h->coins;
    con("boss 0");
    for (int i = 0; i < 14 && q->scene == SC_PLAY; i++) {
        q->fun_ms = 0;
        con("correct");
        cheer_out();
    }
    CHECK(q->scene == SC_DONE_LEVEL && (p->bosses & 1));
    CHECK(h->coins - c0 >= 50);
    CHECK(!((mq_home(q)->sets_seen >> G_POST) & 1)); /* everything is on sale; a place's set is news once she plays there */
    con("level 1 0");
    p->visited |= 1 << G_POST;
    for (int i = 0; i < 14 && q->scene == SC_PLAY; i++)
        q->fun_ms = 0, con("correct"), cheer_out();
    CHECK(((mq_home(q)->sets_seen >> G_POST) & 1) && q->new_set == G_POST);
    /* persistence: a miss, then the similar problem right: +2 for bouncing back */
    con("level 1 0"); /* a skill still being learned */
    con("wrong");
    for (int i = 0; i < 12 && q->sub == GS_HELP; i++)
        con("next");
    CHECK(q->scene == SC_PLAY && q->sub == GS_PLAY && q->gm.helped); /* her own order again, the friend cheering */
    c0 = h->coins;
    int carry = q->pay_carry;
    con("correct");
    CHECK(h->coins - c0 == (2 * q->pay_base + carry) / q->run_n && h->coins - c0 >= 1); /* (+2, at the long round's rate) */
    /* the worked example pays once per level run */
    con("level 1 1");
    con("showme");
    c0 = h->coins;
    for (int i = 0; i < 8 && q->sub == GS_TRICK; i++)
        con("next");
    CHECK(h->coins - c0 == 2);
    con("showme");
    for (int i = 0; i < 8 && q->sub == GS_TRICK; i++)
        con("next");
    CHECK(h->coins - c0 == 2);
    /* a fact turning fluent pays 5 */
    c0 = h->coins;
    int f = mq_fact_index(6, 7, false);
    p->fact[f].box = 3, p->fact[f].t10 = 20;
    mq_fact_record(q, f, true, 1500); /* box 4 today ... */
    CHECK(!mq_fact_fluent(&p->fact[f]) && h->coins - c0 <= 1);
    p->days++;
    c0 = h->coins;
    mq_fact_record(q, f, true, 1500); /* ... and again on the next play day: fluent */
    CHECK(mq_fact_fluent(&p->fact[f]) && h->coins - c0 == 5);
}

static void test_home_shop(void)
{
    con("new Bea 3");
    mq_profile_t *p = mq_prof(q);
    mq_home_t *h = mq_home(q);
    CHECK(mq_buy(q, IT_BED_HEART) == -3); /* 20 coins: not enough */
    con("coins 100");
    mq_quest_roll(q), h->quest_done = 7; /* (the day's quests done: a purchase pays nothing extra) */
    CHECK(mq_buy(q, IT_BED_HEART) == 0 && h->coins == 60 && h->own[IT_BED_HEART] == 2);
    CHECK(mq_item_free(q, IT_BED_HEART) == 1); /* one is in the bedroom */
    CHECK(mq_buy(q, IT_OVEN) != -2);          /* nothing is locked: the Bakery set is on sale from the start */
    CHECK(mq_buy(q, IT_SNOWGLOBE) == -1);     /* a gift: never sold */
    CHECK(mq_buy(q, IT_BONUS_GARDEN) == -1);  /* a set bonus: never sold */
    CHECK(mq_buy(q, IT_WALL_MINT) == 0 && mq_buy(q, IT_WALL_MINT) == -4); /* one of a style is enough */
    CHECK((mq_item_owned(q, IT_BOW_PINK) || mq_buy(q, IT_BOW_PINK) == 0) && mq_wear(q, IT_BOW_PINK) == 0 && h->wear[OS_HEAD] == IT_BOW_PINK); /* (a starter outfit, or bought) */
    CHECK(mq_wear(q, IT_BOW_PINK) == 0 && h->wear[OS_HEAD] == 0);
    CHECK(mq_wear(q, IT_CHEFHAT) == -1);
    /* every set and every room from the start */
    CHECK(mq_item_unlocked(p, IT_OVEN) && mq_item_unlocked(p, IT_GRANDCLOCK));
    CHECK(mq_room_open(p, 1) && mq_room_open(p, 2) && mq_room_open(p, 3) && mq_room_open(p, 4));
    p->bosses = 0x07; /* three bosses: the Sky Castle (and the tower) open */
    CHECK(mq_item_unlocked(p, IT_BED_CLOUD) && mq_room_open(p, 4));
    /* completing a set gives its bonus (and 20 coins) */
    con("coins 9000");
    int tot, n = mq_set_progress(q, HS_GARDEN, &tot);
    CHECK(n == 1 && tot >= 10); /* the pet that follows her is from the Garden set */
    int last = 0;
    for (int id = 1; id <= MQ_NITEM; id++)
        if (mq_item[id].set == HS_GARDEN && mq_item[id].price) {
            CHECK(!(h->bonus & 1) && mq_item_owned(q, IT_BONUS_GARDEN) == 0);
            if (mq_item[id].cat == HC_OUTFIT) { /* clothes come a few a day: on today's rack, then bought */
                mq_rack_fill(q);
                if (!mq_rack_has(q, id))
                    CHECK(mq_buy(q, id) == -2);
                h->rack[0] = (uint8_t)id; /* (its day came) */
            }
            CHECK(mq_buy(q, id) == 0);
            last = id;
        }
    CHECK(last && mq_set_progress(q, HS_GARDEN, NULL) == tot);
    CHECK((h->bonus & 1) && mq_item_owned(q, IT_BONUS_GARDEN) == 1 && q->new_bonus == IT_BONUS_GARDEN);
    mq_check_sets(q); /* only once */
    CHECK(mq_item_owned(q, IT_BONUS_GARDEN) == 1);
    /* the daily gift: coins every time, a surprise item at least every third gift */
    int items = 0;
    for (int k = 0; k < 6; k++) {
        uint16_t c0 = h->coins;
        mq_open_gift(q);
        CHECK(h->coins - c0 >= 10 && h->coins - c0 <= 20);
        items += q->gift_item != 0;
        if (q->gift_item)
            CHECK(mq_item_owned(q, q->gift_item) >= 1);
    }
    CHECK(items >= 2 && h->gifts == 6);
}

static void test_home_rooms(void)
{
    con("new Cleo 1");
    mq_home_t *h = mq_home(q);
    mq_room_t *r = &h->room[0];
    q->hm_room = 0, q->hm_nundo = 0;
    con("coins 2000");
    CHECK(mq_buy(q, IT_SOFA_PEACH) == 0 && mq_buy(q, IT_KITTEN) == 0 && mq_buy(q, IT_LAMP_STAR) == 0);
    int n0 = r->n;
    /* placing: clamped to the grid; the item leaves the tray */
    int k = mq_place(q, 0, IT_SOFA_PEACH, 30, 30, 0);
    CHECK(k == n0 && r->n == n0 + 1 && r->it[k].x == HM_COLS - 4 && r->it[k].y == HM_ROWS - 2);
    CHECK(mq_item_free(q, IT_SOFA_PEACH) == 0 && mq_place(q, 0, IT_SOFA_PEACH, 0, 0, 0) == -1);
    CHECK(mq_place(q, 0, IT_WALL_PINK, 0, 0, 0) == -1); /* styles are not placed */
    /* turning swaps the footprint (and stays inside); mirroring keeps it */
    mq_turn(q, 0, k, false);
    CHECK((r->it[k].fl & 3) == 1 && r->it[k].x <= HM_COLS - 2 && r->it[k].y <= HM_ROWS - 4);
    mq_turn(q, 0, k, true);
    CHECK(r->it[k].fl == 5);
    /* layering: to the back and to the front */
    int kk = mq_place(q, 0, IT_KITTEN, 5, 7, 0);
    CHECK(mq_layer(q, 0, kk, -1) == 0 && r->it[0].id == IT_KITTEN);
    CHECK(mq_layer(q, 0, 0, 1) == r->n - 1 && r->it[r->n - 1].id == IT_KITTEN);
    /* moving, putting away, and undoing every step back */
    mq_room_t before = *r;
    CHECK(mq_move(q, 0, r->n - 1, 2, 2) == 0 && r->it[r->n - 1].x == 2);
    mq_remove(q, 0, 0);
    CHECK(r->n == before.n - 1);
    CHECK(mq_set_style(q, 0, IT_WALL_MINT) == -1); /* not owned */
    CHECK(mq_buy(q, IT_WALL_MINT) == 0 && mq_set_style(q, 0, IT_WALL_MINT) == 0 && r->wall == IT_WALL_MINT);
    CHECK(mq_undo(q) && r->wall == 0);
    CHECK(mq_undo(q) && r->n == before.n);
    CHECK(mq_undo(q) && !memcmp(r, &before, sizeof before));
    int pushed = 0;
    while (mq_undo(q))
        pushed++;
    CHECK(pushed >= 5 && r->n == n0); /* back to the starter room */
    /* a room holds HM_RITEMS things */
    con("coins 60000");
    for (int i = 0; i < 9; i++)
        mq_buy(q, IT_BALL), mq_buy(q, IT_BLOCKS), mq_buy(q, IT_TEAPOT), mq_buy(q, IT_CAKESTAND);
    int placed = 0;
    static const uint8_t kinds[3] = {IT_BALL, IT_BLOCKS, IT_CAKESTAND};
    for (int i = 0; i < 40; i++)
        placed += mq_place(q, 0, kinds[i % 3], i % 20, i % 9, 0) >= 0;
    CHECK(r->n == HM_RITEMS && placed == HM_RITEMS - n0);
    /* rooms are separate; an item placed in one is not free for another */
    CHECK(mq_place(q, 1, IT_TEAPOT, 3, 6, 0) >= 0 && h->room[1].n == 1 && mq_item_free(q, IT_TEAPOT) == 8);
    /* drag and drop through the touch handler: a tray item into the room, then moved */
    con("room 0");
    mq_remove(q, 0, r->n - 1);
    con("go room");
    q->hm_room = 0;
    ops.draw(q, 0, (int[4]){0});
    int bi = -1;
    for (int i = 0; i < q->nbtn; i++)
        if (q->btn[i].id == HB_TRAY && q->btn[i].arg == IT_TEAPOT)
            bi = i;
    if (bi < 0) /* the teapot may be on a later tray page */
        for (int pg = 0; pg < 20 && bi < 0; pg++) {
            q->hm_tray = (uint8_t)pg;
            ops.draw(q, 0, (int[4]){0});
            for (int i = 0; i < q->nbtn; i++)
                if (q->btn[i].id == HB_TRAY && q->btn[i].arg == IT_TEAPOT)
                    bi = i;
        }
    CHECK(bi >= 0);
    { /* CR-14: "next" past the tray's last page stays there (never empty pages, never a wrap to the first) */
        uint8_t t0 = q->hm_tray;
        for (int pk = 0; pk < 300; pk++)
            mq_home_press(q, HB_TRAYPAGE, 1);
        CHECK(q->hm_tray * 7 < mq_home_tray_count(q) && (q->hm_tray + 1) * 7 >= mq_home_tray_count(q));
        q->hm_tray = t0;
    }
    if (bi >= 0) {
        int n1 = r->n, bx = q->btn[bi].x + 10, by = q->btn[bi].y + 10;
        ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(1, bx, by));
        ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(2, 100, 120));
        ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(2, 168, 132));
        ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(3, 168, 132));
        CHECK(r->n == n1 + 1 && r->it[n1].id == IT_TEAPOT && q->hm_sel == n1);
        int x = r->it[n1].x, y = r->it[n1].y;
        CHECK(x == (168 - 16 + 8) / 16 && y == (132 - 16 - (MQ_Y0 + 20) + 8) / 16);
        /* drag it by its middle one cell right */
        int px = x * 16 + 16, py = MQ_Y0 + 20 + y * 16 + 16;
        ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(1, px, py));
        ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(2, px + 17, py));
        ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(3, px + 17, py));
        CHECK(r->it[n1].x == x + 1 && r->it[n1].y == y);
        CHECK(mq_undo(q) && r->it[n1].x == x);
    }
}

/* a 16-character name (MQ_NAME_MAX): typed in full (17th refused), fits every place it is drawn,
 * round-trips through the save; an older save's (10-character) name is carried into the long field */
static char name_pre[8] = "\x01";
static int name_bad, name_seen, name_x0, name_x1, name_ymax = 9999;
static int name_text(const font_t *f, int x, int y, const char *s, uint16_t fg, int bg)
{
    if (strstr(s, name_pre) && y <= name_ymax) {
        int w = modx_api.text_width(f, s);
        name_seen++;
        if ((x < name_x0 || x + w > name_x1) && name_bad++ < 8)
            fprintf(stderr, "scene %s: \"%s\" at %d..%d, outside %d..%d\n", mq_scene_name(q), s, x, x + w, name_x0, name_x1);
    }
    return modx_api.text(f, x, y, s, fg, bg);
}
static void test_long_name(void)
{
    static modx_api_t api;
    api = modx_api;
    api.text = name_text;
    static const char *const names[] = {"WWWWWWWW-WWWWWWW", "Zoe-Mae O'Briens", "Annabelle Rosali"};
    for (int n = 0; n < 3; n++) {
        memset(q, 0, mq_module_rw_size);
        memset(&ops, 0, sizeof ops);
        CHECK(mq_module_init(&api, q, &ops) == 0);
        q->rng = 0x2545F491u, q->scenes_off = true, q->fun_ms = 0;
        CHECK(MQ_NAME_MAX == 16 && strlen(names[n]) <= MQ_NAME_MAX);
        con("new X 2");
        con("go name");
        mq_pname_set(q, q->save.cur, ""); /* typed from a keyboard (MODX_EV_TEXT), the 17th refused */
        for (const char *c = names[n]; *c; c++)
            CHECK(ops.event(q, MODX_EV_TEXT, *c) == 1);
        CHECK(ops.event(q, MODX_EV_TEXT, 'x') == 1 && !strcmp(mq_name(q), names[n]));
        CHECK(ops.event(q, MODX_EV_TEXT, '7') == 0 && ops.event(q, MODX_EV_TEXT, '?') == 0);
        CHECK(!strncmp(mq_prof(q)->name, names[n], 11) && strlen(mq_prof(q)->name) == (strlen(names[n]) > 11 ? 11 : strlen(names[n])));
        snprintf(name_pre, sizeof name_pre, "%.4s", names[n]);
        name_x0 = 0, name_x1 = 9999;
        static const struct { const char *go; int x0, x1; } at[] = {
            {"name", 60, 260},    /* the name's bubble */
            {"map", 0, 128},      /* "Mia's town", to the event badge (the title line: the house's sign scrolls) */
            {"showoff", 70, 250}, /* "Mia's room" in its plate (the show-off) */
            {"wardrobe", 6, 116}, /* under her buddy */
            {"parent", 0, MQ_W},  /* the grown-ups' title */
            {"intro", 86, 312},   /* the guide's greeting, in its bubble */
        };
        int r[4];
        for (size_t i = 0; i < sizeof at / sizeof at[0]; i++) {
            char b[32];
            snprintf(b, sizeof b, "go %s", at[i].go);
            name_x0 = -9999, name_x1 = 9999, name_ymax = 9999;
            con(b);
            if (!strcmp(at[i].go, "intro")) {
                for (int k = 0; k < MQ_NSKILL; k++)
                    mq_prof(q)->sk[k].plays = 1; /* (a returning player: her name in the greeting) */
                q->sub = 0;
            }
            name_x0 = at[i].x0, name_x1 = at[i].x1, name_seen = 0;
            name_ymax = !strcmp(at[i].go, "map") ? MQ_Y0 + 17 : 9999;
            ops.draw(q, 0, r);
            CHECK(name_seen > 0);
        }
        name_x0 = -9999, name_x1 = 9999, name_ymax = 9999;
        con("go title"); /* both cards: inside a card */
        name_x0 = 20, name_x1 = 300, name_seen = 0;
        ops.draw(q, 0, r);
        CHECK(name_seen > 0);
        /* the save round trip */
        mq_save_now(q);
        memset(q, 0, mq_module_rw_size);
        memset(&ops, 0, sizeof ops);
        CHECK(mq_module_init(&api, q, &ops) == 0);
        CHECK(!strcmp(mq_name(q), names[n]));
        name_x0 = -9999, name_x1 = 9999;
    }
    CHECK(name_bad == 0);
    /* an older save (the name in the profile only, the long field zero): carried over on load */
    fresh_module();
    memset(q->save.more[q->save.cur].name, 0, sizeof q->save.more[0].name);
    snprintf(mq_prof(q)->name, sizeof mq_prof(q)->name, "Old Name");
    mq_save_now(q);
    fresh_module();
    CHECK(!strcmp(q->save.more[q->save.cur].name, "Old Name") && !strcmp(mq_name(q), "Old Name"));
    /* a version-5 save (before mq_more_t): the name too */
    static uint8_t v5[sizeof(mq_save_t)];
    memcpy(v5, &q->save, MQ_SAVE_V5_SIZE);
    ((mq_save_t *)v5)->version = 5, ((mq_save_t *)v5)->size = (uint16_t)MQ_SAVE_V5_SIZE;
    CHECK(modx_api.save_put("mathquest", v5, MQ_SAVE_V5_SIZE) == 0);
    fresh_module();
    CHECK(!strcmp(mq_name(q), "Old Name") && !strcmp(q->save.more[q->save.cur].name, "Old Name"));
}

/* saving: the homes round-trip; a version-1 save (before the homes) is migrated */
static void test_home_save(void)
{
    con("new Dina 2");
    mq_home_t *h = mq_home(q);
    con("coins 777");
    mq_buy(q, IT_FISHBOWL);
    q->hm_room = 0;
    int k = mq_place(q, 0, IT_FISHBOWL, 9, 7, 6);
    CHECK(k >= 0);
    mq_home_t copy = *h;
    int cur = q->save.cur;
    mq_save_now(q);
    fresh_module();
    CHECK(q->save.version == MQ_SAVE_VERSION && q->save.cur == cur);
    CHECK(!memcmp(mq_home(q), &copy, sizeof copy));
    CHECK(sizeof(mq_save_t) <= MODX_SAVE_MAX && modx_api.save_max() == MODX_SAVE_MAX);
    /* version 1: the profiles only (the module before the homes) */
    static mq_save_v3_t old; /* (the layout of 71 skills) */
    memset(&old, 0, sizeof old);
    old.magic = MQ_SAVE_MAGIC, old.version = 1, old.size = (uint16_t)MQ_SAVE_V1_SIZE, old.cur = 1;
    mq_profile_v3_t *p3 = &old.prof[1], *p = p3;
    p->used = 1, p->buddy = 3, p->stars = 130, p->bosses = 0x01;
    snprintf(p->name, sizeof p->name, "Old");
    p->stickers = 0x5;
    p->room[0] = 1;      /* sticker 0 (heart) on the wall */
    p->room[7] = 17 + 2; /* decor 2 (a plant): owned at 130 stars (one per 40) */
    p->room[8] = 17 + 5; /* decor 5: not owned yet at 130 stars */
    p->sk[S_GROUPS].mastery = 88;
    CHECK(modx_api.save_put("mathquest", &old, MQ_SAVE_V1_SIZE) == 0);
    fresh_module();
    mq_profile_t *pn = mq_prof(q);
    h = mq_home(q);
    CHECK(q->save.version == MQ_SAVE_VERSION && q->save.size == sizeof(mq_save_t) && q->save.cur == 1);
    CHECK(pn->used && !strcmp(pn->name, "Old") && pn->stars == 130 && pn->sk[S_GROUPS].mastery == 88);
    CHECK(pn->fact[0].box == p3->fact[0].box);
    CHECK(h->coins == 20 + 65);
    CHECK(h->own[IT_RUG_LAV] >= 1 && h->own[IT_LAMP_STAR] == 1 && h->own[IT_PLANT_LEAFY] >= 2 && !h->own[IT_SHELF_BOOKS]);
    int heart = 0, plant = 0, shelf = 0;
    for (int i = 0; i < h->room[0].n; i++)
        heart += h->room[0].it[i].id == IT_STICKER_HEART, plant += h->room[0].it[i].id == IT_PLANT_LEAFY,
            shelf += h->room[0].it[i].id == IT_SHELF_BOOKS;
    CHECK(heart == 1 && plant == 2 && shelf == 0);
    CHECK(h->sets_seen & (1u << G_POST));
    CHECK(!q->save.prof[0].used && q->save.home[0].coins == 0); /* the unused profile stays empty */
    mq_save_now(q);
    fresh_module();
    CHECK(mq_home(q)->coins == 85 && q->save.version == MQ_SAVE_VERSION);
    /* a newer save (a later module wrote more at the end): this module keeps what it knows */
    static uint8_t big[sizeof(mq_save_t) + 16];
    memcpy(big, &q->save, sizeof q->save);
    ((mq_save_t *)big)->version = MQ_SAVE_VERSION + 1;
    ((mq_save_t *)big)->size = (uint16_t)sizeof big;
    CHECK(modx_api.save_put("mathquest", big, sizeof big) == 0);
    fresh_module();
    CHECK(mq_home(q)->coins == 85 && !strcmp(mq_prof(q)->name, "Old"));
    /* version 4 (before the friends, the merge board and events): kept as is, the new block starts
     * fresh (a little sparkle energy to begin with) */
    q->save.extra[1].heart[2] = 77;
    memcpy(big, &q->save, MQ_SAVE_V4_SIZE);
    ((mq_save_t *)big)->version = 4;
    ((mq_save_t *)big)->size = (uint16_t)MQ_SAVE_V4_SIZE;
    CHECK(modx_api.save_put("mathquest", big, MQ_SAVE_V4_SIZE) == 0);
    fresh_module();
    CHECK(q->save.version == MQ_SAVE_VERSION && mq_home(q)->coins == 85 && !strcmp(mq_prof(q)->name, "Old"));
    CHECK(q->save.extra[1].heart[2] == 0 && q->save.extra[1].energy == 10 && q->save.extra[0].energy == 0);
    CHECK(sizeof(mq_extra_t) == 384 && sizeof(mq_more_t) == 4096);
    CHECK(q->save.more[1].mem[0].kind == 0 && q->save.more[1].remembered == 0);
    /* version 5 (before the long memory): the eight memories move into it, the rest starts fresh */
    q->save.extra[1].heart[2] = 77;
    for (int i = 0; i < MQ_NMEM; i++)
        q->save.extra[1].mem[i] = (mq_mem_t){(uint8_t)(MEM_MADE + i % 3), 2, SI_COOKIE, 0, 5, (uint16_t)(10 + i)};
    memcpy(big, &q->save, MQ_SAVE_V5_SIZE);
    ((mq_save_t *)big)->version = 5;
    ((mq_save_t *)big)->size = (uint16_t)MQ_SAVE_V5_SIZE;
    CHECK(modx_api.save_put("mathquest", big, MQ_SAVE_V5_SIZE) == 0);
    fresh_module();
    CHECK(q->save.version == MQ_SAVE_VERSION && q->save.size == MQ_SAVE_SIZE16(sizeof(mq_save_t)) && mq_home(q)->coins == 85);
    CHECK(q->save.extra[1].heart[2] == 77 && q->save.more[1].remembered == MQ_NMEM);
    CHECK(q->save.more[1].mem[7].n == 17 && q->save.more[1].mem[7].kind == MEM_MADE + 1 && !q->save.more[1].mem[8].kind);
    CHECK(!q->save.extra[1].mem[0].kind); /* (only the long memory is used now) */
    /* the save's headroom: at least 64 KB more can be appended before the core's limit */
    CHECK(MODX_SAVE_MAX >= sizeof(mq_save_t) + (64u << 10) && modx_api.save_max() == MODX_SAVE_MAX);
    /* a full save: every memory of both profiles, every byte of the new block set, survives a save and
     * a fresh bind intact */
    for (int pk = 0; pk < 2; pk++) {
        mq_more_t *m = &q->save.more[pk];
        for (int i = 0; i < MQ_NMEMORY; i++)
            m->mem[i] = (mq_mem_t){(uint8_t)(1 + (i + pk) % (MEM_COUNT - 1)), (uint8_t)(i % MQ_NCAST), (uint8_t)(i % SI_COUNT),
                                   (uint8_t)(i % MQ_NWORLD), (uint16_t)(i + pk), (uint16_t)(1000 + i)};
        m->remembered = 50000u + (uint32_t)pk;
        for (int i = 0; i < MQ_NACTED; i++)
            m->acted[i] = (uint8_t)(i * 7 + pk);
        m->visitors_met = 0xFF, m->fests_seen = 0x3FF;
    }
    static mq_save_t keep;
    keep = q->save;
    mq_save_now(q);
    CHECK(modx_api.save_size("mathquest") == sizeof(mq_save_t));
    fresh_module();
    CHECK(!memcmp(&q->save.more, &keep.more, sizeof keep.more) && !memcmp(&q->save.prof, &keep.prof, sizeof keep.prof));
    /* garbage: fresh profiles */
    memset(big, 0x5A, sizeof big);
    CHECK(modx_api.save_put("mathquest", big, 200) == 0);
    fresh_module();
    CHECK(!q->save.prof[0].used && !q->save.prof[1].used);
}

/* theme packs, version 2: pictures for home items */
static void test_theme_items(void)
{
    static uint8_t b[4096];
    CHECK(hal_sd_sim_insert(64) == 0);
    CHECK(fs_mkfs() == 0);
    CHECK(fs_mkdir("/CALC") == 0 && fs_mkdir("/CALC/THEMES") == 0);
    /* no cast sprites, 2 items: the heart bed (4 x 2 art pixels) and an unknown id */
    uint8_t *p = b + 64;
    uint16_t pal[2] = {0, 0x001F};
    memcpy(p, pal, 4), p += 4;
    uint8_t tab[8] = {IT_BED_HEART, 0, 4, 2, 250, 0, 1, 1};
    memcpy(p, tab, 8), p += 8;
    for (int i = 0; i < 8; i++)
        *p++ = (uint8_t)(i % 2);
    *p++ = 1;
    size_t plen = (size_t)(p - (b + 64));
    memset(b, 0, 64);
    memcpy(b, "MQTH", 4);
    b[4] = 2, b[6] = 64;
    memcpy(b + 8, "Items", 5);
    b[32] = 8, b[33] = 8, b[34] = 0, b[36] = 2, b[38] = 2;
    b[40] = (uint8_t)plen;
    uint32_t c = crc32(b + 64, plen);
    for (int i = 0; i < 4; i++)
        b[44 + i] = (uint8_t)(c >> (8 * i));
    CHECK(put_file("/CALC/THEMES/ITEMS.THM", b, 64 + plen) == 0);
    CHECK(mq_theme_load(q, "ITEMS.THM") == 0 && q->theme.nitem == 1 && q->theme.item_id[0] == IT_BED_HEART);
    CHECK(q->theme.item_w[0] == 4 && q->theme.item_h[0] == 2 && q->theme.item_px[1] == 1);
    /* drawn instead of the built-in bed: blue pixels, bottom-centred on the 4 x 3 cell footprint */
    memset(fb, 0, sizeof fb);
    mq_item_draw(q, IT_BED_HEART, 0, 40, 0, 0, 2, -1);
    const uint16_t *s = modx_api.framebuffer();
    int blue = 0;
    for (int i = 0; i < 320 * 240; i++)
        blue += s[i] == 0x001F;
    CHECK(blue == 4 * 2 * 2); /* 4 of 8 art pixels set, each 2 x 2 */
    b[76] ^= 1; /* a pixel: the CRC catches it */
    CHECK(put_file("/CALC/THEMES/BADI.THM", b, 64 + plen) == 0);
    CHECK(mq_theme_load(q, "BADI.THM") == -3 && !q->theme.loaded);
    /* tools/make_theme.py's item pack (make mq-tools-test): every picture equals the built-in art */
    const char *chars = getenv("MQ_SAMPLE_CHARS"); /* version 3: the characters of the stories */
    if (chars) {
        static uint8_t big[256 * 1024];
        FILE *f = fopen(chars, "rb");
        CHECK(f != NULL);
        size_t len = f ? fread(big, 1, sizeof big, f) : 0;
        if (f)
            fclose(f);
        CHECK(put_file("/CALC/THEMES/CHARS.THM", big, len) == 0);
        CHECK(mq_theme_load(q, "CHARS.THM") == 0 && q->theme.has_traits && q->theme.nvariant == 1);
        CHECK(!strcmp(mq_trait(q, 0, TR_FOOD), "blueberry") && mq_pronoun(q, 0) == PR_HE && mq_pronoun(q, 1) == PR_THEY);
        char out[160];
        mq_story_expand(q, "{A} shares 3 {FA} with {B}; {pB} {kB} {tB}.", 0, 1, out, sizeof out);
        CHECK(!strcmp(out, "Clover shares 3 blueberries with Pebble; their lilac umbrella."));
        CHECK(mq_theme_frame(q, 0, AN_POUT, 0) >= 0); /* the pack's own pout */
        big[100] ^= 1; /* damaged: refused as a whole */
        CHECK(put_file("/CALC/THEMES/CHARS.THM", big, len) == 0);
        CHECK(mq_theme_load(q, "CHARS.THM") == -3 && !q->theme.loaded);
        mq_theme_load(q, "");
    }
    const char *sample = getenv("MQ_SAMPLE_ITEMS");
    if (sample) {
        static uint8_t big[256 * 1024];
        FILE *f = fopen(sample, "rb");
        CHECK(f != NULL);
        size_t len = f ? fread(big, 1, sizeof big, f) : 0;
        if (f)
            fclose(f);
        CHECK(put_file("/CALC/THEMES/SAMPLE.THM", big, len) == 0);
        CHECK(mq_theme_load(q, "SAMPLE.THM") == 0 && q->theme.nitem == 48);
        int diff = 0;
        for (int i = 0; i < q->theme.nitem; i++) {
            const mq_item_t *t = &mq_item[q->theme.item_id[i]];
            CHECK(q->theme.item_w[i] == t->aw && q->theme.item_h[i] == t->ah);
            for (int y = 0; y < t->ah; y++)
                for (int x = 0; x < t->aw; x++) {
                    int v = (mq_item_px[t->off + (uint32_t)(y * ((t->aw + 1) / 2) + x / 2)] >> ((x & 1) * 4)) & 15;
                    int tv = q->theme.item_px[q->theme.item_off[i] + y * t->aw + x];
                    diff += (v == 0) != (tv == 0) || (v && mq_item_master[t->pal[v]] != q->theme.pal[tv]);
                }
        }
        CHECK(diff == 0);
        printf("test_mathquest: sample item theme %s checked\n", sample);
    }
    mq_theme_load(q, "");
    hal_sd_sim_insert(0);
}


/* theme packs with animation frames: an old pack is all idle frame 0; an animated pack's
 * frames take turns; anims without frames are made from the still sprite */
static void test_theme_anim(void)
{
    static uint8_t b[4096];
    CHECK(hal_sd_sim_insert(64) == 0 && fs_mkfs() == 0);
    CHECK(fs_mkdir("/CALC") == 0 && fs_mkdir("/CALC/THEMES") == 0);
    /* slot 2: idle 0, idle 1, happy 0, happy 1, happy 2; slot 7: one still sprite */
    static const uint8_t spr[6][3] = {{2, AN_IDLE, 0}, {2, AN_IDLE, 1}, {2, AN_HAPPY, 0}, {2, AN_HAPPY, 1},
                                      {2, AN_HAPPY, 2}, {7, 0, 0}};
    uint8_t *p = b + 64;
    uint16_t pal[3] = {0, 0xF800, 0x07E0};
    memcpy(p, pal, 6), p += 6;
    for (int i = 0; i < 6; i++) {
        memset(p, 0, 16);
        memcpy(p, "Berry", 5);
        p[12] = spr[i][0], p[13] = spr[i][1], p[14] = spr[i][2];
        p += 16;
    }
    for (int i = 0; i < 6; i++)
        for (int k = 0; k < 64; k++)
            *p++ = (uint8_t)(k == i ? 2 : (k % 3 ? 1 : 0)); /* each frame marked by one pixel */
    size_t plen = (size_t)(p - (b + 64));
    memset(b, 0, 64);
    memcpy(b, "MQTH", 4);
    b[4] = 1, b[6] = 64; /* animated, still version 1 */
    memcpy(b + 8, "Anim", 4);
    b[32] = 8, b[33] = 8, b[34] = 6, b[36] = 3;
    b[40] = (uint8_t)plen, b[41] = (uint8_t)(plen >> 8);
    uint32_t c = crc32(b + 64, plen);
    for (int i = 0; i < 4; i++)
        b[44 + i] = (uint8_t)(c >> (8 * i));
    CHECK(put_file("/CALC/THEMES/ANIM.THM", b, 64 + plen) == 0);
    CHECK(mq_theme_load(q, "ANIM.THM") == 0 && q->theme.nspr == 6);
    CHECK(q->theme.anim[2] == AN_HAPPY && q->theme.frame[4] == 2);
    /* happy takes turns over its three frames; think has no frames (made from the still); idle has two */
    int f0 = mq_theme_frame(q, 2, AN_HAPPY, 0), f1 = mq_theme_frame(q, 2, AN_HAPPY, 160), f2 = mq_theme_frame(q, 2, AN_HAPPY, 320);
    CHECK(f0 == 2 && f1 == 3 && f2 == 4 && mq_theme_frame(q, 2, AN_HAPPY, 480) == 2);
    CHECK(mq_theme_frame(q, 2, AN_THINK, 0) == -1 && mq_theme_frame(q, 7, AN_HAPPY, 0) == -1);
    CHECK(mq_theme_frame(q, 2, AN_IDLE, 0) == 0 && mq_theme_frame(q, 2, AN_IDLE, 420) == 1);
    /* drawing every animation of a pack slot, a still slot and the built-in cast */
    for (int a = 0; a < AN_COUNT; a++)
        for (int sl = 0; sl < MQ_NCAST; sl++) {
            q->anim_t = (uint32_t)(a * 977 + sl * 131);
            mq_sprite_anim(q, sl, 40, 60, 2, a);
        }
    mq_theme_load(q, "");
    for (int a = 0; a < AN_COUNT; a++)
        mq_sprite_anim(q, 3, 40, 60, 3, a);
    hal_sd_sim_insert(0);
}

/* the catalogue: sizes, prices, sets, art */
static void test_catalogue(void)
{
    int sold = 0, per_set[HM_NSET] = {0}, art = 0;
    for (int id = 1; id <= MQ_NITEM; id++) {
        const mq_item_t *t = &mq_item[id];
        CHECK(t->name[0] && t->cat < HM_NCAT && t->set < HM_NSET);
        if (t->price)
            sold++, per_set[t->set]++;
        if (t->w && t->cat != HC_STICKER)
            CHECK(t->w <= 5 && t->h <= 4 && t->aw == t->w * 8 && t->ah == t->h * 8);
        if (t->cat == HC_OUTFIT)
            CHECK(t->aw == 20 && t->ah == 20 && t->extra < HM_NSLOT);
        if (t->cat == HC_WALL || t->cat == HC_FLOOR)
            CHECK(t->extra < HM_NPAT);
        art += t->aw > 0;
    }
    CHECK(sold >= 120 && art >= 120);
    for (int s = 0; s < MQ_NWORLD; s++) /* every world brings a set and has a bonus */
        CHECK(per_set[s] >= 8);
    for (int r = 0; r < HM_NROOM; r++)
        CHECK(mq_item[mq_room_default[r][0]].cat == HC_WALL && mq_item[mq_room_default[r][1]].cat == HC_FLOOR);
}


/* ------------------------------------------------------------ the games: every answer is a game action */
#define GFY (MQ_Y0 + 58)
#define GFB (MQ_Y0 + MQ_H)
static void tev(int type, int x, int y) { ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(type, x, y)); }
static void gkey(int k) { ops.event(q, MODX_EV_KEY, k); }
static void gtype(const char *t)
{
    for (const char *c = t; *c; c++)
        mq_game_key(q, 0, *c);
}
/* a fresh order of `skill` told in game `mech` (false: none of 200 problems fits); order_turn >= 0:
 * as the run's order of that number */
static int order_turn = -1;
static bool order_in(int skill, int d, int mech)
{
    char b[32];
    snprintf(b, sizeof b, "skill %d %d", skill, d);
    con(b);
    if (order_turn >= 0)
        q->run_i = (uint8_t)order_turn, q->gm.orders = 0;
    q->fun_ms = 0, q->rapid = 0, q->guess_n = 0;
    for (int w = 0; w < WS_COUNT; w++) /* (the games' own controls: the notebook waits for its button) */
        mq_pace(q)->ws_level[w] = 2;
    memset(mq_pace(q)->recent, 0, sizeof mq_pace(q)->recent), mq_pace(q)->recent_miss = 0; /* (no helper from earlier slips) */
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
static int steps_until_judged(int max)
{
    int i = 0;
    for (; i < max && q->sub == GS_PLAY && !q->tried_wrong; i++)
        con("step 1");
    return i;
}
static mq_gobj_t *obj_of(int opt)
{
    for (int i = 0; i < GM_NOBJ; i++)
        if (q->gm.o[i].kind == 1 && q->gm.o[i].opt == opt)
            return &q->gm.o[i];
    return NULL;
}

/* Floating answers never get stuck (the reported "the balloon got stuck at the top"): in Bubble Pop and
 * Cloud Catch, for every skill that plays there, every difficulty, the twists, the speed controller from
 * its floor to the top and the idle hover, every answer keeps moving or comes round again within a
 * bounded time and never waits under the order board (its number hidden). */
static void idle_frame(void) /* a frame with no input (the idle hover on), as the device runs it */
{
    q->now += MQ_FRAME_MS, q->anim_t += MQ_FRAME_MS;
    mq_game_frame(q);
}
static void test_floating(void)
{
    con("new Float 2");
    con("grant bosses");
    q->fun_off = true;
    int orders = 0, stuck = 0, hidden = 0;
    static const int mods[] = {MOD_NONE, MOD_GIANT, MOD_FLIP, MOD_NIGHT};
    for (int sk = 0; sk < MQ_NSKILL; sk++)
        for (int d = 0; d < mq_rungs(sk); d++)
            for (int mi = 0; mi < 2; mi++)
                for (int k = 0; k < 4; k++) {
                    int mech = mi ? ME_CATCH : ME_POP;
                    if (k == 1 && mech == ME_CATCH)
                        continue; /* (giant bubbles: Bubble Pop only) */
                    q->rng = (uint32_t)(sk * 977 + d * 31 + k * 7 + mi) | 1;
                    mq_pace(q)->speed[mech] = (uint8_t)(k * 33); /* the controller from its floor to the top */
                    if (!order_in(sk, d, mech))
                        continue;
                    q->gm.mod = (uint8_t)mods[k];
                    if (k == 3) /* the speed after a run of escapes: the controller's floor */
                        q->gm.spd = 8;
                    orders++;
                    q->last_input = q->prob_start = q->now;
                    int16_t y0[GM_NOBJ];
                    int top_run[GM_NOBJ] = {0}, worst_top = 0;
                    bool moved[GM_NOBJ] = {0};
                    for (int i = 0; i < GM_NOBJ; i++)
                        y0[i] = q->gm.o[i].y;
                    for (int f = 0; f < 1500 && q->sub == GS_PLAY; f++) { /* a minute, idle */
                        idle_frame();
                        for (int i = 0; i < GM_NOBJ; i++) {
                            mq_gobj_t *o = &q->gm.o[i];
                            if (o->kind != 1)
                                continue;
                            if (abs(o->y - y0[i]) >= 16 * 4)
                                moved[i] = true;
                            if (o->y / 16 < GFY + 12) /* its number under the order board */
                                top_run[i]++, worst_top = top_run[i] > worst_top ? top_run[i] : worst_top;
                            else
                                top_run[i] = 0;
                        }
                    }
                    for (int i = 0; i < GM_NOBJ; i++) /* (Cloud Catch's stars may wait on their hover line) */
                        if (q->gm.o[i].kind == 1 && !moved[i] && !(mech == ME_CATCH && q->gm.o[i].y / 16 >= GFY + 50) &&
                            stuck++ < 6)
                            fprintf(stderr, "floating: skill %d d%d %s twist %d: answer %d stuck at y %d (spd %d)\n", sk, d,
                                    mq_mech_name(mech), mods[k], q->gm.o[i].opt, q->gm.o[i].y / 16, q->gm.spd);
                    if (worst_top > 100 && hidden++ < 6) /* 4 s under the board */
                        fprintf(stderr, "floating: skill %d d%d %s twist %d: an answer waits under the board %d frames\n",
                                sk, d, mq_mech_name(mech), mods[k], worst_top);
                }
    if (getenv("MQ_TRACE"))
        fprintf(stderr, "floating: %d orders, %d stuck, %d hidden\n", orders, stuck, hidden);
    CHECK(orders > 600 && stuck == 0 && hidden == 0);
    /* a bubble at the top edge: a tap on the order board above it pops nothing; its part in the field pops */
    q->gm.mod = 0;
    CHECK(order_in(S_GROUPS, 1, ME_POP));
    mq_gobj_t *o = obj_of(mq_game_right_opt(q));
    CHECK(o != NULL);
    o->y = (int16_t)((GFY + 2) * 16), o->vy = 0;
    tev(1, o->x / 16, GFY - 6);
    CHECK(q->sub == GS_PLAY && !q->tried_wrong);
    tev(1, o->x / 16, GFY + 10);
    CHECK(q->sub == GS_CHEER);
    /* while the friend talks the answers drift on, and come round again (never far off the top) */
    CHECK(order_in(S_GROUPS, 1, ME_POP));
    q->sub = GS_WHY;
    for (int f = 0; f < 3000; f++)
        idle_frame();
    for (int i = 0; i < GM_NOBJ; i++)
        if (q->gm.o[i].kind == 1)
            CHECK(q->gm.o[i].y / 16 >= GFY - 20 && q->gm.o[i].y / 16 <= GFB + 20);
    q->sub = GS_PLAY;
}

/* Every game's opening frame, for every skill whose model may not show the answer: the frame the
 * player sees while an order waits never draws as many countable things as the answer (the card,
 * the stage, the sort card, the pie, the till: whatever the game puts on the screen). */
static void test_no_giveaway_games(void)
{
    con("new Una 2");
    con("grant bosses");
    q->fun_off = true;
    int bad = 0, frames = 0;
    for (int sk = 0; sk < MQ_NSKILL; sk++) {
        if (mq_skill[sk].visual_may_reveal == VR_MAY)
            continue;
        int games[5], ng = mq_skill_games(sk, games);
        if (getenv("MQ_TRACE"))
            fprintf(stderr, "skill %d\n", sk);
        games[ng++] = ME_POP; /* the interleaved review retells an order in the round's game */
        for (int gi = 0; gi < ng; gi++)
            for (int d = 0; d < mq_rungs(sk); d++)
                for (int i = 0; i < 3; i++) {
                    q->rng = (uint32_t)(sk * 97 + d * 13 + i * 7 + gi) | 1;
                    if (!order_in(sk, d, games[gi]))
                        break;
                    for (int st = 0; st < 2; st++) {
                        int r[4];
                        ops.draw(q, 0, r);
                        frames++;
                        if (q->prob.kind == K_NUM && q->frame_units && q->frame_units == q->prob.ans &&
                            !mq_rung_concrete(sk, q->prob.diff) && bad++ < 6)
                            fprintf(stderr, "skill %d (%s) d%d in %s: the frame shows %d countable things = the answer\n",
                                    sk, mq_skill[sk].name, d, mq_mech_name(games[gi]), q->frame_units);
                        con("step 5");
                    }
                }
    }
    CHECK(bad == 0);
    CHECK(frames > 1000);
    q->fun_off = false;
}

/* The board says a few words: every order of every skill, in each of its games, has at most 8
 * words on the board (the stage carries the numbers; the full story is a tap away), and the
 * line fits the bubble. */
/* The board says what to find: every skill in each of its games, every difficulty -- the whole order
 * fits the board (at most 3 lines; at most two short sentences after the scene's tag), and it ends
 * in a question or an instruction that names what to find ("How many kg go in each basket?", not
 * "How much in each?"; the playtest: "5.4kg into 6 baskets" did not say what to do). */
static bool word_in(const char *w, const char *const *list)
{
    for (int i = 0; list[i]; i++)
        if (!strcasecmp(w, list[i]))
            return true;
    return false;
}
static bool board_asks(const char *s, char *why, size_t wn)
{
    /* the sentences (a hint in brackets at the end aside) */
    char t[240];
    snprintf(t, sizeof t, "%s", s);
    char *e = t + strlen(t);
    while (e > t && e[-1] == ' ')
        *--e = 0;
    if (e > t && e[-1] == ')') {
        char *o = strrchr(t, '(');
        if (o)
            *o = 0, e = o;
    }
    while (e > t && e[-1] == ' ')
        *--e = 0;
    char *sent[12];
    int ns = 0;
    sent[ns++] = t;
    for (char *c = t; c < e - 1 && ns < 12; c++)
        if ((*c == '.' || *c == '!' || *c == '?' || *c == ':') && c[1] == ' ' && c > t && c[-1] != ' ' && c[-1] != 0) {
            if (*c == ':')
                *c = 0; /* (a colon ends a lead-in) */
            c[1] = 0, sent[ns++] = c + 2;
        }
    /* the last sentence that is not just the numbers ("left 202, right 282", "3/4  ?  2/3") */
    int k = ns - 1;
    while (k > 0) {
        const char *c = sent[k];
        size_t n = strlen(c);
        bool data = n && c[n - 1] != '?' && !((c[0] >= 'A' && c[0] <= 'Z'));
        bool qmark_data = strstr(c, "  ?  ") != NULL, eq = strchr(c, '?') && strchr(c, '=');
        if ((!data && !qmark_data) || eq)
            break;
        k--;
    }
    const char *last = sent[k];
    static const char *const ask[] = {"how", "what", "which", "where", "when", "who", "is", "are", "can", "does", "do",
                                      "will", "about", NULL};
    static const char *const verbs[] = {"find", "share", "count", "make", "hop", "frost", "write", "show", "tap", "pick",
                                        "put", "fill", "round", "sort", "set", "build", "cut", "measure", "shade", "order",
                                        "split", "name", "read", "add", "take", "turn", "jump", "mark", "give", "choose",
                                        "work", "fix", "plant", "bake", "pop", "catch", "ring", "draw", "place", "drag",
                                        "aim", "dig", "fling", "glue", "bop", "pay", "type", "stack", "pour", "point",
                                        "help", NULL};
    static const char *const stop[] = {"how", "many", "much", "long", "what", "which", "where", "when", "who", "the",
                                       "and", "each", "all", "now", "that", "more", "left", "other", "is", "it", "so",
                                       "are", "there", "does", "do", "can", "will", "in", "of", "to", "for", "her", "his",
                                       "their", "them", "they", "this", "with", "one", "on", "at", "get", "gets", "a",
                                       "an", "be", "too", "please", "it's", "its", "same", "about", "or", "big", NULL};
    char tmp[240], w1[24] = "";
    snprintf(tmp, sizeof tmp, "%s", last);
    sscanf(last, "%23[A-Za-z]", w1);
    size_t n = strlen(last);
    bool qend = n && last[n - 1] == '?', has_ask = false, has_or = strstr(last, " or ") != NULL;
    bool digits = strpbrk(last, "0123456789") != NULL, unknown = digits && (strstr(last, "?") && (strchr(last, '=') || strchr(last, ',')));
    int named = 0;
    for (char *w = strtok(tmp, " ?!.,:;\""); w; w = strtok(NULL, " ?!.,:;\"")) {
        if (word_in(w, ask))
            has_ask = true;
        if (((w[0] | 32) >= 'a' && (w[0] | 32) <= 'z') && !word_in(w, stop) && strcasecmp(w, w1))
            named++;
        else if (w[0] == '$' || (w[0] >= '0' && w[0] <= '9'))
            ; /* (a number is not what to find) */
    }
    if (word_in(w1, verbs) && !qend) /* an instruction: "Hop to 2/3!", "Drag the corner to make it!" */
        return true;
    if (unknown) /* the unknown marked in the sum: "42 ÷ 7 = ?", "2, 4, 6, ?" */
        return true;
    if (!qend || !(has_ask || has_or)) {
        snprintf(why, wn, "no question or instruction (\"%s\")", last);
        return false;
    }
    if (!named) {
        snprintf(why, wn, "the question names nothing to find (\"%s\")", last);
        return false;
    }
    return true;
}
static void test_board_words(bool print)
{
    con("new Wes 2");
    int bad = 0, n = 0;
    for (int sk = 0; sk < MQ_NSKILL; sk++)
        for (int k = 0; k < 48; k++) {
            int games[4], ng = mq_skill_games(sk, games), d = k % mq_rungs(sk);
            q->rng = (uint32_t)(sk * 31 + k * 7) | 1;
            if (!order_in(sk, d, games[k / 4 % ng]))
                continue;
            const mq_prob_t *p = &q->prob;
            char all[200], l[3][MQ_TXT], why[120] = "";
            int lines = mq_board_text(q, p, all, sizeof all, l);
            n++;
            if (print)
                printf("%2d %d %-18s %-12s %d | %s\n", sk, d, mq_skill[sk].name, mq_mech_name(q->gm.mech), lines, all);
            bool ok = lines <= 3 && board_asks(all, why, sizeof why);
            if (lines > 3)
                snprintf(why, sizeof why, "too long for the board");
            if (!ok && bad++ < (getenv("MQ_ALL") ? 100000 : 12))
                fprintf(stderr, "skill %d (%s) d%d in %s: %s: %s\n", sk, mq_skill[sk].name, d, mq_mech_name(q->gm.mech), why, all);
        }
    if (bad)
        fprintf(stderr, "board: %d of %d orders unclear\n", bad, n);
    CHECK(bad == 0);
    CHECK(n > 3000);
    /* Shape families: exactly one right answer -- never a family the drawn shape is also in (a square is a
     * rectangle and a rhombus too; the audit's G1) */
    static const char *const fam[3][6] = {{"rectangle", "rhombus", "trapezoid", "parallelogram", "kite", NULL},
                                          {"trapezoid", "parallelogram", NULL}, {"trapezoid", "parallelogram", "kite", NULL}};
    int shapes = 0, two = 0;
    for (int k = 0; k < 300; k++) {
        mq_prob_t sp;
        q->rng = (uint32_t)k * 2654435761u | 1;
        mq_generate(q, &sp, S_SHAPES, k % 4);
        int sh = sp.v[0];
        shapes++;
        if (sh > 2)
            continue;
        for (int i = 0; i < sp.nchoice; i++)
            for (int j = 0; fam[sh][j]; j++)
                two += i != sp.ans && !strcmp(sp.choice[i], fam[sh][j]);
    }
    CHECK(shapes == 300 && two == 0);
}

/* Kart Race: a real race. Rivals keep within reach while she thinks (and never pass her then),
 * boosts overtake, a dud spins her, and the place at the finish goes on the podium. */
static void test_kart(void)
{
    con("new Kai 2");
    con("grant bosses");
    con("engine stop"); /* the stop's own skill: a race of its orders */
    q->fun_off = true;
    con("level 1 4"); /* the Post Office's "Times tens": a race the first time */
    CHECK(q->gm.mech == ME_KART && q->gm.round_mech == ME_KART);
    con("step 2");
    CHECK(q->gm.race_on && mq_kart_place(q) == 4); /* she starts at the back */
    con("step 1500"); /* a long think: a minute */
    int32_t lead = 0;
    for (int i = 0; i < KART_RIVALS; i++)
        lead = q->gm.rv_d[i] - q->gm.race_d > lead ? q->gm.rv_d[i] - q->gm.race_d : lead;
    CHECK(lead > 0 && lead < 200 * 16); /* still in reach */
    CHECK(q->sub == GS_PLAY);
    con("correct");
    cheer_out();
    int after1 = mq_kart_place(q);
    CHECK(after1 <= 3); /* a boost: overtakes */
    /* nobody passes her while she thinks */
    int before = mq_kart_place(q);
    con("step 600");
    CHECK(mq_kart_place(q) <= before);
    int32_t d0 = q->gm.race_d;
    if (getenv("MQ_TRACE"))
        fprintf(stderr, "kart: mech %d skill %d sub %d nopt %d\n", q->gm.mech, q->prob.skill, q->sub, q->gm.nopt);
    con("try"); /* a dud: a little spin */
    CHECK(q->gm.spin_t != 0);
    con("step 10");
    CHECK(q->gm.race_d - d0 < 10 * 40); /* KART_V */ /* slower than cruising */
    for (int k = 0; k < 10 && q->scene == SC_PLAY; k++) {
        con("correct");
        cheer_out();
    }
    CHECK(q->scene == SC_DONE_LEVEL);
    CHECK(q->gm.podium >= 1 && q->gm.podium <= 2);
    q->fun_off = false;
}

/* The town: a street she walks along. A tap on a building walks her there and in; locked
 * places stay shut; a drag looks along the street; LEFT / RIGHT walk place to place; the pet
 * follows her. */
/* the story acted out before an order: the game waits, a tap skips it, it ends by itself, the
 * answer's clock starts when it ends; every stage kind fits the field */
static void test_scene(void)
{
    fresh_module();
    q->scenes_off = false;
    con("seed 5");
    con("new Ari 1");
    con("level 0 0"); /* Bubble Pop, a story with a stage */
    CHECK(q->scene == SC_PLAY && q->prob.stg && q->gm.scene_on);
    int16_t y0[GM_NOBJ];
    for (int i = 0; i < GM_NOBJ; i++)
        y0[i] = q->gm.o[i].y;
    con("step 10");
    CHECK(q->gm.scene_on);
    for (int i = 0; i < GM_NOBJ; i++) /* the game waits */
        CHECK(q->gm.o[i].y == y0[i]);
    con("step 40");
    CHECK(!q->gm.scene_on); /* over by itself (1.9 s) */
    con("correct");
    con("step 50");
    if (q->sub == GS_PLAY && q->prob.stg) {
        CHECK(q->gm.scene_on);
        con("tap 160 150"); /* a tap: on with the game, and the tap does nothing else */
        CHECK(!q->gm.scene_on && q->sub == GS_PLAY && !q->tried_wrong);
    }
    /* the pace adapts: full the first time a story type is seen, quicker the next two times, then
     * a half-second flourish (the friend already there); counted per story type in the save */
    CHECK(mq_scene_ms(0) == MQ_SCENE_FULL && mq_scene_ms(1) == MQ_SCENE_SHORT && mq_scene_ms(2) == MQ_SCENE_SHORT &&
          mq_scene_ms(3) == MQ_SCENE_QUICK && mq_scene_ms(200) == MQ_SCENE_QUICK && MQ_SCENE_QUICK == 500);
    CHECK(mq_scene_el(700, MQ_SCENE_FULL) == 700 && mq_scene_el(550, MQ_SCENE_SHORT) == 950);
    CHECK(mq_scene_el(0, MQ_SCENE_QUICK) == 420 && mq_scene_el(MQ_SCENE_QUICK, MQ_SCENE_QUICK) == MQ_SCENE_FULL);
    con("level 0 0");
    CHECK(q->gm.scene_on && q->prob.stg);
    {
        int st = q->prob.stg - 1, was = mq_more(q)->acted[st];
        CHECK(was >= 1 && q->gm.scene_ms == mq_scene_ms(was - 1));
        mq_more(q)->acted[st] = 3;
        con("mech 0"); /* the order again: its story type seen three times */
        CHECK(q->gm.scene_on && q->gm.scene_ms == MQ_SCENE_QUICK && mq_more(q)->acted[st] == 4);
        con("step 10");
        CHECK(q->gm.scene_on);
        con("step 3");
        CHECK(!q->gm.scene_on); /* a flourish: over in half a second */
        mq_more(q)->acted[st] = 1;
        con("mech 0");
        CHECK(q->gm.scene_ms == MQ_SCENE_SHORT);
        con("tap 160 150"); /* a tap still skips it */
        CHECK(!q->gm.scene_on);
    }
    /* every stage kind acted, every frame: drawn inside the field */
    int kinds = 0;
    for (int s = 0; s < MQ_NSKILL; s++) {
        mq_prob_t p;
        mq_generate(q, &p, s, 1);
        if (!p.stg)
            continue;
        kinds++;
        q->prob = p;
        for (int el = 0; el <= 2000; el += 250) {
            int16_t c0 = q->clip_y0, c1 = q->clip_y1;
            q->clip_y0 = MQ_Y0 + 58, q->clip_y1 = MQ_Y0 + MQ_H;
            mq_stage_scene(q, &q->prob, el, MQ_Y0 + 58, MQ_Y0 + MQ_H, el == 2000);
            q->clip_y0 = c0, q->clip_y1 = c1;
            CHECK(!q->zm && !q->stg_rv); /* the zoom is put back */
        }
        CHECK(mq_stage_width(q, &q->prob) > 0);
    }
    CHECK(kinds > 10);
    fresh_module();
}

/* the friends: memories of what she did, chatter about them, hearts, their stories a step a play
 * day, the payoff's rewards, the games their stories open */
static void test_friends(void)
{
    fresh_module();
    con("new Ivy 3");
    mq_extra_t *e = mq_extra(q);
    mq_more_t *mo = mq_more(q);
    CHECK(e->energy == 10 && !mo->mem[0].kind);
    mq_prob_t p;
    memset(&p, 0, sizeof p);
    p.stg = ST_GROUPS + 1, p.stg_i = SI_COOKIE, p.ca = 2, p.cb = 2, p.kind = K_NUM, p.ans = 24, p.skill = S_GROUPS;
    mq_friends_right(q, &p, true);
    p.ans = 16;
    mq_friends_right(q, &p, true); /* the same kind, friend and day: it adds up */
    CHECK(mo->mem[0].kind == MEM_MADE && mo->mem[0].n == 40 && mo->mem[0].slot == 2 && !mo->mem[1].kind);
    CHECK(e->heart[2] == 2 && e->energy == 12 && e->count[CT_ORDERS] == 2);
    char line[64];
    mq_chatter(q, 2, 0, line, sizeof line);
    CHECK(!strcmp(line, "You made 40 cookies today!"));
    mq_prof(q)->days++; /* the next play day */
    mq_chatter(q, 2, 0, line, sizeof line);
    CHECK(!strcmp(line, "You made 40 cookies yesterday!"));
    mq_chatter(q, 0, 0, line, sizeof line); /* someone else heard about it */
    CHECK(strstr(line, "heard") != NULL && strstr(line, "40 cookies") != NULL);
    for (int i = 0; i < 12; i++) /* no longer only the newest eight: all twelve and the cookies are kept */
        mq_remember(q, MEM_HELP + (i % 2), 1, 0, 0, i);
    CHECK(mo->mem[12].kind == MEM_MADE && mo->mem[12].n == 40 && mo->remembered == 13);
    mq_chatter(q, 2, 0, line, sizeof line); /* Momo still remembers her cookies, twelve things later */
    CHECK(!strcmp(line, "You made 40 cookies yesterday!"));
    for (int i = 0; i < MQ_NMEMORY + 5; i++) /* the long memory is full: the oldest go */
        mq_remember(q, MEM_RACE, 7, i % 2, 0, i);
    CHECK(mo->mem[MQ_NMEMORY - 1].kind == MEM_RACE && mo->remembered == 13 + MQ_NMEMORY + 5);
    /* Pip's story: hearts, then a step a play day */
    CHECK(mq_chapter_ready(q, 0) == -1 && mq_chapter_news(q) == -1);
    mq_heart_add(q, 0, 2);
    CHECK(mq_chapter_ready(q, 0) == 0 && mq_chapter_news(q) == 0);
    mq_chapter_open(q, 0);
    CHECK(q->scene == SC_CHAPTER);
    int c0 = mq_home(q)->coins;
    for (int i = 0; i < 2 * mq_chapter_lines() && q->scene == SC_CHAPTER; i++)
        mq_chapter_next(q); /* the first press shows the whole line, the second goes on */
    CHECK(q->scene == SC_MAP && e->chap[0] == 1 && mq_home(q)->coins == c0 + 5);
    mq_heart_add(q, 0, 50);
    CHECK(mq_chapter_ready(q, 0) == -1); /* not again today */
    mq_prof(q)->days++;
    CHECK(mq_chapter_ready(q, 0) == 1);
    CHECK(mq_chapter_ready(q, 5) == -1); /* a friend she has not met yet */
    /* Momo's second step opens her kitchen; a payoff pays and is remembered */
    CHECK(!(e->games_open & 1));
    con("chapter 2");
    for (int i = 0; i < 8 && q->scene == SC_CHAPTER; i++)
        mq_chapter_next(q);
    con("chapter 2");
    for (int i = 0; i < 8 && q->scene == SC_CHAPTER; i++)
        mq_chapter_next(q);
    CHECK(e->chap[2] == 2 && (e->games_open & (1u << EG_COOK)));
    con("chapter 2");
    for (int i = 0; i < 8 && q->scene == SC_CHAPTER; i++)
        mq_chapter_next(q);
    c0 = mq_home(q)->coins;
    con("chapter 2");
    for (int i = 0; i < 8 && q->scene == SC_CHAPTER; i++)
        mq_chapter_next(q);
    CHECK(e->chap[2] == 4 && mq_home(q)->coins == c0 + 40 && mq_more(q)->mem[0].kind == MEM_CHAPTER);
    CHECK(mq_chapter_ready(q, 2) == -1 && con("chapter 2") != 0);
    /* saved and loaded */
    mq_save_now(q);
    fresh_module();
    CHECK(mq_extra(q)->chap[2] == 4 && mq_extra(q)->chap[0] == 1 && mq_extra(q)->heart[0] >= 52);
    fresh_module();
}

/* twists (modifiers): a twist every third round or so, only where it fits; a mirrored or upside-down
 * field takes touches and arrows the way they look; surprises by play day: a visitor with a wish
 * granted in the kitchen, lantern night (night mode), bubble day, a parade (double hearts) */
static void test_twists(void)
{
    fresh_module();
    con("new Kit 4");
    q->fun_off = true;
    mq_prof(q)->problems = 20;
    int twists = 0;
    for (int r = 0; r < 9; r++) {
        con("level 0 0");
        if (q->gm.mod) {
            twists++;
            CHECK(mq_mod_fits(q->gm.mod, q->gm.round_mech));
        }
    }
    CHECK(twists >= 2 && twists <= 4);
    CHECK(mq_mod_fits(MOD_NIGHT, ME_TILL) && !mq_mod_fits(MOD_GIANT, ME_GATES) && !mq_mod_fits(MOD_FLIP, ME_GATES));
    /* Gate Dash in a mirror world: the lane that looks right is the right one; the arrows swap */
    con("skill 17 1");
    con("mech 2");
    con("twist 4");
    CHECK(q->gm.mech == ME_GATES && mq_mod_now(q) == MOD_MIRROR);
    con("twist 1"); /* night mode draws (tinted backdrop) and leaves no tint behind */
    ops.draw(q, 0, (int[4]){0, 0, 0, 0});
    CHECK(!q->tint && !q->flipx && !q->flipy);
    con("twist 3");
    ops.draw(q, 0, (int[4]){0, 0, 0, 0});
    CHECK(!q->flipx && !q->flipy);
    /* a flipped Bubble Pop: a tap on the mirrored place pops the bubble that is drawn there */
    con("skill 6 1");
    con("mech 0");
    con("twist 3");
    con("step 30");
    int r = mq_game_right_opt(q);
    for (int i = 0; i < GM_NOBJ; i++)
        if (q->gm.o[i].kind == 1 /* GO_ANS */ && q->gm.o[i].opt == r) {
            int x = q->gm.o[i].x / 16, y = q->gm.o[i].y / 16, fy = 2 * ((MQ_Y0 + 58 + MQ_Y0 + MQ_H) / 2) - y - 1;
            if (y < MQ_Y0 + 60 || y > MQ_Y0 + MQ_H - 4)
                break;
            ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(1, x, fy));
            CHECK(q->sub == GS_CHEER);
            break;
        }
    /* surprises by play day: announced in town, and the one away longest comes next -- all eight
     * visitors and all nine festivals before any comes back */
    con("go map");
    int seen[EV_COUNT] = {0}, vday = -1, first_vis[MQ_NVISITOR], first_fest[EV_NFEST], nv = 0, nf = 0;
    for (int i = 0; i < MQ_NVISITOR; i++)
        first_vis[i] = -1;
    for (int i = 0; i < EV_NFEST; i++)
        first_fest[i] = -1;
    bool repeat_early = false;
    for (int d = 1; d <= 80; d++) {
        char b[16];
        snprintf(b, sizeof b, "day %d", d);
        con(b); /* (on the map: announced) */
        int e = mq_event_today(q), a = mq_event_arg(q);
        seen[e]++;
        if (d < 3)
            CHECK(e == EV_NONE);
        if (e == EV_VISITOR) {
            CHECK(mq_visitor(q, NULL) == a && mq_more(q)->vis_day[a] == d);
            if (first_vis[a] < 0)
                first_vis[a] = d, nv++;
            else if (nv < MQ_NVISITOR)
                repeat_early = true;
            if (vday < 0)
                vday = d;
        } else if (e >= EV_FEST0) {
            CHECK(mq_more(q)->fest_day[e - EV_FEST0] == d && (mq_more(q)->fests_seen >> e) & 1);
            if (first_fest[e - EV_FEST0] < 0)
                first_fest[e - EV_FEST0] = d, nf++;
            else if (nf < EV_NFEST)
                repeat_early = true;
        }
        CHECK(mq_event_today(q) == e); /* the same all day once announced */
    }
    CHECK(!repeat_early && nv == MQ_NVISITOR && nf == EV_NFEST && mq_more(q)->visitors_met == 0xFF);
    for (int e = 0; e < EV_COUNT; e++)
        CHECK(seen[e] > 0);
    CHECK(seen[EV_NONE] > 12 && seen[EV_NONE] < 45);
    /* every visitor and festival drawn in town, every visitor's name and greeting fit */
    for (int v = 0; v < MQ_NVISITOR; v++) {
        con("go map");
        char b[24];
        snprintf(b, sizeof b, "event visitor %d", v);
        CHECK(con(b) == 0 && mq_visitor(q, NULL) == v && q->earn_msg[0]);
        CHECK(q->api->text_width(q->api->font_small, q->earn_msg) <= 300);
    }
    /* the visitor's wish, granted in the kitchen */
    mq_prof(q)->days = (uint16_t)vday;
    con("event visitor 5");
    int wish = 0, v = mq_visitor(q, &wish);
    CHECK(v == 5 && wish && mq_extra(q)->visit_day == vday);
    con("merge open");
    CHECK(wish == CK_WHOLE(CK_PINK)); /* a whole cake */
    mq_extra(q)->board[0] = (uint8_t)wish;
    int coins = mq_home(q)->coins;
    CHECK(mq_merge_visitor(q, 0) == 0 && mq_visitor(q, NULL) < 0 && mq_home(q)->coins > coins);
    /* lantern night and the star shower: the rounds are played at night */
    con("event lantern");
    con("level 0 0");
    CHECK(q->gm.mod == MOD_NIGHT);
    con("event stars");
    con("level 0 0");
    CHECK(q->gm.mod == MOD_NIGHT && mq_town_time(q) == TINT_NIGHT);
    /* a parade: hearts count double; a birthday: hearts with that friend */
    con("event parade");
    int h = mq_extra(q)->heart[3];
    mq_heart_add(q, 3, 2);
    CHECK(mq_extra(q)->heart[3] == h + 4);
    con("event birthday 2");
    CHECK(mq_events_birthday(q) == 2);
    h = mq_extra(q)->heart[2];
    int h3 = mq_extra(q)->heart[3];
    mq_heart_add(q, 2, 1), mq_heart_add(q, 3, 1);
    CHECK(mq_extra(q)->heart[2] == h + 2 && mq_extra(q)->heart[3] == h3 + 1);
    /* a picnic: double sparkle energy for an order */
    con("event picnic");
    int en = mq_extra(q)->energy;
    mq_prob_t pp;
    memset(&pp, 0, sizeof pp);
    pp.ca = pp.cb = 1, pp.skill = S_GROUPS;
    mq_friends_right(q, &pp, true);
    CHECK(mq_extra(q)->energy == (en + 2 > 60 ? 60 : en + 2));
    /* the friends talk about it */
    con("event snow");
    int said = 0;
    for (int k = 0; k < 12; k++) {
        char line[48];
        mq_chatter(q, 1, k, line, sizeof line);
        said += !strcmp(line, "Let's build a snowman!");
    }
    CHECK(said > 0);
    con("event none");
    fresh_module();
}

/* the games friends' stories open: Cake Kitchen (type the amount, mix on the beat for a star
 * more) and Pet Shop (prices and change, the till's coins and keys); they join the rounds of the
 * stops they suit once opened; their bests are kept apart from the older games' */
static void test_new_games(void)
{
    fresh_module();
    con("new Bea 2");
    q->fun_off = true;
    int c[6];
    for (int s = 0; s < MQ_NSKILL; s++) { /* not before they are opened */
        CHECK(mq_round_mech(q, s) != ME_COOK && mq_round_mech(q, s) != ME_PETS);
        (void)c;
    }
    mq_extra(q)->games_open = 3;
    int cooks = 0, pets = 0;
    for (int p = 0; p < 6; p++) {
        mq_prof(q)->sk[S_GROUPS].plays = (uint8_t)p, mq_prof(q)->sk[S_CHANGE].plays = (uint8_t)p;
        cooks += mq_round_mech(q, S_GROUPS) == ME_COOK, pets += mq_round_mech(q, S_CHANGE) == ME_PETS;
    }
    CHECK(cooks >= 1 && pets >= 1);
    CHECK(order_in(S_GROUPS, 1, ME_COOK) && q->gm.nopt >= 3); /* the amounts: labelled choices */
    char t[24];
    /* on the beat: the beat clock started with the order's round */
    q->gm.beat0 = q->anim_t - mq_beat_ms(0) * 3; /* exactly on a beat now */
    int st = q->gm.stars;
    q->gm.sel = (uint8_t)mq_game_right_opt(q);
    gkey(49); /* (ENTER on the one in focus) */
    CHECK(q->sub == GS_CHEER && q->gm.mix_j == 2 && q->gm.stars >= st + 2);
    CHECK(order_in(S_GROUPS, 1, ME_COOK));
    snprintf(t, sizeof t, "%s", q->gm.lab[(mq_game_right_opt(q) + 1) % q->gm.nopt]);
    gtype(t); /* (the keys of a wrong amount's label) */
    CHECK(q->sub != GS_CHEER); /* a wrong amount: not mixed right */
    CHECK(order_in(S_CHANGE, 1, ME_PETS));
    snprintf(t, sizeof t, "%ld", (long)q->prob.ans);
    gtype(t);
    gkey(49);
    CHECK(q->sub == GS_CHEER && mq_extra(q)->count[CT_PETS] == 1);
    CHECK(mq_best_of(q, ME_PETS) == &mq_extra(q)->best_new[1] && mq_best_of(q, ME_VAULT) == &mq_home(q)->best_game[ME_VAULT]);
    fresh_module();
}

static void test_town(void)
{
    con("new Tia 2");
    q->fun_off = true;
    con("go map");
    CHECK(q->scene == SC_MAP && mq_pet_id(q) != 0);
    int gx = mq_town_place_x(2); /* the Garden */
    con("town 140"); /* at her house */
    con("step 2");
    char t[32];
    snprintf(t, sizeof t, "tap %d 100", gx - q->town_cam); /* the Garden's greenhouse */
    con(t);
    for (int i = 0; i < 60 && q->scene == SC_MAP; i++)
        con("step 1");
    CHECK(q->scene == SC_WORLD && q->world == 0 && q->town_x == gx);
    con("go map");
    int px = mq_town_place_x(3); /* the Post Office: open from the start, like every place */
    con("town 300");
    snprintf(t, sizeof t, "tap %d 100", px - q->town_cam);
    con(t);
    for (int i = 0; i < 80 && q->scene == SC_MAP; i++)
        con("step 1");
    CHECK(q->scene == SC_WORLD && q->world == G_POST);
    con("go map");
    con("town 300");
    int cam = q->town_cam; /* a drag looks along the street */
    ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(1, 200, 120));
    ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(2, 120, 120));
    ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(3, 120, 120));
    CHECK(q->town_cam == cam + 80);
    con("town 140");
    gkey(14); /* RIGHT: walk to the next place */
    con("step 40");
    CHECK(q->town_x == mq_town_place_x(1) && q->scene == SC_MAP);
    CHECK(abs(q->pet_x - q->town_x) < 40); /* the pet trots after her */
    gkey(14), gkey(49); /* on to the Garden, and in */
    con("step 60");
    CHECK(q->scene == SC_WORLD);
    con("go map"); /* the Sparkle Kitchen, first on the street (LEFT from her house) */
    con("town 140");
    gkey(13), con("step 40"), gkey(49), con("step 40");
    CHECK(q->scene == SC_MERGE);
    /* walking never goes in (the user's report): tap-to-walk on the pavement in front of every place, the
     * arrows held down (key repeat), a drag and a swipe along the street -- she stays in the town; only a tap on a
     * building, its "go in!" bubble, or ENTER / UP takes her in */
    con("go map");
    int entered = 0;
    for (int pl = 0; pl < 14; pl++) {
        int x = mq_town_place_x(pl);
        if (x <= 0)
            continue;
        char tt[32];
        snprintf(tt, sizeof tt, "town %d", x > 200 ? x - 160 : x + 160);
        con(tt), con("step 2");
        tev(4, x - q->town_cam, MQ_Y0 + 186); /* the pavement right in front of its door */
        for (int i = 0; i < 80; i++)
            con("step 1");
        entered += q->scene != SC_MAP;
        if (q->scene != SC_MAP)
            con("go map");
    }
    CHECK(entered == 0);
    con("town 140");
    for (int k = 0; k < 30; k++) /* RIGHT held: the keyboard's repeat */
        gkey(14), con("step 3");
    con("step 60");
    CHECK(q->scene == SC_MAP);
    for (int k = 0; k < 30; k++)
        gkey(13), con("step 3");
    CHECK(q->scene == SC_MAP);
    tev(1, 280, MQ_Y0 + 186), tev(2, 200, MQ_Y0 + 186), tev(2, 60, MQ_Y0 + 186), tev(3, 60, MQ_Y0 + 186); /* a drag */
    ops.event(q, MODX_EV_SWIPE, MODX_SWIPE(0, 280, MQ_Y0 + 120));                                    /* a swipe */
    for (int d = 0; d < 4; d++) /* swipes every way, on the street and below it: only looking (an upward one went into the
                                 * arcade: the bot's long run) */
        ops.event(q, MODX_EV_SWIPE, MODX_SWIPE(d, 160, MQ_Y0 + (d % 2 ? 120 : 196)));
    con("step 60");
    CHECK(q->scene == SC_MAP);
    /* stopped at a door: the "go in!" bubble takes her in */
    con("town 300"), con("step 2");
    gkey(14), con("step 60");
    CHECK(q->scene == SC_MAP && q->town_x == q->town_tx);
    int hx = q->town_x - q->town_cam;
    tev(4, (hx + 14 + 22 < 316 ? hx + 14 + 22 : 296), MQ_Y0 + 180 - 38 + 7);
    con("step 4");
    CHECK(q->scene != SC_MAP);
    q->fun_off = false;
}

/* The Sparkle Kitchen in the town's flow (the counter itself: test_mq_kitchen.c): opened from the street, an order
 * waiting, the oven spends a sparkle only after the first-time demo's free cake, and no clock brings sparkles */
static void test_merge(void)
{
    fresh_module();
    con("new Zoe 1");
    q->fun_off = true;
    mq_extra_t *e = mq_extra(q);
    con("merge open");
    CHECK(q->scene == SC_MERGE && e->mb_seen == 3 && e->ck_who == 1 && (e->ck_flags & KF_TUTOR));
    int en = e->energy;
    CHECK(mq_merge_gen(q, 0) >= 0 && e->energy == en); /* (the demo's cake is on the house) */
    con("merge demo off");
    con("merge order 3 4");
    CHECK(mq_merge_gen(q, 0) >= 0 && e->energy == en - 1);
    e->energy = 0;
    CHECK(mq_merge_gen(q, 0) < 0); /* no sparkles: nothing (and no clock brings them) */
    con("step 500");
    CHECK(e->energy == 0);
    mq_prob_t p;
    memset(&p, 0, sizeof p);
    p.skill = S_GROUPS, p.ca = 1, p.cb = 1;
    mq_friends_right(q, &p, true); /* sparkles come from playing */
    CHECK(e->energy == 1);
    fresh_module();
}

/* Typed answers only in the calm games (backlog (b)): the action games (Bubble Pop, Cloud Catch, Gate Dash,
 * Kart Race, Peekaboo) and the kitchen's rhythm game always offer labelled choices -- never a number to make
 * or type -- for every order and every turn; the till takes the change typed (a calm game). And a new
 * player in a slot starts with an empty learning engine. */
static void test_make(void)
{
    con("new Mae 2");
    q->fun_off = true;
    mq_prof(q)->problems = 20;
    char t[16];
    static const int mk[5] = {ME_POP, ME_CATCH, ME_GATES, ME_KART, ME_ZAP};
    for (int mech = 0; mech < 5; mech++) {
        int m = mk[mech], picks = 0;
        for (int i = 0; i < 6; i++) {
            order_turn = i;
            if (!order_in(S_GROUPS, 1, m))
                continue;
            CHECK(!q->gm.make && q->gm.nopt >= 2 && mq_game_right_opt(q) >= 0); /* labelled choices, the right one there */
            picks++;
        }
        CHECK(picks >= 3);
    }
    /* the kitchen: the amounts are buttons; the right one on the beat mixes a perfect layer */
    mq_extra(q)->games_open = 3;
    order_turn = -1;
    CHECK(order_in(S_GROUPS, 1, ME_COOK));
    CHECK(q->gm.nopt >= 3 && mq_game_right_opt(q) >= 0);
    q->gm.beat0 = q->anim_t - mq_beat_ms(0) * 3; /* exactly on a beat now */
    int st = q->gm.stars;
    snprintf(t, sizeof t, "%s", q->gm.lab[mq_game_right_opt(q)]);
    gtype(t); /* (the keys of its label pick it) */
    CHECK(q->sub == GS_CHEER && q->gm.mix_j == 2 && q->gm.stars >= st + 2);
    /* the till: the change typed on its keys */
    int typed = 0;
    for (int i = 0; i < 4 && !typed; i++) {
        order_turn = i;
        if (order_in(S_CHANGE, 1, ME_TILL) && q->gm.mode == 2) {
            typed = 1;
            snprintf(t, sizeof t, "%ld", (long)q->prob.ans);
            gtype(t);
            gkey(49);
            CHECK(q->sub == GS_CHEER);
        }
    }
    CHECK(typed);
    /* a reset player: the learning engine starts empty */
    mq_learn_t *L = mq_learn(q);
    CHECK(L->seen[0].ease || L->rev[S_GROUPS].box || L->t10[S_GROUPS]);
    con("go parent");
    q->tab = 3;
    mq_press(q, B_RESET, 0), mq_press(q, B_RESET, 0);
    CHECK(!L->seen[0].ease && !L->rev[S_GROUPS].box && !L->t10[S_GROUPS]);
    order_turn = -1;
    q->fun_off = false;
}

/* The flower games, played the way a finger plays them (the core sends DOWN before every TAP and
 * DRAGs between): Build It's flower bed in equal rows (and the rugs and fences that share its
 * code), for many seeds and every difficulty; the Garden's flower-bed arrays; Blooming Garden.
 * Each check is a bug found in the playtest pass (docs/MATHQUEST.md, "The flower games"). */
static int bed_errs;
#define BED_CHECK(c, ...)                                         \
    do {                                                          \
        checks++;                                                 \
        if (!(c)) {                                               \
            fails++;                                              \
            if (bed_errs++ < 8) {                                 \
                fprintf(stderr, "flowers: " __VA_ARGS__);         \
                fprintf(stderr, " (%s)\n", #c);                   \
            }                                                     \
        }                                                         \
    } while (0)
static void fg_type(int v)
{
    snprintf(q->input, sizeof q->input, "%d", v);
    q->in_len = (uint8_t)strlen(q->input);
    mq_fg_submit(q);
}
static void test_flowers(void)
{
    con("new Fia 2");
    con("grant bosses");
    q->fun_off = true;
    static const int sks[] = {S_SHARE, S_DIVFACT, S_UNKNOWN, S_AREAMUL, S_AREACOUNT, S_FORMULA, S_PERIM};
    int beds = 0, built = 0;
    for (size_t si = 0; si < sizeof sks / sizeof sks[0]; si++)
        for (int d = 0; d <= 3; d++)
            for (int seed = 1; seed <= 40; seed++) {
                q->rng = (uint32_t)(seed * 2654435761u) | 1;
                if (!order_in(sks[si], d, ME_BUILD))
                    continue;
                mq_game_t *g = &q->gm;
                int sk = sks[si], mode = g->mode, cell = mq_build_cell(g), ox = 20, oy = GFY + 10;
                int want = (int)q->prob.ans;
                built++;
                beds += mode == 3;
                /* the plot fits the field and the answer fits the plot (no overflow at big factors) */
                BED_CHECK(ox + g->maxw * cell < 250 && oy + g->maxh * cell < GFB - 4, "skill %d d%d: the plot overflows", sk, d);
                BED_CHECK(want >= 1 && want <= (g->fixh ? g->maxw : g->maxh), "skill %d d%d: answer %d does not fit", sk, d, want);
                if (mode == 3) { /* rows x columns: the rows are given (the bed's height), the flowers in a row are built */
                    BED_CHECK(g->fixh >= 2 && g->bh == g->fixh && g->fixw == 0, "bed rows");
                    char t[200], n1[24], n2[24];
                    snprintf(t, sizeof t, "%s %s %s", q->prob.text[0], q->prob.text[1], q->prob.text[2]);
                    snprintf(n1, sizeof n1, "%d flowers", g->fixh * want);
                    snprintf(n2, sizeof n2, "%d equal rows", g->fixh);
                    BED_CHECK(strstr(t, n1) && strstr(t, n2), "the story names the total and the rows: %s", t);
                }
                /* drag the handle from where it is to the answer, then press done! as a finger does */
                int hx = ox + g->bw * cell, hy = oy + g->bh * cell;
                int tx = g->fixh ? ox + want * cell : hx, ty = g->fixh ? hy : oy + want * cell;
                tev(1, hx, hy);
                tev(2, (hx + tx) / 2, (hy + ty) / 2);
                tev(2, tx + 1, ty - 1);
                tev(3, tx + 1, ty - 1);
                BED_CHECK((g->fixh ? g->bw : g->bh) == want && (g->fixh ? g->bh == g->fixh : g->bw == g->fixw),
                          "skill %d d%d: dragged to %d, built %d x %d", sk, d, want, g->bw, g->bh);
                int r4[4];
                ops.draw(q, 0, r4);
                tev(1, 280, GFB - 18); /* done! (the finger goes down on the button first) */
                BED_CHECK((g->fixh ? g->bw : g->bh) == want, "skill %d d%d: pressing done! changed the bed to %d x %d", sk,
                          d, g->bw, g->bh);
                tev(4, 280, GFB - 18);
                BED_CHECK(q->sub == GS_CHEER, "skill %d d%d mode %d: the right bed was not judged right (%d x %d, want %d)", sk,
                          d, mode, g->bw, g->bh, want);
                /* a tap on the order board (to hear the story) leaves the bed alone */
                if (!order_in(sk, d, ME_BUILD))
                    continue;
                want = (int)q->prob.ans;
                int bw0 = g->bw, bh0 = g->bh;
                tev(1, 100, GFY - 20), tev(4, 100, GFY - 20);
                BED_CHECK(g->bw == bw0 && g->bh == bh0, "a tap on the board resized the bed");
                /* a drag that starts far from the plot does not build */
                tev(1, 300, GFY + 20), tev(2, 200, GFY + 60), tev(3, 200, GFY + 60);
                BED_CHECK(g->bw == bw0 && g->bh == bh0, "a drag away from the plot resized the bed");
                /* a wrong bed: a dud, the bed stays to fix, the friend says why; then the keys build the right one */
                int wrong = want > 1 ? want - 1 : want + 1;
                hx = ox + g->bw * cell, hy = oy + g->bh * cell;
                tev(1, hx, hy);
                tev(2, g->fixh ? ox + wrong * cell : hx, g->fixh ? hy : oy + wrong * cell);
                tev(3, g->fixh ? ox + wrong * cell : hx, g->fixh ? hy : oy + wrong * cell);
                tev(1, 280, GFB - 18), tev(4, 280, GFB - 18);
                BED_CHECK(q->tried_wrong && q->sub == GS_PLAY && g->say[0], "skill %d d%d: the wrong bed %d", sk, d, wrong);
                BED_CHECK((g->fixh ? g->bw : g->bh) == wrong, "the dud's bed stays to be fixed");
                gkey(g->fixh ? (wrong < want ? 14 : 13) : (wrong < want ? 15 : 12));
                gkey(49);
                BED_CHECK(q->sub == GS_CHEER, "skill %d d%d: the bed fixed by the keys", sk, d);
            }
    CHECK(beds > 50 && built > 300);
    /* the same kind of bug in the other games with a go button: the DOWN before the TAP on it must not
     * move the clock's hand (Clock Tower) or the aim (Sky Fling); a finger on the board must not move
     * Cloud Catch's cloud */
    for (int seed = 1; seed <= 30; seed++) {
        q->rng = (uint32_t)seed * 7777u | 1;
        if (order_in(S_TIME5, seed % 4, ME_CLOCK)) {
            q->gm.value = q->prob.ans; /* she set it right */
            tev(1, 280, GFB - 18);
            BED_CHECK(q->gm.value == q->prob.ans, "clock: pressing set! moved the hand (%d, want %d)", (int)q->gm.value, (int)q->prob.ans);
            tev(4, 280, GFB - 18);
            BED_CHECK(q->sub == GS_CHEER, "clock: the right time was not judged right");
        }
        if (order_in(S_PROTRACTOR, seed % 4, ME_LAUNCH) && q->prob.kind == K_NUM) {
            q->gm.aim = (int16_t)q->prob.ans;
            tev(1, 280, GFB - 18);
            BED_CHECK(q->gm.aim == q->prob.ans, "fling: pressing fling! moved the aim (%d, want %d)", q->gm.aim, (int)q->prob.ans);
            tev(4, 280, GFB - 18);
            for (int k = 0; k < 120 && q->sub == GS_PLAY; k++)
                con("step 1");
            BED_CHECK(q->sub == GS_CHEER, "fling: the right aim was not judged right");
        }
        if (order_in(S_GROUPS, seed % 4, ME_CATCH)) {
            int px = q->gm.px;
            tev(1, 60, GFY - 20), tev(4, 60, GFY - 20);
            BED_CHECK(q->gm.px == px, "catch: a tap on the board moved the cloud");
        }
    }
    /* the flower-bed arrays (Flip and split): the box's labels are the order's numbers; a given number
     * that happens to equal the answer is not hidden as "?" */
    for (int k = 0; k < 400; k++) {
        mq_prob_t p;
        q->rng = (uint32_t)k * 7919u + 3;
        mq_generate(q, &p, S_PROPS, k % 4);
        if (p.vis != V_ARRAY)
            continue;
        int a = 0;
        BED_CHECK(sscanf(p.text[1], "%d", &a) == 1 && a == p.v[0], "S_PROPS: the box's rows %d are the order's first number (%s)",
                  p.v[0], p.text[1]);
        if (!strncmp(p.text[0], "Split", 5))
            BED_CHECK(!mq_array_hides(&p, 0) && !mq_array_hides(&p, 1), "S_PROPS: a split bed's rows %d or columns %d shown as ? (ans %d)",
                      p.v[0], p.v[1], (int)p.ans);
        else
            BED_CHECK(mq_array_hides(&p, 0) + mq_array_hides(&p, 1) + mq_array_hides(&p, 2) == 1, "S_PROPS: one unknown");
    }
    /* Blooming Garden (and the other typed fact games): after a miss the hint shows the answer; typing
     * it then opens the flower but is no evidence of a known fact (no box up, no fast time) */
    con("go arcade");
    mq_fg_start(q, FG_BLOOM);
    int f = q->fg_cur;
    uint8_t t10 = mq_prof(q)->fact[f].t10;
    q->now += 1500;
    fg_type(mq_fact_answer(f) + 1);
    CHECK(q->fg_cur == f && mq_prof(q)->fact[f].box == 1 && q->fg_bloom == 0);
    q->now += 900;
    fg_type(mq_fact_answer(f));
    CHECK(q->fg_bloom == 1);                          /* the flower opens */
    CHECK(mq_prof(q)->fact[f].box == 1);              /* copying the hint is not a promotion */
    CHECK(mq_prof(q)->fact[f].t10 == t10);            /* nor a fast time */
    CHECK(q->fg_cur != f || q->nqueue);               /* and the fact comes back later */
    /* Star Catch: a miss counts against the star whose answer was closest, not always the lowest */
    mq_fg_start(q, FG_STARS);
    for (int i = 0; i < 2; i++)
        q->fall[i].alive = 1, q->fall[i].y = (int16_t)(MQ_Y0 + 70 - 30 * i);
    q->fall[0].fact = (int16_t)mq_fact_index(2, 3, false), q->fall[1].fact = (int16_t)mq_fact_index(9, 9, false);
    uint8_t seen0 = mq_prof(q)->fact[q->fall[0].fact].seen, seen1 = mq_prof(q)->fact[q->fall[1].fact].seen;
    fg_type(80); /* 80: meant for 9 x 9 (the higher star) */
    CHECK(mq_prof(q)->fact[q->fall[1].fact].seen == seen1 + 1 && mq_prof(q)->fact[q->fall[0].fact].seen == seen0);
    /* the moving fact games are answered by labelled choices (backlog (b)): both stars' answers and near
     * misses, in order; a tap on one (or the keys that spell it) catches; the calm ones keep the keypad */
    mq_fg_start(q, FG_STARS);
    for (int i = 0; i < 2; i++)
        q->fall[i].alive = 1, q->fall[i].y = (int16_t)(MQ_Y0 + 70 - 30 * i);
    q->fall[0].fact = (int16_t)mq_fact_index(6, 7, false), q->fall[1].fact = (int16_t)mq_fact_index(3, 4, false);
    int32_t ch[4];
    int nc = mq_fg_choices(q, ch), has42 = -1, has12 = 0;
    for (int i = 0; i < nc; i++)
        has42 = ch[i] == 42 ? i : has42, has12 |= ch[i] == 12;
    CHECK(mq_fg_choosing(q) && nc == 4 && has42 >= 0 && has12);
    for (int i = 1; i < nc; i++)
        CHECK(ch[i - 1] < ch[i]);
    int sc = q->fg_score;
    mq_fg_choose(q, has42);
    CHECK(q->fg_score == sc + 1); /* 6 x 7 caught */
    q->fall[1].fact = (int16_t)mq_fact_index(3, 4, false), q->fall[1].alive = 1;
    snprintf(q->input, sizeof q->input, "1"), q->in_len = 1, mq_fg_typed(q); /* "1": still two choices it could be */
    CHECK(q->fg_score == sc + 1 && q->in_len == 1);
    snprintf(q->input, sizeof q->input, "12"), q->in_len = 2, mq_fg_typed(q); /* the keys spell it: picked */
    CHECK(q->fg_score == sc + 2);
    mq_fg_start(q, FG_RACE);
    CHECK(mq_fg_choosing(q) && mq_fg_choices(q, ch) == 4);
    mq_fg_start(q, FG_CAFE);
    CHECK(!mq_fg_choosing(q));
    q->fun_off = false;
}

/* ------------------------------------------------------------ the engine: free exploration */
/* play the current order as a player of grade `grade` would: right when the skill is at or below
 * her grade (now and then a slip), else a miss; through the moments that follow */
static uint32_t eng_rng = 12345;
static int eng_rnd(int n)
{
    eng_rng = eng_rng * 1103515245u + 12345u;
    return (int)((eng_rng >> 16) % (uint32_t)n);
}
static bool eng_answer(int grade, int slip)
{
    if (q->scene != SC_PLAY || q->sub != GS_PLAY)
        return false;
    bool right = mq_skill[q->prob.skill].grade <= grade && eng_rnd(100) >= slip;
    q->now += q->prob.skill == S_FACTS ? 1000 + (uint32_t)eng_rnd(2000) /* (a fact she knows: quick) */
                                       : 2000 + (uint32_t)eng_rnd(3000); /* she thinks a little */
    q->fun_ms = 0;
    con(right ? "correct" : "wrong");
    /* the moments that follow; after a helper friend her own order again (together if missed again) */
    for (int k = 0; k < 60 && q->scene == SC_PLAY && (q->sub != GS_PLAY || q->gm.helped); k++) {
        if (q->sub == GS_PLAY && q->gm.helped) {
            bool r2 = q->gm.hoh || eng_rnd(100) < (mq_skill[q->prob.skill].grade <= grade ? 80 : 35);
            con(r2 ? "correct" : "wrong");
            if (q->sub == GS_CHEER)
                q->gm.helped = 0; /* (solved: on to the next) */
            continue;
        }
        con("next");
    }
    while (q->scene == SC_PLAY && q->sub != GS_PLAY)
        con("step 40");
    return right;
}

static void test_engine(void)
{
    /* 1. a new player can enter every world, every stop and finale, every game, toy and room at once;
     *    nothing on any of those screens says it is locked */
    con("new Ivy 2");
    q->fun_off = true;
    mq_profile_t *p = mq_prof(q);
    for (int w = 0; w < MQ_NWORLD; w++) {
        CHECK(mq_world_open(p, w));
        char b[32];
        snprintf(b, sizeof b, "world %d", w);
        CHECK(con(b) == 0 && q->scene == SC_WORLD);
        for (int l = 0; l <= mq_world_levels(w); l++) {
            CHECK(l == mq_world_levels(w) || mq_skill_unlocked(p, w, l));
            mq_press(q, l == mq_world_levels(w) ? B_BOSS : B_LEVEL, l);
            CHECK(q->scene == SC_INTRO);
            mq_go(q, SC_WORLD);
        }
    }
    /*    the world screen is no level select: no row of identical stops, 2 to 4 choices, each a different
     *    game with its own picture and name; a pick goes straight into a round of that game */
    for (int w = 0; w < MQ_NWORLD; w++) {
        char b[32];
        snprintf(b, sizeof b, "world %d", w);
        CHECK(con(b) == 0 && q->scene == SC_WORLD);
        ops.draw(q, 0, (int[4]){0, 0, 0, 0});
        int nc = 0, levels = 0, args = 0;
        for (int i = 0; i < q->nbtn; i++) {
            if (q->btn[i].id == B_LEVEL || q->btn[i].id == B_BOSS)
                levels++;
            if (q->btn[i].id == B_WGAME) {
                CHECK(!((args >> q->btn[i].arg) & 1)); /* never the same choice twice */
                args |= 1 << q->btn[i].arg, nc++;
            }
        }
        CHECK(levels == 0 && nc >= 2 && nc <= 4);
        int mech[3], stop[3], ng = mq_world_games(w, mech, stop);
        CHECK(ng + 1 == nc);
        for (int i = 0; i < ng; i++) {
            for (int j = 0; j < i; j++)
                CHECK(mech[i] != mech[j]);
            CHECK(con(b) == 0);
            mq_press(q, B_WGAME, i);
            CHECK(q->scene == SC_PLAY && q->gm.round_mech == mech[i]);
            if (getenv("MQ_TRACE"))
                fprintf(stderr, "world %d: %s\n", w, mq_mech_name(mech[i]));
        }
        CHECK(con(b) == 0);
        mq_press(q, B_WGAME, 3);
        CHECK(q->scene == SC_PLAY && q->boss);
        mq_go(q, SC_WORLD);
    }
    for (int g = 0; g < ACT_COUNT; g++)
        CHECK(mq_action_open(p, g));
    for (int r = 0; r < HM_NROOM; r++)
        CHECK(mq_room_open(p, r));
    for (int id = 1; id <= MQ_NITEM; id++) /* everything sold is on sale (a week's seasonal item aside) */
        if (mq_item[id].price && !(mq_item[id].flags & (F_GIFT | F_SPECIAL | F_RETIRED | F_EVENT)))
            CHECK(mq_item_unlocked(p, id));
    for (int t = 1; t < TOY_COUNT; t++) {
        mq_toys_open(q, t);
        CHECK(q->scene == SC_TOYS && q->toy.mode == t);
    }
    int cook = 0, pets = 0;
    for (int s = 0; s < MQ_NSKILL; s++) /* the story games are in the rounds from the start */
        for (int k = 0; k < 6; k++) {
            p->sk[s].plays = (uint8_t)k;
            int m = mq_round_mech(q, s);
            cook += m == ME_COOK, pets += m == ME_PETS;
        }
    CHECK(cook > 0 && pets > 0);
    for (int s = 0; s < MQ_NSKILL; s++)
        p->sk[s].plays = 0;

    /* 2. the placement: a new player of grade 4 starts in the Garden's first game; twelve orders across
     *    the four strands (her first round and part of the next), starting at mid-grade 3, a grade up
     *    only after two rights in a row, never above grade 4; never addition first, multiplication
     *    early; she then starts one grade below what she showed (grade 3), for confidence */
    con("new Kim 3");
    q->fun_off = true;
    p = mq_prof(q);
    CHECK(mq_start_grade(q) == 3 && mq_placing(q)); /* (a gentle default) */
    eng_rng = 99;
    con("level 0 0");
    CHECK(q->scene == SC_PLAY && q->run_n == 8 && q->placing_item);
    CHECK(mq_strand(q->prob.skill) == STR_MULDIV && mq_skill[q->prob.skill].grade == 3 && q->prob.diff == 1);
    int strands = 0, asked = 0, ptop = 0, prev_g[STR_COUNT] = {3, 3, 3, 3};
    while (mq_placing(q) && asked < 30) {
        if (q->scene != SC_PLAY)
            con("level 0 0");
        if (q->placing_item) {
            int k = mq_strand(q->prob.skill), g = mq_skill[q->prob.skill].grade;
            strands |= 1 << k;
            ptop = g > ptop ? g : ptop;
            CHECK(abs(g - prev_g[k]) <= 1); /* one grade at a time */
            prev_g[k] = g;
        }
        eng_answer(4, 0);
        asked++;
    }
    CHECK(strands == 15 && ptop == 4 && asked == 12); /* (never grade 5 in the placement) */
    CHECK(!mq_placing(q) && (mq_pace(q)->placed & PL_DONE));
    for (int k = 0; k < STR_COUNT; k++)
        CHECK(mq_pace(q)->strand[k] == 3);
    CHECK(mq_skill_assumed(q, S_PV3) && !mq_skill_assumed(q, S_ADDSUB) && !mq_skill_assumed(q, S_TIMES_AS));
    /* the goals that follow are grade 3 first, a few grade 4, never decimals or grade 5 yet; the mix */
    int g4 = 0, g3 = 0, late = 0, same = 0, n = 0, last = -1;
    for (int k = 0; k < 200; k++) {
        int s = mq_engine_pick(q, G_GARDEN, ME_POP);
        mq_engine_answered(q, s, false);
        g4 += mq_skill[s].grade >= 4, g3 += mq_skill[s].grade <= 3, n++;
        late += mq_skill[s].grade == 5 || mq_decimal_skill(s);
        same += last >= 0 && mq_strand(s) == mq_strand(last);
        last = s;
    }
    if (getenv("MQ_TRACE"))
        fprintf(stderr, "engine: after placement g4 %d g3 %d late %d same strand %d of %d\n", g4, g3, late, same, n);
    CHECK(g3 * 100 >= n * 60 && late == 0);
    CHECK(same * 100 <= n * 45);
    /* a grade-3 player: the placement steps down, she starts at grade 3 with multiplication */
    con("new Lia 1");
    q->fun_off = true;
    eng_rng = 5;
    con("level 1 0"); /* the Post Office first: the engine still starts with the strands in turn */
    for (int k = 0; k < 30 && mq_placing(q); k++) {
        if (q->scene != SC_PLAY)
            con("level 1 0");
        if (getenv("MQ_TRACE"))
            fprintf(stderr, "engine: placing %d skill %d (grade %d) i %d\n", q->placing_item, q->prob.skill,
                    mq_skill[q->prob.skill].grade, mq_pace(q)->place_i);
        eng_answer(3, 0);
    }
    if (getenv("MQ_TRACE"))
        fprintf(stderr, "engine: grade-3 placement %d %d %d %d\n", mq_pace(q)->strand[0], mq_pace(q)->strand[1],
                mq_pace(q)->strand[2], mq_pace(q)->strand[3]);
    CHECK(mq_pace(q)->strand[STR_MULDIV] == 3 && mq_pace(q)->strand[STR_PLACE] == 2);
    /* the grown-ups' starting grade: every strand starts there */
    mq_set_start_grade(q, 2);
    CHECK(mq_start_grade(q) == 2 && mq_pace(q)->strand[STR_MULDIV] == 3 && mq_pace(q)->strand[STR_FRAC] == 2);
    mq_press(q, B_GRADE, 0); /* the grown-ups' setup: a tap, the next grade */
    CHECK(mq_start_grade(q) == 3);
    CHECK(mq_pace(q)->voice == VO_BABBLE); /* cute babble by default */
    mq_press(q, B_SPEECH, 0);
    CHECK(mq_pace(q)->voice == VO_WORDS);
    mq_press(q, B_SPEECH, 0);
    CHECK(mq_pace(q)->voice == VO_OFF);
    mq_press(q, B_SPEECH, 0);
    /* the school year (the grown-ups: grade 3, week 10; backlog (a)): the pacing map says what is taught, and
     * the placement starts each strand where school is and never asks a topic it has not reached */
    for (int wk = 10; wk <= 36; wk += 26) {
        con("new Ava 1");
        q->fun_off = true;
        eng_rng = 7;
        mq_set_school(q, 3, wk);
        if (wk == 10)
            CHECK(mq_school_taught(q, S_GROUPS) && mq_school_taught(q, S_ADDSUB) && mq_school_taught(q, S_PV3) &&
                  !mq_school_taught(q, S_UNITFRAC) && !mq_school_taught(q, S_AREAMUL) && !mq_school_taught(q, S_TIMES_AS));
        int untaught = 0, probes = 0, frac = 0;
        con("level 0 0");
        for (int k = 0; k < 40 && mq_placing(q); k++) {
            if (q->scene != SC_PLAY)
                con("level 0 0");
            if (q->placing_item) {
                probes++, untaught += !mq_school_taught(q, q->prob.skill), frac += mq_strand(q->prob.skill) == STR_FRAC;
                if (getenv("MQ_TRACE"))
                    fprintf(stderr, "school week %d: probe %s (grade %d, week %d)\n", wk, mq_skill[q->prob.skill].name,
                            mq_skill[q->prob.skill].grade, mq_pace_week(q->prob.skill));
            }
            eng_answer(4, 0); /* (she knows it all: only school's pace holds the placement back) */
        }
        CHECK(probes >= 4 && untaught == 0 && !mq_placing(q));
        CHECK(wk == 10 ? frac == 0 : frac > 0); /* fractions come in week 22 */
        CHECK(mq_pace(q)->strand[STR_MULDIV] >= 3);
    }
    con("school 3 10 default"); /* a page's preset: new players take it */
    con("new Bea 1");
    CHECK(mq_school(q, NULL) == 3 && mq_pace(q)->school_week == 10);
    mq_press(q, B_SCHOOLW, 1);
    mq_press(q, B_SCHOOLG, 0);
    CHECK(mq_school(q, NULL) == 4 && mq_pace(q)->school_week == 11);
    q->save.pad[0] = q->save.pad[1] = 0;

    /* 3. she only ever plays one favourite game (Bubble Pop at the Garden's first stop): its orders
     *    follow her level, difficulty rises with mastery, grade-4 and grade-5 skills come to it, and
     *    every standard of grades 2-4 is passed in the end (cross-world items come as their own game
     *    when Bubble Pop cannot tell them) */
    con("new Zoe 4");
    q->fun_off = true;
    p = mq_prof(q);
    mq_set_start_grade(q, 3);
    eng_rng = 2024;
    int in_pop[MQ_NSKILL] = {0}, seen_diff[MQ_NSKILL][4] = {{0}}, orders = 0, rounds = 0;
    uint8_t diff_first[MQ_NSKILL];
    memset(diff_first, 0xFF, sizeof diff_first);
    int grade_seen_pop[6] = {0};
    while (!mq_quest_complete(p) && rounds < 6000) {
        p->sk[S_GROUPS].plays = 0; /* (her favourite stop's game is Bubble Pop every time) */
        con("level 0 0");
        CHECK(q->gm.round_mech == ME_POP);
        rounds++;
        for (int k = 0; k < 20 && q->scene == SC_PLAY; k++) {
            if (q->sub != GS_PLAY) {
                con("step 40");
                continue;
            }
            int s = q->prob.skill;
            if (q->gm.mech == ME_POP)
                in_pop[s]++, grade_seen_pop[mq_skill[s].grade]++;
            seen_diff[s][q->prob.diff & 3]++;
            if (diff_first[s] == 0xFF)
                diff_first[s] = q->prob.diff;
            eng_answer(5, 8); /* a strong player: a slip now and then */
            orders++;
            p->secs += 25;
        }
        if (rounds % 6 == 0)
            p->days++; /* play days go by (mastery needs a second one) */
        con("go map");
    }
    if (getenv("MQ_TRACE"))
        fprintf(stderr, "engine: %d rounds, %d orders, complete %d; Bubble Pop grade 3 %d, 4 %d, 5 %d\n", rounds, orders,
                mq_quest_complete(p), grade_seen_pop[3], grade_seen_pop[4], grade_seen_pop[5]);
    CHECK(mq_quest_complete(p));
    CHECK(grade_seen_pop[4] > 50 && grade_seen_pop[5] > 5);
    int rose = 0, played = 0;
    for (int s = 0; s < MQ_NSKILL; s++)
        if (diff_first[s] != 0xFF && seen_diff[s][0] + seen_diff[s][1] + seen_diff[s][2] + seen_diff[s][3] >= 8) {
            played++;
            int top = 0;
            for (int d = 0; d < 4; d++)
                if (seen_diff[s][d])
                    top = d;
            rose += top > diff_first[s];
        }
    if (getenv("MQ_TRACE"))
        fprintf(stderr, "engine: difficulty rose in %d of %d skills played 8+ times\n", rose, played);
    if (getenv("MQ_TRACE")) {
        int tot, fl = mq_facts_fluent(p, &tot);
        con("factstat");
        fprintf(stderr, "engine: facts fluent %d of %d; orders by skill:", fl, tot);
        for (int s = 0; s < MQ_NSKILL; s++)
            if (in_pop[s] > 300)
                fprintf(stderr, " %s %d", mq_skill[s].name, in_pop[s]);
        fprintf(stderr, "\n");
    }
    CHECK(played > 30 && rose * 100 >= played * 60);
    int std_ok = 0;
    for (int k = 0; k < MQ_NSTD; k++)
        std_ok += mq_std[k].grade <= 4 && mq_std_passed(p, k);
    CHECK(std_ok >= 60);
    q->fun_off = false;
}

/* typed answers (backlog (b)): a long free play through every place -- about 15-20 % of her orders are
 * typed (the Heist's wheels, the till's keys, the notebook), never one in an action or rhythm game */
static void test_typed_share(void)
{
    fresh_module();
    con("new Tia 3");
    con("grant bosses");
    q->fun_off = true;
    mq_set_start_grade(q, 4);
    eng_rng = 77;
    int orders = 0, typed = 0, typed_action = 0;
    for (int r = 0; r < 260 && orders < 1200; r++) {
        char b[24];
        snprintf(b, sizeof b, "level %d %d", r % MQ_NWORLD, (r / MQ_NWORLD) % 3);
        con(b);
        for (int k = 0; k < 12 && q->scene == SC_PLAY; k++) {
            if (q->sub != GS_PLAY) {
                con("step 40");
                continue;
            }
            bool t = mq_order_typed(q);
            int m = q->gm.mech;
            orders++, typed += t;
            typed_action += t && (mq_mech_moving(m) || m == ME_ZAP || m == ME_COOK);
            CHECK(!q->gm.make); /* (nothing to make in any game) */
            eng_answer(4, 10);
        }
    }
    if (getenv("MQ_TRACE"))
        con("diag");
    if (getenv("MQ_TRACE"))
        fprintf(stderr, "typed: %d of %d orders (%d%%), %d in action games; the game's count %u of %u\n", typed, orders,
                orders ? typed * 100 / orders : 0, typed_action, mq_pace(q)->typed_n, mq_pace(q)->orders_n);
    CHECK(orders >= 400);
    CHECK(typed * 100 >= orders * 13 && typed * 100 <= orders * 22); /* (about 15-20 %) */
    CHECK(typed_action == 0);
    q->fun_off = false;
}

/* ------------------------------------------------------------ what to do, and help */
static void kid_speak(int who, const char *s) { kid_check(s), (void)who; }
/* a skill whose orders the game tells (-1: none) */
static int skill_for(int m)
{
    for (int sk = 0; sk < MQ_NSKILL; sk++)
        for (int k = 0; k < 30; k++) {
            mq_prob_t p;
            q->rng = (uint32_t)(sk * 13 + k) | 1;
            mq_generate(q, &p, sk, 1);
            if (mq_mech_fits(m, &p) && !mq_heavy(q, &p))
                return sk;
        }
    return -1;
}
static void test_guide(void)
{
    /* the whole test draws through the kid-screen checker: no school words in any demo, hint, tag,
     * bulb or helper line, drawn or spoken */
    kid_api = modx_api;
    kid_api.text = kid_text, kid_api.fill_rect = kid_fill, kid_api.toast = kid_toast;
    kid_api.message = kid_message, kid_api.softkey = kid_softkey, kid_api.speak = kid_speak;
    memset(q, 0, mq_module_rw_size);
    memset(&ops, 0, sizeof ops);
    CHECK(mq_module_init(&kid_api, q, &ops) == 0);
    q->rng = 0x2545F491u, q->scenes_off = true, q->fun_ms = 0;
    kid_bad = kid_above = 0;
    con("new Gus 2");
    con("grant bosses");
    q->fun_off = true;
    /* 1. a new player's first round of every game shows the demo (the ghost hand and the goal), once */
    int shown = 0, games = 0;
    for (int m = 0; m < ME_COUNT; m++) {
        int sk = skill_for(m);
        if (sk < 0 || !order_in(sk, 1, m))
            continue;
        games++;
        shown += mq_guide_demo_on(q);
        kid_draw();
        for (int k = 0; k < 90 || (k < 600 && mq_guide_demo_on(q)); k++) /* it runs to its end (its words' reading time): seen */
            q->now += 40, q->anim_t += 40, mq_game_frame(q);
        CHECK(!mq_guide_demo_on(q) && ((mq_pace(q)->demo_seen >> m) & 1));
        CHECK(order_in(sk, 1, m) && !mq_guide_demo_on(q)); /* the second time: the goal banner only */
    }
    if (getenv("MQ_TRACE"))
        fprintf(stderr, "guide: %d of %d games showed their demo\n", shown, games);
    CHECK(games >= 14 && shown == games);
    /* any input ends a demo (and counts it seen) */
    mq_pace(q)->demo_seen = 0;
    CHECK(order_in(S_GROUPS, 1, ME_POP) && mq_guide_demo_on(q));
    gkey(13);
    CHECK(!mq_guide_demo_on(q) && (mq_pace(q)->demo_seen & 1));
    /* 2. idle for five seconds: the targets pulse and the guide reminds her (once) */
    CHECK(order_in(S_GROUPS, 1, ME_POP));
    q->last_input = q->now;
    for (int k = 0; k < 140; k++)
        q->now += 40, q->anim_t += 40, mq_game_frame(q);
    CHECK(q->gm.idle_said == 1);
    kid_draw();
    /* 3. "what do I do?": the demo again (the soft key, the "?" in the top line, the console) */
    con("help");
    CHECK(mq_guide_demo_on(q));
    gkey(14);
    CHECK(!mq_guide_demo_on(q));
    tev(1, 270, MQ_Y0 + 8), tev(4, 270, MQ_Y0 + 8);
    CHECK(mq_guide_demo_on(q));
    /* 4. the lightbulb: the situation again with its operation, never the answer; then the game */
    for (int sk = 0; sk < MQ_NSKILL; sk++) {
        if (mq_skill[sk].visual_may_reveal == VR_MAY)
            continue;
        if (!order_in(sk, 1, ME_POP))
            continue;
        con("bulb");
        CHECK(q->gm.bulb_t != 0);
        for (int k = 0; k < 4; k++) {
            int r4[4];
            q->anim_t += 400;
            ops.draw(q, 0, r4);
            if (q->prob.kind == K_NUM && q->frame_units && q->frame_units == q->prob.ans && !mq_rung_concrete(sk, q->prob.diff) &&
                kid_bad++ < 8)
                fprintf(stderr, "bulb: skill %d shows %d countable things = the answer\n", sk, q->frame_units);
        }
        for (int k = 0; k < 60 || (k < 600 && q->gm.bulb_t); k++) /* (its words' reading time) */
            q->now += 40, q->anim_t += 40, mq_game_frame(q);
        CHECK(q->gm.bulb_t == 0 && q->sub == GS_PLAY);
    }
    /* every story kind has its operation's tag, and the tag never holds the answer */
    for (int sk = 0; sk < MQ_NSKILL; sk++)
        for (int k = 0; k < 20; k++) {
            mq_prob_t p;
            q->rng = (uint32_t)(sk * 7 + k * 3) | 1;
            mq_generate(q, &p, sk, k % 4);
            if (!p.stg)
                continue;
            char cue[48], a[24];
            CHECK(mq_op_cue(q, &p, cue, sizeof cue));
            mq_format(q, &p, p.ans, a, sizeof a);
            char *f = strstr(cue, a);
            bool alone = f && (f == cue || f[-1] == ' ') && (!f[strlen(a)] || f[strlen(a)] == ' ' || f[strlen(a)] == ',');
            if (alone && p.ans != p.stg_n[0] && p.ans != p.stg_n[1] && kid_bad++ < 8)
                fprintf(stderr, "op cue of skill %d tells the answer %s: %s\n", sk, a, cue);
        }
    /* 5. the helper friend: two misses on an order bring one; a third slip of a skill within a few
     *    orders brings one at once; the favourite (slot 0) comes about half the time */
    int favs = 0;
    for (int k = 0; k < 40; k++)
        favs += mq_helper_pick(q) == 0;
    CHECK(favs == 20);
    CHECK(order_in(S_GROUPS, 1, ME_POP));
    con("try");
    CHECK(q->sub == GS_PLAY); /* one miss: try again */
    con("try");
    CHECK(q->sub == GS_HELP && q->gm.helped); /* two: a friend */
    kid_draw();
    for (int i = 0; i < 12 && q->sub == GS_HELP; i++)
        con("next"), kid_draw();
    CHECK(q->sub == GS_PLAY);
    kid_draw();
    CHECK(order_in(S_SHARE, 1, ME_POP));
    for (int i = 0; i < 3; i++)
        mq_engine_answered(q, S_SHARE, true);
    con("try");
    CHECK(q->sub == GS_HELP); /* the same skill slipped three times lately: help at the first miss */
    /* 6. the hand-over-hand guarantee: in every game, missed after help too, it ends in a success */
    int ended = 0, tried = 0;
    for (int m = 0; m < ME_COUNT; m++) {
        int sk = skill_for(m);
        if (sk < 0 || !order_in(sk, 1, m))
            continue;
        tried++;
        con("try");
        if (m == ME_LAUNCH)
            con("step 80"); /* (judged where it lands) */
        if (q->sub == GS_PLAY)
            con("try");
        if (m == ME_LAUNCH)
            con("step 80");
        for (int i = 0; i < 12 && q->sub == GS_HELP; i++)
            con("next");
        CHECK(q->sub == GS_PLAY && q->gm.helped);
        int left = 0;
        for (int i = 0; i < q->gm.nopt; i++)
            left += !((q->gm.gone >> i) & 1);
        if (!q->gm.nopt || left > 1) { /* (with only the right one left it is together already) */
            con("try");
            if (m == ME_LAUNCH)
                con("step 80");
        } else
            q->gm.hoh = 1;
        if (!(q->sub == GS_PLAY && q->gm.hoh) && getenv("MQ_TRACE"))
            fprintf(stderr, "together: %s sub %d helped %d hoh %d skill %d\n", mq_mech_name(m), q->sub, q->gm.helped, q->gm.hoh, sk);
        CHECK(q->sub == GS_PLAY && q->gm.hoh);
        kid_draw();
        if (q->gm.hoh == 2)
            gkey(49); /* the game is set: go! */
        else
            con("correct");
        for (int i = 0; i < 80 && m == ME_LAUNCH && q->sub == GS_PLAY; i++)
            con("step 1"); /* (in flight) */
        ended += q->sub == GS_CHEER;
        if (q->sub != GS_CHEER && getenv("MQ_TRACE"))
            fprintf(stderr, "together: %s did not end in a success\n", mq_mech_name(m));
    }
    CHECK(tried >= 14 && ended == tried);
    CHECK(kid_bad == 0);
    q->fun_off = false;
    fresh_module();
}

/* ------------------------------------------------------------ "focus this week" */
static void test_focus(void)
{
    con("new Fay 2");
    q->fun_off = true;
    mq_profile_t *p = mq_prof(q);
    CHECK(!mq_focus_active(q) && mq_focus_share(q) == 0);
    con("focus mul 4"); /* "know 4 x 0-10" */
    CHECK(mq_focus_active(q) && mq_focus_count(q, NULL, NULL) == 21 && mq_focus_share(q) == 50);
    CHECK(mq_focus_fact(q, mq_fact_index(4, 7, false)) && mq_focus_fact(q, mq_fact_index(7, 4, false)) &&
          !mq_focus_fact(q, mq_fact_index(6, 7, false)));
    /* the facts asked: about the share (or more) are focus facts, never a recent one again */
    int focus = 0, n = 0, rep = 0;
    int16_t last[3] = {-1, -1, -1};
    for (int k = 0; k < 600; k++) {
        int f = mq_fact_next(q);
        int a, b;
        bool dv;
        mq_fact_decode(f, &a, &b, &dv);
        int key = dv ? 200 + f : (a < b ? a * 13 + b : b * 13 + a);
        rep += key == last[0] || key == last[1] || key == last[2];
        last[2] = last[1], last[1] = last[0], last[0] = (int16_t)key;
        focus += mq_focus_fact(q, f), n++;
        mq_fact_record(q, f, k % 5 != 0, 2000); /* (answered: the boxes move) */
        if (k % 30 == 29)
            p->round++;
    }
    if (getenv("MQ_TRACE"))
        fprintf(stderr, "focus: %d of %d facts asked were focus facts, %d repeats\n", focus, n, rep);
    CHECK(focus * 100 >= n * 45 && rep == 0);
    /* the engine's orders: about the share are fact orders */
    int facts = 0;
    for (int k = 0; k < 400; k++) {
        int s = mq_engine_pick(q, G_BAKERY, ME_POP);
        mq_engine_answered(q, s, false);
        facts += s == S_FACTS;
    }
    CHECK(facts * 100 >= 400 * 40);
    /* a skill in focus too */
    con("focus skill 16");
    int sub = 0;
    for (int k = 0; k < 400; k++) {
        int s = mq_engine_pick(q, G_ART, ME_BUILD);
        mq_engine_answered(q, s, false);
        sub += s == S_ADDSUB;
    }
    CHECK(sub * 100 >= 400 * 15);
    /* known: every focus fact fluent on two days -> ready for the test, the share back to maintenance */
    for (int f = 0; f < MQ_NFACT; f++)
        if (mq_focus_fact(q, f))
            p->fact[f].box = 5, p->fact[f].t10 = 20, p->fact[f].right = (uint8_t)(FACT_DAYS | 9);
    CHECK(!mq_focus_ready(q)); /* (the skill is not passed yet) */
    p->sk[S_ADDSUB].flags |= SK_PASSED;
    CHECK(mq_focus_ready(q) && mq_focus_share(q) == 10);
    /* for this week: it ends after seven play days */
    con("focus week");
    CHECK(mq_focus_active(q));
    p->days = (uint16_t)(p->days + 7);
    CHECK(!mq_focus_active(q) && mq_focus_share(q) == 0);
    con("focus week"); /* (until changed again) */
    CHECK(mq_focus_active(q));
    /* the grown-ups' grid: a tap on a cell, a fact; on a row's number, its table; a digit key, a table */
    con("focus clear");
    con("go parent");
    q->tab = 1;
    int r4[4];
    ops.draw(q, 0, r4);
    int oy = MQ_Y0 + 22 + 26;
    tev(4, 30 + 7 * 12 + 5, oy + 3 * 12 + 5);
    CHECK(mq_focus_fact(q, mq_fact_index(3, 7, false)) && mq_focus_count(q, NULL, NULL) == 1);
    tev(4, 10, oy + 6 * 12 + 5);
    CHECK(mq_focus_fact(q, mq_fact_index(6, 9, false)) && mq_pace(q)->focus_mul == 1 << 6);
    mq_press(q, B_FOCUSSHARE, 0);
    CHECK(mq_pace(q)->focus_share == 60);
    con("focus clear");
    q->fun_off = false;
}

/* ------------------------------------------------------------ pacing: the game fits the problem */
static void test_pacing(void)
{
    con("new Pace 2");
    con("grant bosses");
    q->fun_off = true;
    /* 1. every skill and difficulty in the moving games: nothing escapes before 1.5 x the order's think
     *    time; a heavy order goes to a calm game or hovers */
    int bad = 0, orders = 0, heavy_moving = 0, routed = 0;
    static const int moving[] = {ME_POP, ME_CATCH};
    for (int sk = 0; sk < MQ_NSKILL; sk++)
        for (int d = 0; d < mq_rungs(sk); d++)
            for (size_t mi = 0; mi < 2; mi++)
                for (int k = 0; k < 3; k++) {
                    q->rng = (uint32_t)(sk * 131 + d * 17 + k * 5 + (int)mi) | 1;
                    if (!order_in(sk, d, moving[mi])) {
                        if (q->scene == SC_PLAY && mq_heavy(q, &q->prob) && mq_mech_calm(q->gm.mech))
                            routed++;
                        continue;
                    }
                    mq_game_t *g = &q->gm;
                    int think = mq_think_ms(q, &q->prob);
                    int esc = g->esc_ms ? (int)g->esc_ms * 256 / (g->spd ? g->spd : 256) : 0;
                    orders++;
                    if (esc && esc * 2 < think * 3 && bad++ < 6)
                        fprintf(stderr, "pacing: skill %d d%d in %s: escapes after %d ms, think %d ms\n", sk, d,
                                mq_mech_name(g->mech), esc, think);
                    if (mq_heavy(q, &q->prob)) {
                        heavy_moving++;
                        CHECK(g->spd <= 40); /* a heavy order in a moving game: the answers hover */
                    }
                    CHECK(g->spd <= 256);    /* never past the comfortable cruise */
                }
    if (getenv("MQ_TRACE"))
        fprintf(stderr, "pacing: %d orders, %d heavy hovering, %d routed to a calm game\n", orders, heavy_moving, routed);
    CHECK(bad == 0 && orders > 600);
    /* the routing: a heavy order with a calm game of its own goes there; facts stay in the action */
    CHECK(order_in(S_ADDSUBBIG, 2, ME_VAULT) && mq_heavy(q, &q->prob));
    mq_prob_t hv = q->prob;
    CHECK(mq_mech_calm(mq_route_mech(q, &hv, ME_POP)));
    CHECK(order_in(S_FACTS, 1, ME_POP) && !mq_heavy(q, &q->prob) && mq_route_mech(q, &q->prob, ME_POP) == ME_POP);
    /* 2. Cloud Catch: three stars side by side (no overlap); they wait mid-air until she moves; a star
     *    she does not go for never lands on her cloud; a missed right star floats back up */
    CHECK(order_in(S_GROUPS, 1, ME_CATCH) && q->gm.nopt == 3);
    for (int i = 0; i < GM_NOBJ; i++)
        for (int j = 0; j < i; j++)
            if (q->gm.o[i].kind == 1 && q->gm.o[j].kind == 1)
                CHECK(abs(q->gm.o[i].x - q->gm.o[j].x) / 16 >= 60);
    for (int k = 0; k < 40; k++)
        con("step 25"); /* 40 s: she just watches */
    CHECK(q->sub == GS_PLAY && !q->tried_wrong);
    for (int i = 0; i < GM_NOBJ; i++)
        if (q->gm.o[i].kind == 1)
            CHECK(q->gm.o[i].y / 16 <= GFY + 62);
    int r = mq_game_right_opt(q);
    mq_gobj_t *o = obj_of(r);
    tev(1, o->x / 16, GFB - 20); /* she moves under the right one: it comes to her */
    CHECK(steps_until_judged(200) < 200 && q->sub == GS_CHEER);
    /* 3. the speed controller: a miss lowers it at once; quick rights raise it slowly, from the second in
     *    a row; capped at the cruise; alternating right and wrong never oscillates; per player, saved */
    mq_pace_t *pc = mq_pace(q);
    pc->speed[ME_POP] = 0, pc->spd_streak[ME_POP] = 0;
    int l0 = mq_speed_level(q, ME_POP);
    CHECK(l0 == 30);
    mq_speed_event(q, ME_POP, SE_MISS);
    CHECK(mq_speed_level(q, ME_POP) == 24);
    mq_speed_event(q, ME_POP, SE_QUICK); /* one quick right after a miss: nothing yet (hysteresis) */
    CHECK(mq_speed_level(q, ME_POP) == 24);
    mq_speed_event(q, ME_POP, SE_QUICK);
    CHECK(mq_speed_level(q, ME_POP) == 25);
    for (int k = 0; k < 400; k++)
        mq_speed_event(q, ME_POP, SE_QUICK);
    CHECK(mq_speed_level(q, ME_POP) == 100);
    CHECK(order_in(S_FACTS, 1, ME_POP));
    CHECK(mq_speed_now(q, ME_POP, &q->prob, 60000) <= 256); /* the cruise: never toward failure */
    int prev = mq_speed_level(q, ME_POP), ups = 0;
    for (int k = 0; k < 60; k++) {
        mq_speed_event(q, ME_POP, k % 2 ? SE_QUICK : SE_MISS);
        ups += mq_speed_level(q, ME_POP) > prev;
        prev = mq_speed_level(q, ME_POP);
    }
    CHECK(ups == 0 && mq_speed_level(q, ME_POP) >= 20); /* (a floor: never frozen, audit S1) */
    for (int k = 0; k < 30; k++)
        mq_speed_event(q, ME_POP, SE_MISS);
    CHECK(mq_speed_level(q, ME_POP) == 20);
    mq_speed_event(q, ME_POP, SE_SLOW);
    CHECK(mq_speed_level(q, ME_POP) >= 1);
    pc->speed[ME_CATCH] = 77;
    con("save");
    mq_save_now(q);
    fresh_module();
    q->fun_off = true;
    CHECK(mq_pace(q)->speed[ME_CATCH] == 77); /* kept with her player */
    /* idle: nothing done for five seconds hovers the bubbles */
    CHECK(order_in(S_GROUPS, 0, ME_POP));
    o = NULL;
    for (int i = 0; i < GM_NOBJ; i++)
        if (q->gm.o[i].kind == 1)
            o = &q->gm.o[i];
    CHECK(o != NULL);
    q->last_input = q->now;
    for (int k = 0; k < 6; k++)
        con("step 25");
    int16_t y0 = o->y;
    con("step 10");
    CHECK(abs(o->y - y0) <= 16 * 2);
    /* 4. fast and right: the maths moves on (a harder step), not the game's speed */
    mq_profile_t *p = mq_prof(q);
    p->sk[S_ADDSUB].diff = 0, p->sk[S_ADDSUB].streak = 0;
    for (int k = 0; k < 3; k++) {
        con("skill 16 0");
        q->now += 800; /* quick */
        con("correct");
    }
    CHECK(p->sk[S_ADDSUB].diff >= 1);
    /* 5. Star Catch (the arcade's falling stars): a gentle fall capped by her think time; a star that
     *    lands floats back up and is not counted; after a miss the stars wait a moment */
    con("go arcade");
    mq_fg_start(q, FG_STARS);
    int f0 = q->fall[0].fact;
    CHECK(mq_star_speed(q, f0) <= 18);
    mq_fact_t before = p->fact[f0];
    q->last_input = q->now;
    for (int k = 0; k < 600 && q->fall[0].y <= MQ_Y0 + 110; k++) {
        q->now += 40, q->last_input = q->now;
        mq_fg_tick(q, 40);
    }
    for (int k = 0; k < 200 && q->fall[0].y > MQ_Y0 + 40; k++) {
        q->now += 40, q->last_input = q->now;
        mq_fg_tick(q, 40);
    }
    CHECK(q->fall[0].fact == f0 && q->fall[0].y <= MQ_Y0 + 40);    /* back up at the top, the same star */
    CHECK(p->fact[f0].seen == before.seen && p->fact[f0].right == before.right); /* nothing recorded */
    fg_type(mq_fact_answer(q->fall[0].fact) + 1); /* a miss: calm */
    int16_t sy = q->fall[1].y;
    for (int k = 0; k < 20; k++) {
        q->now += 40, q->last_input = q->now;
        mq_fg_tick(q, 40);
    }
    CHECK(q->fall[1].y == sy || sy <= MQ_Y0 + 30);
    q->fun_off = false;
}

static void test_games(void)
{
    char t[16];
    con("new Gia 2");
    con("grant bosses");
    con("seed 5");
    q->fun_off = true;
    /* geometry */
    CHECK(mq_hit_circle(10, 10, 10, 20, 10) && !mq_hit_circle(10, 10, 10, 21, 10));
    CHECK(mq_angle_of(10, 0) == 0 && mq_angle_of(0, 10) == 90 && mq_angle_of(-10, 0) == 180 && mq_angle_of(0, -10) == 270);
    CHECK(mq_angle_of(10, 10) == 45);
    for (int d = 2; d <= 8; d++) /* every slice's middle is that slice */
        for (int k = 0; k < d; k++) {
            int a = k * 360 / d + 180 / d;
            CHECK(mq_pie_slice(mq_isin(a) * 40 / 1024, -mq_icos(a) * 40 / 1024, d) == k);
        }
    CHECK(mq_pie_slice(100, 100, 4) == -1);
    /* every skill has a game, and every problem of it can be told in one */
    for (int sk = 0; sk < MQ_NSKILL; sk++) {
        int g[4], n = mq_skill_games(sk, g);
        CHECK(n >= 1);
        for (int d = 0; d < mq_rungs(sk); d++)
            for (int k = 0; k < 30; k++) {
                mq_prob_t p;
                mq_generate(q, &p, sk, d);
                int m = mq_mech_for(&p, -1, k);
                if (!(m >= 0 && m < ME_COUNT && mq_mech_fits(m, &p)) && k == 0)
                    fprintf(stderr, "skill %d rung %d: no game (vis %d)\n", sk, d, p.vis);
                CHECK(m >= 0 && m < ME_COUNT && mq_mech_fits(m, &p));
            }
    }
    /* options: the answer once, distinct labels, the rest wrong (an equivalent fraction is no dud) */
    for (int sk = 0; sk < MQ_NSKILL; sk++)
        for (int k = 0; k < 12; k++) {
            if (!order_in(sk, k % mq_rungs(sk), ME_POP))
                break;
            int r = mq_game_right_opt(q), nr = 0;
            CHECK(r >= 0 && q->gm.nopt >= 2);
            for (int i = 0; i < q->gm.nopt; i++) {
                nr += mq_correct(&q->prob, q->gm.opt[i]) || (q->prob.kind == K_CHOICE && q->gm.opt[i] == q->prob.ans);
                for (int j = 0; j < i; j++)
                    CHECK(strcmp(q->gm.lab[i], q->gm.lab[j]) != 0);
            }
            CHECK(nr == 1);
        }
    /* Bubble Pop: a tap on a dud wilts it (the friend tells why), a tap on the right one pops it */
    CHECK(order_in(S_GROUPS, 1, ME_POP));
    int r = mq_game_right_opt(q), w = (r + 1) % q->gm.nopt;
    mq_gobj_t *o = obj_of(w);
    CHECK(o != NULL);
    tev(1, o->x / 16 + 3, o->y / 16 - 2);
    CHECK(q->tried_wrong && ((q->gm.gone >> w) & 1) && q->sub == GS_PLAY && q->gm.mood == 1 && q->gm.say[0]);
    o = obj_of(r);
    tev(1, o->x / 16, o->y / 16);
    CHECK(q->sub == GS_CHEER && q->gm.stars >= 1);
    CHECK(order_in(S_GROUPS, 1, ME_POP)); /* a tap on nothing pops nothing */
    tev(1, 2, GFY + 2);
    CHECK(q->sub == GS_PLAY && !q->tried_wrong);
    CHECK(order_in(S_GROUPS, 1, ME_POP)); /* typing the answer pops its bubble: her own answer */
    snprintf(t, sizeof t, "%d", (int)q->prob.ans);
    gtype(t);
    if (q->sub == GS_PLAY)
        gkey(49);
    CHECK(q->sub == GS_CHEER);
    /* Cloud Catch: the cloud under the right star catches it as it falls */
    CHECK(order_in(S_GROUPS, 1, ME_CATCH));
    r = mq_game_right_opt(q);
    for (int i = 0; i < GM_NOBJ; i++)
        if (q->gm.o[i].kind && q->gm.o[i].opt != r)
            q->gm.o[i].kind = 0; /* only the right star falls */
    o = obj_of(r);
    tev(1, o->x / 16, GFB - 20);
    CHECK(steps_until_judged(400) < 400 && q->sub == GS_CHEER);
    /* Cloud Catch, the tap (the playtest: "clicking the right falling number doesn't do anything
     * immediately"): a finger on a falling star catches it in that very event, mid-fall, wherever it is
     * (the cloud swoops under it); a wrong one is a gentle dud at once */
    for (int at = 0; at < 4; at++) {
        CHECK(order_in(S_GROUPS, 1 + at % 2, ME_CATCH));
        con(at == 0 ? "step 2" : at == 1 ? "step 8" : at == 2 ? "step 20" : "step 40");
        int ok = mq_game_right_opt(q), bad2 = (ok + 1) % q->gm.nopt;
        mq_gobj_t *ow = obj_of(bad2);
        CHECK(ow != NULL && q->sub == GS_PLAY);
        if (ow && at % 2) { /* a wrong star first: a dud, at once */
            tev(1, ow->x / 16 + 10, ow->y / 16 - 10);
            tev(3, ow->x / 16 + 10, ow->y / 16 - 10);
            CHECK(q->tried_wrong);
            for (int k = 0; k < 80 && q->sub != GS_PLAY; k++)
                con("step 1");
        }
        mq_gobj_t *oc = obj_of(ok);
        CHECK(oc != NULL);
        if (oc) {
            tev(1, oc->x / 16 - 12, oc->y / 16 + 12); /* (off its middle: the generous box) */
            CHECK(q->sub == GS_CHEER);
            if (q->sub != GS_CHEER) fprintf(stderr, "catch at %d: sub %d tried %d teach %d cols %d scene_on %d y %d x %d mod %d\n", at, q->sub, q->tried_wrong, mq_teach_on(q), mq_cols_on(q), q->gm.scene_on, oc->y / 16, oc->x / 16, mq_mod_now(q));
            tev(3, oc->x / 16 - 12, oc->y / 16 + 12);
        }
    }
    /* Gate Dash: a tap on a gate dashes through it */
    CHECK(order_in(S_MUL10, 1, ME_GATES));
    r = mq_game_right_opt(q);
    con("step 40"); /* the gates arrive and wait */
    CHECK(q->sub == GS_PLAY && obj_of(r) && obj_of(r)->x / 16 == 236);
    tev(4, 236, GFY + 25 + r * 49);
    CHECK(steps_until_judged(100) < 100 && q->sub == GS_CHEER);
    /* Kart Race: UP / DOWN, RIGHT; a right pad is a boost */
    CHECK(order_in(S_MUL10, 1, ME_KART));
    r = mq_game_right_opt(q);
    con("step 40");
    while (q->gm.lane > r)
        gkey(12);
    while (q->gm.lane < r)
        gkey(15);
    gkey(14);
    CHECK(steps_until_judged(100) < 100 && q->sub == GS_CHEER && q->gm.boost_t);
    /* Peekaboo: the keypad keys are the holes; facts become "find the friend with the trays" */
    CHECK(order_in(S_FACTS, 1, ME_ZAP));
    r = mq_game_right_opt(q);
    o = obj_of(r);
    mq_game_key(q, 0, "789456"[o->hp]);
    CHECK(q->sub == GS_CHEER);
    /* Sort Slide: a swipe sends the card to a bin */
    CHECK(order_in(S_CMP3, 1, ME_SORT));
    r = mq_game_right_opt(q);
    q->gm.sel = (uint8_t)r, q->gm.fly = 1;
    CHECK(steps_until_judged(60) < 60 && q->sub == GS_CHEER);
    CHECK(order_in(S_CMP3, 1, ME_SORT));
    ops.event(q, MODX_EV_SWIPE, MODX_SWIPE(0, 160, 120)); /* left: the first bin */
    CHECK(q->gm.fly && q->gm.sel == 0);
    steps_until_judged(60);
    CHECK(q->sub != GS_PLAY || q->tried_wrong);
    /* Hop Along: a tap on the line hops to the nearest stone; land! */
    CHECK(order_in(S_FRACLINE, 1, ME_JUMP));
    int ticks = q->prob.v[1] * q->prob.v[2];
    tev(4, 24 + (int)q->prob.ans * 272 / ticks + 3, GFY + 86);
    CHECK(q->gm.aim == q->prob.ans);
    tev(4, 280, GFB - 18);
    CHECK(q->sub == GS_CHEER);
    /* Hop Along, every way in (the playtest: "Hop Along doesn't seem to work"): a drag of the frog to
     * the stone and its release -- the swipe the core sends after the drag must not hop on a stone
     * more (it put the frog one stone short); the arrows and ENTER; the scroll wheel; a wrong landing
     * is a story moment and the game goes on, and the right one after it cheers */
    for (int way = 0; way < 4; way++) {
        CHECK(order_in(S_FRACLINE, 1, ME_JUMP) && q->gm.mode == 0);
        CHECK(q->gm.aim == 0 && q->prob.ans != 0); /* (never starting on the answer) */
        int tk = q->prob.v[1] * q->prob.v[2], ans = (int)q->prob.ans, ax = 24 + ans * 272 / tk;
        if (way == 0) { /* drag from the start stone to the answer, release, the swipe */
            tev(1, 24, GFY + 86);
            tev(2, ax - 20, GFY + 86);
            tev(2, ax + 1, GFY + 86);
            tev(3, ax + 1, GFY + 86);
            ops.event(q, MODX_EV_SWIPE, MODX_SWIPE(1, 24, GFY + 86));
            CHECK(q->gm.aim == ans);
            tev(4, 280, GFB - 18);
        } else if (way == 1) { /* the arrows, then ENTER */
            for (int k = 0; k < ans; k++)
                gkey(14);
            CHECK(q->gm.aim == ans);
            gkey(49);
        } else if (way == 2) { /* the Mac's scroll wheel over the line (down: on along it), then land! */
            for (int k = 0; k < ans; k++)
                ops.event(q, MODX_EV_SCROLL, MODX_SCROLL(1, 160, GFY + 86));
            CHECK(q->gm.aim == ans);
            tev(4, 280, GFB - 18);
        } else { /* a wrong stone first: told, still playing; then the right one cheers */
            int wrong = ans + 1 <= tk ? ans + 1 : ans - 1;
            tev(4, 24 + wrong * 272 / tk, GFY + 86);
            CHECK(q->gm.aim == wrong);
            if (q->gm.aim != wrong) fprintf(stderr, "hop wrong %d aim %d ans %d tk %d sub %d\n", wrong, q->gm.aim, ans, tk, q->sub);
            tev(4, 280, GFB - 18);
            steps_until_judged(40);
            CHECK(q->tried_wrong);
            for (int k = 0; k < 60 && q->sub != GS_PLAY; k++)
                con("step 1");
            CHECK(q->sub == GS_PLAY && q->scene == SC_PLAY);
            tev(4, 24 + ans * 272 / tk, GFY + 86);
            tev(4, 280, GFB - 18);
        }
        steps_until_judged(40);
        CHECK(q->sub == GS_CHEER);
        if (q->sub != GS_CHEER)
            fprintf(stderr, "Hop Along way %d: %s, aim %d ans %d\n", way, mq_scene_name(q), q->gm.aim, ans);
    }
    /* rounding: hop to the nearer end */
    CHECK(order_in(S_ROUND10, 1, ME_JUMP) && q->gm.mode == 1);
    gkey(q->prob.ans == q->gm.opt[0] ? 13 : 14);
    gkey(49);
    CHECK(q->sub == GS_CHEER);
    for (int k = 0; k < 20; k++) { /* always down: told */
        CHECK(order_in(S_ROUND10, 1, ME_JUMP));
        if (q->prob.ans == q->gm.opt[0])
            continue;
        gkey(13);
        gkey(49);
        CHECK(q->tried_wrong && q->last_mc == MC_ROUND_DOWN_ALWAYS);
        break;
    }
    /* Sky Fling by degrees: a drag aims, the star flies along the aim and lands */
    CHECK(order_in(S_PROTRACTOR, 1, ME_LAUNCH) && q->gm.mode == 2);
    int a = (int)q->prob.ans;
    tev(1, 140 + mq_icos(a) * 80 / 1024, GFB - 12 - mq_isin(a) * 80 / 1024); /* (the protractor's centre: x 140) */
    CHECK(abs(q->gm.aim - a) <= 1);
    q->gm.aim = (int16_t)a;
    mq_game_fire(q);
    CHECK(q->gm.fly);
    int fl = steps_until_judged(80);
    CHECK(fl > 5 && fl < 80 && q->sub == GS_CHEER);
    CHECK(order_in(S_PROTRACTOR, 1, ME_LAUNCH)); /* the protractor's other scale: told */
    if (q->prob.ans != 90) {
        q->gm.aim = (int16_t)(180 - q->prob.ans);
        mq_game_fire(q);
        steps_until_judged(80);
        CHECK(q->tried_wrong && q->last_mc == MC_PROTRACTOR);
    }
    /* Sky Fling by fractions: the power is a number of parts */
    CHECK(order_in(S_FRACLINE, 1, ME_LAUNCH) && q->gm.mode == 3);
    for (int i = 0; i < q->prob.ans; i++)
        gkey(14);
    gkey(49);
    CHECK(steps_until_judged(80) < 80 && q->sub == GS_CHEER);
    /* Pie Shop: a drag across slices frosts them; serve! */
    CHECK(order_in(S_PARTITION, 1, ME_BAKE));
    int d = q->prob.v[1];
    for (int k = 0; k < q->prob.ans; k++) {
        int am = k * 360 / d + 180 / d;
        tev(k ? 2 : 1, 120 + mq_isin(am) * 40 / 1024, GFY + 72 - mq_icos(am) * 40 / 1024);
    }
    tev(4, 280, GFB - 18);
    CHECK(q->sub == GS_CHEER);
    /* a real tap on a slice (DOWN, then the TAP on release) frosts it and it stays frosted; a second tap
     * clears it (the user: "I click on it and the pink goes away") */
    CHECK(order_in(S_PARTITION, 1, ME_BAKE));
    d = q->prob.v[1];
    int px = 120 + mq_isin(180 / d) * 40 / 1024, py = GFY + 72 - mq_icos(180 / d) * 40 / 1024;
    int f0 = q->prob.v[5] & 1;
    tev(1, px, py);
    tev(4, px, py);
    CHECK((q->prob.v[5] & 1) != f0);
    tev(1, px, py);
    tev(4, px, py);
    CHECK((q->prob.v[5] & 1) == f0);
    tev(4, px, py); /* (a tap with no DOWN before it -- a test, a mouse path -- toggles too) */
    CHECK((q->prob.v[5] & 1) != f0);
    /* Build It: drag the corner; a rug with a side missing (the audit's B1: area orders stay area orders) */
    CHECK(order_in(S_AREAMUL, 2, ME_BUILD) && q->gm.mode == 4);
    int cell = mq_build_cell(&q->gm), want = (int)q->prob.ans, ww = q->gm.fixw;
    tev(1, 20 + ww * cell, GFY + 10 + want * cell + 2);
    CHECK(q->gm.bh == want && q->gm.bw == ww);
    gkey(49);
    CHECK(q->sub == GS_CHEER);
    int told = 0;
    for (int k = 0; k < 80 && !told; k++) { /* area and perimeter mixed up */
        CHECK(order_in(S_AREAMUL, 2, ME_BUILD));
        int A = (int)q->prob.ans * q->gm.fixw, w0 = q->gm.fixw;
        if (q->gm.mode != 4 || A % 2 || A / 2 - w0 <= 0 || A / 2 - w0 > q->gm.maxh || A / 2 - w0 == q->prob.ans)
            continue;
        q->gm.bh = (int16_t)(A / 2 - w0);
        gkey(49);
        CHECK(q->tried_wrong && q->last_mc == MC_AREA_PERIM);
        told = 1;
    }
    CHECK(told);
    CHECK(order_in(S_SHARE, 1, ME_BUILD) && q->gm.mode == 3); /* a flower bed in equal rows */
    q->gm.bw = (int16_t)q->prob.ans;
    tev(4, 280, GFB - 18);
    CHECK(q->sub == GS_CHEER);
    CHECK(order_in(S_PERIM, 2, ME_BUILD)); /* a frame with a side missing */
    if (q->gm.fixh)
        q->gm.bw = (int16_t)q->prob.ans;
    else
        q->gm.bh = (int16_t)q->prob.ans;
    gkey(49);
    CHECK(q->sub == GS_CHEER);
    /* Clock Tower: dragging the minute hand sets the minutes; arrows step; typing sets the time */
    CHECK(order_in(S_TIME5, 1, ME_CLOCK));
    tev(1, 110 + 50, GFY + 74); /* 3 o'clock: 15 minutes */
    CHECK(q->gm.value % 100 == 15);
    snprintf(t, sizeof t, "%d", (int)q->prob.ans);
    gtype(t);
    CHECK(q->gm.value == q->prob.ans);
    gkey(49);
    CHECK(q->sub == GS_CHEER);
    CHECK(order_in(S_TIME5, 1, ME_CLOCK));
    q->gm.value = 1200;
    gkey(14), gkey(14);
    CHECK(q->gm.value == 1210);
    gkey(13), gkey(13), gkey(13);
    CHECK(q->gm.value == 1155); /* round the top: the hour goes back too */
    told = 0;
    for (int k = 0; k < 40 && !told; k++) { /* hands swapped: told */
        CHECK(order_in(S_TIME5, 1, ME_CLOCK));
        int hh = (int)q->prob.ans / 100, mm = (int)q->prob.ans % 100;
        if (mm == 0 || mm / 5 == hh % 12 || hh % 12 == 0)
            continue;
        q->gm.value = (mm / 5) * 100 + (hh % 12) * 5;
        gkey(49);
        CHECK(q->tried_wrong && q->last_mc == MC_HANDS_SWAP);
        told = 1;
    }
    CHECK(told);
    /* Shop Till: the coins dropped add up; ring! */
    CHECK(order_in(S_CHANGE, 0, ME_TILL) && q->gm.mode == 0);
    {
        int left = (int)q->prob.ans;
        static const int val[5] = {100, 25, 10, 5, 1};
        for (int i = 0; i < 5; i++)
            while (left >= val[i])
                tev(4, 6 + i * 32 + 15, GFB - 20), left -= val[i]; /* (the drawer's coins: 32 apart) */
    }
    tev(4, 280, GFB - 18);
    CHECK(q->sub == GS_CHEER);
    CHECK(order_in(S_COINS, 1, ME_TILL) && q->gm.mode == 1); /* her coins: ring up the total */
    snprintf(t, sizeof t, "%d", (int)q->prob.ans);
    gtype(t);
    gkey(49);
    CHECK(q->sub == GS_CHEER);
    /* Treasure Dig: a tap on the map picks the point; x and y swapped is told */
    CHECK(order_in(S_COORD, 0, ME_DIG));
    int x = (int)q->prob.ans / 100, y = (int)q->prob.ans % 100;
    tev(4, 30 + x * 20, GFB - 20 - y * 18); /* (the map's columns: 20 wide) */
    CHECK(q->gm.px == x && q->gm.py == y);
    tev(4, 280, GFB - 18);
    CHECK(q->sub == GS_CHEER);
    CHECK(order_in(S_COORD, 0, ME_DIG));
    x = (int)q->prob.ans / 100, y = (int)q->prob.ans % 100;
    if (x != y && x <= 7) {
        q->gm.px = (int16_t)y, q->gm.py = (int16_t)x;
        gkey(49);
        CHECK(q->tried_wrong && q->last_mc == MC_COORD_SWAP);
    }
    /* Treasure Heist: the combination typed (digits shift in from the right, R / the point go on to
     * the next wheels), or rolled wheel by wheel; the clues hide round the room */
    for (int tr = 0; tr < 30 && !(order_in(S_DIVIDE, 2, ME_VAULT) && q->prob.kind == K_REM); tr++)
        q->rng += 77; /* (the rung with a leftover: now and then an exact one too) */
    CHECK(q->gm.mech == ME_VAULT && q->prob.kind == K_REM);
    CHECK(q->gm.lock_n >= 3 && strchr(q->gm.lock, 'R') != NULL);
    mq_answer_text(q, &q->prob, t, sizeof t);
    gtype(t);
    gkey(49);
    CHECK(q->sub == GS_CHEER);
    CHECK(order_in(S_ADDSUB, 1, ME_VAULT));
    mq_answer_text(q, &q->prob, t, sizeof t);
    CHECK(q->gm.lock_n == (int)strlen(t) && !q->gm.clues); /* as many wheels as the answer has digits */
    ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(4, 30, MQ_Y0 + 58 + 20)); /* the painting: a clue */
    CHECK(q->gm.clues == 1 && q->sub == GS_PLAY);
    { /* set the wheels by taps: each wheel rolled up to its digit */
        char an[16];
        mq_answer_text(q, &q->prob, an, sizeof an);
        int nw = q->gm.lock_n, len = (int)strlen(an);
        int pw = nw ? (304 / nw < 40 ? 304 / nw : 40) : 40; /* (the wheels: widgets, up to 40 px apart) */
        for (int i = 0; i < len; i++) {
            int wi = nw - len + i, wx = 160 - nw * pw / 2 + wi * pw + pw / 2;
            for (int k = 0; k < an[i] - '0'; k++) /* the top half: up */
                ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(4, wx, MQ_Y0 + 58 + 44 + 12));
        }
        ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(4, 290, MQ_Y0 + MQ_H - 10)); /* open! */
        CHECK(q->sub == GS_CHEER);
    }
    for (int sk = 0; sk < MQ_NSKILL; sk++) /* every lock fits its answers: money, decimals, leftovers */
        for (int dd = 0; dd < mq_rungs(sk); dd++) {
            mq_prob_t pp;
            mq_generate(q, &pp, sk, dd);
            if (!mq_mech_fits(ME_VAULT, &pp))
                continue;
            q->prob = pp;
            q->gm.round_mech = ME_VAULT;
            mq_game_order(q);
            if (q->gm.mech != ME_VAULT)
                continue;
            char an2[24];
            mq_answer_text(q, &q->prob, an2, sizeof an2);
            { /* the natural layout: each group of the answer's digits, as many wheels (never more) */
                int na = 0, nl = 0;
                for (const char *c = an2; *c; c++)
                    na += *c >= '0' && *c <= '9';
                for (int i = 0; i < q->gm.lock_n; i++)
                    nl += q->gm.lock[i] >= '0' && q->gm.lock[i] <= '9';
                CHECK(nl == na);
                if (nl != na)
                    printf("heist lock [%s] answer [%s]: %d wheels for %d digits\n", q->gm.lock, an2, nl, na);
            }
            gtype(an2);
            int32_t v = q->sub == GS_PLAY ? -2 : 0;
            gkey(49);
            CHECK(q->sub == GS_CHEER || v != -2);
            if (q->sub != GS_CHEER)
                printf("heist lock [%s] answer [%s] skill %d\n", q->gm.lock, an2, sk);
        }
    /* power-ups: the wand makes a dud vanish, the shell slows the field */
    CHECK(order_in(S_GROUPS, 1, ME_POP));
    q->gm.power[0] = PW_WAND;
    gkey(2);
    CHECK(q->gm.gone && !((q->gm.gone >> mq_game_right_opt(q)) & 1) && !q->gm.power[0]);
    q->gm.power[0] = PW_SLOW;
    gkey(2);
    CHECK(q->gm.slow_until > q->anim_t);
    /* the moments move on by themselves */
    CHECK(order_in(S_GROUPS, 1, ME_POP));
    con("correct");
    int ri = q->run_i;
    cheer_out(); /* (its reading time) */
    CHECK(q->run_i == ri + 1 || q->scene == SC_DONE_LEVEL);
    q->fun_off = false;
    fresh_module();
}

/* The question screen (and its feedback, offer and worked-example scenes) is never entered in
 * normal play: every world, every finale, rights and wrongs, tricks, fun beats on. Only the
 * grown-ups' preview shows it. */
static int quiz_seen;
static void watch(void)
{
    if ((q->scene == SC_QUIZ || q->scene == SC_FEEDBACK || q->scene == SC_OFFER || q->scene == SC_EXAMPLE) &&
        quiz_seen++ < 3)
        fprintf(stderr, "normal play entered %s\n", mq_scene_name(q));
}
static void test_no_quiz(void)
{
    con("new Noa 2");
    con("seed 77");
    con("fun on");
    q->fun_ms = 30000;
    quiz_seen = 0;
    int moves = 0;
    q->rng = 4242;
    for (int w = 0; w < MQ_NWORLD; w++)
        for (int l = 0; l <= mq_world_levels(w); l++) {
            char b[32];
            if (l == mq_world_levels(w))
                snprintf(b, sizeof b, "boss %d", w);
            else
                snprintf(b, sizeof b, "level %d %d", w, l);
            con(b);
            watch();
            for (int i = 0; i < 40 && q->scene != SC_DONE_LEVEL; i++, moves++) {
                int r = mq_rnd(q, 0, 9);
                if (q->scene == SC_FUN) {
                    con("step 600");
                    watch();
                    continue;
                }
                if (q->scene != SC_PLAY)
                    break;
                if (q->sub == GS_PLAY)
                    con(r < 6 ? "correct" : r < 8 ? "try" : r < 9 ? "wrong" : "showme");
                else if (q->sub == GS_OFFER)
                    con(r < 5 ? "showme" : "next");
                else
                    con(r < 5 ? "next" : "step 120");
                watch();
                con("step 2");
                watch();
            }
            if (q->scene == SC_DONE_LEVEL)
                con("next"), watch();
        }
    CHECK(moves > 300);
    CHECK(quiz_seen == 0);
    /* the grown-ups' preview shows it, and goes back to their screen */
    con("preview 16 1");
    CHECK(q->scene == SC_QUIZ && q->preview);
    gkey(20); /* CLEAR */
    CHECK(q->scene == SC_PARENT && !q->preview);
    fresh_module();
}

/* ------------------------------------------------------------ stories */
static void test_stories(void)
{
    char out[200], l[3][MQ_TXT];
    fresh_module();
    /* plurals and articles */
    static const char pl[][2][16] = {{"cookie", "cookies"}, {"strawberry", "strawberries"}, {"peach", "peaches"},
                                    {"box", "boxes"},       {"leaf", "leaves"},             {"mouse", "mice"},
                                    {"sheep", "sheep"},     {"lemon tart", "lemon tarts"},  {"toy", "toys"},
                                    {"potato", "potatoes"}, {"wish", "wishes"},             {"child", "children"}};
    for (size_t i = 0; i < sizeof pl / sizeof pl[0]; i++) {
        mq_plural(pl[i][0], out, sizeof out);
        CHECK(!strcmp(out, pl[i][1]));
    }
    mq_story_expand(q, "{A} has {a} apple, {a} kite and 1 {n:cookie}; 3 {n:cookie}.", 2, 0, out, sizeof out);
    CHECK(!strcmp(out, "Momo has an apple, a kite and 1 cookie; 3 cookies."));
    mq_story_expand(q, "{A} gives {B} {a} {fB} for {pA} party.", 1, 3, out, sizeof out);
    CHECK(!strcmp(out, "Dot gives Olli a muffin for his party."));
    mq_story_expand(q, "{a} {fA}", 6, 0, out, sizeof out);
    CHECK(!strcmp(out, "an apple"));
    /* every template of every kind, for every friend: grammar, the fit, kid words */
    kid_bad = 0;
    for (int k = 0; k < ST_COUNT; k++) {
        CHECK(mq_story_count(k) >= 4);
        for (int tt = 0; tt < mq_story_count(k); tt++)
            for (int a = 0; a < 6; a++) {
                mq_prob_t p;
                memset(&p, 0, sizeof p);
                p.skill = (uint8_t)(a * 11);
                q->rng = (uint32_t)(a * 977 + 1);
                mq_story(q, &p, k, tt, 88, 12, 7, "12:45 to 2:05");
                CHECK(p.text[0][0] && p.story == k + 1);
                for (int i = 0; i < 3; i++) {
                    CHECK(modx_api.text_width(modx_api.font_small, p.text[i]) <= MQ_STORY_W);
                    CHECK(!strchr(p.text[i], '{') && !strchr(p.text[i], '%') && !strstr(p.text[i], "  "));
                    CHECK(!strstr(p.text[i], " a a") && !strstr(p.text[i], " a e") && !strstr(p.text[i], " a o") &&
                          !strstr(p.text[i], " a i") && !strstr(p.text[i], " a u"));
                    kid_check(p.text[i]);
                }
                mq_story_fix(q, &p, out, sizeof out);
                CHECK(out[0] && !strchr(out, '{'));
                kid_check(out);
                for (int mc = 0; mc < MC_COUNT; mc++) {
                    mq_story_oops(q, &p, 5, mc, out, sizeof out);
                    CHECK(out[0] && !strchr(out, '{'));
                    CHECK(mq_story_wrap(q, out, l, 262) <= 3);
                    kid_check(out);
                }
            }
    }
    CHECK(kid_bad == 0);
    /* the misconception in the story: an unfair share is told with its numbers */
    mq_prob_t p;
    memset(&p, 0, sizeof p);
    p.skill = S_SHARE, p.kind = K_NUM, p.ans = 6;
    mq_story(q, &p, ST_SHARE, 0, 24, 4, 0, NULL);
    mq_story_oops(q, &p, 5, MC_NEIGHBOR, out, sizeof out);
    CHECK(strstr(out, "leaves 4") != NULL);
    mq_story_oops(q, &p, 7, MC_NEIGHBOR, out, sizeof out);
    CHECK(strstr(out, "needs 28") != NULL);
    memset(&p, 0, sizeof p);
    p.skill = S_REMWORD, p.kind = K_NUM, p.ans = 5;
    mq_story(q, &p, ST_BUS, 0, 27, 6, 0, NULL);
    mq_story_oops(q, &p, 4, MC_REM_ROUND, out, sizeof out);
    CHECK(strstr(out, "3 friends have no seat") != NULL);
    /* a theme pack's cast and traits; long names still fit */
    q->theme.loaded = true, q->theme.has_traits = true, q->theme.nspr = 2;
    snprintf(q->theme.names[0], 12, "Marshmallo");
    q->theme.role[0] = 0;
    snprintf(q->theme.names[1], 12, "Bubblegumm");
    q->theme.role[1] = 1;
    snprintf(q->theme.trait[0][TR_FOOD], 16, "cinnamon bun");
    snprintf(q->theme.trait[0][TR_COLOUR], 16, "sky blue");
    q->theme.pron[0] = PR_HE;
    mq_story_expand(q, "{A} bakes 3 {FA} for {B}; {pA} {kA} {tA}.", 0, 1, out, sizeof out);
    CHECK(!strcmp(out, "Marshmallo bakes 3 cinnamon buns for Bubblegumm; his sky blue teddy bear."));
    for (int k = 0; k < ST_COUNT; k++)
        for (int tt = 0; tt < mq_story_count(k); tt++) {
            memset(&p, 0, sizeof p);
            mq_story(q, &p, k, tt, 888, 88, 8, "12:45 to 12:55");
            for (int i = 0; i < 3; i++)
                CHECK(p.text[i][0] == 0 || modx_api.text_width(modx_api.font_small, p.text[i]) <= MQ_STORY_W);
        }
    q->theme.loaded = false, q->theme.has_traits = false;
}

/* ------------------------------------------------------------ resolution independence */
/* A scene drawn at display scale 1 and 2 (nearest-neighbour: the same art, doubled): every
 * rectangle at scale 2 is the scale-1 one doubled, and the hit-test areas are the same logical
 * rectangles. */
static int rec_n, rec_on;
static int16_t rec[2][6000][4];
static void rec_fill(int x, int y, int w, int h, uint16_t c)
{
    if (rec_n < 6000)
        rec[rec_on][rec_n][0] = (int16_t)x, rec[rec_on][rec_n][1] = (int16_t)y, rec[rec_on][rec_n][2] = (int16_t)w,
        rec[rec_on][rec_n][3] = (int16_t)h;
    rec_n++;
    (void)c;
}
static int scale_k;
static int rec_scale(void) { return scale_k; }
static void test_scale(void)
{
    static modx_api_t sapi;
    sapi = modx_api;
    sapi.fill_rect = rec_fill;
    sapi.display_scale = rec_scale;
    int nb[2], n[2];
    static mq_btn_t btn[2][MQ_MAXBTN];
    for (int k = 0; k < 2; k++) {
        scale_k = k + 1;
        memset(q, 0, mq_module_rw_size);
        memset(&ops, 0, sizeof ops);
        CHECK(mq_module_init(&sapi, q, &ops) == 0);
        CHECK(q->scale == k + 1);
        q->rng = 99;
        con("new Sky 2");
        con("seed 9");
        con("level 2 3"); /* a Pie Shop round */
        q->anim_t = 5000;
        rec_on = k, rec_n = 0;
        int rr[4];
        ops.draw(q, 0, rr);
        n[k] = rec_n, nb[k] = q->nbtn;
        memcpy(btn[k], q->btn, sizeof q->btn);
    }
    CHECK(n[0] == n[1] && n[0] > 50);
    int bad = 0;
    for (int i = 0; i < n[0] && i < 6000; i++)
        for (int j = 0; j < 4; j++)
            if (rec[1][i][j] != rec[0][i][j] * 2 && bad++ < 3)
                fprintf(stderr, "scale: rect %d: %d %d %d %d vs %d %d %d %d\n", i, rec[0][i][0], rec[0][i][1], rec[0][i][2],
                        rec[0][i][3], rec[1][i][0], rec[1][i][1], rec[1][i][2], rec[1][i][3]);
    CHECK(bad == 0);
    CHECK(nb[0] == nb[1] && !memcmp(btn[0], btn[1], sizeof btn[0]));
    fresh_module();
}



/* ------------------------------------------------------------ the meta game */
static void test_meta(void)
{
    fresh_module();
    con("new Uma 2");
    mq_profile_t *p = mq_prof(q);
    mq_home_t *h = mq_home(q);
    /* the unlock ladder: something new every few stars, early ones quick; never during an order */
    int need, have, gaps = 0;
    const char *what;
    CHECK(mq_meta_next(q, &need, &have, &what) == 3 && need == 3 && have == 0 && what[0]);
    p->stars = 3;
    con("go map");
    for (int k = 0; k < 120 && h->ladder < 1; k++) /* (a toast already up has its time first) */
        con("step 1");
    CHECK(h->ladder == 1 && q->earn_msg[0]);
    for (int k = 1; k < 14; k++) /* the steps stay small early on */
        gaps += mq_meta_next(q, NULL, NULL, NULL) <= 8, p->stars = (uint16_t)(p->stars + mq_meta_next(q, NULL, NULL, NULL)),
            q->earn_msg[0] = 0, con("step 1");
    CHECK(h->ladder == 14 && gaps >= 6);
    CHECK(p->met != 0 && h->dough >= 1); /* a friend moved in; dough */
    con("level 0 0");
    CHECK(q->sub == GS_PLAY);
    p->stars = 999, q->earn_msg[0] = 0;
    int lv = h->ladder;
    con("step 1");
    CHECK(h->ladder == lv); /* not while an order waits */
    con("correct");
    con("step 1");
    CHECK(h->ladder == lv + 1); /* in the cheer */
    /* daily quests: three different ones a day, done by playing */
    h->quest_day = 0;
    mq_quest_roll(q);
    CHECK(h->quest_day == p->days && !h->quest_done);
    CHECK(h->quest[0] != h->quest[1] && h->quest[1] != h->quest[2] && h->quest[0] != h->quest[2]);
    int c0 = h->coins;
    for (int i = 0; i < 30; i++)
        for (int e = 0; e < QE_COUNT; e++)
            mq_quest_event(q, e, 1);
    CHECK(h->quest_done == 7 && h->coins - c0 >= 3 * 10 + 20);
    uint8_t qd[3] = {h->quest[0], h->quest[1], h->quest[2]};
    mq_quest_roll(q); /* the same day: the same quests */
    CHECK(h->quest_done == 7 && h->quest[0] == qd[0]);
    p->days++;
    mq_quest_roll(q);
    CHECK(!h->quest_done);
    /* the cafe's tips grow with play time (not the clock) and are collected */
    h->tips = 0, h->tip_secs = 0, h->ladder = 0;
    for (int s = 0; s < 90 * 3; s++)
        mq_meta_second(q);
    CHECK(h->tips == 3);
    for (int s = 0; s < 90 * 100; s++)
        mq_meta_second(q);
    CHECK(h->tips == 40); /* a cap: nothing to hoard, nothing lost */
    c0 = h->coins;
    CHECK(mq_meta_collect(q) == 40 && h->coins - c0 == 40 && h->tips == 0);
    /* CR-02: a grown town with a full counter: a tip every 15 s at most, and still tips (no 16-bit wrap) */
    {
        uint8_t cafe0[6];
        memcpy(cafe0, mq_extra(q)->cafe, 6);
        for (int t = 5; t <= 6; t++) {
            for (int k = 0; k < 6; k++)
                mq_extra(q)->cafe[k] = k < t ? 1 : 0;
            h->tips = 0, h->tip_secs = 0, h->ladder = 6;
            for (int s = 0; s < 150; s++)
                mq_meta_second(q);
            CHECK(h->tips == 10);
        }
        memcpy(mq_extra(q)->cafe, cafe0, 6);
        h->tips = 0, h->tip_secs = 0, h->ladder = 0;
    }
    /* the arcade: a fact game speeds up and remembers the fastest finish */
    con("game 0");
    for (int i = 0; i < 80 && !mq_fg_finished(q); i++)
        con("correct"), con("step 5");
    CHECK(mq_fg_finished(q) && h->best_fg[0] > 0);
    uint16_t b0 = h->best_fg[0];
    con("game 0");
    for (int i = 0; i < 80 && !mq_fg_finished(q); i++)
        con("correct"), con("step 2");
    CHECK(h->best_fg[0] <= b0 && (h->best_fg[0] < b0) == (q->fg_newbest != 0));
    fresh_module();
}


/* the graphs (the playtest: "I only see 5 stars when the answer is 10"): every graph skill, rung and
 * seed, drawn in the stage's box and in a game's small card -- the symbols x the key make the data
 * (a half symbol for half), the key is drawn, in the box, and nothing of the graph leaves its box */
static int gr_x0, gr_y0, gr_x1, gr_y1, gr_out, gr_key;
static int gr_text(const font_t *f, int x, int y, const char *s, uint16_t fg, int bg)
{
    int w = modx_api.text_width(f, s);
    if (x < gr_x0 || x + w > gr_x1 + 1 || y < gr_y0 || y + f->h > gr_y1 + 2) {
        if (gr_out++ < 4)
            fprintf(stderr, "graph: \"%s\" at %d,%d..%d outside %d,%d..%d,%d\n", s, x, y, x + w, gr_x0, gr_y0, gr_x1, gr_y1);
    }
    if (s[0] == '=' && s[1] == ' ')
        gr_key++;
    return modx_api.text(f, x, y, s, fg, bg);
}
static void test_graphs(void)
{
    static modx_api_t api;
    api = modx_api, api.text = gr_text;
    const modx_api_t *api0 = q->api;
    static const int sks[2] = {S_GRAPH1, S_SCALED};
    static const int box[2][2] = {{192, 82}, {132, 68}};
    int bad = 0, pics = 0;
    for (int si = 0; si < 2; si++)
        for (int d = 0; d < mq_rungs(sks[si]); d++)
            for (int seed = 0; seed < 40; seed++) {
                mq_prob_t p;
                q->rng = (uint32_t)(seed * 7919 + d * 31 + si) | 1;
                mq_generate(q, &p, sks[si], d);
                if (p.vis != V_BARGRAPH)
                    continue;
                CHECK(sks[si] == S_SCALED || p.v[1] == 1); /* (scale 1 outside the scaled graphs' own skill) */
                for (int b = 0; b < 2; b++) {
                    gr_x0 = 6, gr_y0 = MQ_Y0 + 68, gr_x1 = 6 + box[b][0], gr_y1 = MQ_Y0 + 68 + box[b][1], gr_key = 0;
                    q->graph_key = 0;
                    q->api = &api;
                    mq_visual(q, &p, gr_x0, gr_y0, box[b][0], box[b][1], 1000);
                    q->api = api0;
                    if (!p.v[6])
                        continue;
                    pics++;
                    int want = 0;
                    for (int i = 0; i < p.v[0] && i < 4; i++)
                        want += (p.v[2 + i] + p.v[1] - 1) / p.v[1];
                    if ((q->vis_units != want || !q->graph_key || gr_key != 1) && bad++ < 4)
                        fprintf(stderr, "graph: skill %d d%d: %d symbols for %d, key %d\n", sks[si], d, q->vis_units, want, gr_key);
                }
            }
    CHECK(bad == 0 && gr_out == 0 && pics > 20);
}

/* The fact mix (the playtest: "lots of 0 x 1, then 0 / 1"): a new player's first 200 fact items,
 * answered right: the x0 / x1 rules come first as a quick check, then at most 5 % trivial facts; the
 * tables come in teaching order (x2, x5, x10, x3, ...); no division before its multiplication is
 * solid; the focus can still ask a division set */
static void test_fact_mix(void)
{
    fresh_module();
    con("new Fay 2");
    mq_profile_t *p = mq_prof(q);
    q->nqueue = 0, q->new_today = 0;
    int trivial_after = 0, after = 0, early_div = 0, first_table[13], nt = 0, before = 0;
    for (int i = 0; i < 200; i++) {
        if (i % 25 == 0)
            p->round = (uint16_t)(p->round + 1), q->new_today = 0; /* fact rounds go by */
        bool rules = mq_fact_rules_known(p);
        int f = mq_fact_next(q), a, b;
        bool div;
        mq_fact_decode(f, &a, &b, &div);
        if (div && !mq_fact_div_ready(p, f) && !p->fact[f].seen)
            early_div++;
        if (rules)
            after++, trivial_after += mq_fact_trivial(f);
        else
            before += mq_fact_trivial(f);
        if (!div && !mq_fact_trivial(f)) { /* the tables in the order they first appear */
            int t = a > b ? a : b, seen = 0;
            for (int k = 0; k < nt; k++)
                seen |= first_table[k] == t || first_table[k] == (a < b ? a : b);
            if (!seen && nt < 13)
                first_table[nt++] = t;
        }
        mq_fact_record(q, f, true, 1500);
    }
    if (getenv("MQ_TRACE"))
        fprintf(stderr, "facts: %d of %d trivial after the rules; tables %d %d %d\n", trivial_after, after, first_table[0],
                nt > 1 ? first_table[1] : -1, nt > 2 ? first_table[2] : -1);
    CHECK(after > 150 && trivial_after * 100 <= after * 5);
    CHECK(before == 2); /* the rules check: one x0 fact and one x1 fact, no more */
    CHECK(early_div == 0);
    CHECK(nt >= 2 && first_table[0] == 2 && (first_table[1] == 5 || first_table[1] == 2));
}

/* night mode (the playtest: "night mode looks the exact same"): the field is much darker at night
 * than by day on the calculator's pixels, and the answers stay readable (their bubbles bright) */
static int field_luma(int y0, int y1)
{
    long sum = 0, n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = 0; x < 320; x++) {
            uint16_t c = fb[y * 320 + x];
            sum += ((c >> 11) & 31) * 8 * 3 + ((c >> 5) & 63) * 4 * 6 + (c & 31) * 8 * 1, n += 10;
        }
    return (int)(sum / (n ? n : 1));
}
static void test_night(void)
{
    fresh_module();
    con("new Nia 2");
    q->fun_off = true;
    int r[4];
    CHECK(order_in(S_MUL10, 1, ME_POP));
    con("step 40");
    con("twist 0");
    ops.draw(q, 0, r);
    int day = field_luma(GFY, GFB);
    con("twist 1");
    ops.draw(q, 0, r);
    int night = field_luma(GFY, GFB);
    if (getenv("MQ_TRACE"))
        fprintf(stderr, "night: day %d, night %d\n", day, night);
    CHECK(night * 100 < day * 70); /* a big difference */
    CHECK(night > 20);             /* (not black: the answers and their bubbles show) */
    q->fun_off = false;
}

/* the twists: never in a game she has not played a few times normally; off by the grown-ups' switch;
 * a new twist comes with its line ("Tonight it's dark: the answers glow!") */
static void test_twist_gate(void)
{
    fresh_module();
    con("new Tia 2");
    q->fun_off = true;
    mq_prof(q)->problems = 50;
    mq_pace_t *pc = mq_pace(q);
    for (int k = 0; k < 16; k++)
        pc->mech_rounds[k] = 0;
    mq_extra(q)->mod_n = 5;
    con("level 0 0");
    CHECK(q->gm.mod == 0); /* a game not played yet: no twist */
    int m = q->gm.round_mech;
    CHECK(m < 16 && pc->mech_rounds[m] == 1);
    for (int k = 0; k < 16; k++)
        pc->mech_rounds[k] = 5;
    int twists = 0;
    for (int r = 0; r < 6; r++) {
        mq_extra(q)->mod_n = 5;
        con("go map");
        con("level 0 0");
        twists += q->gm.mod != 0;
    }
    CHECK(twists >= 5 && pc->twists_seen != 0);
    pc->twists_off = 1;
    for (int r = 0; r < 4; r++) {
        mq_extra(q)->mod_n = 5;
        con("go map");
        con("level 0 0");
        CHECK(q->gm.mod == 0);
    }
    pc->twists_off = 0;
    q->fun_off = false;
}

/* the squishies giggle (every kind, its own voice): a giggle on a squish with the squeeze in its param,
 * a sigh on letting go, rate-limited when squished fast */
static int gg_n, gg_sigh, gg_kind, gg_lvl;
static void gg_hook(int id, int param)
{
    if (id == MODX_SFX_GIGGLE)
        gg_n++, gg_kind = param & 15, gg_lvl = ((param >> 4) & 3) > gg_lvl ? ((param >> 4) & 3) : gg_lvl;
    if (id == MODX_SFX_SIGH)
        gg_sigh++;
    modx_api.sfx(id, param);
}
static void test_giggles(void)
{
    static modx_api_t api;
    api = modx_api, api.sfx = gg_hook;
    memset(q, 0, mq_module_rw_size);
    memset(&ops, 0, sizeof ops);
    CHECK(mq_module_init(&api, q, &ops) == 0);
    q->rng = 7, q->scenes_off = true;
    con("new Gigi 2");
    con("fun 36000");
    for (int k = 0; k < SQ_KINDS; k++) {
        con("fun beat squishy");
        CHECK(q->scene == SC_FUN);
        q->sq.kind = (uint8_t)k;
        con("step 40");
        int cx = q->sq.cx, cy = q->sq.cy;
        gg_n = gg_sigh = 0, gg_lvl = 0, gg_kind = -1;
        ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(1, cx, cy)); /* a squash, held */
        for (int f = 0; f < 30; f++) {
            ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(2, cx, cy + f));
            con("step 1");
        }
        ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(3, cx, cy + 30));
        con("step 10");
        CHECK(gg_n >= 1 && gg_kind == k && gg_sigh >= 1);
        if (gg_n < 1 || gg_kind != k)
            fprintf(stderr, "giggles: kind %d: %d giggles (kind %d), %d sighs\n", k, gg_n, gg_kind, gg_sigh);
    }
    /* a light poke: a small giggle; a long hard squash: a bigger one */
    gg_n = 0, gg_lvl = 0;
    int cx = q->sq.cx, cy = q->sq.cy;
    con("step 20");
    ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(1, cx, cy));
    con("step 1");
    ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(3, cx, cy));
    con("step 20");
    int poke = gg_lvl;
    gg_lvl = 0;
    ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(1, cx, cy));
    for (int f = 0; f < 40; f++)
        ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(2, cx + f / 2, cy + f)), con("step 1");
    ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(3, cx + 20, cy + 40));
    con("step 10");
    CHECK(gg_lvl > poke);
    /* rapid squishing: at most a giggle every 450 ms */
    gg_n = 0;
    con("step 20");
    for (int f = 0; f < 25; f++) { /* 25 pokes in a second */
        ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(1, cx, cy));
        con("step 1");
        ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(3, cx, cy));
    }
    CHECK(gg_n <= 3);
    if (getenv("MQ_TRACE"))
        fprintf(stderr, "giggles: poke %d, squash %d, rapid %d\n", poke, gg_lvl, gg_n);
}

/* ------------------------------------------------------------ Bongo Beats */
static int bg_sfx[MODX_SFX_COUNT], bg_offgrid, bg_grid;
static void bg_sfx_hook(int id, int param)
{
    (void)param;
    if (id > 0 && id < MODX_SFX_COUNT)
        bg_sfx[id]++;
    const mq_toy_t *t = &q->toy;
    if (bg_grid && (id == MODX_SFX_MEW || id == MODX_SFX_PAW) && t->song_bpm) { /* on the song's grid (within a frame) */
        uint32_t bm = 60000u / t->song_bpm, now = q->anim_t - t->t0;
        uint32_t six = bm / 4, off = now >= 4 * bm ? (now - 4 * bm) % six : 0;
        if (off > MQ_FRAME_MS + 1 && six - off > 2 && bg_offgrid++ < 4)
            fprintf(stderr, "bongo: sfx %d at %u ms, %u ms off the grid\n", id, now, off);
    }
    modx_api.sfx(id, param);
}
static int bg_wait_order(int max) /* frames until an order waits at (or nears) the line */
{
    int f = 0;
    for (; f < max; f++) {
        const mq_toy_t *t = &q->toy;
        bool near = false;
        for (int i = 0; i < t->nn; i++)
            near |= t->note[i].kind == 20 && t->note[i].state != 1 &&
                    (int32_t)(q->anim_t - t->t0) >= (int32_t)t->note[i].t - 60;
        if (near || t->done)
            break;
        con("step 1");
    }
    return f;
}
static void test_bongo(void)
{
    static modx_api_t api;
    api = modx_api, api.sfx = bg_sfx_hook;
    memset(q, 0, mq_module_rw_size);
    memset(&ops, 0, sizeof ops);
    CHECK(mq_module_init(&api, q, &ops) == 0);
    q->rng = 0x2545F491u, q->scenes_off = true, q->fun_ms = 0;
    con("new Bongo 2");
    q->fun_off = true;
    mq_toy_t *t = &q->toy;
    /* the songs: ten originals, valid (every bar 4/4, the chords filling the bars, a one-bar beat), the
     * tune singable (G3..D6), 90-130 bpm, a verse and a chorus each, different names */
    CHECK(MQ_NSONG >= 10);
    for (int sg = 0; sg < MQ_NSONG; sg++) {
        static mq_sev_t ev[MQ_SONG_EV];
        char err[80] = "";
        int len16 = 0, n = mq_song_events(&modx_api, sg, ev, MQ_SONG_EV, &len16, err, sizeof err);
        CHECK(n > 0 && len16 > 0 && len16 % 16 == 0);
        if (n <= 0)
            fprintf(stderr, "song %d: %s\n", sg, err);
        int lo = 127, hi = 0, mel = 0, beat = 0, bass = 0, chord = 0;
        for (int i = 0; i < n; i++) {
            CHECK(ev[i].at < len16 && (i == 0 || ev[i].at >= ev[i - 1].at));
            if (ev[i].kind == SE_MEL)
                lo = ev[i].a < lo ? ev[i].a : lo, hi = ev[i].a > hi ? ev[i].a : hi, mel++;
            beat += ev[i].kind == SE_BEAT, bass += ev[i].kind == SE_BASS, chord += ev[i].kind == SE_CHORD;
        }
        CHECK(lo >= 55 && hi <= 86 && hi - lo <= 19 && mel >= 32 && beat >= 32 && bass >= 16 && chord >= 8);
        CHECK(mq_song_bpm(sg) >= 90 && mq_song_bpm(sg) <= 130);
        for (int k = 0; k < sg; k++)
            CHECK(strcmp(mq_song_name(k), mq_song_name(sg)) != 0);
    }
    /* a new player opens Bongo Beats: the demo shows and the order is up within 3 seconds; four
     * drums, four different answers, one right */
    mq_toys_open(q, TOY_BEATS);
    CHECK(q->scene == SC_TOYS && t->mode == TOY_BEATS && t->demo_t != 0 && t->nsev > 0);
    con("step 75");
    int ni = -1;
    for (int i = 0; i < t->nn; i++)
        if (t->note[i].kind == 20 && t->note[i].state == 0)
            ni = i;
    CHECK(ni >= 0);
    {
        int o = ni >= 0 ? t->note[ni].pad & 1 : 0, right = 0;
        CHECK(t->ocue[o][0] || t->onterm[o]);
        for (int i = 0; i < 4; i++) {
            right += t->ocand[o][i] == t->oans[o];
            for (int k = 0; k < i; k++)
                CHECK(t->ocand[o][k] != t->ocand[o][i]);
        }
        CHECK(right == 1 && t->ocand[o][t->oright[o]] == t->oans[o]);
    }
    /* a correct player: every order on the beat, the song to its finale (mews on the grid, paws, a trill
     * on the combo, the big meow), coins */
    memset(bg_sfx, 0, sizeof bg_sfx), bg_offgrid = 0, bg_grid = 1;
    int coins0 = mq_home(q)->coins, orders = 0;
    for (int k = 0; k < 40 && !t->done; k++) {
        bg_wait_order(400);
        if (mq_toys_answer(q, true))
            orders++;
        con("step 2");
    }
    for (int k = 0; k < 100 && !t->done; k++)
        con("step 1");
    CHECK(t->done && orders == 8 && t->perfect == 8 && t->miss == 0 && t->score > 0);
    CHECK(mq_home(q)->coins > coins0 && mq_home(q)->best_beat == t->score);
    CHECK(bg_sfx[MODX_SFX_MEW] > 16 && bg_sfx[MODX_SFX_PAW] > 16 && bg_sfx[MODX_SFX_BASS] > 8 && bg_sfx[MODX_SFX_CHORD] > 2);
    CHECK(bg_sfx[MODX_SFX_TRILL] > 0 && bg_sfx[MODX_SFX_MEOW] == 1 && bg_offgrid == 0);
    bg_grid = 0; /* (below: the clock set by hand) */
    /* the judging: late within the window is good, far off is a fizzle (the order still waits), a wrong
     * drum is a thud (no points, the lane waits, the answer shown after the beat), the right one then
     * goes on without points */
    mq_toys_open(q, TOY_BEATS);
    CHECK(t->demo_t == 0); /* (seen) */
    bg_wait_order(400);
    ni = -1;
    for (int i = 0; i < t->nn; i++)
        if (t->note[i].kind == 20 && t->note[i].state == 0 && (ni < 0 || t->note[i].t < t->note[ni].t))
            ni = i;
    CHECK(ni >= 0);
    if (ni >= 0) {
        int o = t->note[ni].pad & 1;
        q->anim_t = t->t0 + t->note[ni].t - 400; /* far off: a fizzle */
        mq_toys_key(q, 0, '1' + t->oright[o]);
        CHECK(t->note[ni].state == 0 && t->score == 0 && t->fizz_t);
        q->anim_t = t->t0 + t->note[ni].t + 100; /* late but in the window: good */
        t->press_t[3] = 0, t->last_press = 0;
        mq_toys_key(q, 0, '1' + t->oright[o]);
        CHECK(t->note[ni].state == 1 && t->note[ni].j == 1 && t->good == 1 && t->score == 1);
    }
    bg_wait_order(400);
    ni = -1;
    for (int i = 0; i < t->nn; i++)
        if (t->note[i].kind == 20 && t->note[i].state == 0 && (ni < 0 || t->note[i].t < t->note[ni].t))
            ni = i;
    CHECK(ni >= 0);
    if (ni >= 0) {
        int sc = t->score;
        CHECK(mq_toys_answer(q, false));
        CHECK(t->note[ni].state == 3 && t->hold && t->score == sc && t->combo == 0);
        uint32_t frozen = q->anim_t - t->t0;
        con("step 30");
        CHECK(q->anim_t - t->t0 == frozen); /* the lane waits */
        CHECK(mq_toys_answer(q, true));
        CHECK(t->note[ni].state == 1 && t->score == sc && !t->hold);
    }
    /* let go: a note nobody hits waits at the line (the song does not end without the maths) */
    bg_wait_order(400);
    con("step 40");
    CHECK(t->hold);
    uint32_t w0 = q->anim_t - t->t0;
    con("step 100");
    CHECK(q->anim_t - t->t0 == w0 && !t->done);
    /* a masher: random drums every 80 ms for a minute -- no points, no song's end, no coins; the
     * guide steps in ("Listen! ...") and only the order's drum stays lit */
    mq_toys_open(q, TOY_BEATS);
    coins0 = mq_home(q)->coins;
    int calm = 0;
    uint32_t r = 12345;
    for (int f = 0; f < 1500; f++) {
        if (f % 2 == 0) {
            r = r * 1103515245u + 12345u;
            mq_toys_key(q, 0, '1' + (int)(r >> 16) % 4);
        }
        calm |= t->calm;
        con("step 1");
    }
    CHECK(t->score == 0 && !t->done && mq_home(q)->coins == coins0 && calm && !strncmp(t->say, "Listen!", 7));
    q->fun_off = false;
}

/* ------------------------------------------------------------ the toy box */
static void test_toys(void)
{
    fresh_module();
    con("new Toy 2");
    q->fun_off = true;
    /* the beat clock and its windows: perfect within 60 ms, good within 130, else a miss */
    CHECK(mq_beat_bpm(0) == 90 && mq_beat_bpm(1) == 110 && mq_beat_bpm(2) == 130);
    CHECK(mq_beat_ms(1) == 545);
    CHECK(mq_beat_judge(0) == 2 && mq_beat_judge(60) == 2 && mq_beat_judge(-60) == 2);
    CHECK(mq_beat_judge(61) == 1 && mq_beat_judge(-130) == 1 && mq_beat_judge(131) == 0 && mq_beat_judge(-400) == 0);
    test_bongo();
    mq_toy_t *t = &q->toy;
    /* the jam: free drumming, a fun beat that ends by itself and goes on with the story */
    q->fun_off = false;
    mq_fun_start(q, BEAT_JAM, FR_MAP);
    CHECK(q->scene == SC_TOYS && q->toy.mode == TOY_JAM);
    mq_toys_press(q, 100);
    mq_toys_press(q, 103);
    CHECK(q->toy.score == 2);
    con("step 600");
    CHECK(q->scene == SC_MAP);
    q->fun_off = true;
    /* the magic square: the right numbers make every line 15 */
    mq_toys_open(q, TOY_MAGIC);
    CHECK(t->cell[4] == 5 && t->sol[4] == 5);
    for (int i = 0; i < 9; i++)
        if (!t->cell[i]) {
            mq_toys_press(q, 30 + i);
            mq_toys_key(q, 0, '0' + t->sol[i]);
        }
    CHECK(t->solved && t->say[0]);
    /* the cage puzzle */
    mq_toys_open(q, TOY_CAGE);
    for (int i = 0; i < 9; i++) {
        mq_toys_press(q, 30 + i);
        mq_toys_press(q, 40 + (i == 8 ? (t->sol[i] % 3) + 1 : t->sol[i])); /* the last one wrong first */
    }
    CHECK(!t->solved);
    mq_toys_press(q, 40 + t->sol[8]);
    CHECK(t->solved);
    /* the secret code: letters by the rule */
    mq_toys_open(q, TOY_CODE);
    for (int k = 0; k < 12; k++) {
        const char *m = "YOU ROCK";
        (void)m;
    }
    {
        static const char messages[8][12] = {"YOU ROCK", "HUGS", "CAKE PARTY", "SUPER STAR", "BEST FRIEND", "HELLO",
                                             "YAY YOU", "LETS PLAY"};
        const char *m = messages[t->msg & 7];
        for (int i = 0; m[i]; i++)
            if (m[i] != ' ')
                mq_toys_key(q, 0, m[i]);
    }
    CHECK(t->solved);
    /* the mandala: the goal's share of the outer ring */
    mq_toys_open(q, TOY_MANDALA);
    mq_toys_press(q, 120 + t->goal_c);
    for (int s = 0; s < t->goal_n; s++) {
        int a = s * 45 + 22;
        tev(4, 104 + mq_isin(a) * 80 / 1024, MQ_Y0 + 104 - mq_icos(a) * 80 / 1024);
    }
    CHECK(t->solved);
    /* number magic: always 3; the 9s */
    mq_toys_open(q, TOY_TRICK);
    for (int k = 0; k < 6; k++)
        mq_toys_press(q, 4);
    CHECK(t->step == 5 && t->say[0]);
    /* the kaleidoscope: strokes mirrored; between full frames only the new dots are drawn */
    mq_toys_open(q, TOY_KALEIDO);
    con("step 1");
    CHECK(q->toy.kal_ok && mq_toys_partial(q));
    q->frame_px = 0;
    for (int k = 0; k < 10; k++)
        tev(2, 180 + k, MQ_Y0 + 80);
    CHECK(t->npts == 10 && mq_toys_partial(q));
    con("step 1");
    CHECK(t->drawn == 10);
    mq_toys_key(q, 0, '4');
    CHECK(t->sym == 4 && !t->kal_ok);
    q->fun_off = false;
    fresh_module();
}

/* ------------------------------------------------------------ a scripted level */
static void test_level(void)
{
    con("seed 7");
    con("new Mia 2");
    con("engine stop"); /* the round's moments with the stop's own skill (the engine: test_engine) */
    CHECK(q->scene == SC_MAP);
    CHECK(mq_home(q)->coins == 20);
    CHECK(con("level 0 0") == 0 && q->scene == SC_PLAY && q->prob.skill == S_GROUPS && q->sub == GS_PLAY);
    /* right, wrong (feedback names the misconception, then a similar problem), right... */
    con("correct");
    CHECK(q->sub == GS_CHEER && q->combo == 1);
    cheer_out();
    CHECK(q->scene == SC_PLAY && q->sub == GS_PLAY && q->run_i == 1);
    /* a wrong answer: a dud in the game (the friend says what it means), then one more try */
    con("try");
    CHECK(q->scene == SC_PLAY && q->sub == GS_PLAY && q->tried_wrong && q->gm.gone && q->gm.say[0]);
    uint32_t own = mq_prob_ident(&q->prob);
    con("try"); /* the second miss: a helper friend pops in */
    CHECK(q->scene == SC_PLAY && q->sub == GS_HELP && q->last_mc != MC_NONE && q->gm.helped);
    CHECK(mq_prof(q)->review[0] == S_GROUPS + 1); /* review scheduled */
    CHECK(mq_pace(q)->helped[S_GROUPS] == 1);    /* the play log */
    CHECK(mq_prob_ident(&q->ex) != own);         /* the walkthrough: fresh numbers */
    for (int i = 0; i < 12 && q->sub == GS_HELP; i++)
        con("next");                             /* a tap a step */
    CHECK(q->scene == SC_PLAY && q->sub == GS_PLAY && mq_prob_ident(&q->prob) == own); /* her own order again */
    uint16_t tw = mq_pace(q)->teamwork;
    con("correct");
    CHECK(q->sub == GS_CHEER && mq_pace(q)->teamwork == tw + 1 && !strcmp(q->gm.big_s, "Teamwork!"));
    cheer_out();
    CHECK(q->scene == SC_PLAY && q->sub == GS_PLAY && q->run_i == 2);
    /* missed again after help: together (the duds fade, the friend points), and it ends in a success */
    con("wrong");
    CHECK(q->sub == GS_HELP);
    for (int i = 0; i < 12 && q->sub == GS_HELP; i++)
        con("next");
    con("try");
    CHECK(q->sub == GS_PLAY && q->gm.hoh);
    int left = 0;
    for (int i = 0; i < q->gm.nopt; i++)
        left += !((q->gm.gone >> i) & 1);
    CHECK(left == 1 && mq_correct(&q->prob, q->gm.opt[mq_game_right_opt(q)]));
    con("wrong"); /* (nothing wrong is left to pick) */
    CHECK(q->sub == GS_PLAY);
    con("correct");
    CHECK(q->sub == GS_CHEER);
    con("step 40");
    for (int i = 0; i < 10 && q->scene == SC_PLAY; i++) {
        con("correct");
        cheer_out();
    }
    CHECK(q->scene == SC_DONE_LEVEL);
    CHECK(q->stars_won >= 1 && mq_prof(q)->sk[S_GROUPS].plays == 1);
    CHECK(q->reward_sticker != 0xFF);
    /* ends on a win: the last problem of the run was answered right */
    CHECK(q->combo >= 1);
}

/* CR-10 (the code review): the trick's completion order after two misses is the trick's own numbers, and no other
 * order is generated and thrown away on the way (the no-repeat history does not move) */
static void test_completion_no_discard(void)
{
    fresh_module();
    con("new Cm 3");
    con("engine stop");
    mq_prof(q)->sk[S_ADDSUB].mastery = 50, mq_prof(q)->sk[S_ADDSUB].flags = 0;
    int got = 0;
    for (int tries = 0; tries < 6 && !got; tries++) {
        con("skill 16 1");
        q->rapid = 0, q->guess_n = 0;
        q->missrun = 2, q->sub = GS_WHY;
        mq_run_after_miss(q);
        if (q->sub != GS_OFFER)
            continue;
        con("showme");
        if (q->sub != GS_TRICK || !q->ex_fade || !q->ex_then_similar)
            continue;
        uint32_t h[2][10];
        memcpy(h, q->hist_id, sizeof h);
        int run = q->run_i;
        for (int i = 0; i < 8 && q->sub == GS_TRICK; i++)
            con("next");
        CHECK(q->sub == GS_PLAY && mq_prob_ident(&q->prob) == mq_prob_ident(&q->ex));
        CHECK(!memcmp(h, q->hist_id, sizeof h));
        CHECK(q->run_i == run + 1);
        got = 1;
    }
    CHECK(got);
}

/* CR-05 (the code review): a theme pack's sprites past 64 KB of the pool keep their start (32-bit offsets), and
 * the run-length decoder is bounded: a picture larger than the cell buffer is not drawn, and a run of 0 ends it
 * (never an endless loop, never a write past the buffer) */
static void test_rle_bounds(void)
{
    q->theme.off[0] = THM_PXPOOL - 4;
    CHECK(q->theme.off[0] == THM_PXPOOL - 4 && THM_PXPOOL - 4 > 65535);
    q->theme.off[0] = 0;
    static uint8_t dst[THM_MAXCELL * THM_MAXCELL + 64];
    memset(dst, 0xEE, sizeof dst);
    const uint8_t big[] = {0, 0, 255, 255, 255, 1, 255, 1};
    mq_rle_decode(big, dst, 48); /* 255 x 255 into a 48 x 48 buffer: nothing */
    CHECK(dst[0] == 0xEE && dst[THM_MAXCELL * THM_MAXCELL] == 0xEE);
    const uint8_t zero[] = {0, 0, 8, 8, 3, 2, 0, 5, 9, 9};
    mq_rle_decode(zero, dst, 8); /* 3 pixels, then a run of 0: stops */
    CHECK(dst[0] == 2 && dst[2] == 2 && dst[3] == 0xEE);
    const uint8_t wide[] = {40, 0, 20, 2, 40, 1};
    memset(dst, 0xEE, sizeof dst);
    mq_rle_decode(wide, dst, 48); /* x0 + w past the row: nothing */
    CHECK(dst[40] == 0xEE);
}

/* CR-16 (the code review): leaving from the four newest scenes is still logged (with their nearest kept scene) */
static void test_quit_newest(void)
{
    static const int sc[4] = {SC_QUIZ, SC_TOYS, SC_CHAPTER, SC_MERGE}, as[4] = {SC_PLAY, SC_ROOM, SC_WORLD, SC_ROOM};
    for (int i = 0; i < 4; i++) {
        uint16_t before = mq_learn(q)->quit[as[i]];
        int s0 = q->scene;
        q->scene = (uint8_t)sc[i];
        mq_coach_quit(q);
        q->scene = (uint8_t)s0;
        CHECK(mq_learn(q)->quit[as[i]] == before + 1);
    }
}

int main(void)
{
    gfx_init(fb);
    static uint8_t arena[MODX_POOL + MODX_SAVE_MAX + 16];
    modx_mem_init(arena, true);
    q = calloc(1, mq_module_rw_size);
    fresh_module();
    if (getenv("MQ_DUMP")) {
        static const char vn[][10] = {"NONE","ARRAY","GROUPS","PIE","BAR","BAR2","BASE10","NUMLINE","CLOCK","GRID","LSHAPE","ANGLE","SHAPE","COINS","BARGRAPH","LINEPLOT","TABLE","COORD","PRISM","PV","RULER","SCALE","LINES","PATTERN","JUG"};
        for (int sk = 0; sk < MQ_NSKILL; sk++)
            for (int d = 0; d < 4; d += 3) {
                mq_prob_t p;
                q->rng = (uint32_t)sk * 7 + (uint32_t)d + 1;
                mq_generate(q, &p, sk, d);
                char a[24];
                mq_format(q, &p, p.ans, a, sizeof a);
                printf("%2d %-20s d%d %-8s v=%d,%d,%d,%d,%d,%d,%d,%d big=%d k=%d ans=%s | %s %s %s\n", sk, mq_skill[sk].name, d,
                       vn[p.vis], p.v[0], p.v[1], p.v[2], p.v[3], p.v[4], p.v[5], p.v[6], p.v[7], (int)p.big, p.kind, a,
                       p.text[0], p.text[1], p.text[2]);
            }
        return 0;
    }
    if (getenv("MQ_ONLY")) { /* one test (development) */
        const char *o = getenv("MQ_ONLY");
        if (!strcmp(o, "giveaway"))
            test_no_giveaway_games();
        else if (!strcmp(o, "games"))
            test_games();
        else if (!strcmp(o, "flowers"))
            test_flowers();
        else if (!strcmp(o, "engine"))
            test_engine();
        else if (!strcmp(o, "pacing"))
            test_pacing();
        else if (!strcmp(o, "floating"))
            test_floating();
        else if (!strcmp(o, "newplayer"))
            test_new_player();
        else if (!strcmp(o, "guide"))
            test_guide();
        else if (!strcmp(o, "focus"))
            test_focus();
        else if (!strcmp(o, "kid"))
            test_kid_screens();
        else if (!strcmp(o, "stories"))
            test_stories();
        else if (!strcmp(o, "perf")) { /* the frame cost of the busiest scenes (docs/MATHQUEST.md) */
            con("new Pia 2");
            con("grant bosses");
            con("grant stars 120");
            con("seed 3");
            q->fun_off = true;
            con("level 0 0"), con("step 30"), con("perf 50");
            con("correct"), con("step 2"), con("perf 50");
            con("level 1 4"), con("step 30"), con("perf 50");
            con("skill 17 1"), con("mech \"Gate\""), con("perf 50");
            con("go map"), con("step 200"), con("perf 50");
            con("town 700"), con("perf 50");
            con("world 0"), con("perf 50");
            q->scenes_off = false; /* this pass's new scenes: the story acted, a twist, the heist, the kitchen,
                                    * a payoff, the town at night */
            con("level 0 0"), con("step 20"), con("perf 20");
            con("correct"), con("step 50"), con("twist 1"), con("step 50"), con("perf 50");
            con("skill 21 1"), con("perf 50");
            con("merge open"), con("merge demo off"), con("merge order 7 8");
            for (int c = 0; c < KC_CELLS; c++) /* a full counter, a full plate */
                mq_extra(q)->board[c] = (uint8_t)(c % 3 ? CK_CODE(CK_PINK, 1 << c % 4) : CK_CODE(CK_LEMON, 3 + 3 * (c % 2)));
            for (int k = 0; k < KC_PLATE; k++)
                mq_extra(q)->plate[k] = (uint8_t)CK_CODE(CK_PINK, 8); /* (eight eighths: a whole cake) */
            con("perf 50");
            con("chapter 2"), con("chapter 2"), con("chapter 2"), con("chapter 2");
            mq_chapter_next(q), mq_chapter_next(q), con("perf 50");
            con("go map"), con("day 12"), con("town 700"), con("perf 50");
        } else if (!strcmp(o, "make"))
            test_make();
        else if (!strcmp(o, "longrounds"))
            test_long_rounds();
        else if (!strcmp(o, "typed"))
            test_typed_share();
        else if (!strcmp(o, "town"))
            test_town();
        else if (!strcmp(o, "kart"))
            test_kart();
        else if (!strcmp(o, "words"))
            test_board_words(true);
        printf("test_mathquest: %d checks, %d failed\n", checks, fails);
        return fails ? 1 : 0;
    }
    test_coverage();
    test_generators();
    test_classifier();
    test_grade3_gaps();
    test_facts();
    test_completion();
    test_rle_bounds();
    test_quit_newest();
    test_completion_no_discard();
    test_save_load();
    test_level();
    test_themes();
    test_kid_screens();
    test_toys();
    test_meta();
    test_games();
    test_flowers();
    test_engine();
    test_pacing();
    test_guide();
    test_focus();
    test_no_giveaway_games();
    test_board_words(false);
    test_kart();
    test_scene();
    test_friends();
    test_merge();
    test_twists();
    test_new_games();
    test_floating();
    test_new_player();
    test_town();
    test_make();
    test_typed_share();
    test_no_quiz();
    test_stories();
    test_scale();
    test_action();
    test_coach();
    test_colours();
    test_learning();
    test_learn_migrate();
    test_fun();
    test_no_repeats();
    test_no_giveaway();
    test_catalogue();
    test_home_economy();
    test_long_rounds();
    test_home_shop();
    test_home_rooms();
    test_home_save();
    test_long_name();
    test_graphs();
    test_fact_mix();
    test_night();
    test_twist_gate();
    test_giggles();
    test_theme_items();
    test_theme_anim();
    printf("test_mathquest: %d checks, %d failed\n", checks, fails);
    free(q);
    return fails ? 1 : 0;
}
