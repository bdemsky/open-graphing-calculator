/*
 * Math Quest: error patterns -- beyond naming one wrong answer (mq_classify), her wrong answers are
 * followed over time and a recurring PATTERN is diagnosed, shown to her where it happened, practised
 * and re-checked (docs/MATHQUEST.md "Error patterns").
 *
 * The library (PT_*): each pattern knows when an order gives it a chance to show and whether her
 * answer showed it (mq_diag_detect):
 *   - the column work, from the order's own numbers (mq_prob_t.ws, op): a carry forgotten -- into the
 *     tens, the hundreds, the thousands, each its own pattern (she may carry into the tens and never
 *     into the hundreds); the small digit taken from the big one; trading across a zero (the 0 became
 *     10, not 9); a place-value slip (a zero lost or added, x 10 / 10);
 *   - the rest, from the generator's known wrong answers and the misconception behind each
 *     (wrong[] / wrong_mc[], choice_mc[]): adds the denominators, a bigger bottom number a bigger piece,
 *     the clock's hands swapped, an hour taken as 100 minutes, the graph's key skipped, the skip count
 *     a step off, the leftovers forgotten, coin values confused (a nickel taken for more than a dime),
 *     area and perimeter mixed, x and y swapped, a longer decimal taken as bigger. Only an exact match
 *     is evidence (never the classifier's fallback guess).
 *
 * The evidence (mq_diag_rec_t.score): a log-likelihood ratio, x 16 in log2 units, of "she has this
 * pattern" against "her slips are random", per chance it had to show:
 *   showed it       + log2(3 / c), c the chance a random slip lands on it: typed or made answers 1/32,
 *                   a pick game (its wrong options that show it) / (its wrong options);
 *   right           - 23 (log2 0.375: with the pattern she still gets some right);
 *   another slip    + log2(0.5 / (1 - c)).
 * It is diagnosed (PS_SPOTTED) when the score passes 160 (odds of 1024 to 1) with at least 3 of the
 * last 8 chances showing it and twice as many as other slips there. Random slips drift the score down
 * (most answers are right), a guesser's picks spread over all the wrong options (and guessing is not
 * evidence at all: mq_judge skips mashing and guess mode), so tests/unit/test_mq_diag.c feeds random
 * slippers by the thousand and expects no diagnosis, and a pattern-follower is found in a few orders.
 *
 * Then, in the story: the next miss that shows it brings the friend (mq_diag_start): her answer next to
 * the right one, the column (or the clock, the pie, the graph ...) ringed, the step replayed (the 1
 * hopping up to the tens, the ten traded across the zero); then they finish the order together. Three
 * orders aimed at it follow (the engine asks the skill where it showed, generated until the order can
 * show it, every other order: mq_diag_pick / mq_diag_fits), then spaced re-checks after 10, 20 and 40
 * orders; three passed and it is fixed (PS_FIXED). Showing it again sends it back to practice. The
 * grown-ups' Skills tab ("patterns") lists every pattern seen with its status and her last example;
 * the play log has a row per pattern.
 */
#include "mq.h"

enum { HK_DEL = 8, HK_ENTER = 49 };
enum { TV_DOWN = 1, TV_DRAG, TV_UP, TV_TAP };
enum { DW_NEXT = 230, DW_DONE };
#define FB (MQ_Y0 + MQ_H)
#define PICY (MQ_Y0 + 60)
#define PICH 116
#define BTN_Y (FB - 32)
#define SPOT_SCORE 160
#define SCORE_MIN (-64)
#define SCORE_MAX 400

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int pop8(unsigned v)
{
    int n = 0;
    for (v &= 0xFF; v; v &= v - 1)
        n++;
    return n;
}

/* ------------------------------------------------------------------ the library */
enum { DK_CARRY, DK_FLIP, DK_ZERO, DK_PLACE, DK_MC };
typedef struct {
    char name[40];   /* the grown-ups' words */
    uint8_t kind;    /* DK_* */
    uint8_t mc[3];   /* misconceptions whose known wrong answers are its evidence */
    char kid[2][2][46]; /* the friend's words after "yours / it is": two parts, two lines each */
} mq_pattern_t;
static const mq_pattern_t pat[PT_COUNT] = {
    {"Forgets to carry into the tens", DK_CARRY, {MC_NO_REGROUP}, {{"", ""}, {"", ""}}},
    {"Forgets to carry into the hundreds", DK_CARRY, {MC_NO_REGROUP}, {{"", ""}, {"", ""}}},
    {"Forgets to carry into the thousands", DK_CARRY, {MC_NO_REGROUP}, {{"", ""}, {"", ""}}},
    {"Takes the smaller digit from the larger", DK_FLIP, {MC_SUB_FLIP}, {{"", ""}, {"", ""}}},
    {"Trades across a zero wrongly", DK_ZERO, {MC_NO_BORROW_ZERO}, {{"", ""}, {"", ""}}},
    {"Place-value slips (a zero, x 10)", DK_PLACE, {MC_PLACE_SHIFT, MC_ZEROS}, {{"", ""}, {"", ""}}},
    {"Adds the denominators", DK_MC, {MC_ADD_DENOMS},
     {{"The bottom number says how big", "the pieces are. It stays the same!"},
      {"Same size pieces: only the tops", "add up. 2/6 + 3/6 is 5/6."}}},
    {"Bigger denominator = bigger piece", DK_MC, {MC_BIG_DENOM},
     {{"More pieces means smaller pieces!", "Cut a cake in 8: tiny slices."}, {"So 1/8 is smaller than 1/4,", "even though 8 is bigger than 4."}}},
    {"Hour and minute hands swapped", DK_MC, {MC_HANDS_SWAP},
     {{"The short hand tells the hour.", "The long hand tells the minutes."}, {"Find the short one first:", "it says which hour it is."}}},
    {"Treats an hour as 100 minutes", DK_MC, {MC_ELAPSED_100},
     {{"An hour has 60 minutes, not 100!", "At 60 minutes a new hour starts."}, {"So 2:45 and 30 more minutes", "goes past 3:00, to 3:15."}}},
    {"Skips the graph's key", DK_MC, {MC_SCALE_IGNORED},
     {{"Look at the key: one picture", "stands for more than 1!"}, {"Count the pictures, then times", "each one by the key."}}},
    {"Skip counting a step off", DK_MC, {MC_PATTERN_STEP, MC_COUNT_TICKS},
     {{"Count the jumps, not the marks.", "Each jump adds the same amount."}, {"Say each number as you jump:", "the last one you say is it."}}},
    {"Ignores the remainder", DK_MC, {MC_REM_IGNORED},
     {{"Don't forget the leftovers!", "They are part of the answer too."}, {"After sharing, look at what is", "left: it still needs a spot."}}},
    {"Confuses coin values", DK_MC, {MC_COIN_COUNT},
     {{"A coin's size is not its value!", "A dime is small but is 10 cents."}, {"A nickel is 5 cents, a quarter 25:", "count the biggest ones first."}}},
    {"Mixes up area and perimeter", DK_MC, {MC_AREA_PERIM, MC_PERIM_TWO},
     {{"Area is the squares inside.", "Perimeter is the fence around."}, {"For the fence, add every side,", "all the way around."}}},
    {"Swaps x and y on the grid", DK_MC, {MC_COORD_SWAP},
     {{"Go along the bottom first,", "then go up."}, {"(across, up): the first number", "is across, the second is up."}}},
    {"Thinks a longer decimal is bigger", DK_MC, {MC_DEC_LONGER},
     {{"More digits does not mean bigger!", "Look at the tenths first."}, {"0.5 is 5 tenths, 0.45 is only", "4 tenths and a bit: 0.5 wins."}}},
    /* (appended) the times patterns: their picture is the order in a way's model (mq_strat_diag_picture) */
    {"Loses a partial product (multiplying)", DK_MC, {MC_PARTIAL_PRODUCT},
     {{"Times every piece of the number:", "the tens AND the ones!"}, {"Cut the floor at the tens,", "then add up all the pieces."}}},
    {"Adds instead of multiplying", DK_MC, {MC_ADD_NOT_MUL, MC_TIMES_AS_ADD},
     {{"Times means groups of the same:", "3 groups of 4 is 4 + 4 + 4."}, {"Count by the groups,", "not just the two numbers."}}},
    /* (appended) rounding (the user: "teach rounding, not just picking the closer side"): from the order's own
     * number and the place it rounds to (round_detect); shown on its number line, the true spot revealed */
    {"Rounds to the farther ten (or hundred)", DK_MC, {0},
     {{"First find the two tens it is", "between, and the middle of them."}, {"Past the middle: up. Before it: down.", "47 is past 45, so it hops to 50!"}}},
    {"Rounds a 5 (halfway) down", DK_MC, {0},
     {{"Right on the middle? It hops UP!", "45 is halfway: it goes to 50."}, {"The middle always rounds up:", "a 5 there means up."}}},
};
_Static_assert(PT_COUNT <= MQ_NPATTERN + MQ_NPATTERN2, "the saved records hold every pattern");
_Static_assert(sizeof(mq_diag_rec_t) == 40, "a pattern's record is part of the save");

const char *mq_pattern_name(int pt) { return pt >= 0 && pt < PT_COUNT ? pat[pt].name : ""; }
const char *mq_pattern_status(int state)
{
    static const char st[5][12] = {"", "spotted", "practising", "re-checking", "fixed"};
    return st[state >= 0 && state < 5 ? state : 0];
}
mq_diag_rec_t *mq_diag_rec(mq_t *q, int pt)
{
    pt = pt >= 0 && pt < PT_COUNT ? pt : 0;
    return pt < MQ_NPATTERN ? &mq_more(q)->diag[pt] : &mq_more(q)->diag2[pt - MQ_NPATTERN];
}

/* ------------------------------------------------------------------ detection */
static int32_t p10(int k)
{
    int32_t v = 1;
    while (k-- > 0)
        v *= 10;
    return v;
}
static int digit(int32_t v, int k) { return (int)(v / p10(k) % 10); }
static int ndig(int32_t v)
{
    int n = 1;
    while (v >= 10)
        v /= 10, n++;
    return n;
}
/* bit k: a carry goes into column k (k >= 1) */
static unsigned carries(int32_t a, int32_t b)
{
    unsigned m = 0;
    int c = 0;
    for (int k = 0; (a || b || c) && k < 9; k++) {
        int s = (int)(a % 10 + b % 10) + c;
        c = s >= 10;
        if (c)
            m |= 1u << (k + 1);
        a /= 10, b /= 10;
    }
    return m;
}
/* bit k: column k lends to column k - 1 (k >= 1) */
static unsigned lends(int32_t a, int32_t b)
{
    unsigned m = 0;
    int br = 0;
    for (int k = 0; (a || b) && k < 9; k++) {
        int d = (int)(a % 10) - br - (int)(b % 10);
        br = d < 0;
        if (br)
            m |= 1u << (k + 1);
        a /= 10, b /= 10;
    }
    return m;
}
static int32_t flip_sub(int32_t a, int32_t b)
{
    int32_t r = 0, m = 1;
    while (a || b) {
        int d = (int)(a % 10 - b % 10);
        r += (d < 0 ? -d : d) * m;
        a /= 10, b /= 10, m *= 10;
    }
    return r;
}
/* d a sum of distinct powers of ten, each one a column in mask: the columns (bits), else 0 */
static unsigned tens_in(int32_t d, unsigned mask)
{
    unsigned got = 0;
    for (int k = 0; d > 0 && k < 9; k++, d /= 10) {
        int x = (int)(d % 10);
        if (x > 1 || (x == 1 && !((mask >> k) & 1)))
            return 0;
        if (x)
            got |= 1u << k;
    }
    return d ? 0 : got;
}
/* v with one 0 put in (or taken out) somewhere: the digits otherwise the same */
static bool zero_slip(int32_t r, int32_t g)
{
    if (r <= 0 || g <= 0 || r == g)
        return false;
    int32_t lo = r < g ? r : g, hi = r < g ? g : r;
    if (ndig(hi) != ndig(lo) + 1)
        return false;
    for (int k = 0; k < ndig(hi); k++) /* take digit k of hi out: lo? (it must be a 0) */
        if (digit(hi, k) == 0 && hi / p10(k + 1) * p10(k) + hi % p10(k) == lo)
            return true;
    return false;
}
static bool col_numbers(const mq_prob_t *p, int ws, int32_t *a, int32_t *b)
{
    if (p->ws != ws || (p->kind != K_NUM && p->kind != K_MONEY) || p->op[0] <= 0 || p->op[1] <= 0)
        return false;
    *a = p->op[0], *b = p->op[1];
    return ws == WS_ADD ? p->ans == *a + *b : p->ans == *a - *b && *b < *a;
}
static bool has_mc(const mq_pattern_t *d, int mc)
{
    for (int i = 0; i < 3; i++)
        if (d->mc[i] && d->mc[i] == mc)
            return true;
    return false;
}
static bool same_answer(const mq_prob_t *p, int32_t w, int32_t g)
{
    if (w == g)
        return true;
    return p->kind == K_FRAC && w % 1000 && g % 1000 && (int32_t)(w / 1000) * (g % 1000) == (g / 1000) * (w % 1000);
}

/* a rounding order: its number and the place it rounds to (whole numbers: to 10, 100, ...); false otherwise or when
 * it is a round number already */
bool mq_round_numbers(const mq_prob_t *p, int32_t *n, int32_t *unit)
{
    if (p->kind != K_NUM)
        return false;
    if (p->skill == S_ROUND10)
        *n = p->a[0], *unit = p->a[1];
    else if (p->skill == S_ROUNDBIG)
        *n = p->big, *unit = p10(p->v[0]);
    else
        return false;
    return *unit >= 10 && *n > 0 && *n % *unit;
}
/* rounding: to the farther end (a number off the middle), a 5 rounded down (one on it) */
static int round_detect(const mq_prob_t *p, int32_t g, int pt)
{
    int32_t n, unit;
    if (!mq_round_numbers(p, &n, &unit))
        return -1;
    int32_t r = n % unit, lo = n - r, hi = lo + unit;
    bool half = 2 * r == unit;
    if (pt == PT_ROUND_HALF_DOWN)
        return !half ? -1 : g == lo ? 1 : 0;
    return half ? -1 : g == (2 * r > unit ? lo : hi) ? 1 : 0;
}

int mq_diag_detect(const mq_prob_t *p, int32_t g, int pt, int *col)
{
    if (pt < 0 || pt >= PT_COUNT)
        return -1;
    if (pt == PT_ROUND_FAR || pt == PT_ROUND_HALF_DOWN) {
        if (col)
            *col = 1;
        return round_detect(p, g, pt);
    }
    const mq_pattern_t *d = &pat[pt];
    int32_t a, b;
    if (col)
        *col = 0;
    switch (d->kind) {
    case DK_CARRY: {
        if (!col_numbers(p, WS_ADD, &a, &b))
            return -1;
        unsigned cm = carries(a, b), want = pt == PT_CARRY_TENS ? 1u << 1 : pt == PT_CARRY_HUNDREDS ? 1u << 2 : ~7u;
        if (!(cm & want))
            return -1;
        if (col) {
            unsigned w = cm & want;
            int k = 1;
            while (!((w >> k) & 1))
                k++;
            *col = k;
        }
        unsigned got = g < p->ans ? tens_in(p->ans - g, cm) : 0;
        if (got & want) {
            if (col) {
                unsigned w = got & want;
                int k = 1;
                while (!((w >> k) & 1))
                    k++;
                *col = k;
            }
            return 1;
        }
        return 0;
    }
    case DK_FLIP: {
        if (!col_numbers(p, WS_SUB, &a, &b))
            return -1;
        int k0 = -1;
        for (int k = 0; k < ndig(a) && k0 < 0; k++)
            if (digit(a, k) < digit(b, k))
                k0 = k;
        if (k0 < 0)
            return -1;
        if (col)
            *col = k0;
        return g == flip_sub(a, b) ? 1 : 0;
    }
    case DK_ZERO: {
        if (!col_numbers(p, WS_SUB, &a, &b))
            return -1;
        unsigned lm = lends(a, b);
        int kz = -1;
        for (int k = 1; k < ndig(a) - 1 && kz < 0; k++) /* a 0 inside that has to pass a ten on */
            if (digit(a, k) == 0 && ((lm >> k) & 1))
                kz = k;
        if (kz < 0)
            return -1;
        if (col)
            *col = kz;
        int32_t dd = g - p->ans;
        return dd == p10(kz) || dd == p10(kz + 1) ? 1 : 0;
    }
    case DK_PLACE: {
        bool place = p->skill == S_PV3 || p->skill == S_PVBIG || p->skill == S_MUL10 || p->skill == S_POW10;
        bool known = false;
        for (int i = 0; i < p->nwrong; i++)
            if (has_mc(d, p->wrong_mc[i])) {
                known = true;
                if (same_answer(p, p->wrong[i], g))
                    return 1;
            }
        if (!(place && p->kind == K_NUM && p->ans >= 10) && !known)
            return -1;
        if (p->kind == K_NUM && p->ans >= 10 && (g == p->ans * 10 || g * 10 == p->ans || zero_slip(p->ans, g)))
            return 1;
        return 0;
    }
    default: { /* the generator's known wrong answers with its misconceptions */
        bool chance = false;
        if (p->kind == K_CHOICE) {
            for (int i = 0; i < p->nchoice; i++)
                if (has_mc(d, p->choice_mc[i]) && i != p->ans)
                    chance = true;
            if (!chance)
                return -1;
            return g >= 0 && g < p->nchoice && g != p->ans && has_mc(d, p->choice_mc[g]) ? 1 : 0;
        }
        for (int i = 0; i < p->nwrong; i++)
            if (has_mc(d, p->wrong_mc[i]) && !mq_correct(p, p->wrong[i])) {
                chance = true;
                if (same_answer(p, p->wrong[i], g))
                    return 1;
            }
        return chance ? 0 : -1;
    }
    }
}

/* ------------------------------------------------------------------ the evidence */
/* 16 log2(num / den) (num, den > 0), about */
static int log2x16(int32_t num, int32_t den)
{
    int sign = 1;
    if (num < den) {
        int32_t t = num;
        num = den, den = t, sign = -1;
    }
    int e = 0;
    while (num >= 2 * den && e < 30)
        den *= 2, e++;
    /* num / den in [1, 2): log2 ~ (x - 1) * (1.36 - 0.36 (x - 1)) */
    int32_t f = (int32_t)((num - den) * 256 / den); /* (x - 1) x 256 */
    int32_t l = f * (348 - 92 * f / 256) / 256;     /* x 256 */
    return sign * (e * 16 + (int)(l / 16));
}

static void ex_text(mq_t *q, const mq_prob_t *p, int32_t g, char *out, int n)
{
    char ys[16], rs[16];
    mq_format(q, p, g, ys, sizeof ys);
    mq_format(q, p, p->ans, rs, sizeof rs);
    if ((p->ws == WS_ADD || p->ws == WS_SUB) && p->kind == K_NUM && p->op[0] < 100000 && p->op[1] < 100000)
        q->api->snprintf(out, (size_t)n, "%ld%c%ld: %s, not %s", (long)p->op[0], p->ws == WS_ADD ? '+' : '-', (long)p->op[1], ys, rs);
    else
        q->api->snprintf(out, (size_t)n, "%s, not %s", ys, rs);
}

/* an answer (right or not) to an order: every pattern it gave a chance to show; nwrong / nmatch: the
 * wrong options she could pick from and how many of them show the pattern (0: typed or made) */
static int feed(mq_t *q, const mq_prob_t *p, int32_t g, bool right, bool first, int nwrong, int nmatch_of[PT_COUNT])
{
    int shown = -1, best = -1;
    uint16_t now = (uint16_t)mq_prof(q)->problems;
    for (int pt = 0; pt < PT_COUNT; pt++) {
        int col, r = mq_diag_detect(p, right ? p->ans : g, pt, &col);
        if (r < 0)
            continue;
        if (!first && !(r == 1 && !right)) /* a second try: only a pattern shown again is evidence */
            continue;
        mq_diag_rec_t *d = mq_diag_rec(q, pt);
        int nm = nmatch_of ? nmatch_of[pt] : 0;
        int s;
        if (right)
            s = -23;
        else if (r == 1)
            s = nwrong > 0 && nm > 0 ? log2x16(3 * nwrong, nm) : log2x16(96, 1);
        else
            s = nwrong > 0 && nm > 0 && nm < nwrong ? log2x16(nwrong, 2 * (nwrong - nm)) : nwrong > 0 && nm >= nwrong ? 0 : -16;
        d->score = (int16_t)imax(SCORE_MIN, imin(SCORE_MAX, d->score + s));
        if ((d->state == PS_SPOTTED || d->state == PS_PRACTICE) && d->score <= 0) /* her rights wore it down: re-check it */
            d->state = PS_RECHECK, d->step = 0, d->due = (uint16_t)(now + 10);
        d->hits8 = (uint8_t)(d->hits8 << 1 | (!right && r == 1));
        d->slips8 = (uint8_t)(d->slips8 << 1 | (!right && r == 0));
        d->chances = (uint8_t)imin(255, d->chances + 1);
        if (!right && r == 1) {
            d->hits = (uint8_t)imin(255, d->hits + 1);
            d->skill = p->skill, d->col = (uint8_t)col;
            ex_text(q, p, g, d->ex, sizeof d->ex);
            int h = pop8(d->hits8);
            if ((d->state == PS_NONE || d->state == PS_FIXED) && d->score >= SPOT_SCORE && h >= 3 && h >= 2 * pop8(d->slips8)) {
                d->state = PS_SPOTTED, d->step = 3, d->day = mq_prof(q)->days, d->told = 0, d->due = now;
                q->dirty = true;
            } else if (d->state == PS_RECHECK) /* showed again: back to practice */
                d->state = PS_PRACTICE, d->step = 2;
            if (d->state != PS_NONE && d->state != PS_FIXED && (best < 0 || d->score > mq_diag_rec(q, best)->score))
                best = pt;
            if (shown < 0)
                shown = pt;
        }
        /* an order aimed at it (first answers): practice counts down, re-checks count up */
        if (first && q->diag_item == pt + 1) {
            if (d->state == PS_SPOTTED || d->state == PS_PRACTICE) /* the next aimed one in a few orders */
                d->due = (uint16_t)(now + (right ? 3 : 5));
            if (right && (d->state == PS_PRACTICE || d->state == PS_SPOTTED) && d->step && --d->step == 0)

                d->state = PS_RECHECK, d->step = 0, d->due = (uint16_t)(now + 10);
            else if (right && d->state == PS_RECHECK) {
                if (++d->step >= 3)
                    d->state = PS_FIXED, d->score = (int16_t)imin(d->score, 40), d->hits8 = d->slips8 = 0;
                else
                    d->due = (uint16_t)(now + (10 << d->step));
            } else if (!right && r == 1 && d->state == PS_PRACTICE)
                d->step = (uint8_t)imin(3, d->step + 1);
        }
    }
    return best >= 0 ? best : shown >= 0 && mq_diag_rec(q, shown)->state != PS_NONE && mq_diag_rec(q, shown)->state != PS_FIXED ? shown : -1;
}

/* an answer given with these options to pick from (nopt 0: typed or made): the pattern it showed */
int mq_diag_feed(mq_t *q, const mq_prob_t *p, int32_t given, bool right, bool first, const int32_t *opt, int nopt)
{
    int nwrong = 0, nm[PT_COUNT] = {0};
    if (p->kind == K_CHOICE) {
        nwrong = p->nchoice - 1;
        for (int pt = 0; pt < PT_COUNT; pt++)
            for (int i = 0; i < p->nchoice; i++)
                nm[pt] += i != p->ans && mq_diag_detect(p, i, pt, NULL) == 1;
    } else
        for (int i = 0; i < nopt; i++)
            if (!mq_correct(p, opt[i])) {
                nwrong++;
                for (int pt = 0; pt < PT_COUNT; pt++)
                    nm[pt] += mq_diag_detect(p, opt[i], pt, NULL) == 1;
            }
    return feed(q, p, given, right, first, nwrong, nm);
}

/* the judged answer of an order in the game (mq_judge): the options, if she picked */
int mq_diag_judge(mq_t *q, const mq_prob_t *p, int32_t given, bool right, bool first)
{
    if (q->preview || q->placing_item || q->guess_n || q->mashed || p->skill == S_FACTS)
        return -1;
    const mq_game_t *g = &q->gm;
    bool picked = q->scene == SC_PLAY && g->nopt >= 2 && !g->make && !mq_order_typed(q);
    return mq_diag_feed(q, p, given, right, first, g->opt, picked ? g->nopt : 0);
}

/* the answer an order gets from someone with pattern pt (the console's `mq diag slip`, tests); 0x7FFFFFFF:
 * the order cannot show it */
int32_t mq_diag_example(const mq_prob_t *p, int pt)
{
    int col;
    if (mq_diag_detect(p, p->ans, pt, &col) < 0)
        return 0x7FFFFFFF;
    switch (pat[pt].kind) {
    case DK_CARRY: return p->ans - p10(col);
    case DK_FLIP: return flip_sub(p->op[0], p->op[1]);
    case DK_ZERO: return p->ans + p10(col);
    default: break;
    }
    if (p->kind == K_CHOICE) {
        for (int i = 0; i < p->nchoice; i++)
            if (i != p->ans && mq_diag_detect(p, i, pt, NULL) == 1)
                return i;
    } else
        for (int i = 0; i < p->nwrong; i++)
            if (mq_diag_detect(p, p->wrong[i], pt, NULL) == 1)
                return p->wrong[i];
    if (pat[pt].kind == DK_PLACE && p->kind == K_NUM)
        return p->ans * 10;
    return 0x7FFFFFFF;
}

bool mq_diag_due(mq_t *q, int pt)
{
    if (pt < 0 || pt >= PT_COUNT || q->preview || q->placing_item)
        return false;
    mq_diag_rec_t *d = mq_diag_rec(q, pt);
    uint16_t now = (uint16_t)mq_prof(q)->problems;
    if (d->state == PS_SPOTTED)
        return true;
    return (d->state == PS_PRACTICE || d->state == PS_RECHECK) && (!d->told || (uint16_t)(now - d->told) >= 8);
}

int mq_diag_spotted(mq_t *q)
{
    int n = 0;
    for (int pt = 0; pt < PT_COUNT; pt++) {
        int s = mq_diag_rec(q, pt)->state;
        n += s == PS_SPOTTED || s == PS_PRACTICE || s == PS_RECHECK;
    }
    return n;
}

/* ------------------------------------------------------------------ orders aimed at a pattern */
int mq_diag_pick(mq_t *q)
{
    q->diag_item = 0;
    if (q->preview)
        return -1;
    if (q->diag_alt) { /* (at most one aimed order in three: the game stays a game) */
        q->diag_alt--;
        return -1;
    }
    uint16_t now = (uint16_t)mq_prof(q)->problems;
    int best = -1;
    for (int pt = 0; pt < PT_COUNT; pt++) {
        mq_diag_rec_t *d = mq_diag_rec(q, pt);
        bool want = (d->state == PS_SPOTTED || d->state == PS_PRACTICE || d->state == PS_RECHECK) &&
                    (int16_t)(now - d->due) >= 0;
        if (want && d->skill < MQ_NSKILL && (best < 0 || d->state > mq_diag_rec(q, best)->state))
            best = pt;
    }
    if (best < 0)
        return -1;
    mq_diag_rec_t *d = mq_diag_rec(q, best);
    q->diag_item = (int8_t)(best + 1);
    q->diag_alt = 2;
    return d->skill;
}

bool mq_diag_fits(mq_t *q, const mq_prob_t *p)
{
    return !q->diag_item || mq_diag_detect(p, p->ans, q->diag_item - 1, NULL) >= 0;
}

/* ------------------------------------------------------------------ the friend shows her */
static const char col_name[6][10] = {"ones", "tens", "hundreds", "thousands", "ten-thou.", "big ones"};
static const char *cname(int k) { return col_name[imax(0, imin(5, k))]; }

/* what the friend says in part i */
static void words(mq_t *q)
{
    mq_diagv_t *v = &q->diag;
    const modx_api_t *api = q->api;
    char(*s)[48] = v->say;
    s[0][0] = s[1][0] = 0;
    int k = v->col;
    int32_t a = v->a, b = v->b;
    if (v->part == 0) {
        api->snprintf(s[0], 48, "Look! Yours is %s, it is %s.", v->ys, v->rs);
        api->snprintf(s[1], 48, pat[v->pt].kind == DK_MC ? "I think I know what happened!" : "Look at the %s!", cname(k));
        return;
    }
    switch (pat[v->pt].kind) {
    case DK_CARRY: {
        int lo = k - 1, cin = lo > 0 && ((carries(a, b) >> lo) & 1);
        int s1 = digit(a, lo) + digit(b, lo) + cin;
        if (v->part == 1) {
            api->snprintf(s[0], 48, "The %s: %d + %d%s makes %d.", cname(lo), digit(a, lo), digit(b, lo), cin ? " + 1" : "", s1);
            api->snprintf(s[1], 48, "Write the %d, and the 1 is a ten...", s1 % 10);
        } else if (v->part == 2) {
            api->snprintf(s[0], 48, "...that hops up to the %s!", cname(k));
            int c2 = digit(a, k) + digit(b, k) + 1;
            api->snprintf(s[1], 48, "The %s: %d + %d + 1 makes %d.", cname(k), digit(a, k), digit(b, k), c2);
        } else {
            api->snprintf(s[0], 48, "A column making 10 or more");
            api->snprintf(s[1], 48, "sends a 1 up to the next one!");
        }
        break;
    }
    case DK_FLIP:
        if (v->part == 1) {
            api->snprintf(s[0], 48, "The %s: %d take away %d?", cname(k), digit(a, k), digit(b, k));
            api->snprintf(s[1], 48, "Not enough! Flipping it is not fair.");
        } else if (v->part == 2) {
            api->snprintf(s[0], 48, "Trade 1 from the %s: now", cname(k + 1));
            api->snprintf(s[1], 48, "it is %d take away %d: %d!", digit(a, k) + 10, digit(b, k), digit(a, k) + 10 - digit(b, k));
        } else {
            api->snprintf(s[0], 48, "And the %s have one less", cname(k + 1));
            api->snprintf(s[1], 48, "after the trade.");
        }
        break;
    case DK_ZERO:
        if (v->part == 1) {
            api->snprintf(s[0], 48, "The %s need a ten,", cname(k - 1));
            api->snprintf(s[1], 48, "but the %s have 0!", cname(k));
        } else if (v->part == 2) {
            api->snprintf(s[0], 48, "Trade 1 from the %s: 10 %s.", cname(k + 1), cname(k));
            api->snprintf(s[1], 48, "One goes on to the %s: 9 are left!", cname(k - 1));
        } else {
            api->snprintf(s[0], 48, "So that 0 becomes 9, not 10.");
            api->snprintf(s[1], 48, "Then take away, place by place.");
        }
        break;
    case DK_PLACE:
        api->snprintf(s[0], 48, "Each place is 10 times the one");
        api->snprintf(s[1], 48, "on its right. Zeros hold a place!");
        break;
    default:
        api->snprintf(s[0], 48, "%s", pat[v->pt].kid[v->part - 1][0]);
        api->snprintf(s[1], 48, "%s", pat[v->pt].kid[v->part - 1][1]);
        break;
    }
}

void mq_diag_start(mq_t *q, int pt)
{
    if (pt < 0 || pt >= PT_COUNT)
        return;
    mq_diagv_t *v = &q->diag;
    const mq_prob_t *p = &q->prob;
    int32_t g = q->last_given;
    int col = 0;
    mq_diag_detect(p, g, pt, &col);
    v->on = 1, v->pt = (uint8_t)pt, v->part = 0;
    v->ws = p->ws, v->col = (uint8_t)col, v->kind = p->kind, v->slot = (uint8_t)(p->ca % MQ_NCAST);
    v->a = p->op[0], v->b = p->op[1], v->given = g, v->right = p->ans;
    mq_format(q, p, g, v->ys, sizeof v->ys);
    mq_format(q, p, p->ans, v->rs, sizeof v->rs);
    v->nparts = pat[pt].kind == DK_MC ? 3 : pat[pt].kind == DK_PLACE ? 2 : 4;
    v->t0 = v->part_t = mq_mark(q);
    words(q);
    mq_speak(q, v->slot, v->say[0], MODX_SPEAK_NOW);
    mq_sfx(q, MODX_SFX_POP, 1);
    q->redraw = true;
}

static void put_away(mq_t *q)
{
    mq_diagv_t *v = &q->diag;
    v->on = 0;
    mq_diag_rec_t *d = mq_diag_rec(q, v->pt);
    d->told = (uint16_t)imax(1, (int)(uint16_t)mq_prof(q)->problems);
    if (d->state == PS_SPOTTED)
        d->state = PS_PRACTICE, d->step = 3;
    q->dirty = true;
    q->redraw = true;
}
/* the console, a test's answer: put away without finishing the order together */
void mq_diag_put_away(mq_t *q)
{
    if (q->diag.on)
        put_away(q);
}
void mq_diag_close(mq_t *q)
{
    mq_diagv_t *v = &q->diag;
    if (!v->on)
        return;
    put_away(q);
    mq_wg_reset(q);
    if (q->scene == SC_PLAY && q->sub == GS_PLAY)
        mq_game_together(q); /* and the order is finished together */
}
bool mq_diag_on(mq_t *q) { return q->diag.on != 0; }

static void next_part(mq_t *q)
{
    mq_diagv_t *v = &q->diag;
    if (v->part + 1 >= v->nparts) {
        mq_diag_close(q);
        return;
    }
    v->part++;
    v->part_t = mq_mark(q);
    words(q);
    mq_speak(q, v->slot, v->say[0], MODX_SPEAK_NOW);
    q->redraw = true;
}

void mq_diag_frame(mq_t *q)
{
    mq_diagv_t *v = &q->diag;
    if (v->on && q->anim_t - v->part_t < 1200)
        q->redraw = true; /* (the step replayed: moving) */
}

static void layout(mq_t *q)
{
    mq_diagv_t *v = &q->diag;
    mq_wg_begin(q);
    bool last = v->part + 1 >= v->nparts;
    int i = mq_wg_add(q, WG_BUTTON, last ? DW_DONE : DW_NEXT, 0, 196, BTN_Y, 118, 30);
    mq_wg_label(q, i, last ? "got it!" : "next", last ? Q_MINT : Q_YELLOW);
}
static void on_ev(mq_t *q, const mq_wev_t *ev)
{
    if (ev->why != WHY_TAP && ev->why != WHY_KEY)
        return;
    if (ev->id == DW_NEXT || ev->id == DW_DONE)
        next_part(q);
}
bool mq_diag_touch(mq_t *q, int type, int x, int y)
{
    layout(q);
    mq_wev_t ev;
    if (mq_wg_touch(q, type, x, y, &ev) && ev.why != WHY_NONE)
        on_ev(q, &ev);
    q->redraw = true;
    return true;
}
bool mq_diag_key(mq_t *q, int k, int ch)
{
    (void)ch;
    if (k == HK_ENTER || k == HK_DEL) {
        if (k == HK_DEL) /* (put away: the order is finished together) */
            q->diag.part = (uint8_t)(q->diag.nparts - 1);
        next_part(q);
        return true;
    }
    layout(q);
    mq_wev_t ev;
    if (mq_wg_key(q, k, ch, &ev) && ev.why != WHY_NONE)
        on_ev(q, &ev);
    q->redraw = true;
    return true;
}

/* the column work, as on paper: her row under the right one, the column ringed, the step replayed */
#define CW 15
static void digits_row(mq_t *q, int rx, int y, int32_t v, uint16_t c, int ring, uint16_t rc)
{
    const modx_api_t *api = q->api;
    int n = ndig(v < 0 ? -v : v);
    for (int k = 0; k < n; k++) {
        char s[2] = {(char)('0' + digit(v, k)), 0};
        int x = rx - (k + 1) * CW;
        if (k == ring) {
            mq_rrect(q, x - 1, y - 1, CW, 21, 4, rc);
            mq_rrect(q, x + 1, y + 1, CW - 4, 17, 3, Q_WHITE);
        }
        mq_text_c(q, api->font_large, x + CW / 2 - 1, y + 1, s, c);
    }
}
static void draw_columns(mq_t *q)
{
    mq_diagv_t *v = &q->diag;
    const modx_api_t *api = q->api;
    int rx = 190, y0 = PICY + 4, el = (int)(q->anim_t - v->part_t);
    bool add = v->ws == WS_ADD;
    int k = v->col;
    /* the place names over the columns */
    int n = imax(ndig(v->a), imax(ndig(v->right), ndig(v->given)));
    for (int c = 0; c < n && c < 5; c++) {
        static const char pl[5][2] = {"O", "T", "H", "K", "M"};
        mq_text_c(q, api->font_small, rx - (c + 1) * CW + CW / 2 - 1, y0 - 2, pl[c], c == k ? Q_HOTPINK : Q_DIM);
    }
    int ya = y0 + 12, yb = ya + 20, yl = yb + 21, yh = yl + 4, yr = yh + 22;
    bool step = v->part >= 1 && pat[v->pt].kind != DK_PLACE;
    int lo = pat[v->pt].kind == DK_CARRY ? k - 1 : k;
    digits_row(q, rx, ya, v->a, Q_INK, step && v->part == 1 ? lo : -1, Q_YELLOW);
    digits_row(q, rx, yb, v->b, Q_INK, step && v->part == 1 ? lo : -1, Q_YELLOW);
    mq_text(q, api->font_large, rx - (n + 1) * CW - 4, yb + 1, add ? "+" : "-", Q_INK);
    mq_fill(q, rx - (n + 1) * CW - 4, yl, (n + 1) * CW + 6, 2, Q_INK);
    /* her answer (pink, the column ringed) and the right one (green) */
    mq_text(q, api->font_small, 14, yh + 5, "yours", Q_HOTPINK);
    digits_row(q, rx, yh, v->given, Q_HOTPINK, k, Q_HOTPINK);
    mq_text(q, api->font_small, 14, yr + 5, "it is", Q_GREEN);
    digits_row(q, rx, yr, v->right, Q_GREEN, k, Q_GREEN);
    /* the step replayed */
    if (pat[v->pt].kind == DK_CARRY && v->part >= 2) { /* the 1 hops from the column below up over column k */
        int t = imin(600, el), x0 = rx - k * CW + 2, x1 = rx - (k + 1) * CW + 3, yy0 = yh + 2, yy1 = ya - 11;
        int x = x0 + (x1 - x0) * t / 600, y = yy0 + (yy1 - yy0) * t / 600 - (t < 600 ? (int)(mq_isin(t * 180 / 600) * 14 / 1024) : 0);
        mq_rrect(q, x - 1, y - 1, 11, 12, 3, Q_YELLOW);
        mq_text(q, api->font_small, x + 2, y, "1", Q_HOTPINK);
    }
    if ((pat[v->pt].kind == DK_FLIP || pat[v->pt].kind == DK_ZERO) && v->part >= 2) { /* the trade: the top digit crossed out */
        int kk = pat[v->pt].kind == DK_ZERO ? k : k + 1, x = rx - (kk + 1) * CW;
        mq_line(q, x, ya + 16, x + CW - 3, ya + 2, Q_HOTPINK);
        char s[4];
        int nv = pat[v->pt].kind == DK_ZERO ? 9 : digit(v->a, kk) - 1;
        q->api->snprintf(s, sizeof s, "%d", nv < 0 ? 9 : nv);
        if (el > 200)
            mq_text(q, api->font_small, x + 3, ya - 11, s, Q_HOTPINK);
        int xl = rx - (lo + 1) * CW;
        if (pat[v->pt].kind == DK_FLIP && el > 400) /* and the ones get their ten: 12 */
            mq_text(q, api->font_small, xl - 4, ya - 11, "1", Q_HOTPINK);
    }
}
/* any other pattern: her answer next to the right one, and the picture with nothing hidden */
static void draw_model(mq_t *q)
{
    mq_diagv_t *v = &q->diag;
    const modx_api_t *api = q->api;
    mq_rrect(q, 12, PICY + 8, 100, 40, 10, Q_PINK);
    mq_text(q, api->font_small, 18, PICY + 10, "yours", Q_HOTPINK);
    mq_text_fit(q, api->font_large, 18, PICY + 24, 90, v->ys, Q_INK, false);
    mq_rrect(q, 12, PICY + 54, 100, 40, 10, Q_MINT);
    mq_text(q, api->font_small, 18, PICY + 56, "it is", Q_GREEN);
    mq_text_fit(q, api->font_large, 18, PICY + 70, 90, v->rs, Q_INK, false);
    bool vq = q->vis_q;
    q->vis_q = false; /* (the whole picture: the answer is out now) */
    mq_visual(q, &q->prob, 120, PICY + 4, 190, PICH - 12, 1000);
    q->vis_q = vq;
    if ((q->anim_t / 400) % 2) /* the picture is where to look */
        mq_rect(q, 118, PICY + 2, 194, PICH - 8, Q_HOTPINK);
}

void mq_diag_draw(mq_t *q)
{
    mq_diagv_t *v = &q->diag;
    const modx_api_t *api = q->api;
    mq_fill(q, 0, MQ_Y0 + 16, MQ_W, MQ_H - 16, RGB(255, 246, 250));
    bool talk = q->anim_t - v->part_t < 1600;
    mq_cast(q, v->slot, 2, MQ_Y0 + 17, 26, talk ? AN_TALK : AN_HAPPY);
    mq_rrect(q, 30, MQ_Y0 + 18, 286, 24, 8, Q_WHITE);
    mq_text_fit(q, api->font_small, 34, MQ_Y0 + 18, 278, v->say[0], Q_INK, false);
    mq_text_fit(q, api->font_small, 34, MQ_Y0 + 29, 278, v->say[1], Q_INK, false);
    mq_text(q, api->font_small, 8, MQ_Y0 + 46, "Let's look closer!", Q_PURPLE);
    for (int k = 0; k < v->nparts; k++) {
        mq_circle(q, 180 + k * 12, MQ_Y0 + 52, 4, k == v->part ? Q_HOTPINK : k < v->part ? Q_PINK : Q_WHITE);
        mq_ring(q, 180 + k * 12, MQ_Y0 + 52, 4, Q_DIM);
    }
    mq_rrect(q, 6, PICY - 2, 308, PICH, 10, Q_WHITE);
    if (pat[v->pt].kind == DK_MC && !mq_strat_diag_picture(q, v->pt)) /* (a way's model, where one shows it best) */
        draw_model(q);
    else
        draw_columns(q);
    layout(q);
    mq_wg_draw(q);
}

/* ------------------------------------------------------------------ the grown-ups */
void mq_diag_parent(mq_t *q, int y)
{
    const modx_api_t *api = q->api;
    int n = 0;
    mq_text(q, api->font_small, 4, y, "Error patterns (her wrong answers over time):", Q_INK);
    y += 14;
    for (int pt = 0; pt < PT_COUNT && n < 6; pt++) {
        const mq_diag_rec_t *d = mq_diag_rec(q, pt);
        if (!d->hits)
            continue;
        n++;
        char st[24];
        if (d->state == PS_NONE)
            api->snprintf(st, sizeof st, "watching");
        else if (d->state == PS_PRACTICE || d->state == PS_SPOTTED)
            api->snprintf(st, sizeof st, "practice %d/3", 3 - d->step);
        else if (d->state == PS_RECHECK)
            api->snprintf(st, sizeof st, "re-check %d/3", d->step);
        else
            api->snprintf(st, sizeof st, "%s", mq_pattern_status(d->state));
        uint16_t c = d->state == PS_FIXED ? Q_GREEN : d->state == PS_NONE ? Q_DIM : Q_HOTPINK;
        mq_text_fit(q, api->font_small, 4, y, 226, mq_pattern_name(pt), c, false);
        mq_text_fit(q, api->font_small, 234, y, 82, st, c, false);
        api->snprintf(q->tmp, sizeof q->tmp, "last: %.24s", d->ex);
        mq_text_fit(q, api->font_small, 14, y + 13, 216, q->tmp, Q_DIM, false);
        api->snprintf(q->tmp, sizeof q->tmp, "%d of %d", d->hits, d->chances);
        mq_text_fit(q, api->font_small, 234, y + 13, 82, q->tmp, Q_DIM, false);
        y += 27;
    }
    if (!n) {
        mq_text(q, api->font_small, 4, y, "None yet: her slips look random -- good!", Q_GREEN);
        mq_text(q, api->font_small, 4, y + 12, "A pattern shows up here when the same kind", Q_DIM);
        mq_text(q, api->font_small, 4, y + 24, "of wrong answer keeps coming back.", Q_DIM);
    } else
        mq_text_fit(q, api->font_small, 4, MQ_Y0 + 194, 312, "shown / chances to show it; practice, re-checks: done",
                    Q_DIM, false);

}

void mq_diag_log(mq_t *q, void (*out)(void *user, const char *line), void *user)
{
    char b[120];
    out(user, "pattern,status,shown,chances,score,step,skill,last_example");
    for (int pt = 0; pt < PT_COUNT; pt++) {
        const mq_diag_rec_t *d = mq_diag_rec(q, pt);
        if (!d->chances)
            continue;
        char ex[26];
        int i = 0;
        for (; d->ex[i] && i < 23; i++)
            ex[i] = d->ex[i] == ',' ? ';' : d->ex[i];
        ex[i] = 0;
        q->api->snprintf(b, sizeof b, "%s,%s,%d,%d,%d,%d,%s,%s", mq_pattern_name(pt),
                         d->state ? mq_pattern_status(d->state) : "watching", d->hits, d->chances, d->score, d->step,
                         d->skill < MQ_NSKILL ? mq_skill[d->skill].name : "", ex);
        out(user, b);
    }
}

/* `mq diag` the records; `mq diag show PT` the friend shows it (on the order now); `mq diag give V` her
 * answer V to the order now (a game action); `mq diag next|done`; `mq diag reset` */
int mq_diag_console(mq_t *q, int argc, char **argv)
{
    const modx_api_t *api = q->api;
    if (argc < 2 || api->strcmp(argv[1], "diag"))
        return -2;
    long n = 0;
    const char *sub = argc > 2 ? argv[2] : "";
    if (!api->strcmp(sub, "show") && argc > 3 && api->con_parse_int(argv[3], &n)) { /* on the order now, as if she slipped */
        int32_t v = mq_diag_example(&q->prob, (int)n);
        if (v == 0x7FFFFFFF)
            return api->con_error("this order cannot show it");
        q->last_given = v;
        mq_diag_start(q, (int)n);

    } else if (!api->strcmp(sub, "give") && argc > 3 && api->con_parse_int(argv[3], &n)) {
        if (q->scene != SC_PLAY || q->sub != GS_PLAY)
            return api->con_error("not playing");
        q->last_input = q->now; /* (her answer, now: the decision's time, mq_judge) */
        mq_game_answer(q, (int32_t)n);
    } else if (!api->strcmp(sub, "slip") && argc > 3 && api->con_parse_int(argv[3], &n)) { /* her order answered as pattern n would */
        if (q->scene != SC_PLAY || q->sub != GS_PLAY || mq_diag_on(q))
            return api->con_error("not playing");
        int32_t v = mq_diag_example(&q->prob, (int)n);
        if (v == 0x7FFFFFFF)
            return api->con_error("this order cannot show it");
        q->last_input = q->now; /* (her answer, now) */
        mq_game_answer(q, v);
    } else if (!api->strcmp(sub, "next")) {
        if (q->diag.on)
            next_part(q);
    } else if (!api->strcmp(sub, "done")) {
        if (q->diag.on)
            q->diag.part = (uint8_t)(q->diag.nparts - 1), next_part(q);
    } else if (!api->strcmp(sub, "reset")) {
        for (int pt = 0; pt < PT_COUNT; pt++) {
            uint8_t *b = (uint8_t *)mq_diag_rec(q, pt);
            for (size_t i = 0; i < sizeof(mq_diag_rec_t); i++)
                b[i] = 0;
        }
    }
    if (*sub) /* (the list: a plain `mq diag`) */
        return 0;
    api->con_printf("diag %s pattern %d part %d of %d; aimed %d\n", q->diag.on ? "on" : "off", q->diag.pt, q->diag.part,
                    q->diag.nparts, q->diag_item - 1);
    for (int pt = 0; pt < PT_COUNT; pt++) {
        const mq_diag_rec_t *d = mq_diag_rec(q, pt);
        if (d->chances)
            api->con_printf("%s: %s, shown %d of %d, score %d, step %d, col %d, %s\n", mq_pattern_name(pt),
                            d->state ? mq_pattern_status(d->state) : "watching", d->hits, d->chances, d->score, d->step,
                            d->col, d->ex);
    }
    return 0;
}
