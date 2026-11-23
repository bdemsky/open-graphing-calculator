/* Sparkle Town (modules/mathquest) reading time (mq_read.c; docs/MATHQUEST.md "Reading time"):
 *   - the helper: a base plus a time per character at the pace of her grade (Auto) or the grown-ups' choice,
 *     never under the floor; the quickest pace still under a sixth grader's reading rate; the typewriter slower
 *     than it was (25-35 ms a letter) and quicker than the reading pace (she reads along);
 *   - Auto follows a grade change (school grade, else the starting grade); slower / normal / faster override it;
 *   - the setting on the grown-ups' Setup tab, kept over a reload; an older save (zero there) is Auto; the save's
 *     block keeps its size (the field came from reserved bytes);
 *   - texts that go by themselves last at least their reading time: a toast, the cheer, the helper's step,
 *     measured here against the table (not the helper), and a long session of orders with no taps at all
 *     never has one go too soon (`state mq_read`: the module counts every auto-advance against its need);
 *   - a tap skips: a tap on a toast puts it away and is not a tap on what is under it (DOWN and TAP both
 *     its); a tap on the cheer goes on; a tap on a typewriter's line finishes it, the next moves on;
 *     "You already know this!" goes on at a tap. */
#include "mq_test_util.h"

enum { TV_DOWN = 1, TV_TAP = 4 };
static void touch(int type, int x, int y) { ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(type, x, y)); }
static void tap(int x, int y) { touch(TV_DOWN, x, y), touch(TV_TAP, x, y); }
static void draw(void)
{
    int r[4];
    ops.draw(q, 0, r);
}
/* the table, written out again (an independent check of the helper) */
static int want_ms(int pace, int n)
{
    static const int base[4] = {2000, 1500, 1200, 1000}, per[4] = {125, 90, 75, 60};
    int ms = base[pace] + per[pace] * n;
    return ms < 1500 ? 1500 : ms > 20000 ? 20000 : ms;
}

static void test_helper(void)
{
    fresh_module();
    con("new Ada 3");
    CHECK(mq_pace(q)->read_speed == RS_AUTO);
    CHECK(mq_read_grade(q) == 3 && mq_read_pace(q) == RP_COMFY);
    for (int p = 0; p < RP_COUNT; p++)
        for (int n = 0; n <= 200; n += 7)
            CHECK(mq_read_pace_ms(p, n) == want_ms(p, n));
    CHECK(mq_read_ms(q, "") >= MQ_READ_FLOOR && mq_read_ms(q, "Yay!") >= MQ_READ_FLOOR);
    CHECK(mq_read_ms(q, "Pip moved into town!") == 1500 + 90 * 20);
    CHECK(mq_read_chars("  ab c") == 4);
    /* the quickest still readable: under 17 characters a second (about 100 words a minute) past its base */
    for (int p = 0; p < RP_COUNT; p++)
        CHECK(mq_read_pace_ms(p, 110) - mq_read_pace_ms(p, 10) >= 100 * 1000 / 17);
    /* the paces in order, slowest first */
    for (int p = 1; p < RP_COUNT; p++)
        CHECK(mq_read_pace_ms(p, 40) < mq_read_pace_ms(p - 1, 40));
    /* a typewriter she can follow: slower than before (25 ms), quicker than she reads */
    for (int rs = RS_SLOWER; rs <= RS_FASTER; rs++) {
        mq_pace(q)->read_speed = (uint8_t)rs;
        CHECK(mq_type_ms(q) > 35 && mq_type_ms(q) * 100 < mq_read_ms_len(q, 100) - mq_read_ms_len(q, 0));
    }
    mq_pace(q)->read_speed = RS_AUTO;
}

static void test_auto_and_manual(void)
{
    fresh_module();
    con("new Bo 2");
    mq_set_start_grade(q, 2); /* starts at grade 2: the slowest */
    CHECK(mq_read_pace(q) == RP_SLOWEST);
    mq_set_start_grade(q, 3);
    CHECK(mq_read_pace(q) == RP_COMFY);
    mq_set_start_grade(q, 4);
    CHECK(mq_read_pace(q) == RP_QUICK);
    con("school 5 10"); /* the school grade said: it leads */
    CHECK(mq_read_grade(q) == 5 && mq_read_pace(q) == RP_QUICK);
    con("school 3 10");
    CHECK(mq_read_grade(q) == 3 && mq_read_pace(q) == RP_COMFY);
    int t3 = mq_read_ms(q, "Thank you, that's perfect!");
    con("school 4 20");
    CHECK(mq_read_ms(q, "Thank you, that's perfect!") < t3); /* Auto follows the grade change at once */
    /* the grown-ups' choice overrides the grade */
    con("read slower");
    CHECK(mq_pace(q)->read_speed == RS_SLOWER && mq_read_pace(q) == RP_SLOWEST);
    con("read normal");
    CHECK(mq_read_pace(q) == RP_COMFY);
    con("school 3 10");
    con("read faster");
    CHECK(mq_read_pace(q) == RP_QUICK);
    con("read auto");
    CHECK(mq_read_pace(q) == RP_COMFY);
}

static void test_setting_saved(void)
{
    fresh_module();
    con("new Cy 3");
    /* the button on the grown-ups' Setup tab: a tap, the next */
    con("go parent");
    q->tab = 3;
    q->gate.shown = false;
    draw();
    int bi = -1;
    for (int i = 0; i < q->nbtn; i++)
        if (q->btn[i].id == B_READSPEED)
            bi = i;
    CHECK(bi >= 0);
    if (bi >= 0) {
        const mq_btn_t *b = &q->btn[bi];
        int x = b->x + b->w / 2, y = b->y + b->h / 2;
        for (int k = 0; k < bi; k++) /* (nothing else under it) */
            CHECK(!(x >= q->btn[k].x && x < q->btn[k].x + q->btn[k].w && y >= q->btn[k].y && y < q->btn[k].y + q->btn[k].h));
        tap(x, y);
        CHECK(mq_pace(q)->read_speed == RS_SLOWER);
        draw();
        tap(x, y), draw(), tap(x, y);
        CHECK(mq_pace(q)->read_speed == RS_FASTER);
    }
    mq_save_now(q);
    fresh_module(); /* a reload */
    CHECK(mq_pace(q)->read_speed == RS_FASTER && mq_read_pace(q) == RP_QUICK);
    /* an older save: the byte was reserved (zero): Auto */
    mq_pace(q)->read_speed = 0;
    mq_save_now(q);
    fresh_module();
    CHECK(mq_pace(q)->read_speed == RS_AUTO && mq_read_pace(q) == RP_COMFY);
    CHECK(sizeof(mq_more_t) == 4096); /* the block's size: the field came out of its reserved bytes */
}

static void test_toast(void)
{
    fresh_module();
    con("new Di 3");
    con("go map");
    con("step 5");
    /* it stays its reading time (from when she can see it) */
    const char *s = "Pip moved into town!";
    mq_toast(q, s);
    int frames = 0;
    draw();
    for (; frames < 1000 && q->earn_msg[0] && !q->toast_gone; frames++)
        con("step 1"), draw();
    CHECK(frames * MQ_FRAME_MS >= want_ms(RP_COMFY, (int)strlen(s)) - MQ_FRAME_MS);
    CHECK(frames * MQ_FRAME_MS <= want_ms(RP_COMFY, (int)strlen(s)) + 3 * MQ_FRAME_MS);
    con("step 20");
    /* a tap on it: it goes now, and the finger is not a tap on the street under it */
    mq_toast(q, "Sparkle Town grew!");
    con("step 1"), draw();
    CHECK(q->toast_w > 0);
    int x = q->toast_x + q->toast_w / 2, y = q->lane_y + 8;
    int sc = q->scene, pr = q->pressed, nr = q->read_short;
    touch(TV_DOWN, x, y);
    CHECK(q->toast_tap);
    touch(TV_TAP, x, y);
    CHECK(!q->toast_tap && q->scene == sc && q->pressed == pr);
    con("step 1");
    CHECK(!mq_toast_ms(q) || q->toast_gone || !q->earn_msg[0]);
    CHECK(q->read_short == nr); /* (she tapped: not too soon) */
    /* a tap beside it is the screen's own */
    mq_toast(q, "One more!");
    con("step 10"), draw();
    touch(TV_DOWN, 4, MQ_Y0 + MQ_H - 4);
    CHECK(!q->toast_tap);
    touch(TV_TAP, 4, MQ_Y0 + MQ_H - 4);
}

static void test_cheer_and_tap(void)
{
    fresh_module();
    con("new Ed 3");
    con("scene off");
    con("engine stop");
    q->fun_off = true;
    con("level 0 0");
    con("correct");
    CHECK(q->sub == GS_CHEER && q->gm.say[0]);
    int need = want_ms(RP_COMFY, mq_read_chars(q->gm.say)), frames = 0;
    for (; frames < 1000 && q->sub == GS_CHEER; frames++)
        con("step 1");
    CHECK(frames * MQ_FRAME_MS >= need - MQ_FRAME_MS);
    /* a tap: on at once (the finger that answered is not "on": its release only) */
    con("step 20");
    con("correct");
    CHECK(q->sub == GS_CHEER);
    q->wg.downpick = false;
    int ri = q->run_i;
    tap(160, MQ_Y0 + 150);
    CHECK(q->sub == GS_PLAY && q->run_i == ri + 1 && q->scene == SC_PLAY);
    CHECK(!q->tried_wrong); /* (the tap was not an answer) */
}

static void test_typewriter(void)
{
    fresh_module();
    con("new Fi 3");
    con("scene off");
    con("engine stop");
    q->fun_off = true;
    bool found = false; /* the second miss: the helper friend (an order no way fits: the friend's own steps) */
    for (int w = 0; w < 7 && !found; w++)
        for (int l = 0; l < 3 && !found; l++) {
            char b[24];
            snprintf(b, sizeof b, "level %d %d", w, l);
            con(b);
            con("try");
            con("try");
            found = q->scene == SC_PLAY && q->sub == GS_HELP && !mq_strat_on(q);
        }
    CHECK(found && q->sub == GS_HELP);
    if (q->sub != GS_HELP)
        return;
    if (q->gm.help_step == 0) { /* arriving: a tap hurries it, the next starts the first step */
        tap(40, MQ_Y0 + 150), tap(40, MQ_Y0 + 150);
    }
    CHECK(q->sub == GS_HELP && q->gm.help_step >= 1);
    int st = q->gm.help_step;
    con("step 1");
    CHECK(mq_type_shown(q, q->gm.help_t) < 20); /* typing */
    tap(40, MQ_Y0 + 150); /* finishes the line */
    CHECK(q->gm.help_step == st && mq_type_shown(q, q->gm.help_t) > 200);
    tap(40, MQ_Y0 + 150); /* moves on */
    CHECK(q->gm.help_step == st + 1 || q->sub == GS_PLAY);
    /* left alone, a step stays at least its reading time */
    if (q->sub == GS_HELP) {
        uint32_t t0 = q->gm.help_t;
        int frames = 0;
        for (; frames < 1000 && q->sub == GS_HELP && q->gm.help_t == t0; frames++)
            con("step 1");
        CHECK(frames * MQ_FRAME_MS >= 6000 - MQ_FRAME_MS);
    }
}

/* a long session with no taps at all: every text that went by itself had its reading time */
static void test_session(void)
{
    for (int rs = RS_AUTO; rs <= RS_FASTER; rs++) {
        fresh_module();
        con("new Gia 2");
        mq_pace(q)->read_speed = (uint8_t)rs;
        con("engine stop");
        q->fun_off = false;
        int lv = 0;
        for (int r = 0; r < 6; r++) {
            char b[24];
            snprintf(b, sizeof b, "level %d 0", lv++ % 4);
            con(b);
            for (int k = 0; k < 40 && q->scene == SC_PLAY; k++) {
                if (q->sub == GS_PLAY && !mq_teach_on(q) && !mq_strat_on(q) && !mq_cols_on(q))
                    con(k % 5 == 3 ? "try" : "correct");
                mq_toast(q, k % 2 ? "Skill sparkles!" : "A new friend is coming to Sparkle Town soon!");
                con("step 60");
            }
            con("go map");
            con("step 400");
        }
        if (q->read_short)
            fprintf(stderr, "reading speed %d: %u of %u texts too soon (last: %u ms of %u)\n", rs, q->read_short, q->read_n,
                    q->read_short_got, q->read_short_need);
        CHECK(q->read_short == 0 && q->read_n > 10);
    }
}

int main(void)
{
    harness_init();
    test_helper();
    test_auto_and_manual();
    test_setting_saved();
    test_toast();
    test_cheer_and_tap();
    test_typewriter();
    test_session();
    printf("test_mq_read: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
