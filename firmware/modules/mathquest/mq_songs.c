/*
 * Math Quest: Bongo Beats' songs -- ten original pop-style songs (written for Sparkle Town; none is
 * a quote of a real song: docs/MATHQUEST.md "Bongo Beats' songs" has the check), as compact note data.
 *
 * Each song: a name, a tempo (90..130 beats a minute), a form over two four-bar sections (A the verse,
 * B the chorus with the hook), and per section the chords, the melody (meowed by the cats) and a
 * one-bar beat (the bongo layer, every bar of the section); the bassline is the chords' roots in the
 * song's bass rhythm.
 *
 *   melody   "E5:2 G5:1 r:1 | ..."   a note (C..B, # or b, the octave) or r (a rest), ':' its length in
 *                                    eighths; '|' a bar line (every bar 8 eighths: 4/4)
 *   chords   "C:8 Am:4 F#m:4"        a root (m: minor) and its length in eighths
 *   bass     "2222"                  the bass's rhythm in a bar (eighths; the root of the chord sounding)
 *   beat     "L..HL.H.L..HL.HC"      sixteenths: L the low bongo, H the high, C cowbell, X clap,
 *                                    T tambourine, . nothing
 *
 * mq_song_events() turns a song into timed events (sixteenths from the song's start), which Bongo
 * Beats plays along its beat clock: the melody as the cats' mews (MODX_SFX_MEW), the beat as paws on
 * the bongos and accents, the bass and the chords (MODX_SFX_BASS / MODX_SFX_CHORD) -- on the Mac;
 * the calculator shows them (the notes bouncing, the cats' mouths and paws).
 */
#include "mq.h"

typedef struct {
    char name[22];
    uint8_t bpm;
    char form[6];        /* the sections in order: A, B */
    char bass[9];        /* the bass rhythm (eighths, summing to 8) */
    char chords[2][48], mel[2][128], beat[2][17]; /* (arrays: a module keeps no pointers in its data) */
} mq_song_t;

static const mq_song_t songs[MQ_NSONG] = {
    {"Sparkle Pop Party", 124, "ABAB", "2222",
     {"C:8 Em:8 F:8 G:8", "F:8 G:8 Em:8 F:4 G:4"},
     {"E5:1 G5:1 G5:2 E5:1 D5:1 C5:2 | D5:1 E5:1 G5:2 B4:2 r:2 | A4:1 C5:1 C5:2 F5:1 E5:1 C5:2 | D5:2 G5:2 D5:2 r:2",
      "C6:2 A5:1 G5:1 A5:2 C6:2 | B5:1 G5:1 D5:2 G5:4 | G5:1 G5:1 E5:2 G5:1 E5:1 C5:2 | A4:2 B4:2 C5:4"},
     {"L...H...L.L.H...", "L.C.H.C.L.L.H.CT"}},
    {"Bubblegum Bounce", 116, "ABAB", "3122",
     {"G:8 C:8 G:8 D:8", "C:8 D:8 Bm:8 Em:4 D:4"},
     {"B4:1 D5:1 G5:1 D5:1 B4:2 r:2 | C5:1 E5:1 G5:1 E5:1 C5:2 r:2 | B4:1 D5:1 G5:1 B5:1 A5:2 G5:2 | F#5:2 E5:1 F#5:1 D5:4",
      "E5:1 G5:1 E5:1 G5:1 C6:2 B5:2 | A5:1 F#5:1 D5:2 A5:2 r:2 | B5:2 D6:1 B5:1 F#5:2 B5:2 | G5:2 E5:1 G5:1 F#5:4"},
     {"L..HL.H.L..HL.HH", "L..HL.HXL..HL.HX"}},
    {"Lazy Cloud Lo-Fi", 92, "AABB", "44",
     {"Dm:8 G:8 C:8 Am:8", "F:8 Em:8 Dm:8 G:8"},
     {"A4:3 C5:1 D5:2 F5:2 | E5:4 D5:2 r:2 | G4:3 C5:1 E5:2 G5:2 | E5:6 r:2",
      "F5:2 E5:1 D5:1 C5:2 A4:2 | B4:2 G5:4 r:2 | A5:2 G5:1 F5:1 D5:2 F5:2 | E5:2 D5:2 B4:4"},
     {"L.......H..T....", "L......LH..T..T."}},
    {"Disco Whiskers", 118, "ABAB", "11111111",
     {"Am:4 D:4 Am:4 D:4 Am:4 D:4 Em:4 Am:4", "F:8 G:8 Em:8 Am:8"},
     {"A4:1 r:1 A4:1 C5:1 E5:2 D5:1 C5:1 | B4:1 A4:1 r:2 F#5:2 E5:2 | A4:1 r:1 A4:1 C5:1 E5:2 G5:1 E5:1 | D5:2 B4:2 A4:4",
      "C6:1 C6:1 A5:2 G5:1 A5:1 C6:2 | B5:2 G5:2 D5:4 | B5:1 B5:1 G5:2 E5:1 G5:1 B5:2 | A5:6 r:2"},
     {"L.C.H.CLL.C.H.C.", "L.C.HXC.L.C.HXCT"}},
    {"Starlight Jump", 128, "ABAB", "2222",
     {"Bm:8 G:8 D:8 A:8", "G:8 A:8 F#m:8 Bm:8"},
     {"B4:1 B4:1 D5:1 B4:1 F#5:2 E5:2 | D5:1 D5:1 E5:1 D5:1 B4:2 r:2 | A4:1 A4:1 C#5:1 E5:1 A5:2 G5:1 F#5:1 | E5:2 C#5:2 A4:4",
      "D6:2 B5:2 G5:1 A5:1 B5:2 | C#6:2 A5:2 E5:4 | F#5:1 A5:1 C#6:2 B5:1 A5:1 F#5:2 | B5:6 r:2"},
     {"L.H.L.HXL.H.L.HX", "L.HXL.HXL.HXLLHX"}},
    {"Mango Mambo Meow", 96, "ABAB", "3122",
     {"Em:8 C:8 G:8 D:8", "C:8 D:8 Em:8 Em:8"},
     {"E5:2 G5:1 E5:1 r:1 B4:1 D5:2 | C5:1 E5:1 C5:1 A4:1 r:2 G4:2 | B4:1 D5:1 G5:2 F#5:1 G5:1 A5:2 | F#5:3 D5:1 r:4",
      "G5:1 G5:1 A5:1 G5:1 E5:2 C5:2 | D5:1 D5:1 E5:1 F#5:1 A5:4 | B5:2 A5:1 G5:1 E5:2 G5:2 | E5:4 r:4"},
     {"L..H..H.L..H..H.", "L..H..HTL..H..HT"}},
    {"Jingle Paws", 112, "ABAB", "44",
     {"F:8 Bb:8 F:8 C:8", "Bb:8 F:8 G:8 C:8"},
     {"A4:1 C5:1 F5:2 E5:1 F5:1 G5:2 | F5:1 D5:1 Bb4:2 r:4 | A4:1 C5:1 F5:2 A5:1 G5:1 F5:2 | E5:2 G5:2 C5:4",
      "D5:1 F5:1 Bb5:2 A5:1 G5:1 F5:2 | C5:2 F5:2 A5:4 | G5:1 A5:1 B5:2 D6:1 C6:1 B5:2 | C6:2 G5:2 E5:4"},
     {"L.T.H.T.L.T.H.T.", "L.T.H.TCL.T.H.TC"}},
    {"Rainbow Anthem", 104, "AABB", "2222",
     {"C:8 G:8 Am:8 F:8", "F:8 C:8 G:8 C:8"},
     {"G4:2 C5:2 D5:1 E5:1 G5:2 | G5:1 F5:1 E5:2 D5:4 | E5:2 A5:2 G5:1 E5:1 C5:2 | D5:1 C5:1 A4:2 C5:4",
      "G5:4 F5:2 E5:2 | G5:2 A5:2 C6:4 | B5:2 A5:1 G5:1 E5:2 D5:2 | C5:6 r:2"},
     {"L...H...L.L.H..X", "L.L.H..XL.L.H.XX"}},
    {"Robot Kitty Groove", 120, "ABAB", "11111111",
     {"Am:8 Am:8 F:8 G:8", "Dm:8 Am:8 F:8 E:8"},
     {"A4:1 A5:1 A4:1 G5:1 A4:1 E5:1 r:2 | A4:1 A5:1 A4:1 G5:1 E5:2 C5:2 | F4:1 F5:1 F4:1 E5:1 C5:2 A4:2 | G4:1 G5:1 G4:1 F5:1 D5:4",
      "D5:2 F5:2 A5:2 F5:2 | E5:2 C5:2 A4:4 | C5:1 D5:1 F5:2 A5:1 G5:1 F5:2 | E5:2 G#5:2 B5:4"},
     {"L.HCL.HCL.HCL.HX", "L.HCLXHCL.HCLXHX"}},
    {"Picnic Shuffle", 100, "ABAB", "3131",
     {"D:8 G:8 D:8 A:8", "G:8 D:8 Bm:8 A:8"},
     {"F#5:3 E5:1 D5:2 A4:2 | B4:3 D5:1 G5:4 | F#5:3 A5:1 F#5:2 D5:2 | E5:6 r:2",
      "B5:2 A5:1 G5:1 A5:2 D5:2 | F#5:2 E5:1 D5:1 E5:4 | D5:1 F#5:1 B5:2 A5:1 F#5:1 D5:2 | E5:2 C#5:2 D5:4"},
     {"L..HL.H.L..HL.HC", "L..HLTH.L..HLTHC"}},
};

const char *mq_song_name(int s) { return songs[(unsigned)s % MQ_NSONG].name; }
int mq_song_bpm(int s) { return songs[(unsigned)s % MQ_NSONG].bpm; }

/* a pitch class: C..B with # or b; -1 none. *p advanced past it */
static int pclass(const char **p)
{
    static const int8_t base[7] = {9, 11, 0, 2, 4, 5, 7}; /* A..G */
    const char *s = *p;
    if (*s < 'A' || *s > 'G')
        return -1;
    int pc = base[*s - 'A'];
    s++;
    if (*s == '#')
        pc++, s++;
    else if (*s == 'b')
        pc--, s++;
    *p = s;
    return (pc + 12) % 12;
}
static int num(const char **p)
{
    int v = 0;
    if (**p < '0' || **p > '9')
        return -1;
    while (**p >= '0' && **p <= '9')
        v = v * 10 + *(*p)++ - '0';
    return v;
}

/* the events of one section starting at sixteenth `at` (bars: its bars); -1 a fault (err says) */
static int section(const modx_api_t *api, const mq_song_t *sg, int sec, int at, mq_sev_t *ev, int *n, int max, char *err,
                   int en)
{
    /* the melody: bar by bar, 8 eighths each */
    const char *p = sg->mel[sec];
    int bar = 0, in_bar = 0, t = at;
    for (;;) {
        while (*p == ' ')
            p++;
        if (!*p || *p == '|') {
            if (in_bar != 8) {
                if (err)
                    api->snprintf(err, (size_t)en, "%s %c bar %d: %d eighths", sg->name, 'A' + sec, bar + 1, in_bar);
                return -1;
            }
            bar++, in_bar = 0;
            if (!*p)
                break;
            p++;
            continue;
        }
        int note = -1;
        if (*p == 'r')
            p++;
        else {
            int pc = pclass(&p), oct = num(&p);
            if (pc < 0 || oct < 0) {
                if (err)
                    api->snprintf(err, (size_t)en, "%s %c bar %d: a bad note", sg->name, 'A' + sec, bar + 1);
                return -1;
            }
            note = 12 * (oct + 1) + pc;
        }
        int len = *p == ':' ? (p++, num(&p)) : -1;
        if (len <= 0) {
            if (err)
                api->snprintf(err, (size_t)en, "%s %c bar %d: a bad length", sg->name, 'A' + sec, bar + 1);
            return -1;
        }
        if (note >= 0 && *n < max)
            ev[(*n)++] = (mq_sev_t){(uint16_t)t, SE_MEL, (uint8_t)note, (uint8_t)len};
        t += len * 2, in_bar += len;
    }
    int bars = bar;
    /* the chords, and the bass under them */
    p = sg->chords[sec];
    int ct = 0, croot[64], cn = 0, cend[64];
    for (;;) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        int pc = pclass(&p), minor = 0;
        if (*p == 'm')
            minor = 1, p++;
        int len = *p == ':' ? (p++, num(&p)) : -1;
        if (pc < 0 || len <= 0) {
            if (err)
                api->snprintf(err, (size_t)en, "%s %c: a bad chord", sg->name, 'A' + sec);
            return -1;
        }
        if (*n < max)
            ev[(*n)++] = (mq_sev_t){(uint16_t)(at + ct * 2), SE_CHORD, (uint8_t)(60 + pc), (uint8_t)(len | minor << 7)};
        if (cn < 64)
            croot[cn] = pc, cend[cn++] = ct + len;
        ct += len;
    }
    if (ct != bars * 8) {
        if (err)
            api->snprintf(err, (size_t)en, "%s %c: chords %d eighths for %d bars", sg->name, 'A' + sec, ct, bars);
        return -1;
    }
    int bt = 0;
    for (const char *r = sg->bass; *r; r++)
        bt += *r - '0';
    if (bt != 8) {
        if (err)
            api->snprintf(err, (size_t)en, "%s: the bass rhythm is %d eighths", sg->name, bt);
        return -1;
    }
    for (int b = 0; b < bars; b++) {
        int e = 0;
        for (const char *r = sg->bass; *r; r++) {
            int pos = b * 8 + e, k = 0;
            while (k < cn - 1 && cend[k] <= pos)
                k++;
            if (*n < max)
                ev[(*n)++] = (mq_sev_t){(uint16_t)(at + pos * 2), SE_BASS, (uint8_t)(36 + croot[k]), (uint8_t)(*r - '0')};
            e += *r - '0';
        }
    }
    /* the beat: one bar of sixteenths, every bar */
    const char *bp = sg->beat[sec];
    int bl = 0;
    while (bp[bl])
        bl++;
    if (bl != 16) {
        if (err)
            api->snprintf(err, (size_t)en, "%s %c: the beat is %d sixteenths", sg->name, 'A' + sec, bl);
        return -1;
    }
    for (int b = 0; b < bars; b++)
        for (int k = 0; k < 16; k++) {
            static const char drums[] = "LHCXT";
            for (int d = 0; d < 5; d++)
                if (bp[k] == drums[d] && *n < max)
                    ev[(*n)++] = (mq_sev_t){(uint16_t)(at + b * 16 + k), SE_BEAT, (uint8_t)d, 1};
        }
    return bars;
}

/* Song s as events, sorted by time (sixteenths from its start); its length in sixteenths in *len16.
 * Returns the count, or -1 (err: why) */
int mq_song_events(const modx_api_t *api, int s, mq_sev_t *ev, int max, int *len16, char *err, int en)
{
    const mq_song_t *sg = &songs[(unsigned)s % MQ_NSONG];
    int n = 0, at = 0;
    for (const char *f = sg->form; *f; f++) {
        int bars = section(api, sg, *f == 'B', at, ev, &n, max, err, en);
        if (bars < 0)
            return -1;
        at += bars * 16;
    }
    if (n >= max) {
        if (err)
            api->snprintf(err, (size_t)en, "%s: more than %d events", sg->name, max);
        return -1;
    }
    for (int i = 1; i < n; i++) { /* sorted by time (an insertion sort: the sections come nearly sorted) */
        mq_sev_t e = ev[i];
        int k = i;
        while (k > 0 && ev[k - 1].at > e.at)
            ev[k] = ev[k - 1], k--;
        ev[k] = e;
    }
    if (len16)
        *len16 = at;
    return n;
}
