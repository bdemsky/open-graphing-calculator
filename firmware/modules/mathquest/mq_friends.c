/*
 * Math Quest: the friends -- what they remember, how close she is to each, their own little stories.
 *
 * Memories: what she did is remembered (mq_more_t.mem, the newest 160): the 40 cookies she made
 * for Pip, the race she won, the finale, the treats from the merge board. The cast talks about them
 * ("You made 40 cookies yesterday!", "I heard you won the race!") in town and at the round's end;
 * the play day says today, yesterday or the other day.
 *
 * Hearts: every order solved for a friend, every wish granted, brings her closer to them
 * (mq_extra_t.heart). Best friends among the cast stroll together and talk about each other.
 *
 * Their stories: every friend has a little story in four steps (a chapter a play day at most, when
 * she has enough hearts with them): Pip's giant sunflower, Dot's letter with no address, Momo's
 * surprise cake, Olli's star map, Tango's pet shop, Dabble's two toys who get married, Bramble's
 * talent show, Shelly's big race, Nova's trip home. The friend comes running up in town with news
 * (a "!" over them); a tap tells the step as a little scene, and the last step is the payoff: a
 * party with everyone dancing, a toy wedding with petals and a cake, a trip in a balloon, a boat or
 * a train. Momo's kitchen and Tango's pet shop open new games (mq_cook.c).
 *
 * Their voices: every line a friend says fires the voice sound (MODX_SFX_VOICE: the slot and the
 * syllables); the standalone page speaks it as little blips, each friend with their own pitch.
 */
#include "mq.h"

static int imin(int a, int b) { return a < b ? a : b; }

mq_extra_t *mq_extra(mq_t *q) { return &q->save.extra[q->save.cur & 1]; }
mq_more_t *mq_more(mq_t *q) { return &q->save.more[q->save.cur & 1]; }

/* a version-5 save (or a fresh block): the long memory starts with the eight memories version 5
 * kept (mq_extra_t.mem, emptied: only the long memory is used from now on) */
void mq_more_migrate(mq_t *q, int prof)
{
    mq_more_t *m = &q->save.more[prof & 1];
    mq_extra_t *e = &q->save.extra[prof & 1];
    uint8_t *b = (uint8_t *)m;
    for (size_t i = 0; i < sizeof *m; i++)
        b[i] = 0;
    for (int i = 0; i < MQ_NMEM; i++) {
        m->mem[i] = e->mem[i];
        m->remembered += e->mem[i].kind != 0;
        e->mem[i] = (mq_mem_t){0};
    }
}

/* a new player, or an older save: the block starts fresh, with a little sparkle energy */
void mq_extra_migrate(mq_t *q, int prof)
{
    mq_extra_t *e = &q->save.extra[prof & 1];
    uint8_t *b = (uint8_t *)e;
    for (size_t i = 0; i < sizeof *e; i++)
        b[i] = 0;
    e->energy = 10;
    mq_more_migrate(q, prof);
}

/* the voice of a friend saying s (the page blips a syllable a word, up to 15) */
/* A character says s: the speak hook (the Mac babbles it in the character's voice, or reads it aloud
 * in words for reading help; nothing on the calculator), and the typewriter and the talking mouth of
 * the caption on the calculator (mq_t.say_t). flags: MODX_SPEAK_NOW interrupts what is being said. */
void mq_speak(mq_t *q, int slot, const char *s, int flags)
{
    if (!s || !s[0])
        return;
    q->say_t = mq_mark(q), q->say_slot = (uint8_t)slot;
    int mode = mq_pace(q)->voice;
    if (mode == VO_OFF || q->api->size <= offsetof(modx_api_t, speak) || !q->api->speak)
        return;
    q->api->speak((slot & 0xFF) | (mode == VO_WORDS ? MODX_SPEAK_WORDS : 0) | flags, s);
}

void mq_voice(mq_t *q, int slot, const char *s)
{
    mq_speak(q, slot, s, 0);
    if (mq_pace(q)->voice == VO_OFF)
        return;
    const char *s0 = s;
    (void)s0;
    int w = 0, in = 0;
    for (; s && *s; s++) {
        bool l = (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z');
        if (l && !in)
            w++;
        in = l;
    }
    mq_sfx(q, MODX_SFX_VOICE, (slot % MQ_NCAST) * 16 + (w < 1 ? 1 : w > 15 ? 15 : w));
}

/* ------------------------------------------------------------------ memories, hearts */
static const char item_name[SI_COUNT][10] = {"bead",   "cookie", "star",  "flower", "friend", "seed",  "candle", "balloon",
                                             "shell",  "letter", "marble", "apple", "leaf",   "card",  "light",  "ladybug",
                                             "slice",  "egg",    "drop",  "coin",  "chair",  "carrot"};

void mq_heart_add(mq_t *q, int slot, int n)
{
    mq_extra_t *e = mq_extra(q);
    if (slot < 0 || slot >= MQ_NCAST)
        return;
    int h = e->heart[slot] + n * mq_events_hearts(q, slot); /* (a parade day, their birthday: double) */
    e->heart[slot] = (uint8_t)(h > 250 ? 250 : h);
    q->dirty = true;
}

void mq_count(mq_t *q, int ct, int n)
{
    mq_extra_t *e = mq_extra(q);
    if (ct >= 0 && ct < 8 && e->count[ct] < 0xFFFFFF00u)
        e->count[ct] += (uint32_t)n;
}

/* remember something she did (the same kind for the same friend on the same day adds up) */
void mq_remember(mq_t *q, int kind, int slot, int item, int world, int n)
{
    mq_more_t *e = mq_more(q);
    uint16_t day = mq_prof(q)->days;
    mq_mem_t *m = &e->mem[0];
    if (m->kind == kind && m->slot == slot && m->item == item && m->day == day) {
        int v = m->n + n;
        m->n = (uint16_t)(v > 60000 ? 60000 : v);
        return;
    }
    for (int i = MQ_NMEMORY - 1; i > 0; i--)
        e->mem[i] = e->mem[i - 1];
    if (e->remembered < 0xFFFFFFF0u)
        e->remembered++;
    m->kind = (uint8_t)kind, m->slot = (uint8_t)slot, m->item = (uint8_t)item, m->world = (uint8_t)world;
    m->day = day, m->n = (uint16_t)(n < 0 ? 0 : n > 60000 ? 60000 : n);
    q->dirty = true;
}

/* an order solved in a round: a memory of the story, hearts for its friends, sparkle energy */
void mq_friends_right(mq_t *q, const mq_prob_t *p, bool first)
{
    mq_extra_t *e = mq_extra(q);
    int st = p->stg ? p->stg - 1 : p->story ? p->story - 1 : -1, kind = MEM_HELP, n = 0;
    switch (st) {
    case ST_GROUPS: case ST_MUL: case ST_RECIPE: case ST_ADD: case ST_TIMES: kind = MEM_MADE, n = p->kind == K_NUM ? (int)p->ans : 0; break;
    case ST_SHARE: case ST_PERONE: case ST_SHAREADD: case ST_DIVR: case ST_LEFT: case ST_TAKE: case ST_MARBLES:
    case ST_HOWMANY: case ST_FULL:
        kind = MEM_SHARED, n = (int)p->stg_n[0];
        break;
    case ST_BUS: kind = MEM_BUS; break;
    case ST_READY: case ST_HOWLONG: kind = MEM_TIME; break;
    case ST_CHANGE: kind = MEM_CHANGE; break;
    case ST_RUG: case ST_FENCE: kind = MEM_RUG; break;
    default: break;
    }
    if (st >= 0)
        mq_remember(q, kind, p->ca % MQ_NCAST, p->stg_i < SI_COUNT ? p->stg_i : 0, mq_skill[p->skill].world, n);
    mq_heart_add(q, p->ca % MQ_NCAST, 1);
    if (p->cb != p->ca && (mq_prof(q)->problems % 2))
        mq_heart_add(q, p->cb % MQ_NCAST, 1);
    mq_count(q, CT_ORDERS, 1);
    if (first && e->energy < 60) /* the merge board's energy comes only from playing (a picnic day: double) */
        e->energy = (uint8_t)(e->energy + mq_events_energy(q) > 60 ? 60 : e->energy + mq_events_energy(q));
}

static const char *when(mq_t *q, const mq_mem_t *m)
{
    int d = mq_prof(q)->days - m->day;
    return d <= 0 ? "today" : d == 1 ? "yesterday" : "the other day";
}

/* best friends among the cast (they stroll together) and what they do together */
static const uint8_t best_of[MQ_NCAST] = {2, 3, 0, 8, 5, 4, 7, 6, 3};
static const char together[MQ_NCAST][28] = {"baked sunflower bread", "read letters by moonlight", "baked sunflower bread",
                                            "counted shooting stars", "painted the shop sign", "painted the shop sign",
                                            "practised for the show", "practised for the show", "counted shooting stars"};
int mq_best_friend(int slot) { return best_of[slot % MQ_NCAST]; }

/* What friend `who` says now (k: which line, rotating): a memory of what she did -- their own
 * first -- or what they did with their best friend, or a hello. Returns the length. */
int mq_chatter(mq_t *q, int who, int k, char *out, int n)
{
    const modx_api_t *api = q->api;
    mq_extra_t *e = mq_extra(q);
    mq_more_t *mo = mq_more(q);
    mq_profile_t *p = mq_prof(q);
    who %= MQ_NCAST;
    /* their own memories first (the newest six, however long ago: a friend never forgets), then
     * what anyone saw her do in the last three days (the newest four) */
    enum { OWN = 6, ANY = 4 };
    int cand[OWN + ANY], nc = 0;
    for (int i = 0; i < MQ_NMEMORY && nc < OWN; i++)
        if (mo->mem[i].kind && mo->mem[i].slot == who)
            cand[nc++] = i;
    for (int i = 0, j = 0; i < MQ_NMEMORY && j < ANY; i++)
        if (mo->mem[i].kind && mo->mem[i].slot != who && p->days - mo->mem[i].day <= 3)
            cand[nc++] = i, j++;
    int pick = k % (nc + 2);
    if (pick < nc) {
        const mq_mem_t *m = &mo->mem[cand[pick]];
        bool mine = m->slot == who;
        char things[24];
        mq_plural(item_name[m->item % SI_COUNT], things, m->n);
        switch (m->kind) {
        case MEM_MADE:
            if (m->n)
                return api->snprintf(out, n, mine ? "You made %u %s %s!" : "I heard you made %u %s!", (unsigned)m->n, things,
                                     mine ? when(q, m) : "");
            break;
        case MEM_SHARED:
            if (m->n)
                return api->snprintf(out, n, mine ? "You shared %u %s so fairly!" : "%u %s, shared fair! Nice!", (unsigned)m->n,
                                     things);
            break;
        case MEM_BUS: return api->snprintf(out, n, "Everyone got a seat %s!", when(q, m));
        case MEM_TIME: return api->snprintf(out, n, mine ? "We were right on time %s!" : "Always right on time!", when(q, m));
        case MEM_CHANGE: return api->snprintf(out, n, "You gave the right change!");
        case MEM_RUG: return api->snprintf(out, n, mine ? "My rug fits just right now!" : "That rug fits just right!");
        case MEM_FINALE: return api->snprintf(out, n, "What a %s finale!", mq_world_name[m->world % MQ_NWORLD]);
        case MEM_RACE: return api->snprintf(out, n, mine ? "You won the race %s!" : "I heard you won a race!", when(q, m));
        case MEM_MERGE: return api->snprintf(out, n, "Thanks for the treat! Yum!");
        case MEM_CHAPTER: return api->snprintf(out, n, mine ? "Best day ever, %s!" : "I heard about the party!", mq_name(q));
        case MEM_GIFT: return api->snprintf(out, n, "Thank you for the present!");
        default: return api->snprintf(out, n, mine ? "Thanks for helping %s!" : "You help everyone!", when(q, m));
        }
    }
    if (pick == nc && ((p->met >> best_of[who]) & 1))
        return api->snprintf(out, n, "%s and I %s!", mq_cast_name(q, best_of[who]), together[who]);
    if (pick == nc + 1 && mq_event_chat(q)[0]) /* today's special */
        return mq_events_birthday(q) == who ? api->snprintf(out, n, "It's my birthday, %s!", mq_name(q))
                                            : api->snprintf(out, n, "%s", mq_event_chat(q));
    if (e->heart[who] >= 20)
        return api->snprintf(out, n, "You're my best friend, %s!", mq_name(q));
    return api->snprintf(out, n, "Hi, %s!", mq_name(q));
}

/* ------------------------------------------------------------------ their stories */
enum { PAY_PARTY, PAY_WEDDING, PAY_TRIP };
static const uint8_t pay_kind[MQ_NCAST] = {PAY_PARTY, PAY_TRIP, PAY_PARTY, PAY_TRIP, PAY_PARTY, PAY_WEDDING, PAY_PARTY, PAY_TRIP,
                                          PAY_TRIP};
static const char story_title[MQ_NCAST][24] = {"The giant sunflower", "The letter",      "The surprise cake",
                                               "The star map",        "The pet shop",    "Button and Bean",
                                               "The talent show",     "The big race",    "Home in the sky"};
static const char story_line[MQ_NCAST][4][3][36] = {
    {{"I planted a tiny seed!", "Will you help it grow?", "Let's water it every day!"},
     {"Look! A little sprout!", "It's taller than me now!", "Momo says it needs sun."},
     {"It has a bud! A BIG one!", "I think it opens soon...", "Come back tomorrow!"},
     {"It bloomed! A giant sunflower!", "A garden party for everyone!", "Thank you for helping me!"}},
    {{"A letter with no address!", "Who could it be for?", "It smells like snowflakes..."},
     {"A clue! A tiny blue feather.", "From the Snowy Isle, maybe?", "I've never been so far!"},
     {"I packed my scarf and map.", "Will you come with me?", "We sail on the next fun day!"},
     {"We made it to the Snowy Isle!", "The letter was for my grandma!", "She made us cocoa! Yum!"}},
    {{"Shh! It's Olli's birthday soon!", "I want to bake a surprise cake.", "But I need a new kitchen..."},
     {"My new kitchen is ready!", "Come cook with me, to the beat!", "Cake Kitchen is open!"},
     {"The cake has five layers now!", "Shh, don't tell Olli!", "The party is tomorrow!"},
     {"SURPRISE! Happy birthday, Olli!", "Olli is so happy he hoots!", "Best party ever!"}},
    {{"Hoo! I'm counting the stars.", "There are SO many of them!", "Can you help me map them?"},
     {"I found a new star!", "I named it after you!", "It twinkles pink, like you."},
     {"Tomorrow there's a star shower!", "Let's have a night picnic.", "I'll bring cocoa and blankets."},
     {"Look! Falling stars!", "Make a wish! I wished for...", "...more nights like this!"}},
    {{"I found a lost puppy!", "It needs a home and food.", "I wish I had a pet shop..."},
     {"I opened a tiny pet shop!", "Will you help me run it?", "The Pet Shop is open!"},
     {"The puppy found a family!", "Now kittens want homes too.", "We need a big sign!"},
     {"Grand opening! Look at the sign!", "Every pet found a friend.", "You're the best shop helper!"}},
    {{"I sewed a little bear: Button!", "He looks a bit lonely.", "Maybe he needs a friend?"},
     {"I sewed a bunny: Bean!", "Button and Bean love to play.", "They hold hands all day!"},
     {"Button asked Bean to marry him!", "We need flowers and a cake.", "Will you come? Please?"},
     {"Button and Bean are married!", "Everyone throws flower petals!", "Best friends forever!"}},
    {{"I'm shy... but I can juggle!", "Don't tell anyone, OK?", "Well, maybe just you."},
     {"The fair has a talent show.", "Should I try? I'm scared.", "Will you cheer for me?"},
     {"I practised all day!", "Three apples! Four apples!", "The show is tomorrow!"},
     {"I did it! Everyone clapped!", "I'm not scared anymore.", "Thank you for cheering!"}},
    {{"Everyone says snails are slow.", "I want to win the Grand Race!", "Will you train with me?"},
     {"My new shell wheels! Zoom!", "I'm faster every day!", "Watch me go!"},
     {"The Grand Race is by the sea!", "Pack your bag, we're going!", "We leave on the next fun day!"},
     {"We're at the seaside track!", "I WON! A snail won!", "Slow and steady, then zoom!"}},
    {{"I fell from the sky long ago.", "I miss my star family.", "Can stars go home?"},
     {"I found a map of the sky!", "My home is past the moon.", "We need something that flies!"},
     {"A balloon, made of wishes!", "Let's fill it with sparkles!", "Tomorrow we fly!"},
     {"We're flying! Look, the moon!", "My family waves hello!", "Now I have two homes!"}},
};
static const uint8_t heart_need[4] = {2, 8, 15, 24};
#define CHAP_LINES 3

const char *mq_story_title(int slot) { return story_title[slot % MQ_NCAST]; }

/* the step of friend slot's story ready to be told now (-1: none): met, enough hearts, and a
 * later play day than the last step (one step a day: it unfolds over days) */
int mq_chapter_ready(mq_t *q, int slot)
{
    mq_extra_t *e = mq_extra(q);
    mq_profile_t *p = mq_prof(q);
    if (slot < 0 || slot >= MQ_NCAST || !p->used)
        return -1;
    int k = e->chap[slot];
    if (k >= 4 || !(((p->met >> slot) & 1) || slot == p->buddy % MQ_NCAST || slot == 0))
        return -1;
    if (e->heart[slot] < heart_need[k] || (k > 0 && p->days <= e->chap_day[slot]))
        return -1;
    return k;
}

/* a friend with news (a step ready), the buddy last; -1 none */
int mq_chapter_news(mq_t *q)
{
    int b = mq_prof(q)->buddy % MQ_NCAST;
    for (int s = 0; s < MQ_NCAST; s++)
        if (s != b && mq_chapter_ready(q, s) >= 0)
            return s;
    return mq_chapter_ready(q, b) >= 0 ? b : -1;
}

void mq_chapter_open(mq_t *q, int slot)
{
    int k = mq_chapter_ready(q, slot);
    if (k < 0)
        return;
    q->chap_slot = (uint8_t)slot, q->chap_step = (uint8_t)k, q->chap_line = 0;
    q->chap_t = q->anim_t;
    mq_go(q, SC_CHAPTER);
    mq_voice(q, slot, story_line[slot][k][0]);
    mq_sfx(q, MODX_SFX_WHOOSH, 0);
}

/* the step told: its rewards, the next line or back to town */
static void chapter_done(mq_t *q)
{
    mq_extra_t *e = mq_extra(q);
    int s = q->chap_slot, k = q->chap_step;
    e->chap[s] = (uint8_t)(k + 1);
    e->chap_day[s] = mq_prof(q)->days;
    mq_heart_add(q, s, 3);
    mq_count(q, CT_CHAPTERS, 1);
    if (s == 2 && k == 1)
        e->games_open |= 1u << EG_COOK; /* Momo's kitchen */
    if (s == 4 && k == 1)
        e->games_open |= 1u << EG_PETS; /* Tango's pet shop */
    if (k == 3) {
        mq_remember(q, MEM_CHAPTER, s, 0, 0, 0);
        mq_earn(q, 40, "A day to remember! +40");
        mq_home(q)->dough = (uint8_t)imin(99, mq_home(q)->dough + 2);
        mq_sfx(q, MODX_SFX_FANFARE, s);
    } else
        mq_earn(q, 5, NULL);
    q->dirty = true;
    mq_save_now(q);
    mq_go(q, SC_MAP);
}

bool mq_chapter_last(const mq_t *q) { return q->chap_line + 1 >= CHAP_LINES; }
void mq_chapter_next(mq_t *q)
{
    int len = 0;
    while (story_line[q->chap_slot][q->chap_step][q->chap_line][len])
        len++;
    if ((int)(q->anim_t - q->chap_t) < len * mq_type_ms(q)) { /* still being said: all of it at once */
        q->chap_t = q->anim_t - (uint32_t)(len * mq_type_ms(q));
        return;
    }
    if (q->chap_line + 1 < CHAP_LINES) {
        q->chap_line++;
        q->chap_t = q->anim_t;
        mq_voice(q, q->chap_slot, story_line[q->chap_slot][q->chap_step][q->chap_line]);
        mq_sfx(q, MODX_SFX_TAP, q->chap_line);
        return;
    }
    chapter_done(q);
}

/* ------------------------------------------------------------------ drawing the story */
static int world_of(int slot) { return slot == 7 ? G_FAIR : slot == 8 ? G_SKY : slot; }

/* what each step shows beside the friend (from shapes) */
static void chap_prop(mq_t *q, int s, int k, int x, int y)
{
    uint32_t t = q->anim_t;
    switch (s) {
    case 0: { /* a seed, a sprout, a bud, the giant sunflower */
        mq_oval(q, x, y, 22, 6, Q_BROWN);
        int h = k == 0 ? 0 : k == 1 ? 18 : k == 2 ? 40 : 66;
        if (h)
            mq_fill(q, x - 1, y - h, 3, h, Q_GREEN), mq_oval(q, x - 8, y - h / 2, 7, 3, Q_GREEN), mq_oval(q, x + 8, y - h / 3, 7, 3, Q_GREEN);
        if (k == 0)
            mq_oval(q, x, y - 3, 4, 3, RGB(140, 100, 70));
        if (k == 2)
            mq_circle(q, x, y - h - 6, 8, Q_GREEN), mq_circle(q, x, y - h - 8, 4, Q_YELLOW);
        if (k == 3) {
            for (int i = 0; i < 12; i++)
                mq_oval(q, x + mq_icos(i * 30 + (int)(t / 20 % 30)) * 20 / 1024, y - h - 6 + mq_isin(i * 30) * 20 / 1024, 7, 7,
                        Q_YELLOW);
            mq_circle(q, x, y - h - 6, 13, RGB(150, 100, 60));
            mq_fill(q, x - 5, y - h - 8, 2, 3, Q_INK), mq_fill(q, x + 4, y - h - 8, 2, 3, Q_INK);
            mq_fill(q, x - 3, y - h - 2, 7, 2, Q_HOTPINK);
        }
        break;
    }
    case 1: /* the letter */
        mq_rrect(q, x - 18, y - 30, 36, 24, 3, Q_WHITE), mq_rect(q, x - 18, y - 30, 36, 24, Q_DIM);
        mq_line(q, x - 18, y - 30, x, y - 16, Q_DIM), mq_line(q, x + 17, y - 30, x, y - 16, Q_DIM);
        mq_heart(q, x, y - 16, 5, k >= 1 ? Q_SKY : Q_HOTPINK);
        break;
    case 2: /* the cake, layer by layer */
        for (int i = 0; i <= k + 1; i++)
            mq_rrect(q, x - 24 + i * 3, y - 12 - i * 11, 48 - i * 6, 12, 4, i % 2 ? Q_PINK : Q_CREAM);
        mq_fill(q, x - 1, y - 24 - (k + 1) * 11, 3, 10, Q_SKY), mq_circle(q, x, y - 26 - (k + 1) * 11, 3, Q_ORANGE);
        break;
    case 3: /* the stars */
        for (int i = 0; i < 4 + k * 2; i++)
            mq_star(q, x - 30 + (i * 37) % 64, y - 70 + (i * 23) % 56, (i + (int)(t / 300)) % 3 ? 4 : 6, i == 1 && k >= 1 ? Q_PINK : Q_YELLOW);
        break;
    case 4: /* the puppy, the shop */
        mq_item_draw(q, mq_pet_id(q) ? mq_pet_id(q) : 1, x - 16, y - 34, 0, (int)(t / 400 % 2), 2, -1);
        if (k >= 1)
            mq_rrect(q, x - 34, y - 70, 68, 16, 5, Q_ORANGE), mq_text_c(q, q->api->font_small, x, y - 69, "PETS", Q_WHITE);
        break;
    case 5: { /* Button (a bear) and Bean (a bunny) */
        int bx = x - 14, nx = x + 14;
        mq_circle(q, bx, y - 14, 10, Q_BROWN), mq_circle(q, bx - 7, y - 23, 4, Q_BROWN), mq_circle(q, bx + 7, y - 23, 4, Q_BROWN);
        mq_fill(q, bx - 4, y - 16, 2, 2, Q_INK), mq_fill(q, bx + 3, y - 16, 2, 2, Q_INK);
        if (k >= 1) {
            mq_circle(q, nx, y - 14, 10, Q_WHITE), mq_oval(q, nx - 4, y - 30, 3, 8, Q_WHITE), mq_oval(q, nx + 4, y - 30, 3, 8, Q_WHITE);
            mq_fill(q, nx - 4, y - 16, 2, 2, Q_INK), mq_fill(q, nx + 3, y - 16, 2, 2, Q_INK);
            mq_heart(q, x, y - 34 - (int)(t / 300 % 2) * 2, 5, Q_HOTPINK);
        }
        break;
    }
    case 6: /* juggling apples */
        for (int i = 0; i < 2 + k; i++) {
            int a = (int)(t / 4 % 360) + i * 360 / (2 + k);
            mq_circle(q, x + mq_icos(a) * 18 / 1024, y - 40 + mq_isin(a) * 18 / 1024, 5, RGB(240, 90, 100));
        }
        break;
    case 7: /* the shell wheels, the trophy */
        mq_circle(q, x - 10, y - 6, 6, Q_INK), mq_circle(q, x + 10, y - 6, 6, Q_INK);
        mq_circle(q, x - 10, y - 6, 3, Q_GREY), mq_circle(q, x + 10, y - 6, 3, Q_GREY);
        if (k >= 2)
            mq_rrect(q, x - 8, y - 46, 16, 18, 5, Q_YELLOW), mq_fill(q, x - 2, y - 28, 4, 10, Q_YELLOW), mq_fill(q, x - 8, y - 18, 16, 4, Q_YELLOW);
        break;
    default: /* the balloon of wishes */
        if (k >= 2) {
            mq_oval(q, x, y - 52, 22, 26, Q_LAV), mq_oval(q, x - 6, y - 60, 6, 8, Q_WHITE);
            mq_line(q, x - 14, y - 30, x - 8, y - 12, Q_DIM), mq_line(q, x + 14, y - 30, x + 8, y - 12, Q_DIM);
            mq_rrect(q, x - 10, y - 14, 20, 12, 3, Q_BROWN);
        } else
            mq_rrect(q, x - 20, y - 30, 40, 26, 4, RGB(64, 72, 140)), mq_star(q, x + 8, y - 20, 5, Q_YELLOW);
        break;
    }
}

/* the payoff: a party, a toy wedding, a trip */
static void payoff(mq_t *q, int s, int el)
{
    mq_profile_t *p = mq_prof(q);
    uint32_t t = q->anim_t;
    static const uint16_t conf[5] = {Q_HOTPINK, Q_YELLOW, Q_SKY, Q_MINT, Q_LAV};
    switch (pay_kind[s]) {
    case PAY_PARTY: { /* the story's thing (the sunflower, the cake, the shop, the apples), everyone she
                       * has met dancing in a row, balloons and confetti */
        mq_zoom(q, 20, 252, MQ_Y0 + 196);
        chap_prop(q, s, 3, 252, MQ_Y0 + 196);
        mq_zoom(q, 16, 0, 0);
        int n = 0;
        for (int k = 0; k < MQ_NCAST && n < 6; k++)
            if (((p->met >> k) & 1) || k == s || k == p->buddy % MQ_NCAST) {
                int x = 20 + n * 48;
                mq_cast(q, k, x, MQ_Y0 + 150, 40, (n + (int)(t / 500)) % 3 ? AN_DANCE : AN_HAPPY);
                n++;
            }
        for (int i = 0; i < 6; i++) { /* balloons */
            int bx = 30 + i * 52, by = MQ_Y0 + 30 + (int)((t / 40 + (uint32_t)i * 13) % 20);
            mq_line(q, bx, by + 9, bx + 2, by + 28, Q_DIM);
            mq_oval(q, bx, by, 7, 9, conf[i % 5]);
        }
        break;
    }
    case PAY_WEDDING: { /* Button and Bean under a flower arch, a cake, petals (drawn half as big again) */
        int cx = 178, by = MQ_Y0 + 196;
        mq_zoom(q, 24, cx, by);
        for (int a = 180; a <= 360; a += 15)
            mq_circle(q, cx + mq_icos(a) * 50 / 1024, by - 40 + mq_isin(a) * 60 / 1024, 6, (a / 15) % 2 ? Q_PINK : Q_WHITE);
        mq_circle(q, cx - 16, by - 16, 13, Q_BROWN), mq_circle(q, cx - 25, by - 28, 5, Q_BROWN), mq_circle(q, cx - 7, by - 28, 5, Q_BROWN);
        mq_fill(q, cx - 20, by - 18, 2, 2, Q_INK), mq_fill(q, cx - 13, by - 18, 2, 2, Q_INK);
        mq_poly(q, (int16_t[3]){(int16_t)(cx - 21), (int16_t)(cx - 11), (int16_t)(cx - 16)}, (int16_t[3]){(int16_t)(by - 8), (int16_t)(by - 8), (int16_t)(by - 4)}, 3, Q_INK);
        mq_circle(q, cx + 16, by - 16, 13, Q_WHITE), mq_oval(q, cx + 11, by - 36, 4, 10, Q_WHITE), mq_oval(q, cx + 21, by - 36, 4, 10, Q_WHITE);
        mq_fill(q, cx + 12, by - 18, 2, 2, Q_INK), mq_fill(q, cx + 19, by - 18, 2, 2, Q_INK);
        mq_oval(q, cx + 16, by - 30, 15, 6, RGB(250, 250, 255)); /* her veil */
        mq_heart(q, cx, by - 46 - (int)(t / 250 % 3), 6, Q_HOTPINK);
        mq_rrect(q, cx + 50, by - 30, 30, 30, 4, Q_CREAM), mq_rrect(q, cx + 55, by - 44, 20, 14, 4, Q_PINK); /* the cake */
        mq_zoom(q, 16, 0, 0);
        for (int i = 0; i < 14; i++) { /* petals */
            int px = (i * 53 + (int)(t / 30)) % 320, py = MQ_Y0 + 20 + (int)((t / 20 + (uint32_t)i * 37) % 180);
            mq_oval(q, px, py, 3, 2, i % 2 ? Q_PINK : Q_WHITE);
        }
        mq_cast(q, s, 20, MQ_Y0 + 120, 56, AN_HAPPY);
        break;
    }
    default: { /* a trip: the friend and her buddy in a balloon (sky), a boat (sea) or a train */
        int bx = 160 + mq_isin((int)(t / 8 % 360)) * 20 / 1024, by = MQ_Y0 + 70 + mq_isin((int)(t / 5 % 360)) * 6 / 1024;
        for (int i = 0; i < 4; i++) { /* clouds go by */
            int cx = (int)(340 - (t / 12 + (uint32_t)i * 90) % 380), cy = MQ_Y0 + 40 + i * 30;
            mq_circle(q, cx, cy, 8, Q_WHITE), mq_circle(q, cx + 10, cy - 3, 10, Q_WHITE), mq_circle(q, cx + 22, cy, 7, Q_WHITE);
        }
        if (s == 1 || s == 7) { /* a boat on the waves */
            for (int x = 0; x < 320; x += 16)
                mq_oval(q, x + (int)(t / 30 % 16), MQ_Y0 + 186, 10, 4, Q_SKY);
            mq_poly(q, (int16_t[4]){(int16_t)(bx - 50), (int16_t)(bx + 50), (int16_t)(bx + 36), (int16_t)(bx - 36)},
                    (int16_t[4]){(int16_t)(MQ_Y0 + 160), (int16_t)(MQ_Y0 + 160), (int16_t)(MQ_Y0 + 182), (int16_t)(MQ_Y0 + 182)}, 4, Q_HOTPINK);
            mq_fill(q, bx - 1, MQ_Y0 + 96, 3, 64, Q_BROWN);
            mq_poly(q, (int16_t[3]){(int16_t)(bx + 2), (int16_t)(bx + 2), (int16_t)(bx + 40)},
                    (int16_t[3]){(int16_t)(MQ_Y0 + 98), (int16_t)(MQ_Y0 + 150), (int16_t)(MQ_Y0 + 150)}, 3, Q_WHITE);
            mq_cast(q, s, bx - 44, MQ_Y0 + 122, 40, AN_HAPPY);
            mq_buddy_draw(q, p->buddy % MQ_NCAST, bx - 2, MQ_Y0 + 122, 2, 1);
        } else { /* a balloon of wishes */
            by += 6;
            mq_oval(q, bx, by - 20, 40, 46, s == 8 ? Q_LAV : Q_PINK);
            mq_oval(q, bx - 12, by - 36, 10, 14, Q_WHITE);
            mq_line(q, bx - 30, by + 12, bx - 22, by + 50, Q_DIM), mq_line(q, bx + 30, by + 12, bx + 22, by + 50, Q_DIM);
            mq_cast(q, s, bx - 40, by + 12, 40, AN_HAPPY);
            mq_buddy_draw(q, p->buddy % MQ_NCAST, bx, by + 12, 2, 1);
            mq_rrect(q, bx - 34, by + 44, 68, 30, 6, Q_BROWN);
            mq_fill(q, bx - 34, by + 50, 68, 3, RGB(140, 100, 70));
            if (s == 3 || s == 8)
                for (int i = 0; i < 5; i++)
                    mq_star(q, (i * 67 + (int)(t / 25)) % 320, MQ_Y0 + 24 + i * 9, 3, Q_YELLOW);
        }
        break;
    }
    }
    for (int i = 0; i < 18 && el > 0; i++) { /* confetti everywhere */
        int x = (i * 71 + (int)(t / 9)) % 320, y = MQ_Y0 + 20 + (int)((t / 6 + (uint32_t)i * 41) % 190);
        mq_fill(q, x, y, 3, 2 + i % 2, conf[i % 5]);
    }
}

void mq_chapter_draw(mq_t *q)
{
    const modx_api_t *api = q->api;
    int s = q->chap_slot % MQ_NCAST, k = q->chap_step & 3, li = q->chap_line % CHAP_LINES;
    int el = (int)(q->anim_t - q->chap_t);
    q->clip_y0 = MQ_Y0, q->clip_y1 = MQ_Y0 + MQ_H; /* (the scenery's sky reaches up) */
    mq_backdrop(q, world_of(s), MQ_Y0, MQ_Y0 + MQ_H);
    bool pay = k == 3 && li >= 1;
    if (pay)
        payoff(q, s, el);
    else {
        chap_prop(q, s, k, 236, MQ_Y0 + 186);
        mq_cast(q, s, 18, MQ_Y0 + 104, 80, el < 1400 ? AN_TALK : k == 3 ? AN_DANCE : li == 2 ? AN_HAPPY : AN_IDLE);
    }
    /* the title: whose story, its step as hearts */
    mq_rrect(q, 60, MQ_Y0 + 4, 200, 20, 9, Q_WHITE);
    mq_text_c(q, api->font_small, 160 - 18, MQ_Y0 + 7, story_title[s], Q_PURPLE);
    for (int i = 0; i < 4; i++)
        mq_heart(q, 220 + i * 10, MQ_Y0 + 14, 4, i <= k ? Q_HOTPINK : Q_GREY);
    /* the line, said a letter at a time */
    const char *line = story_line[s][k][li];
    char shown[36];
    int n = 0, upto = el / mq_type_ms(q); /* (a typewriter at her reading pace) */
    while (line[n] && n < upto && n < 35)
        shown[n] = line[n], n++;
    shown[n] = 0;
    int bx = pay ? 30 : 100, bw = pay ? 260 : 212, by = pay ? MQ_Y0 + 30 : MQ_Y0 + 40;
    const font_t *f = api->text_width(api->font_medium, line) <= bw - 16 ? api->font_medium : api->font_small;
    mq_rrect(q, bx, by + 2, bw, 30, 12, RGB(220, 210, 228));
    mq_rrect(q, bx, by, bw, 30, 12, Q_WHITE);
    if (!pay)
        mq_poly(q, (int16_t[3]){(int16_t)(bx + 4), (int16_t)(bx + 18), (int16_t)(bx - 14)},
                (int16_t[3]){(int16_t)(by + 16), (int16_t)(by + 26), (int16_t)(by + 34)}, 3, Q_WHITE);
    mq_text(q, f, bx + 10, by + (f == api->font_small ? 9 : 7), shown, Q_INK);
    if (!line[n] && (q->anim_t / 400) % 2) /* more to come: a little arrow */
        mq_poly(q, (int16_t[3]){(int16_t)(bx + bw - 16), (int16_t)(bx + bw - 8), (int16_t)(bx + bw - 12)},
                (int16_t[3]){(int16_t)(by + 20), (int16_t)(by + 20), (int16_t)(by + 25)}, 3, Q_HOTPINK);
}

/* `mq chapter SLOT` (tests): the friend's next step, ready or not */
int mq_chapter_console(mq_t *q, int slot)
{
    mq_extra_t *e = mq_extra(q);
    if (slot < 0 || slot >= MQ_NCAST || e->chap[slot] >= 4)
        return -1;
    if (e->heart[slot] < heart_need[e->chap[slot]])
        e->heart[slot] = heart_need[e->chap[slot]];
    if (e->chap[slot] && e->chap_day[slot] >= mq_prof(q)->days)
        e->chap_day[slot] = (uint16_t)(mq_prof(q)->days - 1);
    mq_prof(q)->met |= (uint16_t)(1u << slot);
    mq_chapter_open(q, slot);
    return 0;
}

int mq_chapter_lines(void) { return CHAP_LINES; }
