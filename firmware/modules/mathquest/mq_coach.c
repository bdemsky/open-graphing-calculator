/*
 * Math Quest: the coach. Research-based tuning that the child never sees (design rationale and
 * references in docs/MATHQUEST.md, "Why it plays like this").
 *
 *   - success near 85 %: each skill's difficulty follows its recent first-try success (the last
 *     8 answers): above 90 % one step harder, below 75 % one step easier (Wilson et al. 2019);
 *   - try again once: a wrong typed answer gets one more try with a small hint before the
 *     answer, the model and the trick are shown (mq_main.c submit_value);
 *   - faded scaffolding: the worked example is full while a skill is new, a completion problem
 *     (she finishes the example's last step herself) in the middle, and not offered at all once
 *     the skill is mastered; the question's model fades too (mq_vis.c);
 *   - spaced evidence: a skill counts as mastered only when the mastery bar is reached again on
 *     a later play day than the first time;
 *   - guessing: two quick wrong first answers (under 1.5 s) in a row: the next three items earn
 *     nothing and choice tiles with numbers become typed answers -- quietly, no message;
 *   - a local play log (never sent anywhere): per skill success, average first-answer time,
 *     long thinks; where play stopped; on the grown-up screen, `mq log`, and as a CSV file on the
 *     card;
 *   - conversation prompts for grown-ups; a greeting that remembers her; weekly events by play
 *     day (there is no clock): a seasonal item in the shop.
 */
#include "mq.h"



/* ------------------------------------------------------------------ the 85 % controller */
static int bits(unsigned v)
{
    int n = 0;
    for (; v; v >>= 1)
        n += v & 1;
    return n;
}

/* recent first-try success of a skill: percent, and how many answers it is based on */
int mq_coach_rate(mq_t *q, int skill, int *n)
{
    uint16_t r = mq_learn(q)->recent[skill];
    int k = 0;
    while ((r >> k) > 1) /* the highest set bit is the sentinel */
        k++;
    if (n)
        *n = k;
    return k ? bits(r & ((1u << k) - 1)) * 100 / k : 0;
}

/* A first answer to a skill: the window of its recent first answers (the success rate the grown-ups
 * see, the 85 % target), and its rung on the ladder: up once the rung is solid, down when she struggles
 * there (mq_rung.c). Times feed the play log. */
void mq_coach_answer(mq_t *q, int skill, bool right, uint32_t ms)
{
    if (skill < 0 || skill >= MQ_NSKILL)
        return;
    mq_learn_t *L = mq_learn(q);
    uint16_t r = L->recent[skill] ? L->recent[skill] : 1;
    if (r >= 0x100) /* keep the last 8 */
        r = (uint16_t)(0x80 | (r & 0x7F)); /* the sentinel above the newest 7 */
    L->recent[skill] = (uint16_t)(r << 1 | (right ? 1 : 0));
    if (q->prob.skill == skill && !q->ws_guided && !q->used_help) { /* (an order she was walked through, or helped with,
                                                                    * is no evidence) */
        int think = mq_think_ms(q, &q->prob);
        bool quick = right && ms * 10 < (uint32_t)think * 6;
        mq_rung_answer(q, skill, q->prob.diff, right, quick);
    }
    uint32_t d = ms / 100;
    if (d > 255)
        d = 255;
    if (d)
        L->t10[skill] = (uint8_t)(L->t10[skill] ? (L->t10[skill] * 3u + d) / 4u : d);
    if (ms > 20000u && L->hesit[skill] < 255)
        L->hesit[skill]++;
    q->dirty = true;
}

/* ------------------------------------------------------------------ spaced evidence */
/* The mastery bar was reached: does it count yet? Only on a later play day than the first time. */
bool mq_coach_spaced(mq_t *q, int skill)
{
    mq_learn_t *L = mq_learn(q);
    uint16_t day = mq_prof(q)->days ? mq_prof(q)->days : 1;
    if (!L->pass_day[skill]) {
        L->pass_day[skill] = day;
        return false;
    }
    return day > L->pass_day[skill];
}

/* mastery fell well below the bar: the first day starts again */
void mq_coach_unspace(mq_t *q, int skill)
{
    if (mq_prof(q)->sk[skill].mastery < 50)
        mq_learn(q)->pass_day[skill] = 0;
}

/* ------------------------------------------------------------------ fading */
/* 0: the full worked example; 1: a completion problem; 2: no example (fluent) */
int mq_coach_fade(mq_t *q, int skill)
{
    const mq_skrec_t *s = &mq_prof(q)->sk[skill < MQ_NSKILL ? skill : 0];
    return (s->flags & SK_PASSED) ? 2 : s->mastery >= 40 ? 1 : 0;
}

/* ------------------------------------------------------------------ guessing */
/* a first answer: quick wrong ones in a row mean guessing */
/* mashing (answers tapped faster than anyone reads): quick wrong ones in a row; then the next answers earn nothing (no coins, no stars: the bot's 16),
 * and the game pauses taps a moment and points at what to look at (mq_play.c) */
void mq_coach_guess(mq_t *q, bool right, uint32_t ms)
{
    if (!right && ms < 1500u)
        q->rapid++;
    else
        q->rapid = 0;
    if (q->rapid >= 2) {
        q->guess_n = 5, q->rapid = 0;
        q->mash_t = q->now ? q->now : 1;
    }
}

/* while guessing: a choice whose tiles are all numbers is asked as a typed answer */
void mq_coach_typed(mq_t *q, mq_prob_t *p)
{
    if (!q->guess_n || p->kind != K_CHOICE || p->nchoice < 2)
        return;
    int32_t val[4];
    for (int i = 0; i < p->nchoice; i++) {
        const char *s = p->choice[i];
        int32_t v = 0;
        if (*s < '0' || *s > '9')
            return;
        for (; *s; s++) {
            if (*s < '0' || *s > '9')
                return;
            v = v * 10 + (*s - '0');
        }
        val[i] = v;
    }
    int32_t ans = val[p->ans];
    p->nwrong = 0;
    for (int i = 0; i < p->nchoice && p->nwrong < 6; i++)
        if (i != p->ans)
            p->wrong[p->nwrong] = val[i], p->wrong_mc[p->nwrong++] = p->choice_mc[i];
    p->kind = K_NUM, p->ans = ans, p->nchoice = 0;
}

/* ------------------------------------------------------------------ the play log */
void mq_coach_quit(mq_t *q)
{
    mq_learn_t *L = mq_learn(q);
    int sc = q->scene; /* (the four newest scenes counted with their nearest of the 24 the save keeps: CR-16) */
    if (sc == SC_QUIZ)
        sc = SC_PLAY;
    else if (sc == SC_TOYS || sc == SC_MERGE)
        sc = SC_ROOM;
    else if (sc == SC_CHAPTER)
        sc = SC_WORLD;
    if (sc < 24 && L->quit[sc] < 65535)
        L->quit[sc]++;
    q->dirty = true;
}

/* the log as CSV lines, one call of out() per line */
void mq_coach_log(mq_t *q, void (*out)(void *user, const char *line), void *user)
{
    const modx_api_t *api = q->api;
    mq_profile_t *p = mq_prof(q);
    mq_learn_t *L = mq_learn(q);
    char b[168];
    api->snprintf(b, sizeof b, "player,%s,days,%u,minutes,%lu,problems,%lu", mq_name(q), (unsigned)p->days,
                  (unsigned long)(p->secs / 60), (unsigned long)p->problems);
    out(user, b);
    out(user, "skill,world,grade,mastery,passed,first_try_right,tries,recent_rate,avg_first_s,long_thinks,rung,rungs,review_box,"
              "rung_name");
    for (int s = 0; s < MQ_NSKILL; s++) {
        const mq_skrec_t *k = &p->sk[s];
        if (!k->tries)
            continue;
        int n, rate = mq_coach_rate(q, s, &n);
        char rn[40];
        mq_rung_name(s, k->diff, rn, sizeof rn);
        for (char *c = rn; *c; c++) /* (a CSV field) */
            if (*c == ',')
                *c = ';';
        api->snprintf(b, sizeof b, "%s,%s,%d,%d,%d,%d,%d,%d,%d.%d,%d,%d,%d,%d,%s", mq_skill[s].name,
                      mq_world_name[mq_skill[s].world], mq_skill[s].grade, k->mastery, (k->flags & SK_PASSED) != 0,
                      k->right, k->tries, n ? rate : -1, L->t10[s] / 10, L->t10[s] % 10, L->hesit[s], k->diff,
                      mq_rungs(s), L->rev[s].box, rn);
        out(user, b);
    }
    /* the helper friend's moments, the misses by misconception, the focus facts (for the grown-ups) */
    mq_pace_t *pc = mq_pace(q);
    out(user, "helper_friend,skill,times");
    for (int s = 0; s < MQ_NSKILL; s++)
        if (pc->helped[s]) {
            api->snprintf(b, sizeof b, "helper_friend,%s,%d", mq_skill[s].name, pc->helped[s]);
            out(user, b);
        }
    out(user, "big_idea,parts,shown,knew,skipped,replays");
    for (int i = 1; i < IDEA_COUNT; i++) {
        int a, b2, c;
        if (mq_teach_summary(q, i, &a, &b2, &c) || pc->idea_replays[i]) {
            api->snprintf(b, sizeof b, "big_idea,%s,%d,%d,%d,%d,%d", mq_idea_name(i), mq_teach_nseg(i), a, b2, c, pc->idea_replays[i]);
            out(user, b);
        }
    }
    mq_diag_log(q, out, user); /* the error patterns: status, shown of chances, her last example */
    mq_strat_log(q, out, user); /* the ways to work it: seen, used, shown by the helper */
    out(user, "misconception,times");
    for (int m = 1; m < MC_COUNT && m < 64; m++)
        if (pc->mc_seen[m]) {
            api->snprintf(b, sizeof b, "%s,%d", mq_mc_name[m], pc->mc_seen[m]);
            out(user, b);
        }
    out(user, "focus_fact,seen,right,avg_s,fast,known_two_days");
    for (int f = 0; f < MQ_NFACT; f++)
        if (mq_focus_fact(q, f)) {
            const mq_fact_t *t = &p->fact[f];
            int a, c;
            bool dv;
            mq_fact_decode(f, &a, &c, &dv);
            api->snprintf(b, sizeof b, "%d%s%d,%d,%d,%d.%d,%d,%d", dv ? a * c : a, dv ? " / " : " x ", c, t->seen, t->right & 0x7F,
                          t->t10 / 10, t->t10 % 10, t->box >= 4 && t->t10 && t->t10 <= 60, mq_fact_fluent(t));
            out(user, b);
        }
    if (mq_focus_active(q) || mq_focus_ready(q)) {
        api->snprintf(b, sizeof b, "focus_ready_for_the_test,%d", mq_focus_ready(q));
        out(user, b);
    }
    out(user, "stopped_at,count");
    for (int c = 0; c < 24 && c < SC_COUNT; c++)
        if (L->quit[c]) {
            api->snprintf(b, sizeof b, "%s,%u", mq_scene_name_of(c), (unsigned)L->quit[c]);
            out(user, b);
        }
}

static void con_line(void *user, const char *line)
{
    mq_t *q = user;
    q->api->con_printf("%s\n", line);
}

static void file_line(void *user, const char *line)
{
    mq_t *q = user;
    if (q->log_err)
        return;
    size_t n = 0;
    while (line[n])
        n++;
    if (q->api->file_write(line, n) || q->api->file_write("\r\n", 2))
        q->log_err = 1;
}

/* the log to /CALC/MQLOGnnn.CSV; path gets the file name; 0 or an FS_E* error */
int mq_coach_export(mq_t *q, char *path, size_t n)
{
    int r = q->api->file_create("MQLOG", "CSV", path, n);
    if (r)
        return r;
    q->log_err = 0;
    mq_coach_log(q, file_line, q);
    return q->api->file_close(!q->log_err) ? -1 : q->log_err ? -1 : 0;
}

int mq_coach_console(mq_t *q) /* `mq log`: the play log as CSV */
{
    mq_coach_log(q, con_line, q);
    return 0;
}

/* ------------------------------------------------------------------ grown-ups, greetings, events */
/* something to talk about at home, from the world she plays most lately */
const char *mq_coach_prompt(mq_t *q)
{
    static const char pr[MQ_NWORLD][64] = {
        "Ask her: how many legs do 4 dogs have?", "Ask her: which is bigger, 307 or 370?",
        "Share a pizza: is 1/3 or 1/4 of it more?", "Ask her: what time is 20 minutes from now?",
        "Ask her: how many quarters make a dollar?", "Ask her: how many tiles cover a 3 by 4 rug?",
        "Count favourite fruits at home and draw a graph", "Ask her: what is 12 x 25? (a tricky one!)",
        "Ask her: is half of a half more or less than a half?"};
    int w = mq_skill[q->prob.skill < MQ_NSKILL ? q->prob.skill : 0].world;
    return pr[w < MQ_NWORLD ? w : 0];
}

/* the guide remembers her: her name and a recent win */
void mq_coach_greet(mq_t *q, char *out, size_t n)
{
    const mq_profile_t *p = mq_prof(q);
    const mq_home_t *h = mq_home(q);
    if (h->best_combo >= 5 && (p->problems & 1))
        q->api->snprintf(out, n, "Hi %s! %d in a row last time!", mq_name(q), h->best_combo);
    else if (p->bosses || p->pad[1])
        q->api->snprintf(out, n, "Welcome back, %s, champion!", mq_name(q));
    else
        q->api->snprintf(out, n, "Welcome back, %s!", mq_name(q));
}

/* this week's event (play days: 7 per event, four events in turn) */
int mq_event(const mq_profile_t *p) { return (p->days ? p->days - 1 : 0) / 7 % 4; }
const char *mq_event_name(int e)
{
    static const char nm[4][16] = {"Blossom week", "Beach week", "Pumpkin week", "Snow week"};
    return nm[e & 3];
}
