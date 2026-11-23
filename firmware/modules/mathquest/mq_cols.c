/*
 * Math Quest: the notebook -- column work "like on paper", in the friend's sparkle notebook (the audit's L4
 * and L5; docs/MATHQUEST.md "The notebook").
 *
 * When an order is a multi-digit written sum, take-away, product or sharing (mq_prob_t.ws, set by the
 * generators) and it is played in a calm game -- Treasure Heist's lock (the treasure scroll), the till's
 * change (the receipt pad), the kitchen (the recipe card) -- the friend opens a notebook with the numbers
 * in the standard written layout, and she fills it box by box, ones first:
 *
 *   adding        the column's digit, the carry in its little box above the next column;
 *   taking away   a trade first when the top digit is too small: the next place's digit crossed out and
 *                 one less written above it, across zeros too (a zero that got ten and gave one: 9);
 *   multiplying   by one digit with its carries; by two digits: two rows (the second with its 0 holding
 *                 the ones place), then the two rows added;
 *   sharing       long division: how many fit (the digit above), that many times written under, what is
 *                 left, the next digit brought down; a leftover written after R.
 *
 * The friend guides each step in a line said aloud on the Mac ("6 + 7 is 13: write the 3!", "Carry the 1
 * next door!"), and the help fades per kind of work (mq_pace_t.ws_level): every step told (0), a prompt
 * for the column only (1: "The tens: 4 + 5 + 1?"), on her own (2: nothing said until she asks with the
 * ? key or slips). Two clean notebooks step the help down (three at the prompts); slipping twice on her
 * own brings the prompts back. A wrong digit gets a friendly, column-specific why straight away (the
 * carried 1 forgotten, the small digit taken from the big one, a zero that lends, a quotient too big ...)
 * and she tries that box again; the order still counts as missed first time for the coach (an order she
 * was walked through all the way is no evidence for her rung either: mq_coach_answer). The finished
 * answer goes into the game: the lock opens, the till rings, the cake is mixed.
 *
 * It is a notebook in the friend's world, never a worksheet: no school words, a cute page, the friend's
 * face and voice.
 */
#include "mq.h"

enum { HK_UP = 12, HK_LEFT = 13, HK_RIGHT = 14, HK_DOWN = 15, HK_DEL = 8, HK_ENTER = 49 };
enum { TV_DOWN = 1, TV_DRAG, TV_UP, TV_TAP };
#define FY (MQ_Y0 + 58)
#define FB (MQ_Y0 + MQ_H)
/* the page: cells of CW x CH, the keypad on the right */
#define PX 6
#define PY (MQ_Y0 + 44)
#define CW 15
#define CH 15
#define NH 10 /* a little row (carries, trades) */
#define KX 184
#define KY (MQ_Y0 + 44)

/* steps */
enum { CS_ANS, CS_CARRY, CS_TRADE, CS_ZERO, CS_QUOT, CS_PROD, CS_DIFF, CS_DOWN, CS_REM };
/* rows' kinds */
enum { RK_DIGITS, RK_NOTE, RK_QUOT };
#define RF_LINE 0x10 /* a line under the row */

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int dig(int32_t n, int i)
{
    while (i-- > 0)
        n /= 10;
    return (int)(n % 10);
}
static int ndig(int32_t n)
{
    int k = 1;
    for (; n >= 10; n /= 10)
        k++;
    return k;
}
static const char place_name[6][20] = {"ones", "tens", "hundreds", "thousands", "ten thousands", "hundred thousands"};

bool mq_cols_on(const mq_t *q) { return q->cols.on != 0; }

/* ------------------------------------------------------------------ the plan */
static void step(mq_cols_t *c, int kind, int row, int col, int want, int x, int y, int z)
{
    if (c->n >= CW_STEPS || col < 0 || col >= CW_COLS || row < 0 || row >= CW_ROWS)
        return;
    mq_cstep_t *s = &c->st[c->n++];
    s->kind = (uint8_t)kind, s->row = (uint8_t)row, s->col = (uint8_t)col, s->want = (uint8_t)want;
    s->x = (uint8_t)x, s->y = (uint8_t)y, s->z = (uint8_t)z;
}
/* write number v right-aligned with its last digit in column `last` of row r */
static void put(mq_cols_t *c, int r, int last, int32_t v, int width)
{
    for (int i = 0; i < width && last - i >= 0; i++)
        c->cell[r][last - i] = (char)('0' + dig(v, i));
}

/* adding x + y (or the rows of a product) into row rs, carries in the note row rn */
static void plan_add(mq_cols_t *c, int ra, int rb, int rs, int rn, int32_t x, int32_t y, int last)
{
    int n = imax(ndig(x), ndig(y)), cin = 0;
    for (int i = 0; i < n; i++) {
        int dx = dig(x, i), dy = i < ndig(y) ? dig(y, i) : 0, s = dx + dy + cin;
        step(c, CS_ANS, rs, last - i, s % 10, dx, dy, cin);
        if (s >= 10) {
            if (i + 1 < n)
                step(c, CS_CARRY, rn, last - i - 1, 1, dx, dy, cin);
            else
                step(c, CS_ANS, rs, last - i - 1, 1, 0, 0, 1); /* the last carry: the answer's first digit */
        }
        cin = s >= 10;
    }
    (void)ra, (void)rb;
}

static void plan(mq_t *q, mq_cols_t *c)
{
    int32_t a = c->a, b = c->b;
    for (int r = 0; r < CW_ROWS; r++) {
        c->rk[r] = RK_DIGITS, c->op[r] = 0, c->cross[r] = 0, c->ten[r] = 0;
        for (int k = 0; k < CW_COLS; k++)
            c->cell[r][k] = 0;
    }
    c->n = 0;
    switch (c->kind) {
    case WS_ADD: {
        int n = imax(ndig(a), ndig(b)) + (ndig(a + b) > imax(ndig(a), ndig(b))), last = n;
        c->rows = 4, c->cols = n + 1;
        c->rk[0] = RK_NOTE, c->rk[2] = RK_DIGITS | RF_LINE, c->op[2] = '+';
        put(c, 1, last, a, ndig(a)), put(c, 2, last, b, ndig(b));
        plan_add(c, 1, 2, 3, 0, a, b, last);
        break;
    }
    case WS_SUB: {
        int n = ndig(a), last = n;
        c->rows = 4, c->cols = n + 1;
        c->rk[0] = RK_NOTE, c->rk[2] = RK_DIGITS | RF_LINE, c->op[2] = '-';
        put(c, 1, last, a, n), put(c, 2, last, b, ndig(b));
        int t[8];
        for (int i = 0; i < n && i < 8; i++)
            t[i] = dig(a, i);
        int len = ndig(a - b);
        for (int i = 0; i < n && i < 8; i++) {
            int u = i < ndig(b) ? dig(b, i) : 0;
            if (t[i] < u) { /* a trade: the next place with something to give */
                int j = i + 1;
                while (j < n && t[j] == 0)
                    j++;
                t[j]--;
                step(c, CS_TRADE, 0, last - j, t[j], t[j] + 1, i, j - i - 1); /* x: what it was; z: zeros crossed */
                for (int k = j - 1; k > i; k--) {
                    t[k] = 9;
                    step(c, CS_ZERO, 0, last - k, 9, 0, i, 0);
                }
                t[i] += 10;
                c->ten[1] |= (uint8_t)(1 << (last - i)); /* (its small 1: ten more) */
            }
            if (i < len || u || i == 0) /* (no leading zeros in the answer) */
                step(c, CS_ANS, 3, last - i, t[i] - u, t[i], u, t[i] >= 10);
        }
        break;
    }
    case WS_MUL:
        if (b < 10) {
            int32_t p = a * b;
            int n = ndig(p), last = n;
            c->rows = 4, c->cols = n + 1;
            c->rk[0] = RK_NOTE, c->rk[2] = RK_DIGITS | RF_LINE, c->op[2] = 'x';
            put(c, 1, last, a, ndig(a)), put(c, 2, last, b, 1);
            int cin = 0;
            for (int i = 0; i < ndig(a); i++) {
                int da = dig(a, i), pr = da * (int)b + cin;
                step(c, CS_ANS, 3, last - i, pr % 10, da, (int)b, cin);
                if (pr >= 10) {
                    if (i + 1 < ndig(a))
                        step(c, CS_CARRY, 0, last - i - 1, pr / 10, da, (int)b, cin);
                    else
                        step(c, CS_ANS, 3, last - i - 1, pr / 10, 0, 0, pr / 10);
                }
                cin = pr / 10;
            }
        } else { /* two rows, then added */
            int32_t p1 = a * (b % 10), p2 = a * (b / 10) * 10, s = a * b;
            int n = ndig(s), last = n;
            c->rows = 8, c->cols = n + 1;
            c->rk[0] = RK_NOTE, c->rk[1] = RK_NOTE, c->rk[3] = RK_DIGITS | RF_LINE, c->op[3] = 'x';
            c->rk[4] = RK_NOTE, c->rk[6] = RK_DIGITS | RF_LINE, c->op[6] = '+';
            put(c, 2, last, a, ndig(a)), put(c, 3, last, b, 2);
            int cin = 0, bo = (int)(b % 10), bt = (int)(b / 10);
            for (int i = 0; i < ndig(a); i++) { /* a x the ones: row 5, carries in row 1 */
                int da = dig(a, i), pr = da * bo + cin;
                step(c, CS_ANS, 5, last - i, pr % 10, da, bo, cin);
                if (pr >= 10) {
                    if (i + 1 < ndig(a))
                        step(c, CS_CARRY, 1, last - i - 1, pr / 10, da, bo, cin);
                    else
                        step(c, CS_ANS, 5, last - i - 1, pr / 10, 0, 0, pr / 10);
                }
                cin = pr / 10;
            }
            step(c, CS_ZERO, 6, last, 0, 0, 0, 1); /* the 0 that holds the ones place */
            cin = 0;
            for (int i = 0; i < ndig(a); i++) { /* a x the tens: row 6 one place left, carries in row 0 */
                int da = dig(a, i), pr = da * bt + cin;
                step(c, CS_ANS, 6, last - i - 1, pr % 10, da, bt, cin);
                if (pr >= 10) {
                    if (i + 1 < ndig(a))
                        step(c, CS_CARRY, 0, last - i - 2, pr / 10, da, bt, cin);
                    else
                        step(c, CS_ANS, 6, last - i - 2, pr / 10, 0, 0, pr / 10);
                }
                cin = pr / 10;
            }
            plan_add(c, 5, 6, 7, 4, p1, p2, last); /* the two rows together: carries in row 4 */
        }
        break;
    case WS_DIV: {
        int m = ndig(a), dl = ndig(b), x0 = dl + 1; /* the divisor, its bracket, then the digits */
        c->cols = imin(CW_COLS, x0 + m + 3);
        c->rk[0] = RK_QUOT;
        put(c, 1, dl - 1, b, dl);
        c->op[1] = ')';
        put(c, 1, x0 + m - 1, a, m);
        int32_t rem = 0;
        int row = 1, first = 1, lastq = x0;
        for (int k = 0; k < m; k++) {
            int32_t part = rem * 10 + dig(a, m - 1 - k);
            int col = x0 + k;
            if (first && part < b && k < m - 1) {
                rem = part;
                continue;
            }
            int qd = (int)(part / b);
            step(c, CS_QUOT, 0, col, qd, (int)(part > 99 ? 99 : part), (int)b, 0);
            lastq = col;
            first = 0;
            int32_t pr = qd * b, df = part - pr;
            if (qd && row + 2 < CW_ROWS) {
                int pw = ndig(pr), w = ndig(df);
                c->rk[row + 1] = RK_DIGITS | RF_LINE, c->op[row + 1] = '-';
                for (int i = 0; i < pw; i++)
                    step(c, CS_PROD, row + 1, col - i, dig(pr, i), qd, (int)b, i);
                for (int i = 0; i < w; i++)
                    step(c, CS_DIFF, row + 2, col - i, dig(df, i), (int)(part > 99 ? 99 : part), (int)(pr > 99 ? 99 : pr), i);
                row += 2;
            }
            if (k + 1 < m) /* the next digit comes down */
                step(c, CS_DOWN, row, col + 1, dig(a, m - 2 - k), 0, 0, 0);
            rem = df;
        }
        c->rows = imin(CW_ROWS, row + 1);
        if (c->has_rem && rem > 0) { /* the leftover after R */
            int rc = lastq + 2;
            c->cell[0][lastq + 1] = 'R';
            for (int i = ndig(rem) - 1; i >= 0; i--, rc++)
                if (rc < CW_COLS)
                    step(c, CS_REM, 0, rc, dig(rem, i), 0, 0, 0);
        }
        break;
    }
    default: break;
    }
    (void)q;
}

/* ------------------------------------------------------------------ what the friend says */
static void say(mq_t *q, const char *s)
{
    mq_cols_t *c = &q->cols;
    q->api->snprintf(c->say, sizeof c->say, "%s", s);
    c->say_t = mq_mark(q);
    mq_speak(q, q->prob.ca % MQ_NCAST, s, MODX_SPEAK_NOW);
}

/* the column's place (ones, tens ...) of a step: counted from the right edge of its number */
static const char *place_of(const mq_cols_t *c, const mq_cstep_t *s)
{
    int last = c->kind == WS_DIV ? c->cols - 1 : c->cols - 1;
    int i = last - s->col;
    if (c->kind == WS_ADD || c->kind == WS_SUB || c->kind == WS_MUL)
        i = (c->cols - 1) - s->col;
    return place_name[i < 0 ? 0 : i > 5 ? 5 : i];
}

/* the whole step, said: every step told (level 0), and when she asks (?) or after a slip */
static void step_line(mq_t *q, const mq_cstep_t *s, char *o, int n)
{
    const modx_api_t *api = q->api;
    const mq_cols_t *c = &q->cols;
    switch (s->kind) {
    case CS_ANS:
        if (c->kind == WS_SUB) {
            api->snprintf(o, (size_t)n, "%d take away %d is %d: write the %d!", s->x, s->y, s->want, s->want);
        } else if (c->kind == WS_MUL && s->y) {
            int pr = s->x * s->y;
            if (s->z)
                api->snprintf(o, (size_t)n, "%d x %d is %d, and %d more is %d: write the %d!", s->x, s->y, pr, s->z, pr + s->z,
                              s->want);
            else
                api->snprintf(o, (size_t)n, "%d x %d is %d: write the %d!", s->x, s->y, pr, s->want);
        } else if (!s->x && !s->y && s->z) {
            api->snprintf(o, (size_t)n, "The %d we carried goes down in front: write %d!", s->want, s->want);
        } else if (s->z)
            api->snprintf(o, (size_t)n, "%d + %d + the carried %d is %d: write the %d!", s->x, s->y, s->z, s->x + s->y + s->z, s->want);
        else
            api->snprintf(o, (size_t)n, "%d + %d is %d: write the %d!", s->x, s->y, s->x + s->y, s->want);
        break;
    case CS_CARRY: api->snprintf(o, (size_t)n, "Carry the %d next door, into its little box!", s->want); break;
    case CS_TRADE:
        if (s->z)
            api->snprintf(o, (size_t)n, "Not enough, and next door has 0: go on to the %d! It trades one: %d becomes %d.", s->x, s->x,
                          s->want);
        else
            api->snprintf(o, (size_t)n, "Not enough to take away: trade one from next door! %d becomes %d.", s->x, s->want);
        break;
    case CS_ZERO:
        if (s->z)
            api->snprintf(o, (size_t)n, "A 0 holds the ones place in this row: write 0!");
        else
            api->snprintf(o, (size_t)n, "This 0 gets ten and gives one away: 9 is left!");
        break;
    case CS_QUOT:
        api->snprintf(o, (size_t)n, "How many %ds fit in %d? %d! (%d x %d = %d)", s->y, s->x, s->want, s->want, s->y, s->want * s->y);
        break;
    case CS_PROD: api->snprintf(o, (size_t)n, "%d x %d is %d: write it under, ones first!", s->x, s->y, s->x * s->y); break;
    case CS_DIFF: api->snprintf(o, (size_t)n, "%d take away %d: what is left? Write the %d!", s->x, s->y, s->want); break;
    case CS_DOWN: api->snprintf(o, (size_t)n, "Bring the %d down beside it!", s->want); break;
    case CS_REM: api->snprintf(o, (size_t)n, "That is left over: write %d after the R!", s->want); break;
    default: o[0] = 0; break;
    }
}
/* the prompt for a column (level 1) */
static void prompt_line(mq_t *q, const mq_cstep_t *s, char *o, int n)
{
    const modx_api_t *api = q->api;
    const mq_cols_t *c = &q->cols;
    const char *pl = place_of(c, s);
    switch (s->kind) {
    case CS_ANS:
        if (c->kind == WS_SUB)
            api->snprintf(o, (size_t)n, "The %s: %d take away %d?", pl, s->x, s->y);
        else if (c->kind == WS_MUL && s->y)
            api->snprintf(o, (size_t)n, "The %s: %d x %d%s?", pl, s->x, s->y, s->z ? ", and the carried" : "");
        else if (!s->x && !s->y)
            api->snprintf(o, (size_t)n, "Anything carried still to write?");
        else
            api->snprintf(o, (size_t)n, "The %s: %d + %d%s?", pl, s->x, s->y, s->z ? " + the carried 1" : "");
        break;
    case CS_CARRY: api->snprintf(o, (size_t)n, "Anything to carry?"); break;
    case CS_TRADE: case CS_ZERO:
        api->snprintf(o, (size_t)n, s->kind == CS_ZERO && s->z ? "What holds the ones place here?" : "Not enough: what does next door have now?");
        break;
    case CS_QUOT: api->snprintf(o, (size_t)n, "How many %ds fit in %d?", s->y, s->x); break;
    case CS_PROD: api->snprintf(o, (size_t)n, "That many times %d?", s->y); break;
    case CS_DIFF: api->snprintf(o, (size_t)n, "What is left?"); break;
    case CS_DOWN: api->snprintf(o, (size_t)n, "Bring the next one down!"); break;
    case CS_REM: api->snprintf(o, (size_t)n, "And left over?"); break;
    default: o[0] = 0; break;
    }
}
/* why digit d is not it: the column-specific mix-up (forgot the carry, took the small from the big, ...) */
int mq_cols_why(mq_t *q, const mq_cstep_t *s, int d, char *o, int n)
{
    const modx_api_t *api = q->api;
    const mq_cols_t *c = &q->cols;
    int mc = MC_CARELESS;
    switch (s->kind) {
    case CS_ANS:
        if (c->kind == WS_ADD || (c->kind == WS_MUL && !s->y)) {
            int raw = s->x + s->y;
            if (s->z && d == raw % 10)
                mc = MC_NO_REGROUP,
                api->snprintf(o, (size_t)n, "Psst, the carried %d joins in: %d + %d + %d is %d!", s->z, s->x, s->y, s->z, raw + s->z);
            else if (raw + s->z >= 10 && d == (raw + s->z) / 10)
                mc = MC_NO_REGROUP,
                api->snprintf(o, (size_t)n, "%d is two digits: the %d stays here, the %d hops next door!", raw + s->z, s->want,
                              (raw + s->z) / 10);
            else
                api->snprintf(o, (size_t)n, "Let's count again: %d + %d%s is %d.", s->x, s->y, s->z ? " + 1" : "", raw + s->z);
        } else if (c->kind == WS_SUB) {
            int top = s->z ? s->x - 10 : s->x;
            if (s->z && d == (s->y > top ? s->y - top : top - s->y))
                mc = MC_SUB_FLIP,
                api->snprintf(o, (size_t)n, "We can't take %d from %d, so we traded: %d take away %d is %d.", s->y, top, s->x, s->y,
                              s->want);
            else
                api->snprintf(o, (size_t)n, "Count back from %d by %d: %d.", s->x, s->y, s->want);
        } else {
            int pr = s->x * s->y;
            if (s->z && d == pr % 10)
                mc = MC_NO_REGROUP, api->snprintf(o, (size_t)n, "Don't forget the %d we carried: %d + %d is %d!", s->z, pr, s->z, pr + s->z);
            else
                api->snprintf(o, (size_t)n, "%d x %d is %d%s.", s->x, s->y, pr, s->z ? ", plus the carried" : "");
        }
        break;
    case CS_CARRY:
        mc = MC_NO_REGROUP;
        if (!d)
            api->snprintf(o, (size_t)n, "That was more than 9: one goes next door, so carry %d!", s->want);
        else
            api->snprintf(o, (size_t)n, "Only the tens go next door: carry %d.", s->want);
        break;
    case CS_TRADE:
        mc = MC_SUB_FLIP;
        if (d == s->x)
            api->snprintf(o, (size_t)n, "It gave one away, so it has one less now: %d.", s->want);
        else
            api->snprintf(o, (size_t)n, "One less than %d is %d.", s->x, s->want);
        break;
    case CS_ZERO:
        if (s->z)
            mc = MC_PARTIAL_PRODUCT, api->snprintf(o, (size_t)n, "This row is tens: a 0 keeps the ones place empty.");
        else
            mc = MC_NO_BORROW_ZERO, api->snprintf(o, (size_t)n, "This 0 had nothing: it got ten from next door and gave one on. 9!");
        break;
    case CS_QUOT:
        mc = MC_CLOSE;
        if (d * s->y > s->x)
            api->snprintf(o, (size_t)n, "%d x %d is %d: more than %d! Try a smaller one.", d, s->y, d * s->y, s->x);
        else
            api->snprintf(o, (size_t)n, "There's room for more: %d more fit after %d x %d.", (s->x - d * s->y) / s->y, d, s->y);
        break;
    case CS_PROD: api->snprintf(o, (size_t)n, "%d x %d is %d.", s->x, s->y, s->x * s->y); break;
    case CS_DIFF: api->snprintf(o, (size_t)n, "%d take away %d leaves %d.", s->x, s->y, s->x - s->y); break;
    case CS_REM: mc = MC_REM_IGNORED, api->snprintf(o, (size_t)n, "What is left at the very bottom is the leftover."); break;
    default: o[0] = 0; break;
    }
    return mc;
}

/* the step now: its line said (every step told) or its prompt (hints); a bring-down fills itself */
static void enter_step(mq_t *q)
{
    mq_cols_t *c = &q->cols;
    char l[96];
    while (c->cur < c->n && c->st[c->cur].kind == CS_DOWN) { /* (brought down: it fills itself) */
        const mq_cstep_t *s = &c->st[c->cur];
        c->cell[s->row][s->col] = (char)('0' + s->want);
        if (c->level == 0) {
            step_line(q, s, l, sizeof l);
            say(q, l);
        }
        c->cur++;
    }
    if (c->cur >= c->n)
        return;
    const mq_cstep_t *s = &c->st[c->cur];
    if (c->level == 0)
        step_line(q, s, l, sizeof l), say(q, l);
    else if (c->level == 1)
        prompt_line(q, s, l, sizeof l), say(q, l);
    c->step_t = mq_mark(q);
}

/* ------------------------------------------------------------------ opening, typing, the end */
static const char note_title[WS_COUNT][24] = {"", "treasure scroll", "receipt pad", "recipe card", "notebook"};

bool mq_cols_wanted(mq_t *q)
{
    const mq_prob_t *p = &q->prob;
    int m = q->gm.mech;
    return p->ws != WS_NONE && (m == ME_VAULT || m == ME_TILL || m == ME_PETS) && !q->preview && /* (the calm ones: not the kitchen's beat) */
           !q->placing_item && !q->gm.boss && (p->kind == K_NUM || p->kind == K_REM || p->kind == K_MONEY) && p->op[0] > 0 &&
           p->op[1] > 0;
}
/* the order's notebook: opened by itself while she is learning the kind (every step told, or prompts), on
 * her own it waits for the notebook button */
void mq_cols_open(mq_t *q)
{
    const mq_prob_t *p = &q->prob;
    mq_cols_t *c = &q->cols;
    uint8_t *b = (uint8_t *)c;
    for (size_t i = 0; i < sizeof *c; i++)
        b[i] = 0;
    c->kind = p->ws, c->a = p->op[0], c->b = p->op[1], c->dp = p->ws_dp, c->has_rem = p->kind == K_REM;
    c->level = mq_pace(q)->ws_level[c->kind % WS_COUNT];
    if (c->level > 2)
        c->level = 2;
    c->title = (uint8_t)(q->gm.mech == ME_VAULT ? 1 : q->gm.mech == ME_COOK ? 3 : q->gm.mech == ME_TILL || q->gm.mech == ME_PETS ? 2 : 4);
    plan(q, c);
    c->on = 1, c->open_t = mq_mark(q), c->bad = -1;
    q->ws_guided = c->level == 0;
    if (c->level == 0)
        q->used_help = true; /* (walked through every step: coins as with help) */
    char l[96];
    q->api->snprintf(l, sizeof l, c->level == 0 ? "Let's work it out on my %s, step by step!" : c->level == 1
                                                                                                ? "My %s! Ones first, you've got this."
                                                                                                : "My %s is yours: ones first!",
                     note_title[c->title]);
    say(q, l);
    enter_step(q);
}
bool mq_cols_auto(mq_t *q) { return mq_cols_wanted(q) && mq_pace(q)->ws_level[q->prob.ws % WS_COUNT] < 2; }

static void finish(mq_t *q)
{
    mq_cols_t *c = &q->cols;
    mq_pace_t *pc = mq_pace(q);
    int k = c->kind % WS_COUNT;
    /* the help fades: clean ones step it down, slips on her own bring the prompts back */
    if (!c->wrongs) {
        if (++pc->ws_clean[k] >= (c->level == 0 ? 2 : 3) && pc->ws_level[k] < 2)
            pc->ws_level[k]++, pc->ws_clean[k] = 0;
        pc->ws_slip[k] = 0;
    } else {
        pc->ws_clean[k] = 0;
        if (c->level == 2 && c->wrongs >= 2 && ++pc->ws_slip[k] >= 2)
            pc->ws_level[k] = 1, pc->ws_slip[k] = 0;
    }
    q->dirty = true;
    c->on = 0;
    say(q, c->wrongs ? "You fixed it all! Here it goes!" : "All by the columns! Here it goes!");
    mq_game_answer(q, q->prob.ans); /* into the game: the lock opens, the till rings, the cake is mixed */
    q->ws_guided = false;
}

/* a digit for the box she is on */
bool mq_cols_digit(mq_t *q, int d)
{
    mq_cols_t *c = &q->cols;
    if (!c->on || c->cur >= c->n || d < 0 || d > 9)
        return false;
    const mq_cstep_t *s = &c->st[c->cur];
    if (d == s->want) {
        c->cell[s->row][s->col] = (char)('0' + d);
        if (s->kind == CS_TRADE || s->kind == CS_ZERO) { /* the old digit crossed out below its new one */
            if (!(s->kind == CS_ZERO && s->z))
                c->cross[s->row + 1] |= (uint8_t)(1 << s->col);
        }
        c->bad = -1;
        mq_sfx(q, MODX_SFX_TAP, d);
        c->cur++;
        enter_step(q);
        if (c->cur >= c->n)
            finish(q);
        return true;
    }
    /* a slip: the why for this column, said; the box tried again */
    char l[96];
    int mc = mq_cols_why(q, s, d, l, sizeof l);
    c->bad = (int8_t)c->cur, c->bad_d = (uint8_t)d, c->bad_t = mq_mark(q), c->last_mc = (uint8_t)mc;
    c->wrongs++;
    say(q, l);
    mq_sfx(q, MODX_SFX_DUD, 0);
    if (c->wrongs == 1 && !q->tried_wrong && c->level > 0) { /* the order counts as missed first time (the coach) */
        mq_judge_slip(q); /* (a miss for the coach; no made-up whole answer for the error patterns: CR-04) */
        q->retrying = false, q->sub = GS_PLAY;
        q->last_mc = (uint8_t)mc;
    }
    if (mc < 64 && mq_pace(q)->mc_seen[mc] < 255)
        mq_pace(q)->mc_seen[mc]++;
    return true;
}

/* the ? key: the whole step said (it counts as help) */
void mq_cols_hint(mq_t *q)
{
    mq_cols_t *c = &q->cols;
    if (!c->on || c->cur >= c->n)
        return;
    char l[96];
    step_line(q, &c->st[c->cur], l, sizeof l);
    say(q, l);
    q->used_help = true;
}

void mq_cols_close(mq_t *q)
{
    if (q->cols.on)
        mq_wg_reset(q);
    q->cols.on = 0;
    q->ws_guided = false;
}

/* ------------------------------------------------------------------ input */
enum { CW_KEY = 200, CW_HINT, CW_BACK };
static void layout(mq_t *q)
{
    mq_cols_t *c = &q->cols;
    mq_wg_begin(q);
    static const char keys[12] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '?', '0', '<'};
    for (int k = 0; k < 12; k++) {
        if (keys[k] == '<' && c->level < 2)
            continue;
        char s[2] = {keys[k], 0};
        int i = mq_wg_add(q, WG_KEY, keys[k] == '?' ? CW_HINT : keys[k] == '<' ? CW_BACK : CW_KEY, (uint8_t)keys[k], KX + (k % 3) * 44,
                          KY + (k / 3) * 44, 42, 40);
        mq_wg_label(q, i, keys[k] == '<' ? "dial" : s, keys[k] == '?' ? Q_YELLOW : keys[k] == '<' ? Q_PEACH : Q_WHITE);
    }
}
static void on_ev(mq_t *q, const mq_wev_t *ev)
{
    if (ev->why != WHY_TAP && ev->why != WHY_KEY)
        return;
    if (ev->id == CW_KEY)
        mq_cols_digit(q, ev->arg - '0');
    else if (ev->id == CW_HINT)
        mq_cols_hint(q);
    else if (ev->id == CW_BACK)
        mq_cols_close(q);
}
bool mq_cols_touch(mq_t *q, int type, int x, int y)
{
    layout(q);
    mq_wev_t ev;
    if (mq_wg_touch(q, type, x, y, &ev) && ev.why != WHY_NONE)
        on_ev(q, &ev);
    q->redraw = true;
    return true;
}
bool mq_cols_key(mq_t *q, int k, int ch)
{
    if (ch >= '0' && ch <= '9')
        return mq_cols_digit(q, ch - '0');
    if (k == HK_ENTER || ch == '?') { /* ENTER asks for the step (the same as ?) */
        mq_cols_hint(q);
        return true;
    }
    if (k == HK_DEL && q->cols.level >= 2) {
        mq_cols_close(q);
        return true;
    }
    return true;
}
bool mq_cols_scroll(mq_t *q, int dy, int x, int y)
{
    (void)q, (void)dy, (void)x, (void)y;
    return true;
}

/* ------------------------------------------------------------------ drawing */
static int row_y(const mq_cols_t *c, int r)
{
    int y = PY + 2;
    for (int k = 0; k < r; k++)
        y += (c->rk[k] & 0x0F) == RK_NOTE ? NH : CH + ((c->rk[k] & RF_LINE) ? 3 : 0);
    return y;
}
static int col_x(const mq_cols_t *c, int k) { return PX + 14 + k * CW + (CW_COLS - c->cols) * CW / 2; }

void mq_cols_draw(mq_t *q)
{
    mq_cols_t *c = &q->cols;
    const modx_api_t *api = q->api;
    const mq_prob_t *p = &q->prob;
    /* the friend and what they say (a speech strip under the top line) */
    mq_fill(q, 0, MQ_Y0 + 16, MQ_W, MQ_H - 16, RGB(255, 236, 244));
    bool talk = c->say_t && q->anim_t - c->say_t < 1600;
    mq_cast(q, p->ca % MQ_NCAST, 2, MQ_Y0 + 17, 26, talk ? AN_TALK : AN_HAPPY);
    mq_rrect(q, 30, MQ_Y0 + 18, 286, 24, 8, Q_WHITE);
    char l2[3][MQ_TXT];
    int nl = mq_story_wrap(q, c->say, l2, 278);
    for (int i = 0; i < 2 && i < nl; i++)
        mq_text(q, api->font_small, 34, MQ_Y0 + 18 + (nl > 1 ? i * 11 : 5), l2[i], c->bad >= 0 ? Q_HOTPINK : Q_INK);
    /* the page: cream, ruled, a margin, its title */
    int ph = FB - PY - 2;
    mq_rrect(q, PX - 2, PY - 2, 176, ph + 2, 6, RGB(255, 252, 240));
    for (int y = PY + 12; y < PY + ph; y += 15)
        mq_fill(q, PX + 2, y, 168, 1, RGB(214, 230, 250));
    mq_fill(q, PX + 10, PY, 1, ph - 2, RGB(255, 190, 200));
    for (int k = 0; k < 4; k++) /* a doodle: little stars in the corner */
        mq_star(q, PX + 160 - k * 7, PY + ph - 8 - (k % 2) * 4, 2, k % 2 ? Q_PINK : Q_YELLOW);
    /* the cells */
    for (int r = 0; r < c->rows; r++) {
        int y = row_y(c, r), kind = c->rk[r] & 0x0F;
        const font_t *f = kind == RK_NOTE ? api->font_small : api->font_medium;
        if (c->op[r] && c->op[r] != ')') {
            char o[2] = {c->op[r] == '-' ? '-' : c->op[r], 0};
            mq_text_c(q, api->font_medium, col_x(c, 0) - 8, y, c->op[r] == 'x' ? "x" : o, Q_PURPLE);
        }
        for (int k = 0; k < c->cols && k < CW_COLS; k++) {
            int x = col_x(c, k);
            char ch = c->cell[r][k];
            if (c->kind == WS_DIV && r == 1 && c->op[1] == ')' && k == ndig(c->b)) { /* the bracket */
                mq_fill(q, x + 4, y - 1, 2, CH + 2, Q_INK);
                mq_fill(q, x + 4, y - 2, (c->cols - k) * CW, 2, Q_INK);
                continue;
            }
            if (!ch)
                continue;
            char s[2] = {ch, 0};
            uint16_t col = kind == RK_NOTE ? Q_HOTPINK : kind == RK_QUOT ? Q_PURPLE : Q_INK;
            mq_text_c(q, f, x + CW / 2, y + (kind == RK_NOTE ? -1 : 0), s, col);
            if ((c->cross[r] >> k) & 1) /* traded: crossed out */
                mq_line(q, x + 2, y + CH - 3, x + CW - 2, y + 2, Q_HOTPINK);
            if ((c->ten[r] >> k) & 1) /* its small 1: ten more */
                mq_text(q, api->font_small, x - 3, y - 4, "1", Q_HOTPINK);
        }
        if (c->rk[r] & RF_LINE)
            mq_fill(q, col_x(c, 0) - 14, y + CH, (c->cols + 1) * CW, 2, Q_INK);
        if (c->dp && kind != RK_NOTE && (r == 1 || r == 2 || r == 3)) /* money: the point */
            mq_circle(q, col_x(c, c->cols - c->dp), y + CH - 3, 1, Q_INK);
    }
    /* the box she fills now: pulsing; a slip shown in pink for a moment */
    if (c->cur < c->n) {
        const mq_cstep_t *s = &c->st[c->cur];
        int x = col_x(c, s->col), y = row_y(c, s->row), h = (c->rk[s->row] & 0x0F) == RK_NOTE ? NH : CH;
        int pulse = (int)(q->anim_t / 250 % 2);
        mq_rect(q, x, y - 1, CW, h + 1, pulse ? Q_HOTPINK : Q_PURPLE);
        if (c->bad == (int8_t)c->cur && q->anim_t - c->bad_t < 1600) {
            char s2[2] = {(char)('0' + c->bad_d), 0};
            mq_text_c(q, h == NH ? api->font_small : api->font_medium, x + CW / 2, y, s2, Q_HOTPINK);
            mq_line(q, x + 2, y + h - 2, x + CW - 2, y + 1, Q_HOTPINK);
        }
        if (c->level == 0) { /* every step told: the friend's paw points at it */
            int16_t px[3] = {(int16_t)(x + CW + 8), (int16_t)(x + CW + 2), (int16_t)(x + CW + 8)};
            int16_t py[3] = {(int16_t)(y + 1), (int16_t)(y + h / 2), (int16_t)(y + h - 1)};
            mq_poly(q, px, py, 3, Q_HOTPINK);
        }
    }
    /* its title */
    char t[32];
    api->snprintf(t, sizeof t, "%s's %s", mq_cast_name(q, p->ca % MQ_NCAST), note_title[c->title]);
    mq_text(q, api->font_small, PX + 14, FB - 14, t, Q_DIM);
    /* the keys (widgets) */
    layout(q);
    mq_wg_draw(q);
}

/* `mq cols`: the plan (tests, the console) */
int mq_cols_console(mq_t *q)
{
    const mq_cols_t *c = &q->cols;
    q->api->con_printf("notebook %s kind %d level %d: %ld %c %ld, step %d of %d\n", c->on ? "open" : "shut", c->kind, c->level,
                       (long)c->a, " +-x/"[c->kind % 5], (long)c->b, c->cur, c->n);
    for (int i = 0; i < c->n; i++)
        q->api->con_printf("%c %d: kind %d row %d col %d want %d\n", i == c->cur ? '*' : ' ', i, c->st[i].kind, c->st[i].row,
                           c->st[i].col, c->st[i].want);
    return 0;
}
