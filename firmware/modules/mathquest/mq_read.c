/*
 * Math Quest: reading time -- how long a line she reads stays up before it goes (or the game goes on) by
 * itself, and how fast a typewriter types it (docs/MATHQUEST.md "Reading time").
 *
 * Every kid-facing text that leaves or moves on by itself (toasts, a friend's speech, cheers, the why after a
 * miss, the helper's and the guide's lines, the teaching moments, the ways' steps, the cashier's lines, the
 * merge board's and the toys' lines, events and fun beats) lasts mq_read_ms of what it says: a base time (to
 * find the text and start) plus a time per character, never less than MQ_READ_FLOOR. A tap (or a key) skips:
 * a typewriter's first tap finishes the line, the next goes on; a tap on a toast puts it away.
 *
 * The pace is the grown-ups' "Reading speed" (mq_pace_t.read_speed): Auto (the default; an older save's 0)
 * follows her grade -- the school grade the grown-ups said, else the starting grade -- or slower / normal /
 * faster set by hand. The rows come from typical reading rates (oral reading fluency norms, Hasbrouck &
 * Tindal 2017, 50th percentile; silent reading rates, Taylor 1965), at about 6 characters a word with its
 * space, slower than either so a line can be read with a game going on around it:
 *
 *   pace      grade        oral wcpm   silent wpm   here            chars/s   base
 *   slowest   2 and below  ~50-100     ~80-115      125 ms a char    8        2.0 s
 *   comfy     3            ~83-112     ~138          90 ms a char   11        1.5 s
 *   quick     4 and 5      ~94-146     ~158-173      75 ms a char   13        1.2 s
 *   quicker   6 and up     ~132-146    ~185          60 ms a char   16.7      1.0 s
 *
 * The quickest (16.7 a second, 100 words a minute) is still slower than a sixth grader's oral reading.
 * A typewriter types at about 1.6 times the reading pace: she reads along as it types, and the whole line
 * stays up for its reading time from the first letter.
 */
#include "mq.h"

typedef struct {
    uint16_t base, per_char, type;
} mq_rpace_t;
static const mq_rpace_t rows[RP_COUNT] = {
    [RP_SLOWEST] = {2000, 125, 75},
    [RP_COMFY] = {1500, 90, 55},
    [RP_QUICK] = {1200, 75, 45},
    [RP_QUICKER] = {1000, 60, 38},
};

int mq_read_grade(mq_t *q)
{
    int sg = mq_school(q, NULL);
    return sg ? sg : mq_start_grade(q);
}

int mq_read_pace(mq_t *q)
{
    switch (mq_pace(q)->read_speed) {
    case RS_SLOWER: return RP_SLOWEST;
    case RS_NORMAL: return RP_COMFY;
    case RS_FASTER: return RP_QUICK;
    default: break;
    }
    int g = mq_read_grade(q); /* Auto: her grade */
    return g <= 2 ? RP_SLOWEST : g == 3 ? RP_COMFY : g <= 5 ? RP_QUICK : RP_QUICKER;
}

int mq_read_pace_ms(int pace, int nchars)
{
    const mq_rpace_t *r = &rows[pace < 0 || pace >= RP_COUNT ? RP_COMFY : pace];
    if (nchars < 0)
        nchars = 0;
    int ms = r->base + nchars * r->per_char;
    return ms < MQ_READ_FLOOR ? MQ_READ_FLOOR : ms > MQ_READ_CAP ? MQ_READ_CAP : ms;
}

int mq_read_ms_len(mq_t *q, int nchars) { return mq_read_pace_ms(mq_read_pace(q), nchars); }

int mq_read_chars(const char *s)
{
    int n = 0;
    for (; s && *s; s++)
        if ((unsigned char)*s >= ' ' && (*s != ' ' || n)) /* (leading spaces are nothing to read) */
            n++;
    return n;
}

int mq_read_ms(mq_t *q, const char *s) { return mq_read_ms_len(q, mq_read_chars(s)); }

int mq_type_ms(mq_t *q)
{
    int p = mq_read_pace(q);
    return rows[p].type;
}

/* the letters a typewriter shows of the line that started at t0 (all of it once she tapped it) */
int mq_type_shown(mq_t *q, uint32_t t0)
{
    if (q->type_full_t == t0 && t0)
        return 0x7FFF;
    return (int)((q->anim_t + 1 - t0) / (uint32_t)mq_type_ms(q));
}
/* a tap on a typewriter's line (started at t0, nchars long): still typing, it finishes it and the tap is used
 * (true); typed out, false (the caller goes on) */
bool mq_type_tap(mq_t *q, uint32_t t0, int nchars)
{
    if (mq_type_shown(q, t0) >= nchars)
        return false;
    q->type_full_t = t0;
    q->redraw = true;
    return true;
}

/* an auto-advancing text goes after shown_ms: too soon for its reading time without her tap is counted (the
 * tests and the bot's read-time invariant: `state mq_read_short`) */
void mq_read_note(mq_t *q, uint32_t shown_ms, int need_ms, bool tapped)
{
    if (!tapped && (int)shown_ms + MQ_FRAME_MS < need_ms) {
        if (q->read_short < 0xFFFF)
            q->read_short++;
        q->read_short_need = (uint16_t)(need_ms > 0xFFFF ? 0xFFFF : need_ms);
        q->read_short_got = (uint16_t)(shown_ms > 0xFFFF ? 0xFFFF : shown_ms);
    }
    if (q->read_n < 0xFFFF)
        q->read_n++;
}

static const char speed_name[4][8] = {"auto", "slower", "normal", "faster"}; /* (no pointers: no .data) */
const char *mq_read_speed_name(int rs) { return speed_name[rs & 3]; }
