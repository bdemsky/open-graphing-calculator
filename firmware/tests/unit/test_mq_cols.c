/* Sparkle Town (modules/mathquest): the notebook -- column work like on paper (mq_cols.c):
 *   - for adding, taking away (trades, across zeros), multiplying (carries, two rows) and long division,
 *     in the Heist, the till and the kitchen: the boxes filled with the digits the plan asks for, by the
 *     keypad's taps, give the right answer on the page (the answer row, the quotient and its leftover) and
 *     in the game (it cheers); ones first, carries in their little boxes;
 *   - every step told (level 0): its line names the sum and what to write ("write the 3", "carry the 1");
 *   - a wrong digit gets the column's own why: the carried 1 forgotten, the small digit taken from the
 *     big one, the zero that lends, the carry in a product, a quotient too big;
 *   - the help fades: every step told, prompts, on her own; slips on her own bring the prompts back;
 *   - a slip counts the order as missed first time; an order walked through all the way moves no rung;
 *   - on her own the notebook waits for its button; "dial" puts it away;
 *   - no school words in anything the friend says.
 * Built and run by `make test`. */
#include "mq_test_util.h"

enum { K_ENTER = 49 };
enum { CS_ANS, CS_CARRY, CS_TRADE, CS_ZERO, CS_QUOT, CS_PROD, CS_DIFF, CS_DOWN, CS_REM };
static const char *const banned[] = {"math", "learn", "lesson", "homework", "grade", "quiz", "test", "skill", "problem",
                                     "level", "mistake", "worksheet", "borrowing", "correct", NULL};

static bool school(const char *s)
{
    char w[128];
    snprintf(w, sizeof w, "%s", s);
    for (char *c = w; *c; c++)
        *c = (char)(*c | 32);
    for (int i = 0; banned[i]; i++)
        if (strstr(w, banned[i]))
            return true;
    return false;
}

static bool open_in(int skill, int rung, int mech, int level)
{
    char b[48];
    snprintf(b, sizeof b, "skill %d %d", skill, rung);
    con(b);
    q->fun_ms = 0, q->rapid = 0, q->guess_n = 0;
    for (int w = 0; w < WS_COUNT; w++)
        mq_pace(q)->ws_level[w] = (uint8_t)level, mq_pace(q)->ws_clean[w] = 0;
    for (int i = 0; i < 300; i++) {
        mq_generate_fresh(q, &q->prob, skill, rung);
        if (q->prob.ws && mq_mech_fits(mech, &q->prob)) {
            q->gm.round_mech = (uint8_t)mech;
            q->tried_wrong = false, q->used_help = false, q->run_i = mech == ME_TILL ? 1 : 0; /* (the till: the change typed) */
            mq_game_order(q);
            q->gm.mod = 0, q->gm.demo_t = 0;
            if (q->gm.mech == mech && (q->prob.kind != K_MONEY || mech != ME_TILL || q->gm.mode == 2))
                return true;
        }
    }
    return false;
}

/* the keypad's key for digit d (its centre) */
static void tap_digit(int d)
{
    q->wg.n = 0;
    int r[4];
    ops.draw(q, 0, r); /* (the notebook lays its keys out) */
    for (int i = 0; i < q->wg.n; i++) {
        const mq_wg_t *g = &q->wg.w[i];
        if (g->kind == WG_KEY && g->arg == '0' + d) {
            ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(4, g->x + g->w / 2, g->y + g->h / 2));
            return;
        }
    }
    CHECK(!"no key");
}

/* the digits of a row of the page, as a number (from column c0) */
static int32_t row_value(int r, int c0, int c1)
{
    int32_t v = 0;
    bool any = false;
    for (int k = c0; k <= c1 && k < CW_COLS; k++)
        if (q->cols.cell[r][k] >= '0' && q->cols.cell[r][k] <= '9')
            v = v * 10 + (q->cols.cell[r][k] - '0'), any = true;
    return any ? v : -1;
}

static void test_fill(void)
{
    static const struct {
        int skill, rung, mech;
    } cases[] = {{S_ADDSUB, 2, ME_VAULT}, {S_ADDSUB, 3, ME_VAULT}, {S_ADDSUB, 4, ME_VAULT}, {S_ADDSUB, 5, ME_VAULT},
                 {S_ADDSUB, 6, ME_VAULT}, {S_ADDSUBBIG, 2, ME_VAULT}, {S_ADDSUBBIG, 3, ME_VAULT}, {S_ADDSUBBIG, 4, ME_VAULT},
                 {S_MULTI, 0, ME_VAULT}, {S_MULTI, 1, ME_VAULT}, {S_MULTI, 3, ME_VAULT}, {S_MULTI, 4, ME_VAULT},
                 {S_MULBIG, 2, ME_VAULT}, {S_DIVIDE, 0, ME_VAULT}, {S_DIVIDE, 1, ME_VAULT}, {S_DIVIDE, 2, ME_VAULT},
                 {S_DIVIDE, 4, ME_VAULT}, {S_DIV2, 3, ME_VAULT}, {S_CHANGE, 3, ME_TILL}}; /* (not the kitchen: a rhythm game takes no typing) */
    int kinds[WS_COUNT] = {0}, n = 0;
    for (size_t ci = 0; ci < sizeof cases / sizeof cases[0]; ci++)
        for (int t = 0; t < 6; t++) {
            q->rng = (uint32_t)(ci * 131 + t * 7) | 1;
            if (!open_in(cases[ci].skill, cases[ci].rung, cases[ci].mech, 0)) {
                fprintf(stderr, "skill %d rung %d: no notebook order in %s\n", cases[ci].skill, cases[ci].rung,
                        mq_mech_name(cases[ci].mech));
                CHECK(0);
                break;
            }
            CHECK(mq_cols_on(q));
            mq_cols_t *c = &q->cols;
            kinds[c->kind]++, n++;
            int lastcol[CW_ROWS];
            for (int r = 0; r < CW_ROWS; r++)
                lastcol[r] = 99;
            bool ones_first = true, said_all = true;
            char prev[96] = "";
            for (int k = 0; k < CW_STEPS && mq_cols_on(q); k++) {
                const mq_cstep_t *s = &c->st[c->cur];
                if ((c->kind != WS_DIV) && s->kind == CS_ANS) { /* right to left along each row */
                    ones_first &= s->col < lastcol[s->row];
                    lastcol[s->row] = s->col;
                }
                said_all &= strchr(c->say, '0' + s->want) != NULL; /* (every step told: the digit to write is in it) */
                CHECK(!school(c->say));
                snprintf(prev, sizeof prev, "%s", c->say);
                tap_digit(s->want);
            }
            if (!ones_first || !said_all)
                fprintf(stderr, "skill %d kind %d: ones first %d, every step said %d (%s)\n", cases[ci].skill, c->kind, ones_first,
                        said_all, prev);
            CHECK(ones_first && said_all);
            /* the page shows the answer, and the game took it */
            int32_t want = q->prob.kind == K_REM ? q->prob.ans / 1000 : q->prob.ans;
            int32_t got = c->kind == WS_DIV ? row_value(0, 0, CW_COLS - 1) : row_value(c->rows - 1, 0, CW_COLS - 1);
            if (c->kind == WS_DIV && q->prob.kind == K_REM) { /* the quotient, then R and the leftover */
                int rc = 0;
                while (rc < CW_COLS && q->cols.cell[0][rc] != 'R')
                    rc++;
                got = row_value(0, 0, rc - 1);
                CHECK(row_value(0, rc + 1, CW_COLS - 1) == q->prob.ans % 1000);
            }
            if (got != want)
                fprintf(stderr, "skill %d: the page says %ld, the answer is %ld\n", cases[ci].skill, (long)got, (long)want);
            CHECK(got == want);
            CHECK(q->sub == GS_CHEER && !q->tried_wrong);
        }
    CHECK(n >= 100 && kinds[WS_ADD] && kinds[WS_SUB] && kinds[WS_MUL] && kinds[WS_DIV]);
}

/* the first step of kind k of the open notebook (-1 none) */
static int find_step(int kind)
{
    for (int i = q->cols.cur; i < q->cols.n; i++)
        if (q->cols.st[i].kind == kind)
            return i;
    return -1;
}
/* fill the boxes up to step i */
static void fill_to(int i)
{
    while (mq_cols_on(q) && q->cols.cur < i)
        mq_cols_digit(q, q->cols.st[q->cols.cur].want);
}

static void test_guided_words(void)
{
    /* an addition with a carry in the ones: "6 + 7 is 13: write the 3!", then "Carry the 1" */
    for (int t = 0; t < 40; t++) {
        q->rng = (uint32_t)t * 77 + 1;
        if (!open_in(S_ADDSUB, 3, ME_VAULT, 0) || q->prob.a[2] != 0 || q->cols.st[1].kind != CS_CARRY)
            continue;
        const mq_cstep_t *s = &q->cols.st[0];
        char want[64];
        snprintf(want, sizeof want, "%d + %d is %d: write the %d!", s->x, s->y, s->x + s->y, s->want);
        CHECK(strstr(q->cols.say, want) != NULL);
        mq_cols_digit(q, s->want);
        CHECK(strstr(q->cols.say, "Carry the 1") != NULL);
        CHECK(q->ws_guided);
        return;
    }
    CHECK(!"no carrying sum");
}

static void test_why(void)
{
    char o[96];
    int found = 0;
    /* adding: the carried 1 forgotten */
    for (int t = 0; t < 60 && !(found & 1); t++) {
        q->rng = (uint32_t)t * 31 + 5;
        if (!open_in(S_ADDSUB, 4, ME_VAULT, 2) || q->prob.a[2] != 0)
            continue;
        mq_cols_open(q);
        for (int i = 0; i < q->cols.n; i++) {
            const mq_cstep_t *s = &q->cols.st[i];
            if (s->kind == CS_ANS && s->z && (s->x + s->y) % 10 != s->want) {
                fill_to(i);
                int mc = mq_cols_why(q, s, (s->x + s->y) % 10, o, sizeof o);
                CHECK(mc == MC_NO_REGROUP && strstr(o, "carried"));
                mq_cols_digit(q, (s->x + s->y) % 10); /* said, and the box tried again */
                CHECK(q->cols.cur == i && q->cols.bad == i && strstr(q->cols.say, "carried"));
                CHECK(q->tried_wrong && q->sub == GS_PLAY); /* a missed first try for the coach */
                found |= 1;
                break;
            }
        }
    }
    /* taking away: the small digit taken from the big one */
    for (int t = 0; t < 60 && !(found & 2); t++) {
        q->rng = (uint32_t)t * 37 + 9;
        if (!open_in(S_ADDSUB, 3, ME_VAULT, 2) || q->prob.a[2] == 0)
            continue;
        mq_cols_open(q);
        for (int i = 0; i < q->cols.n; i++) {
            const mq_cstep_t *s = &q->cols.st[i];
            int top = s->x - 10;
            if (s->kind == CS_ANS && s->z && s->y - top != s->want) {
                int mc = mq_cols_why(q, s, s->y - top, o, sizeof o);
                CHECK(mc == MC_SUB_FLIP && strstr(o, "traded"));
                found |= 2;
                break;
            }
        }
    }
    /* across a zero: the zero that got ten and gave one */
    for (int t = 0; t < 60 && !(found & 4); t++) {
        q->rng = (uint32_t)t * 41 + 3;
        if (!open_in(S_ADDSUB, 6, ME_VAULT, 2) || q->prob.a[2] == 0)
            continue;
        mq_cols_open(q);
        int i = find_step(CS_ZERO);
        if (i < 0)
            continue;
        int mc = mq_cols_why(q, &q->cols.st[i], 0, o, sizeof o);
        CHECK(mc == MC_NO_BORROW_ZERO && strstr(o, "9"));
        found |= 4;
    }
    /* multiplying: the carry left out */
    for (int t = 0; t < 60 && !(found & 8); t++) {
        q->rng = (uint32_t)t * 43 + 1;
        if (!open_in(S_MULTI, 1, ME_VAULT, 2))
            continue;
        mq_cols_open(q);
        for (int i = 0; i < q->cols.n; i++) {
            const mq_cstep_t *s = &q->cols.st[i];
            if (s->kind == CS_ANS && s->z && s->y && (s->x * s->y) % 10 != s->want) {
                int mc = mq_cols_why(q, s, (s->x * s->y) % 10, o, sizeof o);
                CHECK(mc == MC_NO_REGROUP && strstr(o, "carried"));
                found |= 8;
                break;
            }
        }
    }
    /* sharing: a quotient too big */
    for (int t = 0; t < 60 && !(found & 16); t++) {
        q->rng = (uint32_t)t * 47 + 7;
        if (!open_in(S_DIVIDE, 1, ME_VAULT, 2))
            continue;
        mq_cols_open(q);
        int i = find_step(CS_QUOT);
        const mq_cstep_t *s = &q->cols.st[i];
        if (s->want >= 9)
            continue;
        int mc = mq_cols_why(q, s, s->want + 1, o, sizeof o);
        CHECK(mc == MC_CLOSE && strstr(o, "more than"));
        found |= 16;
    }
    CHECK(found == 31);
}

static void finish_notebook(int slips)
{
    for (int k = 0; k < CW_STEPS && mq_cols_on(q); k++) {
        const mq_cstep_t *s = &q->cols.st[q->cols.cur];
        if (slips && k == 0)
            mq_cols_digit(q, (s->want + 1) % 10), slips--;
        if (slips && k == 1)
            mq_cols_digit(q, (s->want + 1) % 10), slips--;
        mq_cols_digit(q, q->cols.st[q->cols.cur].want);
    }
}

static void test_fading(void)
{
    con("new Fay 2");
    mq_pace_t *pc = mq_pace(q);
    pc->ws_level[WS_ADD] = 0, pc->ws_clean[WS_ADD] = 0;
    int levels[8], n = 0;
    for (int t = 0; t < 400 && n < 6; t++) {
        q->rng = (uint32_t)t * 7 + 3;
        con("skill 16 5");
        for (int i = 0; i < 100; i++) {
            mq_generate_fresh(q, &q->prob, S_ADDSUB, 5);
            if (q->prob.ws == WS_ADD)
                break;
        }
        if (q->prob.ws != WS_ADD)
            continue;
        q->gm.round_mech = ME_VAULT, q->tried_wrong = false, q->used_help = false, q->run_i = 0;
        mq_game_order(q);
        if (!mq_cols_on(q)) {
            if (pc->ws_level[WS_ADD] < 2)
                continue;
            mq_cols_open(q); /* on her own: by the notebook button */
        }
        levels[n++] = q->cols.level;
        finish_notebook(0);
    }
    /* every step told twice, prompts three times, then on her own */
    CHECK(n == 6 && levels[0] == 0 && levels[1] == 0 && levels[2] == 1 && levels[3] == 1 && levels[4] == 1 && levels[5] == 2);
    /* slipping on her own twice brings the prompts back */
    for (int k = 0; k < 2; k++) {
        q->rng = (uint32_t)k * 13 + 1;
        CHECK(open_in(S_ADDSUB, 5, ME_VAULT, 2));
        mq_cols_open(q);
        pc->ws_level[q->cols.kind] = 2;
        finish_notebook(2);
    }
    CHECK(pc->ws_level[WS_ADD] == 1 || pc->ws_level[WS_SUB] == 1);
}

static void test_rung_untouched(void)
{
    /* walked through every step (level 0): no evidence for the rung, either way */
    con("new Run 2");
    mq_profile_t *p = mq_prof(q);
    p->sk[S_ADDSUB].diff = 3;
    for (int t = 0; t < 8; t++) {
        q->rng = (uint32_t)t * 19 + 1;
        CHECK(open_in(S_ADDSUB, 3, ME_VAULT, 0));
        CHECK(mq_cols_on(q) && q->ws_guided);
        finish_notebook(0);
    }
    CHECK(p->sk[S_ADDSUB].diff == 3);
}

static void test_button(void)
{
    /* on her own the notebook waits for its button (the dial works too); "dial" puts it away */
    q->rng = 11;
    CHECK(open_in(S_ADDSUB, 5, ME_VAULT, 2));
    CHECK(!mq_cols_on(q));
    mq_game_widgets(q);
    int bi = -1;
    for (int i = 0; i < q->wg.n; i++)
        if (!strcmp(q->wg.w[i].label, "notebook"))
            bi = i;
    CHECK(bi >= 0);
    const mq_wg_t *g = mq_wg(q, bi);
    ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(4, g->x + g->w / 2, g->y + g->h / 2));
    CHECK(mq_cols_on(q) && q->cols.level == 2);
    int r[4];
    ops.draw(q, 0, r);
    for (int i = 0; i < q->wg.n; i++)
        if (!strcmp(q->wg.w[i].label, "dial"))
            ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(4, q->wg.w[i].x + 20, q->wg.w[i].y + 20));
    CHECK(!mq_cols_on(q) && q->sub == GS_PLAY);
    /* the console's `mq correct` fills it all (scripted tests) */
    q->rng = 12;
    CHECK(open_in(S_MULTI, 2, ME_VAULT, 0) && mq_cols_on(q));
    con("correct");
    CHECK(q->sub == GS_CHEER);
}

/* CR-04 (the code review): the first slip on her own counts the order as missed, but no made-up whole answer
 * (the right one + 1) goes to the error patterns or to the grown-ups' recent orders */
static void test_slip_no_fake(void)
{
    con("new Sli 2");
    q->rng = 77;
    CHECK(open_in(S_ADDSUB, 5, ME_VAULT, 2));
    mq_cols_open(q);
    mq_pace(q)->ws_level[q->cols.kind] = 2;
    q->cols.level = 2;
    int n0 = q->recent_n, i0 = q->recent_i;
    const mq_cstep_t *st = &q->cols.st[q->cols.cur];
    mq_cols_digit(q, (st->want + 1) % 10);
    CHECK(q->recent_n == n0 + 1 || n0 == MQ_NRECENT);
    CHECK(!strcmp(q->recent_o[i0 % MQ_NRECENT].her, "a slip") && !q->recent_o[i0 % MQ_NRECENT].right);
    CHECK(q->diag_hit == 0 && !q->judge_slip);
}

int main(void)
{
    harness_init();
    con("new Nia 2");
    q->fun_off = true;
    test_fill();
    test_guided_words();
    test_why();
    test_fading();
    test_slip_no_fake();
    test_rung_untouched();
    test_button();
    printf("test_mq_cols: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
