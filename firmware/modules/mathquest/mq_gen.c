/*
 * Math Quest: problem generators, answers, and the misconception classifier.
 *
 * mq_generate(q, p, skill, diff) fills a problem for one skill (S_*) at an
 * adaptive difficulty 0..3 with fresh random numbers (q->rng): the question
 * (a short story where it fits, starring the cast), the answer, the visual
 * model that explains it (V_*, drawn by mq_vis.c), the generator parameters
 * a[] (the worked example re-uses them, mq_steps.c) and the common wrong
 * answers with the misconception behind each (wrong[] / wrong_mc[]; for
 * choice problems each wrong choice carries its misconception). The
 * classifier maps a wrong answer back to its misconception, which the
 * feedback screen names gently before showing the model.
 *
 * Answer encodings (p->ans, parsed from what the player typed by mq_parse):
 *   K_NUM    the number                    K_REM   quotient * 1000 + remainder ("17R3")
 *   K_FRAC   numerator * 1000 + denominator ("3/4"; p->equiv: equivalent fractions count)
 *   K_DEC    thousandths ("0.45" = 450)    K_MONEY cents ("2.35" = 235, "3" = 300)
 *   K_TIME   hours * 100 + minutes ("345" or "3:45"; minutes are not checked
 *            against 60, so 2:75 can be recognised as the 100-minute hour mistake)
 *   K_CHOICE index of the tile             K_SHADE number of slices shaded
 *   K_LINE   tick index on the number line K_COORD x * 100 + y ("3,4")
 */
#include "mq.h"

#define TX(i, ...) q->api->snprintf(p->text[i], MQ_TXT, __VA_ARGS__)
/* what the friend says on the board: a few words (the full text above is a tap away) */
#define CAP(...) q->api->snprintf(p->cap, sizeof p->cap, __VA_ARGS__)

static int R(mq_t *q, int lo, int hi) { return mq_rnd(q, lo, hi); }
static int imin(int a, int b) { return a < b ? a : b; }
static int iabs(int a) { return a < 0 ? -a : a; }
static int gcd(int a, int b)
{
    while (b) {
        int t = a % b;
        a = b, b = t;
    }
    return a < 0 ? -a : a;
}

/* a known wrong answer (ignored when it equals the answer, is negative or a duplicate) */
static void W(mq_prob_t *p, int32_t v, int mc)
{
    if (v == p->ans || v < 0 || p->nwrong >= 6)
        return;
    for (int i = 0; i < p->nwrong; i++)
        if (p->wrong[i] == v)
            return;
    p->wrong[p->nwrong] = v;
    p->wrong_mc[p->nwrong++] = (uint8_t)mc;
}

/* choice tiles: the first one added is the right answer (shuffled afterwards unless keep) */
static void C(mq_t *q, mq_prob_t *p, const char *label, int mc)
{
    if (p->nchoice >= 4)
        return;
    int i = p->nchoice++;
    q->api->snprintf(p->choice[i], sizeof p->choice[i], "%s", label);
    p->choice_mc[i] = (uint8_t)mc;
}
static void shuffle(mq_t *q, mq_prob_t *p)
{
    int ans = 0;
    for (int i = p->nchoice - 1; i > 0; i--) {
        int j = R(q, 0, i);
        char t[16];
        for (int k = 0; k < 16; k++)
            t[k] = p->choice[i][k], p->choice[i][k] = p->choice[j][k], p->choice[j][k] = t[k];
        uint8_t m = p->choice_mc[i];
        p->choice_mc[i] = p->choice_mc[j], p->choice_mc[j] = m;
        if (ans == i)
            ans = j;
        else if (ans == j)
            ans = i;
    }
    p->ans = ans;
}
static void choices_done(mq_t *q, mq_prob_t *p, bool keep)
{
    p->kind = K_CHOICE;
    p->ans = 0;
    if (!keep)
        shuffle(q, p);
}
#define FR(n, d) ((int32_t)(n) * 1000 + (d))
#define PL(n, w) ((n) == 1 ? w : w "s") /* a word, plural after any number but 1 */

/* digit-wise sum without carrying (the "no regroup" error) */
static int32_t no_carry_add(int32_t a, int32_t b)
{
    int32_t r = 0, m = 1;
    while (a || b) {
        r += ((a % 10 + b % 10) % 10) * m;
        a /= 10, b /= 10, m *= 10;
    }
    return r;
}
/* digit-wise |difference| (subtracting the smaller digit from the bigger) */
static int32_t flip_sub(int32_t a, int32_t b)
{
    int32_t r = 0, m = 1;
    while (a || b) {
        r += iabs(a % 10 - b % 10) * m;
        a /= 10, b /= 10, m *= 10;
    }
    return r;
}
/* a x b (b one digit) with every carry dropped */
static int32_t no_carry_mul(int32_t a, int b)
{
    int32_t r = 0, m = 1;
    while (a) {
        r += ((a % 10) * b % 10) * m;
        a /= 10, m *= 10;
    }
    return r;
}
static int32_t pow10i(int k)
{
    int32_t r = 1;
    while (k-- > 0)
        r *= 10;
    return r;
}
static int32_t round_to(int32_t n, int32_t unit) { return (n + unit / 2) / unit * unit; }

/* ------------------------------------------------------------------ the rung's levels */
/* the problem's level in dimension d (mq_rung.c: four dimensions of 4 bits) */
#define FX(d, lv) (p->cx = (uint16_t)((p->cx & ~(0xFu << (4 * (d)))) | ((((unsigned)(lv)) & 15u) << (4 * (d)))))
/* The level of this problem in dimension `dim`: the rung's new complication at its new level most
 * of the time (else a lower one: the new step mixed with the old), every other dimension at any
 * level the rung has reached, half the time its highest. Rung 0 is level 0 everywhere. */
static int lvl(mq_t *q, mq_prob_t *p, int dim)
{
    int r = p->diff, mx = mq_rung_max(p->skill, r, dim), v = 0;
    if (mx > 0)
        v = mq_rung_dim(p->skill, r) == dim ? (R(q, 0, 4) ? mx : R(q, 0, mx - 1)) : (R(q, 0, 1) ? mx : R(q, 0, mx));
    FX(dim, v);
    return v;
}
static int pick(mq_t *q, const uint8_t *a, int n) { return a[R(q, 0, n - 1)]; }
static int imax(int a, int b) { return a > b ? a : b; }

/* digits, carries, zeros: what makes a column sum hard */
static int ndig(int32_t v)
{
    int n = 1;
    for (v = v < 0 ? -v : v; v >= 10; v /= 10)
        n++;
    return n;
}
static int32_t rnd_nd(mq_t *q, int nd) { return nd <= 1 ? R(q, 1, 9) : R(q, (int)pow10i(nd - 1), (int)pow10i(nd) - 1); }
/* the carries of x + y (count; *mask: bit i = a carry out of column i) */
static int carry_n(int32_t x, int32_t y, int *mask)
{
    int c = 0, k = 0, m = 0;
    for (int i = 0; x || y || c; i++) {
        int s = (int)(x % 10 + y % 10) + c;
        c = s >= 10;
        if (c)
            k++, m |= 1 << i;
        x /= 10, y /= 10;
        if (!x && !y)
            break;
    }
    if (mask)
        *mask = m;
    return k;
}
/* a zero between the first digit and the ones (405, 4003, 600) */
static bool inner_zero(int32_t n)
{
    if (n < 100)
        return false;
    for (n /= 10; n >= 10; n /= 10)
        if (n % 10 == 0)
            return true;
    return false;
}
/* any carry in a x b (b one digit) */
static int mul_carry(int32_t a, int b)
{
    int c = 0, k = 0;
    for (; a; a /= 10) {
        int s = (int)(a % 10) * b + c;
        c = s / 10;
        k += c > 0;
    }
    return k;
}
/* x + y = s: x with nd digits, y with nd2, exactly k carries (k 1 at sizes up to 2-digit: in the ones),
 * zero 0: no zero inside x, y or s; 1: one inside x; 2: one inside s with the ones needing a ten (trading
 * across a zero); s below lim */
static bool colsum(mq_t *q, int32_t *xo, int32_t *yo, int nd, int nd2, int k, bool more, int zero, int32_t lim)
{
    for (int t = 0; t < 900; t++) {
        int32_t x = rnd_nd(q, nd), y = rnd_nd(q, nd2), s = x + y;
        int mask, c = carry_n(x, y, &mask);
        if (s >= lim || (more ? c < k : c != k))
            continue;
        if (k == 1 && !more && nd <= 2 && mask != 1)
            continue;
        bool zx = inner_zero(x), zy = inner_zero(y), zs = inner_zero(s);
        if (zero == 0 && (zx || zy || zs))
            continue;
        if (zero == 1 && !(zx && !zy && (!zs || c > 1))) /* (two carries over a zero leave a 0 in the sum) */
            continue;
        if (zero == 2 && !(zs && (mask & 1)))
            continue;
        *xo = x, *yo = y;
        return true;
    }
    return false;
}

/* choices for "which is more?": the left one, "same", the right one (the bins of Sort Slide say what
 * they mean: the answer is the one picked, never a symbol); right: -1 the right one is bigger, 0 the
 * same, 1 the left one; picking the smaller one is flip_mc */
static void cmp_pick(mq_t *q, mq_prob_t *p, int right, int flip_mc, const char *la, const char *lb)
{
    q->api->snprintf(p->choice[0], sizeof p->choice[0], "%s", la);
    q->api->snprintf(p->choice[1], sizeof p->choice[1], "the same");
    /* (two equal ones look the same and stay two tiles: the right one ends in a space) */
    q->api->snprintf(p->choice[2], sizeof p->choice[2], q->api->strcmp(la, lb) ? "%s" : "%s ", lb);
    p->choice_mc[0] = (uint8_t)(right > 0 ? MC_NONE : right < 0 ? flip_mc : MC_CARELESS);
    p->choice_mc[1] = (uint8_t)(right == 0 ? MC_NONE : MC_CARELESS);
    p->choice_mc[2] = (uint8_t)(right < 0 ? MC_NONE : right > 0 ? flip_mc : MC_CARELESS);
    p->nchoice = 3;
    p->kind = K_CHOICE;
    p->ans = right > 0 ? 0 : right == 0 ? 1 : 2;
}
static void cmp_num(mq_t *q, mq_prob_t *p, int32_t a, int32_t b, int flip_mc)
{
    char la[16], lb[16];
    q->api->snprintf(la, sizeof la, "%ld", (long)a);
    q->api->snprintf(lb, sizeof lb, "%ld", (long)b);
    cmp_pick(q, p, a < b ? -1 : a > b, flip_mc, la, lb);
}
static void cmp_frac(mq_t *q, mq_prob_t *p, int a1, int d1, int a2, int d2, int flip_mc)
{
    char la[16], lb[16];
    q->api->snprintf(la, sizeof la, "%d/%d", a1, d1);
    q->api->snprintf(lb, sizeof lb, "%d/%d", a2, d2);
    int l = a1 * d2, r = a2 * d1;
    cmp_pick(q, p, l < r ? -1 : l > r, flip_mc, la, lb);
}

static const char *cast(mq_t *q, int slot) { return mq_cast_name(q, slot); }

/* ------------------------------------------------------------------ generators */
static void g_garden(mq_t *q, mq_prob_t *p, int d)
{
    static const uint8_t f0[3] = {2, 5, 10}, f1[2] = {3, 4};
    int a, b, t;
    (void)d;
    switch (p->skill) {
    case S_GROUPS: { /* the size of each group (2/5/10, 3/4, 6-9), then many groups */
        int lf = lvl(q, p, 0), lg = lvl(q, p, 1);
        b = lf == 0 ? pick(q, f0, 3) : lf == 1 ? pick(q, f1, 2) : R(q, 6, 9);
        a = lg ? R(q, 6, 9) : R(q, 2, 5);
        mq_story(q, p, ST_GROUPS, -1, a, b, 0, NULL);
        p->ans = a * b, p->vis = V_ARRAY, p->v[0] = (int16_t)a, p->v[1] = (int16_t)b, p->v[3] = 1;
        W(p, a + b, MC_ADD_NOT_MUL), W(p, b, MC_ONE_GROUP), W(p, a * b + b, MC_NEIGHBOR), W(p, a * b - b, MC_NEIGHBOR);
        p->mc_default = MC_NEIGHBOR;
        p->idea = IDEA_GROUPS;
        break;
    }
    case S_SHARE: { /* the number shared by: 2, then 5/10, 3/4, 6-9; then bigger shares */
        int lf = lvl(q, p, 0), ls = lvl(q, p, 1);
        a = lf == 0 ? 2 : lf == 1 ? (R(q, 0, 1) ? 5 : 10) : lf == 2 ? R(q, 3, 4) : R(q, 6, 9);
        b = ls ? R(q, 6, 9) : R(q, 2, 5);
        mq_story(q, p, ST_SHARE, -1, a * b, a, 0, NULL);
        p->ans = b, p->vis = V_GROUPS, p->v[0] = (int16_t)a, p->v[1] = (int16_t)b;
        W(p, a * b - a, MC_WRONG_OP), W(p, a, MC_DIV_MIX), W(p, b + 1, MC_NEIGHBOR), W(p, b - 1, MC_NEIGHBOR);
        p->mc_default = MC_DIV_MIX;
        p->idea = IDEA_SHARE;
        break;
    }
    case S_MDWORD: {
        int kind = lvl(q, p, 0), big = lvl(q, p, 1);
        a = big ? R(q, 3, 9) : R(q, 2, 5), b = big ? R(q, 3, 9) : R(q, 2, 5);
        if (kind == 3) { /* mixed operations: which one does the story need? */
            static const char op[4][4] = {"\xC3\x97", "\xC3\xB7", "+", "\xE2\x88\x92"};
            int which = R(q, 0, 3);
            if (which == 0)
                TX(0, "%d bees each visit %d flowers.", a, b), TX(1, "To count every visit,");
            else if (which == 1)
                TX(0, "%d seeds go in %d pots, the same", a * b, a), TX(1, "in each. To fill one pot,");
            else if (which == 2)
                TX(0, "%d tulips, then %d more bloom.", a, b), TX(1, "To count them all,");
            else
                TX(0, "%d snails, %d slide away.", a + b, b), TX(1, "To see who stays,");
            TX(2, "which machine helps?");
            CAP("Which machine helps?");
            p->kind = K_CHOICE;
            C(q, p, op[which], MC_NONE);
            for (int k = 1; k < 4; k++)
                C(q, p, op[(which + k) % 4], MC_WRONG_OP);
            choices_done(q, p, false);
            p->vis = V_NONE;
            p->mc_default = MC_WRONG_OP;
            p->a[0] = (int16_t)a, p->a[1] = (int16_t)b;
            break;
        }
        if (kind == 0) {
            mq_story(q, p, ST_GROUPS, -1, a, b, 0, NULL);
            p->ans = a * b;
            W(p, a + b, MC_ADD_NOT_MUL), W(p, a * b - b, MC_NEIGHBOR), W(p, a * b + b, MC_NEIGHBOR);
        } else if (kind == 1) {
            mq_story(q, p, ST_PERONE, -1, a * b, a, 0, NULL);
            p->ans = b;
            W(p, a * b - a, MC_WRONG_OP), W(p, a, MC_DIV_MIX);
        } else {
            mq_story(q, p, ST_HOWMANY, -1, a * b, b, 0, NULL);
            p->ans = a;
            W(p, a * b - b, MC_WRONG_OP), W(p, a * b + b, MC_WRONG_OP), W(p, b, MC_DIV_MIX);
        }
        p->vis = V_ARRAY, p->v[0] = (int16_t)a, p->v[1] = (int16_t)b, p->v[3] = 2;
        p->mc_default = MC_WRONG_OP;
        break;
    }
    case S_UNKNOWN: { /* where the unknown is: a x ? = c, ? x b = c, ? / a = b, c / ? = b; then bigger */
        int form = lvl(q, p, 0), big = lvl(q, p, 1);
        a = big ? R(q, 2, 9) : R(q, 2, 5), b = big ? R(q, 2, 9) : R(q, 2, 5);
        TX(0, "The wizard's secret number:");
        if (form == 0) {
            TX(1, "%d \xC3\x97 ? = %d", a, a * b);
            p->ans = b;
            W(p, a * b - a, MC_WRONG_OP), W(p, a * b + a, MC_WRONG_OP), W(p, b + 1, MC_NEIGHBOR), W(p, b - 1, MC_NEIGHBOR);
        } else if (form == 1) {
            TX(1, "? \xC3\x97 %d = %d", b, a * b);
            p->ans = a;
            W(p, a * b - b, MC_WRONG_OP), W(p, a * b + b, MC_WRONG_OP), W(p, a + 1, MC_NEIGHBOR), W(p, a - 1, MC_NEIGHBOR);
        } else if (form == 2) {
            TX(1, "? \xC3\xB7 %d = %d", a, b);
            p->ans = a * b;
            W(p, b, MC_DIV_MIX), W(p, a + b, MC_ADD_NOT_MUL), W(p, b > a ? b - a : a - b, MC_WRONG_OP);
        } else {
            TX(1, "%d \xC3\xB7 ? = %d", a * b, b);
            p->ans = a;
            W(p, a * b * b, MC_WRONG_OP), W(p, a * b - b, MC_WRONG_OP), W(p, a + 1, MC_NEIGHBOR);
        }
        p->vis = V_ARRAY, p->v[0] = (int16_t)a, p->v[1] = (int16_t)b;
        p->mc_default = MC_NEIGHBOR;
        break;
    }
    case S_PROPS: { /* turn around, split off 5, split off 2, three factors, any split */
        int kind = lvl(q, p, 0);
        a = R(q, 3, 9), b = R(q, 6, 9);
        if (kind == 0) {
            TX(0, "Turn the flower bed around:");
            TX(1, "%d \xC3\x97 %d = %d \xC3\x97 ?", a, b, b);
            p->ans = a;
            W(p, b, MC_CARELESS), W(p, a * b, MC_ONE_STEP);
            p->vis = V_ARRAY, p->v[0] = (int16_t)a, p->v[1] = (int16_t)b, p->v[6] = 0x81;
        } else if (kind == 3) {
            int c = R(q, 2, 5);
            a = R(q, 2, 3);
            TX(0, "Stack the flower pots your way:");
            TX(1, "%d \xC3\x97 %d \xC3\x97 %d = %d \xC3\x97 ?", a, c, 2, a, c);
            p->ans = 2 * c;
            W(p, c + 2, MC_ADD_NOT_MUL), W(p, a * c * 2, MC_ONE_STEP);
            p->vis = V_ARRAY, p->v[0] = (int16_t)a, p->v[1] = (int16_t)(2 * c), p->v[6] = 0x82;
        } else {
            int first = kind == 1 ? 5 : kind == 2 ? 2 : R(q, 1, b - 2); /* the part split off first */
            if (kind == 2)
                b = R(q, 4, 9);
            int c = b - first;
            TX(0, "Split the flower bed in two:");
            TX(1, "%d \xC3\x97 %d = %d \xC3\x97 %d + %d \xC3\x97 ?", a, b, a, first, a);
            p->ans = c;
            W(p, b, MC_PARTIAL_PRODUCT), W(p, a * c, MC_ONE_STEP);
            if (first != c)
                W(p, first, MC_CARELESS);
            p->vis = V_ARRAY, p->v[0] = (int16_t)a, p->v[1] = (int16_t)b, p->v[2] = (int16_t)first;
            p->v[6] = 0x80; /* the rows and columns are given (a row count equal to the answer is no "?") */
        }
        p->mc_default = MC_PARTIAL_PRODUCT;
        break;
    }
    case S_DIVFACT: { /* by 2, 5, 10; by 3, 4; by 6, 7; by 8, 9 */
        static const uint8_t fam[4][3] = {{2, 5, 10}, {3, 4, 3}, {6, 7, 6}, {8, 9, 8}};
        int lf = lvl(q, p, 0);
        a = fam[lf][R(q, 0, lf ? 1 : 2)], b = R(q, 2, 9);
        TX(0, "%d \xC3\xB7 %d = ?", a * b, a);
        TX(1, "(Psst: %d \xC3\x97 what makes %d?)", a, a * b);
        p->ans = b;
        p->vis = V_ARRAY, p->v[0] = (int16_t)a, p->v[1] = (int16_t)b;
        W(p, a * b - a, MC_WRONG_OP), W(p, b + 1, MC_NEIGHBOR), W(p, b - 1, MC_NEIGHBOR), W(p, a, MC_DIV_MIX);
        p->mc_default = MC_NEIGHBOR;
        p->idea = IDEA_SHARE;
        break;
    }
    case S_TWOSTEP: { /* add then add, groups then add, groups then take, share then add; bigger */
        int kind = lvl(q, p, 0), big = lvl(q, p, 1);
        a = big ? R(q, 3, 7) : R(q, 2, 4), b = big ? R(q, 4, 9) : R(q, 2, 5);
        const char *nm = cast(q, R(q, 0, 6));
        if (kind == 0) {
            int c = big ? R(q, 4, 9) : R(q, 2, 5);
            a = big ? R(q, 6, 19) : R(q, 2, 6);
            TX(0, "%s picks %d apples, then %d more,", nm, a, b);
            TX(1, "then %d more. How many apples now?", c);
            p->ans = a + b + c;
            W(p, a + b, MC_ONE_STEP), W(p, b + c, MC_ONE_STEP), W(p, a + b + c + 1, MC_CARELESS);
            p->vis = V_NONE;
        } else if (kind == 1) {
            int c = R(q, 2, 9);
            TX(0, "%s fills %d bags with %d seeds each,", nm, a, b);
            TX(1, "then finds %d more seeds.", c);
            TX(2, "How many seeds in all?");
            p->ans = a * b + c;
            W(p, a * b, MC_ONE_STEP), W(p, a + b + c, MC_ADD_NOT_MUL), W(p, a * (b + c), MC_WRONG_OP);
            p->vis = V_ARRAY, p->v[0] = (int16_t)a, p->v[1] = (int16_t)b;
        } else if (kind == 2) {
            int c = R(q, 2, a * b - 2);
            mq_story(q, p, ST_TAKE, -1, a, b, c, NULL);
            p->ans = a * b - c;
            W(p, a * b, MC_ONE_STEP), W(p, a + b - c, MC_ADD_NOT_MUL), W(p, a * b + c, MC_WRONG_OP);
            p->vis = V_ARRAY, p->v[0] = (int16_t)a, p->v[1] = (int16_t)b;
        } else {
            int c = R(q, 2, 9);
            mq_story(q, p, ST_SHAREADD, -1, a, a * b, c, NULL);
            p->ans = b + c;
            W(p, b, MC_ONE_STEP), W(p, a * b + c, MC_ONE_STEP), W(p, b * c, MC_WRONG_OP);
            p->vis = V_ARRAY, p->v[0] = (int16_t)a, p->v[1] = (int16_t)b;
        }
        p->mc_default = MC_ONE_STEP;
        break;
    }
    case S_PATTERN: { /* steps of 2/5/10, 3/4, 6-9; odd or even sums, then rows */
        int step = lvl(q, p, 0), oe = lvl(q, p, 1);
        if (oe == 2) {
            a = R(q, 2, 9), b = R(q, 2, 9);
            TX(0, "%d rows of %d buttons: odd or even?", a, b);
            TX(1, "(Even times anything is even.)");
            CAP("%d rows of %d: odd or even?", a, b);
            bool even = (a * b) % 2 == 0;
            C(q, p, even ? "even" : "odd", MC_NONE);
            C(q, p, even ? "odd" : "even", MC_PATTERN_STEP);
            choices_done(q, p, false);
            p->vis = V_ARRAY, p->v[0] = (int16_t)a, p->v[1] = (int16_t)b;
        } else if (oe == 1) {
            a = R(q, 2, 19), b = R(q, 2, 19);
            TX(0, "%d red buttons and %d blue buttons.", a, b);
            TX(1, "All together: odd or even?");
            TX(2, "(Psst: can they all make pairs?)");
            bool even = (a + b) % 2 == 0;
            C(q, p, even ? "even" : "odd", MC_NONE);
            C(q, p, even ? "odd" : "even", MC_PATTERN_STEP);
            choices_done(q, p, false);
            p->vis = V_ARRAY, p->v[0] = 2, p->v[1] = (int16_t)((a + b) / 2), p->v[5] = (int16_t)((a + b) % 2);
        } else {
            a = R(q, 1, 9), b = step == 0 ? pick(q, f0, 3) : step == 1 ? pick(q, f1, 2) : R(q, 6, 9);
            TX(0, "The flowers grow in a pattern:");
            TX(1, "%d, %d, %d, %d, ?", a, a + b, a + 2 * b, a + 3 * b);
            p->ans = a + 4 * b;
            W(p, a + 3 * b + 1, MC_PATTERN_STEP), W(p, a + 5 * b, MC_PATTERN_STEP), W(p, a + 3 * b + 2, MC_PATTERN_STEP);
            p->vis = V_PATTERN, p->v[0] = (int16_t)a, p->v[1] = (int16_t)b;
            FX(1, 0);
        }
        if (oe)
            FX(0, 0); /* (an odd-or-even order has no step) */
        p->a[0] = (int16_t)a, p->a[1] = (int16_t)b;
        p->mc_default = MC_PATTERN_STEP;
        break;
    }
    case S_TIMES_AS: { /* times as many; the other way round; bigger; how many times as many? */
        int big = lvl(q, p, 0), form = lvl(q, p, 1);
        a = big ? R(q, 3, 9) : R(q, 2, 5), b = big ? R(q, 3, 9) : R(q, 2, 3);
        if (form == 0) {
            mq_story(q, p, ST_TIMES, -1, a, b, 0, NULL);
            p->ans = a * b;
            W(p, a + b, MC_TIMES_AS_ADD), W(p, a * b - a, MC_NEIGHBOR);
        } else if (form == 1) {
            TX(0, "The big pot has %d seeds. That is", a * b);
            TX(1, "%d times as many as the small pot.", b);
            TX(2, "How many seeds in the small pot?");
            p->ans = a;
            W(p, a * b - b, MC_TIMES_AS_ADD), W(p, a * b * b, MC_WRONG_OP);
        } else {
            TX(0, "%s has %d shells, %s has %d.", cast(q, 1), a * b, cast(q, 3), a);
            TX(1, "How many times as many shells");
            TX(2, "does %s have?", cast(q, 1));
            p->ans = b;
            W(p, a * b - a, MC_TIMES_AS_ADD), W(p, a, MC_WRONG_OP);
        }
        p->vis = V_GROUPS, p->v[0] = (int16_t)b, p->v[1] = (int16_t)a;
        p->mc_default = MC_TIMES_AS_ADD;
        break;
    }
    case S_REMWORD: { /* left over; full groups; needed (one more); bigger */
        int kind = lvl(q, p, 0), big = lvl(q, p, 1);
        int c = big ? R(q, 3, 9) : R(q, 3, 5), k = big ? R(q, 3, 9) : R(q, 2, 5), r = R(q, 1, c - 1);
        int n = c * k + r;
        if (kind == 2) {
            mq_story(q, p, ST_BUS, -1, n, c, 0, NULL);
            p->ans = k + 1;
            W(p, k, MC_REM_ROUND), W(p, r, MC_REM_IGNORED);
            p->mc_default = MC_REM_ROUND;
        } else if (kind == 1) {
            mq_story(q, p, ST_FULL, -1, n, c, 0, NULL);
            p->ans = k;
            W(p, k + 1, MC_REM_ROUND), W(p, r, MC_REM_IGNORED);
            p->mc_default = MC_REM_IGNORED;
        } else {
            mq_story(q, p, ST_LEFT, -1, n, c, 0, NULL);
            p->ans = r;
            W(p, k, MC_REM_IGNORED), W(p, 0, MC_REM_IGNORED), W(p, r + c, MC_REM_BIG);
            p->mc_default = MC_REM_IGNORED;
        }
        p->vis = V_GROUPS, p->v[0] = (int16_t)k, p->v[1] = (int16_t)c, p->v[2] = (int16_t)r;
        p->a[0] = (int16_t)n, p->a[1] = (int16_t)c;
        p->idea = IDEA_LEFTOVER;
        break;
    }
    case S_FACTORS: { /* numbers up to 24, 50, 100; the missing side, multiples, one long row */
        int sz = lvl(q, p, 0), kind = lvl(q, p, 1);
        int lim = sz == 0 ? 24 : sz == 1 ? 50 : 100, lo = sz == 0 ? 4 : sz == 1 ? 25 : 51;
        if (kind == 2) {
            static const uint8_t pr[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73, 79,
                                         83, 89, 97};
            static const uint8_t oc[] = {9, 15, 21, 25, 27, 33, 35, 39, 45, 49, 51, 55, 57, 63, 65, 69, 75, 77, 81, 85, 87,
                                         91, 93, 95, 99};
            bool prime = R(q, 0, 1);
            int n = 0;
            for (int tr = 0; tr < 60 && !n; tr++) {
                int c = prime ? pr[R(q, 0, 24)] : oc[R(q, 0, 24)];
                if (c <= lim && (sz == 0 || c >= lo / 2))
                    n = c;
            }
            if (!n)
                n = prime ? 7 : 9;
            TX(0, "Can %d flowers make a bed of rows?", n);
            CAP("%d flowers: one row, or rows?", n);
            C(q, p, prime ? "one long row" : "rows", MC_NONE); /* (game words: not the school ones) */
            C(q, p, prime ? "rows" : "one long row", prime ? MC_FACTOR_MISS : MC_ODD_PRIME);
            choices_done(q, p, false);
            p->a[0] = (int16_t)n;
            int f = 1; /* the array of n: 1 row if it has no rows, else its smallest factor as rows */
            for (int k = 2; k * k <= n; k++)
                if (n % k == 0) {
                    f = k;
                    break;
                }
            p->vis = V_ARRAY, p->v[0] = (int16_t)f, p->v[1] = (int16_t)(n / f);
            FX(0, n > 50 ? 2 : n > 24 ? 1 : 0);
        } else if (kind == 1) {
            a = R(q, 3, 9);
            b = R(q, 2, imax(2, lim / a - 1));
            if (a * b + 2 > lim)
                b = imax(2, (lim - 2) / a);
            TX(0, "Which pile packs into bags of %d?", a);
            char s[8];
            q->api->snprintf(s, sizeof s, "%d", a * b);
            C(q, p, s, MC_NONE);
            /* distractors that are not multiples: one more, a + b (adding), one less than a multiple */
            int cand[5] = {a * b + 1, a + b, a * b - 1, a * (b + 1) + 1, a * b + 2}, used[4] = {a * b}, nu = 1;
            for (int i = 0; i < 5 && p->nchoice < 4; i++) {
                bool dup = cand[i] % a == 0;
                for (int j = 0; j < nu; j++)
                    dup |= used[j] == cand[i];
                if (dup)
                    continue;
                used[nu++] = cand[i];
                q->api->snprintf(s, sizeof s, "%d", cand[i]);
                C(q, p, s, i == 1 ? MC_ADD_NOT_MUL : MC_FACTOR_MISS);
            }
            choices_done(q, p, false);
            p->vis = V_ARRAY, p->v[0] = (int16_t)a, p->v[1] = (int16_t)b;
            p->a[0] = (int16_t)a, p->a[1] = (int16_t)b;
            FX(0, a * b + 2 > 50 ? 2 : a * b + 2 > 24 ? 1 : 0);
        } else {
            for (int tr = 0; tr < 80; tr++) {
                a = R(q, 2, 9), b = R(q, 2, 12);
                if (a * b <= lim && (sz == 0 || a * b >= lo))
                    break;
            }
            TX(0, "The flower bed's missing side:");
            TX(1, "%d = %d \xC3\x97 ?", a * b, a);
            p->ans = b;
            W(p, a * b - a, MC_WRONG_OP), W(p, b + 1, MC_NEIGHBOR);
            p->vis = V_ARRAY, p->v[0] = (int16_t)a, p->v[1] = (int16_t)b;
            FX(0, a * b > 50 ? 2 : a * b > 24 ? 1 : 0);
        }
        p->mc_default = MC_FACTOR_MISS;
        break;
    }
    case S_RULE: { /* the next by adding, by doubling; the 6th one; what does it add? */
        int kind = lvl(q, p, 0);
        a = R(q, 1, 9), b = R(q, 2, 6);
        if (kind == 0) {
            TX(0, "Rule: start at %d and add %d.", a, b);
            TX(1, "%d, %d, %d, ?", a, a + b, a + 2 * b);
            p->ans = a + 3 * b;
            W(p, a + 2 * b + 1, MC_PATTERN_STEP), W(p, a + 4 * b, MC_PATTERN_STEP);
            p->vis = V_PATTERN, p->v[0] = (int16_t)a, p->v[1] = (int16_t)b;
        } else if (kind == 1) {
            a = R(q, 1, 5);
            TX(0, "Rule: start at %d and double.", a);
            TX(1, "%d, %d, %d, ?", a, 2 * a, 4 * a);
            p->ans = 8 * a;
            W(p, 6 * a, MC_PATTERN_STEP), W(p, 5 * a, MC_PATTERN_STEP);
            p->vis = V_PATTERN, p->v[0] = (int16_t)a, p->v[1] = 0, p->v[2] = 2;
        } else if (kind == 2) {
            TX(0, "Rule: start at %d and add %d.", a, b);
            TX(1, "Which number does the 6th frog wear?");
            CAP("Start %d, add %d: frog 6?", a, b);
            p->ans = a + 5 * b;
            W(p, a + 6 * b, MC_PATTERN_STEP), W(p, a + 4 * b, MC_PATTERN_STEP), W(p, 6 * b, MC_PATTERN_STEP);
            p->vis = V_PATTERN, p->v[0] = (int16_t)a, p->v[1] = (int16_t)b, p->v[3] = 6; /* (the "?" on the 6th frog) */
        } else {
            TX(0, "The frogs wear %d, %d, %d, %d.", a, a + b, a + 2 * b, a + 3 * b);
            TX(1, "What does the rule add each time?");
            p->ans = b;
            W(p, b + 1, MC_PATTERN_STEP), W(p, a, MC_PATTERN_STEP), W(p, a + 3 * b, MC_ONE_STEP);
            p->vis = V_PATTERN, p->v[0] = (int16_t)a, p->v[1] = (int16_t)b;
        }
        p->mc_default = MC_PATTERN_STEP;
        break;
    }
    }
    (void)t;
    if (!p->a[0])
        p->a[0] = (int16_t)p->v[0], p->a[1] = (int16_t)p->v[1];
}

/* a number of nd digits with no zero digit (zero: one zero in the tens or the ones) */
static int32_t no_zero(mq_t *q, int nd, bool zero)
{
    int32_t n = 0;
    for (int i = 0; i < nd; i++)
        n = n * 10 + R(q, 1, 9);
    if (zero && nd >= 2) {
        int place = nd >= 3 ? R(q, 0, 1) : 0; /* the ones or (3 digits) the tens */
        int32_t m = pow10i(place);
        n -= (n / m % 10) * m;
    }
    return n;
}

/* Adding and taking away in columns (S_ADDSUB, S_ADDSUBBIG): the rung's size, carries and zeros */
static void g_colsum(mq_t *q, mq_prob_t *p)
{
    bool big = p->skill == S_ADDSUBBIG;
    int sz = lvl(q, p, 0), k = lvl(q, p, 1), z = lvl(q, p, 2);
    bool sub = R(q, 0, 1);
    int nd, nd2;
    int32_t lim;
    if (big) {
        nd = sz ? 5 : 4, nd2 = sz ? (R(q, 0, 1) ? 5 : 4) : 4, lim = sz ? 100000 : 10000;
    } else {
        if (z)
            sz = 3;
        if (k == 2 && sz == 0)
            sz = 2;
        nd = sz == 0 ? 1 : sz <= 2 ? 2 : 3, nd2 = sz <= 1 ? 1 : sz == 2 ? 2 : (R(q, 0, 2) ? 3 : 2), lim = 1000;
        if (sz == 0)
            k = imin(k, 1);
    }
    int32_t x = 0, y = 0;
    bool ok;
    if (z && sub) { /* trading across a zero: the top number has a zero inside, the ones need a ten */
        ok = colsum(q, &x, &y, nd, nd2, 1, true, 2, lim);
    } else
        ok = colsum(q, &x, &y, nd, nd2, k, big && k == 2, z ? 1 : 0, lim);
    for (int k2 = k - 1; !ok && k2 >= 0; k2--) /* (fewer carries rather than the safe one) */
        ok = colsum(q, &x, &y, nd, nd2, k2, false, z ? 1 : 0, lim);
    if (!ok) /* (never on the ladders' sizes; a safe one) */
        x = big ? 4321 : sz == 0 ? 3 : 23, y = big ? 1254 : sz == 0 ? 4 : 14;
    if (R(q, 0, 1) && !(z && !sub)) { /* (either one first) */
        int32_t t = x;
        x = y, y = t;
    }
    int32_t a, b;
    if (sub) {
        a = x + y, b = x; /* a - b = y */
        mq_story(q, p, ST_SUB, -1, a, b, 0, NULL);
        p->ans = y;
        bool across = inner_zero(a) && (a % 10 < b % 10);
        W(p, flip_sub(a, b), across ? MC_NO_BORROW_ZERO : MC_SUB_FLIP);
        W(p, a + b, MC_WRONG_OP);
        if (b % 10 > a % 10) /* the ten traded but not taken from the tens (across a zero: the 0 made 10, not 9) */
            W(p, a - b + 10, across ? MC_NO_BORROW_ZERO : MC_SUB_FLIP);
        if (across)
            W(p, a - b + 100, MC_NO_BORROW_ZERO); /* the hundred never traded */
        p->mc_default = across ? MC_NO_BORROW_ZERO : MC_SUB_FLIP;
        p->a[2] = (int16_t)(across && a < 1000 && a / 10 % 10 == 0 ? 2 : 1);
        p->ws = WS_SUB;
        p->idea = carry_n(x, y, NULL) ? IDEA_TRADE : IDEA_NONE;
    } else {
        a = x, b = y;
        mq_story(q, p, ST_ADD, -1, a, b, 0, NULL);
        p->ans = a + b;
        if (carry_n(a, b, NULL)) {
            W(p, no_carry_add(a, b), MC_NO_REGROUP);
            W(p, a + b - 10, MC_NO_REGROUP);
        }
        W(p, a > b ? a - b : b - a, MC_WRONG_OP);
        p->mc_default = MC_NO_REGROUP;
        p->a[2] = 0;
        p->ws = WS_ADD;
        p->idea = carry_n(a, b, NULL) ? IDEA_CARRY : IDEA_NONE;
    }
    /* the levels the numbers really have */
    int c = carry_n(x, y, NULL), dmax = imax(ndig(x), ndig(y)), dmin = imin(ndig(x), ndig(y));
    bool zr = sub ? inner_zero(a) : inner_zero(x) || inner_zero(y);
    if (big)
        FX(0, dmax >= 5 ? 1 : 0), FX(1, c >= 2 ? 2 : c), FX(2, zr);
    else
        FX(0, dmax == 1 ? 0 : dmax == 2 ? (dmin == 1 ? 1 : 2) : 3), FX(1, c > 2 ? 2 : c), FX(2, zr);
    if (!big && dmin < 2 && !c)
        p->ws = WS_NONE; /* (a 1-digit sum, nothing to carry: in her head) */
    p->vis = V_BASE10, p->big = a, p->v[1] = (int16_t)(b > 32767 ? 0 : b);
    p->a[0] = (int16_t)(a % 30000), p->a[1] = (int16_t)(b % 30000);
    p->op[0] = a, p->op[1] = b;
}

/* sharing in columns (S_DIVIDE): nd digits by one digit; each digit shares (no trade), or a ten is
 * traded down, a leftover */
static void g_divide(mq_t *q, mq_prob_t *p)
{
    int sz = lvl(q, p, 0), trade = lvl(q, p, 1), left = lvl(q, p, 2);
    int nd = sz == 0 ? 2 : sz == 1 ? 3 : 4;
    int32_t n = 0, qt = 0;
    int dv = 2, r = 0;
    bool found = false;
    for (int t = 0; t < 400 && !found; t++) {
        dv = trade ? R(q, 2, 9) : R(q, 2, 4);
        if (!trade) { /* every digit a multiple of dv, the first at least dv */
            int32_t m = 0, qq = 0;
            for (int i = 0; i < nd; i++) {
                int qd = i == 0 ? R(q, 1, 9 / dv) : R(q, 0, 9 / dv);
                m = m * 10 + qd * dv, qq = qq * 10 + qd;
            }
            n = m, qt = qq;
        } else {
            int32_t lo = (pow10i(nd - 1) + dv - 1) / dv, hi = (pow10i(nd) - 1) / dv;
            if (lo < 10)
                lo = 10;
            if (lo > hi)
                continue;
            qt = R(q, (int)lo, (int)hi);
            n = qt * dv;
        }
        r = left ? R(q, 1, dv - 1) : 0;
        if (n + r >= pow10i(nd) || qt < 10)
            continue;
        /* the trades of the long division: a step whose part does not share evenly */
        int32_t rem = 0, num = n + r;
        int trades = 0;
        for (int32_t m = pow10i(nd - 1); m >= 1; m /= 10) {
            int32_t cur = rem * 10 + num / m % 10;
            rem = cur % dv;
            if (rem && m > 1)
                trades++;
        }
        if (trade ? !trades : trades)
            continue;
        n += r;
        found = true;
    }
    if (!found) /* (never on the ladder's sizes; a safe one) */
        dv = 4, qt = 21, r = left ? 1 : 0, n = 84 + r, trade = 0, nd = 2;
    if (left) {
        mq_story(q, p, ST_DIVR, -1, (int)n, dv, 0, NULL);
        p->kind = K_REM;
        p->ans = qt * 1000 + r;
        W(p, qt * 1000, MC_REM_IGNORED), W(p, (qt - 1) * 1000 + r + dv, MC_REM_BIG), W(p, (qt + 1) * 1000, MC_REM_ROUND);
        p->mc_default = MC_REM_BIG;
        p->idea = IDEA_LEFTOVER;
    } else {
        mq_story(q, p, ST_SHARE, -1, (int)n, dv, 0, NULL);
        p->ans = qt;
        W(p, qt + 1, MC_CLOSE), W(p, qt * 10, MC_PLACE_SHIFT), W(p, qt / 10, MC_PLACE_SHIFT);
        p->mc_default = MC_DIV_MIX;
        p->idea = IDEA_SHARE;
    }
    FX(0, nd - 2), FX(1, trade), FX(2, r > 0);
    p->vis = V_GROUPS, p->v[0] = (int16_t)dv, p->v[1] = (int16_t)(qt > 99 ? 0 : qt), p->v[2] = (int16_t)r;
    p->a[0] = (int16_t)(n > 30000 ? 0 : n), p->a[1] = (int16_t)dv;
    p->big = n;
    p->ws = WS_DIV, p->op[0] = n, p->op[1] = dv;
}

/* products in columns (S_MULTI, S_MULBIG): a x b */
static void g_product(mq_t *q, mq_prob_t *p)
{
    int32_t a = 0, b = 0;
    if (p->skill == S_MULBIG) {
        int sz = lvl(q, p, 0);
        a = sz == 0 ? R(q, 11, 30) : sz == 1 ? R(q, 12, 99) : sz == 2 ? R(q, 102, 999) : R(q, 1002, 9999);
        b = sz == 0 ? R(q, 11, 19) : R(q, 12, 99);
    } else {
        int sz = lvl(q, p, 0), carry = lvl(q, p, 1);
        int nd = sz == 0 ? 2 : sz == 1 ? 3 : sz == 2 ? 4 : 2;
        for (int t = 0; t < 400; t++) {
            if (sz == 3)
                a = R(q, 12, 99), b = R(q, 12, 99);
            else {
                b = carry ? R(q, 3, 9) : R(q, 2, 4);
                a = rnd_nd(q, nd);
                if (!carry) { /* every digit times b stays below 10 */
                    a = 0;
                    for (int i = 0; i < nd; i++)
                        a = a * 10 + R(q, 1, 9 / b);
                }
            }
            int c = sz == 3 ? mul_carry(a, (int)(b % 10)) + mul_carry(a, (int)(b / 10)) : mul_carry(a, (int)b);
            if ((c > 0) == (carry > 0) || sz == 3)
                break;
        }
        FX(1, (sz == 3 ? mul_carry(a, (int)(b % 10)) + mul_carry(a, (int)(b / 10)) : mul_carry(a, (int)b)) > 0);
    }
    if (b < 10) {
        mq_story(q, p, ST_MUL, -1, (int)b, (int)a, 0, NULL);
        p->ans = a * b;
        W(p, no_carry_mul(a, (int)b), MC_NO_REGROUP), W(p, a + b, MC_ADD_NOT_MUL);
        if (a % 10 && a >= 10) /* a piece lost: only the ones times b (the area model's other piece forgotten) */
            W(p, a % 10 * b, MC_PARTIAL_PRODUCT);
        p->mc_default = MC_NO_REGROUP;
    } else {
        if (p->skill == S_MULBIG)
            TX(0, "%s counts stars: %ld rows of %ld.", cast(q, 8), (long)b, (long)a);
        else
            TX(0, "%ld boxes hold %ld cards each.", (long)b, (long)a);
        TX(1, "%ld \xC3\x97 %ld = ?", (long)a, (long)b);
        p->ans = a * b;
        W(p, a * (b % 10) + a * (b / 10), MC_PLACE_SHIFT), W(p, a * (b % 10), MC_PARTIAL_PRODUCT);
        W(p, a * (b / 10) * 10, MC_PARTIAL_PRODUCT);
        p->mc_default = MC_PARTIAL_PRODUCT;
    }
    p->vis = V_GRID, p->v[0] = (int16_t)(a > 99 ? 0 : a), p->v[1] = (int16_t)(b > 99 ? 0 : b), p->v[2] = 4;
    p->a[0] = (int16_t)(a % 30000), p->a[1] = (int16_t)b;
    p->big = a;
    p->ws = WS_MUL, p->op[0] = a, p->op[1] = b;
}

static void g_post(mq_t *q, mq_prob_t *p, int d)
{
    int32_t a, b, n;
    const char *dot = cast(q, 1);
    (void)d;
    switch (p->skill) {
    case S_PV3: { /* tens and ones; hundreds; a zero inside; a digit's worth; the parts glued */
        int nd = lvl(q, p, 0) ? 3 : 2, zero = lvl(q, p, 1), kind = lvl(q, p, 2);
        n = no_zero(q, nd, zero);
        p->big = n, p->vis = V_BASE10;
        if (kind == 0) {
            if (nd == 3) {
                int h = (int)(n / 100), t = (int)(n / 10 % 10), o = (int)(n % 10);
                TX(0, "%s's parcel has %d %s,", dot, h, PL(h, "hundred"));
                TX(1, "%d %s and %d %s.", t, PL(t, "ten"), o, PL(o, "one"));
                CAP("%d %s, %d %s, %d %s!", h, PL(h, "hundred"), t, PL(t, "ten"), o, PL(o, "one"));
                W(p, n / 100 * 100 + n % 10 * 10 + n / 10 % 10, MC_PLACE_SHIFT);
            } else {
                int t = (int)(n / 10), o = (int)(n % 10);
                TX(0, "%s's parcel has %d %s and %d %s.", dot, t, PL(t, "ten"), o, PL(o, "one"));
                CAP("%d %s, %d %s!", t, PL(t, "ten"), o, PL(o, "one"));
                W(p, n % 10 * 10 + n / 10, MC_PLACE_SHIFT);
            }
            TX(2, "Which house number is that?");
            p->ans = n;
            W(p, n / 100 + n / 10 % 10 + n % 10, MC_PLACE_SHIFT);
        } else if (kind == 1) {
            int place = nd == 3 ? R(q, 1, 2) : 1;
            if (!(n / pow10i(place) % 10)) /* (the digit asked for is never the zero) */
                place = place == 1 ? 2 : 1;
            if (place >= nd)
                place = 1;
            int dig = (int)(n / pow10i(place) % 10);
            TX(0, "What is the %d worth in %ld?", dig, (long)n);
            CAP("The %d in %ld: worth?", dig, (long)n);
            p->ans = dig * pow10i(place);
            W(p, dig, MC_PLACE_SHIFT), W(p, dig * pow10i(place + 1), MC_PLACE_SHIFT);
            if (place > 1)
                W(p, dig * pow10i(place - 1), MC_PLACE_SHIFT);
        } else {
            TX(0, "Glue the parcels into one number:");
            if (nd == 3 && n / 10 % 10 == 0)
                TX(1, "%ld + %ld", (long)(n / 100 * 100), (long)(n % 10));
            else if (nd == 3 && n % 10 == 0)
                TX(1, "%ld + %ld", (long)(n / 100 * 100), (long)(n / 10 % 10 * 10));
            else if (nd == 3)
                TX(1, "%ld + %ld + %ld", (long)(n / 100 * 100), (long)(n / 10 % 10 * 10), (long)(n % 10));
            else
                TX(1, "%ld + %ld", (long)(n / 10 * 10), (long)(n % 10));
            p->ans = n;
            W(p, n / 100 + n / 10 % 10 + n % 10, MC_PLACE_SHIFT), W(p, n / 100 * 10 + n % 10, MC_ZEROS);
        }
        FX(1, (nd == 3 && (n / 10 % 10 == 0 || n % 10 == 0)) || (nd == 2 && n % 10 == 0));
        p->mc_default = MC_PLACE_SHIFT;
        p->idea = IDEA_PLACE;
        break;
    }
    case S_CMP3: { /* different hundreds; the same hundreds; the same hundreds and tens; equal or zeros */
        int kind = lvl(q, p, 0);
        a = R(q, 100, 999);
        if (kind == 0) {
            do
                b = R(q, 100, 999);
            while (b / 100 == a / 100);
            if ((a % 10 > b % 10) == (a > b)) /* the ones disagree with the hundreds: the tempting mistake */
                b = b / 10 * 10 + (a > b ? 9 : 0);
        } else if (kind == 1) {
            do
                b = a / 100 * 100 + R(q, 0, 99);
            while (b / 10 % 10 == a / 10 % 10 || b < 100);
        } else if (kind == 2) {
            do
                b = a / 10 * 10 + R(q, 0, 9);
            while (b == a);
        } else {
            if (R(q, 0, 1))
                b = a;
            else { /* the same digits with a zero moved: 507 and 570 */
                int h = R(q, 1, 9), x = R(q, 1, 9);
                a = h * 100 + x, b = h * 100 + x * 10;
                if (R(q, 0, 1)) {
                    int32_t t = a;
                    a = b, b = t;
                }
            }
        }
        TX(0, "Two parcels on the scale: %ld and %ld.", (long)a, (long)b);
        TX(1, "Which one is heavier?");
        cmp_num(q, p, a, b, MC_PLACE_SHIFT);
        p->vis = V_BASE10, p->big = a, p->v[2] = (int16_t)b, p->a[0] = (int16_t)a, p->a[1] = (int16_t)b;
        p->mc_default = MC_PLACE_SHIFT;
        p->idea = IDEA_PLACE;
        break;
    }
    case S_ROUND10: { /* 2-digit to 10, 3-digit to 10, to 100, halfway and 95 */
        int nd = lvl(q, p, 0) ? 3 : 2, u100 = lvl(q, p, 1), tricky = lvl(q, p, 2);
        if (u100)
            nd = 3, FX(0, 1);
        int32_t unit = u100 ? 100 : 10;
        if (tricky && R(q, 0, 1)) { /* exactly halfway: rounds up */
            n = nd == 3 ? R(q, 1, 9) * 100 + (unit == 10 ? R(q, 0, 9) * 10 + 5 : 50) : R(q, 1, 9) * 10 + 5;
        } else if (tricky) { /* the next round number crosses a place: 95 -> 100, 397 -> 400, 960 -> 1000 */
            n = unit == 10 ? (nd == 3 ? R(q, 1, 9) * 100 + 90 + R(q, 5, 9) : 90 + R(q, 5, 9)) : 900 + R(q, 5, 9) * 10 + R(q, 0, 9);
        } else {
            do
                n = nd == 3 ? R(q, 101, 989) : R(q, 11, 89);
            while (n % unit == 0 || n % unit == unit / 2 || n / 10 % 10 == 9 || (unit == 100 && n >= 950));
        }
        TX(0, "%s weighs a parcel: %ld g.", dot, (long)n);
        TX(1, "Hop it to the nearest %ld!", (long)unit);
        CAP("%ld g: hop to the nearest %ld!", (long)n, (long)unit);
        p->ans = round_to(n, unit);
        W(p, n / unit * unit, MC_ROUND_DOWN_ALWAYS), W(p, round_to(n, unit == 10 ? 100 : 10), MC_ROUND_DIGIT);
        W(p, n / unit * unit + unit, MC_ROUND_DIGIT);
        p->vis = V_NUMLINE, p->v[0] = (int16_t)(n / unit * unit), p->v[1] = (int16_t)(n / unit * unit + unit);
        p->v[2] = 10, p->v[3] = (int16_t)(n - n / unit * unit), p->v[4] = 2;
        p->a[0] = (int16_t)n, p->a[1] = (int16_t)unit;
        FX(2, n % unit == unit / 2 || (p->ans > n && p->ans % (unit * 10) == 0));
        p->mc_default = MC_ROUND_DIGIT;
        break;
    }
    case S_ADDSUB:
    case S_ADDSUBBIG: g_colsum(q, p); break;
    case S_MUL10: { /* x 10; 2-5 x tens; 6-9 x tens; a zero inside (5 x 80) */
        int kind = lvl(q, p, 0);
        if (kind == 0)
            a = R(q, 2, 9), b = 1;
        else if (kind == 3) {
            static const uint8_t pr[][2] = {{5, 8}, {5, 4}, {4, 5}, {6, 5}, {5, 6}, {8, 5}, {2, 5}, {5, 2}};
            int k = R(q, 0, 7);
            a = pr[k][0], b = pr[k][1];
        } else {
            do
                a = kind == 1 ? R(q, 2, 5) : R(q, 6, 9), b = R(q, 2, 9);
            while (a * b % 10 == 0);
        }
        mq_story(q, p, ST_MUL, -1, (int)a, (int)(b * 10), 0, NULL);
        p->ans = a * b * 10;
        W(p, a * b, MC_ZEROS), W(p, a * b * 100, MC_ZEROS), W(p, a + b * 10, MC_ADD_NOT_MUL);
        p->vis = V_GROUPS, p->v[0] = (int16_t)a, p->v[1] = (int16_t)b, p->v[3] = 10;
        p->a[0] = (int16_t)a, p->a[1] = (int16_t)b;
        FX(0, b == 1 ? 0 : (a * b) % 10 == 0 ? 3 : a <= 5 ? 1 : 2);
        p->mc_default = MC_ZEROS;
        break;
    }
    case S_PVBIG: { /* 4-digit, 5-digit places; 10 times as much; 6-digit */
        int nd = 4 + lvl(q, p, 0), rel = lvl(q, p, 1);
        if (rel) {
            a = R(q, 1, 9) * pow10i(nd - 2); /* (ten sheets of it: a number as long as the rung's) */
            TX(0, "%ld stamps fill 10 sheets, the same", (long)(a * 10));
            TX(1, "on each. How many stamps on one sheet?");
            p->ans = a;
            W(p, a * 100, MC_PLACE_SHIFT), W(p, a * 10 - 10, MC_TIMES_AS_ADD);
            n = a * 10;
            p->v[0] = -1;
            FX(0, ndig(n) >= 6 ? 2 : ndig(n) >= 5 ? 1 : 0);
        } else {
            n = R(q, (int)pow10i(nd - 1), (int)pow10i(nd) - 1);
            int place = R(q, 1, nd - 1);
            int dig = (int)(n / pow10i(place) % 10);
            if (!dig)
                n += pow10i(place) * 3, dig = 3;
            static const char pl[6][20] = {"ones", "tens", "hundreds", "thousands", "ten thousands",
                                           "hundred thousands"};
            TX(0, "On parcel %ld, what is the %d", (long)n, dig);
            TX(1, "in the %s place worth?", pl[place]);
            CAP("%ld: the %d in the %s?", (long)n, dig, pl[place]);
            p->ans = dig * pow10i(place);
            W(p, dig * pow10i(place - 1), MC_PLACE_SHIFT), W(p, dig * pow10i(place + 1), MC_PLACE_SHIFT), W(p, dig, MC_PLACE_SHIFT);
            p->v[0] = (int16_t)place;
            FX(0, nd - 4);
        }
        p->vis = V_PV, p->big = n;
        p->mc_default = MC_PLACE_SHIFT;
        p->idea = IDEA_PLACE;
        break;
    }
    case S_CMPBIG: { /* 4-digit: glue or compare; the same thousands; 5-digit; 6-digit with zeros */
        int same = lvl(q, p, 0), nd = 4 + lvl(q, p, 1);
        if (!same && nd == 4 && R(q, 0, 1)) {
            int th = R(q, 1, 9), hu = R(q, 1, 9), on = R(q, 1, 9);
            TX(0, "Glue the parcels into one number:");
            TX(1, "%d + %d + %d", th * 1000, hu * 100, on);
            p->ans = th * 1000 + hu * 100 + on;
            W(p, th * 100 + hu * 10 + on, MC_PLACE_SHIFT), W(p, th * 1000 + hu * 100 + on * 10, MC_PLACE_SHIFT);
            p->vis = V_NONE, p->big = 0; /* (no place chart: it would be the answer, digit by digit -- CR2-12) */
        } else {
            int32_t top = pow10i(nd - 1);
            a = R(q, (int)top, (int)(top * 10 - 1));
            if (nd == 6) { /* zeros inside: 405 060 and 450 600 */
                a = a / 1000 * 1000 + R(q, 0, 9) * 10;
                if (a / 1000 % 10 == 0)
                    a += 1000;
            }
            if (same) {
                do
                    b = a / top * top + R(q, 0, (int)top - 1);
                while (b == a || b < top);
            } else {
                do
                    b = R(q, (int)top, (int)(top * 10 - 1));
                while (b / top == a / top);
            }
            if (nd == 6 && R(q, 0, 2) == 0)
                b = a;
            TX(0, "Two sacks of letters: %ld and %ld.", (long)a, (long)b);
            TX(1, "Which sack has more letters?");
            cmp_num(q, p, a, b, MC_PLACE_SHIFT);
            p->vis = V_PV, p->big = a;
        }
        p->mc_default = MC_PLACE_SHIFT;
        break;
    }
    case S_ROUNDBIG: { /* 4-digit to 100, to 1000; 5-digit to 1000; to 10000 */
        int nd = 4 + lvl(q, p, 0), u = lvl(q, p, 1);
        if (u == 2)
            nd = 5, FX(0, 1);
        int k = u + 2; /* 100, 1000, 10000 */
        int32_t unit = pow10i(k);
        n = R(q, (int)pow10i(nd - 1), (int)pow10i(nd) - 1);
        while (n % unit == 0)
            n++;
        TX(0, "%ld letters came today.", (long)n);
        TX(1, "Hop it to the nearest %ld!", (long)unit);
        CAP("%ld letters: hop to the nearest %ld!", (long)n, (long)unit);
        p->ans = round_to(n, unit);
        W(p, n / unit * unit, MC_ROUND_DOWN_ALWAYS), W(p, n / unit * unit + unit, MC_ROUND_DIGIT);
        if (k < 4)
            W(p, round_to(n, unit * 10), MC_ROUND_DIGIT);
        W(p, round_to(n, unit / 10), MC_ROUND_DIGIT);
        p->vis = V_PV, p->big = n, p->v[0] = (int16_t)k;
        p->a[0] = (int16_t)(k - 1);
        p->mc_default = MC_ROUND_DIGIT;
        break;
    }
    case S_MULTI: g_product(q, p); break;
    case S_DIVIDE: g_divide(q, p); break;
    }
}

/* the bottom numbers of a family: 0 halves and quarters, 1 thirds and sixths, 2 eighths */
static int den_of(mq_t *q, int fam)
{
    static const uint8_t f[3][2] = {{2, 4}, {3, 6}, {8, 8}};
    return f[fam > 2 ? 2 : fam][R(q, 0, 1)];
}
static int den_fam(int dn) { return dn == 2 || dn == 4 ? 0 : dn == 3 || dn == 6 ? 1 : 2; }

static void g_bakery(mq_t *q, mq_prob_t *p, int d)
{
    static const uint8_t dens[] = {2, 3, 4, 6, 8};
    int n, dn, k;
    const char *momo = cast(q, 2);
    (void)d;
    switch (p->skill) {
    case S_UNITFRAC: { /* halves and quarters; thirds and sixths; more than one slice; eighths; on a bar */
        int fam = lvl(q, p, 0), more = lvl(q, p, 1), onbar = lvl(q, p, 2);
        dn = den_of(q, fam);
        if (more && dn == 2)
            dn = 4;
        n = more ? R(q, 2, dn - 1) : 1;
        TX(0, "%s frosted part of a %s pink.", momo, onbar ? "cake bar" : "cake");
        TX(1, "How much of the %s is pink?", onbar ? "bar" : "cake");
        CAP("How much is pink?");
        p->kind = K_FRAC, p->equiv = 1, p->ans = FR(n, dn);
        W(p, FR(dn, n), MC_NUM_DENOM_SWAP), W(p, FR(n, dn - n), MC_NUM_DENOM_SWAP), W(p, FR(dn - n, dn), MC_CARELESS);
        p->vis = onbar ? V_BAR : V_PIE, p->v[0] = (int16_t)n, p->v[1] = (int16_t)dn;
        FX(0, den_fam(dn)), FX(1, n > 1);
        p->mc_default = MC_NUM_DENOM_SWAP;
        p->idea = IDEA_FRACTION;
        break;
    }
    case S_PARTITION: { /* halves and quarters; thirds and sixths; more than one slice; eighths */
        int fam = lvl(q, p, 0), more = lvl(q, p, 1);
        dn = den_of(q, fam);
        if (more && dn == 2)
            dn = 4;
        n = more ? R(q, 2, dn - 1) : 1;
        TX(0, "The pie is cut into %d equal slices.", dn);
        TX(1, "Frost %d/%d of it for the party!", n, dn);
        CAP("Frost %d/%d of the pie!", n, dn);
        p->kind = K_SHADE, p->ans = n;
        W(p, dn - n, MC_NUM_DENOM_SWAP), W(p, dn, MC_UNEQUAL_PARTS);
        p->vis = V_PIE, p->v[0] = 0, p->v[1] = (int16_t)dn;
        FX(0, den_fam(dn)), FX(1, n > 1);
        p->mc_default = MC_NUM_DENOM_SWAP;
        p->idea = IDEA_FRACTION;
        break;
    }
    case S_FRACLINE: { /* halves and quarters; thirds and sixths; eighths; past 1 */
        int fam = lvl(q, p, 0), past = lvl(q, p, 1);
        dn = den_of(q, fam);
        k = past ? 2 : 1; /* wholes on the line */
        n = past ? R(q, dn + 1, 2 * dn - 1) : R(q, 1, dn - 1);
        TX(0, "%s hops along the cookie line.", momo);
        TX(1, "Hop to %d/%d!", n, dn);
        p->kind = K_LINE, p->ans = n;
        W(p, n - 1, MC_COUNT_TICKS), W(p, n + 1, MC_COUNT_TICKS);
        if (dn - n > 0 && dn - n != n)
            W(p, dn - n, MC_NUM_DENOM_SWAP);
        p->vis = V_NUMLINE, p->v[0] = 0, p->v[1] = (int16_t)k, p->v[2] = (int16_t)dn, p->v[3] = -1, p->v[4] = 1;
        FX(0, den_fam(dn));
        p->mc_default = MC_COUNT_TICKS;
        p->idea = IDEA_FRACTION;
        break;
    }
    case S_EQUIV: { /* 1/2 = ?/4; twice the slices; three times; the missing bottom */
        int kind = lvl(q, p, 0);
        k = kind == 2 ? 3 : kind == 3 ? R(q, 2, 3) : 2;
        dn = kind == 0 ? (R(q, 0, 1) ? 2 : 4) : dens[R(q, 0, 3)];
        if (kind == 2 && dn > 4)
            dn = R(q, 2, 4);
        n = R(q, 1, dn - 1);
        TX(0, "Same amount of cake:");
        if (kind == 3) {
            TX(1, "%d/%d = %d/?", n, dn, n * k);
            p->ans = dn * k;
            W(p, dn + n * k - n, MC_EQUIV_ADD), W(p, dn, MC_EQUIV_ADD), W(p, n * k * k, MC_CARELESS);
        } else {
            TX(1, "%d/%d = ?/%d", n, dn, dn * k);
            p->ans = n * k;
            W(p, n + dn * k - dn, MC_EQUIV_ADD), W(p, n, MC_EQUIV_ADD), W(p, dn * k - n, MC_CARELESS);
        }
        p->vis = V_BAR2, p->v[0] = (int16_t)n, p->v[1] = (int16_t)dn, p->v[2] = (int16_t)(n * k), p->v[3] = (int16_t)(dn * k);
        p->mc_default = MC_EQUIV_ADD;
        p->idea = IDEA_FRACTION;
        break;
    }
    case S_WHOLES: { /* n/n is one whole; 3 = 3/1; 12/4 = ?; 3 = ?/4 */
        int kind = lvl(q, p, 0);
        dn = dens[R(q, 0, 3)];
        k = kind == 0 ? 1 : R(q, 2, 3);
        if (kind == 0) {
            TX(0, "%s's pie has %d of %d slices left.", momo, dn, dn);
            TX(1, "How many whole pies is %d/%d?", dn, dn);
            p->ans = 1;
            W(p, dn, MC_NUM_DENOM_SWAP), W(p, 0, MC_CARELESS);
            p->vis = V_PIE, p->v[0] = (int16_t)dn, p->v[1] = (int16_t)dn, p->v[2] = 1;
        } else if (kind == 1) {
            TX(0, "%d whole cakes, not cut at all:", k);
            TX(1, "%d = ?/1", k);
            p->ans = k;
            W(p, 1, MC_NUM_DENOM_SWAP), W(p, 0, MC_CARELESS);
            p->vis = V_PIE, p->v[0] = (int16_t)k, p->v[1] = 1, p->v[2] = (int16_t)k; /* whole, uncut */
        } else if (kind == 2) {
            TX(0, "How many whole pies is this?");
            TX(1, "%d/%d = ?", k * dn, dn);
            p->ans = k;
            W(p, k * dn, MC_NUM_DENOM_SWAP), W(p, k * dn - dn, MC_WRONG_OP);
            p->vis = V_PIE, p->v[0] = (int16_t)(k * dn), p->v[1] = (int16_t)dn, p->v[2] = (int16_t)k;
        } else {
            TX(0, "%s baked %d whole pies.", momo, k);
            TX(1, "Each pie has %d slices.", dn);
            TX(2, "%d = ?/%d", k, dn);
            p->ans = k * dn;
            W(p, k, MC_MIXED_PARTS), W(p, dn, MC_ONE_GROUP), W(p, k + dn, MC_ADD_NOT_MUL);
            p->vis = V_PIE, p->v[0] = (int16_t)(k * dn), p->v[1] = (int16_t)dn, p->v[2] = (int16_t)k;
        }
        p->mc_default = MC_MIXED_PARTS;
        p->idea = IDEA_FRACTION;
        break;
    }
    case S_CMPFRAC: { /* the same bottom; one slice each (1/3, 1/4); the same top; tenths and twelfths */
        int kind = lvl(q, p, 0), bigd = lvl(q, p, 1);
        static const uint8_t bd[3] = {10, 12, 10};
        int a1, d1, a2, d2;
        if (kind == 0) {
            d1 = d2 = bigd ? bd[R(q, 0, 1)] : dens[R(q, 1, 4)];
            a1 = R(q, 1, d1 - 1), a2 = R(q, 1, d1 - 1);
            if (a1 == a2)
                a2 = a1 == 1 ? 2 : a1 - 1;
            TX(0, "Which plate has more cake?");
            p->mc_default = MC_NUM_DENOM_SWAP;
        } else {
            a1 = a2 = kind == 1 ? 1 : R(q, 2, 3);
            do {
                d1 = bigd && R(q, 0, 1) ? bd[R(q, 0, 1)] : dens[R(q, 0, 4)];
                d2 = bigd && R(q, 0, 1) ? bd[R(q, 0, 1)] : dens[R(q, 0, 4)];
            } while (d2 == d1 || d1 <= a1 || d2 <= a2);
            TX(0, "Which slice of cake is bigger?");
            p->mc_default = MC_BIG_DENOM;
        }
        TX(1, "%d/%d  ?  %d/%d", a1, d1, a2, d2);
        cmp_frac(q, p, a1, d1, a2, d2, kind ? MC_BIG_DENOM : MC_NUM_DENOM_SWAP);
        FX(1, d1 >= 10 || d2 >= 10);
        p->vis = V_BAR2, p->v[0] = (int16_t)a1, p->v[1] = (int16_t)d1, p->v[2] = (int16_t)a2, p->v[3] = (int16_t)d2;
        if (kind == 2 || bigd) /* the same top, tenths and twelfths: reasoned, not seen -- the bars come after her answer
                                * (the user: "as things get more advanced, the answer is not shown in a picture") */
            p->vq = 1;
        p->idea = IDEA_FRACTION;
        break;
    }
    case S_EQUIVMUL: { /* x2 with halves and quarters, x3, x4-5; the missing bottom; make it simpler */
        int mul = lvl(q, p, 0), kind = lvl(q, p, 1);
        k = mul == 0 ? 2 : mul == 1 ? 3 : R(q, 4, 5);
        dn = mul == 0 && kind == 0 ? (R(q, 0, 1) ? 2 : 4) : R(q, 2, 6);
        n = R(q, 1, dn - 1);
        if (kind == 2) { /* simpler: (n k)/(dn k) = ?/dn */
            TX(0, "Same amount, fewer slices:");
            TX(1, "%d/%d = ?/%d", n * k, dn * k, dn);
            p->ans = n;
            W(p, n * k - (dn * k - dn), MC_EQUIV_ADD), W(p, n * k, MC_FRAC_TIMES_BOTH), W(p, n + 1, MC_CARELESS);
        } else if (kind == 1) {
            TX(0, "Same amount, more slices:");
            TX(1, "%d/%d = %d/?", n, dn, n * k);
            p->ans = dn * k;
            W(p, dn + n * k - n, MC_EQUIV_ADD), W(p, dn, MC_FRAC_TIMES_BOTH), W(p, dn * k + 1, MC_CARELESS);
        } else {
            TX(0, "Same amount, more slices:");
            TX(1, "%d/%d = ?/%d", n, dn, dn * k);
            p->ans = n * k;
            W(p, n + dn * k - dn, MC_EQUIV_ADD), W(p, n, MC_FRAC_TIMES_BOTH), W(p, n * k + 1, MC_CARELESS);
        }
        p->vis = V_BAR2, p->v[0] = (int16_t)n, p->v[1] = (int16_t)dn, p->v[2] = (int16_t)(n * k), p->v[3] = (int16_t)(dn * k);
        p->mc_default = MC_EQUIV_ADD;
        p->idea = IDEA_FRACTION;
        break;
    }
    case S_CMPDIFF: { /* against one half; the same top; one bottom fits the other; any two */
        int kind = lvl(q, p, 0), a1 = 1, d1 = 2, a2 = 1, d2 = 2;
        for (int t = 0; t < 200; t++) {
            if (kind == 0) {
                static const uint8_t bd[6] = {3, 4, 5, 6, 8, 10};
                d1 = bd[R(q, 0, 5)], a1 = R(q, 1, d1 - 1), a2 = 1, d2 = 2;
            } else if (kind == 1) {
                a1 = a2 = R(q, 2, 5), d1 = R(q, a1 + 1, 10), d2 = R(q, a1 + 1, 10);
            } else if (kind == 2) {
                d1 = R(q, 2, 5), d2 = d1 * R(q, 2, 3), a1 = R(q, 1, d1 - 1), a2 = R(q, 1, d2 - 1);
            } else {
                d1 = R(q, 3, 6), d2 = R(q, 3, 8), a1 = R(q, 1, d1 - 1), a2 = R(q, 1, d2 - 1);
                if (d1 % d2 == 0 || d2 % d1 == 0 || a1 == a2)
                    continue;
            }
            if (d1 == d2 || a1 * d2 == a2 * d1 || gcd(a1, d1) != 1 || gcd(a2, d2) != 1)
                continue;
            break;
        }
        if (R(q, 0, 1)) {
            int t1 = a1, t2 = d1;
            a1 = a2, d1 = d2, a2 = t1, d2 = t2;
        }
        TX(0, "Which tray has more pie?");
        TX(1, "%d/%d  ?  %d/%d", a1, d1, a2, d2);
        if (kind == 0)
            TX(2, "(Psst: think of half a pie.)");
        cmp_frac(q, p, a1, d1, a2, d2, MC_BIG_DENOM);
        p->vis = V_BAR2, p->v[0] = (int16_t)a1, p->v[1] = (int16_t)d1, p->v[2] = (int16_t)a2, p->v[3] = (int16_t)d2;
        if (kind >= 1) /* past the half benchmark: the same top, one bottom fitting the other, any two -- reasoned with the
                        * numbers (the bars after her answer) */
            p->vq = 1;
        p->mc_default = MC_BIG_DENOM;
        p->idea = IDEA_FRACTION;
        break;
    }
    case S_ADDFRAC: { /* add up to a whole; take away; more than a whole; tenths and twelfths */
        int kind = lvl(q, p, 0), bigd = lvl(q, p, 1);
        static const uint8_t ad[4] = {3, 4, 6, 8};
        dn = bigd ? (R(q, 0, 1) ? 10 : 12) : ad[R(q, 0, 3)];
        int a1, a2;
        if (kind == 1) {
            a1 = R(q, 2, dn - 1), a2 = R(q, 1, a1 - 1);
            TX(0, "%s had %d/%d of a tart and ate %d/%d.", momo, a1, dn, a2, dn);
            TX(1, "How much of the tart is left?");
            CAP("%d/%d of a tart, %d/%d eaten!", a1, dn, a2, dn);
            p->ans = FR(a1 - a2, dn);
            W(p, FR(a1 - a2, 0), MC_ADD_DENOMS), W(p, FR(a1 + a2, dn), MC_WRONG_OP);
        } else {
            do
                a1 = R(q, 1, dn - 1), a2 = R(q, 1, dn - 1);
            while (kind == 2 ? a1 + a2 <= dn : a1 + a2 > dn);
            TX(0, "%d/%d of a pie + %d/%d of a pie.", a1, dn, a2, dn);
            TX(1, "How much pie in all?");
            CAP("Pie in all?");
            p->ans = FR(a1 + a2, dn);
            W(p, FR(a1 + a2, dn * 2), MC_ADD_DENOMS);
        }
        p->kind = K_FRAC, p->equiv = 1;
        if (kind == 1) /* what is left, and the part eaten beside it */
            p->vis = V_BAR, p->v[0] = (int16_t)(a1 - a2), p->v[1] = (int16_t)dn, p->v[2] = (int16_t)a2;
        else if (a1 + a2 > dn) /* more than a whole: a full bar and the rest */
            p->vis = V_BAR2, p->v[0] = (int16_t)dn, p->v[1] = (int16_t)dn, p->v[2] = (int16_t)(a1 + a2 - dn), p->v[3] = (int16_t)dn;
        else
            p->vis = V_BAR, p->v[0] = (int16_t)a1, p->v[1] = (int16_t)dn, p->v[2] = (int16_t)a2;
        p->mc_default = MC_ADD_DENOMS;
        p->idea = IDEA_FRACTION;
        break;
    }
    case S_MIXED: { /* 1 and a part to slices; slices to 1 and a part; 2 or 3 wholes; add a part */
        int kind = lvl(q, p, 0), more = lvl(q, p, 1);
        dn = dens[R(q, 1, 4)], k = more ? R(q, 2, 3) : 1, n = R(q, 1, dn - 1);
        if (kind == 2) {
            int b = R(q, 1, dn - n > 1 ? dn - n - 1 : 1);
            if (n + b >= dn)
                b = dn - n - 1 > 0 ? dn - n - 1 : 0;
            if (!b)
                n = 1, b = 1;
            TX(0, "%s has %d %d/%d pies and bakes %d/%d more.", momo, k, n, dn, b, dn);
            TX(1, "%d %d/%d + %d/%d = %d and ?/%d", k, n, dn, b, dn, k, dn);
            p->ans = n + b;
            W(p, k * dn + n + b, MC_MIXED_PARTS), W(p, n, MC_ONE_STEP), W(p, b, MC_ONE_STEP);
            p->v[0] = (int16_t)(k * dn + n + b);
        } else if (kind == 0) {
            TX(0, "%s has %d whole %s and %d/%d more.", momo, k, k == 1 ? "pie" : "pies", n, dn);
            TX(1, "%d %d/%d = ?/%d", k, n, dn, dn);
            p->ans = k * dn + n;
            W(p, k + n, MC_MIXED_PARTS), W(p, k * n + dn, MC_MIXED_PARTS), W(p, k * dn, MC_ONE_STEP);
            p->v[0] = (int16_t)(k * dn + n);
        } else {
            TX(0, "Stack the slices into whole pies:");
            TX(1, "%d/%d = %d and ?/%d", k * dn + n, dn, k, dn);
            p->ans = n;
            W(p, k * dn + n - k, MC_MIXED_PARTS), W(p, k * dn + n, MC_ONE_STEP);
            p->v[0] = (int16_t)(k * dn + n);
        }
        p->vis = V_PIE, p->v[1] = (int16_t)dn, p->v[2] = (int16_t)(k + 1);
        p->mc_default = MC_MIXED_PARTS;
        p->idea = IDEA_FRACTION;
        break;
    }
    case S_FRACWHOLE: { /* a single slice a few times; more slices; 4 or 5 times; more than a whole */
        int non = lvl(q, p, 0), more = lvl(q, p, 1), over = lvl(q, p, 2);
        n = non ? R(q, 2, 3) : 1, k = more ? R(q, 4, 5) : R(q, 2, 3);
        if (!over && k * n > 9) { /* (less than a whole needs a big bottom: keep it small) */
            if (more)
                n = 1;
            else
                k = 2;
        }
        if (over) {
            if (k * n <= 2)
                k = 3;
            dn = R(q, n + 1, imax(n + 1, k * n - 1));
        } else
            dn = R(q, k * n + 1, imin(12, k * n + 4));
        mq_story(q, p, ST_RECIPE, -1, k, n, dn, NULL);
        p->kind = K_FRAC, p->equiv = 1, p->ans = FR(k * n, dn);
        W(p, FR(k * n, k * dn), MC_FRAC_TIMES_BOTH), W(p, FR(n, k * dn), MC_FRAC_TIMES_BOTH), W(p, FR(k + n, dn), MC_ADD_NOT_MUL);
        p->vis = V_PIE, p->v[0] = (int16_t)(k * n), p->v[1] = (int16_t)dn, p->v[2] = (int16_t)((k * n + dn - 1) / dn);
        FX(0, n > 1), FX(1, k >= 4), FX(2, k * n > dn);
        p->mc_default = MC_FRAC_TIMES_BOTH;
        p->idea = IDEA_FRACTION;
        break;
    }
    case S_TENTHS: { /* tenths as hundredths; tenths and hundredths; the other way round */
        int kind = lvl(q, p, 0);
        n = R(q, 1, 9);
        if (kind == 1) {
            k = R(q, 1, 9);
            TX(0, "Add the sprinkles:");
            TX(1, "%d/10 + %d/100 = ?/100", n, k);
            p->ans = n * 10 + k;
            W(p, n + k, MC_TENTHS_HUNDREDTHS), W(p, n * 100 + k, MC_TENTHS_HUNDREDTHS);
        } else if (kind == 2) {
            TX(0, "Hundredths into tenths:");
            TX(1, "%d/100 = ?/10", n * 10);
            p->ans = n;
            W(p, n * 10, MC_TENTHS_HUNDREDTHS), W(p, n * 100, MC_ZEROS);
        } else {
            TX(0, "Tenths into hundredths:");
            TX(1, "%d/10 = ?/100", n);
            p->ans = n * 10;
            W(p, n, MC_TENTHS_HUNDREDTHS), W(p, n * 100, MC_ZEROS);
        }
        /* the hundredths grid shows the amount */
        p->vis = V_GRID, p->v[0] = 10, p->v[1] = 10, p->v[2] = 5, p->v[3] = (int16_t)(kind == 1 ? p->ans : n * 10);
        p->mc_default = MC_TENTHS_HUNDREDTHS;
        p->idea = IDEA_DECIMAL;
        break;
    }
    case S_DECIMAL: { /* tenths, hundredths pictured; on a number line; just the numbers; a zero first */
        int hund = lvl(q, p, 0), pic = lvl(q, p, 1), zero = lvl(q, p, 2);
        if (zero && pic == 1)
            pic = 2, FX(1, 2); /* (a zero first is written, not hopped to) */
        if (pic == 1)
            hund = 0, FX(0, 0); /* (the line is marked in tenths) */
        if (zero)
            hund = 1, FX(0, 1);
        n = zero ? R(q, 1, 9) : hund ? R(q, 11, 99) : R(q, 1, 9);
        if (hund && !zero && n % 10 == 0)
            n++;
        p->kind = K_DEC;
        if (pic == 1) {
            TX(0, "%s hops to the mark on the line.", momo);
            TX(1, "What number is the mark at?");
            p->ans = n * 100;
            W(p, n * 10, MC_DEC_PLACE), W(p, n * 1000, MC_DEC_PLACE), W(p, (n + 1) * 100, MC_COUNT_TICKS);
            p->vis = V_NUMLINE, p->v[0] = 0, p->v[1] = 1, p->v[2] = 10, p->v[3] = (int16_t)n, p->v[4] = 1;
        } else if (pic == 2 && !zero && R(q, 0, 1)) { /* the other way: 0.ab = ?/100 */
            TX(0, "0.%02d = ?/100", n);
            p->kind = K_NUM, p->ans = n;
            W(p, n / 10 + (n % 10) * 10, MC_PLACE_SHIFT), W(p, n * 10, MC_TENTHS_HUNDREDTHS);
            p->vis = V_GRID, p->v[0] = 10, p->v[1] = 10, p->v[2] = 5, p->v[3] = (int16_t)n;
        } else {
            TX(0, "Write %d/%s of a dollar on the price tag!", n, hund ? "100" : "10");
            CAP("%d/%s of a dollar: the tag?", n, hund ? "100" : "10");
            p->ans = hund ? n * 10 : n * 100;
            W(p, hund ? n * 100 : n * 10, MC_DEC_PLACE), W(p, n * 1000, MC_DEC_PLACE);
            p->vis = V_GRID, p->v[0] = 10, p->v[1] = 10, p->v[2] = 5, p->v[3] = (int16_t)(hund ? n : n * 10);
        }
        if (pic == 2)
            p->vq = 1; /* just the numbers: no picture with the question */
        FX(2, hund && n < 10);
        p->mc_default = MC_DEC_PLACE;
        p->idea = IDEA_DECIMAL;
        break;
    }
    case S_CMPDEC: { /* tenths, pictured; 0.5 or 0.49?; just the numbers; 0.5 and 0.50 */
        int kind = lvl(q, p, 0), num = lvl(q, p, 1), zeros = lvl(q, p, 2);
        int x = R(q, 1, 9), y; /* x tenths against y (tenths, or hundredths) */
        char la[16], lb[16];
        if (zeros) { /* 0.5 and 0.50: the same, or 0.40 against 0.4x */
            y = R(q, 0, 2) ? x * 10 : x * 10 + R(q, 1, 9);
            q->api->snprintf(la, sizeof la, "0.%d", x), q->api->snprintf(lb, sizeof lb, "0.%02d", y);
            if (R(q, 0, 1) && y % 10) /* (the zero on the shorter one: 0.50 against 0.5x) */
                q->api->snprintf(la, sizeof la, "0.%d0", x);
            kind = 1;
        } else if (kind == 1) {
            y = (x > 1 ? x - 1 : 0) * 10 + R(q, 1, 9); /* the tempting case: 0.5 vs 0.45 */
            if (R(q, 0, 2) == 0)
                y = R(q, 11, 99);
            if (y % 10 == 0)
                y++;
            q->api->snprintf(la, sizeof la, "0.%d", x), q->api->snprintf(lb, sizeof lb, "0.%02d", y);
        } else {
            y = R(q, 1, 9) * 10;
            if (y == x * 10)
                y = x == 9 ? 80 : y + 10;
            q->api->snprintf(la, sizeof la, "0.%d", x), q->api->snprintf(lb, sizeof lb, "0.%d", y / 10);
        }
        int l = x * 10, r = y;
        TX(0, "Which cookie jar is fuller?");
        TX(1, "%s  ?  %s", la, lb);
        cmp_pick(q, p, l < r ? -1 : l > r, MC_DEC_LONGER, la, lb);
        p->vis = V_GRID, p->v[0] = 10, p->v[1] = 10, p->v[2] = 6, p->v[3] = (int16_t)l, p->v[4] = (int16_t)r;
        p->a[0] = (int16_t)x, p->a[1] = (int16_t)y;
        if (num)
            p->vq = 1;
        FX(0, kind), FX(2, l == r || lb[3] == '0' || la[3] == '0');
        p->mc_default = MC_DEC_LONGER;
        p->idea = IDEA_DECIMAL;
        break;
    }
    }
}

/* minutes past midnight -> h*100+m on a 12-hour clock */
static int32_t hm(int mins)
{
    int h = mins / 60 % 12, m = mins % 60;
    return (h ? h : 12) * 100 + m;
}

static void g_clock(mq_t *q, mq_prob_t *p, int d)
{
    const char *olli = cast(q, 3);
    int h, m;
    char s[24];
    (void)d;
    switch (p->skill) {
    case S_TIME5:
    case S_TIME1: {
        int kind = lvl(q, p, 0);
        h = R(q, 1, 12);
        if (p->skill == S_TIME5) { /* o'clock, half past; quarters; 5-minute times; :55 and :05 */
            static const uint8_t m5[4][4] = {{0, 30, 0, 30}, {15, 45, 15, 45}, {10, 20, 40, 50}, {5, 55, 5, 55}};
            m = m5[kind][R(q, 0, 3)];
            if (kind == 2 && R(q, 0, 1))
                m = R(q, 0, 1) ? 25 : 35;
        } else { /* a few minutes past; any minute in the first half; any minute; just before the hour */
            m = kind == 0 ? R(q, 1, 9) : kind == 1 ? R(q, 11, 29) : kind == 2 ? R(q, 31, 49) : R(q, 51, 59);
            if (m % 5 == 0)
                m++;
        }
        TX(0, "%s's clock tower rings!", olli);
        TX(1, "When does it ring?");
        p->kind = K_TIME, p->ans = h * 100 + m;
        W(p, (m / 5 ? m / 5 : 12) * 100 + (h % 12) * 5, MC_HANDS_SWAP);
        if (m >= 5)
            W(p, h * 100 + m / 5, MC_MIN_ONES);
        if (m >= 30)
            W(p, (h % 12 + 1) * 100 + m, MC_CLOSE);
        p->vis = V_CLOCK, p->v[0] = (int16_t)h, p->v[1] = (int16_t)m, p->v[2] = -1;
        p->mc_default = MC_HANDS_SWAP;
        break;
    }
    case S_ELAPSED: { /* whole hours later; minutes later in the hour; how long; across the hour; over an hour */
        int kind = lvl(q, p, 0), howlong = lvl(q, p, 1);
        int start, dur;
        if (kind == 0) {
            start = R(q, 1, 10) * 60 + R(q, 0, 1) * 30, dur = R(q, 1, 3) * 60;
        } else if (kind == 1) { /* within the hour */
            start = R(q, 1, 11) * 60 + R(q, 0, 6) * 5, dur = R(q, 2, (55 - start % 60) / 5) * 5;
        } else if (kind == 2) { /* across the hour: 2:45 + 30 min = 3:15 */
            start = R(q, 1, 10) * 60 + R(q, 7, 11) * 5;
            dur = R(q, (65 - start % 60) / 5, 11) * 5;
        } else { /* over an hour */
            start = R(q, 1, 9) * 60 + R(q, 1, 11) * 5, dur = 60 + R(q, 1, 11) * 5 + R(q, 0, 1) * 60;
        }
        if (howlong) {
            int s1 = hm(start), s2 = hm(start + dur);
            q->api->snprintf(s, sizeof s, "%d:%02d to %d:%02d", s1 / 100, s1 % 100, s2 / 100, s2 % 100);
            mq_story(q, p, ST_HOWLONG, -1, 0, 0, 0, s);
            p->ans = dur;
            if (s2 > s1)
                W(p, s2 - s1, MC_ELAPSED_100);
            W(p, dur - 60, MC_CLOSE);
        } else {
            q->api->snprintf(s, sizeof s, "%d:%02d", hm(start) / 100, hm(start) % 100);
            mq_story(q, p, ST_READY, -1, dur, 0, 0, s);
            p->kind = K_TIME, p->ans = hm(start + dur);
            W(p, hm(start) + dur / 60 * 100 + dur % 60, MC_ELAPSED_100);
            W(p, hm(start + dur - 60), MC_CLOSE);
        }
        FX(0, dur % 60 == 0 ? 0 : dur > 60 ? 3 : start % 60 + dur % 60 >= 60 ? 2 : 1);
        p->mc_default = MC_ELAPSED_100;
        p->vis = V_CLOCK, p->v[0] = (int16_t)(hm(start) / 100), p->v[1] = (int16_t)(start % 60), p->v[2] = (int16_t)hm(start + dur);
        p->a[0] = (int16_t)start, p->a[1] = (int16_t)dur;
        break;
    }
    case S_MASSVOL: { /* about how heavy; add and take away; times; share */
        int kind = lvl(q, p, 0);
        if (kind == 0) {
            static const char obj[4][14] = {"an apple", "a paper clip", "a puppy", "a backpack"};
            static const char good[4][8] = {"150 g", "1 g", "5 kg", "3 kg"};
            static const char bad[4][3][8] = {{"150 kg", "1 g", "15 kg"},
                                              {"1 kg", "100 g", "10 kg"},
                                              {"5 g", "500 kg", "50 g"},
                                              {"3 g", "300 kg", "30 g"}};
            int o = R(q, 0, 3);
            TX(0, "About how heavy is %s?", obj[o]);
            C(q, p, good[o], MC_NONE);
            for (int i = 0; i < 3; i++)
                C(q, p, bad[o][i], MC_UNIT_WAY);
            choices_done(q, p, false);
            p->vis = V_SCALE, p->v[0] = (int16_t)o;
        } else if (kind == 2) {
            int a = R(q, 2, 9), b = R(q, 2, 6);
            TX(0, "A bag of flour is %d kg.", a);
            TX(1, "%s buys %d bags. How many kg?", olli, b);
            CAP("%d bags of %d kg!", b, a);
            p->ans = a * b;
            W(p, a + b, MC_ADD_NOT_MUL);
            p->vis = V_SCALE, p->v[0] = 9, p->v[1] = (int16_t)a;
        } else if (kind == 3) {
            int a = R(q, 2, 9), b = R(q, 2, 5);
            TX(0, "%d kg of flour fill %d bags equally.", a * b, b);
            TX(1, "How many kg in each bag?");
            CAP("%d kg in %d bags!", a * b, b);
            p->ans = a;
            W(p, a * b - b, MC_WRONG_OP), W(p, a * b * b, MC_WRONG_OP), W(p, b, MC_DIV_MIX);
            p->vis = V_SCALE, p->v[0] = 9, p->v[1] = (int16_t)(a * b);
        } else {
            int a = R(q, 2, 9), b = R(q, 2, 9);
            mq_story(q, p, ST_JUG, -1, a + b, b, 0, NULL);
            p->ans = a;
            W(p, a + 2 * b, MC_WRONG_OP);
            p->vis = V_JUG, p->v[0] = (int16_t)(a + b), p->v[1] = (int16_t)a;
        }
        p->mc_default = MC_UNIT_WAY;
        break;
    }
    case S_CONVERT: { /* m to cm and hours to minutes; x 1000; lb to oz; two units at once */
        static const char big[6][8] = {"km", "m", "kg", "l", "hours", "lb"};
        static const char sm[6][8] = {"m", "cm", "g", "ml", "minutes", "oz"};
        static const int16_t f[6] = {1000, 100, 1000, 1000, 60, 16};
        static const uint8_t fam[3][3] = {{1, 4, 1}, {0, 2, 3}, {5, 5, 5}};
        int lf = lvl(q, p, 0), two = lvl(q, p, 1);
        int u = fam[lf][R(q, 0, 2)], a = lf == 0 ? R(q, 2, 5) : R(q, 2, 9);
        if (two) {
            int b = R(q, 1, f[u] - 1);
            if (f[u] >= 100)
                b = R(q, 1, f[u] / 10 - 1) * (f[u] == 1000 ? 10 : 1) + (f[u] == 100 ? 0 : R(q, 0, 9));
            if (f[u] == 100)
                b = R(q, 5, 95);
            if (b <= 0 || b >= f[u])
                b = f[u] / 4;
            TX(0, "%d %s %d %s = ? %s", a, big[u], b, sm[u], sm[u]);
            TX(1, "(1 %s = %d %s)", big[u], f[u], sm[u]);
            p->ans = a * f[u] + b;
            W(p, a + b, MC_UNIT_WAY), W(p, a * f[u], MC_ONE_STEP), W(p, a * 100 + b, f[u] == 60 ? MC_ELAPSED_100 : MC_ZEROS);
        } else {
            TX(0, "%d %s = ? %s", a, big[u], sm[u]);
            TX(1, "(1 %s = %d %s)", big[u], f[u], sm[u]);
            p->ans = a * f[u];
            W(p, a, MC_UNIT_WAY), W(p, a * (f[u] == 60 ? 100 : f[u] * 10), f[u] == 60 ? MC_ELAPSED_100 : MC_ZEROS);
            if (f[u] >= 100)
                W(p, a * f[u] / 10, MC_ZEROS);
            W(p, a + f[u], MC_ADD_NOT_MUL);
        }
        FX(0, f[u] == 16 ? 2 : f[u] == 1000 ? 1 : 0);
        p->vis = V_RULER, p->v[0] = (int16_t)u, p->v[1] = 0;
        p->a[0] = (int16_t)u, p->a[1] = (int16_t)a;
        p->mc_default = MC_UNIT_WAY;
        break;
    }
    case S_MEASWORD: { /* add lengths; take away; times; m and cm together */
        int op = lvl(q, p, 0), mixed = lvl(q, p, 1);
        if (mixed) {
            int a = R(q, 2, 4), b = R(q, 15, 85);
            TX(0, "A ribbon is %d m long.", a);
            TX(1, "%s cuts off %d cm.", olli, b);
            CAP("%d m of ribbon, %d cm cut!", a, b);
            TX(2, "How many cm are left?");
            p->ans = a * 100 - b;
            W(p, b - a, MC_UNIT_WAY), W(p, a * 100 + b, MC_WRONG_OP), W(p, flip_sub(a * 100, b), MC_NO_BORROW_ZERO);
            p->mc_default = MC_UNIT_WAY;
            p->ws = WS_SUB, p->op[0] = a * 100, p->op[1] = b;
            FX(0, 1);
        } else if (op == 0) {
            int a = R(q, 120, 480), b = R(q, 120, 480);
            mq_story(q, p, ST_FAR, -1, a, b, 0, NULL);
            p->ans = a + b;
            W(p, no_carry_add(a, b), MC_NO_REGROUP), W(p, a - b > 0 ? a - b : b - a, MC_WRONG_OP);
            p->mc_default = MC_NO_REGROUP;
            p->ws = WS_ADD, p->op[0] = a, p->op[1] = b;
        } else if (op == 1) {
            int a = R(q, 300, 900), b = R(q, 110, a - 60);
            TX(0, "A jug holds %d ml of juice.", a);
            TX(1, "%s pours out %d ml.", olli, b);
            TX(2, "How many ml are left?");
            p->ans = a - b;
            W(p, flip_sub(a, b), MC_SUB_FLIP), W(p, a + b, MC_WRONG_OP);
            p->mc_default = MC_SUB_FLIP;
            p->ws = WS_SUB, p->op[0] = a, p->op[1] = b;
        } else {
            int a = R(q, 2, 5), b = R(q, 100, 400);
            TX(0, "Each bottle holds %d ml of juice.", b);
            TX(1, "How many ml in %d bottles?", a);
            CAP("%d bottles of %d ml!", a, b);
            p->ans = a * b;
            W(p, a + b, MC_ADD_NOT_MUL), W(p, no_carry_mul(b, a), MC_NO_REGROUP);
            p->mc_default = MC_NO_REGROUP;
            p->ws = WS_MUL, p->op[0] = b, p->op[1] = a;
        }
        p->vis = V_RULER, p->v[0] = 1, p->v[1] = 1;
        break;
    }
    }
}

static void g_shop(mq_t *q, mq_prob_t *p, int d)
{
    const char *tango = cast(q, 4);
    char s[24];
    (void)d;
    switch (p->skill) {
    case S_COINS: { /* dimes and pennies; with nickels; with quarters; with dollars */
        int set = lvl(q, p, 0);
        int qu = set >= 2 ? R(q, 1, 3) : 0, di = R(q, 1, 4), ni = set >= 1 ? R(q, 0, 2) : 0, pe = R(q, 0, 4),
            dol = set >= 3 ? R(q, 1, 3) : 0;
        if (set == 1 && !ni)
            ni = 1;
        TX(0, "%s counts the coins in the till.", tango);
        TX(1, "How much money is there?");
        CAP("Count the coins!");
        p->kind = K_MONEY, p->ans = dol * 100 + qu * 25 + di * 10 + ni * 5 + pe;
        W(p, qu + di + ni + pe + dol, MC_COIN_COUNT), W(p, (qu + di + ni + pe + dol) * 100, MC_COIN_COUNT);
        W(p, dol * 100 + qu * 10 + di * 25 + ni * 5 + pe, MC_COIN_COUNT);
        if (ni && di && ni != di) /* a nickel taken for more than a dime (it is the bigger coin) */
            W(p, dol * 100 + qu * 25 + di * 5 + ni * 10 + pe, MC_COIN_COUNT);
        p->vis = V_COINS, p->v[0] = (int16_t)qu, p->v[1] = (int16_t)di, p->v[2] = (int16_t)ni, p->v[3] = (int16_t)pe,
        p->v[4] = (int16_t)dol;
        FX(0, dol ? 3 : qu ? 2 : ni ? 1 : 0);
        p->mc_default = MC_COIN_COUNT;
        break;
    }
    case S_CHANGE: { /* from $1 with 5-cent prices; any price; from $5; from $10 */
        int k = lvl(q, p, 0);
        int pay = k <= 1 ? 100 : k == 2 ? 500 : 1000, cost = R(q, 11, pay - 6);
        if (k == 0)
            cost = R(q, 2, 19) * 5;
        else if (cost % 5 == 0)
            cost += R(q, 1, 4);
        if (k >= 2 && cost < pay / 2)
            cost += pay / 2 - 50; /* (a price worth the bigger bill) */
        q->api->snprintf(s, sizeof s, "$%d.%02d", cost / 100, cost % 100);
        mq_story(q, p, ST_CHANGE, -1, pay / 100, 0, 0, s);
        p->kind = K_MONEY, p->ans = pay - cost;
        W(p, flip_sub(pay, cost), MC_NO_BORROW_ZERO), W(p, pay + cost, MC_WRONG_OP), W(p, pay - cost + 100, MC_SUB_FLIP);
        p->vis = V_COINS, p->v[0] = (int16_t)((pay - cost) % 100 / 25), p->v[1] = (int16_t)((pay - cost) % 25 / 10),
        p->v[2] = (int16_t)((pay - cost) % 25 % 10 / 5), p->v[3] = (int16_t)((pay - cost) % 5),
        p->v[4] = (int16_t)((pay - cost) / 100);
        p->a[0] = (int16_t)pay, p->a[1] = (int16_t)cost;
        FX(0, pay == 1000 ? 3 : pay == 500 ? 2 : cost % 5 ? 1 : 0);
        p->mc_default = MC_NO_BORROW_ZERO;
        p->ws = WS_SUB, p->ws_dp = 2, p->op[0] = pay, p->op[1] = cost;
        p->idea = IDEA_TRADE;
        break;
    }
    case S_SHOPWORD: { /* one step; groups then take away; change from $1 */
        int kind = lvl(q, p, 0);
        if (kind == 2) {
            int n = R(q, 2, 4), c = R(q, 1, 4) * 5 + 5;
            if (n * c >= 100)
                c = 15;
            TX(0, "Stickers cost %d cents each.", c);
            TX(1, "%s buys %d and pays with $1.", tango, n);
            TX(2, "How many cents of change?");
            CAP("%d at %dc each, paid $1!", n, c);
            p->ans = 100 - n * c;
            W(p, n * c, MC_ONE_STEP), W(p, 100 - c, MC_ONE_STEP), W(p, 100 - n - c, MC_ADD_NOT_MUL);
        } else if (kind == 1) {
            int a = R(q, 3, 8), b = R(q, 2, 6), c = R(q, 2, imin(9, a * b - 1));
            mq_story(q, p, ST_MARBLES, -1, a, b, c, NULL);
            p->ans = a * b - c;
            W(p, a * b, MC_ONE_STEP), W(p, a + b - c, MC_ADD_NOT_MUL);
        } else {
            int n = R(q, 2, 5), c = R(q, 2, 9) * 5;
            TX(0, "Stickers cost %d cents each.", c);
            TX(1, "%s buys %d of them.", tango, n);
            TX(2, "How many cents is that?");
            CAP("%d stickers at %dc each!", n, c);
            p->ans = n * c;
            W(p, n + c, MC_ADD_NOT_MUL), W(p, n * c + c, MC_NEIGHBOR);
        }
        p->vis = V_COINS, p->v[0] = 4, p->v[1] = 0;
        p->mc_default = MC_ONE_STEP;
        break;
    }
    case S_MONEYDEC: { /* $0.45 in cents; cents on the price tag; under 10 cents; over a dollar */
        int kind = lvl(q, p, 0), sz = lvl(q, p, 1);
        int c = sz == 1 ? R(q, 1, 9) : sz == 2 ? R(q, 101, 499) : R(q, 11, 99);
        if (sz != 1 && c % 10 == 0)
            c++;
        if (kind == 0) {
            q->api->snprintf(s, sizeof s, "$%d.%02d", c / 100, c % 100);
            TX(0, "How many cents is %s?", s);
            p->ans = c;
            W(p, c * 10, MC_DEC_PLACE), W(p, c * 100, MC_DEC_PLACE);
            if (c >= 100)
                W(p, c % 100, MC_ONE_STEP);
        } else {
            TX(0, "%d cents is %d/100 of a dollar.", c, c);
            TX(1, "How does the price tag show it?");
            CAP("%d cents on the price tag?", c);
            p->kind = K_DEC, p->ans = c * 10;
            W(p, c * 100, MC_DEC_PLACE), W(p, c * 1000, MC_DEC_PLACE);
        }
        p->vis = V_COINS, p->v[0] = (int16_t)(c % 100 / 25), p->v[1] = (int16_t)(c % 25 / 10), p->v[2] = (int16_t)(c % 25 % 10 / 5),
        p->v[3] = (int16_t)(c % 5), p->v[4] = (int16_t)(c / 100);
        FX(1, c < 10 ? 1 : c > 100 ? 2 : 0);
        p->mc_default = MC_DEC_PLACE;
        p->idea = IDEA_DECIMAL;
        break;
    }
    }
}

static void g_art(mq_t *q, mq_prob_t *p, int d)
{
    const char *dab = cast(q, 5);
    int w, h, t;
    static const char shape_name[11][14] = {"square", "rectangle", "rhombus", "trapezoid", "parallelogram",
                                            "right tri.", "acute tri.", "obtuse tri.", "kite", "pentagon", "hexagon"};
    (void)d, (void)t;
    switch (p->skill) {
    case S_AREACOUNT: { /* count a small rug; bigger rugs; L-shaped rugs */
        int big = lvl(q, p, 0), ell = lvl(q, p, 1);
        if (ell) {
            int w1 = R(q, 3, big ? 6 : 4), h1 = R(q, 1, 2), w2 = R(q, 1, w1 - 1), h2 = R(q, 1, big ? 3 : 2);
            TX(0, "%s tiles an L-shaped rug.", dab);
            TX(1, "How many squares does it cover?");
            CAP("How many squares?");
            p->ans = w1 * h1 + w2 * h2;
            W(p, w1 * (h1 + h2), MC_CARELESS), W(p, w1 * h1, MC_ONE_STEP), W(p, 2 * (w1 + h1 + h2), MC_AREA_PERIM);
            p->vis = V_LSHAPE, p->v[0] = (int16_t)w1, p->v[1] = (int16_t)h1, p->v[2] = (int16_t)w2, p->v[3] = (int16_t)h2;
            FX(0, w1 > 4 || h2 > 2);
        } else {
            w = big ? R(q, 3, 5) : R(q, 2, 3), h = big ? R(q, 2, 4) : R(q, 2, 3);
            TX(0, "%s tiles a rug with squares.", dab);
            TX(1, "How many squares does it cover?");
            CAP("How many squares?");
            p->ans = w * h;
            W(p, 2 * (w + h), MC_AREA_PERIM), W(p, w + h, MC_ADD_NOT_MUL), W(p, w * h - 1, MC_CLOSE);
            p->vis = V_GRID, p->v[0] = (int16_t)w, p->v[1] = (int16_t)h, p->v[2] = 0;
            FX(0, w > 3 || h > 3);
        }
        p->mc_default = MC_AREA_PERIM;
        p->idea = IDEA_AREA;
        break;
    }
    case S_AREAMUL: { /* small rugs; up to 9 by 9; one side missing */
        int big = lvl(q, p, 0), miss = lvl(q, p, 1);
        w = big ? R(q, 3, 9) : R(q, 2, 5), h = big ? R(q, 2, 9) : R(q, 2, 5);
        if (miss) {
            if (h > 8)
                h = 8;
            TX(0, "A rug of %d squares is %d squares long.", w * h, w);
            TX(1, "How many squares wide is it?");
            CAP("%d squares, %d long!", w * h, w);
            p->ans = h;
            W(p, w * h - w, MC_WRONG_OP), W(p, h + 1, MC_CLOSE), W(p, w * h / 2 - w > 0 ? w * h / 2 - w : 1, MC_AREA_PERIM);
        } else {
            mq_story(q, p, ST_RUG, -1, w, h, 0, NULL);
            p->ans = w * h;
            W(p, 2 * (w + h), MC_AREA_PERIM), W(p, w + h, MC_ADD_NOT_MUL);
        }
        FX(0, w > 5 || h > 5);
        p->vis = V_GRID, p->v[0] = (int16_t)w, p->v[1] = (int16_t)h, p->v[2] = 1;
        p->mc_default = MC_AREA_PERIM;
        p->idea = IDEA_AREA;
        break;
    }
    case S_AREASPLIT: { /* add the two parts; the missing part; split off the tens */
        int kind = lvl(q, p, 0);
        h = R(q, 3, 7), w = kind == 2 ? R(q, 11, 16) : R(q, 6, 9);
        int c = kind == 2 ? 10 : R(q, 2, w - 2);
        TX(0, "Split the %d by %d mural:", w, h);
        if (kind == 0) {
            TX(1, "%d \xC3\x97 %d + %d \xC3\x97 %d = ?", h, c, h, w - c);
            TX(2, "How many squares in all?");
            p->ans = h * w;
            W(p, h * c, MC_ONE_STEP), W(p, h * (w - c), MC_ONE_STEP), W(p, h + w, MC_ADD_NOT_MUL);
        } else {
            TX(1, "%d \xC3\x97 %d = %d \xC3\x97 %d + %d \xC3\x97 ?", h, w, h, c, h);
            TX(2, "What number goes in the ?");
            p->ans = w - c;
            W(p, w, MC_PARTIAL_PRODUCT), W(p, h * (w - c), MC_ONE_STEP);
            if (c != w - c)
                W(p, c, MC_CARELESS);
        }
        p->vis = V_GRID, p->v[0] = (int16_t)w, p->v[1] = (int16_t)h, p->v[2] = 3, p->v[3] = (int16_t)c;
        p->mc_default = MC_PARTIAL_PRODUCT;
        p->idea = IDEA_AREA;
        break;
    }
    case S_AREAL: { /* on squared paper; the sides labelled; the take-away way */
        int kind = lvl(q, p, 0);
        int w1 = kind ? R(q, 4, 7) : R(q, 3, 5), h1 = kind ? R(q, 2, 3) : R(q, 1, 2);
        int w2 = kind ? R(q, 2, w1 - 2) : R(q, 1, w1 - 1), h2 = kind ? R(q, 2, 4) : R(q, 1, 2);
        if (kind == 2) {
            TX(0, "A %d by %d stage has a %d by %d", w1, h1 + h2, w1 - w2, h2);
            TX(1, "corner cut off. How many squares");
            TX(2, "are left to paint?");
            CAP("A stage with a corner cut off!");
            W(p, w1 * (h1 + h2), MC_ONE_STEP), W(p, (w1 - w2) * h2, MC_ONE_STEP);
        } else {
            TX(0, "%s paints an L-shaped stage.", dab);
            TX(1, "Split it into two rectangles.");
            TX(2, "How many squares to paint?");
            CAP("Paint the whole stage!");
            W(p, w1 * h1, MC_ONE_STEP), W(p, w2 * h2, MC_ONE_STEP), W(p, w1 * (h1 + h2), MC_CARELESS);
        }
        p->ans = w1 * h1 + w2 * h2;
        W(p, 2 * (w1 + h1 + h2), MC_AREA_PERIM);
        p->vis = V_LSHAPE, p->v[0] = (int16_t)w1, p->v[1] = (int16_t)h1, p->v[2] = (int16_t)w2, p->v[3] = (int16_t)h2;
        p->mc_default = MC_ONE_STEP;
        p->idea = IDEA_AREA;
        break;
    }
    case S_PERIM: { /* small frames; bigger frames; one side missing */
        int big = lvl(q, p, 0), miss = lvl(q, p, 1);
        w = big ? R(q, 3, 9) : R(q, 2, 5), h = big ? R(q, 2, 7) : R(q, 2, 4);
        if (miss) {
            TX(0, "A frame has a perimeter of %d cm.", 2 * (w + h));
            TX(1, "One side is %d cm long.", w);
            TX(2, "How long is the side next to it?");
            CAP("%d cm around, one side %d cm!", 2 * (w + h), w);
            p->ans = h;
            W(p, 2 * (w + h) - w, MC_PERIM_TWO), W(p, 2 * h + w, MC_PERIM_TWO), W(p, 2 * (w + h) / w, MC_AREA_PERIM);
        } else {
            mq_story(q, p, ST_FENCE, -1, w, h, 0, NULL);
            p->ans = 2 * (w + h);
            W(p, w * h, MC_AREA_PERIM), W(p, w + h, MC_PERIM_TWO), W(p, 2 * w + h, MC_PERIM_TWO);
        }
        FX(0, w > 5 || h > 4);
        p->vis = V_GRID, p->v[0] = (int16_t)w, p->v[1] = (int16_t)h, p->v[2] = 2;
        p->mc_default = MC_PERIM_TWO;
        p->idea = IDEA_AREA;
        break;
    }
    case S_SHAPES: { /* square, pentagon, hexagon; rectangles; rhombus and kite; trapezoid, parallelogram */
        static const uint8_t grp[4][2] = {{0, 9}, {1, 1}, {2, 8}, {3, 4}};
        static const uint8_t lvl_of[11] = {0, 1, 2, 3, 3, 0, 0, 0, 2, 0, 0};
        int set = lvl(q, p, 0);
        int k = set == 0 ? (R(q, 0, 2) == 2 ? 10 : grp[0][R(q, 0, 1)]) : grp[set][R(q, 0, 1)];
        TX(0, "%s drew a shape.", dab);
        TX(1, "Which family does it belong to?");
        CAP("Which shape family?");
        C(q, p, shape_name[k], MC_NONE);
        int others[3], no = 0;
        /* never a family the shape is also in (a square is a rectangle and a rhombus too; every
         * parallelogram has a pair of parallel sides, a trapezoid's mark): exactly one right answer */
        static const uint16_t also[11] = {(1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 8), (1 << 3) | (1 << 4),
                                          (1 << 3) | (1 << 4) | (1 << 8), 0, 0, 0, 0, 0, 0, 0, 0};
        static const uint8_t pool[] = {0, 9, 10, 1, 2, 8, 3, 4};
        int top = mq_rung_max(p->skill, p->diff, 0), npool = top == 0 ? 3 : top == 1 ? 4 : top == 2 ? 6 : 8;
        for (int tries = 0; no < 3 && tries < 80; tries++) {
            int o = pool[R(q, 0, npool - 1)];
            bool dup = o == k || ((also[k] >> o) & 1);
            for (int i = 0; i < no; i++)
                dup |= others[i] == o;
            if (!dup)
                others[no++] = o;
        }
        for (int i = 0; i < no; i++)
            C(q, p, shape_name[others[i]], MC_SHAPE_LOOK);
        choices_done(q, p, false);
        p->vis = V_SHAPE, p->v[0] = (int16_t)k, p->v[1] = (int16_t)R(q, 0, 1);
        FX(0, lvl_of[k]);
        p->mc_default = MC_SHAPE_LOOK;
        break;
    }
    case S_FORMULA: { /* area by rule; perimeter by rule; a missing side */
        int kind = lvl(q, p, 0);
        w = R(q, 6, 15), h = R(q, 3, 9);
        if (kind == 0) {
            mq_story(q, p, ST_RUG, -1, w, h, 0, NULL);
            p->ans = w * h;
            W(p, 2 * (w + h), MC_AREA_PERIM);
        } else if (kind == 1) {
            TX(0, "The studio floor is %d m by %d m.", w, h);
            TX(1, "How many m is it all around?");
            TX(2, "(P = 2 \xC3\x97 l + 2 \xC3\x97 w)");
            CAP("%d m by %d m: all around?", w, h);
            p->ans = 2 * (w + h);
            W(p, w * h, MC_AREA_PERIM), W(p, w + h, MC_PERIM_TWO);
        } else {
            TX(0, "A rug has an area of %d square m.", w * h);
            TX(1, "It is %d m long. How wide is it?", w);
            CAP("%d square m, %d m long!", w * h, w);
            p->ans = h;
            W(p, w * h - w, MC_WRONG_OP), W(p, w * h / 2 - w, MC_AREA_PERIM);
        }
        p->vis = V_GRID, p->v[0] = (int16_t)w, p->v[1] = (int16_t)h, p->v[2] = 1;
        p->mc_default = MC_AREA_PERIM;
        p->idea = IDEA_AREA;
        break;
    }
    case S_ANGLES: { /* quarter and half turns; 3/4 and full; kinds of corner; tens of degrees; any */
        int kind = lvl(q, p, 0);
        if (kind <= 1) {
            static const int16_t turns[4][2] = {{1, 4}, {1, 2}, {3, 4}, {1, 1}};
            int k = kind * 2 + R(q, 0, 1);
            TX(0, "%s spins a turn of %d/%d circle.", dab, turns[k][0], turns[k][1]);
            if (k == 3)
                TX(0, "%s spins all the way round, once.", dab);
            TX(1, "How many degrees is that?");
            TX(2, "(A full spin is 360 degrees.)");
            if (k == 3)
                CAP("A full spin: degrees?");
            else
                CAP("A %d/%d spin: degrees?", turns[k][0], turns[k][1]);
            p->ans = 360 * turns[k][0] / turns[k][1];
            W(p, 100 * turns[k][0] / turns[k][1], MC_ANGLE_SIZE), W(p, turns[k][0] * 90 + 90, MC_ANGLE_SIZE);
            p->vis = V_ANGLE, p->v[0] = (int16_t)(p->ans >= 360 ? 359 : p->ans);
        } else if (kind == 2) {
            int deg = R(q, 2, 17) * 10;
            if (deg == 90)
                deg = 70;
            if (R(q, 0, 3) == 0)
                deg = 90;
            TX(0, "Which kind of corner is this?");
            C(q, p, deg < 90 ? "acute" : deg == 90 ? "right" : "obtuse", MC_NONE);
            if (deg >= 90)
                C(q, p, "acute", MC_ANGLE_SIZE);
            if (deg != 90)
                C(q, p, "right", MC_ANGLE_SIZE);
            if (deg <= 90)
                C(q, p, "obtuse", MC_ANGLE_SIZE);
            choices_done(q, p, false);
            p->vis = V_ANGLE, p->v[0] = (int16_t)deg;
        } else {
            int deg = kind == 3 ? R(q, 2, 17) * 10 : R(q, 15, 170);
            if (kind == 4 && deg % 10 == 0)
                deg += R(q, 1, 4);
            TX(0, "An angle is made of %d", deg);
            TX(1, "one-degree angles. How many degrees?");
            CAP("%d tiny angles in a row!", deg);
            p->ans = deg;
            W(p, 180 - deg, MC_PROTRACTOR), W(p, deg + 1, MC_COUNT_TICKS);
            p->vis = V_ANGLE, p->v[0] = (int16_t)deg;
        }
        p->mc_default = MC_ANGLE_SIZE;
        p->idea = IDEA_ANGLE;
        break;
    }
    case S_PROTRACTOR: { /* 0 to 90 by tens; to 180; by fives; the other scale */
        int kind = lvl(q, p, 0);
        int deg = kind == 0 ? R(q, 1, 8) * 10 : kind == 2 ? R(q, 3, 34) * 5 : R(q, 1, 17) * 10;
        if (kind == 1 && deg <= 90)
            deg = R(q, 10, 17) * 10;
        if (kind == 2 && deg % 10 == 0)
            deg += 5;
        if (deg == 90)
            deg = kind == 0 ? 80 : 110;
        TX(0, "The kite string's angle:");
        TX(1, "How many degrees is the angle?");
        CAP("How many degrees?");
        p->ans = deg;
        W(p, 180 - deg, MC_PROTRACTOR), W(p, deg + 10, MC_COUNT_TICKS), W(p, deg - 10, MC_COUNT_TICKS);
        p->vis = V_ANGLE, p->v[0] = (int16_t)deg, p->v[2] = 1, p->v[3] = (int16_t)(kind == 3); /* 3: it opens from the left */
        p->mc_default = MC_PROTRACTOR;
        p->idea = IDEA_ANGLE;
        break;
    }
    case S_ANGLEADD: { /* two tens together; a right angle's partner; a straight angle's partner; by fives */
        int kind = lvl(q, p, 0), fives = kind == 3;
        if (fives)
            kind = R(q, 0, 2);
        int a = fives ? R(q, 3, 15) * 5 + 5 * (R(q, 0, 1)) : R(q, 2, 8) * 10, b = fives ? R(q, 3, 12) * 5 : R(q, 2, 8) * 10;
        if (fives && a % 10 == 0 && b % 10 == 0)
            a += 5;
        if (kind == 0) {
            if (a + b > 170)
                b = 170 - a > 10 ? (170 - a) / 10 * 10 : 10;
            TX(0, "Two angles of %d and %d degrees", a, b);
            TX(1, "fit together. How big is the angle?");
            CAP("%d and %d degrees together!", a, b);
            p->ans = a + b;
            W(p, a > b ? a - b : b - a, MC_WRONG_OP), W(p, 180 - a - b, MC_PROTRACTOR);
            p->vis = V_ANGLE, p->v[0] = (int16_t)(a + b), p->v[1] = (int16_t)a;
        } else {
            int whole = kind == 1 ? 90 : 180;
            if (a >= whole - 5)
                a = whole == 90 ? 35 : 125;
            TX(0, kind == 1 ? "A right angle is split in two." : "A straight line is split in two.");
            TX(1, "One part is %d degrees. How many degrees", a);
            TX(2, "is the other part?");
            CAP(kind == 1 ? "A right angle: %d and ...?" : "A straight line: %d and ...?", a);
            p->ans = whole - a;
            W(p, (whole == 90 ? 180 : 90) - a > 0 ? (whole == 90 ? 180 : 90) - a : whole + a, MC_ANGLE_SIZE);
            W(p, whole + a, MC_WRONG_OP), W(p, (whole == 90 ? 100 : 200) - a, MC_ELAPSED_100);
            p->vis = V_ANGLE, p->v[0] = (int16_t)whole, p->v[1] = (int16_t)a;
        }
        FX(0, fives ? 3 : kind);
        p->mc_default = MC_WRONG_OP;
        p->idea = IDEA_ANGLE;
        break;
    }
    case S_LINES: { /* segment or line; a ray; parallel and perpendicular */
        static const char nm[5][14] = {"segment", "ray", "line", "parallel", "perpendicular"};
        static const uint8_t lv[5] = {0, 1, 0, 2, 2};
        int top = lvl(q, p, 0), k;
        do /* (a line of the problem's level: lvl mixes in the older ones) */
            k = R(q, 0, 4);
        while (lv[k] != top);
        TX(0, "What did %s paint?", dab);
        C(q, p, nm[k], MC_NONE);
        int used = 1 << k;
        int maxc = mq_rung_max(p->skill, p->diff, 0);
        for (int tries = 0; p->nchoice < 4 && tries < 40; tries++) {
            int o = R(q, 0, 4);
            if ((used >> o) & 1 || lv[o] > maxc)
                continue;
            used |= 1 << o;
            C(q, p, nm[o], MC_LINE_RAY);
        }
        choices_done(q, p, false);
        p->vis = V_LINES, p->v[0] = (int16_t)k;
        FX(0, lv[k]);
        p->mc_default = MC_LINE_RAY;
        break;
    }
    case S_CLASSIFY: { /* a square corner or not; sharp or wide corners; parallel sides */
        int kind = lvl(q, p, 0);
        if (kind <= 1) {
            int k = kind == 0 ? R(q, 5, 6) : R(q, 5, 7);
            TX(0, "Which triangle family is this?");
            C(q, p, shape_name[k], MC_NONE);
            for (int o = 5; o <= (kind == 0 ? 6 : 7); o++)
                if (o != k)
                    C(q, p, shape_name[o], MC_ANGLE_SIZE);
            choices_done(q, p, false);
            p->vis = V_SHAPE, p->v[0] = (int16_t)k;
            p->mc_default = MC_ANGLE_SIZE;
            FX(0, k == 7 ? 1 : kind);
        } else {
            static const uint8_t par[5] = {2, 2, 2, 1, 2}; /* pairs of parallel sides of shapes 0..4 */
            int k = R(q, 0, 4);
            TX(0, "How many pairs of parallel sides");
            TX(1, "does this %s have?", shape_name[k]);
            CAP("Parallel pairs on the %s?", shape_name[k]);
            p->ans = par[k];
            W(p, par[k] == 2 ? 4 : 2, MC_SHAPE_LOOK), W(p, 0, MC_SHAPE_LOOK), W(p, 1, MC_SHAPE_LOOK);
            p->vis = V_SHAPE, p->v[0] = (int16_t)k;
            p->mc_default = MC_SHAPE_LOOK;
        }
        p->idea = IDEA_ANGLE;
        break;
    }
    case S_SYMMETRY: { /* fold: yes or no; diagonal folds; count: rectangle; count: square */
        int kind = lvl(q, p, 0);
        if (kind >= 2) {
            static const uint8_t cnt[2][3] = {{1, 3, 2}, {0, 4, 0}};
            static const uint8_t nlines[5] = {4, 2, 2, 1, 0};
            int k = cnt[kind - 2][R(q, 0, 2)];
            TX(0, "How many lines of symmetry");
            TX(1, "does this %s have?", shape_name[k]);
            CAP("Mirror lines of the %s?", shape_name[k]);
            p->ans = nlines[k];
            W(p, k == 1 ? 4 : nlines[k] + 2, MC_SYMMETRY_DIAG), W(p, nlines[k] == 1 ? 2 : 1, MC_SYMMETRY_DIAG);
            p->vis = V_SHAPE, p->v[0] = (int16_t)k, p->v[1] = 0, p->v[2] = 4;
            FX(0, k == 0 || k == 4 ? 3 : 2);
        } else {
            /* shape, line (0 up and down, 1 across, 2 corner to corner), a mirror line? */
            static const uint8_t cases[2][8][3] = {
                {{1, 0, 1}, {0, 0, 1}, {3, 0, 1}, {3, 1, 0}, {1, 1, 1}, {2, 0, 1}, {4, 0, 0}, {9, 1, 0}},
                {{0, 2, 1}, {1, 2, 0}, {2, 2, 0}, {4, 2, 0}, {0, 2, 1}, {1, 2, 0}, {9, 0, 1}, {4, 1, 0}}};
            const uint8_t *c = cases[kind][R(q, 0, 7)];
            TX(0, "Is the dashed line a mirror line?");
            TX(1, "(Would the halves match if folded?)");
            CAP("Is it a mirror line?");
            C(q, p, c[2] ? "yes" : "no", MC_NONE);
            C(q, p, c[2] ? "no" : "yes", MC_SYMMETRY_DIAG);
            choices_done(q, p, true);
            p->vis = V_SHAPE, p->v[0] = (int16_t)c[0], p->v[2] = (int16_t)(c[1] + 1);
            FX(0, c[1] == 2);
        }
        p->mc_default = MC_SYMMETRY_DIAG;
        break;
    }
    }
}

static void g_fair(mq_t *q, mq_prob_t *p, int d)
{
    const char *br = cast(q, 6);
    int n = 4, t;
    static const char fruit[4][10] = {"apples", "pears", "plums", "grapes"};
    (void)d;
    switch (p->skill) {
    case S_GRAPH1:
    case S_SCALED: {
        int key = 0, kind = 0;
        if (p->skill == S_GRAPH1)
            kind = lvl(q, p, 0); /* read one; two together; how many more */
        else {
            key = lvl(q, p, 0), kind = lvl(q, p, 1) ? R(q, 1, 3) : 0; /* key of 2, 5, 10; read, then compare */
        }
        int scale = p->skill == S_GRAPH1 ? 1 : key == 0 ? 2 : key == 1 ? 5 : 10;
        for (int i = 0; i < n; i++)
            p->v[2 + i] = (int16_t)(R(q, 1, 8) * scale);
        int a = R(q, 0, 3), b = (a + R(q, 1, 3)) % 4;
        if (p->v[2 + a] == p->v[2 + b])
            p->v[2 + a] = (int16_t)(p->v[2 + a] + scale);
        p->vis = V_BARGRAPH, p->v[0] = (int16_t)n, p->v[1] = (int16_t)scale, p->v[6] = (int16_t)R(q, 0, 1);
        t = p->skill == S_GRAPH1 ? (kind == 0 ? 0 : kind == 1 ? 2 : 1 + 2 * R(q, 0, 1)) : kind;
        if (t == 3) { /* how many fewer */
            int hi = p->v[2 + a] > p->v[2 + b] ? a : b, lo = hi == a ? b : a;
            TX(0, "%s's fruit vote graph:", br);
            TX(1, "How many fewer voted for %s", fruit[lo]);
            TX(2, "than for %s?", fruit[hi]);
            CAP("%s vs %s: how many fewer?", fruit[lo], fruit[hi]);
            p->ans = p->v[2 + hi] - p->v[2 + lo];
            if (scale > 1)
                W(p, p->ans / scale, MC_SCALE_IGNORED); /* counted symbols, skipped the key */
            W(p, p->v[2 + lo], MC_ONE_STEP), W(p, p->v[2 + hi] + p->v[2 + lo], MC_WRONG_OP);
            p->mc_default = scale > 1 ? MC_SCALE_IGNORED : MC_GRAPH_READ;
        } else if (t == 0) {
            TX(0, "%s's fruit vote graph:", br);
            TX(1, "How many votes for %s?", fruit[a]);
            CAP("Votes for %s?", fruit[a]);
            if (scale > 1)
                TX(2, p->v[6] ? "(Look at the key!)" : "(Check the scale!)");
            p->ans = p->v[2 + a];
            if (scale > 1)
                W(p, p->v[2 + a] / scale, MC_SCALE_IGNORED);
            W(p, p->v[2 + b], MC_GRAPH_READ);
            p->mc_default = scale > 1 ? MC_SCALE_IGNORED : MC_GRAPH_READ;
        } else if (t == 1) {
            int hi = p->v[2 + a] > p->v[2 + b] ? a : b, lo = hi == a ? b : a;
            TX(0, "%s's fruit vote graph:", br);
            TX(1, "How many more voted for %s", fruit[hi]);
            TX(2, "than for %s?", fruit[lo]);
            CAP("%s vs %s: how many more?", fruit[hi], fruit[lo]);
            p->ans = p->v[2 + hi] - p->v[2 + lo];
            W(p, p->v[2 + hi], MC_ONE_STEP), W(p, p->v[2 + hi] + p->v[2 + lo], MC_WRONG_OP);
            if (scale > 1)
                W(p, p->ans / scale, MC_SCALE_IGNORED);
            p->mc_default = MC_GRAPH_READ;
        } else {
            TX(0, "%s's fruit vote graph:", br);
            TX(1, "How many votes for %s and %s", fruit[a], fruit[b]);
            TX(2, "together?");
            CAP("%s and %s together?", fruit[a], fruit[b]);
            p->ans = p->v[2 + a] + p->v[2 + b];
            W(p, p->v[2 + a] > p->v[2 + b] ? p->v[2 + a] - p->v[2 + b] : p->v[2 + b] - p->v[2 + a], MC_WRONG_OP);
            if (scale > 1)
                W(p, p->ans / scale, MC_SCALE_IGNORED);
            p->mc_default = MC_GRAPH_READ;
        }
        if (p->skill == S_GRAPH1)
            FX(0, t == 0 ? 0 : t == 2 ? 1 : 2);
        else
            FX(1, t != 0);
        p->a[0] = (int16_t)a, p->a[1] = (int16_t)b;
        break;
    }
    case S_RULER: { /* whole inches; halves; quarters */
        int lv = lvl(q, p, 0), den = lv == 0 ? 1 : lv == 1 ? 2 : 4;
        int len = den == 1 ? R(q, 2, 5) : R(q, den + 1, den * 5 - 1); /* in 1/den inches */
        if (den > 1 && len % den == 0)
            len++;
        if (den == 4 && len % 2 == 0 && R(q, 0, 2)) /* (mostly a real quarter) */
            len = len + 1 < 20 ? len + 1 : len - 1;
        char s[16];
        TX(0, "%s measures a ribbon.", br);
        TX(1, "How many inches long is it?");
        p->vis = V_RULER, p->v[0] = (int16_t)len, p->v[1] = (int16_t)den, p->v[2] = 1;
        /* choices: right, one tick off either way, the whole-inch count */
        int opt[4] = {len, len + 1, len - 1, den > 1 ? len / den * den : len + 2};
        int mcs[4] = {MC_NONE, MC_COUNT_TICKS, MC_COUNT_TICKS, MC_CARELESS};
        for (int i = 0; i < 4; i++) {
            int L = opt[i], wh = L / den, f = L % den, g = f ? gcd(f, den) : 1;
            bool dup = false;
            for (int j = 0; j < i; j++)
                dup |= opt[j] == L;
            if (i && (dup || L <= 0))
                continue;
            if (f && wh)
                q->api->snprintf(s, sizeof s, "%d %d/%d in", wh, f / g, den / g);
            else if (f)
                q->api->snprintf(s, sizeof s, "%d/%d in", f / g, den / g);
            else
                q->api->snprintf(s, sizeof s, "%d in", wh);
            C(q, p, s, mcs[i]);
        }
        choices_done(q, p, false);
        FX(0, len % den == 0 ? 0 : (den == 4 && len % 2) ? 2 : 1);
        p->mc_default = MC_COUNT_TICKS;
        break;
    }
    case S_LINEPLOT: { /* count the marks; how many more; the total length */
        /* ribbon lengths at 0, 1/4, ..., 6/4 of an inch beyond 1 inch: counts in v[1..7] */
        for (int i = 1; i <= 6; i++)
            p->v[i] = (int16_t)R(q, 0, 4);
        p->v[0] = 4;
        p->vis = V_LINEPLOT;
        int k = R(q, 1, 6);
        if (!p->v[k])
            p->v[k] = 2;
        t = lvl(q, p, 0);
        if (t == 0) {
            TX(0, "%s's line plot of ribbon lengths.", br);
            TX(1, "How many ribbons are %d/4 in long?", k + 3);
            TX(2, "(each X is one ribbon)");
            CAP("Ribbons of %d/4 in?", k + 3);
            p->ans = p->v[k];
            W(p, p->v[k > 1 ? k - 1 : k + 1], MC_GRAPH_READ);
        } else if (t == 1) {
            int a = R(q, 1, 3), b = R(q, 4, 6);
            TX(0, "How many more ribbons are %d/4 in", b + 3);
            TX(1, "than %d/4 in long?", a + 3);
            CAP("%d/4 vs %d/4 in: how many more?", b + 3, a + 3);
            if (p->v[b] <= p->v[a])
                p->v[b] = (int16_t)(p->v[a] + 1);
            p->ans = p->v[b] - p->v[a];
            W(p, p->v[b], MC_ONE_STEP), W(p, p->v[a] + p->v[b], MC_WRONG_OP);
        } else {
            TX(0, "All the %d/4 inch ribbons are laid", k + 3);
            TX(1, "end to end. How many inches is that?");
            CAP("All %d/4 in ribbons, end to end!", k + 3);
            p->kind = K_FRAC, p->equiv = 1;
            p->ans = FR(p->v[k] * (k + 3), 4);
            W(p, FR(p->v[k], 4), MC_ONE_STEP), W(p, FR(p->v[k] * (k + 3), 4 * p->v[k]), MC_FRAC_TIMES_BOTH);
        }
        p->a[0] = (int16_t)k;
        p->mc_default = MC_GRAPH_READ;
        break;
    }
    case S_TABLE: { /* read a row; how many more; together; bigger numbers */
        int kind = lvl(q, p, 0), big = lvl(q, p, 1);
        for (int i = 0; i < 4; i++)
            p->v[1 + i] = (int16_t)(big ? R(q, 21, 99) : R(q, 3, 20));
        p->v[0] = 4, p->vis = V_TABLE;
        int a = R(q, 0, 3), b = (a + R(q, 1, 3)) % 4;
        if (p->v[1 + a] == p->v[1 + b])
            p->v[1 + a]++;
        TX(0, "Tickets won at the fair (table).");
        if (kind == 0) {
            TX(1, "How many tickets did %s win?", mq_cast_name(q, a));
            CAP("%s's tickets?", mq_cast_name(q, a));
            p->ans = p->v[1 + a];
            W(p, p->v[1 + b], MC_GRAPH_READ), W(p, p->v[1 + (a + 1) % 4], MC_GRAPH_READ);
        } else if (kind == 1) {
            int hi = p->v[1 + a] > p->v[1 + b] ? a : b, lo = hi == a ? b : a;
            TX(1, "How many more did %s win", mq_cast_name(q, hi));
            TX(2, "than %s?", mq_cast_name(q, lo));
            CAP("%s vs %s: how many more?", mq_cast_name(q, hi), mq_cast_name(q, lo));
            p->ans = p->v[1 + hi] - p->v[1 + lo];
            W(p, p->v[1 + hi], MC_ONE_STEP), W(p, p->v[1 + hi] + p->v[1 + lo], MC_WRONG_OP);
        } else {
            TX(1, "%s and %s share their tickets.", mq_cast_name(q, a), mq_cast_name(q, b));
            TX(2, "How many tickets together?");
            CAP("%s and %s together?", mq_cast_name(q, a), mq_cast_name(q, b));
            p->ans = p->v[1 + a] + p->v[1 + b];
            W(p, no_carry_add(p->v[1 + a], p->v[1 + b]), MC_NO_REGROUP);
            W(p, p->v[1 + a] + p->v[1 + (b + 1) % 4], MC_GRAPH_READ);
        }
        p->a[0] = (int16_t)a, p->a[1] = (int16_t)b;
        p->mc_default = MC_GRAPH_READ;
        break;
    }
    }
}

static void g_sky(mq_t *q, mq_prob_t *p, int d)
{
    const char *nova = cast(q, 8);
    (void)d;
    switch (p->skill) {
    case S_MULBIG: g_product(q, p); break;
    case S_FRUNLIKE: { /* one bottom fits the other; more than a whole; take away; any two bottoms */
        static const uint8_t pr[][2] = {{2, 4}, {2, 6}, {3, 6}, {4, 8}, {2, 8}, {3, 9}, {5, 10}, {2, 10}};
        static const uint8_t un[][2] = {{2, 3}, {3, 4}, {2, 5}, {3, 5}, {4, 6}, {2, 7}};
        int kind = lvl(q, p, 0);
        int d1, d2, a1, a2;
        if (kind == 3) {
            const uint8_t *pp = un[R(q, 0, 5)];
            d1 = pp[0], d2 = pp[1];
        } else {
            const uint8_t *pp = pr[R(q, 0, 7)];
            d1 = pp[0], d2 = pp[1];
        }
        int L = d1 * d2 / gcd(d1, d2);
        bool sub = kind == 2 || (kind == 3 && R(q, 0, 2) == 0);
        for (int t = 0; t < 100; t++) {
            a1 = R(q, 1, d1 - 1), a2 = R(q, 1, d2 - 1);
            int s = a1 * (L / d1) + a2 * (L / d2), df = a1 * (L / d1) - a2 * (L / d2);
            if (sub ? df > 0 : kind == 1 ? s > L : kind == 0 ? s < L : true)
                break;
        }
        if (sub) {
            if (a1 * (L / d1) <= a2 * (L / d2))
                a1 = d1 - 1, a2 = 1;
            TX(0, "%d/%d \xE2\x88\x92 %d/%d = ?", a1, d1, a2, d2);
            p->ans = FR(a1 * (L / d1) - a2 * (L / d2), L);
            W(p, FR(a1 - a2 > 0 ? a1 - a2 : a2 - a1, d2 - d1 > 0 ? d2 - d1 : d1 - d2), MC_ADD_DENOMS);
        } else {
            TX(0, "%d/%d + %d/%d = ?", a1, d1, a2, d2);
            p->ans = FR(a1 * (L / d1) + a2 * (L / d2), L);
            W(p, FR(a1 + a2, d1 + d2), MC_ADD_DENOMS), W(p, FR(a1 + a2, L), MC_EQUIV_ADD);
        }
        TX(1, "(Make the slices the same size first.)");
        p->kind = K_FRAC, p->equiv = 1;
        p->vis = V_BAR2, p->v[0] = (int16_t)a1, p->v[1] = (int16_t)d1, p->v[2] = (int16_t)a2, p->v[3] = (int16_t)d2;
        FX(0, L != d1 && L != d2 ? 3 : sub ? 2 : a1 * (L / d1) + a2 * (L / d2) > L ? 1 : 0);
        p->mc_default = MC_ADD_DENOMS;
        p->idea = IDEA_FRACTION;
        break;
    }
    case S_DEC1000: { /* write thousandths; compare; in parts */
        int kind = lvl(q, p, 0);
        if (kind == 1) {
            int x = R(q, 1, 9), y = R(q, 101, 999);
            if (y / 100 == x)
                y = (x > 1 ? x - 1 : 0) * 100 + R(q, 1, 99);
            if (R(q, 0, 1))
                y = (x - 1 > 0 ? x - 1 : 0) * 100 + R(q, 10, 99);
            char la[16], lb[16];
            q->api->snprintf(la, sizeof la, "0.%d", x), q->api->snprintf(lb, sizeof lb, "0.%03d", y);
            TX(0, "Which star is brighter?");
            TX(1, "%s  ?  %s", la, lb);
            cmp_pick(q, p, x * 100 < y ? -1 : x * 100 > y, MC_DEC_LONGER, la, lb);
            p->mc_default = MC_DEC_LONGER;
        } else if (kind == 2) {
            int a = R(q, 1, 9), b = R(q, 1, 9), c = R(q, 1, 9);
            TX(0, "Glue the star dust together:");
            TX(1, "0.%d + 0.0%d + 0.00%d = ?", a, b, c);
            p->kind = K_DEC, p->ans = a * 100 + b * 10 + c;
            W(p, a + b + c, MC_DEC_PLACE), W(p, a * 100 + b + c * 10, MC_DEC_PLACE);
            p->mc_default = MC_DEC_PLACE;
        } else {
            int n = R(q, 1, 999);
            if (n % 10 == 0)
                n++;
            TX(0, "Write %d/1000 of a star on the sign!", n);
            p->kind = K_DEC, p->ans = n;
            W(p, n * 10, MC_DEC_PLACE), W(p, n * 100, MC_DEC_PLACE);
            p->mc_default = MC_DEC_PLACE;
        }
        p->vis = V_PV, p->big = 0, p->v[0] = -2;
        p->idea = IDEA_DECIMAL;
        break;
    }
    case S_VOLUME: { /* count the cubes; length x width x height; two boxes */
        int kind = lvl(q, p, 0);
        int l = kind == 0 ? R(q, 2, 3) : R(q, 2, 6), w = R(q, 2, kind == 0 ? 2 : 4), h = kind == 0 ? R(q, 2, 3) : R(q, 2, 4);
        p->vis = V_PRISM, p->v[0] = (int16_t)l, p->v[1] = (int16_t)w, p->v[2] = (int16_t)h;
        if (kind == 2) {
            int l2 = R(q, 2, 4), w2 = R(q, 2, 3), h2 = R(q, 2, 3);
            TX(0, "%s stacks two boxes of cubes:", nova);
            TX(1, "%d by %d by %d, and %d by %d by %d.", l, w, h, l2, w2, h2);
            TX(2, "How many cubes in all?");
            CAP("Two boxes of cubes!");
            p->ans = l * w * h + l2 * w2 * h2;
            W(p, l * w * h, MC_ONE_STEP), W(p, l2 * w2 * h2, MC_ONE_STEP), W(p, l * w + l2 * w2, MC_VOLUME_AREA);
            p->v[3] = (int16_t)l2, p->v[4] = (int16_t)w2, p->v[5] = (int16_t)h2;
        } else {
            TX(0, "%s fills a box with cubes:", nova);
            TX(1, "%d long, %d wide, %d high.", l, w, h);
            TX(2, "How many cubes fill it?");
            CAP("Cubes: %d long, %d wide, %d high!", l, w, h);
            p->ans = l * w * h;
            W(p, l * w, MC_VOLUME_AREA), W(p, l + w + h, MC_ADD_NOT_MUL), W(p, 2 * (l * w + w * h + l * h), MC_AREA_PERIM);
        }
        p->mc_default = MC_VOLUME_AREA;
        break;
    }
    case S_COORD: { /* along the bottom; a small map; the whole map */
        int kind = lvl(q, p, 0);
        int x = R(q, 1, kind == 1 ? 4 : 8), y = kind == 0 ? 0 : R(q, 1, kind == 1 ? 4 : 6);
        if (x == y)
            x = x % (kind == 1 ? 4 : 8) + 1;
        TX(0, "%s hid a treasure on the map.", nova);
        TX(1, "Where is it on the map?");
        CAP("Where is the treasure?");
        p->kind = K_COORD, p->ans = x * 100 + y;
        if (y)
            W(p, y * 100 + x, MC_COORD_SWAP);
        W(p, (x + 1) * 100 + y, MC_COUNT_TICKS), W(p, x * 100 + y + 1, MC_COUNT_TICKS);
        p->vis = V_COORD, p->v[0] = (int16_t)x, p->v[1] = (int16_t)y;
        FX(0, y == 0 ? 0 : x <= 4 && y <= 4 ? 1 : 2);
        p->mc_default = MC_COORD_SWAP;
        break;
    }
    }
}


/* ------------------------------------------------------------------ grade 5 (Sky Castle, Star Harbor) */
static void g_five(mq_t *q, mq_prob_t *p, int d)
{
    const char *nova = cast(q, 8);
    int a, b, c;
    (void)d;
    switch (p->skill) {
    case S_POW10: { /* 5.NBT.1-2: whole x 10; tenths x 10; x 100 and x 1000; smaller; tens multiplied */
        int kind = lvl(q, p, 0);
        if (kind == 4) { /* powers of ten */
            int k = R(q, 2, 4);
            TX(0, "%s stacks boxes of ten, %d times:", nova, k);
            TX(1, "10 x 10 ... (%d tens). How many stars?", k);
            CAP("%d tens multiplied: stars?", k);
            p->ans = 1;
            for (int i = 0; i < k; i++)
                p->ans *= 10;
            W(p, 10 * k, MC_ZEROS), W(p, p->ans / 10, MC_ZEROS), W(p, p->ans * 10, MC_ZEROS);
            p->mc_default = MC_ZEROS;
            p->vis = V_PV, p->big = p->ans, p->v[0] = -1;
            break;
        }
        int pw = kind == 2 ? (R(q, 0, 1) ? 100 : 1000) : kind == 3 ? (R(q, 0, 1) ? 10 : 100) : 10;
        int32_t n = kind == 0 ? R(q, 2, 99) * 10 : R(q, 11, 99); /* tenths: 1.1 .. 9.9; whole numbers: 2 .. 99 */
        if (n % 10 == 0 && kind)
            n++;
        p->kind = K_DEC;
        if (kind != 3) { /* bigger */
            TX(0, "%s's grow ray makes it %d times bigger:", nova, pw);
            if (kind == 0)
                TX(1, "%ld cm becomes how many cm?", (long)(n / 10));
            else
                TX(1, "%ld.%ld cm becomes how many cm?", (long)(n / 10), (long)(n % 10));
            CAP("%ld.%ld cm, %d times bigger!", (long)(n / 10), (long)(n % 10), pw);
            if (kind == 0)
                CAP("%ld cm, %d times bigger!", (long)(n / 10), pw);
            p->ans = n * 100 * pw;
            W(p, n * 100 / pw, MC_PLACE_SHIFT), W(p, n * 100 * (pw / 10 ? pw / 10 : 1), MC_ZEROS);
            if (pw == 10)
                W(p, n * 100 + 0, MC_ZEROS); /* 4.7 -> 4.70: a zero stuck on the end */
        } else { /* smaller */
            TX(0, "%s's shrink ray makes it %d times smaller:", nova, pw);
            TX(1, "%ld.%ld m becomes how many m?", (long)(n / 10), (long)(n % 10));
            CAP("%ld.%ld m, %d times smaller!", (long)(n / 10), (long)(n % 10), pw);
            p->ans = n * 100 / pw;
            W(p, n * 100 * pw, MC_PLACE_SHIFT), W(p, n * 100 / (pw * 10), MC_ZEROS);
        }
        p->vis = V_PV, p->big = n, p->v[0] = -1;
        p->mc_default = MC_PLACE_SHIFT;
        p->idea = IDEA_DECIMAL;
        break;
    }
    case S_ROUNDDEC: { /* 5.NBT.4: to a whole; to a tenth; to a hundredth */
        int kind = lvl(q, p, 0);
        int32_t x = R(q, 1001, 9999); /* thousandths: 1.001 .. 9.999 */
        while (x % 10 == 0 || x % 100 == 50 || x % 1000 == 500)
            x++;
        int32_t unit = kind == 0 ? 1000 : kind == 1 ? 100 : 10;
        static const char un[3][10] = {"whole", "tenth", "hundredth"};
        TX(0, "A shooting star flew %ld.%03ld km.", (long)(x / 1000), (long)(x % 1000));
        TX(1, "Round it to the nearest %s!", un[kind]);
        CAP("%ld.%03ld km: the nearest %s!", (long)(x / 1000), (long)(x % 1000), un[kind]);
        p->kind = K_DEC, p->ans = (x + unit / 2) / unit * unit;
        W(p, x / unit * unit, MC_ROUND_DOWN_ALWAYS), W(p, x / unit * unit + unit, MC_ROUND_DIGIT);
        if (unit > 10)
            W(p, (x + unit / 20) / (unit / 10) * (unit / 10), MC_ROUND_DIGIT);
        if (unit < 1000)
            W(p, (x + unit * 5) / (unit * 10) * (unit * 10), MC_ROUND_DIGIT);
        p->vis = V_NONE;
        p->mc_default = MC_ROUND_DIGIT;
        p->idea = IDEA_DECIMAL;
        break;
    }
    case S_DECOPS: { /* 5.NBT.7: tenths + tenths; times a whole; hundredths and tenths; take away; share */
        int kind = lvl(q, p, 0);
        p->kind = K_DEC;
        if (kind == 0) {
            a = R(q, 1, 8), b = R(q, 1, 9 - a);
            TX(0, "%s pours 0.%d l of juice,", nova, a);
            TX(1, "then 0.%d l more. How much juice?", b);
            CAP("0.%d l, then 0.%d l more!", a, b);
            p->ans = (a + b) * 100;
            W(p, (a + b) * 10, MC_DEC_PLACE), W(p, a * 100 + b * 10, MC_DEC_PLACE);
        } else if (kind == 2 || kind == 3) {
            a = R(q, 105, 899), b = R(q, 11, 99); /* hundredths and tenths */
            if (b % 10 == 0)
                b++;
            if (kind == 2) {
                TX(0, "%s pours %d.%02d l of juice,", nova, a / 100, a % 100);
                TX(1, "then %d.%d l more. How much juice?", b / 10, b % 10);
                CAP("%d.%02d l, then %d.%d l more!", a / 100, a % 100, b / 10, b % 10);
                p->ans = (a + b * 10) * 10;
                W(p, (a + b) * 10, MC_DEC_PLACE), W(p, (a + b * 10 + 100) * 10, MC_NO_REGROUP);
            } else {
                if (b * 10 >= a)
                    b = a / 20;
                TX(0, "A rope is %d.%02d m. %s cuts off", a / 100, a % 100, nova);
                TX(1, "%d.%d m for a kite. How many m are left?", b / 10, b % 10);
                CAP("%d.%02d m of rope, %d.%d m cut!", a / 100, a % 100, b / 10, b % 10);
                p->ans = (a - b * 10) * 10;
                W(p, (a - b) * 10, MC_DEC_PLACE), W(p, (a + b * 10) * 10, MC_WRONG_OP);
            }
        } else if (kind == 1) {
            a = R(q, 2, 9), b = R(q, 2, 9);
            TX(0, "%s fills %d jars with 0.%d l of honey each.", nova, a, b);
            TX(1, "How much honey is that?");
            CAP("%d jars of 0.%d l honey!", a, b);
            p->ans = a * b * 100;
            W(p, a * b * 10, MC_DEC_PLACE), W(p, (a + b) * 100, MC_ADD_NOT_MUL);
        } else {
            a = R(q, 2, 9), c = R(q, 2, 9);
            b = a * c; /* tenths shared by a */
            TX(0, "Share %d.%d kg of berries equally into", b / 10, b % 10);
            TX(1, "%d baskets. How many kg go in each basket?", a);
            CAP("%d.%d kg into %d baskets!", b / 10, b % 10, a);
            p->ans = c * 100;
            W(p, c * 1000, MC_DEC_PLACE), W(p, c * 10, MC_DEC_PLACE);
        }
        p->vis = V_NONE;
        p->mc_default = MC_DEC_PLACE;
        p->idea = IDEA_DECIMAL;
        break;
    }
    case S_DIV2: { /* 5.NBT.6: by 10, 20, 30; by 11-15; 3-digit; bigger divisors; 4-digit */
        int kind = lvl(q, p, 0), dv;
        int32_t qt;
        if (kind == 0)
            dv = R(q, 1, 3) * 10, qt = R(q, 2, 9);
        else if (kind == 1)
            dv = R(q, 11, 15), qt = R(q, 2, 9);
        else if (kind == 2)
            dv = R(q, 11, 19), qt = R(q, (100 + dv - 1) / dv, 999 / dv);
        else if (kind == 3)
            dv = R(q, 21, 39), qt = R(q, 12, 99);
        else
            dv = R(q, 11, 39), qt = R(q, (1000 + dv - 1) / dv, 9999 / dv);
        if (qt < 2)
            qt = 2;
        TX(0, "%ld stars go into %d equal clouds.", (long)(qt * dv), dv);
        TX(1, "How many stars on each cloud?");
        CAP("%ld stars, %d clouds!", (long)(qt * dv), dv);
        p->ans = qt;
        W(p, qt + 1, MC_CLOSE), W(p, qt - 1, MC_CLOSE), W(p, qt * 10, MC_PLACE_SHIFT), W(p, qt / 10, MC_PLACE_SHIFT);
        FX(0, kind);
        p->vis = V_NONE;
        p->mc_default = MC_CLOSE;
        p->ws = WS_DIV, p->op[0] = qt * dv, p->op[1] = dv;
        p->idea = IDEA_SHARE;
        break;
    }
    case S_ORDEROPS: { /* 5.OA.1: a x (b + c); (a + b) x c; then take away; two brackets */
        int kind = lvl(q, p, 0);
        a = R(q, 2, 9), b = R(q, 2, 9), c = R(q, 2, 9);
        TX(0, "What number opens the castle door?");
        if (kind == 0) {
            TX(1, "%d \xC3\x97 (%d + %d)", a, b, c);
            p->ans = a * (b + c);
            W(p, a * b + c, MC_ORDER_OPS), W(p, a + b + c, MC_ADD_NOT_MUL);
        } else if (kind == 1) {
            TX(1, "(%d + %d) \xC3\x97 %d", a, b, c);
            p->ans = (a + b) * c;
            W(p, a + b * c, MC_ORDER_OPS), W(p, a + b + c, MC_ADD_NOT_MUL);
        } else if (kind == 2) {
            TX(1, "(%d + %d) \xC3\x97 %d \xE2\x88\x92 %d", a, b, c, a);
            p->ans = (a + b) * c - a;
            W(p, a + b * c - a, MC_ORDER_OPS), W(p, (a + b) * (c - a) > 0 ? (a + b) * (c - a) : (a + b) * c, MC_ORDER_OPS);
        } else {
            int s = R(q, 2, 5);
            TX(1, "[%d \xC3\x97 (%d + %d)] \xC3\xB7 %d", s * a, b, c, a);
            p->ans = s * (b + c);
            W(p, s * a * b + c, MC_ORDER_OPS), W(p, s * a * (b + c), MC_ONE_STEP);
        }
        p->vis = V_NONE;
        p->mc_default = MC_ORDER_OPS;
        break;
    }
    case S_EXPR: { /* 5.OA.2: words to spells; other orders; how many times as big */
        int kind = lvl(q, p, 0);
        a = R(q, 2, 9), b = R(q, 11, 40), c = R(q, 3, 19);
        char s[16];
        if (kind == 0) {
            TX(0, "Add %d and %d, then multiply by %d.", b, c, a);
            TX(1, "Which spell says that?");
            CAP("Which spell says it?");
            q->api->snprintf(s, sizeof s, "%d \xC3\x97 (%d + %d)", a, b, c), C(q, p, s, MC_NONE);
            q->api->snprintf(s, sizeof s, "%d \xC3\x97 %d + %d", a, b, c), C(q, p, s, MC_ORDER_OPS);
            q->api->snprintf(s, sizeof s, "%d + %d \xC3\x97 %d", b, c, a), C(q, p, s, MC_ORDER_OPS);
            q->api->snprintf(s, sizeof s, "%d + %d + %d", a, b, c), C(q, p, s, MC_ADD_NOT_MUL);
            choices_done(q, p, false);
        } else if (kind == 1) {
            TX(0, "Multiply %d by %d, then add %d.", b, a, c);
            TX(1, "Which spell says that?");
            CAP("Which spell says it?");
            q->api->snprintf(s, sizeof s, "%d \xC3\x97 %d + %d", b, a, c), C(q, p, s, MC_NONE);
            q->api->snprintf(s, sizeof s, "%d \xC3\x97 (%d + %d)", b, a, c), C(q, p, s, MC_ORDER_OPS);
            q->api->snprintf(s, sizeof s, "(%d + %d) \xC3\x97 %d", b, c, a), C(q, p, s, MC_ORDER_OPS);
            q->api->snprintf(s, sizeof s, "%d + %d + %d", b, a, c), C(q, p, s, MC_ADD_NOT_MUL);
            choices_done(q, p, false);
        } else {
            TX(0, "%d \xC3\x97 (%d + %d) is how many times", a, b, c);
            TX(1, "as big as %d + %d?", b, c);
            CAP("How many times as big?");
            p->ans = a;
            W(p, a * (b + c), MC_ONE_STEP), W(p, b + c, MC_ONE_STEP);
        }
        p->vis = V_NONE;
        p->mc_default = MC_ORDER_OPS;
        break;
    }
    case S_TWOPAT: { /* 5.OA.3: two patterns, small; bigger; how the two compare */
        int kind = lvl(q, p, 0);
        a = kind == 0 ? R(q, 2, 3) : R(q, 2, 5), b = a * R(q, 2, kind == 0 ? 3 : 4), c = kind == 0 ? R(q, 2, 4) : R(q, 3, 8);
        mq_story(q, p, ST_TIMES, 0, 0, 0, 0, NULL); /* (the friends) */
        char l[2][80], o[3][MQ_TXT];
        q->api->snprintf(l[0], sizeof l[0], "{A} saves %d coins a day, {B} saves %d.", a, b);
        if (kind == 2)
            q->api->snprintf(l[1], sizeof l[1], " {B} always has how many times as many as {A}?");
        else
            q->api->snprintf(l[1], sizeof l[1], " When {A} has %d, how many has {B}?", a * c);
        char all[200], ex[220];
        q->api->snprintf(all, sizeof all, "%s%s", l[0], l[1]);
        mq_story_expand(q, all, p->ca, p->cb, ex, sizeof ex);
        mq_story_wrap(q, ex, o, MQ_STORY_W);
        for (int i = 0; i < 3; i++)
            q->api->snprintf(p->text[i], MQ_TXT, "%s", o[i]);
        p->story = 0;
        if (kind == 2) {
            q->api->snprintf(all, sizeof all, "+%d and +%d a day: times as many?", a, b);
            mq_story_cap(q, p, all);
            p->ans = b / a;
            W(p, b - a, MC_TIMES_AS_ADD), W(p, b, MC_ONE_STEP);
        } else {
            q->api->snprintf(all, sizeof all, "+%d and +%d a day. {A}: %d!", a, b, a * c);
            mq_story_cap(q, p, all);
            p->ans = b * c;
            W(p, a * c + (b - a), MC_TIMES_AS_ADD), W(p, a * c + b, MC_PATTERN_STEP), W(p, b * (c + 1), MC_PATTERN_STEP);
        }
        p->vis = V_PATTERN, p->v[0] = 0, p->v[1] = (int16_t)a, p->v[4] = (int16_t)b; /* (both patterns: {B}'s "?" where {A}
                                                                                   * has a * c -- CR2-14) */
        if (kind != 2)
            p->v[3] = (int16_t)imin(9, c + 1);
        p->mc_default = MC_TIMES_AS_ADD;
        break;
    }
    case S_FRACWORD: { /* 5.NF.2: halves and quarters; thirds and sixths; any two bottoms */
        static const uint8_t den[3][4][2] = {{{2, 4}, {4, 2}, {2, 4}, {4, 2}}, {{3, 6}, {6, 3}, {3, 6}, {6, 3}},
                                             {{2, 3}, {2, 5}, {3, 4}, {3, 5}}};
        int kind = lvl(q, p, 0);
        const uint8_t *dd = den[kind][R(q, 0, 3)];
        int d1 = dd[0], d2 = dd[1];
        int n1 = R(q, 1, d1 - 1), n2 = R(q, 1, d2 - 1);
        int L = d1 * d2 / gcd(d1, d2);
        if (n1 * (L / d1) + n2 * (L / d2) > L && kind < 2)
            n1 = 1, n2 = 1;
        mq_story(q, p, ST_ADD, 0, 0, 0, 0, NULL);
        char all[160], ex[200];
        q->api->snprintf(all, sizeof all, "{A} eats %d/%d of a pizza and {B} eats %d/%d of it. How much pizza did they eat?",
                         n1, d1, n2, d2);
        mq_story_expand(q, all, p->ca, p->cb, ex, sizeof ex);
        mq_story_wrap(q, ex, p->text, MQ_STORY_W);
        p->story = 0;
        q->api->snprintf(all, sizeof all, "%d/%d + %d/%d of a pizza eaten!", n1, d1, n2, d2);
        mq_story_cap(q, p, all);
        p->kind = K_FRAC, p->equiv = 1, p->ans = FR(n1 * (L / d1) + n2 * (L / d2), L);
        W(p, FR(n1 + n2, d1 + d2), MC_ADD_DENOMS), W(p, FR(n1 + n2, L), MC_EQUIV_ADD);
        p->vis = V_BAR2, p->v[0] = (int16_t)n1, p->v[1] = (int16_t)d1, p->v[2] = (int16_t)n2, p->v[3] = (int16_t)d2;
        p->mc_default = MC_ADD_DENOMS;
        p->idea = IDEA_FRACTION;
        break;
    }
    case S_FRACDIV: { /* 5.NF.3: one pizza shared; fewer pizzas than friends; more pizzas */
        int kind = lvl(q, p, 0);
        if (kind == 0)
            a = 1, b = R(q, 2, 8);
        else if (kind == 1)
            a = R(q, 2, 7), b = R(q, a + 1, 9);
        else
            b = R(q, 2, 5), a = R(q, b + 1, imin(9, 4 * b - 1));
        if (a % b == 0)
            a = a + 1 < 4 * b ? a + 1 : a - 1;
        mq_story(q, p, ST_SHARE, 0, 0, 0, 0, NULL);
        char all[160], ex[200];
        q->api->snprintf(all, sizeof all, "%d %s for %d friends at {A}'s party, shared fairly. How much pizza each?",
                         a, a == 1 ? "pizza" : "pizzas", b);
        mq_story_expand(q, all, p->ca, p->cb, ex, sizeof ex);
        mq_story_wrap(q, ex, p->text, MQ_STORY_W);
        p->story = 0;
        q->api->snprintf(all, sizeof all, "%d %s, %d friends: each?", a, a == 1 ? "pizza" : "pizzas", b);
        mq_story_cap(q, p, all);
        p->kind = K_FRAC, p->equiv = 1, p->ans = FR(a, b);
        W(p, FR(b, a), MC_NUM_DENOM_SWAP), W(p, FR(1, b), MC_ONE_GROUP);
        p->vis = V_PIE, p->v[0] = (int16_t)a, p->v[1] = (int16_t)b, p->v[2] = (int16_t)((a + b - 1) / b); /* each one's share */
        FX(0, a == 1 ? 0 : a < b ? 1 : 2);
        p->mc_default = MC_NUM_DENOM_SWAP;
        p->idea = IDEA_FRACTION;
        break;
    }
    case S_FRACMUL: { /* 5.NF.4, 5.NF.6: half of a half; a slice of a slice; more slices of slices */
        int kind = lvl(q, p, 0);
        int d1, d2, n1, n2;
        if (kind == 0) /* half of a slice, or a slice of a half */
            d1 = 2, d2 = R(q, 2, 6), n1 = n2 = 1;
        else if (kind == 1)
            d1 = R(q, 2, 5), d2 = R(q, 2, 6), n1 = n2 = 1;
        else {
            d1 = R(q, 3, 5), d2 = R(q, 3, 6), n1 = R(q, 2, d1 - 1), n2 = R(q, 1, d2 - 1);
        }
        mq_story(q, p, ST_RUG, 0, 0, 0, 0, NULL);
        char all[160], ex[200];
        q->api->snprintf(all, sizeof all, "%d/%d of {A}'s garden has flowers, and %d/%d of those are tulips. How much of the garden is tulips?",
                         n2, d2, n1, d1);
        mq_story_expand(q, all, p->ca, p->cb, ex, sizeof ex);
        if (mq_story_wrap(q, ex, p->text, MQ_STORY_W) > 3) {
            TX(0, "%d/%d of the garden has flowers,", n2, d2);
            TX(1, "%d/%d of those are tulips.", n1, d1), TX(2, "How much of the garden is tulips?");
        }
        p->story = 0;
        q->api->snprintf(all, sizeof all, "Tulips: %d/%d of %d/%d of the garden!", n1, d1, n2, d2);
        mq_story_cap(q, p, all);
        p->kind = K_FRAC, p->equiv = 1, p->ans = FR(n1 * n2, d1 * d2);
        W(p, FR(n1 + n2, d1 + d2), MC_ADD_DENOMS), W(p, FR(n1 * n2, d1 + d2), MC_FRAC_TIMES_BOTH);
        if (d1 == d2)
            W(p, FR(n1 * n2, d1), MC_FRAC_TIMES_BOTH);
        p->vis = V_GRID, p->v[0] = (int16_t)d2, p->v[1] = (int16_t)d1, p->v[2] = 5, p->v[3] = (int16_t)(n1 * n2), p->v[4] = (int16_t)n2;
        p->mc_default = MC_FRAC_TIMES_BOTH;
        p->idea = IDEA_FRACTION;
        break;
    }
    case S_SCALING: { /* 5.NF.5: x2 or x1/2; x5/4; x3/4; x4/4 */
        int kind = lvl(q, p, 0);
        int n = R(q, 2, 9) * 4, f; /* f: 0 3/4, 1 5/4, 2 4/4, 3 2, 4 1/2 */
        static const char fr[5][8] = {"3/4", "5/4", "4/4", "2", "1/2"};
        f = kind == 0 ? 3 + R(q, 0, 1) : kind == 1 ? 1 : kind == 2 ? 0 : 2;
        bool less = f == 0 || f == 4, same = f == 2;
        TX(0, "A magic hat makes %d rabbits", n);
        TX(1, "%s times as many. More, fewer", fr[f]);
        TX(2, "or the same number of rabbits?");
        CAP("%d rabbits x %s: more or fewer?", n, fr[f]);
        C(q, p, less ? "fewer" : same ? "the same" : "more", MC_NONE);
        C(q, p, less ? "more" : "fewer", less ? MC_SCALE_BIGGER : MC_CARELESS);
        C(q, p, same ? "more" : "the same", same ? MC_SCALE_BIGGER : MC_CARELESS);
        choices_done(q, p, false);
        p->vis = V_NONE;
        p->mc_default = MC_SCALE_BIGGER;
        break;
    }
    case S_UNITDIV: { /* 5.NF.7: half cups; any whole by a slice; a slice shared */
        int kind = lvl(q, p, 0);
        if (kind == 2) {
            int u = R(q, 2, 5), k = R(q, 2, 4);
            mq_story(q, p, ST_SHARE, 0, 0, 0, 0, NULL);
            char all[160], ex[200];
            q->api->snprintf(all, sizeof all, "{A} has 1/%d of a cake left and shares it fairly with %d friends. How much cake each?",
                             u, k);
            mq_story_expand(q, all, p->ca, p->cb, ex, sizeof ex);
            mq_story_wrap(q, ex, p->text, MQ_STORY_W);
            p->story = 0;
            q->api->snprintf(all, sizeof all, "1/%d of a cake, %d friends!", u, k);
            mq_story_cap(q, p, all);
            p->kind = K_FRAC, p->equiv = 1, p->ans = FR(1, u * k);
            W(p, FR(k, u), MC_DIV_FLIP), W(p, FR(1, u + k), MC_ADD_DENOMS);
            p->vis = V_BAR, p->v[0] = 1, p->v[1] = (int16_t)(u * k);
        } else {
            int n = R(q, 2, 6), u = kind == 0 ? 2 : R(q, 3, 5);
            TX(0, "%s has %d cups of lemonade.", nova, n);
            TX(1, "Each glass gets 1/%d cup.", u);
            TX(2, "How many glasses can be filled?");
            CAP("%d cups, 1/%d cup a glass!", n, u);
            p->ans = n * u;
            W(p, n + u, MC_ADD_NOT_MUL), W(p, n, MC_DIV_FLIP), W(p, u, MC_DIV_FLIP);
            p->vis = V_BAR, p->v[0] = 0, p->v[1] = (int16_t)u, p->v[2] = 0;
        }
        p->mc_default = MC_DIV_FLIP;
        p->idea = IDEA_FRACTION;
        break;
    }
    case S_CONVERT5: { /* 5.MD.1: whole m to cm; cm to m; h and min to min; decimals to small units */
        int kind = lvl(q, p, 0);
        if (kind == 0) {
            a = R(q, 2, 9);
            TX(0, "A beanstalk is %d m tall.", a);
            TX(1, "How many cm is that?");
            CAP("%d m in cm?", a);
            p->ans = a * 100;
            W(p, a, MC_UNIT_WAY), W(p, a * 10, MC_ZEROS), W(p, a * 1000, MC_ZEROS);
        } else if (kind == 3) {
            if (R(q, 0, 1)) {
                a = R(q, 11, 99);
                if (a % 10 == 0)
                    a++;
                TX(0, "A beanstalk is %d.%d m tall.", a / 10, a % 10);
                TX(1, "How many cm is that?");
                CAP("%d.%d m in cm?", a / 10, a % 10);
                p->ans = a * 10;
                W(p, a, MC_UNIT_WAY), W(p, a * 100, MC_ZEROS);
            } else {
                a = R(q, 2, 9), b = R(q, 1, 9);
                TX(0, "A bag of star dust is %d.%d kg.", a, b);
                TX(1, "How many grams is that?");
                CAP("%d.%d kg in grams?", a, b);
                p->ans = a * 1000 + b * 100;
                W(p, a * 100 + b * 10, MC_ZEROS), W(p, a * 10 + b, MC_UNIT_WAY);
            }
        } else if (kind == 1) {
            a = R(q, 105, 975) / 5 * 5;
            TX(0, "The ribbon is %d cm long.", a);
            TX(1, "How many meters is that?");
            CAP("%d cm in meters?", a);
            p->kind = K_DEC, p->ans = a * 10;
            W(p, a * 100, MC_UNIT_WAY), W(p, a, MC_DEC_PLACE);
        } else {
            a = R(q, 1, 4), b = R(q, 1, 11) * 5;
            TX(0, "The rocket ride takes %d h %d min.", a, b);
            TX(1, "How many minutes is that?");
            CAP("%d h %d min in minutes?", a, b);
            p->ans = a * 60 + b;
            W(p, a * 100 + b, MC_ELAPSED_100), W(p, a + b, MC_UNIT_WAY);
        }
        p->vis = V_NONE;
        p->mc_default = MC_UNIT_WAY;
        break;
    }
    case S_LINEPLOT5: { /* 5.MD.2: all of it together; poured fairly */
        int kind = lvl(q, p, 0);
        int k = R(q, 3, 5), tot = 0;
        int16_t m[5];
        for (int i = 0; i < k; i++)
            m[i] = (int16_t)R(q, 1, 3), tot += m[i]; /* quarters */
        int jars = R(q, 0, 1) ? 2 : 4;
        if (kind)
            while (tot % jars)
                m[0]++, tot++;
        TX(0, "%d potion bottles, each 1/4, 2/4 or 3/4 full.", k);
        if (kind) {
            TX(1, "Pour them fairly into %d big jars:", jars);
            TX(2, "how much in each jar?");
            CAP("Pour %d bottles into %d jars!", k, jars);
            p->ans = FR(tot, 4 * jars);
            W(p, FR(tot, 4), MC_ONE_STEP), W(p, FR(tot, jars), MC_DIV_FLIP);
        } else {
            TX(1, "They hold %d quarter cups in all.", tot);
            TX(2, "How many cups is that?");
            CAP("%d bottles: how much in all?", k);
            p->ans = FR(tot, 4);
            W(p, FR(tot, 1), MC_ONE_STEP), W(p, FR(k, 4), MC_ONE_STEP);
        }
        p->kind = K_FRAC, p->equiv = 1;
        p->vis = V_NONE, p->a[0] = (int16_t)tot, p->a[1] = (int16_t)jars;
        p->mc_default = MC_ONE_STEP;
        p->idea = IDEA_FRACTION;
        break;
    }
    case S_SHAPES5: { /* 5.G.3-4: always-true riddles; not always; more families */
        static const char q5[3][4][44] = {
            {"Every square is also a rectangle.", "Every square is also a rhombus.", "Every rectangle has 4 right angles.",
             "Every square has 4 equal sides."},
            {"Every rectangle is also a square.", "Every rhombus is also a square.", "Every rectangle is a rhombus.",
             "Every square is a rectangle."},
            {"Every rhombus is a parallelogram.", "Every parallelogram is a rhombus.", "Every rectangle is a parallelogram.",
             "Every kite is a rhombus."}};
        static const uint8_t yes[3][4] = {{1, 1, 1, 1}, {0, 0, 0, 1}, {1, 0, 1, 0}};
        static const uint8_t shape_of[3][4] = {{0, 0, 1, 0}, {1, 2, 1, 0}, {2, 4, 1, 8}};
        int kind = lvl(q, p, 0), k = R(q, 0, 3);
        if (kind == 1 && k == 3 && R(q, 0, 1))
            k = R(q, 0, 2); /* (mostly the "not always" ones) */
        TX(0, "%s's shape riddle:", nova);
        TX(1, "\"%s\"", q5[kind][k]);
        TX(2, "Is the riddle true?");
        CAP("%s", q5[kind][k]);
        bool y = yes[kind][k];
        C(q, p, y ? "yes" : "not always", MC_NONE);
        C(q, p, y ? "not always" : "yes", MC_SHAPE_LOOK);
        choices_done(q, p, false);
        p->vis = V_SHAPE, p->v[0] = shape_of[kind][k];
        p->mc_default = MC_SHAPE_LOOK;
        break;
    }
    default: break;
    }
}

/* Fill p with a fresh problem of `skill` at difficulty `diff` (0..3). */
void mq_generate(mq_t *q, mq_prob_t *p, int skill, int diff)
{
    uint8_t *b = (uint8_t *)p;
    for (size_t i = 0; i < sizeof *p; i++)
        b[i] = 0;
    p->skill = (uint8_t)skill;
    int top = mq_rungs(skill) - 1;
    p->diff = (uint8_t)(diff < 0 ? 0 : diff > top ? top : diff); /* the rung (mq_rung.c) */
    p->kind = K_NUM;
    p->mc_default = MC_CARELESS;
    if (skill == S_FACTS) { /* the fact boxes are their own ladder; the rung caps the tables taught */
        static const uint8_t cap[5] = {3, 4, 6, 8, 12}; /* the last factor of the teaching order a rung allows */
        q->fact_cap = (uint8_t)(cap[p->diff > 4 ? 4 : p->diff] + 1);
        int f = mq_fact_next(q);
        q->fact_cap = 0;
        mq_fact_problem(q, p, f);
        int fa, fb;
        bool dv;
        mq_fact_decode(f, &fa, &fb, &dv);
        int r = mq_fact_rank_of(fa) > mq_fact_rank_of(fb) ? mq_fact_rank_of(fa) : mq_fact_rank_of(fb);
        p->cx = (uint16_t)(r <= 3 ? 0 : r == 4 ? 1 : r <= 6 ? 2 : r <= 8 ? 3 : 4);
        p->idea = IDEA_GROUPS;
    } else if (skill >= S_POW10)
        g_five(q, p, p->diff);
    else
        switch (mq_skill[skill].world) {
    case G_GARDEN: g_garden(q, p, p->diff); break;
    case G_POST: g_post(q, p, p->diff); break;
    case G_BAKERY: g_bakery(q, p, p->diff); break;
    case G_CLOCK: g_clock(q, p, p->diff); break;
    case G_SHOP: g_shop(q, p, p->diff); break;
    case G_ART: g_art(q, p, p->diff); break;
    case G_FAIR: g_fair(q, p, p->diff); break;
    default: g_sky(q, p, p->diff); break; /* (Sky Castle, Star Harbor's first skills) */
    }
    /* a "wrong" answer that is in fact right (13:05 is 1:05, 1/4 is 2/8) is not a mistake */
    int k = 0;
    for (int i = 0; i < p->nwrong; i++)
        if (!mq_correct(p, p->wrong[i]))
            p->wrong[k] = p->wrong[i], p->wrong_mc[k++] = p->wrong_mc[i];
    p->nwrong = (uint8_t)k;
}

/* ------------------------------------------------------------------ no repeats */
static uint32_t fnv(uint32_t h, int32_t v)
{
    for (int k = 0; k < 4; k++)
        h = (h ^ (uint8_t)(v >> (8 * k))) * 16777619u;
    return h;
}
static void sort_ints(int32_t *a, int n)
{
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && a[j] < a[j - 1]; j--) {
            int32_t t = a[j];
            a[j] = a[j - 1], a[j - 1] = t;
        }
}

/* What makes two problems "the same": the skill and the multiset of numbers in the story and
 * the model (6 x 7 and 7 x 6 alike: the same numbers in another order), and the answer. */
uint32_t mq_prob_ident(const mq_prob_t *p)
{
    if (p->ident) /* as generated (a game may have retold it) */
        return p->ident;
    int32_t num[24];
    int n = 0;
    for (int l = 0; l < 3; l++)
        for (const char *s = p->text[l]; *s && n < 16;) {
            if (*s >= '0' && *s <= '9') {
                int32_t v = 0;
                while (*s >= '0' && *s <= '9')
                    v = v * 10 + (*s++ - '0');
                num[n++] = v;
            } else
                s++;
        }
    for (int i = 0; i < 8 && n < 24; i++)
        if (p->v[i])
            num[n++] = p->v[i];
    sort_ints(num, n);
    uint32_t h = fnv(2166136261u, p->skill);
    for (int i = 0; i < n; i++)
        h = fnv(h, num[i]);
    h = fnv(h, p->ans);
    if (!n) /* no numbers at all (name the shape): the words then */
        for (const char *s = p->text[0]; *s; s++)
            h = fnv(h, *s);
    return fnv(h, p->big);
}

/* the answer and the picture, in order */
static uint32_t prob_look(const mq_prob_t *p)
{
    uint32_t h = fnv(fnv(2166136261u, p->ans), p->vis);
    for (int i = 0; i < 8; i++)
        h = fnv(h, p->v[i]);
    return fnv(h, p->big);
}

/* mq_generate, but never one of the last 10 problems (any skill, commutative twins included),
 * nor the same answer with the same picture as one of the last 3, nor one whose history says
 * not yet (answered right lately: mq_learn.c). Tiny problem sets (a choice among two shapes)
 * relax step by step and at least never repeat back to back. The facts have their own
 * scheduler (mq_fact_next keeps a fact away for the next problems; Leitner boxes). */
void mq_generate_fresh(mq_t *q, mq_prob_t *p, int skill, int diff)
{
    uint32_t *hid = q->hist_id[q->save.cur & 1], *hlk = q->hist_look[q->save.cur & 1];
    uint32_t id = 0, lk = 0;
    for (int t = 0; t < 80; t++) {
        mq_generate(q, p, skill, diff);
        id = mq_prob_ident(p), lk = prob_look(p);
        if (skill == S_FACTS)
            break;
        /* strict first; a skill with only a few possible problems relaxes step by step and
         * then alternates (never the same one twice in a row) */
        int nid = t < 20 ? 10 : t < 50 ? 3 : 1, nlk = t < 20 ? 3 : 1;
        bool bad = false;
        for (int i = 0; i < nid && !bad; i++)
            bad = hid[i] == id || (i < nlk && hlk[i] == lk);
        /* answered right lately (its history puts it away for a while, mq_learn.c) */
        if (!bad && t < 50 && mq_seen_blocked(q, id))
            bad = true;
        if (!bad)
            break;
    }
    for (int i = 9; i > 0; i--)
        hid[i] = hid[i - 1], hlk[i] = hlk[i - 1];
    hid[0] = id, hlk[0] = lk;
    p->ident = id;
}

/* ------------------------------------------------------------------ answers */
/* Parse what the player typed for problem p (see the encodings above). */
int mq_parse(const mq_prob_t *p, const char *in, int32_t *out)
{
    int32_t a = 0, b = 0;
    int na = 0, nb = 0, sep = 0, frac = 0;
    const char *s = in;
    switch (p->kind) {
    case K_NUM:
    case K_SHADE:
    case K_LINE:
    case K_CHOICE:
        for (; *s; s++) {
            if (*s < '0' || *s > '9' || na > 7)
                return -1;
            a = a * 10 + (*s - '0'), na++;
        }
        *out = a;
        return na ? 0 : -1;
    case K_TIME:
        for (; *s; s++) {
            if (*s == ':' && !sep)
                sep = 1;
            else if (*s >= '0' && *s <= '9' && na < 4)
                a = a * 10 + (*s - '0'), na++;
            else
                return -1;
        }
        if (na < 3)
            return -1;
        *out = a;
        return 0;
    case K_DEC:
    case K_MONEY: {
        int places = p->kind == K_DEC ? 3 : 2;
        for (; *s; s++) {
            if (*s == '.' && !sep)
                sep = 1;
            else if (*s >= '0' && *s <= '9') {
                if (sep) {
                    if (frac >= places)
                        return -1;
                    b = b * 10 + (*s - '0'), frac++;
                } else if (na < 6)
                    a = a * 10 + (*s - '0'), na++;
                else
                    return -1;
            } else
                return -1;
        }
        if (!na && !frac)
            return -1;
        while (frac < places)
            b *= 10, frac++;
        *out = a * (p->kind == K_DEC ? 1000 : 100) + b;
        return 0;
    }
    case K_FRAC:
    case K_REM:
    case K_COORD: {
        char want = p->kind == K_FRAC ? '/' : p->kind == K_REM ? 'R' : ',';
        for (; *s; s++) {
            if ((*s == want || (p->kind == K_REM && *s == 'r')) && !sep)
                sep = 1;
            else if (*s >= '0' && *s <= '9') {
                if (sep && nb < 4)
                    b = b * 10 + (*s - '0'), nb++;
                else if (!sep && na < 5)
                    a = a * 10 + (*s - '0'), na++;
                else
                    return -1;
            } else
                return -1;
        }
        if (!na)
            return -1;
        if (p->kind == K_REM && !sep)
            b = 0, nb = 1; /* no remainder typed: R0 */
        if (!nb || (p->kind == K_FRAC && b == 0))
            return -1;
        *out = p->kind == K_COORD ? a * 100 + b : a * 1000 + b;
        return 0;
    }
    }
    return -1;
}

bool mq_correct(const mq_prob_t *p, int32_t g)
{
    if (g == p->ans)
        return true;
    if (p->kind == K_FRAC && p->equiv) {
        int32_t n1 = g / 1000, d1 = g % 1000, n2 = p->ans / 1000, d2 = p->ans % 1000;
        return d1 && d2 && n1 * d2 == n2 * d1;
    }
    if (p->kind == K_TIME) /* 12:05 and 0:05 */
        return g % 100 == p->ans % 100 && (g / 100) % 12 == (p->ans / 100) % 12;
    return false;
}

/* The misconception behind a wrong answer: a known wrong answer of the
 * generator, else a near miss ("so close"), else the generator's most
 * likely one. */
int mq_classify(const mq_prob_t *p, int32_t g)
{
    if (mq_correct(p, g))
        return MC_NONE;
    if (p->kind == K_CHOICE)
        return g >= 0 && g < p->nchoice ? p->choice_mc[g] : MC_CARELESS;
    for (int i = 0; i < p->nwrong; i++) {
        if (p->wrong[i] == g)
            return p->wrong_mc[i];
        if (p->kind == K_FRAC && p->wrong[i] % 1000 && g % 1000 &&
            (int32_t)(p->wrong[i] / 1000) * (g % 1000) == (g / 1000) * (p->wrong[i] % 1000))
            return p->wrong_mc[i];
    }
    if (p->kind == K_NUM && p->ans > 3) {
        int32_t dlt = g - p->ans;
        if (dlt < 0)
            dlt = -dlt;
        if (dlt <= 1 || dlt * 20 <= p->ans)
            return MC_CLOSE;
    }
    return p->mc_default ? p->mc_default : MC_CARELESS;
}

/* Value v of problem p as the player would read it ("3/4", "17 R 3", "$2.65", "3:45"). */
void mq_format(mq_t *q, const mq_prob_t *p, int32_t v, char *out, size_t n)
{
    const modx_api_t *api = q->api;
    switch (p->kind) {
    case K_REM: api->snprintf(out, n, v % 1000 ? "%d R %d" : "%d", v / 1000, v % 1000); break;
    case K_FRAC: api->snprintf(out, n, "%d/%d", v / 1000, v % 1000); break;
    case K_DEC:
        if (v % 10)
            api->snprintf(out, n, "%d.%03d", v / 1000, v % 1000);
        else if (v % 100)
            api->snprintf(out, n, "%d.%02d", v / 1000, v % 1000 / 10);
        else
            api->snprintf(out, n, "%d.%d", v / 1000, v % 1000 / 100);
        break;
    case K_MONEY: api->snprintf(out, n, "$%d.%02d", v / 100, v % 100); break;
    case K_TIME: api->snprintf(out, n, "%d:%02d", v / 100, v % 100); break;
    case K_CHOICE: api->snprintf(out, n, "%s", v >= 0 && v < p->nchoice ? p->choice[v] : "?"); break;
    case K_COORD: api->snprintf(out, n, "(%d, %d)", v / 100, v % 100); break;
    case K_SHADE: api->snprintf(out, n, "%d slices", v); break;
    case K_LINE: api->snprintf(out, n, "%d/%d", v, p->v[2]); break;
    default: api->snprintf(out, n, "%d", v); break;
    }
}

/* The keys that type answer v (console `mq correct`, tests). */
void mq_answer_text(mq_t *q, const mq_prob_t *p, char *out, size_t n)
{
    int32_t v = p->ans;
    const modx_api_t *api = q->api;
    switch (p->kind) {
    case K_REM: api->snprintf(out, n, "%dR%d", v / 1000, v % 1000); break;
    case K_FRAC: api->snprintf(out, n, "%d/%d", v / 1000, v % 1000); break;
    case K_DEC: api->snprintf(out, n, "%d.%03d", v / 1000, v % 1000); break;
    case K_MONEY: api->snprintf(out, n, "%d.%02d", v / 100, v % 100); break;
    case K_TIME: api->snprintf(out, n, "%d%02d", v / 100, v % 100); break;
    case K_COORD: api->snprintf(out, n, "%d,%d", v / 100, v % 100); break;
    default: api->snprintf(out, n, "%d", v); break;
    }
}
