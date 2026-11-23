/*
 * Math Quest: the game round. There is no question screen: every answer is a game action.
 *
 * A round (a stop on a world's path, or its finale) is one game with an order board: a friend
 * has a little drama (mq_story.c) and the player fixes it by playing. The round's game comes
 * from its skill (mq_skill_mech[]); a problem brought in from an earlier skill is retold in the
 * round's game when it fits, else in one of its own.
 *
 *   Bubble Pop   answers float up as bubbles: pop the one that fits (tap; arrows + ENTER; or type it)
 *   Cloud Catch  a cloud catches the falling answer star (arrows, drag, or type it to steer)
 *   Gate Dash    run through the right gate (UP / DOWN or a tap picks the lane, RIGHT / ENTER dashes)
 *   Kart Race    drive over the right boost pad (the same controls); right answers are boosts
 *   Peekaboo     friends pop out of holes carrying answers or sums: bop the right one (keys 7-9, 4-6)
 *   Sort Slide   slide the card into the right bin (swipe, tap a bin, arrows + ENTER, keys 1-4)
 *   Hop Along    hop along a number line and land on the spot (arrows, a tap, ENTER)
 *   Sky Fling    aim by degrees (a protractor) or set the power by fractions, then fling
 *   Pie Shop     frost the slices of a pie by dragging across them (or keys 1-9)
 *   Build It     drag the corner of a rug, a fence or a flower bed to the order's size
 *   Clock Tower  turn the clock's hands by dragging (arrows; or type the time)
 *   Shop Till    drop coins into the till, or ring up the total on its keys
 *   Treasure Dig dig at the map's spot (tap, arrows)
 *   Vault Run    type the vault's code
 *
 * Everything waits for the player (no timer on thinking); pure fun (stars, coins, plain bubbles,
 * power-ups) streams between orders. A dud (a wrong answer) is a moment in the story: the friend
 * says what it would mean ("3 friends have no seat yet!"), the dud bubble wilts, and the player
 * tries again; a second miss shows why in the game itself (the right answer built, set or popped,
 * with the model) and a similar order follows. The guide's trick plays as a short animation in
 * the round (soft key F1, or offered after two misses). The bookkeeping -- mastery, the 85 % rule,
 * fact boxes, the review cache, the play log -- is the same as before (mq_judge, mq_main.c).
 *
 * Coordinates are logical (the 320 x 240 screen); positions of moving things are x 16.
 */
#include "mq.h"

enum { HK_UP = 12, HK_LEFT = 13, HK_RIGHT = 14, HK_DOWN = 15, HK_DEL = 8, HK_ENTER = 49, HK_DIV = 30, HK_DOT = 47,
       HK_F2 = 2 };
enum { TV_DOWN = 1, TV_DRAG, TV_UP, TV_TAP };
enum { GO_NONE, GO_ANS, GO_STAR, GO_COIN, GO_ROCK, GO_POWER, GO_PLAIN, GO_PAL };

#define FY (MQ_Y0 + 58)          /* the field: below the order board */
#define FB (MQ_Y0 + MQ_H)
#define FH (FB - FY)
#define LAUNCH_CX 140          /* Sky Fling's protractor: its centre (left of the fling! button: its 0 shows) and radius */
#define LAUNCH_R 104
#define DIG_DX 20              /* Treasure Dig's map: a column's width */
#define DRAWER_X(k) (6 + (k) * 32) /* the till's coin drawer: five coins left of the notebook and ring! buttons */
#define DRAWER_W 30
#define Q4(v) ((int16_t)((v) * 16))

static const char me_name[ME_COUNT][16] = {"Bubble Pop", "Cloud Catch", "Gate Dash", "Kart Race",  "Peekaboo",
                                           "Sort Slide", "Hop Along",   "Sky Fling", "Pie Shop",   "Build It",
                                           "Clock Tower", "Shop Till",  "Treasure Dig", "Treasure Heist", "Cake Kitchen",
                                           "Pet Shop"};
static const char me_goal[ME_COUNT][32] = {
    "Pop the bubble that fits!", "Catch the star that fits!", "Dash through the right gate!",
    "Zoom over the right pad!",  "Bop the friend who fits!",  "Slide it in the right bin!",
    "Hop to the right spot!",    "Aim it and fling!",         "Frost the pie just right!",
    "Drag the corner to build!", "Turn the hands!",           "Drop in the coins!",
    "Dig at the right spot!",    "Dial the answer, then open!",   "Type the amount and mix!",
    "Give the right change!"};

const char *mq_mech_name(int m) { return me_name[m >= 0 && m < ME_COUNT ? m : 0]; }
uint16_t *mq_best_of(mq_t *q, int m)
{
    m = m >= 0 && m < ME_COUNT ? m : 0;
    return m < ME_COUNT_V4 ? &mq_home(q)->best_game[m] : &mq_extra(q)->best_new[(m - ME_COUNT_V4) & 3];
}
const char *mq_mech_goal(int m) { return me_goal[m >= 0 && m < ME_COUNT ? m : 0]; }

/* Every skill's games, its favourite first (tests: every skill has one; mq_mech_for picks). */
/* up to four games, packed 4 bits each (game + 1), the favourite in the low bits */
#define MG1(a) ((a) + 1)
#define MG2(a, b) (MG1(a) | ((b) + 1) << 4)
#define MG3(a, b, c) (MG2(a, b) | ((c) + 1) << 8)
#define MG4(a, b, c, d) (MG3(a, b, c) | ((d) + 1) << 12)
const uint16_t mq_skill_mech[MQ_NSKILL] = {
    [S_GROUPS] = MG3(ME_POP, ME_CATCH, ME_KART),           [S_SHARE] = MG3(ME_BUILD, ME_POP, ME_CATCH),
    [S_MDWORD] = MG3(ME_POP, ME_CATCH, ME_SORT),           [S_UNKNOWN] = MG3(ME_BUILD, ME_ZAP, ME_GATES),
    [S_PROPS] = MG2(ME_ZAP, ME_POP),    [S_DIVFACT] = MG3(ME_BUILD, ME_ZAP, ME_CATCH),
    [S_FACTS] = MG4(ME_ZAP, ME_POP, ME_CATCH, ME_GATES), [S_TWOSTEP] = MG2(ME_POP, ME_GATES),
    [S_PATTERN] = MG3(ME_CATCH, ME_SORT, ME_POP), [S_TIMES_AS] = MG2(ME_POP, ME_KART),
    [S_REMWORD] = MG2(ME_GATES, ME_POP), [S_FACTORS] = MG3(ME_SORT, ME_ZAP, ME_POP),
    [S_RULE] = MG2(ME_CATCH, ME_POP),
    [S_PV3] = MG3(ME_GATES, ME_VAULT, ME_POP), [S_CMP3] = MG1(ME_SORT),
    [S_ROUND10] = MG2(ME_JUMP, ME_GATES), [S_ADDSUB] = MG3(ME_VAULT, ME_GATES, ME_KART),
    [S_MUL10] = MG3(ME_KART, ME_GATES, ME_POP), [S_PVBIG] = MG2(ME_GATES, ME_POP),
    [S_CMPBIG] = MG2(ME_SORT, ME_VAULT), [S_ROUNDBIG] = MG2(ME_JUMP, ME_GATES),
    [S_ADDSUBBIG] = MG2(ME_VAULT, ME_KART), [S_MULTI] = MG2(ME_VAULT, ME_GATES),
    [S_DIVIDE] = MG2(ME_VAULT, ME_POP),
    [S_UNITFRAC] = MG2(ME_POP, ME_CATCH), [S_FRACLINE] = MG2(ME_JUMP, ME_LAUNCH), [S_EQUIV] = MG2(ME_ZAP, ME_POP),
    [S_WHOLES] = MG2(ME_CATCH, ME_POP), [S_CMPFRAC] = MG1(ME_SORT), [S_PARTITION] = MG1(ME_BAKE),
    [S_EQUIVMUL] = MG2(ME_ZAP, ME_CATCH), [S_CMPDIFF] = MG1(ME_SORT), [S_ADDFRAC] = MG2(ME_POP, ME_CATCH),
    [S_MIXED] = MG2(ME_POP, ME_ZAP), [S_FRACWHOLE] = MG2(ME_CATCH, ME_POP),
    [S_TENTHS] = MG2(ME_CATCH, ME_POP), [S_DECIMAL] = MG2(ME_POP, ME_CATCH), [S_CMPDEC] = MG1(ME_SORT),
    [S_TIME5] = MG2(ME_CLOCK, ME_POP), [S_TIME1] = MG2(ME_CLOCK, ME_CATCH),
    [S_ELAPSED] = MG2(ME_CLOCK, ME_POP), [S_MASSVOL] = MG3(ME_SORT, ME_POP, ME_CATCH),
    [S_CONVERT] = MG3(ME_KART, ME_GATES, ME_POP), [S_MEASWORD] = MG2(ME_KART, ME_POP),
    [S_COINS] = MG2(ME_TILL, ME_POP), [S_CHANGE] = MG2(ME_TILL, ME_POP),
    [S_SHOPWORD] = MG2(ME_POP, ME_CATCH), [S_MONEYDEC] = MG2(ME_POP, ME_CATCH),
    [S_AREACOUNT] = MG2(ME_POP, ME_CATCH), [S_AREAMUL] = MG3(ME_BUILD, ME_POP, ME_ZAP),
    [S_AREASPLIT] = MG2(ME_ZAP, ME_POP), [S_AREAL] = MG2(ME_POP, ME_CATCH), [S_PERIM] = MG2(ME_BUILD, ME_POP),
    [S_SHAPES] = MG1(ME_SORT), [S_FORMULA] = MG2(ME_BUILD, ME_POP),
    [S_ANGLES] = MG3(ME_LAUNCH, ME_SORT, ME_POP), [S_PROTRACTOR] = MG2(ME_LAUNCH, ME_POP),
    [S_ANGLEADD] = MG2(ME_LAUNCH, ME_POP), [S_LINES] = MG1(ME_SORT), [S_CLASSIFY] = MG2(ME_SORT, ME_POP),
    [S_SYMMETRY] = MG2(ME_SORT, ME_POP),
    [S_GRAPH1] = MG2(ME_POP, ME_CATCH), [S_SCALED] = MG2(ME_POP, ME_GATES), [S_RULER] = MG2(ME_POP, ME_CATCH), [S_LINEPLOT] = MG2(ME_POP, ME_CATCH),
    [S_TABLE] = MG2(ME_POP, ME_GATES),
    [S_MULBIG] = MG2(ME_VAULT, ME_KART), [S_FRUNLIKE] = MG2(ME_POP, ME_ZAP),
    [S_DEC1000] = MG3(ME_SORT, ME_POP, ME_CATCH), [S_VOLUME] = MG2(ME_POP, ME_CATCH), [S_COORD] = MG2(ME_DIG, ME_POP),
    [S_POW10] = MG3(ME_KART, ME_POP, ME_VAULT), [S_ROUNDDEC] = MG2(ME_CATCH, ME_POP), [S_DECOPS] = MG3(ME_VAULT, ME_POP, ME_GATES),
    [S_DIV2] = MG2(ME_VAULT, ME_GATES), [S_ORDEROPS] = MG3(ME_GATES, ME_POP, ME_VAULT), [S_EXPR] = MG2(ME_SORT, ME_ZAP),
    [S_TWOPAT] = MG2(ME_CATCH, ME_KART), [S_FRACWORD] = MG2(ME_POP, ME_CATCH), [S_FRACDIV] = MG2(ME_POP, ME_ZAP),
    [S_FRACMUL] = MG2(ME_CATCH, ME_POP), [S_SCALING] = MG1(ME_SORT), [S_UNITDIV] = MG3(ME_POP, ME_ZAP, ME_CATCH),
    [S_CONVERT5] = MG3(ME_KART, ME_GATES, ME_POP), [S_LINEPLOT5] = MG2(ME_POP, ME_CATCH), [S_SHAPES5] = MG1(ME_SORT),
};

/* the games of a skill, the favourite first */
int mq_skill_games(int skill, int out[4])
{
    uint16_t v = mq_skill_mech[skill >= 0 && skill < MQ_NSKILL ? skill : 0];
    int n = 0;
    for (; n < 4 && (v & 15); v >>= 4)
        out[n++] = (v & 15) - 1;
    return n;
}

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

/* ------------------------------------------------------------------ twists (modifiers) */
/* Now and then a round comes with a twist, announced with a banner: night mode (the place at night,
 * the moon and fireflies; the answers glow), giant bubbles, gravity flip (the field upside down:
 * bubbles sink, stars rise to the cloud at the top), mirror world (Gate Dash runs to the left).
 * The finale is a remix: every order its own twist. Text stays readable; touch and the arrows
 * follow the mirror. */
#define TWIST_AFTER 3 /* rounds of a game played normally before it may come with a twist */
static const char mod_name[MOD_COUNT][16] = {"", "Night mode!", "Giant bubbles!", "Gravity flip!", "Mirror world!",
                                             "Finale remix!"};
const char *mq_mod_name(int m) { return mod_name[m >= 0 && m < MOD_COUNT ? m : 0]; }
bool mq_mod_fits(int m, int mech)
{
    switch (m) {
    case MOD_NIGHT: return true;
    case MOD_GIANT: return mech == ME_POP;
    case MOD_FLIP: return mech == ME_POP || mech == ME_CATCH;
    case MOD_MIRROR: return mech == ME_POP || mech == ME_GATES || mech == ME_KART || mech == ME_ZAP || mech == ME_CATCH;
    default: return false;
    }
}
/* the twist of the order being played (0: none) */
int mq_mod_now(const mq_t *q)
{
    const mq_game_t *g = &q->gm;
    int m = g->mod == MOD_REMIX ? g->mod_order : g->mod;
    return mq_mod_fits(m, g->mech) ? m : 0;
}
static int iabs(int a) { return a < 0 ? -a : a; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static bool picky(int m) { return m == ME_POP || m == ME_CATCH || m == ME_GATES || m == ME_KART || m == ME_ZAP; }
static int nlanes(int m) { return m == ME_GATES || m == ME_KART || m == ME_CATCH ? 3 : 4; } /* (Cloud Catch: three stars, uncluttered) */
/* the games that ask her to make (type) the answer now and then (mq_game_order): none of the action games any
 * more -- typed answers belong to the calm games (the notebook, the till, the Heist: backlog (b)); a moving or
 * rhythm game always offers labelled choices */
static bool makes(int m) { (void)m; return false; }
static bool lanes(int m) { return m == ME_GATES || m == ME_KART; }

/* ------------------------------------------------------------------ which game */
/* the build variants (Build It) */
enum { BM_RUG = 1, BM_FENCE, BM_BED, BM_SIDE_AREA, BM_SIDE_PERIM };
static int build_mode(const mq_prob_t *p)
{
    if (p->kind != K_NUM)
        return 0;
    int w = p->v[0], h = p->v[1];
    switch (p->skill) {
    case S_SHARE: case S_DIVFACT: case S_UNKNOWN:
        return p->vis == V_ARRAY || p->vis == V_GROUPS ? (p->ans == h && w >= 2 && w <= 8 && h <= 12 ? BM_BED : 0) : 0;
    case S_AREACOUNT: case S_AREAMUL: case S_FORMULA: case S_PERIM:
        /* (an order for the area or the perimeter is not retold as "find the width": the audit's B1; Build
         * It takes the orders with a side missing) */
        if (w < 1 || h < 1 || p->ans == w * h || p->ans == 2 * (w + h))
            return 0;
        if (p->ans == h && h <= 8 && w <= 14)
            return p->skill == S_PERIM ? BM_SIDE_PERIM : BM_SIDE_AREA;
        return 0;
    default: return 0;
    }
}

/* can game m show the picture a question needs (the card of the picking games, Sort Slide's card), or
 * does it tell the order its own way (the clock to set, the coins to count, the map, the line, the pie,
 * the plot to build)? The Heist, the kitchen and the pet shop draw no picture: such orders go elsewhere */
static bool draws_picture(int m, const mq_prob_t *p)
{
    switch (m) {
    case ME_POP: case ME_CATCH: case ME_GATES: case ME_KART: case ME_ZAP: case ME_SORT:
        return mq_skill[p->skill].visual_may_reveal != VR_NO;
    case ME_VAULT: case ME_COOK: case ME_PETS: return false;
    case ME_TILL: return p->skill == S_COINS || p->vis != V_COINS;
    default: return true;
    }
}
bool mq_mech_fits(int m, const mq_prob_t *p)
{
    int k = p->kind;
    if (mq_vis_needed(p) && !draws_picture(m, p))
        return false;
    switch (m) {
    case ME_POP: case ME_CATCH: case ME_ZAP:
        return k != K_SHADE && k != K_LINE && (k != K_CHOICE || p->nchoice >= 2);
    case ME_GATES: case ME_KART:
        return k != K_SHADE && k != K_LINE && (k != K_CHOICE || (p->nchoice >= 2 && p->nchoice <= 3));
    case ME_SORT: return k == K_CHOICE && p->nchoice >= 2;
    case ME_JUMP:
        return k == K_LINE || ((p->skill == S_ROUND10 || p->skill == S_ROUNDBIG) && k == K_NUM);
    case ME_LAUNCH:
        return k == K_LINE || (k == K_NUM && p->ans >= 5 && p->ans <= 180 &&
                               (p->skill == S_ANGLES || p->skill == S_PROTRACTOR || p->skill == S_ANGLEADD));
    case ME_BAKE: return k == K_SHADE;
    case ME_BUILD: return build_mode(p) != 0;
    case ME_CLOCK: return k == K_TIME;
    case ME_TILL: return k == K_MONEY;
    case ME_DIG: return k == K_COORD;
    case ME_VAULT: return k == K_NUM || k == K_REM || k == K_DEC || k == K_MONEY;
    case ME_COOK: return (k == K_NUM && p->ans >= 0 && p->ans < 10000) || k == K_FRAC;
    case ME_PETS: return k == K_MONEY;
    default: return false;
    }
}

/* the game for problem p in a round of game `round` (-1: its own) */
int mq_mech_for(const mq_prob_t *p, int round, int turn)
{
    if (round >= 0 && mq_mech_fits(round, p))
        return round;
    int n = 0, cand[4], all[4], na = mq_skill_games(p->skill, all);
    for (int i = 0; i < na; i++)
        if (mq_mech_fits(all[i], p))
            cand[n++] = all[i];
    if (n)
        return cand[(turn < 0 ? 0 : turn) % n];
    /* told in any game that fits */
    static const uint8_t order[] = {ME_POP, ME_SORT, ME_BAKE, ME_JUMP, ME_CLOCK, ME_TILL, ME_DIG, ME_VAULT};
    for (size_t i = 0; i < sizeof order; i++)
        if (mq_mech_fits(order[i], p))
            return order[i];
    return ME_POP;
}

/* ------------------------------------------------------------------ options */
static bool has_opt(const mq_game_t *g, int32_t v)
{
    for (int i = 0; i < g->nopt; i++)
        if (g->opt[i] == v)
            return true;
    return false;
}
static void add_opt(mq_game_t *g, const mq_prob_t *p, int32_t v, int n)
{
    if (g->nopt >= n || v < 0 || has_opt(g, v) || (g->nopt && mq_correct(p, v)))
        return;
    if (p->kind == K_FRAC && (v % 1000 == 0 || v / 1000 > 99))
        return;
    if (p->kind == K_TIME && (v % 100 > 59 || v / 100 < 1 || v / 100 > 12) && g->nopt)
        return;
    g->opt[g->nopt++] = v;
}

/* near misses of each kind */
static int32_t near_miss(const mq_prob_t *p, int k)
{
    int32_t a = p->ans;
    static const int8_t dn[8] = {1, -1, 2, 10, -10, -2, 3, 5};
    switch (p->kind) {
    case K_FRAC: {
        int n = a / 1000, d = a % 1000;
        static const int8_t fn[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {2, 0}, {1, 1}, {-2, 0}, {0, 2}};
        return (int32_t)(n + fn[k & 7][0]) * 1000 + (d + fn[k & 7][1]);
    }
    case K_DEC: {
        static const int16_t dd[8] = {100, -100, 10, -10, 200, 1, 50, -50};
        return a + dd[k & 7];
    }
    case K_MONEY: {
        static const int16_t dm[8] = {5, -5, 10, -10, 25, -25, 1, 100};
        return a + dm[k & 7];
    }
    case K_TIME: {
        int h = a / 100, m = a % 100;
        static const int8_t th[8][2] = {{0, 5}, {0, -5}, {1, 0}, {-1, 0}, {0, 10}, {0, -10}, {0, 1}, {1, 5}};
        m += th[k & 7][1], h += th[k & 7][0];
        if (m < 0)
            m += 60, h--;
        if (m > 59)
            m -= 60, h++;
        h = (h + 11) % 12 + 1;
        return h * 100 + m;
    }
    case K_REM: {
        static const int8_t dr[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {2, 0}, {1, 1}, {-1, 1}, {0, 2}};
        return a + dr[k & 7][0] * 1000 + dr[k & 7][1];
    }
    case K_COORD: {
        int x = a / 100, y = a % 100;
        static const int8_t dc[8][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 1}, {2, 0}, {0, 2}, {-1, 1}};
        return (x + dc[k & 7][0]) * 100 + (y + dc[k & 7][1]);
    }
    default: return a + dn[k & 7] * (a >= 100 && k >= 3 ? 10 : 1);
    }
}

static void format_opt(mq_t *q, const mq_prob_t *p, int32_t v, char *out, size_t n)
{
    if (p->kind == K_CHOICE) {
        q->api->snprintf(out, n, "%s", v >= 0 && v < p->nchoice ? p->choice[v] : "?");
        return;
    }
    mq_format(q, p, v, out, n);
}

/* the keys that type v (Bubble Pop, Cloud Catch, Gate Dash: typing an answer picks it) */
static void typed_form(mq_t *q, const mq_prob_t *p, int32_t v, char *out, size_t n)
{
    mq_prob_t t = *p;
    t.ans = v;
    mq_answer_text(q, &t, out, n);
    if (p->kind == K_DEC) { /* 0.450 -> 0.45 */
        int l = 0;
        while (out[l])
            l++;
        while (l > 1 && out[l - 1] == '0' && out[l - 2] != '.')
            out[--l] = 0;
    }
}

static void make_options(mq_t *q)
{
    mq_game_t *g = &q->gm;
    const mq_prob_t *p = &q->prob;
    int n = nlanes(g->mech);
    g->nopt = 0, g->gone = 0;
    if (p->kind == K_CHOICE) {
        g->opt[g->nopt++] = p->ans;
        for (int i = 0; i < p->nchoice && g->nopt < n; i++)
            if (i != p->ans)
                g->opt[g->nopt++] = i;
    } else {
        add_opt(g, p, p->ans, n);
        for (int i = 0; i < p->nwrong; i++)
            add_opt(g, p, p->wrong[i], n);
        for (int k = 0; k < 16 && g->nopt < n; k++)
            add_opt(g, p, near_miss(p, k), n);
    }
    for (int i = g->nopt - 1; i > 0; i--) { /* shuffle */
        int j = mq_rnd(q, 0, i);
        int32_t t = g->opt[i];
        g->opt[i] = g->opt[j], g->opt[j] = t;
    }
    if (g->mech == ME_SORT && p->kind == K_CHOICE) /* bins keep the choices' order (<, =, >) */
        for (int i = 0; i < g->nopt; i++)
            g->opt[i] = i;
    for (int i = 0; i < g->nopt; i++)
        format_opt(q, p, g->opt[i], g->lab[i], sizeof g->lab[i]);
}

int mq_game_right_opt(mq_t *q)
{
    for (int i = 0; i < q->gm.nopt; i++)
        if (mq_correct(&q->prob, q->gm.opt[i]) || (q->prob.kind == K_CHOICE && q->gm.opt[i] == q->prob.ans))
            return i;
    return -1;
}

/* ------------------------------------------------------------------ retelling a problem for a game */
/* the order told again for this game: the full story and what the friend says (no stage: the game
 * itself is the scene) */
static void retell(mq_t *q, const char *fmt, int n0, int n1, const char *cap)
{
    mq_prob_t *p = &q->prob;
    char a[160], b[200];
    q->api->snprintf(a, sizeof a, fmt, n0, n1);
    mq_story_expand(q, a, p->ca, p->cb, b, sizeof b);
    mq_story_wrap(q, b, p->text, MQ_STORY_W);
    p->stg = 0, p->cap[0] = 0;
    if (cap) {
        q->api->snprintf(a, sizeof a, cap, n0, n1);
        mq_story_cap(q, p, a);
    }
}

static void reset_wrong(mq_prob_t *p) { p->nwrong = 0; }
static void wrong_add(mq_prob_t *p, int32_t v, int mc)
{
    if (v < 0 || v == p->ans || p->nwrong >= 6)
        return;
    for (int i = 0; i < p->nwrong; i++)
        if (p->wrong[i] == v)
            return;
    p->wrong[p->nwrong] = v, p->wrong_mc[p->nwrong++] = (uint8_t)mc;
}

/* Some games ask the same thing the other way round: set the clock to a time that is told, build
 * the rug of an area, aim at a number of degrees, dig at a point, multiply facts become "find the
 * friend whose trays make 24". The skill and its bookkeeping stay; the answer is what is built. */
static void lock_make(mq_t *q);
static void lock_show(mq_t *q);
static void make_options(mq_t *q);
static void place_answers(mq_t *q);
static void hand_over_hand(mq_t *q);
static void adapt(mq_t *q)
{
    mq_game_t *g = &q->gm;
    mq_prob_t *p = &q->prob;
    if (!p->ca && !p->cb)
        p->ca = (uint8_t)(mq_skill[p->skill].world >= G_SKY ? 8 : mq_skill[p->skill].world % MQ_NCAST), p->cb = 0;
    if (p->cb == p->ca)
        p->cb = (uint8_t)((p->ca + 1) % MQ_NCAST);
    g->mode = 0;
    switch (g->mech) {
    case ME_CLOCK:
        if (p->skill == S_TIME5 || p->skill == S_TIME1) {
            static const char f[3][80] = {
                "{A}'s train leaves at %d:%02d. Set the tower clock so {A} isn't late!",
                "{B}'s party starts at %d:%02d. Set the clock for {A}!",
                "The puppet show is at %d:%02d. Set the clock for {A}, please!"};
            static const char c[3][28] = {"The train is at %d:%02d!", "Party at %d:%02d!", "The show is at %d:%02d!"};
            int k = mq_rnd(q, 0, 2);
            retell(q, f[k], p->ans / 100, p->ans % 100, c[k]);
            g->value = 1200; /* the hands start at 12:00 */
        } else
            g->value = (p->v[0] * 100 + p->v[1]) > 0 ? (p->v[0] ? p->v[0] : 12) * 100 + p->v[1] : 1200; /* (CR-15) */
        if (g->value % 100 > 59 || g->value / 100 < 1 || g->value / 100 > 12)
            g->value = 1200;
        g->mode = p->skill == S_TIME1 ? 1 : 5; /* minute steps */
        if (p->ans % 5)
            g->mode = 1;
        break;
    case ME_BUILD: {
        int bm = build_mode(p), w = p->v[0], h = p->v[1];
        g->mode = (uint8_t)bm;
        g->bw = 1, g->bh = 1, g->fixw = 0, g->fixh = 0, g->maxw = 14, g->maxh = 8;
        reset_wrong(p);
        if (bm == BM_BED) { /* plant n flowers in r equal rows: the row length */
            int r = w, c = h;
            g->fixh = (int16_t)r, g->bh = (int16_t)r, g->maxw = 12;
            retell(q, "{A} wants %d flowers in %d equal rows for {B}. Drag to plant them!", r * c, r,
                   "%d flowers, %d rows!");
            p->ans = c;
            wrong_add(p, r * c - r, MC_WRONG_OP), wrong_add(p, c + 1, MC_NEIGHBOR), wrong_add(p, c - 1, MC_NEIGHBOR);
            p->mc_default = MC_NEIGHBOR;
        } else if (bm == BM_RUG) { /* a rug of area w*h, h tall: the width */
            g->fixh = (int16_t)h, g->bh = (int16_t)h;
            retell(q, "{A}'s floor needs a rug of %d squares, %d tall. Drag the corner to make it!", w * h, h,
                   "A rug of %d squares, %d tall!");
            p->ans = w;
            if ((w * h) % 2 == 0 && (w * h) / 2 - h > 0)
                wrong_add(p, (w * h) / 2 - h, MC_AREA_PERIM);
            wrong_add(p, w * h - h, MC_WRONG_OP), wrong_add(p, w + 1, MC_CLOSE), wrong_add(p, w - 1, MC_CLOSE);
            p->mc_default = MC_AREA_PERIM;
        } else if (bm == BM_FENCE) { /* a fence of perimeter 2(w+h), h deep: the width */
            g->fixh = (int16_t)h, g->bh = (int16_t)h;
            retell(q, "The bunny hops out! {A} has %d m of fence for a pen %d m deep. Drag to build it!",
                   2 * (w + h), h, "%d m of fence, %d m deep!");
            p->ans = w;
            if ((2 * (w + h)) % h == 0)
                wrong_add(p, 2 * (w + h) / h, MC_AREA_PERIM);
            wrong_add(p, 2 * (w + h) - h, MC_PERIM_TWO), wrong_add(p, w + 1, MC_CLOSE), wrong_add(p, w - 1, MC_CLOSE);
            p->mc_default = MC_PERIM_TWO;
        } else { /* the other side of a rectangle from its area or perimeter, one side given */
            g->fixw = (int16_t)w, g->bw = (int16_t)w;
            if (bm == BM_SIDE_PERIM) {
                retell(q, "{A}'s frame has %d cm all around, %d cm wide. Drag to make the frame!", 2 * (w + h), w,
                       "%d cm around, %d cm wide!");
                wrong_add(p, 2 * (w + h) - w, MC_PERIM_TWO);
                p->mc_default = MC_PERIM_TWO;
            } else {
                retell(q, "{A} wants a rug of %d squares, %d wide. Drag to make it!", w * h, w,
                       "A rug of %d squares, %d wide!");
                wrong_add(p, w * h - w, MC_WRONG_OP);
                if ((w * h) % 2 == 0 && (w * h) / 2 - w > 0) /* the area taken as the way round */
                    wrong_add(p, (w * h) / 2 - w, MC_AREA_PERIM);
                p->mc_default = MC_AREA_PERIM;
            }
            wrong_add(p, h + 1, MC_CLOSE), wrong_add(p, h - 1, MC_CLOSE);
            p->ans = h;
        }
        g->value = g->fixh ? g->bw : g->bh;
        break;
    }
    case ME_LAUNCH:
        if (p->kind == K_NUM) { /* aim by degrees */
            g->mode = p->skill == S_PROTRACTOR ? 2 : 1; /* 2: read the protractor (no read-out) */
            if (p->skill == S_PROTRACTOR) {
                static const char f[2][80] = {"{A} wants the star to fly at %d degrees. Aim with the protractor!",
                                                 "Fling a cupcake to {B} at %d degrees!"};
                retell(q, f[mq_rnd(q, 0, 1)], p->ans, 0, "Fling it at %d degrees!");
                wrong_add(p, 180 - p->ans, MC_PROTRACTOR);
            }
            g->aim = 0;
        } else
            g->mode = 3, g->aim = 0; /* fling by fractions: K_LINE ticks */
        break;
    case ME_VAULT:
        lock_make(q);
        break;
    case ME_DIG: {
        int x = p->ans / 100, y = p->ans % 100;
        retell(q, "{A}'s map says: the treasure is at (%d, %d). Dig it up!", x, y, "Treasure at (%d, %d)!");
        g->px = 0, g->py = 0;
        break;
    }
    case ME_TILL: case ME_PETS:
        g->mode = p->skill == S_COINS ? 1 : 0; /* 1: count the coins and ring them up */
        if (p->skill == S_CHANGE && q->run_i % 2 == 1)
            g->mode = 2; /* 2: work out the change and type it on the till's keys */
        for (int i = 0; i < 5; i++)
            g->tray[i] = 0;
        if (p->skill == S_COINS)
            retell(q, "{A} pays with these coins for {a} {tB}. Ring up the total on the till!", 0, 0,
                   "Ring up {A}'s coins!");
        break;
    case ME_ZAP:
        if (p->skill == S_FACTS && p->kind == K_NUM && p->a[0] < 169) { /* "find 24": the friend with the trays */
            int a, b;
            bool dv;
            mq_fact_decode(p->a[0], &a, &b, &dv);
            if (!dv && a > 1 && b > 1) { /* (x0 and x1 are rules, not trays) */
                char s[16];
                p->nchoice = 0;
                q->api->snprintf(s, sizeof s, "%d \xC3\x97 %d", a, b);
                q->api->snprintf(p->choice[p->nchoice], 16, "%s", s), p->choice_mc[p->nchoice++] = MC_NONE;
                int cand[6][2] = {{a + 1, b}, {a, b + 1}, {a > 1 ? a - 1 : a + 2, b}, {a, b > 1 ? b - 1 : b + 2},
                                  {a + b, 1}, {a + 1, b + 1}};
                for (int i = 0; i < 6 && p->nchoice < 4; i++) {
                    int x = cand[i][0], y = cand[i][1];
                    if (x * y == a * b || x > 12 || y > 12)
                        continue;
                    q->api->snprintf(s, sizeof s, "%d \xC3\x97 %d", x, y);
                    bool dup = false;
                    for (int k = 0; k < p->nchoice; k++)
                        dup |= !q->api->strcmp(p->choice[k], s);
                    if (dup)
                        continue;
                    q->api->snprintf(p->choice[p->nchoice], 16, "%s", s);
                    p->choice_mc[p->nchoice++] = i == 4 ? MC_ADD_NOT_MUL : MC_NEIGHBOR;
                }
                p->kind = K_CHOICE, p->ans = 0;
                retell(q, "{A} needs %d {FA} on trays. Bop the friend with the right trays!", a * b, 0,
                       "%d {FA}, please!");
                g->mode = 1;
            }
        }
        break;
    case ME_JUMP:
        if (p->kind == K_NUM) { /* rounding: hop to the nearer end */
            int32_t n = p->skill == S_ROUND10 ? p->a[0] : p->big, unit = p->skill == S_ROUND10 ? p->a[1] : 1;
            if (p->skill == S_ROUNDBIG)
                for (int k = 0; k < p->v[0]; k++)
                    unit *= 10;
            if (unit <= 0)
                unit = 10;
            int32_t lo = n / unit * unit;
            g->mode = 1;
            g->opt[0] = lo, g->opt[1] = lo + unit, g->value = n;
            g->aim = -1;
            /* the method, not the nearer-looking side: the number is on its card, never on the line; the line shows
             * the two round numbers and the middle between them -- or she finds the two first -- or only the
             * numbers are left; the line shows the number's real spot after (the user) */
            g->rlevel = (uint8_t)mq_round_level(q, p->skill), g->rstep = g->rlevel == 1 ? 0 : 1, g->rwob = 0;
            g->rright = (uint8_t)mq_rnd(q, 0, (int)(lo / unit < 2 ? lo / unit : 2)), g->rsel = 1; /* (never a pair below 0) */
        } else
            g->aim = 0;
        break;
    default: break;
    }
    if (p->skill == S_FACTS && p->kind == K_NUM && p->a[0] >= 0 && !(g->mech == ME_ZAP && g->mode == 1)) {
        int a, b;
        bool dv;
        mq_fact_decode(p->a[0], &a, &b, &dv);
        if (a <= 1 || b <= 1) /* x0 and x1 are rules, not stories ("0 plates with 1 strawberries"): the sum itself */
            return;
        static const char f[2][2][72] = {
            {"{A} needs %d \xC3\x97 %d {FA} for the party. How many {FA} is that?", "{A} stacks %d \xC3\x97 %d boxes of {TA}. How many boxes?"},
            {"{A} shares %d {FA} into %d equal bags. How many {FA} in each bag?", "%d stickers, %d friends: how many stickers each?"}};
        static const char c[2][2][28] = {{"Treats for the party!", "Boxes to stack!"},
                                         {"Fair bags for all!", "Fair stickers for all!"}};
        int k = mq_rnd(q, 0, 1);
        if (dv)
            retell(q, f[1][k], a * b, b, c[1][k]);
        else
            retell(q, f[0][k], a, b, c[0][k]);
        /* told on the stage: a groups of b (the answer: all of them), or a * b shared by b */
        p->stg = (uint8_t)((dv ? ST_SHARE : ST_GROUPS) + 1);
        p->stg_c = (uint8_t)(dv ? (k ? SP_PAGE : SP_BAG) : (k ? SP_BOX : SP_TRAY));
        p->stg_i = (uint8_t)(dv ? (k ? SI_STAR : SI_COOKIE) : (k ? SI_CARD : SI_COOKIE));
        p->stg_n[0] = dv ? a * b : a, p->stg_n[1] = b, p->stg_n[2] = 0;
        if (a * b == 0 || (dv && b < 1))
            p->stg = 0, p->cap[0] = 0; /* nothing to show for a zero: the (short) story says it */
    }
}

/* Hop Along's rounding help for her now: the line with both round numbers and the middle while the skill is new (or
 * an error pattern of rounding is being practised), then she finds the two round numbers herself, then the numbers
 * only (docs/MATHQUEST.md "Rounding: the method") */
int mq_round_level(mq_t *q, int skill)
{
    if (q->round_force)
        return q->round_force - 1;
    for (int pt = PT_ROUND_FAR; pt <= PT_ROUND_HALF_DOWN; pt++) {
        int st = mq_diag_rec(q, pt)->state;
        if (st == PS_SPOTTED || st == PS_PRACTICE)
            return 0;
    }
    const mq_skrec_t *r = &mq_prof(q)->sk[skill < MQ_NSKILL ? skill : 0];
    return r->mastery >= 75 ? 2 : r->mastery >= 40 || (r->tries >= 6 && r->right * 10 >= r->tries * 8) ? 1 : 0;
}
/* state mq_round (tests, the bot): "LEVEL STEP RIGHTPAIR NUMBER LO HI" in Hop Along's rounding, else "" */
const char *mq_round_state(const mq_t *q, char *b, size_t n)
{
    const mq_game_t *g = &q->gm;
    if (q->scene != SC_PLAY || g->mech != ME_JUMP || g->mode != 1)
        b[0] = 0;
    else
        q->api->snprintf(b, n, "%d %d %d %ld %ld %ld", g->rlevel, g->rstep, g->rright, (long)g->value, (long)g->opt[0], (long)g->opt[1]);
    return b;
}
/* the k-th pair offered in the first step: its lower round number */
static int32_t round_pair(const mq_game_t *g, int k) { return g->opt[0] + (int32_t)(k - g->rright) * (g->opt[1] - g->opt[0]); }
/* the first step's pick: the two round numbers it sits between */
static void round_pick(mq_t *q, int k)
{
    mq_game_t *g = &q->gm;
    if (k == g->rright) {
        g->rstep = 1, g->aim = -1, g->moves++, g->hud_ok = 0;
        mq_sfx(q, MODX_SFX_POP, 2);
        mq_spark_burst(q, 160, FY + 74, 8);
    } else { /* a gentle wobble and a hint, nothing counted */
        g->rwob = (uint8_t)(k + 1), g->rwob_t = mq_mark(q), g->hud_ok = 0;
        mq_sfx(q, MODX_SFX_DUD, 1);
    }
}

/* ------------------------------------------------------------------ objects */
static mq_gobj_t *spawn(mq_t *q, int kind, int x, int y, int vx, int vy, int opt)
{
    mq_game_t *g = &q->gm;
    for (int i = 0; i < GM_NOBJ; i++)
        if (!g->o[i].kind) {
            g->o[i] = (mq_gobj_t){(uint8_t)kind, (uint8_t)opt, 1, (uint8_t)mq_rnd(q, 0, 4), Q4(x), Q4(y),
                                  (int16_t)vx, (int16_t)vy, (uint16_t)(q->anim_t / 10), 0};
            return &g->o[i];
        }
    return NULL;
}
static void clear_kind(mq_t *q, int kind)
{
    for (int i = 0; i < GM_NOBJ; i++)
        if (q->gm.o[i].kind == kind || (kind < 0 && q->gm.o[i].kind))
            q->gm.o[i].kind = 0;
}
static mq_gobj_t *ans_obj(mq_t *q, int opt)
{
    for (int i = 0; i < GM_NOBJ; i++)
        if (q->gm.o[i].kind == GO_ANS && q->gm.o[i].opt == opt)
            return &q->gm.o[i];
    return NULL;
}

/* a bubble bursts: a ring and droplets fly out */
static void burst(mq_t *q, int x, int y, uint16_t c)
{
    mq_game_t *g = &q->gm;
    int k = 0;
    for (int i = 1; i < 4; i++)
        if (g->burst_t[i] < g->burst_t[k])
            k = i;
    g->burst_x[k] = (int16_t)x, g->burst_y[k] = (int16_t)y, g->burst_t[k] = mq_mark(q), g->burst_c[k] = c;
}
static const uint16_t bub_col[6] = {Q_PINK, Q_SKY, Q_MINT, Q_LAV, Q_PEACH, Q_YELLOW};

static void pop_up(mq_t *q, int x, int y, const char *s)
{
    mq_game_t *g = &q->gm;
    int k = 0;
    for (int i = 1; i < 4; i++)
        if (g->pop_t[i] < g->pop_t[k])
            k = i;
    g->pop_x[k] = (int16_t)x, g->pop_y[k] = (int16_t)y, g->pop_t[k] = q->anim_t ? q->anim_t : 1;
    q->api->snprintf(g->pop_s[k], sizeof g->pop_s[k], "%s", s);
}

/* a model card is shown where reading the picture is the point (clocks, coins, graphs, shapes) */
static bool shows_card(const mq_t *q)
{
    const mq_prob_t *p = &q->prob;
    int m = q->gm.mech;
    if (p->vq) /* a "just the numbers" rung: no picture with the question */
        return false;
    if (m == ME_CLOCK || m == ME_BUILD || m == ME_BAKE || m == ME_JUMP || m == ME_LAUNCH || m == ME_DIG || m == ME_SORT)
        return false;
    if (m == ME_TILL || m == ME_PETS || m == ME_COOK)
        return false;
    return p->vis != V_NONE && mq_skill[p->skill].visual_may_reveal != VR_NO;
}
static int field_x0(const mq_t *q) { return shows_card(q) ? 120 : 4; }
/* Gate Dash / Kart Race: where she waits in her lane (right of the card when there is one) */
static int lane_x0(const mq_t *q) { return field_x0(q) + 42; }
#define MK_LX 236                                          /* a made gate / boost pad waits here */
#define MK_HOLE 4                                          /* Peekaboo's made friend: the bottom middle hole */
#define MK_ZX(q) (field_x0(q) + (316 - field_x0(q)) / 2)   /* (hole_xy of MK_HOLE) */
#define MK_ZY (FY + 108)

/* ------------------------------------------------------------------ the order board's words */
int mq_board_text(mq_t *q, const mq_prob_t *p, char *out, int n, char l[3][MQ_TXT])
{
    int k = 0;
    out[0] = 0;
    for (int i = 0; i < 3; i++)
        if (p->text[i][0])
            k += q->api->snprintf(out + k, (size_t)(n - k), k ? " %s" : "%s", p->text[i]);
    bool qm = false;
    for (const char *c = out; *c; c++)
        qm |= *c == '?';
    if (p->skill == S_FACTS && p->kind == K_NUM && !qm && k < n - 5) /* a plain fact: its unknown marked ("6 x 7 = ?") */
        q->api->snprintf(out + k, (size_t)(n - k), " = ?");
    int lines = mq_story_wrap(q, out, l, 262);
    bool num = false; /* (a tag with numbers, or a question, only repeats the order: "4 bags of 3 kg!") */
    for (const char *c = p->cap; *c; c++)
        num |= (*c >= '0' && *c <= '9') || *c == '?';
    if (p->cap[0] && !num && lines <= 3) { /* the scene's tag, as an extra: when it costs no line */
        char t[3 * MQ_TXT + 48], l2[3][MQ_TXT];
        int tl = q->api->snprintf(t, sizeof t, "%s %s", p->cap, out), m = mq_story_wrap(q, t, l2, 262);
        if (m <= lines && tl < n) {
            q->api->snprintf(out, (size_t)n, "%s", t);
            for (int i = 0; i < 3; i++)
                q->api->snprintf(l[i], MQ_TXT, "%s", l2[i]);
        }
    }
    return lines;
}

/* ------------------------------------------------------------------ an order arrives */
static void place_answers(mq_t *q)
{
    mq_game_t *g = &q->gm;
    int x0 = field_x0(q), w = 316 - x0;
    clear_kind(q, -1); /* (the extras between orders go too: only the answers while an order waits) */
    switch (g->mech) {
    case ME_POP:
        for (int i = 0; i < g->nopt; i++)
            spawn(q, GO_ANS, x0 + w * (2 * i + 1) / (2 * g->nopt), FB - 24 - i * 30, 0, -(10 + mq_rnd(q, 0, 4)), i);
        break;
    case ME_CATCH: /* side by side, never overlapping, all coming down together (a gentle fall) */
        for (int i = 0; i < g->nopt; i++)
            spawn(q, GO_ANS, x0 + w * (2 * i + 1) / (2 * g->nopt), FY + 22 - (i % 2) * 8, 0, 6 + mq_rnd(q, 0, 2), i);
        break;
    case ME_GATES: case ME_KART:
        for (int i = 0; i < g->nopt; i++)
            spawn(q, GO_ANS, 350, FY + 25 + i * 49, -56, 0, i);
        break;
    case ME_ZAP: {
        int used = 0;
        for (int i = 0; i < g->nopt; i++) {
            int h;
            do
                h = mq_rnd(q, 0, 5);
            while ((used >> h) & 1);
            used |= 1 << h;
            mq_gobj_t *o = spawn(q, GO_ANS, 0, 0, 0, 0, i);
            if (o)
                o->hp = (uint8_t)h, o->col = (uint8_t)mq_rnd(q, 0, MQ_NCAST - 1);
        }
        break;
    }
    default: break;
    }
}

/* ------------------------------------------------------------------ pacing */
/* Cloud Catch's hover line: the stars wait here until she moves the cloud */
#define CATCH_HOVER (FY + 62)
/* the top band, where an answer's number is under the order board: Bubble Pop's bubbles rise through
 * it, and Cloud Catch's stars come down through it (from the top, after a miss), at least at the base
 * speed -- however slow the order's pace, nothing waits there half-hidden */
#define POP_BAND (FY + 12)
#define CATCH_BAND (FY + 22)
/* an answer's step this frame: v (Q4 a frame) x num / 512, the remainder carried, so a slow pace (a
 * hover, the controller's floor) still moves it -- it used to round to nothing and stand still */
static int ans_step(mq_gobj_t *o, int v, int num)
{
    int32_t a = (int32_t)o->fr + (int32_t)v * num;
    int d = (int)(a / 512);
    o->fr = (int16_t)(a - (int32_t)d * 512);
    return d;
}
/* the time (ms at the base speed) before the first answer would escape her (Bubble Pop: float off the
 * top; Cloud Catch: reach the cloud), from where they are now; 0: nothing escapes (the gates wait) */
/* the answers whose number is hidden under the order board now (Bubble Pop, Cloud Catch; tests) */
int mq_game_hidden(const mq_t *q)
{
    int n = 0;
    for (int i = 0; i < GM_NOBJ; i++)
        if (q->gm.o[i].kind == GO_ANS && (q->gm.mech == ME_POP || q->gm.mech == ME_CATCH) && q->gm.o[i].y < Q4(POP_BAND) &&
            q->gm.o[i].y > Q4(FY - 18))
            n++;
    return n;
}
int mq_game_escape_ms(const mq_t *q)
{
    const mq_game_t *g = &q->gm;
    int best = 0;
    for (int i = 0; i < GM_NOBJ; i++) {
        const mq_gobj_t *o = &g->o[i];
        if (o->kind != GO_ANS || !o->vy)
            continue;
        /* (Bubble Pop: from the top band on it rises at least briskly: counted to the band) */
        int d = g->mech == ME_POP ? o->y - Q4(POP_BAND) : g->mech == ME_CATCH ? Q4(FB - 26) - o->y : 0;
        int v = o->vy < 0 ? -o->vy : o->vy; /* x16 px a frame at the base speed (sp 2: vy a frame) */
        if (d <= 0 || v <= 0)
            continue;
        int ms = d / v * MQ_FRAME_MS;
        if (!best || ms < best)
            best = ms;
    }
    return best;
}
/* the order's speed: her level for this game, capped by the think time (heavy: a hover) */
static void pace_order(mq_t *q)
{
    mq_game_t *g = &q->gm;
    g->esc_ms = (uint16_t)imin(60000, mq_game_escape_ms(q));
    g->spd = (uint16_t)mq_speed_now(q, g->mech, &q->prob, g->esc_ms);
    g->px0 = g->px;
}
/* the speed this frame: hesitation (nothing done for 5 s) hovers the answers; once she has gone for
 * one (Cloud Catch: moved the cloud) the thinking is done and it comes to her briskly; Q8 */
static int pace_now(const mq_t *q)
{
    const mq_game_t *g = &q->gm;
    int s = g->spd ? g->spd : 256;
    if (g->mech == ME_CATCH && g->px != g->px0)
        return imax(s, 512);
    if (q->sub == GS_PLAY && q->now - q->last_input > 5000u && q->now - q->prob_start > 5000u)
        s = imin(s, 16);
    return s;
}
/* a heavy order (multi-digit, several steps) told in a moving game goes to a calm game of its own
 * when it has one (the till, the heist, building, cooking); else it stays, hovering (mq_speed_now) */
int mq_route_mech(mq_t *q, const mq_prob_t *p, int mech)
{
    if (!mq_mech_moving(mech) || !mq_heavy(q, p))
        return mech;
    int g[4], n = mq_skill_games(p->skill, g);
    for (int i = 0; i < n; i++)
        if (mq_mech_calm(g[i]) && mq_mech_fits(g[i], p))
            return g[i];
    if (mq_mech_fits(ME_VAULT, p) && (p->kind == K_NUM || p->kind == K_REM || p->kind == K_MONEY))
        return ME_VAULT;
    return mech;
}

/* ------------------------------------------------------------------ Kart Race: a real race */
/* Three rivals with a little driving sense: each cruises a touch faster than the base speed but
 * keeps within reach (gentle rubber bands both ways), never passes her while she is thinking,
 * and drops back when far ahead. Right answers are boosts (bigger on a combo), a dud a little
 * spin. Overtakes pop up; the place at the finish goes on the podium of the round's end. */
#define KART_V 40 /* the base speed, x16 px a frame */
static void kart_start(mq_t *q)
{
    mq_game_t *g = &q->gm;
    int me = mq_prof(q)->buddy % MQ_NCAST, n = 0;
    g->race_on = 1, g->race_d = 0, g->place_shown = KART_RIVALS + 1, g->podium = 0;
    for (int c = 1; c < MQ_NCAST && n < KART_RIVALS; c++) {
        int slot = (me + c * 2) % MQ_NCAST;
        if (slot == me)
            continue;
        g->rv_slot[n] = (uint8_t)slot, g->rv_lane[n] = (uint8_t)(n % 3);
        g->rv_d[n] = (int32_t)(40 + n * 45) * 16; /* she starts at the back */
        g->rv_skill[n] = (uint8_t)(2 + n);
        n++;
    }
    g->place = (uint8_t)mq_kart_place(q);
}
int mq_kart_place(const mq_t *q)
{
    int p = 1;
    for (int i = 0; i < KART_RIVALS; i++)
        p += q->gm.rv_d[i] > q->gm.race_d;
    return p;
}
static void kart_frame(mq_t *q)
{
    mq_game_t *g = &q->gm;
    if (!g->race_on)
        kart_start(q);
    int boost = q->anim_t - g->boost_t < 1200 && g->boost_t ? 56 + (q->combo >= 3 ? 24 : 0) : 0;
    int me = KART_V + boost - (q->anim_t - g->spin_t < 700 && g->spin_t ? 26 : 0);
    g->race_d += me;
    bool thinking = q->sub == GS_PLAY;
    for (int i = 0; i < KART_RIVALS; i++) {
        int32_t gap = g->rv_d[i] - g->race_d; /* > 0: ahead of her */
        int v = KART_V + g->rv_skill[i] + mq_isin((int)((q->anim_t / 20 + (uint32_t)i * 120) % 360)) / 512;
        if (gap > (int32_t)(70 + 40 * i) * 16)
            v = KART_V - 8; /* far ahead: eases off */
        else if (gap < -140 * 16)
            v = KART_V + 14; /* far behind: catches up, to be in the race */
        if (thinking && gap < 0 && gap > -36 * 16 && v > me)
            v = me; /* no passing her while she thinks */
        g->rv_d[i] += v;
        if (mq_rnd(q, 0, 199) == 0) /* a lane change now and then */
            g->rv_lane[i] = (uint8_t)((g->rv_lane[i] + (mq_rnd(q, 0, 1) ? 1 : 2)) % 3);
    }
    int pl = mq_kart_place(q);
    if (pl < g->place_shown && g->place_shown <= KART_RIVALS + 1 && q->anim_t - g->order_t > 200) { /* an overtake */
        g->pass_t = mq_mark(q);
        q->api->snprintf(g->big_s, sizeof g->big_s, pl == 1 ? "1st place!" : "Overtake!");
        g->big_t = mq_mark(q);
        mq_sfx(q, MODX_SFX_BOOST, pl);
        mq_spark_burst(q, g->px + 20, FY + 25 + g->lane * 49, 10);
    }
    g->place_shown = (uint8_t)pl, g->place = (uint8_t)pl;
}
/* the round is over: the place at the finish line goes on the podium */
void mq_kart_finish(mq_t *q)
{
    mq_game_t *g = &q->gm;
    g->podium = g->race_on && g->round_mech == ME_KART ? (uint8_t)mq_kart_place(q) : 0;
}

/* the podium: her place and the rivals' (the round's end of a Kart Race) */
void mq_kart_draw_podium(mq_t *q, int x, int y)
{
    mq_game_t *g = &q->gm;
    const modx_api_t *api = q->api;
    int who[4], np = 0; /* drivers by place: -1 her, else a rival */
    int order[4] = {-1, 0, 1, 2};
    for (int i = 0; i < 4; i++) /* sort by distance, she at her place */
        for (int j = i + 1; j < 4; j++) {
            int32_t di = order[i] < 0 ? g->race_d : g->rv_d[order[i]], dj = order[j] < 0 ? g->race_d : g->rv_d[order[j]];
            if (dj > di) {
                int t = order[i];
                order[i] = order[j], order[j] = t;
            }
        }
    for (int i = 0; i < 4; i++)
        who[np++] = order[i];
    static const int8_t col[3] = {1, 0, 2};  /* 2nd, 1st, 3rd from the left */
    static const int8_t hgt[3] = {22, 34, 14};
    static const char lab[3][2] = {"2", "1", "3"};
    for (int c = 0; c < 3; c++) {
        int place = col[c], bx = x + c * 30, h = hgt[c], top = y + 50 - h;
        mq_fill(q, bx, top, 28, h, place == 0 ? Q_YELLOW : place == 1 ? Q_GREY : Q_PEACH);
        mq_text_c(q, api->font_medium, bx + 14, top + 2, lab[c], Q_INK);
        int d = who[place];
        int bob = (q->anim_t / 200 + (uint32_t)c) % 2;
        if (d < 0) {
            q->buddy_anim = place == 0 ? AN_DANCE : AN_HAPPY;
            mq_buddy_draw(q, mq_prof(q)->buddy % MQ_NCAST, bx + 4, top - 21 - (place == 0 ? bob * 2 : 0), 1, 1);
            q->buddy_anim = 0;
        } else
            mq_sprite(q, g->rv_slot[d] % MQ_NCAST, bx + 4, top - 21, 1, 0);
    }
    (void)np;
}

/* the round's game: the skill's games in turn (each visit another one) */
/* the games a friend's story opened join the rounds of the stops they suit: Cake Kitchen for the
 * recipes and making things (times, fractions of a recipe), Pet Shop for prices and change */
static bool cook_skill(int s)
{
    return s == S_GROUPS || s == S_MDWORD || s == S_TIMES_AS || s == S_MUL10 || s == S_MULTI || s == S_FRACWHOLE ||
           s == S_MULBIG || s == S_FRACMUL || s == S_FRACWORD || s == S_TWOSTEP || s == S_SHARE;
}
static bool pets_skill(int s) { return s == S_COINS || s == S_CHANGE || s == S_SHOPWORD || s == S_MONEYDEC; }
int mq_skill_round_games(int skill, int out[6])
{
    int n = mq_skill_games(skill, out);
    /* Cake Kitchen and Pet Shop are open from the start (the friends' stories still celebrate them) */
    if (cook_skill(skill))
        out[n++] = ME_COOK;
    if (pets_skill(skill))
        out[n++] = ME_PETS;
    return n;
}
/* A world card is a promise: "Shop Till" plays the till, "Pet Shop" the pet shop. The game plays a skill
 * when it is one of the skill's games (its own, the friends' games); a pick game (the bubbles, the clouds,
 * the gates, the karts, Star Zap) tells any skill one of the pick games tells. */
bool mq_game_plays(int mech, int skill)
{
    if (mech < 0 || mech >= ME_COUNT || skill < 0 || skill >= MQ_NSKILL)
        return false;
    int c[6], n = mq_skill_round_games(skill, c);
    for (int i = 0; i < n; i++)
        if (c[i] == mech)
            return true;
    if (picky(mech))
        for (int i = 0; i < n; i++)
            if (picky(c[i]))
                return true;
    return false;
}
/* typed answers (backlog (b)): only the calm games take a typed answer -- the Heist's wheels, the till's keys,
 * the notebook; the action and rhythm games always offer labelled choices */
static bool typed_game(int m) { return m == ME_VAULT || m == ME_TILL || m == ME_PETS; }
bool mq_order_typed(mq_t *q)
{
    const mq_game_t *g = &q->gm;
    if (q->scene != SC_PLAY)
        return false;
    return mq_cols_on(q) || g->mech == ME_VAULT || ((g->mech == ME_TILL || g->mech == ME_PETS) && g->mode >= 1);
}
int mq_round_mech(mq_t *q, int skill)
{
    int cand[6], n = mq_skill_round_games(skill, cand);
    for (int i = 0; i < n; i++) /* the game she picked on the world screen */
        if (q->want_mech1 && cand[i] == q->want_mech1 - 1)
            return cand[i];
    if (!n)
        return ME_POP;
    int k0 = mq_prof(q)->sk[skill].plays % n, e = mq_pace(q)->typed_ema;
    /* the share of typed orders kept about 15-20 %: past it, the skill's next game that is picked; under it, its
     * next calm typed one (the Heist, the till) */
    bool settled = mq_pace(q)->orders_n >= 20; /* (her first orders: the games as they come) */
    bool more = settled && e < MQ_TYPED_LO, less = settled && e > MQ_TYPED_HI;
    for (int i = 0; i < n && (more || less); i++) {
        int m = cand[(k0 + i) % n];
        if (more ? typed_game(m) : !typed_game(m))
            return m;
    }
    return cand[k0];
}
int mq_world_games(int w, int mech[3], int stop[3])
{
    int cnt[ME_COUNT] = {0}, first[ME_COUNT], at[ME_COUNT];
    for (int m = 0; m < ME_COUNT; m++)
        first[m] = 1000, at[m] = -1;
    int nl = mq_world_levels(w);
    for (int l = 0; l < nl; l++) {
        int c[6], n = mq_skill_round_games(mq_levels[w][l] - 1, c);
        for (int i = 0; i < n; i++) {
            cnt[c[i]] += i == 0 ? 3 : 1; /* (a stop's own first game counts most) */
            if (first[c[i]] > l * 8 + i)
                first[c[i]] = l * 8 + i, at[c[i]] = l;
        }
    }
    /* the place's own games first (the Bakery's pies and cakes, the Clock Tower's clocks): a game scores
     * by how much of it is played here against everywhere, then by how much here */
    int all[ME_COUNT] = {0}, sc[ME_COUNT];
    for (int v = 0; v < MQ_NWORLD; v++)
        for (int l = 0; l < mq_world_levels(v); l++) {
            int c[6], n = mq_skill_round_games(mq_levels[v][l] - 1, c);
            for (int i = 0; i < n; i++)
                all[c[i]] += i == 0 ? 3 : 1;
        }
    for (int m = 0; m < ME_COUNT; m++)
        sc[m] = cnt[m] ? cnt[m] * 256 / all[m] + cnt[m] * 4 : 0;
    int k = 0;
    while (k < 3) {
        int best = -1;
        for (int m = 0; m < ME_COUNT; m++)
            if (cnt[m] && (best < 0 || sc[m] > sc[best] || (sc[m] == sc[best] && first[m] < first[best])))
                best = m;
        if (best < 0)
            break;
        mech[k] = best, stop[k] = at[best], k++;
        cnt[best] = 0;
    }
    return k;
}

bool mq_round_plays(mq_t *q, int skill)
{
    const mq_game_t *g = &q->gm;
    if (!g->chosen || g->round_mech >= ME_COUNT)
        return true;
    return mq_game_plays(g->round_mech, skill) && !((g->ban[(skill & 127) / 8] >> (skill % 8)) & 1);
}

/* the round starts (mq_main.c start_level) */
void mq_game_round(mq_t *q, int skill, bool boss)
{
    mq_game_t *g = &q->gm;
    uint8_t *b = (uint8_t *)g;
    for (size_t i = 0; i < sizeof *g; i++)
        b[i] = 0;
    g->boss = boss;
    g->t0 = mq_mark(q);
    g->round_mech = boss ? 0xFF : (uint8_t)mq_round_mech(q, skill);
    g->chosen = !boss && q->want_mech1 && g->round_mech == q->want_mech1 - 1;
    g->grump = g->grump_max = boss ? (uint8_t)q->run_n : 0;
    g->best_round = g->round_mech < ME_COUNT ? *mq_best_of(q, g->round_mech) : 0;

    g->px = 160, g->lane = 1, g->py = (int16_t)(FY + 74);
    if (boss)
        g->mod = MOD_REMIX;
    else if (!q->preview && mq_prof(q)->used && mq_prof(q)->problems >= 12 && !mq_pace(q)->twists_off &&
             g->round_mech < 16 && mq_pace(q)->mech_rounds[g->round_mech] >= TWIST_AFTER) {
        /* every third round or so: a twist -- never in a game she has not played a few times normally,
         * never when the grown-ups switched them off */
        mq_extra_t *e = mq_extra(q);
        if (++e->mod_n >= 3 || mq_events_night(q)) {
            int cand[4], n = 0;
            for (int m = MOD_NIGHT; m <= MOD_MIRROR; m++)
                if (mq_mod_fits(m, g->round_mech))
                    cand[n++] = m;
            g->mod = (uint8_t)(mq_events_night(q) ? MOD_NIGHT : cand[mq_rnd(q, 0, n - 1)]);
            e->mod_n = 0;
        }
    }
    if (g->round_mech < 16 && mq_pace(q)->mech_rounds[g->round_mech] < 255)
        mq_pace(q)->mech_rounds[g->round_mech]++, q->dirty = true;
    if (g->mod && g->mod < MOD_REMIX && !((mq_pace(q)->twists_seen >> g->mod) & 1)) { /* a new twist: its line, once */
        static const char intro[MOD_REMIX][56] = {"", "Tonight it's dark: the answers glow!", "Whoa, giant bubbles today!",
                                                  "Gravity flipped: everything floats up!", "Mirror world: left is right!"};
        mq_speak(q, mq_skill[skill].world >= G_SKY ? 8 : mq_skill[skill].world % MQ_NCAST, intro[g->mod], MODX_SPEAK_NOW);
        mq_pace(q)->twists_seen |= (uint8_t)(1 << g->mod), q->dirty = true;
    }
    mq_sfx(q, boss ? MODX_SFX_BOSS : MODX_SFX_WHOOSH, 0);
    mq_music(q, boss ? MODX_MUS_BOSS : MODX_MUS_PLAY, 1);
}

/* a new order: q->prob is fresh (mq_main.c new_problem) */
/* a big idea she meets for the first time (or the helper came twice for it): the teaching moment first;
 * column work she is still learning: the friend's notebook opens */
static void teach_or_notebook(mq_t *q)
{
    if (mq_strat_auto(q)) /* a "use this way" order: its model (mq_strat.c) */
        return;
    if (mq_teach_due(q))
        mq_teach_start(q, q->prob.idea, (mq_pace(q)->idea_offered[q->prob.idea % IDEA_COUNT] & 0x80) ? 1 : 0);
    else if (mq_cols_auto(q))
        mq_cols_open(q);
}

void mq_game_order(mq_t *q)
{
    mq_game_t *g = &q->gm;
    if (!g->t0) /* no round yet (the console's `mq skill`) */
        mq_game_round(q, q->prob.skill, false);
    g->mech = (uint8_t)mq_mech_for(&q->prob, g->round_mech == 0xFF ? -1 : g->round_mech, g->orders + q->run_i);
    g->mech = (uint8_t)mq_route_mech(q, &q->prob, g->mech);
    mq_wg_reset(q); /* (a new order: nothing picked on the last one is dropped on this one, CR2-07) */
    mq_cols_close(q);
    q->teach.on = 0;
    q->strat.on = 0;
    g->typed[0] = 0, g->typed_n = 0, g->sel = 0, g->moves = 0, g->fly = 0, g->mood = 0, g->say[0] = 0;
    g->given = -1;
    adapt(q);
    if (picky(g->mech) || g->mech == ME_SORT || g->mech == ME_COOK) /* (the kitchen: labelled amounts, on the beat) */
        make_options(q);
    else
        g->nopt = 0;
    if (g->mech == ME_SORT)
        g->fx = Q4(160), g->fy = Q4(FY - 30), g->sel = (uint8_t)(g->nopt / 2);
    if (g->mech == ME_GATES || g->mech == ME_KART)
        g->lane = 1, g->px = (int16_t)lane_x0(q);
    if (g->mech == ME_CATCH)
        g->px = 210;
    g->order_t = q->anim_t;
    if (g->mech == ME_COOK && q->music_now != MODX_MUS_RHYTHM) { /* the kitchen cooks to a beat (its clock) */
        mq_music(q, MODX_MUS_RHYTHM, 0);
        g->beat0 = q->anim_t;
    }
    if (g->mod == MOD_REMIX) { /* the finale's remix: a twist for this order (or none) */
        int cand[5], n = 0;
        for (int m = MOD_NONE; m <= MOD_MIRROR; m++)
            if (m == MOD_NONE || mq_mod_fits(m, g->mech))
                cand[n++] = m;
        g->mod_order = (uint8_t)cand[mq_rnd(q, 0, n - 1)];
    }
    /* Now and then she makes the answer instead of picking it: Bubble Pop's bubble wand (type the
     * number, blow its bubble), Cloud Catch's wishing star, Gate Dash's gate with a blank sign (make
     * its number, dash through), Kart Race's boost pad (type the boost code, zoom) and Peekaboo's
     * friend peeking out of a hole (write the number on their sign, they pop out). From her seventh
     * order on, every other order of a run she makes it; if nothing is typed for a while (or after
     * a dud) the answers to pick come in after all. */
    {
        const mq_prob_t *p = &q->prob;
        int k = p->kind;
        g->make = makes(g->mech) && mq_prof(q)->problems >= 6 && !g->boss &&
                  (k == K_NUM || k == K_DEC || k == K_MONEY || k == K_FRAC) && q->run_i % 2 == 1;
        g->blowing = 0, g->make_t = q->anim_t;
    }
    if (g->make)
        clear_kind(q, GO_ANS);
    else
        place_answers(q);
    g->escapes = 0;
    pace_order(q);
    if (mq_story_sad(&q->prob))
        g->mood = 1;
    g->hud_ok = 0;
    q->sub = GS_PLAY;
    mq_sfx(q, MODX_SFX_WHOOSH, g->mech);
    /* the story is acted out first (a tap skips it); the game waits. Its pace adapts: the full
     * scene the first time she sees a story type, quicker the next two times, then a flourish */
    g->scene_on = q->prob.stg && !q->scenes_off && !q->preview;
    g->scene_t = q->anim_t;
    g->helped = 0, g->hoh = 0, g->bulb_t = 0, g->demo_t = 0;
    {   /* the order said aloud: what the board says, the whole order */
        char all[3 * MQ_TXT + 48], l[3][MQ_TXT];
        mq_board_text(q, &q->prob, all, (int)sizeof all, l);
        mq_speak(q, q->prob.ca % MQ_NCAST, all, MODX_SPEAK_NOW);
    }
    if (!g->scene_on)
        mq_guide_order(q);
    if (!g->scene_on)
        teach_or_notebook(q);
    if (g->scene_on) {
        mq_more_t *mo = mq_more(q);
        int st = (q->prob.stg - 1) % MQ_NACTED;
        g->scene_ms = (uint16_t)mq_scene_ms(mo->acted[st]);
        if (mo->acted[st] < 255)
            mo->acted[st]++;
        mq_sfx(q, MODX_SFX_VOICE, (q->prob.ca % MQ_NCAST) * 16 + 3);
    }
}

int mq_scene_ms(int acted) { return acted <= 0 ? MQ_SCENE_FULL : acted <= 2 ? MQ_SCENE_SHORT : MQ_SCENE_QUICK; }
/* the time line of the full scene, played in ms: the same scene quicker, or (the flourish) from
 * the moment the friend is in place (420 ms: the walk-on skipped) */
int mq_scene_el(int el, int ms)
{
    if (ms >= MQ_SCENE_FULL || ms <= 0)
        return el;
    if (ms <= MQ_SCENE_QUICK)
        return 420 + el * (MQ_SCENE_FULL - 420) / ms;
    return el * MQ_SCENE_FULL / ms;
}
/* the acted story is over (or skipped): the game begins, the clock on the answer starts now */
static void scene_end(mq_t *q)
{
    mq_game_t *g = &q->gm;
    if (!g->scene_on)
        return;
    g->scene_on = 0;
    g->order_t = q->anim_t, g->make_t = q->anim_t;
    q->prob_start = q->now;
    if (q->run_i == 0 || q->anim_t - g->t0 < 3000) /* the game's goal banner after the first scene */
        g->t0 = mq_mark(q);
    g->hud_ok = 0;
    mq_sfx(q, MODX_SFX_WHOOSH, g->mech);
    mq_guide_order(q); /* the first time in this game: the demo */
    teach_or_notebook(q);
}

/* ------------------------------------------------------------------ reading time (mq_read.c) */
/* the big words ("Super x3!", "Overtake!") and the flourishes ("yay!") stay for their reading time */
static uint32_t big_ms(mq_t *q) { return (uint32_t)mq_read_ms(q, q->gm.big_s); }
static uint32_t pop_ms(mq_t *q, int i) { return (uint32_t)mq_read_ms(q, q->gm.pop_s[i]); }
/* the cheer stays for her friend's line (after the combo's big words, if they came), never shorter than the
 * game's own beat (Bubble Pop: a brisk flow when there is nothing to read) */
uint32_t mq_cheer_ms(mq_t *q)
{
    mq_game_t *g = &q->gm;
    uint32_t beat = g->mech == ME_POP ? 1000u : 1500u;
    if (!g->say[0])
        return beat;
    uint32_t need = (uint32_t)mq_read_ms(q, g->say) + (g->big_t && g->big_t >= g->cheer_t ? big_ms(q) : 0u);
    return need > beat ? need : beat;
}
/* the why after the placement's second miss: "It's 24" and "Next one!" */
static int why_read_ms(mq_t *q)
{
    char a[24];
    mq_format(q, &q->prob, q->prob.ans, a, sizeof a);
    return mq_read_ms_len(q, mq_read_chars(a) + 15);
}
/* a step of the trick: its lines */
static int trick_read_ms(mq_t *q)
{
    char out[3][MQ_TXT];
    mq_prob_t vis;
    mq_steps(q, &q->ex, q->ex_step, out, &vis);
    return mq_read_ms_len(q, mq_read_chars(out[0]) + mq_read_chars(out[1]) + mq_read_chars(out[2]));
}

/* ------------------------------------------------------------------ answering */
static void cheer(mq_t *q)
{
    mq_game_t *g = &q->gm;
    g->cheer_t = q->anim_t;
    g->mood = 2;
    g->orders++;
    int add = 1 + (q->combo >= 5 ? 2 : q->combo >= 3 ? 1 : 0);
    bool perfect = g->mech == ME_COOK && g->mix_t && (int32_t)(q->anim_t - g->mix_t) < 600 && g->mix_j == 2;
    add += perfect; /* mixed right on the beat: a star more */
    if (q->mashed) /* (mashed: nothing) */
        add = 0;
    g->stars = (uint16_t)(g->stars + add);
    if (g->mech == ME_COOK && add) /* (mashed: not a cake for the quests either -- the kitchen paid a quest's coins) */
        mq_count(q, CT_COOKED, 1);
    if (g->mech == ME_PETS && add)
        mq_count(q, CT_PETS, 1);
    static const char yay[4][6] = {"yay!", "yes!", "wow!", "ooh!"}; /* a flourish, never points */
    if (add)
        pop_up(q, 160, FY + 40, yay[(add - 1) & 3]);
    mq_story_fix(q, &q->prob, g->say, sizeof g->say);
    mq_voice(q, q->prob.ca % MQ_NCAST, g->say);
    mq_spark_burst(q, 160, FY + 50, q->combo >= 3 ? 22 : 14);
    if (q->combo >= 5)
        g->shake_t = q->anim_t;
    if (q->combo >= 2) { /* the combo: a big pop-text */
        static const char cw[5][10] = {"Nice", "Super", "Wow", "Amazing", "Sparkly"};
        q->api->snprintf(g->big_s, sizeof g->big_s, "%s x%d!", cw[q->combo >= 6 ? 4 : q->combo - 2], q->combo);
        g->big_t = mq_mark(q);
    }
    if (perfect) {
        q->api->snprintf(g->big_s, sizeof g->big_s, "Perfect mix!");
        g->big_t = mq_mark(q);
    }
    if (!q->mashed && mq_run_halfway(q)) { /* a long round's halfway point: the band says so, the tickets shine */
        q->api->snprintf(g->big_s, sizeof g->big_s, "Halfway! Yay!");
        g->big_t = mq_mark(q), g->half_t = mq_mark(q);
        mq_spark_burst(q, 140, MQ_Y0 + 8, 12);
    }
    if (g->boss && g->grump)
        g->grump--;
    if (q->combo && q->combo % 4 == 0 && !g->power[1]) /* a power-up to keep */
        g->power[g->power[0] ? 1 : 0] = (uint8_t)(q->combo % 8 == 0 ? PW_WAND : PW_SLOW);
    if (g->mech == ME_KART)
        g->boost_t = q->anim_t;
    if (g->make) /* the made answer stays on its sign (the gate, the pad, the friend's) */
        mq_format(q, &q->prob, q->prob.ans, g->blow_s, sizeof g->blow_s);
    if (g->mech == ME_VAULT) /* the wheels show the combination that opened it */
        lock_show(q);
    if (g->combo_peak < q->combo)
        g->combo_peak = q->combo;
    q->sub = GS_CHEER;
    mq_sfx(q, MODX_SFX_CHEER, q->combo);
    if (q->combo >= 2)
        mq_sfx(q, MODX_SFX_COMBO, q->combo);
    if (!g->boss && g->round_mech != ME_COOK) /* the music grows with the combo */
        mq_music(q, MODX_MUS_PLAY, q->combo >= 5 ? 3 : q->combo >= 2 ? 2 : 1);
    mq_quest_event(q, QE_HELP, 1);
    mq_quest_event(q, QE_STARS, add);
}

/* the player's game action gave answer v */
void mq_game_answer(mq_t *q, int32_t v)
{
    mq_game_t *g = &q->gm;
    if (q->scene != SC_PLAY || q->sub != GS_PLAY)
        return;
    g->given = v;
    /* a sort with two bins, or the last two options: a second try would be no thinking at all */
    bool retry = !(g->mech == ME_SORT && g->nopt <= 2) && !(g->mech == ME_JUMP && g->mode == 1); /* (two ends: the
                                                                                     * other would be no thinking) */
    if (picky(g->mech)) {
        int left = 0;
        for (int i = 0; i < g->nopt; i++)
            left += !((g->gone >> i) & 1);
        retry &= left > 2;
    }
    if (g->hoh) /* together: no second guess needed */
        retry = false;
    int r = mq_judge(q, v, retry);
    if (r > 0) {
        cheer(q);
        if (g->helped) { /* solved with a friend: teamwork */
            q->api->snprintf(g->big_s, sizeof g->big_s, "Teamwork!");
            g->big_t = mq_mark(q);
            mq_heart_add(q, g->help_slot % MQ_NCAST, 3);
            mq_pace_t *pc = mq_pace(q);
            if (pc->teamwork < 60000)
                pc->teamwork++;
            mq_speak(q, g->help_slot % MQ_NCAST, "Teamwork! We did it!", MODX_SPEAK_NOW);
            mq_spark_burst(q, 270, FY + 20, 14);
        }
        return;
    }
    if (q->last_mc < 64 && mq_pace(q)->mc_seen[q->last_mc] < 255)
        mq_pace(q)->mc_seen[q->last_mc]++;
    if (q->diag_hit && !g->hoh && !g->helped && !q->placing_item && !q->preview && !g->boss && mq_diag_due(q, q->diag_hit - 1)) {
        /* her answer shows an error pattern she has: the friend shows her where it went wrong (her answer, the
         * right one, the step replayed), then they finish the order together (mq_diag.c) */
        g->mood = 1, g->dud_t = q->anim_t;
        mq_sfx(q, MODX_SFX_DUD, 0);
        mq_diag_start(q, q->diag_hit - 1);
        return;
    }
    mq_story_oops(q, &q->prob, v, q->last_mc, g->say, sizeof g->say);
    if (g->mech == ME_JUMP && g->mode == 1 && g->opt[1] > g->opt[0]) { /* rounding: the method's question */
        int32_t lo = g->opt[0], hi = g->opt[1], mid = (lo + hi) / 2;
        if (g->value == mid)
            q->api->snprintf(g->say, sizeof g->say, "%ld is right on the middle of %ld and %ld. Which way does the middle go?",
                             (long)g->value, (long)lo, (long)hi);
        else
            q->api->snprintf(g->say, sizeof g->say, "%ld is between %ld and %ld. Is it past the middle, %ld?", (long)g->value,
                             (long)lo, (long)hi, (long)mid);
    }
    mq_voice(q, q->prob.ca % MQ_NCAST, g->say);
    g->mood = 1;
    if (g->mech == ME_KART)
        g->spin_t = mq_mark(q);
    g->dud_t = q->anim_t;
    g->shake_t = q->anim_t;
    for (int i = 0; i < g->nopt; i++)
        if (g->opt[i] == v)
            g->gone |= (uint8_t)(1 << i);
    mq_sfx(q, MODX_SFX_DUD, 0);
    if (!g->boss)
        mq_music(q, MODX_MUS_PLAY, 1);
    if (r == 0 && !g->helped && !q->placing_item && !q->preview && mq_engine_recent_misses(q, q->prob.skill, 6) >= 3) {
        mq_helper_start(q); /* the same skill slipped again and again lately: a friend helps at once */
        return;
    }
    if (r == 0) { /* try again: the game goes on, calmly (Cloud Catch's stars wait again until she moves) */
        g->typed[0] = 0, g->typed_n = 0;
        g->px0 = g->px;
        if (g->make) /* made one was not it: the bubbles float in to pick from */
            g->make = 0, place_answers(q);
        return;
    }
    /* the second miss: a friend comes to help (a walkthrough, then her own order again); missed after
     * help too: they do it together, so it always ends in a success. (The placement's orders just move
     * on: why, then the next strand.) */
    if (q->placing_item) {
        q->sub = GS_WHY;
        g->trick_t = q->anim_t;
        g->hud_ok = 0;
    } else if (!g->helped && !q->preview)
        mq_helper_start(q);
    else
        hand_over_hand(q);
}

/* Together: the duds fade and the friend points at the one (a pick game), or the game is set to the
 * answer and its go button glows (Build It, the clock, the pie, the line, the map, the lock); other
 * games are retold as a bubble to pop for this order. The round always ends on a success. */
static void hand_over_hand(mq_t *q)
{
    mq_game_t *g = &q->gm;
    const mq_prob_t *p = &q->prob;
    q->sub = GS_PLAY;
    g->hud_ok = 0;
    g->make = 0, g->blowing = 0, g->typed[0] = 0, g->typed_n = 0;
    g->hoh = 1;
    switch (g->mech) {
    case ME_BUILD:
        if (g->fixh)
            g->bw = (int16_t)p->ans;
        else
            g->bh = (int16_t)p->ans;
        g->hoh = 2;
        break;
    case ME_CLOCK: g->value = p->ans, g->hoh = 2; break;
    case ME_DIG: g->px = (int16_t)(p->ans / 100), g->py = (int16_t)(p->ans % 100), g->hoh = 2; break;
    case ME_BAKE: q->prob.v[5] = (int16_t)((1 << p->ans) - 1), g->hoh = 2; break;
    case ME_JUMP: g->aim = (int16_t)(g->mode == 1 ? (p->ans == g->opt[0] ? 0 : 1) : p->ans), g->hoh = 2, g->rstep = 1; break;
    case ME_LAUNCH: g->aim = (int16_t)p->ans, g->hoh = 2; break;
    case ME_VAULT: lock_show(q), g->hoh = 2; break;
    default:
        if (!picky(g->mech) && mq_mech_fits(ME_POP, p)) { /* told as a bubble, just for this order */
            g->mech = ME_POP;
            make_options(q);
            place_answers(q);
        }
        break;
    }
    if (g->nopt && picky(g->mech)) { /* only the right one is left to pick */
        int r = mq_game_right_opt(q);
        for (int i = 0; i < g->nopt; i++)
            if (i != r)
                g->gone |= (uint8_t)(1 << i);
        g->sel = (uint8_t)(r < 0 ? 0 : r);
        g->px0 = -1; /* (Cloud Catch: the stars come down at once) */
        g->spd = 256;
    }
    mq_speak(q, g->help_slot % MQ_NCAST, "Let's do it together!", MODX_SPEAK_NOW);
    q->prob_start = q->now, q->last_input = q->now;
}
void mq_game_together(mq_t *q) { hand_over_hand(q); }

/* a pick game's option i was chosen */
static void cook_mix(mq_t *q);
static void choose(mq_t *q, int i)
{
    mq_game_t *g = &q->gm;
    if (i < 0 || i >= g->nopt || ((g->gone >> i) & 1))
        return;
    if (g->mech == ME_COOK) /* (the kitchen: the amount goes in on the beat) */
        cook_mix(q);
    mq_gobj_t *o = ans_obj(q, i);
    if (o) {
        mq_spark_burst(q, o->x / 16, o->y / 16, 8);
        if (g->mech == ME_POP) {
            burst(q, o->x / 16, o->y / 16, bub_col[o->col % 6]);
            mq_quest_event(q, QE_POP, 1);
        }
    }
    mq_sfx(q, g->mech == ME_POP ? MODX_SFX_POP : MODX_SFX_TAP, 0);
    mq_game_answer(q, g->opt[i]);
}

/* a soft key or the "?" while the story is acted or a teaching moment is up: it is put away first, so the key does
 * what it says at once (the bot's 10: help?, show me and ? did nothing then) */
void mq_game_unblock(mq_t *q)
{
    if (q->scene != SC_PLAY || q->sub != GS_PLAY)
        return;
    scene_end(q);
    if (mq_teach_on(q))
        mq_teach_close(q);
    mq_strat_close(q);
    q->gm.hud_ok = 0, q->redraw = true;
}

/* the console and tests: answer right (or wrong) the way the game does it */
void mq_game_auto(mq_t *q, bool right)
{
    mq_game_t *g = &q->gm;
    const mq_prob_t *p = &q->prob;
    if (q->scene != SC_PLAY || q->sub != GS_PLAY)
        return;
    scene_end(q);
    if (mq_teach_on(q)) /* (the teaching moment put away first) */
        mq_teach_close(q);
    mq_strat_close(q);
    if (mq_diag_on(q))
        mq_diag_put_away(q);

    if (mq_cols_on(q)) { /* the notebook: every box filled right, or (wrong) put away and a wrong answer given */
        if (right) {
            for (int k = 0; k < CW_STEPS && mq_cols_on(q); k++)
                mq_cols_digit(q, q->cols.st[q->cols.cur].want);
            return;
        }
        mq_cols_close(q);
    }
    if (g->nopt) {
        int r = mq_game_right_opt(q);
        if (right) {
            if (g->mech == ME_SORT)
                g->sel = (uint8_t)r;
            choose(q, r);
        } else
            for (int i = 0; i < g->nopt; i++)
                if (i != r && !((g->gone >> i) & 1)) {
                    choose(q, i);
                    break;
                }
        return;
    }
    if (g->mech == ME_JUMP && g->mode == 1)
        g->rstep = 1; /* (the console's answer: the two round numbers found) */
    int32_t v = p->ans;
    if (!right) {
        v = p->nwrong ? p->wrong[0] : p->ans + 1;
        if (p->kind == K_TIME && (v % 100 > 59 || v / 100 > 12 || v / 100 < 1))
            v = near_miss(p, 0);
        if (g->mech == ME_JUMP && g->mode == 1)
            v = g->opt[0] == p->ans ? g->opt[1] : g->opt[0];
        if (g->mech == ME_BAKE)
            v = p->ans + 1 <= p->v[1] ? p->ans + 1 : p->ans - 1;
    }
    if (g->mech == ME_LAUNCH) { /* the star flies; it is judged where it lands */
        if (g->fly)
            return; /* (one star at a time) */
        g->aim = (int16_t)v;
        mq_game_fire(q);
        return;
    }
    if (g->mech == ME_CLOCK)
        g->value = v;
    else if (g->mech == ME_JUMP)
        g->aim = (int16_t)(g->mode == 1 ? (v == g->opt[0] ? 0 : 1) : v);
    else if (g->mech == ME_DIG)
        g->px = (int16_t)(v / 100), g->py = (int16_t)(v % 100);
    else if (g->mech == ME_BUILD) {
        if (g->fixh)
            g->bw = (int16_t)v;
        else
            g->bh = (int16_t)v;
    }
    mq_game_answer(q, v);
}

/* ------------------------------------------------------------------ one frame */
static int speed(const mq_t *q) { return q->anim_t < q->gm.slow_until ? 1 : 2; }

/* The game's little extras -- bubbles to pop, stars and coins to grab, friends peeking, power-ups -- come only
 * BETWEEN orders: while an order waits, the only things to tap are its answers and the game's controls (the
 * user: "rising bubbles you can click on that are not the maths are a distraction"). */
static void fun_stream(mq_t *q)
{
    mq_game_t *g = &q->gm;
    int x0 = field_x0(q);
    if (q->sub == GS_PLAY || mq_rnd(q, 0, 99) >= 8)
        return;
    switch (g->mech) {
    case ME_POP: {
        mq_gobj_t *o = spawn(q, GO_PLAIN, mq_rnd(q, x0 + 10, 306), FB + 12, 0, -(14 + mq_rnd(q, 0, 8)), 0);
        if (o && mq_rnd(q, 0, mq_events_bubbles(q) ? 3 : 11) == 0)
            o->hp = 2; /* now and then a golden bubble: a handful of stars (a surprise, never bought) */
        break;
    }
    case ME_CATCH: { /* between orders: stars (and a harmless rock); while an order waits, nothing */
        if (q->sub == GS_PLAY)
            break;
        int k = mq_rnd(q, 0, 9);
        spawn(q, k < 2 ? GO_ROCK : GO_STAR, mq_rnd(q, x0 + 10, 306), FY - 6, 0, 12 + mq_rnd(q, 0, 6), 0);
        break;
    }
    case ME_GATES: case ME_KART: spawn(q, GO_COIN, 340, FY + 25 + mq_rnd(q, 0, 2) * 49, -48, 0, 0); break;
    case ME_ZAP: {
        int h = mq_rnd(q, 0, 5);
        if (g->make && (h == MK_HOLE || h == 1)) /* (the made friend's hole, and the one above its sign) */
            return;
        for (int i = 0; i < GM_NOBJ; i++)
            if (g->o[i].kind && g->o[i].hp == h)
                return;
        mq_gobj_t *o = spawn(q, GO_PAL, 0, 0, 0, 0, 0);
        if (o)
            o->hp = (uint8_t)h, o->col = (uint8_t)mq_rnd(q, 0, MQ_NCAST - 1);
        break;
    }
    default: break;
    }
    if (g->orders >= 1 && !g->power[1] && mq_rnd(q, 0, 99) < 4 &&
        (g->mech == ME_POP || g->mech == ME_CATCH)) /* now and then a power-up floats by (between orders) */
        spawn(q, GO_POWER, mq_rnd(q, x0 + 10, 300), g->mech == ME_POP ? FB + 12 : FY - 6, 0,
              g->mech == ME_POP ? -12 : 12, mq_rnd(q, 1, 2));
}

/* a right answer escaped her (she was not too slow: the game was too fast): slower at once, for this
 * order and the next */
static void escaped(mq_t *q)
{
    mq_game_t *g = &q->gm;
    if (g->escapes < 255)
        g->escapes++;
    mq_speed_event(q, g->mech, SE_MISS);
    g->spd = (uint16_t)imax(8, g->spd * 8 / 10);
}

static void grab_fun(mq_t *q, mq_gobj_t *o)
{
    mq_game_t *g = &q->gm;
    if (o->kind == GO_ROCK) { /* a bump: a wobble, nothing lost */
        g->shake_t = q->anim_t;
        mq_sfx(q, MODX_SFX_DUD, 1);
    } else if (o->kind == GO_POWER) {
        g->power[g->power[0] ? 1 : 0] = o->opt ? o->opt : PW_SLOW;
        pop_up(q, o->x / 16, o->y / 16, o->opt == PW_WAND ? "wand!" : "slow!");
        mq_sfx(q, MODX_SFX_UNLOCK, 0);
    } else if (o->kind == GO_PLAIN && o->hp == 2) { /* a golden bubble */
        int k = 2 + mq_rnd(q, 0, 3);
        if (q->guess_n) /* (while she mashes: nothing earned) */
            k = 0;
        g->stars = (uint16_t)(g->stars + k);
        if (k)
            pop_up(q, o->x / 16, o->y / 16, "wow!");
        mq_spark_burst(q, o->x / 16, o->y / 16, 12);
        burst(q, o->x / 16, o->y / 16, Q_YELLOW);
        q->api->snprintf(g->big_s, sizeof g->big_s, "Golden!");
        g->big_t = mq_mark(q);
        mq_sfx(q, MODX_SFX_UNLOCK, k);
        mq_quest_event(q, QE_POP, 1);
        mq_quest_event(q, QE_STARS, k);
    } else {
        if (!q->guess_n) /* (while she mashes: nothing earned) */
            g->stars++, pop_up(q, o->x / 16, o->y / 16, "yay!");
        mq_spark_burst(q, o->x / 16, o->y / 16, 4);
        if (o->kind == GO_PLAIN)
            burst(q, o->x / 16, o->y / 16, bub_col[o->col % 6]);
        mq_sfx(q, o->kind == GO_COIN ? MODX_SFX_COIN : MODX_SFX_POP, 0);
        if (o->kind == GO_PLAIN)
            mq_quest_event(q, QE_POP, 1);
        else
            mq_quest_event(q, QE_CATCH, 1);
    }
    o->kind = 0;
}

void mq_game_frame(mq_t *q)
{
    mq_game_t *g = &q->gm;
    if (q->scene != SC_PLAY)
        return;
    if (g->scene_on) { /* the story being acted: the game waits */
        mq_fun_time(q, MQ_FRAME_MS, true);
        if (q->anim_t - g->scene_t >= (g->scene_ms ? g->scene_ms : MQ_SCENE_FULL))
            scene_end(q);
        return;
    }
    if (mq_diag_on(q)) { /* the friend shows where her answer went wrong: the game waits */
        mq_diag_frame(q);
        mq_fun_time(q, MQ_FRAME_MS, true);
        return;
    }
    if (mq_teach_on(q)) { /* the teaching moment: the game waits */
        mq_teach_frame(q);
        mq_fun_time(q, MQ_FRAME_MS, true);
        return;
    }
    if (mq_strat_on(q) && q->sub == GS_PLAY) { /* a way to work it, hands on: the game waits */
        mq_strat_frame(q);
        mq_fun_time(q, MQ_FRAME_MS, true);
        return;
    }
    if (mq_cols_on(q)) { /* the notebook: the game waits */
        mq_fun_time(q, MQ_FRAME_MS, true);
        return;
    }
    mq_guide_frame(q);
    mq_helper_frame(q);
    if (mq_bulb_frame(q)) /* the lightbulb's scene: the game waits */
        return;
    int sp = speed(q), pace = pace_now(q);
    int x0 = field_x0(q);
    if (q->sub != GS_CHEER) /* (the cheer is her reading time: it neither earns nor spends the fun budget) */
        mq_fun_time(q, MQ_FRAME_MS, q->sub == GS_PLAY);
    if (q->sub == GS_PLAY || q->sub == GS_CHEER)
        fun_stream(q);
    uint16_t now10 = (uint16_t)(q->anim_t / 10);
    for (int i = 0; i < GM_NOBJ; i++) {
        mq_gobj_t *o = &g->o[i];
        if (!o->kind)
            continue;
        if (o->kind == GO_ANS && q->sub != GS_PLAY && g->mech != ME_ZAP) {
            o->y = (int16_t)(o->y + o->vy * sp / 4); /* the answers drift on slowly while the friend talks */
            if (g->mech == ME_POP && o->y < Q4(POP_BAND)) /* and come round, as in play (never lost off the top) */
                o->y = Q4(FB + 18), o->fr = 0;
            else if (g->mech == ME_CATCH && o->y > Q4(FB + 10))
                o->y = Q4(POP_BAND), o->fr = 0;
            continue;
        }
        switch (g->mech) {
        case ME_POP:
            if (o->kind == GO_ANS) /* (in the top band: briskly on, so it comes round) */
                o->y = (int16_t)(o->y + ans_step(o, o->vy, sp * (o->y < Q4(POP_BAND) ? imax(pace, 256) : pace)));
            else
                o->y = (int16_t)(o->y + o->vy * sp / 2);
            o->x = (int16_t)(o->x + mq_isin((int)((q->anim_t / 6 + (uint32_t)i * 47) % 360)) / 160);
            if (o->y < Q4(o->kind == GO_ANS ? POP_BAND : FY - 18)) { /* (an answer never goes on under the order board,
                                                                       * its number hidden: it comes round at once) */
                if (o->kind == GO_ANS) { /* the answers come round again: nothing is ever lost */
                    o->y = Q4(FB + 18), o->fr = 0, o->x = (int16_t)clampi(o->x, Q4(x0 + 18), Q4(300));
                    if (q->sub == GS_PLAY && mq_correct(&q->prob, g->opt[o->opt % 4]) && !mq_heavy(q, &q->prob))
                        escaped(q); /* the right one floated off before she got to it: slower */
                } else
                    o->kind = 0;
            }
            break;
        case ME_CATCH:
            if (o->kind == GO_ANS) /* (coming down from the top: briskly into view) */
                o->y = (int16_t)(o->y + ans_step(o, o->vy, sp * (o->y < Q4(CATCH_BAND) ? imax(pace, 256) : pace)));
            else
                o->y = (int16_t)(o->y + o->vy * sp / 2);
            /* the answer stars wait mid-air until she moves the cloud (and again after a dud): only a
             * star she went for lands on her cloud */
            if (o->kind == GO_ANS && q->sub == GS_PLAY && g->px == g->px0 && o->y > Q4(CATCH_HOVER))
                o->y = Q4(CATCH_HOVER);
            if (o->y / 16 > FB - 26 && iabs(o->x / 16 - g->px) < 26) {
                if (o->kind == GO_ANS) {
                    if (q->sub == GS_PLAY)
                        choose(q, o->opt);
                } else
                    grab_fun(q, o);
            } else if (o->y > Q4(FB + 10)) {
                if (o->kind == GO_ANS) { /* missed: it floats back up to the top, nothing is lost (in view) */
                    o->y = Q4(POP_BAND), o->fr = 0;
                    if (q->sub == GS_PLAY && mq_correct(&q->prob, g->opt[o->opt % 4]))
                        escaped(q);
                } else
                    o->kind = 0;
            }
            break;
        case ME_GATES: case ME_KART: {
            int stop = o->kind == GO_ANS ? 236 : -30; /* the gates wait for her */
            if (o->kind == GO_ANS && g->fly)
                stop = -60;
            o->x = (int16_t)(o->x + o->vx * sp / 2 * (g->mech == ME_KART && q->anim_t - g->boost_t < 800 ? 2 : 1) *
                                        1); /* (the gates and pads wait for her: nothing escapes) */
            if (o->x < Q4(stop) && o->kind == GO_ANS)
                o->x = Q4(stop);
            if (o->kind == GO_COIN && iabs(o->x / 16 - g->px) < 14 && iabs(o->y / 16 - (FY + 25 + g->lane * 49)) < 16)
                grab_fun(q, o);
            else if (o->x < Q4(-20))
                o->kind = 0;
            break;
        }
        case ME_ZAP:
            if (o->kind == GO_PAL && (uint16_t)(now10 - o->t) > 140)
                o->kind = 0;
            break;
        default: break;
        }
        if (o->kind == GO_POWER && (g->mech == ME_POP || g->mech == ME_CATCH)) {
            if (g->mech == ME_CATCH && o->y / 16 > FB - 26 && iabs(o->x / 16 - g->px) < 26)
                grab_fun(q, o);
        }
    }
    if (g->mech == ME_KART)
        kart_frame(q);
    if (g->make && q->sub == GS_PLAY) {
        if (g->blowing && q->anim_t - g->blow_t > 650) { /* it arrived: the answer */
            g->blowing = 0;
            if (g->mech == ME_POP)
                burst(q, 160, FY + 24, Q_SKY);
            if (g->mech == ME_ZAP)
                mq_spark_burst(q, MK_ZX(q), MK_ZY - 30, 10);
            mq_sfx(q, g->mech == ME_POP || g->mech == ME_ZAP ? MODX_SFX_POP : g->mech == ME_KART ? MODX_SFX_BOOST
                                                                                         : MODX_SFX_LAND, 0);
            mq_game_answer(q, g->blow_v);
        } else if (!g->blowing && !g->typed_n && q->anim_t - g->make_t > 9000) { /* nothing typed: pick one */
            g->make = 0;
            place_answers(q);
        }
    }
    /* Gate Dash / Kart Race: the dash through a gate */
    if ((g->mech == ME_GATES || g->mech == ME_KART) && g->fly && q->sub == GS_PLAY) {
        g->px = (int16_t)(g->px + 12);
        mq_gobj_t *o = ans_obj(q, g->lane);
        if (o && g->px >= o->x / 16 - 4) {
            g->fly = 0;
            g->px = (int16_t)lane_x0(q);
            choose(q, g->lane);
        } else if (!o)
            g->fly = 0, g->px = (int16_t)lane_x0(q);
    } else if (g->mech == ME_GATES || g->mech == ME_KART) {
        if (g->px > lane_x0(q))
            g->px = (int16_t)imax(lane_x0(q), g->px - 6);
    }
    if (g->make && g->blowing && lanes(g->mech)) /* a made gate / boost code: the dash to it */
        g->px = (int16_t)(lane_x0(q) + (MK_LX - 30 - lane_x0(q)) * imin(650, (int)(q->anim_t - g->blow_t)) / 650);
    /* Sort Slide: the card sinks to the waiting line, or flies to a bin */
    if (g->mech == ME_SORT) {
        if (g->fly) {
            int bx = bin_x(g, g->sel);
            g->fx = (int16_t)(g->fx + (Q4(bx) - g->fx) / 3), g->fy = (int16_t)(g->fy + Q4(9));
            if (g->fy >= Q4(FB - 34)) {
                g->fly = 0;
                g->fy = Q4(FY + 6);
                mq_game_answer(q, g->opt[g->sel]);
            }
        } else if (g->fy < Q4(FY + 6))
            g->fy = (int16_t)(g->fy + Q4(3));
    }
    /* a launched star / cake */
    if (g->mech == ME_LAUNCH && g->fly) {
        g->fx = (int16_t)(g->fx + g->fvx), g->fy = (int16_t)(g->fy + g->fvy);
        if (g->mode == 3)
            g->fvy = (int16_t)(g->fvy + 6);
        bool landed = g->mode == 3 ? g->fy >= Q4(FB - 30) && g->fvy > 0
                                   : iabs(g->fx / 16 - LAUNCH_CX) > LAUNCH_R + 10 || g->fy / 16 < FY + 4 ||
                                         iabs(g->fx / 16 - LAUNCH_CX) * iabs(g->fx / 16 - LAUNCH_CX) +
                                         (FB - 12 - g->fy / 16) * (FB - 12 - g->fy / 16) > (LAUNCH_R + 4) * (LAUNCH_R + 4);
        if (landed) {
            g->fly = 0;
            mq_sfx(q, MODX_SFX_LAND, 0);
            mq_spark_burst(q, g->fx / 16, g->fy / 16, 8);
            mq_game_answer(q, g->fly_aim);
        }
    }
    /* a held arrow moves the cloud smoothly */
    if (g->mech == ME_CATCH && q->sub == GS_PLAY) {
        uint32_t ms = 0;
        int k = q->api->held_key(&ms);
        if (k == HK_LEFT && ms > 120)
            g->px = (int16_t)imax(x0 + 16, g->px - 6);
        if (k == HK_RIGHT && ms > 120)
            g->px = (int16_t)imin(304, g->px + 6);
    }
    /* the moments move on by themselves, once what they say has had its reading time (a tap: at once) */
    if (q->sub == GS_CHEER) {
        uint32_t el = q->anim_t - g->cheer_t, need = mq_cheer_ms(q);
        if (el > need) {
            mq_read_note(q, el, (int)need, false);
            mq_run_after_right(q);
        }
    } else if (q->sub == GS_WHY && q->anim_t - g->trick_t > (uint32_t)imax(4200, why_read_ms(q))) {
        mq_read_note(q, q->anim_t - g->trick_t, why_read_ms(q), false);
        mq_run_after_miss(q);
    } else if (q->sub == GS_TRICK && q->anim_t - g->trick_t > (uint32_t)imax(2600, trick_read_ms(q))) {
        mq_read_note(q, q->anim_t - g->trick_t, trick_read_ms(q), false);
        mq_trick_next(q);
    }
}

/* ------------------------------------------------------------------ input */
static int hole_of_key(int c)
{
    switch (c) {
    case '7': return 0;
    case '8': return 1;
    case '9': return 2;
    case '4': return 3;
    case '5': return 4;
    case '6': return 5;
    default: return -1;
    }
}
static void hole_xy(const mq_t *q, int h, int *x, int *y)
{
    int x0 = field_x0(q), w = 316 - x0;
    *x = x0 + w * (2 * (h % 3) + 1) / 6, *y = FY + 46 + (h / 3) * 62;
}
int bin_x(const mq_game_t *g, int i)
{
    int n = g->nopt ? g->nopt : 1;
    return 4 + 312 * (2 * i + 1) / (2 * n);
}

static void bop_hole(mq_t *q, int h)
{
    mq_game_t *g = &q->gm;
    for (int i = 0; i < GM_NOBJ; i++) {
        mq_gobj_t *o = &g->o[i];
        if (o->kind && o->hp == h) {
            if (o->kind == GO_ANS) {
                if (q->sub == GS_PLAY)
                    choose(q, o->opt);
            } else if (q->sub != GS_PLAY) /* (a friend who is not an answer: nothing while an order waits) */
                grab_fun(q, o);
            return;
        }
    }
}

/* typing picks an answer that matches; ENTER answers what was typed */
static bool type_pick(mq_t *q, int c)
{
    mq_game_t *g = &q->gm;
    const mq_prob_t *p = &q->prob;
    if (p->kind == K_CHOICE)
        return false;
    if (c == 'r')
        c = 'R';
    bool ok = (c >= '0' && c <= '9') || (c == '/' && p->kind == K_FRAC) || (c == '.' && (p->kind == K_DEC || p->kind == K_MONEY)) ||
              (c == 'R' && p->kind == K_REM) || (c == ',' && p->kind == K_COORD);
    if (!ok || g->typed_n >= 10)
        return false;
    g->typed[g->typed_n++] = (char)c, g->typed[g->typed_n] = 0;
    if (g->make) { /* making it: the number grows in the bubble */
        mq_sfx(q, MODX_SFX_TAP, c);
        g->make_t = q->anim_t;
        return true;
    }
    int match = -1, prefix = 0;
    for (int i = 0; i < g->nopt; i++) {
        if ((g->gone >> i) & 1)
            continue;
        char t[16];
        typed_form(q, p, g->opt[i], t, sizeof t);
        int k = 0;
        while (g->typed[k] && t[k] == g->typed[k])
            k++;
        if (!g->typed[k]) {
            prefix++;
            if (!t[k])
                match = i;
        }
    }
    if (match >= 0 && prefix == 1) {
        if (g->mech == ME_CATCH) { /* steer under it: the catch is hers (and the focus there too) */
            mq_gobj_t *o = ans_obj(q, match);
            g->sel = (uint8_t)match;
            if (o)
                g->px = (int16_t)(o->x / 16);
        } else if (g->mech == ME_GATES || g->mech == ME_KART)
            g->lane = (int16_t)match, g->fly = 1;
        else if (g->mech == ME_ZAP) {
            mq_gobj_t *o = ans_obj(q, match);
            if (o)
                bop_hole(q, o->hp);
        } else
            choose(q, match);
        g->typed[0] = 0, g->typed_n = 0;
    } else if (!prefix && g->typed_n >= 4)
        g->typed[0] = 0, g->typed_n = 0;
    return true;
}

static int32_t parse_typed(mq_t *q);
/* the digit tiles of a made answer: 0-9, the point or the fraction bar when the answer has one, and
 * a tile to take the last one back */
static int field_x0(const mq_t *q);
static void tiles(const mq_t *q, char *t, int *n)
{
    int k = q->prob.kind, m = 0;
    for (int d = 0; d < 10; d++)
        t[m++] = (char)('0' + (d + 1) % 10);
    if (k == K_DEC || k == K_MONEY)
        t[m++] = '.';
    if (k == K_FRAC)
        t[m++] = '/';
    t[m++] = '<';
    t[m] = 0;
    *n = m;
}
/* Cake Kitchen: the mix, on the beat or not (on the beat: a little bonus, never a penalty) */
static void cook_mix(mq_t *q)
{
    mq_game_t *g = &q->gm;
    uint32_t bm = mq_beat_ms(0), ph = (q->anim_t - g->beat0) % bm;
    int32_t dt = (int32_t)(ph < bm / 2 ? ph : ph - bm);
    g->mix_j = (uint8_t)mq_beat_judge(dt), g->mix_t = mq_mark(q);
    mq_sfx(q, g->mix_j == 2 ? MODX_SFX_BONGO_HI : MODX_SFX_BONGO_LO, g->mix_j);
}
/* the made answer goes: a bubble floats up from the wand / a star falls to her cloud */
static void make_go(mq_t *q)
{
    mq_game_t *g = &q->gm;
    if (!g->typed_n || g->blowing)
        return;
    int32_t v = parse_typed(q);
    if (v < 0) {
        g->typed[0] = 0, g->typed_n = 0;
        return;
    }
    q->api->snprintf(g->blow_s, sizeof g->blow_s, "%s", g->typed);
    g->blow_v = v, g->blowing = 1, g->blow_t = mq_mark(q);
    g->typed[0] = 0, g->typed_n = 0;
    mq_sfx(q, g->mech == ME_POP ? MODX_SFX_WHOOSH : MODX_SFX_LAUNCH, 0);
}
static int32_t parse_typed(mq_t *q)
{
    int32_t v;
    if (mq_parse(&q->prob, q->gm.typed, &v))
        return -1;
    return v;
}

static int32_t till_total(const mq_game_t *g)
{
    static const int16_t val[5] = {100, 25, 10, 5, 1};
    int32_t t = 0;
    for (int i = 0; i < 5; i++)
        t += g->tray[i] * val[i];
    return t;
}

static void use_power(mq_t *q)
{
    mq_game_t *g = &q->gm;
    if (!g->power[0] || q->sub != GS_PLAY)
        return;
    if (g->power[0] == PW_SLOW)
        g->slow_until = q->anim_t + 10000;
    else { /* the wand: a dud fades away */
        int r = mq_game_right_opt(q);
        for (int i = 0; i < g->nopt; i++)
            if (i != r && !((g->gone >> i) & 1)) {
                g->gone |= (uint8_t)(1 << i);
                mq_gobj_t *o = ans_obj(q, i);
                if (o)
                    mq_spark_burst(q, o->x / 16, o->y / 16, 6), o->kind = 0;
                break;
            }
        g->wand_t = q->anim_t;
    }
    g->power[0] = g->power[1], g->power[1] = 0;
    mq_sfx(q, MODX_SFX_UNLOCK, 1);
}

static void clock_add(mq_game_t *g, int dmin)
{
    int h = g->value / 100, m = g->value % 100, t = (h % 12) * 60 + m + dmin;
    t = ((t % 720) + 720) % 720;
    h = t / 60, m = t % 60;
    g->value = (h ? h : 12) * 100 + m;
}

int bin_x(const mq_game_t *g, int i);
static int line_ticks(const mq_prob_t *p) { return p->v[1] * p->v[2] > 0 ? p->v[1] * p->v[2] : 4; }

/* ------------------------------------------------------------------ Treasure Heist: the lock */
/* The chest's combination lock is made from the shape of the answer: every group of digits a row of
 * as many wheels as it has digits (the natural layout: a 2-digit sum, two wheels; the playtest: "no
 * need for 4 digits for a 2 digit sum"), the point, an R for a leftover, a $ and a / as fixed plates. */
static void lock_make(mq_t *q)
{
    mq_game_t *g = &q->gm;
    char a[24];
    mq_answer_text(q, &q->prob, a, sizeof a);
    int n = 0;
    for (int i = 0; a[i] && n < 15;) {
        if (a[i] >= '0' && a[i] <= '9') {
            int len = 0;
            while (a[i + len] >= '0' && a[i + len] <= '9')
                len++;
            for (int k = 0; k < len && n < 15; k++)
                g->lock[n++] = '0';
            i += len;
            continue;
        }
        if (a[i] == '.' || a[i] == 'R' || a[i] == '$' || a[i] == '/')
            g->lock[n++] = a[i];
        i++;
    }
    g->lock[n] = 0, g->lock_n = (uint8_t)n, g->lock_sel = 0, g->clues = 0;
    { /* the last wheel of the first group selected: digits come in from the right */
        int i = 0;
        while (i < n && !(g->lock[i] >= '0' && g->lock[i] <= '9'))
            i++;
        while (i + 1 < n && g->lock[i + 1] >= '0' && g->lock[i + 1] <= '9')
            i++;
        g->lock_sel = (uint8_t)i;
    }
}
/* the lock's setting as an answer (leading zeros of a group dropped); -1 none */
static int32_t lock_value(mq_t *q)
{
    mq_game_t *g = &q->gm;
    char s[28];
    int n = 0;
    bool lead = true;
    for (int i = 0; i < g->lock_n && n < 24; i++) {
        char c = g->lock[i];
        if (c >= '0' && c <= '9') {
            bool last = !(g->lock[i + 1] >= '0' && g->lock[i + 1] <= '9');
            if (lead && c == '0' && !last)
                continue;
            lead = false;
            s[n++] = c;
        } else {
            s[n++] = c;
            lead = c != '.';
        }
    }
    s[n] = 0;
    int32_t v;
    return mq_parse(&q->prob, s, &v) ? -1 : v;
}
/* a digit typed: it shifts into the group of the selected wheel from the right */
static void lock_type(mq_game_t *g, char c)
{
    int i = g->lock_sel, a = i, b = i;
    if (i >= g->lock_n || g->lock[i] < '0' || g->lock[i] > '9')
        return;
    while (a > 0 && g->lock[a - 1] >= '0' && g->lock[a - 1] <= '9')
        a--;
    while (g->lock[b + 1] >= '0' && g->lock[b + 1] <= '9')
        b++;
    for (int k = a; k < b; k++)
        g->lock[k] = g->lock[k + 1];
    g->lock[b] = c;
}
/* on to the next group of wheels (after a '.' or an R typed): its last wheel */
static void lock_next_group(mq_game_t *g)
{
    int i = g->lock_sel;
    while (i < g->lock_n && g->lock[i] >= '0' && g->lock[i] <= '9')
        i++;
    while (i < g->lock_n && !(g->lock[i] >= '0' && g->lock[i] <= '9'))
        i++;
    if (i >= g->lock_n)
        return;
    while (i + 1 < g->lock_n && g->lock[i + 1] >= '0' && g->lock[i + 1] <= '9')
        i++;
    g->lock_sel = (uint8_t)i;
}
static void lock_show(mq_t *q)
{
    mq_game_t *g = &q->gm;
    char a[24];
    mq_answer_text(q, &q->prob, a, sizeof a);
    uint8_t clues = g->clues;
    lock_make(q);
    g->clues = clues;
    for (int i = 0; a[i]; i++)
        if (a[i] >= '0' && a[i] <= '9')
            lock_type(g, a[i]);
        else if (a[i] == '.' || a[i] == 'R' || a[i] == '/')
            lock_next_group(g);
}
static void lock_roll(mq_t *q, int i, int d)
{
    mq_game_t *g = &q->gm;
    if (i < 0 || i >= g->lock_n || g->lock[i] < '0' || g->lock[i] > '9')
        return;
    g->lock[i] = (char)('0' + (g->lock[i] - '0' + d + 10) % 10);
    g->lock_sel = (uint8_t)i;
    mq_sfx(q, MODX_SFX_CLICK, g->lock[i] - '0');
}
static void lock_open(mq_t *q)
{
    mq_game_t *g = &q->gm;
    int32_t v = lock_value(q);
    if (v < 0)
        return;
    if (!mq_correct(&q->prob, v))
        g->rattle_t = mq_mark(q);
    mq_game_answer(q, v);
}
/* the hiding spots: a painting, a pot, a barrel (x, y, w, h), along the top of the cave */
static const int16_t spot[3][4] = {{10, FY + 4, 46, 34}, {62, FY + 2, 40, 38}, {268, FY + 2, 44, 40}};
/* the story's numbers, the clues hidden round the room (from the full story) */
static int clue_nums(const mq_prob_t *p, int32_t out[3])
{
    int n = 0;
    for (int l = 0; l < 3 && n < 3; l++)
        for (const char *s = p->text[l]; *s && n < 3; s++)
            if (*s >= '0' && *s <= '9' && (s == p->text[l] || s[-1] < '0' || s[-1] > '9')) {
                int32_t v = 0;
                while (*s >= '0' && *s <= '9')
                    v = v * 10 + *s++ - '0';
                out[n++] = v;
                s--;
            }
    return n;
}

bool mq_game_key(mq_t *q, int k, int c)
{
    mq_game_t *g = &q->gm;
    if (q->scene != SC_PLAY)
        return false;
    if (g->scene_on && q->sub == GS_PLAY) { /* any key: on with the game */
        scene_end(q);
        return true;
    }
    if (mq_diag_on(q) && q->sub == GS_PLAY)
        return mq_diag_key(q, k, c);
    if (mq_teach_on(q) && q->sub == GS_PLAY)
        return mq_teach_key(q, k, c);
    if (mq_strat_on(q) && q->sub == GS_PLAY)
        return mq_strat_key(q, k, c);
    if (mq_cols_on(q) && q->sub == GS_PLAY) {
        if (k == HK_DIV)
            c = '/';
        return mq_cols_key(q, k, c);
    }
    int md = mq_mod_now(q); /* a mirrored field: the arrows go the way they look */
    if (md == MOD_MIRROR && (k == HK_LEFT || k == HK_RIGHT))
        k = k == HK_LEFT ? HK_RIGHT : HK_LEFT;
    if (md == MOD_FLIP && (k == HK_UP || k == HK_DOWN))
        k = k == HK_UP ? HK_DOWN : HK_UP;
    if (q->sub == GS_CHEER) {
        if (k == HK_ENTER)
            mq_run_after_right(q);
        return k == HK_ENTER;
    }
    if (q->sub == GS_WHY) {
        if (k == HK_ENTER || k == HK_RIGHT)
            mq_run_after_miss(q);
        return true;
    }
    if (q->sub == GS_TRICK) {
        if (k == HK_ENTER || k == HK_RIGHT)
            mq_trick_next(q);
        return true;
    }
    if (q->sub == GS_HELP) {
        if (k == HK_ENTER || k == HK_RIGHT || k == HK_DOWN)
            mq_helper_tap(q);
        return true;
    }
    if (q->sub != GS_PLAY)
        return false;
    mq_guide_input(q);
    if (g->bulb_t) { /* the lightbulb's scene: a key goes back to the game (ENTER: its big idea) */
        g->bulb_t = 0, g->hud_ok = 0;
        if (k == HK_ENTER && mq_teach_has(q))
            mq_teach_start(q, q->prob.idea, 2);
        else if (k == HK_ENTER || c == '2')
            mq_strat_bulb(q); /* (no big idea: ENTER, else 2: the friend's trick) */
        return true;
    }
    if (k == HK_F2) {
        use_power(q);
        return true;
    }
    if (k == HK_DIV)
        c = '/';
    else if (k == HK_DOT)
        c = '.';
    const mq_prob_t *p = &q->prob;
    if (g->make && makes(g->mech)) { /* making the answer: the keys type it */
        if (k == HK_ENTER || (k == HK_RIGHT && lanes(g->mech))) { /* blow, wish, dash, zoom, call them out */
            make_go(q);
            return true;
        }
        if (k == HK_DEL) {
            if (g->typed_n)
                g->typed[--g->typed_n] = 0;
            return true;
        }
        if (lanes(g->mech) && (k == HK_UP || k == HK_DOWN)) { /* the gate / the pad stays in front of her */
            g->lane = (int16_t)clampi(g->lane + (k == HK_UP ? -1 : 1), 0, 2);
            mq_sfx(q, MODX_SFX_STEP, 0);
            return true;
        }
        if (g->mech == ME_CATCH && (k == HK_LEFT || k == HK_RIGHT)) {
            g->px = (int16_t)clampi(g->px + (k == HK_LEFT ? -18 : 18), field_x0(q) + 16, 304);
            return true;
        }
        return type_pick(q, c);
    }
    switch (g->mech) {
    case ME_POP: case ME_ZAP: case ME_CATCH:
        if (g->mech == ME_ZAP && p->kind == K_CHOICE && hole_of_key(c) >= 0) {
            bop_hole(q, hole_of_key(c));
            return true;
        }
        if (g->mech == ME_ZAP && hole_of_key(c) >= 0 && !g->typed_n) { /* a key on its hole bops it */
            int h = hole_of_key(c);
            for (int i = 0; i < GM_NOBJ; i++)
                if (g->o[i].kind && g->o[i].hp == h) {
                    bop_hole(q, h);
                    return true;
                }
        }
        if (g->mech == ME_CATCH && (k == HK_LEFT || k == HK_RIGHT)) {
            g->px = (int16_t)clampi(g->px + (k == HK_LEFT ? -18 : 18), field_x0(q) + 16, 304);
            return true;
        }
        if (k == HK_LEFT || k == HK_RIGHT || k == HK_UP || k == HK_DOWN) { /* a focus ring over the answers */
            for (int s = 0; s < g->nopt; s++) {
                g->sel = (uint8_t)((g->sel + (k == HK_LEFT || k == HK_UP ? g->nopt - 1 : 1)) % g->nopt);
                if (!((g->gone >> g->sel) & 1))
                    break;
            }
            return true;
        }
        if (k == HK_ENTER) {
            if (g->typed_n) {
                int32_t v = parse_typed(q);
                g->typed[0] = 0, g->typed_n = 0;
                if (v >= 0)
                    mq_game_answer(q, v); /* typed: an answer of her own */
            } else if (g->mech == ME_ZAP) {
                mq_gobj_t *o = ans_obj(q, g->sel);
                if (o)
                    bop_hole(q, o->hp);
            } else if (g->mech == ME_CATCH) {
                mq_gobj_t *o = ans_obj(q, g->sel);
                if (o)
                    g->px = (int16_t)(o->x / 16);
            } else
                choose(q, g->sel);
            return true;
        }
        if (k == HK_DEL) {
            if (g->typed_n)
                g->typed[--g->typed_n] = 0;
            return true;
        }
        return type_pick(q, c);
    case ME_GATES: case ME_KART:
        if (k == HK_UP || k == HK_DOWN) {
            g->lane = (int16_t)clampi(g->lane + (k == HK_UP ? -1 : 1), 0, 2);
            mq_sfx(q, MODX_SFX_STEP, 0);
            return true;
        }
        if (k == HK_RIGHT || k == HK_ENTER) {
            if (g->typed_n && k == HK_ENTER) {
                int32_t v = parse_typed(q);
                g->typed[0] = 0, g->typed_n = 0;
                for (int i = 0; i < g->nopt; i++)
                    if (g->opt[i] == v)
                        g->lane = (int16_t)i;
            }
            if (g->lane < g->nopt && ans_obj(q, g->lane)) {
                g->fly = 1;
                mq_sfx(q, MODX_SFX_JUMP, 0);
            }
            return true;
        }
        if (k == HK_DEL) {
            if (g->typed_n)
                g->typed[--g->typed_n] = 0;
            return true;
        }
        return type_pick(q, c);
    case ME_SORT:
        if (k == HK_LEFT || k == HK_RIGHT) {
            g->sel = (uint8_t)clampi(g->sel + (k == HK_LEFT ? -1 : 1), 0, g->nopt - 1);
            g->fx = Q4(bin_x(g, g->sel));
            return true;
        }
        if (c >= '1' && c <= '4' && c - '1' < g->nopt) {
            g->sel = (uint8_t)(c - '1');
            g->fly = 1;
            return true;
        }
        if (k == HK_ENTER || k == HK_DOWN) {
            g->fly = 1;
            mq_sfx(q, MODX_SFX_WHOOSH, 1);
            return true;
        }
        return false;
    case ME_JUMP:
        if (g->mode == 1 && !g->rstep) { /* finding the two round numbers: the three pairs */
            if (k == HK_LEFT || k == HK_RIGHT) {
                g->rsel = (uint8_t)clampi(g->rsel + (k == HK_LEFT ? -1 : 1), 0, 2);
                mq_sfx(q, MODX_SFX_STEP, 0);
                return true;
            }
            if (k == HK_ENTER || k == HK_UP) {
                round_pick(q, g->rsel);
                return true;
            }
            return false;
        }
        if (g->mode == 1) {
            if (k == HK_LEFT || k == HK_RIGHT) {
                g->aim = k == HK_LEFT ? 0 : 1;
                mq_sfx(q, MODX_SFX_JUMP, 0);
                return true;
            }
            if ((k == HK_ENTER || k == HK_UP) && g->aim >= 0) {
                mq_game_answer(q, g->opt[g->aim]);
                return true;
            }
            return false;
        }
        if (k == HK_LEFT || k == HK_RIGHT) {
            g->aim = (int16_t)clampi(g->aim + (k == HK_LEFT ? -1 : 1), 0, line_ticks(p));
            g->moves++;
            mq_sfx(q, MODX_SFX_STEP, 0);
            return true;
        }
        if (k == HK_ENTER || k == HK_UP) {
            mq_sfx(q, MODX_SFX_JUMP, 0);
            mq_game_answer(q, g->aim);
            return true;
        }
        return false;
    case ME_LAUNCH:
        if (g->fly)
            return true;
        if (g->mode == 3) {
            if (k == HK_LEFT || k == HK_RIGHT || k == HK_UP || k == HK_DOWN) {
                g->aim = (int16_t)clampi(g->aim + (k == HK_LEFT || k == HK_DOWN ? -1 : 1), 0, line_ticks(p));
                return true;
            }
        } else {
            if (k == HK_LEFT || k == HK_RIGHT || k == HK_UP || k == HK_DOWN) {
                /* LEFT / RIGHT a degree, UP / DOWN five (audit C4: fine adjust) */
                int st = k == HK_LEFT ? 1 : k == HK_RIGHT ? -1 : k == HK_UP ? 5 : -5;
                g->aim = (int16_t)clampi(g->aim + st, 0, 180);
                g->typed[0] = 0, g->typed_n = 0;
                return true;
            }
            if (c >= '0' && c <= '9') { /* type the degrees */
                if (g->typed_n >= 3)
                    g->typed_n = 0;
                g->typed[g->typed_n++] = (char)c, g->typed[g->typed_n] = 0;
                int v = 0;
                for (int i = 0; i < g->typed_n; i++)
                    v = v * 10 + g->typed[i] - '0';
                g->aim = (int16_t)clampi(v, 0, 180);
                return true;
            }
        }
        if (k == HK_ENTER) {
            mq_game_fire(q);
            return true;
        }
        return false;
    case ME_BAKE:
        if (c >= '1' && c <= '9' && c - '1' < p->v[1]) {
            q->prob.v[5] = (int16_t)(q->prob.v[5] ^ (1 << (c - '1')));
            mq_sfx(q, MODX_SFX_SQUISH, 0);
            return true;
        }
        if (k == HK_DEL) {
            q->prob.v[5] = 0;
            return true;
        }
        if (k == HK_ENTER) {
            int n = 0;
            for (int i = 0; i < 12; i++)
                n += (p->v[5] >> i) & 1;
            mq_game_answer(q, n);
            return true;
        }
        return false;
    case ME_BUILD:
        if (k == HK_LEFT || k == HK_RIGHT) {
            if (!g->fixw)
                g->bw = (int16_t)clampi(g->bw + (k == HK_LEFT ? -1 : 1), 1, g->maxw);
            return true;
        }
        if (k == HK_UP || k == HK_DOWN) {
            if (!g->fixh)
                g->bh = (int16_t)clampi(g->bh + (k == HK_UP ? -1 : 1), 1, g->maxh);
            return true;
        }
        if (k == HK_ENTER) {
            mq_game_answer(q, g->fixh ? g->bw : g->bh);
            return true;
        }
        return false;
    case ME_CLOCK:
        if (k == HK_LEFT || k == HK_RIGHT) {
            clock_add(g, (k == HK_LEFT ? -1 : 1) * g->mode);
            mq_sfx(q, MODX_SFX_STEP, 0);
            return true;
        }
        if (k == HK_UP || k == HK_DOWN) {
            clock_add(g, k == HK_UP ? 60 : -60);
            return true;
        }
        if (c >= '0' && c <= '9') { /* type the time: 340 -> 3:40 */
            if (g->typed_n >= 4)
                g->typed_n = 0;
            g->typed[g->typed_n++] = (char)c, g->typed[g->typed_n] = 0;
            if (g->typed_n >= 3) {
                int v = 0;
                for (int i = 0; i < g->typed_n; i++)
                    v = v * 10 + g->typed[i] - '0';
                if (v % 100 < 60 && v / 100 >= 1 && v / 100 <= 12)
                    g->value = v;
            }
            return true;
        }
        if (k == HK_ENTER) {
            g->typed_n = 0;
            mq_game_answer(q, g->value);
            return true;
        }
        return false;
    case ME_TILL: case ME_PETS:
        if (c >= '0' && c <= '9') { /* the till's keys: the digits shift in, cents first */
            if (g->typed_n >= 5)
                return true;
            g->typed[g->typed_n++] = (char)c, g->typed[g->typed_n] = 0;
            return true;
        }
        if (k == HK_DEL) {
            if (g->typed_n)
                g->typed[--g->typed_n] = 0;
            else
                for (int i = 4; i >= 0; i--)
                    if (g->tray[i]) {
                        g->tray[i]--;
                        break;
                    }
            return true;
        }
        if (k == HK_ENTER) {
            int32_t v = 0;
            for (int i = 0; i < g->typed_n; i++)
                v = v * 10 + g->typed[i] - '0';
            if (!g->typed_n)
                v = till_total(g);
            g->typed[0] = 0, g->typed_n = 0;
            mq_sfx(q, MODX_SFX_COIN, 2);
            mq_game_answer(q, v);
            return true;
        }
        return false;
    case ME_DIG:
        if (k == HK_LEFT || k == HK_RIGHT) {
            g->px = (int16_t)clampi(g->px + (k == HK_LEFT ? -1 : 1), 0, 10);
            return true;
        }
        if (k == HK_UP || k == HK_DOWN) {
            g->py = (int16_t)clampi(g->py + (k == HK_UP ? 1 : -1), 0, 7);
            return true;
        }
        if (k == HK_ENTER) {
            mq_sfx(q, MODX_SFX_LAND, 1);
            mq_game_answer(q, g->px * 100 + g->py);
            return true;
        }
        return false;
    case ME_VAULT: /* the lock: digits shift in, arrows choose and roll a wheel, ENTER opens */
        if (k == HK_LEFT || k == HK_RIGHT) {
            for (int i = g->lock_sel + (k == HK_LEFT ? -1 : 1); i >= 0 && i < g->lock_n; i += k == HK_LEFT ? -1 : 1)
                if (g->lock[i] >= '0' && g->lock[i] <= '9') {
                    g->lock_sel = (uint8_t)i;
                    break;
                }
            return true;
        }
        if (k == HK_UP || k == HK_DOWN) {
            lock_roll(q, g->lock_sel, k == HK_UP ? 1 : -1);
            return true;
        }
        if (k == HK_DEL) { /* the group back to zeros */
            for (int i = 0; i < 6; i++)
                lock_type(g, '0');
            return true;
        }
        if (k == HK_ENTER) {
            lock_open(q);
            return true;
        }
        if (c >= '0' && c <= '9') {
            lock_type(g, (char)c);
            mq_sfx(q, MODX_SFX_CLICK, c - '0');
            return true;
        }
        if (c == '.' || c == 'R' || c == 'r' || c == '/' || c == ' ') {
            if (c != ' ')
                lock_next_group(g);
            return true;
        }
        return false;
    case ME_COOK: /* labelled amounts: the arrows and ENTER, or the keys of an amount's label, pick one */
        if (k == HK_LEFT || k == HK_RIGHT || k == HK_UP || k == HK_DOWN) {
            for (int s = 0; s < g->nopt; s++) {
                g->sel = (uint8_t)((g->sel + (k == HK_LEFT || k == HK_UP ? g->nopt - 1 : 1)) % g->nopt);
                if (!((g->gone >> g->sel) & 1))
                    break;
            }
            return true;
        }
        if (k == HK_ENTER) {
            choose(q, g->sel);
            return true;
        }
        if (k == HK_DEL) {
            g->typed[0] = 0, g->typed_n = 0;
            return true;
        }
        return type_pick(q, c);
    default: return false;
    }
}


/* fire the launcher */
void mq_game_fire(mq_t *q)
{
    mq_game_t *g = &q->gm;
    if (g->fly || q->sub != GS_PLAY)
        return;
    g->fly = 1, g->fly_aim = g->aim;
    if (g->mode == 3) { /* by fraction power: a lob to the tick */
        int ticks = line_ticks(&q->prob), tx = 24 + g->aim * 272 / (ticks ? ticks : 1);
        g->fx = Q4(24), g->fy = Q4(FB - 30);
        int frames = 18;
        g->fvx = (int16_t)(Q4(tx - 24) / frames), g->fvy = (int16_t)(-(6 * frames) / 2);
    } else {
        g->fx = Q4(LAUNCH_CX), g->fy = Q4(FB - 12);
        g->fvx = (int16_t)(mq_icos(g->aim) * 10 * 16 / 1024 / 2), g->fvy = (int16_t)(-mq_isin(g->aim) * 10 * 16 / 1024 / 2);
    }
    mq_sfx(q, MODX_SFX_LAUNCH, g->aim);
}

/* ------------------------------------------------------------------ the widgets of the round */
/* Every control of every game is a widget (mq_widget.c): the same gestures, hit areas of at least 10 mm,
 * feedback, the focus ring and the first-time demo everywhere. layout() makes the round's set from its
 * state (before it is drawn and before input is read); on_widget() is what each one does. */
enum { W_GO = 1, W_ANS, W_FUN, W_STEER, W_LANE, W_CARD, W_BIN, W_LINE, W_PAD, W_AIM, W_PIE, W_PLOT, W_HAND, W_COIN,
       W_TRAY, W_TRAYROW, W_KEY, W_MAP, W_WHEEL, W_SPOT, W_TILE, W_MAKE, W_HELP, W_POWER, W_BOARD, W_FRIEND, W_HOLE, W_NOTE,
       W_PAIR };
#define GO_X 246
#define GO_Y (FB - 40)
#define GO_W 70
#define GO_H 36
static const char go_label[ME_COUNT][8] = {"", "", "", "", "", "", "land!", "fling!", "serve!", "done!", "set!", "ring!",
                                           "dig!", "open!", "", "ring!"}; /* (the kitchen: a tap on the right amount mixes) */
/* the lock's wheels: a pitch that fits them all, at most 40 px */
static int lock_pitch(int n) { return n ? imin(40, 304 / n) : 40; }
static int lock_wx(int n, int i) { return 160 - n * lock_pitch(n) / 2 + i * lock_pitch(n); }
#define WHEEL_Y (FY + 44)
#define WHEEL_H 64
/* the made answer's digit tiles: a row along the bottom, the field's width */
#define TILE_W 26
#define TILE_Y (FB - 26)
static int tiles_x0_(void) { return 4; }
/* the kitchen's and the till's keys */
static void till_key(int i, int *x, int *y) { *x = 212 + (i % 3) * 34, *y = FY + 32 + (i / 3) * 27; }
/* the till's keys: 1..9, 0 under 1 (beside the ring! button, never under it), and '<' beside the display */
static const char till_keys[12] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0', ' ', ' '};

static void layout(mq_t *q)
{
    mq_game_t *g = &q->gm;
    const mq_prob_t *p = &q->prob;
    mq_wg_begin(q);
    if (q->scene != SC_PLAY || g->scene_on || g->bulb_t)
        return;
    int i;
    if (q->sub == GS_CHEER && g->mech == ME_POP) { /* the cheer: the fun that floats up (plain and golden bubbles,
                                                    * power-ups) is hers to pop -- a tap elsewhere goes on (CR2-23) */
        for (int k = 0; k < GM_NOBJ; k++) {
            const mq_gobj_t *o = &g->o[k];
            if (!o->kind || o->kind == GO_ANS || o->kind == GO_ROCK || o->y / 16 < FY || o->y / 16 >= FB)
                continue;
            i = mq_wg_add(q, WG_CHOICE, W_FUN, k, o->x / 16 - 16, o->y / 16 - 16, 32, 32);
            mq_wg_flags(q, i, WF_CUSTOM | WF_ONDOWN | WF_NOFOCUS);
        }
        return;
    }
    if (q->sub != GS_PLAY)
        return;
    /* the top line and the board: what do I do?, the power-up, the friend (a giggle), the whole story */
    i = mq_wg_add(q, WG_BUTTON, W_HELP, 0, 262, MQ_Y0 + 1, 16, 15), mq_wg_flags(q, i, WF_CUSTOM | WF_NOFOCUS);
    if (g->power[0])
        i = mq_wg_add(q, WG_BUTTON, W_POWER, 0, 277, MQ_Y0 + 1, 40, 15), mq_wg_flags(q, i, WF_CUSTOM | WF_NOFOCUS);
    i = mq_wg_add(q, WG_AREA, W_FRIEND, 0, 0, MQ_Y0 + 16, 44, FY - MQ_Y0 - 16), mq_wg_flags(q, i, WF_CUSTOM | WF_NOFOCUS);
    i = mq_wg_add(q, WG_AREA, W_BOARD, 0, 44, MQ_Y0 + 16, 276, FY - MQ_Y0 - 16), mq_wg_flags(q, i, WF_CUSTOM | WF_NOFOCUS);
    int x0 = field_x0(q);
    if (g->make && makes(g->mech) && !g->blowing) { /* making the answer: the digit tiles, the wand, the sign */
        char t[14];
        int n = 0;
        tiles(q, t, &n);
        for (int k = 0; k < n; k++) {
            char s[2] = {t[k], 0};
            i = mq_wg_add(q, WG_KEY, W_TILE, k, tiles_x0_() + k * TILE_W, TILE_Y, TILE_W - 2, 22);
            mq_wg_label(q, i, s, t[k] == '<' ? Q_PINK : k % 2 ? Q_CREAM : RGB(255, 236, 214));
        }
        if (g->mech == ME_POP || g->mech == ME_CATCH)
            i = mq_wg_add(q, WG_BUTTON, W_MAKE, 0, 210, FY + 10, 100, FB - FY - 50);
        else if (lanes(g->mech))
            i = mq_wg_add(q, WG_BUTTON, W_MAKE, 0, MK_LX - 40, FY + 4 + g->lane * 49, 80, 46);
        else
            i = mq_wg_add(q, WG_BUTTON, W_MAKE, 0, MK_ZX(q) - 44, MK_ZY - 60, 88, 80);
        mq_wg_flags(q, i, WF_CUSTOM);
        if (lanes(g->mech))
            i = mq_wg_add(q, WG_STEER, W_LANE, 0, field_x0(q), FY + 2, 140, 146), mq_wg_range(q, i, g->lane, 0, 2, 1),
            mq_wg_flags(q, i, WF_CUSTOM | WF_VERT | WF_NOFOCUS);
        if (g->mech == ME_CATCH)
            i = mq_wg_add(q, WG_STEER, W_STEER, 0, x0 + 16, FB - 50, 288 - x0, 48), mq_wg_range(q, i, g->px, x0 + 16, 304, 18),
            mq_wg_flags(q, i, WF_CUSTOM | WF_NOFOCUS);
        return;
    }
    switch (g->mech) {
    case ME_POP: /* the bubbles: popped the moment the finger lands on one (they move) */
        for (int k = 0; k < GM_NOBJ; k++) {
            const mq_gobj_t *o = &g->o[k];
            if (!o->kind || o->kind == GO_ROCK || o->y / 16 < FY || o->y / 16 >= FB || (o->kind != GO_ANS && q->sub == GS_PLAY))
                continue; /* (while an order waits only its answers are tapped) */
            int r = o->kind == GO_ANS ? (mq_mod_now(q) == MOD_GIANT ? 33 : 24) : 16;
            if (o->kind == GO_ANS && ((g->gone >> o->opt) & 1))
                continue;
            i = mq_wg_add(q, WG_CHOICE, o->kind == GO_ANS ? W_ANS : W_FUN, o->kind == GO_ANS ? o->opt : k, o->x / 16 - r, o->y / 16 - r,
                          2 * r, 2 * r);
            mq_wg_flags(q, i, WF_CUSTOM | WF_ONDOWN | (o->kind == GO_ANS ? 0 : WF_NOFOCUS));
        }
        break;
    case ME_CATCH: /* the cloud steers; a tap on a star brings the cloud under it */
        i = mq_wg_add(q, WG_STEER, W_STEER, 0, x0 + 16, FY, 288 - x0, FB - FY), mq_wg_range(q, i, g->px, x0 + 16, 304, 18);
        mq_wg_flags(q, i, WF_CUSTOM);
        for (int k = 0; k < GM_NOBJ; k++) {
            const mq_gobj_t *o = &g->o[k];
            if (o->kind == GO_ANS && !((g->gone >> o->opt) & 1) && o->y / 16 >= FY - 8) /* (picked as the finger lands) */
                i = mq_wg_add(q, WG_CHOICE, W_ANS, o->opt, o->x / 16 - 24, o->y / 16 - 24, 48, 48),
                mq_wg_flags(q, i, WF_CUSTOM | WF_ONDOWN);
        }
        break;
    case ME_GATES: case ME_KART: /* a gate (a pad) to dash through; the lanes to step between */
        i = mq_wg_add(q, WG_STEER, W_LANE, 0, field_x0(q), FY + 2, 140, 146), mq_wg_range(q, i, g->lane, 0, 2, 1);
        mq_wg_flags(q, i, WF_CUSTOM | WF_VERT | WF_NOFOCUS);
        for (int k = 0; k < GM_NOBJ; k++) {
            const mq_gobj_t *o = &g->o[k];
            if (o->kind == GO_ANS) /* where it is drawn (the bot's 8: a tap on a pad that had rolled past x 180 fell to the
                                    * lanes behind it), taken as the finger lands; still rolling in: at the edge */
                i = mq_wg_add(q, WG_CHOICE, W_ANS, o->opt, imin(MQ_W - 70, imax(0, o->x / 16 - 35)), FY + 2 + o->opt * 49, 70, 46),
                mq_wg_flags(q, i, WF_CUSTOM | WF_ONDOWN);
        }
        break;
    case ME_ZAP: /* the holes */
        for (int h = 0; h < 6; h++) {
            int hx, hy;
            hole_xy(q, h, &hx, &hy);
            i = mq_wg_add(q, WG_CHOICE, W_HOLE, h, hx - 30, hy - 34, 60, 48), mq_wg_flags(q, i, WF_CUSTOM | WF_ONDOWN);
        }
        break;
    case ME_SORT: /* the card to carry, the bins it goes in */
        if (!g->fly) {
            i = mq_wg_add(q, WG_DRAG, W_CARD, 0, g->fx / 16 - 70, g->fy / 16, 140, 72), mq_wg_flags(q, i, WF_CUSTOM);
            for (int k = 0; k < g->nopt; k++) {
                int bx = bin_x(g, k), w = 312 / g->nopt - 8;
                i = mq_wg_add(q, WG_DROP, W_BIN, k, bx - w / 2, FB - 50, w, 48), mq_wg_flags(q, i, WF_CUSTOM);
            }
        }
        break;
    case ME_JUMP:
        if (g->mode == 1 && !g->rstep) { /* rounding, first: the two round numbers it sits between */
            for (int k = 0; k < 3; k++) {
                char l[24];
                int32_t a = round_pair(g, k);
                q->api->snprintf(l, sizeof l, "%ld and %ld", (long)a, (long)(a + g->opt[1] - g->opt[0]));
                i = mq_wg_add(q, WG_CHOICE, W_PAIR, k, 8 + k * 103, FY + 34, 98, 34);
                mq_wg_label(q, i, l, k == g->rwob - 1 && q->anim_t - g->rwob_t < 2500 ? Q_PINK : Q_WHITE);
            }
        } else if (g->mode == 1) { /* rounding: hop to an end */
            for (int e = 0; e < 2; e++)
                i = mq_wg_add(q, WG_CHOICE, W_PAD, e, e ? MQ_W - 62 : 2, FY + 46, 60, 60), mq_wg_flags(q, i, WF_CUSTOM); /* (on the screen, above land!) */
        } else {
            i = mq_wg_add(q, WG_NUMLINE, W_LINE, 0, 24, FY + 62, 272, 48);
            mq_wg_range(q, i, g->aim, 0, line_ticks(p), 1), mq_wg_flags(q, i, WF_CUSTOM);
        }
        break;
    case ME_LAUNCH:
        if (g->mode == 3) {
            i = mq_wg_add(q, WG_SLIDER, W_AIM, 0, 24, FY + 2, 272, FB - FY - 44);
            mq_wg_range(q, i, g->aim, 0, line_ticks(p), 1), mq_wg_flags(q, i, WF_CUSTOM);
        } else {
            i = mq_wg_add(q, WG_SLIDER, W_AIM, 0, LAUNCH_CX - LAUNCH_R - 10, FB - 12 - LAUNCH_R - 10, 2 * LAUNCH_R + 20, LAUNCH_R + 10);
            mq_wg_range(q, i, g->aim, 0, 180, 1), mq_wg_flags(q, i, WF_CUSTOM | WF_DIAL);
        }
        break;
    case ME_BAKE: i = mq_wg_add(q, WG_AREA, W_PIE, 0, 120 - 66, FY + 72 - 66, 132, 132), mq_wg_flags(q, i, WF_CUSTOM); break;
    case ME_BUILD: {
        int cell = mq_build_cell(g);
        i = mq_wg_add(q, WG_AREA, W_PLOT, 0, 20 - cell, FY + 10 - cell / 2, g->maxw * cell + 2 * cell, g->maxh * cell + 3 * cell / 2);
        mq_wg_flags(q, i, WF_CUSTOM);
        break;
    }
    case ME_CLOCK: { /* the two hands: the one nearest the finger's angle moves */
        int h = g->value / 100 % 12, m = g->value % 100;
        i = mq_wg_add(q, WG_CLOCKHAND, W_HAND, 1, 110 - 76, FY + 74 - 76, 152, 152), mq_wg_range(q, i, m, 0, 59, g->mode);
        mq_wg_flags(q, i, WF_CUSTOM);
        i = mq_wg_add(q, WG_CLOCKHAND, W_HAND, 2, 110 - 76, FY + 74 - 76, 152, 152), mq_wg_range(q, i, h, 0, 11, 1);
        mq_wg_flags(q, i, WF_CUSTOM);
        break;
    }
    case ME_TILL: case ME_PETS:
        if (g->mode == 0) {
            for (int k = 0; k < 5; k++) /* the drawer's coins: tap one in, or carry it to the tray */
                i = mq_wg_add(q, WG_DRAG, W_COIN, k, DRAWER_X(k), FB - 34, DRAWER_W, 30), mq_wg_flags(q, i, WF_CUSTOM);
            i = mq_wg_add(q, WG_DROP, W_TRAY, 0, 2, FY + 2, 200, FB - FY - 42), mq_wg_flags(q, i, WF_CUSTOM);
            for (int k = 0; k < 5; k++) /* a coin in the tray: tap it back out */
                if (g->tray[k])
                    i = mq_wg_add(q, WG_CHOICE, W_TRAYROW, k, 4, FY + 4 + k * 22, 196, 22), mq_wg_flags(q, i, WF_CUSTOM | WF_NOFOCUS);
        } else
            for (int k = 0; k < 12; k++)
                if (till_keys[k] != ' ') {
                    int kx, ky;
                    char s[2] = {till_keys[k], 0};
                    till_key(k, &kx, &ky);
                    i = mq_wg_add(q, WG_KEY, W_KEY, (uint8_t)till_keys[k], kx, ky, 32, 25);
                    mq_wg_label(q, i, s, Q_WHITE);
                }
        if (g->mode != 0) { /* (delete: by the display) */
            i = mq_wg_add(q, WG_KEY, W_KEY, '<', 280, FY + 6, 32, 24);
            mq_wg_label(q, i, "<", Q_PINK);
        }
        break;
    case ME_DIG: i = mq_wg_add(q, WG_AREA, W_MAP, 0, 20, FB - 20 - 7 * 18 - 9, 240, 7 * 18 + 22), mq_wg_flags(q, i, WF_CUSTOM); break;
    case ME_VAULT: { /* the clues hidden round the room; the lock's wheels */
        for (int s = 0; s < 3; s++)
            i = mq_wg_add(q, WG_CHOICE, W_SPOT, s, spot[s][0], spot[s][1], spot[s][2], spot[s][3]),
            mq_wg_flags(q, i, WF_CUSTOM | WF_NOFOCUS);
        int n = g->lock_n, pw = lock_pitch(n);
        for (int k = 0; k < n; k++)
            if (g->lock[k] >= '0' && g->lock[k] <= '9') {
                i = mq_wg_add(q, WG_WHEEL, W_WHEEL, k, lock_wx(n, k) + 2, WHEEL_Y, pw - 4, WHEEL_H);
                mq_wg_range(q, i, g->lock[k] - '0', 0, 9, 1);
                if (k == g->lock_sel && mq_wg_focused(q) < 0)
                    mq_wg_focus(q, i);
            }
        break;
    }
    case ME_COOK: /* the amounts to mix in: a tap on the right one, on the beat (no typing in a rhythm game) */
        for (int k = 0; k < g->nopt; k++)
            if (!((g->gone >> k) & 1)) {
                i = mq_wg_add(q, WG_KEY, W_ANS, k, 6 + k * 58, FB - 54, 54, 34);
                mq_wg_label(q, i, g->lab[k], Q_WHITE);
            }
        break;
    default: break;
    }
    if (go_label[g->mech][0] && !(g->mech == ME_JUMP && g->mode == 1 && !g->rstep)) {
        i = mq_wg_add(q, WG_BUTTON, W_GO, 0, GO_X, GO_Y, GO_W, GO_H);
        mq_wg_label(q, i, go_label[g->mech], Q_MINT);
    }
    if (mq_cols_wanted(q)) { /* the notebook, to work it out on paper */
        bool keys = (g->mech == ME_TILL || g->mech == ME_PETS) && g->mode != 0; /* (the till's keys: left of its 0) */
        i = mq_wg_add(q, WG_BUTTON, W_NOTE, 0, keys ? 212 - 78 : GO_X - 78, GO_Y, 72, GO_H);
        mq_wg_label(q, i, "notebook", Q_LAV);
    }
}

/* the go button of each game: what ENTER does */
static void go(mq_t *q)
{
    mq_game_key(q, HK_ENTER, 0);
}

static void on_widget(mq_t *q, const mq_wev_t *ev)
{
    mq_game_t *g = &q->gm;
    const mq_prob_t *p = &q->prob;
    int v = ev->value;
    switch (ev->id) {
    case W_HELP:
        if (ev->why == WHY_TAP)
            mq_guide_help(q);
        break;
    case W_POWER:
        if (ev->why == WHY_TAP)
            use_power(q);
        break;
    case W_FRIEND:
        if (ev->why == WHY_TAP) {
            static const char poke[4][16] = {"Hee hee!", "That tickles!", "Hi there!", "Boop!"};
            g->poke_t = mq_mark(q);
            mq_speak(q, q->prob.ca % MQ_NCAST, poke[(q->anim_t / 40) % 4], MODX_SPEAK_NOW);
            mq_sfx(q, MODX_SFX_SQUISH, 1);
            g->hud_ok = 0;
        }
        break;
    case W_BOARD: /* the whole story, told */
        if (ev->why == WHY_TAP && (q->prob.stg || q->prob.cap[0])) {
            g->full_t = g->full_t && q->anim_t - g->full_t < 6000 ? 0 : (mq_mark(q));
            g->hud_ok = 0;
        }
        break;
    case W_GO:
        if (ev->why == WHY_TAP || ev->why == WHY_KEY)
            go(q);
        break;
    case W_NOTE:
        if (ev->why == WHY_TAP || ev->why == WHY_KEY)
            mq_cols_open(q);
        break;
    case W_TILE:
        if (ev->why == WHY_TAP || ev->why == WHY_KEY) {
            char t[14];
            int n = 0;
            tiles(q, t, &n);
            if (ev->arg < n) {
                if (t[ev->arg] == '<') {
                    if (g->typed_n)
                        g->typed[--g->typed_n] = 0;
                } else
                    mq_game_key(q, 0, t[ev->arg]);
                mq_sfx(q, MODX_SFX_TAP, ev->arg);
            }
        }
        break;
    case W_MAKE:
        if (ev->why == WHY_TAP || ev->why == WHY_KEY)
            make_go(q);
        break;
    case W_ANS:
        if (ev->why != WHY_TAP && ev->why != WHY_KEY)
            break;
        if (ev->why == WHY_TAP && ev->y < FY && (g->mech != ME_CATCH || !ans_obj(q, ev->arg) || ans_obj(q, ev->arg)->y / 16 < FY))
            break; /* (a finger on the order board pops nothing that floats under it -- but the top of a star in the field
                    * is the star's: the bot's 8) */
        if (g->mech == ME_CATCH) { /* a tap on a falling answer: the cloud swoops under it and catches it at
                                     * once (the playtest: tapping it did nothing until it landed) */
            mq_gobj_t *o = ans_obj(q, ev->arg);
            if (o)
                g->px = (int16_t)clampi(o->x / 16, field_x0(q) + 16, 304);
            choose(q, ev->arg);
        } else if (lanes(g->mech)) { /* that lane, and dash */
            g->lane = (int16_t)ev->arg;
            if (ans_obj(q, ev->arg))
                g->fly = 1, mq_sfx(q, MODX_SFX_JUMP, 0);
        } else
            choose(q, ev->arg);
        break;
    case W_FUN: {
        mq_gobj_t *o = &g->o[ev->arg % GM_NOBJ];
        if (ev->y < FY)
            break;
        if (o->kind && o->kind != GO_ANS && q->sub != GS_PLAY)
            grab_fun(q, o);
        break;
    }
    case W_HOLE:
        if (ev->why == WHY_TAP || ev->why == WHY_KEY)
            bop_hole(q, ev->arg);
        break;
    case W_STEER: g->px = (int16_t)clampi(v, field_x0(q) + 16, 304); break;
    case W_LANE:
        if (v != g->lane)
            g->lane = (int16_t)clampi(v, 0, 2), mq_sfx(q, MODX_SFX_STEP, 0);
        break;
    case W_CARD: /* carried over a bin, dropped in one, or tapped then a bin tapped */
        if (ev->why == WHY_DRAG && ev->x > 0) {
            g->fx = Q4(clampi(ev->x, 20, 300));
            if (v >= 0)
                g->sel = (uint8_t)v;
        } else if (ev->why == WHY_DROP) {
            g->sel = (uint8_t)v, g->fly = 1;
            mq_sfx(q, MODX_SFX_WHOOSH, 1);
        } else if (ev->why == WHY_RELEASE && ev->y > FB - 70) { /* let go low over a bin */
            g->sel = (uint8_t)clampi(ev->x * g->nopt / 320, 0, g->nopt - 1), g->fly = 1;
        }
        break;
    case W_BIN:
        if (ev->why == WHY_TAP || ev->why == WHY_KEY) {
            g->sel = (uint8_t)v, g->fly = 1;
            mq_sfx(q, MODX_SFX_WHOOSH, 1);
        }
        break;
    case W_PAD:
        if (ev->why == WHY_TAP || ev->why == WHY_KEY)
            g->aim = (int16_t)ev->arg, g->moves++, mq_sfx(q, MODX_SFX_JUMP, 0);
        break;
    case W_PAIR:
        if (ev->why == WHY_TAP || ev->why == WHY_KEY)
            round_pick(q, ev->arg);
        break;
    case W_LINE:
        if (v != g->aim)
            g->moves++, mq_sfx(q, MODX_SFX_STEP, 0);
        g->aim = (int16_t)clampi(v, 0, line_ticks(p));
        break;
    case W_AIM:
        if (!g->fly)
            g->aim = (int16_t)v, g->typed[0] = 0, g->typed_n = 0;
        break;
    case W_PIE: { /* a tap toggles a slice, a drag frosts across */
        if (ev->why == WHY_SCROLL || ev->why == WHY_RELEASE)
            break;
        int s = mq_pie_slice(ev->x - 120, ev->y - (FY + 72), p->v[1]);
        if (s < 0)
            break;
        if (ev->why == WHY_PRESS || ev->why == WHY_TAP) {
            if (ev->why == WHY_PRESS) /* (toggled on the DOWN; its TAP on release must not toggle it back) */
                q->prob.v[5] = (int16_t)(p->v[5] ^ (1 << s)), g->grab = (uint8_t)(s + 1);
            else {
                if (g->grab != s + 1) /* (a tap without its DOWN) */
                    q->prob.v[5] = (int16_t)(p->v[5] ^ (1 << s));
                g->grab = 0;
            }
        } else {
            if (s != g->grab - 1) /* (dragged on to another slice: no TAP follows) */
                g->grab = 0;
            if (s != g->sel && !((p->v[5] >> s) & 1))
                q->prob.v[5] = (int16_t)(p->v[5] | (1 << s));
        }
        g->sel = (uint8_t)s;
        mq_sfx(q, MODX_SFX_SQUISH, 0);
        break;
    }
    case W_PLOT: { /* the corner follows the finger; arrows and the wheel step it */
        int cell = mq_build_cell(g), ox = 20, oy = FY + 10, ow = g->bw, oh = g->bh;
        if (ev->why == WHY_PRESS || ev->why == WHY_DRAG || ev->why == WHY_TAP) {
            if (!g->fixw)
                g->bw = (int16_t)clampi((ev->x - ox + cell / 2) / cell, 1, g->maxw);
            if (!g->fixh)
                g->bh = (int16_t)clampi((ev->y - oy + cell / 2) / cell, 1, g->maxh);
        } else if (ev->why == WHY_SCROLL) {
            if (!g->fixw)
                g->bw = (int16_t)clampi(g->bw - v, 1, g->maxw);
            else
                g->bh = (int16_t)clampi(g->bh - v, 1, g->maxh);
        }
        if (g->bw != ow || g->bh != oh)
            mq_sfx(q, MODX_SFX_STEP, g->fixh ? g->bw : g->bh);
        break;
    }
    case W_HAND: { /* the minute hand (arg 1) or the hour hand (2) */
        int h = g->value / 100 % 12, m = g->value % 100;
        if (ev->arg == 1) {
            int nm = (v / g->mode) * g->mode % 60;
            if (ev->why == WHY_SCROLL || ev->why == WHY_KEY || ev->why == WHY_FLICK) { /* a step either way */
                int d = v - m;
                d = d > 30 ? d - 60 : d < -30 ? d + 60 : d;
                clock_add(g, d);
                break;
            }
            if (m >= 45 && nm < 15) /* round the top: the hour moves on, as on a real clock */
                h = (h + 1) % 12;
            else if (m < 15 && nm >= 45)
                h = (h + 11) % 12;
            m = nm;
        } else
            h = v % 12;
        g->value = (h ? h : 12) * 100 + m;
        break;
    }
    case W_COIN: /* tapped: in it goes; carried and dropped on the tray: the same */
        if (ev->why == WHY_PRESS || (ev->why == WHY_DROP && ev->target >= 0)) {
            if (g->tray[ev->arg] < 9)
                g->tray[ev->arg]++, mq_sfx(q, MODX_SFX_COIN, ev->arg);
            mq_wgs(q)->picked = -1;
        }
        break;
    case W_TRAYROW:
        if (ev->why == WHY_TAP && g->tray[ev->arg])
            g->tray[ev->arg]--;
        break;
    case W_KEY:
        if (ev->why != WHY_TAP && ev->why != WHY_KEY)
            break;
        if (ev->arg == '<')
            mq_game_key(q, HK_DEL, 0);
        else if (g->mech == ME_TILL || g->mech == ME_PETS) {
            if (g->typed_n < 5)
                g->typed[g->typed_n++] = (char)ev->arg, g->typed[g->typed_n] = 0;
        } else
            mq_game_key(q, 0, ev->arg);
        break;
    case W_MAP:
        if (ev->why == WHY_PRESS || ev->why == WHY_DRAG || ev->why == WHY_TAP) {
            g->px = (int16_t)clampi((ev->x - 30 + DIG_DX / 2) / DIG_DX, 0, 10);
            g->py = (int16_t)clampi((FB - 20 - ev->y + 9) / 18, 0, 7);
        }
        break;
    case W_SPOT:
        if (ev->why == WHY_TAP && !((g->clues >> ev->arg) & 1))
            g->clues |= (uint8_t)(1 << ev->arg), mq_sfx(q, MODX_SFX_UNLOCK, ev->arg);
        break;
    case W_WHEEL:
        if (ev->arg < g->lock_n && g->lock[ev->arg] >= '0' && g->lock[ev->arg] <= '9') {
            g->lock[ev->arg] = (char)('0' + (v % 10 + 10) % 10);
            g->lock_sel = ev->arg;
            mq_sfx(q, MODX_SFX_CLICK, v);
        }
        break;
    default: break;
    }
}

/* a twisted field: a point as the game sees it (the card beside the field is not mirrored) */
static void twist_xy(mq_t *q, int *x, int *y)
{
    if (*y < FY || q->sub != GS_PLAY)
        return;
    int md = mq_mod_now(q);
    if (md == MOD_MIRROR && *x >= field_x0(q))
        *x = 2 * ((field_x0(q) + 320) / 2) - *x - 1;
    if (md == MOD_FLIP)
        *y = 2 * ((FY + FB) / 2) - *y - 1;
}

bool mq_game_touch(mq_t *q, int type, int x, int y)
{
    mq_game_t *g = &q->gm;
    if (q->scene != SC_PLAY)
        return false;
    int sx = x, sy = y; /* (where it is on the screen: the lightbulb's scene is drawn untwisted -- CR2-22) */
    if (q->sub == GS_PLAY && (mq_teach_on(q) || mq_strat_on(q) || g->scene_on) && type == TV_TAP && y < MQ_Y0 + 16 && x >= 258 && x < 284) {
        mq_game_unblock(q); /* the "?": what to do, now */
        mq_guide_help(q);
        return true;
    }
    if (mq_diag_on(q) && q->sub == GS_PLAY)
        return mq_diag_touch(q, type, x, y);
    if (mq_teach_on(q) && q->sub == GS_PLAY)
        return mq_teach_touch(q, type, x, y);
    if (mq_strat_on(q) && q->sub == GS_PLAY)
        return mq_strat_touch(q, type, x, y);
    if (mq_cols_on(q) && q->sub == GS_PLAY)
        return mq_cols_touch(q, type, x, y);
    if (g->scene_on && q->sub == GS_PLAY) { /* a tap: on with the game */
        if (type == TV_TAP)
            scene_end(q);
        return true;
    }
    twist_xy(q, &x, &y); /* a twisted field: the touch as the game sees it */
    if (q->sub == GS_CHEER || q->sub == GS_WHY || q->sub == GS_TRICK || q->sub == GS_HELP) {
        if (type == TV_TAP && q->wg.downpick && mq_since(q, q->wg.press_t) < 1500) { /* the release of the finger that
                                       * picked as it landed: not "on" -- the cheer (or the why) is seen (CR-06) */
            q->wg.downpick = false;
            return true;
        }
        if (q->sub == GS_CHEER && (type == TV_DOWN || type == TV_TAP)) { /* a fun thing floating up: popped (CR2-23) */
            layout(q);
            mq_wev_t ev;
            if (q->wg.n && mq_wg_touch(q, type, x, y, &ev) && ev.why != WHY_NONE) {
                on_widget(q, &ev);
                return true;
            }
        }
        if (type == TV_TAP) {
            if (q->sub == GS_CHEER)
                mq_run_after_right(q);
            else if (q->sub == GS_WHY)
                mq_run_after_miss(q);
            else if (q->sub == GS_HELP)
                mq_helper_tap(q);
            else
                mq_trick_next(q);
        }
        return true;
    }
    if (q->sub != GS_PLAY)
        return false; /* the offer: its buttons */
    if (q->mash_t && q->now - q->mash_t < MASH_PAUSE_MS) /* mashing: a moment to look (the taps wait) */
        return true;
    mq_guide_input(q);
    if (g->bulb_t) { /* the lightbulb's scene: a tap goes back to the game (its big idea button: the idea) */
        if (type == TV_TAP) {
            g->bulb_t = 0, g->hud_ok = 0;
            int bx, by, bw, bh;
            if (mq_bulb_idea_btn(q, &bx, &by, &bw, &bh) && sx >= bx - 4 && sx < bx + bw + 4 && sy >= by - 6 && sy < by + bh + 6)
                mq_teach_start(q, q->prob.idea, 2);
            else if (mq_strat_bulb_btn(q, &bx, &by, &bw, &bh) && sx >= bx - 4 && sx < bx + bw + 4 && sy >= by - 6 && sy < by + bh + 6)
                mq_strat_bulb(q); /* a friend's trick for her order (mq_strat.c) */
        }
        return true;
    }
    layout(q);
    mq_wev_t ev;
    if (mq_wg_touch(q, type, x, y, &ev) && ev.why != WHY_NONE)
        on_widget(q, &ev);
    return true;
}

bool mq_game_swipe(mq_t *q, int dir, int x0, int y0)
{
    mq_game_t *g = &q->gm;
    if (q->scene != SC_PLAY || q->sub != GS_PLAY)
        return false;
    layout(q);
    mq_wev_t ev;
    twist_xy(q, &x0, &y0);
    if (mq_wg_swipe(q, dir, x0, y0, &ev) && ev.why != WHY_NONE) { /* (looked up where the flick began, not where the
                                                                   * finger left the glass: CR2-08) */
        on_widget(q, &ev);
        return true;
    }
    if (g->mech == ME_SORT && !g->fly) { /* swipe the card towards its bin */
        int n = g->nopt;
        g->sel = (uint8_t)(dir == 0 ? 0 : dir == 1 ? n - 1 : n == 3 ? 1 : g->sel);
        g->fly = 1;
        mq_sfx(q, MODX_SFX_WHOOSH, 1);
        return true;
    }
    if ((g->mech == ME_GATES || g->mech == ME_KART) && dir >= 2) {
        g->lane = (int16_t)clampi(g->lane + (dir == 2 ? -1 : 1), 0, 2);
        return true;
    }
    if (g->mech == ME_LAUNCH && dir == 2) {
        mq_game_fire(q);
        return true;
    }
    return false;
}

/* A scroll wheel (the Mac): the widget under the pointer (or the focused one) takes it: a wheel rolls, a
 * slider or a line steps, the cloud steers, the clock turns, a rug grows */
bool mq_game_scroll(mq_t *q, int dy, int x, int y)
{
    if (q->scene != SC_PLAY)
        return false;
    if (mq_teach_on(q) || mq_diag_on(q))
        return true;
    if (mq_strat_on(q) && q->sub == GS_PLAY)
        return mq_strat_scroll(q, dy, x, y);
    if (mq_cols_on(q))
        return mq_cols_scroll(q, dy, x, y);
    if (q->sub != GS_PLAY || q->gm.scene_on)
        return false;
    mq_guide_input(q);
    layout(q);
    mq_wev_t ev;
    /* only an adjustable widget under the pointer: a wheel, a slider, a line, a clock hand, the rug's corner
     * (never the steering of a game, never anywhere else) */
    if (mq_wg_scroll(q, dy, x, y, &ev) && ev.why != WHY_NONE && (ev.kind != WG_AREA || ev.id == W_PLOT))
        on_widget(q, &ev);
    return true;
}

/* the widgets of the round now (tests, the console) */
void mq_game_widgets(mq_t *q) { layout(q); }
/* a box of the field as it is on the screen: a twisted field (mirror world, gravity flip) is drawn mirrored
 * and its touches mirrored back (the debug hooks list the targets where she sees them) */
void mq_game_screen_box(mq_t *q, int *x, int *y, int w, int h)
{
    if (*y < FY || q->sub != GS_PLAY)
        return;
    int md = mq_mod_now(q);
    if (md == MOD_MIRROR && *x >= field_x0(q))
        *x = 2 * ((field_x0(q) + 320) / 2) - *x - w;
    if (md == MOD_FLIP)
        *y = 2 * ((FY + FB) / 2) - *y - h;
}
/* the game's main control (its first in the field: what the first-time demo shows); -1 none */
int mq_game_main_widget(mq_t *q)
{
    layout(q);
    for (int i = 0; i < q->wg.n; i++)
        if (q->wg.w[i].id < W_HELP && q->wg.w[i].id != W_FUN && q->wg.w[i].id != W_SPOT)
            return i;
    return -1;
}

/* ------------------------------------------------------------------ geometry helpers (tested) */
bool mq_hit_circle(int x, int y, int cx, int cy, int r) { return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r; }

/* the angle of (dx, dy) in degrees, 0 = +x, counter-clockwise, 0..359 (dy up) */
int mq_angle_of(int dx, int dy)
{
    if (!dx && !dy)
        return 0;
    int best = 0, bd = 0x7FFFFFFF;
    for (int a = 0; a < 360; a++) { /* the direction (dot > 0) with the smallest cross product */
        if (dx * mq_icos(a) + dy * mq_isin(a) <= 0)
            continue;
        int c = dx * mq_isin(a) - dy * mq_icos(a);
        c = c < 0 ? -c : c;
        if (c < bd)
            bd = c, best = a;
    }
    return best;
}

/* the slice of a pie of d slices (clockwise from 12 o'clock) under (dx, dy) from its centre; -1 */
int mq_pie_slice(int dx, int dy, int d)
{
    if (d <= 0 || dx * dx + dy * dy > 66 * 66)
        return -1;
    int a = mq_angle_of(-dy, dx); /* clockwise from 12 o'clock */
    return a * d / 360 % d;
}

/* Build It: the cell size for the plot */
int mq_build_cell(const mq_game_t *g)
{
    int c = imin(270 / (g->maxw ? g->maxw : 14), 118 / (g->maxh ? g->maxh : 8));
    return c > 18 ? 18 : c;
}

/* ------------------------------------------------------------------ drawing */
static uint16_t world_bg(mq_t *q, int k)
{
    static const uint16_t bg[MQ_NWORLD][2] = {
        {RGB(214, 244, 222), RGB(150, 214, 160)}, {RGB(214, 234, 255), RGB(160, 200, 240)},
        {RGB(255, 228, 238), RGB(244, 180, 200)}, {RGB(234, 226, 255), RGB(190, 176, 236)},
        {RGB(255, 236, 214), RGB(240, 196, 150)}, {RGB(255, 248, 214), RGB(240, 220, 140)},
        {RGB(222, 248, 240), RGB(160, 222, 200)}, {RGB(226, 228, 255), RGB(176, 180, 240)},
        {RGB(220, 236, 250), RGB(150, 190, 230)}};
    int w = q->world < MQ_NWORLD ? q->world : 0;
    if (q->theme.loaded && q->theme.has_skin && w < THM_NWORLD && q->theme.skin[w][k ? 2 : 0])
        return q->theme.skin[w][k ? 2 : 0];
    return bg[w][k];
}

static void bubble_box(mq_t *q, int x, int y, int w, int h, uint16_t c)
{
    mq_rrect(q, x, y + 2, w, h, 9, RGB(220, 210, 228));
    mq_rrect(q, x, y, w, h, 9, c);
}

/* the order board: the friend says a few words and the story is a little scene with its numbers
 * as labels (mq_stage.c); a tap on the board tells the whole story. Orders without a scene show
 * their text. */
static void draw_order(mq_t *q)
{
    mq_game_t *g = &q->gm;
    const mq_prob_t *p = &q->prob;
    const modx_api_t *api = q->api;
    mq_fill(q, 0, MQ_Y0 + 16, MQ_W, FY - MQ_Y0 - 16, world_bg(q, 0));
    mq_fill(q, 0, FY - 3, MQ_W, 3, world_bg(q, 1));
    if (g->scene_on) { /* the story is being acted below: the game that comes next, as a title */
        const char *nm = g->boss ? "Big finale!" : me_name[g->mech];
        int w = mq_text_w(q, api->font_large, nm);
        mq_star(q, 160 - w / 2 - 14, MQ_Y0 + 36, 6, Q_YELLOW), mq_star(q, 160 + w / 2 + 14, MQ_Y0 + 36, 6, Q_YELLOW);
        mq_text_c(q, api->font_large, 161, MQ_Y0 + 27, nm, Q_WHITE);
        mq_text_c(q, api->font_large, 160, MQ_Y0 + 26, nm, Q_PURPLE);
        return;
    }
    int anim = g->mood == 2 ? (q->anim_t - g->cheer_t < 700 ? AN_HAPPY : AN_DANCE) : g->mood == 1 ? AN_POUT : AN_IDLE;
    bool talk = g->say[0] && (q->sub != GS_PLAY || q->anim_t - g->dud_t < (uint32_t)imax(4000, mq_read_ms(q, g->say))); /* (the oops line: its reading time, then the order again) */
    bool typing = q->say_t && q->anim_t - q->say_t < 1400 && q->say_slot == p->ca % MQ_NCAST;
    if (typing && anim == AN_IDLE)
        anim = AN_TALK; /* the mouth goes while the line types out */
    uint32_t in = q->anim_t - g->order_t;
    int fx = 2;
    if (in < 420 && anim == AN_IDLE) /* an order: the friend hops in from the side */
        fx = -40 + (int)in * 42 / 420, anim = AN_HOP;
    else if (in < 1300 && anim == AN_IDLE)
        anim = AN_WAVE; /* and waves hello */
    if (g->poke_t && q->anim_t - g->poke_t < 760)
        anim = AN_POKED; /* she tapped them: a surprised hop and a giggle */
    mq_sprite_anim(q, p->ca % MQ_NCAST, fx, MQ_Y0 + 18, 2, anim);
    if (q->sub == GS_CHEER && g->mood == 2 && p->cb != p->ca) { /* the friends hug and dance (beside the one who
                                                                   asked: not over the field's numbers) */
        int el = (int)(q->anim_t - g->cheer_t);
        mq_cast(q, p->cb % MQ_NCAST, 18, MQ_Y0 + 20, 30, el < 600 ? AN_HAPPY : AN_DANCE);
        if (el > 300)
            mq_heart(q, 22, MQ_Y0 + 22 - (el / 100) % 4, 4, Q_HOTPINK);
    }
    if (q->sub == GS_CHEER && g->big_t && q->anim_t - g->big_t < big_ms(q)) { /* the combo's big words, in the band */
        int el = (int)(q->anim_t - g->big_t), dy = el < 200 ? (200 - el) / 10 : 0;
        const font_t *f = api->font_xlarge;
        static const uint16_t bc[4] = {Q_HOTPINK, Q_ORANGE, Q_PURPLE, Q_BLUE};
        bubble_box(q, 46, MQ_Y0 + 18, 272, 38, Q_WHITE);
        mq_fill(q, 42, MQ_Y0 + 30, 5, 6, Q_WHITE);
        mq_text_fit(q, f, 183, MQ_Y0 + 24 - dy, 260, g->big_s, Q_LAV, true);
        mq_text_fit(q, f, 182, MQ_Y0 + 23 - dy, 260, g->big_s, bc[(el / 90) % 4], true);
        return;
    }
    bool full = g->full_t && q->anim_t - g->full_t < 6000 && q->sub == GS_PLAY;
    char board[3 * MQ_TXT + 48], bl[3][MQ_TXT];
    int blines = mq_board_text(q, p, board, (int)sizeof board, bl);
    if ((p->stg || p->cap[0]) && !full && (talk || blines <= 1)) { /* one line: the stage below it */
        const char *say = talk ? g->say : board;
        char l[3][MQ_TXT];
        int lines = say ? mq_story_wrap(q, say, l, 262) : 0;
        if (lines <= 1) {
            const font_t *f = say && api->text_width(api->font_medium, say) <= 262 ? api->font_medium : api->font_small;
            bubble_box(q, 46, MQ_Y0 + 18, 272, 18, Q_WHITE);
            mq_fill(q, 42, MQ_Y0 + 26, 5, 5, Q_WHITE);
            if (say)
                mq_text(q, f, 52, MQ_Y0 + 19 + (f == api->font_small ? 1 : 0), lines <= 1 ? say : l[0],
                        talk ? (g->mood == 2 ? Q_GREEN : Q_HOTPINK) : Q_INK);
            if (!g->scene_on) { /* (while the story is acted, its things are in the field) */
                int sw = g->mech == ME_KART ? 230 : 276;
                mq_stage_draw(q, p, 44, MQ_Y0 + 36, sw, q->sub == GS_CHEER || q->sub == GS_WHY);
                char cue[40]; /* the operation's tag at the row's end, where it fits */
                int tw = mq_stage_width(q, p);
                if (mq_op_cue(q, p, cue, sizeof cue)) {
                    int cw = mq_text_w(q, api->font_small, cue) + 10;
                    if (tw + 24 + cw < sw)
                        mq_rrect(q, 44 + sw - cw, MQ_Y0 + 40, cw, 13, 6, Q_LAV),
                            mq_text(q, api->font_small, 49 + sw - cw, MQ_Y0 + 40, cue, Q_INK);
                }
            }
            return;
        }
    }
    bubble_box(q, 46, MQ_Y0 + 18, 272, 38, Q_WHITE);
    mq_fill(q, 42, MQ_Y0 + 30, 5, 6, Q_WHITE);
    if (talk && !full) {
        char l[3][MQ_TXT];
        mq_story_wrap(q, g->say, l, 262);
        for (int i = 0; i < 3; i++)
            mq_text(q, api->font_small, 52, MQ_Y0 + 19 + i * 12, l[i], g->mood == 2 ? Q_GREEN : Q_HOTPINK);
    } else { /* the whole order, wrapped to the bubble (long names too): what to find, always */
        bool fits = blines <= 3;
        for (int i = 0; i < 3; i++)
            mq_text(q, api->font_small, 52, MQ_Y0 + 19 + i * 12, fits ? bl[i] : p->text[i], Q_INK);
    }
}

/* what the top line says over the game's name now (the audit's O1: none of it over the field; the
 * render audit's M3 and H9: it is the game's one lane for words, nothing else covers the field or the
 * order): the first-time demo's words, the round's twist, the goal for a moment, a toast that waited
 * for it, the guide's "psst" when she has been still; NULL. *kind: HS_* */
enum { HS_GUIDE, HS_DEMO, HS_TWIST, HS_TOAST, HS_BIG };
static const char *hud_say(mq_t *q, int *who, int *kind)
{
    mq_game_t *g = &q->gm;
    *who = q->world >= G_SKY ? 8 : q->world % MQ_NCAST;
    *kind = HS_GUIDE;
    if (g->scene_on || g->bulb_t || q->sub == GS_HELP || q->sub == GS_WHY || q->sub == GS_TRICK)
        return NULL;
    if (q->sub == GS_PLAY) {
        int md = mq_mod_now(q);
        uint32_t in = q->anim_t - g->order_t;
        bool twisted = md && (g->mod == MOD_REMIX || q->run_i == 0);
        uint32_t tw_ms = (uint32_t)imax(1600, mq_read_ms(q, mq_mod_name(md)) + (g->mod == MOD_REMIX && q->run_i == 0 ? 800 : 0));
        if (twisted && in < tw_ms) /* the twist, announced (a remix: "Remix!" first; before a demo's words) */
            return *kind = HS_TWIST, mq_mod_name(g->mod == MOD_REMIX && q->run_i == 0 && in < 800 ? MOD_REMIX : md);
        if (mq_guide_demo_on(q))
            return *kind = HS_DEMO, mq_mech_goal(g->mech);
        if (g->big_t && q->anim_t - g->big_t < big_ms(q)) /* "Overtake!", "Golden!" in the middle of play */
            return *kind = HS_BIG, g->big_s;
        static const char mk_goal[5][36] = {"Make the number, blow its bubble!", "Make the number, wish on the star!",
                                            "Make the gate's number, dash!", "Type the boost code, zoom!",
                                            "Make their number, peekaboo!"};
        int mi = g->mech == ME_POP ? 0 : g->mech == ME_CATCH ? 1 : g->mech == ME_GATES ? 2 : g->mech == ME_KART ? 3 : 4;
        const char *goal = g->make ? mk_goal[mi] : me_goal[g->mech];
        uint32_t gm = (uint32_t)mq_read_ms(q, goal); /* (the goal: its reading time at least, after a twist's) */
        if (q->anim_t - g->t0 < (uint32_t)(twisted ? imax(4600, (int)(tw_ms + gm)) : imax(3000, (int)gm)) ||
            (mq_prof(q)->problems < 3 && in < (uint32_t)imax(4000, (int)gm)))
            return goal;
    }
    if (q->sub == GS_PLAY && q->mash_t && q->now - q->mash_t < (uint32_t)imax(2 * MASH_PAUSE_MS, mq_read_ms(q, "Whoa, slow down! Look first."))) /* mashing: slow down, look */
        return "Whoa, slow down! Look first.";
    const char *t = mq_toast_claim(q);
    if (t)
        return *kind = HS_TOAST, t;
    uint32_t idle = q->now - (q->last_input > q->prob_start ? q->last_input : q->prob_start);
    if (q->sub == GS_PLAY && idle > 5000u && !g->blowing && !g->hoh && !g->helped)
        return "Psst! Look what glows!";
    return NULL;
}
/* (a number that changes when the top line's words do: its cache key) */
static uint32_t hud_now(mq_t *q)
{
    int who, kind;
    const char *s = hud_say(q, &who, &kind);
    uint32_t h = 7 + (uint32_t)kind;
    for (; s && *s; s++)
        h = h * 31u + (uint8_t)*s;
    return h;
}
/* (the top line and the band show "+2" and the combo's words while they last: a cache key) */
static uint32_t fx_now(mq_t *q)
{
    mq_game_t *g = &q->gm;
    uint32_t v = 0;
    for (int i = 0; i < 4; i++)
        if (g->pop_t[i] && q->anim_t - g->pop_t[i] < pop_ms(q, i))
            v += 1 + (q->anim_t - g->pop_t[i]) / 100;
    if (g->big_t && q->anim_t - g->big_t < big_ms(q))
        v += 31u * (1 + (q->anim_t - g->big_t) / 40);
    return v;
}
/* the top line: the game's name, the round's stars, orders done, combo, power-ups; or the guide's words */
static void draw_hud(mq_t *q)
{
    mq_game_t *g = &q->gm;
    const modx_api_t *api = q->api;
    mq_fill(q, 0, MQ_Y0, MQ_W, 16, Q_WHITE);
    int who, kind;
    const char *say = hud_say(q, &who, &kind);
    if (say) { /* the guide's face and words, in the top line (a twist: in its own colours) */
        bool night = kind == HS_TWIST && mq_mod_now(q) == MOD_NIGHT;
        uint16_t bg = kind == HS_DEMO ? Q_LAV : kind == HS_TWIST || kind == HS_BIG ? (night ? RGB(64, 72, 140) : Q_HOTPINK) : Q_YELLOW;
        int w = mq_text_w(q, api->font_small, say) + 26;
        w = w < 256 ? w : 256;
        mq_rrect(q, 2, MQ_Y0 + 1, w, 15, 7, bg);
        mq_cast(q, who, 3, MQ_Y0, 16, AN_TALK);
        mq_text_fit(q, api->font_small, 21, MQ_Y0 + 2, w - 24, say, kind == HS_TWIST || kind == HS_BIG ? (night ? Q_YELLOW : Q_WHITE) : Q_INK, false);
        mq_circle(q, 270, MQ_Y0 + 8, 7, Q_LAV);
        mq_text_c(q, api->font_small, 270, MQ_Y0 + 2, "?", Q_WHITE);
        return;
    }
    int pk = -1; /* stars just won ("+2"): where the game's name is, for a moment (the render audit's M5: not
                  * over the field where it happened) */
    for (int i = 0; i < 4; i++)
        if (g->pop_t[i] && q->anim_t - g->pop_t[i] < pop_ms(q, i) && (pk < 0 || g->pop_t[i] > g->pop_t[pk]))
            pk = i;
    if (pk >= 0) {
        int el = (int)(q->anim_t - g->pop_t[pk]);
        char ps[16];
        api->snprintf(ps, sizeof ps, "%s", g->pop_s[pk]); /* (a flourish: "yay!", never a count) */
        mq_rrect(q, 2, MQ_Y0 + 1, mq_text_w(q, api->font_small, ps) + 10, 14, 7, el / 100 % 2 ? Q_PINK : Q_YELLOW);
        mq_text(q, api->font_small, 7, MQ_Y0 + 2, ps, Q_INK);
    } else
        mq_text(q, api->font_small, 4, MQ_Y0 + 2, g->boss ? "Big finale!" : me_name[g->mech], Q_PURPLE);
    if (g->mech == ME_KART) { /* her place in the race (the audit's Z1: in the top line, not on the board) */
        static const char pl[5][5] = {"", "1st", "2nd", "3rd", "4th"};
        int k = g->place >= 1 && g->place <= 4 ? g->place : 4;
        mq_rrect(q, 160, MQ_Y0 + 1, 34, 14, 7, k == 1 ? Q_YELLOW : k == 2 ? Q_GREY : Q_PEACH);
        mq_text_c(q, api->font_small, 177, MQ_Y0 + 2, pl[k], Q_INK);
    }
    int total = q->run_n + q->extra;
    { /* the order board's tickets: a heart an order, closer together in a long round (clear of the race's place) */
        int x0 = 92, room = (g->mech == ME_KART ? 156 : 195) - x0, sp = total > 1 ? room / (total - 1) : 9;
        sp = sp > 9 ? 9 : sp < 4 ? 4 : sp;
        int r = sp >= 8 ? 3 : 2;
        bool shine = g->half_t && q->anim_t - g->half_t < 1100 && (q->anim_t / 150) % 2; /* the halfway cheer */
        for (int i = 0; i < total && x0 + i * sp <= x0 + room; i++)
            mq_heart(q, x0 + i * sp, MQ_Y0 + 8, r, i < q->run_i ? (shine ? Q_YELLOW : Q_HOTPINK) : i == q->run_i ? Q_PINK : Q_GREY);
    }
    if (q->combo >= 2) {
        char s[12];
        api->snprintf(s, sizeof s, "x%d", q->combo);
        int z = q->anim_t - g->cheer_t < 300 ? 1 : 0;
        mq_rrect(q, 200 - z, MQ_Y0 + 1 - z, 30 + 2 * z, 14 + 2 * z, 7, q->combo >= 5 ? Q_YELLOW : Q_PINK);
        mq_text_c(q, api->font_small, 215, MQ_Y0 + 2, s, Q_INK);
    }
    /* her purse: the coins of this run go into it (the round keeps no score of its own) */
    mq_purse_icon(q, 238, MQ_Y0 + 8, 6);
    q->coin_ix = 238, q->coin_iy = MQ_Y0 + 8;
    if (q->run_coins)
        mq_cents_text(q, api->font_small, 246, MQ_Y0 + 2, q->run_coins > 99 ? 99 : q->run_coins, Q_INK, false);
    mq_circle(q, 270, MQ_Y0 + 8, 7, Q_LAV); /* "what do I do?" (F3 too) */
    mq_text_c(q, api->font_small, 270, MQ_Y0 + 2, "?", Q_WHITE);
    for (int i = 0; i < 2; i++) /* power-ups held (F2 or a tap uses one) */
        if (g->power[i]) {
            int x = 284 + i * 18;
            mq_circle(q, x, MQ_Y0 + 8, 7, g->power[i] == PW_WAND ? Q_LAV : Q_MINT);
            if (g->power[i] == PW_WAND)
                mq_star(q, x, MQ_Y0 + 8, 5, Q_YELLOW);
            else
                mq_ring(q, x, MQ_Y0 + 8, 3, Q_INK);
        }
}

/* half way to white */
static uint16_t light(uint16_t c) { return (uint16_t)(((c & 0xF7DE) >> 1) + (0xF7DE >> 1)); }

/* a soap bubble: it wobbles (squash and stretch), its rim shimmers, a shine sits on top */
static void soap(mq_t *q, int x, int y, int r, int k, uint16_t col)
{
    int w = mq_isin((int)((q->anim_t / 5 + (uint32_t)k * 77) % 360)) * 2 / 1024;
    int32_t sv[3] = {col, k, r}; /* (a high-resolution platform may draw it from its own art: MODX_HR_SPRITE) */
    int hr = mq_hr_sprite(q, "game/bubble", x - r - 4, y - r - 4, 2 * r + 8, 2 * r + 8, sv, 3);
    mq_oval(q, x, y, r + w, r - w, col);
    mq_oval(q, x, y, r + w - 3, r - w - 3, light(light(col)));
    mq_oval(q, x - r / 3, y - r / 3, r / 4 + 1, r / 5 + 1, Q_WHITE);
    int a = (int)((q->anim_t / 4 + (uint32_t)k * 90) % 360); /* a glint runs round the rim */
    mq_circle(q, x + mq_icos(a) * (r - 1) / 1024, y + mq_isin(a) * (r - 1) / 1024, 2, (q->anim_t / 160 + (uint32_t)k) % 2 ? Q_WHITE : Q_YELLOW);
    if (hr)
        mq_hr_end(q);
}

static void draw_answer_bubble(mq_t *q, int x, int y, int i, uint16_t col)
{
    mq_game_t *g = &q->gm;
    const modx_api_t *api = q->api;
    /* the bubble fits its number (4 digits too): the medium font while the bubble stays within a
     * lane, the small one beyond */
    const font_t *f = api->text_width(api->font_medium, g->lab[i]) / 2 + 9 > 23 ? api->font_small : api->font_medium;
    int w = api->text_width(f, g->lab[i]), r = imax(18, w / 2 + 9);
    bool dud = (g->gone >> i) & 1;
    if (dud) { /* a dud: wilted and grey */
        mq_rrect(q, x - r + 2, y - 10, 2 * r - 4, 22, 10, Q_GREY);
        mq_text_c(q, f, x, y - 6, g->lab[i], Q_DIM);
        return;
    }
    int32_t rv[1] = {col};
    int hr = mq_hr_sprite(q, "game/ring", x - r, y - r, 2 * r, 2 * r, rv, 1);
    mq_circle(q, x, y, r, col);
    mq_circle(q, x, y, r - 3, Q_WHITE);
    mq_circle(q, x - r * 3 / 5, y - r * 3 / 5, 2, col); /* the shine, by the rim (never over a digit) */
    if (hr)
        mq_hr_end(q);
    if (q->kbd && g->sel == i)
        mq_ring(q, x, y, r + 2, Q_PURPLE);
    if (x - w / 2 >= 0 && x + w / 2 <= MQ_W) /* (its number once all of it is on the screen: never half a number) */
        mq_text_c(q, f, x, y - (f == api->font_small ? 6 : 7), g->lab[i], Q_INK);
}

static void draw_card(mq_t *q)
{
    if (!shows_card(q))
        return;
    mq_rrect(q, 2, FY + 2, 114, FH - 6, 8, Q_WHITE);
    q->vis_q = true;
    mq_visual(q, &q->prob, 4, FY + 4, 110, FH - 10, 1000);
    q->vis_q = false;
}

static void draw_player(mq_t *q, int x, int y, int anim)
{
    q->buddy_anim = (uint8_t)anim;
    mq_buddy_draw(q, mq_prof(q)->buddy % MQ_NCAST, x, y, 1, 1);
    q->buddy_anim = 0;
}

/* the digit tiles of a made answer: widgets (layout), drawn with the others */
static void draw_tiles(mq_t *q) { (void)q; }

/* a sign with the number she is making: blinking "?" while empty, a hint under it */
static void make_sign(mq_t *q, int x, int y, uint16_t bg, const char *go, int hint_dy)
{
    const modx_api_t *api = q->api;
    const char *s = q->gm.blowing || q->sub != GS_PLAY ? q->gm.blow_s : q->gm.typed;
    int w = imax(52, mq_text_w(q, api->font_medium, s) + 18);
    mq_rrect(q, x - w / 2, y - 12, w, 24, 8, bg);
    mq_rect(q, x - w / 2, y - 12, w, 24, Q_INK);
    if (s[0])
        mq_text_c(q, api->font_medium, x, y - 8, s, Q_INK);
    else if ((q->anim_t / 400) % 2)
        mq_text_c(q, api->font_medium, x, y - 8, "?", Q_HOTPINK);
    if (q->sub == GS_PLAY && !q->gm.blowing && go) {
        const char *h = q->gm.typed_n ? go : "type it!";
        int hw = mq_text_w(q, api->font_small, h) + 12;
        mq_rrect(q, x - hw / 2, y + hint_dy, hw, 14, 7, Q_WHITE);
        mq_text_c(q, api->font_small, x, y + hint_dy - 1, h, Q_INK);
    }
}

/* Gate Dash, Kart Race and Peekaboo when she makes the answer: the gate with a blank sign she
 * dashes through, the boost pad whose code she types (the kart zooms over it), the friend peeking
 * out of a hole who pops out holding the number */
static void draw_make_target(mq_t *q)
{
    mq_game_t *g = &q->gm;
    bool done = q->sub == GS_CHEER, el_on = g->blowing;
    int el = el_on ? imin(650, (int)(q->anim_t - g->blow_t)) : done ? 650 : 0;
    if (g->mech == ME_GATES) { /* a big gate in her lane, its doors swinging open on a right one */
        int x = MK_LX, y = FY + 25 + g->lane * 49, open = done ? imin(14, (int)(q->anim_t - g->cheer_t) / 30) : 0;
        int32_t ov[1] = {open};
        int hr = mq_hr_sprite(q, "game/gate_big", x - 31, y - 35, 62, 50, ov, 1);
        mq_fill(q, x - 26, y - 30, 5, 44, Q_BROWN), mq_fill(q, x + 21, y - 30, 5, 44, Q_BROWN);
        mq_rrect(q, x - 30, y - 34, 60, 8, 4, Q_PEACH);
        mq_fill(q, x - 21, y - 22, 20 - open, 34, RGB(214, 170, 120)), mq_fill(q, x + 1 + open, y - 22, 20 - open, 34, RGB(214, 170, 120));
        mq_fill(q, x - 1, y - 22, 2, 34, Q_BROWN);
        if (hr)
            mq_hr_end(q);
        make_sign(q, x, y - 48 + (done ? -(int)(q->anim_t / 120 % 2) * 2 : 0), done ? Q_MINT : Q_CREAM, "ENTER: dash!", 14);
    } else if (g->mech == ME_KART) { /* a boost pad: arrows that glow brighter as the code grows */
        int x = MK_LX, y = FY + 25 + g->lane * 49 + 2, glow = (int)(q->anim_t / 150 % 3);
        int32_t bv[2] = {done ? 2 : g->typed_n ? 1 : 0, glow};
        int hr = mq_hr_sprite(q, "game/boost", x - 29, y - 11, 58, 24, bv, 2);
        mq_rrect(q, x - 28, y - 10, 56, 22, 6, done ? Q_YELLOW : g->typed_n ? Q_PEACH : Q_GREY);
        for (int k = 0; k < 3; k++) {
            int ax = x - 16 + k * 12;
            uint16_t c = k == glow || done ? Q_HOTPINK : Q_WHITE;
            int16_t px[3] = {(int16_t)ax, (int16_t)(ax + 8), (int16_t)ax}, py[3] = {(int16_t)(y - 6), (int16_t)(y + 1), (int16_t)(y + 8)};
            mq_poly(q, px, py, 3, c);
        }
        if (hr)
            mq_hr_end(q);
        make_sign(q, x, y - 34, done ? Q_YELLOW : Q_WHITE, "ENTER: boost!", -28);
        if (!done && !g->typed_n)
            mq_text_c(q, q->api->font_small, x - 56, y - 40, "BOOST", Q_HOTPINK);
    } else { /* Peekaboo: the story's friend peeks out of the middle hole */
        int x = MK_ZX(q), y = MK_ZY, up = el * 26 / 650, slot = q->prob.ca % MQ_NCAST;
        int peek = (int)(q->anim_t / 700 % 3) == 0 ? 2 : 0; /* a little peek up and down */
        mq_cast(q, slot, x - 15, y - 20 - up - peek, 30, done ? AN_HAPPY : AN_IDLE);
        if (up < 20) {
            int hr = mq_hr_sprite(q, "game/mound", x - 31, y + 1, 62, 24, 0, 0);
            mq_rrect(q, x - 30, y + 2, 60, 22, 9, Q_BROWN); /* the mound in front hides the rest of them */
            if (hr)
                mq_hr_end(q);
        }
        int hr = mq_hr_sprite(q, "game/hole", x - 27, y + 3, 54, 16, 0, 0);
        mq_rrect(q, x - 26, y + 4, 52, 14, 7, RGB(140, 100, 70));
        if (hr)
            mq_hr_end(q);
        make_sign(q, x, y - 40 - up, done ? Q_MINT : Q_WHITE, "ENTER: peekaboo!", -28);
    }
}

static void draw_pick_field(mq_t *q)
{
    mq_game_t *g = &q->gm;
    const modx_api_t *api = q->api;
    int x0 = field_x0(q);
    static const uint16_t cols[4] = {Q_PINK, Q_SKY, Q_MINT, Q_LAV};
    if (mq_mod_now(q) == MOD_NIGHT) /* night: the game's own ground and road darken too (the answers glow) */
        q->tint = TINT_NIGHT;
    switch (g->mech) {
    case ME_POP:
        for (int i = 0; i < 5; i++) /* sea bubbles in the back */
            mq_ring(q, x0 + 20 + i * 44, FB - (int)((q->anim_t / 30 + (uint32_t)i * 37) % (uint32_t)FH), 3, Q_WHITE);
        break;
    case ME_CATCH:
        for (int i = 0; i < 4; i++)
            mq_circle(q, (int)(330 - (q->anim_t / 25 + (uint32_t)i * 90) % 360), FY + 20 + (i % 2) * 40, 8, Q_WHITE);
        break;
    case ME_GATES:
        for (int l = 0; l < 3; l++)
            mq_rrect(q, 0, FY + 36 + l * 49, MQ_W, 6, 3, world_bg(q, 1));
        break;
    case ME_KART: {
        int sc = (int)(g->race_d / 16); /* the road scrolls by with her speed */
        mq_fill(q, 0, FY, MQ_W, 6, RGB(150, 214, 160));
        for (int k = 0; k < 9; k++) /* flowers on the verge */
            mq_circle(q, (k * 40 + 400 - sc % 40) % 360 - 20, FY + 3, 2, k % 2 ? Q_PINK : Q_YELLOW);
        mq_fill(q, 0, FY + 6, MQ_W, FH - 10, RGB(150, 150, 170));
        mq_fill(q, 0, FB - 4, MQ_W, 4, RGB(150, 214, 160));
        for (int k = 0; k < 12; k++) /* kerb stripes */
            mq_fill(q, (k * 32 + 384 - sc % 32) % 384 - 32, FY + 6, 16, 3, k % 2 ? Q_WHITE : Q_HOTPINK);
        for (int l = 1; l < 3; l++)
            for (int k = 0; k < 8; k++)
                mq_fill(q, (k * 48 + 384 - sc % 48) % 384 - 40, FY + l * 49, 24, 3, Q_WHITE);
        for (int i = 0; i < KART_RIVALS; i++) { /* the rivals, where they are on the track */
            int rx = g->px + (int)((g->rv_d[i] - g->race_d) / 16), ry = FY + 25 + g->rv_lane[i] * 49 + 12;
            if (rx < -30 || rx > 350)
                continue;
            static const uint16_t kc[3] = {Q_ORANGE, Q_SKY, Q_LAV};
            int32_t kv[1] = {kc[i]};
            int hr = mq_hr_sprite(q, "game/kart", rx - 16, ry - 6, 32, 18, kv, 1);
            mq_rrect(q, rx - 14, ry - 4, 28, 12, 5, kc[i]);
            mq_circle(q, rx - 8, ry + 8, 3, Q_INK), mq_circle(q, rx + 9, ry + 8, 3, Q_INK);
            if (hr)
                mq_hr_end(q);
            mq_cast(q, g->rv_slot[i] % MQ_NCAST, rx - 15, ry - 28, 30, AN_IDLE | AN_STILL);
        }
        break;
    }
    case ME_ZAP:
        for (int h = 0; h < 6; h++) {
            int x, y;
            hole_xy(q, h, &x, &y);
            int hr = mq_hr_sprite(q, "game/hole", x - 27, y + 3, 54, 16, 0, 0);
            mq_rrect(q, x - 26, y + 4, 52, 14, 7, Q_BROWN);
            if (hr)
                mq_hr_end(q);
            bool busy = false; /* the key's cap only beside a friend who is out (audit P5: no stray digits) */
            for (int i = 0; i < GM_NOBJ; i++)
                busy |= g->o[i].kind && g->o[i].hp == h;
            if (busy) {
                char k[2] = {(char)("789456"[h]), 0};
                mq_rrect(q, x + 28, y + 4, 11, 12, 3, Q_WHITE);
                mq_rect(q, x + 28, y + 4, 11, 12, Q_GREY);
                mq_text_c(q, api->font_small, x + 33, y + 4, k, Q_DIM);
            }
        }
        break;
    default: break;
    }
    q->tint = 0;
    for (int pass = 0; pass < 2; pass++) /* the fun things first, the answers over them (no coin over a gate's number) */
    for (int i = 0; i < GM_NOBJ; i++) {
        const mq_gobj_t *o = &g->o[i];
        if ((o->kind == GO_ANS) != (pass == 1))
            continue;
        int x = o->x / 16, y = o->y / 16;
        if (g->mech == ME_ZAP && o->kind) {
            hole_xy(q, o->hp, &x, &y);
            int up = imin(26, (int)(uint16_t)((uint16_t)(q->anim_t / 10) - o->t) * 2);
            y = y - up + 4;
        }
        switch (o->kind) {
        case GO_PLAIN:
            if (o->hp == 2) /* golden: it glitters */
                soap(q, x, y, 12, i, RGB(240, 196, 80)), mq_star(q, x, y, 6 + (int)(q->anim_t / 120 % 2), Q_YELLOW);
            else
                soap(q, x, y, 8 + o->col % 3 * 2, i, bub_col[o->col % 6]), mq_star(q, x, y, 4, Q_YELLOW);
            break;
        case GO_STAR: mq_star(q, x, y, 8, Q_YELLOW); break;
        case GO_COIN: {
            int hr = mq_hr_sprite(q, "game/coin", x - 8, y - 8, 16, 16, 0, 0);
            mq_circle(q, x, y, 7, RGB(240, 196, 80)), mq_circle(q, x, y, 5, Q_YELLOW);
            if (hr)
                mq_hr_end(q);
            break;
        }
        case GO_ROCK: {
            int hr = mq_hr_sprite(q, "game/rock", x - 11, y - 9, 22, 18, 0, 0);
            mq_rrect(q, x - 10, y - 8, 20, 16, 6, Q_DIM), mq_fill(q, x - 4, y - 3, 3, 3, Q_GREY);
            if (hr)
                mq_hr_end(q);
            break;
        }
        case GO_POWER: {
            int32_t pv[1] = {o->opt == PW_WAND};
            int hr = mq_hr_sprite(q, "game/power", x - 12, y - 12, 24, 24, pv, 1);
            mq_circle(q, x, y, 11, Q_WHITE);
            mq_ring(q, x, y, 11, (q->anim_t / 200) % 2 ? Q_HOTPINK : Q_PURPLE);
            if (o->opt == PW_WAND)
                mq_star(q, x, y, 7, Q_LAV);
            else
                mq_circle(q, x, y, 5, Q_MINT);
            if (hr)
                mq_hr_end(q);
            break;
        }
        case GO_PAL: mq_cast(q, o->col, x - 15, y - 26, 30, AN_IDLE | AN_STILL); break;
        case GO_ANS:
            if (g->mech == ME_GATES || g->mech == ME_KART) {
                int32_t gv[1] = {(g->gone >> o->opt) & 1};
                int hr = g->mech == ME_GATES ? mq_hr_sprite(q, "game/gate", x - 25, y - 27, 50, 46, gv, 1)
                                             : mq_hr_sprite(q, "game/pad", x - 27, y - 15, 54, 30, gv, 1);
                if (g->mech == ME_GATES) { /* an arch with a sign */
                    mq_fill(q, x - 18, y - 22, 4, 40, Q_BROWN), mq_fill(q, x + 14, y - 22, 4, 40, Q_BROWN);
                    mq_rrect(q, x - 24, y - 26, 48, 10, 4, Q_PEACH);
                } else
                    mq_rrect(q, x - 26, y - 14, 52, 28, 6, (g->gone >> o->opt) & 1 ? Q_GREY : Q_YELLOW);
                if (hr)
                    mq_hr_end(q);
                draw_answer_bubble(q, x, y + 2, o->opt, cols[o->opt & 3]);
            } else if (g->mech == ME_ZAP) {
                mq_cast(q, o->col, x - 15, y - 36, 30, AN_IDLE | AN_STILL);
                mq_rrect(q, x - 26, y - 8, 52, 20, 6, (g->gone >> o->opt) & 1 ? Q_GREY : Q_WHITE);
                mq_text_c(q, api->font_small, x, y - 5, g->lab[o->opt], Q_INK);
                if (q->kbd && g->sel == o->opt)
                    mq_rect(q, x - 28, y - 10, 56, 24, Q_PURPLE);
            } else if (g->mech == ME_CATCH) { /* a falling star, its number on it (backlog (h): a star, not a bubble) */
                if ((g->gone >> o->opt) & 1)
                    draw_answer_bubble(q, x, y, o->opt, cols[o->opt & 3]); /* (a dud: wilted, as everywhere) */
                else { /* the star, its number in a white heart of it (no bubble: on the Mac the star stays a star) */
                    const font_t *f = api->text_width(api->font_medium, g->lab[o->opt]) > 30 ? api->font_small : api->font_medium;
                    int rr = imax(15, api->text_width(f, g->lab[o->opt]) / 2 + 4);
                    mq_star(q, x, y, rr + 14, Q_YELLOW);
                    mq_star(q, x, y, rr + 11, cols[o->opt & 3]);
                    mq_circle(q, x, y, rr, Q_WHITE);
                    if (q->kbd && g->sel == o->opt)
                        mq_ring(q, x, y, rr + 2, Q_PURPLE);
                    mq_text_c(q, f, x, y - (f == api->font_small ? 6 : 7), g->lab[o->opt], Q_INK);
                }
            } else { /* Bubble Pop: soap bubbles of many sizes and colours */
                const font_t *f = api->text_width(api->font_medium, g->lab[o->opt]) > 36 ? api->font_small : api->font_medium;
                int r = imax(18, api->text_width(f, g->lab[o->opt]) / 2 + 7) + o->col % 3 * 2;
                if (mq_mod_now(q) == MOD_GIANT)
                    r = r * 3 / 2;
                if ((g->gone >> o->opt) & 1)
                    draw_answer_bubble(q, x, y, o->opt, Q_GREY);
                else {
                    soap(q, x, y, r, i, bub_col[o->col % 6]);
                    if (q->kbd && g->sel == o->opt)
                        mq_ring(q, x, y, r + 3, Q_PURPLE);
                    mq_text_c(q, f, x, y - (f == api->font_small ? 6 : 7), g->lab[o->opt], Q_INK);
                }
            }
            break;
        default: break;
        }
    }
    bool hurt = q->anim_t - g->dud_t < 500;
    if (g->mech == ME_CATCH) { /* the catcher (backlog (h)): a big cloud, her buddy on it holding up a basket for the star */
        int x = g->px;
        int hr = mq_hr_sprite(q, "game/cloud", x - 32, FB - 32, 64, 30, 0, 0);
        mq_circle(q, x - 20, FB - 12, 11, Q_WHITE), mq_circle(q, x, FB - 18, 15, Q_WHITE), mq_circle(q, x + 20, FB - 12, 11, Q_WHITE);
        mq_fill(q, x - 28, FB - 12, 56, 10, Q_WHITE);
        if (hr)
            mq_hr_end(q);
        draw_player(q, x - 10, FB - 50 + (hurt ? 3 : 0), q->sub == GS_CHEER ? AN_HAPPY : AN_IDLE);
        int by = FB - 58 + (hurt ? 3 : 0); /* the basket over her head, where a star lands */
        mq_rrect(q, x - 15, by, 30, 10, 4, Q_BROWN);
        mq_fill(q, x - 13, by + 2, 26, 2, Q_PEACH), mq_fill(q, x - 13, by + 6, 26, 1, Q_PEACH);
        mq_line(q, x - 15, by + 2, x - 8, by + 12, Q_BROWN), mq_line(q, x + 15, by + 2, x + 8, by + 12, Q_BROWN);
        mq_pet_draw(q, x + 24, FB - 20, q->sub == GS_CHEER, 0); /* her pet rides along */
    } else if (g->mech == ME_GATES) {
        draw_player(q, g->px - 10, FY + 25 + g->lane * 49 - 22, g->fly ? AN_HAPPY : AN_IDLE);
        mq_pet_draw(q, g->px - 22, FY + 25 + g->lane * 49 - 2, g->fly || q->sub == GS_CHEER, 0);
    } else if (g->mech == ME_KART) {
        int y = FY + 25 + g->lane * 49;
        int32_t kv[1] = {Q_HOTPINK};
        int hr = mq_hr_sprite(q, "game/kart", g->px - 18, y - 8, 38, 23, kv, 1);
        mq_rrect(q, g->px - 16, y - 6, 34, 16, 6, Q_HOTPINK);
        mq_circle(q, g->px - 10, y + 10, 4, Q_INK), mq_circle(q, g->px + 12, y + 10, 4, Q_INK);
        if (hr)
            mq_hr_end(q);
        bool spin = g->spin_t && q->anim_t - g->spin_t < 700;
        draw_player(q, g->px - 10 + (spin ? ((q->anim_t / 60) % 2 ? 3 : -3) : 0), y - 26, spin ? AN_POUT : AN_IDLE);
        if (q->anim_t - g->boost_t < 1200 && g->boost_t) /* boost flames */
            for (int k = 0; k < 3; k++)
                mq_circle(q, g->px - 22 - k * 7, y + 2 + (k % 2) * 3, 4 - k + (int)(q->anim_t / 50 % 2), k ? Q_YELLOW : Q_ORANGE);

    } else if (g->mech == ME_POP) {
        draw_player(q, 296, FB - 24, q->sub == GS_CHEER ? AN_HAPPY : AN_IDLE);
        mq_pet_draw(q, 284, FB - 2, q->sub == GS_CHEER, 1);
    }
    if (g->make && (g->mech == ME_POP || g->mech == ME_CATCH)) { /* making the answer */
        bool pop = g->mech == ME_POP;
        int wx = pop ? 250 : 262, wy = pop ? FB - 56 : FY + 26;
        if (pop) /* the bubble wand in her hand */
            mq_line(q, 286, FB - 14, wx + 10, wy + 12, Q_BROWN), mq_line(q, 287, FB - 14, wx + 11, wy + 12, Q_BROWN);
        if (!g->blowing) {
            int grow = g->typed_n ? 4 + g->typed_n * 3 : 0, wob = (int)(q->anim_t / 160 % 2);
            if (pop) {
                mq_ring(q, wx, wy, 12, Q_HOTPINK), mq_ring(q, wx, wy, 11, Q_HOTPINK);
                if (g->typed_n)
                    soap(q, wx, wy - grow / 2, 12 + grow, 3, Q_SKY);
            } else {
                mq_star(q, wx, wy, 14 + wob, Q_YELLOW);
                if (g->typed_n)
                    mq_circle(q, wx, wy, 10 + grow / 2, Q_WHITE);
            }
            const font_t *f = api->font_medium;
            if (g->typed_n)
                mq_text_c(q, f, wx, wy - (pop ? grow / 2 : 0) - 7, g->typed, Q_INK);
            else if ((q->anim_t / 400) % 2)
                mq_text_c(q, f, wx, wy - 7, "?", pop ? Q_HOTPINK : Q_INK);
            mq_rrect(q, wx - 34, wy + (pop ? 18 : 18), 68, 14, 7, Q_WHITE);
            mq_text_c(q, api->font_small, wx, wy + (pop ? 17 : 17), g->typed_n ? (pop ? "ENTER: blow!" : "ENTER: wish!") : "type it!", Q_INK);
        } else { /* on its way */
            int el = (int)(q->anim_t - g->blow_t), x, y;
            if (pop)
                x = wx + (160 - wx) * el / 650, y = wy + (FY + 24 - wy) * el / 650 + mq_isin(el / 3) * 4 / 1024;
            else
                x = wx + (g->px - wx) * el / 650, y = wy + (FB - 34 - wy) * el / 650;
            if (pop)
                soap(q, x, y, 18, 5, Q_SKY);
            else
                mq_star(q, x, y, 14, Q_YELLOW);
            mq_text_c(q, api->font_medium, x, y - 7, g->blow_s, Q_INK);
        }
        if (!g->blowing) /* the digit tiles to make it from */
            draw_tiles(q);
    } else if (g->make && makes(g->mech)) {
        draw_make_target(q);
        if (!g->blowing)
            draw_tiles(q);
    } else if (g->typed_n) { /* what she is typing */
        mq_rrect(q, x0 + 4, FB - 20, 80, 18, 8, Q_YELLOW);
        mq_text(q, api->font_small, x0 + 10, FB - 18, g->typed, Q_INK);
    }
}

static void go_button(mq_t *q, const char *label) { (void)q, (void)label; } /* (the W_GO widget) */

static void draw_sort(mq_t *q)
{
    mq_game_t *g = &q->gm;
    const modx_api_t *api = q->api;
    const mq_prob_t *p = &q->prob;
    static const uint16_t cols[4] = {Q_PINK, Q_SKY, Q_MINT, Q_LAV};
    for (int i = 0; i < g->nopt; i++) { /* the bins (machines for "which way") */
        int bx = bin_x(g, i), w = 312 / g->nopt - 8;
        mq_rrect(q, bx - w / 2, FB - 46, w, 44, 8, (g->gone >> i) & 1 ? Q_GREY : cols[i & 3]);
        mq_fill(q, bx - w / 2 + 6, FB - 50, w - 12, 6, Q_DIM);
        if (p->mc_default == MC_WRONG_OP && p->kind == K_CHOICE) { /* "which way?": four machines */
            for (int k = 0; k < 6; k++) /* a gear on each */
                mq_circle(q, bx - 14 + mq_icos(k * 60 + (int)(q->anim_t / 8 % 360)) * 7 / 1024,
                          FB - 26 + mq_isin(k * 60 + (int)(q->anim_t / 8 % 360)) * 7 / 1024, 2, Q_DIM);
            mq_circle(q, bx - 14, FB - 26, 5, Q_GREY);
            mq_text_c(q, api->font_large, bx + 8, FB - 36, g->lab[i], Q_INK);
        } else
            mq_text_c(q, w > 90 ? api->font_medium : api->font_small, bx, FB - 32, g->lab[i], Q_INK);
        if (g->sel == i && !g->fly)
            mq_rect(q, bx - w / 2 - 2, FB - 48, w + 4, 48, Q_PURPLE);
    }
    /* the card: the picture, or the two things to compare */
    int cx = g->fx / 16, cy = g->fy / 16;
    mq_rrect(q, cx - 70, cy + 2, 140, 72, 10, Q_DIM);
    mq_rrect(q, cx - 70, cy, 140, 72, 10, Q_WHITE);
    if (p->vis != V_NONE && mq_skill[p->skill].visual_may_reveal != VR_NO && !p->vq) {
        q->vis_q = true;
        mq_visual(q, p, cx - 66, cy + 2, 132, 68, 1000);
        q->vis_q = false;
    } else {
        /* the whole order, small, wrapped on the card (audit Z2: not just its last line) */
        char all[3 * MQ_TXT + 4], l2[3][MQ_TXT];
        int k = 0;
        all[0] = 0;
        for (int i = 0; i < 3; i++)
            if (p->text[i][0])
                k += api->snprintf(all + k, sizeof all - (size_t)k, k ? " %s" : "%s", p->text[i]);
        int n = mq_story_wrap(q, all, l2, 132);
        if (n > 3) { /* (too long for the card: the line with the most numbers) */
            int best = 0, bd = -1;
            for (int i = 0; i < 3; i++) {
                int d = 0;
                for (const char *c = p->text[i]; *c; c++)
                    d += *c >= '0' && *c <= '9';
                if (p->text[i][0] && d > bd)
                    bd = d, best = i;
            }
            mq_text_c(q, api->font_small, cx, cy + 30, p->text[best], Q_INK);
        } else
            for (int i = 0; i < n && i < 3; i++)
                mq_text_c(q, api->font_small, cx, cy + 36 - n * 6 + i * 12, l2[i], Q_INK);
    }
}

static void draw_jump(mq_t *q)
{
    mq_game_t *g = &q->gm;
    const modx_api_t *api = q->api;
    const mq_prob_t *p = &q->prob;
    int ly = g->mode == 1 ? FY + 74 : FY + 86; /* (above the land! button: the line's right end, its label and the
                                                * rounding's end pads in sight) */
    if (g->mode == 1) { /* rounding: the number on its card (never on the line), the line, two landing pads */
        int32_t lo = g->opt[0], hi = g->opt[1], n = g->value, mid = (lo + hi) / 2;
        bool done = q->sub != GS_PLAY && g->given == p->ans; /* (answered right: the line shows where it really is) */
        bool line = g->rlevel < 2 || done, ends = g->rstep || done, middle = g->rstep && (g->rlevel < 2 || done);
        const char *uname = hi - lo >= 10000 ? "ten thousands" : hi - lo >= 1000 ? "thousands" : hi - lo >= 100 ? "hundreds" : "tens";
        char s[64];
        /* the card */
        api->snprintf(s, sizeof s, "%ld", (long)n);
        int cw = mq_text_w(q, api->font_large, s) + 28;
        mq_rrect(q, 160 - cw / 2, FY + 2, cw, 28, 8, Q_PEACH);
        mq_rrect(q, 160 - cw / 2 + 2, FY + 4, cw - 4, 24, 7, RGB(255, 236, 214));
        mq_text_c(q, api->font_large, 160, FY + 6, s, Q_INK);
        if (line) {
            mq_fill(q, 24, ly, 272, 3, Q_INK);
            for (int k = 0; k <= 10; k++)
                mq_fill(q, 24 + k * 272 / 10, ly - (k % 5 ? 3 : 6), 1, k % 5 ? 6 : 12, Q_INK);
        } else /* (the numbers only: just the ground between the two) */
            for (int x = 30; x < 290; x += 8)
                mq_fill(q, x, ly + 1, 4, 1, Q_GREY);
        for (int e = 0; e < 2; e++) {
            int x = e ? 296 : 24;
            mq_rrect(q, x - 26, ly + 10, 52, 22, 8, e ? Q_SKY : Q_PINK);
            api->snprintf(s, sizeof s, "%ld", (long)(e ? hi : lo));
            mq_text_c(q, api->font_small, x, ly + 14, ends ? s : "?", Q_INK);
        }
        if (middle) { /* the middle: its mark and its number */
            mq_fill(q, 159, ly - 12, 3, 24, Q_HOTPINK);
            api->snprintf(s, sizeof s, "the middle: %ld", (long)mid);
            mq_text_c(q, api->font_small, 160, ly + 14, s, Q_HOTPINK);
        }
        if (!g->rstep && g->rwob && q->anim_t - g->rwob_t < 2500) { /* a wrong pair: a hint, never the pair */
            int32_t u = hi - lo;
            api->snprintf(s, sizeof s, "%ld is %ld %s and a bit more.", (long)n, (long)(n / u), uname);
            mq_text_fit(q, api->font_small, 160, ly + 36, 300, s, Q_HOTPINK, true);
        } else if (!g->rstep) { /* the first step's question (the three pairs are its answers) */
            api->snprintf(s, sizeof s, "Which two %s is it between?", uname);
            mq_text_c(q, api->font_small, 160, ly + 36, s, Q_PURPLE);
        }
        int hx = g->aim < 0 ? 160 : g->aim ? 296 : 24;
        if (done) { /* its real spot, and why it goes that way */
            int px = 24 + (int)((n - lo) * 272 / (hi - lo > 0 ? hi - lo : 1));
            mq_rrect(q, px - 8, ly - 16, 16, 14, 3, Q_PEACH);
            mq_fill(q, px, ly - 2, 1, 4, Q_INK);
            if (n == mid)
                api->snprintf(s, sizeof s, "%ld is on the middle: the middle goes up, to %ld!", (long)n, (long)hi);
            else if (n > mid)
                api->snprintf(s, sizeof s, "%ld is past the middle: up to %ld!", (long)n, (long)hi);
            else
                api->snprintf(s, sizeof s, "%ld is before the middle: down to %ld!", (long)n, (long)lo);
            mq_text_fit(q, api->font_small, 160, ly + 36, 300, s, Q_GREEN, true);
        }
        if (g->rstep)
            draw_player(q, hx - 10, ly - 30 - (g->aim >= 0 ? 6 : 0), g->aim >= 0 ? AN_HAPPY : AN_IDLE);
        go_button(q, "land!");
        return;
    }
    int ticks = line_ticks(p), wholes = p->v[1] > 0 ? p->v[1] : 1;
    mq_fill(q, 24, ly, 272, 3, Q_INK);
    for (int k = 0; k <= ticks; k++) { /* stepping stones; only the wholes are labelled */
        int x = 24 + k * 272 / ticks;
        bool whole = p->v[2] && k % p->v[2] == 0;
        mq_circle(q, x, ly + 1, whole ? 5 : 3, whole ? Q_BROWN : Q_PEACH);
        if (whole) {
            char s[6];
            api->snprintf(s, sizeof s, "%d", k / p->v[2] + p->v[0]);
            mq_text_c(q, api->font_small, x, ly + 8, s, Q_INK);
        }
    }
    (void)wholes;
    int hx = 24 + g->aim * 272 / ticks;
    draw_player(q, hx - 10, ly - 26 - (q->anim_t / 120 % 2), q->sub == GS_CHEER ? AN_HAPPY : AN_IDLE);
    go_button(q, "land!");
}

static void draw_launch(mq_t *q)
{
    mq_game_t *g = &q->gm;
    const modx_api_t *api = q->api;
    const mq_prob_t *p = &q->prob;
    if (g->mode == 3) { /* power by fractions: the track, a slingshot */
        int ticks = line_ticks(p), ly = FB - 22;
        mq_fill(q, 24, ly, 272, 3, Q_GREEN);
        for (int k = 0; k <= ticks; k++) {
            bool whole = p->v[2] && k % p->v[2] == 0;
            mq_fill(q, 24 + k * 272 / ticks, ly - (whole ? 6 : 3), 1, whole ? 12 : 6, Q_INK);
            if (whole) {
                char s[6];
                api->snprintf(s, sizeof s, "%d", k / p->v[2] + p->v[0]);
                mq_text_c(q, api->font_small, 24 + k * 272 / ticks, ly + 4, s, Q_INK);
            }
        }
        for (int k = 0; k < ticks; k++) /* the power meter, a segment per part */
            mq_fill(q, 24 + k * 272 / ticks + 1, FY + 8, 272 / ticks - 2, 10, k < g->aim ? Q_HOTPINK : Q_GREY);
        draw_player(q, 6, ly - 30, AN_IDLE);
    } else { /* the protractor and the cannon */
        int cx = LAUNCH_CX, cy = FB - 12, r = LAUNCH_R;
        for (int a = 0; a <= 180; a += 2) {
            int x = cx + mq_icos(a) * r / 1024, y = cy - mq_isin(a) * r / 1024;
            mq_fill(q, x, y, 2, 2, Q_DIM);
        }
        for (int a = 0; a <= 180; a += 10) {
            int len = a % 30 ? 6 : 11;
            mq_line(q, cx + mq_icos(a) * r / 1024, cy - mq_isin(a) * r / 1024, cx + mq_icos(a) * (r - len) / 1024,
                    cy - mq_isin(a) * (r - len) / 1024, Q_INK);
            if (a % 30 == 0) {
                char s[6];
                api->snprintf(s, sizeof s, "%d", a);
                mq_text_c(q, api->font_small, cx + mq_icos(a) * (r - 22) / 1024, cy - mq_isin(a) * (r - 22) / 1024 - 6 - (a % 180 ? 0 : 8), s, Q_INK);
            }
        }
        mq_fill(q, cx - r, cy, 2 * r, 2, Q_INK);
        int bx = cx + mq_icos(g->aim) * 46 / 1024, by = cy - mq_isin(g->aim) * 46 / 1024;
        for (int k = -2; k <= 2; k++)
            mq_line(q, cx + k, cy, bx + k, by, Q_HOTPINK);
        mq_circle(q, cx, cy, 9, Q_PURPLE);
        if (g->mode == 1) {
            char s[8];
            api->snprintf(s, sizeof s, "%d\xC2\xB0", g->aim);
            mq_rrect(q, 6, FB - 26, 46, 20, 8, Q_WHITE);
            mq_text_c(q, api->font_medium, 29, FB - 24, s, Q_INK);
        }
    }
    if (g->fly)
        mq_star(q, g->fx / 16, g->fy / 16, 7, Q_YELLOW);
    go_button(q, "fling!");
}

static void draw_bake(mq_t *q)
{
    mq_prob_t vis = q->prob;
    vis.vis = V_PIE;
    mq_rrect(q, 56, FY + 4, 128, 136, 12, Q_WHITE);
    mq_visual(q, &vis, 58, FY + 8, 124, 128, 1000);
    mq_sprite_anim(q, 2, 220, FY + 40, 3, q->sub == GS_CHEER ? AN_HAPPY : AN_IDLE);
    go_button(q, "serve!");
}

static void draw_build(mq_t *q)
{
    mq_game_t *g = &q->gm;
    const modx_api_t *api = q->api;
    int cell = mq_build_cell(g), ox = 20, oy = FY + 10;
    for (int r = 0; r <= g->maxh; r++) /* the plot */
        for (int c = 0; c <= g->maxw; c++)
            mq_fill(q, ox + c * cell, oy + r * cell, 1, 1, Q_DIM);
    int w = g->bw, h = g->bh;
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++) {
            int x = ox + c * cell, y = oy + r * cell;
            if (g->mode == BM_BED) {
                mq_fill(q, x + 1, y + 1, cell - 1, cell - 1, RGB(206, 172, 130));
                mq_circle(q, x + cell / 2, y + cell / 2, cell / 4, (r + c) % 2 ? Q_PINK : Q_YELLOW);
            } else if (g->mode == BM_FENCE || g->mode == BM_SIDE_PERIM)
                mq_fill(q, x + 1, y + 1, cell - 1, cell - 1, Q_MINT);
            else
                mq_fill(q, x + 1, y + 1, cell - 1, cell - 1, (r + c) % 2 ? Q_PINK : Q_LAV);
        }
    uint16_t edge = g->mode == BM_FENCE || g->mode == BM_SIDE_PERIM ? Q_BROWN : Q_HOTPINK;
    for (int t = 0; t < 2; t++)
        mq_rect(q, ox - t, oy - t, w * cell + 1 + 2 * t, h * cell + 1 + 2 * t, edge);
    if (g->mode == BM_FENCE || g->mode == BM_SIDE_PERIM) /* posts */
        for (int c = 0; c <= w; c++)
            for (int r = 0; r <= h; r++)
                if (c == 0 || r == 0 || c == w || r == h)
                    mq_fill(q, ox + c * cell - 1, oy + r * cell - 1, 3, 3, Q_BROWN);
    /* the handle to drag */
    int hx = ox + w * cell, hy = oy + h * cell;
    mq_circle(q, hx, hy, 7, Q_PURPLE);
    mq_circle(q, hx, hy, 4, Q_WHITE);
    if (g->mode == BM_BED) { /* the order's numbers on a garden sign (never the count built so far) */
        char t[24];
        mq_rrect(q, 236, FY + 10, 80, 44, 6, Q_CREAM);
        mq_rect(q, 236, FY + 10, 80, 44, Q_BROWN);
        mq_fill(q, 274, FY + 54, 4, 14, Q_BROWN);
        api->snprintf(t, sizeof t, "%d flowers", g->fixh * (int)q->prob.ans);
        mq_text_c(q, api->font_small, 276, FY + 15, t, Q_INK);
        api->snprintf(t, sizeof t, "%d rows", g->fixh);
        mq_text_c(q, api->font_small, 276, FY + 30, t, Q_INK);
    }
    go_button(q, "done!");
}

static void draw_clock(mq_t *q)
{
    mq_game_t *g = &q->gm;
    const modx_api_t *api = q->api;
    int cx = 110, cy = FY + 74, r = 66;
    int32_t cv[4] = {r, 0, 0, 0};
    int hr = mq_hr_sprite(q, "game/clock_face", cx - r - 5, cy - r - 5, 2 * r + 10, 2 * r + 10, cv, 1);
    mq_circle(q, cx, cy, r + 4, Q_PURPLE);
    mq_circle(q, cx, cy, r, Q_WHITE);
    if (hr)
        mq_hr_end(q);
    for (int k = 1; k <= 12; k++) {
        char s[4];
        api->snprintf(s, sizeof s, "%d", k);
        mq_text_c(q, api->font_small, cx + mq_isin(k * 30) * (r - 12) / 1024, cy - mq_icos(k * 30) * (r - 12) / 1024 - 6, s, Q_INK);
    }
    hr = mq_hr_sprite(q, "game/clock_ticks", cx - r - 5, cy - r - 5, 2 * r + 10, 2 * r + 10, cv, 1);
    for (int k = 0; k < 60; k++)
        mq_fill(q, cx + mq_isin(k * 6) * (r - 3) / 1024, cy - mq_icos(k * 6) * (r - 3) / 1024, k % 5 ? 1 : 2, k % 5 ? 1 : 2, Q_DIM);
    if (hr)
        mq_hr_end(q);
    int h = g->value / 100 % 12, m = g->value % 100;
    int ha = h * 30 + m / 2, ma = m * 6;
    cv[1] = ha, cv[2] = ma, cv[3] = 1;
    hr = mq_hr_sprite(q, "game/clock_hands", cx - r, cy - r, 2 * r, 2 * r, cv, 4);
    for (int k = -1; k <= 1; k++) {
        mq_line(q, cx + k, cy, cx + mq_isin(ha) * 34 / 1024 + k, cy - mq_icos(ha) * 34 / 1024, Q_INK);
        mq_line(q, cx, cy + k, cx + mq_isin(ma) * 54 / 1024, cy - mq_icos(ma) * 54 / 1024 + k, Q_HOTPINK);
    }
    mq_circle(q, cx, cy, 4, Q_INK);
    mq_circle(q, cx + mq_isin(ma) * 54 / 1024, cy - mq_icos(ma) * 54 / 1024, 5, Q_HOTPINK); /* grab me */
    if (hr)
        mq_hr_end(q);
    mq_sprite_anim(q, q->prob.ca % MQ_NCAST, 214, FY + 40, 3, q->sub == GS_CHEER ? AN_DANCE : q->gm.mood == 1 ? AN_POUT : AN_IDLE);
    go_button(q, "set!");
}

static void draw_till(mq_t *q)
{
    mq_game_t *g = &q->gm;
    const modx_api_t *api = q->api;
    static const char den[5][6] = {"$1", "25c", "10c", "5c", "1c"};
    static const uint16_t dc[5] = {Q_MINT, RGB(210, 210, 220), RGB(210, 210, 220), RGB(200, 200, 210), RGB(220, 160, 110)};
    bool pets = g->mech == ME_PETS;
    mq_rrect(q, 2, FY + 2, 200, FH - 42, 8, pets ? RGB(232, 248, 240) : RGB(250, 240, 220));
    if (pets && g->mode == 0) { /* the pet shop's counter (paw prints) and a shelf of pet things */
        for (int k = 0; k < 5; k++)
            mq_circle(q, 30 + k * 36, FY + FH - 52, 3, RGB(200, 230, 214));
        int sy = FY + FH - 70;
        mq_fill(q, 120, sy, 78, 3, RGB(214, 170, 130));
        mq_rrect(q, 126, sy - 8, 12, 5, 2, Q_WHITE), mq_circle(q, 126, sy - 6, 3, Q_WHITE), mq_circle(q, 138, sy - 6, 3, Q_WHITE);
        mq_oval(q, 154, sy - 6, 7, 4, Q_SKY), mq_fill(q, 160, sy - 9, 3, 6, Q_SKY);
        mq_circle(q, 174, sy - 6, 5, Q_HOTPINK), mq_fill(q, 170, sy - 7, 8, 1, Q_WHITE);
        mq_circle(q, 190, sy - 6, 5, Q_LAV), mq_line(q, 186, sy - 9, 194, sy - 3, Q_WHITE);
    }
    if (g->mode == 1) { /* her coins, to count */
        mq_prob_t vis = q->prob;
        mq_visual(q, &vis, 6, FY + 6, 192, FH - 50, 1000);
    } else if (g->mode == 2) { /* the change, worked out: the drawer stays shut, the keys do it */
        mq_rrect(q, 20, FY + 30, 160, 60, 10, RGB(240, 214, 170));
        mq_fill(q, 30, FY + 56, 140, 3, RGB(214, 170, 120));
        mq_rrect(q, 84, FY + 62, 32, 8, 4, Q_BROWN);
        mq_sprite_anim(q, q->prob.ca % MQ_NCAST, 70, FY + 94, 2, AN_IDLE);
        mq_text_c(q, api->font_small, 100, FY + 12, "Type the change!", Q_INK);
    } else
        for (int i = 0; i < 5; i++)
            for (int k = 0; k < g->tray[i]; k++) {
                int x = 12 + k * 20, y = FY + 14 + i * 22;
                if (i == 0)
                    mq_rrect(q, x - 8, y - 6, 18, 12, 2, dc[0]);
                else
                    mq_circle(q, x, y, i == 1 ? 8 : i == 4 ? 6 : 7, dc[i]);
            }
    /* the till: its display and keys (in the pet shop: a paw-print till, the customer and their pet) */
    int32_t tv[1] = {pets};
    int hr = mq_hr_sprite(q, "game/till", 207, FY + 1, 110, FH - 40, tv, 1);
    mq_rrect(q, 208, FY + 2, 108, FH - 42, 8, pets ? Q_MINT : Q_PEACH);
    if (hr)
        mq_hr_end(q);
    if (pets && g->mode == 0) {
        int pid = mq_pet_id(q) ? mq_pet_id(q) : 1, hop = q->sub == GS_CHEER ? (int)(q->anim_t / 100 % 3) * 2 : 0;
        mq_cast(q, q->prob.ca % MQ_NCAST, 222, FY + 36, 40, q->sub == GS_CHEER ? AN_HAPPY : AN_IDLE);
        mq_item_draw(q, pid, 266, FY + 54 - hop, 0, (int)(q->anim_t / 400 % 2), 1, -1);
        if (q->sub == GS_CHEER) /* a treat for the pet, hearts */
            mq_heart(q, 290, FY + 46 - (int)(q->anim_t / 120 % 6), 4, Q_HOTPINK), mq_circle(q, 276, FY + 74, 3, RGB(214, 160, 100));
    }
    mq_rrect(q, 214, FY + 8, g->mode ? 62 : 96, 22, 6, Q_INK); /* (typing: '<' beside it) */
    char s[16];
    int32_t v = 0;
    if (g->typed_n)
        for (int i = 0; i < g->typed_n; i++)
            v = v * 10 + g->typed[i] - '0';
    else
        v = g->mode >= 1 ? 0 : till_total(g);
    api->snprintf(s, sizeof s, "$%ld.%02ld", (long)(v / 100), (long)(v % 100));
    mq_text_fit(q, api->font_medium, g->mode ? 245 : 262, FY + 10, g->mode ? 58 : 92, s, Q_MINT, true);
    if (g->mode >= 1)
        ; /* (the till's keys: widgets) */
    else
        for (int i = 0; i < 5; i++) { /* the coin drawer */
            int x = DRAWER_X(i);
            mq_rrect(q, x, FB - 34, DRAWER_W, 30, 8, dc[i]);
            mq_text_c(q, api->font_small, x + DRAWER_W / 2, FB - 26, den[i], Q_INK);
        }
    go_button(q, "ring!");
}

static void draw_dig(mq_t *q)
{
    mq_game_t *g = &q->gm;
    const modx_api_t *api = q->api;
    int ox = 30, oy = FB - 20; /* (10 columns of DIG_DX: the x axis and its 10 end left of the dig! button) */
    mq_fill(q, 2, FY + 2, 316, FH - 4, RGB(240, 226, 190));
    for (int x = 0; x <= 10; x++)
        mq_fill(q, ox + x * DIG_DX, oy - 7 * 18, 1, 7 * 18, Q_PEACH);
    for (int y = 0; y <= 7; y++)
        mq_fill(q, ox, oy - y * 18, 10 * DIG_DX, 1, Q_PEACH);
    mq_fill(q, ox, oy, 10 * DIG_DX + 1, 2, Q_INK), mq_fill(q, ox, oy - 7 * 18, 2, 7 * 18, Q_INK);
    int x = ox + g->px * DIG_DX, y = oy - g->py * 18;
    mq_ring(q, x, y, 7, Q_HOTPINK);
    draw_player(q, x - 10, y - 26, AN_IDLE);
    for (int k = 0; k <= 10; k++) { /* the axes' numbers over everything on the map: always in sight (the render
                                    * audit's M11) */
        char s[4];
        api->snprintf(s, sizeof s, "%d", k);
        mq_text_c(q, api->font_small, ox + k * DIG_DX, oy + 3, s, Q_INK);
        if (k <= 7)
            mq_text(q, api->font_small, ox - 19, oy - k * 18 - 6, s, Q_INK); /* (left of the digger at x 0) */
    }
    go_button(q, "dig!");
}

/* Treasure Heist: a treasure room in a cave -- a painting, a pot and a barrel to search for the
 * story's numbers (clue notes), the chest with its combination lock; the right combination opens it
 * in a burst of treasure, a wrong one rattles the lock */
static void draw_vault(mq_t *q)
{
    mq_game_t *g = &q->gm;
    const modx_api_t *api = q->api;
    uint32_t t = q->anim_t;
    /* the cave: stone, a rug, a torch */
    if (mq_mod_now(q) == MOD_NIGHT) /* night: the cave and the chest darken (the lock's wheels glow) */
        q->tint = TINT_NIGHT;
    mq_fill(q, 0, FY, MQ_W, FH, RGB(120, 104, 140));
    for (int i = 0; i < 8; i++)
        mq_oval(q, 20 + i * 42, FY + 6 + (i % 3) * 9, 18, 8, RGB(134, 118, 156));
    mq_fill(q, 0, FB - 30, MQ_W, 30, RGB(150, 120, 110));
    mq_oval(q, 160, FB - 16, 100, 10, RGB(200, 90, 110));
    mq_oval(q, 160, FB - 16, 86, 7, RGB(236, 170, 90));
    int fl = (int)(t / 90 % 3);
    mq_fill(q, 238, FY + 20, 4, 18, Q_BROWN), mq_circle(q, 240, FY + 16 - fl, 5, Q_ORANGE), mq_circle(q, 240, FY + 15 - fl, 3, Q_YELLOW);
    /* the hiding spots, and the clues found in them */
    int32_t cn[3] = {0, 0, 0};
    int nc = clue_nums(&q->prob, cn);
    for (int s = 0; s < 3; s++) {
        int x = spot[s][0], y = spot[s][1], w = spot[s][2], h = spot[s][3];
        bool found = (g->clues >> s) & 1;
        int wig = !found && (t / 150 + (uint32_t)s * 4) % 20 == 0 ? 1 : 0;
        if (s == 0) { /* a painting of a star */
            mq_rrect(q, x + wig, y, w, h, 3, RGB(214, 170, 90)), mq_fill(q, x + 4 + wig, y + 4, w - 8, h - 8, Q_SKY);
            mq_star(q, x + w / 2 + wig, y + h / 2, 8, Q_YELLOW);
        } else if (s == 1) { /* a pot */
            mq_rrect(q, x + 4 + wig, y + 10, w - 8, h - 10, 8, RGB(222, 130, 90)), mq_fill(q, x + 2 + wig, y + 6, w - 4, 6, RGB(236, 150, 110));
        } else { /* a barrel */
            mq_rrect(q, x + wig, y + 6, w, h - 6, 8, Q_BROWN);
            mq_fill(q, x + wig, y + 14, w, 3, RGB(140, 100, 70)), mq_fill(q, x + wig, y + h - 12, w, 3, RGB(140, 100, 70));
        }
        if (!found && (t / 400) % 2) /* a little sparkle: something is hidden here */
            mq_star(q, x + w - 4, y + 2, 3, Q_WHITE);
        if (found && s < nc) { /* the clue note */
            char b[12];
            api->snprintf(b, sizeof b, "%ld", (long)cn[s]);
            int nw = mq_text_w(q, api->font_medium, b) + 14, nx = x + w / 2 - nw / 2 + (s == 2 ? -12 : 10), ny = y + (s == 0 ? 20 : -12);
            nx = imax(2, imin(nx, MQ_W - nw - 2)); /* (on the screen, a long number too) */
            mq_rrect(q, nx, ny, nw, 18, 4, Q_CREAM), mq_rect(q, nx, ny, nw, 18, RGB(214, 170, 90));
            mq_text_c(q, api->font_medium, nx + nw / 2, ny + 1, b, Q_INK);
        } else if (found)
            mq_text_c(q, api->font_small, x + w / 2, y - 12, "empty!", Q_WHITE);
    }
    /* the chest: it opens on the right combination */
    int open = q->sub == GS_CHEER ? imin(24, (int)(t - g->cheer_t) / 12) : 0;
    int shake = g->rattle_t && t - g->rattle_t < 400 ? ((t / 40) % 2 ? 2 : -2) : 0;
    int cx = 160 + shake;
    mq_rrect(q, cx - 76, FY + 60, 152, 52, 8, RGB(176, 120, 80));
    mq_fill(q, cx - 76, FY + 70, 152, 3, RGB(240, 196, 80));
    mq_rrect(q, cx - 78, FY + 38 - open, 156, 24, 10, RGB(196, 140, 96)); /* the lid */
    mq_fill(q, cx - 78, FY + 48 - open, 156, 3, RGB(240, 196, 80));
    if (open) { /* treasure! */
        static const uint16_t gem[4] = {Q_HOTPINK, Q_SKY, Q_MINT, Q_LAV};
        for (int i = 0; i < 9; i++)
            mq_circle(q, cx - 48 + i * 12, FY + 60 - (i % 3) * 4, 5, i % 2 ? RGB(240, 196, 80) : gem[i % 4]);
        for (int i = 0; i < 6; i++) {
            int a = i * 60 + (int)(t / 6 % 60), r = 20 + open * 2;
            mq_star(q, cx + mq_icos(a) * r / 1024, FY + 48 + mq_isin(a) * r / 2048 - open, 4, Q_YELLOW);
        }
    }
    q->tint = 0;
    /* the lock: a plate, the fixed plates ('.', R, $, /) between the wheels (the wheels are widgets) */
    int n = g->lock_n, pw = lock_pitch(n);
    mq_rrect(q, lock_wx(n, 0) - 4 + shake, WHEEL_Y - 6, n * pw + 8, WHEEL_H + 12, 8, RGB(90, 80, 110));
    for (int i = 0; i < n; i++)
        if (!(g->lock[i] >= '0' && g->lock[i] <= '9')) {
            char ch[2] = {g->lock[i], 0};
            mq_text_c(q, api->font_large, lock_wx(n, i) + pw / 2 + shake, WHEEL_Y + WHEEL_H / 2 - 10, ch, Q_YELLOW);
        }
    q->buddy_anim = q->sub == GS_CHEER ? AN_HAPPY : AN_IDLE; /* her buddy, the treasure hunter */
    mq_buddy_draw(q, mq_prof(q)->buddy % MQ_NCAST, 8, FB - 44, 2, 1);
    q->buddy_anim = 0;
    if (g->given >= 0 && q->tried_wrong && q->sub == GS_PLAY) { /* what she tried stays on the wheels; said too */
        char tr[24], v[16];
        mq_format(q, &q->prob, g->given, v, sizeof v);
        api->snprintf(tr, sizeof tr, "You tried %s", v);
        int tw = mq_text_w(q, api->font_small, tr) + 12;
        mq_rrect(q, 160 - tw / 2, FB - 24, tw, 16, 8, Q_WHITE);
        mq_text_c(q, api->font_small, 160, FB - 23, tr, Q_HOTPINK);
    }
}

/* Cake Kitchen: a big bowl, the story's thing going in on the beat, the recipe card with the amount
 * typed, the keys; the oven behind; right answers stack the cake higher (the combo) */
static void draw_cook(mq_t *q)
{
    mq_game_t *g = &q->gm;
    const modx_api_t *api = q->api;
    uint32_t bm = mq_beat_ms(0), el = q->anim_t - g->beat0, ph = el % bm;
    int pulse = ph < 120 ? 2 : 0;
    /* the counter and the oven */
    mq_fill(q, 0, FB - 66, MQ_W, 5, RGB(214, 170, 130)); /* (the keys below it: widgets) */
    int32_t ov[1] = {q->sub == GS_CHEER};
    int hr = mq_hr_sprite(q, "game/oven", 253, FY + 5, 64, 72, ov, 1);
    mq_rrect(q, 254, FY + 6, 62, 70, 10, RGB(255, 214, 190));
    mq_rrect(q, 260, FY + 26, 50, 42, 8, q->sub == GS_CHEER ? RGB(255, 200, 120) : RGB(120, 90, 90));
    mq_circle(q, 266, FY + 14, 3, Q_HOTPINK), mq_circle(q, 278, FY + 14, 3, Q_YELLOW);
    if (hr)
        mq_hr_end(q);
    /* the cake so far: a layer for every order in a row */
    int layers = q->combo > 6 ? 6 : q->combo;
    int32_t lv[1] = {layers};
    hr = layers ? mq_hr_sprite(q, "game/cake", 194, FB - 75 - 6 * 9 - 10, 72, 6 * 9 + 21, lv, 1) : 0;
    for (int i = 0; i < layers; i++)
        mq_rrect(q, 210 - i * 2, FB - 75 - i * 9, 40 + i * 4 - i * 8 + 8, 9, 3, i % 2 ? Q_PINK : Q_CREAM);
    if (layers)
        mq_circle(q, 224, FB - 79 - layers * 9, 3, RGB(236, 70, 80));
    if (hr)
        mq_hr_end(q);
    /* the bowl, the whisk, the thing going in on the beat */
    int bx = 58, by = FB - 86;
    int32_t pv[1] = {pulse};
    hr = mq_hr_sprite(q, "game/bowl", bx - 54, by - 14, 108, 36, pv, 1);
    mq_oval(q, bx, by + 2, 50 + pulse, 18, RGB(206, 236, 250));
    mq_oval(q, bx, by - 4, 46 + pulse, 8, RGB(255, 246, 236));
    if (hr)
        mq_hr_end(q);
    int wa = (int)(el / 4 % 360), wx = bx + mq_icos(wa) * 18 / 1024;
    mq_line(q, wx, by - 6, wx + 20, by - 40, Q_DIM), mq_ring(q, wx, by - 4, 5, Q_GREY);
    if (q->prob.stg_i < SI_COUNT) {
        int dy = (int)(ph * 40 / bm);
        mq_stage_item(q, q->prob.stg_i, bx - 24, by - 46 + dy, 0);
    }
    for (int k = 0; k < 4; k++) /* the beat: four dots, the one now lit */
        mq_circle(q, 40 + k * 26, FY + 10, k == (int)(el / bm % 4) ? 5 : 3, k == (int)(el / bm % 4) ? Q_HOTPINK : Q_GREY);
    if (g->mix_t && q->anim_t - g->mix_t < 500) { /* the mix: a swirl, "perfect" on the beat */
        int e2 = (int)(q->anim_t - g->mix_t);
        mq_ring(q, bx, by - 4, 20 + e2 / 10, g->mix_j == 2 ? Q_YELLOW : Q_LAV);
    }
    /* the recipe card: how many (picked from the amounts below) */
    mq_rrect(q, 120, FY + 18, 90, 40, 8, Q_WHITE);
    mq_rect(q, 120, FY + 18, 90, 40, Q_PINK);
    mq_text_c(q, api->font_small, 165, FY + 20, "how many?", Q_DIM);
    mq_text_c(q, api->font_large, 165, FY + 34, g->typed_n ? g->typed : ((q->anim_t / 400) % 2 ? "?" : ""), Q_HOTPINK);
    /* (the amounts: widgets) */
    draw_player(q, 284, FY + 84, q->sub == GS_CHEER ? AN_HAPPY : AN_IDLE);
}

/* the why: the right answer shown in the game, with the picture */
static void draw_why(mq_t *q)
{
    mq_game_t *g = &q->gm;
    const modx_api_t *api = q->api;
    mq_prob_t vis = q->prob;
    if (vis.kind == K_SHADE)
        vis.v[5] = (int16_t)((1 << vis.ans) - 1), vis.v[4] = -1;
    if (vis.kind == K_LINE)
        vis.v[3] = (int16_t)vis.ans, vis.v[5] = -1;
    mq_rrect(q, 8, FY + 6, 304, FH - 12, 12, Q_WHITE);
    mq_rect(q, 8, FY + 6, 304, FH - 12, Q_LAV);
    if (vis.vis != V_NONE)
        mq_visual(q, &vis, 12, FY + 10, 180, FH - 20, (int)(q->anim_t - g->trick_t) * 2);
    char a[24];
    mq_format(q, &q->prob, q->prob.ans, a, sizeof a);
    if (g->mech == ME_ZAP && g->mode == 1) /* "find 24": the right trays */
        api->snprintf(a, sizeof a, "%s", q->prob.choice[q->prob.ans]);
    else if (g->mech == ME_BUILD && g->mode == BM_BED) /* rows first, as the story says them */
        api->snprintf(a, sizeof a, "%d rows of %d", g->fixh, (int)q->prob.ans);
    else if (g->mech == ME_BUILD)
        api->snprintf(a, sizeof a, "%d by %d", g->fixh ? (int)q->prob.ans : g->fixw, g->fixh ? g->fixh : (int)q->prob.ans);
    mq_text_c(q, api->font_small, 252, FY + 20, "It's", Q_INK);
    mq_text_c(q, api->font_large, 252, FY + 36, a, Q_GREEN);
    mq_sprite_anim(q, q->prob.ca % MQ_NCAST, 232, FY + 76, 2, AN_THINK);
    mq_text_c(q, api->font_small, 252, FY + 122, "Next one!", Q_DIM);
}

/* the trick: the guide's worked example, played as a little show in the round */
static void draw_trick(mq_t *q)
{
    const modx_api_t *api = q->api;
    char out[3][MQ_TXT];
    mq_prob_t vis;
    q->ex_n = (uint8_t)mq_steps(q, &q->ex, q->ex_step, out, &vis);
    if (vis.kind == K_SHADE)
        vis.v[5] = (int16_t)((1 << vis.ans) - 1), vis.v[4] = -1;
    if (vis.kind == K_LINE)
        vis.v[3] = (int16_t)(q->ex_step + 1 >= q->ex_n ? vis.ans : -1), vis.v[5] = -1;
    mq_rrect(q, 4, FY + 2, 312, FH - 4, 12, Q_CREAM);
    int stage = (int)(q->anim_t - q->gm.trick_t) * 2;
    if (q->ex_step + 1 < q->ex_n)
        stage = stage * (q->ex_step + 1) / q->ex_n;
    mq_visual(q, &vis, 8, FY + 4, 200, 92, stage);
    int g = mq_skill[q->ex.skill].world >= G_SKY ? 8 : mq_skill[q->ex.skill].world;
    mq_sprite_anim(q, g, 236, FY + 18, 3, AN_HAPPY);
    mq_rrect(q, 8, FY + 98, 304, 44, 8, Q_WHITE);
    for (int i = 0; i < 3; i++)
        mq_text(q, api->font_small, 14, FY + 100 + i * 13, out[i], Q_INK);
    for (int i = 0; i < q->ex_n; i++)
        mq_circle(q, 240 + i * 10, FY + 88, 3, i <= q->ex_step ? Q_HOTPINK : Q_GREY);
}

static void draw_offer(mq_t *q)
{
    const modx_api_t *api = q->api;
    mq_rrect(q, 30, FY + 20, 260, 100, 14, Q_WHITE);
    int g = mq_skill[q->prob.skill].world >= G_SKY ? 8 : mq_skill[q->prob.skill].world;
    mq_sprite_anim(q, g, 40, FY + 30, 2, AN_HAPPY);
    mq_text(q, api->font_medium, 86, FY + 32, "That one is tricky!", Q_INK);
    api->snprintf(q->tmp, sizeof q->tmp, "Want to see %s's trick?", mq_cast_name(q, g));
    mq_text(q, api->font_small, 86, FY + 52, q->tmp, Q_INK);
    mq_button(q, 46, FY + 80, 110, 32, B_YES, 0, "yes please", Q_LAV);
    mq_button(q, 166, FY + 80, 110, 32, B_NO, 0, "I'll try", Q_MINT);
}

/* the finale's grumble: a grumpy cloud who cheers up with every order done */
static void draw_grumble(mq_t *q)
{
    mq_game_t *g = &q->gm;
    if (!g->boss)
        return;
    int x = 292, y = MQ_Y0 + 38 - 18, happy = g->grump_max ? (g->grump_max - g->grump) * 4 / g->grump_max : 4;
    (void)y;
    int bx = 296, by = FY + 14;
    mq_circle(q, bx - 9, by, 8, happy >= 4 ? Q_WHITE : RGB(170, 170, 190));
    mq_circle(q, bx + 4, by - 4, 10, happy >= 4 ? Q_WHITE : RGB(170, 170, 190));
    mq_circle(q, bx + 12, by + 2, 7, happy >= 4 ? Q_WHITE : RGB(170, 170, 190));
    mq_fill(q, bx - 4, by - 3, 2, 2, Q_INK), mq_fill(q, bx + 6, by - 3, 2, 2, Q_INK);
    if (happy >= 3)
        mq_fill(q, bx - 2, by + 3, 8, 2, Q_HOTPINK), mq_fill(q, bx - 3, by + 2, 1, 1, Q_HOTPINK), mq_fill(q, bx + 6, by + 2, 1, 1, Q_HOTPINK);
    else
        mq_fill(q, bx - 2, by + 4, 8, 1, Q_INK), mq_fill(q, bx - 3, by + 5, 1, 1, Q_INK), mq_fill(q, bx + 6, by + 5, 1, 1, Q_INK);
    for (int i = 0; i < g->grump_max && i < 10; i++) /* its mood meter */
        mq_fill(q, bx - 14 + i * 3, by + 12, 2, 3, i < g->grump_max - g->grump ? Q_HOTPINK : Q_GREY);
    (void)x;
}

void mq_game_draw(mq_t *q)
{
    mq_game_t *g = &q->gm;
    q->frame_units = 0;
    if (q->sk_power != g->power[0]) /* a power-up came or went: its button */
        mq_softkeys_update(q);
    /* dirty rectangles: the top (title line and order board) is drawn only when it changed */
    uint32_t key = (uint32_t)g->scene_on * 524287u + (uint32_t)q->sub * 7919u + g->mech * 131u + (uint32_t)q->run_i * 31u + g->stars * 17u +
                   (uint32_t)q->combo * 3u + g->mood + g->power[0] * 5u + g->power[1] * 11u + (uint32_t)(g->say[0] * 13) +
                   (uint32_t)(q->anim_t / 160) * (g->mood != 0 || g->boss || q->sub != GS_PLAY ? 1u : 0u) +
                   (uint32_t)(q->anim_t / 420) + (q->anim_t - g->cheer_t < 300 ? 1u : 0u) * 977u +
                   (g->full_t && q->anim_t - g->full_t < 6000 ? 8191u : 0u) + (q->prob.stg ? (uint32_t)(q->anim_t / 140) * 3u : 0u) +
                   g->place * 65537u +
                   hud_now(q) * 104729u + fx_now(q) * 7907u +
                   (q->anim_t - g->order_t < 1300u || (g->poke_t && q->anim_t - g->poke_t < 760u) ||
                    (q->say_t && q->anim_t - q->say_t < 1400u) ? (uint32_t)(q->anim_t / 40) * 7u : 0u); /* (the friend moving, talking) */
    bool shake = q->anim_t - g->shake_t < 240 && g->shake_t;
    if (mq_diag_on(q) && q->sub == GS_PLAY) { /* the friend shows where her answer went wrong */
        draw_hud(q);
        g->hud_ok = 0;
        q->gm_top = 1;
        mq_diag_draw(q);
        return;
    }
    if (mq_teach_on(q) && q->sub == GS_PLAY) { /* a teaching moment */
        draw_hud(q);
        g->hud_ok = 0;
        q->gm_top = 1;
        mq_teach_draw(q);
        return;
    }
    if (mq_strat_on(q) && q->sub == GS_PLAY) { /* a way to work it, hands on */
        draw_hud(q);
        g->hud_ok = 0;
        q->gm_top = 1;
        mq_strat_draw(q);
        return;
    }
    if (mq_cols_on(q) && q->sub == GS_PLAY) { /* the friend's notebook */
        draw_hud(q);
        g->hud_ok = 0;
        q->gm_top = 1;
        mq_cols_draw(q);
        return;
    }
    if (!g->hud_ok || key != g->hud_key || q->redraw || shake || q->ox || q->oy) {
        draw_hud(q);
        draw_order(q);
        g->hud_ok = 1, g->hud_key = key;
        q->gm_top = 1;
    } else
        q->gm_top = 0;
    if (shake) /* a little screen shake on big moments */
        q->ox = (int16_t)((q->anim_t / 40) % 2 ? 2 : -2);
    q->clip_y0 = FY, q->clip_y1 = FB; /* the field */
    int md = g->scene_on ? (g->mod == MOD_NIGHT ? MOD_NIGHT : 0) : mq_mod_now(q);
    if (md == MOD_NIGHT)
        q->tint = TINT_NIGHT;
    mq_backdrop(q, q->world < MQ_NWORLD ? q->world : 0, FY, FB); /* the place the game is played in */
    q->tint = 0;
    if (md == MOD_NIGHT) { /* the moon, stars, fireflies: the answers glow */
        mq_circle(q, 288, FY + 22, 12, RGB(255, 246, 200)), mq_circle(q, 294, FY + 18, 10, mq_tint(q, RGB(80, 90, 150)));
        for (int i = 0; i < 9; i++)
            if ((q->anim_t / 300 + (uint32_t)i * 5) % 4)
                mq_star(q, 20 + i * 31, FY + 8 + (i * 23) % 40, 2, Q_WHITE);
        for (int i = 0; i < 6; i++) {
            int fx = (int)((q->anim_t / 20 + (uint32_t)i * 97) % 340) - 10, fy = FB - 20 - (i * 17) % 60 + mq_isin((int)(q->anim_t / 6 + (uint32_t)i * 60) % 360) * 6 / 1024;
            if ((q->anim_t / 200 + (uint32_t)i) % 3)
                mq_circle(q, fx, fy, 2, RGB(255, 250, 150));
        }
    }
    mq_events_field(q); /* today's special over the field: snow, kites, a rainbow, shooting stars */
    draw_grumble(q);
    if (g->scene_on && q->sub == GS_PLAY) {
        mq_stage_scene(q, &q->prob, mq_scene_el((int)(q->anim_t - g->scene_t), g->scene_ms), FY, FB, false);
    } else if (q->sub == GS_WHY) {
        draw_why(q);
    } else if (q->sub == GS_TRICK) {
        draw_trick(q);
    } else if (q->sub == GS_HELP) {
        mq_helper_draw(q);
    } else if (g->bulb_t) {
        mq_bulb_draw(q);
    } else {
        mq_helper_cheer_draw(q, true);
        mq_flip(q, md == MOD_MIRROR, md == MOD_FLIP, (field_x0(q) + 320) / 2, (FY + FB) / 2);
        switch (g->mech) {
        case ME_SORT: draw_sort(q); break;
        case ME_JUMP: draw_jump(q); break;
        case ME_LAUNCH: draw_launch(q); break;
        case ME_BAKE: draw_bake(q); break;
        case ME_BUILD: draw_build(q); break;
        case ME_CLOCK: draw_clock(q); break;
        case ME_TILL: draw_till(q); break;
        case ME_DIG: draw_dig(q); break;
        case ME_VAULT: draw_vault(q); break;
        case ME_COOK: draw_cook(q); break;
        case ME_PETS: draw_till(q); break;
        default: draw_pick_field(q); break;
        }
        if (q->sub == GS_PLAY) { /* the widgets: the ones the game does not draw, and everyone's feedback */
            layout(q);
            mq_wg_draw(q);
        }
        mq_flip(q, false, false, 0, 0);
        draw_card(q); /* the order's card over the field, unmirrored, on its own plain white: no road, lane line, cloud or
                       * particle runs through a graph or a clock (the user: green lines through Gate Dash's bar chart;
                       * a graph's order in Kart Race showed no graph) */
        if (q->sub == GS_OFFER)
            draw_offer(q);
    }
    /* (the twist's name, the combo's big words, "+2", the friends' hug: in the top line and the order's
     * band, never over the field -- draw_hud, draw_order; the render audit's M3, M5) */
    if (!g->bulb_t && q->sub != GS_HELP) {
        mq_guide_draw(q);
        mq_helper_cheer_draw(q, false);
    }
    mq_flip(q, md == MOD_MIRROR, md == MOD_FLIP, (field_x0(q) + 320) / 2, (FY + FB) / 2); /* bursts, pop-ups where it happened */
    for (int i = 0; i < 4; i++) /* bubbles bursting: a ring and droplets */
        if (g->burst_t[i] && q->anim_t - g->burst_t[i] < 360) {
            int el = (int)(q->anim_t - g->burst_t[i]), bx = g->burst_x[i], by = g->burst_y[i];
            mq_ring(q, bx, by, 14 + el / 12, g->burst_c[i]);
            if (el < 200)
                mq_ring(q, bx, by, 10 + el / 10, Q_WHITE);
            for (int k = 0; k < 8; k++)
                mq_circle(q, bx + mq_icos(k * 45 + 20) * (16 + el / 7) / 1024, by + mq_isin(k * 45 + 20) * (16 + el / 7) / 1024 + el * el / 9000,
                          el < 240 ? 2 : 1, k % 2 ? g->burst_c[i] : Q_WHITE);
        }
    mq_flip(q, false, false, 0, 0);
    q->ox = q->oy = 0;
    q->clip_y1 = q->gm_top ? 0 : FB; /* the top not drawn: sparks and coins stay in the field */
    q->clip_y0 = FY;
}

/* state for tests */
const char *mq_game_moment(const mq_t *q)
{
    static const char m[6][8] = {"play", "cheer", "why", "offer", "trick", "help"};
    _Static_assert(sizeof m / sizeof m[0] == GS_HELP + 1, "a name for every GS_ sub-state (code review 2, B5)");
    return m[q->sub < 6 ? q->sub : 0];
}
