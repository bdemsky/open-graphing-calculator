/*
 * Math Quest: the Sparkle Kitchen -- a cake counter where cutting and serving are fractions (docs/MATHQUEST.md "The
 * Sparkle Kitchen: fractions of a cake").
 *
 * What she sees: a friend at the counter with an order in a bubble ("3/4 of a cake!"), an empty round plate, a
 * "Give!" button, the oven, the knives (cut in 2, 4; then 3, 6; then 8, as her rung allows) and the counter (6 x 3
 * places). A tap on the oven (one sparkle) bakes a whole cake onto the counter. She picks a knife and taps a piece:
 * it is cut in that many equal slices ("A half cut in 2: two quarters!") -- a whole in 4, or a whole in 2 and then one
 * half in 2. She drags slices onto the plate: each snaps in after the last, so the plate shows its part of a whole
 * cake (no numbers, no total); any slices of any sizes go together there. A slice dragged off the plate goes back to
 * the counter. Pieces never join on the counter: the plate is where they come together.
 *
 * The maths: the order is a problem of S_PARTITION (make n/dn of a shape: halves and quarters; thirds and sixths;
 * more than one slice; eighths) at her rung of its ladder, made by the generator (mq_generate_fresh: the problem
 * histories, no repeats). The plate is her answer: its slices' sizes add up to an amount, right when it is n/dn
 * (any slices that make it: 2/4 is 1/2). Cutting is the equivalence (a half cut in 2 is two quarters); the fewest
 * pieces cut from one cake earn a bonus sparkle ("So neat!"). The first "Give!" of an order is booked once like an
 * order in a round: the problem's history, the review, the success window, the rung, the skill, the strand
 * controller, the error patterns (three eighths for 3/4: the bigger bottom number taken for the bigger piece), the
 * grown-ups' recent orders. A miss: "A little more, please!" or "Oops, that's too much!", one more try; a second
 * miss: the friend shows the cake cut in dn with n pink ("3/4 is 3 of 4 pieces") and she finishes it.
 *
 * The first time (mq_pace_t.demo_seen bit MQ_DEMO_KITCHEN): Pip asks for 1/2 of a cake and the guide's ghost hand
 * shows each step as it comes -- tap the oven, pick the knife that cuts in 2, tap the cake, drag a half onto the
 * plate, tap Give -- with the step's words; that order is never booked as hers. Later an idle hint (or "?" / the
 * help soft key) says the goal and points at the plate (never at the pieces that answer it).
 *
 * The counter never blocks play: the oven bakes one whole cake (one place), and when the counter is full spare
 * pieces go to the cafe (a cafe treat each: its tip jar fills faster; a whole cake pays coins); so does a cut
 * without room; a new order keeps at most KC_KEEP leftovers, the rest to the cafe.
 *
 * Where it goes: coins and hearts for each order, a present every third order (a squishy for her shelf, then a plant
 * for her room), spare pieces and whole cakes to the cafe in town, today's visitor wishes for a whole cake. Fair: the
 * oven's sparkles come only from playing (a right answer, a round), never from a clock; nothing spoils; no timer;
 * cutting is free; leftovers wait for the next order.
 */
#include "mq.h"

enum { TV_DOWN = 1, TV_DRAG, TV_UP, TV_TAP, TV_PINCH, TV_PINCH_END, TV_CANCEL };
enum { HK_UP = 12, HK_LEFT = 13, HK_RIGHT = 14, HK_DOWN = 15, HK_ENTER = 49 };
enum { W_BOARD = 1, W_PLATE, W_GIVE, W_OVEN, W_CAFE, W_FRIEND, W_HELP, W_VISITOR, W_KNIFE };
#define FR(n, d) ((int32_t)(n) * 1000 + (d))
#define CELL 30
#define BX 4
#define BY (MQ_Y0 + 22)
#define RX 190                 /* the counter's right side: the friend, the plate, Give, the cafe */
#define FY (MQ_Y0 + 22)        /* the friend and the bubble */
#define TCX (RX + 30)          /* the plate: a round plate, the cake's place on it a circle */
#define TCY (MQ_Y0 + 114)
#define TR 25
#define GX (RX + 66)           /* Give! */
#define GY (MQ_Y0 + 100)
#define GW (MQ_W - 4 - GX)
#define CY (MQ_Y0 + 152)       /* the cafe */
#define OY (MQ_Y0 + 118)       /* the oven */
#define OW 50
#define KX (BX + OW + 4)       /* the knives */
#define KY (MQ_Y0 + 118)
#define KW (RX - 4 - KX)
#define KH 26
#define SX KX                  /* the words (the guide's steps, what happened) */
#define SY (KY + KH + 4)

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int iabs(int a) { return a < 0 ? -a : a; }
static int gcd(int a, int b) { return b ? gcd(b, a % b) : a; }

/* ------------------------------------------------------------------ the pieces */
static int fam_of(int code) { return code >> 4; }
static int den_of(int code) { return code & 15; }
static bool is_den(int d) { return d == 1 || d == 2 || d == 3 || d == 4 || d == 6 || d == 8; }
static bool is_piece(int code) { return fam_of(code) >= CK_PINK && fam_of(code) <= CK_LEMON && is_den(den_of(code)); }

int mq_merge_value(int code) { return is_piece(code) ? 24 / den_of(code) : 0; }

static const char *nm1(int d) { return d == 1 ? "whole cake" : d == 2 ? "half" : d == 3 ? "third" : d == 4 ? "quarter" : d == 6 ? "sixth" : "eighth"; }
static const char *nm2(int d) { return d == 1 ? "whole cakes" : d == 2 ? "halves" : d == 3 ? "thirds" : d == 4 ? "quarters" : d == 6 ? "sixths" : "eighths"; }
static const char *num_word(int k)
{
    static const char w[9][6] = {"", "one", "two", "three", "four", "five", "six", "seven", "eight"};
    return k >= 0 && k <= 8 ? w[k] : "";
}
const char *mq_merge_name(int code) { return is_piece(code) ? nm1(den_of(code)) : ""; }
static const char *fam_name(int fam) { return fam == CK_LEMON ? "lemon" : "pink"; }
static int fam_for(int dn) { return dn == 3 || dn == 6 ? CK_LEMON : CK_PINK; }

/* the slices a rung's knives make (bits of d: 1/d of a cake): halves and quarters; then thirds and sixths; eighths
 * from the rung that has them (the same ladder as the orders) */
static unsigned rung_dens(int rung) { return rung <= 0 ? 1u << 2 | 1u << 4 : rung <= 2 ? 1u << 2 | 1u << 3 | 1u << 4 | 1u << 6 : 1u << 2 | 1u << 3 | 1u << 4 | 1u << 6 | 1u << 8; }
int mq_merge_knives(int rung, int *k)
{
    static const uint8_t all[5] = {2, 3, 4, 6, 8};
    unsigned m = rung_dens(rung);
    int n = 0;
    for (int i = 0; i < 5; i++)
        if ((m >> all[i]) & 1)
            k[n++] = all[i];
    return n;
}

/* best[v]: the fewest pieces cut from one piece of size s (24ths) whose sizes make v (99: none can) */
static void reach(int s, unsigned dens, uint8_t best[25])
{
    static const uint8_t ks[5] = {2, 3, 4, 6, 8};
    for (int v = 0; v <= 24; v++)
        best[v] = 99;
    best[0] = 0, best[s] = 1;
    for (int i = 0; i < 5; i++) {
        int k = ks[i], c = s / k;
        if (s % k || !c || 24 % c || !((dens >> (24 / c)) & 1))
            continue;
        uint8_t sub[25], acc[25], nx[25];
        reach(c, dens, sub);
        for (int v = 0; v <= 24; v++)
            acc[v] = v ? 99 : 0;
        for (int j = 0; j < k; j++) { /* each slice gives some of its own pieces (or none) */
            for (int v = 0; v <= 24; v++)
                nx[v] = 99;
            for (int a = 0; a <= s; a++)
                if (acc[a] < 99)
                    for (int b = 0; b <= c && a + b <= 24; b++)
                        if (sub[b] < 99 && acc[a] + sub[b] < nx[a + b])
                            nx[a + b] = (uint8_t)(acc[a] + sub[b]);
            for (int v = 0; v <= 24; v++)
                acc[v] = nx[v];
        }
        for (int v = 0; v <= s; v++)
            if (acc[v] < best[v])
                best[v] = acc[v];
    }
}

/* the fewest pieces cut from one cake (with the knives of the rung) that make v24 (0: none can) */
int mq_merge_fewest(int rung, int v24)
{
    if (v24 <= 0 || v24 > 24)
        return 0;
    uint8_t best[25];
    reach(24, rung_dens(rung), best);
    return best[v24] < 99 ? best[v24] : 0;
}

/* ------------------------------------------------------------------ drawing a piece */
static void face(mq_t *q, int cx, int cy, int s)
{
    int g = s > 10 ? 3 : 2;
    mq_fill(q, cx - g, cy, 1, 2, Q_INK), mq_fill(q, cx + g, cy, 1, 2, Q_INK);
    mq_fill(q, cx - 1, cy + 2, 3, 1, Q_INK);
    if (s > 8)
        mq_fill(q, cx - g - 2, cy + 2, 2, 1, Q_BLUSH), mq_fill(q, cx + g + 1, cy + 2, 2, 1, Q_BLUSH);
}
static uint16_t crust_of(int fam) { return fam == CK_PINK ? RGB(232, 150, 176) : RGB(226, 186, 70); }
static uint16_t sponge_of(int fam) { return fam == CK_PINK ? RGB(255, 200, 218) : RGB(255, 236, 140); }

/* a whole cake of its kind (the pink one, the lemon one), r across from its middle */
static void whole_cake(mq_t *q, int fam, int cx, int cy, int r)
{
    mq_circle(q, cx, cy, r, crust_of(fam));
    mq_circle(q, cx, cy - 1, r - 2, sponge_of(fam));
    for (int i = 0; i < 8; i++) /* cream round the edge */
        mq_circle(q, cx + mq_isin(i * 45) * (r - 3) / 1024, cy - 1 - mq_icos(i * 45) * (r - 3) / 1024, r > 9 ? 2 : 1, Q_WHITE);
    mq_circle(q, cx, cy - r / 2, r > 9 ? 3 : 2, fam == CK_PINK ? RGB(236, 70, 80) : RGB(250, 210, 40)); /* a berry, a lemon */
    face(q, cx, cy + 1, r);
}

/* a slice from a0 to a1 degrees (clockwise from 12 o'clock) of a cake of radius r at cx, cy: crust, sponge, a berry */
static void slice(mq_t *q, int fam, int cx, int cy, int r, int a0, int a1, int s)
{
    mq_cake_slice(q, cx, cy, r, a0, a1, crust_of(fam));
    mq_cake_slice(q, cx, cy, r - 2, a0 + 1, a1 - 1, sponge_of(fam));
    int mid = (a0 + a1) / 2, d = r * 3 / 5; /* the middle of the slice: a face on big ones, a berry */
    int mx = cx + mq_isin(mid) * d / 1024, my = cy - mq_icos(mid) * d / 1024;
    if (a1 - a0 >= 90 && r >= 8)
        face(q, mx, my - 1, s);
    else
        mq_circle(q, mx, my, 1, Q_WHITE);
}

/* a piece of cake (CK_CODE(cake, d)) in its place: a faint whole and its part of it (the size is the picture),
 * centred at cx, cy, the whole about 2 s across */
void mq_merge_item(mq_t *q, int code, int cx, int cy, int s)
{
    if (!is_piece(code)) /* (nothing a game path makes: a damaged save, the console -- CR2-18) */
        return;
    int fam = fam_of(code), v = mq_merge_value(code);
    mq_ring(q, cx, cy, s, RGB(214, 194, 208)); /* the whole it is a part of */
    if (v >= 24)
        whole_cake(q, fam, cx, cy, s);
    else
        slice(q, fam, cx, cy, s, 0, v * 15, s);
}

/* a little knife, its tip at the right (x, y: the middle of the handle's end) */
static void knife_icon(mq_t *q, int x, int y)
{
    mq_rrect(q, x, y - 2, 8, 5, 2, Q_HOTPINK);
    mq_fill(q, x + 8, y - 2, 11, 4, RGB(200, 206, 220));
    mq_poly(q, (int16_t[3]){(int16_t)(x + 19), (int16_t)(x + 24), (int16_t)(x + 19)}, (int16_t[3]){(int16_t)(y - 2), (int16_t)(y + 1), (int16_t)(y + 2)}, 3,
            RGB(200, 206, 220));
    mq_fill(q, x + 9, y - 2, 10, 1, Q_WHITE);
}

/* ------------------------------------------------------------------ the order */
static bool demo_seen(mq_t *q) { return (mq_pace(q)->demo_seen >> MQ_DEMO_KITCHEN) & 1; }
static int ord_n(const mq_extra_t *e) { return e->ck_nd & 15; }
static int ord_dn(const mq_extra_t *e) { return e->ck_nd >> 4; }
static int ord_v24(const mq_extra_t *e) { return ord_dn(e) ? ord_n(e) * 24 / ord_dn(e) : 0; }
static bool tutor(mq_t *q) { return (mq_extra(q)->ck_flags & KF_TUTOR) != 0; }
/* the knives' rung: the order's (the demo's: halves and quarters) */
static int knife_rung(mq_t *q) { return tutor(q) ? 0 : imin(mq_extra(q)->ck_rung, 3); }

int mq_merge_order(mq_t *q, int *n, int *dn)
{
    mq_extra_t *e = mq_extra(q);
    if (n)
        *n = ord_n(e);
    if (dn)
        *dn = ord_dn(e);
    return e->ck_who ? e->ck_who - 1 : -1;
}

static void say(mq_t *q, const char *s)
{
    q->api->snprintf(q->mb_say, sizeof q->mb_say, "%s", s);
    q->mb_say_t = mq_mark(q);
}

/* the order in the engine's terms: the generator's problem of S_PARTITION (its identity, rung and levels), told as
 * a fraction to make with any pieces (K_FRAC, equivalent ones right) and the kitchen's own wrong amounts */
static void as_kitchen(mq_prob_t *p, int n, int dn)
{
    p->kind = K_FRAC, p->equiv = 1, p->ans = FR(n, dn);
    p->nwrong = 0;
    if (dn * 2 <= 8) /* the same count of pieces half the size (three eighths for 3/4): the bigger bottom number
                      * taken for the bigger piece */
        p->wrong[p->nwrong] = FR(n, dn * 2), p->wrong_mc[p->nwrong++] = MC_BIG_DENOM;
    if (dn - n > 0 && dn - n != n) /* the rest of the cake */
        p->wrong[p->nwrong] = FR(dn - n, dn), p->wrong_mc[p->nwrong++] = MC_CARELESS;
    p->mc_default = MC_CARELESS;
}

/* the problem of the order on the counter (after a load: made again from its numbers, the same identity) */
static mq_prob_t *order_prob(mq_t *q)
{
    mq_extra_t *e = mq_extra(q);
    mq_prob_t *p = &q->mb_prob;
    int n = ord_n(e), dn = ord_dn(e);
    if (p->skill == S_PARTITION && p->ident && p->ans == FR(n, dn) && p->diff == e->ck_rung)
        return p;
    uint32_t rng = q->rng; /* (the counter's own dice stay as they were) */
    for (int t = 0; t < 200; t++) {
        mq_generate(q, p, S_PARTITION, e->ck_rung);
        if (p->ans == n && p->v[1] == dn)
            break;
    }
    q->rng = rng;
    if (p->ans != n || p->v[1] != dn) { /* (never for an order the counter made; a damaged save: its own identity) */
        mq_generate(q, p, S_PARTITION, e->ck_rung);
        p->ans = n, p->v[1] = (int16_t)dn, p->ident = 0;
    }
    p->ident = mq_prob_ident(p);
    as_kitchen(p, n, dn);
    return p;
}

/* the friend says the order (the bubble shows it as a fraction) */
static void say_order(mq_t *q, bool voice)
{
    mq_extra_t *e = mq_extra(q);
    if (!e->ck_who)
        return;
    char m[56];
    q->api->snprintf(m, sizeof m, "%s would love %d/%d of a cake!", mq_cast_name(q, e->ck_who - 1), ord_n(e), ord_dn(e));
    if (!(e->ck_flags & KF_TUTOR)) /* (the demo's first step says it with what to do) */
        say(q, m);
    if (voice)
        mq_voice(q, e->ck_who - 1, m);
}

/* ------------------------------------------------------------------ the cafe and room on the counter */
/* a piece onto the cafe's table (full: the oldest is eaten) */
static void cafe_put(mq_extra_t *e, int code)
{
    int k = 0;
    while (k < 6 && e->cafe[k])
        k++;
    if (k == 6) {
        for (int i = 0; i < 5; i++)
            e->cafe[i] = e->cafe[i + 1];
        k = 5;
    }
    e->cafe[k] = (uint8_t)code;
}

/* a piece from the counter to the cafe: a treat on its table (tips faster); a whole cake pays coins */
static int to_cafe(mq_t *q, int a, bool quiet)
{
    mq_extra_t *e = mq_extra(q);
    if (a < 0 || a >= KC_CELLS || !e->board[a])
        return -1;
    bool whole = mq_merge_value(e->board[a]) >= 24;
    cafe_put(e, e->board[a]);
    e->board[a] = 0;
    mq_sfx(q, MODX_SFX_COIN, mq_cafe_treats(q));
    if (whole)
        mq_earn(q, 5, "A whole cake for the cafe!");
    else if (!quiet)
        say(q, "Yum! The cafe's tips come faster!");
    q->dirty = true;
    return 0;
}

static int free_cells(mq_t *q)
{
    int n = 0;
    for (int i = 0; i < KC_CELLS; i++)
        n += !mq_extra(q)->board[i];
    return n;
}
static int pieces_on_counter(mq_t *q) { return KC_CELLS - free_cells(q); }

/* spare pieces to the cafe (from the far end of the counter, never the cell keep) until need places are free: the
 * counter never stops the oven or the knife; how many went */
static int make_room(mq_t *q, int need, int keep)
{
    mq_extra_t *e = mq_extra(q);
    int sent = 0;
    for (int i = KC_CELLS - 1; i >= 0 && free_cells(q) < need; i--)
        if (e->board[i] && i != keep)
            to_cafe(q, i, true), sent++;
    if (sent)
        say(q, "Spare pieces went to the cafe!");
    return sent;
}

/* ------------------------------------------------------------------ the next friend */
/* the next friend comes to the counter: someone she has met (not the one just served), with an order at her rung of
 * the ladder (the first time: Pip and 1/2 of a cake, the demo's order); at most KC_KEEP leftovers stay */
void mq_merge_order_new(mq_t *q)
{
    mq_extra_t *e = mq_extra(q);
    mq_profile_t *p = mq_prof(q);
    int who[MQ_NCAST], nw = 0, last = e->ck_pad[0];
    for (int s = 0; s < MQ_NCAST; s++)
        if ((((p->met >> s) & 1) || s == 0) && (s + 1 != last || nw == 0))
            who[nw++] = s;
    int s = demo_seen(q) ? who[mq_rnd(q, 0, nw - 1)] : 0, n = 1, dn = 2, rung = 0, fl = KF_TUTOR;
    if (demo_seen(q) && !q->preview) {
        int top = mq_rungs(S_PARTITION) - 1;
        rung = imin(p->sk[S_PARTITION].diff, top);
        if (!mq_school_taught(q, S_PARTITION)) /* before school's fractions: halves, quarters, thirds and sixths */
            rung = imin(rung, 2);
        mq_generate_fresh(q, &q->mb_prob, S_PARTITION, rung);
        n = (int)q->mb_prob.ans, dn = q->mb_prob.v[1], fl = 0;
        if (dn < 2 || dn > 8 || n < 1 || n >= dn) /* (never: the generator's halves to eighths) */
            n = 1, dn = 4;
        as_kitchen(&q->mb_prob, n, dn);
    } else
        q->mb_prob = (mq_prob_t){0};
    for (int i = KC_CELLS - 1; i >= 0 && pieces_on_counter(q) > KC_KEEP; i--) /* leftovers past KC_KEEP: the cafe */
        if (e->board[i])
            to_cafe(q, i, true);
    e->ck_who = (uint8_t)(s + 1), e->ck_nd = (uint8_t)(n | dn << 4), e->ck_rung = (uint8_t)rung, e->ck_flags = (uint8_t)fl;
    q->mb_t0 = mq_mark_now(q), q->mb_put_t = 0, q->mb_next_t = 0, q->mb_mood = 0, q->mb_knife = 0;
    say_order(q, true);
    q->dirty = true;
}

/* ------------------------------------------------------------------ the counter */
static int free_cell(mq_t *q)
{
    for (int i = 0; i < KC_CELLS; i++)
        if (!mq_extra(q)->board[i])
            return i;
    return -1;
}
static int plate_n(const mq_extra_t *e)
{
    int n = 0;
    while (n < KC_PLATE && e->plate[n])
        n++;
    return n;
}
int mq_merge_plate(mq_t *q)
{
    int v = 0;
    for (int i = 0; i < KC_PLATE; i++)
        v += mq_merge_value(mq_extra(q)->plate[i]);
    return v;
}

/* the merging counter's piece (CK_* x 16 + its level: pink an eighth, a quarter, a half, a whole; lemon a sixth, a
 * third) as a cut piece (CK_* x 16 + d); 0: not one */
static int from_merge_code(int c)
{
    static const uint8_t pink[5] = {0, 8, 4, 2, 1}, lemon[3] = {0, 6, 3};
    int f = c >> 4, l = c & 15;
    if (f == CK_PINK && l >= 1 && l <= 4)
        return CK_CODE(CK_PINK, pink[l]);
    if (f == CK_LEMON && l >= 1 && l <= 2)
        return CK_CODE(CK_LEMON, lemon[l]);
    return 0;
}

/* the saves of older kitchens, once each:
 *  - mb_seen 1, the old merge board (a sugar cube to a royal cake, squishies, flowers) and its wishes: its things go
 *    back as the sparkles that made them (a thing of level L took 2^(L-1); at most 60, as ever), the cafe's treats
 *    become whole cakes (as many: the tip jar as fast), the wishes go;
 *  - mb_seen 2, the merging cake counter (6 x 4, two the same made the next size): each piece as the same piece of
 *    cake, the counter's last row moved up into free places, the plate kept up to a whole cake (the rest back on the
 *    counter); what finds no place goes to the cafe, so nothing she made is lost. */
void mq_merge_migrate(mq_t *q, int prof)
{
    (void)q;
    mq_extra_t *e = &q->save.extra[prof & 1];
    if (e->mb_seen == 1) {
        int back = e->energy;
        for (int i = 0; i < MB_CELLS; i++) {
            int l = e->board[i] & 15;
            if (e->board[i] && l >= 1 && l <= 7)
                back += 1 << (l - 1);
            e->board[i] = 0;
        }
        e->energy = (uint8_t)imin(60, back);
        for (int k = 0; k < 6; k++)
            e->cafe[k] = e->cafe[k] ? (uint8_t)CK_WHOLE(CK_PINK) : 0;
        e->ck_who = e->ck_nd = e->ck_rung = e->ck_flags = 0, e->ck_pad[0] = e->ck_pad[1] = 0;
        for (int k = 0; k < 4; k++)
            e->made[k] = 0;
        for (int k = 0; k < KC_PLATE; k++)
            e->plate[k] = 0;
        e->order_day = 0;
        e->mb_seen = 3;
        return;
    }
    if (e->mb_seen != 2)
        return;
    uint8_t spare[MB_CELLS + KC_PLATE];
    int ns = 0;
    for (int i = 0; i < MB_CELLS; i++) {
        int c = from_merge_code(e->board[i]);
        e->board[i] = 0;
        if (c && i < KC_CELLS)
            e->board[i] = (uint8_t)c;
        else if (c)
            spare[ns++] = (uint8_t)c;
    }
    uint8_t pl[KC_PLATE];
    int np = 0, v = 0;
    for (int k = 0; k < KC_PLATE; k++) {
        int c = from_merge_code(e->plate[k]);
        e->plate[k] = 0;
        if (c && v + mq_merge_value(c) <= 24)
            pl[np++] = (uint8_t)c, v += mq_merge_value(c);
        else if (c)
            spare[ns++] = (uint8_t)c;
    }
    for (int k = 0; k < np; k++)
        e->plate[k] = pl[k];
    for (int k = 0; k < 6; k++)
        e->cafe[k] = (uint8_t)from_merge_code(e->cafe[k]);
    for (int k = 0; k < ns; k++) { /* a free place on the counter, else the cafe */
        int c = 0;
        while (c < KC_CELLS && e->board[c])
            c++;
        if (c < KC_CELLS)
            e->board[c] = spare[k];
        else
            cafe_put(e, spare[k]);
    }
    e->mb_seen = 3;
}

/* a damaged save never indexes the drawing's tables: what no path makes is cleared (CR2-18) */
static void tidy(mq_t *q)
{
    mq_extra_t *e = mq_extra(q);
    for (int i = 0; i < MB_CELLS; i++)
        if (e->board[i] && (i >= KC_CELLS || !is_piece(e->board[i])))
            e->board[i] = 0;
    int k = 0, v = 0;
    for (int i = 0; i < KC_PLATE; i++) /* (the plate in one run, from its first place; a whole cake at most) */
        if (is_piece(e->plate[i]) && v + mq_merge_value(e->plate[i]) <= 24)
            v += mq_merge_value(e->plate[i]), e->plate[k++] = e->plate[i];
    for (; k < KC_PLATE; k++)
        e->plate[k] = 0;
    for (int i = 0; i < 6; i++)
        if (e->cafe[i] && !is_piece(e->cafe[i]))
            e->cafe[i] = 0;
    int n = ord_n(e), dn = ord_dn(e);
    if (e->ck_who && (e->ck_who > MQ_NCAST || dn < 2 || dn > 8 || n < 1 || n >= dn))
        e->ck_who = 0;
}

void mq_merge_open(mq_t *q)
{
    mq_extra_t *e = mq_extra(q);
    mq_merge_migrate(q, q->save.cur);
    e->mb_seen = 3;
    tidy(q);
    q->mb_drag = -1, q->mb_cur = 0, q->mb_pick = -1, q->mb_thanks_t = 0, q->mb_hint_t = 0, q->mb_bake_mask = 0;
    q->mb_say_t = 0, q->mb_tut = 0xFF, q->mb_knife = 0, q->mb_from = -1;
    if (!e->ck_who)
        mq_merge_order_new(q);
    else {
        say_order(q, false);
        if (!q->mb_t0)
            q->mb_t0 = mq_mark_now(q);
    }
    q->dirty = true;
    mq_go(q, SC_MERGE);
    q->wg.focus_id = W_BOARD, q->wg.focus_arg = 0, q->wg.focus = -1; /* (the keys start on the counter) */
}

/* the oven bakes a whole cake of the order's kind onto the counter (one sparkle; the first-time demo's is on the
 * house); a full counter sends a spare piece to the cafe first: baking never fails for room */
int mq_merge_gen(mq_t *q, int g)
{
    mq_extra_t *e = mq_extra(q);
    (void)g;
    if (!e->ck_who)
        mq_merge_order_new(q);
    int fam = fam_for(ord_dn(e));
    bool free_one = tutor(q);
    if (!free_one && !e->energy) {
        say(q, "Play a game to earn sparkles for the oven!");
        mq_sfx(q, MODX_SFX_DUD, 0);
        return -1;
    }
    bool sent = make_room(q, 1, -1) > 0;
    int c = free_cell(q);
    if (c < 0) /* (never: make_room frees one) */
        return -1;
    if (!free_one)
        e->energy--;
    e->board[c] = (uint8_t)CK_WHOLE(fam);
    e->made[2] = (uint8_t)imin(255, e->made[2] + 1);
    q->mb_bake_mask = 1u << c, q->mb_from = -1;
    q->mb_bake_t = mq_mark(q);
    if (!sent) {
        char m[56];
        q->api->snprintf(m, sizeof m, "A whole %s cake, fresh from the oven!", fam_name(fam));
        say(q, m);
    }
    mq_sfx(q, MODX_SFX_POP, 0);
    q->dirty = true;
    return c;
}

/* the piece on cell a cut in k equal slices with the knife: the slices take a's place and the free places nearest
 * it (room made at the cafe if need be); 1 cut, 0 too small for that knife, -1 nothing there */
int mq_merge_cut(mq_t *q, int a, int k)
{
    mq_extra_t *e = mq_extra(q);
    if (a < 0 || a >= KC_CELLS || !is_piece(e->board[a]) || k < 2 || k > 8)
        return -1;
    int code = e->board[a], fam = fam_of(code), d = den_of(code), nd = d * k;
    unsigned dens = rung_dens(knife_rung(q));
    char m[56];
    if (!((dens >> k) & 1)) /* (no such knife at her rung: none is drawn) */
        return 0;
    if (nd > 8 || !((dens >> nd) & 1)) {
        bool any = false;
        for (int j = 2; j <= 8; j++)
            any |= d * j <= 8 && ((dens >> (d * j)) & 1);
        if (any)
            q->api->snprintf(m, sizeof m, "A %s is too tiny to cut in %d!", nm1(d), k);
        else
            q->api->snprintf(m, sizeof m, "A %s is as small as it gets!", nm1(d));
        say(q, m);
        mq_sfx(q, MODX_SFX_DUD, 1);
        return 0;
    }
    make_room(q, k - 1, a);
    e->board[a] = (uint8_t)CK_CODE(fam, nd);
    q->mb_bake_mask = 1u << a;
    for (int j = 1; j < k; j++) { /* the nearest free place for each other slice */
        int best = -1, bd = 999;
        for (int c = 0; c < KC_CELLS; c++)
            if (!e->board[c]) {
                int dd = iabs(c % KC_COLS - a % KC_COLS) + iabs(c / KC_COLS - a / KC_COLS);
                if (dd < bd)
                    bd = dd, best = c;
            }
        if (best < 0) /* (never: room was made) */
            break;
        e->board[best] = (uint8_t)CK_CODE(fam, nd);
        q->mb_bake_mask |= 1u << best;
    }
    if (e->merges < 65535)
        e->merges++;
    mq_count(q, CT_MERGES, 1);
    q->mb_from = (int8_t)a, q->mb_bake_t = mq_mark(q);
    q->mb_pop = (uint8_t)a, q->mb_pop_t = mq_mark(q);
    mq_sfx(q, MODX_SFX_MERGE, k);
    mq_spark_burst(q, BX + (a % KC_COLS) * CELL + CELL / 2, BY + (a / KC_COLS) * CELL + CELL / 2, 8);
    if (d == 1)
        q->api->snprintf(m, sizeof m, "Cut in %d: %s %s!", k, num_word(k), nm2(nd));
    else
        q->api->snprintf(m, sizeof m, "A %s cut in %d: %s %s!", nm1(d), k, num_word(k), nm2(nd));
    say(q, m);
    q->dirty = true;
    return 1;
}

/* the piece on a to cell b: a move, or a swap when b is taken (pieces never join: the plate puts them together) */
int mq_merge_move(mq_t *q, int a, int b)
{
    mq_extra_t *e = mq_extra(q);
    if (a < 0 || b < 0 || a >= KC_CELLS || b >= KC_CELLS || a == b || !e->board[a])
        return -1;
    uint8_t va = e->board[a], vb = e->board[b];
    e->board[a] = vb, e->board[b] = va;
    mq_sfx(q, MODX_SFX_TAP, 0);
    q->dirty = true;
    return 0;
}

/* the piece on cell a onto the plate: it snaps in after the last slice (the plate holds one whole cake) */
int mq_merge_put(mq_t *q, int a)
{
    mq_extra_t *e = mq_extra(q);
    if (a < 0 || a >= KC_CELLS || !e->board[a] || !e->ck_who)
        return -1;
    int k = plate_n(e);
    if (k >= KC_PLATE || mq_merge_plate(q) + mq_merge_value(e->board[a]) > 24) {
        say(q, "The plate holds one cake!");
        mq_sfx(q, MODX_SFX_DUD, 2);
        return -1;
    }
    e->plate[k] = e->board[a], e->board[a] = 0;
    if (!q->mb_put_t)
        q->mb_put_t = mq_mark_now(q);
    mq_sfx(q, MODX_SFX_TAP, 1);
    q->dirty = true;
    return k;
}

/* the plate's slice i back onto cell c (-1: the first free one); the later slices snap back */
static int take_to(mq_t *q, int i, int c)
{
    mq_extra_t *e = mq_extra(q);
    int n = plate_n(e);
    if (i < 0 || i >= n)
        return -1;
    if (c < 0 || c >= KC_CELLS || e->board[c])
        c = free_cell(q);
    if (c < 0) {
        make_room(q, 1, -1);
        c = free_cell(q);
    }
    if (c < 0)
        return -1;
    e->board[c] = e->plate[i];
    for (int k = i; k < KC_PLATE - 1; k++)
        e->plate[k] = e->plate[k + 1];
    e->plate[KC_PLATE - 1] = 0;
    mq_sfx(q, MODX_SFX_TAP, 0);
    q->dirty = true;
    return c;
}
int mq_merge_take(mq_t *q, int i) { return take_to(q, i, -1); }

/* the first answer of an order, booked once like an order in a round (mq_purse.c's counter does the same) */
static void record(mq_t *q, bool right, int32_t given)
{
    mq_extra_t *e = mq_extra(q);
    if (e->ck_flags & KF_TRIED)
        return;
    e->ck_flags |= KF_TRIED;
    if ((e->ck_flags & KF_TUTOR) || q->preview)
        return;
    mq_prob_t *pr = order_prob(q);
    int s = pr->skill;
    uint32_t ms = q->now - (q->mb_put_t ? q->mb_put_t : q->mb_t0); /* (from the first piece on the plate: the baking
                                                                       * and cutting before it are the play) */
    mq_prof(q)->problems++;
    mq_seen_record(q, pr, right, false);
    mq_rev_t *rv = &mq_learn(q)->rev[s];
    if (!rv->box)
        mq_review_join(q, s, false);
    else if ((int16_t)(uint16_t)(rv->due - mq_play_min(q)) <= 0)
        mq_review_answer(q, s, right);
    uint8_t keep = q->prob.skill; /* (the success window, not the round's rung: q->prob is the round's) */
    q->prob.skill = 0xFF;
    mq_coach_answer(q, s, right, ms);
    q->prob.skill = keep;
    mq_rung_answer(q, s, pr->diff, right, false);
    mq_skill_answer(q, s, right, right);
    mq_engine_answered(q, s, !right);
    mq_diag_judge(q, pr, given, right, true); /* (a made answer: no options to pick from) */
    mq_fun_maths_moment(q);
    mq_recent_t *r = &q->recent_o[q->recent_i % MQ_NRECENT]; /* the grown-ups' recent orders */
    q->api->snprintf(r->q, sizeof r->q, "Kitchen: make %d/%d of a cake", ord_n(e), ord_dn(e));
    q->api->snprintf(r->her, sizeof r->her, "%ld/%ld", (long)(given / 1000), (long)(given % 1000));
    r->skill = (uint8_t)s, r->rung = pr->diff, r->right = right, r->why = PW_KITCHEN;
    q->recent_i = (uint8_t)((q->recent_i + 1) % MQ_NRECENT);
    if (q->recent_n < MQ_NRECENT)
        q->recent_n++;
}

/* a present now and then: every third order, a squishy for her shelf, then a plant for her room */
static void present(mq_t *q)
{
    mq_extra_t *e = mq_extra(q);
    mq_home_t *h = mq_home(q);
    e->made[3] = (uint8_t)imin(255, e->made[3] + 1);
    if (e->made[3] % 2) {
        int kind = mq_rnd(q, 0, SQ_KINDS - 1), col = mq_rnd(q, 0, SQ_COLS - 1);
        for (int t = 0; t < 32 && mq_squish_owned(h, kind, col, false); t++)
            kind = (kind + 1) % SQ_KINDS, col = (col + (kind == 0)) % SQ_COLS;
        mq_squish_give(h, kind, col, mq_rnd(q, 0, 4) == 0);
        mq_toast(q, "A present: a new squishy for your shelf!");
        return;
    }
    int best = 0;
    for (int id = 1; id <= MQ_NITEM; id++)
        if (mq_item[id].cat == HC_PLANT && !h->own[id] && mq_item_unlocked(mq_prof(q), id) &&
            (!best || mq_item[id].price < mq_item[best].price))
            best = id;
    if (best) {
        h->own[best]++;
        q->api->snprintf(q->toast_in, sizeof q->toast_in, "A present: a %s for your room!", mq_item[best].name);
        mq_toast(q, q->toast_in);
    } else
        mq_earn(q, 10, "A present from your friends!");
}

/* served: thanks, coins and hearts; the fewest pieces a bonus sparkle; the next friend comes after the thank-you */
static void served(mq_t *q, bool first)
{
    mq_extra_t *e = mq_extra(q);
    int s = e->ck_who - 1, pieces = plate_n(e), few = mq_merge_fewest(e->ck_rung, ord_v24(e));
    bool tut = tutor(q), neat = few && pieces <= few;
    for (int k = 0; k < KC_PLATE; k++)
        e->plate[k] = 0;
    int coins = tut ? 5 : first ? 6 : 2;
    char m[48];
    q->api->snprintf(m, sizeof m, "%s loves it!", mq_cast_name(q, s));
    mq_earn(q, coins, m);
    if (neat && first && !tut) {
        e->made[1] = (uint8_t)imin(255, e->made[1] + 1);
        if (e->energy < 60)
            e->energy++;
        say(q, "So neat! The fewest pieces: a bonus sparkle!");
    } else
        say(q, "Thank you!");
    mq_heart_add(q, s, 2);
    mq_remember(q, MEM_MERGE, s, 0, 0, 1);
    mq_count(q, CT_TREATS, 1);
    mq_quest_event(q, QE_HELP, 1);
    q->mb_thanks = (uint8_t)(s + 1), q->mb_thanks_t = mq_mark(q);
    mq_voice(q, s, "Thank you so much");
    mq_sfx(q, MODX_SFX_CHEER, 1);
    mq_spark_burst(q, RX + 30, FY + 30, 20);
    e->made[0] = (uint8_t)imin(255, e->made[0] + 1);
    if (tut)
        mq_demo_seen(q, MQ_DEMO_KITCHEN);
    else if (e->made[0] % 3 == 0)
        present(q);
    e->ck_pad[0] = e->ck_who; /* (the next friend is someone else) */
    e->ck_who = 0, e->ck_flags = 0;
    q->mb_next_t = mq_mark(q);
    q->mb_pick = -1, q->mb_knife = 0;
    if (q->api->size >= offsetof(modx_api_t, save_soon) + sizeof q->api->save_soon && q->api->save_soon)
        q->api->save_soon();
    q->dirty = true;
}

/* the plate to the friend: 1 right, 0 a miss with one more try, -1 a second miss (shown how) or nothing to give */
int mq_merge_give(mq_t *q)
{
    mq_extra_t *e = mq_extra(q);
    if (!e->ck_who)
        return -1;
    if (!plate_n(e)) {
        say(q, "Put some cake on the plate first!");
        mq_sfx(q, MODX_SFX_DUD, 2);
        return -1;
    }
    int v = mq_merge_plate(q), want = ord_v24(e), g = gcd(v, 24);
    bool right = v == want, first = !(e->ck_flags & (KF_TRIED | KF_MISSED));
    record(q, right, FR(v / g, 24 / g));
    if (right) {
        served(q, first);
        return 1;
    }
    q->mb_mood = 1, q->mb_mood_t = mq_mark(q);
    mq_sfx(q, MODX_SFX_DUD, 3);
    if (!(e->ck_flags & KF_MISSED)) {
        e->ck_flags |= KF_MISSED;
        say(q, v < want ? "Hmm... a little more, please!" : "Oops, that's too much!");
        q->dirty = true;
        return 0;
    }
    e->ck_flags |= KF_SHOWN;
    char m[56];
    q->api->snprintf(m, sizeof m, "Look: %d/%d is %d of %d pieces!", ord_n(e), ord_dn(e), ord_n(e), ord_dn(e));
    say(q, m);
    mq_voice(q, e->ck_who - 1, m);
    q->dirty = true;
    return -1;
}

/* a piece to the cafe: it sits on the cafe's table in town and the tips come faster (full: the oldest is eaten) */
int mq_merge_cafe(mq_t *q, int a) { return to_cafe(q, a, false); }
int mq_cafe_treats(mq_t *q)
{
    int n = 0;
    for (int k = 0; k < 6; k++)
        n += mq_extra(q)->cafe[k] != 0;
    return n;
}

/* the piece on cell a to today's visitor (who wishes for a whole cake: either kind) */
int mq_merge_visitor(mq_t *q, int a)
{
    mq_extra_t *e = mq_extra(q);
    int v = mq_visitor(q, NULL);
    if (v < 0 || a < 0 || a >= KC_CELLS)
        return -1;
    if (mq_merge_value(e->board[a]) < 24) {
        char m[56];
        q->api->snprintf(m, sizeof m, "%s would love a whole cake!", mq_visitor_name(v));
        say(q, m);
        mq_sfx(q, MODX_SFX_DUD, 2);
        return -1;
    }
    e->board[a] = 0;
    mq_visitor_thanks(q);
    mq_spark_burst(q, RX + 22, CY + 10, 24);
    return 0;
}

/* a frame: the next friend after the thank-you; an idle hint */
void mq_merge_frame(mq_t *q)
{
    if (q->scene != SC_MERGE)
        return;
    mq_extra_t *e = mq_extra(q);
    if (!e->ck_who && (!q->mb_next_t || mq_since(q, q->mb_next_t) >= imax(1800, mq_read_ms(q, "Thank you!")))) {
        q->mb_next_t = 0;
        mq_merge_order_new(q);
    }
    if (demo_seen(q) && e->ck_who && q->now - q->last_input > 9000u && !q->mb_hint_t) /* nothing done a while: the goal */
        q->mb_hint_t = mq_mark(q);
}

/* ------------------------------------------------------------------ geometry */
static void cell_xy(int i, int *x, int *y) { *x = BX + (i % KC_COLS) * CELL + CELL / 2, *y = BY + (i / KC_COLS) * CELL + CELL / 2; }
static int find_d(mq_t *q, int d)
{
    for (int i = 0; i < KC_CELLS; i++)
        if (is_piece(mq_extra(q)->board[i]) && den_of(mq_extra(q)->board[i]) == d)
            return i;
    return -1;
}
/* the knives at the rung now: their counts and buttons */
static int knives(mq_t *q, int *k) { return mq_merge_knives(knife_rung(q), k); }
static void knife_box(int i, int n, int *x, int *w)
{
    int bw = imin(40, (KW - 3 * (n - 1)) / n), tot = n * bw + (n - 1) * 3;
    *w = bw, *x = KX + (KW - tot) / 2 + i * (bw + 3);
}
static int knife_x(mq_t *q, int kk) /* the middle of the knife that cuts in kk (-1: none) */
{
    int k[5], n = knives(q, k);
    for (int i = 0; i < n; i++)
        if (k[i] == kk) {
            int x, w;
            knife_box(i, n, &x, &w);
            return x + w / 2;
        }
    return -1;
}
static int give_x(void) { return GX; }
/* the plate's slices: slice i from a0 to a1 degrees (each snaps in after the last) */
static void slice_angles(const mq_extra_t *e, int i, int *a0, int *a1)
{
    int v = 0;
    for (int k = 0; k < i; k++)
        v += mq_merge_value(e->plate[k]);
    *a0 = imin(360, v * 15), *a1 = imin(360, (v + mq_merge_value(e->plate[i])) * 15);
}

/* ------------------------------------------------------------------ the first-time demo and the hints */
enum { ST_NONE, ST_OVEN, ST_KNIFE, ST_CUT, ST_HALF, ST_MORE, ST_BACK, ST_GIVE, ST_GOAL, ST_GOAL_CUT };
/* what to do next and the hand that shows it: the demo's steps (it may show the pieces: the demo's order is never
 * hers), else the goal (pointing at the plate or the knives, never at an answer); ST_NONE: nothing to show */
static int next_step(mq_t *q, char *cap, int n, int *x0, int *y0, int *x1, int *y1, bool *drag)
{
    mq_extra_t *e = mq_extra(q);
    const char *who = e->ck_who ? mq_cast_name(q, e->ck_who - 1) : "";
    int pn = plate_n(e), v = mq_merge_plate(q), want = ord_v24(e), have = pieces_on_counter(q), whole = find_d(q, 1);
    *drag = false;
    if (!e->ck_who)
        return ST_NONE;
    if (!pn && !have) { /* the oven */
        *x0 = *x1 = BX + OW / 2, *y0 = *y1 = OY + 50;
        if (tutor(q))
            q->api->snprintf(cap, (size_t)n, "%s wants 1/2 of a cake. Tap the oven!", who);
        else if (e->energy)
            q->api->snprintf(cap, (size_t)n, "Tap the oven to bake a cake!");
        else
            q->api->snprintf(cap, (size_t)n, "Play a game to earn sparkles for the oven!");
        return ST_OVEN;
    }
    if (tutor(q)) {
        int h = find_d(q, 2), qt = find_d(q, 4);
        if (v == want) {
            *x0 = *x1 = give_x() + GW / 2, *y0 = *y1 = GY + 14;
            q->api->snprintf(cap, (size_t)n, "Now tap Give!");
            return ST_GIVE;
        }
        if (v > want) {
            *x0 = TCX, *y0 = TCY, *x1 = 100, *y1 = BY + 50, *drag = true;
            q->api->snprintf(cap, (size_t)n, "Too much! Drag a piece back.");
            return ST_BACK;
        }
        if (h >= 0) {
            cell_xy(h, x0, y0);
            *x1 = TCX, *y1 = TCY, *drag = true;
            q->api->snprintf(cap, (size_t)n, "Drag a half onto %s's plate!", who);
            return ST_HALF;
        }
        if (qt >= 0) {
            cell_xy(qt, x0, y0);
            *x1 = TCX, *y1 = TCY, *drag = true;
            q->api->snprintf(cap, (size_t)n, "A little more: drag a quarter onto the plate!");
            return ST_MORE;
        }
        if (whole >= 0 && q->mb_knife != 2) {
            *x0 = *x1 = knife_x(q, 2), *y0 = *y1 = KY + KH / 2;
            q->api->snprintf(cap, (size_t)n, "Pick the knife that cuts in 2!");
            return ST_KNIFE;
        }
        if (whole >= 0) {
            cell_xy(whole, x0, y0);
            *x1 = *x0, *y1 = *y0;
            q->api->snprintf(cap, (size_t)n, "Now tap the cake to cut it in 2!");
            return ST_CUT;
        }
        *x0 = *x1 = BX + OW / 2, *y0 = *y1 = OY + 50;
        q->api->snprintf(cap, (size_t)n, "Tap the oven to bake a cake!");
        return ST_OVEN;
    }
    bool only_whole = !pn && whole >= 0;
    for (int i = 0; i < KC_CELLS && only_whole; i++)
        only_whole = !e->board[i] || den_of(e->board[i]) == 1;
    if (only_whole) { /* a whole cake and nothing else: the knives */
        int k[5], nk = knives(q, k), x, w;
        knife_box(0, nk, &x, &w);
        *x0 = x + w / 2, *y0 = KY + KH / 2, *x1 = *x0, *y1 = *y0;
        q->api->snprintf(cap, (size_t)n, "Pick a knife, then tap the cake to cut it!");
        return ST_GOAL_CUT;
    }
    *x0 = 100, *y0 = BY + 45, *x1 = TCX, *y1 = TCY, *drag = true;
    q->api->snprintf(cap, (size_t)n, "Make %d/%d of a cake on the plate, then tap Give!", ord_n(e), ord_dn(e));
    return ST_GOAL;
}

/* the hint now ("?", the help soft key) */
void mq_merge_help(mq_t *q)
{
    q->mb_hint_t = mq_mark(q);
    char cap[64];
    int a, b, c, d;
    bool dr;
    if (next_step(q, cap, sizeof cap, &a, &b, &c, &d, &dr))
        mq_speak(q, 8, cap, MODX_SPEAK_NOW);
    mq_sfx(q, MODX_SFX_TAP, 0);
}

/* ------------------------------------------------------------------ the widgets */
static void layout(mq_t *q)
{
    mq_extra_t *e = mq_extra(q);
    mq_wg_begin(q);
    int i = mq_wg_add(q, WG_AREA, W_BOARD, 0, BX, BY, KC_COLS * CELL, KC_ROWS * CELL);
    i = mq_wg_add(q, WG_BUTTON, W_FRIEND, 0, RX, FY, MQ_W - 4 - RX, 60);
    mq_wg_flags(q, i, WF_CUSTOM);
    i = mq_wg_add(q, WG_BUTTON, W_PLATE, 0, TCX - TR - 4, TCY - TR - 4, 2 * TR + 8, 2 * TR + 8);
    mq_wg_flags(q, i, WF_CUSTOM);
    i = mq_wg_add(q, WG_BUTTON, W_GIVE, 0, give_x(), GY, GW, 28);
    mq_wg_label(q, i, "Give!", Q_PINK);
    if (!plate_n(e) || !e->ck_who)
        mq_wg_flags(q, i, WF_DISABLED);
    i = mq_wg_add(q, WG_BUTTON, W_OVEN, 0, BX, OY, OW, MQ_Y0 + MQ_H - 2 - OY);
    mq_wg_flags(q, i, WF_CUSTOM);
    int k[5], nk = knives(q, k);
    for (int j = 0; j < nk; j++) {
        int x, w;
        knife_box(j, nk, &x, &w);
        i = mq_wg_add(q, WG_BUTTON, W_KNIFE, k[j], x, KY, w, KH);
        mq_wg_flags(q, i, WF_CUSTOM);
    }
    bool vis = mq_visitor(q, NULL) >= 0;
    i = mq_wg_add(q, WG_BUTTON, W_CAFE, 0, vis ? RX + 50 : RX, CY, vis ? MQ_W - 4 - RX - 50 : MQ_W - 4 - RX, MQ_Y0 + MQ_H - 2 - CY);
    mq_wg_flags(q, i, WF_CUSTOM);
    if (vis) {
        i = mq_wg_add(q, WG_BUTTON, W_VISITOR, 0, RX, CY - 4, 44, MQ_Y0 + MQ_H - 2 - CY + 4);
        mq_wg_flags(q, i, WF_CUSTOM);
    }
    i = mq_wg_add(q, WG_BUTTON, W_HELP, 0, 206, MQ_Y0 + 2, 20, 16);
    mq_wg_label(q, i, "?", Q_LAV);
}
static int wg_of(mq_t *q, int id)
{
    for (int i = 0; i < q->wg.n; i++)
        if (q->wg.w[i].id == id)
            return i;
    return -1;
}

/* ------------------------------------------------------------------ drawing */
/* a fraction as at school: the top number over a line over the bottom one, centred at cx */
static void frac_stack(mq_t *q, const font_t *f, int cx, int y, int n, int d, uint16_t c)
{
    char a[4], b[4];
    q->api->snprintf(a, sizeof a, "%d", n);
    q->api->snprintf(b, sizeof b, "%d", d);
    int w = imax(mq_text_w(q, f, a), mq_text_w(q, f, b)) + 4;
    mq_text_c(q, f, cx, y, a, c);
    mq_fill(q, cx - w / 2, y + f->h + 1, w, 2, c);
    mq_text_c(q, f, cx, y + f->h + 4, b, c);
}

/* the oven: a little pink oven with a window, sparkles over it while it can bake */
static void oven(mq_t *q, bool can, bool down)
{
    uint32_t t = q->anim_t;
    int x = BX, y = OY + (down ? 2 : 0), w = OW, h = MQ_Y0 + MQ_H - 2 - OY;
    int wig = can && (t / 180) % 6 == 0 ? 1 : 0;
    mq_rrect(q, x, y + 2 - wig, w, h - 2, 8, Q_PEACH);
    mq_rrect(q, x + 4, y + 6 - wig, w - 8, 10, 4, RGB(255, 236, 214));
    mq_circle(q, x + 12, y + 11 - wig, 2, Q_HOTPINK), mq_circle(q, x + 22, y + 11 - wig, 2, Q_YELLOW), mq_circle(q, x + 32, y + 11 - wig, 2, Q_MINT);
    mq_rrect(q, x + 6, y + 20 - wig, w - 12, h - 28, 5, RGB(214, 120, 90));
    mq_rrect(q, x + 9, y + 23 - wig, w - 18, h - 34, 4, can ? RGB(255, 200, 120) : RGB(236, 210, 200));
    mq_text_c(q, q->api->font_small, x + w / 2, y + 26 - wig, "Bake", Q_INK);
    if (can) {
        mq_star(q, x + w / 2 - 6, y + h - 16 - wig, 4, (t / 300) % 2 ? Q_YELLOW : RGB(255, 240, 170));
        mq_text(q, q->api->font_small, x + w / 2 + 1, y + h - 22 - wig, "1", Q_INK);
    }
}

/* the knives: a button for each count, a little cake cut in that many beside the number; the one in her hand glows */
static void knives_draw(mq_t *q)
{
    int k[5], n = knives(q, k);
    mq_rrect(q, KX - 2, KY - 2, KW + 4, KH + 4, 8, RGB(236, 206, 170));
    for (int i = 0; i < n; i++) {
        int x, w, wi = -1;
        knife_box(i, n, &x, &w);
        for (int j = 0; j < q->wg.n; j++)
            if (q->wg.w[j].id == W_KNIFE && q->wg.w[j].arg == k[i])
                wi = j;
        bool held = q->mb_knife == k[i], dn = wi >= 0 && mq_wg_pressed(q, wi);
        int y = KY + (dn ? 1 : 0);
        mq_rrect(q, x, y, w, KH, 6, held ? Q_YELLOW : Q_WHITE);
        if (q->kbd && wi >= 0 && mq_wg_focused(q) == wi)
            mq_rect(q, x, y, w, KH, Q_PURPLE), mq_rect(q, x + 1, y + 1, w - 2, KH - 2, Q_PURPLE);
        int ccx = x + w / 2 - 5, ccy = y + KH / 2, r = 8;
        mq_circle(q, ccx, ccy, r, crust_of(CK_PINK));
        mq_circle(q, ccx, ccy, r - 1, sponge_of(CK_PINK));
        for (int s = 0; s < k[i]; s++)
            mq_line(q, ccx, ccy, ccx + mq_isin(s * 360 / k[i]) * r / 1024, ccy - mq_icos(s * 360 / k[i]) * r / 1024, Q_BROWN);
        char b[4];
        q->api->snprintf(b, sizeof b, "%d", k[i]);
        mq_text(q, q->api->font_small, ccx + r + 2, ccy - 6, b, Q_INK);
    }
}

/* the friend's bubble: the order as a fraction, a little cake of its kind; after two misses, the cake cut in dn with
 * n pink (the friend shows what it looks like) */
static void bubble(mq_t *q, int s)
{
    mq_extra_t *e = mq_extra(q);
    const modx_api_t *api = q->api;
    int bx = RX + 50, by = FY, bw = MQ_W - 4 - bx, bh = 60, n = ord_n(e), dn = ord_dn(e), fam = fam_for(dn);
    (void)s;
    mq_rrect(q, bx, by, bw, bh, 10, Q_WHITE);
    mq_poly(q, (int16_t[3]){(int16_t)(bx + 1), (int16_t)(bx - 6), (int16_t)(bx + 1)}, (int16_t[3]){(int16_t)(by + 26), (int16_t)(by + 32), (int16_t)(by + 36)}, 3,
            Q_WHITE);
    if (e->ck_flags & KF_SHOWN) { /* shown how: the cake cut in dn, n of them pink */
        int cx = bx + 20, cy = by + 30, r = 15;
        mq_circle(q, cx, cy, r + 1, Q_BROWN);
        mq_circle(q, cx, cy, r, Q_CREAM);
        for (int k = 0; k < n; k++)
            mq_cake_slice(q, cx, cy, r, k * 360 / dn, (k + 1) * 360 / dn, fam == CK_PINK ? RGB(255, 170, 200) : RGB(255, 226, 110));
        for (int k = 0; k < dn; k++)
            mq_line(q, cx, cy, cx + mq_isin(k * 360 / dn) * r / 1024, cy - mq_icos(k * 360 / dn) * r / 1024, Q_BROWN);
        frac_stack(q, api->font_medium, bx + bw - 16, by + 12, n, dn, Q_HOTPINK);
        return;
    }
    frac_stack(q, api->font_large, bx + 20, by + 6, n, dn, Q_HOTPINK);
    mq_text_c(q, api->font_small, bx + bw - 18, by + 4, "of a", Q_INK);
    whole_cake(q, fam, bx + bw - 18, by + 30, 10);
    mq_text_c(q, api->font_small, bx + bw - 18, by + 43, "cake!", Q_INK);
}

/* the plate: a round plate with the cake's place on it, the slices snapped in one after another round from 12
 * o'clock (each its own slice: nothing adds them up in words or numbers) */
static void plate_draw(mq_t *q)
{
    mq_extra_t *e = mq_extra(q);
    mq_circle(q, TCX, TCY + 2, TR + 4, RGB(214, 200, 214));
    mq_circle(q, TCX, TCY, TR + 4, Q_WHITE);
    mq_circle(q, TCX, TCY, TR, RGB(246, 238, 246));
    mq_ring(q, TCX, TCY, TR, RGB(214, 194, 208)); /* where a whole cake goes */
    for (int k = 0; k < KC_PLATE && e->plate[k]; k++) {
        int a0, a1;
        slice_angles(e, k, &a0, &a1);
        if (q->mb_drag == KC_ON_PLATE + k && q->mb_moved) /* (lifted: under the finger) */
            continue;
        if (a1 > a0)
            slice(q, fam_of(e->plate[k]), TCX, TCY, TR, a0, a1, 12);
    }
    for (int k = 0; k < KC_PLATE && e->plate[k]; k++) { /* the cuts between slices */
        int a0, a1;
        slice_angles(e, k, &a0, &a1);
        mq_line(q, TCX, TCY, TCX + mq_isin(a0) * TR / 1024, TCY - mq_icos(a0) * TR / 1024, crust_of(fam_of(e->plate[k])));
        mq_line(q, TCX, TCY, TCX + mq_isin(a1) * TR / 1024, TCY - mq_icos(a1) * TR / 1024, crust_of(fam_of(e->plate[k])));
    }
    if (!plate_n(e) && e->ck_who)
        mq_text_c(q, q->api->font_small, TCX, TCY - 6, "plate", RGB(214, 200, 214));
}

void mq_merge_draw(mq_t *q)
{
    mq_extra_t *e = mq_extra(q);
    const modx_api_t *api = q->api;
    uint32_t t = q->anim_t;
    layout(q);
    mq_fill(q, 0, MQ_Y0, MQ_W, MQ_H, RGB(255, 240, 246));
    for (int i = 0; i < 8; i++) /* a striped kitchen wall */
        mq_fill(q, i * 40 + 20, MQ_Y0, 20, MQ_H, RGB(255, 232, 240));
    mq_fill(q, 0, MQ_Y0, MQ_W, 19, Q_WHITE);
    mq_text(q, api->font_small, 6, MQ_Y0 + 3, "Sparkle Kitchen", Q_HOTPINK);
    char b[24];
    api->snprintf(b, sizeof b, "%d", e->energy); /* the oven's sparkles */
    mq_star(q, 164, MQ_Y0 + 9, 6, (t / 300) % 2 ? Q_YELLOW : RGB(255, 240, 170));
    mq_text(q, api->font_small, 173, MQ_Y0 + 3, b, Q_INK);
    mq_purse_badge(q, MQ_W - 6, MQ_Y0 + 2);
    /* the counter */
    mq_rrect(q, BX - 3, BY - 3, KC_COLS * CELL + 6, KC_ROWS * CELL + 6, 8, RGB(236, 206, 170));
    for (int i = 0; i < KC_CELLS; i++) {
        int x = BX + (i % KC_COLS) * CELL, y = BY + (i / KC_COLS) * CELL;
        mq_rrect(q, x + 1, y + 1, CELL - 2, CELL - 2, 6, ((i % KC_COLS) + (i / KC_COLS)) % 2 ? RGB(255, 250, 240) : RGB(255, 244, 228));
        if (q->kbd && i == q->mb_cur && mq_wg_focused(q) == wg_of(q, W_BOARD))
            mq_rect(q, x + 1, y + 1, CELL - 2, CELL - 2, Q_PURPLE), mq_rect(q, x + 2, y + 2, CELL - 4, CELL - 4, Q_PURPLE);
        if (i == q->mb_pick)
            mq_rrect(q, x + 2, y + 2, CELL - 4, CELL - 4, 6, Q_YELLOW);
    }
    for (int i = 0; i < KC_CELLS; i++) {
        if (!e->board[i] || (i == q->mb_drag && q->mb_moved))
            continue;
        int x, y;
        cell_xy(i, &x, &y);
        if (q->mb_bake_t && ((q->mb_bake_mask >> i) & 1)) { /* out of the oven, or apart from the cut */
            int el = mq_since(q, q->mb_bake_t), dur = q->mb_from < 0 ? 300 : 200, gx, gy;
            if (q->mb_from < 0)
                gx = BX + OW / 2, gy = OY + 20;
            else
                cell_xy(q->mb_from, &gx, &gy);
            if (el < dur) {
                x = gx + (x - gx) * el / dur, y = gy + (y - gy) * el / dur - (q->mb_from < 0 ? (el * (dur - el)) / 1500 : 0);
            }
        }
        mq_merge_item(q, e->board[i], x, y, 13);
        if (q->mb_pop_t && i == q->mb_pop && mq_since(q, q->mb_pop_t) < 260) /* just cut: a ring */
            mq_ring(q, x, y, 12 + mq_since(q, q->mb_pop_t) / 12, Q_YELLOW);
    }
    /* the oven and the knives */
    int wo = wg_of(q, W_OVEN);
    oven(q, e->energy > 0 || tutor(q), mq_wg_pressed(q, wo));
    knives_draw(q);
    /* the friend at the counter and the order */
    if (e->ck_who) {
        int s = e->ck_who - 1;
        bool hmm = q->mb_mood && mq_since(q, q->mb_mood_t) < 1500;
        mq_cast(q, s, RX, FY + 6, 48, hmm ? AN_THINK : plate_n(e) ? AN_LOOK : AN_IDLE);
        bubble(q, s);
    } else if (q->mb_thanks && q->mb_thanks_t) { /* served: a happy friend, "Thank you!" in the bubble, hearts (the
                                                  * counter stays in sight: she can go on) */
        int el = mq_since(q, q->mb_thanks_t), bx = RX + 50;
        mq_cast(q, q->mb_thanks - 1, RX, FY + 6, 48, AN_HAPPY);
        mq_rrect(q, bx, FY, MQ_W - 4 - bx, 60, 10, Q_WHITE);
        mq_text_c(q, api->font_medium, bx + (MQ_W - 4 - bx) / 2, FY + 10, "Thank", Q_HOTPINK);
        mq_text_c(q, api->font_medium, bx + (MQ_W - 4 - bx) / 2, FY + 28, "you!", Q_HOTPINK);
        if (el < 1600)
            mq_heart(q, RX + 44, FY + 30 - el / 80, 5, Q_HOTPINK), mq_heart(q, RX + 8, FY + 40 - el / 100, 4, Q_PINK);
    }
    plate_draw(q);
    /* the cafe counter and today's visitor */
    int vv = mq_visitor(q, NULL), cx0 = vv >= 0 ? RX + 50 : RX, cw = MQ_W - 4 - cx0, ch = MQ_Y0 + MQ_H - 2 - CY;
    mq_rrect(q, cx0, CY, cw, ch, 8, RGB(214, 170, 130));
    mq_rrect(q, cx0 + 3, CY + 3, cw - 6, ch - 19, 6, RGB(255, 246, 236));
    for (int k = 0, m = 0; k < 6; k++)
        if (e->cafe[k])
            mq_merge_item(q, e->cafe[k], cx0 + 10 + m * ((cw - 20) / 5), CY + 17, 6), m++;
    mq_text_c(q, api->font_small, cx0 + cw / 2, CY + ch - 15, "to the cafe", Q_WHITE);
    if (vv >= 0) {
        bool have = false;
        for (int i = 0; i < KC_CELLS; i++)
            have |= mq_merge_value(e->board[i]) >= 24;
        mq_rrect(q, RX, CY - 4, 44, ch + 4, 9, (t / 400) % 2 ? Q_YELLOW : RGB(255, 240, 170));
        mq_visitor_draw(q, vv, RX + 8, CY + ch - 30, 28, have);
        mq_merge_item(q, CK_WHOLE(CK_PINK), RX + 22, CY + 4, 6);
    }
    /* the thing being dragged, under the finger */
    if (q->mb_drag >= KC_KNIFE) {
        if (q->mb_moved)
            knife_icon(q, q->mb_fx - 12, q->mb_fy - 10);
    } else {
        int dc = q->mb_drag >= KC_ON_PLATE ? e->plate[q->mb_drag - KC_ON_PLATE] : q->mb_drag >= 0 ? e->board[q->mb_drag] : 0;
        if (dc && q->mb_moved)
            mq_merge_item(q, dc, q->mb_fx, q->mb_fy - 8, 12);
    }
    /* the words: the demo's step (with the guide and the hand), a hint, what just happened, else the goal */
    char cap[64];
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool drag = false;
    bool tut = !demo_seen(q) && tutor(q) && !q->mb_thanks_t;
    bool hinting = q->mb_hint_t && (uint32_t)mq_since(q, q->mb_hint_t) < (uint32_t)imax(4000, mq_read_ms_len(q, 50));
    int st = (tut || hinting) ? next_step(q, cap, sizeof cap, &x0, &y0, &x1, &y1, &drag) : ST_NONE;
    bool step = st != ST_NONE;
    const char *words = NULL;
    bool said = q->mb_say_t && (uint32_t)mq_since(q, q->mb_say_t) < (uint32_t)imax(2600, mq_read_ms(q, q->mb_say));
    if (step && !(said && tut && mq_since(q, q->mb_say_t) < imin(2400, mq_read_ms(q, q->mb_say)))) /* (what just
                                                                                                    * happened first) */
        words = cap;
    else if (said)
        words = q->mb_say;
    else if (e->ck_who && q->mb_knife) {
        api->snprintf(cap, sizeof cap, "Tap a piece to cut it in %d!", q->mb_knife);
        words = cap;
    } else if (e->ck_who) {
        api->snprintf(cap, sizeof cap, "Make %d/%d of a cake, then tap Give!", ord_n(e), ord_dn(e));
        words = cap;
    }
    int sw = RX - 4 - SX;
    mq_rrect(q, SX, SY, sw, MQ_Y0 + MQ_H - 2 - SY, 8, words == cap && step ? Q_LAV : RGB(255, 250, 236));
    if (words) {
        char lines[3][MQ_TXT];
        int face = words == cap && step ? 16 : 0;
        if (face)
            mq_cast(q, 8, SX + 2, SY + 3, 16, AN_TALK);
        int nl = mq_story_wrap(q, words, lines, sw - 8 - face);
        for (int l = 0; l < 3 && l < nl; l++)
            mq_text(q, api->font_small, SX + 4 + face, SY + 3 + l * 15, lines[l], Q_INK);
    }
    if (step) { /* the guide's ghost hand doing the step (a 1.6 s loop) */
        if (tut && q->mb_tut != (uint8_t)st) /* a new step: said once */
            q->mb_tut = (uint8_t)st, mq_speak(q, 8, cap, MODX_SPEAK_NOW);
        int ph = (int)(t % 1600), x = x0, y = y0, press = 0;
        if (drag) {
            int k = ph < 300 ? 0 : ph > 1300 ? 1000 : ph - 300;
            x = x0 + (x1 - x0) * k / 1000, y = y0 + (y1 - y0) * k / 1000, press = ph > 200 && ph < 1400 ? 16 : 0;
        } else {
            press = ph > 500 && ph < 800 ? 16 : 0;
            if (ph > 600 && ph < 1200)
                mq_ring(q, x, y, 4 + (ph - 600) / 75, Q_WHITE);
        }
        mq_hand(q, x, y, press);
    }
    mq_wg_draw(q);
}

/* ------------------------------------------------------------------ input */
static int cell_at(int x, int y)
{
    if (x < BX || y < BY || x >= BX + KC_COLS * CELL || y >= BY + KC_ROWS * CELL)
        return -1;
    return (y - BY) / CELL * KC_COLS + (x - BX) / CELL;
}
static bool plate_at(int x, int y) /* (the friend or the plate) */
{
    return (x >= RX && y >= FY && y < FY + 62) || (iabs(x - TCX) <= TR + 8 && iabs(y - TCY) <= TR + 8);
}
static bool cafe_at(mq_t *q, int x, int y)
{
    int x0 = mq_visitor(q, NULL) >= 0 ? RX + 50 : RX;
    return x >= x0 && y >= CY - 4;
}
static bool visitor_at(mq_t *q, int x, int y) { return mq_visitor(q, NULL) >= 0 && x >= RX && x < RX + 46 && y >= CY - 8; }
/* the plate's slice under (x, y): by its angle round the plate (-1: none) */
static int plate_piece_at(mq_t *q, int x, int y)
{
    mq_extra_t *e = mq_extra(q);
    int dx = x - TCX, dy = y - TCY, n = plate_n(e);
    if (!n || dx * dx + dy * dy > (TR + 6) * (TR + 6))
        return -1;
    if (dx * dx + dy * dy < 9) /* (the middle: the last slice) */
        return n - 1;
    int ang = mq_angle_of(-dy, dx);
    for (int k = 0; k < n; k++) {
        int a0, a1;
        slice_angles(e, k, &a0, &a1);
        if (ang >= a0 && ang < a1)
            return k;
    }
    return -1;
}
/* the knife's count under (x, y) (0: none) */
static int knife_at(mq_t *q, int x, int y)
{
    int k[5], n = knives(q, k);
    if (y < KY - 4 || y >= KY + KH + 4)
        return 0;
    for (int i = 0; i < n; i++) {
        int bx, w;
        knife_box(i, n, &bx, &w);
        if (x >= bx - 1 && x < bx + w + 2)
            return k[i];
    }
    return 0;
}

/* the knife picked up (or put down when it is the one in her hand) */
static void knife_pick(mq_t *q, int k)
{
    q->mb_pick = -1;
    if (q->mb_knife == k) {
        q->mb_knife = 0;
        mq_sfx(q, MODX_SFX_TAP, 0);
        return;
    }
    q->mb_knife = (uint8_t)k;
    char m[48];
    q->api->snprintf(m, sizeof m, "The knife cuts in %d. Tap a piece!", k);
    say(q, m);
    mq_sfx(q, MODX_SFX_TAP, 2);
}

/* the piece src (a cell, KC_ON_PLATE + its place, or KC_KNIFE + a knife's count) let go at x, y */
static void drop(mq_t *q, int src, int x, int y)
{
    int c = cell_at(x, y);
    if (src >= KC_KNIFE) { /* a knife dragged onto a piece: cut */
        if (c >= 0 && mq_extra(q)->board[c])
            mq_merge_cut(q, c, src - KC_KNIFE);
        return;
    }
    if (src >= KC_ON_PLATE) { /* off the plate: back onto the counter (where it was dropped, if free) */
        if (!plate_at(x, y))
            take_to(q, src - KC_ON_PLATE, c);
        return;
    }
    if (c >= 0 && c != src)
        mq_merge_move(q, src, c);
    else if (plate_at(x, y))
        mq_merge_put(q, src);
    else if (visitor_at(q, x, y))
        mq_merge_visitor(q, src);
    else if (cafe_at(q, x, y))
        mq_merge_cafe(q, src);
}

/* a widget's act: a tap on it, or ENTER while it has the focus */
static void act(mq_t *q, int id, int arg, int x, int y, bool key)
{
    mq_extra_t *e = mq_extra(q);
    int pk = q->mb_pick;
    if (id == W_KNIFE) {
        knife_pick(q, arg);
        return;
    }
    if (id != W_HELP)
        q->mb_knife = 0; /* (anything else done: the knife is put down) */
    switch (id) {
    case W_OVEN: mq_merge_gen(q, 0); break;
    case W_GIVE: mq_merge_give(q); break;
    case W_HELP: mq_merge_help(q); break;
    case W_FRIEND: case W_PLATE: {
        int k = key ? plate_n(e) - 1 : plate_piece_at(q, x, y);
        if (pk >= 0 && pk < KC_ON_PLATE)
            mq_merge_put(q, pk);
        else if (id == W_PLATE && plate_n(e) && k >= 0)
            take_to(q, k, -1);
        else if (e->ck_who) {
            int f = e->ck_flags;
            e->ck_flags &= (uint8_t)~KF_TUTOR; /* (said in the words too) */
            say_order(q, true);
            e->ck_flags = (uint8_t)f;
        }
        break;
    }
    case W_CAFE:
        if (pk >= 0 && pk < KC_ON_PLATE)
            mq_merge_cafe(q, pk);
        else
            say(q, "Spare pieces and whole cakes go to the cafe!");
        break;
    case W_VISITOR:
        if (pk >= 0 && pk < KC_ON_PLATE)
            mq_merge_visitor(q, pk);
        else {
            int v = mq_visitor(q, NULL);
            char m[56];
            q->api->snprintf(m, sizeof m, "%s would love a whole cake!", mq_visitor_name(v));
            say(q, m);
        }
        break;
    default: break;
    }
    q->mb_pick = -1;
}

/* a place on the counter tapped (or ENTER on it): with the knife, cut the piece; else pick a piece, then where it
 * goes */
static void board_tap(mq_t *q, int c)
{
    mq_extra_t *e = mq_extra(q);
    if (c < 0)
        return;
    if (q->mb_knife) {
        if (e->board[c])
            mq_merge_cut(q, c, q->mb_knife);
        return;
    }
    if (q->mb_pick >= 0 && q->mb_pick != c) {
        mq_merge_move(q, q->mb_pick, c);
        q->mb_pick = -1;
    } else if (q->mb_pick == c || !e->board[c])
        q->mb_pick = -1;
    else {
        q->mb_pick = (int8_t)c;
        char m[40];
        int v = mq_merge_value(e->board[c]), g = gcd(v, 24);
        if (v >= 24)
            q->api->snprintf(m, sizeof m, "A whole %s cake!", fam_name(fam_of(e->board[c])));
        else
            q->api->snprintf(m, sizeof m, "A %s: %d/%d of a cake", mq_merge_name(e->board[c]), v / g, 24 / g);
        say(q, m);
        mq_sfx(q, MODX_SFX_TAP, 2);
    }
}

bool mq_merge_touch(mq_t *q, int type, int x, int y)
{
    if (q->scene != SC_MERGE)
        return false;
    mq_extra_t *e = mq_extra(q);
    q->kbd = false;
    layout(q);
    q->mb_hint_t = 0;
    mq_wev_t ev;
    mq_wg_touch(q, type, x, y, &ev);
    switch (type) {
    case TV_DOWN: { /* (only lifts a piece or a knife: a tap acts on its TAP, never on both) */
        int c = cell_at(x, y), kk = knife_at(q, x, y);
        q->mb_drag = -1;
        if (c >= 0 && e->board[c])
            q->mb_drag = (int8_t)c;
        else if (kk)
            q->mb_drag = (int8_t)(KC_KNIFE + kk);
        else {
            int k = plate_piece_at(q, x, y);
            if (k >= 0)
                q->mb_drag = (int8_t)(KC_ON_PLATE + k);
        }
        q->mb_fx = (int16_t)x, q->mb_fy = (int16_t)y, q->mb_moved = 0;
        return true;
    }
    case TV_DRAG:
        if (q->mb_drag >= 0) {
            q->mb_moved |= iabs(x - q->mb_fx) + iabs(y - q->mb_fy) > 3;
            q->mb_fx = (int16_t)x, q->mb_fy = (int16_t)y;
        }
        return true;
    case TV_UP:
        if (q->mb_drag >= 0 && q->mb_moved)
            drop(q, q->mb_drag, x, y), q->mb_pick = -1;
        q->mb_drag = -1;
        return true;
    case TV_CANCEL: q->mb_drag = -1; return true;
    case TV_TAP: {
        q->mb_drag = -1;
        int c = cell_at(x, y);
        if (ev.why == WHY_TAP && c >= 0) /* (the counter's own places first: a knife's wide hit area reaches them) */
            board_tap(q, c);
        else if (ev.why == WHY_TAP && ev.id != W_BOARD)
            act(q, ev.id, ev.arg, x, y, false);
        else
            q->mb_pick = -1;
        return true;
    }
    default: return true;
    }
}

/* keys: the arrows move round the counter (and on to the plate, the oven, the knives ...: the focus), ENTER picks a
 * piece and puts it, cuts with the knife in her hand, or does what the focused thing does; 1 bakes, 0 sends the
 * picked piece to the cafe, CLEAR lets go */
bool mq_merge_key(mq_t *q, int k, int c)
{
    if (q->scene != SC_MERGE)
        return false;
    q->kbd = true;
    q->mb_hint_t = 0;
    layout(q);
    int wb = wg_of(q, W_BOARD), f = mq_wg_focused(q);
    if (f < 0 || (mq_wg(q, f)->flags & WF_DISABLED)) /* (nothing in focus, or Give with an empty plate: the counter) */
        mq_wg_focus(q, wb);
    if (c == '1') {
        mq_merge_gen(q, 0);
        return true;
    }
    if (c == '0' && q->mb_pick >= 0 && q->mb_pick < KC_ON_PLATE) {
        mq_merge_cafe(q, q->mb_pick), q->mb_pick = -1;
        return true;
    }
    int cur = q->mb_cur;
    if (mq_wg_focused(q) == wb) {
        switch (k) {
        case HK_LEFT: q->mb_cur = (uint8_t)(cur % KC_COLS ? cur - 1 : cur); return true;
        case HK_UP: q->mb_cur = (uint8_t)(cur >= KC_COLS ? cur - KC_COLS : cur); return true;
        case HK_RIGHT:
            if (cur % KC_COLS < KC_COLS - 1)
                q->mb_cur = (uint8_t)(cur + 1);
            else
                mq_wg_focus(q, wg_of(q, W_PLATE)); /* off the counter: the plate */
            return true;
        case HK_DOWN:
            if (cur + KC_COLS < KC_CELLS)
                q->mb_cur = (uint8_t)(cur + KC_COLS);
            else if (cur % KC_COLS < 2)
                mq_wg_focus(q, wg_of(q, W_OVEN)); /* below it: the oven, the knives */
            else
                mq_wg_focus(q, wg_of(q, W_KNIFE));
            return true;
        case HK_ENTER: board_tap(q, cur); return true;
        default: break;
        }
    }
    int fw = mq_wg_focused(q), fid = fw >= 0 ? mq_wg(q, fw)->id : 0;
    if (k == HK_UP && (fid == W_KNIFE || fid == W_OVEN)) { /* up from the knives and the oven: the counter's last row */
        mq_wg_focus(q, wb);
        if (q->mb_cur < KC_CELLS - KC_COLS)
            q->mb_cur = (uint8_t)(KC_CELLS - KC_COLS + (fid == W_OVEN ? 0 : 2));
        return true;
    }
    mq_wev_t ev;
    int pk = q->mb_pick; /* (moving the focus keeps the piece picked) */
    if (mq_wg_key(q, k, c, &ev)) {
        q->mb_pick = (int8_t)pk;
        if (ev.why == WHY_KEY && ev.id != W_BOARD)
            act(q, ev.id, ev.arg, ev.x, ev.y, true);
        return true;
    }
    return false;
}

/* CLEAR or back while a piece or a knife is held: it is let go (and she stays) */
bool mq_merge_back(mq_t *q)
{
    if (q->scene != SC_MERGE || (q->mb_pick < 0 && !q->mb_knife))
        return false;
    q->mb_pick = -1, q->mb_knife = 0;
    q->dirty = true;
    return true;
}

/* `mq_kitchen` (tests): who, the order, the plate in 24ths and in pieces, KF_* */
int mq_merge_state(mq_t *q, char *b, int n)
{
    mq_extra_t *e = mq_extra(q);
    return q->api->snprintf(b, (size_t)n, "who=%d,order=%d/%d,plate=%d,pieces=%d,flags=%d", e->ck_who - 1, ord_n(e), ord_dn(e),
                            mq_merge_plate(q), plate_n(e), e->ck_flags);
}

/* `mq merge open | gen | cut A K | knife K | move A B | put A | put CELL CODE | take I | give | cafe A | visitor A |
 * order N D [WHO] | energy N | demo on|off | state` (tests) */
int mq_merge_console(mq_t *q, int argc, char **argv)
{
    const modx_api_t *api = q->api;
    mq_extra_t *e = mq_extra(q);
    long a = 0, b = 0;
    const char *sub = argc > 2 ? argv[2] : "open";
    if (!api->strcmp(sub, "open")) {
        mq_merge_open(q);
        return 0;
    }
    if (!api->strcmp(sub, "gen"))
        return mq_merge_gen(q, 0) >= 0 ? 0 : api->con_error("nothing baked");
    if (!api->strcmp(sub, "cut") && argc > 4 && api->con_parse_int(argv[3], &a) && api->con_parse_int(argv[4], &b))
        return mq_merge_cut(q, (int)a, (int)b) > 0 ? 0 : api->con_error("not cut");
    if (!api->strcmp(sub, "knife") && argc > 3 && api->con_parse_int(argv[3], &a)) {
        q->mb_knife = (uint8_t)(a >= 2 && a <= 8 ? a : 0);
        return 0;
    }
    if (!api->strcmp(sub, "move") && argc > 4 && api->con_parse_int(argv[3], &a) && api->con_parse_int(argv[4], &b))
        return mq_merge_move(q, (int)a, (int)b) >= 0 ? 0 : api->con_error("nothing there");
    if (!api->strcmp(sub, "put") && argc > 4 && api->con_parse_int(argv[3], &a) && api->con_parse_int(argv[4], &b)) {
        if (a < 0 || a >= KC_CELLS) /* (put CELL CODE: a piece on a place) */
            return api->con_error("no such place");
        e->board[a] = (uint8_t)(is_piece((int)b) ? b : 0);
        return 0;
    }
    if (!api->strcmp(sub, "put") && argc > 3 && api->con_parse_int(argv[3], &a))
        return mq_merge_put(q, (int)a) >= 0 ? 0 : api->con_error("not for the plate");
    if (!api->strcmp(sub, "take") && argc > 3 && api->con_parse_int(argv[3], &a))
        return mq_merge_take(q, (int)a) >= 0 ? 0 : api->con_error("nothing there");
    if (!api->strcmp(sub, "give")) {
        int r = mq_merge_give(q);
        api->con_printf("%s\n", r > 0 ? "right" : r == 0 ? "again" : "missed");
        return 0;
    }
    if (!api->strcmp(sub, "cafe") && argc > 3 && api->con_parse_int(argv[3], &a))
        return mq_merge_cafe(q, (int)a) >= 0 ? 0 : api->con_error("nothing there");
    if (!api->strcmp(sub, "visitor") && argc > 3 && api->con_parse_int(argv[3], &a))
        return mq_merge_visitor(q, (int)a) >= 0 ? 0 : api->con_error("not wished for");
    if (!api->strcmp(sub, "order") && argc > 4 && api->con_parse_int(argv[3], &a) && api->con_parse_int(argv[4], &b)) {
        long w = -1; /* (a test's order: as if the generator had made it at the order's rung; WHO: who asks) */
        if (a < 1 || b < 2 || b > 8 || a >= b || (argc > 5 && (!api->con_parse_int(argv[5], &w) || w < 0 || w >= MQ_NCAST)))
            return api->con_error("order N D [WHO]: 1 <= N < D <= 8, WHO a friend's slot");
        e->ck_who = (uint8_t)(w >= 0 ? w + 1 : e->ck_who ? e->ck_who : 1), e->ck_nd = (uint8_t)(a | b << 4), e->ck_flags = 0;
        /* (its rung: the first that has it -- eighths 3, more than one slice 2, thirds and sixths 1; the knives too) */
        e->ck_rung = (uint8_t)(b == 8 ? 3 : a > 1 ? 2 : b == 3 || b == 6 ? 1 : 0);
        q->mb_prob = (mq_prob_t){0};
        order_prob(q);
        say_order(q, false);
        return 0;
    }
    if (!api->strcmp(sub, "energy") && argc > 3 && api->con_parse_int(argv[3], &a)) {
        e->energy = (uint8_t)(a < 0 ? 0 : a > 60 ? 60 : a);
        return 0;
    }
    if (!api->strcmp(sub, "demo") && argc > 3) { /* the first-time demo seen (off) or not yet (on) */
        if (!api->strcmp(argv[3], "on"))
            mq_pace(q)->demo_seen &= ~(1u << MQ_DEMO_KITCHEN);
        else
            mq_demo_seen(q, MQ_DEMO_KITCHEN);
        return 0;
    }
    char sb[96];
    mq_merge_state(q, sb, sizeof sb);
    api->con_printf("%s\n", sb);
    return !api->strcmp(sub, "state") ? 0 : api->con_error("usage: mq merge open|gen|cut A K|knife K|move A B|put A|put CELL CODE|take I|give|cafe A|visitor A|order N D [WHO]|energy N|demo on|off|state");
}
