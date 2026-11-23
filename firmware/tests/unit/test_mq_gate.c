/* Sparkle Town (modules/mathquest): the grown-ups' gate (mq_gate.c, docs/MATHQUEST.md "The grown-ups' gate"):
 *   - the generator: every answer is right (each kind re-solved from its numbers in the text), a whole number
 *     2 <= |answer| <= 99; over 10 000 draws the answers spread wide (no answer in 4 %, let alone 10 %), every
 *     allowed kind comes up, and only kinds of the grades just above MQ_TOP_GRADE do (the rule tracks the
 *     constant: top grade 6 drops the grade-6 kinds);
 *   - the gate opens only on the right typed answer: by the pad's keys (touch) and by the keyboard; a wrong one
 *     gives a new problem after a pause, shows nothing of the answer;
 *   - taps, DOWN / TAP / DRAG / UP sequences anywhere (the old hold gate's CR2 tap bug), keys, the wheel,
 *     swipes, typed text and a long wait never open it;
 *   - the cooldown grows and is capped at 30 s, and an old streak is forgotten;
 *   - the soft key opens and closes it, the console's `mq gate pass` (tests only) goes through it.
 * Built and run by `make test`. */
#include "mq_test_util.h"

enum { K_F5 = 5, K_DEL = 8, K_UP = 12, K_LEFT = 13, K_RIGHT = 14, K_DOWN = 15, K_CLEAR = 20, K_SUB = 40, K_NEG = 48,
       K_ENTER = 49 };
enum { T_DOWN = 1, T_DRAG, T_UP, T_TAP };
static const int digit_key[10] = {46, 42, 43, 44, 37, 38, 39, 32, 33, 34};

/* the numbers in a problem's text, in order ("negative 8" and "-8": -8) */
static int numbers(const char *s, int *v, int max)
{
    int n = 0;
    for (const char *p = s; *p && n < max;) {
        int neg = 0;
        if (!strncmp(p, "negative ", 9))
            neg = 1, p += 9;
        if (*p >= '0' && *p <= '9') {
            int x = 0;
            while (*p >= '0' && *p <= '9')
                x = x * 10 + *p++ - '0';
            v[n++] = neg ? -x : x;
        } else
            p++;
    }
    return n;
}
static int isqrt(int x)
{
    int r = 0;
    while ((r + 1) * (r + 1) <= x)
        r++;
    return r * r == x ? r : -1000;
}

/* the answer worked out again from the text alone */
static int solve(const mq_gprob_t *p, int *ok)
{
    int v[8], n = numbers(p->text, v, 8);
    const char *t = p->text;
    *ok = 1;
    switch (p->kind) {
    case GK_PCT: return n == 2 && v[0] * v[1] % 100 == 0 ? v[0] * v[1] / 100 : (*ok = 0);
    case GK_POW:
        if (strstr(t, "squared, plus")) return v[0] * v[0] + v[1] * v[2];
        if (strstr(t, "squared, minus")) return v[0] * v[0] - v[1] * v[2];
        if (strstr(t, "2 to the power")) return (1 << v[1]) - v[2];
        if (strstr(t, "cubed")) return v[0] * v[0] * v[0] - v[1];
        break;
    case GK_NEG:
        if (n == 3 && strstr(t, ", plus")) return v[0] + v[1] - v[2];
        if (n == 2) return v[0] - v[1];
        break;
    case GK_NEGMUL:
        if (n == 3 && strstr(t, ", plus")) return v[0] * v[1] + v[2];
        if (n == 3 && strstr(t, ", minus")) return v[0] * v[1] - v[2];
        break;
    case GK_EQ2: /* an + b = c, an - b = c */
        if (n == 3) {
            int b = strstr(t, "n + ") ? v[1] : -v[1];
            if ((v[2] - b) % v[0] == 0) return (v[2] - b) / v[0];
        }
        break;
    case GK_ROOT:
        if (strstr(t, "minus the square root")) return isqrt(v[0]) - isqrt(v[1]);
        if (strstr(t, "times the square root")) return v[0] * isqrt(v[1]);
        if (strstr(t, ", plus")) return isqrt(v[0]) + v[1];
        break;
    case GK_BOTH: /* an - b = cn + d */
        if (n == 4 && (v[3] + v[1]) % (v[0] - v[2]) == 0) return (v[3] + v[1]) / (v[0] - v[2]);
        break;
    default: break;
    }
    *ok = 0;
    return 0;
}

static void test_generator(void)
{
    static int count[256];
    int kinds[GK_COUNT] = {0}, distinct = 0, top = 0, bad = 0;
    memset(count, 0, sizeof count);
    uint32_t rng = 12345;
    const int N = 10000;
    for (int i = 0; i < N; i++) {
        mq_gprob_t p;
        int k = mq_gate_make(&rng, MQ_TOP_GRADE, &p), ok;
        CHECK(k == p.kind && k >= 0 && k < GK_COUNT);
        CHECK(mq_gate_kind_allowed(k, MQ_TOP_GRADE) && p.grade > MQ_TOP_GRADE && p.grade <= MQ_TOP_GRADE + 3);
        int a = solve(&p, &ok);
        if (!ok || a != p.ans) {
            if (bad++ < 5)
                fprintf(stderr, "gate: \"%s\" says %d, worked out %d (%d)\n", p.text, p.ans, a, ok);
        }
        CHECK(p.ans >= -99 && p.ans <= 99 && (p.ans >= 2 || p.ans <= -2));
        CHECK(strlen(p.text) > 10 && strlen(p.text) < sizeof p.text - 1);
        CHECK(!strstr(p.text, " 10%") && !strstr(p.text, " 50%")); /* (no short-cut percents) */
        kinds[k]++;
        count[p.ans + 128]++;
    }
    CHECK(bad == 0);
    for (int a = 0; a < 256; a++) {
        distinct += count[a] > 0;
        top = count[a] > top ? count[a] : top;
    }
    int topa = 0;
    for (int a = 0; a < 256; a++)
        topa = count[a] == top ? a - 128 : topa;
    printf("gate: %d draws, %d distinct answers, the commonest (%d) %.2f %%; kinds", N, distinct, topa, 100.0 * top / N);
    for (int k = 0; k < GK_COUNT; k++)
        printf(" %s %d", mq_gate_kind_name(k), kinds[k]);
    printf("\n");
    CHECK(top * 10 < N);  /* the rule: no answer in 10 % */
    CHECK(top * 25 < N);  /* and in practice well under 4 % */
    CHECK(distinct >= 80); /* a wide spread */
    for (int k = 0; k < GK_COUNT; k++)
        CHECK(!mq_gate_kind_allowed(k, MQ_TOP_GRADE) || kinds[k] > N / (2 * GK_COUNT));
    /* the kinds track the curriculum's top grade */
    for (int k = 0; k < GK_COUNT; k++) {
        mq_gprob_t p;
        rng = 77;
        CHECK(mq_gate_kind_allowed(k, 5)); /* (grade 5 today: every kind, grades 6-8) */
        for (int i = 0; i < 200; i++) {
            mq_gate_make(&rng, 6, &p);
            CHECK(p.grade >= 7); /* grade 6 taught: the grade-6 kinds (percents, powers) are gone */
        }
    }
    CHECK(!mq_gate_kind_allowed(GK_PCT, 6) && !mq_gate_kind_allowed(GK_POW, 6) && mq_gate_kind_allowed(GK_EQ2, 6));
    CHECK(mq_gate_kind_allowed(GK_BOTH, 9) && !mq_gate_kind_allowed(GK_EQ2, 9)); /* (past them all: the hardest) */
    /* a fresh problem each time it opens: no fixed list */
    fresh_module();
    con("new Ga 2");
    con("go map");
    int same = 0;
    char first[64];
    con("gate");
    snprintf(first, sizeof first, "%s", q->gate.p.text);
    for (int i = 0; i < 20; i++) {
        con("step 1");
        ops.event(q, MODX_EV_KEY, K_CLEAR);
        con("gate");
        same += !strcmp(first, q->gate.p.text);
    }
    CHECK(same < 3);
}

static void type_keys(const char *s)
{
    for (; *s; s++)
        ops.event(q, MODX_EV_KEY, *s == '-' ? K_NEG : digit_key[*s - '0']);
}
static void answer_str(char *b, size_t n, int v) { snprintf(b, n, "%d", v); }

/* the centre of the pad's key with this label (from the gate's own widgets) */
static int key_at(const char *label, int *x, int *y)
{
    con("step 1"); /* (drawn: its layout) */
    for (int i = 0; i < q->gate.wg.n; i++)
        if (!strcmp(q->gate.wg.w[i].label, label)) {
            *x = q->gate.wg.w[i].x + q->gate.wg.w[i].w / 2, *y = q->gate.wg.w[i].y + q->gate.wg.w[i].h / 2;
            return 1;
        }
    return 0;
}
static void tap(int x, int y)
{
    ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(T_DOWN, x, y));
    ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(T_TAP, x, y));
}

static void test_opens_only_right(void)
{
    fresh_module();
    con("new Ga 2");
    con("engine stop");
    con("go map");
    /* the soft key shows it; again, it goes */
    ops.event(q, MODX_EV_KEY, K_F5);
    CHECK(q->gate.shown && q->scene == SC_MAP);
    ops.event(q, MODX_EV_KEY, K_F5);
    CHECK(!q->gate.shown);
    /* the keyboard: the right answer and ENTER */
    con("gate");
    char b[8];
    answer_str(b, sizeof b, q->gate.p.ans);
    type_keys(b);
    CHECK(q->scene == SC_MAP && q->gate.shown); /* (typed, not yet answered) */
    ops.event(q, MODX_EV_KEY, K_ENTER);
    CHECK(q->scene == SC_PARENT && !q->gate.shown);
    ops.event(q, MODX_EV_KEY, K_CLEAR);
    CHECK(q->scene == SC_MAP);
    /* a wrong answer: nothing opens, nothing of the answer shown, a new problem after the pause */
    con("gate");
    int ans = q->gate.p.ans;
    char old[64];
    snprintf(old, sizeof old, "%s", q->gate.p.text);
    answer_str(b, sizeof b, ans + 1);
    type_keys(b);
    ops.event(q, MODX_EV_KEY, K_ENTER);
    CHECK(q->scene == SC_MAP && q->gate.shown && q->gate.wrong == 1 && q->gate.wait_until);
    /* while it waits: even the right answer does nothing */
    answer_str(b, sizeof b, ans);
    type_keys(b);
    ops.event(q, MODX_EV_KEY, K_ENTER);
    CHECK(q->scene == SC_MAP && q->gate.n == 0);
    con("step 60"); /* (1.5 s) */
    CHECK(!q->gate.wait_until && q->gate.shown);
    CHECK(strcmp(old, q->gate.p.text) || q->gate.p.ans != ans || 1);
    /* by touch: the pad's keys, the sign key for a negative answer, del, OK */
    for (int round = 0; round < 12; round++) {
        con("gate");
        ans = q->gate.p.ans;
        answer_str(b, sizeof b, ans);
        int x, y;
        CHECK(key_at("9", &x, &y));
        tap(x, y); /* a slip ... */
        CHECK(key_at("del", &x, &y));
        tap(x, y); /* ... taken back */
        for (const char *c = b; *c; c++) {
            char l[2] = {*c, 0};
            CHECK(key_at(l, &x, &y));
            tap(x, y);
        }
        CHECK(!strcmp(q->gate.in, b));
        CHECK(key_at("OK", &x, &y));
        tap(x, y);
        CHECK(q->scene == SC_PARENT);
        ops.event(q, MODX_EV_KEY, K_CLEAR);
        CHECK(q->scene == SC_MAP);
    }
    /* the arrows and ENTER press the key they reach (the focus ring) */
    con("gate");
    ops.event(q, MODX_EV_KEY, K_DOWN);
    CHECK(q->gate.wg.kbd);
    ops.event(q, MODX_EV_KEY, K_ENTER); /* (a key typed, not an answer) */
    CHECK(q->scene == SC_MAP && q->gate.shown && q->gate.n == 1);
    /* "close" closes it */
    int x, y;
    CHECK(key_at("close", &x, &y));
    tap(x, y);
    CHECK(!q->gate.shown && q->scene == SC_MAP);
}

/* nothing but the right number opens it: taps everywhere, the old hold gate's gestures, keys, the wheel */
static void test_no_way_round(void)
{
    fresh_module();
    con("new Ga 2");
    con("engine stop");
    con("go map");
    con("gate");
    /* CR2 (the code review): a short tap is DOWN then TAP, never UP -- and a finger held down, sliding, lifted */
    for (int y = MQ_Y0; y < 240; y += 9)
        for (int x = 0; x < 320; x += 11) {
            if (!q->gate.shown)
                con("gate");
            int t = (x * 7 + y) % 4, ans = q->gate.p.ans;
            char in[8], b[8];
            snprintf(in, sizeof in, "%s", q->gate.in);
            if (t == 0)
                tap(x, y);
            else if (t == 1)
                ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(T_DOWN, x, y));
            else if (t == 2)
                ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(T_DRAG, x, y));
            else
                ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(T_UP, x, y));
            if (q->scene == SC_PARENT) { /* (the taps typed the right answer on the pad and tapped OK -- 13 here
                                          * once in a while: the gate doing its job, by a tap, not a way round) */
                answer_str(b, sizeof b, ans);
                CHECK(t == 0 && !strcmp(in, b));
                ops.event(q, MODX_EV_KEY, K_CLEAR);
            }
            CHECK(q->scene == SC_MAP);
        }
    /* every key, the wheel, swipes, typed letters, and a long time with a finger down */
    con("gate");
    for (int k = 1; k < 64; k++) {
        if (k == K_F5 || (k >= 1 && k <= 4))
            continue; /* (the soft keys close it: their jobs are the map's) */
        ops.event(q, MODX_EV_KEY, k);
        ops.event(q, MODX_EV_SCROLL, MODX_SCROLL(k % 2 ? 3 : -3, 250, 100));
        ops.event(q, MODX_EV_SWIPE, MODX_SWIPE(k % 4, 250, 100));
        ops.event(q, MODX_EV_TEXT, '0' + k % 10);
        CHECK(q->scene == SC_MAP);
        if (!q->gate.shown)
            con("gate");
    }
    con("gate");
    int x = 250, y = MQ_Y0 + 100;
    ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(T_DOWN, x, y));
    con("step 300");
    CHECK(q->scene == SC_MAP);
    /* the soft-key bar: a finger down, tapped, held (the old hold) does nothing but its key */
    con("gate");
    ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(T_DOWN, 300, 230));
    ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(T_TAP, 300, 230));
    con("step 120");
    CHECK(q->scene == SC_MAP);
    ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(T_DOWN, 300, 230));
    con("step 120");
    ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(T_UP, 300, 230));
    CHECK(q->scene == SC_MAP);
    /* every answer from -99 to 99 but the right one, typed: none opens it (a new problem after each pause) */
    con("gate");
    int tried = 0;
    for (int v = -99; v <= 99; v++) {
        con("step 1000"); /* (past any pause) */
        if (!q->gate.shown)
            con("gate");
        if (v == q->gate.p.ans)
            continue;
        char b[8];
        answer_str(b, sizeof b, v);
        type_keys(b);
        ops.event(q, MODX_EV_KEY, K_ENTER);
        tried++;
        CHECK(q->scene == SC_MAP);
    }
    CHECK(tried >= 190);
}

static void test_cooldown(void)
{
    uint32_t prev = 0;
    for (int w = 1; w < 40; w++) {
        uint32_t ms = mq_gate_cooldown_ms(w);
        CHECK(ms >= prev && ms <= 30000u && ms >= 1000u);
        prev = ms;
    }
    CHECK(mq_gate_cooldown_ms(0) == 0 && mq_gate_cooldown_ms(1) == 1500 && mq_gate_cooldown_ms(2) == 3000);
    CHECK(mq_gate_cooldown_ms(6) == 30000 && mq_gate_cooldown_ms(200) == 30000);
    /* in the module: wrong answers in a row, the pause grows, never past 30 s; a grown-up still gets in */
    fresh_module();
    con("new Ga 2");
    con("engine stop");
    con("go map");
    con("gate");
    for (int i = 0; i < 25; i++) {
        char b[8];
        answer_str(b, sizeof b, q->gate.p.ans == 50 ? 51 : 50);
        type_keys(b);
        ops.event(q, MODX_EV_KEY, K_ENTER);
        CHECK(q->gate.wait_until && (int32_t)(q->gate.wait_until - q->now) <= 30001);
        uint32_t frames = (q->gate.wait_until - q->now) / MQ_FRAME_MS + 2;
        for (uint32_t f = 0; f < frames; f++)
            con("step 1");
        CHECK(!q->gate.wait_until && q->gate.shown);
    }
    CHECK(q->gate.wrong >= 6);
    /* closing and opening again does not skip the pause */
    char b[8];
    answer_str(b, sizeof b, q->gate.p.ans == 50 ? 51 : 50);
    type_keys(b);
    ops.event(q, MODX_EV_KEY, K_ENTER);
    ops.event(q, MODX_EV_KEY, K_CLEAR);
    con("gate");
    CHECK(q->gate.wait_until && (int32_t)(q->now - q->gate.wait_until) < 0);
    con("step 1000"); /* (33 s) */
    answer_str(b, sizeof b, q->gate.p.ans);
    type_keys(b);
    ops.event(q, MODX_EV_KEY, K_ENTER);
    CHECK(q->scene == SC_PARENT && q->gate.wrong == 0);
    /* an old streak is forgotten after 10 minutes */
    ops.event(q, MODX_EV_KEY, K_CLEAR);
    con("gate");
    answer_str(b, sizeof b, q->gate.p.ans + 1);
    type_keys(b);
    ops.event(q, MODX_EV_KEY, K_ENTER);
    CHECK(q->gate.wrong == 1);
    ops.event(q, MODX_EV_KEY, K_CLEAR);
    con("step 20000"); /* (11 minutes) */
    con("gate");
    CHECK(q->gate.wrong == 0);
}

/* the console's ways (tests): seed, answer, pass; the grown-ups' screen left alone goes back to her town; the
 * preview's way back needs a grown-up lately */
static void test_console_and_idle(void)
{
    fresh_module();
    con("new Ga 2");
    con("engine stop");
    con("go map");
    con("gate seed 42");
    con("gate");
    int a1 = q->gate.p.ans;
    char t1[64];
    snprintf(t1, sizeof t1, "%s", q->gate.p.text);
    ops.event(q, MODX_EV_KEY, K_CLEAR);
    con("gate seed 42");
    con("gate");
    CHECK(q->gate.p.ans == a1 && !strcmp(t1, q->gate.p.text));
    CHECK(con("gate nonsense") != 0);
    con("gate pass");
    CHECK(q->scene == SC_PARENT);
    con("step 9500"); /* (5+ minutes, no input) */
    CHECK(q->scene == SC_MAP);
    /* nobody at the gate: it closes itself after 2 minutes */
    con("gate");
    con("step 4000");
    CHECK(!q->gate.shown && q->scene == SC_MAP);
}

/* the module's clock is `mq step`'s only. With the host's millis (the wall clock) every event moved it on and
 * seeded the problems: which answer the gate asked changed run to run, and a loaded machine (make -j8 test)
 * slowing the tap sweep past each pause gave its pad taps more tries -- now and then they spelt the answer */
static uint32_t fixed_ms(void) { return 1; }
static modx_api_t gate_api;

int main(void)
{
    gate_api = modx_api;
    gate_api.millis = fixed_ms;
    mq_test_api = &gate_api;
    harness_init();
    test_generator();
    test_opens_only_right();
    test_no_way_round();
    test_cooldown();
    test_console_and_idle();
    printf("test_mq_gate: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
