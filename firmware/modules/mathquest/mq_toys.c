/*
 * Math Quest: the toy box -- toys and puzzles where the numbers ARE the toy. No timers on
 * thinking, nothing to lose; clever thinking is celebrated in character ("Whoa, you figured out
 * the trick!"). On the map's Arcade screen and as a fun beat (the jam).
 *
 *   Bongo Beats   Puff, a fluffy drum sheep, taps bongos, a cowbell and claps on the beat.
 *                 Notes scroll to the line; hit them on the beat with the keys (1 low bongo,
 *                 3 high bongo, 7 cowbell, 9 clap) or the pads. Clouds bring maths: "7 x 6"
 *                 is played as the digits 4 and 2 on the next beats; count by 3s: "3 6 ? 12";
 *                 halves and quarters of a beat are marked under the notes. Combos make Puff
 *                 go wild. A fixed-tempo clock (mq_beat_*) the web synth follows (music hook
 *                 MODX_MUS_RHYTHM, intensity = the tempo step); perfect / good / miss windows.
 *   Jam           free drumming with Puff, the beat pulsing (a fun beat).
 *   Kaleidoscope  draw with a finger: every stroke is mirrored round the circle (3 to 8 ways:
 *                 the number keys set it). Only new dots are drawn each frame (dirty drawing).
 *   Magic square  fill a 3 x 3 square with 1-9 so every row, column and diagonal makes 15.
 *   Cage puzzle   a 3 x 3 grid, 1-3 in every row and column, cages that add up (KenKen-lite).
 *   Number magic  the characters' tricks: think of a number (always 3!), the 9s on fingers.
 *   Growing dots  triangle, square and star numbers growing step by step.
 *   Mandala       colour the rings; each colour's share is shown as a fraction ("3/8 pink").
 *   Secret code   a friend's message in numbers; the code wheel's rule turns them into letters.
 */
#include "mq.h"

enum { HK_UP = 12, HK_LEFT = 13, HK_RIGHT = 14, HK_DOWN = 15, HK_DEL = 8, HK_ENTER = 49 };
enum { TV_DOWN = 1, TV_DRAG, TV_UP, TV_TAP };
static const char toy_name[TOY_COUNT][16] = {"Toy box",      "Bongo Beats", "Jam!",         "Kaleidoscope",
                                             "Magic square", "Cage puzzle", "Number magic", "Growing dots",
                                             "Mandala",      "Secret code"};
const char *mq_toy_name(int t) { return toy_name[t >= 0 && t < TOY_COUNT ? t : 0]; }

static int imin(int a, int b) { return a < b ? a : b; }
static int iabs(int a) { return a < 0 ? -a : a; }

#define T (&q->toy)
#define FY (MQ_Y0 + 20)

static void clever(mq_t *q, const char *say)
{
    q->api->snprintf(T->say, sizeof T->say, "%s", say);
    T->say_t = mq_mark(q);
    mq_spark_burst(q, 160, MQ_Y0 + 80, 22);
    mq_sfx(q, MODX_SFX_CHEER, 7);
    mq_quest_event(q, QE_TOY, 1);
}

/* ------------------------------------------------------------------ the beat clock */
/* a fixed tempo: beats per minute by step (the web synth plays MODX_MUS_RHYTHM at the same step) */
int mq_beat_bpm(int step) { return step <= 0 ? 90 : step == 1 ? 110 : 130; }
uint32_t mq_beat_ms(int step) { return 60000u / (uint32_t)mq_beat_bpm(step); }
/* a hit dt ms off its note: 2 perfect, 1 good, 0 a miss */
int mq_beat_judge(int32_t dt)
{
    dt = dt < 0 ? -dt : dt;
    return dt <= 60 ? 2 : dt <= 130 ? 1 : 0;
}

static const char drum_key[4] = {'1', '3', '7', '9'}; /* the jam's drums on the keys */
enum { NT_LO = 0, NT_HI, NT_BELL, NT_CLAP, NT_DIGIT = 10, NT_ORDER = 20 };
#define BEATS_BARS 8      /* orders in a song */
#define BEATS_DEMO 24     /* mq_pace_t.demo_seen bit of Bongo Beats' first-time demo (past the games' speed ids) */
#define DEMO_LEN 3200
#define PAD_Y (MQ_Y0 + 156)
#define PAD_H 40
static int pad_x(int i) { return 8 + i * 77; }
/* a beat's ms: the song's tempo (Bongo Beats), the tempo step's (the jam) */
static uint32_t beats_bm(const mq_toy_t *t) { return t->song_bpm ? 60000u / t->song_bpm : mq_beat_ms(t->tempo); }

/* ---- Bongo Beats: drum the answer. Each bar brings an order (a fact "7 x 6", or a skip-count run
 * "5 10 ? 20") on a cloud that rides its note to the hit line; the four bongos carry four answers
 * (one right, the others the usual slips); she hits the right one on the beat. One hit an order
 * counts. A note nobody hit waits at the line (the song waits for the maths); a wrong drum is a soft
 * thud, the answer shows after the beat and she drums it to go on (no points). Hits away from a note
 * fizzle; mashing (four hits within a second, or fizzle after fizzle) pauses the song: the guide
 * points at the order ("Listen! ...") and only its drum stays lit until she drums it, calmly. Points,
 * combos, the song's end and its coins come only from right answers. Jam (the toy box's "Jam!", the
 * reward after a song, a fun beat) is the free drumming: anything goes there. */

/* the answers of an order: the right one and three slips (a group more or less, a near fact, the sum),
 * distinct and positive, the right one at a random drum */
static void beats_cands(mq_t *q, int o, int ans, const int *slip, int nslip)
{
    mq_toy_t *t = T;
    int c[4] = {ans, 0, 0, 0}, n = 1;
    for (int i = 0; i < nslip && n < 4; i++) {
        bool dup = slip[i] <= 0;
        for (int k = 0; k < n; k++)
            dup |= c[k] == slip[i];
        if (!dup)
            c[n++] = slip[i];
    }
    for (int d = 1; n < 4; d++) { /* (filled with neighbours) */
        int v = ans + (d % 2 ? (d + 1) / 2 : -(d / 2));
        bool dup = v <= 0;
        for (int k = 0; k < n; k++)
            dup |= c[k] == v;
        if (!dup)
            c[n++] = v;
    }
    int r = mq_rnd(q, 0, 3);
    c[0] = c[r], c[r] = ans;
    for (int i = 0; i < 4; i++)
        t->ocand[o][i] = (int16_t)c[i];
    t->oright[o] = (uint8_t)r, t->oans[o] = (int16_t)ans;
}

/* the order of bar `bar` and its note (the bar's first beat) */
static void beats_bar(mq_t *q, int bar)
{
    mq_toy_t *t = T;
    uint32_t bm = beats_bm(t), at = (uint32_t)(bar * 4 + 4) * bm; /* bar 0 after a count-in */
    int o = bar & 1;
    t->onterm[o] = 0, t->owrong[o] = 0, t->ocue[o][0] = 0;
    /* a song of one table (the times-table run: its facts, the others shuffled, then counting by it), else a mix */
    int f = t->song_table ? (q->trun_t == t->song_table + 1 && mq_rnd(q, 0, 3) < 3 ? mq_fact_run_next(q) : -1)
                          : mq_rnd(q, 0, 2) < 2 ? mq_fact_next(q) : -1;
    if (f >= 0) { /* a fact */
        int a, b;
        bool dv;
        mq_fact_decode(f, &a, &b, &dv);
        int ans = mq_fact_answer(f);
        t->ofact[o] = (int16_t)f;
        q->api->snprintf(t->ocue[o], sizeof t->ocue[o], dv ? "%d \xC3\xB7 %d" : "%d \xC3\x97 %d", dv ? a * b : a, b);
        int sl[5];
        if (dv)
            sl[0] = ans + 1, sl[1] = ans - 1, sl[2] = b, sl[3] = a * b - b, sl[4] = ans + 2;
        else
            sl[0] = (a + 1) * b, sl[1] = (a - 1) * b, sl[2] = a * (b + 1), sl[3] = a + b, sl[4] = ans + 1;
        beats_cands(q, o, ans, sl, 5);
    } else { /* skip counting along the beat: the terms on beats, one of them "?" */
        int s = t->song_table ? t->song_table : mq_rnd(q, 2, 9), k = mq_rnd(q, 1, 3); /* (the unknown: the 2nd, 3rd or 4th term) */
        int m = mq_rnd(q, 1, 7); /* (from anywhere up to ten times: 21 28 ? 42) */
        t->ofact[o] = -1, t->onterm[o] = 4, t->ohole[o] = (uint8_t)k;
        for (int i = 0; i < 4; i++)
            t->oterm[o][i] = (int16_t)(s * (m + i));
        int ans = s * (m + k), sl[4] = {ans + 1, ans + s, ans - s + 1, ans - 1};
        beats_cands(q, o, ans, sl, 4);
    }
    if (t->nn < TOY_NOTES)
        t->note[t->nn++] = (mq_note_t){at, NT_ORDER, 0, 0, (uint8_t)bar};
}

static bool beats_demo_seen(mq_t *q) { return (mq_pace(q)->demo_seen >> BEATS_DEMO) & 1; }

static void beats_feed(mq_t *q);
static void beats_start(mq_t *q, int mode)
{
    mq_toy_t *t = T;
    t->mode = (uint8_t)mode, t->t0 = q->anim_t, t->nn = 0, t->score = 0, t->combo = 0, t->best_combo = 0;
    t->perfect = t->good = t->miss = 0, t->cue_bar = -1, t->bars = 0, t->done = 0, t->new_best = 0;
    t->hold = t->calm = t->nfizz = 0, t->start_t = q->anim_t, t->demo_t = 0;
    mq_game_coins_start(q); /* the end card shows the coins of this song */
    for (int i = 0; i < 4; i++)
        t->press_t[i] = 0;
    t->tempo = (uint8_t)(mq_home(q)->best_beat > 300 ? 2 : mq_home(q)->best_beat > 120 ? 1 : 0);
    t->song_bpm = 0, t->nsev = 0, t->song_p = -1;
    if (mode != TOY_BEATS) {
        mq_music(q, MODX_MUS_RHYTHM, t->tempo);
        return;
    }
    { /* a song for the tempo step (the slower ones first), never the last one again */
        int cap = t->tempo == 0 ? 104 : t->tempo == 1 ? 118 : 130, pick = -1;
        for (int k = 0, n = 0; k < MQ_NSONG; k++)
            if (mq_song_bpm(k) <= cap && k != (int)t->song && mq_rnd(q, 0, n++) == 0)
                pick = k;
        t->song = (uint8_t)(pick < 0 ? 0 : pick);
        int len16 = 0, n = mq_song_events(q->api, t->song, t->sev, MQ_SONG_EV, &len16, NULL, 0);
        t->nsev = (uint16_t)(n > 0 ? n : 0), t->song_len = (uint16_t)len16;
        t->song_bpm = (uint8_t)mq_song_bpm(t->song);
    }
    t->song_table = 0, t->flash_t = 0; /* a song of the table she is learning (the times-table run), when she has one */
    if (!q->preview) {
        int tb = mq_fact_run_table(q);
        if (tb >= 2 && mq_fact_run_start(q, tb))
            t->song_table = (uint8_t)tb;
    }
    mq_music(q, MODX_MUS_OFF, 0); /* (the song is the module's: the cats sing it along the beat clock) */
    beats_feed(q); /* the first order: on the screen at once */
    if (!beats_demo_seen(q) && !q->preview) { /* the first time: how to play, the song waits for it */
        t->demo_t = mq_mark(q);
        mq_speak(q, 8, "The cloud asks. Drum the answer!", MODX_SPEAK_NOW);
    } else { /* later: the one line */
        q->api->snprintf(t->say, sizeof t->say, "Drum the answer!");
        t->say_t = mq_mark(q);
    }
}

/* drop played notes, add the next bar's order a bar ahead */
static void beats_feed(mq_t *q)
{
    mq_toy_t *t = T;
    uint32_t now = q->anim_t - t->t0, bm = beats_bm(t);
    int k = 0;
    for (int i = 0; i < t->nn; i++)
        if (t->note[i].state != 1 || now - t->note[i].t < 600)
            t->note[k++] = t->note[i];
    t->nn = (uint8_t)k;
    int total = t->mode == TOY_JAM ? 0 : BEATS_BARS;
    bool open = false; /* (never a new order while one is still waiting: two alive at most) */
    for (int i = 0; i < t->nn; i++)
        open |= t->note[i].kind == NT_ORDER && t->note[i].state != 1 && t->note[i].pad + 1 < t->bars;
    while (!open && t->bars < total && (uint32_t)(t->bars * 4 + 4) * bm < now + 6 * bm)
        beats_bar(q, t->bars++);
}

/* the order waiting now (the earliest not answered), or -1 */
static int beats_cur(const mq_toy_t *t)
{
    int best = -1;
    for (int i = 0; i < t->nn; i++)
        if (t->note[i].kind == NT_ORDER && t->note[i].state != 1 && (best < 0 || t->note[i].t < t->note[best].t))
            best = i;
    return best;
}

static void hit_feedback(mq_t *q, int j, int drum)
{
    mq_toy_t *t = T;
    t->last_j = (uint8_t)j, t->last_j_t = mq_mark(q);
    if (drum >= 0) {
        t->paw = (uint8_t)(drum & 1), t->paw_t = mq_mark(q);
        static const uint8_t sfx[4] = {MODX_SFX_BONGO_LO, MODX_SFX_BONGO_HI, MODX_SFX_COWBELL, MODX_SFX_CLAP};
        mq_sfx(q, sfx[drum & 3], j);
    }
}

/* the song pauses and the guide points at the order: only its drum is lit until she drums it */
static void beats_calm(mq_t *q, int ni)
{
    mq_toy_t *t = T;
    int o = t->note[ni].pad & 1;
    if (!t->hold)
        t->hold = 1, t->hold_now = q->anim_t - t->t0;
    t->calm = 1, t->nfizz = 0;
    if (t->onterm[o])
        q->api->snprintf(t->say, sizeof t->say, "Listen! Count on by %d. Drum the %d!", t->oterm[o][0], t->oans[o]);
    else
        q->api->snprintf(t->say, sizeof t->say, "Listen! %s is %d. Drum the %d!", t->ocue[o], t->oans[o], t->oans[o]);
    t->say_t = mq_mark(q);
    mq_speak(q, 8, t->say, MODX_SPEAK_NOW);
}

/* Bongo Beats: drum i (0..3) hit now */
static void beats_drum(mq_t *q, int i)
{
    mq_toy_t *t = T;
    uint32_t now = q->anim_t - t->t0, bm = beats_bm(t);
    if (t->demo_t) /* any input ends the demo (seen) */
        t->demo_t = 0, mq_demo_seen(q, BEATS_DEMO);
    bool rapid = t->press_t[3] && q->anim_t - t->press_t[3] < 1000; /* the fourth hit back within a second */
    for (int k = 3; k > 0; k--)
        t->press_t[k] = t->press_t[k - 1];
    t->press_t[0] = q->anim_t;
    int ni = beats_cur(t);
    if (t->calm) { /* calm: only the order's drum, and calmly */
        bool quick = q->anim_t - t->last_press < 600;
        t->last_press = q->anim_t;
        if (ni < 0 || quick || i != t->oright[t->note[ni].pad & 1])
            return;
    }
    t->last_press = q->anim_t;
    if (ni < 0 || (!t->hold && iabs((int32_t)(now - t->note[ni].t)) > 130)) { /* no order on the beat: a fizzle */
        mq_sfx(q, MODX_SFX_DUD, 0);
        t->fizz_t = mq_mark(q), t->last_j = 0, t->last_j_t = 0;
        t->nfizz = (uint8_t)(q->anim_t - t->fizz_t < 3000 ? t->nfizz + 1 : 1);
        t->combo = 0;
        if (ni >= 0 && (rapid || t->nfizz >= 3))
            beats_calm(q, ni);
        return;
    }
    if (rapid && !t->calm) { /* hammering: the guide steps in */
        beats_calm(q, ni);
        return;
    }
    mq_note_t *n = &t->note[ni];
    int o = n->pad & 1;
    bool right = i == t->oright[o];
    t->paw = (uint8_t)(i & 1), t->paw_t = mq_mark(q), t->cat_paw = (uint8_t)(i & 1), t->cat_t = mq_mark(q);
    mq_sfx(q, MODX_SFX_PAW, i & 1);
    if (!right) { /* a soft thud; the answer after the beat; she drums it to go on (no points) */
        mq_sfx(q, MODX_SFX_DUD, 1);
        if (n->state == 0) {
            n->state = 3, t->combo = 0, t->miss++;
            if (t->ofact[o] >= 0)
                mq_fact_record(q, t->ofact[o], false, 6000), t->ofact[o] = -1;
        }
        t->owrong[o] = 1, t->shown_t = q->anim_t + bm;
        if (!t->hold)
            t->hold = 1, t->hold_now = now;
        t->last_j = 0, t->last_j_t = mq_mark(q);
        return;
    }
    int j = 0;
    if (n->state == 0 && !t->calm) { /* the order's one hit: right, on the beat (or late at the waiting line) */
        j = t->hold ? 1 : mq_beat_judge((int32_t)(now - n->t));
        j = j ? j : 1;
        t->combo++, t->score = (uint16_t)(t->score + (j == 2 ? 3 : 1) + (t->combo >= 4 ? 1 : 0));
        if (j == 2)
            t->perfect++;
        else
            t->good++;
        if (t->combo > t->best_combo)
            t->best_combo = t->combo;
        if (t->ofact[o] >= 0)
            mq_fact_record(q, t->ofact[o], true, 2000), t->ofact[o] = -1;
    } else if (t->ofact[o] >= 0) /* guided (calm): right, but it was shown */
        mq_fact_record(q, t->ofact[o], false, 6000), t->ofact[o] = -1;
    n->state = 1, n->j = (uint8_t)j;
    if (t->onterm[o]) /* the whole fact, a beat on the stage: what was drummed, said in full */
        q->api->snprintf(t->flash, sizeof t->flash, "%d, %d, %d, %d", t->oterm[o][0], t->oterm[o][1], t->oterm[o][2],
                         t->oterm[o][3]);
    else
        q->api->snprintf(t->flash, sizeof t->flash, "%s = %d", t->ocue[o], t->oans[o]);
    t->flash_t = mq_mark(q);
    if (t->hold) /* the song goes on from where it waited */
        t->t0 = q->anim_t - t->hold_now, t->hold = 0;
    t->calm = 0, t->nfizz = 0, t->owrong[o] = 0;
    t->mew_t = mq_mark(q);
    mq_sfx(q, MODX_SFX_MEW, (t->sing_note ? t->sing_note + 12 : 84) | 1 << 7); /* (an octave over the tune) */
    mq_spark_burst(q, pad_x(i) + 34, PAD_Y, j ? 10 : 4);
    if (j && t->combo >= 3) /* a combo: a burst and a happy trill */
        mq_spark_burst(q, 160, MQ_Y0 + 70, 8 + t->combo), mq_sfx(q, MODX_SFX_TRILL, t->combo);
    hit_feedback(q, j ? j : 3, i);
}

/* the jam: a drum (0..3) or a digit (10 + d) played now -- free drumming, anything goes */
static void beats_play(mq_t *q, int what)
{
    mq_toy_t *t = T;
    if (t->mode == TOY_BEATS) {
        if (what < NT_DIGIT)
            beats_drum(q, what);
        return;
    }
    hit_feedback(q, 3, what < NT_DIGIT ? what : (what & 1));
    t->score++;
    t->cat_paw = (uint8_t)(what & 1), t->cat_t = mq_mark(q);
}

/* the song up to `now`: every event whose sixteenth has come (the song loops; it stands still while
 * the lane waits). The melody: the cats' mews (MODX_SFX_MEW: the note, its length; a pickup a "mrrp");
 * the beat: paws on the bongos and the accents; the bass and the chords. */
static void beats_song(mq_t *q, uint32_t now)
{
    mq_toy_t *t = T;
    uint32_t bm = beats_bm(t);
    if (!t->nsev || !t->song_len || now < 4 * bm)
        return;
    int32_t p = (int32_t)((now - 4 * bm) * 4 / bm);
    for (int32_t k = t->song_p + 1; k <= p && k < t->song_p + 64; k++) {
        int at = (int)(k % t->song_len);
        for (int i = 0; i < t->nsev; i++) {
            const mq_sev_t *e = &t->sev[i];
            if (e->at != at)
                continue;
            switch (e->kind) {
            case SE_MEL: {
                int len = e->len & 0x7F, kind = len >= 3 ? 1 : len == 1 && at % 8 == 6 ? 2 : 0; /* meow / mrrp / mew */
                mq_sfx(q, MODX_SFX_MEW, e->a | len << 7 | kind << 12);
                t->sing_note = e->a, t->sing_t = mq_mark(q);
                break;
            }
            case SE_BEAT:
                if (e->a <= 1) {
                    mq_sfx(q, MODX_SFX_PAW, e->a);
                    t->cat_paw = e->a, t->cat_t = mq_mark(q);
                } else
                    mq_sfx(q, e->a == 2 ? MODX_SFX_COWBELL : e->a == 3 ? MODX_SFX_CLAP : MODX_SFX_TAMB, 9);
                break;
            case SE_BASS: mq_sfx(q, MODX_SFX_BASS, e->a | (e->len & 0x7F) << 7); break;
            default: mq_sfx(q, MODX_SFX_CHORD, e->a | (e->len & 0x7F) << 7 | (e->len >> 7) << 14); break;
            }
        }
    }
    t->song_p = p;
}

static void beats_frame(mq_t *q)
{
    mq_toy_t *t = T;
    if (t->done)
        return;
    if (t->mode == TOY_BEATS && t->demo_t) { /* the demo: the song waits at its start */
        t->t0 = q->anim_t;
        if ((int)(q->anim_t - t->demo_t) >= (DEMO_LEN > mq_read_ms(q, "Drum the answer on the beat!") ? DEMO_LEN : mq_read_ms(q, "Drum the answer on the beat!"))) /* (its words' reading time) */
            t->demo_t = 0, mq_demo_seen(q, BEATS_DEMO), t->t0 = q->anim_t;
    }
    if (t->hold) /* waiting for the answer: the lane stands still */
        t->t0 = q->anim_t - t->hold_now;
    uint32_t now = q->anim_t - t->t0, bm = beats_bm(t);
    beats_feed(q);
    beats_song(q, now);
    int ni = beats_cur(t);
    if (t->mode == TOY_BEATS && ni >= 0 && !t->hold && (int32_t)(now - t->note[ni].t) > 130) { /* let go: it waits */
        t->hold = 1, t->hold_now = t->note[ni].t;
        if (t->note[ni].state == 0)
            t->combo = 0;
        t->t0 = q->anim_t - t->hold_now;
    }
    if (now / bm != t->last_beat) { /* a beat: the pulse (the downbeat shakes); the cat taps along */
        t->last_beat = now / bm;
        if (t->last_beat % 4 == 0)
            t->shake_t = mq_mark(q);
    }
    mq_fun_time(q, MQ_FRAME_MS, t->mode == TOY_BEATS); /* the orders count as maths, the jam as fun */
    if (t->mode == TOY_BEATS && t->bars >= BEATS_BARS && beats_cur(t) < 0) { /* every order drummed: the finale */
        t->done = 1, t->done_t = q->anim_t;

        mq_home_t *h = mq_home(q);
        t->new_best = t->score > h->best_beat;
        if (t->new_best)
            h->best_beat = t->score, q->dirty = true;
        if (t->score) /* (coins only for right answers drummed) */
            mq_earn(q, 2 + t->score / 10, NULL);
        mq_sfx(q, MODX_SFX_MEOW, 0);
        mq_sfx(q, t->new_best ? MODX_SFX_UNLOCK : MODX_SFX_CHEER, 5);
        mq_quest_event(q, QE_TOY, 1);
        mq_music(q, MODX_MUS_ARCADE, 0);
    }
    if (t->mode == TOY_JAM && t->jam_until && q->anim_t >= t->jam_until) { /* the fun beat is over */
        t->jam_until = 0;
        q->beat = 0xFF;
        mq_fun_resume(q, t->jam_ret);
    }
}

/* Puff, the fluffy drum sheep (the band's second drummer): wilder with the combo */
static void draw_puff(mq_t *q, int x, int y)
{
    mq_toy_t *t = T;
    int wild = t->combo >= 6 ? 3 : t->combo >= 4 ? 2 : t->combo >= 2 ? 1 : 0;
    uint32_t el = q->anim_t - t->paw_t;
    int bounce = (int)((q->anim_t - t->t0) % beats_bm(t)) < 90 ? 2 + wild : 0;
    y -= bounce;
    for (int i = 0; i < 7; i++) /* the fluff */
        mq_circle(q, x - 18 + i * 6, y + (i % 2) * 4, 9 + (i % 3), Q_WHITE);
    mq_circle(q, x, y + 6, 16, Q_WHITE);
    mq_circle(q, x - 13, y - 8, 5, Q_PINK), mq_circle(q, x + 13, y - 8, 5, Q_PINK); /* ears */
    mq_rrect(q, x - 11, y - 2, 22, 16, 7, RGB(250, 230, 215)); /* face */
    if (wild >= 2 && (q->anim_t / 120) % 2)
        mq_star(q, x - 5, y + 3, 3, Q_HOTPINK), mq_star(q, x + 5, y + 3, 3, Q_HOTPINK);
    else
        mq_fill(q, x - 6, y + 3, 2, 3, Q_INK), mq_fill(q, x + 4, y + 3, 2, 3, Q_INK);
    mq_fill(q, x - 2, y + 9, 4, 2, Q_HOTPINK);
    /* the drums, a paw hitting */
    mq_rrect(q, x - 34, y + 22, 26, 14, 6, Q_BROWN), mq_rrect(q, x + 8, y + 22, 26, 14, 6, Q_BROWN);
    mq_fill(q, x - 32, y + 22, 22, 3, Q_PEACH), mq_fill(q, x + 10, y + 22, 22, 3, Q_PEACH);
    int down = el < 120 && t->paw_t;
    mq_circle(q, x - 18, y + 16 + (down && t->paw == 0 ? 6 : 0), 5, Q_WHITE);
    mq_circle(q, x + 18, y + 16 + (down && t->paw == 1 ? 6 : 0), 5, Q_WHITE);
    for (int k = 0; k < wild * 2; k++) /* going wild: notes fly */
        mq_text(q, q->api->font_small, x - 40 + k * 18, y - 20 - (int)((q->anim_t / 50 + (uint32_t)k * 13) % 20), "*",
                k % 2 ? Q_HOTPINK : Q_PURPLE);
}

/* Miso, the band's cat drummer: a round apricot tabby with a cream muzzle, big teal eyes and a
 * striped tail, slapping a pair of bongos with her paws (left paw the low drum, right the high);
 * a little note and a "mew!" puff on a right hit */
static void draw_cat(mq_t *q, int x, int y)
{
    mq_toy_t *t = T;
    const uint16_t fur = RGB(250, 190, 120), stripe = RGB(226, 140, 70), cream = RGB(255, 240, 220);
    uint32_t el = q->anim_t - t->cat_t;
    bool slap = t->cat_t && el < 140;
    int bob = (int)((q->anim_t - t->t0) % beats_bm(t)) < 100 ? 1 : 0; /* nodding along */
    y += bob;
    /* the tail, swishing */
    int sw = (int)((q->anim_t / 200) % 4);
    mq_circle(q, x + 26, y + 18 - sw, 5, fur), mq_circle(q, x + 31, y + 10 - sw, 4, stripe), mq_circle(q, x + 33, y + 3 - sw, 4, fur);
    /* body and head */
    mq_oval(q, x, y + 18, 20, 15, fur);
    mq_oval(q, x, y + 22, 11, 9, cream);
    mq_circle(q, x, y - 4, 17, fur);
    int16_t ex[3] = {(int16_t)(x - 16), (int16_t)(x - 13), (int16_t)(x - 4)}, ey[3] = {(int16_t)(y - 12), (int16_t)(y - 26), (int16_t)(y - 18)};
    mq_poly(q, ex, ey, 3, fur);
    int16_t fx[3] = {(int16_t)(x + 16), (int16_t)(x + 13), (int16_t)(x + 4)};
    mq_poly(q, fx, ey, 3, fur);
    mq_circle(q, x - 12, y - 17, 2, Q_PINK), mq_circle(q, x + 12, y - 17, 2, Q_PINK);
    mq_fill(q, x - 3, y - 20, 2, 6, stripe), mq_fill(q, x + 1, y - 20, 2, 6, stripe); /* the forehead stripes */
    mq_oval(q, x, y + 3, 8, 5, cream); /* the muzzle */
    if (t->mew_t && q->anim_t - t->mew_t < 500) { /* eyes shut happily, the mouth open: mew! */
        mq_fill(q, x - 9, y - 5, 5, 2, Q_INK), mq_fill(q, x + 4, y - 5, 5, 2, Q_INK);
        mq_circle(q, x, y + 5, 2, Q_HOTPINK);
    } else {
        mq_circle(q, x - 7, y - 5, 3, RGB(40, 150, 150)), mq_circle(q, x + 7, y - 5, 3, RGB(40, 150, 150));
        mq_circle(q, x - 6, y - 6, 1, Q_WHITE), mq_circle(q, x + 8, y - 6, 1, Q_WHITE);
        if (t->sing_t && q->anim_t - t->sing_t < 160) /* singing the tune: the mouth opens a note */
            mq_oval(q, x, y + 5, 2, 2, Q_HOTPINK);
        else
            mq_fill(q, x - 2, y + 4, 4, 1, Q_INK);
    }
    mq_circle(q, x, y + 1, 1, Q_PINK); /* the nose */
    /* the bongos and the paws */
    mq_rrect(q, x - 30, y + 26, 24, 13, 6, RGB(170, 110, 70)), mq_rrect(q, x + 6, y + 26, 22, 12, 6, RGB(190, 125, 80));
    mq_fill(q, x - 28, y + 26, 20, 3, cream), mq_fill(q, x + 8, y + 26, 18, 3, cream);
    int dl = slap && t->cat_paw == 0 ? 6 : 0, dr = slap && t->cat_paw == 1 ? 6 : 0;
    mq_circle(q, x - 18, y + 18 + dl, 5, cream), mq_circle(q, x + 17, y + 18 + dr, 5, cream);
    if (slap) { /* the slap: lines off the drum */
        int sx = t->cat_paw ? x + 17 : x - 18;
        mq_line(q, sx - 9, y + 22, sx - 13, y + 18, Q_INK), mq_line(q, sx + 9, y + 22, sx + 13, y + 18, Q_INK);
    }
    if (t->mew_t && q->anim_t - t->mew_t < 700) { /* a note floats up, and a "mew!" puff */
        int up = (int)((q->anim_t - t->mew_t) / 30);
        mq_circle(q, x - 24, y - 20 - up, 3, Q_PURPLE), mq_fill(q, x - 22, y - 30 - up, 1, 10, Q_PURPLE);
        mq_rrect(q, x + 14, y - 34, 34, 15, 7, Q_WHITE);
        mq_text_c(q, q->api->font_small, x + 31, y - 34, "mew!", Q_HOTPINK);
    }
}

/* a run of terms in their own slots, the unknown a highlighted "?" (the skip count's cloud) */
static void draw_terms(mq_t *q, const mq_toy_t *t, int o, int cx, int cy, bool show)
{
    int n = t->onterm[o], sw = 30, x0 = cx - n * sw / 2;
    for (int i = 0; i < n; i++) {
        int x = x0 + i * sw;
        char s[8];
        bool hole = i == t->ohole[o];
        mq_rrect(q, x + 2, cy - 1, sw - 4, 18, 6, hole ? Q_YELLOW : RGB(236, 246, 255));
        if (hole)
            mq_rect(q, x + 2, cy - 1, sw - 4, 18, Q_HOTPINK);
        q->api->snprintf(s, sizeof s, "%d", t->oterm[o][i]);
        mq_text_fit(q, q->api->font_medium, x + sw / 2, cy, sw - 6, hole && !show ? "?" : s, Q_INK, true);
    }
}

/* a toy's words of the moment ("Listen! ...", a clever find) take the line on top: the toy's own words there wait */
static bool say_on(const mq_t *q)
{
    const mq_toy_t *t = &q->toy;
    int need = mq_read_ms((mq_t *)q, t->say); /* (its reading time, never less than before) */
    return t->say_t && q->anim_t - t->say_t < (uint32_t)(need > 2600 ? need : 2600) && t->say[0] && t->mode != TOY_GROW;
}

/* the first-time demo (and "help?"): the ghost hand drums the right bongo, the guide says how */
static void draw_demo(mq_t *q)
{
    mq_toy_t *t = T;
    int ni = beats_cur(t);
    if (!t->demo_t || ni < 0)
        return;
    /* the cloud asks (the hand under it), then the hand goes down to the drum with its answer and drums it */
    int el = (int)(q->anim_t - t->demo_t), ph = el % 2400, o = t->note[ni].pad & 1;
    int dx = pad_x(t->oright[o]) + 54, dy = PAD_Y + 32, x, y, press = 0; /* (low on the pad: its number shows) */
    if (ph < 800)
        x = 160 + 40, y = MQ_Y0 + 50, press = ph > 300 && ph < 500 ? 6 : 0;
    else if (ph < 1200)
        x = 200 + (dx - 200) * (ph - 800) / 400, y = MQ_Y0 + 50 + (dy - MQ_Y0 - 50) * (ph - 800) / 400;
    else
        x = dx, y = dy, press = ph > 1300 && ph < 1600 ? 16 : 0;
    if (ph > 1400 && ph < 2000)
        mq_ring(q, x, y, 4 + (ph - 1400) / 75, Q_HOTPINK);
    mq_hand(q, x, y, press);
    static const char cap[] = "The cloud asks. Drum the answer!"; /* (in the line on top: over nothing she plays) */
    int w = mq_text_w(q, q->api->font_small, cap) + 26;
    mq_rrect(q, 2, MQ_Y0 + 2, w, 17, 8, Q_LAV);
    mq_cast(q, 8, 4, MQ_Y0 + 2, 16, AN_TALK);
    mq_text(q, q->api->font_small, 22, MQ_Y0 + 4, cap, Q_INK);
}

static void draw_beats(mq_t *q)
{
    mq_toy_t *t = T;
    const modx_api_t *api = q->api;
    uint32_t now = q->anim_t - t->t0, bm = beats_bm(t);
    bool beats = t->mode == TOY_BEATS;
    int beat_in = t->hold ? 200 : (int)(now % bm), hx = 64, ly = MQ_Y0 + 74;
    bool down = t->shake_t && q->anim_t - t->shake_t < 120;
    q->ox = (int16_t)(down && !t->hold ? ((q->anim_t / 40) % 2 ? 2 : -2) : 0);
    mq_fill(q, 0, MQ_Y0, MQ_W, MQ_H, beat_in < 80 ? RGB(255, 236, 248) : RGB(250, 244, 255));
    if (beats && t->demo_t && beats_cur(t) >= 0) /* (the demo's words have the line on top meanwhile: draw_demo) */
        mq_button(q, 262, MQ_Y0 + 2, 54, 18, B_TOY, 9, "help?", Q_LAV);
    else if (beats && say_on(q))
        mq_button(q, 262, MQ_Y0 + 2, 54, 18, B_TOY, 9, "help?", Q_LAV);
    else if (beats) { /* the line on top: what to do, and help */
        mq_text(q, api->font_medium, 6, MQ_Y0 + 2, t->calm ? "Listen..." : "Drum the answer!", Q_PURPLE);
        if (t->song_table) { /* the song of a table: its name says so */
            char sn[24];
            api->snprintf(sn, sizeof sn, "Song of %ds", t->song_table);
            mq_text_fit(q, api->font_small, 136, MQ_Y0 + 5, 120, sn, Q_HOTPINK, false);
        } else if (t->nsev)
            mq_text_fit(q, api->font_small, 136, MQ_Y0 + 5, 120, mq_song_name(t->song), Q_DIM, false);
        mq_button(q, 262, MQ_Y0 + 2, 54, 18, B_TOY, 9, "help?", Q_LAV);
    }
    /* the beat pulse: rings from the hit line */
    int pr = beat_in * 40 / (int)bm;
    mq_ring(q, hx, ly, 12 + pr, Q_LAV);
    if (beat_in < 100)
        mq_ring(q, hx, ly, 14 + pr / 2, Q_HOTPINK);
    /* the lane, beat lines, the hit line */
    mq_rrect(q, 0, ly - 20, MQ_W, 40, 12, Q_WHITE);
    int pxb = 56; /* pixels per beat */
    for (int b = 0; b < 6; b++) {
        int x = hx + (int)(((uint32_t)b * bm - (uint32_t)(now % bm)) * (uint32_t)pxb / bm);
        if (x < MQ_W)
            mq_fill(q, x, ly - 18, 1, 36, (now / bm + (uint32_t)b) % 4 == 0 ? Q_PURPLE : Q_GREY);
    }
    mq_circle(q, hx, ly, 16, Q_YELLOW);
    mq_ring(q, hx, ly, 16, Q_HOTPINK);
    if (beats && t->nsev && t->song_len) { /* the tune: its notes bouncing in along the lane, higher for higher */
        for (int32_t k = t->song_p + 1; k <= t->song_p + 20; k++) {
            int at = (int)(k % t->song_len);
            for (int i = 0; i < t->nsev; i++) {
                const mq_sev_t *e = &t->sev[i];
                if (e->at != at || e->kind != SE_MEL)
                    continue;
                int32_t ms = (int32_t)(4 * bm + (uint32_t)k * bm / 4) - (int32_t)now;
                int x = hx + ms * pxb / (int32_t)bm, y = ly - 2 - (e->a - 72) * 3 / 4;
                y = y < ly - 17 ? ly - 17 : y > ly + 14 ? ly + 14 : y;
                if (x > hx + 18 && x < MQ_W - 4)
                    mq_circle(q, x, y, 3, Q_LAV), mq_fill(q, x + 2, y - 8, 1, 8, Q_LAV);
            }
        }
        if (t->sing_t && q->anim_t - t->sing_t < 200) /* the note being sung hops off the line */
            mq_circle(q, hx + 22, ly - 12 - (int)((q->anim_t - t->sing_t) / 25), 3, Q_PURPLE),
                mq_fill(q, hx + 24, ly - 20 - (int)((q->anim_t - t->sing_t) / 25), 1, 8, Q_PURPLE);
    }
    int cur = beats_cur(t);
    for (int i = 0; i < t->nn; i++) { /* the orders riding in, each with its cloud */
        const mq_note_t *n = &t->note[i];
        if (n->kind != NT_ORDER)
            continue;
        int o = n->pad & 1, x = hx + (int)(((int32_t)n->t - (int32_t)now) * pxb / (int32_t)bm);
        if (x < -20 || (n->state == 1 && q->anim_t - t->mew_t > 500))
            continue;
        bool show = n->state == 1 || (t->owrong[o] && q->anim_t >= t->shown_t) || (t->calm && i == cur);
        int nx = x > MQ_W - 18 ? MQ_W - 18 : x;
        mq_circle(q, nx, ly, 15, n->state == 1 ? Q_MINT : t->hold && i == cur ? Q_HOTPINK : Q_SKY);
        char s[8];
        api->snprintf(s, sizeof s, "%d", t->oans[o]);
        mq_text_fit(q, api->font_medium, nx, ly - 7, 28, show ? s : "?", n->state == 1 ? Q_INK : Q_WHITE, true);
    }
    if (beats && cur >= 0) { /* the cloud: the waiting order, big in the middle from the moment it is next (read it
                              * before its note comes: the user, "not sure what is being taught") -- "7 x 6 = ?" */
        int o = t->note[cur].pad & 1;
        bool show = (t->owrong[o] && q->anim_t >= t->shown_t) || t->calm;
        char cue[40];
        if (!t->onterm[o])
            api->snprintf(cue, sizeof cue, show ? "%s = %d" : "%s = ?", t->ocue[o], t->oans[o]);
        int cw = t->onterm[o] ? 4 * 30 + 24 : imin(220, mq_text_w(q, api->font_large, cue) + 36), cx = 160, cy = MQ_Y0 + 24;
        for (int sh = 1; sh >= 0; sh--) { /* the cloud, on its soft shadow (under the line on top) */
            uint16_t cc = sh ? RGB(214, 200, 240) : Q_WHITE;
            int d = sh * 2;
            mq_circle(q, cx - cw / 2 + 12 + d, cy + 14 + d, 12, cc), mq_circle(q, cx + cw / 2 - 12 + d, cy + 14 + d, 12, cc);
            mq_circle(q, cx + d, cy + 9 + d, 12, cc);
            mq_rrect(q, cx - cw / 2 + 4 + d, cy + 6 + d, cw - 8, 22, 10, cc);
        }
        if (t->onterm[o])
            draw_terms(q, t, o, cx, cy + 8, show);
        else
            mq_text_fit(q, api->font_large, cx, cy + 6, cw - 16, cue, Q_INK, true);
    }
    if (beats) { /* the band: Miso the cat and Puff */
        draw_cat(q, 40, MQ_Y0 + 118);
        draw_puff(q, 280, MQ_Y0 + 116);
    } else
        draw_puff(q, 70, MQ_Y0 + 128);
    static const char jl[4][9] = {"oops", "good!", "perfect!", "yes!"};
    bool flash = beats && t->flash_t && q->anim_t - t->flash_t < 1400;
    if (flash) /* the whole fact she drummed, said in full ("7 x 6 = 42"), the judgement over it */
        mq_text_fit(q, api->font_large, 160, ly + 34, 150, t->flash, Q_HOTPINK, true);
    if (t->last_j_t && q->anim_t - t->last_j_t < 500)
        mq_text_c(q, flash ? api->font_small : api->font_large, 160, ly + 22, jl[t->last_j & 3],
                  t->last_j == 2 ? Q_HOTPINK : t->last_j ? Q_PURPLE : Q_DIM);
    else if (t->fizz_t && q->anim_t - t->fizz_t < 400)
        mq_text_c(q, api->font_small, 160, ly + 24, "fizz...", Q_DIM);
    char s[32];
    if (beats) { /* the four bongos, an answer on each (calm: only the order's lit) */
        int o = cur >= 0 ? t->note[cur].pad & 1 : 0;
        for (int i = 0; i < 4; i++) {
            bool lit = !t->calm || i == t->oright[o];
            char k[2] = {(char)('1' + i), 0};
            int pulse = t->calm && lit && (q->anim_t / 250) % 2 ? 2 : 0;
            mq_button(q, pad_x(i) - pulse, PAD_Y - pulse, 68 + 2 * pulse, PAD_H, B_TOY, 100 + i, NULL,
                      !lit ? Q_GREY : i % 2 ? Q_PEACH : RGB(240, 200, 160));
            if (cur >= 0) {
                api->snprintf(s, sizeof s, "%d", t->ocand[o][i]);
                mq_text_fit(q, api->font_large, pad_x(i) + 34, PAD_Y + 8, 60, s, lit ? Q_INK : Q_DIM, true);
            }
            mq_text(q, api->font_small, pad_x(i) + 8, PAD_Y + PAD_H - 16, k, Q_DIM); /* its key (inside the round corner) */
        }
    } else { /* the jam: four drums and the number row, anything goes */
        static const char pn[4][8] = {"bongo", "bongo", "bell", "clap"};
        for (int i = 0; i < 4; i++) {
            int x = 132 + i * 46;
            mq_button(q, x, MQ_Y0 + 110, 42, 32, B_TOY, 100 + i, pn[i], i == 2 ? Q_YELLOW : i == 3 ? Q_PINK : Q_PEACH);
        }
        for (int dgt = 0; dgt < 10; dgt++) {
            char d[2] = {(char)('0' + dgt), 0};
            mq_button(q, 4 + dgt * 31, MQ_Y0 + 176, 29, 24, B_TOY, 110 + dgt, d, Q_WHITE);
        }
        mq_text_c(q, api->font_small, 200, MQ_Y0 + 154, "Jam along!", Q_PURPLE);
    }
    if (t->combo >= 2) { /* the combo (a flourish: the score stays the game's own, never shown) */
        api->snprintf(s, sizeof s, "x%d", t->combo);
        mq_star(q, beats ? 120 : 130, MQ_Y0 + (beats ? 144 : 154), 6, Q_YELLOW);
        mq_text(q, api->font_medium, beats ? 130 : 140, MQ_Y0 + (beats ? 137 : 147), s, Q_HOTPINK);
    }
    if (beats)
        draw_demo(q);
    if (t->done) {
        mq_rrect(q, 40, MQ_Y0 + 30, 240, 110, 12, Q_WHITE);
        mq_coins_end(q, 160, MQ_Y0 + 36, mq_game_coins(q), t->done_t, t->new_best);
        char sw[32];
        api->snprintf(sw, sizeof sw, "You drummed the %ds!", t->song_table);
        if (t->song_table) /* the song's table, said (always: it is what she played) */
            mq_text_c(q, api->font_small, 160, MQ_Y0 + 94, sw, Q_HOTPINK);
        if (!t->new_best) /* a word for the song, never a count */
            mq_text_c(q, api->font_small, 160, MQ_Y0 + 80,
                      t->perfect * 2 >= t->perfect + t->good + t->miss && t->perfect ? "Perfect rhythm!"
                      : t->best_combo >= 6                                         ? "What a groove!"
                                                                                   : "What a song!",
                      Q_PURPLE);
        mq_button(q, 48, MQ_Y0 + 112, 70, 24, B_TOY, 1, "again!", Q_MINT);
        mq_button(q, 125, MQ_Y0 + 112, 70, 24, B_TOY, 10, "free jam", Q_YELLOW); /* the reward: drum freely */
        mq_button(q, 202, MQ_Y0 + 112, 70, 24, B_TOY, 0, "toys", Q_PEACH);
    }
    q->ox = 0;
}

/* ------------------------------------------------------------------ the kaleidoscope */
static void kal_dot(mq_t *q, int i)
{
    mq_toy_t *t = T;
    int cx = 160, cy = MQ_Y0 + 100, x = t->pts[i].x - cx, y = t->pts[i].y - cy, n = t->sym ? t->sym : 6;
    static const uint16_t pal[6] = {Q_HOTPINK, Q_PURPLE, Q_SKY, Q_MINT, Q_YELLOW, Q_ORANGE};
    for (int k = 0; k < n; k++) { /* rotated n ways, each also mirrored */
        int a = k * 360 / n, c = mq_icos(a), s = mq_isin(a);
        int rx = (x * c - y * s) / 1024, ry = (x * s + y * c) / 1024;
        int mx = (x * c + y * s) / 1024, my = (x * s - y * c) / 1024;
        mq_circle(q, cx + rx, cy + ry, 3, pal[t->pts[i].c % 6]);
        mq_circle(q, cx + mx, cy + my, 3, pal[(t->pts[i].c + 2) % 6]);
    }
}

static void draw_kaleido(mq_t *q, bool full)
{
    mq_toy_t *t = T;
    if (full) {
        mq_fill(q, 0, MQ_Y0, MQ_W, MQ_H, RGB(40, 30, 70));
        mq_circle(q, 160, MQ_Y0 + 100, 92, RGB(60, 48, 100));
        for (int i = 0; i < t->npts; i++)
            kal_dot(q, i);
        char s[24];
        q->api->snprintf(s, sizeof s, "%d-way mirror", t->sym ? t->sym : 6);
        if (!say_on(q))
            mq_text(q, q->api->font_small, 6, MQ_Y0 + 4, s, Q_WHITE);
        mq_text(q, q->api->font_small, 6, MQ_Y0 + 190, "keys 3-8: mirrors  DEL: clear", Q_LAV);
    } else /* dirty drawing: only the new dots */
        for (int i = t->drawn; i < t->npts; i++)
            kal_dot(q, i);
    t->drawn = t->npts;
}

/* ------------------------------------------------------------------ magic square, cage puzzle */
static const uint8_t lo_shu[9] = {2, 7, 6, 9, 5, 1, 4, 3, 8};
static void magic_new(mq_t *q)
{
    mq_toy_t *t = T;
    int r = mq_rnd(q, 0, 7); /* one of the 8 turns and flips of the Lo Shu square */
    for (int i = 0; i < 9; i++) {
        int x = i % 3, y = i / 3, sx = x, sy = y;
        if (r & 1)
            sx = 2 - sx;
        if (r & 2)
            sy = 2 - sy;
        if (r & 4) {
            int tt = sx;
            sx = sy, sy = tt;
        }
        t->sol[i] = lo_shu[sy * 3 + sx];
    }
    int keep = mq_rnd(q, 3, 4);
    t->given = 0;
    t->given |= 1 << 4; /* the middle 5: the first clue */
    for (int n = 1; n < keep;) {
        int k = mq_rnd(q, 0, 8);
        if (!((t->given >> k) & 1))
            t->given |= (uint16_t)(1 << k), n++;
    }
    for (int i = 0; i < 9; i++)
        t->cell[i] = (t->given >> i) & 1 ? t->sol[i] : 0;
    t->sel = 0, t->solved = 0;
}

static const uint8_t cage_layout[3][9] = {{0, 0, 1, 2, 3, 1, 2, 3, 3}, {0, 1, 1, 0, 2, 3, 4, 4, 3}, {0, 0, 1, 2, 2, 1, 3, 4, 4}};
static void cage_new(mq_t *q)
{
    mq_toy_t *t = T;
    int a = mq_rnd(q, 0, 5), shift = mq_rnd(q, 0, 2); /* a Latin square of 1-3 */
    static const uint8_t perm[6][3] = {{1, 2, 3}, {1, 3, 2}, {2, 1, 3}, {2, 3, 1}, {3, 1, 2}, {3, 2, 1}};
    for (int i = 0; i < 9; i++)
        t->sol[i] = perm[a][(i % 3 + i / 3 + shift) % 3];
    t->layout = (uint8_t)mq_rnd(q, 0, 2);
    for (int i = 0; i < 9; i++)
        t->cell[i] = 0;
    t->given = 0, t->sel = 0, t->solved = 0;
}

static int cage_sum(const mq_toy_t *t, int c, bool sol)
{
    int s = 0;
    for (int i = 0; i < 9; i++)
        if (cage_layout[t->layout][i] == c)
            s += sol ? t->sol[i] : t->cell[i];
    return s;
}

static bool grid_check(mq_toy_t *t)
{
    if (t->mode == TOY_MAGIC) {
        static const uint8_t ln[8][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6}, {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}};
        int used = 0;
        for (int i = 0; i < 9; i++) {
            if (!t->cell[i] || ((used >> t->cell[i]) & 1))
                return false;
            used |= 1 << t->cell[i];
        }
        for (int l = 0; l < 8; l++)
            if (t->cell[ln[l][0]] + t->cell[ln[l][1]] + t->cell[ln[l][2]] != 15)
                return false;
        return true;
    }
    for (int r = 0; r < 3; r++) {
        int rr = 0, cc = 0;
        for (int k = 0; k < 3; k++) {
            if (!t->cell[r * 3 + k] || !t->cell[k * 3 + r])
                return false;
            rr |= 1 << t->cell[r * 3 + k], cc |= 1 << t->cell[k * 3 + r];
        }
        if (rr != 14 || cc != 14)
            return false;
    }
    for (int c = 0; c < 5; c++)
        if (cage_sum(t, c, false) != cage_sum(t, c, true))
            return false;
    return true;
}

static void grid_put(mq_t *q, int v)
{
    mq_toy_t *t = T;
    if (t->solved || ((t->given >> t->sel) & 1))
        return;
    int max = t->mode == TOY_MAGIC ? 9 : 3;
    if (v < 0 || v > max)
        return;
    t->cell[t->sel] = (uint8_t)v;
    mq_sfx(q, MODX_SFX_TAP, v);
    if (grid_check(t)) {
        t->solved = 1;
        mq_earn(q, 5, NULL);
        clever(q, t->mode == TOY_MAGIC ? "Whoa, every line makes 15! You found the magic!"
                                       : "Whoa, you figured out the cages!");
    }
}

static void draw_grid(mq_t *q)
{
    mq_toy_t *t = T;
    const modx_api_t *api = q->api;
    mq_fill(q, 0, MQ_Y0, MQ_W, MQ_H, RGB(255, 248, 236));
    int gx = 24, gy = MQ_Y0 + 28, c = 44;
    static const uint16_t cagec[5] = {RGB(255, 226, 236), RGB(226, 240, 255), RGB(232, 250, 232), RGB(255, 244, 214),
                                      RGB(240, 230, 255)};
    for (int i = 0; i < 9; i++) {
        int x = gx + (i % 3) * c, y = gy + (i / 3) * c;
        uint16_t bg = t->mode == TOY_CAGE ? cagec[cage_layout[t->layout][i]] : Q_WHITE;
        mq_fill(q, x, y, c - 2, c - 2, t->solved ? Q_MINT : bg);
        mq_add_btn(q, x, y, c - 2, c - 2, B_TOY, 30 + i);
        if (i == t->sel && !t->solved)
            mq_rect(q, x - 1, y - 1, c, c, Q_HOTPINK), mq_rect(q, x, y, c - 2, c - 2, Q_HOTPINK);
        if (t->cell[i]) {
            char s[2] = {(char)('0' + t->cell[i]), 0};
            mq_text_c(q, api->font_xlarge, x + c / 2 - 1, y + 8, s, (t->given >> i) & 1 ? Q_INK : Q_PURPLE);
        }
    }
    if (t->mode == TOY_CAGE)
        for (int k = 0; k < 5; k++) /* each cage's sum, in its first cell */
            for (int i = 0; i < 9; i++)
                if (cage_layout[t->layout][i] == k) {
                    char s[6];
                    api->snprintf(s, sizeof s, "%d+", cage_sum(t, k, true));
                    mq_text(q, api->font_small, gx + (i % 3) * c + 2, gy + (i / 3) * c + 1, s, Q_INK);
                    break;
                }
    if (t->mode == TOY_MAGIC) { /* the sums so far, at the row and column ends */
        for (int r = 0; r < 3; r++) {
            int sr = 0, sc = 0;
            for (int k = 0; k < 3; k++)
                sr += t->cell[r * 3 + k], sc += t->cell[k * 3 + r];
            char s[6];
            api->snprintf(s, sizeof s, "%d", sr);
            mq_text(q, api->font_small, gx + 3 * c + 2, gy + r * c + 14, s, sr == 15 ? Q_GREEN : Q_DIM);
            api->snprintf(s, sizeof s, "%d", sc);
            mq_text_c(q, api->font_small, gx + r * c + c / 2, gy + 3 * c, s, sc == 15 ? Q_GREEN : Q_DIM);
        }
    }
    int max = t->mode == TOY_MAGIC ? 9 : 3;
    for (int v = 1; v <= max; v++) { /* the number tiles */
        char s[2] = {(char)('0' + v), 0};
        mq_button(q, 186 + ((v - 1) % 3) * 42, MQ_Y0 + 30 + ((v - 1) / 3) * 34, 38, 30, B_TOY, 40 + v, s, Q_LAV);
    }
    mq_button(q, 186, MQ_Y0 + 140, 80, 26, B_TOY, 40, "erase", Q_PEACH);
    if (t->mode == TOY_MAGIC) /* (the rule fitted beside the grid: on two lines when long) */
        mq_text_fit(q, api->font_small, 186, MQ_Y0 + 172, 130, "Every line: 15!", Q_INK, false);
    else
        mq_text(q, api->font_small, 186, MQ_Y0 + 170, "1, 2, 3 in each", Q_INK),
            mq_text(q, api->font_small, 186, MQ_Y0 + 180, "row and column", Q_INK);
    if (t->solved)
        mq_button(q, 270, MQ_Y0 + 140, 46, 26, B_TOY, 2, "again!", Q_MINT); /* (beside erase: the rule below stays clear) */
}

/* ------------------------------------------------------------------ number magic */
static void draw_trick(mq_t *q)
{
    mq_toy_t *t = T;
    const modx_api_t *api = q->api;
    mq_fill(q, 0, MQ_Y0, MQ_W, MQ_H, RGB(244, 236, 255));
    int n = t->pick ? t->pick : 7;
    mq_sprite_anim(q, 8, 10, MQ_Y0 + 30, 3, t->step >= 5 ? AN_DANCE : AN_IDLE);
    mq_rrect(q, 80, MQ_Y0 + 24, 234, 120, 12, Q_WHITE);
    char l[6][44];
    if (t->sub == 0) { /* think of a number */
        api->snprintf(l[0], 44, "Pick a number, any number!");
        api->snprintf(l[1], 44, "Your number: %d", n);
        api->snprintf(l[2], 44, "Double it: %d", 2 * n);
        api->snprintf(l[3], 44, "Add 6: %d", 2 * n + 6);
        api->snprintf(l[4], 44, "Half of it: %d", n + 3);
        api->snprintf(l[5], 44, "Take away your number: 3!");
    } else { /* the nines on your fingers */
        api->snprintf(l[0], 44, "9 times %d? Fold finger number %d!", n, n);
        api->snprintf(l[1], 44, "Fingers before it: %d", n - 1);
        api->snprintf(l[2], 44, "Fingers after it: %d", 10 - n);
        api->snprintf(l[3], 44, "Put them together: %d%d", n - 1, 10 - n);
        api->snprintf(l[4], 44, "9 x %d = %d. Magic!", n, 9 * n);
        l[5][0] = 0;
        for (int f = 1; f <= 10 && t->step >= 1; f++) { /* ten fingers */
            int x = 96 + f * 18 + (f > 5 ? 10 : 0), h = f == n ? 8 : 22;
            mq_rrect(q, x, MQ_Y0 + 150 - h, 10, h, 4, f == n ? Q_GREY : Q_PEACH);
        }
    }
    for (int i = 0; i <= t->step && i < 6; i++)
        mq_text(q, api->font_small, 90, MQ_Y0 + 30 + i * 18, l[i], i == 5 || (t->sub && i == 4) ? Q_HOTPINK : Q_INK);
    for (int v = 1; v <= 10; v++) {
        char s[4];
        api->snprintf(s, sizeof s, "%d", v);
        mq_button(q, 4 + (v - 1) * 31, MQ_Y0 + 158, 29, 22, B_TOY, 50 + v, s, v == n ? Q_YELLOW : Q_WHITE);
    }
    mq_button(q, 4, MQ_Y0 + 184, 150, 20, B_TOY, 3, t->sub ? "think of a number" : "the 9s trick", Q_LAV);
    mq_button(q, 164, MQ_Y0 + 184, 150, 20, B_TOY, 4, "next step", Q_MINT);
}

/* ------------------------------------------------------------------ growing dots */
static void draw_grow(mq_t *q)
{
    mq_toy_t *t = T;
    const modx_api_t *api = q->api;
    mq_fill(q, 0, MQ_Y0, MQ_W, MQ_H, RGB(236, 250, 244));
    int k = t->step + 1, n = 0, cx = 120, by = MQ_Y0 + 170;
    static const char kind[3][12] = {"triangle", "square", "star"};
    int grow = (int)imin(1000, (int)(q->anim_t - t->say_t) * 3); /* the newest row pops in */
    for (int r = 1; r <= k; r++) {
        int w = t->sub == 0 ? r : t->sub == 1 ? k : 2 * r - 1;
        for (int i = 0; i < w; i++) {
            int x = cx + (i - (w - 1) / 2) * 12 - ((w - 1) % 2) * 6, y = by - (k - r) * 12;
            if (t->sub == 1)
                x = cx - (k - 1) * 6 + i * 12;
            int rad = r == k ? 1 + 4 * grow / 1000 : 5;
            mq_circle(q, x, y, rad, r == k ? Q_HOTPINK : Q_PURPLE);
            n++;
        }
    }
    char s[40];
    api->snprintf(s, sizeof s, "%s numbers: step %d has %d dots", kind[t->sub % 3], k, n);
    mq_text(q, api->font_small, 6, MQ_Y0 + 4, s, Q_INK);
    mq_button(q, 230, MQ_Y0 + 60, 84, 30, B_TOY, 5, "grow!", Q_MINT);
    mq_button(q, 230, MQ_Y0 + 100, 84, 26, B_TOY, 6, "next shape", Q_LAV);
    mq_button(q, 230, MQ_Y0 + 132, 84, 26, B_TOY, 7, "start over", Q_PEACH);
}

/* ------------------------------------------------------------------ the mandala */
static const uint16_t mpal[5] = {Q_WHITE, Q_HOTPINK, Q_SKY, Q_YELLOW, Q_MINT};
static const char mname[5][8] = {"white", "pink", "blue", "yellow", "mint"};
static void draw_mandala(mq_t *q)
{
    mq_toy_t *t = T;
    const modx_api_t *api = q->api;
    mq_fill(q, 0, MQ_Y0, MQ_W, MQ_H, RGB(252, 246, 236));
    int cx = 104, cy = MQ_Y0 + 104;
    static const uint8_t ring_n[3] = {8, 8, 4}; /* outer, middle, inner */
    for (int r = 0; r < 3; r++) {
        int rad = 92 - r * 30;
        for (int s = 0; s < ring_n[r]; s++) {
            int a0 = s * 360 / ring_n[r], a1 = (s + 1) * 360 / ring_n[r];
            int16_t px[24], py[24];
            int m = 0;
            px[m] = (int16_t)cx, py[m++] = (int16_t)cy;
            for (int a = a0; a <= a1 && m < 23; a += 6)
                px[m] = (int16_t)(cx + mq_isin(a) * rad / 1024), py[m++] = (int16_t)(cy - mq_icos(a) * rad / 1024);
            mq_poly(q, px, py, m, mpal[t->cell[r * 8 + s] % 5]);
            mq_line(q, cx, cy, cx + mq_isin(a0) * rad / 1024, cy - mq_icos(a0) * rad / 1024, Q_DIM);
        }
        mq_ring(q, cx, cy, rad, Q_DIM);
    }
    for (int c = 0; c < 5; c++) /* the paint pots */
        mq_button(q, 210 + (c % 3) * 36, MQ_Y0 + 10 + (c / 3) * 34, 32, 30, B_TOY, 120 + c, NULL, mpal[c]);
    mq_rect(q, 210 + (t->sel % 3) * 36 - 2, MQ_Y0 + 8 + (t->sel / 3) * 34, 36, 34, Q_INK);
    /* each colour's share of the outer ring, as a fraction */
    int y = MQ_Y0 + 84;
    for (int c = 1; c < 5; c++) {
        int k = 0;
        for (int s = 0; s < 8; s++)
            k += t->cell[s] == c;
        if (!k)
            continue;
        char s[24];
        api->snprintf(s, sizeof s, "%d/8 %s", k, mname[c]);
        mq_text(q, api->font_small, 212, y, s, Q_INK);
        y += 14;
    }
    char g[32];
    api->snprintf(g, sizeof g, "Make %d/8 of the outside %s!", t->goal_n, mname[t->goal_c]);
    mq_text(q, api->font_small, 6, MQ_Y0 + 191, g, Q_PURPLE);
}

static void mandala_tap(mq_t *q, int x, int y)
{
    mq_toy_t *t = T;
    int cx = 104, cy = MQ_Y0 + 104, dx = x - cx, dy = y - cy, r2 = dx * dx + dy * dy;
    if (r2 > 92 * 92)
        return;
    int ring = r2 > 62 * 62 ? 0 : r2 > 32 * 32 ? 1 : 2, n = ring == 2 ? 4 : 8;
    int s = mq_angle_of(-dy, dx) * n / 360 % n; /* clockwise from 12 o'clock */
    t->cell[ring * 8 + s] = t->sel;
    mq_sfx(q, MODX_SFX_SQUISH, s);
    int k = 0;
    for (int i = 0; i < 8; i++)
        k += t->cell[i] == t->goal_c;
    if (k == t->goal_n && !t->solved) {
        t->solved = 1;
        clever(q, "Beautiful! That's exactly the right share!");
    }
}

/* ------------------------------------------------------------------ the secret code */
static const char messages[8][12] = {"YOU ROCK", "HUGS", "CAKE PARTY", "SUPER STAR", "BEST FRIEND", "HELLO",
                                     "YAY YOU", "LETS PLAY"};
static void code_new(mq_t *q)
{
    mq_toy_t *t = T;
    t->msg = (uint8_t)mq_rnd(q, 0, 7);
    t->key = (uint8_t)mq_rnd(q, 1, 4); /* the rule: letter number + key */
    for (int i = 0; i < 12; i++)
        t->cell[i] = 0;
    t->sel = 0, t->solved = 0;
}
static void draw_code(mq_t *q)
{
    mq_toy_t *t = T;
    const modx_api_t *api = q->api;
    const char *m = messages[t->msg & 7];
    mq_fill(q, 0, MQ_Y0, MQ_W, MQ_H, RGB(236, 244, 255));
    char s[48];
    api->snprintf(s, sizeof s, "%s sent a secret! The rule: letter + %d", mq_cast_name(q, t->msg % MQ_NCAST), t->key);
    if (!say_on(q))
        mq_text(q, api->font_small, 6, MQ_Y0 + 4, s, Q_INK);
    mq_text(q, api->font_small, 6, MQ_Y0 + 18, "A=1 B=2 C=3 ... Z=26", Q_DIM);
    int n = 0;
    for (int i = 0; m[i]; i++)
        n++;
    for (int i = 0; m[i]; i++) {
        int x = 6 + i * 28, y = MQ_Y0 + 44;
        if (m[i] == ' ')
            continue;
        api->snprintf(s, sizeof s, "%d", m[i] - 'A' + 1 + t->key);
        mq_rrect(q, x, y, 26, 18, 5, Q_WHITE);
        mq_text_c(q, api->font_small, x + 13, y + 3, s, Q_PURPLE);
        mq_rrect(q, x, y + 22, 26, 26, 5, i == t->sel ? Q_YELLOW : Q_WHITE);
        mq_add_btn(q, x, y + 22, 26, 26, B_TOY, 70 + i);
        if (t->cell[i]) {
            char c[2] = {(char)t->cell[i], 0};
            mq_text_c(q, api->font_large, x + 13, y + 26, c, t->cell[i] == m[i] ? Q_GREEN : Q_HOTPINK);
        }
    }
    for (int k = 0; k < 26; k++) { /* the letter keys */
        char c[2] = {(char)('A' + k), 0};
        mq_button(q, 4 + (k % 13) * 24, MQ_Y0 + 120 + (k / 13) * 26, 22, 24, B_TOY, 80 + k, c, Q_LAV);
    }
    if (t->solved) {
        api->snprintf(s, sizeof s, "\"%s\"", m);
        mq_text_c(q, api->font_large, 160, MQ_Y0 + 96, s, Q_HOTPINK);
        mq_button(q, 230, MQ_Y0 + 180, 84, 22, B_TOY, 8, "another!", Q_MINT);
    }
    (void)n;
}
static void code_put(mq_t *q, char c)
{
    mq_toy_t *t = T;
    const char *m = messages[t->msg & 7];
    if (t->solved || !m[t->sel])
        return;
    t->cell[t->sel] = (uint8_t)c;
    mq_sfx(q, MODX_SFX_TAP, 0);
    do /* on to the next letter */
        t->sel++;
    while (m[t->sel] == ' ');
    if (!m[t->sel])
        t->sel = 0;
    bool all = true;
    for (int i = 0; m[i]; i++)
        all &= m[i] == ' ' || t->cell[i] == m[i];
    if (all) {
        t->solved = 1;
        mq_earn(q, 5, NULL);
        clever(q, "Whoa, you cracked the code!");
    }
}

/* ------------------------------------------------------------------ the menu, input, frames */
void mq_toys_open(mq_t *q, int toy)
{
    mq_toy_t *t = T;
    uint8_t *b = (uint8_t *)t;
    uint8_t keep_sym = t->sym;
    for (size_t i = 0; i < sizeof *t; i++)
        b[i] = 0;
    t->mode = (uint8_t)toy, t->sym = keep_sym ? keep_sym : 6;
    switch (toy) {
    case TOY_BEATS: case TOY_JAM: beats_start(q, toy); break;
    case TOY_MAGIC: magic_new(q); break;
    case TOY_CAGE: cage_new(q); break;
    case TOY_MANDALA:
        t->sel = 1, t->goal_c = (uint8_t)mq_rnd(q, 1, 4), t->goal_n = (uint8_t)mq_rnd(q, 1, 7);
        break;
    case TOY_CODE: code_new(q); break;
    case TOY_TRICK: t->pick = 7; break;
    default: break;
    }
    mq_go(q, SC_TOYS);
}

/* the jam as a fun beat (mq_fun.c) */
void mq_toys_jam(mq_t *q, int ret, uint32_t len)
{
    mq_toys_open(q, TOY_JAM);
    T->jam_until = q->anim_t + len, T->jam_ret = (uint8_t)ret;
}

void mq_toys_press(mq_t *q, int arg)
{
    mq_toy_t *t = T;
    q->redraw = true, t->kal_ok = 0;
    if (t->mode == TOY_MENU) {
        if (arg >= 200)
            mq_toys_open(q, arg - 200);
        return;
    }
    if (arg == 0) {
        mq_toys_open(q, TOY_MENU);
        return;
    }
    switch (t->mode) {
    case TOY_BEATS: case TOY_JAM:
        if (arg == 1)
            beats_start(q, TOY_BEATS);
        else if (arg == 10) /* after a song: the free jam */
            mq_toys_open(q, TOY_JAM);
        else if (arg == 9 && !t->done) /* help?: the demo again (the song waits) */
            t->demo_t = mq_mark(q), mq_speak(q, 8, "The cloud asks. Drum the answer!", MODX_SPEAK_NOW);
        else if (arg >= 100 && arg < 104)
            beats_play(q, arg - 100);
        else if (arg >= 110 && arg < 120)
            beats_play(q, NT_DIGIT + arg - 110);
        break;
    case TOY_MAGIC: case TOY_CAGE:
        if (arg == 2) {
            if (t->mode == TOY_MAGIC)
                magic_new(q);
            else
                cage_new(q);
        } else if (arg >= 30 && arg < 39)
            t->sel = (uint8_t)(arg - 30);
        else if (arg >= 40 && arg <= 49)
            grid_put(q, arg - 40);
        break;
    case TOY_TRICK: case TOY_GROW:
        if (arg == 3)
            t->sub ^= 1, t->step = 0;
        else if (arg == 4 || arg == 5) {
            int max = t->mode == TOY_TRICK ? (t->sub ? 4 : 5) : 9;
            if (t->step < max)
                t->step++, t->say_t = q->anim_t;
            if (t->mode == TOY_TRICK && t->step == max)
                clever(q, t->sub ? "Whoa, the 9s trick works every time!" : "Whoa, it's always 3! Try another number!");
            else if (t->mode == TOY_GROW && t->step == 4)
                clever(q, "Look how it grows! Can you guess the next one?");
            mq_sfx(q, MODX_SFX_POP, t->step);
        } else if (arg == 6)
            t->sub = (uint8_t)((t->sub + 1) % 3), t->step = 0;
        else if (arg == 7)
            t->step = 0;
        else if (arg > 50 && arg <= 60)
            t->pick = (uint8_t)(arg - 50), t->step = 0;
        break;
    case TOY_MANDALA:
        if (arg >= 120 && arg < 125)
            t->sel = (uint8_t)(arg - 120);
        break;
    case TOY_CODE:
        if (arg == 8)
            code_new(q);
        else if (arg >= 70 && arg < 80)
            t->sel = (uint8_t)(arg - 70);
        else if (arg >= 80 && arg < 106)
            code_put(q, (char)('A' + arg - 80));
        break;
    default: break;
    }
}

bool mq_toys_key(mq_t *q, int k, int c)
{
    mq_toy_t *t = T;
    if (q->scene != SC_TOYS)
        return false;
    switch (t->mode) {
    case TOY_BEATS: case TOY_JAM:
        if (t->done)
            return false;
        if (t->mode == TOY_BEATS) { /* the four bongos on 1-4 */
            if (c >= '1' && c <= '4') {
                beats_drum(q, c - '1');
                return true;
            }
            return false;
        }
        if (c >= '0' && c <= '9') {
            int d = c - '0';
            bool near_digit = false;
            uint32_t now = q->anim_t - t->t0;
            for (int i = 0; i < t->nn; i++)
                near_digit |= t->note[i].kind >= NT_DIGIT && !t->note[i].state && iabs((int32_t)(now - t->note[i].t)) < 400;
            if (near_digit)
                beats_play(q, NT_DIGIT + d);
            else
                for (int i = 0; i < 4; i++)
                    if (drum_key[i] == c)
                        beats_play(q, i);
            return true;
        }
        return false;
    case TOY_KALEIDO:
        if (c >= '3' && c <= '8') {
            t->sym = (uint8_t)(c - '0');
            t->kal_ok = 0;
            return true;
        }
        if (k == HK_DEL) {
            t->npts = 0, t->drawn = 0, t->kal_ok = 0;
            return true;
        }
        if (k == HK_LEFT || k == HK_RIGHT || k == HK_UP || k == HK_DOWN || k == HK_ENTER) { /* a brush by keys */
            if (!t->npts)
                t->bx = 190, t->by = MQ_Y0 + 70;
            t->bx = (int16_t)(t->bx + (k == HK_LEFT ? -4 : k == HK_RIGHT ? 4 : 0));
            t->by = (int16_t)(t->by + (k == HK_UP ? -4 : k == HK_DOWN ? 4 : 0));
            if (t->npts < TOY_PTS)
                t->pts[t->npts++] = (mq_tpt_t){t->bx, t->by, (uint8_t)(t->npts / 12)};
            return true;
        }
        return false;
    case TOY_MAGIC: case TOY_CAGE:
        if (c >= '1' && c <= '9') {
            grid_put(q, c - '0');
            q->redraw = true;
            return true;
        }
        if (k == HK_DEL)
            grid_put(q, 0);
        if (k == HK_LEFT || k == HK_RIGHT)
            t->sel = (uint8_t)((t->sel + (k == HK_LEFT ? 8 : 1)) % 9);
        if (k == HK_UP || k == HK_DOWN)
            t->sel = (uint8_t)((t->sel + (k == HK_UP ? 6 : 3)) % 9);
        q->redraw = true;
        return k == HK_DEL || k == HK_LEFT || k == HK_RIGHT || k == HK_UP || k == HK_DOWN;
    case TOY_TRICK:
        if (c >= '1' && c <= '9') {
            t->pick = (uint8_t)(c - '0'), t->step = 0, q->redraw = true;
            return true;
        }
        if (k == HK_ENTER) {
            mq_toys_press(q, 4);
            return true;
        }
        return false;
    case TOY_GROW:
        if (k == HK_ENTER || k == HK_RIGHT) {
            mq_toys_press(q, 5);
            return true;
        }
        return false;
    case TOY_CODE: {
        int ch = c >= 'a' && c <= 'z' ? c - 32 : c;
        if (ch >= 'A' && ch <= 'Z') {
            code_put(q, (char)ch);
            q->redraw = true;
            return true;
        }
        return false;
    }
    default: return false;
    }
}

bool mq_toys_touch(mq_t *q, int type, int x, int y)
{
    mq_toy_t *t = T;
    if (q->scene != SC_TOYS)
        return false;
    if (t->mode == TOY_KALEIDO && (type == TV_DOWN || type == TV_DRAG)) {
        int dx = x - 160, dy = y - (MQ_Y0 + 100);
        if (dx * dx + dy * dy < 90 * 90 && t->npts < TOY_PTS) {
            t->pts[t->npts++] = (mq_tpt_t){(int16_t)x, (int16_t)y, (uint8_t)(t->npts / 12)};
            if (t->npts % 6 == 0)
                mq_sfx(q, MODX_SFX_TAP, t->npts);
        }
        return true;
    }
    if (t->mode == TOY_MANDALA && type == TV_TAP && x < 200) {
        mandala_tap(q, x, y);
        return true;
    }
    /* The drums hit on touch-down; the same finger's TAP (or UP) that follows is theirs too and does nothing more:
     * it used to reach the pad's button and drum a second time -- a fizzle right after every right hit ("fizz...",
     * a thud, the combo back to 0: the user, "I think bongo beats is broken"). test_mq_bongo. */
    bool lift = type == TV_TAP || type == TV_UP;
    if (t->mode == TOY_BEATS && (type == TV_DOWN || lift) && !t->done) { /* the bongos hit on touch-down */
        for (int i = 0; i < 4; i++)
            if (x >= pad_x(i) && x < pad_x(i) + 68 && y >= PAD_Y && y < PAD_Y + PAD_H) {
                if (!lift)
                    beats_drum(q, i);
                return true;
            }
    }
    if (t->mode == TOY_JAM && (type == TV_DOWN || lift) && !t->done) { /* pads hit on touch-down */
        for (int i = 0; i < 4; i++)
            if (x >= 132 + i * 46 && x < 174 + i * 46 && y >= MQ_Y0 + 110 && y < MQ_Y0 + 142) {
                if (!lift)
                    beats_play(q, i);
                return true;
            }
        if (y >= MQ_Y0 + 176 && y < MQ_Y0 + 200) {
            if (!lift)
                beats_play(q, NT_DIGIT + iabs(x - 4) / 31 % 10);
            return true;
        }
    }
    return false; /* buttons */
}

void mq_toys_frame(mq_t *q)
{
    if (q->scene != SC_TOYS)
        return;
    if (T->mode == TOY_BEATS || T->mode == TOY_JAM)
        beats_frame(q);
    else
        mq_fun_time(q, MQ_FRAME_MS, false);
}

static void draw_menu(mq_t *q)
{
    const modx_api_t *api = q->api;
    mq_header(q, "Toy box", RGB(255, 246, 236));
    static const uint16_t col[TOY_COUNT] = {0, Q_PINK, Q_PEACH, Q_LAV, Q_MINT, Q_SKY, Q_YELLOW, Q_MINT, Q_PINK, Q_SKY};
    for (int i = 1; i < TOY_COUNT; i++) {
        int k = i - 1, x = 6 + (k % 3) * 104, y = MQ_Y0 + 26 + (k / 3) * 58;
        mq_button(q, x, y, 100, 52, B_TOY, 200 + i, NULL, col[i]);
        mq_text_c(q, api->font_small, x + 50, y + 34, toy_name[i], Q_INK);
        switch (i) { /* a little picture each */
        case TOY_BEATS: case TOY_JAM:
            mq_rrect(q, x + 34, y + 12, 14, 12, 4, Q_BROWN), mq_rrect(q, x + 52, y + 12, 14, 12, 4, Q_BROWN);
            mq_circle(q, x + 50, y + 8, 6, Q_WHITE);
            break;
        case TOY_KALEIDO:
            for (int a = 0; a < 6; a++)
                mq_circle(q, x + 50 + mq_icos(a * 60) * 10 / 1024, y + 16 + mq_isin(a * 60) * 10 / 1024, 3, a % 2 ? Q_HOTPINK : Q_PURPLE);
            break;
        case TOY_MAGIC: case TOY_CAGE: mq_text_c(q, api->font_medium, x + 50, y + 8, i == TOY_MAGIC ? "15" : "3+", Q_INK); break;
        case TOY_TRICK: mq_star(q, x + 50, y + 16, 10, Q_PURPLE); break;
        case TOY_GROW:
            for (int r = 0; r < 3; r++)
                for (int c = 0; c <= r; c++)
                    mq_circle(q, x + 50 - r * 4 + c * 8, y + 8 + r * 7, 3, Q_HOTPINK);
            break;
        case TOY_MANDALA: mq_circle(q, x + 50, y + 16, 11, Q_HOTPINK), mq_circle(q, x + 50, y + 16, 6, Q_SKY); break;
        default: mq_text_c(q, api->font_medium, x + 50, y + 8, "?", Q_INK); break;
        }
    }
}

void mq_toys_draw(mq_t *q)
{
    mq_toy_t *t = T;
    switch (t->mode) {
    case TOY_BEATS: case TOY_JAM: draw_beats(q); break;
    case TOY_KALEIDO: draw_kaleido(q, !t->kal_ok); t->kal_ok = 1; break;
    case TOY_MAGIC: case TOY_CAGE: draw_grid(q); break;
    case TOY_TRICK: draw_trick(q); break;
    case TOY_GROW: draw_grow(q); break;
    case TOY_MANDALA: draw_mandala(q); break;
    case TOY_CODE: draw_code(q); break;
    default: draw_menu(q); break;
    }
    if (say_on(q)) { /* clever thinking, celebrated (in the line on top, the toy's own words there waiting) */
        int w = mq_text_w(q, q->api->font_small, t->say) + 16;
        w = w < 256 ? w : 256;
        mq_rrect(q, 2, MQ_Y0 + 2, w, 16, 8, Q_YELLOW);
        mq_text_fit(q, q->api->font_small, 2 + w / 2, MQ_Y0 + 3, w - 10, t->say, Q_INK, true);
    }
}

/* the kaleidoscope draws only what is new between full frames */
bool mq_toys_partial(const mq_t *q) { return q->scene == SC_TOYS && q->toy.mode == TOY_KALEIDO && q->toy.kal_ok; }

const char *mq_toys_state(const mq_t *q, char *b, size_t n)
{
    const mq_toy_t *t = &q->toy;
    q->api->snprintf(b, n, "%s %d %d %d", toy_name[t->mode < TOY_COUNT ? t->mode : 0], t->score, t->solved, t->combo);
    return b;
}

/* Bongo Beats for the tests (state mq_beats): "drum DT" -- the waiting order's right drum (0..3) and its note's ms
 * to the hit line (negative: still coming); "" with no order waiting */
const char *mq_beats_state(const mq_t *q, char *b, size_t n)
{
    const mq_toy_t *t = &q->toy;
    int ni = t->mode == TOY_BEATS && !t->done ? beats_cur(t) : -1;
    if (ni < 0)
        b[0] = 0;
    else
        q->api->snprintf(b, n, "%d %d", t->oright[t->note[ni].pad & 1], (int)(int32_t)(q->anim_t - t->t0 - t->note[ni].t));
    return b;
}

/* `mq correct` / `mq wrong` in Bongo Beats: the clock to the waiting order's beat, then its right (or a
 * wrong) drum; false: no order */
bool mq_toys_answer(mq_t *q, bool right)
{
    mq_toy_t *t = T;
    if (q->scene != SC_TOYS || t->mode != TOY_BEATS || t->done)
        return false;
    if (t->demo_t)
        t->demo_t = 0, mq_demo_seen(q, BEATS_DEMO);
    int ni = beats_cur(t);
    if (ni < 0)
        return false;
    if (!t->hold)
        t->t0 = q->anim_t - t->note[ni].t; /* on the beat */
    t->press_t[3] = 0, t->last_press = 0; /* (a scripted hit is never hammering) */
    int o = t->note[ni].pad & 1;
    beats_drum(q, right ? t->oright[o] : (t->oright[o] + 1) % 4);
    return true;
}
