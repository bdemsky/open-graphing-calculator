/*
 * Math Quest: the learning engine under the game. (Nothing here is ever shown to the child;
 * the grown-up screen shows its numbers.)
 *
 * 1. Every problem's history (mq_seen_t, MQ_NSEEN per profile, saved): its canonical hash
 *    (mq_prob_ident: the skill and its numbers, commutative twins alike), when it was last
 *    seen (minutes of PLAY: the calculator has no clock), the last 8 results (bit 0 newest,
 *    1 = right), the current streak of rights, an ease (SM-2 style, 1.30..3.00 stored x50) and
 *    the expiry. While the expiry is in the future the problem is not asked again.
 *      right:  ease += 0.05 (a slow right: no change), streak++,
 *              TTL = base(mastery) x ease x 2^(streak-1) minutes (base 10 + 2 x mastery,
 *              a slow right half of it): a long clean streak puts a problem away for a long
 *              time, one right after many misses only briefly;
 *      wrong:  ease -= 0.20, streak = 0, TTL 2 minutes (it comes back soon, with similar
 *              problems in between).
 *    The x / / facts keep their own Leitner boxes (mq_facts.c), which already space them by
 *    fact rounds; their attempts are recorded here too (history for the grown-ups).
 *    The oldest entry (last seen longest ago) makes room for a new one.
 *
 * 2. Interleaved review of earlier skills (mq_rev_t per skill): a skill joins when it was
 *    first played (box 1) and moves up a box when it is passed; box k is due again
 *    5, 15, 45, 120, 360, 900 minutes of play later. A share of the items of any run (15 % at
 *    first) comes from a due earlier skill instead of the run's own skill: due skills first,
 *    then the ones whose problems are weak and due, and when the current skill is hard for her
 *    its prerequisites (mq_prereq[]: division -> multiplication, adding fractions ->
 *    equivalent fractions, ...). A right answer moves the skill up a box and lowers the share
 *    toward 10 %; a wrong one moves it down a box, lowers its mastery a step, raises the share
 *    (up to 40 %), and a second slip on the same skill brings back its worked example. The item
 *    is an ordinary game item of the run (no label), and it goes through the no-repeat rule.
 */
#include "mq.h"

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

mq_learn_t *mq_learn(mq_t *q) { return &q->save.learn[q->save.cur & 1]; }

/* minutes of play (the profile's play time) */
uint16_t mq_play_min(mq_t *q) { return (uint16_t)(mq_prof(q)->secs / 60u); }
static int16_t mdiff(uint16_t a, uint16_t b) { return (int16_t)(uint16_t)(a - b); } /* a - b, wrap-safe */

/* ------------------------------------------------------------------ per-problem history */
#define EASE_MIN 65  /* 1.30 x 50 */
#define EASE_MAX 150 /* 3.00 x 50 */
#define EASE_START 125

/* minutes until a problem may come back after a right answer */
uint32_t mq_seen_ttl(int mastery, int ease, int streak, bool slow)
{
    uint32_t base = 10u + 2u * (uint32_t)(mastery < 0 ? 0 : mastery > 100 ? 100 : mastery);
    if (streak < 1)
        streak = 1;
    if (streak > 9)
        streak = 9;
    uint32_t t = base * (uint32_t)ease / 50u << (streak - 1);
    if (slow)
        t /= 2;
    return t > 30000u ? 30000u : t < 2 ? 2 : t; /* (under 32768: the wrap-safe compare of a 16-bit minute -- CR-03) */
}

mq_seen_t *mq_seen_find(mq_t *q, uint32_t hash)
{
    mq_learn_t *L = mq_learn(q);
    for (int i = 0; i < MQ_NSEEN; i++)
        if (L->seen[i].hash == hash && (L->seen[i].res || L->seen[i].ease))
            return &L->seen[i];
    return NULL;
}

/* not to be asked again yet? */
bool mq_seen_blocked(mq_t *q, uint32_t hash)
{
    mq_seen_t *e = mq_seen_find(q, hash);
    return e && mdiff(e->exp, mq_play_min(q)) > 0;
}

/* one attempt at problem p: right or not, slow or not */
mq_seen_t *mq_seen_record(mq_t *q, const mq_prob_t *p, bool right, bool slow)
{
    mq_learn_t *L = mq_learn(q);
    uint32_t h = mq_prob_ident(p);
    uint16_t now = mq_play_min(q);
    mq_seen_t *e = mq_seen_find(q, h);
    if (!e) { /* a new entry: the first empty one, else the one seen longest ago */
        int best = 0;
        for (int i = 0; i < MQ_NSEEN; i++) {
            if (!L->seen[i].ease) {
                best = i;
                break;
            }
            if (mdiff(L->seen[best].last, L->seen[i].last) > 0)
                best = i;
        }
        e = &L->seen[best];
        *e = (mq_seen_t){h, now, now, 0, 0, EASE_START, p->skill};
    }
    e->last = now;
    e->res = (uint8_t)(e->res << 1 | (right ? 1 : 0));
    e->skill = p->skill;
    if (right) {
        if (!slow)
            e->ease = (uint8_t)imin(EASE_MAX, e->ease + 2);
        e->streak = (uint8_t)imin(255, e->streak + 1);
        int m = p->skill < MQ_NSKILL ? mq_prof(q)->sk[p->skill].mastery : 0;
        e->exp = (uint16_t)(now + mq_seen_ttl(m, e->ease, e->streak, slow));
    } else {
        e->ease = (uint8_t)imax(EASE_MIN, e->ease - 10);
        e->streak = 0;
        e->exp = (uint16_t)(now + 2);
    }
    q->dirty = true;
    return e;
}

/* weak problems of a skill whose time has come (missed lately, expired) */
static int weak_due(mq_t *q, int skill)
{
    mq_learn_t *L = mq_learn(q);
    uint16_t now = mq_play_min(q);
    int n = 0;
    for (int i = 0; i < MQ_NSEEN; i++) {
        const mq_seen_t *e = &L->seen[i];
        if (e->ease && e->skill == skill && mdiff(e->exp, now) <= 0 && (e->res & 0x0F) != 0x0F)
            n++;
    }
    return n;
}

/* ------------------------------------------------------------------ interleaved review */
static const uint16_t interval[6] = {5, 5, 15, 45, 120, 360}; /* by box (box 6+: 900) */
static uint16_t ivl(int box) { return box >= 6 ? 900 : interval[box < 0 ? 0 : box]; }

int mq_review_share(mq_t *q)
{
    int s = mq_learn(q)->share;
    return s ? s : 15;
}

static void set_share(mq_t *q, int s) { mq_learn(q)->share = (uint8_t)imax(10, imin(40, s)); }

/* a skill was played (box 1) or passed (up a box) */
void mq_review_join(mq_t *q, int skill, bool passed)
{
    if (skill < 0 || skill >= MQ_NSKILL || skill == S_FACTS)
        return;
    mq_rev_t *r = &mq_learn(q)->rev[skill];
    if (!r->box)
        r->box = 1;
    if (passed && r->box < 3)
        r->box = 3;
    r->due = (uint16_t)(mq_play_min(q) + ivl(r->box));
    q->dirty = true;
}

/* The earlier skill to bring in now instead of `cur` (-1: none): due ones by how overdue they
 * are, weak-and-due problems first; when `cur` is hard for her, its prerequisites even if not
 * due yet. */
int mq_review_pick(mq_t *q, int cur, bool struggling)
{
    mq_learn_t *L = mq_learn(q);
    uint16_t now = mq_play_min(q);
    int best = -1, bscore = -0x7FFF;
    for (int s = 0; s < MQ_NSKILL; s++) {
        const mq_rev_t *r = &L->rev[s];
        if (!r->box || s == cur || s == S_FACTS)
            continue;
        int over = -mdiff(r->due, now); /* minutes overdue (<0: not yet) */
        bool pre = cur >= 0 && cur < MQ_NSKILL && (mq_prereq[cur][0] == s + 1 || mq_prereq[cur][1] == s + 1);
        /* fraction magnitude (the number line, comparing) weighs more (Siegler) */
        bool mag = s == S_FRACLINE || s == S_CMPFRAC || s == S_CMPDIFF;
        int score = over + weak_due(q, s) * 30 + (pre && struggling ? 1000 : 0) + (mag ? 20 : 0);
        if (over < 0 && !(pre && struggling))
            continue;
        if (score > bscore)
            bscore = score, best = s;
    }
    return best;
}

/* the answer to an item brought in for review */
void mq_review_answer(mq_t *q, int skill, bool right)
{
    if (skill < 0 || skill >= MQ_NSKILL)
        return;
    mq_learn_t *L = mq_learn(q);
    mq_rev_t *r = &L->rev[skill];
    if (right) {
        r->box = (uint8_t)imin(7, r->box + 1);
        r->slips = 0;
        set_share(q, mq_review_share(q) - 1);
    } else {
        r->box = (uint8_t)imax(1, r->box - 1);
        r->slips = (uint8_t)imin(255, r->slips + 1);
        set_share(q, mq_review_share(q) + 5);
        mq_skrec_t *sk = &mq_prof(q)->sk[skill]; /* a step down */
        sk->mastery = (uint8_t)(sk->mastery > 15 ? sk->mastery - 15 : 0);
        if (sk->mastery < 75)
            sk->flags &= (uint8_t)~SK_PASSED;
    }
    r->due = (uint16_t)(mq_play_min(q) + (right ? ivl(r->box) : 3));
    q->dirty = true;
}

/* Should the next item of a run be an earlier skill? -> that skill, or -1. */
int mq_review_next(mq_t *q, int cur)
{
    mq_profile_t *p = mq_prof(q);
    bool struggling = q->missrun >= 1 || (cur >= 0 && cur < MQ_NSKILL && p->sk[cur].tries >= 3 && p->sk[cur].mastery < 40 &&
                                          (mq_learn(q)->recent[cur] & 7) != 7); /* (a new skill she is getting right
                                          * is not a struggle: a raised grade's first orders stayed reviews) */
    int share = mq_review_share(q) + (struggling ? 15 : 0);
    if (mq_rnd(q, 0, 99) >= share)
        return -1;
    return mq_review_pick(q, cur, struggling);
}

/* grown-up screen: skills in review, due now, and those that slipped lately */
void mq_review_stats(mq_t *q, int *in, int *due, int *slipped)
{
    mq_learn_t *L = mq_learn(q);
    uint16_t now = mq_play_min(q);
    *in = *due = *slipped = 0;
    for (int s = 0; s < MQ_NSKILL; s++)
        if (L->rev[s].box) {
            (*in)++;
            *due += mdiff(L->rev[s].due, now) <= 0;
            *slipped += L->rev[s].slips > 0;
        }
}

/* the slipped skill that slipped most (-1 none) */
int mq_review_worst(mq_t *q)
{
    mq_learn_t *L = mq_learn(q);
    int w = -1;
    for (int s = 0; s < MQ_NSKILL; s++)
        if (L->rev[s].slips && (w < 0 || L->rev[s].slips > L->rev[w].slips))
            w = s;
    return w;
}

/* Save version 2 -> 3: the history starts empty; skills already played join the review. */
void mq_learn_migrate(mq_t *q, int prof)
{
    mq_learn_t *L = &q->save.learn[prof & 1];
    const mq_profile_t *p = &q->save.prof[prof & 1];
    uint16_t now = (uint16_t)(p->secs / 60u);
    for (int s = 0; s < MQ_NSKILL; s++)
        if (p->sk[s].plays && s != S_FACTS) {
            L->rev[s].box = (uint8_t)((p->sk[s].flags & SK_PASSED) ? 3 : 1);
            L->rev[s].due = (uint16_t)(now + ivl(L->rev[s].box));
        }
}
