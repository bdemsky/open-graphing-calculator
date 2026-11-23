/*
 * Math Quest: the engine that chooses every order, wherever she plays (design: docs/MATHQUEST.md,
 * "Exploration is free; learning adapts to wherever she plays").
 *
 * Every world, building, game, toy and room is open from the start. Where she goes picks the
 * flavour (the place's stories, its game), never the maths: each order's skill and difficulty
 * come from here --
 *
 *   1. the placement: a new player's first games ask twelve orders across four strands
 *      (multiplication and division, place value and sums, fractions, measurement), starting at
 *      mid-grade 3, stepping up a grade only after two rights in a row there and down after a miss,
 *      one grade at a time, never above grade 4 (decimals and grade 5 never come from the
 *      placement). She then starts one grade below what she showed, for confidence. Skills of a
 *      strand below that grade are "assumed": checked now and then (they still need their evidence
 *      to count as passed), not taught first. It looks like any first game: no screen of its own;
 *   2. her current goals: skills not yet passed whose prerequisites she knows, the lowest grade
 *      first, those she has started before new ones, different strands in turn (operations mix);
 *      the place's own skills and the round's game weigh in, so the bakery prefers fractions and
 *      Bubble Pop prefers what pops well -- but any skill comes to any place when it is due.
 *      Grade 5 comes wherever she is once she is ready (mq_grade5_ready) or placed there;
 *   3. the interleaved review of earlier skills (mq_learn.c) and the 85 % controller of each
 *      skill's difficulty (mq_coach.c) as before.
 *
 * The pacing of the games is here too: every order carries an expected think time (its skill,
 * difficulty, digits, steps and story length, calibrated by her own first-answer times), heavy
 * orders prefer the calm games (the till, the heist, building, cooking amounts) and slow the action
 * games to a hover, and each game keeps a closed-loop speed level for her (a miss or an escape
 * lowers it at once, quick rights raise it slowly, idle time hovers): the speed used is the lower of
 * that and the think-time cap (nothing escapes before 1.5 x her expected think time).
 */
#include "mq.h"

#define RAMP_ORDERS 150 /* her first sessions (orders): a slow ramp */
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

mq_pace_t *mq_pace(mq_t *q) { return &mq_more(q)->pace; }

/* ------------------------------------------------------------------ strands */
int mq_strand(int s)
{
    switch (s) {
    case S_MUL10: case S_MULTI: case S_DIVIDE: case S_MULBIG: case S_DIV2: case S_ORDEROPS: case S_EXPR:
    case S_TWOPAT: return STR_MULDIV;
    case S_POW10: case S_DEC1000: case S_ROUNDDEC: case S_DECOPS: return STR_PLACE;
    case S_COORD: case S_CONVERT5: case S_LINEPLOT5: case S_VOLUME: case S_SHAPES5: return STR_MEAS;
    default: break;
    }
    switch (mq_skill[s >= 0 && s < MQ_NSKILL ? s : 0].world) {
    case G_GARDEN: return STR_MULDIV;
    case G_POST: return STR_PLACE;
    case G_BAKERY: case G_STAR: return STR_FRAC;
    case G_SKY: return STR_PLACE;
    default: return STR_MEAS;
    }
}

/* ------------------------------------------------------------------ the school year */
/* The grown-ups may say where school is: her grade (3..5) and how many weeks into the year (0..40). A
 * pacing map of grades 3-5 (the usual order of a year: multiplication, then place value and measures,
 * bigger facts, area, fractions, data, shapes in grade 3; ...) says the week each skill is usually
 * taught in its grade. A skill is taught when its grade is below hers, or it is hers and its week has
 * come. The placement starts each strand at the highest grade whose probes are taught and never asks
 * a topic school has not reached yet (backlog (a)). */
static const uint8_t pace_week[S_COUNT] = {
    /* grade 3: 1 multiplication and division, 2 measures and place value, 3 the facts 6-9 and tens, 4 area,
     * 5 fractions, 6 data, 7 shapes and perimeter */
    [S_GROUPS] = 1, [S_SHARE] = 2, [S_MDWORD] = 3, [S_UNKNOWN] = 4, [S_PROPS] = 4, [S_DIVFACT] = 5,
    [S_TIME1] = 6, [S_ELAPSED] = 7, [S_MASSVOL] = 8, [S_ROUND10] = 9, [S_ADDSUB] = 10, [S_SHOPWORD] = 11,
    [S_FACTS] = 12, [S_MUL10] = 15, [S_TWOSTEP] = 16, [S_PATTERN] = 17,
    [S_AREACOUNT] = 18, [S_AREAMUL] = 19, [S_AREASPLIT] = 20, [S_AREAL] = 21,
    [S_UNITFRAC] = 22, [S_PARTITION] = 22, [S_FRACLINE] = 24, [S_EQUIV] = 25, [S_WHOLES] = 26, [S_CMPFRAC] = 27,
    [S_SCALED] = 29, [S_RULER] = 30, [S_SHAPES] = 32, [S_PERIM] = 33,
    /* grade 4: 1 place value to a million, 2 units, 3 multi-digit multiplication and division, 4 angles and
     * lines, 5 fraction equivalence and operations, 6 decimals, 7 measures, plots, tables */
    [S_PVBIG] = 1, [S_CMPBIG] = 2, [S_ROUNDBIG] = 3, [S_ADDSUBBIG] = 4, [S_CONVERT] = 6, [S_MEASWORD] = 7,
    [S_TIMES_AS] = 8, [S_MULTI] = 9, [S_FACTORS] = 11, [S_DIVIDE] = 12, [S_REMWORD] = 13, [S_RULE] = 15,
    [S_LINES] = 16, [S_ANGLES] = 17, [S_PROTRACTOR] = 17, [S_ANGLEADD] = 18, [S_CLASSIFY] = 19, [S_SYMMETRY] = 19,
    [S_EQUIVMUL] = 20, [S_CMPDIFF] = 22, [S_ADDFRAC] = 23, [S_MIXED] = 24, [S_FRACWHOLE] = 26,
    [S_TENTHS] = 28, [S_DECIMAL] = 29, [S_CMPDEC] = 30, [S_MONEYDEC] = 31,
    [S_FORMULA] = 32, [S_CHANGE] = 33, [S_LINEPLOT] = 34, [S_TABLE] = 35,
    /* grade 5: 1 place value and decimals, 2 multi-digit and decimal operations, 3 adding fractions, 4
     * multiplying and dividing fractions, 5 volume and shapes, 6 the coordinate plane */
    [S_POW10] = 1, [S_DEC1000] = 2, [S_ROUNDDEC] = 4, [S_DECOPS] = 5,
    [S_MULBIG] = 6, [S_DIV2] = 9, [S_EXPR] = 10, [S_ORDEROPS] = 11,
    [S_FRUNLIKE] = 13, [S_FRACWORD] = 16,
    [S_FRACDIV] = 18, [S_FRACMUL] = 19, [S_LINEPLOT5] = 20, [S_CONVERT5] = 21, [S_SCALING] = 22, [S_UNITDIV] = 24,
    [S_VOLUME] = 26, [S_SHAPES5] = 30, [S_COORD] = 31, [S_TWOPAT] = 33,
    /* (grade 2, known by grade 3: S_PV3, S_CMP3, S_TIME5, S_COINS, S_GRAPH1 -- week 0) */
};
int mq_pace_week(int s) { return s >= 0 && s < S_COUNT ? pace_week[s] : 0; }

/* where school is (the grown-ups'): her grade 3..5 and its week (0: not said) */
int mq_school(mq_t *q, int *week)
{
    const mq_pace_t *pc = mq_pace(q);
    if (week)
        *week = pc->school_week;
    return pc->school_grade >= 3 && pc->school_grade <= 5 ? pc->school_grade : 0;
}
void mq_set_school(mq_t *q, int grade, int week)
{
    mq_pace_t *pc = mq_pace(q);
    int was = pc->school_grade;
    pc->school_grade = (uint8_t)(grade >= 3 && grade <= 5 ? grade : 0);
    pc->school_week = (uint8_t)(week < 0 ? 0 : week > MQ_SCHOOL_WEEKS ? MQ_SCHOOL_WEEKS : week);
    if (pc->school_grade && pc->school_grade != was && mq_prof(q)->problems > 0)
        mq_set_start_grade(q, pc->school_grade); /* an existing player: the new grade takes effect now (the user: "I set
                                                  * 4th grade ... not much difference") */
    q->dirty = true;
}
/* has school taught skill s yet? (no school said: everything is fair game) */
bool mq_school_taught(mq_t *q, int s)
{
    int wk, g = mq_school(q, &wk);
    if (!g || s < 0 || s >= S_COUNT)
        return true;
    int sg = mq_skill[s].grade;
    return sg < g || (sg == g && pace_week[s] <= wk);
}

int mq_start_grade(mq_t *q)
{
    int g = mq_pace(q)->start_grade;
    return g >= 2 && g <= 5 ? g : MQ_START_GRADE;
}

/* the grown-ups set the starting grade: every strand starts there (no placement needed) */
/* The grown-ups set her grade: it takes effect at once, for a new player and an existing one alike. Every strand
 * starts there (raised or lowered): what comes before it is assumed known (checked now and then, lightly), the
 * goals are the grade's own topics (what school has taught first), and the slow ramp of a new player's first
 * sessions does not hold a set grade back (PL_SET). The skills she already passed stay passed. */
void mq_set_start_grade(mq_t *q, int g)
{
    mq_pace_t *pc = mq_pace(q);
    pc->start_grade = (uint8_t)(g < 2 ? 2 : g > 5 ? 5 : g);
    for (int k = 0; k < STR_COUNT; k++)
        pc->strand[k] = (uint8_t)(k == STR_MULDIV ? imax(3, pc->start_grade) : pc->start_grade);
    pc->placed |= PL_SET;
    for (int k = 0; k < 3; k++) /* (the strands' recent answers start afresh at the new level) */
        pc->str_hist[k] = 0;
    pc->str_hist3 = 0;
    q->dirty = true;
}

/* a skill whose strand she was placed above: checked now and then, not taught first */
static bool assumed(mq_t *q, int s)
{
    const mq_pace_t *pc = mq_pace(q);
    if (s == S_FACTS) { /* the facts are known one by one (their boxes): assumed from her grade only once most
                         * are solid (a strand moved up to grade 4 left them "known" and they were no longer asked) */
        const mq_profile_t *p = mq_prof(q);
        int n = 0, k = 0;
        for (int f = 0; f < 169; f++)
            if (mq_fact_required(p, f) && !mq_fact_trivial(f))
                n++, k += p->fact[f].box >= 3;
        if (k * 100 < n * 70)
            return false;
    }
    int g = pc->strand[mq_strand(s)];
    if (!g && (pc->placed & PL_SET))
        g = mq_start_grade(q);
    return g && mq_skill[s].grade < g;
}
bool mq_skill_assumed(mq_t *q, int s) { return s >= 0 && s < MQ_NSKILL && assumed(q, s); }

static bool known(mq_t *q, int s)
{
    const mq_skrec_t *r = &mq_prof(q)->sk[s];
    return (r->flags & SK_PASSED) || r->mastery >= 60 || assumed(q, s);
}

/* decimals: their base (fractions, place value) has to be solid first */
bool mq_decimal_skill(int s)
{
    return s == S_TENTHS || s == S_DECIMAL || s == S_CMPDEC || s == S_MONEYDEC || s == S_DEC1000 || s == S_POW10 ||
           s == S_ROUNDDEC || s == S_DECOPS || s == S_CONVERT5;
}
/* the play day she must have reached before decimals and grade 5 (the first sessions ramp slowly) */
#define LATE_DAY 3
/* decimals and grade 5 only after their grade-4 base is solidly mastered across sessions -- passed
 * with a high mastery (not merely assumed from the placement), and not in her first sessions -- unless
 * the grown-ups set her starting grade there */
static bool late_ok(mq_t *q, int s)
{
    const mq_profile_t *p = mq_prof(q);
    if (mq_pace(q)->placed & PL_SET && mq_start_grade(q) > mq_skill[s].grade)
        return true;
    if (p->days < LATE_DAY)
        return false;
    for (int k = 0; k < 2; k++)
        if (mq_prereq[s][k]) {
            int r = mq_prereq[s][k] - 1;
            if (mq_skill[r].grade == 5 && mq_skill[s].grade == 5)
                continue; /* (a grade-5 prerequisite is gated itself) */
            if (!(p->sk[r].flags & SK_PASSED) || p->sk[r].mastery < 80)
                return false;
        }
    return true;
}
bool mq_skill_late_ok(mq_t *q, int s) { return s >= 0 && s < MQ_NSKILL && late_ok(q, s); }

/* may skill s be a goal now? its prerequisites known; decimals and grade 5 once their base is solid; in
 * her first sessions nothing above her strand's starting grade (a slow ramp) */
bool mq_engine_ramp_ok(mq_t *q, int s)
{
    const mq_pace_t *pc0 = mq_pace(q);
    return !(mq_prof(q)->problems < RAMP_ORDERS && (pc0->placed & PL_DONE) && !(pc0->placed & PL_SET) &&
             pc0->strand[mq_strand(s)] && mq_skill[s].grade > pc0->strand[mq_strand(s)]);
}
static bool reachable(mq_t *q, int s)
{
    if (!mq_engine_ramp_ok(q, s))
        return false;
    { /* never two grades above where her strand stands (it steps down when she struggles) */
        const mq_pace_t *pc = mq_pace(q);
        int g = pc->strand[mq_strand(s)];
        if (g && (pc->placed & PL_DONE) && !(pc->placed & PL_SET) && mq_skill[s].grade > g + 1)
            return false;
        /* a grade above the one school is in: only once her strand is there (she sailed past her grade) */
        int sg = mq_school(q, NULL);
        if (sg && mq_skill[s].grade > sg && g < mq_skill[s].grade)
            return false;
    }
    for (int k = 0; k < 2; k++)
        if (mq_prereq[s][k] && !known(q, mq_prereq[s][k] - 1))
            return false;
    if ((mq_decimal_skill(s) || mq_skill[s].grade == 5) && !late_ok(q, s))
        return false;
    if (mq_skill[s].grade == 5)
        return mq_grade5_ready(mq_prof(q)) || mq_start_grade(q) >= 5;
    return true;
}

/* does the game tell this skill well? (its own games; the pick games tell nearly anything) */
static int fit_score(int s, int mech)
{
    if (mech < 0 || mech >= ME_COUNT)
        return 0;
    int g[4], n = mq_skill_games(s, g);
    for (int i = 0; i < n; i++)
        if (g[i] == mech)
            return i == 0 ? 24 : 18;
    if (mech == ME_POP || mech == ME_CATCH || mech == ME_ZAP || mech == ME_GATES || mech == ME_KART)
        for (int i = 0; i < n; i++)
            if (g[i] == ME_POP || g[i] == ME_CATCH || g[i] == ME_ZAP)
                return 8;
    return 0;
}

static void recent_push(mq_t *q, int s, bool missed)
{
    mq_pace_t *pc = mq_pace(q);
    for (int i = 7; i > 0; i--)
        pc->recent[i] = pc->recent[i - 1];
    pc->recent[0] = (uint8_t)(s + 1);
    pc->recent_miss = (uint8_t)(pc->recent_miss << 1 | (missed ? 1 : 0));
}
/* The strand's own controller (beside each skill's 85 % one, which needs six answers of that skill): her
 * last first answers in each strand; below 70 % over five or more the strand steps down a grade (the
 * skills below it are no longer assumed: they become her goals again), all of the last seven right
 * lets it step up a grade (never above 4 on its own: grade 5 waits for its solid base) */
static uint8_t *str_hist(mq_pace_t *pc, int k) { return k < 3 ? &pc->str_hist[k] : &pc->str_hist3; }
static void strand_answer(mq_t *q, int s, bool missed)
{
    mq_pace_t *pc = mq_pace(q);
    int k = mq_strand(s);
    if (k < 0 || k >= STR_COUNT || !pc->strand[k] || mq_placing(q))
        return;
    uint8_t *h = str_hist(pc, k), r = *h ? *h : 1;
    if (r >= 0x80) /* keep the last 7 */
        r = (uint8_t)(0x40 | (r & 0x3F));
    r = (uint8_t)(r << 1 | (missed ? 0 : 1));
    int n = 0;
    while ((r >> n) > 1)
        n++;
    int ok = 0;
    for (int i = 0; i < n; i++)
        ok += (r >> i) & 1;
    int floor = k == STR_MULDIV ? 3 : 2;
    /* (at or under the grade the grown-ups set, it steps down only when she clearly struggles: their setting holds) */
    bool held = (pc->placed & PL_SET) && pc->strand[k] <= mq_start_grade(q);
    if (held ? n >= 7 && ok * 100 < n * 50 && pc->strand[k] > floor : n >= 5 && ok * 100 < n * 70 && pc->strand[k] > floor)
        pc->strand[k]--, r = 1; /* struggling: a grade down */
    else if (n >= 7 && ok == n && pc->strand[k] < 4 && mq_prof(q)->problems >= RAMP_ORDERS)
        pc->strand[k]++, r = 1; /* sailing: a grade up (after her first sessions' slow ramp) */
    *h = r;
    q->dirty = true;
}
int mq_strand_rate(mq_t *q, int k, int *n)
{
    uint8_t r = *str_hist(mq_pace(q), k & 3);
    int c = 0, ok = 0;
    while (r && (r >> c) > 1)
        ok += (r >> c) & 1, c++;
    if (n)
        *n = c;
    return c ? ok * 100 / c : 0;
}
void mq_engine_answered(mq_t *q, int s, bool missed)
{
    recent_push(q, s, missed);
    strand_answer(q, s, missed);
}

/* misses of skill s among the last few orders (the helper friend's "same skill within a short span") */
int mq_engine_recent_misses(mq_t *q, int s, int span)
{
    const mq_pace_t *pc = mq_pace(q);
    int n = 0;
    for (int i = 0; i < span && i < 8; i++)
        n += pc->recent[i] == s + 1 && ((pc->recent_miss >> i) & 1);
    return n;
}

/* ------------------------------------------------------------------ the placement */
/* the order that probes strand k at grade g (a skill of that strand and grade that the pick games tell) */
static int probe_skill(int k, int g, int turn)
{
    static const uint8_t t[STR_COUNT][4][2] = {
        /* grade 2, 3, 4, 5 (two each, in turn) */
        {{S_GROUPS, S_GROUPS}, {S_GROUPS, S_SHARE}, {S_TIMES_AS, S_MULTI}, {S_MULBIG, S_DIV2}},
        {{S_PV3, S_CMP3}, {S_ADDSUB, S_ROUND10}, {S_ADDSUBBIG, S_PVBIG}, {S_POW10, S_DECOPS}},
        {{S_UNITFRAC, S_UNITFRAC}, {S_UNITFRAC, S_WHOLES}, {S_EQUIVMUL, S_ADDFRAC}, {S_FRUNLIKE, S_FRACMUL}},
        {{S_TIME5, S_COINS}, {S_AREAMUL, S_TIME1}, {S_CONVERT, S_FORMULA}, {S_VOLUME, S_CONVERT5}}};
    g = g < 2 ? 2 : g > 5 ? 5 : g;
    return t[k & 3][g - 2][turn & 1];
}
#define PLACE_N 12  /* three orders a strand */
#define PLACE_TOP 4 /* never above grade 4 */
#define PLACE_RUN 0x80 /* place_lv: one right at this grade already (two in a row step up) */

/* the probe of strand k at grade g that school has taught (this turn's, else the other one); -1 none */
static int probe_taught(mq_t *q, int k, int g, int turn)
{
    int s = probe_skill(k, g, turn);
    if (mq_school_taught(q, s))
        return s;
    s = probe_skill(k, g, turn + 1);
    return mq_school_taught(q, s) ? s : -1;
}
/* the grade a strand's placement starts at: 3 (mid-way); with school said, her grade (at most
 * PLACE_TOP), lower while its probes there are not taught yet */
static int place_start(mq_t *q, int k)
{
    int g = mq_school(q, NULL);
    if (!g)
        return 3;
    g = g > PLACE_TOP ? PLACE_TOP : g;
    while (g > 2 && probe_taught(q, k, g, 0) < 0)
        g--;
    return g;
}

bool mq_placing(mq_t *q)
{
    const mq_pace_t *pc = mq_pace(q);
    return !(pc->placed & (PL_DONE | PL_SET)) && mq_prof(q)->used && pc->place_i < PLACE_N &&
           pc->place_done != (1u << STR_COUNT) - 1;
}

static void place_finish(mq_t *q)
{
    mq_pace_t *pc = mq_pace(q);
    for (int k = 0; k < STR_COUNT; k++) /* one grade below what she showed, for confidence */
        pc->strand[k] = (uint8_t)imax(k == STR_MULDIV ? 3 : 2, pc->place_ok[k] - 1);
    pc->placed |= PL_DONE;
    q->dirty = true;
}

/* the next placement order: the skill (and its difficulty in *diff), -1 when the placement is over */
int mq_place_next(mq_t *q, int *diff)
{
    mq_pace_t *pc = mq_pace(q);
    if (!mq_placing(q))
        return -1;
    if (pc->place_i == 0) /* every strand starts at mid-grade 3; with school said, at her grade, lower where
                           * that strand's topics have not been taught yet */
        for (int k = 0; k < STR_COUNT; k++)
            pc->place_lv[k] = (uint8_t)place_start(q, k), pc->place_ok[k] = 0;
    for (int tries = 0; tries < 4 * STR_COUNT; tries++) {
        int k = (pc->place_i + tries) % STR_COUNT;
        if ((pc->place_done >> k) & 1)
            continue;
        int g = pc->place_lv[k] & 7, s = probe_taught(q, k, g, pc->place_i / STR_COUNT);
        if (s < 0) { /* nothing of this strand taught at this grade: a grade lower, or the strand is done */
            if (g > 2)
                pc->place_lv[k] = (uint8_t)(g - 1);
            else
                pc->place_done |= (uint8_t)(1 << k);
            if (!mq_placing(q)) {
                place_finish(q);
                return -1;
            }
            continue;
        }
        *diff = mq_rung_of_band(s, g <= 2 ? 0 : 1); /* (the probe: low on its ladder) */
        return s;
    }
    return -1;
}

/* a placement order answered (first try) */
void mq_place_answer(mq_t *q, int skill, bool right)
{
    mq_pace_t *pc = mq_pace(q);
    if (!mq_placing(q))
        return;
    int k = mq_strand(skill), g = pc->place_lv[k] & 7;
    bool run = pc->place_lv[k] & PLACE_RUN;
    if (right) {
        pc->place_ok[k] = (uint8_t)imax(pc->place_ok[k], g);
        if (g >= PLACE_TOP || (run && probe_taught(q, k, g + 1, 0) < 0)) /* (the top, or school is not there yet) */
            pc->place_done |= (uint8_t)(1 << k);
        else if (run) /* two in a row here: one grade up */
            pc->place_lv[k] = (uint8_t)(g + 1);
        else
            pc->place_lv[k] = (uint8_t)(g | PLACE_RUN);
    } else {
        if (pc->place_ok[k] || g <= 2) /* it stepped up and found the edge, or the floor */
            pc->place_done |= (uint8_t)(1 << k);
        else
            pc->place_lv[k] = (uint8_t)(g - 1); /* one grade down (and the run starts again) */
    }
    pc->place_i++;
    if (!mq_placing(q))
        place_finish(q);
    q->dirty = true;
}

/* ------------------------------------------------------------------ "focus this week" */
/* The grown-ups can put what school sends home in focus: whole tables (x 4, / 4), single facts and up
 * to two skills, optionally for this week (seven play days). A share of the fact moments (40-60 %,
 * 50 by default) then asks focus facts, with the Leitner intervals halved (fast mastery), mixed into
 * whatever she plays under the usual no-repeat rules; once every focus fact is fluent on two play days
 * ("ready for the test" on the grown-ups' screen) the share drops back to maintenance (10 %). The child
 * sees nothing of it. */
bool mq_focus_active(mq_t *q)
{
    const mq_pace_t *pc = mq_pace(q);
    bool any = pc->focus_mul || pc->focus_div || pc->focus_skill[0] || pc->focus_skill[1];
    for (int i = 0; i < 8 && !any; i++)
        any = pc->focus_fact[i] != 0;
    return any && (!pc->focus_end || mq_prof(q)->days <= pc->focus_end);
}
bool mq_focus_fact(mq_t *q, int f)
{
    const mq_pace_t *pc = mq_pace(q);
    if (f < 0 || f >= MQ_NFACT)
        return false;
    int a, b;
    bool dv;
    mq_fact_decode(f, &a, &b, &dv);
    if (dv ? (pc->focus_div >> b) & 1 : ((pc->focus_mul >> a) & 1) || ((pc->focus_mul >> b) & 1))
        return a <= 10 || mq_prof(q)->facts12;
    for (int i = 0; i < 8; i++)
        if (pc->focus_fact[i] == f + 1)
            return true;
    return false;
}
int mq_focus_count(mq_t *q, int *fluent, int *days2)
{
    int n = 0, fl = 0, d2 = 0;
    for (int f = 0; f < MQ_NFACT; f++)
        if (mq_focus_fact(q, f) && mq_fact_required(mq_prof(q), f)) {
            const mq_fact_t *t = &mq_prof(q)->fact[f];
            n++, fl += t->box >= 4 && t->t10 && t->t10 <= 60, d2 += mq_fact_fluent(t);
        }
    if (fluent)
        *fluent = fl;
    if (days2)
        *days2 = d2;
    return n;
}
bool mq_focus_ready(mq_t *q)
{
    int d2, n = mq_focus_count(q, NULL, &d2);
    const mq_pace_t *pc = mq_pace(q);
    for (int k = 0; k < 2; k++)
        if (pc->focus_skill[k] && !(mq_prof(q)->sk[pc->focus_skill[k] - 1].flags & SK_PASSED))
            return false;
    return (n || pc->focus_skill[0] || pc->focus_skill[1]) && d2 == n;
}
int mq_focus_share(mq_t *q)
{
    if (!mq_focus_active(q))
        return 0;
    if (mq_focus_ready(q))
        return 10; /* known: maintenance */
    int s = mq_pace(q)->focus_share;
    return s >= 20 && s <= 80 ? s : 50;
}
/* the focus fact to ask now: a due one from the lowest box, else one not fluent yet (never a recent one) */
int mq_focus_pick(mq_t *q, bool (*recent)(const mq_t *, int))
{
    mq_profile_t *p = mq_prof(q);
    int best = -1, bk = 99;
    int start = mq_rnd(q, 0, MQ_NFACT - 1);
    for (int i = 0; i < MQ_NFACT; i++) {
        int f = (start + i) % MQ_NFACT;
        if (!mq_focus_fact(q, f) || !mq_fact_required(p, f) || recent(q, f))
            continue;
        const mq_fact_t *t = &p->fact[f];
        int k = (t->due <= p->round ? 0 : 10) + t->box + (mq_fact_fluent(t) ? 20 : 0);
        if (k < bk)
            bk = k, best = f;
    }
    return best;
}
bool mq_focus_toggle(mq_t *q, int f)
{
    mq_pace_t *pc = mq_pace(q);
    for (int i = 0; i < 8; i++)
        if (pc->focus_fact[i] == f + 1) {
            pc->focus_fact[i] = 0;
            q->dirty = true;
            return false;
        }
    for (int i = 0; i < 8; i++)
        if (!pc->focus_fact[i]) {
            pc->focus_fact[i] = (int16_t)(f + 1);
            q->dirty = true;
            return true;
        }
    return false;
}
/* `mq focus mul|div N` | `fact A B [div]` | `skill S` | `share P` | `week` | `clear` | (nothing: the state) */
int mq_focus_console(mq_t *q, int argc, char **argv)
{
    const modx_api_t *api = q->api;
    mq_pace_t *pc = mq_pace(q);
    long a = 0, b = 0;
    const char *sub = argc > 2 ? argv[2] : "";
    if (argc > 3)
        api->con_parse_int(argv[3], &a);
    if (argc > 4)
        api->con_parse_int(argv[4], &b);
    if (!api->strcmp(sub, "mul") && a >= 0 && a <= 12)
        pc->focus_mul ^= (uint16_t)(1u << a);
    else if (!api->strcmp(sub, "div") && a >= 1 && a <= 12)
        pc->focus_div ^= (uint16_t)(1u << a);
    else if (!api->strcmp(sub, "fact"))
        mq_focus_toggle(q, mq_fact_index((int)a, (int)b, argc > 5));
    else if (!api->strcmp(sub, "skill") && a >= 0 && a < MQ_NSKILL)
        pc->focus_skill[pc->focus_skill[0] ? 1 : 0] = (uint8_t)(a + 1);
    else if (!api->strcmp(sub, "share"))
        pc->focus_share = (uint8_t)(a < 20 ? 20 : a > 80 ? 80 : a);
    else if (!api->strcmp(sub, "week"))
        pc->focus_end = pc->focus_end ? 0 : (uint16_t)(mq_prof(q)->days + 6);
    else if (!api->strcmp(sub, "clear")) {
        pc->focus_mul = pc->focus_div = 0, pc->focus_end = 0;
        for (int i = 0; i < 8; i++)
            pc->focus_fact[i] = 0;
        pc->focus_skill[0] = pc->focus_skill[1] = 0;
    }
    q->dirty = true;
    int fl, d2, n = mq_focus_count(q, &fl, &d2);
    api->con_printf("focus: %d facts (%d fast, %d known on two days), share %d %%, %s%s\n", n, fl, d2, mq_focus_share(q),
                    mq_focus_active(q) ? "on" : "off", mq_focus_ready(q) ? ", ready for the test" : "");
    return 0;
}

/* ------------------------------------------------------------------ choosing an order */
/* A taste of the strands that need little before them: shapes (3.G, 4.G), time, data. They have few
 * prerequisites, so they can come early in a light share whatever the school's week or the place she plays
 * in (the playtest: "shapes never appear" -- the year's bigger work and her favourite places crowded them
 * out). The skill of the strand to taste: one she has not passed (the lowest grade first, then the one she
 * has seen least), else the one least mastered; -1: none reachable. */
static const uint8_t taste_skills[3][6] = {
    {S_SHAPES + 1, S_PARTITION + 1, S_LINES + 1, S_CLASSIFY + 1, S_SYMMETRY + 1, S_SHAPES5 + 1},
    {S_TIME5 + 1, S_TIME1 + 1, S_ELAPSED + 1},
    {S_GRAPH1 + 1, S_SCALED + 1, S_RULER + 1, S_TABLE + 1, S_LINEPLOT + 1},
};
#define TASTE_EVERY 9 /* one order in nine: a taste, the three strands in turn */
static bool reachable(mq_t *q, int s);
static bool assumed(mq_t *q, int s);
int mq_engine_taste(mq_t *q, int k)
{
    const mq_profile_t *p = mq_prof(q);
    int best = -1, bs = -0x7FFF;
    for (int i = 0; i < 6 && taste_skills[k % 3][i]; i++) {
        int s = taste_skills[k % 3][i] - 1;
        const mq_skrec_t *r = &p->sk[s];
        bool passed = r->flags & SK_PASSED;
        if (!passed && !assumed(q, s) && !reachable(q, s))
            continue;
        if (!mq_school_taught(q, s) && mq_skill[s].grade > imax(3, mq_school(q, NULL))) /* never a grade school is not in */
            continue;
        if ((mq_pace(q)->placed & PL_SET) && mq_skill[s].grade < mq_start_grade(q)) /* (a grade the grown-ups set: its own) */
            continue;
        int sc = passed || assumed(q, s) ? -200 - r->mastery : -20 * mq_skill[s].grade - imin(r->tries, 40);
        sc += mq_rnd(q, 0, 3);
        if (sc > bs)
            bs = sc, best = s;
    }
    return best;
}

/* The skill of the next order in world w, in a round of game `mech` (-1 any). */
int mq_engine_pick(mq_t *q, int w, int mech)
{
    mq_profile_t *p = mq_prof(q);
    const mq_pace_t *pc = mq_pace(q);
    if (p->problems % TASTE_EVERY == TASTE_EVERY / 2) { /* a taste of shapes, time or data, in turn */
        int s = mq_engine_taste(q, (int)(p->problems / TASTE_EVERY));
        if (s >= 0 && s != pc->recent[0] - 1 && mq_round_plays(q, s)) /* (only in a game that plays it, when she picked one) */
            return q->pick_why = PW_TASTE, s;
    }
    /* "focus this week": a share of the orders goes to the focus (the facts, a skill) */
    int fs = mq_focus_share(q);
    if (fs && mq_rnd(q, 0, 99) < fs) {
        int fsk[3], nf = 0;
        if (mq_focus_count(q, NULL, NULL))
            fsk[nf++] = S_FACTS;
        for (int k = 0; k < 2; k++)
            if (pc->focus_skill[k])
                fsk[nf++] = pc->focus_skill[k] - 1;
        if (nf) {
            int s = fsk[mq_rnd(q, 0, nf - 1)];
            if ((s != pc->recent[0] - 1 || nf == 1) && mq_round_plays(q, s))
                return q->pick_why = PW_FOCUS, s;
        }
    }
    int last = pc->recent[0] - 1, last2 = pc->recent[1] - 1;
    int lo = 9, nverify = 0, ngoal = 0;
    bool only = q->gm.chosen && mech >= 0; /* she picked the game: only what it plays (the card's promise) */
    for (int s = 0; s < MQ_NSKILL; s++) {
        if ((p->sk[s].flags & SK_PASSED) || (only && !mq_round_plays(q, s)))
            continue;
        if (assumed(q, s))
            nverify++;
        else if (reachable(q, s))
            ngoal++, lo = s == S_FACTS ? lo : imin(lo, mq_skill[s].grade); /* (the facts: never holding the grade back) */
    }
    /* now and then an assumed skill is checked (quickly: it starts harder and climbs fast) */
    bool verify = nverify && (!ngoal || mq_rnd(q, 0, 99) < (pc->placed & PL_SET ? 8 : 22)); /* (a grade the grown-ups set:
                                                                       * its topics first, what comes before checked lightly) */
    int best = -1, bs = -0x7FFF;
    for (int s = 0; s < MQ_NSKILL; s++) {
        const mq_skrec_t *r = &p->sk[s];
        bool passed = r->flags & SK_PASSED;
        int sc;
        if (only && !mq_round_plays(q, s))
            continue;
        if (verify) {
            if (passed || !assumed(q, s))
                continue;
            sc = -8 * mq_skill[s].grade;
        } else if (ngoal) {
            if (passed || assumed(q, s) || !reachable(q, s))
                continue;
            sc = -14 * (mq_skill[s].grade - lo);
            if (!mq_school_taught(q, s) && s != S_FACTS) /* what school has taught first (the grown-ups' week; the
                                                        * tastes aside; the facts are a thread all year) */
                sc -= 30;
            if (r->tries && r->mastery < 75) /* her current goals: carry on with what she started */
                sc += 14 + r->mastery / 8;
            if (s == S_FACTS) /* the facts are many: they come often while multiplication is on -- a goal she is
                               * on until every fact is fluent (its mastery bar fills after a few rights: the
                               * facts then came only in the Garden, a few an hour, the tables never reached) */
                sc += 10 + (r->tries && r->mastery >= 75 ? 8 : 0);
        } else { /* everything passed: keep it all fresh, in the place's flavour */
            sc = -(int)(r->mastery / 4);
        }
        sc += mq_skill[s].world == w || (w == G_SKY && mq_skill[s].world == G_STAR) ? 22 : 0; /* the place's flavour */
        sc += fit_score(s, mech);
        if (s == last) /* the operations mix: never the same twice, rarely the same strand */
            sc -= 40;
        else if (s == last2)
            sc -= 14;
        if (last >= 0 && mq_strand(s) == mq_strand(last))
            sc -= 22;
        sc += mq_rnd(q, 0, 12);
        if (sc > bs)
            bs = sc, best = s;
    }
    if (best < 0 && only) /* nothing of hers it plays is a goal now: the easiest it plays she can reach, else its own */
        for (int s = 0; s < MQ_NSKILL; s++)
            if (mq_round_plays(q, s) && (best < 0 || (reachable(q, s) && !reachable(q, best)) ||
                                           (reachable(q, s) == reachable(q, best) && mq_skill[s].grade < mq_skill[best].grade)))
                best = s;
    q->pick_why = verify ? PW_CHECK : !ngoal ? PW_FRESH : best >= 0 && p->sk[best].tries ? PW_GOAL : PW_NEW;
    return best >= 0 ? best : S_GROUPS;
}

/* the difficulty for an order of skill s: the coach's, or harder at first for an assumed skill */
int mq_engine_diff(mq_t *q, int s)
{
    const mq_skrec_t *r = &mq_prof(q)->sk[s];
    int top = mq_rungs(s) - 1;
    if (assumed(q, s) && r->tries < 3 && mq_prof(q)->problems >= RAMP_ORDERS) /* checked a little way up its ladder */
        return imin(top, imax(r->diff, mq_rung_of_band(s, 1)));
    return imin(top, r->diff);
}

/* ------------------------------------------------------------------ think time */
static int digits(int32_t v)
{
    if (v < 0)
        v = -v;
    int n = 1;
    while (v >= 10)
        v /= 10, n++;
    return n;
}
static bool multistep(int s)
{
    return s == S_TWOSTEP || s == S_REMWORD || s == S_SHOPWORD || s == S_MEASWORD || s == S_FRACWORD ||
           s == S_ORDEROPS || s == S_CHANGE || s == S_ELAPSED || s == S_AREAL || s == S_AREASPLIT || s == S_DECOPS ||
           s == S_DIV2 || s == S_MULBIG || s == S_MULTI || s == S_DIVIDE || s == S_LINEPLOT || s == S_LINEPLOT5;
}

/* the model's estimate (ms) before her own history is known */
static int think_model(const mq_prob_t *p)
{
    int ms = 3500;
    int dg = p->kind == K_NUM || p->kind == K_REM || p->kind == K_MONEY || p->kind == K_DEC ? digits(p->ans) : 2;
    ms += (dg > 1 ? dg - 1 : 0) * 1800; /* digits to work out */
    if (p->big)
        ms += (digits(p->big) > 3 ? digits(p->big) - 3 : 0) * 1200;
    ms += mq_rung_band(p->skill, p->diff) * 900;
    if (multistep(p->skill))
        ms = ms * 16 / 10 + 2000;
    int chars = 0;
    for (int l = 0; l < 3; l++)
        for (const char *c = p->text[l]; *c; c++)
            chars++;
    ms += chars * 40; /* reading the story: about 25 characters a second */
    if (p->skill == S_FACTS)
        ms = 3000;
    return imin(ms, 45000);
}

int mq_think_ms(mq_t *q, const mq_prob_t *p)
{
    int model = think_model(p);
    if (p->skill >= MQ_NSKILL)
        return model;
    int h = mq_pace(q)->think[p->skill][mq_rung_band(p->skill, p->diff)];
    if (!h) /* nothing yet at this difficulty: her time at a neighbouring one */
        for (int d = 0; d < 4 && !h; d++)
            h = mq_pace(q)->think[p->skill][d];
    if (!h)
        return model;
    int mine = h * 250;
    return imin(45000, imax(1500, (mine * 3 + model) / 4));
}

/* her first answer took ms: the think time of the skill at that difficulty follows it (slowly,
 * clipped so one long pause does not make everything crawl) */
void mq_think_record(mq_t *q, const mq_prob_t *p, uint32_t ms)
{
    if (p->skill >= MQ_NSKILL)
        return;
    uint8_t *t = &mq_pace(q)->think[p->skill][mq_rung_band(p->skill, p->diff)];
    uint32_t v = ms / 250u;
    v = v < 1 ? 1 : v > 240 ? 240 : v;
    v = (uint32_t)imin((int)v, think_model(p) * 2 / 250 + 8); /* a long pause (a chat, a snack) is no think time */
    if (*t)
        v = (uint32_t)imin((int)v, *t * 2 + 8);
    *t = (uint8_t)(*t ? (*t * 3u + v) / 4u : v);
    q->dirty = true;
}

/* a heavy order: multi-digit work (column sums, long products and sharing) or several steps, or
 * simply a long think for her (the calm games; the action hovers) */
static bool multidigit(int s)
{
    return s == S_ADDSUB || s == S_ADDSUBBIG || s == S_MULTI || s == S_DIVIDE || s == S_MULBIG || s == S_DIV2 ||
           s == S_DECOPS;
}
bool mq_heavy(mq_t *q, const mq_prob_t *p)
{
    if (p->skill == S_FACTS)
        return false;
    (void)q; /* (her own times set the speed, not the kind of order) */
    return multidigit(p->skill) || multistep(p->skill) || think_model(p) > 14000;
}

/* the calm games (nothing escapes: she builds, sets or types the answer) */
bool mq_mech_calm(int m)
{
    return m == ME_VAULT || m == ME_TILL || m == ME_PETS || m == ME_BUILD || m == ME_COOK || m == ME_CLOCK || m == ME_BAKE ||
           m == ME_SORT || m == ME_JUMP || m == ME_DIG || m == ME_LAUNCH;
}
/* the action games, whose answers move */
bool mq_mech_moving(int m) { return m == ME_POP || m == ME_CATCH || m == ME_GATES || m == ME_KART; }

/* ------------------------------------------------------------------ the speed controller */
/* Each game keeps a speed level 1..100 for her (0: new). The multiplier is 0.35 + 0.9 x level %
 * (Q8), capped at a comfortable cruise (the base speed, level 72 and up): it never ramps toward
 * failure. New: level 30 (a gentle start). A miss, a timeout or an escaped right answer: -20 % at once;
 * a slow answer (past her think time): -5 %; quick rights (within 60 % of it) raise it by 5 % per
 * streak step, from the second in a row on (hysteresis: one quick right after a miss changes
 * nothing, so right-wrong-right-wrong only ever goes down, then sits). */
#define SPD_START 30
#define SPD_CRUISE 256 /* the comfortable cap of the learning games (Q8: the base speed) */
#define SPD_FLOOR 20   /* the slowest level: about half the cruise */
int mq_speed_level(mq_t *q, int game)
{
    int l = mq_pace(q)->speed[game >= 0 && game < MQ_NSPEED ? game : 0];
    return l ? l : SPD_START;
}
int mq_speed_mult(mq_t *q, int game) { return 90 + 230 * mq_speed_level(q, game) / 100; }


void mq_speed_event(mq_t *q, int game, int ev)
{
    if (game < 0 || game >= MQ_NSPEED)
        return;
    mq_pace_t *pc = mq_pace(q);
    int l = mq_speed_level(q, game);
    switch (ev) {
    case SE_MISS: l = l * 8 / 10, pc->spd_streak[game] = 0; break;
    case SE_SLOW: l = l * 95 / 100, pc->spd_streak[game] = 0; break;
    case SE_QUICK:
        if (pc->spd_streak[game] < 255)
            pc->spd_streak[game]++;
        if (pc->spd_streak[game] >= 2)
            l = imin(100, l + imax(1, l * 5 / 100));
        break;
    default: pc->spd_streak[game] = 0; break; /* a right in time: steady */
    }
    pc->speed[game] = (uint8_t)imax(SPD_FLOOR, imin(100, l)); /* (a floor: never frozen, audit S1; the maths eases instead) */
    q->dirty = true;
}

/* the game's speed for this order (Q8 of the base): her level, capped so that nothing escapes
 * before 1.5 x the order's think time (escape_ms: the time at the base speed); a fast speed only
 * for a fluent fact; a heavy order hovers */
int mq_speed_now(mq_t *q, int game, const mq_prob_t *p, int escape_ms)
{
    /* a comfortable cruise at most: the base speed, never faster (being quick moves the maths on, not
     * the game: mq_judge) */
    int m = imin(mq_speed_mult(q, game), SPD_CRUISE);
    if (p && escape_ms > 0) {
        int need = mq_think_ms(q, p) * 3 / 2;
        int cap = (int)((int32_t)escape_ms * 256 / imax(1, need));
        m = imin(m, cap);
        if (mq_heavy(q, p))
            m = imin(m, 40);
    }
    return imax(8, m);
}

/* the speed console: `mq speed [GAME [LEVEL]]` (tests) */
int mq_speed_console(mq_t *q, int argc, char **argv)
{
    const modx_api_t *api = q->api;
    long g = -1, l = -1;
    if (argc >= 3)
        api->con_parse_int(argv[2], &g);
    if (argc >= 4)
        api->con_parse_int(argv[3], &l);
    if (g >= 0 && g < MQ_NSPEED && l >= 0)
        mq_pace(q)->speed[g] = (uint8_t)imin(100, (int)l);
    for (int k = 0; k < MQ_NSPEED; k++)
        if (g < 0 || g == k)
            api->con_printf("%d: level %d x%d/256\n", k, mq_speed_level(q, k), mq_speed_mult(q, k));
    return 0;
}
