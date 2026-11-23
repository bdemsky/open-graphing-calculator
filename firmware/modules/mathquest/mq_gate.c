/*
 * Math Quest: the grown-ups' gate (docs/MATHQUEST.md "The grown-ups' gate").
 *
 * The way to the grown-ups' screen is one problem just beyond the top of Sparkle Town's curriculum
 * (MQ_TOP_GRADE): a grown-up does it in their head in a few seconds; a child the program is for has not
 * met it yet (negative numbers, a letter in an equation, percents, powers, square roots). A child who can
 * do these has outgrown the program -- and is welcome in. The kinds come from the grades above
 * MQ_TOP_GRADE (up to three grades above it), so when the curriculum gains a grade the gate moves up one.
 *
 * The answer is typed (a pad of the shared widgets, the digit keys, (-) or -, DEL, ENTER), never picked:
 * a pick would be a guess. A wrong answer: no hint, nothing shown, a new problem after a pause that
 * grows with the wrong answers in a row (1.5 s, 3, 6, 12, 24, then 30 s at most: never a lock-out).
 * The problems are generated (no list to remember) with their own random numbers, and no answer is
 * common (test_mq_gate.c: over 10 000 draws none comes up in 4 %).
 *
 * The gate keeps its own widget set (q->gate.wg) and swaps it in while it handles an input or draws, so
 * the scene under it keeps its own.
 */
#include "mq.h"

enum { K_F1 = 1, K_F5 = 5, K_DEL = 8, K_UP = 12, K_LEFT = 13, K_RIGHT = 14, K_DOWN = 15, K_CLEAR = 20, K_SUB = 40,
       K_NEG = 48, K_ENTER = 49 };
enum { GW_KEY = 1, GW_SIGN, GW_DEL, GW_OK, GW_CLOSE };
#define GATE_IDLE_MS 120000u    /* nobody at the gate: it closes */
#define GATE_FORGET_MS 600000u  /* wrong answers this long ago are forgotten */
#define GATE_COOL_MAX 30000u    /* the longest pause after a wrong answer */

/* ------------------------------------------------------------------ the problems */
static const uint8_t kind_grade[GK_COUNT] = {
    [GK_PCT] = 6,    /* 6.RP.3c  a percent of a number */
    [GK_POW] = 6,    /* 6.EE.1   whole-number exponents, with the order of operations */
    [GK_NEG] = 7,    /* 7.NS.1   adding and subtracting negative numbers */
    [GK_NEGMUL] = 7, /* 7.NS.2   multiplying negative numbers */
    [GK_EQ2] = 7,    /* 7.EE.4a  a two-step equation with a letter */
    [GK_ROOT] = 8,   /* 8.EE.2   square roots */
    [GK_BOTH] = 8,   /* 8.EE.7   the letter on both sides */
};
static const char kind_name[GK_COUNT][8] = {"pct", "pow", "neg", "negmul", "eq2", "root", "both"};
const char *mq_gate_kind_name(int kind) { return kind >= 0 && kind < GK_COUNT ? kind_name[kind] : "?"; }

/* a kind of a grade just above the curriculum (up to three grades above; past the hardest we have: the hardest) */
bool mq_gate_kind_allowed(int kind, int top)
{
    if (kind < 0 || kind >= GK_COUNT)
        return false;
    int g = kind_grade[kind], hi = 0;
    for (int k = 0; k < GK_COUNT; k++)
        hi = kind_grade[k] > hi ? kind_grade[k] : hi;
    if (top >= hi)
        return g == hi;
    return g > top && g <= top + 3;
}

static uint32_t rnd(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13, x ^= x >> 17, x ^= x << 5;
    return *s = x;
}
static int rr(uint32_t *s, int lo, int hi) { return lo + (int)(rnd(s) % (uint32_t)(hi - lo + 1)); }
static int gcd(int a, int b) { return b ? gcd(b, a % b) : a; }

/* the text: a few formats, no snprintf needed (the generator is pure: tests call it without a module) */
static void put(char **o, char *end, const char *s)
{
    while (*s && *o < end)
        *(*o)++ = *s++;
}
static void put_n(char **o, char *end, int v)
{
    char b[8];
    int n = 0;
    if (v < 0)
        put(o, end, "-"), v = -v;
    do
        b[n++] = (char)('0' + v % 10), v /= 10;
    while (v && n < 7);
    while (n && *o < end)
        *(*o)++ = b[--n];
}
/* fmt: %d takes the next number, everything else is copied */
static void say(char *out, size_t cap, const char *fmt, const int *v)
{
    char *o = out, *end = out + cap - 1;
    for (const char *f = fmt; *f && o < end; f++) {
        if (f[0] == '%' && f[1] == 'd') {
            put_n(&o, end, *v++);
            f++;
        } else
            *o++ = *f;
    }
    *o = 0;
}

static int make_one(uint32_t *s, int kind, mq_gprob_t *p)
{
    static const uint8_t pcts[] = {15, 20, 25, 30, 35, 40, 45, 60, 75};
    int v[6] = {0}, ans = 0;
    const char *f = "";
    switch (kind) {
    case GK_PCT: { /* "What is 15% of 80?" (no 10 %, no 50 %: a short cut a child may know) */
        int pc = pcts[rr(s, 0, (int)sizeof pcts - 1)], base = 100 / gcd(pc, 100);
        int n = base * rr(s, 1, 400 / base);
        if (n < 20 || n % 100 == 0)
            return 0;
        v[0] = pc, v[1] = n, ans = pc * n / 100, f = "What is %d% of %d?";
        break;
    }
    case GK_POW: {
        int a, b, c, k;
        switch (rr(s, 0, 3)) {
        case 0:
            a = rr(s, 3, 9), b = rr(s, 2, 9), c = rr(s, 2, 9);
            v[0] = a, v[1] = b, v[2] = c, ans = a * a + b * c, f = "What is %d squared, plus %d times %d?";
            break;
        case 1:
            a = rr(s, 5, 12), b = rr(s, 2, 9), c = rr(s, 2, 9);
            v[0] = a, v[1] = b, v[2] = c, ans = a * a - b * c, f = "What is %d squared, minus %d times %d?";
            break;
        case 2:
            k = rr(s, 4, 6), b = rr(s, 3, 40);
            v[0] = k, v[1] = b, ans = (1 << k) - b, f = "What is 2 to the power %d, minus %d?";
            break;
        default:
            a = rr(s, 2, 4), b = rr(s, 2, 30);
            v[0] = a, v[1] = b, ans = a * a * a - b, f = "What is %d cubed, minus %d?";
            break;
        }
        break;
    }
    case GK_NEG:
        if (rr(s, 0, 1)) {
            v[0] = rr(s, 2, 19), v[1] = rr(s, 3, 30), v[2] = rr(s, 2, 30);
            ans = -v[0] + v[1] - v[2], f = "What is negative %d, plus %d, minus %d?";
        } else {
            v[0] = rr(s, 3, 20), v[1] = v[0] + rr(s, 2, 40);
            ans = v[0] - v[1], f = "What is %d minus %d?";
        }
        break;
    case GK_NEGMUL:
        v[0] = rr(s, 2, 9), v[1] = rr(s, 3, 9), v[2] = rr(s, 3, 60);
        if (rr(s, 0, 1))
            ans = -v[0] * v[1] + v[2], f = "What is negative %d times %d, plus %d?";
        else
            ans = v[0] * v[1] - v[2], f = "What is negative %d times negative %d, minus %d?";
        break;
    case GK_EQ2: { /* "If 3n + 7 = 31, what is n?" */
        int a = rr(s, 2, 9), n = rr(s, 2, 25), b = rr(s, 2, 30);
        v[0] = a, v[1] = b, ans = n;
        if (rr(s, 0, 1))
            v[2] = a * n + b, f = "If %dn + %d = %d, what is n?";
        else
            v[2] = a * n - b, f = "If %dn - %d = %d, what is n?";
        if (v[2] < 1)
            return 0;
        break;
    }
    case GK_ROOT: {
        int r = rr(s, 4, 20), k;
        switch (rr(s, 0, 2)) {
        case 0:
            k = rr(s, 2, 30);
            v[0] = r * r, v[1] = k, ans = r + k, f = "What is the square root of %d, plus %d?";
            break;
        case 1:
            k = rr(s, 2, r - 2);
            v[0] = r * r, v[1] = k * k, ans = r - k, f = "What is the square root of %d, minus the square root of %d?";
            break;
        default:
            k = rr(s, 2, 6);
            v[0] = k, v[1] = r * r, ans = k * r, f = "What is %d times the square root of %d?";
            break;
        }
        break;
    }
    case GK_BOTH: { /* "If 5n - 4 = 2n + 11, what is n?" */
        int n = rr(s, 2, 15), c = rr(s, 2, 6), a = c + rr(s, 2, 6), b = rr(s, 1, 20), d = (a - c) * n - b;
        if (d < 1)
            return 0;
        v[0] = a, v[1] = b, v[2] = c, v[3] = d, ans = n, f = "If %dn - %d = %dn + %d, what is n?";
        break;
    }
    default: return 0;
    }
    if (ans > 99 || ans < -99 || (ans < 2 && ans > -2))
        return 0;
    p->kind = (uint8_t)kind, p->grade = kind_grade[kind], p->ans = (int16_t)ans;
    say(p->text, sizeof p->text, f, v);
    return 1;
}

int mq_gate_make(uint32_t *s, int top, mq_gprob_t *p)
{
    int kinds[GK_COUNT], nk = 0;
    for (int k = 0; k < GK_COUNT; k++)
        if (mq_gate_kind_allowed(k, top))
            kinds[nk++] = k;
    for (int t = 0; t < 200; t++)
        if (make_one(s, kinds[rr(s, 0, nk - 1)], p))
            return p->kind;
    uint32_t z = 7; /* (never reached in practice: every kind makes one in a few tries) */
    while (!make_one(&z, kinds[0], p))
        ;
    return p->kind;
}

uint32_t mq_gate_cooldown_ms(int wrong)
{
    if (wrong <= 0)
        return 0;
    uint32_t ms = wrong > 6 ? GATE_COOL_MAX : 1500u << (wrong - 1);
    return ms > GATE_COOL_MAX ? GATE_COOL_MAX : ms;
}

/* ------------------------------------------------------------------ the gate */
static void swap_wg(mq_t *q)
{
    uint8_t *a = (uint8_t *)&q->wg, *b = (uint8_t *)&q->gate.wg;
    for (size_t i = 0; i < sizeof q->wg; i++) {
        uint8_t t = a[i];
        a[i] = b[i], b[i] = t;
    }
}
static bool waiting(const mq_t *q) { return q->gate.wait_until && (int32_t)(q->now - q->gate.wait_until) < 0; }

static void new_problem(mq_t *q)
{
    mq_gate_t *g = &q->gate;
    mq_gate_make(&g->rng, MQ_TOP_GRADE, &g->p);
    g->n = 0, g->in[0] = 0;
}

void mq_gate_open(mq_t *q)
{
    mq_gate_t *g = &q->gate;
    g->shown = true, g->since = q->now, g->opened++;
    if (g->wrong && q->now - g->wrong_t > GATE_FORGET_MS)
        g->wrong = 0;
    if (!g->fixed) /* (fresh numbers each time: the clock to the millisecond, how often it opened) */
        g->rng ^= (q->api->millis() * 2654435761u) ^ (q->now << 7) ^ (g->opened * 0x85EBCA6Bu);
    g->wg.kbd = false, g->wg.focus = -1, g->wg.focus_id = 0; /* (no focus ring left from the last time) */
    g->wg.press = g->wg.carry = g->wg.picked = g->wg.hover = g->wg.dragged = -1; /* (nothing held: not 0, the first key) */
    new_problem(q);
    q->redraw = true;
}
void mq_gate_close(mq_t *q) { q->gate.shown = false, q->gate.n = 0, q->gate.in[0] = 0, q->redraw = true; }

bool mq_gate_submit(mq_t *q)
{
    mq_gate_t *g = &q->gate;
    if (!g->shown || waiting(q))
        return false;
    int v = 0, k = g->in[0] == '-';
    if (g->n <= k) /* nothing typed: nothing happens */
        return false;
    for (int i = k; i < g->n; i++)
        v = v * 10 + g->in[i] - '0';
    if (k)
        v = -v;
    if (v == g->p.ans) {
        g->wrong = 0, g->wait_until = 0, g->adult_t = q->now;
        return true;
    }
    if (g->wrong < 20)
        g->wrong++;
    g->wrong_t = q->now;
    g->wait_until = (q->now + mq_gate_cooldown_ms(g->wrong)) | 1;
    g->n = 0, g->in[0] = 0;
    mq_sfx(q, MODX_SFX_DUD, 0);
    q->redraw = true;
    return false;
}

/* the panel: the problem on the left, the pad on the right */
#define GP_X 6
#define GP_Y (MQ_Y0 + 4)
#define GP_W 308
#define GP_H (MQ_H - 8)
#define PAD_X 196
#define PAD_Y (MQ_Y0 + 12)
#define KEY_W 36
#define KEY_H 30
#define KEY_GAP 4
static void layout(mq_t *q)
{
    static const char keys[12] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '-', '0', '<'};
    bool w = waiting(q);
    mq_wg_begin(q);
    for (int k = 0; k < 12; k++) {
        int c = keys[k], id = c == '-' ? GW_SIGN : c == '<' ? GW_DEL : GW_KEY;
        int i = mq_wg_add(q, WG_KEY, id, (uint8_t)c, PAD_X + (k % 3) * (KEY_W + KEY_GAP), PAD_Y + (k / 3) * (KEY_H + KEY_GAP),
                          KEY_W, KEY_H);
        char s[2] = {(char)c, 0};
        mq_wg_label(q, i, c == '<' ? "del" : s, c == '<' ? Q_PEACH : c == '-' ? Q_LAV : Q_WHITE);
        if (w)
            mq_wg_flags(q, i, WF_DISABLED);
    }
    int i = mq_wg_add(q, WG_KEY, GW_OK, 0, PAD_X, PAD_Y + 4 * (KEY_H + KEY_GAP), 3 * KEY_W + 2 * KEY_GAP, KEY_H);
    mq_wg_label(q, i, "OK", Q_MINT);
    if (w)
        mq_wg_flags(q, i, WF_DISABLED);
    i = mq_wg_add(q, WG_BUTTON, GW_CLOSE, 0, GP_X + 10, GP_Y + GP_H - 30, 70, 22);
    mq_wg_label(q, i, "close", Q_PEACH);
}

static void type(mq_t *q, int c)
{
    mq_gate_t *g = &q->gate;
    if (waiting(q))
        return;
    if (c == '-') { /* the sign: on and off, in front */
        if (g->in[0] == '-') {
            for (int i = 0; i < g->n; i++)
                g->in[i] = g->in[i + 1];
            g->n--;
        } else if (g->n < 4) {
            for (int i = g->n; i >= 0; i--)
                g->in[i + 1] = g->in[i];
            g->in[0] = '-', g->n++;
        }
    } else if (c == '<') {
        if (g->n)
            g->in[--g->n] = 0;
    } else if (c >= '0' && c <= '9' && g->n - (g->in[0] == '-') < 3)
        g->in[g->n++] = (char)c, g->in[g->n] = 0;
    q->redraw = true;
}

static int act(mq_t *q, const mq_wev_t *ev)
{
    if (ev->why != WHY_TAP && ev->why != WHY_KEY)
        return MQG_NONE;
    switch (ev->id) {
    case GW_KEY: type(q, ev->arg); break;
    case GW_SIGN: type(q, '-'); break;
    case GW_DEL: type(q, '<'); break;
    case GW_OK: return mq_gate_submit(q) ? MQG_PASS : MQG_NONE;
    case GW_CLOSE: mq_gate_close(q); return MQG_CLOSE;
    default: break;
    }
    return MQG_NONE;
}

int mq_gate_touch(mq_t *q, int type_, int x, int y)
{
    q->gate.since = q->now;
    swap_wg(q);
    layout(q);
    mq_wev_t ev;
    bool hit = mq_wg_touch(q, type_, x, y, &ev) && ev.why != WHY_NONE;
    swap_wg(q);
    q->redraw = true;
    return hit ? act(q, &ev) : MQG_NONE;
}

int mq_gate_key(mq_t *q, int k, int c)
{
    mq_gate_t *g = &q->gate;
    g->since = q->now;
    if (k == K_CLEAR) {
        mq_gate_close(q);
        return MQG_CLOSE;
    }
    if (k == K_DEL) {
        type(q, '<');
        return MQG_NONE;
    }
    if (k == K_SUB || k == K_NEG)
        c = '-';
    if (k == K_ENTER && !g->wg.kbd) /* ENTER answers (after the arrows: it presses the key they chose) */
        return mq_gate_submit(q) ? MQG_PASS : MQG_NONE;
    bool arrow = k == K_UP || k == K_DOWN || k == K_LEFT || k == K_RIGHT;
    if (!arrow && k != K_ENTER && !(c >= '0' && c <= '9') && c != '-')
        return MQG_NONE; /* (letters, soft keys, anything else: nothing) */
    swap_wg(q);
    layout(q);
    mq_wev_t ev;
    bool hit = mq_wg_key(q, k, c, &ev) && ev.why != WHY_NONE && ev.idx >= 0;
    if (!arrow && !(k == K_ENTER))
        q->wg.kbd = false; /* (a digit key typed: ENTER answers again) */
    swap_wg(q);
    q->redraw = true;
    if (hit)
        return act(q, &ev);
    if (c == '-' || (c >= '0' && c <= '9')) /* (while the pad waits: its keys are off) */
        type(q, c);
    return MQG_NONE;
}

int mq_gate_frame(mq_t *q)
{
    mq_gate_t *g = &q->gate;
    if (!g->shown)
        return MQG_NONE;
    if (g->wait_until && !waiting(q)) { /* the pause is over: a new problem */
        g->wait_until = 0;
        new_problem(q);
        q->redraw = true;
    }
    if (q->now - g->since > GATE_IDLE_MS && !waiting(q)) { /* nobody came */
        mq_gate_close(q);
        return MQG_CLOSE;
    }
    return MQG_NONE;
}

/* the problem's words over up to four lines of w px (the small font if the medium one needs more) */
static void wrap(mq_t *q, int x, int y, int w, const char *s, uint16_t col)
{
    const modx_api_t *api = q->api;
    for (int pass = 0; pass < 2; pass++) {
        const font_t *f = pass ? api->font_small : api->font_medium;
        int lh = pass ? 14 : 20, lines = 0;
        char ln[4][64];
        const char *p = s;
        while (*p && lines < 5) {
            char cur[64];
            int n = 0, last = -1;
            const char *start = p;
            while (*p && n < 63) {
                cur[n] = *p, cur[n + 1] = 0;
                if (*p == ' ')
                    last = n;
                if (mq_text_w(q, f, cur) > w && last > 0) {
                    n = last;
                    p = start + last + 1;
                    break;
                }
                n++, p++;
            }
            cur[n] = 0;
            if (lines < 4)
                api->snprintf(ln[lines], 64, "%s", cur);
            lines++;
        }
        if (lines > 4 && !pass)
            continue;
        for (int i = 0; i < lines && i < 4; i++)
            mq_text(q, f, x, y + i * lh, ln[i], col);
        return;
    }
}

void mq_gate_draw(mq_t *q)
{
    const modx_api_t *api = q->api;
    mq_gate_t *g = &q->gate;
    swap_wg(q);
    layout(q);
    mq_fill(q, 0, MQ_Y0, MQ_W, MQ_H, RGB(236, 230, 244)); /* (a quiet screen behind it) */
    mq_rrect(q, GP_X, GP_Y, GP_W, GP_H, 12, Q_INK);
    mq_text(q, api->font_medium, GP_X + 10, GP_Y + 6, "Grown-ups only", Q_YELLOW);
    mq_text(q, api->font_small, GP_X + 10, GP_Y + 26, "To open the settings:", Q_WHITE);
    char s[40];
    if (waiting(q)) {
        uint32_t left = (g->wait_until - q->now + 999) / 1000;
        mq_text(q, api->font_medium, GP_X + 10, GP_Y + 52, "Not quite.", Q_WHITE);
        api->snprintf(s, sizeof s, "A new problem in %u s.", (unsigned)left);
        mq_text(q, api->font_small, GP_X + 10, GP_Y + 76, s, Q_WHITE);
    } else {
        wrap(q, GP_X + 10, GP_Y + 46, PAD_X - GP_X - 24, g->p.text, Q_WHITE);
        mq_rrect(q, GP_X + 10, GP_Y + 128, 120, 26, 8, Q_WHITE);
        api->snprintf(s, sizeof s, "%s_", g->in);
        mq_text(q, api->font_medium, GP_X + 18, GP_Y + 132, s, Q_INK);
        mq_text(q, api->font_small, GP_X + 10, GP_Y + 158, "ENTER answers, (-) minus", Q_LAV);
    }
    mq_wg_draw(q);
    swap_wg(q);
}

/* mq gate [seed N | answer | pass | wait]: show it (the Mac's corner menu "Grown-ups..."); tests: fixed problems,
 * the answer, the way in without typing (console only: nothing a child can reach), the cooldown left */
int mq_gate_console(mq_t *q, int argc, char **argv)
{
    const modx_api_t *api = q->api;
    mq_gate_t *g = &q->gate;
    if (argc <= 2) {
        mq_gate_open(q);
        return MQG_NONE;
    }
    const char *a = argv[2];
    if (!api->strcmp(a, "seed") && argc > 3) {
        uint32_t v = 0;
        for (const char *c = argv[3]; *c >= '0' && *c <= '9'; c++)
            v = v * 10 + (uint32_t)(*c - '0');
        g->fixed = 1, g->rng = v ? v : 1;
        if (g->shown)
            new_problem(q), q->redraw = true;
        return MQG_NONE;
    }
    if (!api->strcmp(a, "answer")) {
        api->con_printf("%d\n%s\n", g->p.ans, g->p.text);
        return MQG_NONE;
    }
    if (!api->strcmp(a, "pass")) { /* (tests: through the gate as a right answer would) */
        g->wrong = 0, g->wait_until = 0, g->adult_t = q->now;
        return MQG_PASS;
    }
    if (!api->strcmp(a, "wait")) {
        api->con_printf("wrong=%d wait_ms=%d\n", g->wrong, waiting(q) ? (int)(g->wait_until - q->now) : 0);
        return MQG_NONE;
    }
    return -1;
}
