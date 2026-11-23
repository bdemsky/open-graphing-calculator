/*
 * Math Quest: multiplication and division facts.
 *
 * Every fact is tracked on its own (mq_fact_t in the profile): how often it
 * was seen and answered right, a moving average of the response time (0.1 s
 * units) and its Leitner box. Facts: a x b for a, b = 0..12 (169) and
 * (a*b) / b = a for b = 1..12, a = 0..12 (156). The required set is 0..10
 * (231 facts); the parent screen can add 11 and 12.
 *
 * Spaced repetition (Leitner): a new fact is in box 0. A quick right answer (within FLUENT_MS, or
 * within her own usual pace in the games: mq_fact_quick_ms, timed from the moment the answer can be
 * given) moves it up one box (at most 5), a right but slow answer keeps it
 * (at least box 1), a wrong answer puts it back in box 1 and asks it again two
 * questions later. A fact in box k is due again INTERVAL[k] fact rounds later
 * (a round = one fact game or a level that drills facts). A fact is fluent
 * when it reached box 4 and its average time is at most 3 s; finishing the
 * game needs every required fact fluent (mq_curric.c mq_quest_complete).
 *
 * Choosing the next fact (mq_fact_next): a queued fact first (fact families
 * and re-asks), else a due fact from the lowest box, else a new fact in the
 * teaching order of the factors (0, 1, 2, 10, 5, 3, 4, 9, 6, 7, 8, 11, 12;
 * at most NEW_PER_ROUND new facts per round), else a fact that is not fluent
 * yet, else any. Whenever a new fact appears (or one is missed), its fact
 * family follows: 3 x 7 brings 7 x 3, 21 / 3 and 21 / 7.
 *
 * Strategies for the hard facts (mq_fact_strategy): doubles (x2, x4, x8),
 * half of x10 (x5), x10 minus one group (x9), x5 plus one group (x6), split
 * arrays 5 + 2 (x7), x10 plus x1 / x2 (x11, x12), and "think multiplication"
 * for division; each comes with an array split at the helpful place.
 *
 * Fact games (all short, all end on a win, nothing is ever lost):
 *   FG_STARS  catch falling stars: type the answer of a star before it lands
 *             (a landed star just floats away and its fact is asked again)
 *   FG_CAFE   cafe rush: serve each customer's order; patience refills
 *   FG_BLOOM  blooming garden: each quick answer opens a flower, streaks glow
 *   FG_RACE   race the snail friend, whose speed adapts to the player's pace
 *             so the race stays close and is won by answering
 */
#include "mq.h"

#define FLUENT_MS 3000u
#define NEW_PER_ROUND 6
#define QUICK_MAX_MS 6000u /* "quick" relative to her pace: never slower than this */
#define WORKING 3 /* facts being learnt at once (met, box 2 or lower): fewer, and a new one comes first */
static const uint8_t INTERVAL[6] = {0, 1, 2, 4, 7, 12};
static const uint8_t ORDER[13] = {0, 1, 2, 5, 10, 3, 4, 9, 6, 7, 8, 11, 12}; /* teaching order of factors (0 and 1: rules) */
#define TRIVIAL_PCT 2   /* once the x0 / x1 rules are known: a trivial fact now and then (the mix: at most 5 %) */
#define DIV_TABLE_PCT 70 /* a table's division: when this share of its multiplication is solid (box 3+) */

int mq_fact_index(int a, int b, bool div)
{
    if (!div)
        return a * 13 + b;
    return 169 + (b - 1) * 13 + a; /* (a*b) / b = a */
}

void mq_fact_decode(int f, int *a, int *b, bool *div)
{
    if (f < 169) {
        *a = f / 13, *b = f % 13, *div = false;
    } else {
        f -= 169;
        *b = f / 13 + 1, *a = f % 13, *div = true;
    }
}

int mq_fact_answer(int f)
{
    int a, b;
    bool div;
    mq_fact_decode(f, &a, &b, &div);
    return div ? a : a * b;
}

bool mq_fact_required(const mq_profile_t *p, int f)
{
    int a, b;
    bool div;
    mq_fact_decode(f, &a, &b, &div);
    return p->facts12 || (a <= 10 && b <= 10);
}

/* Fluent: box 4 or more, quick, and held on two different play days (a fact learnt in one sitting
 * is not yet a fact known: spaced evidence, like a mastered skill) */
bool mq_fact_fluent(const mq_fact_t *f) { return f->box >= 4 && f->t10 && f->t10 <= QUICK_MAX_MS / 100 && (f->right & FACT_DAYS); }

/* Quick enough to be a fact she recalls: within 3 s, or a little under her own usual time in the games
 * (a game's answers fly in and are chosen with a tap or a catch: 3 s from the order's start was out of
 * reach there and box 4 never came, the playtest's "fluency never reached"), at most 6 s */
uint32_t mq_fact_quick_ms(mq_t *q)
{
    uint32_t own = (uint32_t)mq_pace(q)->fact_t10 * 100u * 9u / 10u;
    return own > FLUENT_MS ? (own < QUICK_MAX_MS ? own : QUICK_MAX_MS) : FLUENT_MS;
}

static bool rules_known(const mq_profile_t *p);
int mq_facts_fluent(const mq_profile_t *p, int *total)
{
    int n = 0, k = 0;
    bool rules = rules_known(p);
    for (int f = 0; f < MQ_NFACT; f++)
        if (mq_fact_required(p, f)) {
            n++;
            const mq_fact_t *t = &p->fact[f];
            /* (x0 / x1 and their division: known by the rule once the rules are known and not missed since) */
            k += mq_fact_fluent(t) || (rules && mq_fact_trivial(f) && !(t->seen && t->box <= 1 && (t->right & 0x7F) < t->seen));
        }
    if (total)
        *total = n;
    return k;
}

static int rank_of(int x)
{
    for (int i = 0; i < 13; i++)
        if (ORDER[i] == x)
            return i;
    return 12;
}
int mq_fact_rank_of(int x) { return rank_of(x); }
/* within the rung's tables (mq_generate sets q->fact_cap for an order of the fact garden; 0: all) */
static bool in_cap(const mq_t *q, int f)
{
    if (!q->fact_cap)
        return true;
    int a, b;
    bool div;
    mq_fact_decode(f, &a, &b, &div);
    return rank_of(a) < q->fact_cap && rank_of(b) < q->fact_cap;
}
static int fact_rank(int f)
{
    int a, b;
    bool div;
    mq_fact_decode(f, &a, &b, &div);
    int r = rank_of(a) > rank_of(b) ? rank_of(a) : rank_of(b);
    return r + (div ? 20 : 0); /* all the multiplication tables in teaching order, then the division */
}

/* x0, x1, divided by 1, n / n, 0 / n: quick rules, not facts to drill */
bool mq_fact_trivial(int f)
{
    int a, b;
    bool div;
    mq_fact_decode(f, &a, &b, &div);
    return div ? b == 1 || a <= 1 : a <= 1 || b <= 1;
}
/* the rules known: one x0 fact and one x1 fact answered right (a quick check: two facts, not four --
 * the playtest's "lots of 0 x and 1 x") */
static bool rules_known(const mq_profile_t *p) { return mq_fact_rules_known(p); }
static int rule_of(int f) /* x0 (0), x1 (1), else -1 (multiplication only) */
{
    if (f >= 169)
        return -1;
    int a = f / 13, b = f % 13;
    return !a || !b ? 0 : a == 1 || b == 1 ? 1 : -1;
}
bool mq_fact_rules_known(const mq_profile_t *p)
{
    bool r[2] = {false, false};
    for (int f = 0; f < 169; f++)
        if (rule_of(f) >= 0 && (p->fact[f].right & 0x7F))
            r[rule_of(f)] = true;
    return r[0] && r[1];
}
/* a x0 / x1 fact missed and not answered right since (a right answer lifts it out of box 1) */
static bool trivial_missed(const mq_fact_t *t) { return t->seen && t->box <= 1 && (t->right & 0x7F) < t->seen; }
/* solid: box 3 or more */
static bool solid(const mq_profile_t *p, int f) { return f >= 0 && f < MQ_NFACT && p->fact[f].box >= 3; }
/* Division only after its multiplication: the fact's own x fact solid, and most of the divisor's
 * table (2..10) solid too (the grown-ups' focus can still ask any) */
bool mq_fact_div_ready(const mq_profile_t *p, int f)
{
    int a, b;
    bool div;
    mq_fact_decode(f, &a, &b, &div);
    if (!div)
        return true;
    if (!solid(p, mq_fact_index(a, b, false)) && !solid(p, mq_fact_index(b, a, false)))
        return false;
    int n = 0, k = 0;
    for (int x = 2; x <= 10; x++)
        n++, k += solid(p, mq_fact_index(x, b, false)) || solid(p, mq_fact_index(b, x, false));
    return k * 100 >= n * DIV_TABLE_PCT;
}
/* may f come now? (the rules: once known, a trivial fact only after a miss, or now and then; a division
 * fact only when ready, or when already met) */
static bool fact_ok(mq_t *q, const mq_profile_t *p, int f)
{
    const mq_fact_t *t = &p->fact[f];
    if (mq_fact_trivial(f) && mq_fact_rules_known(p)) {
        bool missed = t->seen && t->box <= 1 && (t->right & 0x7F) < t->seen;
        if (!missed && mq_rnd(q, 0, 99) >= TRIVIAL_PCT)
            return false;
    }
    int a, b;
    bool div;
    mq_fact_decode(f, &a, &b, &div);
    return !div || t->seen || mq_fact_div_ready(p, f);
}

static void enqueue(mq_t *q, int f, int at)
{
    for (int i = 0; i < q->nqueue; i++)
        if (q->queue[i] == f)
            return;
    if (q->nqueue >= 8)
        return;
    if (at > q->nqueue)
        at = q->nqueue;
    for (int i = q->nqueue; i > at; i--)
        q->queue[i] = q->queue[i - 1];
    q->queue[at] = (int16_t)f;
    q->nqueue++;
}

/* queue the other members of f's family (3 x 7: 7 x 3, 21 / 3, 21 / 7; the division ones once the
 * multiplication is solid) */
static void enqueue(mq_t *q, int f, int at);
static void enqueue_ready(mq_t *q, int f, int at)
{
    if (mq_fact_div_ready(mq_prof(q), f))
        enqueue(q, f, at);
}
static void enqueue_family(mq_t *q, int f)
{
    int a, b;
    bool div;
    mq_fact_decode(f, &a, &b, &div);
    if (div) { /* (a*b)/b: the family of a x b */
        int t = a;
        a = b, b = t; /* now a = divisor */
        enqueue(q, mq_fact_index(b, a, false), 8);
        if (a != b)
            enqueue(q, mq_fact_index(a, b, false), 8);
        if (b >= 1 && a != b)
            enqueue_ready(q, mq_fact_index(a, b, true), 8);
        return;
    }
    if (a != b)
        enqueue(q, mq_fact_index(b, a, false), 8);
    if (b >= 1)
        enqueue_ready(q, mq_fact_index(a, b, true), 8);
    if (a >= 1 && a != b)
        enqueue_ready(q, mq_fact_index(b, a, true), 8);
}

/* 3 x 7 and 7 x 3 are the same to a player; a division fact is its own */
static int fact_key(int f)
{
    int a, b;
    bool div;
    mq_fact_decode(f, &a, &b, &div);
    return div ? 200 + f : (a < b ? a * 13 + b : b * 13 + a);
}

/* asked within the last 3 (twins included)? */
static bool recent(const mq_t *q, int f)
{
    int k = fact_key(f) + 1;
    return q->fhist[0] == k || q->fhist[1] == k || q->fhist[2] == k;
}

static int fact_pick(mq_t *q);

/* a new fact in teaching order (the rules first, until known; then the tables; their division when ready):
 * -1 none; its family follows */
static int new_fact(mq_t *q, const mq_profile_t *p)
{
    int bf = -1, br = 999;
    bool rules = mq_fact_rules_known(p);
    for (int f = 0; f < MQ_NFACT; f++) {
        if (!mq_fact_required(p, f) || p->fact[f].box != 0 || p->fact[f].seen || recent(q, f) || !in_cap(q, f) ||
            !mq_fact_div_ready(p, f))
            continue;
        bool tr = mq_fact_trivial(f);
        if (tr && rules)
            continue;
        if (tr && f >= 169)
            continue; /* (the division rules come with the division) */
        int r = fact_rank(f);
        if (tr) { /* the rules first, until known: one x0 fact, then one x1 fact, each with a factor of 2..9 */
            int a = f / 13, b = f % 13, other = a <= 1 ? b : a, ru = rule_of(f);
            bool have = false;
            for (int g = 0; g < 169 && !have; g++)
                have = rule_of(g) == ru && (p->fact[g].right & 0x7F);
            if (have || other < 2 || other > 9 || (a <= 1 && b <= 1))
                continue;
            r = -100 + ru * 20 + (int)(((unsigned)f * 7u + (unsigned)p->round) % 13u); /* (x0 before x1, varied) */
        }
        if (r < br)
            br = r, bf = f;
    }
    if (bf >= 0) {
        q->new_today++;
        enqueue_family(q, bf);
    }
    return bf;
}

/* The next fact to ask. Re-asks and fact families come back, but never within the next
 * three facts (a fact and its twin alike) unless nothing else is left (then they alternate). */
int mq_fact_next(mq_t *q)
{
    int f = fact_pick(q);
    q->fhist[2] = q->fhist[1], q->fhist[1] = q->fhist[0], q->fhist[0] = (int16_t)(fact_key(f) + 1);
    return f;
}

/* ------------------------------------------------------------------ times-table runs
 * Now and then a run of one table, as schools practise it: the same first factor, the other shuffled (3 x 7,
 * 3 x 2, 3 x 9, ...), five or six facts in a row -- for the grown-ups' focus table, else the newest table she is
 * learning. Between runs, the usual mix. (The playtest: "fact review in fixed-factor sets, as schools do".) */
#define TRUN_LEN 6
#define TRUN_PCT 20  /* the chance a run starts at a fact pick (when none was lately) */
#define TRUN_COOL 18 /* fact picks after a run before the next may start */
static int trun_rnd(mq_t *q, int n)
{
    q->trun_rng = q->trun_rng * 1103515245u + 12345u + q->anim_t;
    return (int)((q->trun_rng >> 16) % (uint32_t)n);
}
static bool trun_ok(mq_t *q, int t, int k)
{
    int f = mq_fact_index(t, k, false);
    return k >= 2 && k <= 10 && mq_fact_required(mq_prof(q), f) && in_cap(q, f) && !mq_fact_trivial(f) &&
           (mq_prof(q)->fact[f].seen || mq_focus_fact(q, f)); /* (facts she has met: a run reviews, it never
                                                                * brings a new table early -- but the focus may) */
}
bool mq_fact_run_start(mq_t *q, int table)
{
    if (table < 2 || table > 12)
        return false;
    int n = 0;
    for (int k = 2; k <= 10; k++)
        n += trun_ok(q, table, k);
    if (n < 4)
        return false;
    q->trun_t = (uint8_t)(table + 1), q->trun_left = (uint8_t)(n < TRUN_LEN ? n : TRUN_LEN), q->trun_mask = 0;
    return true;
}
/* the table for a run now: the focus table, else the newest one she is learning (met, not fluent); -1 none */
static int trun_table(mq_t *q)
{
    const mq_pace_t *pc = mq_pace(q);
    if (mq_focus_active(q))
        for (int i = 0; i < 13; i++)
            if (ORDER[i] >= 2 && ((pc->focus_mul >> ORDER[i]) & 1))
                return ORDER[i];
    const mq_profile_t *p = mq_prof(q);
    for (int i = 12; i >= 2; i--) { /* (the latest table in teaching order with a fact being learnt) */
        int t = ORDER[i], learning = 0;
        for (int k = 2; k <= 10; k++) {
            const mq_fact_t *ft = &p->fact[mq_fact_index(t, k, false)];
            learning += ft->seen && !mq_fact_fluent(ft) && trun_ok(q, t, k);
        }
        if (learning >= 2)
            return t;
    }
    return -1;
}
int mq_fact_run_table(mq_t *q) { return mq_fact_rules_known(mq_prof(q)) ? trun_table(q) : -1; }
/* the run's next fact: -1 when it is over */
static int trun_next(mq_t *q)
{
    int t = q->trun_t - 1, n = 0;
    for (int k = 2; k <= 10; k++)
        n += trun_ok(q, t, k) && !((q->trun_mask >> k) & 1) && !recent(q, mq_fact_index(t, k, false));
    if (!q->trun_left || !n) {
        q->trun_t = 0, q->trun_left = 0, q->trun_cool = TRUN_COOL;
        return -1;
    }
    int pick = trun_rnd(q, n);
    for (int k = 2; k <= 10; k++)
        if (trun_ok(q, t, k) && !((q->trun_mask >> k) & 1) && !recent(q, mq_fact_index(t, k, false)) && pick-- == 0) {
            q->trun_mask |= (uint16_t)(1u << k), q->trun_left--;
            return mq_fact_index(t, k, false);
        }
    return -1;
}

/* the next fact of the run going on (and only of it: a song of one table), -1 when it is over */
int mq_fact_run_next(mq_t *q)
{
    int f = q->trun_t ? trun_next(q) : -1;
    if (f >= 0)
        q->fhist[2] = q->fhist[1], q->fhist[1] = q->fhist[0], q->fhist[0] = (int16_t)(fact_key(f) + 1);
    return f;
}

static int fact_pick(mq_t *q)
{
    mq_profile_t *p = mq_prof(q);
    /* a times-table run going on, or one starting now and then */
    if (!q->trun_t && q->trun_cool)
        q->trun_cool--;
    else if (!q->trun_t && mq_fact_rules_known(p) && trun_rnd(q, 100) < TRUN_PCT) {
        int t = trun_table(q);
        if (t >= 0)
            mq_fact_run_start(q, t);
    }
    if (q->trun_t) {
        int f = trun_next(q);
        if (f >= 0)
            return f;
    }
    /* "focus this week": its share of the fact moments (never a recent one) */
    int fs = mq_focus_share(q);
    if (fs && mq_rnd(q, 0, 99) < fs) {
        int f = mq_focus_pick(q, recent);
        if (f >= 0)
            return f;
    }
    for (int k = 0; k < q->nqueue; k++) { /* the first queued fact that is not too recent */
        int f = q->queue[k];
        if (f >= 0 && f < MQ_NFACT && mq_fact_required(p, f) && (recent(q, f) || !in_cap(q, f)))
            continue;
        if (f >= 0 && f < MQ_NFACT && mq_fact_trivial(f) && mq_fact_rules_known(p) && !trivial_missed(&p->fact[f]))
            f = -1; /* (a known rule's fact, not missed since: dropped from the queue) */
        for (int i = k + 1; i < q->nqueue; i++)
            q->queue[i - 1] = q->queue[i];
        q->nqueue--;
        k--;
        if (f >= 0 && f < MQ_NFACT && mq_fact_required(p, f))
            return f;
    }
    /* the working set: while fewer than WORKING facts are being learnt (met, in the low boxes), a new one
     * comes before the due reviews -- else a quick player's reviews of facts she knows filled every fact
     * order and the next tables never came (the playtest: "a lot of 0x and 1x, never 6 x 5") */
    if (q->new_today < NEW_PER_ROUND) {
        int learning = 0;
        for (int f = 0; f < MQ_NFACT; f++)
            learning += mq_fact_required(p, f) && p->fact[f].seen && p->fact[f].box <= 2 && !mq_fact_trivial(f) && in_cap(q, f);
        if (learning < WORKING) {
            int nf = new_fact(q, p);
            if (nf >= 0)
                return nf;
        }
    }
    /* due facts from the lowest box */
    int best = 99, cnt = 0;
    for (int f = 0; f < MQ_NFACT; f++) {
        const mq_fact_t *t = &p->fact[f];
        if (!mq_fact_required(p, f) || !t->box || t->due > p->round || recent(q, f) || !in_cap(q, f) ||
            (mq_fact_trivial(f) && mq_fact_rules_known(p) && !(t->seen && t->box <= 1 && (t->right & 0x7F) < t->seen)))
            continue;
        if (t->box < best)
            best = t->box, cnt = 0;
        if (t->box == best)
            cnt++;
    }
    if (cnt) {
        int k = mq_rnd(q, 0, cnt - 1);
        for (int f = 0; f < MQ_NFACT; f++) {
            const mq_fact_t *t = &p->fact[f];
            if (mq_fact_required(p, f) && t->box == best && t->due <= p->round && !recent(q, f) && in_cap(q, f) &&
                !(mq_fact_trivial(f) && mq_fact_rules_known(p) && !(t->seen && t->box <= 1 && (t->right & 0x7F) < t->seen)) &&
                k-- == 0)
                return f;
        }
    }
    /* a new fact in teaching order: first a quick check of the x0 / x1 rules (a few), then the tables
     * (x2, x5, x10, x3, x4, x9, x6, x7, x8, then x11, x12), then their division when ready; past the
     * round's new facts, the ones met and not yet fluent first (a new one only when none waits) */
    bool met_waits = false;
    for (int f = 0; f < MQ_NFACT && !met_waits; f++)
        met_waits = mq_fact_required(p, f) && p->fact[f].seen && !mq_fact_fluent(&p->fact[f]) && !recent(q, f) &&
                    in_cap(q, f) && !mq_fact_trivial(f);
    if (q->new_today < NEW_PER_ROUND || !met_waits) {
        int bf = new_fact(q, p);
        if (bf >= 0)
            return bf;
    }
    /* not yet fluent, then anything */
    int start = mq_rnd(q, 0, MQ_NFACT - 1);
    for (int i = 0; i < MQ_NFACT; i++) {
        int f = (start + i) % MQ_NFACT;
        if (mq_fact_required(p, f) && !mq_fact_fluent(&p->fact[f]) && !recent(q, f) && in_cap(q, f) && fact_ok(q, p, f) &&
            p->fact[f].seen) /* (met already: the order of the tables kept) */
            return f;
    }
    for (int pass = 0; pass < 3; pass++) /* anything not recent; with almost nothing left, anything */
        for (int i = 0; i < MQ_NFACT; i++) {
            int f = (start + i) % MQ_NFACT;
            if (mq_fact_required(p, f) && (pass || !recent(q, f)) && (pass == 2 || in_cap(q, f)) && (pass == 2 || fact_ok(q, p, f)))
                return f;
        }
    return 0;
}

/* Record an answer to fact f: right or not, after ms milliseconds. */
void mq_fact_record(mq_t *q, int f, bool right, uint32_t ms)
{
    if (f < 0 || f >= MQ_NFACT)
        return;
    if (q->scene == SC_FACTGAME || q->scene == SC_ACTION) /* the arcade's quick facts: home time back, a third each */
        mq_hstory_order(q, 3);
    mq_profile_t *p = mq_prof(q);
    mq_fact_t *t = &p->fact[f];
    bool was = mq_fact_fluent(t);
    if (t->seen < 255)
        t->seen++;
    uint32_t d = ms / 100;
    if (d > 255)
        d = 255;
    if (d == 0)
        d = 1;
    if (right) {
        if ((t->right & 0x7F) < 0x7F)
            t->right++;
        t->t10 = (uint8_t)(t->t10 ? (t->t10 * 2u + d) / 3u : d);
        uint8_t *par = &mq_learn(q)->fact_par[f / 8], bit = (uint8_t)(1 << (f % 8));
        bool day_odd = p->days & 1;
        uint32_t quick = mq_fact_quick_ms(q);
        { /* her usual pace (a running average of her right answers, from when they could be given) */
            mq_pace_t *pc = mq_pace(q);
            pc->fact_t10 = (uint8_t)(pc->fact_t10 ? (pc->fact_t10 * 7u + d) / 8u : d);
        }
        if (ms <= quick && t->box >= 4 && !(t->right & FACT_DAYS) && (((*par & bit) != 0) != day_odd))
            t->right |= FACT_DAYS; /* box 4 again on another play day: known for good */
        if (ms <= quick && t->box == 3) /* box 4 now: the day is noted */
            *par = (uint8_t)(day_odd ? *par | bit : *par & ~bit);
        /* up a box: quick, up to the top; right but slow (a game's answers fly in; she thought), up to box 3
         * -- the boxes space her facts out either way; fluency (box 4+) needs quick answers. A x0 / x1 fact
         * answered right once the rules are known: out of the missed box at once */
        if (t->seen == 1 && !t->box) /* right the very first time: she knew it already (box 3 quick, else box 2) */
            t->box = ms <= quick ? 3 : 2;
        else if (ms <= quick)
            t->box = (uint8_t)(t->box < 5 ? t->box + 1 : 5);
        else
            t->box = (uint8_t)(t->box < 3 ? t->box + 1 : t->box);
        if (mq_fact_trivial(f) && mq_fact_rules_known(p) && t->box < 2)
            t->box = 2;
    } else {
        t->box = 1;
        enqueue(q, f, 2);
        enqueue_family(q, f);
    }
    t->due = (uint16_t)(p->round + (mq_focus_active(q) && mq_focus_fact(q, f) ? (INTERVAL[t->box] + 1) / 2 : INTERVAL[t->box]));
    q->dirty = true;
    if (right) /* coins: a fact turning fluent, and the fact games' answers (mq_home.c) */
        mq_on_fact(q, was, mq_fact_fluent(t), q->scene == SC_FACTGAME);
}

/* A save from before the fix of the stalled boxes (a right answer in a game was too slow to count, the
 * rounds went by only at the fact stop): the same few facts stayed in box 1, due again and again, and
 * x0 / x1 facts once missed stayed "missed" for good. Once per player: a fact answered right mostly
 * (70 % or more, at least twice) goes to box 2, a x0 / x1 fact answered right at least once too (the
 * rules are known); every due round within its box's interval from now. */
void mq_fact_migrate(mq_t *q, int prof)
{
    mq_profile_t *p = &q->save.prof[prof & 1];
    mq_pace_t *pc = &q->save.more[prof & 1].pace;
    if (pc->fact_ver)
        return;
    for (int f = 0; f < MQ_NFACT; f++) {
        mq_fact_t *t = &p->fact[f];
        if (!t->seen)
            continue;
        int r = t->right & 0x7F;
        if (t->box <= 1 && ((mq_fact_trivial(f) && r >= 1) || (r >= 2 && r * 10 >= t->seen * 7)))
            t->box = 2;
        uint16_t lim = (uint16_t)(p->round + INTERVAL[t->box > 5 ? 5 : t->box]);
        if ((int16_t)(uint16_t)(t->due - lim) > 0)
            t->due = lim;
    }
    pc->fact_ver = 1;
    q->dirty = true;
}

/* A fact as a problem (S_FACTS levels; the fact games use the same text). */
void mq_fact_problem(mq_t *q, mq_prob_t *p, int f)
{
    int a, b;
    bool div;
    mq_fact_decode(f, &a, &b, &div);
    p->skill = S_FACTS;
    p->kind = K_NUM;
    p->a[0] = (int16_t)f;
    if (div) {
        q->api->snprintf(p->text[0], MQ_TXT, "%d \xC3\xB7 %d", a * b, b);
        p->ans = a;
        p->wrong[0] = a * b - b, p->wrong_mc[0] = MC_WRONG_OP;
        p->wrong[1] = a + 1, p->wrong_mc[1] = MC_NEIGHBOR;
        p->wrong[2] = a ? a - 1 : 2, p->wrong_mc[2] = MC_NEIGHBOR;
        p->nwrong = 3;
        p->vis = V_ARRAY, p->v[0] = (int16_t)b, p->v[1] = (int16_t)a;
        p->mc_default = MC_DIV_MIX;
    } else {
        q->api->snprintf(p->text[0], MQ_TXT, "%d \xC3\x97 %d", a, b);
        p->ans = a * b;
        p->nwrong = 0;
        int w[4] = {a + b, a * b + b, a * b - b, a * b + a};
        uint8_t m[4] = {MC_ADD_NOT_MUL, MC_NEIGHBOR, MC_NEIGHBOR, MC_NEIGHBOR};
        for (int i = 0; i < 4; i++)
            if (w[i] != p->ans && w[i] >= 0) {
                bool dup = false;
                for (int j = 0; j < p->nwrong; j++)
                    dup |= p->wrong[j] == w[i];
                if (!dup)
                    p->wrong[p->nwrong] = w[i], p->wrong_mc[p->nwrong++] = m[i];
            }
        p->vis = V_ARRAY, p->v[0] = (int16_t)a, p->v[1] = (int16_t)b;
        p->mc_default = MC_NEIGHBOR;
    }
    p->text[1][0] = p->text[2][0] = 0;
}

/* The strategy for fact f: up to three lines and an array split where it helps. */
void mq_fact_strategy(mq_t *q, int f, char out[3][MQ_TXT], mq_prob_t *vis)
{
    const modx_api_t *api = q->api;
    int a, b;
    bool div;
    mq_fact_decode(f, &a, &b, &div);
    int prod = a * b;
    for (int i = 0; i < 3; i++)
        out[i][0] = 0;
    if (div) {
        api->snprintf(out[0], MQ_TXT, "Think multiplication:");
        api->snprintf(out[1], MQ_TXT, "%d \xC3\x97 ? = %d", b, prod);
        api->snprintf(out[2], MQ_TXT, "%d \xC3\x97 %d = %d, so %d \xC3\xB7 %d = %d", b, a, prod, prod, b, a);
        if (vis)
            vis->vis = V_ARRAY, vis->v[0] = (int16_t)b, vis->v[1] = (int16_t)a, vis->v[2] = 0;
        return;
    }
    /* use the factor with the best strategy as the one being split */
    int x = a, y = b; /* x: strategy factor, y: the other */
    static const uint8_t pri[13] = {0, 1, 3, 9, 4, 2, 10, 11, 5, 6, 0, 7, 8};
    if (pri[b] < pri[a] || (a > 10 && b <= 10))
        x = b, y = a;
    if (x == 10 || y == 10)
        x = 10, y = a == 10 ? b : a;
    if (vis)
        vis->vis = V_ARRAY, vis->v[0] = (int16_t)y, vis->v[1] = (int16_t)x, vis->v[2] = 0;
    switch (x) {
    case 0:
        api->snprintf(out[0], MQ_TXT, "Zero groups (or groups of zero)");
        api->snprintf(out[1], MQ_TXT, "make nothing: %d \xC3\x97 %d = 0.", a, b);
        break;
    case 1:
        api->snprintf(out[0], MQ_TXT, "One group of %d is just %d.", y, y);
        break;
    case 2:
        api->snprintf(out[0], MQ_TXT, "Doubles! %d + %d = %d.", y, y, prod);
        break;
    case 4:
        api->snprintf(out[0], MQ_TXT, "Double, then double again:");
        api->snprintf(out[1], MQ_TXT, "2 \xC3\x97 %d = %d, double it: %d.", y, 2 * y, prod);
        if (vis)
            vis->v[2] = 2;
        break;
    case 8:
        api->snprintf(out[0], MQ_TXT, "Double three times:");
        api->snprintf(out[1], MQ_TXT, "%d, %d, %d, %d.", y, 2 * y, 4 * y, prod);
        if (vis)
            vis->v[2] = 4;
        break;
    case 5:
        api->snprintf(out[0], MQ_TXT, "Half of ten groups:");
        api->snprintf(out[1], MQ_TXT, "10 \xC3\x97 %d = %d, half is %d.", y, 10 * y, prod);
        break;
    case 9:
        api->snprintf(out[0], MQ_TXT, "Nines: ten groups, take one away.");
        api->snprintf(out[1], MQ_TXT, "10 \xC3\x97 %d = %d, %d \xE2\x88\x92 %d = %d.", y, 10 * y, 10 * y, y, prod);
        break;
    case 10:
        api->snprintf(out[0], MQ_TXT, "Tens: %d tens is %d.", y, prod);
        break;
    case 3:
        api->snprintf(out[0], MQ_TXT, "Double, plus one more group:");
        api->snprintf(out[1], MQ_TXT, "%d + %d = %d.", 2 * y, y, prod);
        if (vis)
            vis->v[2] = 2;
        break;
    case 6:
        api->snprintf(out[0], MQ_TXT, "Five groups plus one more:");
        api->snprintf(out[1], MQ_TXT, "%d + %d = %d.", 5 * y, y, prod);
        if (vis)
            vis->v[2] = 5;
        break;
    case 7:
        api->snprintf(out[0], MQ_TXT, "Split the array: 5 and 2.");
        api->snprintf(out[1], MQ_TXT, "%d + %d = %d.", 5 * y, 2 * y, prod);
        if (vis)
            vis->v[2] = 5;
        break;
    default: /* 11, 12 */
        api->snprintf(out[0], MQ_TXT, "Ten groups plus %d more:", x - 10);
        api->snprintf(out[1], MQ_TXT, "%d + %d = %d.", 10 * y, (x - 10) * y, prod);
        if (vis)
            vis->v[2] = 10;
        break;
    }
    if (!out[1][0])
        api->snprintf(out[1], MQ_TXT, "%d \xC3\x97 %d = %d", a, b, prod);
    if (vis && vis->v[2]) { /* the array has the strategy factor as columns */
        vis->v[0] = (int16_t)y, vis->v[1] = (int16_t)x;
    }
}

/* ------------------------------------------------------------------ fact games */
static void fg_new_faller(mq_t *q, mq_faller_t *s, int slot)
{
    s->fact = (int16_t)mq_fact_next(q);
    s->alive = 1;
    s->x = (int16_t)(slot ? mq_rnd(q, 200, 270) : mq_rnd(q, 40, 110)); /* apart: never overlapping */
    s->y = (int16_t)(MQ_Y0 + 4 - slot * 40);
    s->patience = (int16_t)q->now; /* spawn time (low 15 bits are enough for a difference) */
}

void mq_fg_start(mq_t *q, int game)
{
    mq_profile_t *p = mq_prof(q);
    p->round++;
    q->new_today = 0;
    q->nqueue = 0;
    q->fg = (uint8_t)game;
    q->fg_score = q->fg_streak = q->fg_bloom = q->fg_hinted = 0;
    q->fg_goal = game == FG_BLOOM ? 12 : game == FG_CAFE ? 10 : 12;
    q->fg_you = q->fg_friend = 0;
    q->fg_last = q->now;
    q->fg_t0 = mq_mark(q), q->fg_newbest = 0, q->fg_end_t = 0;
    mq_game_coins_start(q); /* the end card shows the coins of this game */
    q->in_len = 0, q->input[0] = 0;
    q->msg[0][0] = q->msg[1][0] = 0;
    for (int i = 0; i < 4; i++)
        q->fall[i].alive = 0;
    /* the friend's pace: about 90 % of the player's (average answer time + 1 s per step) */
    uint32_t sum = 0, n = 0;
    for (int f = 0; f < MQ_NFACT; f++)
        if (p->fact[f].t10)
            sum += p->fact[f].t10, n++;
    uint32_t avg_ms = n ? sum * 100 / n : 4000;
    uint32_t step_ms = avg_ms + 1000;
    q->fg_friend_rate = (uint16_t)(1000u * 1000u / (step_ms * q->fg_goal) * 9 / 10); /* per 1000 track units per s */
    if (game == FG_STARS) {
        fg_new_faller(q, &q->fall[0], 0);
        fg_new_faller(q, &q->fall[1], 1);
    } else {
        q->fg_cur = (int16_t)mq_fact_next(q);
        q->prob_start = q->now;
        q->fall[0].patience = 1000;
    }
    q->dirty = true;
}

static bool fg_done(mq_t *q)
{
    return q->fg == FG_RACE ? q->fg_you >= 1000 : q->fg_score >= q->fg_goal;
}

/* the game was just won: its time, a new best? */
static void fg_finish(mq_t *q)
{
    mq_home_t *h = mq_home(q);
    uint32_t s = (q->anim_t - q->fg_t0) / 1000 + 1;
    uint16_t *b = &h->best_fg[q->fg & 3];
    q->fg_newbest = (uint8_t)(*b && s < *b);
    if (!*b || s < *b)
        *b = (uint16_t)(s > 60000 ? 60000 : s), q->dirty = true;
    mq_quest_event(q, QE_ARCADE, 1);
    q->fg_end_t = mq_mark(q);
    mq_sfx(q, q->fg_newbest ? MODX_SFX_UNLOCK : MODX_SFX_CHEER, 4);

}

/* dt: milliseconds since the last frame (frames are MQ_FRAME_MS apart). */
void mq_fg_tick(mq_t *q, uint32_t dt)
{
    if (fg_done(q))
        return;
    if (q->fg == FG_STARS) {
        /* A gentle fall: 8 px/s to start, a little faster only on a streak, and never so fast that a
         * star lands before 1.5 x her think time for it (mq_star_speed). After a miss, or when she
         * has not typed for a while, the stars stop mid-air until she types (calm). A star that lands
         * floats back up to the top: nothing is lost, and nothing is counted (it was never answered). */
        bool calm = (q->now - q->fg_miss_t < 4000u && q->fg_miss_t) || (q->now - q->last_input > 6000u && !q->in_len);
        for (int i = 0; i < 2; i++) {
            mq_faller_t *s = &q->fall[i];
            if (!s->alive) {
                fg_new_faller(q, s, i);
                continue;
            }
            if (calm && s->y > MQ_Y0 + 30)
                continue;
            q->fg_acc[i] = (uint16_t)(q->fg_acc[i] + dt * (uint32_t)mq_star_speed(q, s->fact));
            s->y = (int16_t)(s->y + q->fg_acc[i] / 1000), q->fg_acc[i] %= 1000;
            if (s->y > MQ_Y0 + 120) { /* landed: it floats back up (no score lost, nothing recorded) */
                mq_spark_burst(q, s->x, s->y, 3);
                s->y = (int16_t)(MQ_Y0 + 4 - i * 40);
                q->fg_streak = 0;
            }
        }
    } else if (q->fg == FG_CAFE) {
        q->fall[0].patience = (int16_t)(q->fall[0].patience - (int)dt / 12);
        if (q->fall[0].patience <= 0) { /* "no rush!": patience refills */
            q->fall[0].patience = 1000;
            q->api->snprintf(q->msg[0], MQ_TXT, "\"No rush, I can wait!\"");
        }
    } else if (q->fg == FG_RACE) {
        int ahead = q->fg_friend - q->fg_you;
        uint32_t rate = q->fg_friend_rate;
        if (ahead > 120)
            rate /= 3; /* the friend waits for you */
        if (q->fg_friend < 950 || q->fg_you >= 900)
            q->fg_friend = (int16_t)(q->fg_friend + (int)(rate * dt / 1000));
        if (q->fg_friend > 960)
            q->fg_friend = 960; /* stops just before the line: the player wins by answering */
    }
}

/* The moving fact games (Star Catch's falling stars, the race) are answered by labelled choices, never typed
 * (backlog (b)): the answers on screen and near misses of the lowest one, in order. The calm ones (the cafe,
 * the garden) keep the keypad: typing a fact is their point. */
bool mq_fg_choosing(const mq_t *q) { return q->fg == FG_STARS || q->fg == FG_RACE; }
static void opt_add(int32_t *out, int *n, int32_t v)
{
    if (v < 0 || *n >= 4)
        return;
    for (int i = 0; i < *n; i++)
        if (out[i] == v)
            return;
    out[(*n)++] = v;
}
int mq_fg_choices(mq_t *q, int32_t out[4])
{
    int n = 0, f;
    if (q->fg == FG_STARS) {
        int lo = q->fall[0].y >= q->fall[1].y ? 0 : 1;
        for (int i = 0; i < 2; i++)
            if (q->fall[(lo + i) % 2].alive)
                opt_add(out, &n, mq_fact_answer(q->fall[(lo + i) % 2].fact));
        f = q->fall[lo].fact;
    } else {
        f = q->fg_cur;
        opt_add(out, &n, mq_fact_answer(f));
    }
    int a, b;
    bool div;
    mq_fact_decode(f, &a, &b, &div);
    int32_t near[6] = {div ? a + 1 : (a + 1) * b, div ? a - 1 : (a - 1) * b, div ? a + 2 : a * (b + 1),
                       div ? b : a * (b - 1), div ? a * b : a + b, div ? a + 3 : (a + 2) * b};
    for (int k = 0; k < 6 && n < 4; k++)
        opt_add(out, &n, near[k] > 0 ? near[k] : -1);
    for (int k = 1; k < 20 && n < 4; k++) /* (x0, x1: the near ones collapse; the next numbers up) */
        opt_add(out, &n, mq_fact_answer(f) + k);
    for (int i = 1; i < n; i++) /* in order: the same buttons while the same stars fall */
        for (int j = i; j > 0 && out[j - 1] > out[j]; j--) {
            int32_t t = out[j];
            out[j] = out[j - 1], out[j - 1] = t;
        }
    return n;
}
/* a choice picked (its index), or typed: what is typed so far picks the one choice it spells */
void mq_fg_choose(mq_t *q, int i)
{
    int32_t c[4];
    int n = mq_fg_choices(q, c);
    if (i < 0 || i >= n)
        return;
    q->api->snprintf(q->input, sizeof q->input, "%ld", (long)c[i]);
    q->in_len = 0;
    while (q->input[q->in_len])
        q->in_len++;
    mq_fg_submit(q);
}
void mq_fg_typed(mq_t *q)
{
    int32_t c[4];
    int n = mq_fg_choices(q, c), match = -1, prefix = 0;
    for (int i = 0; i < n; i++) {
        char s[12];
        q->api->snprintf(s, sizeof s, "%ld", (long)c[i]);
        int k = 0;
        while (q->input[k] && s[k] == q->input[k])
            k++;
        if (!q->input[k])
            prefix++, match = s[k] ? match : i;
    }
    if (match >= 0 && prefix == 1)
        mq_fg_choose(q, match);
    else if (!prefix)
        q->in_len = 0, q->input[0] = 0; /* (spells none of them: start again) */
}

/* The player pressed ENTER (or ok) with q->input typed. */
void mq_fg_submit(mq_t *q)
{
    mq_demo_seen(q, SPD_FG + q->fg);
    if (!q->in_len || fg_done(q))
        return;
    int32_t v = 0;
    for (int i = 0; i < q->in_len; i++)
        v = v * 10 + (q->input[i] - '0');
    q->in_len = 0, q->input[0] = 0;
    q->msg[0][0] = q->msg[1][0] = 0;
    if (q->fg == FG_STARS) {
        int hit = -1;
        for (int i = 0; i < 2; i++)
            if (q->fall[i].alive && mq_fact_answer(q->fall[i].fact) == v &&
                (hit < 0 || q->fall[i].y > q->fall[hit].y))
                hit = i;
        if (hit >= 0) {
            mq_faller_t *s = &q->fall[hit];
            uint32_t ms = (uint32_t)(uint16_t)((int16_t)q->now - s->patience);
            mq_fact_record(q, s->fact, true, ms);
            mq_spark_burst(q, s->x, s->y, 12);
            q->fg_score++, q->fg_streak++;
            mq_prof(q)->stars = mq_sat16(mq_prof(q)->stars + 1u);
            fg_new_faller(q, s, hit);
        } else {
            /* gentle: show the strategy of the star she was going for -- the one whose answer is
             * nearest to what she typed (the lower one on a tie); only that fact counts the miss */
            int lo = q->fall[0].y > q->fall[1].y ? 0 : 1;
            if (q->fall[0].alive && q->fall[1].alive) {
                int d0 = mq_fact_answer(q->fall[0].fact) - v, d1 = mq_fact_answer(q->fall[1].fact) - v;
                d0 = d0 < 0 ? -d0 : d0, d1 = d1 < 0 ? -d1 : d1;
                if (d0 != d1)
                    lo = d0 < d1 ? 0 : 1;
            }
            char st[3][MQ_TXT];
            mq_fact_strategy(q, q->fall[lo].fact, st, NULL);
            q->api->snprintf(q->msg[0], MQ_TXT, "%s", st[0]);
            q->api->snprintf(q->msg[1], MQ_TXT, "%s", st[1]);
            mq_fact_record(q, q->fall[lo].fact, false, 0);
            q->fg_streak = 0;
            q->fg_miss_t = mq_mark_now(q); /* calm: the stars wait a moment */
        }
        if (fg_done(q))
            fg_finish(q);
        return;
    }
    int f = q->fg_cur;
    uint32_t ms = q->now - q->prob_start;
    if (mq_fact_answer(f) == v) {
        /* typed again after the hint showed it: the flower opens, but it is no evidence of a known
         * fact (no box up, no fast time; the miss already queued it to come back) */
        if (!q->fg_hinted)
            mq_fact_record(q, f, true, ms);
        q->fg_hinted = 0;
        q->fg_score++, q->fg_streak++;
        mq_prof(q)->stars = mq_sat16(mq_prof(q)->stars + 1u);
        if (q->fg == FG_BLOOM)
            q->fg_bloom = (uint8_t)(q->fg_bloom + 1);
        if (q->fg == FG_RACE)
            q->fg_you = (int16_t)(q->fg_you + 1000 / q->fg_goal + 1);
        if (q->fg == FG_CAFE)
            q->fall[0].patience = 1000, q->fall[0].x = (int16_t)((q->fall[0].x + 1) % 7);
        mq_spark_burst(q, 160, MQ_Y0 + 70, ms <= FLUENT_MS ? 14 : 6);
        q->fg_cur = (int16_t)mq_fact_next(q);
        q->prob_start = q->now;
    } else {
        char st[3][MQ_TXT];
        mq_fact_strategy(q, f, st, NULL);
        q->api->snprintf(q->msg[0], MQ_TXT, "%s", st[0]);
        q->api->snprintf(q->msg[1], MQ_TXT, "%s", st[1]);
        mq_fact_record(q, f, false, ms);
        q->fg_streak = 0;
        q->fg_hinted = 1;
        q->prob_start = q->now; /* same fact again right away, with the hint showing */
    }
    if (fg_done(q))
        fg_finish(q);
}

static void fact_text(mq_t *q, int f, char *out, size_t n)
{
    int a, b;
    bool div;
    mq_fact_decode(f, &a, &b, &div);
    if (div)
        q->api->snprintf(out, n, "%d\xC3\xB7%d", a * b, b);
    else
        q->api->snprintf(out, n, "%d\xC3\x97%d", a, b);
}

/* Star Catch's fall (px a second): 8 to start, +1 a streak step (at most +6), and capped so a star
 * from the top (110 px) lands no sooner than 1.5 x her think time for its fact */
int mq_star_speed(mq_t *q, int f)
{
    int v = 8 + (q->fg_streak > 6 ? 6 : q->fg_streak);
    const mq_fact_t *t = f >= 0 && f < MQ_NFACT ? &mq_prof(q)->fact[f] : NULL;
    int think = t && t->t10 ? t->t10 * 100 : 4000;
    if (think < 1500)
        think = 1500;
    int cap = 110 * 1000 * 2 / (3 * think);
    return v < cap ? v : cap < 3 ? 3 : cap;
}

/* The game scene (the input line and keypad are drawn by mq_main.c). */
void mq_fg_draw(mq_t *q)
{
    const modx_api_t *api = q->api;
    char s[24];
    int y0 = MQ_Y0;
    switch (q->fg) {
    case FG_STARS:
        mq_fill(q, 0, y0, MQ_W, 150, RGB(60, 52, 110));
        for (int i = 0; i < 18; i++) /* twinkling sky */
            mq_fill(q, (i * 53 + 17) % 320, y0 + (i * 29) % 140, 2, 2, (q->anim_t / 300 + i) % 3 ? Q_LAV : Q_YELLOW);
        mq_fill(q, 0, y0 + 136, MQ_W, 14, RGB(120, 100, 170));
        /* the goal, the same words every time, on the ground under the stars (the hint takes its place) */
        if (!q->msg[0][0] && !fg_done(q) && !mq_demo_running(q, SPD_FG + q->fg, q->fg_t0)) { /* (the demo says it meanwhile) */
            mq_rrect(q, 60, y0 + 137, 200, 13, 6, RGB(90, 80, 150));
            mq_text_c(q, api->font_small, 160, y0 + 138, "Tap a star's answer to catch it!", Q_YELLOW);
        }
        for (int i = 0; i < 2; i++) {
            mq_faller_t *st = &q->fall[i];
            if (!st->alive || st->y < y0 + 10)
                continue;
            mq_star(q, st->x, st->y + 12, 26, Q_YELLOW); /* bigger stars, big dark numbers */
            mq_circle(q, st->x, st->y + 13, 15, Q_WHITE);
            fact_text(q, st->fact, s, sizeof s);
            mq_text_c(q, api->font_medium, st->x, st->y + 6, s, Q_INK);
        }
        mq_sprite(q, 8, 140, q->msg[0][0] ? y0 + 86 : y0 + 100, 2, 1); /* (a hint below: the catcher hops up out of its way) */
        break;
    case FG_CAFE:
        mq_fill(q, 0, y0, MQ_W, 150, Q_CREAM);
        mq_fill(q, 0, y0 + 100, MQ_W, 50, Q_PEACH);
        mq_fill(q, 0, y0 + 98, MQ_W, 4, Q_BROWN);
        mq_sprite(q, q->fall[0].x % 7, 30, y0 + 40, 3, 1);
        mq_rrect(q, 110, y0 + 20, 150, 50, 10, Q_WHITE);
        fact_text(q, q->fg_cur, s, sizeof s);
        mq_text_c(q, api->font_xlarge, 185, y0 + 30, s, Q_INK);
        /* patience as a heart that fills (never a penalty) */
        mq_fill(q, 120, y0 + 76, 100, 8, Q_GREY);
        mq_fill(q, 120, y0 + 76, q->fall[0].patience / 10, 8, Q_PINK);
        api->snprintf(s, sizeof s, "orders %d/%d", q->fg_score, q->fg_goal);
        mq_text(q, api->font_small, 230, y0 + 76, s, Q_INK);
        for (int i = 0; i < q->fg_score && i < 10; i++)
            mq_circle(q, 130 + i * 18, y0 + 108, 7, i % 2 ? Q_PINK : Q_YELLOW); /* plates served (above where a hint goes) */
        break;
    case FG_BLOOM:
        mq_fill(q, 0, y0, MQ_W, 150, Q_SKY);
        mq_fill(q, 0, y0 + 110, MQ_W, 40, Q_MINT);
        for (int i = 0; i < 12; i++) {
            int x = 20 + i * 25, y = y0 + 112;
            mq_line(q, x, y, x, y - 18, Q_GREEN);
            if (i < q->fg_bloom) {
                uint16_t c = i % 3 == 0 ? Q_PINK : i % 3 == 1 ? Q_YELLOW : Q_LAV;
                for (int k = 0; k < 5; k++)
                    mq_circle(q, x + mq_icos(k * 72) * 6 / 1024, y - 22 + mq_isin(k * 72) * 6 / 1024, 4, c);
                mq_circle(q, x, y - 22, 3, Q_ORANGE);
            } else
                mq_circle(q, x, y - 20, 3, Q_GREEN);
        }
        mq_rrect(q, 90, y0 + 20, 140, 44, 10, Q_WHITE);
        fact_text(q, q->fg_cur, s, sizeof s);
        mq_text_c(q, api->font_xlarge, 160, y0 + 28, s, Q_INK);
        if (q->fg_streak >= 3) {
            api->snprintf(s, sizeof s, "streak %d!", q->fg_streak);
            mq_text_c(q, api->font_medium, 160, y0 + 70, s, Q_HOTPINK);
        }
        mq_sprite(q, 0, 262, y0 + 50, 2, 1);
        break;
    default: { /* FG_RACE */
        mq_fill(q, 0, y0, MQ_W, 150, Q_MINT);
        mq_fill(q, 0, y0 + 50, MQ_W, 30, Q_CREAM);
        mq_fill(q, 0, y0 + 90, MQ_W, 30, Q_CREAM);
        for (int i = 0; i < 8; i++)
            mq_fill(q, 296, y0 + 50 + i * 9, 6, 9, i % 2 ? Q_INK : Q_WHITE);
        int bx = 10 + q->fg_you * 260 / 1000, fx = 10 + q->fg_friend * 260 / 1000;
        mq_sprite(q, mq_prof(q)->buddy % MQ_NCAST, bx, y0 + 46, 2, 1);
        mq_sprite(q, 7, fx, y0 + 86, 2, 1);
        mq_rrect(q, 100, y0 + 4, 120, 40, 10, Q_WHITE);
        fact_text(q, q->fg_cur, s, sizeof s);
        mq_text_c(q, api->font_xlarge, 160, y0 + 10, s, Q_INK);
        break;
    }
    }
    { /* the first time: the guide shows how to answer (the keys below) */
        static const char cap[FG_COUNT][40] = {"Tap a star's answer to catch it!", "Type the order's number, then ok!",
                                               "Type the answer: a flower blooms!", "Tap the answer to zoom ahead!"};
        if (mq_demo_running(q, SPD_FG + q->fg, q->fg_t0))
            mq_lane(q, 20, y0 + 134, 300); /* (the words over the ground, not over the keys) */
        if (mq_fg_choosing(q)) /* a tap on an answer (low on it: its number shows) */
            mq_demo_overlay(q, SPD_FG + q->fg, q->fg_t0, 62, y0 + 192, 62, y0 + 192, false, cap[q->fg % FG_COUNT]);
        else
            mq_demo_overlay(q, SPD_FG + q->fg, q->fg_t0, 30, y0 + 170, 200, y0 + 170, true, cap[q->fg % FG_COUNT]);
    }
    if (q->msg[0][0]) {
        mq_rrect(q, 6, y0 + 124, 308, 26, 6, Q_WHITE);
        mq_text(q, api->font_small, 10, y0 + 125, q->msg[0], Q_INK);
        mq_text(q, api->font_small, 10, y0 + 137, q->msg[1], Q_INK);
    }
}

bool mq_fg_finished(mq_t *q);
bool mq_fg_finished(mq_t *q) { return fg_done(q); }
