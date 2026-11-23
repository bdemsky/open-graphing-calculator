/*
 * Math Quest: the ways to work it -- school's Common Core strategies for grades 3 to 5 as hands-on models,
 * each one a friend's trick ("Pip's jumping trick" is the open number line), so the game works sums the
 * way her class does (docs/MATHQUEST.md "Ways to work it").
 *
 * Every way (STG_*) has
 *   - a model: a picture she works with her hands, built from the order's numbers -- hops dragged onto a
 *     path, blocks dragged into their houses, bags tapped full, a garden split or turned with a slider, a
 *     floor cut at the tens, handfuls peeled off a pile, a ribbon labelled, a cake strip cut and frosted,
 *     coins sorted, X marks dropped on a line. A model is a plan of steps (mq_sstep_t: a hop, a drag, a tap,
 *     a slide or a pick of an answer), each told by the friend while it is new; a wrong move is refused
 *     with its own why (the first one counts the order as missed first time, as the notebook does);
 *   - a teaching segment: the friend who owns the trick shows it on an example of their own, step by step,
 *     the ghost hand doing each gesture, every line said aloud (mq_speak); "try one" skips straight to her
 *     own;
 *   - orders: "use this way" orders of its skill (mq_strat_order: the first time a way is open and an order
 *     of its skill comes, it is introduced; later now and then), and its pictures for the helper friend and
 *     the lightbulb, which pick the way that fits the order best (mq_strat_fit) and sometimes show a second.
 * When it comes is school's: the pacing map (the grade and the week of its year the grown-ups set, else
 * where she plays) says which ways her class has met, and the standard algorithms come only at their
 * Common Core grade (adding and taking away in columns at 4, multiplying at 5; no long division).
 * The grown-ups' Skills tab lists the ways with their Common Core names, seen and used.
 *
 * Drawing: the mq_vis primitives, every piece of a model inside an hr bracket with its own name
 * (model/...: the Mac page's vector twins, tools/mqvec/models.py); the numbers are drawn after the
 * bracket. Image rules as everywhere: no RW but mq_t, no library calls, tables of characters and numbers.
 */
#include "mq.h"

enum { HK_UP = 12, HK_LEFT = 13, HK_RIGHT = 14, HK_DOWN = 15, HK_DEL = 8, HK_ENTER = 49 };
enum { TV_DOWN = 1, TV_DRAG, TV_UP, TV_TAP };
#define FB (MQ_Y0 + MQ_H)
#define MY0 (MQ_Y0 + 60)  /* the model's box */
#define MY1 (FB - 38)
#define MX0 8
#define MX1 312
#define BY (FB - 34)      /* the row of chips, answers and buttons */
#define BEAT_MS 2600u     /* a step the friend shows */
#define HAND_MS 1100u     /* ... the ghost hand's gesture in it */
#define DONE_MS 1300u

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int iabs(int a) { return a < 0 ? -a : a; }

/* ------------------------------------------------------------------ the ways */
enum { MD_JUMPS, MD_PLACES, MD_GIVE, MD_GROUPS, MD_ARRAY, MD_AREA, MD_PEEL, MD_FRIENDLY, MD_TAPE, MD_STRIP, MD_MONEY,
       MD_PLOT, MD_COLS, MD_COUNT };
/* variants: the path's (MD_JUMPS), the houses' (MD_PLACES), the bags' (MD_GROUPS), the garden's (MD_ARRAY), the
 * floor's (MD_AREA), the ribbon's (MD_TAPE), the strip's (MD_STRIP), the columns' (MD_COLS) */
enum { V_ADD, V_MAKE, V_UP, V_TIME, V_FRAC, V_BENCH };
enum { V_PSUM, V_EXP, V_CHART, V_BLOCKS };
enum { V_EQ, V_DEAL, V_BAG, V_IDZ };
enum { V_BUILD, V_TURN, V_ASSOC, V_SPLIT, V_DH, V_KNOWN, V_GROW };
enum { V_AM, V_PP, V_DIV };
enum { V_PPW, V_CMP, V_EQG };
enum { V_STRIP, V_CIRC, V_UNITS, V_EQUIV };
enum { V_CADD, V_CMUL };
/* how an answer reads */
enum { FM_NUM, FM_FRAC, FM_MIN, FM_MONEY, FM_DEC, FM_SAME, FM_NEAR, FM_TIME };
/* a slider's look */
enum { SL_ROWS = 1, SL_TURN, SL_MERGE, SL_CUT, SL_VCUT, SL_PIECES, SL_RECUT, SL_BREAK };

typedef struct {
    char cc[36];       /* the Common Core name (the grown-ups' screen only) */
    char nm[22];       /* the trick's name in the story, after "Pip's " */
    char std[9];       /* the standard it serves */
    uint8_t who, fam, model, var, grade, week, skill, algo;
    int16_t ex[4];     /* the friend's own example */
    char intro[2][72]; /* the friend's opening lines ('@': the friend's name) */
} strat_t;

static const strat_t tab[STG_COUNT] = {
    [STG_JUMPS] = {"Open number line (tens, ones)", "jumping trick", "3.NBT.2", 0, SF_ADD, MD_JUMPS, V_ADD, 3, 10, S_ADDSUB, 0,
                   {38, 25, 0, 0}, {"@ hops along a path: big hops of ten first,", "then little hops of one. Where does @ land?"}},
    [STG_PSUMS] = {"Partial sums", "sorting trick", "3.NBT.2", 1, SF_ADD, MD_PLACES, V_PSUM, 3, 11, S_ADDSUB, 0, {246, 132, 0, 0},
                   {"@ sorts the blocks into houses: hundreds, tens, ones.", "Add up each house, then put them all together!"}},
    [STG_EXPAND] = {"Expanded form", "stretch-out trick", "2.NBT.3", 1, SF_ADD, MD_PLACES, V_EXP, 3, 9, S_PV3, 0, {352, 1, 0, 0},
                    {"@ stretches a number out: every digit gets its own card.", "Each card shows what its digit is worth!"}},
    [STG_MAKE10] = {"Making ten or a hundred", "make-a-ten trick", "3.NBT.2", 2, SF_ADD, MD_JUMPS, V_MAKE, 3, 10, S_ADDSUB, 0,
                    {97, 25, 0, 0}, {"@ loves round numbers: hop to the next ten or hundred first,", "then hop the rest."}},
    [STG_COMP] = {"Compensation (299 + 45 = 300 + 44)", "round-and-fix trick", "3.NBT.2", 4, SF_ADD, MD_GIVE, 0, 3, 11, S_ADDSUB, 0,
                  {299, 45, 0, 0}, {"@ moves one from the small jar to the big one.", "299 + 45 is just as much as 300 + 44. So easy!"}},
    [STG_COUNTUP] = {"Counting up to subtract", "count-up trick", "3.NBT.2", 3, SF_ADD, MD_JUMPS, V_UP, 3, 11, S_ADDSUB, 0,
                     {62, 37, 0, 0}, {"To take 37 from 62, @ starts at 37 and hops up to 62.", "How far @ hops: that's how many are left!"}},
    [STG_ALGO_AS] = {"Standard algorithm (add, subtract)", "column trick", "4.NBT.4", 1, SF_ADD, MD_COLS, V_CADD, 4, 4,
                     S_ADDSUBBIG, 1, {476, 358, 0, 0}, {"@ writes the numbers in columns, ones under ones.", "Ones first, then tens, then hundreds: a ten hops over!"}},
    [STG_GROUPS] = {"Equal groups", "bags trick", "3.OA.1", 0, SF_MUL, MD_GROUPS, V_EQ, 3, 1, S_GROUPS, 0, {4, 6, 0, 0},
                    {"@ fills bags with the same number in each.", "Then count by the bags, not one by one!"}},
    [STG_ARRAY] = {"Arrays", "garden rows", "3.OA.1", 0, SF_MUL, MD_ARRAY, V_BUILD, 3, 2, S_GROUPS, 0, {3, 7, 0, 0},
                   {"@ plants flowers in rows, the same in every row.", "Count by the rows!"}},
    [STG_AREA] = {"Area model", "floor trick", "4.NBT.5", 5, SF_MUL, MD_AREA, V_AM, 4, 9, S_MULTI, 0, {23, 4, 0, 0},
                  {"@ draws the floor and cuts it at the tens.", "Two small floors are easy to count, then add them!"}},
    [STG_PPROD] = {"Partial products", "piece-by-piece trick", "4.NBT.5", 5, SF_MUL, MD_AREA, V_PP, 4, 10, S_MULTI, 0,
                   {34, 6, 0, 0}, {"@ times each piece on its own, one line each,", "then adds the lines together."}},
    [STG_DOUBLE] = {"Doubling and halving", "double-half trick", "3.OA.5", 0, SF_MUL, MD_ARRAY, V_DH, 3, 13, S_PROPS, 0,
                    {4, 16, 0, 0}, {"@ cuts the garden in half and puts one half under the other:", "twice the rows, half as long. Just as many flowers!"}},
    [STG_KNOWN] = {"Using known facts", "head-start trick", "3.OA.5", 0, SF_MUL, MD_ARRAY, V_KNOWN, 3, 12, S_GROUPS, 0,
                   {6, 7, 0, 0}, {"@ starts from one @ knows by heart: 5 rows of 7.", "Then one more row of 7!"}},
    [STG_ALGO_M] = {"Standard algorithm (multiply)", "column-times trick", "5.NBT.5", 8, SF_MUL, MD_COLS, V_CMUL, 5, 6, S_MULBIG,
                    1, {46, 23, 0, 0}, {"@ writes the numbers in columns and times them line by line:", "first the ones, then the tens. Then add the lines!"}},
    [STG_TURN] = {"Commutative property", "turn-around trick", "3.OA.5", 0, SF_PROP, MD_ARRAY, V_TURN, 3, 3, S_PROPS, 0,
                  {2, 4, 0, 0}, {"Here are 2 rows of 4 and 4 rows of 2.", "@ turns one around: does it cover the other?"}},
    [STG_REGROUP] = {"Associative property", "regroup trick", "3.OA.5", 0, SF_PROP, MD_ARRAY, V_ASSOC, 3, 14, S_PROPS, 0,
                     {4, 5, 3, 0}, {"Three gardens of 4 rows of 5. @ slides them together:", "now it's 4 rows of 15! Group them the easy way."}},
    [STG_SPLIT] = {"Distributive property", "break-apart trick", "3.OA.5", 0, SF_PROP, MD_ARRAY, V_SPLIT, 3, 4, S_PROPS, 0,
                   {7, 6, 5, 0}, {"7 rows of 6 is a lot to count. @ breaks the garden apart:", "7 rows of 5 and 7 rows of 1. Then add the two!"}},
    [STG_ONEZERO] = {"Identity and zero properties", "one-and-none trick", "3.OA.5", 0, SF_PROP, MD_GROUPS, V_IDZ, 3, 12, S_GROUPS, 0,
                     {1, 7, 0, 0}, {"One bag of 7 is just 7. And no bags at all?", "Nothing! Times 1 stays the same, times 0 is 0."}},
    [STG_FRIENDLY] = {"Friendly numbers (25 x 4 x 7)", "friendly-pair trick", "3.OA.5", 4, SF_PROP, MD_FRIENDLY, 0, 3, 15, S_PROPS,
                      0, {25, 7, 4, 0}, {"@ looks for two numbers that make a round one:", "25 and 4 make 100! Then 100 times 7 is easy."}},
    [STG_SHARE] = {"Equal sharing", "dealing trick", "3.OA.2", 2, SF_DIV, MD_GROUPS, V_DEAL, 3, 2, S_SHARE, 0, {3, 4, 0, 0},
                   {"@ deals the cookies like cards: one for you, one for you ...", "until the pile is gone. How many on each plate?"}},
    [STG_BAGS] = {"Equal grouping", "bagging trick", "3.OA.2", 0, SF_DIV, MD_GROUPS, V_BAG, 3, 2, S_SHARE, 0, {5, 4, 0, 0},
                  {"@ packs the seeds, 5 to a bag, until none are left.", "Then count the bags!"}},
    [STG_THINKX] = {"Think multiplication", "think-times trick", "3.OA.6", 0, SF_DIV, MD_ARRAY, V_GROW, 3, 5, S_DIVFACT, 0,
                    {6, 7, 0, 0}, {"42 in rows of 6? @ thinks: 6 times what makes 42?", "@ plants rows of 6 until there are 42."}},
    [STG_PEEL] = {"Partial quotients", "peel-off trick", "4.NBT.6", 1, SF_DIV, MD_PEEL, 0, 4, 13, S_DIVIDE, 0, {96, 4, 0, 0},
                  {"@ peels big handfuls of groups off the pile:", "10 groups at a time, then smaller ones, until it's gone."}},
    [STG_AREADIV] = {"Area model for division", "floor-share trick", "4.NBT.6", 5, SF_DIV, MD_AREA, V_DIV, 4, 13, S_DIVIDE, 0,
                     {96, 4, 0, 0}, {"A floor 4 squares tall has 96 squares. How long is it?", "@ lays it piece by piece: a big piece, then the rest."}},
    [STG_TAPE_PPW] = {"Tape diagram: part-part-whole", "ribbon trick", "3.OA.8", 6, SF_WORD, MD_TAPE, V_PPW, 3, 11, S_ADDSUB, 0,
                      {45, 72, 0, 0}, {"@ draws the story as a ribbon: all of it on top,", "the parts below. The part with the ? is the one to find!"}},
    [STG_TAPE_CMP] = {"Tape diagram: comparison", "two-ribbon trick", "3.OA.8", 6, SF_WORD, MD_TAPE, V_CMP, 3, 11, S_ADDSUB, 0,
                      {18, 7, 0, 0}, {"@ puts two ribbons side by side.", "The longer one sticks out by how many more!"}},
    [STG_TAPE_EQ] = {"Tape diagram: equal groups", "ribbon-pieces trick", "3.OA.3", 6, SF_WORD, MD_TAPE, V_EQG, 3, 3, S_MDWORD, 0,
                     {4, 6, 0, 0}, {"@ cuts the ribbon into equal pieces, one for each friend.", "Every piece is the same!"}},
    [STG_STRIP] = {"Fraction strips and bars", "cake-strip trick", "3.NF.1", 2, SF_FRAC, MD_STRIP, V_STRIP, 3, 22, S_UNITFRAC, 0,
                   {3, 4, 0, 0}, {"@ cuts a long cake into fair pieces, all one size.", "Pink pieces over all the pieces: that's its name!"}},
    [STG_PIE] = {"Area models (circles, rectangles)", "pie trick", "3.NF.1", 2, SF_FRAC, MD_STRIP, V_CIRC, 3, 22, S_UNITFRAC, 0,
                 {2, 6, 0, 0}, {"@ cuts a round pie into fair slices.", "Frost some slices pink and give the pink part its name!"}},
    [STG_FLINE] = {"Fractions on a number line", "hop-along trick", "3.NF.2", 2, SF_FRAC, MD_JUMPS, V_FRAC, 3, 24, S_FRACLINE, 0,
                   {3, 4, 0, 0}, {"From 0 to 1 the path is cut into fair steps.", "@ hops step by step: where does @ land?"}},
    [STG_BENCH] = {"Benchmark fractions (1/2, 1)", "halfway trick", "3.NF.3", 2, SF_FRAC, MD_JUMPS, V_BENCH, 3, 27, S_CMPFRAC, 0,
                   {3, 8, 0, 0}, {"Halfway from 0 to 1 is 1/2. @ hops and looks:", "is it nearer 0, nearer 1/2 or nearer 1?"}},
    [STG_UNITS] = {"Unit fractions (3/4 = 1/4+1/4+1/4)", "one-piece trick", "4.NF.3", 2, SF_FRAC, MD_STRIP, V_UNITS, 4, 23,
                   S_ADDFRAC, 0, {3, 4, 0, 0}, {"3/4 is 1/4, and 1/4, and 1/4 again.", "@ builds it one piece at a time!"}},
    [STG_EQUIV] = {"Equivalent fractions (models)", "cut-again trick", "3.NF.3", 2, SF_FRAC, MD_STRIP, V_EQUIV, 3, 25, S_EQUIV, 0,
                   {1, 2, 2, 0}, {"@ cuts every piece again: more pieces, the same cake.", "The pink part stays just as big!"}},
    [STG_PVCHART] = {"Place-value chart", "house of places", "2.NBT.1", 1, SF_PV, MD_PLACES, V_CHART, 3, 8, S_PV3, 0,
                     {306, 0, 0, 0}, {"Every digit has a house: hundreds, tens, ones.", "@ puts discs in each house to show the number."}},
    [STG_BLOCKS] = {"Base-ten blocks", "block trick", "2.NBT.1", 1, SF_PV, MD_PLACES, V_BLOCKS, 3, 0, S_PV3, 0, {2, 12, 5, 0},
                    {"A cube is 1, a rod is 10 cubes, a flat is 10 rods.", "@ glues 10 rods into a flat when there are too many!"}},
    [STG_MONEY] = {"Money model for decimals", "coin trick", "4.NF.6", 4, SF_PV, MD_MONEY, 0, 4, 31, S_MONEYDEC, 0, {247, 0, 0, 0},
                   {"A dime is one tenth of a dollar, a penny one hundredth.", "@ sorts the coins: dollars, dimes, pennies!"}},
    [STG_TIMELINE] = {"Number line for elapsed time", "time-line trick", "3.MD.1", 3, SF_MEAS, MD_JUMPS, V_TIME, 3, 7, S_ELAPSED, 0,
                      {165, 85, 0, 0}, {"@ draws the time as a path: hop to the next o'clock,", "then whole hours, then the minutes left."}},
    [STG_PLOT] = {"Line plots", "X-marks trick", "3.MD.4", 6, SF_MEAS, MD_PLOT, 0, 3, 30, S_RULER, 0, {6, 0, 2, 0},
                  {"@ measured some ribbons. Each one gets an X", "above its length on the line!"}},
};
_Static_assert(STG_COUNT <= MQ_NSTRAT_SAVE, "the saved records (strat_seen, strat_used) hold every way (code review 2, B5)");
static const char fam_nm[SF_COUNT][16] = {"Add, subtract", "Multiply", "Properties", "Divide", "Word problems", "Fractions",
                                         "Place value", "Measurement"};

const char *mq_strat_cc(int s) { return s >= 0 && s < STG_COUNT ? tab[s].cc : ""; }
const char *mq_strat_std(int s) { return s >= 0 && s < STG_COUNT ? tab[s].std : ""; }
const char *mq_strat_fam_name(int f) { return f >= 0 && f < SF_COUNT ? fam_nm[f] : ""; }
int mq_strat_family(int s) { return s >= 0 && s < STG_COUNT ? tab[s].fam : -1; }
int mq_strat_skill(int s) { return s >= 0 && s < STG_COUNT ? tab[s].skill : -1; }
int mq_strat_grade(int s, int *week)
{
    if (s < 0 || s >= STG_COUNT)
        return 0;
    if (week)
        *week = tab[s].week;
    return tab[s].grade;
}
int mq_strat_teach_n(int s) { return s >= 0 && s < STG_COUNT ? (tab[s].intro[1][0] ? 2 : 1) : 0; }

/* "Pip's jumping trick" (the friend's name as the theme pack has it) */
void mq_strat_name(mq_t *q, int s, char *out, int n)
{
    s = s >= 0 && s < STG_COUNT ? s : 0;
    q->api->snprintf(out, (size_t)n, "%s's %s", mq_cast_name(q, tab[s].who), tab[s].nm);
}
/* the line with '@' as the friend's name */
static void expand(mq_t *q, const char *in, int who, char *out, int n)
{
    const char *nm = mq_cast_name(q, who);
    int k = 0;
    for (int i = 0; in[i] && k < n - 1; i++)
        if (in[i] == '@')
            for (int j = 0; nm[j] && k < n - 1; j++)
                out[k++] = nm[j];
        else
            out[k++] = in[i];
    out[k] = 0;
}

/* ------------------------------------------------------------------ when: school's pacing */
static int my_grade(mq_t *q, int *week)
{
    int wk, g = mq_school(q, &wk);
    if (g) {
        *week = wk;
        return g;
    }
    /* school not said: where she plays (the starting grade, the grade she works at), all of its year */
    g = imax(mq_start_grade(q), mq_working_grade(mq_prof(q)));
    *week = MQ_SCHOOL_WEEKS;
    return g;
}
bool mq_strat_open(mq_t *q, int s)
{
    if (s < 0 || s >= STG_COUNT)
        return false;
    int wk, g = my_grade(q, &wk);
    const strat_t *t = &tab[s];
    if (t->algo) /* the standard algorithms only from their own grade (and week) on */
        return g > t->grade || (g == t->grade && wk >= t->week);
    return t->grade < g || (t->grade == g && t->week <= wk);
}

/* ------------------------------------------------------------------ the numbers */
static int R(mq_t *q, int lo, int hi) { return mq_rnd(q, lo, hi); }

static void order_numbers(mq_t *q, int s, int d, int32_t x[6]);
/* numbers for an order of way s at difficulty d (0 easy .. 3) */
void mq_strat_numbers(mq_t *q, int s, int d, int32_t x[6]) { order_numbers(q, s, d, x); }
static void order_numbers(mq_t *q, int s, int d, int32_t x[6])
{
    for (int i = 0; i < 6; i++)
        x[i] = 0;
    switch (s) {
    case STG_JUMPS:
        x[0] = d >= 2 ? R(q, 120, 680) : R(q, 21, 68), x[1] = d >= 2 ? R(q, 112, 299) : R(q, 12, 29);
        if (d >= 1 && R(q, 0, 1)) { /* taking away: hop back */
            x[2] = 1, x[0] += x[1] + R(q, 3, 20);
        }
        break;
    case STG_PSUMS: x[0] = d >= 1 ? R(q, 112, 589) : R(q, 21, 68), x[1] = d >= 1 ? R(q, 104, 389) : R(q, 12, 29); break;
    case STG_EXPAND: x[0] = R(q, 101, 989), x[1] = R(q, 0, 2); break;
    case STG_MAKE10:
        if (d >= 1)
            x[0] = R(q, 1, 8) * 100 + R(q, 91, 98), x[1] = R(q, 12, 48);
        else
            x[0] = R(q, 2, 8) * 10 + R(q, 7, 9), x[1] = R(q, 5, 9);
        break;
    case STG_COMP:
        x[0] = d >= 1 ? R(q, 1, 8) * 100 + R(q, 97, 99) : R(q, 2, 8) * 10 + 9, x[1] = R(q, 12, 68);
        break;
    case STG_COUNTUP: x[1] = R(q, 22, 68), x[0] = x[1] + R(q, 8, 29); break;
    case STG_ALGO_AS: x[0] = R(q, 1203, 7489), x[1] = R(q, 1102, 2399), x[2] = R(q, 0, 1); break;
    case STG_GROUPS: x[0] = R(q, 3, 6), x[1] = R(q, 2, 9); break;
    case STG_ARRAY: x[0] = R(q, 2, 6), x[1] = R(q, 3, 9); break;
    case STG_AREA: x[0] = R(q, 13, 48), x[1] = d >= 2 ? R(q, 12, 19) : R(q, 3, 8); break;
    case STG_PPROD: x[0] = d >= 2 ? R(q, 112, 389) : R(q, 23, 79), x[1] = d >= 3 ? R(q, 12, 19) : R(q, 3, 8); break;
    case STG_DOUBLE: x[0] = R(q, 2, 4), x[1] = 2 * R(q, 4, 8); break;
    case STG_KNOWN: x[0] = R(q, 0, 1) ? 6 : 9, x[1] = R(q, 3, 9); break;
    case STG_ALGO_M: x[0] = R(q, 21, 89), x[1] = R(q, 12, 49); break;
    case STG_TURN:
        x[0] = R(q, 2, 5), x[1] = R(q, 3, 8), x[2] = R(q, 0, 1);
        if (x[0] == x[1])
            x[1]++;
        break;
    case STG_REGROUP: x[0] = R(q, 2, 4), x[1] = R(q, 2, 5), x[2] = R(q, 2, 3); break;
    case STG_SPLIT: x[0] = R(q, 3, 8), x[1] = R(q, 6, 9), x[2] = 5; break;
    case STG_ONEZERO: {
        int k = R(q, 0, 3), n = R(q, 3, 9);
        x[0] = k == 0 ? 1 : k == 1 ? 0 : n, x[1] = k == 2 ? 1 : k == 3 ? 0 : n;
        break;
    }
    case STG_FRIENDLY: {
        static const uint8_t pr[3][2] = {{5, 2}, {25, 4}, {50, 2}};
        int k = d >= 1 ? R(q, 0, 2) : 0, m = R(q, 3, 9);
        x[0] = pr[k][0], x[1] = m, x[2] = pr[k][1];
        if (R(q, 0, 1)) /* (the pair not always first and last) */
            x[1] = pr[k][1], x[2] = m;
        break;
    }
    case STG_SHARE: x[0] = R(q, 2, 5), x[1] = R(q, 2, 7); break;
    case STG_BAGS: x[0] = R(q, 2, 6), x[1] = R(q, 2, 6); break;
    case STG_THINKX: x[0] = R(q, 3, 9), x[1] = R(q, 3, 9); break;
    case STG_PEEL: case STG_AREADIV: x[1] = R(q, 3, 8), x[0] = x[1] * (d >= 2 ? R(q, 21, 99) : R(q, 12, 29)); break;
    case STG_TAPE_PPW: x[1] = R(q, 40, 95), x[0] = R(q, 12, x[1] - 11); break;
    case STG_TAPE_CMP:
        x[2] = mq_strat_open(q, STG_AREA) && R(q, 0, 2) == 0 ? 1 : 0; /* grade 4: "times as many" too */
        x[0] = x[2] ? R(q, 3, 9) : R(q, 12, 45), x[1] = x[2] ? R(q, 2, 5) : R(q, 5, 19);
        break;
    case STG_TAPE_EQ: x[0] = R(q, 3, 6), x[1] = R(q, 3, 9), x[2] = R(q, 0, 1); break;
    case STG_STRIP: case STG_PIE: x[1] = R(q, 3, 8), x[0] = R(q, 1, x[1] - 1), x[2] = R(q, 0, 1); break;
    case STG_FLINE: x[1] = R(q, 3, 8), x[0] = R(q, 1, x[1] - 1); break;
    case STG_BENCH:
        for (int t = 0; t < 30; t++) { /* nearest one of 0, 1/2, 1 (never in between two) */
            x[1] = R(q, 3, 10), x[0] = R(q, 1, x[1] - 1);
            int n2 = 4 * (int)x[0], d4 = 4 * (int)x[1]; /* x 4 d: 0 .. 4 d; 1/2 at 2 d */
            if (n2 != d4 / 4 && n2 != 3 * d4 / 4 && 2 * x[0] != x[1])
                break;
        }
        break;
    case STG_UNITS: x[1] = R(q, 3, 8), x[0] = R(q, 2, x[1] - 1); break;
    case STG_EQUIV: {
        static const uint8_t fr[5][2] = {{1, 2}, {1, 3}, {2, 3}, {1, 4}, {3, 4}};
        int k = R(q, 0, 4);
        x[0] = fr[k][0], x[1] = fr[k][1], x[2] = R(q, 2, fr[k][1] == 4 ? 2 : 3);
        break;
    }
    case STG_PVCHART:
        x[1] = mq_strat_open(q, STG_MONEY) && R(q, 0, 1); /* grade 4 at decimals: ones, tenths, hundredths */
        x[0] = x[1] ? R(q, 101, 999) : R(q, 101, 989);
        if (!x[1] && R(q, 0, 2) == 0)
            x[0] = x[0] / 100 * 100 + x[0] % 10; /* a zero inside */
        break;
    case STG_BLOCKS: x[0] = R(q, 1, 4), x[1] = d >= 1 ? R(q, 10, 13) : R(q, 1, 8), x[2] = R(q, 1, 8); break;
    case STG_MONEY: x[0] = R(q, 1, 4) * 100 + R(q, 1, 8) * 10 + R(q, 1, 9); break;
    case STG_TIMELINE: x[0] = R(q, 1, 9) * 60 + R(q, 7, 11) * 5, x[1] = 60 + R(q, 2, 11) * 5; break;
    case STG_PLOT: {
        int m = 6, v = 0;
        for (int i = 0; i < m; i++)
            v |= R(q, 1, 4) << (4 * i);
        x[0] = m, x[1] = v, x[2] = (v >> (4 * R(q, 0, m - 1))) & 15;
        break;
    }
    default: break;
    }
}
static void example_numbers(int s, int32_t x[6])
{
    for (int i = 0; i < 6; i++)
        x[i] = i < 4 ? tab[s].ex[i] : 0;
    if (s == STG_PLOT) /* 1/4, 1/2, 1/2, 3/4, 1/2, 1 (in quarters) */
        x[1] = 1 | 2 << 4 | 2 << 8 | 3 << 12 | 2 << 16 | 4 << 20;
}

/* ------------------------------------------------------------------ formatting */
static int fmt(mq_t *q, int f, int32_t v, char *o, int n)
{
    const modx_api_t *api = q->api;
    static const char same[2][12] = {"the same", "different"}, near[3][12] = {"nearer 0", "nearer 1/2", "nearer 1"};
    switch (f) {
    case FM_FRAC: return api->snprintf(o, (size_t)n, "%ld/%ld", (long)(v / 1000), (long)(v % 1000));
    case FM_MIN: return api->snprintf(o, (size_t)n, "%ld min", (long)v);
    case FM_MONEY: return api->snprintf(o, (size_t)n, "$%ld.%02ld", (long)(v / 100), (long)(v % 100));
    case FM_DEC: return api->snprintf(o, (size_t)n, "%ld.%02ld", (long)(v / 1000), (long)(v % 1000 / 10));
    case FM_SAME: return api->snprintf(o, (size_t)n, "%s", same[v & 1]);
    case FM_NEAR: return api->snprintf(o, (size_t)n, "%s", near[v < 0 ? 0 : v > 2 ? 2 : v]);
    case FM_TIME: return api->snprintf(o, (size_t)n, "%ld:%02ld", (long)((v / 60 + 11) % 12 + 1), (long)(v % 60));
    default: return api->snprintf(o, (size_t)n, "%ld", (long)v);
    }
}

/* ------------------------------------------------------------------ the plans */
#define SM (&q->strat)
#define SAY(st, ...) q->api->snprintf((st)->say, sizeof (st)->say, __VA_ARGS__)

static mq_sstep_t *step(mq_t *q, int g, int32_t want)
{
    mq_strat_t *m = SM;
    if (m->n >= SS_MAX)
        return &m->st[SS_MAX - 1];
    mq_sstep_t *s = &m->st[m->n++];
    uint8_t *b = (uint8_t *)s;
    for (size_t i = 0; i < sizeof *s; i++)
        b[i] = 0;
    s->g = (uint8_t)g, s->want = want;
    return s;
}
static mq_sstep_t *hop_step(mq_t *q, int32_t from, int32_t to, int32_t c0, int32_t c1, int32_t c2)
{
    mq_sstep_t *s = step(q, GE_JUMP, to);
    s->a = from;
    int32_t c[3] = {c0, c1, c2};
    for (int i = 0; i < 3; i++)
        if (c[i])
            s->ch[s->nch++] = c[i];
    return s;
}
static mq_sstep_t *slide_step(mq_t *q, int look, int lo, int hi, int32_t want)
{
    mq_sstep_t *s = step(q, GE_SLIDE, want);
    s->slide = (uint8_t)look, s->lo = (int16_t)lo, s->hi = (int16_t)hi;
    return s;
}
/* a pick of the right answer among it and two near ones (deterministic order; distinct) */
static mq_sstep_t *pick_step(mq_t *q, int f, int32_t right, int32_t w1, int32_t w2)
{
    mq_sstep_t *s = step(q, GE_PICK, right);
    s->fmt = (uint8_t)f;
    int32_t v[3] = {right, w1, w2};
    if (f == FM_SAME) {
        s->nch = 2, s->ch[0] = 0, s->ch[1] = 1;
        return s;
    }
    if (f == FM_NEAR) {
        s->nch = 3, s->ch[0] = 0, s->ch[1] = 1, s->ch[2] = 2;
        return s;
    }
    for (int i = 1; i < 3; i++) { /* distinct, not negative */
        int k = 0;
        while (k++ < 20 && (v[i] < 0 || v[i] == v[0] || (i == 2 && v[2] == v[1])))
            v[i] = f == FM_FRAC ? v[i] + 1000 : v[i] + (i == 1 ? 1 : 2);
    }
    int r = (int)((right * 7 + q->strat.n * 3) % 3), o[3];
    if (r < 0)
        r = -r;
    o[r] = 0, o[(r + 1) % 3] = 1, o[(r + 2) % 3] = 2;
    s->nch = 3;
    for (int i = 0; i < 3; i++)
        s->ch[i] = v[o[i]];
    return s;
}
static int digit(int32_t n, int place) /* place 0 ones, 1 tens, 2 hundreds */
{
    for (int i = 0; i < place; i++)
        n /= 10;
    return (int)(n % 10);
}
static int32_t pow10(int k)
{
    int32_t v = 1;
    while (k-- > 0)
        v *= 10;
    return v;
}
static int ndig(int32_t n)
{
    int k = 1;
    while (n >= 10)
        n /= 10, k++;
    return k;
}
static const char house_nm[3][3][11] = {{"ones", "tens", "hundreds"}, {"hundredths", "tenths", "ones"},
                                        {"pennies", "dimes", "dollars"}};
static const char block_nm[3][6] = {"cube", "rod", "flat"};

/* the plan of way s for numbers x: the steps, the friend's words for each */
static void plan(mq_t *q, int s, const int32_t *x)
{
    mq_strat_t *m = SM;
    const modx_api_t *api = q->api;
    m->n = 0, m->cur = 0, m->cnt = 0, m->nhop = 0, m->pos = 0, m->val = 0, m->picked = -1, m->alt = 0;
    for (int i = 0; i < 10; i++)
        m->put[i] = 0;
    for (int i = 0; i < 16; i++)
        m->taps[i] = 0;
    for (int i = 0; i < 6; i++)
        m->x[i] = x[i];
    const strat_t *t = &tab[s];
    const char *nm = mq_cast_name(q, t->who);
    mq_sstep_t *st;
    int32_t a = x[0], b = x[1], c = x[2];
    char l1[24], l2[24];
    switch (t->model) {
    case MD_JUMPS:
        if (t->var == V_ADD) {
            int sg = c ? -1 : 1;
            int32_t big = b - b % 10, end = a + sg * b;
            m->pos = a;
            if (big) {
                st = hop_step(q, a, a + sg * big, b >= 100 ? sg * 100 : 0, sg * 10, 0);
                SAY(st, c ? "Big hops back first: drag a hop of ten onto the path!" : "Big hops first: drag a hop of ten onto the path!");
            }
            if (b % 10) {
                st = hop_step(q, a + sg * big, end, sg, 0, 0);
                SAY(st, "Now little hops of one, %ld of them.", (long)(b % 10));
            }
            st = pick_step(q, FM_NUM, end, end + 10, end - 1);
            SAY(st, "%ld %s %ld lands on ...?", (long)a, c ? "\xE2\x88\x92" : "+", (long)b);
        } else if (t->var == V_MAKE) {
            int32_t f = a % 100 >= 90 ? (a / 100 + 1) * 100 : (a / 10 + 1) * 10, r = f - a;
            m->pos = a;
            st = slide_step(q, SL_BREAK, 0, (int)b, r);
            SAY(st, "Break %ld apart: drag the cut so %ld can reach %ld.", (long)b, (long)a, (long)f);
            st = hop_step(q, a, f, r, 0, 0);
            SAY(st, "Hop the %ld piece first: to %ld, a round number!", (long)r, (long)f);
            st = hop_step(q, f, a + b, b - r, 0, 0);
            SAY(st, "Now hop the %ld piece that's left.", (long)(b - r));
            st = pick_step(q, FM_NUM, a + b, a + b + 10, a + b - 10);
            SAY(st, "%ld + %ld lands on ...?", (long)a, (long)b);
        } else if (t->var == V_UP) { /* a - b: from b up to a */
            int32_t nt = (b / 10 + 1) * 10, p = b;
            m->pos = b;
            if (b % 10 && nt <= a) {
                st = hop_step(q, b, nt, 1, 0, 0);
                SAY(st, "Hop up to the next ten, %ld, with little hops.", (long)nt);
                p = nt;
            }
            if (a / 10 * 10 > p) {
                st = hop_step(q, p, a / 10 * 10, a / 10 * 10 - p >= 100 ? 100 : 0, 10, 0);
                SAY(st, "Now big hops of ten, up to %ld.", (long)(a / 10 * 10));
                p = a / 10 * 10;
            }
            if (a > p) {
                st = hop_step(q, p, a, 1, 0, 0);
                SAY(st, "Little hops the rest of the way to %ld.", (long)a);
            }
            st = pick_step(q, FM_NUM, a - b, a - b + 10, a + b);
            SAY(st, "How far did %s hop from %ld to %ld?", nm, (long)b, (long)a);
        } else if (t->var == V_TIME) { /* from a (minutes) for b minutes */
            int32_t e = a + b, p = a, nh = (a / 60 + 1) * 60;
            m->pos = a;
            if (a % 60 && nh <= e) {
                st = hop_step(q, a, nh, 10, 5, 0);
                fmt(q, FM_TIME, nh, l1, sizeof l1);
                SAY(st, "Hop the minutes up to the next o'clock: %s.", l1);
                p = nh;
            }
            if (e / 60 * 60 > p) {
                st = hop_step(q, p, e / 60 * 60, 60, 0, 0);
                SAY(st, "Now hop whole hours.");
                p = e / 60 * 60;
            }
            if (e > p) {
                st = hop_step(q, p, e, 10, 5, 0);
                fmt(q, FM_TIME, e, l2, sizeof l2);
                SAY(st, "Then the minutes left, to %s.", l2);
            }
            st = pick_step(q, FM_MIN, b, b + 40, b - 15);
            SAY(st, "How long is it, all the hops together?");
        } else { /* V_FRAC, V_BENCH: steps of 1/b from 0 */
            m->pos = 0;
            st = hop_step(q, 0, a, 1, 0, 0);
            SAY(st, "Drag %ld hops of 1/%ld along the path.", (long)a, (long)b);
            if (t->var == V_FRAC) {
                st = pick_step(q, FM_FRAC, a * 1000 + b, (a + 1) * 1000 + b, a * 1000 + b + 1);
                SAY(st, "Where did %s land?", nm);
            } else {
                int32_t n4 = 4 * a; /* nearest: 0 (n4 < b), 1/2 (b < n4 < 3b), 1 */
                st = pick_step(q, FM_NEAR, n4 < b ? 0 : n4 < 3 * b ? 1 : 2, 0, 0);
                SAY(st, "%ld/%ld: is it nearer 0, 1/2 or 1?", (long)a, (long)b);
            }
        }
        break;
    case MD_PLACES:
        if (t->var == V_PSUM) {
            int np = imax(ndig(a), ndig(b));
            int32_t tot = 0;
            for (int k = np - 1; k >= 0; k--) {
                int32_t part = (digit(a, k) + digit(b, k)) * pow10(k);
                st = step(q, GE_TAP, 1);
                st->a = k;
                SAY(st, "Tap the %s house: slide its blocks together!", house_nm[0][k]);
                st = pick_step(q, FM_NUM, part, part + pow10(k), part > pow10(k) ? part - pow10(k) : part + 2 * pow10(k));
                st->a = k;
                SAY(st, "%ld + %ld in the %s house is ...?", (long)(digit(a, k) * pow10(k)), (long)(digit(b, k) * pow10(k)),
                    house_nm[0][k]);
                tot += part;
            }
            st = pick_step(q, FM_NUM, tot, tot + 10, tot - 10);
            SAY(st, "Now put the houses together: altogether ...?");
        } else if (t->var == V_EXP) {
            int nd = ndig(a);
            st = step(q, GE_DRAG, nd);
            st->nch = (uint8_t)nd;
            for (int k = 0; k < nd; k++)
                st->ch[k] = nd - 1 - k, st->need[nd - 1 - k] = 1; /* the digits left to right: their houses */
            SAY(st, "Drag each digit of %ld to its own house.", (long)a);
            int32_t part = digit(a, (int)b) * pow10((int)b);
            st = pick_step(q, FM_NUM, part, digit(a, (int)b), part ? part * 10 : 10);
            SAY(st, "So the %s card is worth ...?", house_nm[0][b]);
        } else if (t->var == V_CHART) {
            int dec = (int)b;
            st = step(q, GE_DRAG, 0);
            st->nch = 3;
            for (int k = 0; k < 3; k++)
                st->ch[k] = 2 - k, st->need[k] = (int16_t)digit(a, k), st->want += digit(a, k);
            /* (the number to build, told in its parts: its name is the last question's answer) */
            if (dec)
                SAY(st, "Build %d ones, %d tenths, %d hundredths: drag discs in!", digit(a, 2), digit(a, 1), digit(a, 0));
            else
                SAY(st, "Build %d hundreds, %d tens, %d ones: drag discs in!", digit(a, 2), digit(a, 1), digit(a, 0));
            int32_t v = dec ? a * 10 : a, w1 = dec ? digit(a, 1) * 1000 + digit(a, 2) * 100 + digit(a, 0) * 10 : (a % 10) * 100 + a / 10 % 10 * 10 + a / 100;
            st = pick_step(q, dec ? FM_DEC : FM_NUM, v, w1, dec ? v + 1000 : v + 100);
            SAY(st, "The houses show the number ...?");
        } else { /* V_BLOCKS: a flats, b rods, c cubes */
            st = step(q, GE_DRAG, a + b + c);
            st->nch = 3, st->ch[0] = 2, st->ch[1] = 1, st->ch[2] = 0;
            st->need[2] = (int16_t)a, st->need[1] = (int16_t)b, st->need[0] = (int16_t)c;
            SAY(st, "Drag %ld flats, %ld rods and %ld cubes onto the mat.", (long)a, (long)b, (long)c);
            if (b >= 10) {
                st = step(q, GE_TAP, b / 10);
                st->a = 1;
                SAY(st, "%ld rods! Tap the rods to glue 10 into a flat.", (long)b);
            }
            int32_t v = a * 100 + b * 10 + c;
            st = pick_step(q, FM_NUM, v, a * 100 + b % 10 * 10 + c, v + 100);
            SAY(st, "So the blocks show ...?");
        }
        break;
    case MD_GIVE: {
        int32_t f = a % 100 >= 90 ? (a / 100 + 1) * 100 : (a / 10 + 1) * 10, k = f - a;
        st = step(q, GE_DRAG, k);
        st->nch = 1, st->ch[0] = 0, st->need[1] = (int16_t)k;
        SAY(st, k == 1 ? "Drag 1 from the %ld jar to the %ld jar: it makes %ld!" : "Drag cubes from the %ld jar to the %ld jar until it makes %ld!",
            (long)b, (long)a, (long)f);
        st = pick_step(q, FM_NUM, a + b, a + b + 1, a + b - 10);
        SAY(st, "%ld + %ld is easy: ...?", (long)f, (long)(b - k));
        break;
    }
    case MD_GROUPS:
        if (t->var == V_EQ || t->var == V_IDZ) {
            if (a) {
                st = step(q, GE_TAP, a);
                SAY(st, b ? "Tap each bag to fill it with %ld." : "Tap each bag: %ld in it ... none at all!", (long)b);
            }
            st = pick_step(q, FM_NUM, a * b, a + b, a * b + (b ? b : 1));
            SAY(st, a ? "%ld bags of %ld: how many in all?" : "%ld bags of %ld: how many in all?", (long)a, (long)b);
        } else if (t->var == V_DEAL) { /* a plates, b each */
            st = step(q, GE_TAP, b);
            SAY(st, "Tap the pile to deal one to each plate. Again, until it's gone!");
            st = pick_step(q, FM_NUM, b, a, b + 1);
            SAY(st, "%ld shared by %ld plates: how many on each?", (long)(a * b), (long)a);
        } else { /* V_BAG: bags of a, b bags */
            st = step(q, GE_TAP, b);
            SAY(st, "Tap the pile to fill a bag with %ld. Again, until it's gone!", (long)a);
            st = pick_step(q, FM_NUM, b, a, b + 1);
            SAY(st, "%ld in bags of %ld: how many bags?", (long)(a * b), (long)a);
        }
        break;
    case MD_ARRAY:
        switch (t->var) {
        case V_BUILD:
            st = slide_step(q, SL_ROWS, 0, 10, a);
            SAY(st, "Drag down to plant %ld rows of %ld.", (long)a, (long)b);
            st = pick_step(q, FM_NUM, a * b, a + b, a * b + b);
            SAY(st, "%ld rows of %ld: how many flowers?", (long)a, (long)b);
            break;
        case V_GROW:
            st = slide_step(q, SL_ROWS, 0, 10, b);
            SAY(st, "Drag down to plant rows of %ld until there are %ld.", (long)a, (long)(a * b));
            st = pick_step(q, FM_NUM, b, a, b + 1);
            SAY(st, "%ld times what makes %ld?", (long)a, (long)(a * b));
            break;
        case V_TURN:
            st = slide_step(q, SL_TURN, 0, 90, 90);
            SAY(st, "Drag to turn the %ld by %ld garden around.", (long)a, (long)b);
            st = pick_step(q, FM_SAME, 0, 0, 0);
            SAY(st, "%ld rows of %ld and %ld rows of %ld: the same amount?", (long)a, (long)b, (long)b, (long)a);
            if (c) {
                st = pick_step(q, FM_NUM, a, b, a + 1);
                SAY(st, "%ld \xC3\x97 %ld = %ld \xC3\x97 ?", (long)a, (long)b, (long)b);
            }
            break;
        case V_ASSOC:
            st = slide_step(q, SL_MERGE, 0, 4, 4);
            SAY(st, "Drag to slide the %ld gardens together.", (long)c);
            st = pick_step(q, FM_NUM, b * c, b + c, b * c + b);
            SAY(st, "Now each row has %ld \xC3\x97 %ld flowers: ...?", (long)b, (long)c);
            st = pick_step(q, FM_NUM, a * b * c, a * b + c, a * b * c + b * c);
            SAY(st, "%ld rows of %ld: how many flowers?", (long)a, (long)(b * c));
            break;
        case V_SPLIT:
            st = slide_step(q, SL_CUT, 1, (int)b - 1, c);
            SAY(st, "Drag the cut to split off %ld rows of %ld.", (long)a, (long)c);
            st = pick_step(q, FM_NUM, a * c, a + c, a * c + a);
            SAY(st, "The big piece: %ld \xC3\x97 %ld is ...?", (long)a, (long)c);
            st = pick_step(q, FM_NUM, a * (b - c), a + b - c, a * (b - c) + a);
            SAY(st, "The small piece: %ld \xC3\x97 %ld is ...?", (long)a, (long)(b - c));
            st = pick_step(q, FM_NUM, a * b, a * c, a * b + a);
            SAY(st, "Both pieces together: %ld + %ld is ...?", (long)(a * c), (long)(a * (b - c)));
            break;
        case V_DH:
            st = slide_step(q, SL_CUT, 1, (int)b - 1, b / 2);
            SAY(st, "Drag the cut to the middle: cut the garden in half!");
            st = pick_step(q, FM_NUM, a * b, a * b / 2, a * b + 2 * a);
            SAY(st, "Now it's %ld rows of %ld: how many?", (long)(2 * a), (long)(b / 2));
            break;
        case V_KNOWN: {
            int kn = a == 9 ? 10 : 5;
            st = pick_step(q, FM_NUM, kn * b, kn * b + b, kn + b);
            SAY(st, "You know this one: %d rows of %ld is ...?", kn, (long)b);
            st = step(q, GE_TAP, 1);
            SAY(st, a == 9 ? "Tap the last row to take it away." : "Tap below to plant one more row of %ld.", (long)b);
            st = pick_step(q, FM_NUM, a * b, kn * b, a * b + b);
            SAY(st, a == 9 ? "%ld take away %ld is ...?" : "%ld and %ld more is ...?", (long)(kn * b), (long)b);
            break;
        }
        default: break;
        }
        break;
    case MD_AREA:
        if (t->var == V_DIV) { /* a = b x qq: pieces of the length */
            int32_t qq = a / imax(1, (int)b), done = 0;
            for (int k = ndig(qq) - 1; k >= 0; k--) {
                int32_t w = digit(qq, k) * pow10(k);
                if (!w)
                    continue;
                st = pick_step(q, FM_NUM, w, w + pow10(k), w * 10);
                st->a = done, st->b = w;
                SAY(st, "%ld rows: %ld \xC3\x97 ? = %ld. How long a piece?", (long)b, (long)b, (long)(b * w));
                done += w;
            }
            st = pick_step(q, FM_NUM, qq, qq + 10, qq - 1);
            SAY(st, "The pieces together: the floor is how long?");
        } else {
            int32_t at = a - a % 10, bt = b - b % 10;
            if (t->var == V_AM && at && a % 10) {
                st = slide_step(q, SL_CUT, 1, (int)a - 1, at);
                SAY(st, "Drag the cut to %ld: split %ld into %ld and %ld.", (long)at, (long)a, (long)at, (long)(a % 10));
            }
            if (t->var == V_AM && bt && b % 10) {
                st = slide_step(q, SL_VCUT, 1, (int)b - 1, bt);
                SAY(st, "And down the side: cut %ld into %ld and %ld.", (long)b, (long)bt, (long)(b % 10));
            }
            /* the pieces: the ones of b first (as the lines are written), then its tens */
            int32_t tot = 0;
            for (int j = 0; j < 2; j++) {
                int32_t bp = j ? bt : b % 10;
                if (!bp)
                    continue;
                for (int k = 0; k < 3; k++) {
                    int32_t ap = digit(a, k) * pow10(k);
                    if (!ap)
                        continue;
                    st = pick_step(q, FM_NUM, ap * bp, ap * bp + ap, ap + bp);
                    st->a = ap, st->b = bp;
                    SAY(st, "%ld \xC3\x97 %ld is ...?", (long)bp, (long)ap);
                    tot += ap * bp;
                }
            }
            st = pick_step(q, FM_NUM, tot, tot + 10, tot - 10);
            SAY(st, "Now add all the pieces: ...?");
        }
        break;
    case MD_PEEL: {
        int32_t qq = a / imax(1, (int)b);
        m->pos = a;
        st = step(q, GE_DRAG, 0);
        int k = 0;
        static const int16_t gs[5] = {100, 10, 5, 2, 1};
        for (int i = 0; i < 5 && k < 4; i++)
            if (gs[i] <= qq)
                st->ch[k++] = gs[i];
        st->nch = (uint8_t)k;
        SAY(st, "Peel handfuls of groups of %ld off the pile until it's gone.", (long)b);
        st = pick_step(q, FM_NUM, qq, qq + 10, qq - 1);
        SAY(st, "Add up the groups you peeled: ...?");
        break;
    }
    case MD_FRIENDLY: {
        st = step(q, GE_DRAG, 1);
        st->nch = 3, st->ch[0] = 0, st->ch[1] = 1, st->ch[2] = 2;
        SAY(st, "Drag one number onto another to make a round number.");
        int32_t p = a * b * c;
        st = pick_step(q, FM_NUM, p, p + 10, p / 10);
        SAY(st, "And the round number times the last one is ...?");
        break;
    }
    case MD_TAPE:
        if (t->var == V_PPW) { /* part a, whole b */
            st = step(q, GE_DRAG, 2);
            st->nch = 2, st->ch[0] = 0, st->ch[1] = 1, st->need[0] = st->need[1] = 1;
            SAY(st, "Drag %ld onto its part of the ribbon, and %ld over all of it.", (long)a, (long)b);
            st = pick_step(q, FM_NUM, b - a, a + b, b - a + 10);
            SAY(st, "The part with the ? is ...?");
        } else if (t->var == V_CMP) {
            if (c) { /* times as many: a, b times */
                st = step(q, GE_DRAG, 1);
                st->nch = 1, st->ch[0] = 0, st->need[0] = 1;
                SAY(st, "Drag %ld onto the short ribbon. The long one is %ld of them!", (long)a, (long)b);
                st = pick_step(q, FM_NUM, a * b, a + b, a * b + a);
                SAY(st, "%ld pieces of %ld: the long ribbon is ...?", (long)b, (long)a);
            } else {
                st = step(q, GE_DRAG, 2);
                st->nch = 2, st->ch[0] = 0, st->ch[1] = 1, st->need[0] = st->need[1] = 1;
                SAY(st, "Drag %ld onto the short ribbon and %ld onto the bit that sticks out.", (long)a, (long)b);
                st = pick_step(q, FM_NUM, a + b, a + b + 10, a > b ? a - b : b - a);
                SAY(st, "So the long ribbon is ...?");
            }
        } else { /* V_EQG: a pieces of b */
            st = step(q, GE_DRAG, 1);
            st->nch = 1, st->ch[0] = 0, st->need[c ? 1 : 0] = 1;
            SAY(st, c ? "Drag %ld over the whole ribbon." : "Drag %ld onto one piece: every piece is the same!", (long)(c ? a * b : b));
            st = c ? pick_step(q, FM_NUM, b, a, b + 1) : pick_step(q, FM_NUM, a * b, a + b, a * b + b);
            SAY(st, c ? "%ld pieces make %ld: each piece is ...?" : "%ld pieces of %ld: the whole ribbon is ...?", (long)a,
                (long)(c ? a * b : b));
        }
        break;
    case MD_STRIP:
        if (t->var == V_STRIP || t->var == V_CIRC) {
            st = slide_step(q, SL_PIECES, 1, 12, b);
            SAY(st, t->var == V_CIRC && !c ? "Drag to cut the pie into %ld fair slices." : "Drag to cut the cake into %ld fair pieces.",
                (long)b);
            st = step(q, GE_TAP, a);
            SAY(st, "Tap %ld %s to frost them pink.", (long)a, a == 1 ? "piece" : "pieces");
            st = pick_step(q, FM_FRAC, a * 1000 + b, b * 1000 + a, a * 1000 + b + 1);
            SAY(st, "%ld pink of %ld fair pieces: the pink part is ...?", (long)a, (long)b);
        } else if (t->var == V_UNITS) {
            st = step(q, GE_DRAG, a);
            st->nch = 1, st->ch[0] = 0, st->need[0] = (int16_t)a;
            SAY(st, "Drag pieces of 1/%ld into the cake, one by one: %ld of them.", (long)b, (long)a);
            st = pick_step(q, FM_FRAC, a * 1000 + b, a * 1000 + a * b, (a + 1) * 1000 + b);
            SAY(st, "%ld pieces of 1/%ld make ...?", (long)a, (long)b);
        } else { /* V_EQUIV: a/b, cut c times */
            st = slide_step(q, SL_RECUT, 1, 4, c);
            SAY(st, "Drag to cut every piece into %ld.", (long)c);
            st = pick_step(q, FM_NUM, a * c, a + c, a * c + 1);
            SAY(st, "%ld/%ld = ?/%ld", (long)a, (long)b, (long)(b * c));
        }
        break;
    case MD_MONEY:
        st = step(q, GE_DRAG, digit(a, 0) + digit(a, 1) + digit(a, 2));
        st->nch = 3, st->ch[0] = 2, st->ch[1] = 1, st->ch[2] = 0;
        for (int k = 0; k < 3; k++)
            st->need[k] = (int16_t)digit(a, k);
        SAY(st, "Drag %d dollars, %d dimes and %d pennies to their trays.", digit(a, 2), digit(a, 1), digit(a, 0));
        st = pick_step(q, FM_MONEY, a, digit(a, 2) * 100 + digit(a, 0) * 10 + digit(a, 1), a + 100);
        SAY(st, "So that's ...?");
        break;
    case MD_PLOT: {
        int nm2 = (int)a, cnt = 0;
        st = step(q, GE_DRAG, nm2);
        st->nch = 1, st->ch[0] = 0;
        for (int i = 0; i < nm2; i++) {
            int v = (int)((b >> (4 * i)) & 15);
            st->need[v]++, cnt += v == c;
        }
        SAY(st, "Drag an X above each ribbon's length, one by one.");
        st = pick_step(q, FM_NUM, cnt, cnt + 1, cnt ? cnt - 1 : 2);
        static const char qn[9][8] = {"0", "1/4", "1/2", "3/4", "1", "1 1/4", "1 1/2", "1 3/4", "2"};
        SAY(st, "How many ribbons are %s inch long?", qn[c % 9]);
        break;
    }
    case MD_COLS:
        if (t->var == V_CMUL) { /* a x b, b two digits: a line for each digit */
            int32_t r1 = a * (b % 10), r2 = a * (b / 10) * 10;
            st = pick_step(q, FM_NUM, r1, r1 + 10, a + b % 10);
            SAY(st, "First line: %ld \xC3\x97 %ld = ?", (long)a, (long)(b % 10));
            st = pick_step(q, FM_NUM, r2, r2 / 10, r2 + 10);
            SAY(st, "Next line: %ld \xC3\x97 %ld = ? (a 0 holds the ones place)", (long)a, (long)(b / 10 * 10));
            st = pick_step(q, FM_NUM, r1 + r2, r1 + r2 + 100, r1 + r2 - 10);
            SAY(st, "Add the two lines: ...?");
        } else { /* add or take away, column by column (a carry or a trade said) */
            int sub = (int)c, carry = 0, nd = ndig(imax((int)a, (int)b));
            for (int k = 0; k < nd; k++) {
                int da = digit(a, k), db = digit(b, k), v;
                if (sub) {
                    v = da - db - carry, carry = v < 0, v += carry ? 10 : 0;
                } else {
                    v = da + db + carry, carry = v >= 10, v %= 10;
                }
                st = pick_step(q, FM_NUM, v, (v + 1) % 10, (v + 9) % 10 == v ? 2 : (v + 9) % 10);
                st->a = k;
                SAY(st, carry ? (sub ? "The %s: trade a ten, then ...?" : "The %s: write ..., carry the 1!") : "The %s: ...?",
                    house_nm[0][k < 3 ? k : 2]);
                if (k >= 3)
                    SAY(st, "The thousands: ...?");
            }
        }
        break;
    default: break;
    }
    (void)api;
}

/* (tests) the steps of way s for numbers x */
bool mq_strat_plan(mq_t *q, int s, const int32_t x[6])
{
    if (s < 0 || s >= STG_COUNT)
        return false;
    q->strat.id = (uint8_t)s;
    plan(q, s, x);
    return q->strat.n > 0;
}

/* ------------------------------------------------------------------ "use this way" orders */
#define TIMES "\xC3\x97"
#define DIVS "\xC3\xB7"
#define MINUS "\xE2\x88\x92"
static void wr(mq_prob_t *p, int32_t v, int mc)
{
    if (v < 0 || v == p->ans || p->nwrong >= 6)
        return;
    for (int i = 0; i < p->nwrong; i++)
        if (p->wrong[i] == v)
            return;
    p->wrong[p->nwrong] = v, p->wrong_mc[p->nwrong++] = (uint8_t)mc;
}
static void choice(mq_t *q, mq_prob_t *p, const char *s, int mc)
{
    if (p->nchoice >= 4)
        return;
    q->api->snprintf(p->choice[p->nchoice], sizeof p->choice[0], "%s", s);
    p->choice_mc[p->nchoice++] = (uint8_t)mc;
}
static const char qtr[5][6] = {"0", "1/4", "1/2", "3/4", "1"};

/* an order of way s with numbers x (its skill, its friend asking, the answer the model ends with) */
bool mq_strat_make(mq_t *q, mq_prob_t *p, int s, int32_t x[6])
{
    if (s < 0 || s >= STG_COUNT)
        return false;
    const modx_api_t *api = q->api;
    const strat_t *t = &tab[s];
    uint8_t diff = p->diff;
    uint8_t *b8 = (uint8_t *)p;
    for (size_t i = 0; i < sizeof *p; i++)
        b8[i] = 0;
    p->skill = t->skill, p->diff = diff, p->kind = K_NUM, p->vis = V_NONE;
    p->ca = t->who, p->cb = (uint8_t)((t->who + 1) % MQ_NCAST);
    p->mc_default = MC_CARELESS;
    int32_t a = x[0], b = x[1], c = x[2];
    char nm[40], l[MQ_TXT];
    mq_strat_name(q, s, nm, sizeof nm);
    api->snprintf(p->text[0], MQ_TXT, "%s!", nm);
    const char *who = mq_cast_name(q, t->who), *pal = mq_cast_name(q, (t->who + 1) % MQ_NCAST);
    switch (s) {
    case STG_JUMPS: case STG_MAKE10: case STG_COMP: case STG_PSUMS:
        if (s == STG_JUMPS && c) {
            api->snprintf(l, sizeof l, "%ld " MINUS " %ld = ?", (long)a, (long)b), p->ans = a - b;
            wr(p, a + b, MC_WRONG_OP), wr(p, p->ans + 10, MC_SUB_FLIP), p->mc_default = MC_SUB_FLIP;
        } else {
            api->snprintf(l, sizeof l, "%ld + %ld = ?", (long)a, (long)b), p->ans = a + b;
            wr(p, p->ans - 10, MC_NO_REGROUP), wr(p, p->ans + 10, MC_CARELESS), p->mc_default = MC_NO_REGROUP;
        }
        break;
    case STG_COUNTUP:
        api->snprintf(l, sizeof l, "%ld " MINUS " %ld = ?", (long)a, (long)b), p->ans = a - b;
        wr(p, a + b, MC_WRONG_OP), wr(p, p->ans + 10, MC_SUB_FLIP), p->mc_default = MC_SUB_FLIP;
        break;
    case STG_EXPAND: {
        char d[3][8];
        for (int k = 0; k < 3; k++)
            if (k == b)
                api->snprintf(d[k], sizeof d[k], "?");
            else
                api->snprintf(d[k], sizeof d[k], "%ld", (long)(digit(a, k) * pow10(k)));
        api->snprintf(l, sizeof l, "%ld = %s + %s + %s", (long)a, d[2], d[1], d[0]);
        p->ans = digit(a, (int)b) * pow10((int)b);
        wr(p, digit(a, (int)b), MC_PLACE_SHIFT), wr(p, p->ans * 10, MC_PLACE_SHIFT), p->mc_default = MC_PLACE_SHIFT;
        break;
    }
    case STG_ALGO_AS:
        api->snprintf(l, sizeof l, c ? "%ld " MINUS " %ld = ?" : "%ld + %ld = ?", (long)a, (long)b);
        p->ans = c ? a - b : a + b, p->ws = (uint8_t)(c ? WS_SUB : WS_ADD), p->op[0] = a, p->op[1] = b;
        wr(p, p->ans + (c ? 10 : -10), c ? MC_SUB_FLIP : MC_NO_REGROUP), p->mc_default = c ? MC_SUB_FLIP : MC_NO_REGROUP;
        break;
    case STG_ALGO_M:
        api->snprintf(l, sizeof l, "%ld " TIMES " %ld = ?", (long)a, (long)b);
        p->ans = a * b, p->ws = WS_MUL, p->op[0] = a, p->op[1] = b;
        wr(p, a * (b % 10) + a * (b / 10), MC_PLACE_SHIFT), wr(p, a * (b % 10), MC_PARTIAL_PRODUCT);
        p->mc_default = MC_PARTIAL_PRODUCT;
        break;
    case STG_GROUPS: case STG_ONEZERO: case STG_KNOWN: case STG_DOUBLE: case STG_AREA: case STG_PPROD:
        if (s == STG_GROUPS) {
            api->snprintf(p->text[1], MQ_TXT, "%ld bags with %ld seeds in each.", (long)a, (long)b);
            api->snprintf(l, sizeof l, "How many seeds in all?");
        } else
            api->snprintf(l, sizeof l, "%ld " TIMES " %ld = ?", (long)a, (long)b);
        p->ans = a * b;
        wr(p, a + b, MC_ADD_NOT_MUL), wr(p, a * b + (b ? b : 1), MC_NEIGHBOR);
        if (s == STG_AREA || s == STG_PPROD)
            wr(p, a % 10 * b, MC_PARTIAL_PRODUCT), wr(p, (a - a % 10) * b, MC_PARTIAL_PRODUCT);
        p->mc_default = s == STG_AREA || s == STG_PPROD ? MC_PARTIAL_PRODUCT : MC_NEIGHBOR;
        break;
    case STG_ARRAY:
        api->snprintf(p->text[1], MQ_TXT, "Plant %ld rows of %ld tulips.", (long)a, (long)b);
        api->snprintf(l, sizeof l, "How many tulips?"), p->ans = a * b;
        wr(p, a + b, MC_ADD_NOT_MUL), wr(p, a * b + b, MC_NEIGHBOR), p->mc_default = MC_NEIGHBOR;
        break;
    case STG_TURN:
        if (c) {
            api->snprintf(l, sizeof l, "%ld " TIMES " %ld = %ld " TIMES " ?", (long)a, (long)b, (long)b), p->ans = a;
            wr(p, b, MC_CARELESS), wr(p, a * b, MC_ONE_STEP);
        } else {
            api->snprintf(l, sizeof l, "Is %ld " TIMES " %ld the same as %ld " TIMES " %ld?", (long)a, (long)b, (long)b, (long)a);
            p->kind = K_CHOICE;
            choice(q, p, "yes", MC_NONE), choice(q, p, "no", MC_CARELESS);
            p->ans = 0;
        }
        break;
    case STG_REGROUP:
        api->snprintf(l, sizeof l, "(%ld " TIMES " %ld) " TIMES " %ld = ?", (long)a, (long)b, (long)c), p->ans = a * b * c;
        wr(p, a * b + c, MC_ONE_STEP), wr(p, a * b * c + b * c, MC_NEIGHBOR), p->mc_default = MC_ONE_STEP;
        break;
    case STG_SPLIT:
        api->snprintf(p->text[1], MQ_TXT, "Split the garden to find");
        api->snprintf(l, sizeof l, "%ld " TIMES " %ld = ?", (long)a, (long)b), p->ans = a * b;
        wr(p, a * c, MC_PARTIAL_PRODUCT), wr(p, a * (b - c), MC_PARTIAL_PRODUCT), wr(p, a * b + a, MC_NEIGHBOR);
        p->mc_default = MC_PARTIAL_PRODUCT;
        break;
    case STG_FRIENDLY:
        api->snprintf(l, sizeof l, "%ld " TIMES " %ld " TIMES " %ld = ?", (long)a, (long)b, (long)c), p->ans = a * b * c;
        wr(p, p->ans / 10, MC_ZEROS), wr(p, p->ans * 10, MC_ZEROS), p->mc_default = MC_ZEROS;
        break;
    case STG_SHARE:
        api->snprintf(p->text[1], MQ_TXT, "%ld cookies shared by %ld plates.", (long)(a * b), (long)a);
        api->snprintf(l, sizeof l, "How many cookies on each plate?"), p->ans = b;
        wr(p, a, MC_DIV_MIX), wr(p, a * b - a, MC_WRONG_OP), p->mc_default = MC_DIV_MIX;
        break;
    case STG_BAGS:
        api->snprintf(p->text[1], MQ_TXT, "%ld seeds, %ld in each bag.", (long)(a * b), (long)a);
        api->snprintf(l, sizeof l, "How many bags?"), p->ans = b;
        wr(p, a, MC_DIV_MIX), wr(p, a * b - a, MC_WRONG_OP), p->mc_default = MC_DIV_MIX;
        break;
    case STG_THINKX:
        api->snprintf(l, sizeof l, "%ld " DIVS " %ld = ?", (long)(a * b), (long)a), p->ans = b;
        wr(p, a * b - a, MC_WRONG_OP), wr(p, b + 1, MC_NEIGHBOR), p->mc_default = MC_NEIGHBOR;
        break;
    case STG_PEEL: case STG_AREADIV:
        api->snprintf(l, sizeof l, "%ld " DIVS " %ld = ?", (long)a, (long)b), p->ans = a / b;
        wr(p, p->ans * 10, MC_PLACE_SHIFT), wr(p, p->ans + 1, MC_CLOSE), p->mc_default = MC_DIV_MIX;
        break;
    case STG_TAPE_PPW:
        api->snprintf(p->text[1], MQ_TXT, "%s has %ld ribbons, %ld red.", who, (long)b, (long)a);
        api->snprintf(l, sizeof l, "The rest are blue. How many blue?"), p->ans = b - a;
        wr(p, a + b, MC_WRONG_OP), p->mc_default = MC_WRONG_OP;
        break;
    case STG_TAPE_CMP:
        if (c) {
            api->snprintf(p->text[1], MQ_TXT, "%s has %ld shells. %s has", pal, (long)a, who);
            api->snprintf(l, sizeof l, "%ld times as many. How many?", (long)b), p->ans = a * b;
            wr(p, a + b, MC_TIMES_AS_ADD), p->mc_default = MC_TIMES_AS_ADD;
        } else {
            api->snprintf(p->text[1], MQ_TXT, "%s has %ld shells, %s %ld more.", pal, (long)a, who, (long)b);
            api->snprintf(l, sizeof l, "How many shells has %s?", who), p->ans = a + b;
            wr(p, a > b ? a - b : b - a, MC_WRONG_OP), p->mc_default = MC_WRONG_OP;
        }
        break;
    case STG_TAPE_EQ:
        if (c) {
            api->snprintf(p->text[1], MQ_TXT, "%ld stickers shared by %ld friends.", (long)(a * b), (long)a);
            api->snprintf(l, sizeof l, "How many stickers each?"), p->ans = b;
            wr(p, a, MC_DIV_MIX), p->mc_default = MC_DIV_MIX;
        } else {
            api->snprintf(p->text[1], MQ_TXT, "%ld friends get %ld stickers each.", (long)a, (long)b);
            api->snprintf(l, sizeof l, "How many stickers in all?"), p->ans = a * b;
            wr(p, a + b, MC_ADD_NOT_MUL), p->mc_default = MC_ADD_NOT_MUL;
        }
        break;
    case STG_STRIP: case STG_PIE:
        api->snprintf(p->text[1], MQ_TXT, "A %s in %ld fair %s, %ld pink.", s == STG_PIE && !c ? "pie" : "cake", (long)b,
                      s == STG_PIE && !c ? "slices" : "pieces", (long)a);
        api->snprintf(l, sizeof l, "What part is pink?");
        p->kind = K_FRAC, p->ans = a * 1000 + b;
        wr(p, b * 1000 + a, MC_NUM_DENOM_SWAP), wr(p, a * 1000 + (b - a), MC_UNEQUAL_PARTS), p->mc_default = MC_NUM_DENOM_SWAP;
        break;
    case STG_FLINE:
        api->snprintf(p->text[1], MQ_TXT, "%s hops %ld steps of 1/%ld from 0.", who, (long)a, (long)b);
        api->snprintf(l, sizeof l, "Where does %s land?", who);
        p->kind = K_FRAC, p->ans = a * 1000 + b;
        wr(p, a * 1000 + b + 1, MC_COUNT_TICKS), wr(p, (a + 1) * 1000 + b, MC_COUNT_TICKS), p->mc_default = MC_COUNT_TICKS;
        break;
    case STG_BENCH: {
        api->snprintf(l, sizeof l, "Is %ld/%ld nearer 0, 1/2 or 1?", (long)a, (long)b);
        p->kind = K_CHOICE;
        choice(q, p, "nearer 0", MC_BIG_DENOM), choice(q, p, "nearer 1/2", MC_BIG_DENOM), choice(q, p, "nearer 1", MC_BIG_DENOM);
        int32_t n4 = 4 * a;
        p->ans = n4 < b ? 0 : n4 < 3 * b ? 1 : 2;
        p->choice_mc[p->ans] = MC_NONE;
        break;
    }
    case STG_UNITS: {
        int k = 0;
        if (a <= 4) {
            for (int i = 0; i < a; i++)
                k += api->snprintf(l + k, sizeof l - (size_t)k, i ? " + 1/%ld" : "1/%ld", (long)b);
            api->snprintf(l + k, sizeof l - (size_t)k, " = ?");
        } else
            api->snprintf(l, sizeof l, "%ld pieces of 1/%ld make ?", (long)a, (long)b);
        p->kind = K_FRAC, p->ans = a * 1000 + b;
        wr(p, a * 1000 + a * b, MC_ADD_DENOMS), wr(p, 1000 + b, MC_CARELESS), p->mc_default = MC_ADD_DENOMS;
        break;
    }
    case STG_EQUIV:
        api->snprintf(l, sizeof l, "%ld/%ld = ?/%ld", (long)a, (long)b, (long)(b * c)), p->ans = a * c;
        wr(p, a + c, MC_EQUIV_ADD), wr(p, a, MC_CARELESS), p->mc_default = MC_EQUIV_ADD;
        break;
    case STG_PVCHART:
        if (b) {
            api->snprintf(p->text[1], MQ_TXT, "%d ones, %d tenths, %d hundredths:", digit(a, 2), digit(a, 1), digit(a, 0));
            p->kind = K_DEC, p->ans = a * 10;
            wr(p, a * 100, MC_DEC_PLACE), p->mc_default = MC_DEC_PLACE;
        } else {
            api->snprintf(p->text[1], MQ_TXT, "%d hundreds, %d tens, %d ones:", digit(a, 2), digit(a, 1), digit(a, 0));
            p->ans = a;
            wr(p, digit(a, 2) * 10 + digit(a, 0), MC_PLACE_SHIFT), p->mc_default = MC_PLACE_SHIFT;
        }
        api->snprintf(l, sizeof l, "What number is it?");
        break;
    case STG_BLOCKS:
        api->snprintf(p->text[1], MQ_TXT, "%ld flats, %ld rods, %ld cubes:", (long)a, (long)b, (long)c);
        api->snprintf(l, sizeof l, "What number do they make?"), p->ans = a * 100 + b * 10 + c;
        wr(p, a * 100 + b % 10 * 10 + c, MC_PLACE_SHIFT), p->mc_default = MC_PLACE_SHIFT;
        break;
    case STG_MONEY:
        api->snprintf(p->text[1], MQ_TXT, "%d dollars, %d dimes, %d pennies:", digit(a, 2), digit(a, 1), digit(a, 0));
        api->snprintf(l, sizeof l, "How much money?");
        p->kind = K_MONEY, p->ans = a;
        wr(p, digit(a, 2) * 100 + digit(a, 0) * 10 + digit(a, 1), MC_COIN_COUNT), p->mc_default = MC_COIN_COUNT;
        break;
    case STG_TIMELINE: {
        char s1[8], s2[8];
        fmt(q, FM_TIME, a, s1, sizeof s1), fmt(q, FM_TIME, a + b, s2, sizeof s2);
        api->snprintf(p->text[1], MQ_TXT, "%s naps from %s to %s.", who, s1, s2);
        api->snprintf(l, sizeof l, "How many minutes is the nap?"), p->ans = b;
        wr(p, (a + b) / 60 * 100 + (a + b) % 60 - (a / 60 * 100 + a % 60), MC_ELAPSED_100), p->mc_default = MC_ELAPSED_100;
        break;
    }
    case STG_PLOT: {
        int k = api->snprintf(p->text[1], MQ_TXT, "Ribbons:");
        for (int i = 0; i < a && k < MQ_TXT - 6; i++)
            k += api->snprintf(p->text[1] + k, (size_t)(MQ_TXT - k), i ? ", %s" : " %s", qtr[((b >> (4 * i)) & 15) % 5]);
        int cnt = 0;
        for (int i = 0; i < a; i++)
            cnt += ((b >> (4 * i)) & 15) == c;
        api->snprintf(l, sizeof l, "How many are %s inch long?", qtr[c % 5]);
        p->ans = cnt;
        wr(p, cnt + 1, MC_GRAPH_READ), p->mc_default = MC_GRAPH_READ;
        break;
    }
    default: return false;
    }
    int ln = p->text[1][0] ? 2 : 1;
    api->snprintf(p->text[ln], MQ_TXT, "%s", l);
    api->snprintf(p->cap, sizeof p->cap, "%s", l);
    p->way = (uint8_t)(s + 1), p->wx[0] = a, p->wx[1] = b, p->wx[2] = c;
    p->a[1] = (int16_t)(a % 30000), p->a[2] = (int16_t)(b % 30000), p->a[3] = (int16_t)(c % 30000);
    p->ident = mq_prob_ident(p);
    return true;
}

/* ------------------------------------------------------------------ the flow */
static mq_sstep_t *cur(mq_t *q) { return &q->strat.st[q->strat.cur < SS_MAX ? q->strat.cur : SS_MAX - 1]; }
static int who(mq_t *q) { return q->strat.mode == SM_HELP ? q->gm.help_slot % MQ_NCAST : tab[q->strat.id].who; }
static void say(mq_t *q, const char *s)
{
    mq_strat_t *m = SM;
    q->api->snprintf(m->say, sizeof m->say, "%s", s);
    m->say_t = mq_mark(q);
    mq_speak(q, who(q), s, MODX_SPEAK_NOW);
    if (m->mode == SM_HELP) { /* (the helper's moment is drawn in the field: the board says it) */
        q->api->snprintf(q->gm.say, sizeof q->gm.say, "%s", s);
        q->gm.mood = 0, q->gm.hud_ok = 0;
    }
    q->redraw = true;
}
/* on (the helper's moment's way only while that moment is on) */
bool mq_strat_on(const mq_t *q) { return q->strat.on != 0 && (q->strat.mode != SM_HELP || q->sub == GS_HELP); }
static mq_more_t *rec(mq_t *q) { return mq_more(q); }
static void bump(uint8_t *v)
{
    if (*v < 255)
        (*v)++;
}

static void enter_step(mq_t *q)
{
    mq_strat_t *m = SM;
    m->cnt = 0, m->picked = -1, m->hint = 0, m->step_t = mq_mark(q);
    mq_wgs(q)->focus = -1;
    if (m->cur < m->n) {
        mq_sstep_t *s = cur(q);
        if (s->g == GE_SLIDE)
            m->val = s->lo;
        if (m->phase == SP_DO)
            say(q, s->say);
    }
}
static void teach_begin(mq_t *q, const int32_t *x)
{
    mq_strat_t *m = SM;
    m->phase = SP_TEACH, m->beat = 0, m->t = mq_mark(q);
    plan(q, m->id, x);
    m->intro_n = (uint8_t)(m->mode == SM_HELP ? 0 : mq_strat_teach_n(m->id));
    char l[110];
    if (m->intro_n) {
        expand(q, tab[m->id].intro[0], tab[m->id].who, l, sizeof l);
        say(q, l);
    } else
        say(q, m->st[0].say);
    if (m->mode != SM_HELP)
        bump(&rec(q)->strat_seen[m->id]);
    q->dirty = true;
}
/* the notebook is typed: only in the calm games (never on the kitchen's beat or in a moving game) */
static bool notebook_game(const mq_t *q)
{
    int m = q->gm.mech;
    return m == ME_VAULT || m == ME_TILL || m == ME_PETS;
}
static void do_begin(mq_t *q)
{
    mq_strat_t *m = SM;
    m->phase = SP_DO, m->t = mq_mark(q);
    if (tab[m->id].model == MD_COLS && m->mode == SM_ORDER && q->prob.ws && notebook_game(q)) { /* the columns: in the
                                                                                                * friend's notebook */
        bump(&rec(q)->strat_used[m->id]);
        mq_strat_close(q);
        mq_cols_open(q);
        return;
    }
    plan(q, m->id, m->ex);
    enter_step(q);
}

void mq_strat_start(mq_t *q, int s, int mode, const int32_t *x)
{
    if (s < 0 || s >= STG_COUNT)
        return;
    mq_strat_t *m = SM;
    if (mode != SM_HELP) { /* (the helper's moment leaves her order's notebook as it is) */
        mq_cols_close(q);
        q->teach.on = 0;
    }
    m->on = 1, m->id = (uint8_t)s, m->mode = (uint8_t)mode, m->wrongs = 0, m->judged = 0, m->second = 0;
    for (int i = 0; i < 6; i++)
        m->ex[i] = x ? x[i] : 0;
    if (!x)
        example_numbers(s, m->ex);
    q->gm.bulb_t = 0, q->gm.hud_ok = 0;
    if (mode == SM_BULB)
        q->used_help = true;
    if (mode == SM_HELP) { /* the similar order's numbers, shown by the friend */
        bump(&rec(q)->strat_help[s]);
        teach_begin(q, m->ex);
        return;
    }
    if (mode == SM_LESSON) { /* a similar example of its own numbers, shown */
        teach_begin(q, m->ex);
        return;
    }
    if (mode == SM_SHOW || !rec(q)->strat_seen[s]) { /* new to her: the friend shows it with their own example first */
        int32_t e[6];
        example_numbers(s, e);
        teach_begin(q, e);
        return;
    }
    do_begin(q);
}

void mq_strat_close(mq_t *q)
{
    mq_strat_t *m = SM;
    if (!m->on)
        return;
    m->on = 0;
    q->gm.hud_ok = 0;
    mq_wg_reset(q); /* (a chip picked in the model is not the game's: CR2-07) */
    q->prob_start = q->now, q->last_input = q->now;
    q->redraw = true;
}

/* her order worked to the end: into the game (the lock opens, the bubble pops ...) */
static void finish(mq_t *q)
{
    mq_strat_t *m = SM;
    m->phase = SP_DONE, m->t = mq_mark(q);
    bump(&rec(q)->strat_used[m->id]);
    q->dirty = true;
    mq_sfx(q, MODX_SFX_CHEER, 0);
    say(q, m->wrongs ? "You worked it out! Here it goes!" : "That's the trick! Here it goes!");
}
static void done_now(mq_t *q)
{
    mq_strat_t *m = SM;
    int mode = m->mode;
    bool worked = m->phase == SP_DONE;
    uint32_t start = q->prob_start;
    mq_strat_close(q);
    if (worked && (mode == SM_ORDER || mode == SM_BULB) && q->scene == SC_PLAY && q->sub == GS_PLAY) {
        q->prob_start = start; /* (her order's own time, the model's included: never a 0 ms answer, CR2-24) */
        mq_game_answer(q, q->prob.ans);
    }
}

static void next_step(mq_t *q)
{
    mq_strat_t *m = SM;
    m->cur++;
    if (m->phase == SP_DO && m->cur >= m->n) {
        finish(q);
        return;
    }
    enter_step(q);
}

/* a wrong move: its why, said; the first one counts the order as missed first time */
static void wrong(mq_t *q, const char *why)
{
    mq_strat_t *m = SM;
    m->wrongs++, m->bad_t = mq_mark(q);
    mq_sfx(q, MODX_SFX_DUD, 0);
    say(q, why);
    if (m->mode == SM_ORDER && !m->judged && !q->tried_wrong && m->phase == SP_DO && q->scene == SC_PLAY) {
        m->judged = 1;
        mq_judge_slip(q); /* (a miss for the coach, never a made-up answer of hers: CR2-09) */
        q->retrying = false, q->sub = GS_PLAY;
    }
}

/* ------------------------------------------------------------------ the moves */
static const char *hop_word(mq_t *q, int32_t v, char *o, int n)
{
    const strat_t *t = &tab[q->strat.id];
    int32_t av = v < 0 ? -v : v;
    if (t->var == V_TIME && t->model == MD_JUMPS)
        q->api->snprintf(o, (size_t)n, av >= 60 ? "+%ld h" : "+%ld min", (long)(av >= 60 ? av / 60 : av));
    else if ((t->var == V_FRAC || t->var == V_BENCH) && t->model == MD_JUMPS)
        q->api->snprintf(o, (size_t)n, "+1/%ld", (long)q->strat.x[1]);
    else
        q->api->snprintf(o, (size_t)n, v < 0 ? MINUS "%ld" : "+%ld", (long)av);
    return o;
}
/* a hop of v onto the path */
static bool do_hop(mq_t *q, int32_t v)
{
    mq_strat_t *m = SM;
    mq_sstep_t *s = cur(q);
    int32_t to = m->pos + v;
    bool fwd = s->want >= s->a;
    if (v == 0 || (fwd ? (v < 0 || to > s->want) : (v > 0 || to < s->want))) {
        char l[90], w[12];
        q->api->snprintf(l, sizeof l, "A hop of %s goes past the spot. Try a smaller hop!", hop_word(q, v, w, sizeof w));
        wrong(q, l);
        return false;
    }
    if (m->nhop < 24)
        m->hop[m->nhop++] = (int16_t)v;
    m->pos = to;
    mq_sfx(q, MODX_SFX_JUMP, 0);
    if (m->pos == s->want)
        next_step(q);
    return true;
}

/* a thing dragged to place `slot` (chip: the chip's index in the step) */
static bool friendly_pair(int32_t a, int32_t b) { return a * b == 10 || a * b == 100 || a * b == 1000; }
static bool do_drop(mq_t *q, int chip, int slot)
{
    mq_strat_t *m = SM;
    mq_sstep_t *s = cur(q);
    const strat_t *t = &tab[m->id];
    char l[96];
    if (chip < 0 || chip >= s->nch)
        return false;
    int32_t cv = s->ch[chip];
    switch (t->model) {
    case MD_PLACES: case MD_MONEY: {
        int house = slot;
        if (house < 0 || house > 2)
            return false;
        if (t->model == MD_PLACES && t->var == V_EXP) { /* a digit card to its house */
            if (cv != house) {
                q->api->snprintf(l, sizeof l, "That digit lives in the %s house!", house_nm[0][cv % 3]);
                wrong(q, l);
                return false;
            }
            if (m->put[house]) /* (already there) */
                return false;
            m->put[house] = 1;
        } else {
            int mt = t->model == MD_MONEY ? 2 : t->var == V_CHART && m->x[1] ? 1 : 0;
            if (cv != house) {
                if (t->model == MD_MONEY)
                    q->api->snprintf(l, sizeof l, "That coin goes on the %s tray!", house_nm[2][cv % 3]);
                else if (t->var == V_BLOCKS)
                    q->api->snprintf(l, sizeof l, "A %s goes in the %s house!", block_nm[cv % 3], house_nm[0][cv % 3]);
                else
                    q->api->snprintf(l, sizeof l, "That disc goes in the %s house!", house_nm[mt][cv % 3]);
                wrong(q, l);
                return false;
            }
            if (m->put[house] >= s->need[house]) {
                q->api->snprintf(l, sizeof l, "The %s house has enough!", house_nm[mt][house]);
                wrong(q, l);
                return false;
            }
            m->put[house]++;
        }
        m->cnt++;
        mq_sfx(q, MODX_SFX_TAP, 0);
        for (int k = 0; k < 3; k++)
            if (m->put[k] < s->need[k])
                return true;
        next_step(q);
        return true;
    }
    case MD_GIVE:
        if (slot != 1) {
            wrong(q, "Give it to the big number: it wants to be round!");
            return false;
        }
        m->put[1]++, m->cnt++;
        mq_sfx(q, MODX_SFX_TAP, 0);
        if (m->put[1] >= s->need[1])
            next_step(q);
        return true;
    case MD_PEEL: {
        int32_t take = cv * m->x[1];
        if (take > m->pos) {
            q->api->snprintf(l, sizeof l, "%ld groups need %ld, but only %ld are left!", (long)cv, (long)take, (long)m->pos);
            wrong(q, l);
            return false;
        }
        m->pos -= take;
        if (m->nhop < 24)
            m->hop[m->nhop++] = (int16_t)cv;
        mq_sfx(q, MODX_SFX_POP, 0);
        if (m->pos == 0)
            next_step(q);
        return true;
    }
    case MD_FRIENDLY: {
        if (slot < 0 || slot > 2 || slot == chip)
            return false;
        if (!friendly_pair(m->x[chip], m->x[slot])) {
            q->api->snprintf(l, sizeof l, "%ld and %ld don't make a round number. Try another pair!", (long)m->x[chip], (long)m->x[slot]);
            wrong(q, l);
            return false;
        }
        m->put[0] = (uint8_t)(chip + 1), m->put[1] = (uint8_t)(slot + 1);
        mq_sfx(q, MODX_SFX_MERGE, 0);
        next_step(q);
        return true;
    }
    case MD_TAPE: {
        int want = s->nch == 1 ? (s->need[1] ? 1 : 0) : chip; /* (one chip: where it goes) */
        if (slot != want) {
            wrong(q, want == 1 ? "That one is all of it: it goes over the whole ribbon!"
                               : "That number is a part: it goes on a piece of the ribbon!");
            return false;
        }
        if (m->put[slot])
            return false;
        m->put[slot] = 1, m->cnt++;
        mq_sfx(q, MODX_SFX_TAP, 0);
        if (m->cnt >= s->want)
            next_step(q);
        return true;
    }
    case MD_STRIP: /* a piece of 1/b into the cake */
        if (m->put[0] >= s->need[0])
            return false;
        m->put[0]++;
        mq_sfx(q, MODX_SFX_TAP, 0);
        if (m->put[0] >= s->need[0])
            next_step(q);
        return true;
    case MD_PLOT: { /* the next ribbon's X onto its tick */
        int v = (int)((m->x[1] >> (4 * m->cnt)) & 15);
        if (slot != v) {
            q->api->snprintf(l, sizeof l, "This ribbon is %s inch: find %s on the line!", qtr[v % 5], qtr[v % 5]);
            wrong(q, l);
            return false;
        }
        m->put[slot]++, m->cnt++;
        mq_sfx(q, MODX_SFX_TAP, 0);
        if (m->cnt >= s->want)
            next_step(q);
        return true;
    }
    default: return false;
    }
}

/* a tap on part k of the model (a bag, a piece, a house, the pile, a row) */
static bool do_tap(mq_t *q, int k)
{
    mq_strat_t *m = SM;
    mq_sstep_t *s = cur(q);
    const strat_t *t = &tab[m->id];
    switch (t->model) {
    case MD_GROUPS:
        if (t->var == V_EQ || t->var == V_IDZ) { /* bag k filled */
            if (k < 0 || k >= m->x[0] || (m->taps[k / 8] >> (k % 8) & 1))
                k = -1;
            for (int i = 0; k < 0 && i < m->x[0]; i++) /* (a tap off a bag: the next empty one) */
                if (!(m->taps[i / 8] >> (i % 8) & 1))
                    k = i;
            if (k < 0)
                return false;
            m->taps[k / 8] |= (uint8_t)(1 << (k % 8));
        }
        break;
    case MD_STRIP: { /* piece k frosted (a tap off the pieces: the next plain one) */
        int d = (int)m->x[1];
        if (k < 0 || k >= d || (m->taps[k / 8] >> (k % 8) & 1))
            k = -1;
        for (int i = 0; k < 0 && i < d; i++)
            if (!(m->taps[i / 8] >> (i % 8) & 1))
                k = i;
        if (k < 0)
            return false;
        m->taps[k / 8] |= (uint8_t)(1 << (k % 8));
        break;
    }
    case MD_PLACES:
        if (t->var == V_PSUM) {
            if (k != s->a) {
                char l[64];
                q->api->snprintf(l, sizeof l, "Now the %s house!", house_nm[0][s->a % 3]);
                wrong(q, l);
                return false;
            }
            m->taps[k] = 1;
        } else
            m->taps[0]++; /* ten rods glued into a flat */
        break;
    case MD_ARRAY: m->taps[0] = 1; break; /* the row added or taken away */
    default: break;
    }
    m->cnt++;
    mq_sfx(q, MODX_SFX_POP, 0);
    if (m->cnt >= s->want)
        next_step(q);
    return true;
}

/* a slider's new value (commit: she let go, a key, a tap: it counts if it is right) */
static void do_slide(mq_t *q, int v, bool commit)
{
    mq_strat_t *m = SM;
    mq_sstep_t *s = cur(q);
    v = v < s->lo ? s->lo : v > s->hi ? s->hi : v;
    m->val = v;
    q->redraw = true;
    if (commit && v == s->want) {
        mq_sfx(q, MODX_SFX_CLICK, 0);
        next_step(q);
    }
}

static bool do_pick(mq_t *q, int i)
{
    mq_strat_t *m = SM;
    mq_sstep_t *s = cur(q);
    if (i < 0 || i >= s->nch)
        return false;
    if (s->ch[i] == s->want) {
        mq_sfx(q, MODX_SFX_POP, 0);
        next_step(q);
        return true;
    }
    m->picked = (int8_t)i;
    char l[110];
    q->api->snprintf(l, sizeof l, "Not quite: look at the picture again. %s", s->say);
    wrong(q, l);
    return true;
}

/* (tests, the console) the current step done with this value: a hop, a place (chip x 16 + place), a tap, a
 * slide, an answer */
bool mq_strat_act(mq_t *q, int value)
{
    mq_strat_t *m = SM;
    if (!m->on || m->phase != SP_DO || m->cur >= m->n)
        return false;
    mq_sstep_t *s = cur(q);
    switch (s->g) {
    case GE_JUMP: return do_hop(q, value);
    case GE_DRAG: return do_drop(q, value / 16, value % 16);
    case GE_TAP: return do_tap(q, value);
    case GE_SLIDE: do_slide(q, value, true); return true;
    case GE_PICK:
        for (int i = 0; i < s->nch; i++)
            if (s->ch[i] == value)
                return do_pick(q, i);
        for (int i = 0; i < s->nch; i++) /* (a wrong one) */
            if (s->ch[i] != s->want)
                return do_pick(q, i);
        return false;
    default: return false;
    }
}

/* the friend does the step (the teaching segment, the helper): the same moves, right ones */
static void auto_step(mq_t *q)
{
    mq_strat_t *m = SM;
    if (m->cur >= m->n)
        return;
    mq_sstep_t *s = cur(q);
    const strat_t *t = &tab[m->id];
    int guard = 0;
    switch (s->g) {
    case GE_JUMP:
        while (m->pos != s->want && guard++ < 40) { /* the biggest hop that fits */
            int32_t best = 0, left = s->want - m->pos;
            for (int i = 0; i < s->nch; i++)
                if ((left > 0 ? s->ch[i] > 0 && s->ch[i] <= left : s->ch[i] < 0 && s->ch[i] >= left) &&
                    iabs((int)s->ch[i]) > iabs((int)best))
                    best = s->ch[i];
            if (!best)
                break;
            if (m->nhop < 24)
                m->hop[m->nhop++] = (int16_t)best;
            m->pos += best;
        }
        break;
    case GE_DRAG:
        if (t->model == MD_PEEL) {
            while (m->pos > 0 && guard++ < 40)
                for (int i = 0; i < s->nch; i++)
                    if (s->ch[i] * m->x[1] <= m->pos) {
                        m->pos -= s->ch[i] * m->x[1];
                        if (m->nhop < 24)
                            m->hop[m->nhop++] = (int16_t)s->ch[i];
                        break;
                    }
        } else if (t->model == MD_FRIENDLY) {
            for (int i = 0; i < 3 && !m->put[0]; i++)
                for (int j = i + 1; j < 3; j++)
                    if (!m->put[0] && friendly_pair(m->x[i], m->x[j]))
                        m->put[0] = (uint8_t)(i + 1), m->put[1] = (uint8_t)(j + 1);
        } else if (t->model == MD_TAPE) {
            for (int k = 0; k < 2; k++)
                m->put[k] = (uint8_t)(s->need[k] ? 1 : 0);
        } else if (t->model == MD_PLOT) {
            for (int i = 0; i < 10; i++)
                m->put[i] = (uint8_t)s->need[i];
        } else
            for (int k = 0; k < 10; k++)
                m->put[k] = (uint8_t)(m->put[k] > s->need[k] ? m->put[k] : s->need[k]);
        break;
    case GE_TAP:
        if ((t->model == MD_GROUPS && (t->var == V_EQ || t->var == V_IDZ)) || t->model == MD_STRIP)
            for (int i = 0; i < m->x[0]; i++)
                m->taps[i / 8] |= (uint8_t)(1 << (i % 8));
        else if (t->model == MD_PLACES && t->var == V_PSUM)
            m->taps[s->a] = 1;
        else if (t->model == MD_PLACES)
            m->taps[0] = (uint8_t)s->want;
        else if (t->model == MD_ARRAY)
            m->taps[0] = 1;
        break;
    case GE_SLIDE: m->val = s->want; break;
    case GE_PICK:
        if (m->phase == SP_TEACH) { /* the friend says the answer of the example */
            char a[24], l[40];
            fmt(q, s->fmt, s->want, a, sizeof a);
            q->api->snprintf(l, sizeof l, "It's %s!", a);
            say(q, l);
        }
        break;
    default: break;
    }
    m->cur++;
    m->cnt = 0;
    if (m->cur < m->n && cur(q)->g == GE_SLIDE)
        m->val = cur(q)->lo;
}

/* the teaching segment's next beat: an opening line, a step shown, "your turn" */
static void teach_next(mq_t *q)
{
    mq_strat_t *m = SM;
    char l[110];
    if (m->phase != SP_TEACH)
        return;
    int k0 = m->beat - m->intro_n;
    if (k0 >= 0 && k0 < m->n && m->cur <= k0) /* (the step shown: done, if the hand had not finished yet) */
        auto_step(q);
    m->beat++, m->t = mq_mark(q);
    int b = m->beat;
    if (b < m->intro_n) {
        expand(q, tab[m->id].intro[b], tab[m->id].who, l, sizeof l);
        say(q, l);
        return;
    }
    int k = b - m->intro_n; /* the step being shown */
    if (k < m->n) {
        say(q, m->st[k].say);
        return;
    }
    if (m->mode == SM_HELP) { /* shown: a second way sometimes, then her turn */
        int32_t x2[6];
        int s2 = mq_strat_fit(q, &q->ex, 1, x2);
        if (!m->second && s2 >= 0 && s2 != m->id && rec(q)->strat_help[m->id] % 2 == 0) {
            m->second = 1, m->id = (uint8_t)s2;
            for (int i = 0; i < 6; i++)
                m->ex[i] = x2[i];
            bump(&rec(q)->strat_help[s2]);
            plan(q, s2, x2);
            m->beat = 0, m->intro_n = 1;
            char nm[40];
            mq_strat_name(q, s2, nm, sizeof nm);
            q->api->snprintf(l, sizeof l, "Here's another way: %s!", nm);
            say(q, l);
            return;
        }
        m->phase = SP_DONE, m->t = mq_mark(q);
        return;
    }
    if (m->mode == SM_LESSON) {
        say(q, "Now your own one: you can do it!");
        m->phase = SP_DONE, m->t = mq_mark(q);
        return;
    }
    if (m->mode == SM_SHOW) {
        say(q, "Now you know it! Look for it in the games.");
        m->phase = SP_DONE, m->t = mq_mark(q);
        return;
    }
    say(q, "Your turn: try one!");
    do_begin(q);
}

void mq_strat_frame(mq_t *q)
{
    mq_strat_t *m = SM;
    if (!m->on)
        return;
    uint32_t el = q->anim_t + 1 - m->t;
    if (m->phase == SP_TEACH) {
        int k = m->beat - m->intro_n;
        bool pick = k >= 0 && k < m->n && m->st[k].g == GE_PICK;
        if (k >= 0 && k < m->n && m->cur == k && el >= HAND_MS && !pick) /* the hand did it: the model moves */
            auto_step(q);
        if (pick && m->cur == k && el >= HAND_MS + 600u)
            auto_step(q);
        uint32_t need = BEAT_MS + (pick ? 600u : 0u), rd = (uint32_t)mq_read_ms(q, m->say); /* (her reading time) */
        if (rd > need)
            need = rd;
        if (el >= need) {
            mq_read_note(q, el, (int)need, false);
            teach_next(q);
        }
        q->redraw = true;
    } else if (m->phase == SP_DONE && el >= (uint32_t)imax((int)DONE_MS, mq_read_ms(q, m->say))) {
        mq_read_note(q, el, imax((int)DONE_MS, mq_read_ms(q, m->say)), false);
        if (m->mode == SM_HELP)
            mq_helper_next(q); /* (shown: the helper's moment goes on, to her own order) */
        else
            done_now(q);
    } else if (m->phase == SP_DO && (el < 800 || (m->hint && q->anim_t - m->hint_t < 1800)))
        q->redraw = true;
}

/* ------------------------------------------------------------------ geometry */
enum { W_BACK = SW_BACK, W_NEXT = SW_NEXT, W_TRY = SW_TRY, W_HINT = SW_HINT, W_CHIP = SW_CHIP, W_SLOT = SW_SLOT, W_AREA = SW_AREA,
       W_SLIDE = SW_SLIDE, W_PICK = SW_PICK };
typedef struct {
    int16_t x, y, w, h;
} rect_t;
static rect_t R4(int x, int y, int w, int h)
{
    rect_t r = {(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h};
    return r;
}
static const strat_t *T(mq_t *q) { return &tab[q->strat.id]; }
/* the index of the first step of gesture g (and slider look), -1 none */
static int step_of(mq_t *q, int g, int look)
{
    for (int i = 0; i < q->strat.n; i++)
        if (q->strat.st[i].g == g && (look < 0 || q->strat.st[i].slide == look))
            return i;
    return -1;
}
static bool past(mq_t *q, int i) { return i >= 0 && q->strat.cur > i; }
/* a slider's value as the picture shows it: set (its step done), being moved (now), not yet (lo) */
static int slid(mq_t *q, int look, int dflt)
{
    int i = step_of(q, GE_SLIDE, look);
    if (i < 0)
        return dflt;
    if (past(q, i))
        return (int)q->strat.st[i].want;
    if (q->strat.cur == i)
        return (int)q->strat.val;
    return q->strat.st[i].lo;
}
static int nplaces(mq_t *q)
{
    const strat_t *t = T(q);
    const int32_t *x = q->strat.x;
    if (t->model == MD_PLACES && t->var == V_PSUM)
        return imax(ndig(x[0]), ndig(x[1]));
    if (t->model == MD_PLACES && t->var == V_EXP)
        return ndig(x[0]);
    return 3;
}
/* house k (0: ones, at the right) */
static rect_t house(mq_t *q, int k)
{
    int np = nplaces(q), w = (MX1 - MX0) / np;
    return R4(MX0 + (np - 1 - k) * w + 2, MY0 + 2, w - 4, MY1 - MY0 - 4);
}
/* ARRAY: the cell size and the grid's corner */
static int cell(int rows, int cols, int wmax, int hmax)
{
    int c = imin(wmax / imax(1, cols), hmax / imax(1, rows));
    return c < 5 ? 5 : c > 14 ? 14 : c;
}
/* JUMPS: the path's ends and a value's place on it */
#define LY (MY0 + 80)
#define PX0 (MX0 + 18)
#define PX1 (MX1 - 22)
static bool scaled(mq_t *q) { return T(q)->model == MD_JUMPS && (T(q)->var == V_FRAC || T(q)->var == V_BENCH); }
static int path_x(mq_t *q, int32_t v)
{
    mq_strat_t *m = SM;
    if (scaled(q)) /* 0 .. 1 in steps of 1/x[1] */
        return PX0 + (int)((v * (PX1 - PX0)) / imax(1, (int)m->x[1]));
    /* the open path: each hop step a stretch of its own (big hops wider), in the order she walks */
    int32_t w[SS_MAX], tot = 0, start = 0;
    int ns = 0, idx[SS_MAX];
    for (int i = 0; i < m->n; i++)
        if (m->st[i].g == GE_JUMP) {
            int32_t d = iabs((int)(m->st[i].want - m->st[i].a));
            if (!ns)
                start = m->st[i].a;
            w[ns] = d >= 100 ? 100 : d >= 10 ? 80 : 56, idx[ns++] = i;
            tot += w[ns - 1];
        }
    if (!ns || !tot)
        return PX0;
    int32_t dir = m->st[idx[ns - 1]].want >= start ? 1 : -1, acc = 0;
    int x0 = dir > 0 ? PX0 : PX1, span = PX1 - PX0;
    for (int k = 0; k < ns; k++) {
        const mq_sstep_t *s = &m->st[idx[k]];
        int32_t lo = s->a, hi = s->want, len = (hi - lo) * dir;
        int32_t t = (v - lo) * dir;
        if (t <= len || k == ns - 1) {
            if (t < 0 && k == 0)
                t = 0;
            int32_t off = acc * span / tot + (len ? t * w[k] * span / tot / len : 0);
            if (off > span)
                off = span;
            return x0 + (int)(dir * off);
        }
        acc += w[k];
    }
    return x0;
}
static rect_t slot_rect(mq_t *q, int k)
{
    mq_strat_t *m = SM;
    const strat_t *t = T(q);
    switch (t->model) {
    case MD_JUMPS: return R4(PX0 - 6, MY0 + 30, PX1 - PX0 + 12, LY - MY0 - 22);
    case MD_PLACES: case MD_MONEY: return house(q, k);
    case MD_GIVE: return k ? R4(36, MY0 + 30, 96, 70) : R4(188, MY0 + 30, 96, 70);
    case MD_PEEL: return R4(MX0 + 8, MY0 + 6, 196, 44);
    case MD_FRIENDLY: return R4(28 + k * 98, MY0 + 34, 70, 40);
    case MD_TAPE: {
        int32_t a = m->x[0], b = m->x[1];
        int W = 260, X = 30;
        if (t->var == V_PPW)
            return k ? R4(X, MY0 + 6, W, 22) : R4(X, MY0 + 40, (int)(W * a / imax(1, (int)b)), 28);
        if (t->var == V_CMP) { /* (the friends' names at the left) */
            int32_t big = m->x[2] ? a * b : a + b;
            X = 66, W = 236;
            int wa = (int)(W * a / imax(1, (int)big));
            return k ? R4(X + wa, MY0 + 60, W - wa, 26) : R4(X, MY0 + 20, wa, 26);
        }
        return k ? R4(X, MY0 + 6, W, 22) : R4(X, MY0 + 40, W / imax(1, (int)a), 28);
    }
    case MD_STRIP: return R4(30, MY0 + 26, 260, 40);
    case MD_PLOT: {
        int tx = 44 + k * (232 / 4);
        return R4(tx - 17, MY0 + 22, 34, 66);
    }
    default: return R4(MX0, MY0, MX1 - MX0, MY1 - MY0);
    }
}
static int nslots(mq_t *q)
{
    switch (T(q)->model) {
    case MD_JUMPS: case MD_PEEL: case MD_STRIP: return 1;
    case MD_PLACES: return nplaces(q);
    case MD_MONEY: case MD_FRIENDLY: return 3;
    case MD_GIVE: case MD_TAPE: return 2;
    case MD_PLOT: return 5;
    default: return 0;
    }
}
/* a slider's track (WG_SLIDER: lo at its left / top) */
static rect_t slider_rect(mq_t *q, const mq_sstep_t *s, bool *vert)
{
    mq_strat_t *m = SM;
    *vert = false;
    switch (s->slide) {
    case SL_ROWS: *vert = true; return R4(MX1 - 40, MY0 + 4, 28, MY1 - MY0 - 8);
    case SL_TURN: return R4(60, MY1 - 16, 150, 14);
    case SL_MERGE: return R4(60, MY1 - 16, 150, 14);
    case SL_CUT:
        if (T(q)->model == MD_AREA)
            return R4(30, MY0 + 2, 180, 14);
        else {
            int c = (int)m->x[1], r = (int)m->x[0], cs = cell(r, c, 170, 78);
            int gx = MX0 + 16, w = cs * imax(1, c - 2);
            return R4(gx + cs, MY0 + 8 + r * cs + 4, w > 0 ? w : cs, 14);
        }
    case SL_VCUT: *vert = true; return R4(MX0, MY0 + 18, 16, 74);
    case SL_PIECES: return R4(40, MY1 - 18, 220, 14);
    case SL_RECUT: return R4(60, MY1 - 18, 160, 14);
    case SL_BREAK: return R4(60, MY0 + 4, 200, 14);
    default: return R4(40, MY1 - 18, 220, 14);
    }
}

/* ------------------------------------------------------------------ drawing: pieces with their vector twins */
static int spr(mq_t *q, const char *id, int x, int y, int w, int h) { return mq_hr_sprite(q, id, x, y, w, h, 0, 0); }
#define END(hr) \
    do {        \
        if (hr) \
            mq_hr_end(q); \
    } while (0)
static void cube(mq_t *q, int x, int y, int s, uint16_t c)
{
    int hr = spr(q, c == Q_YELLOW ? "model/cube_yellow" : "model/cube", x, y, s, s);
    mq_fill(q, x, y, s, s, c);
    mq_rect(q, x, y, s, s, Q_BROWN);
    END(hr);
}
static void rod(mq_t *q, int x, int y, int w, int h)
{
    int hr = spr(q, "model/rod", x, y, w, h);
    mq_fill(q, x, y, w, h, Q_MINT);
    mq_rect(q, x, y, w, h, Q_GREEN);
    for (int i = 1; i < 10; i++)
        mq_fill(q, x, y + h * i / 10, w, 1, Q_GREEN);
    END(hr);
}
static void flat(mq_t *q, int x, int y, int s)
{
    int hr = spr(q, "model/flat", x, y, s, s);
    mq_fill(q, x, y, s, s, Q_SKY);
    mq_rect(q, x, y, s, s, Q_BLUE);
    for (int i = 1; i < 5; i++)
        mq_fill(q, x + s * i / 5, y, 1, s, Q_BLUE), mq_fill(q, x, y + s * i / 5, s, 1, Q_BLUE);
    END(hr);
}
static void flower(mq_t *q, int cx, int cy, int r, uint16_t c)
{
    int hr = spr(q, c == Q_GREY ? "model/bud" : c == Q_LAV ? "model/flower_lav" : c == Q_YELLOW ? "model/flower_yellow" : "model/flower",
                 cx - r, cy - r, 2 * r + 1, 2 * r + 1);
    mq_circle(q, cx, cy, r, c);
    if (r >= 3)
        mq_circle(q, cx, cy, r / 3, c == Q_GREY ? Q_WHITE : Q_YELLOW);
    END(hr);
}
static void treat(mq_t *q, int cx, int cy, int r)
{
    int hr = spr(q, "model/treat", cx - r, cy - r, 2 * r + 1, 2 * r + 1);
    mq_circle(q, cx, cy, r, Q_HOTPINK);
    END(hr);
}
static void card(mq_t *q, int x, int y, int w, int h, const char *s, uint16_t col, uint16_t ink)
{
    int hr = spr(q, col == Q_YELLOW ? "model/card_yellow" : col == Q_WHITE ? "model/card_white" : col == Q_PINK ? "model/card_pink" : "model/card",
                 x, y, w, h);
    mq_rrect(q, x, y, w, h, 5, col);
    mq_rect(q, x, y, w, h, Q_DIM);
    END(hr);
    if (s && s[0])
        mq_text_fit(q, q->api->font_small, x + w / 2, y + (h - 12) / 2, w - 4, s, ink, true);
}
static const char *ribbon_id(uint16_t c)
{
    return c == Q_PINK ? "model/ribbon_pink" : c == Q_SKY ? "model/ribbon_sky" : c == Q_LAV ? "model/ribbon_lav" : c == Q_YELLOW
           ? "model/ribbon_yellow" : c == Q_MINT ? "model/ribbon_mint" : "model/ribbon_peach";
}
static void ribbon(mq_t *q, int x, int y, int w, int h, uint16_t c)
{
    int hr = spr(q, ribbon_id(c), x, y, w, h);
    mq_fill(q, x, y, w, h, c);
    mq_rect(q, x, y, w, h, Q_DIM);
    END(hr);
}
static void xmark(mq_t *q, int cx, int cy)
{
    int hr = spr(q, "model/xmark", cx - 5, cy - 5, 10, 10);
    mq_line(q, cx - 4, cy - 4, cx + 4, cy + 4, Q_HOTPINK);
    mq_line(q, cx - 4, cy + 4, cx + 4, cy - 4, Q_HOTPINK);
    END(hr);
}
static void arc(mq_t *q, int x0, int x1, int y, uint16_t c)
{
    if (x1 < x0) {
        int t = x0;
        x0 = x1, x1 = t;
    }
    int w = imax(4, x1 - x0), h = imin(26, w / 2 + 4);
    int hr = spr(q, "model/hop", x0, y - h, w, h);
    int px = x0, py = y;
    for (int i = 1; i <= 12; i++) { /* a half ellipse */
        int a = 180 - i * 15, nx = x0 + w / 2 + mq_icos(a) * (w / 2) / 1024, ny = y - mq_isin(a) * h / 1024;
        mq_line(q, px, py, nx, ny, c);
        px = nx, py = ny;
    }
    END(hr);
}
static void handle(mq_t *q, int cx, int cy, bool active)
{
    int hr = spr(q, "model/handle", cx - 8, cy - 8, 16, 16);
    mq_circle(q, cx, cy, 8, active ? Q_YELLOW : Q_PEACH);
    mq_ring(q, cx, cy, 8, Q_INK);
    END(hr);
}
static void label(mq_t *q, int cx, int y, const char *s, uint16_t c) { mq_text_c(q, q->api->font_small, cx, y, s, c); }

/* ------------------------------------------------------------------ the models */
static void draw_jumps(mq_t *q)
{
    mq_strat_t *m = SM;
    const strat_t *t = T(q);
    const modx_api_t *api = q->api;
    char b[20];
    bool sc = scaled(q);
    /* the path */
    mq_fill(q, PX0 - 6, LY, PX1 - PX0 + 12, 2, Q_INK);
    if (sc) {
        int d = (int)m->x[1];
        for (int k = 0; k <= d; k++) {
            int x = path_x(q, k);
            bool big = k == 0 || k == d || (t->var == V_BENCH && 2 * k == d);
            mq_fill(q, x, LY - (big ? 6 : 3), 1, big ? 12 : 6, Q_INK);
        }
        label(q, path_x(q, 0), LY + 6, "0", Q_INK);
        label(q, path_x(q, d), LY + 6, "1", Q_INK);
        if (t->var == V_BENCH) {
            int hx = (PX0 + PX1) / 2;
            mq_fill(q, hx, LY - 8, 1, 16, Q_PURPLE);
            label(q, hx, LY + 6, "1/2", Q_PURPLE);
        }
    }
    int32_t start = sc ? 0 : m->n && m->st[0].g == GE_JUMP ? m->st[0].a : m->n > 1 ? m->st[1].a : 0;
    int js = step_of(q, GE_JUMP, -1);
    if (js >= 0)
        start = m->st[js].a;
    /* the friend standing where they are, the end flag */
    int32_t end = start;
    for (int i = 0; i < m->n; i++)
        if (m->st[i].g == GE_JUMP)
            end = m->st[i].want;
    if (!sc) {
        fmt(q, t->var == V_TIME ? FM_TIME : FM_NUM, start, b, sizeof b);
        mq_fill(q, path_x(q, start), LY - 5, 2, 12, Q_INK);
        label(q, path_x(q, start), LY + 6, b, Q_INK);
        bool given = t->var == V_UP || t->var == V_TIME; /* (the end is in the story) */
        int ex = path_x(q, end);
        mq_fill(q, ex, LY - 5, 2, 12, Q_PURPLE);
        if (m->pos == end || given) {
            fmt(q, t->var == V_TIME ? FM_TIME : FM_NUM, end, b, sizeof b);
            label(q, ex, LY + 6, b, m->pos == end ? Q_GREEN : Q_PURPLE);
        } else
            label(q, ex, LY + 6, "?", Q_PURPLE);
    }
    /* the hops so far, step by step: their arcs, each step's total over them, where each step landed */
    int32_t p = start, sum = 0;
    int hi = 0;
    for (int i = 0; i < m->n; i++) {
        const mq_sstep_t *s = &m->st[i];
        if (s->g != GE_JUMP)
            continue;
        int32_t tot = 0;
        int x0 = path_x(q, p);
        while (hi < m->nhop && (s->want >= s->a ? p + m->hop[hi] <= s->want : p + m->hop[hi] >= s->want) && p != s->want) {
            arc(q, path_x(q, p), path_x(q, p + m->hop[hi]), LY - 1, Q_HOTPINK);
            p += m->hop[hi], tot += m->hop[hi], sum += m->hop[hi], hi++;
            m->units[0]++;
        }
        if (tot) {
            int x1 = path_x(q, p);
            hop_word(q, tot, b, sizeof b);
            if (sc)
                api->snprintf(b, sizeof b, "+%ld/%ld", (long)tot, (long)m->x[1]);
            label(q, (x0 + x1) / 2, LY - imin(26, iabs(x1 - x0) / 2 + 4) - 14, b, Q_HOTPINK);
            if (!sc && p != end) { /* a landing on the way */
                fmt(q, t->var == V_TIME ? FM_TIME : FM_NUM, p, b, sizeof b);
                mq_fill(q, x1, LY - 3, 1, 8, Q_DIM);
                label(q, x1, LY + 6 + (i % 2) * 11, b, Q_DIM);
            }
        }
    }
    m->units[2] = (uint16_t)(sum < 0 ? -sum : sum);
    int fx = path_x(q, m->pos);
    mq_cast(q, t->who, fx - 9, LY - 20 - (m->phase == SP_DO && q->anim_t - m->step_t < 300 ? 4 : 0), 18, AN_HAPPY);
    /* make a ten: the number broken in two */
    int bs = step_of(q, GE_SLIDE, SL_BREAK);
    if (bs >= 0) {
        int32_t bb = m->x[1], v = slid(q, SL_BREAK, 0);
        int X = 60, W = 200, cut = X + (int)(W * v / imax(1, (int)bb));
        if (cut > X)
            ribbon(q, X, MY0 + 18, cut - X, 14, Q_YELLOW);
        if (X + W > cut)
            ribbon(q, cut, MY0 + 18, X + W - cut, 14, Q_SKY);
        mq_fill(q, cut, MY0 + 14, 2, 22, Q_HOTPINK);
        if (v > 0) {
            api->snprintf(b, sizeof b, "%ld", (long)v);
            label(q, (X + cut) / 2, MY0 + 19, b, Q_INK);
        }
        api->snprintf(b, sizeof b, "%ld", (long)(bb - v));
        label(q, (cut + X + W) / 2, MY0 + 19, b, Q_INK);
        if (m->cur == bs && m->phase == SP_DO)
            handle(q, cut, MY0 + 25, true);
    }
}

static void blocks(mq_t *q, int k, int n, int x, int y, int w)
{
    for (int i = 0; i < n; i++) {
        if (k == 2)
            flat(q, x + (i % 5) * 17, y + (i / 5) * 17, 14);
        else if (k == 1)
            rod(q, x + (i % 10) * imin(7, (w - 6) / 10) + (i / 10) * 3, y + (i / 10) * 4, 4, 22);
        else
            cube(q, x + (i % 5) * 7, y + (i / 5) * 7, 5, Q_PINK);
    }
}
static void draw_places(mq_t *q)
{
    mq_strat_t *m = SM;
    const strat_t *t = T(q);
    const modx_api_t *api = q->api;
    char b[16];
    int np = nplaces(q), mt = t->model == MD_MONEY ? 2 : t->var == V_CHART && m->x[1] ? 1 : 0;
    int32_t val = 0;
    for (int k = 0; k < np; k++) {
        rect_t r = house(q, k);
        int hr = spr(q, "model/house", r.x, r.y, r.w, r.h);
        mq_rrect(q, r.x, r.y, r.w, r.h, 8, k % 2 ? RGB(255, 244, 250) : RGB(244, 250, 255));
        mq_rect(q, r.x, r.y, r.w, r.h, Q_LAV);
        END(hr);
        label(q, r.x + r.w / 2, r.y + 2, house_nm[mt][k], Q_PURPLE);
        int cx = r.x + 6, cy = r.y + 18;
        if (t->model == MD_MONEY) {
            static const uint8_t dn[3] = {DN_PENNY, DN_DIME, DN_DOLLAR};
            for (int i = 0; i < m->put[k]; i++)
                if (k == 2) /* (bills: two to a row) */
                    mq_money_draw(q, dn[k], cx + 20 + (i % 2) * 42, cy + 12 + (i / 2) * 24, 2);
                else
                    mq_money_draw(q, dn[k], cx + 12 + (i % 4) * 22, cy + 12 + (i / 4) * 24, 2);
            val += m->put[k] * (k == 0 ? 1 : k == 1 ? 10 : 100);
            api->snprintf(b, sizeof b, "%d", m->put[k]);
            label(q, r.x + r.w / 2, r.y + r.h - 14, b, Q_INK);
            continue;
        }
        switch (t->var) {
        case V_PSUM: {
            int da = digit(m->x[0], k), db = digit(m->x[1], k);
            if (m->taps[k])
                blocks(q, k, da + db, cx, cy, r.w - 12);
            else {
                blocks(q, k, da, cx, cy, r.w - 12);
                mq_fill(q, r.x + 4, cy + 34, r.w - 8, 1, Q_GREY);
                blocks(q, k, db, cx, cy + 38, r.w - 12);
            }
            val += (da + db) * pow10(k);
            for (int i = 0; i < m->n; i++)
                if (m->st[i].g == GE_PICK && m->st[i].a == k && i + 1 < m->n && past(q, i)) {
                    api->snprintf(b, sizeof b, "%ld", (long)m->st[i].want);
                    card(q, r.x + r.w / 2 - 22, r.y + r.h - 18, 44, 15, b, Q_YELLOW, Q_INK);
                }
            break;
        }
        case V_EXP:
            if (m->put[k]) {
                int32_t v = digit(m->x[0], k) * pow10(k);
                api->snprintf(b, sizeof b, "%ld", (long)v);
                card(q, r.x + 6, cy + 20, r.w - 12, 24, b, Q_YELLOW, Q_INK);
                val += v;
            } else {
                mq_rect(q, r.x + 6, cy + 20, r.w - 12, 24, Q_GREY);
            }
            break;
        case V_CHART: {
            static const char dv[2][3][6] = {{"1", "10", "100"}, {".01", ".1", "1"}};
            for (int i = 0; i < m->put[k]; i++) {
                int dx = cx + 9 + (i % 5) * 17, dy = cy + 9 + (i / 5) * 18;
                int hr2 = spr(q, k == 0 ? "model/disc_pink" : k == 1 ? "model/disc_mint" : "model/disc_sky", dx - 8, dy - 8, 17, 17);
                mq_circle(q, dx, dy, 8, k == 0 ? Q_PINK : k == 1 ? Q_MINT : Q_SKY);
                END(hr2);
                label(q, dx, dy - 5, dv[m->x[1] ? 1 : 0][k], Q_INK);
            }
            val += m->put[k] * pow10(k);
            api->snprintf(b, sizeof b, "%d", m->put[k]);
            label(q, r.x + r.w / 2, r.y + r.h - 14, b, Q_INK);
            break;
        }
        case V_BLOCKS: {
            int n = m->put[k], g = m->taps[0];
            if (k == 2)
                n += g;
            if (k == 1)
                n -= 10 * g;
            blocks(q, k, n < 0 ? 0 : n, cx, cy, r.w - 12);
            val += (n < 0 ? 0 : n) * pow10(k);
            api->snprintf(b, sizeof b, "%d", n < 0 ? 0 : n);
            label(q, r.x + r.w / 2, r.y + r.h - 14, b, Q_INK);
            break;
        }
        default: break;
        }
    }
    m->units[0] = (uint16_t)val;
}

static void draw_give(mq_t *q)
{
    mq_strat_t *m = SM;
    char b[16];
    int32_t mv = m->put[1], va = m->x[0] + mv, vb = m->x[1] - mv;
    for (int k = 0; k < 2; k++) {
        rect_t r = slot_rect(q, k);
        int hr = spr(q, "model/jar", r.x, r.y, r.w, r.h);
        mq_rrect(q, r.x, r.y, r.w, r.h, 12, RGB(236, 248, 255));
        mq_rect(q, r.x, r.y, r.w, r.h, Q_SKY);
        END(hr);
        q->api->snprintf(b, sizeof b, "%ld", (long)(k ? va : vb));
        mq_text_c(q, q->api->font_large, r.x + r.w / 2, r.y - 26, b, k ? Q_PURPLE : Q_INK);
    }
    rect_t ra = slot_rect(q, 1);
    for (int i = 0; i < mv; i++)
        cube(q, ra.x + 10 + i * 9, ra.y + 10, 7, Q_YELLOW);
    mq_text_c(q, q->api->font_medium, 160, MY0 + 56, "+", Q_INK);
    m->units[0] = (uint16_t)(va + vb);
}

static void draw_groups(mq_t *q)
{
    mq_strat_t *m = SM;
    const strat_t *t = T(q);
    char b[16];
    int32_t a = m->x[0], bb = m->x[1];
    int tot = 0;
    if (t->var == V_EQ || t->var == V_IDZ) {
        int g = (int)a, bw = imin(60, 290 / imax(1, g));
        if (!g)
            label(q, 160, MY0 + 40, "No bags at all!", Q_DIM);
        for (int i = 0; i < g; i++) {
            int x = 15 + i * bw + (290 - g * bw) / 2, y = MY0 + 18;
            int hr = spr(q, "model/bag", x + 2, y, bw - 4, 56);
            mq_rrect(q, x + 2, y + 6, bw - 4, 50, 10, Q_PEACH);
            mq_fill(q, x + bw / 2 - 5, y, 10, 8, Q_BROWN);
            END(hr);
            if (m->taps[i / 8] >> (i % 8) & 1) {
                for (int j = 0; j < bb; j++)
                    treat(q, x + 9 + (j % 3) * imin(12, (bw - 14) / 3), y + 14 + (j / 3) * 11, 4), tot++;
                q->api->snprintf(b, sizeof b, "%d", (i + 1) * (int)bb);
                label(q, x + bw / 2, y + 60, b, Q_PURPLE);
            }
        }
    } else { /* the pile at the left, plates or bags to the right */
        bool deal = t->var == V_DEAL;
        int n = (int)(deal ? a : bb), each = (int)(deal ? bb : a), done = m->cnt;
        if (m->cur > 0 || m->phase == SP_DONE)
            done = deal ? each : n;
        int left = (int)(a * bb) - (deal ? done * (int)a : done * (int)a);
        int hr = spr(q, "model/pile", MX0 + 4, MY0 + 10, 64, 80);
        mq_rrect(q, MX0 + 4, MY0 + 10, 64, 80, 10, RGB(255, 240, 230));
        END(hr);
        for (int i = 0; i < left && i < 90; i++)
            treat(q, MX0 + 10 + (i % 9) * 6, MY0 + 16 + (i / 9) * 7, 2);
        q->api->snprintf(b, sizeof b, "%d", left);
        label(q, MX0 + 36, MY0 + 92, b, Q_INK);
        int bw = imin(60, 230 / imax(1, n));
        for (int i = 0; i < n; i++) {
            int x = 76 + i * bw, y = MY0 + 22;
            int hr2 = spr(q, deal ? "model/plate" : "model/bag", x + 2, y, bw - 4, 52);
            if (deal)
                mq_circle(q, x + bw / 2, y + 26, imin(24, bw / 2 - 2), Q_WHITE), mq_ring(q, x + bw / 2, y + 26, imin(24, bw / 2 - 2), Q_GREY);
            else
                mq_rrect(q, x + 2, y + 4, bw - 4, 48, 10, Q_PEACH);
            END(hr2);
            int k = deal ? done : (i < done ? each : 0);
            for (int j = 0; j < k; j++)
                treat(q, x + 10 + (j % 3) * imin(11, (bw - 14) / 3), y + 12 + (j / 3) * 10, 4), tot++;
        }
    }
    m->units[0] = (uint16_t)tot;
}

static void draw_array(mq_t *q)
{
    mq_strat_t *m = SM;
    const strat_t *t = T(q);
    const modx_api_t *api = q->api;
    char b[40];
    int32_t a = m->x[0], bb = m->x[1], c = m->x[2];
    int dots = 0, dots2 = 0;
    switch (t->var) {
    case V_BUILD: case V_GROW: {
        int cols = (int)(t->var == V_BUILD ? bb : a), want = (int)(t->var == V_BUILD ? a : bb);
        int rows = slid(q, SL_ROWS, 0), cs = cell(10, cols, 190, 98), gx = MX0 + 20, gy = MY0 + 4;
        for (int r = 0; r < rows; r++) {
            for (int k = 0; k < cols; k++)
                flower(q, gx + k * cs + cs / 2, gy + r * cs + cs / 2, cs / 2 - 1, Q_PINK), dots++;
            api->snprintf(b, sizeof b, "%d", (r + 1) * cols);
            mq_text(q, api->font_small, gx + cols * cs + 6, gy + r * cs + cs / 2 - 6, b, r + 1 == want ? Q_GREEN : Q_DIM);
        }
        (void)want;
        break;
    }
    case V_TURN: { /* the same rectangle in both turns: hers (r rows of k) turns a quarter round onto the other */
        int r = (int)a, k = (int)bb, cs = cell(imax(r, k), imax(r, k), 84, 52);
        int cx = 80, cy = MY0 + 40, rx = 232, si = step_of(q, GE_SLIDE, SL_TURN);
        bool turned = past(q, si);
        int ang = slid(q, SL_TURN, 0), slide = 0;
        uint32_t el = q->anim_t - m->step_t;
        if (turned) { /* (shown by the friend: it turns smoothly; then it slides over the other) */
            int tturn = m->phase == SP_TEACH && m->cur == si + 1 ? 600 : 0;
            ang = tturn && el < (uint32_t)tturn ? (int)(el * 90 / (uint32_t)tturn) : 90;
            slide = el < (uint32_t)tturn ? 0 : el >= (uint32_t)tturn + 500u ? 1000 : (int)((el - (uint32_t)tturn) * 2);
        }
        /* the other one: k rows of r, with its rectangle */
        int ow = r * cs, oh = k * cs;
        mq_rect(q, rx - ow / 2 - 2, cy - oh / 2 - 2, ow + 4, oh + 4, Q_PURPLE);
        for (int i = 0; i < k; i++)
            for (int j = 0; j < r; j++) {
                if (slide < 1000) /* (covered exactly once hers is over it) */
                    flower(q, rx - ow / 2 + j * cs + cs / 2, cy - oh / 2 + i * cs + cs / 2, cs / 2 - 1, Q_LAV);
                dots2++;
            }
        /* a ghost of hers where it started */
        int hw = k * cs, hh = r * cs;
        if (ang > 0)
            mq_rect(q, cx - hw / 2 - 2, cy - hh / 2 - 2, hw + 4, hh + 4, Q_GREY);
        /* hers, turned by ang round its middle, then slid over the other */
        int sx = cx + (rx - cx) * slide / 1000, ca = mq_icos(ang), sa = mq_isin(ang);
        int16_t px[4], py[4];
        static const int8_t sgx[4] = {-1, 1, 1, -1}, sgy[4] = {-1, -1, 1, 1};
        for (int i = 0; i < 4; i++) {
            int dx = sgx[i] * (hw / 2 + 2), dy = sgy[i] * (hh / 2 + 2);
            px[i] = (int16_t)(sx + (dx * ca - dy * sa) / 1024), py[i] = (int16_t)(cy + (dx * sa + dy * ca) / 1024);
        }
        for (int i = 0; i < 4; i++)
            mq_line(q, px[i], py[i], px[(i + 1) % 4], py[(i + 1) % 4], Q_HOTPINK);
        for (int i = 0; i < r; i++)
            for (int j = 0; j < k; j++) {
                int dx = -hw / 2 + j * cs + cs / 2, dy = -hh / 2 + i * cs + cs / 2;
                flower(q, sx + (dx * ca - dy * sa) / 1024, cy + (dx * sa + dy * ca) / 1024, cs / 2 - 1, Q_PINK), dots++;
            }
        api->snprintf(b, sizeof b, "%ld rows of %ld", (long)a, (long)bb);
        label(q, cx, MY1 - 40, b, Q_HOTPINK);
        api->snprintf(b, sizeof b, "%ld " TIMES " %ld", (long)a, (long)bb);
        label(q, cx, MY1 - 28, b, Q_INK);
        api->snprintf(b, sizeof b, "%ld rows of %ld", (long)bb, (long)a);
        label(q, rx, MY1 - 40, b, Q_PURPLE);
        api->snprintf(b, sizeof b, "%ld " TIMES " %ld", (long)bb, (long)a);
        label(q, rx, MY1 - 28, b, Q_INK);
        if (turned && slide >= 1000)
            label(q, rx, MY0, past(q, si + 1) ? "the same!" : "", Q_GREEN);
        break;
    }
    case V_ASSOC: {
        int g = (int)c, cols = (int)bb, rows = (int)a, gap = (4 - slid(q, SL_MERGE, 0)) * 5;
        int cs = cell(rows, g * cols, 250 - 3 * gap, 70), w = g * cols * cs + (g - 1) * gap, gx = 160 - w / 2;
        for (int k = 0; k < g; k++)
            for (int i = 0; i < rows; i++)
                for (int j = 0; j < cols; j++)
                    flower(q, gx + k * (cols * cs + gap) + j * cs + cs / 2, MY0 + 8 + i * cs + cs / 2, cs / 2 - 1,
                           k % 2 ? Q_PINK : Q_LAV), dots++;
        break;
    }
    case V_SPLIT: case V_DH: {
        int r = (int)a, cols = (int)bb, cut = slid(q, SL_CUT, cols), cs = cell(r, cols, 170, 78);
        int si = step_of(q, GE_SLIDE, SL_CUT);
        bool split = past(q, si) || (m->phase != SP_DO && m->cur > si);
        int gx = MX0 + 16, gy = MY0 + 8;
        if (t->var == V_DH && split) { /* the right half under the left: twice the rows, half as long */
            for (int i = 0; i < 2 * r; i++)
                for (int j = 0; j < cut; j++)
                    flower(q, gx + j * cs + cs / 2, gy + i * cs + cs / 2, cs / 2 - 1, i < r ? Q_PINK : Q_LAV), dots++;
            api->snprintf(b, sizeof b, "%d rows of %d", 2 * r, cut);
            mq_text(q, api->font_small, gx + cut * cs + 10, gy + r * cs - 6, b, Q_INK);
            break;
        }
        for (int i = 0; i < r; i++)
            for (int j = 0; j < cols; j++) {
                int x = gx + j * cs + cs / 2 + (split && j >= cut ? 8 : 0);
                flower(q, x, gy + i * cs + cs / 2, cs / 2 - 1, j < cut ? Q_PINK : Q_LAV), dots++;
                dots2 += j < cut;
            }
        if (!split) /* the cut line */
            mq_fill(q, gx + cut * cs - 1, gy - 2, 2, r * cs + 4, Q_HOTPINK);
        if (t->var == V_SPLIT && split) { /* the expression beside the garden */
            int ex = gx + cols * cs + 18;
            api->snprintf(b, sizeof b, "%d " TIMES " %d =", r, cols);
            mq_text(q, api->font_small, ex, gy, b, Q_INK);
            api->snprintf(b, sizeof b, "%d " TIMES " %d + %d " TIMES " %d", r, cut, r, cols - cut);
            mq_text(q, api->font_small, ex, gy + 14, b, Q_PURPLE);
            int k = 0;
            for (int i = 0; i < m->n; i++)
                if (m->st[i].g == GE_PICK && past(q, i)) {
                    api->snprintf(b, sizeof b, k == 0 ? "%ld" : k == 1 ? "+ %ld" : "= %ld", (long)m->st[i].want);
                    mq_text(q, api->font_small, ex + (k == 2 ? 0 : k * 34), gy + 30 + (k == 2 ? 14 : 0), b, k == 2 ? Q_GREEN : Q_INK);
                    k++;
                }
            api->snprintf(b, sizeof b, "%d " TIMES " %d", r, cut);
            label(q, gx + cut * cs / 2, gy + r * cs + 4, b, Q_INK);
            api->snprintf(b, sizeof b, "%d " TIMES " %d", r, cols - cut);
            label(q, gx + cut * cs + 8 + (cols - cut) * cs / 2, gy + r * cs + 4 + 12, b, Q_INK);
        }
        break;
    }
    case V_KNOWN: {
        int r = (int)a, cols = (int)bb, kn = r == 9 ? 10 : 5, cs = cell(10, cols, 160, 98), gx = MX0 + 16, gy = MY0 + 2;
        bool done = m->taps[0] != 0;
        int rows = r == 9 ? 10 : done ? 6 : 5;
        for (int i = 0; i < rows; i++)
            for (int j = 0; j < cols; j++) {
                bool gone = r == 9 && done && i == 9, extra = i >= kn;
                flower(q, gx + j * cs + cs / 2, gy + i * cs + cs / 2, cs / 2 - 1, gone ? Q_GREY : extra ? Q_YELLOW : Q_PINK);
                dots += !gone;
            }
        api->snprintf(b, sizeof b, "%d rows of %d", kn, cols);
        mq_text(q, api->font_small, gx + cols * cs + 10, gy + 4, b, Q_PURPLE);
        if (past(q, 0)) {
            api->snprintf(b, sizeof b, "= %d", kn * cols);
            mq_text(q, api->font_small, gx + cols * cs + 10, gy + 18, b, Q_PURPLE);
        }
        break;
    }
    default: break;
    }
    m->units[0] = (uint16_t)dots, m->units[1] = (uint16_t)dots2;
}

static void draw_area(mq_t *q)
{
    mq_strat_t *m = SM;
    const strat_t *t = T(q);
    const modx_api_t *api = q->api;
    char b[32];
    int32_t a = m->x[0], bb = m->x[1];
    int X = 30, Y = MY0 + 22, W = t->var == V_PP ? 170 : 180, H = bb >= 10 && t->var != V_DIV ? 74 : 54;
    int32_t sum = 0;
    if (t->var == V_DIV) { /* b tall; the pieces laid so far, the rest still to find */
        int32_t qq = a / imax(1, (int)bb), done = 0;
        api->snprintf(b, sizeof b, "%ld", (long)bb);
        mq_text(q, api->font_small, X - 14, Y + H / 2 - 6, b, Q_INK);
        int x = X;
        for (int i = 0; i < m->n; i++) {
            const mq_sstep_t *s = &m->st[i];
            if (s->g != GE_PICK || !s->b || !past(q, i))
                continue;
            int w = imax(30, (int)(W * s->b / imax(1, (int)qq)));
            if (x + w > X + W)
                w = X + W - x;
            int hr = spr(q, done % 2 ? "model/floor_mint" : "model/floor", x, Y, w, H);
            mq_fill(q, x, Y, w, H, done % 2 ? Q_MINT : Q_PEACH);
            mq_rect(q, x, Y, w, H, Q_BROWN);
            END(hr);
            api->snprintf(b, sizeof b, "%ld", (long)s->b);
            label(q, x + w / 2, Y - 13, b, Q_PURPLE);
            api->snprintf(b, sizeof b, "%ld", (long)(s->b * bb));
            label(q, x + w / 2, Y + H / 2 - 6, b, Q_INK);
            x += w, done += s->b, sum += s->b * bb;
        }
        if (done < qq) {
            mq_rect(q, x, Y, X + W - x, H, Q_DIM);
            api->snprintf(b, sizeof b, "%ld left", (long)(a - done * bb));
            if (X + W - x >= 50)
                label(q, (x + X + W) / 2, Y + H / 2 - 6, b, Q_DIM);
            else
                mq_text(q, api->font_small, X + W + 6, Y + H / 2 - 6, b, Q_DIM);
        }
        m->units[0] = (uint16_t)sum;
        return;
    }
    /* the floor, cut at the tens (the cut follows her slider while she moves it) */
    int ca = slid(q, SL_CUT, (int)(a - a % 10)), cb = slid(q, SL_VCUT, (int)(bb - bb % 10));
    int32_t pa[3], pb[2];
    int na = 0, nb = 0;
    if (t->var == V_PP) {
        for (int k = ndig(a) - 1; k >= 0; k--)
            if (digit(a, k))
                pa[na++] = digit(a, k) * pow10(k);
    } else {
        if (ca > 0 && ca < a)
            pa[na++] = ca, pa[na++] = a - ca;
        else
            pa[na++] = a;
    }
    if (bb >= 10 && cb > 0 && cb < bb && (t->var == V_PP ? bb % 10 != 0 : 1))
        pb[nb++] = cb, pb[nb++] = bb - cb;
    else
        pb[nb++] = bb;
    if (t->var == V_PP && bb >= 10 && bb % 10)
        nb = 2, pb[0] = bb - bb % 10, pb[1] = bb % 10;
    int y = Y;
    for (int j = 0; j < nb; j++) {
        int h = nb == 1 ? H : imax(18, (int)(H * pb[j] / imax(1, (int)bb)));
        if (j == nb - 1)
            h = Y + H - y;
        int x = X;
        for (int i = 0; i < na; i++) {
            int w = na == 1 ? W : imax(28, (int)(W * pa[i] / imax(1, (int)a)));
            if (i == na - 1)
                w = X + W - x;
            int hr = spr(q, (i + j) % 2 ? "model/floor_mint" : "model/floor", x, y, w, h);
            mq_fill(q, x, y, w, h, (i + j) % 2 ? Q_MINT : Q_PEACH);
            mq_rect(q, x, y, w, h, Q_BROWN);
            END(hr);
            if (j == 0) {
                api->snprintf(b, sizeof b, "%ld", (long)pa[i]);
                label(q, x + w / 2, Y - 13, b, Q_PURPLE);
            }
            /* its product, once she found it */
            for (int k = 0; k < m->n; k++)
                if (m->st[k].g == GE_PICK && m->st[k].a == pa[i] && m->st[k].b == pb[j] && past(q, k)) {
                    api->snprintf(b, sizeof b, "%ld", (long)(pa[i] * pb[j]));
                    label(q, x + w / 2, y + h / 2 - 6, b, Q_INK);
                    sum += pa[i] * pb[j];
                }
            x += w;
        }
        api->snprintf(b, sizeof b, "%ld", (long)pb[j]);
        mq_text(q, api->font_small, X - 6 - mq_text_w(q, api->font_small, b), y + h / 2 - 6, b, Q_PURPLE);
        y += h;
    }
    if (m->phase == SP_DO && m->cur < m->n && m->st[m->cur].g == GE_SLIDE) { /* the knife */
        const mq_sstep_t *s = &m->st[m->cur];
        if (s->slide == SL_CUT) {
            int cx = X + (int)(W * m->val / imax(1, (int)a));
            mq_fill(q, cx, Y - 4, 2, H + 8, Q_HOTPINK);
            handle(q, cx, MY0 + 9, true);
        } else {
            int cy = Y + (int)(H * m->val / imax(1, (int)bb));
            mq_fill(q, X - 4, cy, W + 8, 2, Q_HOTPINK);
            handle(q, MX0 + 8, cy, true);
        }
    }
    if (t->var == V_PP) { /* the lines, as they are written */
        int ly = MY0 + 4, k = 0;
        for (int i = 0; i < m->n; i++)
            if (m->st[i].g == GE_PICK && past(q, i)) {
                if (m->st[i].a)
                    api->snprintf(b, sizeof b, "%ld " TIMES " %ld = %ld", (long)m->st[i].b, (long)m->st[i].a, (long)m->st[i].want);
                else
                    api->snprintf(b, sizeof b, "all: %ld", (long)m->st[i].want);
                mq_text(q, api->font_small, 212, ly + k * 13, b, m->st[i].a ? Q_INK : Q_GREEN);
                k++;
            }
    }
    m->units[0] = (uint16_t)(sum > 65535 ? 65535 : sum);
    m->units[1] = (uint16_t)(na * nb);
}

static void draw_peel(mq_t *q)
{
    mq_strat_t *m = SM;
    const modx_api_t *api = q->api;
    char b[32];
    int32_t a = m->x[0], d = m->x[1];
    rect_t r = slot_rect(q, 0);
    int w = (int)(r.w * m->pos / imax(1, (int)a));
    ribbon(q, r.x, r.y + 8, imax(2, w), 26, Q_PEACH);
    mq_rect(q, r.x, r.y + 8, r.w, 26, Q_DIM);
    api->snprintf(b, sizeof b, "%ld left", (long)m->pos);
    label(q, r.x + r.w / 2, r.y + 15, b, Q_INK);
    int32_t peeled = 0;
    int first = imax(0, m->nhop - 6);
    for (int i = 0; i < m->nhop; i++) {
        peeled += m->hop[i] * d;
        if (i < first)
            continue;
        api->snprintf(b, sizeof b, "%d group%s = %ld", m->hop[i], m->hop[i] == 1 ? "" : "s", (long)(m->hop[i] * d));
        mq_text(q, api->font_small, 214, MY0 + 4 + (i - first) * 13, b, Q_INK);
        m->units[2]++;
    }
    api->snprintf(b, sizeof b, "groups of %ld", (long)d);
    label(q, r.x + r.w / 2, r.y + r.h + 6, b, Q_PURPLE);
    m->units[0] = (uint16_t)peeled;
}

static void draw_friendly(mq_t *q)
{
    mq_strat_t *m = SM;
    const modx_api_t *api = q->api;
    char b[16];
    int32_t prod = 1;
    int mi = m->put[0] - 1, mj = m->put[1] - 1;
    for (int k = 0; k < 3; k++) {
        rect_t r = slot_rect(q, k);
        if (m->put[0] && k == mi)
            continue; /* (it went into the other) */
        int32_t v = m->put[0] && k == mj ? m->x[mi] * m->x[mj] : m->x[k];
        api->snprintf(b, sizeof b, "%ld", (long)v);
        card(q, r.x, r.y, r.w, r.h, b, m->put[0] && k == mj ? Q_YELLOW : Q_CREAM, Q_INK);
        prod *= v;
    }
    for (int k = 0; k < 2; k++)
        mq_text_c(q, api->font_medium, 28 + k * 98 + 70 + 14, MY0 + 46, TIMES, Q_DIM);
    m->units[0] = (uint16_t)(prod > 65535 ? 65535 : prod);
}

static void brace(mq_t *q, int x, int y, int w)
{
    mq_line(q, x, y + 8, x + 4, y + 2, Q_INK);
    mq_line(q, x + 4, y + 2, x + w - 4, y + 2, Q_INK);
    mq_line(q, x + w - 4, y + 2, x + w, y + 8, Q_INK);
}
static void draw_tape(mq_t *q)
{
    mq_strat_t *m = SM;
    const strat_t *t = T(q);
    const modx_api_t *api = q->api;
    char b[16];
    int32_t a = m->x[0], bb = m->x[1], c = m->x[2], parts = 0, whole = 0;
    if (t->var == V_PPW) {
        rect_t p = slot_rect(q, 0), wb = slot_rect(q, 1);
        brace(q, wb.x, wb.y + 12, wb.w);
        ribbon(q, p.x, p.y, p.w, p.h, Q_PINK);
        ribbon(q, p.x + p.w, p.y, wb.w - p.w, p.h, Q_SKY);
        api->snprintf(b, sizeof b, "%ld", (long)a);
        label(q, p.x + p.w / 2, p.y + 8, m->put[0] ? b : "", Q_INK);
        label(q, p.x + p.w + (wb.w - p.w) / 2, p.y + 8, "?", Q_PURPLE);
        api->snprintf(b, sizeof b, "%ld", (long)bb);
        label(q, wb.x + wb.w / 2, wb.y, m->put[1] ? b : "", Q_INK);
        parts = a + (bb - a), whole = bb;
    } else if (t->var == V_CMP) {
        rect_t s0 = slot_rect(q, 0), s1 = slot_rect(q, 1);
        mq_text(q, api->font_small, MX0, s0.y + 7, mq_cast_name(q, (t->who + 1) % MQ_NCAST), Q_DIM);
        mq_text(q, api->font_small, MX0, s1.y + 7, mq_cast_name(q, t->who), Q_DIM);
        ribbon(q, s0.x, s0.y, s0.w, s0.h, Q_PINK);
        api->snprintf(b, sizeof b, "%ld", (long)a);
        label(q, s0.x + s0.w / 2, s0.y + 7, m->put[0] ? b : "", Q_INK);
        if (c) { /* b copies of the short one */
            for (int i = 0; i < bb; i++) {
                ribbon(q, s0.x + i * s0.w, s1.y, s0.w, s1.h, Q_LAV);
                label(q, s0.x + i * s0.w + s0.w / 2, s1.y + 7, m->put[0] ? b : "", Q_INK);
            }
            parts = a * bb;
        } else {
            ribbon(q, s0.x, s1.y, s0.w, s1.h, Q_LAV);
            label(q, s0.x + s0.w / 2, s1.y + 7, m->put[0] ? b : "", Q_INK);
            ribbon(q, s1.x, s1.y, s1.w, s1.h, Q_YELLOW);
            api->snprintf(b, sizeof b, "%ld", (long)bb);
            label(q, s1.x + s1.w / 2, s1.y + 7, m->put[1] ? b : "", Q_INK);
            parts = a + bb;
        }
        label(q, s0.x + (c ? (int)bb * s0.w : s0.w + s1.w) / 2, s1.y + s1.h + 2, "?", Q_PURPLE);
        whole = c ? a * bb : a + bb;
    } else { /* a pieces of b */
        rect_t u = slot_rect(q, 0), wb = slot_rect(q, 1);
        brace(q, wb.x, wb.y + 12, wb.w);
        for (int i = 0; i < a; i++) {
            ribbon(q, u.x + i * u.w, u.y, u.w, u.h, i % 2 ? Q_MINT : Q_PINK);
            api->snprintf(b, sizeof b, "%ld", (long)bb);
            label(q, u.x + i * u.w + u.w / 2, u.y + 8, (c ? past(q, 1) : m->put[0]) ? b : c ? "?" : "", Q_INK);
        }
        api->snprintf(b, sizeof b, "%ld", (long)(a * bb));
        label(q, wb.x + wb.w / 2, wb.y, (c ? m->put[1] : past(q, 1)) ? b : c ? "" : "?", c ? Q_INK : Q_PURPLE);
        parts = a * bb, whole = a * bb;
    }
    m->units[0] = (uint16_t)parts, m->units[1] = (uint16_t)whole;
}

static void draw_strip(mq_t *q)
{
    mq_strat_t *m = SM;
    const strat_t *t = T(q);
    const modx_api_t *api = q->api;
    char b[48];
    int32_t a = m->x[0], d = m->x[1], c = m->x[2];
    int pieces = 0, pink = 0;
    if (t->var == V_CIRC && !c) { /* a round pie */
        int n = slid(q, SL_PIECES, 1), cx = 150, cy = MY0 + 48, r = 44;
        int hr = spr(q, "model/pie", cx - r - 3, cy - r - 3, 2 * r + 6, 2 * r + 6);
        mq_circle(q, cx, cy, r + 3, Q_BROWN);
        mq_circle(q, cx, cy, r, Q_CREAM);
        END(hr);
        for (int i = 0; i < n; i++) {
            bool pk = m->taps[i / 8] >> (i % 8) & 1;
            if (pk) { /* the slice as a fan of triangles */
                int16_t px[14], py[14];
                int k = 0;
                px[k] = (int16_t)cx, py[k++] = (int16_t)cy;
                for (int s = 0; s <= 12; s++) {
                    int ang = (i * 360 + s * 360 / 12) / n - 90;
                    px[k] = (int16_t)(cx + mq_icos(ang) * (r - 1) / 1024), py[k++] = (int16_t)(cy + mq_isin(ang) * (r - 1) / 1024);
                }
                mq_poly(q, px, py, k, Q_PINK);
                pink++;
            }
            int ang = i * 360 / n - 90;
            if (n > 1)
                mq_line(q, cx, cy, cx + mq_icos(ang) * r / 1024, cy + mq_isin(ang) * r / 1024, Q_BROWN);
            pieces++;
        }
    } else if (t->var == V_EQUIV) {
        int k = slid(q, SL_RECUT, 1), X = 40, W = 200;
        for (int row = 0; row < 2; row++) {
            int n = (int)(row ? d * k : d), pk = (int)(row ? a * k : a), y = MY0 + 10 + row * 44;
            for (int i = 0; i < n; i++) {
                int x0 = X + W * i / n, x1 = X + W * (i + 1) / n;
                int hr = spr(q, i < pk ? "model/piece_pink" : "model/piece", x0, y, x1 - x0, 30);
                mq_fill(q, x0, y, x1 - x0, 30, i < pk ? Q_PINK : Q_CREAM);
                mq_rect(q, x0, y, x1 - x0 + 1, 30, Q_BROWN);
                END(hr);
                if (row)
                    pieces++, pink += i < pk;
            }
            if (row && past(q, 1))
                api->snprintf(b, sizeof b, "%ld/%ld", (long)(a * k), (long)(d * k));
            else if (row)
                api->snprintf(b, sizeof b, "?/%ld", (long)(d * k));
            else
                api->snprintf(b, sizeof b, "%ld/%ld", (long)a, (long)d);
            mq_text(q, api->font_small, X + W + 10, y + 9, b, Q_PURPLE);
        }
    } else { /* a long cake (a strip), or a square cake cut in columns */
        bool sq = t->var == V_CIRC;
        int n = t->var == V_UNITS ? (int)d : slid(q, SL_PIECES, 1);
        int X = sq ? 90 : 30, W = sq ? 140 : 260, Y = sq ? MY0 + 6 : MY0 + 26, H = sq ? 86 : 40;
        int fill = t->var == V_UNITS ? m->put[0] : -1;
        for (int i = 0; i < n; i++) {
            int x0 = X + W * i / n, x1 = X + W * (i + 1) / n;
            bool pk = fill >= 0 ? i < fill : (m->taps[i / 8] >> (i % 8) & 1);
            int hr = spr(q, pk ? "model/piece_pink" : "model/piece", x0, Y, x1 - x0, H);
            mq_fill(q, x0, Y, x1 - x0, H, pk ? Q_PINK : fill >= 0 ? Q_WHITE : Q_CREAM);
            mq_rect(q, x0, Y, x1 - x0 + 1, H, Q_BROWN);
            END(hr);
            pieces++, pink += pk;
        }
        if (t->var == V_UNITS && fill > 0) { /* 1/4 + 1/4 + ... */
            int k = 0;
            for (int i = 0; i < fill && k < 40; i++)
                k += api->snprintf(b + k, sizeof b - (size_t)k, i ? " + 1/%ld" : "1/%ld", (long)d);
            mq_text_fit(q, api->font_small, 20, Y + H + 8, 280, b, Q_PURPLE, true);
        }
    }
    m->units[0] = (uint16_t)pieces, m->units[1] = (uint16_t)pink;
}

static void draw_plot(mq_t *q)
{
    mq_strat_t *m = SM;
    const modx_api_t *api = q->api;
    int n = (int)m->x[0], ly = MY0 + 86, xs = 0, at = 0;
    /* the ribbons' lengths: the next one ringed */
    for (int i = 0; i < n; i++) {
        int v = (int)((m->x[1] >> (4 * i)) & 15);
        int x = 20 + i * 46;
        uint16_t col = i < m->cnt || m->phase != SP_DO || m->cur > 0 ? Q_DIM : i == m->cnt ? Q_HOTPINK : Q_INK;
        if (i == m->cnt && m->phase == SP_DO && m->cur == 0)
            mq_rrect(q, x - 2, MY0 + 1, 40, 14, 6, Q_YELLOW);
        mq_text_fit(q, api->font_small, x, MY0 + 2, 36, qtr[v % 5], col, true);
    }
    mq_fill(q, 30, ly, 260, 2, Q_INK);
    for (int k = 0; k <= 4; k++) {
        rect_t r = slot_rect(q, k);
        int tx = r.x + r.w / 2;
        mq_fill(q, tx, ly - 4, 1, 10, Q_INK);
        label(q, tx, ly + 6, qtr[k], Q_INK);
        for (int j = 0; j < m->put[k]; j++)
            xmark(q, tx, ly - 9 - j * 11), xs++;
        if (k == m->x[2])
            at = m->put[k];
    }
    m->units[0] = (uint16_t)xs, m->units[1] = (uint16_t)at;
}

static void draw_cols(mq_t *q)
{
    mq_strat_t *m = SM;
    const strat_t *t = T(q);
    const modx_api_t *api = q->api;
    const font_t *f = api->font_large;
    char b[16];
    int rx = 190, y = MY0 + 4, lh = f->h + 2;
    int32_t a = m->x[0], bb = m->x[1];
    api->snprintf(b, sizeof b, "%ld", (long)a);
    mq_text(q, f, rx - mq_text_w(q, f, b), y, b, Q_INK);
    api->snprintf(b, sizeof b, "%ld", (long)bb);
    mq_text(q, f, rx - mq_text_w(q, f, b), y + lh, b, Q_INK);
    mq_text(q, f, rx - 110, y + lh, t->var == V_CMUL ? TIMES : m->x[2] ? MINUS : "+", Q_INK);
    mq_fill(q, rx - 110, y + 2 * lh + 1, 112, 2, Q_INK);
    int32_t shown = 0;
    if (t->var == V_CMUL) {
        int row = 0;
        for (int i = 0; i < m->n; i++)
            if (past(q, i)) {
                api->snprintf(b, sizeof b, "%ld", (long)m->st[i].want);
                int yy = y + 2 * lh + 4 + row * lh - (i == 2 ? 0 : 0);
                if (i == 2) {
                    mq_fill(q, rx - 110, yy - 1, 112, 1, Q_INK);
                    shown = m->st[i].want;
                }
                mq_text(q, f, rx - mq_text_w(q, f, b), yy, b, i == 2 ? Q_GREEN : Q_PURPLE);
                row++;
            }
    } else {
        int cw = mq_text_w(q, f, "0");
        for (int i = 0; i < m->n; i++)
            if (past(q, i)) {
                b[0] = (char)('0' + m->st[i].want), b[1] = 0;
                mq_text(q, f, rx - (m->st[i].a + 1) * cw, y + 2 * lh + 4, b, Q_GREEN);
                shown += m->st[i].want * pow10((int)m->st[i].a);
            }
    }
    m->units[0] = (uint16_t)(shown > 65535 ? 65535 : shown);
}

static void draw_model(mq_t *q)
{
    mq_strat_t *m = SM;
    for (int i = 0; i < 6; i++)
        m->units[i] = 0;
    switch (T(q)->model) {
    case MD_JUMPS: draw_jumps(q); break;
    case MD_PLACES: case MD_MONEY: draw_places(q); break;
    case MD_GIVE: draw_give(q); break;
    case MD_GROUPS: draw_groups(q); break;
    case MD_ARRAY: draw_array(q); break;
    case MD_AREA: draw_area(q); break;
    case MD_PEEL: draw_peel(q); break;
    case MD_FRIENDLY: draw_friendly(q); break;
    case MD_TAPE: draw_tape(q); break;
    case MD_STRIP: draw_strip(q); break;
    case MD_PLOT: draw_plot(q); break;
    case MD_COLS: draw_cols(q); break;
    default: break;
    }
}

/* ------------------------------------------------------------------ the widgets */
static void chip_label(mq_t *q, const mq_sstep_t *s, int i, char *b, int n)
{
    mq_strat_t *m = SM;
    const strat_t *t = T(q);
    const modx_api_t *api = q->api;
    int32_t v = s->ch[i];
    b[0] = 0;
    switch (t->model) {
    case MD_JUMPS: hop_word(q, v, b, n); break;
    case MD_PLACES:
        if (t->var == V_EXP)
            api->snprintf(b, (size_t)n, "%d", digit(m->x[0], (int)v));
        else if (t->var == V_BLOCKS)
            api->snprintf(b, (size_t)n, "%s", block_nm[v % 3]);
        else
            api->snprintf(b, (size_t)n, "%s", m->x[1] ? (v == 2 ? "1" : v == 1 ? ".1" : ".01") : (v == 2 ? "100" : v == 1 ? "10" : "1"));
        break;
    case MD_MONEY: api->snprintf(b, (size_t)n, "%s", v == 2 ? "$1" : v == 1 ? "10" : "1"); break;
    case MD_GIVE: api->snprintf(b, (size_t)n, "1"); break;
    case MD_PEEL: api->snprintf(b, (size_t)n, "%ld group%s", (long)v, v == 1 ? "" : "s"); break;
    case MD_TAPE: {
        int32_t a = m->x[0], bb = m->x[1], c = m->x[2], val = a;
        if (t->var == V_PPW || (t->var == V_CMP && !c))
            val = i ? bb : a;
        else if (t->var == V_EQG)
            val = c ? a * bb : bb;
        api->snprintf(b, (size_t)n, "%ld", (long)val);
        break;
    }
    case MD_STRIP: api->snprintf(b, (size_t)n, "1/%ld", (long)m->x[1]); break;
    case MD_PLOT: api->snprintf(b, (size_t)n, "X"); break;
    case MD_FRIENDLY: api->snprintf(b, (size_t)n, "%ld", (long)m->x[v % 3]); break;
    default: break;
    }
}
static rect_t chip_rect(mq_t *q, int i)
{
    if (T(q)->model == MD_FRIENDLY)
        return slot_rect(q, i);
    return R4(6 + i * 62, BY, 58, 30);
}
static rect_t pick_rect(int i) { return R4(6 + i * 82, BY, 76, 30); }

static void layout(mq_t *q)
{
    mq_strat_t *m = SM;
    const strat_t *t = T(q);
    int i;
    char b[24];
    mq_wg_begin(q);
    if (m->mode == SM_HELP)
        return; /* (the helper's moment: a tap anywhere goes on) */
    if (m->phase == SP_TEACH) {
        i = mq_wg_add(q, WG_BUTTON, W_NEXT, 0, 262, MQ_Y0 + 44, 54, 16);
        mq_wg_label(q, i, "next", Q_MINT);
        i = mq_wg_add(q, WG_BUTTON, W_TRY, 0, 252, BY, 64, 30);
        mq_wg_label(q, i, m->mode == SM_SHOW ? "done" : m->mode == SM_LESSON ? "my turn" : "try one", Q_YELLOW);
        return;
    }
    if (m->phase != SP_DO || m->cur >= m->n)
        return;
    i = mq_wg_add(q, WG_BUTTON, W_BACK, 0, 262, MQ_Y0 + 44, 54, 16);
    mq_wg_label(q, i, "back", Q_LAV);
    const mq_sstep_t *s = cur(q);
    switch (s->g) {
    case GE_JUMP: case GE_DRAG:
        for (int k = 0; k < s->nch; k++) {
            rect_t r = chip_rect(q, k);
            if (t->model == MD_FRIENDLY && m->put[0])
                continue;
            i = mq_wg_add(q, WG_DRAG, W_CHIP, k, r.x, r.y, r.w, r.h);
            chip_label(q, s, k, b, sizeof b);
            mq_wg_label(q, i, b, Q_CREAM);
            mq_wg_flags(q, i, WF_CUSTOM);
        }
        for (int k = 0; k < nslots(q); k++) {
            rect_t r = slot_rect(q, k);
            i = mq_wg_add(q, WG_DROP, W_SLOT, k, r.x, r.y, r.w, r.h);
            mq_wg_flags(q, i, WF_CUSTOM);
        }
        break;
    case GE_TAP:
        i = mq_wg_add(q, WG_AREA, W_AREA, 0, MX0, MY0, MX1 - MX0, MY1 - MY0);
        mq_wg_flags(q, i, WF_CUSTOM);
        break;
    case GE_SLIDE: {
        bool vert;
        rect_t r = slider_rect(q, s, &vert);
        i = mq_wg_add(q, WG_SLIDER, W_SLIDE, 0, r.x, r.y, r.w, r.h);
        mq_wg_range(q, i, (int)m->val, s->lo, s->hi, s->slide == SL_TURN ? 15 : 1);
        mq_wg_flags(q, i, WF_CUSTOM | (vert ? WF_VERT : 0));
        break;
    }
    case GE_PICK:
        for (int k = 0; k < s->nch; k++) {
            rect_t r = pick_rect(k);
            i = mq_wg_add(q, WG_CHOICE, W_PICK, k, r.x, r.y, r.w, r.h);
            fmt(q, s->fmt, s->ch[k], b, sizeof b);
            mq_wg_label(q, i, b, m->picked == k ? Q_GREY : Q_WHITE);
        }
        break;
    default: break;
    }
    i = mq_wg_add(q, WG_BUTTON, W_HINT, 0, 252, BY, 64, 30);
    mq_wg_label(q, i, "show how", Q_YELLOW);
}

/* where part k of the model is tapped */
static int tap_at(mq_t *q, int x, int y)
{
    mq_strat_t *m = SM;
    const strat_t *t = T(q);
    if (t->model == MD_GROUPS && (t->var == V_EQ || t->var == V_IDZ)) {
        int g = (int)m->x[0], bw = imin(60, 290 / imax(1, g)), x0 = 15 + (290 - g * bw) / 2;
        return x < x0 ? -1 : (x - x0) / imax(1, bw) < g ? (x - x0) / imax(1, bw) : -1;
    }
    if (t->model == MD_STRIP) {
        int n = (int)m->x[1];
        if (t->var == V_CIRC && !m->x[2]) {
            int dx = x - 150, dy = y - (MY0 + 48);
            int a = mq_angle_of(-dy, dx); /* clockwise from 12 */
            return a * n / 360 % n;
        }
        bool sq = t->var == V_CIRC;
        int X = sq ? 90 : 30, W = sq ? 140 : 260;
        return x < X || x >= X + W ? -1 : (x - X) * n / W;
    }
    if (t->model == MD_PLACES) {
        for (int k = 0; k < nplaces(q); k++) {
            rect_t r = house(q, k);
            if (x >= r.x && x < r.x + r.w)
                return k;
        }
        return -1;
    }
    (void)y;
    return 0;
}

/* the ghost hand's gesture for the step now: from (x0, y0) to (x1, y1) */
static bool hand_path(mq_t *q, int *x0, int *y0, int *x1, int *y1, bool *drag)
{
    mq_strat_t *m = SM;
    const strat_t *t = T(q);
    if (m->cur >= m->n)
        return false;
    const mq_sstep_t *s = cur(q);
    *drag = true;
    switch (s->g) {
    case GE_JUMP: {
        int best = 0;
        int32_t left = s->want - m->pos;
        for (int i = 0; i < s->nch; i++)
            if ((left > 0 ? s->ch[i] > 0 && s->ch[i] <= left : s->ch[i] < 0 && s->ch[i] >= left) && iabs((int)s->ch[i]) >= iabs((int)s->ch[best]))
                best = i;
        rect_t r = chip_rect(q, best);
        *x0 = r.x + r.w / 2, *y0 = r.y + r.h / 2;
        *x1 = path_x(q, m->pos + s->ch[best]), *y1 = LY - 10;
        return true;
    }
    case GE_DRAG: {
        int chip = 0, slot = 0;
        switch (t->model) {
        case MD_PLACES: case MD_MONEY:
            for (int k = 0; k < 3; k++)
                if ((t->var == V_EXP && t->model == MD_PLACES) ? !m->put[k] && k < nplaces(q) : m->put[k] < s->need[k]) {
                    slot = k;
                    break;
                }
            for (int i = 0; i < s->nch; i++)
                if (s->ch[i] == slot)
                    chip = i;
            break;
        case MD_GIVE: slot = 1; break;
        case MD_PEEL:
            for (int i = s->nch - 1; i >= 0; i--)
                if (s->ch[i] * m->x[1] <= m->pos)
                    chip = i;
            break;
        case MD_FRIENDLY:
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++)
                    if (i != j && friendly_pair(m->x[i], m->x[j]) && !(chip || slot))
                        chip = i, slot = j;
            break;
        case MD_TAPE: chip = m->put[0] && s->nch > 1 ? 1 : 0, slot = s->nch == 1 ? (s->need[1] ? 1 : 0) : chip; break;
        case MD_PLOT: slot = (int)((m->x[1] >> (4 * m->cnt)) & 15); break;
        default: break;
        }
        rect_t a = chip_rect(q, chip), b = slot_rect(q, slot);
        *x0 = a.x + a.w / 2, *y0 = a.y + a.h / 2, *x1 = b.x + b.w / 2, *y1 = b.y + b.h / 2;
        return true;
    }
    case GE_TAP: {
        *drag = false;
        *x0 = *x1 = 160, *y0 = *y1 = MY0 + 50;
        if (t->model == MD_GROUPS && (t->var == V_EQ || t->var == V_IDZ)) {
            int g = (int)m->x[0], bw = imin(60, 290 / imax(1, g)), k = 0;
            while (k < g && (m->taps[k / 8] >> (k % 8) & 1))
                k++;
            *x0 = *x1 = 15 + (290 - g * bw) / 2 + k * bw + bw / 2, *y0 = *y1 = MY0 + 50;
        } else if (t->model == MD_GROUPS)
            *x0 = *x1 = MX0 + 36, *y0 = *y1 = MY0 + 50;
        else if (t->model == MD_STRIP) {
            int n = (int)m->x[1], k = 0;
            while (k < n && (m->taps[k / 8] >> (k % 8) & 1))
                k++;
            if (t->var == V_CIRC && !m->x[2]) {
                int a = (2 * k + 1) * 180 / n - 90;
                *x0 = *x1 = 150 + mq_icos(a) * 28 / 1024, *y0 = *y1 = MY0 + 48 + mq_isin(a) * 28 / 1024;
            } else {
                bool sq = t->var == V_CIRC;
                int X = sq ? 90 : 30, W = sq ? 140 : 260;
                *x0 = *x1 = X + W * (2 * k + 1) / (2 * n), *y0 = *y1 = sq ? MY0 + 50 : MY0 + 46;
            }
        } else if (t->model == MD_PLACES) {
            rect_t r = house(q, t->var == V_PSUM ? (int)s->a : 1);
            *x0 = *x1 = r.x + r.w / 2, *y0 = *y1 = r.y + 40;
        }
        return true;
    }
    case GE_SLIDE: {
        bool vert;
        rect_t r = slider_rect(q, s, &vert);
        int span = imax(1, s->hi - s->lo);
        if (vert)
            *x0 = *x1 = r.x + r.w / 2, *y0 = r.y + (int)((m->val - s->lo) * r.h / span), *y1 = r.y + (int)((s->want - s->lo) * r.h / span);
        else
            *y0 = *y1 = r.y + r.h / 2, *x0 = r.x + (int)((m->val - s->lo) * r.w / span), *x1 = r.x + (int)((s->want - s->lo) * r.w / span);
        return true;
    }
    case GE_PICK:
        *drag = false;
        for (int i = 0; i < s->nch; i++)
            if (s->ch[i] == s->want) {
                rect_t r = pick_rect(i);
                *x0 = *x1 = r.x + r.w * 3 / 4, *y0 = *y1 = r.y + r.h - 6;
            }
        return true;
    default: return false;
    }
}

/* ------------------------------------------------------------------ input */
static void on_ev(mq_t *q, const mq_wev_t *ev)
{
    mq_strat_t *m = SM;
    switch (ev->id) {
    case W_BACK:
        if (ev->why == WHY_TAP || ev->why == WHY_KEY)
            mq_strat_close(q);
        return;
    case W_NEXT:
        if (ev->why == WHY_TAP || ev->why == WHY_KEY)
            teach_next(q);
        return;
    case W_TRY:
        if (ev->why != WHY_TAP && ev->why != WHY_KEY)
            return;
        if (m->mode == SM_SHOW || m->mode == SM_LESSON) {
            mq_strat_close(q);
            return;
        }
        say(q, "Your turn: try one!");
        do_begin(q);
        return;
    case W_HINT:
        if (ev->why == WHY_TAP || ev->why == WHY_KEY) {
            m->hint = 1, m->hint_t = mq_mark(q);
            say(q, cur(q)->say);
        }
        return;
    case W_CHIP:
        if (ev->why == WHY_DROP) {
            const mq_sstep_t *s = cur(q);
            if (s->g == GE_JUMP)
                do_hop(q, s->ch[ev->arg]);
            else
                do_drop(q, ev->arg, ev->value);
        }
        return;
    case W_SLOT: /* a house tapped with nothing carried: one of its kind into it */
        if (ev->why == WHY_TAP && (T(q)->model == MD_PLACES || T(q)->model == MD_MONEY) && T(q)->var != V_EXP) {
            const mq_sstep_t *s = cur(q);
            for (int i = 0; i < s->nch; i++)
                if (s->ch[i] == ev->arg) {
                    do_drop(q, i, ev->arg);
                    return;
                }
        } else if (ev->why == WHY_TAP && cur(q)->g == GE_JUMP && cur(q)->nch == 1)
            do_hop(q, cur(q)->ch[0]);
        return;
    case W_AREA:
        if (ev->why == WHY_TAP)
            do_tap(q, tap_at(q, ev->x, ev->y));
        else if (ev->why == WHY_KEY && ev->value == HK_ENTER)
            do_tap(q, -1);
        return;
    case W_SLIDE:
        if (ev->why == WHY_DRAG)
            m->alt = 1, do_slide(q, ev->value, false);
        else if (ev->why != WHY_NONE)
            do_slide(q, ev->value, true);
        return;
    case W_PICK:
        if (ev->why == WHY_TAP || ev->why == WHY_KEY)
            do_pick(q, ev->arg);
        return;
    default: return;
    }
}

bool mq_strat_touch(mq_t *q, int type, int x, int y)
{
    mq_strat_t *m = SM;
    if (!m->on)
        return false;
    q->last_input = q->now;
    if (m->phase == SP_DONE) {
        if (type == TV_TAP)
            done_now(q);
        return true;
    }
    layout(q);
    mq_wev_t ev;
    bool took = mq_wg_touch(q, type, x, y, &ev);
    if (took && ev.why != WHY_NONE)
        on_ev(q, &ev);
    if ((type == TV_UP) && m->alt && m->on && m->phase == SP_DO) { /* she let go of the slider: is it there? */
        m->alt = 0;
        if (cur(q)->g == GE_SLIDE)
            do_slide(q, (int)m->val, true);
    }
    if (!took && type == TV_TAP && m->phase == SP_TEACH && y >= MY0) /* a tap on the picture: on with the show */
        teach_next(q);
    q->redraw = true;
    return true;
}
bool mq_strat_scroll(mq_t *q, int dy, int x, int y)
{
    if (!q->strat.on || q->strat.phase != SP_DO)
        return true;
    layout(q);
    mq_wev_t ev;
    if (mq_wg_scroll(q, dy, x, y, &ev) && ev.why != WHY_NONE)
        on_ev(q, &ev);
    return true;
}
bool mq_strat_key(mq_t *q, int k, int ch)
{
    mq_strat_t *m = SM;
    if (!m->on)
        return false;
    q->last_input = q->now;
    if (m->phase == SP_DONE) {
        if (k == HK_ENTER)
            done_now(q);
        return true;
    }
    if (m->phase == SP_DO && m->cur < m->n && cur(q)->g == GE_PICK && ch >= '1' && ch <= '3')
        return do_pick(q, ch - '1'), true;
    if (k == HK_DEL) {
        mq_strat_close(q);
        return true;
    }
    layout(q);
    mq_wev_t ev;
    if (mq_wg_key(q, k, ch, &ev)) {
        if (ev.why != WHY_NONE)
            on_ev(q, &ev);
        q->redraw = true;
        return true;
    }
    if (k == HK_ENTER && m->phase == SP_TEACH)
        teach_next(q);
    q->redraw = true;
    return true;
}

/* ------------------------------------------------------------------ the screen */
static void expr(mq_t *q, char *b, int n)
{
    mq_strat_t *m = SM;
    const strat_t *t = T(q);
    const modx_api_t *api = q->api;
    long a = m->x[0], bb = m->x[1], c = m->x[2];
    char s1[8], s2[8];
    b[0] = 0;
    switch (m->id) {
    case STG_JUMPS: api->snprintf(b, (size_t)n, c ? "%ld " MINUS " %ld" : "%ld + %ld", a, bb); break;
    case STG_COUNTUP: api->snprintf(b, (size_t)n, "%ld " MINUS " %ld", a, bb); break;
    case STG_PSUMS: case STG_MAKE10: case STG_COMP: api->snprintf(b, (size_t)n, "%ld + %ld", a, bb); break;
    case STG_ALGO_AS: case STG_ALGO_M: break;
    case STG_EXPAND: api->snprintf(b, (size_t)n, "%ld", a); break;
    case STG_GROUPS: api->snprintf(b, (size_t)n, "%ld bags of %ld", a, bb); break;
    case STG_ONEZERO: case STG_KNOWN: case STG_DOUBLE: case STG_SPLIT: case STG_AREA: case STG_PPROD:
        api->snprintf(b, (size_t)n, "%ld " TIMES " %ld", a, bb);
        break;
    case STG_ARRAY: api->snprintf(b, (size_t)n, "%ld rows of %ld", a, bb); break;
    case STG_TURN: api->snprintf(b, (size_t)n, "%ld " TIMES " %ld and %ld " TIMES " %ld", a, bb, bb, a); break;
    case STG_REGROUP: api->snprintf(b, (size_t)n, "(%ld " TIMES " %ld) " TIMES " %ld", a, bb, c); break;
    case STG_FRIENDLY: api->snprintf(b, (size_t)n, "%ld " TIMES " %ld " TIMES " %ld", a, bb, c); break;
    case STG_SHARE: api->snprintf(b, (size_t)n, "%ld shared by %ld", a * bb, a); break;
    case STG_BAGS: api->snprintf(b, (size_t)n, "%ld in bags of %ld", a * bb, a); break;
    case STG_THINKX: api->snprintf(b, (size_t)n, "%ld " DIVS " %ld", a * bb, a); break;
    case STG_PEEL: case STG_AREADIV: api->snprintf(b, (size_t)n, "%ld " DIVS " %ld", a, bb); break;
    case STG_TIMELINE:
        fmt(q, FM_TIME, a, s1, sizeof s1), fmt(q, FM_TIME, a + bb, s2, sizeof s2);
        api->snprintf(b, (size_t)n, "%s to %s", s1, s2);
        break;
    case STG_STRIP: case STG_PIE: api->snprintf(b, (size_t)n, "%ld pieces, %ld pink", bb, a); break;
    case STG_FLINE: api->snprintf(b, (size_t)n, "%ld hops of 1/%ld", a, bb); break;
    case STG_BENCH: api->snprintf(b, (size_t)n, "%ld/%ld", a, bb); break;
    case STG_UNITS: api->snprintf(b, (size_t)n, "%ld pieces of 1/%ld", a, bb); break;
    case STG_EQUIV: api->snprintf(b, (size_t)n, "%ld/%ld = ?/%ld", a, bb, bb * c); break;
    default: break;
    }
    (void)t;
}

void mq_strat_draw(mq_t *q)
{
    mq_strat_t *m = SM;
    const modx_api_t *api = q->api;
    const strat_t *t = T(q);
    mq_fill(q, 0, MQ_Y0 + 16, MQ_W, MQ_H - 16, RGB(244, 248, 255));
    /* the friend and what they say */
    bool talk = m->say_t && q->anim_t - m->say_t < 1600;
    mq_cast(q, who(q), 2, MQ_Y0 + 17, 26, m->phase == SP_DONE ? AN_DANCE : talk ? AN_TALK : AN_HAPPY);
    mq_rrect(q, 30, MQ_Y0 + 18, 286, 24, 8, Q_WHITE);
    char l2[3][MQ_TXT];
    int nl = mq_story_wrap(q, m->say, l2, 278);
    bool bad = m->bad_t && q->anim_t - m->bad_t < 2500;
    for (int i = 0; i < 2 && i < nl; i++)
        mq_text_fit(q, api->font_small, 34, MQ_Y0 + 18 + (nl > 1 ? i * 11 : 5), 278, l2[i],
                    m->phase == SP_DONE ? Q_GREEN : bad ? Q_HOTPINK : Q_INK, false);
    /* the trick's name, its numbers, the steps (done green) */
    char nm[40], ex[40], l[90];
    mq_strat_name(q, m->id, nm, sizeof nm);
    expr(q, ex, sizeof ex);
    api->snprintf(l, sizeof l, ex[0] ? "%s: %s" : "%s", nm, ex);
    mq_text_fit(q, api->font_small, 8, MQ_Y0 + 46, 250, l, Q_PURPLE, false);
    for (int k = 0; k < m->n && k < SS_MAX; k++) /* (the steps: done green, now pink) */
        mq_fill(q, 8 + k * 10, MQ_Y0 + 58, 7, 2, k < m->cur ? Q_GREEN : k == m->cur ? Q_HOTPINK : Q_GREY);
    /* the model */
    mq_rrect(q, MX0 - 2, MY0 - 2, MX1 - MX0 + 4, MY1 - MY0 + 4, 10, Q_WHITE);
    draw_model(q);
    /* its controls: chips, answers, buttons, the slider's knob */
    layout(q);
    for (int i = 0; i < q->wg.n; i++) {
        const mq_wg_t *g = &q->wg.w[i];
        int ci, cxx, cyy;
        if (g->id == W_CHIP && t->model != MD_FRIENDLY && !(mq_wg_carrying(q, &ci, &cxx, &cyy) && ci == i)) {
            if (t->model == MD_MONEY) {
                int dn = g->arg < 3 ? (cur(q)->ch[g->arg] == 2 ? DN_DOLLAR : cur(q)->ch[g->arg] == 1 ? DN_DIME : DN_PENNY) : DN_PENNY;
                mq_rrect(q, g->x, g->y, g->w, g->h, 8, Q_CREAM);
                mq_money_draw(q, dn, g->x + g->w / 2, g->y + g->h / 2, 2);
            } else
                card(q, g->x, g->y, g->w, g->h, g->label, t->model == MD_STRIP ? Q_PINK : Q_CREAM, Q_INK);
        }
        if (g->id == W_SLIDE) { /* the knob where the value is */
            int span = imax(1, g->hi - g->lo), vert = g->flags & WF_VERT;
            mq_fill(q, vert ? g->x + g->w / 2 - 1 : g->x, vert ? g->y : g->y + g->h / 2 - 1, vert ? 3 : g->w, vert ? g->h : 3, Q_GREY);
            int kx = vert ? g->x + g->w / 2 : g->x + (g->value - g->lo) * g->w / span;
            int ky = vert ? g->y + (g->value - g->lo) * g->h / span : g->y + g->h / 2;
            handle(q, kx, ky, mq_wg_pressed(q, i));
        }
    }
    if (m->phase == SP_DO || m->phase == SP_TEACH)
        mq_wg_draw(q);
    if (m->phase == SP_TEACH && t->model != MD_FRIENDLY) { /* (the chips and answers of the step shown, for the hand) */
        int k = m->beat - m->intro_n;
        if (k >= 0 && k < m->n && m->cur == k) {
            const mq_sstep_t *s = &m->st[k];
            char b[24];
            if (s->g == GE_JUMP || s->g == GE_DRAG)
                for (int i = 0; i < s->nch; i++) {
                    rect_t r = chip_rect(q, i);
                    chip_label(q, s, i, b, sizeof b);
                    card(q, r.x, r.y, r.w, r.h, b, Q_CREAM, Q_INK);
                }
            if (s->g == GE_PICK)
                for (int i = 0; i < s->nch; i++) {
                    rect_t r = pick_rect(i);
                    fmt(q, s->fmt, s->ch[i], b, sizeof b);
                    card(q, r.x, r.y, r.w, r.h, b, Q_WHITE, Q_INK);
                }
        }
    }
    int ci, cxx, cyy;
    if (mq_wg_carrying(q, &ci, &cxx, &cyy)) { /* the thing carried, under her finger */
        const mq_wg_t *g = mq_wg(q, ci);
        if (g && g->id == W_CHIP)
            card(q, cxx - g->w / 2, cyy - g->h / 2 - 6, g->w, g->h, g->label, Q_YELLOW, Q_INK);
    }
    /* the ghost hand: the step the friend is showing, or "show me" */
    int el = (int)(q->anim_t + 1 - (m->phase == SP_TEACH ? m->t : m->hint_t));
    bool showing = (m->phase == SP_TEACH && m->beat >= m->intro_n && m->cur == m->beat - m->intro_n && el < (int)HAND_MS + 600) ||
                   (m->phase == SP_DO && m->hint && el < 1800 && cur(q)->g != GE_PICK);
    if (m->mode == SM_HELP && m->phase == SP_TEACH)
        showing = m->beat >= m->intro_n && m->cur == m->beat - m->intro_n && el < (int)HAND_MS + 600;
    int x0, y0, x1, y1;
    bool drag;
    if (showing && hand_path(q, &x0, &y0, &x1, &y1, &drag)) {
        int ph = el % 1400, tt = ph < 200 ? 0 : ph > 1000 ? 1000 : (ph - 200) * 1000 / 800;
        int hx = drag ? x0 + (x1 - x0) * tt / 1000 : x0, hy = drag ? y0 + (y1 - y0) * tt / 1000 : y0;
        mq_hand(q, hx, hy, ph > 150 && ph < 1100 ? 16 : 0);
    }
    if (m->phase == SP_DONE)
        for (int k = 0; k < 5; k++)
            mq_star(q, 40 + k * 60, BY + 10 + (int)((q->anim_t / 150 + (uint32_t)k) % 3) * 3, 6, Q_YELLOW);
}

/* the helper friend's moment, with a way: drawn in the round's field */
bool mq_strat_help_draw(mq_t *q)
{
    if (!q->strat.on || q->strat.mode != SM_HELP)
        return false;
    mq_strat_draw(q);
    return true;
}
bool mq_strat_help_next(mq_t *q)
{
    mq_strat_t *m = SM;
    if (!m->on || m->mode != SM_HELP)
        return false;
    if (m->phase == SP_DONE) { /* shown: her own order now */
        mq_strat_close(q);
        q->gm.help_step = q->ex_n;
        q->gm.say[0] = 0, q->gm.hud_ok = 0; /* (the board: her order again) */
        return false;
    }
    teach_next(q);
    return true;
}

/* ------------------------------------------------------------------ which way fits an order */
static bool fits_numbers(int s, const int32_t *x)
{
    int32_t a = x[0], b = x[1];
    switch (tab[s].model) {
    case MD_JUMPS:
        if (tab[s].var == V_ADD)
            return a > 0 && b > 0 && b < 300 && a < 1000 && (!x[2] || a > b);
        if (tab[s].var == V_MAKE)
            return a > 0 && b > (a % 100 >= 90 ? (a / 100 + 1) * 100 : (a / 10 + 1) * 10) - a && b < 100;
        if (tab[s].var == V_UP)
            return a > b && b > 0 && a - b <= 60 && a < 1000;
        if (tab[s].var == V_TIME)
            return b > 0 && b <= 240;
        return b >= 2 && b <= 12 && a > 0 && a < b;
    case MD_PLACES: return tab[s].var == V_PSUM ? a > 0 && b > 0 && a < 1000 && b < 1000 && a + b < 1000 : true;
    case MD_GIVE: return a > 0 && b > 9 && b < 100 && (a % 10 >= 8 || a % 100 >= 97);
    case MD_GROUPS: return a >= 0 && a <= 9 && b >= 0 && b <= 9;
    case MD_ARRAY:
        if (tab[s].var == V_SPLIT)
            return a >= 2 && a <= 9 && b >= 6 && b <= 10 && x[2] > 0 && x[2] < b;
        if (tab[s].var == V_KNOWN)
            return (a == 6 || a == 9) && b >= 2 && b <= 10;
        if (tab[s].var == V_TURN)
            return a >= 1 && b >= 1 && a <= 6 && b <= 6 && a != b;
        if (tab[s].var == V_ASSOC)
            return a >= 1 && b >= 1 && x[2] >= 2 && b * x[2] <= 16 && a <= 6;
        if (tab[s].var == V_DH)
            return a >= 1 && a <= 5 && b % 2 == 0 && b <= 20;
        return a >= 1 && a <= 10 && b >= 1 && b <= 10;
    case MD_AREA:
        if (tab[s].var == V_DIV)
            return b >= 2 && b <= 9 && a % b == 0 && a / b < 100 && a / b >= 10;
        if (tab[s].var == V_AM)
            return a >= 11 && a <= 99 && b >= 2 && b <= 99;
        return a >= 11 && a <= 999 && b >= 2 && b <= 99 && (a > 99 ? b < 10 : 1);
    case MD_PEEL: return b >= 2 && b <= 9 && a % b == 0 && a / b >= 10 && a / b < 200;
    case MD_STRIP: return b >= 2 && b <= 12 && a >= 1 && a < b;
    case MD_COLS: return true;
    default: return true;
    }
}
static int add_cand(int *c, int32_t (*cx)[6], int n, int s, int32_t a, int32_t b, int32_t d)
{
    if (n >= 8)
        return n;
    c[n] = s, cx[n][0] = a, cx[n][1] = b, cx[n][2] = d, cx[n][3] = cx[n][4] = cx[n][5] = 0;
    return n + 1;
}
int mq_strat_fit(mq_t *q, const mq_prob_t *p, int which, int32_t x[6])
{
    int c[8], n = 0;
    int32_t cx[8][6];
    int32_t a = p->op[0], b = p->op[1];
    int ws = p->ws;
    if (p->way) /* a way's own order: that way first */
        n = add_cand(c, cx, n, p->way - 1, p->wx[0], p->wx[1], p->wx[2]);
    if (!ws && (p->skill == S_ADDSUB || p->skill == S_ADDSUBBIG) && p->kind == K_NUM && a > 0 && b > 0) /* (in her head) */
        ws = p->ans == a + b ? WS_ADD : p->ans == a - b ? WS_SUB : WS_NONE;
    if (ws == WS_ADD) {
        if (a % 10 == 9 || a % 100 >= 97)
            n = add_cand(c, cx, n, STG_COMP, a, b, 0);
        else if (b % 10 == 9 || b % 100 >= 97)
            n = add_cand(c, cx, n, STG_COMP, b, a, 0);
        if (b < 10 && a % 10 + b > 10)
            n = add_cand(c, cx, n, STG_MAKE10, a, b, 0);
        n = add_cand(c, cx, n, STG_JUMPS, a, b, 0);
        n = add_cand(c, cx, n, STG_PSUMS, a, b, 0);
        n = add_cand(c, cx, n, STG_ALGO_AS, a, b, 0);
    } else if (ws == WS_SUB && p->kind != K_MONEY) {
        if (a - b <= 30)
            n = add_cand(c, cx, n, STG_COUNTUP, a, b, 0);
        n = add_cand(c, cx, n, STG_JUMPS, a, b, 1);
        n = add_cand(c, cx, n, STG_COUNTUP, a, b, 0);
        n = add_cand(c, cx, n, STG_ALGO_AS, a, b, 1);
    } else if (ws == WS_MUL) {
        n = add_cand(c, cx, n, STG_AREA, a, b, 0);
        n = add_cand(c, cx, n, STG_PPROD, a, b, 0);
        n = add_cand(c, cx, n, STG_ALGO_M, a, b, 0);
    } else if (ws == WS_DIV && p->kind == K_NUM) {
        n = add_cand(c, cx, n, STG_PEEL, a, b, 0);
        n = add_cand(c, cx, n, STG_AREADIV, a, b, 0);
    }
    switch (p->skill) {
    case S_GROUPS:
        if (!p->way)
            n = add_cand(c, cx, n, STG_GROUPS, p->v[0], p->v[1], 0), n = add_cand(c, cx, n, STG_ARRAY, p->v[0], p->v[1], 0);
        break;
    case S_MDWORD:
        if (p->stg - 1 == ST_GROUPS)
            n = add_cand(c, cx, n, STG_GROUPS, p->v[0], p->v[1], 0), n = add_cand(c, cx, n, STG_TAPE_EQ, p->v[0], p->v[1], 0);
        break;
    case S_FACTS: {
        int fa, fb;
        bool div;
        mq_fact_decode(p->a[0], &fa, &fb, &div);
        if (div)
            n = add_cand(c, cx, n, STG_THINKX, fb, fa, 0), n = add_cand(c, cx, n, STG_BAGS, fb, fa, 0);
        else {
            int lo = fa < fb ? fa : fb, hi = fa < fb ? fb : fa;
            if (lo == 6 || lo == 9 || hi == 6 || hi == 9)
                n = add_cand(c, cx, n, STG_KNOWN, lo == 6 || lo == 9 ? lo : hi, lo == 6 || lo == 9 ? hi : lo, 0);
            if (hi >= 6 && lo >= 3)
                n = add_cand(c, cx, n, STG_SPLIT, lo, hi, 5);
            n = add_cand(c, cx, n, STG_TURN, fa, fb, 1);
            n = add_cand(c, cx, n, STG_ARRAY, fa, fb, 0);
        }
        break;
    }
    case S_SHARE: n = add_cand(c, cx, n, STG_SHARE, p->v[0], p->v[1], 0), n = add_cand(c, cx, n, STG_THINKX, p->v[0], p->v[1], 0); break;
    case S_DIVFACT: n = add_cand(c, cx, n, STG_THINKX, p->v[0], p->v[1], 0), n = add_cand(c, cx, n, STG_SHARE, p->v[0], p->v[1], 0); break;
    case S_PROPS:
        if (p->way)
            break;
        if (p->v[6] == 0x81)
            n = add_cand(c, cx, n, STG_TURN, p->v[0], p->v[1], 1);
        else if (p->v[6] == 0x82)
            n = add_cand(c, cx, n, STG_REGROUP, p->v[0], p->v[1] / 2, 2);
        else if (p->v[6] == 0x80)
            n = add_cand(c, cx, n, STG_SPLIT, p->v[0], p->v[1], p->v[2]);
        break;
    case S_ELAPSED:
        if (p->kind == K_NUM)
            n = add_cand(c, cx, n, STG_TIMELINE, p->a[0], p->a[1], 0);
        break;
    case S_UNITFRAC:
        if (p->kind == K_FRAC && !p->way)
            n = add_cand(c, cx, n, STG_STRIP, p->ans / 1000, p->ans % 1000, 0), n = add_cand(c, cx, n, STG_PIE, p->ans / 1000, p->ans % 1000, 0);
        break;
    case S_FRACLINE:
        if (p->kind == K_FRAC && !p->way)
            n = add_cand(c, cx, n, STG_FLINE, p->ans / 1000, p->ans % 1000, 0), n = add_cand(c, cx, n, STG_STRIP, p->ans / 1000, p->ans % 1000, 0);
        break;
    default: break;
    }
    int k = 0;
    for (int i = 0; i < n; i++) {
        bool dup = false;
        for (int j = 0; j < i; j++)
            dup |= c[j] == c[i];
        if (dup || !mq_strat_open(q, c[i]) || !fits_numbers(c[i], cx[i]))
            continue;
        if (tab[c[i]].model == MD_COLS && !p->way) /* (the notebook already is the columns' way) */
            continue;
        if (k++ == which) {
            for (int j = 0; j < 6; j++)
                x[j] = cx[i][j];
            return c[i];
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ the lightbulb and the helper friend */
bool mq_strat_bulb_btn(mq_t *q, int *x, int *y, int *w, int *h)
{
    int32_t xx[6];
    *x = 216, *y = MQ_Y0 + 58 + 40, *w = 96, *h = 28;
    return q->scene == SC_PLAY && mq_strat_fit(q, &q->prob, 0, xx) >= 0;
}
void mq_strat_bulb(mq_t *q)
{
    int32_t x[6];
    int s = mq_strat_fit(q, &q->prob, 0, x);
    if (s >= 0)
        mq_strat_start(q, s, SM_BULB, x);
}
bool mq_strat_helper(mq_t *q)
{
    int32_t x[6];
    int s = mq_strat_fit(q, &q->ex, 0, x);
    if (s < 0 || tab[s].model == MD_COLS)
        return false;
    mq_strat_start(q, s, SM_HELP, x);
    return true;
}

/* ------------------------------------------------------------------ "use this way" orders now and then */
static bool fits_skill(mq_t *q, int s)
{
    int32_t x[6];
    int32_t e[6];
    example_numbers(s, e);
    (void)x;
    return mq_strat_open(q, s) && fits_numbers(s, e);
}
void mq_strat_order(mq_t *q)
{
    mq_prob_t *p = &q->prob;
    mq_more_t *mo = rec(q);
    if (q->preview || q->placing_item || q->gm.boss || q->fixed_sk1 || q->engine_stop || p->way)
        return;
    if (mo->strat_since < 255)
        mo->strat_since++;
    int intro = -1, use = -1, best_used = 256;
    for (int s = 0; s < STG_COUNT; s++) {
        if (tab[s].skill != p->skill || !fits_skill(q, s))
            continue;
        if (!mo->strat_seen[s]) {
            if (intro < 0 || tab[s].week < tab[intro].week)
                intro = s;
        } else if (mo->strat_used[s] < best_used)
            use = s, best_used = mo->strat_used[s];
    }
    uint32_t n = mq_prof(q)->problems;
    int pick = -1;
    if (intro >= 0 && (uint8_t)(n - mo->strat_intro) >= 4) /* something new from school: introduced */
        pick = intro, mo->strat_intro = (uint8_t)n;
    else if (use >= 0 && mo->strat_since >= (best_used < 3 ? 5 : 12)) /* now and then: "use this way" */
        pick = use;
    if (pick < 0)
        return;
    int32_t x[6];
    order_numbers(q, pick, mq_engine_diff(q, p->skill) > 1 ? 1 : 0, x);
    if (!fits_numbers(pick, x))
        return;
    mq_strat_make(q, p, pick, x);
    mo->strat_since = 0;
    q->dirty = true;
}
/* the order is a way's: it opens by itself (the notebook for the columns' way) */
bool mq_strat_auto(mq_t *q)
{
    const mq_prob_t *p = &q->prob;
    if (!p->way || q->preview || q->strat.on)
        return false;
    int s = p->way - 1;
    if (tab[s].model == MD_COLS && rec(q)->strat_seen[s] && notebook_game(q)) {
        mq_cols_open(q);
        bump(&rec(q)->strat_used[s]);
        return true;
    }
    int32_t x[6] = {p->wx[0], p->wx[1], p->wx[2], 0, 0, 0};
    mq_strat_start(q, s, SM_ORDER, x);
    return true;
}

/* ------------------------------------------------------------------ the picture matches the numbers */
/* (call after drawing the model with every step done) 0: it does; else why */
int mq_strat_check(mq_t *q, char *why, int n)
{
    mq_strat_t *m = SM;
    const strat_t *t = T(q);
    const modx_api_t *api = q->api;
    int32_t a = m->x[0], b = m->x[1], c = m->x[2];
    int32_t want0 = -1, want1 = -1, got0 = m->units[0], got1 = m->units[1];
    switch (t->model) {
    case MD_JUMPS: {
        int32_t start = 0, end = 0;
        for (int i = 0; i < m->n; i++)
            if (m->st[i].g == GE_JUMP) {
                if (!end && !start)
                    start = m->st[i].a;
                end = m->st[i].want;
            }
        want0 = m->nhop, want1 = iabs((int)(end - start)), got1 = m->units[2];
        if (m->pos != end)
            want0 = -2;
        break;
    }
    case MD_PLACES: case MD_MONEY:
        want0 = t->var == V_PSUM && t->model == MD_PLACES ? a + b : t->var == V_BLOCKS && t->model == MD_PLACES ? a * 100 + b * 10 + c : a;
        break;
    case MD_GIVE: want0 = a + b; break;
    case MD_GROUPS: want0 = a * b; break;
    case MD_ARRAY:
        switch (t->var) {
        case V_BUILD: case V_GROW: case V_DH: case V_KNOWN: want0 = a * b; break;
        case V_TURN: want0 = a * b, want1 = a * b; break;
        case V_ASSOC: want0 = a * b * c; break;
        case V_SPLIT: want0 = a * b, want1 = a * c; break;
        default: break;
        }
        break;
    case MD_AREA: want0 = t->var == V_DIV ? a : a * b; break;
    case MD_PEEL: want0 = a; break;
    case MD_FRIENDLY: want0 = a * b * c; break;
    case MD_TAPE:
        want0 = t->var == V_PPW ? b : t->var == V_CMP ? (c ? a * b : a + b) : a * b;
        want1 = want0;
        break;
    case MD_STRIP:
        want0 = t->var == V_EQUIV ? b * c : b, want1 = t->var == V_EQUIV ? a * c : a;
        break;
    case MD_PLOT: {
        int cnt = 0;
        for (int i = 0; i < a; i++)
            cnt += ((b >> (4 * i)) & 15) == c;
        want0 = a, want1 = cnt;
        break;
    }
    case MD_COLS: want0 = t->var == V_CMUL ? a * b : c ? a - b : a + b; break;
    default: break;
    }
    if (want0 > 65535)
        want0 = 65535;
    if ((want0 >= -1 && want0 != -1 && got0 != want0) || want0 == -2 || (want1 >= 0 && got1 != want1)) {
        api->snprintf(why, (size_t)n, "%s: the picture shows %ld / %ld, the numbers %ld / %ld", t->cc, (long)got0, (long)got1,
                      (long)want0, (long)want1);
        return 1;
    }
    why[0] = 0;
    return 0;
}

/* ------------------------------------------------------------------ the grown-ups */
#define PAGE_ROWS 6
void mq_strat_parent(mq_t *q, int y)
{
    mq_strat_t *m = SM;
    const modx_api_t *api = q->api;
    mq_more_t *mo = rec(q);
    char b[64], nm[40];
    int pages = (STG_COUNT + PAGE_ROWS - 1) / PAGE_ROWS, pg = m->page % pages, seen = 0, used = 0;
    for (int s = 0; s < STG_COUNT; s++)
        seen += mo->strat_seen[s] || mo->strat_help[s] || mo->strat_used[s] ? 1 : 0, used += mo->strat_used[s] ? 1 : 0;
    api->snprintf(b, sizeof b, "Ways to work it (Common Core): %d seen, %d used", seen, used);
    mq_text_fit(q, api->font_small, 4, y, 312, b, Q_PURPLE, false);
    y += 14;
    for (int i = 0; i < PAGE_ROWS; i++) {
        int s = pg * PAGE_ROWS + i;
        if (s >= STG_COUNT)
            break;
        int wk, g = mq_strat_grade(s, &wk);
        bool open = mq_strat_open(q, s);
        uint16_t c = mo->strat_used[s] ? Q_GREEN : mo->strat_seen[s] || mo->strat_help[s] ? Q_HOTPINK : open ? Q_YELLOW : Q_GREY;
        mq_circle(q, 7, y + 5, 3, c);
        mq_text_fit(q, api->font_small, 14, y, 250, tab[s].cc, Q_INK, false);
        if (mo->strat_used[s])
            api->snprintf(b, sizeof b, "used %d", mo->strat_used[s]);
        else if (mo->strat_seen[s] || mo->strat_help[s])
            api->snprintf(b, sizeof b, "seen");
        else
            api->snprintf(b, sizeof b, open ? "now" : "later");
        mq_text_fit(q, api->font_small, 268, y, 48, b, c == Q_GREY || c == Q_YELLOW ? Q_DIM : c, false);
        mq_strat_name(q, s, nm, sizeof nm);
        api->snprintf(b, sizeof b, "%s (%s, school: grade %d wk %d)", nm, tab[s].std, g, wk);
        mq_text_fit(q, api->font_small, 22, y + 12, 294, b, Q_DIM, false);
        y += 26;
    }
    api->snprintf(b, sizeof b, "page %d/%d: green used, pink seen, yellow at school", pg + 1, pages);
    mq_text_fit(q, api->font_small, 4, MQ_Y0 + 194, 236, b, Q_DIM, false);
    mq_add_btn(q, 244, MQ_Y0 + 190, 72, 16, B_STRATS, 1);
    mq_rrect(q, 244, MQ_Y0 + 190, 72, 16, 6, Q_SKY);
    mq_text_c(q, api->font_small, 280, MQ_Y0 + 192, "more", Q_INK);
}
void mq_strat_page(mq_t *q) { q->strat.page = (uint8_t)((q->strat.page + 1) % ((STG_COUNT + PAGE_ROWS - 1) / PAGE_ROWS)); }

void mq_strat_log(mq_t *q, void (*out)(void *user, const char *line), void *user)
{
    mq_more_t *mo = rec(q);
    char b[120];
    for (int s = 0; s < STG_COUNT; s++)
        if (mo->strat_seen[s] || mo->strat_used[s] || mo->strat_help[s]) {
            q->api->snprintf(b, sizeof b, "way,%s,%s,%s,seen %d,used %d,helper %d", tab[s].std, tab[s].cc, tab[s].nm, mo->strat_seen[s],
                             mo->strat_used[s], mo->strat_help[s]);
            out(user, b);
        }
}

void mq_strat_state(mq_t *q, void (*kv)(const char *, const char *, void *), void *user)
{
    mq_strat_t *m = SM;
    char b[48];
    if (m->on) {
        q->api->snprintf(b, sizeof b, "%d %d %d %d/%d", m->id, m->mode, m->phase, m->cur, m->n);
        kv("mq_strat", b, user);
    } else
        kv("mq_strat", "", user);
    q->api->snprintf(b, sizeof b, "%d", q->prob.way);
    kv("mq_way", b, user);
}

/* `mq strat [list | S [show|use|bulb] [N...] | act V | next | try | back | auto | order S | check | page]` */
int mq_strat_console(mq_t *q, int argc, char **argv)
{
    mq_strat_t *m = SM;
    const modx_api_t *api = q->api;
    long v = 0;
    const char *sub = argc > 2 ? argv[2] : "";
    if (!api->strcmp(sub, "list")) {
        for (int s = 0; s < STG_COUNT; s++) {
            char nm[40];
            mq_strat_name(q, s, nm, sizeof nm);
            api->con_printf("%d %s %s (%s) %s seen %d used %d\n", s, mq_strat_open(q, s) ? "open" : "----", tab[s].cc, tab[s].std, nm,
                            rec(q)->strat_seen[s], rec(q)->strat_used[s]);
        }
        return 0;
    }
    if (!api->strcmp(sub, "act") && argc > 3 && api->con_parse_int(argv[3], &v))
        mq_strat_act(q, (int)v);
    else if (!api->strcmp(sub, "next"))
        teach_next(q);
    else if (!api->strcmp(sub, "try") && m->on && m->phase == SP_TEACH) {
        say(q, "Your turn: try one!");
        do_begin(q);
    } else if (!api->strcmp(sub, "back"))
        mq_strat_close(q);
    else if (!api->strcmp(sub, "done") && m->on && m->phase == SP_DONE)
        done_now(q);
    else if (!api->strcmp(sub, "auto")) { /* every step right, the friend's way (tests) */
        for (int k = 0; k < SS_MAX && m->on && m->phase == SP_DO && m->cur < m->n; k++) {
            int c0 = m->cur;
            auto_step(q);
            m->cur = (uint8_t)c0;
            next_step(q);
        }
    } else if (!api->strcmp(sub, "lesson") && q->scene == SC_PLAY) /* the help paths' lesson for her order */
        mq_lesson_start(q, q->prob.skill, q->prob.diff, &q->prob);
    else if (!api->strcmp(sub, "page"))
        mq_strat_page(q);
    else if (!api->strcmp(sub, "bulb") && q->scene == SC_PLAY)
        mq_strat_bulb(q);
    else if (!api->strcmp(sub, "order") && argc > 3 && api->con_parse_int(argv[3], &v) && v >= 0 && v < STG_COUNT &&
             q->scene == SC_PLAY) { /* her order now becomes one of way V (its numbers, or N...) */
        int32_t x[6];
        order_numbers(q, (int)v, 0, x);
        for (int i = 4; i < argc && i < 10; i++) {
            long n;
            if (api->con_parse_int(argv[i], &n))
                x[i - 4] = (int32_t)n;
        }
        mq_strat_close(q);
        mq_strat_make(q, &q->prob, (int)v, x);
        mq_game_order(q);
    } else if (!api->strcmp(sub, "check")) {
        char why[120];
        int r = mq_strat_check(q, why, sizeof why);
        api->con_printf("check %s\n", r ? why : "ok");
        return r ? 1 : 0;
    } else if (argc > 2 && api->con_parse_int(sub, &v) && v >= 0 && v < STG_COUNT) {
        const char *how = argc > 3 ? argv[3] : "show";
        int mode = !api->strcmp(how, "use") ? SM_ORDER : !api->strcmp(how, "bulb") ? SM_BULB : SM_SHOW;
        int32_t x[6];
        example_numbers((int)v, x);
        for (int i = 4; i < argc && i < 10; i++) {
            long n;
            if (api->con_parse_int(argv[i], &n))
                x[i - 4] = (int32_t)n;
        }
        if (q->scene == SC_PLAY)
            mq_strat_start(q, (int)v, mode, x);
    }
    if (m->on)
        api->con_printf("strat on %d (%s) mode %d phase %d step %d of %d beat %d pos %ld val %ld\n", m->id, tab[m->id].cc, m->mode,
                        m->phase, m->cur, m->n, m->beat, (long)m->pos, (long)m->val);
    else
        api->con_printf("strat off (order way %d)\n", q->prob.way);
    return 0;
}

/* ------------------------------------------------------------------ lessons for the help paths */
int mq_lesson_for(mq_t *q, int skill, int rung, const mq_prob_t *order, int32_t ex[6])
{
    int32_t x[6] = {0};
    int s = order ? mq_strat_fit(q, order, 0, x) : -1;
    if (s >= 0 && tab[s].model == MD_COLS)
        s = -1;
    if (s < 0) /* the order's skill: its earliest open way */
        for (int k = 0; k < STG_COUNT; k++)
            if (tab[k].skill == skill && tab[k].model != MD_COLS && mq_strat_open(q, k) && (s < 0 || tab[k].week < tab[s].week))
                s = k;
    if (s < 0)
        return -1;
    for (int t = 0; t < 30; t++) { /* a similar one: the same way, as hard, other numbers */
        order_numbers(q, s, rung >= 3 ? 1 : 0, ex);
        if (fits_numbers(s, ex) && (ex[0] != x[0] || ex[1] != x[1] || ex[2] != x[2]))
            return s;
    }
    example_numbers(s, ex);
    return s;
}
bool mq_lesson_start(mq_t *q, int skill, int rung, const mq_prob_t *order)
{
    int32_t ex[6];
    int s = mq_lesson_for(q, skill, rung, order, ex);
    if (s < 0 || q->scene != SC_PLAY)
        return false;
    mq_strat_start(q, s, SM_LESSON, ex);
    return true;
}

/* ------------------------------------------------------------------ the error patterns' pictures (mq_diag.c) */
/* where a way's model shows best where her answer went wrong: the order worked to the end in it (a lost
 * partial product: every piece of the floor with its product; adding for times: the bags filled; a skip
 * count off: the hops on the path; an hour as 100 minutes: the time line). True when drawn. */
bool mq_strat_diag_picture(mq_t *q, int pt)
{
    static const uint8_t pref[4][4] = {{PT_LOST_PARTIAL, STG_AREA, STG_PPROD, STG_SPLIT},
                                       {PT_ADD_FOR_TIMES, STG_GROUPS, STG_ARRAY, STG_TAPE_EQ},
                                       {PT_SKIP_STEP, STG_FLINE, STG_JUMPS, STG_JUMPS},
                                       {PT_HOUR_100, STG_TIMELINE, STG_TIMELINE, STG_TIMELINE}};
    mq_strat_t *m = SM;
    if (m->on)
        return false;
    for (int r = 0; r < 4; r++) {
        if (pref[r][0] != pt)
            continue;
        for (int w = 0; w < 3; w++) {
            int32_t x[6];
            int s = -1;
            for (int k = 0; k < 4 && s != pref[r][1 + w]; k++)
                s = mq_strat_fit(q, &q->prob, k, x);
            if (s != pref[r][1 + w])
                continue;
            m->id = (uint8_t)s, m->mode = SM_SHOW, m->phase = SP_DONE;
            plan(q, s, x);
            for (int k = 0; k < SS_MAX && m->cur < m->n; k++)
                auto_step(q);
            m->cur = m->n;
            mq_rrect(q, MX0 - 2, MY0 - 2, MX1 - MX0 + 4, MY1 - MY0 + 4, 10, Q_WHITE);
            draw_model(q);
            char nm[40];
            mq_strat_name(q, s, nm, sizeof nm);
            mq_text_fit(q, q->api->font_small, MX0 + 2, MY1 - 12, 200, nm, Q_PURPLE, false);
            return true;
        }
    }
    return false;
}
