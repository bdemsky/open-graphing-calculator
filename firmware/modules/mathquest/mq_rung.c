/*
 * Math Quest: the ladders. Every skill is a ladder of rungs, and each rung adds exactly ONE new
 * complication to the one below (docs/MATHQUEST.md "Ladders"; the audit's L1-L4):
 *
 *   adding and taking away: 1-digit, nothing to carry -> 2-digit and 1-digit -> 2-digit and
 *   2-digit -> a carry in the ones -> two carries -> 3-digit numbers -> zeros in the middle, and on
 *   in "Big add and subtract" (4-digit, ...).
 *
 * A rung's complications are levels in up to four dimensions of its skill (how many digits, how many
 * carries, a zero inside, which kind of story ...): rung 0 is level 0 everywhere and every further
 * rung raises one dimension by one level -- that is the "one complication". The generators
 * (mq_gen.c) report the levels of every problem they make (mq_prob_t.cx), never above the rung's,
 * and most problems of a rung show its new complication; tests/unit/test_mathquest.c checks both,
 * and recomputes them from the numbers for the column sums, products and sharing.
 *
 * Her rung of a skill (mq_skrec_t.diff) moves only on first answers of that skill:
 *   - up one rung once the rung is solid: five or more answers there, the last four right first time,
 *     90 % or more of them (two will do on the first rungs while every answer of the skill so far was
 *     right and quick -- she already knew them; four more after she fell from the rung above), never
 *     past the top, never two at once;
 *   - down one rung when she struggles there: two misses in a row, or under 80 % over five or more;
 *   - together that settles her where she is right about 85 % of the time (the test's simulated
 *     players);
 *   - a rung is judged on its own answers (mq_pace_t.rung_win starts again at every step); the
 *     success-rate window of the grown-ups' log (mq_learn_t.recent, mq_coach.c) keeps every answer.
 * The strand controller (mq_engine.c) still moves whole grades; the extra order after a miss and the
 * helper friend's walkthrough are one rung easier (mq_main.c, mq_guide.c).
 *
 * Saves before the ladders kept a difficulty 0..3 in the same byte: mq_rung_migrate maps it onto
 * the ladder once (0 -> rung 0, 3 -> the band-3 rung) and sets mq_pace_t.rung_ver.
 */
#include "mq.h"

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

/* Per skill: the dimension each rung raises (one character a rung: '-' for rung 0, then '0'..'3'),
 * a '|', then the rungs' names separated by '|' (for the grown-ups; never shown to her). */
static const char ladder[MQ_NSKILL][184] = {
    [S_GROUPS] = "-001|2-5 groups of 2, 5 or 10|groups of 3 or 4|groups of 6 to 9|6 to 9 groups",
    [S_SHARE] = "-0001|share up to 10 by 2|share by 5 or 10|share by 3 or 4|share by 6 to 9|bigger shares (6 to 9 each)",
    [S_MDWORD] = "-0010|times stories, small|sharing stories|how-many-groups stories|bigger numbers|which operation fits?",
    [S_UNKNOWN] = "-0001|3 x ? = 12|? x 4 = 20|? / 3 = 5|24 / ? = 6|bigger numbers",
    [S_PROPS] = "-0000|turn the bed around|split off 5|split off 2|three factors, regrouped|any split",
    [S_DIVFACT] = "-000|divide by 2, 5, 10|divide by 3, 4|divide by 6, 7|divide by 8, 9",
    [S_FACTS] = "-0000|x0, x1, x2, x10|x5|x3, x4|x9, x6|x7, x8 and the rest",
    [S_TWOSTEP] = "-0001|add, then add more|groups, then add|groups, then take away|share, then add|bigger numbers",
    [S_PATTERN] = "-0011|steps of 2, 5, 10|steps of 3, 4|steps of 6 to 9|odd or even sums|odd or even rows",
    [S_TIMES_AS] = "-101|times as many, small|the other way round|bigger numbers|how many times as many?",
    [S_REMWORD] = "-001|how many left over|how many full groups|how many needed (one more)|bigger numbers",
    [S_FACTORS] = "-0110|factor pairs up to 24|up to 50|which pile packs (multiples)|one long row only?|up to 100",
    [S_RULE] = "-000|next: add the same|next: double|the 6th one|what does it add?",
    [S_PV3] = "-0122|tens and ones|hundreds, tens, ones|a zero inside (506)|what a digit is worth|glue the parts (500 + 6)",
    [S_CMP3] = "-000|different hundreds|same hundreds|same hundreds and tens|equal, or zeros inside",
    [S_ROUND10] = "-012|2-digit to the nearest 10|3-digit to 10|to the nearest 100|halfway, and 95",
    [S_ADDSUB] = ("-001102|1-digit, nothing to carry|2-digit and 1-digit|2-digit and 2-digit|carry in the ones|"
                  "two carries|3-digit numbers|zeros in the middle"),
    [S_MUL10] = "-000|x 10|2 to 5 x tens|6 to 9 x tens|a zero inside (5 x 80)",
    [S_PVBIG] = "-010|4-digit places|5-digit places|10 times as much|6-digit places",
    [S_CMPBIG] = "-011|4-digit: glue or compare|same thousands|5-digit|6-digit with zeros",
    [S_ROUNDBIG] = "-101|4-digit to 100|4-digit to 1000|5-digit to 1000|to 10000",
    [S_ADDSUBBIG] = "-1120|4-digit, nothing to carry|one carry or trade|several|zeros in the middle|5-digit numbers",
    [S_MULTI] = "-1000|2-digit x 1, nothing to carry|carrying|3-digit x 1|4-digit x 1|2-digit x 2-digit",
    [S_DIVIDE] = "-1200|2-digit, each digit shares|trade a ten|with a leftover|3-digit|4-digit",
    [S_UNITFRAC] = "-0102|halves and quarters|thirds and sixths|more than one slice|eighths|on a bar",
    [S_FRACLINE] = "-001|halves and quarters|thirds and sixths|eighths|past 1",
    [S_EQUIV] = "-000|1/2 = ?/4|twice as many slices|three times as many|the missing bottom",
    [S_WHOLES] = "-000|n/n is one whole|3 = 3/1|12/4 = ?|3 = ?/4",
    [S_CMPFRAC] = "-001|same bottom number|one slice each (1/3, 1/4)|same top number|tenths and twelfths",
    [S_PARTITION] = "-010|halves and quarters|thirds and sixths|more than one slice|eighths",
    [S_EQUIVMUL] = "-0011|x2 with halves and quarters|x3|x4 and x5|the missing bottom|make it simpler",
    [S_CMPDIFF] = "-000|against one half|same top number|one bottom fits the other|any two",
    [S_ADDFRAC] = "-001|add, up to a whole|take away|more than a whole|tenths and twelfths",
    [S_MIXED] = "-010|1 and a part to slices|slices to 1 and a part|2 or 3 wholes|add a part",
    [S_FRACWHOLE] = "-012|a single slice, a few times|more than one slice|4 or 5 times|more than a whole",
    [S_TENTHS] = "-00|tenths as hundredths|tenths and hundredths|the other way round",
    [S_DECIMAL] = "-0112|tenths, pictured|hundredths, pictured|on a number line|just the numbers|a zero first (0.07)",
    [S_CMPDEC] = "-012|tenths, pictured|0.5 or 0.49?|just the numbers|0.5 and 0.50",
    [S_TIME5] = "-000|o'clock and half past|quarter past and to|5-minute times|:55 and :05",
    [S_TIME1] = "-000|a few minutes past|any minute, first half|any minute|just before the hour",
    [S_ELAPSED] = "-0100|whole hours later|minutes later, same hour|how long?|across the hour|over an hour",
    [S_MASSVOL] = "-000|about how heavy?|add and take away|times|share",
    [S_CONVERT] = "-001|m to cm, hours to minutes|kg to g, l to ml, km to m|lb to oz|two units at once",
    [S_MEASWORD] = "-001|add lengths|take away|times|m and cm together",
    [S_COINS] = "-000|dimes and pennies|with nickels|with quarters|with dollars",
    [S_CHANGE] = "-000|from $1, 5-cent prices|from $1, any price|from $5|from $10",
    [S_SHOPWORD] = "-00|one step|groups, then take away|change from $1",
    [S_MONEYDEC] = "-011|$0.45 in cents|cents on the price tag|under 10 cents|over a dollar",
    [S_AREACOUNT] = "-01|count a small rug|bigger rugs|L-shaped rugs",
    [S_AREAMUL] = "-01|small rugs|up to 9 by 9|one side missing",
    [S_AREASPLIT] = "-00|add the two parts|the missing part|split off the tens",
    [S_AREAL] = "-00|on squared paper|sides labelled|the take-away way",
    [S_PERIM] = "-01|small frames|bigger frames|one side missing",
    [S_SHAPES] = "-000|square, pentagon, hexagon|rectangles|rhombus and kite|trapezoid, parallelogram",
    [S_FORMULA] = "-00|area by rule|perimeter by rule|a missing side",
    [S_ANGLES] = "-0000|quarter and half turns|three-quarter and full turns|kinds of corner|tens of degrees|any degrees",
    [S_PROTRACTOR] = "-000|0 to 90 by tens|0 to 180 by tens|by fives|the other scale",
    [S_ANGLEADD] = "-000|two tens together|a right angle's partner|a straight angle's partner|by fives",
    [S_LINES] = "-00|segment or line|ray|parallel, perpendicular",
    [S_CLASSIFY] = "-00|a square corner or not|sharp or wide corners|parallel sides",
    [S_SYMMETRY] = "-000|fold: yes or no|diagonal folds|count: rectangle|count: square",
    [S_GRAPH1] = "-00|read one bar|two together|how many more",
    [S_SCALED] = "-100|key of 2: read|key of 2: compare|key of 5|key of 10",
    [S_RULER] = "-00|whole inches|halves|quarters",
    [S_LINEPLOT] = "-00|count the marks|how many more|the total length",
    [S_TABLE] = "-001|read a row|how many more|together|bigger numbers",
    [S_MULBIG] = "-000|2-digit x 2-digit, small|2-digit x 2-digit|3-digit x 2-digit|4-digit x 2-digit",
    [S_FRUNLIKE] = "-000|one bottom fits the other|more than a whole|take away|any two bottoms",
    [S_DEC1000] = "-00|write thousandths|compare|in parts",
    [S_VOLUME] = "-00|count the cubes|length x width x height|two boxes",
    [S_COORD] = "-00|along the bottom|a small map|the whole map",
    [S_POW10] = "-0000|whole x 10|tenths x 10|x 100 and x 1000|smaller by 10, 100|tens multiplied",
    [S_ROUNDDEC] = "-00|to a whole|to a tenth|to a hundredth",
    [S_DECOPS] = "-0000|tenths + tenths|times a whole|hundredths and tenths|take away|share",
    [S_DIV2] = "-0000|by 10, 20, 30|by 11 to 15|3-digit|bigger divisors|4-digit",
    [S_ORDEROPS] = "-000|a x (b + c)|(a + b) x c|then take away|two brackets",
    [S_EXPR] = "-00|words to spells|other orders|how many times as big?",
    [S_TWOPAT] = "-00|two patterns, small|bigger|how the two compare",
    [S_FRACWORD] = "-00|halves and quarters|thirds and sixths|any two bottoms",
    [S_FRACDIV] = "-00|one pizza shared|fewer pizzas than friends|more pizzas than friends",
    [S_FRACMUL] = "-00|half of a half|a slice of a slice|more slices of slices",
    [S_SCALING] = "-000|x2 or x1/2|x5/4|x3/4|x4/4",
    [S_UNITDIV] = "-00|half cups|any whole by a slice|a slice shared",
    [S_CONVERT5] = "-000|whole m to cm|cm to m (2.45 m)|h and min to min|decimals to small units",
    [S_LINEPLOT5] = "-0|all of it together|poured fairly",
    [S_SHAPES5] = "-00|always-true riddles|not always|more families",
};

int mq_rungs(int skill)
{
    if (skill < 0 || skill >= MQ_NSKILL)
        return 1;
    int n = 0;
    while (ladder[skill][n] && ladder[skill][n] != '|')
        n++;
    return imax(1, imin(n, MQ_MAXRUNG));
}

int mq_rung_dim(int skill, int r)
{
    if (skill < 0 || skill >= MQ_NSKILL || r <= 0 || r >= mq_rungs(skill))
        return -1;
    char c = ladder[skill][r];
    return c >= '0' && c <= '3' ? c - '0' : -1;
}

int mq_rung_max(int skill, int r, int dim)
{
    int lv = 0;
    for (int k = 1; k <= r && k < mq_rungs(skill); k++)
        lv += mq_rung_dim(skill, k) == dim;
    return lv;
}

int mq_rung_name(int skill, int r, char *out, int n)
{
    out[0] = 0;
    if (skill < 0 || skill >= MQ_NSKILL || n < 1)
        return 0;
    const char *s = ladder[skill];
    while (*s && *s != '|')
        s++;
    for (int k = 0; *s && k <= r; k++) {
        s++; /* the '|' */
        int i = 0;
        while (s[i] && s[i] != '|') {
            if (k == r && i < n - 1)
                out[i] = s[i];
            i++;
        }
        if (k == r) {
            out[imin(i, n - 1)] = 0;
            return imin(i, n - 1);
        }
        s += i;
    }
    return 0;
}

int mq_rung_band(int skill, int r)
{
    int n = mq_rungs(skill);
    if (n <= 1)
        return 0;
    r = imax(0, imin(r, n - 1));
    return r * 4 / n; /* 0..3 */
}

int mq_rung_of_band(int skill, int band)
{
    int n = mq_rungs(skill);
    for (int r = 0; r < n; r++)
        if (mq_rung_band(skill, r) >= band)
            return r;
    return n - 1;
}

int mq_rung_cx(const mq_prob_t *p, int dim) { return dim < 0 || dim > 3 ? 0 : (p->cx >> (4 * dim)) & 15; }

/* ------------------------------------------------------------------ the rung controller */
static int bits(unsigned v)
{
    int n = 0;
    for (; v; v >>= 1)
        n += v & 1;
    return n;
}

/* A first answer of `skill` on a problem of rung `rung` (mq_coach_answer): the rung's own window of
 * answers (only those on her rung: an easier order after a miss is no evidence either way) decides. */
void mq_rung_answer(mq_t *q, int skill, int rung, bool right, bool quick)
{
    if (skill < 0 || skill >= MQ_NSKILL)
        return;
    mq_skrec_t *s = &mq_prof(q)->sk[skill];
    mq_pace_t *pc = mq_pace(q);
    int top = mq_rungs(skill) - 1, cur = imin(s->diff, top);
    if (rung != cur) /* (an easier order after a miss, a review at another rung: no evidence for this one) */
        return;
    uint16_t r = pc->rung_win[skill] ? pc->rung_win[skill] : 1;
    if (r >= 0x100) /* the last 8 */
        r = (uint16_t)(0x80 | (r & 0x7F));
    r = (uint16_t)(r << 1 | (right ? 1 : 0));
    pc->rung_win[skill] = r;
    int n = 0;
    while (n < 15 && (r >> n) > 1)
        n++;
    unsigned win = n ? r & ((1u << n) - 1) : 0;
    int ok = bits(win);
    /* solid: at least five answers here, the last four right, 90 % or more (tuned with the test's
     * simulated players to settle near 85 % right); after a step down from the rung above, the next
     * try there needs four more; every answer of the skill so far right and quick: two will do on the
     * first rungs (she already knew them) */
    bool clean = s->tries == s->right && quick;
    bool fell = pc->rung_fell[skill] == cur + 1;
    int need = clean && cur < 3 && !fell ? 2 : 4 + (fell ? 4 : 0), least = need > 5 ? need : clean && cur < 3 ? 2 : 5;
    unsigned mask = (1u << need) - 1;
    /* her first sessions: the lower half of every ladder (a slow ramp; the strand controller and the
     * placement do the rest) */
    int cap = mq_prof(q)->problems < 60 ? imax(0, mq_rung_of_band(skill, 2) - 1) : top;
    if (right && cur < imin(top, cap) && n >= least && (win & mask) == mask && ok * 100 >= n * 90) {
        s->diff = (uint8_t)(cur + 1);
        pc->rung_win[skill] = 1;
        if (pc->rung_top[skill] < s->diff)
            pc->rung_top[skill] = s->diff;
        if (pc->rung_fell[skill] <= s->diff)
            pc->rung_fell[skill] = 0;
    } else if (!right && cur > 0 && ((n >= 2 && (win & 3) == 0) || (n >= 5 && ok * 100 < n * 80))) {
        s->diff = (uint8_t)(cur - 1); /* struggling here: a rung down */
        pc->rung_fell[skill] = (uint8_t)cur;
        pc->rung_win[skill] = 1;
    }
    q->dirty = true;
}

/* A save from before the ladders: its difficulty 0..3 onto the ladder (the first rung of that band) */
void mq_rung_migrate(mq_t *q)
{
    mq_pace_t *pc = mq_pace(q);
    if (pc->rung_ver)
        return;
    mq_profile_t *p = mq_prof(q);
    for (int s = 0; s < MQ_NSKILL; s++) {
        int d = p->sk[s].diff > 3 ? 3 : p->sk[s].diff;
        p->sk[s].diff = (uint8_t)mq_rung_of_band(s, d);
        if (pc->rung_top[s] < p->sk[s].diff)
            pc->rung_top[s] = p->sk[s].diff;
    }
    pc->rung_ver = 1;
    q->dirty = true;
}

/* `mq rung`: every skill's rung; `mq rung S`: its ladder; `mq rung S R`: set it (tests) */
int mq_rung_console(mq_t *q, int argc, char **argv)
{
    const modx_api_t *api = q->api;
    long s = -1, r = -1;
    if (argc > 2)
        api->con_parse_int(argv[2], &s);
    if (argc > 3)
        api->con_parse_int(argv[3], &r);
    mq_profile_t *p = mq_prof(q);
    char nm[40];
    if (s >= 0 && s < MQ_NSKILL) {
        if (r >= 0) {
            p->sk[s].diff = (uint8_t)imin((int)r, mq_rungs((int)s) - 1);
            mq_pace(q)->rung_win[s] = 1;
            q->dirty = true;
        }
        for (int k = 0; k < mq_rungs((int)s); k++) {
            mq_rung_name((int)s, k, nm, sizeof nm);
            api->con_printf("%s %d: %s (dim %d)\n", k == p->sk[s].diff ? "*" : " ", k, nm, mq_rung_dim((int)s, k));
        }
        return 0;
    }
    for (int k = 0; k < MQ_NSKILL; k++)
        if (p->sk[k].tries || p->sk[k].diff) {
            mq_rung_name(k, p->sk[k].diff, nm, sizeof nm);
            api->con_printf("%d %s: rung %d of %d (%s)\n", k, mq_skill[k].name, p->sk[k].diff, mq_rungs(k), nm);
        }
    return 0;
}
