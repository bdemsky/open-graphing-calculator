/*
 * Math Quest: teaching moments -- a big idea shown in the friend's world, in short parts she can skip
 * (the audit's L6; docs/MATHQUEST.md "Teaching moments").
 *
 * The first time an order needs a big idea (mq_prob_t.idea, set by the generators: groups, sharing,
 * leftovers, place value, carrying, trading, fractions, area, decimals, angles) the friend stops by with
 * it before the game goes on. A moment is a few short parts, each one complication (fractions: fair
 * pieces, the bottom number, the top number, the same amount in different pieces, which is bigger).
 * Every part opens with a try: a picture and a question with two or three answers to tap. Right, and the
 * friend cheers "You already know this!" and the part is skipped; a miss (or "show me") and the part is
 * shown: the picture moves and the friend tells it in two or three lines, said aloud on the Mac. "skip"
 * jumps over a part, "done" goes back to the game at once. The lightbulb can show the order's idea again
 * (its "big idea" button), and when the helper friend has come twice for orders with the same idea, the
 * moment is offered again on the next one. The grown-ups' screen and play log show per idea what was
 * shown, what she knew and what she skipped (mq_pace_t.idea_seg: 2 bits a part).
 *
 * The pictures are drawn here with the module's shape calls (resolution independent: the hr brackets of
 * mq_vis are not needed for plain shapes). Never a lesson screen: no school words, the friend's face.
 */
#include "mq.h"

enum { HK_UP = 12, HK_LEFT = 13, HK_RIGHT = 14, HK_DOWN = 15, HK_DEL = 8, HK_ENTER = 49 };
enum { TV_DOWN = 1, TV_DRAG, TV_UP, TV_TAP };
#define FB (MQ_Y0 + MQ_H)
#define PICY (MQ_Y0 + 64) /* the picture's box */
#define PICH 64
#define KNEW_MS 1500
#define BEAT_MS 6000

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

/* ------------------------------------------------------------------ the parts */
enum { P_NONE, P_BAR, P_BARS, P_GROUPS, P_BLOCKS, P_GRID, P_STRIP, P_ANGLE, P_CAKE };
typedef struct {
    uint8_t pic, a, b, c, d;
} tpic_t;
typedef struct {
    tpic_t tp, sp;        /* the try's picture, the show's */
    char ask[58];
    char ch[3][14];       /* its answers ("" none) */
    uint8_t right;
    char show[3][60];     /* the friend's lines, a beat each */
} tseg_t;
#define NSEG 5

/* P_BAR pieces, pink, unequal; P_BARS a/b over c/d; P_GROUPS groups, each, extra, plates; P_BLOCKS
 * hundreds, tens, ones, mode (1 ten ones glued into a ten, 2 a ten opened into ones, 3 one ten more: the carried one,
 * in yellow); P_GRID across, down, pink (0 all), side numbers; P_STRIP pink of 10 or 100; P_ANGLE degrees, a second
 * one, no square mark (a try that asks about it); P_CAKE a round
 * cake shared by friends (backlog (d)): pieces, pink (handed out), unequal, friends */
static const tseg_t segs[IDEA_COUNT][NSEG] = {
    [IDEA_GROUPS] = {
        {{P_GROUPS, 3, 4, 0, 0}, {P_GROUPS, 3, 4, 0, 0}, "How many in each bag?", {"3", "4", "12"}, 1,
         {"Every bag holds the same: 4 each.", "3 bags of 4: count by 4s, 4, 8, 12!"}},
        {{P_GROUPS, 2, 5, 0, 0}, {P_GROUPS, 2, 5, 0, 0}, "How many in all?", {"7", "10", "25"}, 1,
         {"Count by the bags: 5, 10!", "2 bags of 5 is 10. No need to count one by one."}},
        {{P_GRID, 4, 2, 0, 0}, {P_GRID, 2, 4, 0, 0}, "2 rows of 4. Turned around: 4 rows of 2. The same?",
         {"the same", "different"}, 0, {"Turn it around: still 8!", "2 x 4 and 4 x 2 make the same."}},
    },
    [IDEA_SHARE] = {
        {{P_GROUPS, 3, 2, 0, 3}, {P_GROUPS, 3, 2, 0, 1}, "6 treats on 3 plates, fair. How many each?", {"2", "3", "6"}, 0,
         {"Deal them out: one for you, one for you ...", "Every plate gets 2: a fair share!"}},
        {{P_GROUPS, 4, 3, 0, 3}, {P_GROUPS, 4, 3, 0, 1}, "12 shared by 4 friends. How many each?", {"3", "4", "8"}, 0,
         {"12 dealt to 4 plates: 3 each.", "Check it: 4 plates of 3 is 12!"}},
        {{P_GROUPS, 4, 3, 0, 3}, {P_GROUPS, 4, 3, 0, 1}, "4 x 3 is 12. So 12 shared by 4 is ...", {"3", "12", "16"}, 0,
         {"Sharing goes back the other way:", "4 groups of 3 make 12, so 12 shared by 4 is 3."}},
    },
    [IDEA_PLACE] = {
        {{P_BLOCKS, 0, 1, 0, 0}, {P_BLOCKS, 0, 1, 10, 1}, "How many little cubes in one rod?", {"5", "10", "100"}, 1,
         {"Ten little cubes glued together make a ten rod.", "So one rod is 10!"}},
        {{P_BLOCKS, 0, 3, 4, 0}, {P_BLOCKS, 0, 3, 4, 0}, "Rods and cubes: what number is it?", {"34", "43", "7"}, 0,
         {"3 rods: 30. 4 cubes: 4.", "30 and 4 is 34: tens first, then ones."}},
        {{P_BLOCKS, 0, 5, 2, 0}, {P_BLOCKS, 0, 5, 2, 0}, "In 52, what is the 5 worth?", {"5", "50", "500"}, 1,
         {"The 5 sits in the tens place: 5 rods.", "5 tens is 50!"}},
        {{P_BLOCKS, 4, 0, 6, 0}, {P_BLOCKS, 4, 0, 6, 0}, "4 big squares, no rods, 6 cubes. Which number?", {"406", "46", "460"}, 0,
         {"Each big square is 100: 400.", "No rods: a 0 holds the tens place. 406!"}},
    },
    [IDEA_CARRY] = {
        {{P_BLOCKS, 0, 0, 13, 0}, {P_BLOCKS, 0, 0, 13, 1}, "13 little cubes. Can we glue a ten rod?", {"yes", "no"}, 0,
         {"Glue 10 of them into a rod!", "13 is 1 ten and 3 ones."}},
        {{P_BLOCKS, 0, 0, 13, 0}, {P_BLOCKS, 0, 1, 3, 0}, "6 + 7 is 13. What goes under the ones?", {"3", "13", "1"}, 0,
         {"Write the 3 under the ones.", "The 1 ten hops next door, into the box on top!"}},
        {{P_BLOCKS, 0, 6, 0, 3}, {P_BLOCKS, 0, 6, 0, 3}, "The tens: 2 + 4, and the 1 carried. How many tens?", {"6", "7", "8"}, 1,
         {"2 + 4 is 6, and the carried ten makes 7.", "The little box always joins in!"}},
    },
    [IDEA_TRADE] = {
        {{P_BLOCKS, 0, 4, 2, 0}, {P_BLOCKS, 0, 4, 2, 2}, "42 take away 7. Enough cubes to take 7?", {"yes", "no"}, 1,
         {"Only 2 cubes! Open one ten rod: 10 more cubes.", "Now 3 tens and 12 ones. Take 7: 5 are left."}},
        {{P_BLOCKS, 0, 4, 2, 2}, {P_BLOCKS, 0, 4, 2, 2}, "We opened a rod of 42. How many rods now?", {"3", "4", "5"}, 0,
         {"One rod became cubes, so 4 rods turned into 3.", "That's the little 3 written over the 4!"}},
        {{P_BLOCKS, 4, 0, 0, 0}, {P_BLOCKS, 3, 9, 10, 0}, "400 take away 8: no rods to open. Who trades?",
         {"the 4", "nobody"}, 0,
         {"The hundreds trade one: 10 rods.", "Then a rod opens: 10 cubes.", "400 is 3 hundreds, 9 tens, 10 ones!"}},
    },
    [IDEA_FRACTION] = {
        {{P_CAKE, 4, 0, 1, 4}, {P_CAKE, 4, 0, 0, 4}, "4 friends share a cake. Is it cut fair?", {"fair", "not fair"}, 1,
         {"Some pieces are bigger: not fair!", "Fair pieces are all one size."}},
        {{P_CAKE, 4, 1, 0, 4}, {P_CAKE, 4, 4, 0, 4}, "Shared fairly by 4. How many pieces?", {"1", "3", "4"}, 2,
         {"One piece for each friend: 4 pieces.", "Each friend gets 1 of 4: 1/4!"}},
        {{P_BAR, 4, 3, 0, 0}, {P_BAR, 4, 3, 0, 0}, "How many pieces are pink?", {"1", "3", "4"}, 1,
         {"The top number counts the pink pieces:", "3 pink of 4 pieces is 3/4."}},
        {{P_BARS, 1, 2, 2, 4}, {P_BARS, 1, 2, 2, 4}, "1/2 and 2/4: is the pink the same amount?", {"the same", "different"}, 0,
         {"Line them up: the pink ends at the same spot.", "2/4 is just 1/2 cut into more pieces!"}},
        {{P_BARS, 1, 3, 1, 6}, {P_BARS, 1, 3, 1, 6}, "Which pink piece is bigger?", {"1/3", "1/6"}, 0,
         {"Cut into more pieces, each piece is smaller.", "So 1/3 is bigger than 1/6!"}},
    },
    [IDEA_AREA] = {
        {{P_GRID, 4, 3, 0, 0}, {P_GRID, 4, 3, 0, 0}, "How many squares cover the floor?", {"7", "12", "14"}, 1,
         {"Count the squares inside: 12.", "That's how much floor it covers."}},
        {{P_GRID, 5, 3, 0, 1}, {P_GRID, 5, 3, 0, 1}, "3 rows of 5 squares. How many?", {"8", "15", "16"}, 1,
         {"No need to count one by one:", "3 rows of 5 is 15!"}},
        {{P_GRID, 4, 2, 0, 1}, {P_GRID, 4, 2, 0, 1}, "Does a floor count squares inside, or the edge?",
         {"inside", "around"}, 0, {"Inside is the floor: squares.", "Around the edge is the fence: steps."}},
    },
    [IDEA_DECIMAL] = {
        {{P_STRIP, 3, 10, 0, 0}, {P_STRIP, 3, 10, 0, 0}, "Cut into 10 pieces. How many are pink?", {"3", "7", "10"}, 0,
         {"3 of 10 pieces: 3 tenths.", "We write it 0.3!"}},
        {{P_STRIP, 25, 100, 0, 0}, {P_STRIP, 25, 100, 0, 0}, "100 tiny squares. How many are pink?", {"25", "2", "75"}, 0,
         {"25 of 100: 25 hundredths.", "We write it 0.25."}},
        {{P_BARS, 5, 10, 25, 100}, {P_BARS, 5, 10, 25, 100}, "Which is more pink: 0.5 or 0.25?", {"0.5", "0.25"}, 0,
         {"0.5 is 5 tenths: half of it!", "0.25 is only a quarter. More digits is not more!"}},
    },
    [IDEA_ANGLE] = {
        {{P_ANGLE, 90, 0, 1, 0}, {P_ANGLE, 90, 0, 0, 0}, "Is this a square corner, like a book's?", {"yes", "no"}, 0,
         {"A square corner is 90 degrees.", "Look for the little square in the corner!"}},
        {{P_ANGLE, 45, 120, 0, 0}, {P_ANGLE, 45, 120, 0, 0}, "Which one opens wider?", {"left", "right"}, 1,
         {"The right one opens wider: a bigger angle.", "Long or short arms don't matter, only the turn!"}},
        {{P_ANGLE, 180, 0, 0, 0}, {P_ANGLE, 180, 0, 0, 0}, "How many degrees is a turn all the way around?",
         {"90", "180", "360"}, 2, {"A straight line is half a turn: 180.", "All the way around is 360 degrees!"}},
    },
    [IDEA_LEFTOVER] = {
        {{P_GROUPS, 3, 4, 2, 0}, {P_GROUPS, 3, 4, 2, 0}, "14 in bags of 4. How many are left over?", {"0", "2", "4"}, 1,
         {"3 full bags of 4 is 12 ...", "and 2 left over: not enough for a bag."}},
        {{P_GROUPS, 2, 4, 1, 0}, {P_GROUPS, 2, 4, 1, 0}, "Bags of 4: can 5 be left over?", {"yes", "no"}, 1,
         {"5 left over would fill one more bag!", "Left over is always less than a bag."}},
        {{P_GROUPS, 3, 4, 2, 0}, {P_GROUPS, 4, 4, 0, 0}, "14 friends, 4 in a car. How many cars?", {"3", "4"}, 1,
         {"3 cars take 12, but 2 friends still wait.", "One more car: 4 cars!"}},
    },
};
static const char idea_nm[IDEA_COUNT][14] = {"", "groups", "sharing", "place value", "carrying", "trading",
                                             "fractions", "area", "decimals", "angles", "leftovers"};

const char *mq_idea_name(int idea) { return idea > 0 && idea < IDEA_COUNT ? idea_nm[idea] : ""; }
int mq_teach_nseg(int idea)
{
    int n = 0;
    if (idea > 0 && idea < IDEA_COUNT)
        while (n < NSEG && segs[idea][n].ask[0])
            n++;
    return n;
}
/* (tests) the number of tens a moment's try draws in its blocks (the carried one included), -1 not blocks; whether
 * its angle draws the square mark -- the picture counts what the question names (CR2-16) */
int mq_teach_try_tens(int idea, int seg)
{
    if (idea <= IDEA_NONE || idea >= IDEA_COUNT || seg < 0 || seg >= NSEG || segs[idea][seg].tp.pic != P_BLOCKS)
        return -1;
    const tpic_t *p = &segs[idea][seg].tp;
    return p->b + (p->d == 3 || p->d == 1) - (p->d == 2);
}
bool mq_teach_try_mark(int idea, int seg)
{
    if (idea <= IDEA_NONE || idea >= IDEA_COUNT || seg < 0 || seg >= NSEG)
        return false;
    const tpic_t *p = &segs[idea][seg].tp;
    return p->pic == P_ANGLE && p->a == 90 && !p->c;
}
const char *mq_teach_try_ask(int idea, int seg, const char **right)
{
    if (idea <= IDEA_NONE || idea >= IDEA_COUNT || seg < 0 || seg >= NSEG)
        return "";
    *right = segs[idea][seg].ch[segs[idea][seg].right];
    return segs[idea][seg].ask;
}

static int nch(const tseg_t *s)
{
    int n = 0;
    while (n < 3 && s->ch[n][0])
        n++;
    return n;
}

/* ------------------------------------------------------------------ the record (2 bits a part) */
static void mark(mq_t *q, int idea, int seg, int v)
{
    mq_pace_t *pc = mq_pace(q);
    pc->idea_seg[idea] = (uint16_t)((pc->idea_seg[idea] & ~(3u << (2 * seg))) | ((unsigned)v << (2 * seg)));
    q->dirty = true;
}
int mq_teach_summary(mq_t *q, int idea, int *shown, int *knew, int *skipped)
{
    mq_pace_t *pc = mq_pace(q);
    int a = 0, b = 0, c = 0, n = mq_teach_nseg(idea);
    for (int s = 0; s < n; s++) {
        int v = (pc->idea_seg[idea] >> (2 * s)) & 3;
        a += v == 1, b += v == 2, c += v == 3;
    }
    if (shown)
        *shown = a;
    if (knew)
        *knew = b;
    if (skipped)
        *skipped = c;
    return a + b + c;
}

/* ------------------------------------------------------------------ what the friend says */
static void say(mq_t *q, const char *s)
{
    mq_teach_t *t = &q->teach;
    q->api->snprintf(t->say, sizeof t->say, "%s", s);
    t->say_t = mq_mark(q);
    mq_speak(q, q->prob.ca % MQ_NCAST, s, MODX_SPEAK_NOW);
    q->redraw = true;
}
/* the friend's line, then the try's question (said together; the question is on the page) */
static void say2(mq_t *q, const char *s, const char *ask)
{
    char l[160];
    mq_teach_t *t = &q->teach;
    q->api->snprintf(l, sizeof l, "%s %s", s, ask);
    q->api->snprintf(t->say, sizeof t->say, "%s", s);
    t->say_t = mq_mark(q);
    mq_speak(q, q->prob.ca % MQ_NCAST, l, MODX_SPEAK_NOW);
    q->redraw = true;
}

/* ------------------------------------------------------------------ the flow */
bool mq_teach_on(mq_t *q) { return q->teach.on != 0; }

bool mq_teach_due(mq_t *q)
{
    int idea = q->prob.idea;
    if (idea <= IDEA_NONE || idea >= IDEA_COUNT || q->preview || q->scenes_off || q->scene != SC_PLAY || q->gm.boss)
        return false;
    uint8_t o = mq_pace(q)->idea_offered[idea];
    uint32_t n = (uint32_t)mq_prof(q)->problems;
    if (q->teach.last_p && n + 1 < q->teach.last_p + 3) /* (one moment in three orders at most: the game first) */
        return false;
    return !(o & 0x80) || (o & 0x0F) >= 2;
}
bool mq_teach_has(mq_t *q)
{
    int idea = q->prob.idea;
    return idea > IDEA_NONE && idea < IDEA_COUNT && (mq_pace(q)->idea_offered[idea] & 0x80);
}
void mq_teach_helper(mq_t *q)
{
    int idea = q->prob.idea;
    if (idea <= IDEA_NONE || idea >= IDEA_COUNT)
        return;
    uint8_t *o = &mq_pace(q)->idea_offered[idea];
    if ((*o & 0x0F) < 15)
        (*o)++;
    q->dirty = true;
}

static void open_seg(mq_t *q, const char *intro)
{
    mq_teach_t *t = &q->teach;
    const tseg_t *s = &segs[t->idea][t->seg];
    t->phase = TP_TRY, t->beat = 0, t->picked = -1, t->t = mq_mark(q);
    mq_wgs(q)->focus = -1;
    say2(q, intro, s->ask);
}
static void next_seg(mq_t *q)
{
    mq_teach_t *t = &q->teach;
    if (++t->seg >= t->nseg) {
        say(q, "Now you try it in the game!");
        mq_teach_close(q);
        return;
    }
    open_seg(q, t->phase == TP_KNEW ? "Next one!" : "Try this one!");
}

void mq_teach_start(mq_t *q, int idea, int why)
{
    if (idea <= IDEA_NONE || idea >= IDEA_COUNT || q->scene != SC_PLAY)
        return;
    mq_teach_t *t = &q->teach;
    mq_pace_t *pc = mq_pace(q);
    uint8_t *o = &pc->idea_offered[idea];
    if (why == 2 && pc->idea_replays[idea] < 255)
        pc->idea_replays[idea]++;
    *o = 0x80; /* met; the helper visits counted from here */
    q->dirty = true;
    mq_cols_close(q);
    t->on = 1, t->idea = (uint8_t)idea, t->seg = 0, t->nseg = (uint8_t)mq_teach_nseg(idea), t->why = (uint8_t)why;
    t->knew = t->shown = t->skipped = 0;
    if (why != 2)
        t->last_p = (uint32_t)mq_prof(q)->problems + 1;
    q->gm.bulb_t = 0, q->gm.hud_ok = 0;
    char l[96];
    q->api->snprintf(l, sizeof l, why == 1 ? "Want to see %s once more? Tap skip if not!" : why == 2 ? "The big idea: %s!"
                                                                                                   : "Something new: %s! Let's see.",
                     idea_nm[idea]);
    open_seg(q, l);
}

void mq_teach_close(mq_t *q)
{
    if (!q->teach.on)
        return;
    q->teach.on = 0;
    q->gm.hud_ok = 0;
    mq_wg_reset(q);
    q->prob_start = q->now, q->last_input = q->now; /* (the thinking time starts now) */
    q->redraw = true;
    if (q->scene == SC_PLAY && q->sub == GS_PLAY && mq_cols_auto(q)) /* column work next: the notebook */
        mq_cols_open(q);
}

static void show(mq_t *q)
{
    mq_teach_t *t = &q->teach;
    t->phase = TP_SHOW, t->beat = 0, t->t = mq_mark(q);
    mark(q, t->idea, t->seg, 1);
    t->shown++;
    say(q, segs[t->idea][t->seg].show[0]);
}

bool mq_teach_pick(mq_t *q, int i)
{
    mq_teach_t *t = &q->teach;
    if (!t->on || t->phase != TP_TRY)
        return false;
    const tseg_t *s = &segs[t->idea][t->seg];
    if (i < 0 || i >= nch(s))
        return false;
    if (i == s->right) { /* she knew it: on to the next part */
        t->phase = TP_KNEW, t->t = mq_mark(q), t->picked = -1;
        mark(q, t->idea, t->seg, 2);
        t->knew++;
        mq_sfx(q, MODX_SFX_CHEER, 0);
        say(q, "You already know this!");
        return true;
    }
    t->picked = (int8_t)i; /* not yet: the part, shown */
    mq_sfx(q, MODX_SFX_TAP, 0);
    show(q);
    return true;
}

void mq_teach_skip(mq_t *q)
{
    mq_teach_t *t = &q->teach;
    if (!t->on)
        return;
    if (t->phase == TP_TRY) {
        mark(q, t->idea, t->seg, 3);
        t->skipped++;
    }
    next_seg(q);
}

/* the next beat of a part being shown (after the last: the next part) */
void mq_teach_next(mq_t *q)
{
    mq_teach_t *t = &q->teach;
    if (!t->on)
        return;
    if (t->phase == TP_TRY) { /* "show me" */
        show(q);
        return;
    }
    if (t->phase == TP_KNEW) {
        next_seg(q);
        return;
    }
    const tseg_t *s = &segs[t->idea][t->seg];
    if (t->beat + 1 < 3 && s->show[t->beat + 1][0]) {
        t->beat++, t->t = mq_mark(q);
        say(q, s->show[t->beat]);
        return;
    }
    next_seg(q);
}

void mq_teach_frame(mq_t *q)
{
    mq_teach_t *t = &q->teach;
    if (!t->on)
        return;
    uint32_t el = q->anim_t + 1 - t->t;
    /* a calm pace: it moves on by itself once the friend's line has had its reading time (mq_read.c) */
    int need = t->phase == TP_KNEW ? imax(KNEW_MS, mq_read_ms(q, t->say)) : imax(BEAT_MS, mq_read_ms(q, t->say));
    if ((t->phase == TP_KNEW || t->phase == TP_SHOW) && (int)el >= need)
        mq_read_note(q, el, need, false);
    if (t->phase == TP_KNEW && (int)el >= need)
        next_seg(q);
    else if (t->phase == TP_SHOW && (int)el >= need)
        mq_teach_next(q);
    if (t->on && t->phase == TP_SHOW && el < 1400)
        q->redraw = true; /* (the picture moving) */
}

/* ------------------------------------------------------------------ input */
enum { TW_CH = 210, TW_SKIP, TW_NEXT, TW_DONE };
#define BTN_Y (FB - 32)
static void layout(mq_t *q)
{
    mq_teach_t *t = &q->teach;
    const tseg_t *s = &segs[t->idea][t->seg];
    mq_wg_begin(q);
    int i;
    if (t->phase == TP_TRY) {
        int n = nch(s), w = n == 3 ? 66 : 92;
        for (int k = 0; k < n; k++) {
            i = mq_wg_add(q, WG_CHOICE, TW_CH, k, 6 + k * (w + 4), BTN_Y, w, 30);
            mq_wg_label(q, i, s->ch[k], t->picked == k ? Q_GREY : Q_WHITE);
        }
        i = mq_wg_add(q, WG_BUTTON, TW_NEXT, 0, 216, BTN_Y, 50, 30);
        mq_wg_label(q, i, "show me", Q_YELLOW);
    } else if (t->phase == TP_SHOW) {
        i = mq_wg_add(q, WG_BUTTON, TW_NEXT, 0, 160, BTN_Y, 106, 30);
        mq_wg_label(q, i, "next", Q_MINT);
    }
    i = mq_wg_add(q, WG_BUTTON, TW_SKIP, 0, 270, BTN_Y, 46, 30);
    mq_wg_label(q, i, "skip", Q_PEACH);
    i = mq_wg_add(q, WG_BUTTON, TW_DONE, 0, 262, MQ_Y0 + 44, 54, 16);
    mq_wg_label(q, i, "done", Q_LAV);
}
static void on_ev(mq_t *q, const mq_wev_t *ev)
{
    if (ev->why != WHY_TAP && ev->why != WHY_KEY)
        return;
    if (ev->id == TW_CH)
        mq_teach_pick(q, ev->arg);
    else if (ev->id == TW_SKIP)
        mq_teach_skip(q);
    else if (ev->id == TW_NEXT)
        mq_teach_next(q);
    else if (ev->id == TW_DONE) {
        mq_teach_t *t = &q->teach;
        for (int s = t->seg + (t->phase != TP_TRY); s < t->nseg; s++) /* the rest: skipped */
            if (!((mq_pace(q)->idea_seg[t->idea] >> (2 * s)) & 3) || t->why == 2)
                mark(q, t->idea, s, 3), t->skipped++;
        mq_teach_close(q);
    }
}
bool mq_teach_touch(mq_t *q, int type, int x, int y)
{
    layout(q);
    mq_wev_t ev;
    bool hit = mq_wg_touch(q, type, x, y, &ev);
    if (hit && ev.why != WHY_NONE)
        on_ev(q, &ev);
    else if (!hit && type == TV_TAP && q->teach.phase == TP_KNEW) /* "You already know this!": a tap goes on */
        next_seg(q);
    q->redraw = true;
    return true;
}
bool mq_teach_key(mq_t *q, int k, int ch)
{
    mq_teach_t *t = &q->teach;
    if (t->phase == TP_TRY && ch >= '1' && ch <= '3')
        return mq_teach_pick(q, ch - '1'), true;
    if (k == HK_DEL) {
        mq_teach_skip(q);
        return true;
    }
    layout(q);
    mq_wev_t ev;
    if (mq_wg_key(q, k, ch, &ev)) { /* the arrows walk the buttons, ENTER presses */
        if (ev.why != WHY_NONE)
            on_ev(q, &ev);
        q->redraw = true;
        return true;
    }
    if (k == HK_ENTER)
        mq_teach_next(q);
    q->redraw = true;
    return true;
}

/* ------------------------------------------------------------------ the pictures */
static void cube(mq_t *q, int x, int y, int s, uint16_t c)
{
    mq_fill(q, x, y, s, s, c);
    mq_rect(q, x, y, s, s, Q_BROWN);
}
static void bar(mq_t *q, int x, int y, int w, int h, int parts, int pink, bool unequal, int reveal)
{
    static const uint8_t uw[4] = {40, 14, 30, 16};
    mq_fill(q, x, y, w, h, Q_CREAM);
    int x0 = x, shade = pink * reveal / 1000;
    for (int i = 0; i < parts; i++) {
        int x1 = unequal && parts == 4 ? x0 + w * uw[i] / 100 : x + w * (i + 1) / parts;
        if (i == parts - 1)
            x1 = x + w;
        if (i < shade)
            mq_fill(q, x0, y, x1 - x0, h, Q_PINK);
        if (parts <= 20 || i % 10 == 0)
            mq_fill(q, x0, y, 1, h, Q_BROWN);
        x0 = x1;
    }
    mq_rect(q, x, y, w, h, Q_BROWN);
}
static void pic(mq_t *q, const tpic_t *p, int el)
{
    const modx_api_t *api = q->api;
    int reveal = imin(1000, el);
    int x = 10, y = PICY, w = 300, h = PICH;
    char l[16];
    switch (p->pic) {
    case P_BAR:
        bar(q, x + 30, y + 14, w - 60, 36, p->a, p->b, p->c != 0, reveal);
        break;
    case P_CAKE: { /* a round cake in the middle, the friends around it; the pink pieces handed out one by one */
        int n = imax(1, p->a), cx = x + w / 2, cy = y + h / 2, r = h / 2 - 3, given = p->b * reveal / 1000;
        static const int16_t uneq[5] = {0, 50, 160, 230, 360}; /* an unfair cut: four pieces of four sizes */
        mq_circle(q, cx, cy, r + 2, Q_BROWN);
        mq_circle(q, cx, cy, r, Q_CREAM);
        for (int i = 0; i < n; i++) {
            int a0 = p->c ? uneq[i % 4] : i * 360 / n, a1 = p->c ? uneq[i % 4 + 1] : (i + 1) * 360 / n;
            if (i < given)
                mq_cake_slice(q, cx, cy, r, a0, a1, Q_PINK);
            mq_line(q, cx, cy, cx + mq_isin(a0) * r / 1024, cy - mq_icos(a0) * r / 1024, Q_BROWN);
        }
        for (int i = 0; i < imin(p->d, 4); i++) { /* the friends, two on each side, each with a plate */
            int fx = i < 2 ? x + 30 + i * 46 : x + w - 76 + (i - 2) * 46, fy = y + h - 34;
            mq_sprite(q, (i * 2 + 1) % MQ_NCAST, fx, fy, 1, 1);
            mq_circle(q, fx + 10, y + h - 6, 7, i < given ? Q_PINK : Q_WHITE);
            mq_ring(q, fx + 10, y + h - 6, 7, Q_GREY);
        }
        break;
    }
    case P_BARS:
        for (int k = 0; k < 2; k++) {
            int n = k ? p->c : p->a, d = k ? p->d : p->b;
            bar(q, x + 54, y + 4 + k * 32, w - 70, 24, d, n, false, reveal);
            if (d == 10 || d == 100)
                api->snprintf(l, sizeof l, d == 10 ? "0.%d" : "0.%02d", n);
            else
                api->snprintf(l, sizeof l, "%d/%d", n, d);
            mq_text(q, api->font_medium, x + 4, y + 6 + k * 32, l, Q_PURPLE);
        }
        break;
    case P_GROUPS: { /* bags (or plates) of dots, the extras in orange beside them */
        int g = imax(1, p->a), each = p->b, ex = p->c, tot = g * each + ex, seen = tot * reveal / 1000, k = 0;
        bool pile = p->d & 2; /* a sharing question: the treats in one pile, the plates empty (dealt out on them, the
                               * answer showed: the user, "don't show how many are in each part") */
        int px0 = pile ? 64 : 0;
        if (pile) {
            for (int j = 0; j < tot; j++)
                mq_circle(q, x + 10 + (j % 4) * 13, y + h / 2 - 18 + (j / 4) * 13, 5, Q_HOTPINK);
            x += px0, w -= px0, seen = 0;
        }
        int cw = imin(64, (w - (ex ? 50 : 0)) / g), r = imin(cw / 2 - 4, h / 2 - 4);
        for (int i = 0; i < g; i++) {
            int cx = x + cw * i + cw / 2, cy = y + h / 2;
            if (p->d) { /* a plate */
                mq_circle(q, cx, cy, r + 2, Q_GREY);
                mq_circle(q, cx, cy, r - 2, Q_WHITE);
            } else { /* a bag */
                mq_circle(q, cx, cy, r, Q_PEACH);
                mq_fill(q, cx - 5, cy - r - 4, 10, 6, Q_BROWN);
            }
            for (int j = 0; j < each; j++, k++)
                if (k < seen) {
                    int a = j * 360 / imax(1, each), rr = each == 1 ? 0 : r / 2;
                    mq_circle(q, cx + mq_isin(a) * rr / 1024, cy - mq_icos(a) * rr / 1024, imax(3, r / 6), Q_HOTPINK);
                }
        }
        for (int j = 0; j < ex; j++, k++)
            if (k < seen)
                mq_circle(q, x + cw * g + 14 + (j % 2) * 14, y + h / 2 - 8 + (j / 2) * 14, 5, Q_ORANGE);
        break;
    }
    case P_BLOCKS: { /* big squares (100), rods (10), cubes (1) */
        int cx = x + 6, s = 6, glue = p->d == 1 && reveal >= 600, open = p->d == 2 && reveal >= 600;
        for (int i = 0; i < p->a; i++, cx += 10 * s / 2 + 8) /* (the hundreds drawn at half scale) */
            for (int r = 0; r < 10; r++)
                for (int c = 0; c < 10; c++)
                    mq_fill(q, cx + c * 3, y + 10 + r * 3, 3, 3, (r + c) % 2 ? Q_SKY : RGB(130, 190, 240));
        int tens = p->b - (open ? 1 : 0) + (glue || p->d == 3 ? 1 : 0), ones = p->c + (open ? 10 : 0) - (glue ? 10 : 0);
        for (int i = 0; i < tens; i++, cx += s + 4) /* (the glued or carried ten in yellow: the picture counts what the
                                                     * question names -- CR2-16) */
            for (int r = 0; r < 10; r++)
                cube(q, cx, y + 2 + r * s, s, (glue || p->d == 3) && i == tens - 1 ? Q_YELLOW : Q_MINT);
        cx += 6;
        for (int i = 0; i < ones; i++) {
            bool fresh = open && i >= p->c;
            cube(q, cx + (i % 5) * (s + 3), y + 10 + (i / 5) * (s + 3), s, fresh ? Q_YELLOW : Q_PINK);
        }
        if (p->d == 1 && !glue && p->c >= 10) /* the ten about to be glued */
            mq_rect(q, cx - 2, y + 8, 5 * (s + 3) + 2, 2 * (s + 3) + 2, Q_HOTPINK);
        break;
    }
    case P_GRID: { /* floor squares */
        int c = imax(1, p->a), r = imax(1, p->b), s = imin(18, imin((w - 60) / c, (h - 8) / r));
        int gx = x + (w - c * s) / 2, gy = y + (h - r * s) / 2, n = c * r, pink = p->c ? p->c : n, shade = pink * reveal / 1000;
        for (int i = 0; i < n; i++) {
            int a = i % c, b = i / c;
            mq_fill(q, gx + a * s, gy + b * s, s - 1, s - 1, i < shade ? Q_PINK : Q_CREAM);
        }
        mq_rect(q, gx - 1, gy - 1, c * s + 1, r * s + 1, Q_BROWN);
        if (p->d) {
            api->snprintf(l, sizeof l, "%d", c);
            mq_text_c(q, api->font_small, gx + c * s / 2, gy - 12, l, Q_PURPLE);
            api->snprintf(l, sizeof l, "%d", r);
            mq_text(q, api->font_small, gx - 14, gy + r * s / 2 - 5, l, Q_PURPLE);
        }
        break;
    }
    case P_STRIP: {
        int pink = p->a * reveal / 1000;
        if (p->b == 100) { /* a hundreds grid */
            int s = 6, gx = x + (w - 10 * s) / 2;
            for (int i = 0; i < 100; i++)
                mq_fill(q, gx + (i % 10) * s, y + 2 + (i / 10) * s, s - 1, s - 1, i < pink ? Q_PINK : Q_CREAM);
            mq_rect(q, gx - 1, y + 1, 10 * s + 1, 10 * s + 1, Q_BROWN);
        } else
            bar(q, x + 30, y + 14, w - 60, 36, 10, p->a, false, reveal);
        break;
    }
    case P_ANGLE: { /* one or two angles, the turn drawn as an arc of dots */
        int na = p->b ? 2 : 1;
        for (int k = 0; k < na; k++) {
            int deg = k ? p->b : p->a, vx = x + (na == 2 ? 40 + k * 150 : 110), vy = y + h - 6, len = k ? 44 : 54;
            int d = deg * reveal / 1000;
            mq_line(q, vx, vy, vx + len, vy, Q_INK);
            mq_line(q, vx, vy, vx + mq_icos(d) * len / 1024, vy - mq_isin(d) * len / 1024, Q_INK);
            if (deg == 90 && reveal >= 1000 && !p->c) /* (not in the try that asks about it: CR2-16) */
                mq_rect(q, vx, vy - 10, 10, 10, Q_HOTPINK);
            else
                for (int a = 0; a <= d; a += 6)
                    mq_circle(q, vx + mq_icos(a) * 18 / 1024, vy - mq_isin(a) * 18 / 1024, 1, Q_HOTPINK);
        }
        break;
    }
    default: break;
    }
}

void mq_teach_draw(mq_t *q)
{
    mq_teach_t *t = &q->teach;
    const modx_api_t *api = q->api;
    const tseg_t *s = &segs[t->idea][t->seg];
    mq_fill(q, 0, MQ_Y0 + 16, MQ_W, MQ_H - 16, RGB(240, 248, 255));
    /* the friend and what they say */
    bool talk = t->say_t && q->anim_t - t->say_t < 1600;
    mq_cast(q, q->prob.ca % MQ_NCAST, 2, MQ_Y0 + 17, 26, t->phase == TP_KNEW ? AN_DANCE : talk ? AN_TALK : AN_HAPPY);
    mq_rrect(q, 30, MQ_Y0 + 18, 286, 24, 8, Q_WHITE);
    char l2[3][MQ_TXT];
    int nl = mq_story_wrap(q, t->say, l2, 278);
    for (int i = 0; i < 2 && i < nl; i++)
        mq_text(q, api->font_small, 34, MQ_Y0 + 18 + (nl > 1 ? i * 11 : 5), l2[i], t->phase == TP_KNEW ? Q_GREEN : Q_INK);
    /* the idea's name and its parts (dots: shown pink, known green, skipped grey) */
    char nm[40];
    api->snprintf(nm, sizeof nm, "The big idea: %s", idea_nm[t->idea]);
    mq_text(q, api->font_small, 8, MQ_Y0 + 46, nm, Q_PURPLE);
    for (int k = 0; k < t->nseg; k++) {
        int v = (mq_pace(q)->idea_seg[t->idea] >> (2 * k)) & 3;
        uint16_t c = k == t->seg ? Q_HOTPINK : v == 2 ? Q_GREEN : v == 1 ? Q_PINK : v == 3 ? Q_GREY : Q_WHITE;
        mq_circle(q, 180 + k * 12, MQ_Y0 + 52, 4, c);
        mq_ring(q, 180 + k * 12, MQ_Y0 + 52, 4, Q_DIM);
    }
    /* the picture: still for the try, moving for the show */
    mq_rrect(q, 6, PICY - 4, 308, PICH + 8, 10, Q_WHITE);
    int el = (int)(q->anim_t + 1 - t->t);
    pic(q, t->phase == TP_SHOW ? &s->sp : &s->tp, t->phase == TP_SHOW && t->beat == 0 ? el : 1000);
    /* the try's question stays; the show's lines build up */
    int ty = PICY + PICH + 6;
    if (t->phase == TP_SHOW) {
        for (int b = 0; b <= t->beat && b < 3; b++)
            mq_text_fit(q, api->font_small, 10, ty + b * 11, 302, s->show[b], b == t->beat ? Q_INK : Q_DIM, false);
    } else {
        mq_text_fit(q, api->font_small, 10, ty, 302, s->ask, Q_INK, false);
        if (t->phase == TP_KNEW)
            for (int k = 0; k < 5; k++)
                mq_star(q, 40 + k * 60, ty + 22 + (int)((q->anim_t / 150 + (uint32_t)k) % 3) * 3, 5, Q_YELLOW);
    }
    if (t->phase != TP_KNEW) {
        layout(q);
        mq_wg_draw(q);
    }
}

/* `mq teach [IDEA|pick N|skip|next|done]`: start one, act in it, its record (tests, the console) */
int mq_teach_console(mq_t *q, int argc, char **argv)
{
    const modx_api_t *api = q->api;
    mq_teach_t *t = &q->teach;
    long n = -1;
    if (argc > 2 && !api->strcmp(argv[2], "pick") && argc > 3 && api->con_parse_int(argv[3], &n))
        mq_teach_pick(q, (int)n);
    else if (argc > 2 && !api->strcmp(argv[2], "skip"))
        mq_teach_skip(q);
    else if (argc > 2 && !api->strcmp(argv[2], "next"))
        mq_teach_next(q);
    else if (argc > 2 && !api->strcmp(argv[2], "done")) {
        mq_wev_t ev = {0};
        ev.why = WHY_TAP, ev.id = TW_DONE;
        if (t->on)
            on_ev(q, &ev);
    } else if (argc > 2 && api->con_parse_int(argv[2], &n))
        mq_teach_start(q, (int)n, 2);
    api->con_printf("teach %s idea %d (%s) part %d of %d phase %d beat %d\n", t->on ? "on" : "off", t->idea,
                    mq_idea_name(t->idea), t->seg, t->nseg, t->phase, t->beat);
    for (int i = 1; i < IDEA_COUNT; i++) {
        int a, b, c;
        if (mq_teach_summary(q, i, &a, &b, &c) || mq_pace(q)->idea_offered[i])
            api->con_printf("%s: shown %d, knew %d, skipped %d, replays %d\n", idea_nm[i], a, b, c, mq_pace(q)->idea_replays[i]);
    }
    return 0;
}
