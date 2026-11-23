/*
 * Math Quest code module: entry point, scenes, input, rewards, the parent
 * screen, saving, the console `mq` and the state keys. (Overview in mq.h,
 * design in docs/MATHQUEST.md.)
 *
 * Scenes (q->scene) and how they are driven:
 *   title     two player cards (tap or arrows + ENTER), F5 = the grown-ups' gate (mq_gate.c)
 *   name      new player: pick a buddy, type a name (ALPHA letters or the
 *             on-screen letters)
 *   map       the worlds (Garden, Post Office, Bakery, Clock Tower, Shop,
 *             Art Studio, Fair, Sky Castle = challenge track) and the Arcade;
 *             a world opens when the boss before it is beaten
 *   world     the levels as stepping stones (stars earned), the boss at the end
 *   intro     a level's skill; offers "show me how" before a new skill
 *   play      a problem: the story, its visual model, the answer pad
 *             (digits, / . R , keys, choice tiles, slices to tap, a number
 *             line to tap); F1 "show me" any time
 *   feedback  after a wrong answer: the likely misconception named gently,
 *             the model with the right answer, then a similar problem
 *   offer     after two misses in a row: "want me to show you how?"
 *   example   the worked example, step by step, paced by taps
 *   done      level celebration: stars, sticker, mastered badge, boss rewards
 *   arcade    the four fact games; factgame plays one (mq_facts.c)
 *   room      the home (mq_home.c): rooms to decorate by drag and drop, the
 *             shop, the catalogue book, dress-up, show-off mode
 *   stickers  the sticker book, friends met and hats
 *   parent    behind the grown-ups' gate (a problem past her curriculum): standards and domains, the fact grid,
 *             time played and the daily limit, settings (11-12 facts, theme)
 *   complete  the whole quest done; break: the daily limit reached;
 *             surprise: the daily gift box
 *
 * Every scene draws its buttons through mq_add_btn(); a tap hits a button,
 * arrows move a focus ring over them and ENTER presses the focused one, so
 * every screen works by touch and by keys. Nothing is ever lost: wrong
 * answers lead to help and a similar problem, the session ends on a win
 * (an extra easier problem if the last one was missed), there is no
 * timer that ends a game badly. No sound: all feedback is visual (sparkles,
 * hops, combo flames, celebration screens).
 *
 * Saving: the profiles are copied to the core (api->save_put) at most once
 * a second after a change and at the end of every level and game; the core
 * keeps them in the session (blob "modx.save").
 */
#include "mq.h"

#define KEYC(k) (k)
/* key codes (core/gen/keymap_gen.h), repeated here as numbers the module may use */
enum {
    K_F1 = 1, K_F2, K_F3, K_F4, K_F5, K_DEL = 8, K_UP = 12, K_LEFT = 13, K_RIGHT = 14, K_DOWN = 15, K_CLEAR = 20,
    K_DIV = 30, K_COMMA = 36, K_DOT = 47, K_ENTER = 49
};
#define SAVE_NAME "mathquest"

static const char scene_name[SC_COUNT][10] = {"title", "name", "map", "world", "intro", "play", "feedback",
                                              "example", "done", "arcade", "factgame", "room", "stickers",
                                              "parent", "complete", "break", "offer", "surprise", "shop", "book",
                                              "showoff", "wardrobe", "fun", "action", "preview", "toys", "chapter",
                                              "merge"};
static const char game_name[FG_COUNT][16] = {"Star Catch", "Cafe Rush", "Blooming Garden", "Snail Race"};
static const char praise[6][12] = {"Yay!", "Great!", "Super!", "You did it!", "Sparkly!", "Wow!"};
/* Kid-facing words are game words only (tests/unit/test_mathquest.c scans every kid screen for
 * school words); skills, standards and facts live on the grown-up screen. Each world is a story
 * chapter with a goal, and a run is an order board to fill. */
static const char chapter[MQ_NWORLD][24] = {"The garden show", "Parcel rescue", "%s's bakery", "The tower clock",
                                            "%s's shop", "The big mural", "The fun fair", "Sky Castle", "Star Harbor"};
static const char goal[MQ_NWORLD][28] = {"Win the garden show!", "Rescue every parcel!", "Open the bakery!",
                                         "Fix the tower clock!", "Open the shop!", "Paint the big mural!",
                                         "Run the fun fair!", "Reach the Sky Castle!", "Sort the star ships!"};
static const uint8_t limits[7] = {0, 10, 15, 20, 30, 45, 60};

bool mq_fg_finished(mq_t *q);
const char *mq_scene_name(mq_t *q);
const char *mq_scene_name(mq_t *q) { return scene_name[q->scene % SC_COUNT]; }
const char *mq_scene_name_of(int s) { return scene_name[s >= 0 && s < SC_COUNT ? s : 0]; }

/* ------------------------------------------------------------------ helpers */
mq_profile_t *mq_prof(mq_t *q) { return &q->save.prof[q->save.cur & 1]; }

/* a player's name: in full in mq_more_t.name (from reserved bytes); an older save's (up to 10
 * characters) in mq_profile_t.name, copied there on load */
const char *mq_pname(const mq_t *q, int slot)
{
    const char *s = q->save.more[slot & 1].name;
    return s[0] ? s : q->save.prof[slot & 1].name;
}
const char *mq_name(const mq_t *q) { return mq_pname(q, q->save.cur); }
void mq_pname_set(mq_t *q, int slot, const char *s)
{
    char *full = q->save.more[slot & 1].name, *old = q->save.prof[slot & 1].name;
    int n = 0;
    while (s[n] && n < MQ_NAME_MAX)
        n++;
    for (int i = 0; i < (int)sizeof q->save.more[0].name; i++)
        full[i] = i < n ? s[i] : 0;
    for (int i = 0; i < (int)sizeof q->save.prof[0].name; i++)
        old[i] = i < n && i < (int)sizeof q->save.prof[0].name - 1 ? s[i] : 0;
}

int mq_rnd(mq_t *q, int lo, int hi)
{
    uint32_t x = q->rng ? q->rng : 0x9E3779B9u;
    x ^= x << 13, x ^= x >> 17, x ^= x << 5;
    q->rng = x;
    if (hi <= lo)
        return lo;
    return lo + (int)(x % (uint32_t)(hi - lo + 1));
}

const char *mq_cast_name(mq_t *q, int slot)
{
    if (q->theme.loaded)
        for (int i = 0; i < q->theme.nspr; i++)
            if (q->theme.role[i] == slot && q->theme.names[i][0])
                return q->theme.names[i];
    return mq_art_cast_name[slot >= 0 && slot < MQ_NCAST ? slot : 0];
}

int mq_add_btn(mq_t *q, int x, int y, int w, int h, int id, int arg)
{
    if (q->nbtn >= MQ_MAXBTN)
        return -1;
    q->btn[q->nbtn] = (mq_btn_t){(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, (uint8_t)id, (uint8_t)arg};
    return q->nbtn++;
}

void mq_spark_burst(mq_t *q, int x, int y, int n)
{
    if (q->frame_us > 25000u) /* over budget (a slow frame): fewer particles */
        n = n / 2 + 1;
    static const uint16_t cols[5] = {Q_PINK, Q_YELLOW, Q_LAV, Q_MINT, Q_SKY};
    for (int i = 0; i < MQ_NSPARK && n > 0; i++)
        if (!q->spark[i].life) {
            int a = mq_rnd(q, 0, 359), sp = mq_rnd(q, 40, 140);
            q->spark[i] = (mq_spark_t){(int16_t)(x * 16), (int16_t)(y * 16), (int16_t)(mq_icos(a) * sp / 400),
                                       (int16_t)(mq_isin(a) * sp / 400 - 40), (uint8_t)mq_rnd(q, 12, 24),
                                       (uint8_t)mq_rnd(q, 0, 1), cols[mq_rnd(q, 0, 4)]};
            n--;
        }
}

static void sparks_step(mq_t *q)
{
    for (int i = 0; i < MQ_NSPARK; i++) {
        mq_spark_t *s = &q->spark[i];
        if (!s->life)
            continue;
        s->x = (int16_t)(s->x + s->vx), s->y = (int16_t)(s->y + s->vy), s->vy = (int16_t)(s->vy + 4);
        s->life--;
    }
}

static bool sparks_alive(mq_t *q)
{
    for (int i = 0; i < MQ_NSPARK; i++)
        if (q->spark[i].life)
            return true;
    return false;
}

void mq_sparks_draw(mq_t *q)
{
    for (int i = 0; i < MQ_NSPARK; i++) {
        mq_spark_t *s = &q->spark[i];
        if (!s->life)
            continue;
        int x = s->x / 16, y = s->y / 16;
        if (y < MQ_Y0 + 26 || y > MQ_Y0 + MQ_H - 4 || x < 2 || x > MQ_W - 4) /* below the title line */
            continue;
        if (s->kind)
            mq_star(q, x, y, 4, s->col);
        else
            mq_heart(q, x, y, 3, s->col);
    }
}

void mq_save_now(mq_t *q)
{
    q->save.magic = MQ_SAVE_MAGIC;
    q->save.version = MQ_SAVE_VERSION;
    q->save.size = MQ_SAVE_SIZE16(sizeof q->save);
    q->api->save_put(SAVE_NAME, &q->save, sizeof q->save);
    q->dirty = false;
}
/* progress worth keeping now (a round won, a purchase, a placement, something new): saved, and the core asked to
 * write the session at its next quiet moment (debounced there), so a battery pull loses little (the bot's 9) */
void mq_keep(mq_t *q)
{
    mq_save_now(q);
    const modx_api_t *a = q->api;
    if (a->size >= offsetof(modx_api_t, save_soon) + sizeof a->save_soon && a->save_soon)
        a->save_soon();
}

static uint16_t world_col(mq_t *q, int w, int k)
{
    static const uint16_t bg[MQ_NWORLD][3] = {
        {RGB(232, 250, 238), Q_GREEN, Q_MINT},  {RGB(234, 244, 255), Q_BLUE, Q_SKY},
        {RGB(255, 238, 244), Q_HOTPINK, Q_PINK}, {RGB(243, 238, 255), Q_PURPLE, Q_LAV},
        {RGB(255, 243, 230), Q_ORANGE, Q_PEACH}, {RGB(255, 252, 230), Q_ORANGE, Q_YELLOW},
        {RGB(240, 252, 250), Q_GREEN, Q_MINT},  {RGB(236, 236, 255), Q_PURPLE, Q_LAV},
        {RGB(228, 240, 252), Q_BLUE, Q_SKY}};
    if (q->theme.loaded && q->theme.has_skin && w < THM_NWORLD && q->theme.skin[w][k])
        return q->theme.skin[w][k];
    return bg[w < MQ_NWORLD ? w : 0][k];
}

/* the cast member who guides world w (the challenge track has the star sprite) */
static int guide(int w) { return w >= G_SKY ? 8 : w; }

#define world_open mq_world_open

static int skill_world_level(int skill, int *level)
{
    for (int w = 0; w < MQ_NWORLD; w++)
        for (int l = 0; l < MQ_MAXLV; l++)
            if (mq_levels[w][l] == skill + 1) {
                *level = l;
                return w;
            }
    *level = 0;
    return 0;
}

/* ------------------------------------------------------------------ scene changes */
static void set_softkeys(mq_t *q)
{
    /* game buttons with icons (app/softkeys.c draws a label's first byte 1..15 as an icon): 1 bulb,
     * 2 wand, 3 house, 4 book, 5 star, 6 friends, 7 key, 8 bag, 9 door, 10 undo, 11 sparkle,
     * 12 camera, 13 heart, 14 bow, 15 tick */
    const modx_api_t *api = q->api;
    static const char none[1] = "";
    char lab[5][10] = {"", "", "", "", ""};
    switch (q->scene) {
    case SC_TITLE: /* (leaving is this explicit key or HOME: CLEAR never leaves -- the bot's 19) */
        api->snprintf(lab[3], 10, "\x09" "exit");
        api->snprintf(lab[4], 10, "\x07grown-up");
        break;
    case SC_MAP:
        api->snprintf(lab[0], 10, "\x03home");
        api->snprintf(lab[1], 10, "\x04" "book");
        api->snprintf(lab[2], 10, "\x05" "arcade");
        api->snprintf(lab[3], 10, "\x06players");
        api->snprintf(lab[4], 10, "\x07grown-up");
        break;
    case SC_QUIZ:
        api->snprintf(lab[0], 10, "\x01trick");
        api->snprintf(lab[4], 10, "\x03" "back");
        break;
    case SC_PLAY:
        api->snprintf(lab[0], 10, "\x01show me");
        if (q->gm.power[0]) /* a power-up to use */
            api->snprintf(lab[1], 10, "\x02power");
        api->snprintf(lab[2], 10, "help?");
        api->snprintf(lab[4], 10, "\x03" "back");
        q->sk_power = q->gm.power[0];
        break;
    case SC_PARENT:
        api->snprintf(lab[0], 10, "skills");
        api->snprintf(lab[1], 10, "facts");
        api->snprintf(lab[2], 10, "time");
        api->snprintf(lab[3], 10, "setup");
        api->snprintf(lab[4], 10, "\x0f" "done");
        break;
    case SC_ROOM: case SC_SHOP: case SC_BOOK: case SC_SHOWOFF: case SC_WARDROBE: mq_home_softkeys(q, lab); break;
    case SC_MERGE: /* the Sparkle Kitchen: what to do now, and back to town */
        api->snprintf(lab[2], 10, "help?");
        api->snprintf(lab[4], 10, "\x03" "back");
        break;
    default: api->snprintf(lab[4], 10, "\x03" "back"); break;
    }
    for (int i = 0; i < 5; i++) /* (a high-resolution display: the words alone, no pixel-art icons) */
        api->softkey(i, !lab[i][0] ? none : q->scale > 1 && (unsigned char)lab[i][0] < 16 ? lab[i] + 1 : lab[i]);
}
void mq_softkeys_update(mq_t *q) { set_softkeys(q); }

static void go(mq_t *q, int scene)
{
    q->prev_scene = q->scene;
    q->scene = (uint8_t)scene;
    q->scene_t = q->anim_t;
    q->focus = 0;
    q->nbtn = 0;
    q->redraw = true;
    q->gate.shown = false;
    q->reset_arm = false;
    if (q->prev_scene != scene) {
        mq_wg_reset(q); /* (nothing held from the old scene: CR2-07) */
        q->trans_t = q->anim_t ? q->anim_t : 1; /* the curtain opens on the new scene */
        for (int i = 0; i < 8; i++) /* coins land, banners go, with the old scene */
            q->fly[i].t0 = 0;
        if (scene != SC_DONE_LEVEL && q->earn_msg_t) /* (one she saw goes with the old scene; one still waiting for a lane stays) */
            q->earn_msg_t = q->now - mq_toast_ms(q) ? q->now - mq_toast_ms(q) : 1;
        if (scene == SC_MAP)
            mq_town_enter(q); /* the street, her place on it */
        if (scene == SC_DONE_LEVEL || scene == SC_COMPLETE) /* a round won: kept (written soon) */
            mq_keep(q);
        switch (scene) { /* the music of the place (sound hooks: the web page plays them) */
        case SC_TITLE: case SC_NAME: mq_music(q, MODX_MUS_TITLE, 0); break;
        case SC_MAP: case SC_WORLD: case SC_INTRO: mq_music(q, MODX_MUS_MAP, scene == SC_MAP ? 0 : 1); break;
        case SC_ROOM: case SC_SHOP: case SC_BOOK: case SC_WARDROBE: case SC_SHOWOFF: mq_music(q, MODX_MUS_CAFE, 0); break;
        case SC_BREAK: mq_music(q, MODX_MUS_WINDDOWN, 0); break;
        case SC_DONE_LEVEL: case SC_COMPLETE: mq_music(q, MODX_MUS_MAP, 2); break;
        case SC_ARCADE: case SC_FACTGAME: case SC_ACTION: mq_music(q, MODX_MUS_ARCADE, 1); break;
        default: break;
        }
    }
    set_softkeys(q);
}
void mq_go(mq_t *q, int scene) { go(q, scene); }
static void press(mq_t *q, int id, int arg);
static void done_open(mq_t *q);
void mq_press(mq_t *q, int id, int arg) { press(q, id, arg); }
/* on with the round: the game (or the grown-ups' preview of the question screen) */
static void to_play(mq_t *q) { go(q, q->preview ? SC_QUIZ : SC_PLAY); }

void mq_sfx(mq_t *q, int id, int param)
{
    if (q->api->size >= offsetof(modx_api_t, music) && q->api->sfx)
        q->api->sfx(id, param);
}
void mq_music(mq_t *q, int track, int level)
{
    if (q->api->size >= offsetof(modx_api_t, display_scale) && q->api->music && (q->music_now != track || q->music_lv != level)) {
        q->music_now = (uint8_t)track, q->music_lv = (uint8_t)level;
        q->api->music(track, level);
    }
}

/* ------------------------------------------------------------------ levels */
static void new_problem(mq_t *q, int skill, int diff);

/* The skill of the run's next order: the placement while it runs, the one skill a test asked for
 * (`mq skill`), the grown-ups' preview's, else the engine's choice for this place and game
 * (mq_engine.c): where she plays picks the flavour, never the maths. */
/* the round's game was picked on the world screen: only skills it plays (the card's promise) */
static bool chosen_plays(mq_t *q, int s)
{
    return mq_round_plays(q, s);
}

static int pick_skill(mq_t *q, int *diff)
{
    mq_profile_t *p = mq_prof(q);
    q->placing_item = false;
    q->diag_item = 0;
    q->pick_why = PW_TEST;
    if (q->fixed_sk1) {
        *diff = p->sk[q->fixed_sk1 - 1].diff;
        return q->fixed_sk1 - 1;
    }
    if (q->engine_stop) { /* `mq engine stop` (scripted tests of other parts): the stop's own skill */
        int s = q->boss ? mq_levels[q->world][mq_rnd(q, 0, mq_world_levels(q->world) - 1)] - 1
                        : mq_levels[q->world][q->level] - 1;
        *diff = p->sk[s].diff;
        return s;
    }
    int d, s = q->preview ? -1 : mq_place_next(q, &d);
    if (s >= 0 && chosen_plays(q, s)) { /* (a placement order the picked game cannot play waits for another round) */
        q->placing_item = true, q->pick_why = PW_PLACE;
        *diff = d;
        return s;
    }
    if ((s = mq_diag_pick(q)) >= 0 && chosen_plays(q, s)) { /* an order aimed at an error pattern she has (practice, a re-check) */
        *diff = p->sk[s].diff;
        return s;
    }
    s = mq_engine_pick(q, q->world, q->gm.round_mech < ME_COUNT ? q->gm.round_mech : -1);
    *diff = mq_engine_diff(q, s);
    return s;
}

static void start_level(mq_t *q, int world, int level, bool boss)
{
    mq_profile_t *p = mq_prof(q);
    mq_diag_put_away(q); /* (a friend's moment left open: a new round) */
    q->world = (uint8_t)world, q->level = (uint8_t)level, q->boss = boss;
    q->run_n = boss ? MQ_RUN_BOSS : MQ_RUN_N; /* (a visit long enough to settle in: the user's "pushed around") */
    q->pay_base = boss ? 8 : 6, q->half_done = false; /* (the old lengths: the coins of a round) */
    q->run_i = q->run_right = q->run_misses = q->missrun = q->extra = 0;
    q->combo = 0;
    q->stars_won = 0;
    q->reward_sticker = q->reward_hat = 0xFF;
    q->ex_paid = false, q->run_coins = 0, q->new_set = 0xFF, q->new_bonus = 0, q->dip = 0, q->errand_gift = 0;
    q->new_today = 0;
    q->nqueue = 0;
    /* a round of the facts' boxes: every round she plays (the engine mixes fact orders into any round; counting
     * only the fact stop's rounds left the boxes' due rounds far apart and the same few facts due again) */
    int skill = boss ? mq_levels[world][0] - 1 : mq_levels[world][level] - 1; /* the stop's: its game */
    p->round++;
    /* a review that is due goes first */
    int rv = -1;
    for (int i = 0; i < 6; i++)
        if (p->review[i] && p->review_at[i] <= p->problems &&
            (!q->want_mech1 || boss || mq_game_plays(q->want_mech1 - 1, p->review[i] - 1))) { /* (the picked game's) */
            rv = p->review[i] - 1;
            p->review[i] = 0;
            if (!mq_engine_ramp_ok(q, rv)) { /* (a placement probe above her start: not in her first sessions) */
                rv = -1;
                continue;
            }
            break;
        }
    q->review_mode = rv >= 0;
    if (!q->preview)
        mq_game_round(q, skill, boss);
    if (q->gm.round_mech == ME_POP && !boss) /* Bubble Pop: brisker rounds */
        q->run_n = MQ_RUN_POP, q->pay_base = 5;
    int d = p->sk[skill].diff;
    bool placing = mq_placing(q) && !q->preview && !q->fixed_sk1 && !q->engine_stop;
    if (placing)
        q->run_n = MQ_RUN_PLACE, q->pay_base = MQ_RUN_PLACE; /* her first game: the placement's eight orders */
    q->pay_carry = (uint8_t)(q->run_n / 2); /* (the coins rounded, not cut: the remainder starts at a half) */
    if (rv >= 0 && !placing)
        skill = rv, q->placing_item = false, q->pick_why = PW_REVIEW;
    else if (!q->preview)
        skill = pick_skill(q, &d);
    new_problem(q, skill, d);
    to_play(q);
}

static void new_problem(mq_t *q, int skill, int diff)
{
    mq_generate_fresh(q, &q->prob, skill, diff); /* never a recent problem again (the similar one after a miss too) */
    mq_strat_order(q); /* now and then: "use this way" (mq_strat.c) */
    if (q->diag_item && skill != mq_diag_rec(q, q->diag_item - 1)->skill) /* (a review took its place) */
        q->diag_item = 0;
    for (int t = 0; t < 30 && !mq_diag_fits(q, &q->prob); t++) /* aimed at a pattern: an order that can show it */
        mq_generate_fresh(q, &q->prob, skill, diff);
    /* the picked game: an order it can play (a skill it could not make one of is set aside for the round) */
    for (int t = 0; q->gm.chosen && q->gm.round_mech < ME_COUNT && !q->preview && t < 40 &&
                    mq_route_mech(q, &q->prob, mq_mech_for(&q->prob, q->gm.round_mech, 0)) != q->gm.round_mech;
         t++) {
        if (t % 8 == 7) {
            q->gm.ban[(skill & 127) / 8] |= (uint8_t)(1 << (skill % 8));
            skill = mq_engine_pick(q, q->world, q->gm.round_mech), diff = mq_engine_diff(q, skill);
        }
        mq_generate_fresh(q, &q->prob, skill, diff);
    }
    if (!mq_diag_fits(q, &q->prob))
        q->diag_item = 0;

    /* (guessing no longer turns the choices into a typed answer: typed answers are the calm games' only) */
    q->retrying = false;
    if (q->prob.kind == K_SHADE)
        q->prob.v[4] = 0, q->prob.v[5] = 0;
    if (q->prob.kind == K_LINE)
        q->prob.v[5] = -1;
    q->in_len = 0, q->input[0] = 0;
    q->sel = q->prob.kind == K_LINE ? -1 : 0;
    q->tried_wrong = false;
    q->used_help = false;
    q->prob_start = q->now;
    q->rev_item = false;
    q->scene_t = q->anim_t; /* the model animates in again */
    q->sub = 0;
    if (!q->preview)
        mq_game_order(q); /* the order arrives in the round's game */
}

/* A round is longer than it was (MQ_RUN_N), its coins are not: an answer's coins scaled by the old length over the
 * new one, the remainder carried to the next answer (no coin lost, none made up) */
static int run_pay(mq_t *q, int coins)
{
    if (coins <= 0 || !q->pay_base || q->pay_base >= q->run_n)
        return coins;
    int t = coins * q->pay_base + q->pay_carry;
    q->pay_carry = (uint8_t)(t % q->run_n);
    return t / q->run_n;
}

/* the round's halfway point (a long round: something in the story marks it -- the cheer says "Halfway!"), once; the
 * facts' boxes count rounds, so a long round ticks their clock here too (their spacing in orders stays as it was) */
bool mq_run_halfway(mq_t *q)
{
    if (q->half_done || q->run_n < MQ_RUN_N || (q->run_i + 1) * 2 < q->run_n || q->preview || q->extra)
        return false;
    q->half_done = true;
    mq_prof(q)->round++;
    return true;
}

static void next_in_run(mq_t *q)
{
    int d, skill = pick_skill(q, &d);
    /* now and then an earlier skill instead (interleaved review; an ordinary item for her) */
    int rv = q->extra || q->placing_item ? -1 : mq_review_next(q, skill);
    if (rv >= 0 && !chosen_plays(q, rv))
        rv = -1;
    if (rv >= 0)
        skill = rv, d = mq_prof(q)->sk[rv].diff, q->pick_why = PW_REVIEW;
    if (q->extra) /* the extra problem that ends the session on a win: a little easier */
        d = d > 0 ? d - 1 : 0;
    new_problem(q, skill, d);
    q->rev_item = rv >= 0;
    q->redraw = true;
}

static int next_sticker(const mq_profile_t *p, int start)
{
    for (int i = 0; i < MQ_NSTICKER; i++) {
        int k = (start + i) % MQ_NSTICKER;
        if (!((p->stickers >> k) & 1))
            return k;
    }
    return -1;
}

static void finish_level(mq_t *q)
{
    mq_profile_t *p = mq_prof(q);
    mq_kart_finish(q); /* a race: its podium */
    /* three stars: 90 % first time (all of a short round, one slip in ten or twelve); two: two thirds */
    int stars = q->run_right * 10 >= q->run_n * 9 ? 3 : q->run_right * 3 >= q->run_n * 2 ? 2 : 1;
    q->stars_won = (uint8_t)stars;
    bool first_win = false, first_finish = false, first3 = false;
    if (q->boss) {
        first_win = !mq_boss_won(p, q->world);
        if (!mq_boss_won(p, q->world)) {
            mq_boss_set(p, q->world);
            int h = q->world % MQ_NHAT;
            if (!((p->hats >> h) & 1))
                p->hats |= (uint16_t)(1 << h), q->reward_hat = (uint8_t)h;
            if (q->world + 1 < MQ_NWORLD)
                p->met |= (uint16_t)(1 << guide(q->world + 1));
        }
        p->stars = mq_sat16(p->stars + 10u);
    } else {
        int skill = mq_levels[q->world][q->level] - 1;
        mq_skrec_t *s = &p->sk[skill];
        bool first = s->plays == 0;
        first_finish = first;
        first3 = stars == 3 && s->stars < 3;
        if (s->plays < 255)
            s->plays++;
        if (stars > s->stars)
            s->stars = (uint8_t)stars;
        if (first || stars == 3) {
            int k = next_sticker(p, q->world * 3 + q->level);
            if (k >= 0)
                p->stickers |= 1u << k, q->reward_sticker = (uint8_t)k;
        }
    }
    mq_on_level_done(q, q->boss, first_win, first_finish, first3);
    mq_hstory_level_done(q); /* a friend's errand done: her thank-you present */
    if (q->gm.round_mech < ME_COUNT && !q->preview) { /* the game's personal best */
        uint16_t *best = mq_best_of(q, q->gm.round_mech);
        if (q->gm.stars > *best) {
            if (*best)
                q->gm.new_best_t = mq_mark(q), mq_sfx(q, MODX_SFX_UNLOCK, 2);
            *best = q->gm.stars;
        }
    }
    if (q->boss) {
        mq_quest_event(q, QE_FINALE, 1);
        mq_remember(q, MEM_FINALE, guide(q->world), 0, q->world, 1);
    }
    if (q->gm.podium == 1) {
        mq_remember(q, MEM_RACE, q->prob.ca % MQ_NCAST, 0, q->world, 1);
        mq_count(q, CT_RACES, 1);
    }
    if (mq_extra(q)->energy < 60) /* a round played: sparkle energy for the merge board */
        mq_extra(q)->energy = (uint8_t)(mq_extra(q)->energy + 3 > 60 ? 60 : mq_extra(q)->energy + 3);
    mq_sfx(q, MODX_SFX_CHEER, 9);
    mq_spark_burst(q, 160, MQ_Y0 + 60, 24);
    q->dirty = true;
    mq_save_now(q);
    go(q, SC_DONE_LEVEL);
    done_open(q);
}

/* update a skill after an answer (mastery, adaptive difficulty, passed flag) */
static void skill_update(mq_t *q, int skill, bool right, bool first_try)
{
    mq_profile_t *p = mq_prof(q);
    mq_skrec_t *s = &p->sk[skill];
    bool was = s->flags & SK_PASSED;
    if (s->tries < 255)
        s->tries++;
    if (right) {
        if (first_try && s->right < 255)
            s->right++;
        /* the first answers of a skill all right first time: it moves on quickly (a placed-above skill too) */
        bool fast = first_try && s->tries <= 4 && s->right == s->tries;
        int m = s->mastery + (first_try ? (fast || mq_skill_assumed(q, skill) ? 25 : 15) : 6);
        s->mastery = (uint8_t)(m > 100 ? 100 : m);
        s->streak = (uint8_t)(first_try && s->streak < 255 ? s->streak + 1 : 0);
    } else {
        s->mastery = (uint8_t)(s->mastery > 10 ? s->mastery - 10 : 0);
        s->streak = 0;
        mq_coach_unspace(q, skill);
    } /* (the difficulty follows the recent success rate: mq_coach_answer) */
    if (skill == S_FACTS) {
        int tot, fl = mq_facts_fluent(p, &tot);
        if (fl == tot)
            s->flags |= SK_PASSED;
        else
            s->flags &= (uint8_t)~SK_PASSED;
    } else if (s->mastery >= 75 && s->right >= 5 && !(s->flags & SK_PASSED) && mq_coach_spaced(q, skill))
        s->flags |= SK_PASSED; /* mastered: the bar reached again on a later play day */
    if (!was && (s->flags & SK_PASSED)) {
        mq_on_skill_passed(q, skill);
        mq_review_join(q, skill, true);
    }
    q->dirty = true;
}
void mq_skill_answer(mq_t *q, int skill, bool right, bool first_try)
{
    if (skill >= 0 && skill < MQ_NSKILL)
        skill_update(q, skill, right, first_try);
}

static void schedule_review(mq_t *q, int skill)
{
    mq_profile_t *p = mq_prof(q);
    for (int i = 0; i < 6; i++)
        if (p->review[i] == skill + 1)
            return;
    for (int i = 0; i < 6; i++)
        if (!p->review[i]) {
            p->review[i] = (uint8_t)(skill + 1);
            p->review_at[i] = (uint16_t)(p->problems + 5);
            return;
        }
}

/* The player answered value v (already parsed, or a game action): the bookkeeping. Returns 1
 * right, 0 wrong but one more try (allow_retry, the first miss), -1 missed (show why). */
int mq_judge(mq_t *q, int32_t v, bool allow_retry)
{
    mq_profile_t *p = mq_prof(q);
    mq_prob_t *pr = &q->prob;
    bool right = mq_correct(pr, v);
    if (q->preview) { /* the grown-ups' preview: shown as it would be, never booked as hers (B8, CR2-21: no record,
                       * no skill, stars, coins, recent orders or facts) */
        if (right) {
            q->praise = mq_praise(q);
            q->sub = 1, q->pressed_t = q->now, q->retrying = false;
            if (pr->kind == K_LINE)
                pr->v[3] = (int16_t)pr->ans;
            q->redraw = true;
            return 1;
        }
        q->last_given = v;
        q->last_mc = (uint8_t)mq_classify(pr, v);
        bool again = allow_retry && !q->tried_wrong;
        q->tried_wrong = true, q->retrying = again;
        if (again) {
            q->in_len = 0, q->input[0] = 0;
            q->prob_start = q->now;
            q->redraw = true;
            return 0;
        }
        return -1;
    }
    uint32_t ms = q->now - q->prob_start;
    /* her decision: timed to the input that answered (a caught star's or a flung one's flight is not hers) */
    uint32_t dec = q->last_input >= q->prob_start && q->last_input <= q->now ? q->last_input - q->prob_start : ms;
    if (pr->skill == S_FACTS)
        mq_fact_record(q, pr->a[0], right, dec);
    p->problems++;
    if (!q->tried_wrong) { /* the grown-ups' recent orders: the order, her first answer, why it came */
        mq_recent_t *r = &q->recent_o[q->recent_i % MQ_NRECENT];
        int k = 0;
        for (int l = 0; l < 3 && k < (int)sizeof r->q - 2; l++)
            for (const char *c = pr->text[l]; *c && k < (int)sizeof r->q - 1; c++)
                r->q[k++] = *c == '\n' ? ' ' : *c;
        r->q[k] = 0;
        if (q->judge_slip) /* (a slip in one column: no whole answer of hers to show) */
            q->api->snprintf(r->her, sizeof r->her, "a slip");
        else
            mq_format(q, pr, v, r->her, sizeof r->her);
        r->skill = (uint8_t)pr->skill, r->rung = (uint8_t)pr->diff, r->right = right;
        r->why = q->rev_item ? PW_REVIEW : q->pick_why;
        q->recent_i = (uint8_t)((q->recent_i + 1) % MQ_NRECENT);
        if (q->recent_n < MQ_NRECENT)
            q->recent_n++;
    }
    mq_seen_record(q, pr, right, ms > 15000u); /* the problem's own history */
    /* error patterns over time (never from a made-up answer: a notebook's slip is one digit, CR-04) */
    q->diag_hit = q->judge_slip ? 0 : (int8_t)(mq_diag_judge(q, pr, v, right, !q->tried_wrong) + 1);
    if (q->rev_item && !q->tried_wrong) {      /* an earlier skill brought back */
        mq_review_answer(q, pr->skill, right);
        if (!right && mq_learn(q)->rev[pr->skill].slips >= 2)
            q->missrun = 2; /* slipped twice: the guide offers the trick */
    }
    if (!q->tried_wrong) { /* a first answer: the success-rate controller, guessing */
        mq_coach_answer(q, pr->skill, right, ms);
        mq_coach_guess(q, right, dec); /* (mashing: decided at once, whatever the flight takes -- Sky Fling paid) */
        if (q->placing_item)
            mq_place_answer(q, pr->skill, right);
        if (right)
            mq_think_record(q, pr, ms);
        if (!mq_learn(q)->rev[pr->skill].box)
            mq_review_join(q, pr->skill, false); /* played: it will come back now and then */
        int think = mq_think_ms(q, pr);
        bool quick = right && ms * 10 < (uint32_t)think * 6;
        if (q->scene == SC_PLAY && q->gm.mech < ME_COUNT) /* the game's speed follows her play */
            mq_speed_event(q, q->gm.mech, !right ? SE_MISS : ms > (uint32_t)think ? SE_SLOW : quick ? SE_QUICK : SE_RIGHT);
        /* (being quick moves her up the skill's ladder sooner -- mq_rung_answer -- never the game's speed) */
        mq_engine_answered(q, pr->skill, !right);
        if (q->scene == SC_PLAY) { /* typed or picked: the share of typed orders (backlog (b)) */
            mq_pace_t *pc = mq_pace(q);
            bool typed = mq_order_typed(q);
            pc->typed_n = (uint16_t)(pc->typed_n + (typed && pc->typed_n < 60000)), pc->orders_n += pc->orders_n < 60000;
            pc->typed_ema = (uint8_t)(typed ? pc->typed_ema + (255 - pc->typed_ema) / 12 : pc->typed_ema - pc->typed_ema / 12);
        }
    }
    if (right) {
        if (q->api->size >= offsetof(modx_api_t, save_soon) + sizeof q->api->save_soon && q->api->save_soon)
            q->api->save_soon(); /* (an order won: the session written at the next quiet moment, debounced) */
        bool first = !q->tried_wrong && !q->used_help;
        /* coins by the skill's state before this answer (mastering it pays on its own) */
        /* bouncing back pays: the similar order after a miss, or her own order solved with a helper friend */
        bool bounce = (q->used_help && !q->tried_wrong) || (q->scene == SC_PLAY && q->gm.helped);
        int coins = mq_answer_coins(p, pr->skill, first, bounce, q->combo + 1, p->problems);
        q->mashed = q->guess_n > 0;
        if (q->guess_n) /* guessing earns nothing (quietly): no coins, no stars */
            coins = 0, q->guess_n--;
        coins = run_pay(q, coins);
        q->retrying = false;
        skill_update(q, pr->skill, true, first);
        if (q->scene == SC_PLAY)
            mq_friends_right(q, pr, first); /* a memory, hearts, sparkle energy */
        mq_earn(q, coins, NULL);
        mq_fun_maths_moment(q);
        mq_hstory_order(q, 1); /* (home time back: mq_homestory.c) */
        if (ms > 25000u) /* a long think: a little fun may help */
            q->dip = 1;
        if ((q->combo + 1) % 10 == 0 && mq_home(q)->dough < 99) /* a long streak: squishy dough */
            mq_home(q)->dough++;
        q->praise = mq_praise(q);
        q->run_right = (uint8_t)(q->run_right + first);
        q->combo++;
        if (q->combo > q->best_combo)
            q->best_combo = q->combo;
        if (!q->mashed)
            p->stars = mq_sat16(p->stars + 1u + (q->combo % 5 == 0 ? 3u : 0u));
        q->missrun = 0;
        mq_spark_burst(q, 250, MQ_Y0 + 100, q->combo >= 3 ? 20 : 12);
        q->sub = 1; /* celebrating; the next problem follows */
        q->sel = (int8_t)mq_rnd(q, 0, 5);
        q->pressed_t = q->now;
        if (pr->kind == K_LINE)
            pr->v[3] = (int16_t)pr->ans;
        q->redraw = true;
        return 1;
    }
    q->last_given = v;
    q->last_mc = (uint8_t)mq_classify(pr, v);
    bool retry = allow_retry && !q->tried_wrong && !q->guess_n;
    if (!q->tried_wrong) { /* the first miss of this item counts once */
        q->combo = 0;
        q->missrun++;
        mq_fun_maths_moment(q);
        mq_hstory_order(q, 1);
        if (q->missrun >= 2)
            q->dip = 1;
        q->run_misses++;
        skill_update(q, pr->skill, false, false);
        schedule_review(q, pr->skill);
    }
    q->tried_wrong = true;
    if (retry) { /* try again once, with a little hint, before the answer is shown */
        q->retrying = true;
        q->in_len = 0, q->input[0] = 0;
        q->prob_start = q->now;
        q->redraw = true;
        return 0;
    }
    q->retrying = false;
    if (q->guess_n)
        q->guess_n--;
    return -1;
}

/* A slip, not an answer (B8: CR-04, CR2-09): one wrong move in a friend's model or one column of the notebook counts
 * the order as missed first time for the coach, with no made-up whole answer of hers for the error patterns, the
 * recent orders or the why. */
int mq_judge_slip(mq_t *q)
{
    mq_prob_t *pr = &q->prob;
    int32_t v = pr->kind == K_CHOICE ? (pr->ans + 1) % (pr->nchoice > 1 ? pr->nchoice : 2) : pr->ans + 1;
    for (int k = 2; mq_correct(pr, v) && k < 10; k++) /* (never right by accident) */
        v = pr->ans + k;
    q->judge_slip = true;
    int r = mq_judge(q, v, true);
    q->judge_slip = false;
    q->last_mc = MC_CARELESS;
    return r;
}

/* the preview's question screen (grown-ups): typed answers get one more try, choices do not */
static void submit_value(mq_t *q, int32_t v)
{
    mq_prob_t *pr = &q->prob;
    bool typed = pr->kind != K_CHOICE && pr->kind != K_SHADE && pr->kind != K_LINE;
    if (mq_judge(q, v, typed) < 0)
        go(q, SC_FEEDBACK);
}

static void submit_input(mq_t *q)
{
    mq_prob_t *pr = &q->prob;
    int32_t v;
    if (pr->kind == K_CHOICE) {
        submit_value(q, q->sel);
        return;
    }
    if (pr->kind == K_SHADE) {
        int n = 0;
        for (int i = 0; i < 12; i++)
            n += (pr->v[5] >> i) & 1;
        submit_value(q, n);
        return;
    }
    if (pr->kind == K_LINE) {
        if (q->sel < 0) {
            q->api->snprintf(q->msg[0], MQ_TXT, "Tap a mark on the line first.");
            q->redraw = true;
            return;
        }
        submit_value(q, q->sel);
        return;
    }
    if (mq_parse(pr, q->input, &v)) {
        q->api->snprintf(q->msg[0], MQ_TXT, q->in_len ? "Hmm, check how it is typed." : "Type your answer first.");
        q->redraw = true;
        return;
    }
    q->msg[0][0] = 0;
    submit_value(q, v);
}

/* after the celebration of a right answer */
static void after_right(mq_t *q)
{
    q->run_i++;
    if (q->preview) { /* (the grown-ups' preview: the next order, never a round won -- CR2-21) */
        next_in_run(q);
        return;
    }
    if (q->run_i >= q->run_n + q->extra)
        finish_level(q);
    else if ((q->combo % 5 == 0 && mq_fun_maybe(q, FUN_STREAK, FR_RUN)) || (q->dip && mq_fun_maybe(q, FUN_DIP, FR_RUN)))
        q->dip = 0; /* a fun beat first; mq_fun_resume carries on with the run */
    else
        next_in_run(q);
}

/* after a fun beat: on with the story */
void mq_fun_resume(mq_t *q, int ret)
{
    switch (ret) {
    case FR_RUN:
        next_in_run(q);
        to_play(q);
        break;
    case FR_WORLD: go(q, SC_WORLD); break;
    case FR_ROOM: mq_home_enter(q, SC_ROOM); break;
    default: go(q, SC_MAP); break;
    }
}

/* after the feedback of a wrong answer: a similar problem (it counts as the next one) */
static void after_feedback(mq_t *q)
{
    mq_profile_t *p = mq_prof(q);
    int skill = q->prob.skill;
    q->run_i++;
    if (q->run_i >= q->run_n + q->extra && q->extra < 2)
        q->extra++; /* end on a win: one more, a little easier */
    if (q->placing_item && mq_placing(q)) { /* the placement found an edge: on to the next strand */
        next_in_run(q);
        to_play(q);
        return;
    }
    int d = p->sk[skill].diff;
    new_problem(q, skill, d > 0 ? d - 1 : 0);
    q->used_help = true;
    q->placing_item = false; /* (a similar order after a miss is no placement order) */
    to_play(q);
}

void mq_run_after_right(mq_t *q) { after_right(q); }

/* after the why of a second miss: the trick is offered after two misses in a row, else a
 * similar order (a little easier) */
void mq_run_after_miss(mq_t *q)
{
    if (q->sub == GS_WHY && q->missrun == 2 && mq_coach_fade(q, q->prob.skill) < 2) {
        q->sub = GS_OFFER;
        q->gm.hud_ok = 0, q->redraw = true;
        return;
    }
    after_feedback(q);
}

static void start_example(mq_t *q, int skill, bool then_similar, int back)
{
    mq_profile_t *p = mq_prof(q);
    mq_generate_fresh(q, &q->ex, skill, p->sk[skill].diff);
    char out[3][MQ_TXT];
    mq_prob_t vis;
    q->ex_n = (uint8_t)mq_steps(q, &q->ex, 0, out, &vis);
    q->ex_step = 0;
    if (!q->preview) { /* the trick plays in the round, as a little show */
        if (back == SC_INTRO) {
            mq_prob_t ex = q->ex;
            start_level(q, q->world, q->level, false);
            q->ex = ex, then_similar = false;
        }
        q->ex_fade = (uint8_t)(mq_coach_fade(q, skill) >= 1 && back != SC_INTRO);
        q->ex_then_similar = then_similar;
        q->ex_back = SC_PLAY;
        p->sk[skill].flags |= SK_SHOWN;
        q->sub = GS_TRICK;
        q->gm.trick_t = q->anim_t, q->gm.hud_ok = 0;
        q->redraw = true;
        return;
    }
    q->ex_fade = (uint8_t)(mq_coach_fade(q, skill) >= 1 && back != SC_INTRO); /* she finishes it herself */
    q->ex_then_similar = then_similar;
    q->ex_back = (uint8_t)back;
    p->sk[skill].flags |= SK_SHOWN;
    go(q, SC_EXAMPLE);
}

/* the trick's completion order (its last step hers): the trick's own numbers are the run's next order -- never a
 * fresh order generated and thrown away (CR-10: the no-repeat history, the ways' bookkeeping and the placement's next
 * probe all moved on for an order that was never asked) */
static void completion_order(mq_t *q)
{
    mq_on_example_done(q, q->ex.skill);
    q->run_i++;
    if (q->run_i >= q->run_n + q->extra && q->extra < 2)
        q->extra++;
    q->placing_item = false; /* (a trick's order is no placement order) */
    q->prob = q->ex;
}

/* the trick's next step in the round; at its end on with the round */
void mq_trick_next(mq_t *q)
{
    if (q->scene != SC_PLAY || q->sub != GS_TRICK)
        return;
    q->gm.trick_t = q->anim_t;
    q->redraw = true;
    if (q->ex_fade && q->ex_then_similar && q->ex_step + 2 >= q->ex_n) { /* a completion order: the last step is hers
                                                                       * (after misses only: "show me" on her own order
                                                                       * goes back to that order -- it never flips to
                                                                       * another one, the user's report) */
        completion_order(q);
        q->tried_wrong = false, q->used_help = true, q->retrying = false;
        q->prob_start = q->now;
        mq_game_order(q);
        return;
    }
    if (q->ex_step + 1 < q->ex_n) {
        q->ex_step++;
        return;
    }
    mq_on_example_done(q, q->ex.skill);
    if (q->ex_then_similar)
        after_feedback(q);
    else /* back to her own order, the same numbers (helped: no evidence for her rung or mastery) */
        q->sub = GS_PLAY, q->gm.hud_ok = 0, q->used_help = true;
}

/* ------------------------------------------------------------------ drawing helpers */
/* the title line: title, her purse (coins are the one thing she counts; stars are only sparkle) */
static void header(mq_t *q, const char *title, uint16_t bg)
{
    const modx_api_t *api = q->api;
    mq_fill(q, 0, MQ_Y0, MQ_W, MQ_H, bg);
    mq_fill(q, 0, MQ_Y0, MQ_W, 20, RGB(255, 255, 255));
    mq_text(q, api->font_medium, 6, MQ_Y0 + 3, title, Q_INK);
    int pw = mq_purse_badge(q, MQ_W - 6, MQ_Y0 + 3);
    /* the toast lane: between the title and the purse (and its "+3"), in the header's own row */
    mq_lane(q, 6 + api->text_width(api->font_medium, title) + 8, MQ_Y0 + 2, MQ_W - 6 - pw - 40);
}
void mq_header(mq_t *q, const char *title, uint16_t bg) { header(q, title, bg); }

static void button(mq_t *q, int x, int y, int w, int h, int id, int arg, const char *label, uint16_t col);
void mq_button(mq_t *q, int x, int y, int w, int h, int id, int arg, const char *label, uint16_t col)
{
    button(q, x, y, w, h, id, arg, label, col);
}

/* a rounded button's look (dy: pressed; fit: a label too wide for the medium font in the small one) */
static void button_look(mq_t *q, int x, int y, int w, int h, int dy, const char *label, uint16_t col, bool fit)
{
    int32_t bv[2] = {col, dy}; /* (a high-resolution platform may draw it from its own art: MODX_HR_SPRITE) */
    int hr = mq_hr_sprite(q, "ui/button", x, y, w, h + 2, bv, 2);
    mq_rrect(q, x, y + 2, w, h, 8, Q_DIM);
    mq_rrect(q, x, y + dy, w, h - 1, 8, col);
    if (hr)
        mq_hr_end(q);
    if (label && label[0]) {
        const font_t *f = h >= 30 && (!fit || q->api->text_width(q->api->font_medium, label) <= w - 4) ? q->api->font_medium
                                                                                             : q->api->font_small;
        mq_text_c(q, f, x + w / 2, y + dy + (h - (f == q->api->font_medium ? 14 : 13)) / 2, label, Q_INK);
    }
}
/* a rounded button with a centred label; registered for taps and focus */
static void button(mq_t *q, int x, int y, int w, int h, int id, int arg, const char *label, uint16_t col)
{
    int k = mq_add_btn(q, x, y, w, h, id, arg);
    int dy = (k >= 0 && k == q->pressed && q->now - q->pressed_t < 150) ? 2 : 0;
    button_look(q, x, y, w, h, dy, label, col, false);
}

static void focus_ring(mq_t *q)
{
    if (!q->kbd || q->focus >= q->nbtn)
        return;
    const mq_btn_t *b = &q->btn[q->focus];
    mq_rect(q, b->x - 2, b->y - 2, b->w + 4, b->h + 4, Q_PURPLE);
    mq_rect(q, b->x - 3, b->y - 3, b->w + 6, b->h + 6, Q_PURPLE);
}

static void bubble(mq_t *q, int x, int y, int w, int h)
{
    mq_rrect(q, x, y, w, h, 10, Q_WHITE);
    mq_rect(q, x + 6, y, w - 12, 1, Q_LAV);
}

/* buddy with its hat */
static void buddy(mq_t *q, int x, int y, int s) { mq_buddy_draw(q, mq_prof(q)->buddy % MQ_NCAST, x, y, s, 1); }

/* ------------------------------------------------------------------ scenes: draw */
static void draw_title(mq_t *q)
{
    const modx_api_t *api = q->api;
    mq_fill(q, 0, MQ_Y0, MQ_W, MQ_H, Q_CREAM);
    for (int i = 0; i < 6; i++)
        mq_circle(q, 30 + i * 55, MQ_Y0 + 18 + (i % 2) * 8, 14, Q_WHITE);
    mq_text_c(q, api->font_xlarge, 160, MQ_Y0 + 14, "Sparkle Town", Q_HOTPINK);
    for (int k = 0; k < 2; k++) {
        mq_profile_t *p = &q->save.prof[k];
        int x = 20 + k * 150, y = MQ_Y0 + 48;
        button(q, x, y, 130, 96, B_PROFILE, k, NULL, k ? Q_SKY : Q_PINK);
        if (p->used) {
            mq_sprite(q, p->buddy % MQ_NCAST, x + 45, y + 6, 2, 1);
            mq_text_fit(q, api->font_medium, x + 65, y + 54, 122, mq_pname(q, k), Q_INK, true);
            { /* her purse */
                const mq_home_t *hm = &q->save.home[k];
                int cents = (int)(hm->purse_ver ? hm->purse : hm->coins), w = mq_cents_width(q, api->font_small, cents);
                mq_purse_icon(q, x + 65 - w / 2 - 8, y + 80, 6);
                mq_cents_text(q, api->font_small, x + 65 - w / 2 + 2, y + 74, cents, Q_INK, false);
            }
        } else {
            mq_text_c(q, api->font_xlarge, x + 65, y + 18, "+", Q_INK);
            mq_text_c(q, api->font_medium, x + 65, y + 58, "new player", Q_INK);
        }
    }
    mq_text_c(q, api->font_small, 160, MQ_Y0 + 146, "Tap a player to start!", Q_DIM);
    /* the whole cast parades along the grass below, dancing, with a balloon now and then: the
     * friends she is about to meet */
    mq_fill(q, 0, MQ_Y0 + 194, MQ_W, MQ_H - 194, RGB(170, 226, 186));
    for (int k = 0; k < 12; k++)
        mq_circle(q, k * 28 + 10, MQ_Y0 + 198 + (k % 2) * 3, 2, k % 3 == 0 ? Q_PINK : k % 3 == 1 ? Q_YELLOW : Q_WHITE);
    uint32_t t = q->anim_t;
    for (int k = 0; k < MQ_NCAST; k++) {
        int x = (int)((t / 28 + (uint32_t)(MQ_NCAST - k) * 52) % 468) - 40, hop = (int)((t / 140 + (uint32_t)k) % 4 == 1 ? 3 : 0);
        if (x < -36 || x > MQ_W + 4)
            continue;
        mq_cast(q, k, x, MQ_Y0 + 164 - hop, 30, (t / 1200 + (uint32_t)k) % 3 == 0 ? AN_DANCE : AN_HAPPY);
        if (k % 3 == 1) { /* a balloon on a string */
            mq_line(q, x + 27, MQ_Y0 + 184 - hop, x + 33, MQ_Y0 + 177, Q_DIM);
            mq_oval(q, x + 34, MQ_Y0 + 171, 5, 6, k % 2 ? Q_SKY : Q_HOTPINK);
        }
    }
}

static void draw_name(mq_t *q)
{
    const modx_api_t *api = q->api;
    mq_profile_t *p = mq_prof(q);
    header(q, "Who's playing?", Q_CREAM);
    for (int i = 0; i < MQ_NCAST; i++) {
        int x = 6 + i * 34;
        button(q, x, MQ_Y0 + 24, 32, 32, B_BUDDY, i, NULL, i == p->buddy ? Q_YELLOW : Q_WHITE);
        mq_sprite(q, i, x + 6, MQ_Y0 + 30, 1, i == p->buddy);
    }
    bubble(q, 60, MQ_Y0 + 62, 200, 26);
    api->snprintf(q->tmp, sizeof q->tmp, "%s_", mq_name(q));
    mq_text_fit(q, api->font_large, 160, MQ_Y0 + 64, 192, q->tmp, Q_INK, true);
    for (int i = 0; i < 26; i++) {
        char s[2] = {(char)('A' + i), 0};
        int r = i / 9, c = i % 9;
        button(q, 10 + c * 34, MQ_Y0 + 94 + r * 28, 32, 26, B_LETTER, 'A' + i, s, Q_LAV);
    }
    button(q, 10 + 8 * 34, MQ_Y0 + 94 + 2 * 28, 32, 26, B_DEL, 0, "<", Q_PEACH);
    button(q, 110, MQ_Y0 + 180, 100, 24, B_OK, 0, "ok!", Q_MINT);
}

static void draw_map(mq_t *q)
{
    const modx_api_t *api = q->api;
    mq_profile_t *p = mq_prof(q);
    mq_town_scene(q); /* the town: a street she walks along (mq_town.c) */
    mq_fill(q, 0, MQ_Y0, MQ_W, 18, RGB(255, 255, 255));
    { /* this week's event (by play days) */
        static const uint16_t ec[4] = {Q_PINK, Q_SKY, Q_ORANGE, Q_WHITE};
        int e = mq_event(p), sp = mq_event_today(q);
        static const uint16_t spc[EV_COUNT] = {0,      Q_YELLOW, RGB(255, 196, 140), Q_SKY,    Q_MINT, RGB(230, 240, 255),
                                               Q_LAV, Q_PEACH, RGB(200, 236, 255), Q_PINK, RGB(214, 206, 255)};
        mq_rrect(q, 112, MQ_Y0 + 1, 92, 16, 8, sp ? spc[sp] : ec[e]); /* in the title line, between name and coins */
        mq_star(q, 121, MQ_Y0 + 9, 5, sp && (q->anim_t / 300) % 2 ? Q_WHITE : Q_YELLOW);
        mq_text_fit(q, api->font_small, 128, MQ_Y0 + 2, 74, mq_event_label(q), Q_INK, false);
    }
    if (mq_name(q)[0])
        api->snprintf(q->tmp, sizeof q->tmp, "%s's town", mq_name(q)); /* (to the event badge at 112) */
    else /* (no name yet: never "'s town") */
        api->snprintf(q->tmp, sizeof q->tmp, "Sparkle Town");
    if (mq_name(q)[0] && api->text_width(api->font_medium, q->tmp) > 104) /* a long name: "Annabelle-Rose's" */
        api->snprintf(q->tmp, sizeof q->tmp, "%s's", mq_name(q));
    mq_text_fit(q, api->font_medium, 6, MQ_Y0 + 2, 104, q->tmp, Q_INK, false);
    mq_purse_badge(q, MQ_W - 6, MQ_Y0 + 2); /* her purse, as on every header (the bot's 17) */
    mq_meta_bar(q, 60, MQ_Y0 + MQ_H - 17, 200); /* the next unlock */
}

/* a game's picture on the world screen: each game its own (never a row of the same marker) */
static void game_pic(mq_t *q, int m, int cx, int cy)
{
    uint32_t t = q->anim_t;
    int b = (int)(t / 300 % 2);
    switch (m) {
    case ME_POP: /* bubbles */
        mq_circle(q, cx - 8, cy + 4 - b, 10, Q_SKY), mq_circle(q, cx + 9, cy - 6 + b, 8, Q_PINK);
        mq_circle(q, cx - 10, cy, 3, Q_WHITE), mq_circle(q, cx + 7, cy - 9, 2, Q_WHITE);
        break;
    case ME_CATCH: /* a star falling to a cloud */
        mq_star(q, cx, cy - 10 + b * 2, 7, Q_YELLOW);
        mq_circle(q, cx - 10, cy + 10, 6, Q_WHITE), mq_circle(q, cx, cy + 8, 8, Q_WHITE), mq_circle(q, cx + 10, cy + 10, 6, Q_WHITE);
        break;
    case ME_GATES: /* an arch */
        mq_fill(q, cx - 14, cy - 8, 4, 22, Q_BROWN), mq_fill(q, cx + 10, cy - 8, 4, 22, Q_BROWN);
        mq_rrect(q, cx - 18, cy - 14, 36, 9, 4, Q_PEACH);
        break;
    case ME_KART: /* a kart */
        mq_rrect(q, cx - 16, cy - 4, 32, 12, 5, Q_HOTPINK);
        mq_circle(q, cx - 9, cy + 10, 4, Q_INK), mq_circle(q, cx + 9, cy + 10, 4, Q_INK);
        mq_fill(q, cx - 2 + b * 2, cy - 16, 2, 12, Q_DIM), mq_fill(q, cx + b * 2, cy - 16, 8, 6, Q_YELLOW); /* a flag */
        break;
    case ME_ZAP: /* a friend peeking from a hole */
        mq_circle(q, cx, cy - 2 - b * 2, 8, Q_PEACH), mq_circle(q, cx - 3, cy - 4 - b * 2, 1, Q_INK), mq_circle(q, cx + 3, cy - 4 - b * 2, 1, Q_INK);
        mq_oval(q, cx, cy + 8, 16, 5, Q_BROWN);
        break;
    case ME_SORT: /* two bins */
        mq_rrect(q, cx - 18, cy, 16, 14, 3, Q_LAV), mq_rrect(q, cx + 2, cy, 16, 14, 3, Q_MINT);
        mq_rrect(q, cx - 7, cy - 16 + b * 2, 14, 10, 2, Q_WHITE);
        break;
    case ME_JUMP: /* hops along a line */
        mq_fill(q, cx - 18, cy + 8, 36, 2, Q_INK);
        for (int k = 0; k < 4; k++)
            mq_fill(q, cx - 18 + k * 12, cy + 5, 1, 8, Q_INK);
        mq_circle(q, cx - 6 + b * 12, cy - 2, 5, Q_GREEN);
        break;
    case ME_LAUNCH: /* a star flying off a ramp */
        { int16_t px[3] = {(int16_t)(cx - 18), (int16_t)(cx + 2), (int16_t)(cx - 18)}, py[3] = {(int16_t)(cy + 12), (int16_t)(cy + 12), (int16_t)(cy - 2)};
          mq_poly(q, px, py, 3, Q_BROWN); }
        mq_star(q, cx + 8 + b * 2, cy - 8 - b * 2, 6, Q_YELLOW);
        break;
    case ME_BAKE: /* a pie */
        mq_oval(q, cx, cy + 4, 18, 8, Q_BROWN), mq_oval(q, cx, cy + 2, 15, 6, Q_ORANGE);
        mq_line(q, cx, cy + 2, cx + 12, cy - 1, Q_BROWN), mq_line(q, cx, cy + 2, cx - 10, cy + 6, Q_BROWN);
        break;
    case ME_BUILD: /* blocks */
        mq_fill(q, cx - 16, cy + 2, 14, 12, Q_SKY), mq_fill(q, cx, cy + 2, 14, 12, Q_PINK), mq_fill(q, cx - 8, cy - 10, 14, 12, Q_YELLOW);
        break;
    case ME_CLOCK: /* a clock */
        mq_circle(q, cx, cy, 14, Q_WHITE), mq_ring(q, cx, cy, 14, Q_INK);
        mq_line(q, cx, cy, cx, cy - 9, Q_INK), mq_line(q, cx, cy, cx + 6 - b, cy + 2, Q_INK);
        break;
    case ME_TILL: /* a till with a coin */
        mq_rrect(q, cx - 16, cy - 4, 32, 18, 4, Q_PEACH), mq_fill(q, cx - 10, cy - 12, 20, 8, Q_WHITE);
        mq_circle(q, cx + 12, cy - 12 + b * 2, 5, Q_YELLOW);
        break;
    case ME_DIG: /* a treasure chest */
        mq_rrect(q, cx - 16, cy - 4, 32, 18, 3, Q_BROWN), mq_fill(q, cx - 16, cy + 1, 32, 2, Q_YELLOW);
        mq_star(q, cx, cy - 10 - b, 5, Q_YELLOW);
        break;
    case ME_VAULT: /* a safe's dial */
        mq_rrect(q, cx - 16, cy - 14, 32, 28, 4, Q_GREY), mq_circle(q, cx, cy, 8, Q_DIM), mq_circle(q, cx, cy, 3, Q_WHITE);
        break;
    case ME_COOK: /* a cake */
        mq_rrect(q, cx - 15, cy - 2, 30, 14, 3, Q_PINK), mq_fill(q, cx - 15, cy + 2, 30, 2, Q_WHITE);
        mq_fill(q, cx - 1, cy - 10, 2, 8, Q_SKY), mq_circle(q, cx, cy - 12 - b, 2, Q_ORANGE);
        break;
    case ME_PETS: /* a puppy's paw */
        mq_circle(q, cx, cy + 4, 8, Q_BROWN);
        for (int k = 0; k < 3; k++)
            mq_circle(q, cx - 8 + k * 8, cy - 8 - (k == 1 ? 3 : 0), 3, Q_BROWN);
        mq_heart(q, cx + 14, cy - 12 - b, 4, Q_HOTPINK);
        break;
    default: break;
    }
}

/* the world's choices: its games, then the finale (B_WGAME arg: the game's index; 3: the finale) */
static int world_choices(int w, int mech[3], int stop[3]) { return mq_world_games(w, mech, stop) + 1; }

/* Entering a world: its few games to pick from, each with its own picture and name (the engine picks
 * every order); the place itself grows fancier with her stars there -- bunting, flowers, friends
 * visiting -- instead of a row of stops. */
static void draw_world(mq_t *q)
{
    const modx_api_t *api = q->api;
    mq_profile_t *p = mq_prof(q);
    int w = q->world, n = mq_world_levels(w);
    header(q, mq_world_name[w], world_col(q, w, 0));
    mq_backdrop(q, w, MQ_Y0 + 20, MQ_Y0 + 170); /* the place itself */
    mq_fill(q, 0, MQ_Y0 + 170, MQ_W, 36, world_col(q, w, 2));
    int got = 0;
    for (int l = 0; l < n; l++)
        got += p->sk[mq_levels[w][l] - 1].stars;
    int fancy = n ? got * 8 / (n * 3) : 0; /* 0..8: how dressed up the place is */
    if (mq_boss_won(p, w))
        fancy = fancy < 4 ? 4 : fancy;
    mq_text(q, api->font_small, 110, MQ_Y0 + 4, goal[w], Q_PURPLE); /* the chapter's goal */
    static const uint16_t fc[6] = {Q_PINK, Q_YELLOW, Q_SKY, Q_MINT, Q_LAV, Q_PEACH};
    if (fancy >= 1) /* bunting across the place */
        for (int i = 0; i < 16; i++) {
            int16_t fx[3] = {(int16_t)(i * 20 + 2), (int16_t)(i * 20 + 16), (int16_t)(i * 20 + 9)},
                    fy[3] = {(int16_t)(MQ_Y0 + 22), (int16_t)(MQ_Y0 + 22), (int16_t)(MQ_Y0 + 32)};
            mq_poly(q, fx, fy, 3, fc[i % 6]);
        }
    if (fancy >= 2) /* flowers along the ground, more with more stars */
        for (int i = 0; i < 4 + fancy * 2; i++) {
            int x = 8 + (i * 53) % 300, y = MQ_Y0 + 176 + (i * 7) % 12;
            mq_fill(q, x, y, 1, 5, Q_GREEN), mq_circle(q, x, y - 1, 2, fc[i % 6]);
        }
    for (int k = 0; k < (fancy - 2) / 2 && k < 3; k++) /* friends come to visit */
        mq_cast(q, (guide(w) + 1 + k * 3) % MQ_NCAST, 14 + k * 70, MQ_Y0 + 140, 30,
                (q->anim_t / 900 + (uint32_t)k) % 2 ? AN_DANCE : AN_HAPPY);
    if (fancy >= 8 && (q->anim_t / 250) % 2) /* all stars: sparkles */
        for (int i = 0; i < 5; i++)
            mq_star(q, 30 + i * 64, MQ_Y0 + 42 + (i % 2) * 6, 3, Q_YELLOW);
    /* the choices: 2 to 4 different games, each its own picture */
    int mech[3], stop[3], nc = world_choices(w, mech, stop), cw = 70, gap = 8, x0 = (MQ_W - (nc * cw + (nc - 1) * gap)) / 2;
    mq_text_c(q, api->font_medium, 160, MQ_Y0 + 36, "What shall we play?", Q_INK);
    for (int i = 0; i < nc; i++) {
        int x = x0 + i * (cw + gap), y = MQ_Y0 + 56, fin = i == nc - 1;
        int k = mq_add_btn(q, x, y, cw, 84, B_WGAME, fin ? 3 : i);
        int dy = (k >= 0 && k == q->pressed && q->now - q->pressed_t < 150) ? 2 : 0;
        mq_rrect(q, x, y + 2, cw, 84, 10, Q_DIM);
        mq_rrect(q, x, y + dy, cw, 83, 10, fin ? Q_YELLOW : Q_WHITE);
        if (fin) { /* the finale: a trophy */
            mq_fill(q, x + cw / 2 - 7, y + 30 + dy, 14, 14, Q_ORANGE), mq_fill(q, x + cw / 2 - 11, y + 44 + dy, 22, 4, Q_BROWN);
            mq_star(q, x + cw / 2, y + 22 + dy - (int)(q->anim_t / 300 % 2), 9, Q_HOTPINK);
            if (mq_boss_won(p, w))
                mq_heart(q, x + cw - 12, y + 12 + dy, 4, Q_HOTPINK);
        } else
            game_pic(q, mech[i], x + cw / 2, y + 32 + dy);
        const char *nm = fin ? "Big finale" : mq_mech_name(mech[i]);
        char a[24];
        api->snprintf(a, sizeof a, "%s", nm);
        char *sp = NULL;
        for (char *c = a; *c; c++)
            if (*c == ' ')
                sp = c;
        if (sp && mq_text_w(q, api->font_small, a) > cw - 6) { /* two lines */
            *sp = 0;
            mq_text_c(q, api->font_small, x + cw / 2, y + 56 + dy, a, Q_INK);
            mq_text_c(q, api->font_small, x + cw / 2, y + 68 + dy, sp + 1, Q_INK);
        } else
            mq_text_c(q, api->font_small, x + cw / 2, y + 62 + dy, a, Q_INK);
        if (!fin && *mq_best_of(q, mech[i])) /* played: a trophy, never a number */
            mq_trophy(q, x + cw - 11, y + 11 + dy, 7);
    }
    mq_sprite(q, guide(w), 270, MQ_Y0 + 150, 2, 1);
}

/* a pick on the world screen: straight into a round of that game (the engine picks the orders) */
static void world_pick(mq_t *q, int i)
{
    int mech[3], stop[3], nc = world_choices(q->world, mech, stop);
    q->fixed_sk1 = 0;
    if (i >= 3 || i >= nc - 1) { /* the finale */
        q->want_mech1 = 0;
        q->sub = 1;
        start_level(q, q->world, 0, true);
        return;
    }
    q->want_mech1 = (uint8_t)(mech[i] + 1);
    start_level(q, q->world, stop[i], false);
    q->want_mech1 = 0;
}

/* the home story's errand (mq_homestory.c): out of her home to the friend's place, straight into its game's orders */
void mq_errand_go(mq_t *q, int world)
{
    q->story_card = 0, q->out_t = 0, q->hm_drag = 0, q->shop_pick = 0;
    mq_home(q)->story_fl |= HSF_KNOCKED;
    mq_save_now(q);
    press(q, B_WORLD, world % MQ_NWORLD);
    if (q->scene == SC_WORLD)
        world_pick(q, 0);
    if (q->scene == SC_PLAY || q->scene == SC_QUIZ || q->scene == SC_INTRO) {
        char b[48];
        q->api->snprintf(b, sizeof b, "%s: You came!", mq_cast_name(q, guide(world % MQ_NWORLD)));
        mq_toast(q, b);
    }
}

static void draw_intro(mq_t *q)
{
    const modx_api_t *api = q->api;
    mq_profile_t *p = mq_prof(q);
    bool boss = q->sub == 1;
    int w = q->world, skill = mq_levels[w][q->level] - 1;
    char ch[32];
    api->snprintf(ch, sizeof ch, chapter[w], mq_cast_name(q, guide(w))); /* the guide's name, as the theme has it */
    header(q, boss ? "The big finale!" : ch, world_col(q, w, 0));
    mq_sprite_anim(q, guide(w), 14, MQ_Y0 + 40, 3, AN_IDLE);
    bubble(q, 86, MQ_Y0 + 30, 226, 80);
    if (boss) {
        api->snprintf(q->tmp, sizeof q->tmp, "%s finale!", mq_world_name[w]);
        mq_text(q, api->font_medium, 94, MQ_Y0 + 36, q->tmp, Q_INK);
        int el = (int)(q->anim_t - q->scene_t), z = el < 400 ? el / 40 : 10; /* the title bounces in */
        for (int i = 0; i < 6; i++) /* spotlight stars spin around */
            mq_star(q, 160 + mq_icos(i * 60 + el / 8) * 70 / 1024, MQ_Y0 + 122 + mq_isin(i * 60 + el / 8) * 12 / 1024, 5,
                    i % 2 ? Q_YELLOW : Q_PINK);
        mq_text_c(q, api->font_xlarge, 160, MQ_Y0 + 96 - (10 - z) * 4 + (el / 120 % 2), "BIG FINALE!", Q_HOTPINK);
        mq_text(q, api->font_small, 94, MQ_Y0 + 56, "A bit of everything. Ready?", Q_INK);
        mq_text(q, api->font_small, 94, MQ_Y0 + 70, "Win it for a big party!", Q_INK);
        button(q, 100, MQ_Y0 + 140, 120, 34, B_PLAY, 0, "let's go!", Q_MINT);
        return;
    }
    bool fresh = p->sk[skill].plays == 0;
    if (fresh)
        api->snprintf(q->tmp, sizeof q->tmp, "Hi! I'm %s.", mq_cast_name(q, guide(w)));
    else
        mq_coach_greet(q, q->tmp, sizeof q->tmp);
    if (!fresh && api->text_width(api->font_medium, q->tmp) > MQ_W - 106) /* a long name: the short greeting (in the bubble) */
        api->snprintf(q->tmp, sizeof q->tmp, "Hi, %s!", mq_name(q));
    mq_text_fit(q, api->font_medium, 94, MQ_Y0 + 36, MQ_W - 106, q->tmp, Q_INK, false);
    { /* the game of this stop, with its best */
        int m = mq_round_mech(q, skill);
        mq_rrect(q, 94, MQ_Y0 + 56, 214, 34, 10, world_col(q, w, 2));
        mq_text(q, api->font_large, 100, MQ_Y0 + 58, mq_mech_name(m), Q_INK);
        mq_text(q, api->font_small, 100, MQ_Y0 + 76, mq_mech_goal(m), Q_INK);
        if (*mq_best_of(q, m)) /* played before: a trophy (a best is never a number) */
            mq_trophy(q, 294, MQ_Y0 + 66, 8);

        mq_text(q, api->font_small, 94, MQ_Y0 + 96, fresh ? "Something new! See my trick first?" : "Ready? Let's play!", Q_INK);
    }
    api->snprintf(q->tmp, sizeof q->tmp, "%s's trick", mq_cast_name(q, guide(w)));
    if (mq_coach_fade(q, skill) < 2) /* a fluent skill goes straight to play */
        button(q, 30, MQ_Y0 + 140, 120, 34, B_SHOWME, 0, q->tmp, Q_LAV);
    button(q, 170, MQ_Y0 + 140, 120, 34, B_PLAY, 0, "let's go!", Q_MINT);
    if (fresh && !q->kbd)
        q->focus = 0;
}

static char pad_symbol(const mq_prob_t *p)
{
    switch (p->kind) {
    case K_FRAC: return '/';
    case K_DEC:
    case K_MONEY: return '.';
    case K_REM: return 'R';
    case K_COORD: return ',';
    default: return 0;
    }
}

static void draw_input_box(mq_t *q, int x, int y, int w, int h)
{
    const modx_api_t *api = q->api;
    mq_rrect(q, x, y, w, h, 8, Q_WHITE);
    mq_rect(q, x + 2, y + 2, w - 4, h - 4, Q_LAV);
    char s[24];
    if (q->prob.kind == K_TIME && q->in_len >= 3)
        api->snprintf(s, sizeof s, "%.*s:%s", q->in_len - 2, q->input, q->input + q->in_len - 2);
    else if (q->prob.kind == K_MONEY)
        api->snprintf(s, sizeof s, "$%s", q->input);
    else
        api->snprintf(s, sizeof s, "%s", q->input);
    bool blink = (q->anim_t / 500) % 2;
    int tw = api->text_width(api->font_xlarge, s);
    mq_text(q, api->font_xlarge, x + w / 2 - tw / 2, y + (h - 28) / 2, s, Q_INK);
    if (blink)
        mq_fill(q, x + w / 2 + tw / 2 + 2, y + 6, 2, h - 12, Q_HOTPINK);
}

static void draw_keypad(mq_t *q, int y, char sym)
{
    static const char keys[10] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'};
    char s[2] = {0, 0};
    for (int i = 0; i < 8; i++) {
        s[0] = keys[i];
        button(q, 4 + i * 39, y, 37, 23, B_KEY, keys[i], s, Q_WHITE);
    }
    s[0] = '9';
    button(q, 4, y + 26, 37, 23, B_KEY, '9', s, Q_WHITE);
    s[0] = '0';
    button(q, 43, y + 26, 37, 23, B_KEY, '0', s, Q_WHITE);
    int x = 82;
    if (sym) {
        s[0] = sym;
        button(q, x, y + 26, 37, 23, B_KEY, sym, s, Q_LAV);
        x += 39;
    }
    button(q, x, y + 26, 50, 23, B_DEL, 0, "erase", Q_PEACH);
    button(q, 240, y + 26, 76, 23, B_OK, 0, "ok!", Q_MINT);
}

static void draw_play(mq_t *q)
{
    const modx_api_t *api = q->api;
    mq_prob_t *p = &q->prob;
    int w = q->world;
    char ch[32];
    api->snprintf(ch, sizeof ch, chapter[w], mq_cast_name(q, guide(w)));
    header(q, q->boss ? "The big finale" : ch, world_col(q, w, 0));
    /* the order board: one token per order of the run (a flower, a parcel, a cupcake, ...) */
    int total = q->run_n + q->extra;
    for (int i = 0; i < total && i < 12; i++) {
        int cx = 168 + i * 9, cy = MQ_Y0 + 10;
        uint16_t c = i < q->run_i ? world_col(q, w, 1) : i == q->run_i ? Q_HOTPINK : Q_GREY;
        if (w == G_GARDEN || w == G_FAIR || w == G_SKY)
            mq_star(q, cx, cy, 4, c);
        else if (w == G_BAKERY || w == G_ART)
            mq_heart(q, cx, cy, 3, c);
        else
            mq_rrect(q, cx - 3, cy - 3, 7, 7, 2, c);
    }
    if (q->combo >= 1) { /* the combo meter: fills over 5, bursts, starts again */
        int f = q->combo % 5 ? q->combo % 5 : 5;
        mq_rrect(q, 118, MQ_Y0 + 6, 44, 9, 4, Q_GREY);
        mq_rrect(q, 118, MQ_Y0 + 6, 4 + f * 8, 9, 4, f == 5 ? Q_YELLOW : Q_HOTPINK);
        mq_heart(q, 116, MQ_Y0 + 10, 6, Q_HOTPINK);
        if (f == 5 && q->sub == 1 && q->now - q->pressed_t < 200)
            mq_spark_burst(q, 140, MQ_Y0 + 12, 10);
    }
    for (int i = 0; i < 3; i++)
        if (p->text[i][0])
            mq_text(q, api->font_medium, 6, MQ_Y0 + 22 + i * 14, p->text[i], Q_INK);
    /* visual model */
    mq_rrect(q, 2, MQ_Y0 + 66, 200, 86, 8, Q_WHITE);
    int reveal = (int)(q->anim_t - q->scene_t) * 2;
    if (p->kind == K_SHADE || p->kind == K_LINE)
        reveal = 1000;
    if (p->kind == K_LINE)
        p->v[5] = q->sel;
    q->vis_q = true; /* the question's model: nothing to count for the answer (mq_vis.c) */
    mq_visual(q, p, 6, MQ_Y0 + 68, 192, 82, reveal);
    q->vis_q = false;
    if (p->kind == K_LINE)
        mq_add_btn(q, 2, MQ_Y0 + 66, 200, 86, B_LINE, 0);
    if (p->kind == K_SHADE)
        mq_add_btn(q, 2, MQ_Y0 + 66, 200, 86, B_PIE, 0);
    /* right panel: the friend, the answer */
    int bob = q->sub == 1 ? 1 : 0;
    int jump = 0;
    if (bob) { /* a happy jump */
        int t = (int)((q->now - q->pressed_t) % 600);
        jump = t * (600 - t) / 9000;
    }
    (void)jump;
    mq_sprite_anim(q, guide(w), 236, MQ_Y0 + 108, 2, bob ? AN_HAPPY : AN_IDLE); /* a happy jump on a right answer */
    if (q->sub == 1) {
        bubble(q, 206, MQ_Y0 + 70, 110, 34);
        mq_text_c(q, api->font_large, 261, MQ_Y0 + 76, q->praise ? q->praise : praise[0], Q_HOTPINK);
    } else if (p->kind != K_CHOICE && p->kind != K_SHADE && p->kind != K_LINE)
        draw_input_box(q, 206, MQ_Y0 + 68, 110, 38);
    if (q->msg[0][0] && q->sub == 0)
        mq_text(q, api->font_small, 206, MQ_Y0 + 152, q->msg[0], Q_PURPLE);
    if (q->retrying && q->sub == 0) { /* one more try: a small hint from the guide */
        mq_rrect(q, 4, MQ_Y0 + 128, 196, 22, 8, Q_YELLOW);
        api->snprintf(q->tmp, sizeof q->tmp, "Almost! %.36s", mq_mc_help[q->last_mc][0]);
        mq_text(q, api->font_small, 10, MQ_Y0 + 132, q->tmp, Q_INK);
    } else if (mq_prof(q)->problems < 3 && q->sub == 0 && p->kind != K_CHOICE) /* how to answer, in place */
        mq_text(q, api->font_small, 206, MQ_Y0 + 140, "Tap, then ok!", Q_PURPLE);
    /* answer area */
    int y = MQ_Y0 + 156;
    if (q->sub == 1) {
        button(q, 100, y + 6, 120, 34, B_CONT, 0, "next!", Q_MINT);
    } else if (p->kind == K_CHOICE) {
        int n = p->nchoice, bw = (312 - (n - 1) * 4) / n;
        for (int i = 0; i < n; i++)
            button(q, 4 + i * (bw + 4), y + 2, bw, 44, B_CHOICE, i, p->choice[i], i % 2 ? Q_SKY : Q_PINK);
    } else if (p->kind == K_SHADE || p->kind == K_LINE) {
        mq_text(q, api->font_small, 8, y + 4,
                p->kind == K_SHADE ? "Tap slices (or keys 1-8), then ok." : "Tap the line (or use arrows), then ok.",
                Q_DIM);
        button(q, 220, y + 20, 92, 26, B_OK, 0, "ok!", Q_MINT);
    } else
        draw_keypad(q, y, pad_symbol(p));
}

static void draw_feedback(mq_t *q)
{
    const modx_api_t *api = q->api;
    mq_prob_t *p = &q->prob;
    header(q, "Oops! Let's look...", world_col(q, q->world, 0));
    mq_sprite_anim(q, guide(q->world), 6, MQ_Y0 + 30, 3, AN_THINK); /* a gentle "hmm" */
    bubble(q, 70, MQ_Y0 + 24, 246, 50);
    /* (the misconception's name is for the grown-ups' log: she hears the friendly help) */
    api->snprintf(q->tmp, sizeof q->tmp, "%s", q->last_mc == MC_CLOSE || q->last_mc == MC_CARELESS ? mq_mc_name[q->last_mc]
                                                                                              : "Hmm! Let's look again.");
    mq_text(q, api->font_small, 76, MQ_Y0 + 26, q->tmp, Q_HOTPINK);
    mq_text(q, api->font_small, 76, MQ_Y0 + 41, mq_mc_help[q->last_mc][0], Q_INK);
    mq_text(q, api->font_small, 76, MQ_Y0 + 55, mq_mc_help[q->last_mc][1], Q_INK);
    mq_rrect(q, 4, MQ_Y0 + 80, 200, 92, 8, Q_WHITE);
    mq_prob_t vis = *p;
    if (vis.kind == K_SHADE) { /* show the right shading */
        vis.v[5] = (int16_t)((1 << p->ans) - 1);
        vis.v[4] = -1;
    }
    if (vis.kind == K_LINE)
        vis.v[3] = (int16_t)p->ans, vis.v[5] = -1;
    mq_visual(q, &vis, 8, MQ_Y0 + 84, 192, 84, (int)(q->anim_t - q->scene_t) * 2);
    char a[24], g[24];
    mq_format(q, p, p->ans, a, sizeof a);
    mq_format(q, p, q->last_given, g, sizeof g);
    mq_text(q, api->font_small, 210, MQ_Y0 + 84, "It's", Q_INK);
    mq_text(q, api->font_large, 210, MQ_Y0 + 98, a, Q_GREEN);
    api->snprintf(q->tmp, sizeof q->tmp, "You tried %s", g);
    mq_text(q, api->font_small, 210, MQ_Y0 + 124, q->tmp, Q_DIM);
    mq_text(q, api->font_small, 210, MQ_Y0 + 140, "No worries,", Q_DIM);
    mq_text(q, api->font_small, 210, MQ_Y0 + 153, "try another!", Q_DIM);
    button(q, 4, MQ_Y0 + 176, 170, 28, B_NEXT, 0, "next one!", Q_MINT);
    api->snprintf(q->tmp, sizeof q->tmp, "%s's trick", mq_cast_name(q, guide(q->world)));
    button(q, 180, MQ_Y0 + 176, 136, 28, B_SHOWME, 0, q->tmp, Q_LAV);
}

static void draw_offer(mq_t *q)
{
    const modx_api_t *api = q->api;
    header(q, "Need a hand?", world_col(q, q->world, 0));
    mq_sprite(q, guide(q->world), 20, MQ_Y0 + 50, 3, 1);
    bubble(q, 90, MQ_Y0 + 40, 220, 60);
    mq_text(q, api->font_medium, 98, MQ_Y0 + 48, "That one is tricky!", Q_INK);
    mq_text(q, api->font_small, 98, MQ_Y0 + 70, "Want to see my trick?", Q_INK);
    button(q, 40, MQ_Y0 + 140, 120, 34, B_YES, 0, "yes please", Q_LAV);
    button(q, 170, MQ_Y0 + 140, 120, 34, B_NO, 0, "I'll try", Q_MINT);
}

static void draw_example(mq_t *q)
{
    const modx_api_t *api = q->api;
    char out[3][MQ_TXT];
    mq_prob_t vis;
    q->ex_n = (uint8_t)mq_steps(q, &q->ex, q->ex_step, out, &vis);
    api->snprintf(q->tmp, sizeof q->tmp, "%s's trick", mq_cast_name(q, guide(mq_skill[q->ex.skill].world)));
    header(q, q->tmp, Q_CREAM);
    for (int i = 0; i < 3; i++)
        if (q->ex.text[i][0])
            mq_text(q, api->font_small, 6, MQ_Y0 + 22 + i * 13, q->ex.text[i], Q_INK);
    mq_rrect(q, 4, MQ_Y0 + 62, 230, 86, 8, Q_WHITE);
    if (vis.kind == K_SHADE)
        vis.v[5] = (int16_t)((1 << vis.ans) - 1), vis.v[4] = -1;
    if (vis.kind == K_LINE)
        vis.v[3] = (int16_t)(q->ex_step + 1 >= q->ex_n ? vis.ans : -1), vis.v[5] = -1;
    int stage_reveal = (int)(q->anim_t - q->scene_t) * 2;
    if (q->ex_step + 1 < q->ex_n)
        stage_reveal = stage_reveal * (q->ex_step + 1) / q->ex_n;
    mq_visual(q, &vis, 8, MQ_Y0 + 64, 222, 82, stage_reveal);
    mq_sprite(q, guide(mq_skill[q->ex.skill].world), 254, MQ_Y0 + 90, 2, 1);
    int k = mq_add_btn(q, 4, MQ_Y0 + 150, 312, 54, B_CONT, 0);
    (void)k;
    bubble(q, 4, MQ_Y0 + 150, 312, 54);
    for (int i = 0; i < 3; i++)
        mq_text(q, api->font_small, 10, MQ_Y0 + 152 + i * 13, out[i], Q_INK);
    for (int i = 0; i < q->ex_n - (q->ex_fade ? 1 : 0); i++)
        mq_circle(q, 240 + i * 10, MQ_Y0 + 196, 3, i <= q->ex_step ? Q_HOTPINK : Q_GREY);
    if (q->ex_fade && q->ex_step + 2 >= q->ex_n)
        mq_text(q, api->font_small, 150, MQ_Y0 + 189, "your turn!", Q_HOTPINK);
    mq_text(q, api->font_small, 286, MQ_Y0 + 189, "tap", Q_DIM);
}

/* The done screen's ways on, as widgets (mq_widget.c: tap with the pressed look, the arrows and ENTER, a
 * finger-sized hit area each): home, the kitchen (or the arcade), "Stay & play again!" (another round of the same
 * game in the same place: the user's "pushed around shops too quickly") and the usual way on. A tap anywhere
 * else, a swipe or ENTER with no other button chosen goes on, as ever; for a moment after the round's last
 * answer nothing is taken (her taps in the game must not wave the screen away). */
#define DONE_GUARD_MS 800u
#define DONE_Y (MQ_Y0 + 172)
static const char *done_label(int id)
{
    return id == B_GOHOME ? "home" : id == B_KITCHEN ? "kitchen" : id == B_ARCADE ? "arcade" : id == B_STAY ? "Stay & play again!"
                                                                                              : "next stop!";
}
static void done_layout(mq_t *q)
{
    static const struct { uint8_t x, w; uint16_t col; } at[4] = {{4, 40, Q_PEACH}, {47, 55, Q_PINK}, {105, 132, Q_YELLOW},
                                                                 {240, 76, Q_MINT}};
    bool kit = mq_extra(q)->energy >= 3; /* sparkles to spend in the Sparkle Kitchen */
    const uint8_t id[4] = {B_GOHOME, kit ? B_KITCHEN : B_ARCADE, B_STAY, B_CONT};
    mq_wg_begin(q);
    int next = -1;
    for (int k = 0; k < 4; k++) {
        int i = mq_wg_add(q, WG_BUTTON, id[k], 0, at[k].x, DONE_Y, at[k].w, 30);
        mq_wg_label(q, i, done_label(id[k]), k == 1 && !kit ? Q_SKY : k == 0 && q->errand_gift ? Q_YELLOW : at[k].col); /* (home: lit for an errand's present; the console's label cut to fit) */
        mq_wg_flags(q, i, WF_CUSTOM);
        if (id[k] == B_CONT)
            next = i;
    }
    if (q->wg.focus < 0) /* (the usual way on, as before: ENTER goes on) */
        mq_wg_focus(q, next);
}
static void done_open(mq_t *q) /* (nothing of the game's widgets carried over: the focus on the way on) */
{
    mq_wgs_t *s = &q->wg;
    s->focus_id = B_CONT, s->focus_arg = 0, s->kbd = false;
    s->press = s->carry = s->picked = s->hover = s->dragged = -1, s->flash_t = 0, s->downpick = false;
    s->n = 0, s->focus = -1;
}
static void done_touch(mq_t *q, int type, int x, int y)
{
    done_layout(q);
    if (q->anim_t - q->scene_t < DONE_GUARD_MS)
        return;
    mq_wev_t ev;
    bool took = mq_wg_touch(q, type, x, y, &ev);
    q->redraw = true;
    if (took && ev.why == WHY_TAP && ev.idx >= 0) {
        q->kbd = false;
        press(q, ev.id, ev.arg);
    } else if (type == 4 /* TEV_TAP */ && !took)
        press(q, B_CONT, 0); /* a tap anywhere else: on, as ever */
}
static void done_key(mq_t *q, int k)
{
    done_layout(q);
    mq_wev_t ev;
    q->redraw = true;
    if (mq_wg_key(q, k, 0, &ev)) {
        if ((ev.why == WHY_KEY || ev.why == WHY_TAP) && ev.idx >= 0)
            press(q, ev.id, ev.arg);
    } else if (k == K_ENTER)
        press(q, B_CONT, 0);
}
static void draw_done_buttons(mq_t *q)
{
    done_layout(q);
    for (int i = 0; i < q->wg.n; i++) {
        const mq_wg_t *g = &q->wg.w[i];
        button_look(q, g->x, g->y, g->w, g->h, mq_wg_pressed(q, i) ? 2 : 0, done_label(g->id), g->col, true);
        if (g->id == B_KITCHEN)
            mq_star(q, g->x + g->w - 10, g->y + 4, 4, (q->anim_t / 300) % 2 ? Q_YELLOW : Q_WHITE);
    }
    mq_wg_draw(q); /* (the focus ring, the pressed rim) */
}

static void draw_done(mq_t *q)
{
    const modx_api_t *api = q->api;
    mq_profile_t *p = mq_prof(q);
    header(q, q->boss ? "Big finale won!" : "Hooray!", Q_CREAM);
    for (int i = 0; i < 3; i++)
        mq_star(q, 110 + i * 50, MQ_Y0 + 52 - (i == 1 ? 6 : 0), i == 1 ? 20 : 16,
                i < q->stars_won ? Q_YELLOW : Q_GREY);
    int y = MQ_Y0 + 84;
    if (q->gm.podium) { /* a Kart Race: the podium */
        static const char pl[5][16] = {"", "1st place!", "2nd place!", "3rd place!", "What a race!"};
        mq_kart_draw_podium(q, 4, MQ_Y0 + 92);
        mq_text(q, api->font_medium, 100, y, pl[q->gm.podium <= 4 ? q->gm.podium : 4], Q_HOTPINK);
    } else {
        q->buddy_anim = AN_DANCE; /* a little dance */
        buddy(q, 20, MQ_Y0 + 90, 3);
        q->buddy_anim = AN_IDLE;
        mq_text(q, api->font_medium, 100, y, praise[(q->run_right + q->world) % 6], Q_HOTPINK);
    }
    if (q->gm.round_mech < ME_COUNT && q->gm.new_best_t) { /* a new best: a flourish, never a score */
        int bx = 12, by = MQ_Y0 + 150;
        mq_star(q, bx, by + 8, 7, Q_YELLOW);
        mq_text(q, api->font_small, bx + 10, by + 1, "New best!", Q_HOTPINK);
        if ((q->anim_t / 200) % 2)
            mq_spark_burst(q, bx + 40, by + 8, 2);
    }
    mq_home_t *hm = mq_home(q);
    if (q->best_combo > hm->best_combo && q->best_combo >= 3) {
        api->snprintf(q->tmp, sizeof q->tmp, "Record: %d in a row!", q->best_combo);
        mq_text(q, api->font_small, 196, y + 2, q->tmp, Q_PURPLE);
    }
    if (!q->boss) { /* (by the stars, top left: no row of its own, so the rewards below fit) */
        int skill = mq_levels[q->world][q->level] - 1;
        if (p->sk[skill].flags & SK_PASSED)
            mq_text(q, api->font_small, 8, MQ_Y0 + 40, "Super star!", Q_GREEN);
    }
    y += 18;
    /* the rewards, as many as fit above the "almost there" bar (MQ_Y0 + 140): a sticker and a hat
     * share a row when both came; the rest are told in the shop and the town anyway */
    const int room_end = MQ_Y0 + 138;
    bool both = q->reward_sticker != 0xFF && q->reward_hat != 0xFF;
    if (q->reward_sticker != 0xFF) {
        mq_sticker(q, q->reward_sticker, 100, y, 2);
        api->snprintf(q->tmp, sizeof q->tmp, both ? "%s!" : "New sticker: %s", mq_art_sticker_name[q->reward_sticker]);
        mq_text(q, api->font_small, 130, y + 6, q->tmp, Q_INK);
        if (!both)
            y += 28;
    }
    if (q->reward_hat != 0xFF) {
        int hx = both ? 206 : 96;
        mq_hat(q, q->reward_hat + 1, hx, y + 14, 2);
        mq_text(q, api->font_small, hx + 44, y + 6, both ? "A hat!" : "New hat for your buddy!", Q_INK);
        y += 28;
    }
    if (q->boss && q->world + 1 < MQ_NWORLD && y + 12 <= room_end) {
        api->snprintf(q->tmp, sizeof q->tmp, "%s is open!", mq_world_name[q->world + 1]);
        mq_text(q, api->font_small, 100, y, q->tmp, Q_PURPLE);
        y += 14;
    }
    if (q->new_set != 0xFF && q->new_set < MQ_NWORLD && y + 12 <= room_end) {
        api->snprintf(q->tmp, sizeof q->tmp, "New at the store: %s things!", mq_world_name[q->new_set]);
        mq_text(q, api->font_small, 100, y, q->tmp, Q_HOTPINK);
    }
    { /* "just one more": what the coins could buy next */
        const mq_profile_t *pp = mq_prof(q);
        int best = 0;
        for (int id = 1; id <= MQ_NITEM; id++)
            if (mq_item_unlocked(pp, id) && !hm->own[id] && (!best || mq_item[id].price < mq_item[best].price) &&
                mq_item[id].price > (int)mq_purse_cents(q))
                best = id;
        if (q->errand_gift) { /* a friend's errand done (mq_homestory.c): the thank-you, and "home" is right there */
            if (q->errand_gift != 0xFF)
                api->snprintf(q->tmp, sizeof q->tmp, "Thank you! A %s for you!", mq_item[q->errand_gift].name);
            else
                api->snprintf(q->tmp, sizeof q->tmp, "Thank you! A tip for you!");
            mq_text_fit(q, api->font_small, 100, MQ_Y0 + 156, 214, q->tmp, Q_HOTPINK, false);
        } else if (best) {
            api->snprintf(q->tmp, sizeof q->tmp, "Next: %s!", mq_item[best].name);
            mq_text(q, api->font_small, 100, MQ_Y0 + 156, q->tmp, Q_DIM);
        }
    }
    mq_meta_bar(q, 100, MQ_Y0 + 140, 210); /* "almost there": the next unlock */
    if (q->run_coins) { /* the coins of this run, into her purse */
        mq_purse_icon(q, 238, MQ_Y0 + 66, 9);
        mq_cents_run(q, api->font_medium, 250, MQ_Y0 + 59, "+", q->run_coins, Q_INK, false); /* "+37c", one run */
    }
    draw_done_buttons(q); /* her choice what's next: home, the kitchen, stay and play again, the next stop */
}

static void draw_arcade(mq_t *q)
{
    const modx_api_t *api = q->api;
    mq_profile_t *p = mq_prof(q);
    header(q, "Arcade", RGB(255, 246, 225));
    static const uint16_t col[4] = {Q_LAV, Q_PEACH, Q_MINT, Q_SKY};
    for (int g = 0; g < FG_COUNT; g++) {
        int x = 6 + (g % 2) * 157, y = MQ_Y0 + 24 + (g / 2) * 46;
        button(q, x, y, 151, 42, B_GAME, g, NULL, col[g]);
        mq_sprite(q, g == FG_STARS ? 8 : g == FG_CAFE ? 2 : g == FG_BLOOM ? 0 : 7, x + 6, y + 10, 1, 1);
        mq_text(q, api->font_medium, x + 32, y + 13, game_name[g], Q_INK);
    }
    static const uint16_t ac[ACT_COUNT] = {Q_SKY, Q_LAV, Q_MINT};
    for (int g = 0; g < ACT_COUNT; g++) { /* the action games (keypad) */
        int x = 6 + g * 104, y = MQ_Y0 + 116;
        bool open = mq_action_open(p, g); /* (all of them) */
        button(q, x, y, 100, 44, B_GAME, 10 + g, NULL, open ? ac[g] : Q_GREY);
        mq_text_c(q, api->font_small, x + 50, y + 8, mq_action_name(g), open ? Q_INK : Q_DIM);
        if (open && mq_home(q)->best[4 + g]) { /* played: a trophy (a best is never a number) */
            mq_trophy(q, x + 50, y + 30, 7);
        } else if (!open) {
            mq_rrect(q, x + 44, y + 24, 12, 10, 3, Q_DIM);
            mq_ring(q, x + 50, y + 23, 4, Q_DIM);
        }
    }
    button(q, 6, MQ_Y0 + 166, 308, 36, B_TOY, 0, NULL, Q_PINK); /* the toy box */
    mq_text_c(q, api->font_medium, 160, MQ_Y0 + 170, "Toy box: drums, puzzles and magic!", Q_INK);
    for (int k = 0; k < 3; k++)
        mq_star(q, 20 + k * 140, MQ_Y0 + 194, 4, k == 1 ? Q_HOTPINK : Q_YELLOW);
}

static void draw_factgame(mq_t *q)
{
    const modx_api_t *api = q->api;
    if (mq_fg_finished(q)) { /* the finish card, her buddy and a friend cheering beside "yay!" (never a friend left
                              * under the card: the user's "heads chopped off") */
        mq_fill(q, 0, MQ_Y0, MQ_W, MQ_H, Q_CREAM);
        bubble(q, 40, MQ_Y0 + 30, 240, 90);
        mq_text_c(q, api->font_large, 160, MQ_Y0 + 34, "You did it!", Q_PURPLE);
        mq_coins_end(q, 160, MQ_Y0 + 58, mq_game_coins(q), q->fg_end_t, q->fg_newbest); /* coins, never a score */
        mq_sprite_anim(q, mq_prof(q)->buddy % MQ_NCAST, 36, MQ_Y0 + 150, 2, AN_HAPPY);
        mq_sprite_anim(q, 8, 244, MQ_Y0 + 150, 2, AN_DANCE);
        button(q, 100, MQ_Y0 + 160, 120, 34, B_CONT, 0, "yay!", Q_MINT);
        return;
    }
    mq_fg_draw(q);
    mq_fill(q, 0, MQ_Y0 + 150, MQ_W, 56, Q_CREAM);
    if (mq_fg_choosing(q)) { /* the moving games: the answers to tap (never typed) */
        int32_t c[4];
        int n = mq_fg_choices(q, c);
        for (int i = 0; i < n; i++) {
            char t[12];
            api->snprintf(t, sizeof t, "%ld", (long)c[i]);
            button(q, 6 + i * 78, MQ_Y0 + 156, 74, 44, B_FGOPT, i, t, i % 2 ? Q_SKY : Q_PINK);
        }
        api->snprintf(q->tmp, sizeof q->tmp, "%d/%d", q->fg_score, q->fg_goal);
        mq_rrect(q, 270, MQ_Y0 + 3, 46, 16, 8, Q_WHITE);
        mq_text_c(q, api->font_small, 293, MQ_Y0 + 4, q->tmp, Q_INK);
        return;
    }
    static const char keys[10] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'};
    char s[2] = {0, 0};
    for (int i = 0; i < 10; i++) {
        s[0] = keys[i];
        button(q, 4 + i * 31, MQ_Y0 + 152, 29, 24, B_KEY, keys[i], s, Q_WHITE);
    }
    mq_rrect(q, 4, MQ_Y0 + 180, 150, 24, 8, Q_WHITE);
    mq_text(q, api->font_large, 12, MQ_Y0 + 182, q->input, Q_INK);
    button(q, 160, MQ_Y0 + 180, 70, 24, B_DEL, 0, "erase", Q_PEACH);
    button(q, 236, MQ_Y0 + 180, 80, 24, B_OK, 0, "ok!", Q_MINT);
    api->snprintf(q->tmp, sizeof q->tmp, "%d/%d", q->fg_score, q->fg_goal);
    mq_rrect(q, 270, MQ_Y0 + 3, 46, 16, 8, Q_WHITE);
    mq_text_c(q, api->font_small, 293, MQ_Y0 + 4, q->tmp, Q_INK);
}

static void draw_stickers(mq_t *q)
{
    const modx_api_t *api = q->api;
    mq_profile_t *p = mq_prof(q);
    header(q, "Sticker book", RGB(245, 240, 255));
    for (int k = 0; k < MQ_NSTICKER; k++) {
        int x = 8 + (k % 8) * 39, y = MQ_Y0 + 26 + (k / 8) * 40;
        mq_rrect(q, x, y, 36, 36, 6, Q_WHITE);
        if ((p->stickers >> k) & 1)
            mq_sticker(q, k, x + 6, y + 6, 2);
        else
            mq_text_c(q, api->font_medium, x + 18, y + 11, "?", Q_GREY);
    }
    mq_text(q, api->font_small, 8, MQ_Y0 + 110, "Friends", Q_INK);
    for (int i = 0; i < MQ_NCAST; i++) {
        bool met = i == 0 || ((p->met >> i) & 1) || ((p->visited >> i) & 1);
        if (met)
            mq_sprite(q, i, 8 + i * 34, MQ_Y0 + 126, 1, 0);
        else
            mq_circle(q, 18 + i * 34, MQ_Y0 + 136, 9, Q_GREY);
    }
    mq_text(q, api->font_small, 8, MQ_Y0 + 156, "Hats", Q_INK);
    for (int h = 0; h < MQ_NHAT; h++)
        if ((p->hats >> h) & 1)
            mq_hat(q, h + 1, 10 + h * 40, MQ_Y0 + 182, 2);
        else
            mq_circle(q, 30 + h * 40, MQ_Y0 + 182, 6, Q_GREY);
}

static void draw_parent(mq_t *q)
{
    const modx_api_t *api = q->api;
    mq_profile_t *p = mq_prof(q);
    static const char tabs[4][8] = {"Skills", "Facts", "Time", "Setup"};
    api->snprintf(q->tmp, sizeof q->tmp, "Grown-ups: %s (%s)", tabs[q->tab & 3], mq_name(q));
    mq_fill(q, 0, MQ_Y0, MQ_W, MQ_H, Q_WHITE);
    mq_fill(q, 0, MQ_Y0, MQ_W, 18, Q_LAV);
    mq_text_fit(q, api->font_small, 6, MQ_Y0 + 3, q->tab == 0 ? 158 : q->tab == 2 ? 226 : MQ_W - 12, q->tmp, Q_INK, false); /* (clear of the tab's buttons) */
    int y = MQ_Y0 + 22;
    if (q->tab == 0) { /* the error patterns (a tap on "patterns"), or the domains and the standards */
        int ns = mq_diag_spotted(q);
        api->snprintf(q->tmp, sizeof q->tmp, q->parent_pat == 1 ? "standards" : ns ? "patterns (%d)" : "patterns", ns);
        button(q, 236, MQ_Y0 + 1, 80, 16, B_PATTERNS, 0, q->tmp, ns && q->parent_pat != 1 ? Q_PINK : Q_WHITE);
        button(q, 168, MQ_Y0 + 1, 64, 16, B_STRATS, 0, q->parent_pat == 2 ? "standards" : "ways", Q_WHITE);
    }
    if (q->tab == 0 && q->parent_pat == 2) {
        mq_strat_parent(q, y); /* the ways she has met, with their Common Core names */
    } else if (q->tab == 0 && q->parent_pat) {
        mq_diag_parent(q, y);
    } else if (q->tab == 0) {
        for (int d = 0; d < MQ_NDOM; d++) {
            int ok, tot, pc = mq_dom_progress(p, d, &ok, &tot);
            api->snprintf(q->tmp, sizeof q->tmp, "%s %d/%d", mq_dom_name[d], ok, tot);
            mq_text(q, api->font_small, 4, y, q->tmp, Q_INK);
            mq_fill(q, 220, y + 3, 96, 7, Q_GREY);
            mq_fill(q, 220, y + 3, 96 * pc / 100, 7, Q_GREEN);
            y += 13;
        }
        y += 2;
        int rows = 8, first = q->parent_scroll;
        for (int i = 0; i < rows && first + i < MQ_NSTD; i++) {
            const mq_std_t *s = &mq_std[first + i];
            bool ok = mq_std_passed(p, first + i);
            int m = 0, n = 0;
            for (int j = 0; j < 4; j++)
                if (s->skills[j])
                    m += p->sk[s->skills[j] - 1].mastery, n++;
            api->snprintf(q->tmp, sizeof q->tmp, "%-8s%s %3d%% %.27s", s->id, ok ? "*" : " ", n ? m / n : 0, s->desc);
            mq_text(q, api->font_small, 4, y, q->tmp, ok ? Q_GREEN : s->grade == 5 ? Q_PURPLE : Q_INK);
            if (s->skills[0]) /* a tap previews how it is asked */
                mq_add_btn(q, 0, y, MQ_W, 13, B_PREVIEW, s->skills[0] - 1);
            y += 13;
        }
        { /* the level each strand works at now (what the grade setting changes, at once) */
            const mq_pace_t *pc = mq_pace(q);
            api->snprintf(q->tmp, sizeof q->tmp, "* passed  now: x%d place%d frac%d meas%d  tap: preview", pc->strand[0],
                          pc->strand[1], pc->strand[2], pc->strand[3]);
        }
        mq_text(q, api->font_small, 4, MQ_Y0 + 194, q->tmp, Q_DIM);
    } else if (q->tab == 1) {
        int tot, fl = mq_facts_fluent(p, &tot);
        api->snprintf(q->tmp, sizeof q->tmp, "%s: %d of %d fluent (3 s)", q->parent_div ? "Division" : "Times", fl, tot);
        mq_text(q, api->font_small, 4, y, q->tmp, Q_INK);
        button(q, 250, y - 2, 66, 16, B_DIVT, 0, q->parent_div ? "show x" : "show \xC3\xB7", Q_LAV);
        int cs = 12, ox = 30, oy = y + 26;
        for (int i = 0; i <= 12; i++) {
            api->snprintf(q->tmp, sizeof q->tmp, "%d", i);
            if (i % 2 == 0)
                mq_text_c(q, api->font_small, ox + i * cs + cs / 2, oy - 13, q->tmp, Q_DIM);
            if (i % 2 == 0)
                mq_text(q, api->font_small, 8, oy + i * cs - 1, q->tmp, Q_DIM);
        }
        for (int a = 0; a <= 12; a++)
            for (int b = 0; b <= 12; b++) {
                uint16_t c = Q_WHITE;
                if (!(q->parent_div && b == 0)) {
                    int f = q->parent_div ? mq_fact_index(a, b, true) : mq_fact_index(a, b, false);
                    const mq_fact_t *t = &p->fact[f];
                    c = mq_fact_fluent(t) ? Q_GREEN : t->box >= 3 ? Q_YELLOW : t->seen ? Q_PINK : Q_GREY;
                    if (!mq_fact_required(p, f) && !t->seen)
                        c = RGB(240, 240, 244);
                }
                mq_fill(q, ox + b * cs, oy + a * cs, cs - 1, cs - 1, c);
                if (!(q->parent_div && b == 0) && mq_focus_fact(q, q->parent_div ? mq_fact_index(a, b, true) : mq_fact_index(a, b, false)))
                    mq_ring(q, ox + b * cs + cs / 2 - 1, oy + a * cs + cs / 2 - 1, 3, Q_PURPLE); /* in focus */
            }
        /* a tap on a cell puts that fact in focus, on a row's number the whole table */
        mq_add_btn(q, 0, oy, ox + 13 * cs, 13 * cs, B_FOCUSGRID, 0);
        int lx = 196;
        static const char lg[4][14] = {"fluent", "almost", "learning", "not yet"};
        static const uint16_t lc[4] = {Q_GREEN, Q_YELLOW, Q_PINK, Q_GREY};
        for (int i = 0; i < 4; i++) {
            mq_fill(q, lx + (i % 2) * 60, oy - 2 + (i / 2) * 12, 8, 8, lc[i]);
            mq_text(q, api->font_small, lx + 11 + (i % 2) * 60, oy - 4 + (i / 2) * 12, lg[i], Q_INK);
        }
        /* focus this week: what school sends home (tap the grid: a fact; a row's number: its table; keys 0-9
         * a table); its progress, "ready for the test" when every fact is known on two play days */
        mq_text(q, api->font_medium, lx, oy + 24, "Focus this week", Q_PURPLE);
        int ffl, d2, nf = mq_focus_count(q, &ffl, &d2);
        mq_pace_t *pc = mq_pace(q);
        api->snprintf(q->tmp, sizeof q->tmp, nf ? "%d facts in focus" : "tap facts or tables", nf);
        mq_text(q, api->font_small, lx, oy + 38, q->tmp, Q_INK);
        if (mq_focus_ready(q))
            mq_text(q, api->font_small, lx, oy + 50, "ready for the test!", Q_GREEN);
        else if (nf) {
            api->snprintf(q->tmp, sizeof q->tmp, "%d fast, %d known", ffl, d2);
            mq_text(q, api->font_small, lx, oy + 50, q->tmp, Q_DIM);
        }
        button(q, lx, oy + 66, 120, 18, B_FOCUSWEEK, 0, pc->focus_end ? "for this week" : "until changed", Q_SKY);
        api->snprintf(q->tmp, sizeof q->tmp, "share %d%%", pc->focus_share >= 20 ? pc->focus_share : 50);
        button(q, lx, oy + 88, 58, 18, B_FOCUSSHARE, 0, q->tmp, Q_LAV);
        button(q, lx + 62, oy + 88, 58, 18, B_FOCUSCLR, 0, "clear", Q_PEACH);
        api->snprintf(q->tmp, sizeof q->tmp, "+ %.16s", pc->focus_skill[0] ? mq_skill[pc->focus_skill[0] - 1].name : "a skill");
        button(q, lx, oy + 110, 120, 18, B_FOCUSSKILL, 0, q->tmp, Q_MINT);
        mq_text(q, api->font_small, lx, oy + 132, q->parent_div ? "row a: a*b / b" : "row a, col b", Q_DIM);
    } else if (q->tab == 2 && (button(q, 236, MQ_Y0 + 1, 80, 16, B_RECENT, 0, q->parent_recent ? "summary" : "recent",
                                      q->parent_recent ? Q_PINK : Q_WHITE),
                               q->parent_recent)) {
        /* the recent orders (this session): the order, her first answer, right or not, the rung, why it came -- the
         * tracking at work (the user: "I'm not sure the problem tracking is working") */
        static const char why[PW_COUNT][8] = {"goal", "new", "review", "focus", "taste", "check", "place", "fresh", "test",
                                              "kitchen"};
        mq_text(q, api->font_small, 4, y, "order                       her  right rung why", Q_DIM);
        int rows = q->recent_n < 13 ? q->recent_n : 13;
        for (int i = 0; i < rows; i++) {
            const mq_recent_t *r = &q->recent_o[(q->recent_i + MQ_NRECENT - 1 - i) % MQ_NRECENT];
            api->snprintf(q->tmp, sizeof q->tmp, "%-27.27s %5.5s %s %d %s", r->q, r->her, r->right ? "yes" : "no ", r->rung,
                          why[r->why % PW_COUNT]);
            mq_text(q, api->font_small, 4, y + 13 + i * 13, q->tmp, r->right ? Q_INK : Q_HOTPINK);
        }
        if (!q->recent_n)
            mq_text(q, api->font_small, 4, y + 13, "(no orders yet this session)", Q_DIM);
    } else if (q->tab == 2) {
        api->snprintf(q->tmp, sizeof q->tmp, "Time played: %lu min (today %u min)", (unsigned long)(p->secs / 60),
                      (unsigned)(p->day_secs / 60));
        mq_text(q, api->font_small, 4, y, q->tmp, Q_INK);
        int right = 0, tries = 0;
        for (int s = 0; s < MQ_NSKILL; s++)
            right += p->sk[s].right, tries += p->sk[s].tries;
        api->snprintf(q->tmp, sizeof q->tmp, "Play days: %u  sessions: %u  problems: %lu", (unsigned)p->days,
                      (unsigned)mq_home(q)->sessions, (unsigned long)p->problems);
        mq_text(q, api->font_small, 4, y + 14, q->tmp, Q_INK);
        api->snprintf(q->tmp, sizeof q->tmp, "Right first try: %d%%", tries ? right * 100 / tries : 0);
        mq_text(q, api->font_small, 4, y + 28, q->tmp, Q_INK);
        int tot, fl = mq_facts_fluent(p, &tot), ok = 0;
        for (int s = 0; s < MQ_NSTD; s++)
            ok += mq_std[s].grade <= 4 && mq_std_passed(p, s);
        api->snprintf(q->tmp, sizeof q->tmp, "Standards passed: %d   facts fluent: %d/%d", ok, fl, tot);
        mq_text(q, api->font_small, 4, y + 42, q->tmp, Q_INK);
        mq_text(q, api->font_small, 4, y + 150, mq_coach_prompt(q), Q_GREEN); /* something to talk about */
        int rin, rdue, rslip, worst = mq_review_worst(q);
        mq_review_stats(q, &rin, &rdue, &rslip);
        api->snprintf(q->tmp, sizeof q->tmp, "Review: %d skills (%d due), mixed in %d%%, %d slipped%s%.20s", rin, rdue,
                      mq_review_share(q), rslip, worst >= 0 ? ": " : "", worst >= 0 ? mq_skill[worst].name : "");
        mq_text(q, api->font_small, 4, y + 54, q->tmp, Q_PURPLE);
        { /* the big ideas: what was shown, what she knew, what she skipped */
            int sh = 0, kn = 0, sk = 0, met = 0, last = 0;
            for (int i = 1; i < IDEA_COUNT; i++) {
                int a, b2, c;
                if (mq_teach_summary(q, i, &a, &b2, &c))
                    met++, sh += a, kn += b2, sk += c, last = i;
            }
            api->snprintf(q->tmp, sizeof q->tmp, met ? "Big ideas met %d (last: %s): parts shown %d, knew %d, skipped %d"
                                                     : "Big ideas: none met yet", met, mq_idea_name(last), sh, kn, sk);
            mq_text(q, api->font_small, 4, y + 138, q->tmp, Q_PURPLE);
        }
        mq_text(q, api->font_small, 4, y + 66, "Daily play limit (a day starts at power-on):", Q_INK);
        for (int i = 0; i < 7; i++) {
            char s[8];
            api->snprintf(s, sizeof s, i ? "%d" : "off", limits[i]);
            button(q, 4 + i * 44, y + 82, 40, 24, B_LIMIT, i, s, p->limit_min == limits[i] ? Q_YELLOW : Q_GREY);
        }
        mq_text(q, api->font_small, 4, y + 108, "minutes; then \"time for a break\" after the round.", Q_DIM);
        /* the home story (mq_homestory.c): home time before a friend calls her outside */
        mq_text(q, api->font_small, 4, y + 124, "Home time:", Q_INK);
        static const uint8_t hmin[5] = {0, 3, 6, 10, 15};
        int hb = mq_hstory_budget(q) / 60;
        for (int i = 0; i < 5; i++) {
            char s[8];
            api->snprintf(s, sizeof s, i ? "%d" : "off", hmin[i]);
            button(q, 74 + i * 40, y + 120, 36, 16, B_HOMEMIN, hmin[i], s, hb == hmin[i] ? Q_YELLOW : Q_GREY);
        }
        mq_text(q, api->font_small, 276, y + 124, "min", Q_DIM);
    } else {
        button(q, 4, y, 200, 22, B_TOGGLE12, 0, p->facts12 ? "11 and 12 facts: on" : "11 and 12 facts: off", Q_LAV);
        /* where she starts (the placement in her first game finds her level from there; a tap: the next) */
        api->snprintf(q->tmp, sizeof q->tmp, "starts at grade %d", mq_start_grade(q));
        button(q, 210, y, 106, 22, B_GRADE, 0, q->tmp, Q_MINT);
        /* where school is: her grade and the week of its year (the placement starts there, never ahead of school) */
        int wk, sg = mq_school(q, &wk);
        mq_text(q, api->font_small, 4, y + 30, "School:", Q_INK);
        api->snprintf(q->tmp, sizeof q->tmp, sg ? "grade %d" : "grade: -", sg);
        button(q, 46, y + 26, 58, 20, B_SCHOOLG, 0, q->tmp, sg ? Q_MINT : Q_GREY);
        if (sg) {
            button(q, 108, y + 26, 22, 20, B_SCHOOLW, 0, "-", Q_LAV);
            api->snprintf(q->tmp, sizeof q->tmp, "week %d", wk);
            mq_text_fit(q, api->font_small, 158, y + 30, 50, q->tmp, Q_INK, true);
            button(q, 186, y + 26, 22, 20, B_SCHOOLW, 1, "+", Q_LAV);
        }
        button(q, 226, y + 26, 90, 20, B_TWISTS, 0, mq_pace(q)->twists_off ? "twists: off" : "twists: on", Q_PEACH);
        button(q, 210, y + 112, 106, 22, B_SPEECH, 0, mq_pace(q)->voice == VO_OFF ? "voices: off" : mq_pace(q)->voice == VO_WORDS ?
               "voices: words" : "voices: babble", Q_SKY);
        api->snprintf(q->tmp, sizeof q->tmp, "reading: %s", mq_read_speed_name(mq_pace(q)->read_speed));
        button(q, 4, y + 88, 100, 20, B_READSPEED, 0, q->tmp, Q_SKY); /* (how long lines stay; a tap skips them) */
        mq_text(q, api->font_small, 4, y + 52, "Theme pack (/CALC/THEMES/*.THM):", Q_INK);
        button(q, 4, y + 66, 100, 20, B_THEME, 0, "built-in", q->save.theme[0] ? Q_GREY : Q_YELLOW);
        for (int i = 0; i < q->ntheme_files && i < 4; i++) { /* (two rows of them; the rest by name in the console) */
            bool cur = !q->api->strcmp(q->save.theme, q->theme_files[i]);
            button(q, 108 + (i % 2) * 104, y + 66 + (i / 2) * 22, 100, 20, B_THEME, i + 1, q->theme_files[i],
                   cur ? Q_YELLOW : Q_GREY);
        }
        if (q->theme_err)
            mq_text(q, api->font_small, 4, y + 112, q->theme_err == -1 ? "Theme not found: built-in cast."
                                                    : q->theme_err == -2 ? "Not a theme pack: built-in cast."
                                                                         : "Theme damaged: built-in cast.",
                    Q_HOTPINK);
        else if (q->theme.loaded)
            mq_text(q, api->font_small, 4, y + 112, q->theme.title, Q_INK);
        button(q, 4, y + 140, 200, 22, B_RESET, 0, q->reset_arm ? "tap again to reset" : "reset this player",
               q->reset_arm ? Q_HOTPINK : Q_PEACH);
        button(q, 210, y + 140, 106, 22, B_LOG, 0, "play log to card", Q_SKY);
        /* the share of pure fun (pets, bubbles, squishies, decorating) earned by maths play */
        mq_text(q, api->font_small, 4, y + 166, "Free fun share:", Q_INK);
        static const uint8_t pcts[5] = {0, 5, 10, 20, 30};
        int cur = mq_fun_pct(p);
        for (int i = 0; i < 5; i++) {
            char s[8];
            api->snprintf(s, sizeof s, "%d%%", pcts[i]);
            button(q, 100 + i * 43, y + 162, 40, 20, B_FUNPCT, pcts[i], s, cur == pcts[i] ? Q_YELLOW : Q_GREY);
        }
    }
}

static void draw_simple(mq_t *q, const char *title, const char *l1, const char *l2, const char *btn, int id)
{
    const modx_api_t *api = q->api;
    header(q, title, Q_CREAM);
    buddy(q, 20, MQ_Y0 + 60, 3);
    bubble(q, 90, MQ_Y0 + 40, 222, 60);
    mq_text(q, api->font_medium, 98, MQ_Y0 + 48, l1, Q_INK);
    mq_text(q, api->font_small, 98, MQ_Y0 + 70, l2, Q_INK);
    button(q, 160, MQ_Y0 + 150, 140, 34, id, 0, btn, Q_MINT);
}

static void draw_scene(mq_t *q)
{
    q->nbtn = 0;
    q->lane_on = false; /* (the screen gives its toast lane while it draws) */
    q->toast_w = 0;
    q->coin_ix = 0;
    switch (q->scene) {
    case SC_TITLE: draw_title(q); break;
    case SC_NAME: draw_name(q); break;
    case SC_MAP: draw_map(q); break;
    case SC_WORLD: draw_world(q); break;
    case SC_INTRO: draw_intro(q); break;
    case SC_QUIZ: draw_play(q); break;
    case SC_PLAY: mq_game_draw(q); break;
    case SC_TOYS: mq_toys_draw(q); break;
    case SC_FEEDBACK: draw_feedback(q); break;
    case SC_OFFER: draw_offer(q); break;
    case SC_EXAMPLE: draw_example(q); break;
    case SC_DONE_LEVEL: draw_done(q); break;
    case SC_ARCADE: draw_arcade(q); break;
    case SC_FACTGAME: draw_factgame(q); break;
    case SC_ROOM: mq_draw_room(q, false); break;
    case SC_SHOWOFF: mq_draw_room(q, true); break;
    case SC_SHOP: mq_draw_shop(q); break;
    case SC_BOOK: mq_draw_book(q); break;
    case SC_WARDROBE: mq_draw_wardrobe(q); break;
    case SC_STICKERS: draw_stickers(q); break;
    case SC_PARENT: draw_parent(q); break;
    case SC_COMPLETE:
        draw_simple(q, "Sparkle Town is saved!", "You did it, every place!", "The whole town sparkles. Amazing!",
                    "hooray!", B_CONT);
        mq_hat(q, 1, 20, MQ_Y0 + 60, 3);
        break;
    case SC_BREAK:
        draw_simple(q, "Good night, Sparkle Town", "The cafe closes for the night.", "Everyone is resting. See you soon!",
                    "night night", B_CONT);
        break;
    case SC_SURPRISE: mq_draw_gift(q); break;
    case SC_FUN: mq_fun_draw(q); break;
    case SC_ACTION: mq_action_draw(q); break;
    case SC_CHAPTER: mq_chapter_draw(q); break;
    case SC_MERGE: mq_merge_draw(q); break;
    default: break;
    }
    if (q->gate.shown)
        mq_gate_draw(q);
    bool partial = mq_toys_partial(q); /* only new dots are drawn: nothing that would leave trails */
    if (!partial)
        mq_fun_hook_draw(q);
    if (!partial && !q->gate.shown)
        mq_hstory_draw(q); /* the home story: a thought, a friend's card */
    if (q->scene != SC_SHOWOFF && !partial)
        mq_sparks_draw(q);
    if (!partial)
        mq_juice_draw(q);
    if (q->earn_msg[0] && q->lane_on && q->scene != SC_SHOWOFF && q->scene != SC_PLAY) { /* a toast, in its lane */
        if (!q->earn_msg_t)
            q->earn_msg_t = q->now ? q->now : 1; /* (its time starts when she can see it) */
        if (q->now - q->earn_msg_t < mq_toast_ms(q)) {
            int room = q->lane_x1 - q->lane_x0;
            int w = q->api->text_width(q->api->font_small, q->earn_msg) + 16;
            w = w < room ? w : room;
            if (w >= 40) {
                int x = q->lane_x0 + (room - w) / 2;
                q->toast_x = (int16_t)x, q->toast_w = (int16_t)w; /* (a tap there puts it away) */
                mq_rrect(q, x, q->lane_y, w, 16, 7, Q_YELLOW);
                mq_text_fit(q, q->api->font_small, x + w / 2, q->lane_y + 1, w - 10, q->earn_msg, Q_INK, true);
            }
        }
    }
    focus_ring(q);
    q->clip_y1 = 0;
}

/* a new player (or one who never had it said) takes the save's default school year, if a page preset one
 * (`mq school G W default`: save.pad[0] the grade, pad[1] the week) */
static void school_default(mq_t *q)
{
    int wk, g = mq_school(q, &wk);
    if (!g && !wk && q->save.pad[0] >= 3 && q->save.pad[0] <= 5)
        mq_set_school(q, q->save.pad[0], q->save.pad[1]);
}

/* ------------------------------------------------------------------ toasts */
static void toast_shift(mq_t *q) /* the first waiting one leaves the queue */
{
    for (int i = 0; i + 1 < q->earn_qn; i++)
        q->api->snprintf(q->earn_q[i], sizeof q->earn_q[i], "%s", q->earn_q[i + 1]);
    if (q->earn_qn)
        q->earn_qn--;
}
void mq_toast(mq_t *q, const char *s)
{
    if (!s || !s[0])
        return;
    if (!q->earn_msg[0] || (q->earn_msg_t && q->now - q->earn_msg_t >= mq_toast_ms(q) && !q->earn_qn)) {
        q->api->snprintf(q->earn_msg, sizeof q->earn_msg, "%s", s);
        q->earn_msg_t = 0;
    } else { /* after the others (the oldest waiting one goes when there are too many) */
        if (q->earn_qn == 3)
            toast_shift(q);
        q->api->snprintf(q->earn_q[q->earn_qn++], sizeof q->earn_q[0], "%s", s);
    }
    q->redraw = true;
}
uint32_t mq_toast_ms(mq_t *q) { return (uint32_t)mq_read_ms(q, q->earn_msg); }
/* a tap on the toast puts it away (the next one comes after the gap); a finger that came down on it is the
 * toast's to the end of its gesture: never also a tap on what is under it */
static bool toast_up(mq_t *q)
{
    return q->earn_msg[0] && q->earn_msg_t && q->now - q->earn_msg_t < mq_toast_ms(q);
}
void mq_toast_dismiss(mq_t *q)
{
    if (!toast_up(q))
        return;
    q->toast_skip = true;
    q->earn_msg_t = q->now - mq_toast_ms(q) ? q->now - mq_toast_ms(q) : 1;
    q->redraw = true;
}
static bool toast_touch(mq_t *q, int type, int x, int y)
{
    if (type == 1) { /* (a finger down: on the toast's box, drawn in its lane this frame) */
        q->toast_tap = toast_up(q) && q->toast_w && x >= q->toast_x && x < q->toast_x + q->toast_w &&
                       y >= q->lane_y - 2 && y < q->lane_y + 18;
        return q->toast_tap;
    }
    if (!q->toast_tap)
        return false;
    if (type == 3 || type == 4 || type == 7) { /* (7: the one-press guard's cancel) */
        q->toast_tap = false;
        if (type == 4)
            mq_toast_dismiss(q);
    }
    return true;
}
bool mq_toast_busy(mq_t *q)
{
    return q->earn_msg[0] && (!q->earn_msg_t || q->now - q->earn_msg_t < mq_toast_ms(q) + TOAST_GAP_MS);
}
void mq_lane(mq_t *q, int x0, int y, int x1)
{
    q->lane_on = x1 - x0 >= 40;
    q->lane_x0 = (int16_t)x0, q->lane_x1 = (int16_t)x1, q->lane_y = (int16_t)y;
}
const char *mq_toast_claim(mq_t *q)
{
    if (!q->earn_msg[0])
        return NULL;
    if (!q->earn_msg_t)
        q->earn_msg_t = q->now ? q->now : 1;
    return q->now - q->earn_msg_t < mq_toast_ms(q) ? q->earn_msg : NULL;
}
/* every tick: a toast shown long enough goes (the screen redrawn without it), the next one comes */
static void toast_tick(mq_t *q)
{
    if (q->earn_msg[0] && !q->earn_msg_t && q->lane_on && q->scene != SC_PLAY && q->scene != SC_SHOWOFF)
        q->earn_msg_t = q->now ? q->now : 1; /* the screen on show has its lane: up now */
    if (!q->earn_msg[0] || !q->earn_msg_t || q->now - q->earn_msg_t < mq_toast_ms(q))
        return;
    if (q->now - q->earn_msg_t < mq_toast_ms(q) + TOAST_GAP_MS) { /* gone: a short gap before the next */
        if (!q->toast_gone) {
            q->toast_gone = true, q->redraw = true;
            mq_read_note(q, q->now - q->earn_msg_t, (int)mq_toast_ms(q), q->toast_skip);
            q->toast_skip = false, q->toast_w = 0;
        }
        return;
    }
    q->toast_gone = false;
    if (q->earn_qn) {
        q->api->snprintf(q->earn_msg, sizeof q->earn_msg, "%s", q->earn_q[0]);
        toast_shift(q);
        q->earn_msg_t = 0;
    } else
        q->earn_msg[0] = 0;
    q->redraw = true;
}

/* ------------------------------------------------------------------ actions */
/* the grown-ups' gate (mq_gate.c): its answer right, the grown-ups' screen */
static void open_parent(mq_t *q);
static void gate_result(mq_t *q, int r)
{
    if (r == MQG_PASS) {
        mq_gate_close(q);
        open_parent(q);
    }
    q->redraw = true;
}

static void open_parent(mq_t *q)
{
    q->tab = 0, q->parent_scroll = 0, q->gate.adult_t = q->now;
    mq_theme_scan(q);
    go(q, SC_PARENT);
}

static void enter_map(mq_t *q)
{
    mq_profile_t *p = mq_prof(q);
    if (!q->day_started) { /* a new play day (the first visit after power-on) */
        q->day_started = true;
        if (mq_home(q)->sessions < 65535)
            mq_home(q)->sessions++;
        p->days++;
        p->day_secs = 0;
        mq_quest_roll(q); /* three new quests */
        if (p->surprise_day != (uint8_t)p->days && p->problems > 0) {
            p->surprise_day = (uint8_t)p->days;
            q->surprise = 0;
            q->dirty = true;
            go(q, SC_SURPRISE);
            return;
        }
    }
    go(q, SC_MAP);
}

static bool over_limit(mq_t *q)
{
    mq_profile_t *p = mq_prof(q);
    return p->limit_min && p->day_secs >= (uint32_t)p->limit_min * 60u;
}

/* a character into the name (up to MQ_NAME_MAX: letters, space, - and '); the on-screen letters
 * (as_typed false) give "Mia" from M, I, A; a keyboard's (and the calculator's ALPHA, SHIFT ALPHA for
 * upper case) keep their case, the first letter made a capital. false: not a name character. */
static bool name_type(mq_t *q, char c, bool as_typed)
{
    bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    if (!letter && c != ' ' && c != '-' && c != '\'')
        return false;
    char b[MQ_NAME_MAX + 1];
    const char *s = mq_name(q);
    int n = 0;
    while (s[n] && n < MQ_NAME_MAX)
        b[n] = s[n], n++;
    if (n < MQ_NAME_MAX && (letter || n > 0)) { /* (no space or mark first) */
        if (c >= 'a' && c <= 'z' && n == 0)
            c = (char)(c - 32);
        else if (!as_typed && c >= 'A' && c <= 'Z' && n > 0)
            c = (char)(c + 32);
        b[n++] = c;
    }
    b[n] = 0;
    mq_pname_set(q, q->save.cur, b);
    q->redraw = true;
    return true;
}

/* MODX_EV_TEXT: a typed character for a text field (the name); false: no field here took it */
static bool text_in(mq_t *q, int c)
{
    if (q->scene != SC_NAME)
        return false;
    q->last_input = q->now;
    return name_type(q, (char)c, true);
}

static void type_char(mq_t *q, char c)
{
    if (q->scene == SC_NAME) {
        name_type(q, c, false);
        q->redraw = true;
        return;
    }
    if (q->scene == SC_FACTGAME) {
        if (c >= '0' && c <= '9' && q->in_len < 4)
            q->input[q->in_len++] = c, q->input[q->in_len] = 0;
        if (mq_fg_choosing(q)) /* (the keys pick the choice they spell) */
            mq_fg_typed(q);
        q->redraw = true;
        return;
    }
    if (q->scene != SC_QUIZ || q->sub)
        return;
    mq_prob_t *p = &q->prob;
    if (p->kind == K_SHADE) {
        if (c >= '1' && c <= '9' && c - '1' < p->v[1])
            p->v[5] = (int16_t)(p->v[5] ^ (1 << (c - '1'))), p->v[4] = (int16_t)(c - '1');
        q->redraw = true;
        return;
    }
    if (p->kind == K_CHOICE) {
        if (c >= '1' && c <= '4' && c - '1' < p->nchoice) {
            q->sel = (int8_t)(c - '1');
            submit_input(q);
        }
        return;
    }
    if (p->kind == K_LINE)
        return;
    if (c == 'r')
        c = 'R';
    char sym = pad_symbol(p);
    bool ok = (c >= '0' && c <= '9') || (sym && c == sym);
    if (p->kind == K_TIME && c == ':')
        ok = true;
    if (ok && q->in_len < 10) {
        q->input[q->in_len++] = c, q->input[q->in_len] = 0;
        q->msg[0][0] = 0;
    }
    q->redraw = true;
}

static void backspace(mq_t *q)
{
    if (q->scene == SC_NAME) {
        char b[MQ_NAME_MAX + 1];
        const char *s = mq_name(q);
        int n = 0;
        while (s[n] && n < MQ_NAME_MAX)
            b[n] = s[n], n++;
        b[n ? n - 1 : 0] = 0;
        mq_pname_set(q, q->save.cur, b);
    } else if (q->in_len)
        q->input[--q->in_len] = 0;
    else if (q->scene == SC_QUIZ && q->prob.kind == K_SHADE)
        q->prob.v[5] = 0;
    q->redraw = true;
}

static void back(mq_t *q);

/* a new player in a slot (or a reset): the learning engine starts empty too (histories, review
 * boxes, the coach's records) -- never the old player's */
static void learn_reset(mq_t *q, int slot)
{
    uint8_t *b = (uint8_t *)&q->save.learn[slot & 1];
    for (size_t i = 0; i < sizeof q->save.learn[0]; i++)
        b[i] = 0;
    q->save.more[slot & 1].pace.rung_ver = 1; /* a new player starts on the ladders (mq_rung.c) */
    q->save.more[slot & 1].pace.fact_ver = 1; /* and with the facts' schedule of today (mq_facts.c) */
}

static void press(mq_t *q, int id, int arg)
{
    mq_profile_t *p = mq_prof(q);
    switch (id) {
    case B_PROFILE:
        q->save.cur = (uint8_t)arg;
        q->guess_n = q->rapid = 0;
        p = mq_prof(q);
        if (!p->used) {
            for (int i = 0; i < (int)sizeof *p; i++)
                ((uint8_t *)p)[i] = 0;
            mq_home_init(q, arg);
            mq_extra_migrate(q, arg);
            learn_reset(q, arg);
            p->buddy = (uint8_t)(arg ? 4 : 2);
            go(q, SC_NAME);
        } else
            enter_map(q);
        break;
    case B_BUDDY: p->buddy = (uint8_t)arg; q->redraw = true; break;
    case B_KITCHEN: mq_merge_open(q); break;
    case B_LETTER: type_char(q, (char)arg); break;
    case B_DEL: backspace(q); break;
    case B_OK:
        if (q->scene == SC_NAME) {
            { /* a name ending in a space or a mark: without it */
                char b[MQ_NAME_MAX + 1];
                q->api->snprintf(b, sizeof b, "%s", mq_name(q));
                int n = 0;
                while (b[n])
                    n++;
                while (n && (b[n - 1] == ' ' || b[n - 1] == '-' || b[n - 1] == '\''))
                    b[--n] = 0;
                if (!n)
                    q->api->snprintf(b, sizeof b, "Player %d", q->save.cur + 1);
                mq_pname_set(q, q->save.cur, b);
            }
            p->used = 1;
            q->dirty = true;
            mq_save_now(q);
            /* no tutorial: straight into the first adventure, a win and a squishy within minutes */
            q->day_started = true;
            p->days = 1;
            school_default(q); /* (a home page's preset: where school is) */
            mq_home(q)->sessions = 1;
            q->first_run = 1;
            q->world = 0;
            go(q, SC_MAP); /* she always starts in her town (the user): the garden is right there, its friend waving */
        } else if (q->scene == SC_FACTGAME)
            mq_fg_submit(q), q->redraw = true;
        else if (q->scene == SC_QUIZ)
            submit_input(q);
        break;
    case B_WORLD:
        if (over_limit(q)) {
            go(q, SC_BREAK);
            break;
        }
        q->world = (uint8_t)arg;
        mq_visit_set(p, arg);
        p->met |= (uint16_t)(1 << guide(arg));
        q->want_mech1 = 0;
        go(q, SC_WORLD);
        {
            int mech[3], stop[3];
            if (world_choices(q->world, mech, stop) < 2) /* nothing to choose: straight into play */
                world_pick(q, 0);
        }
        break;
    case B_ARCADE:
        if (over_limit(q)) {
            go(q, SC_BREAK);
            break;
        }
        p->met |= 1 << 7;
        go(q, SC_ARCADE);
        break;
    case B_LEVEL:
        q->fixed_sk1 = 0;
        q->level = (uint8_t)arg;
        q->sub = 0;
        go(q, SC_INTRO);
        break;
    case B_WGAME: world_pick(q, arg); break;
    case B_BOSS: {
        q->fixed_sk1 = 0;
        go(q, SC_INTRO);
        q->sub = 1;
        break;
    }
    case B_SHOWME:
        if (q->scene == SC_FEEDBACK || q->scene == SC_OFFER || (q->scene == SC_PLAY && (q->sub == GS_WHY || q->sub == GS_OFFER)))
            start_example(q, q->prob.skill, true, SC_QUIZ);
        else if (q->scene == SC_INTRO)
            start_example(q, mq_levels[q->world][q->level] - 1, false, SC_INTRO);
        else if (q->scene == SC_PLAY && q->sub != GS_PLAY)
            break;
        else
            start_example(q, q->prob.skill, false, SC_QUIZ);
        break;
    case B_PLAY:
        start_level(q, q->world, q->level, q->sub == 1);
        break;
    case B_KEY: type_char(q, (char)arg); break;
    case B_FGOPT: mq_fg_choose(q, arg); q->redraw = true; break;
    case B_CHOICE:
        q->sel = (int8_t)arg;
        submit_input(q);
        break;
    case B_NEXT:
        if (q->scene == SC_FEEDBACK && q->missrun == 2 && mq_coach_fade(q, q->prob.skill) < 2) /* offer the trick */
            go(q, SC_OFFER);
        else
            after_feedback(q);
        break;
    case B_YES: start_example(q, q->prob.skill, true, SC_QUIZ); break;
    case B_NO: after_feedback(q); break;
    case B_CONT:
        if (q->scene == SC_QUIZ && q->sub == 1)
            after_right(q);
        else if (q->scene == SC_EXAMPLE) {
            if (q->ex_fade && q->ex_then_similar && q->ex_step + 2 >= q->ex_n) { /* a completion problem: the last step is
                                                                                   * hers (after misses; else her own) */
                completion_order(q);
                q->in_len = 0, q->input[0] = 0, q->sel = q->prob.kind == K_LINE ? -1 : 0;
                q->tried_wrong = false, q->used_help = true, q->retrying = false;
                q->prob_start = q->now;
                go(q, SC_QUIZ);
            } else if (q->ex_step + 1 < q->ex_n) {
                q->ex_step++;
                q->scene_t = q->anim_t;
                q->redraw = true;
            } else if (q->ex_then_similar) {
                mq_on_example_done(q, q->ex.skill);
                after_feedback(q);
            } else if (q->ex_back == SC_INTRO) {
                start_level(q, q->world, q->level, false);
                mq_on_example_done(q, q->ex.skill); /* after start_level reset the run */
            } else {
                mq_on_example_done(q, q->ex.skill);
                go(q, q->ex_back);
            }
        } else if (q->scene == SC_DONE_LEVEL) {
            if (q->best_combo > mq_home(q)->best_combo)
                mq_home(q)->best_combo = q->best_combo, q->dirty = true;
            if (q->first_run && !q->fun_off) { /* the very first win: a squishy to squish, then her room */
                q->first_run = 0;
                mq_fun_start(q, BEAT_SQUISH, q->new_item ? FR_ROOM : FR_MAP);
                break;
            }
            if (!p->complete && mq_quest_complete(p)) {
                p->complete = 1;
                q->dirty = true;
                mq_spark_burst(q, 160, MQ_Y0 + 80, 24);
                go(q, SC_COMPLETE);
            } else if (over_limit(q))
                go(q, SC_BREAK);
            else if (!mq_fun_maybe(q, q->boss ? FUN_BOSS : FUN_RUN_END, FR_WORLD))
                go(q, SC_WORLD);
        } else if (q->scene == SC_FACTGAME) {
            mq_save_now(q);
            go(q, over_limit(q) ? SC_BREAK : SC_ARCADE);
        } else if (q->scene == SC_SURPRISE)
            go(q, SC_MAP);
        else
            go(q, SC_MAP);
        break;
    case B_STAY: /* the done screen: another round of the same game, here (no fun beat in between: she chose to play) */
        if (q->scene != SC_DONE_LEVEL)
            break;
        if (q->best_combo > mq_home(q)->best_combo)
            mq_home(q)->best_combo = q->best_combo, q->dirty = true;
        mq_save_now(q);
        if (!p->complete && mq_quest_complete(p)) { /* (everything done: that party first) */
            p->complete = 1;
            q->dirty = true;
            mq_spark_burst(q, 160, MQ_Y0 + 80, 24);
            go(q, SC_COMPLETE);
        } else if (over_limit(q)) /* (the grown-ups' time is up: the break, as on any way on) */
            go(q, SC_BREAK);
        else {
            bool boss = q->boss;
            q->fixed_sk1 = 0;
            q->want_mech1 = (uint8_t)(!boss && q->gm.round_mech < ME_COUNT ? q->gm.round_mech + 1 : 0); /* the same game */
            q->sub = boss;
            start_level(q, q->world, q->level, boss);
            q->want_mech1 = 0;
        }
        break;
    case B_GAME:
        if (arg >= 10) { /* the action games */
            mq_action_start(q, arg - 10);
            break;
        }
        q->best_combo = 0;
        mq_fg_start(q, arg);
        go(q, SC_FACTGAME);
        break;
    case B_GIFT:
        if (!q->surprise)
            mq_open_gift(q);
        q->redraw = true;
        break;
    case B_PARENT: break;
    case B_GOHOME: mq_save_now(q); mq_home_enter(q, SC_ROOM); break;
    case B_QUEST: {
        int have, need;
        const char *t = mq_quest_text(q, arg % 3, &have, &need);
        q->api->snprintf(q->tmp, sizeof q->tmp, "%s: %d of %d", t, have, need);
        q->api->toast(q->tmp);
        break;
    }
    case B_TIPS:
        if (!mq_meta_collect(q))
            mq_toast(q, "The tip jar fills as you play!");
        break;
    case B_LOG: {
        char path[40];
        int r = mq_coach_export(q, path, sizeof path);
        if (r) {
            char m[64];
            q->api->card_error(r, m, sizeof m);
            q->api->toast(m);
        } else
            q->api->toast(path);
        break;
    }
    case B_LINE:
    case B_PIE: break; /* handled with the tap position */
    case B_GAMEGO: mq_game_key(q, K_ENTER, 0); break;
    case B_TOY:
        if (q->scene == SC_TOYS)
            mq_toys_press(q, arg);
        else
            mq_toys_open(q, 0);
        break; /* land!, fling!, serve!, set!, ring!, dig!, open! */
    case B_RECENT:
        q->parent_recent ^= 1;
        q->nbtn = 0, q->focus = 0;
        q->redraw = true;
        break;
    case B_STRATS: /* the ways she has met (arg 1: their next page) */
        if (arg)
            mq_strat_page(q);
        else
            q->parent_pat = q->parent_pat == 2 ? 0 : 2;
        q->nbtn = 0, q->focus = 0;
        q->redraw = true;
        break;
    case B_PATTERNS:
        q->parent_pat = q->parent_pat == 1 ? 0 : 1;
        q->nbtn = 0, q->focus = 0;
        q->redraw = true;
        break;
    case B_PREVIEW: { /* grown-ups: the question screen of a skill, as it would be asked */

        int sk = arg < MQ_NSKILL ? arg : 0, lv, w = skill_world_level(sk, &lv);
        q->preview = true;
        q->fixed_sk1 = (uint8_t)(sk + 1);
        start_level(q, w, lv, false);
        new_problem(q, sk, p->sk[sk].diff);
        break;
    }
    case B_DIVT: q->parent_div = !q->parent_div; q->redraw = true; break;
    case B_LIMIT: p->limit_min = limits[arg % 7]; q->dirty = true, q->redraw = true; break;
    case B_TOGGLE12: p->facts12 = !p->facts12; q->dirty = true, q->redraw = true; break;
    case B_FUNPCT: p->pad[0] = (uint8_t)(arg + 1); q->dirty = true, q->redraw = true; break;
    case B_HOMEMIN: mq_home(q)->home_min = (uint8_t)(arg ? arg : MQ_HOME_OFF); q->dirty = true, q->redraw = true; break;
    case B_FOCUSWEEK:
        mq_pace(q)->focus_end = mq_pace(q)->focus_end ? 0 : (uint16_t)(p->days + 6);
        q->dirty = true, q->redraw = true;
        break;
    case B_FOCUSSHARE: {
        mq_pace_t *pc = mq_pace(q);
        int s = pc->focus_share >= 20 ? pc->focus_share : 50;
        pc->focus_share = (uint8_t)(s >= 60 ? 40 : s + 10);
        q->dirty = true, q->redraw = true;
        break;
    }
    case B_FOCUSCLR: {
        char a0[] = "mq", a1[] = "focus", a2[] = "clear";
        char *av[3] = {a0, a1, a2};
        mq_focus_console(q, 3, av);
        q->redraw = true;
        break;
    }
    case B_FOCUSSKILL: { /* the skills school often sends home, in turn (then none) */
        static const uint8_t opts[] = {S_ADDSUB, S_ADDSUBBIG, S_TIME1, S_ELAPSED, S_COINS, S_CHANGE, S_UNITFRAC,
                                       S_FRACLINE, S_EQUIV, S_ROUND10, S_PV3, S_AREAMUL, S_PERIM, S_MULTI, S_DIVIDE};
        mq_pace_t *pc = mq_pace(q);
        int cur = -1;
        for (size_t i = 0; i < sizeof opts; i++)
            if (pc->focus_skill[0] == opts[i] + 1)
                cur = (int)i;
        pc->focus_skill[0] = cur + 1 < (int)sizeof opts ? (uint8_t)(opts[cur + 1] + 1) : 0;
        q->dirty = true, q->redraw = true;
        break;
    }
    case B_GRADE: mq_set_start_grade(q, mq_start_grade(q) >= 5 ? 2 : mq_start_grade(q) + 1); q->redraw = true; break;
    case B_SPEECH: mq_pace(q)->voice = (uint8_t)((mq_pace(q)->voice + 1) % 3); q->dirty = true, q->redraw = true; break;
    case B_TWISTS: mq_pace(q)->twists_off ^= 1; q->dirty = true, q->redraw = true; break;
    case B_READSPEED: mq_pace(q)->read_speed = (uint8_t)((mq_pace(q)->read_speed + 1) % 4); q->dirty = true, q->redraw = true; break;
    case B_SCHOOLG: { /* not said, 3, 4, 5, not said (a new grade: its week kept) */
        int wk, g = mq_school(q, &wk);
        mq_set_school(q, g == 0 ? 3 : g >= 5 ? 0 : g + 1, wk);
        q->redraw = true;
        break;
    }
    case B_SCHOOLW: {
        int wk, g = mq_school(q, &wk);
        mq_set_school(q, g, wk + (arg ? 1 : -1));
        q->redraw = true;
        break;
    }
    case B_THEME: {
        const char *f = arg ? q->theme_files[arg - 1] : "";
        q->theme_err = (int8_t)mq_theme_load(q, f);
        if (!q->theme_err)
            q->api->snprintf(q->save.theme, sizeof q->save.theme, "%s", f);
        q->dirty = true, q->redraw = true;
        break;
    }
    case B_RESET:
        if (!q->reset_arm) {
            q->reset_arm = true;
            q->redraw = true;
        } else {
            uint8_t *b = (uint8_t *)p;
            for (size_t i = 0; i < sizeof *p; i++)
                b[i] = 0;
            mq_home_init(q, q->save.cur);
            mq_extra_migrate(q, q->save.cur);
            learn_reset(q, q->save.cur);
            q->dirty = true;
            mq_save_now(q);
            go(q, SC_TITLE);
        }
        break;
    default:
        if (id == HB_FUNTAP || id == HB_MAKER || id == HB_HOOK)
            mq_fun_press(q, id, arg, q->tap_x, q->tap_y);
        else if (id == HB_ACTION) {
            if (arg < ACT_COUNT)
                mq_action_start(q, arg);
            else
                go(q, SC_ARCADE);
        }
        else if (id >= HB_FIRST && id < HB_LAST)
            mq_home_press(q, id, arg);
        break;
    }
}

static void back(mq_t *q)
{
    switch (q->scene) {
    case SC_CHAPTER: go(q, SC_MAP); break; /* (the step is told again next time) */
    case SC_MERGE: /* a picked piece is let go first; then back to town */
        if (mq_merge_back(q))
            break;
        mq_save_now(q);
        go(q, SC_MAP);
        break;
    case SC_TITLE: break; /* (CLEAR stays: the exit soft key or HOME leaves) */
    case SC_NAME: go(q, SC_TITLE); break;
    case SC_MAP: go(q, SC_TITLE); break;
    case SC_SHOP:
        if (mq_pay_back(q)) /* paying: the coins go back to her purse, then the counter closes */
            break;
        if (q->shop_pick) {
            q->shop_pick = 0;
            q->redraw = true;
            break;
        } /* fall through */
    case SC_WARDROBE:
        if (mq_pay_back(q)) /* paying for clothes: as in the shop */
            break;
        if (q->shop_pick) {
            q->shop_pick = 0;
            q->redraw = true;
            break;
        } /* fall through */
    case SC_SHOWOFF:
        mq_save_now(q);
        mq_home_enter(q, SC_ROOM);
        break;
    case SC_WORLD: case SC_ARCADE: case SC_ROOM: case SC_STICKERS: case SC_BREAK: case SC_COMPLETE:
    case SC_SURPRISE: case SC_BOOK:
        mq_save_now(q);
        go(q, SC_MAP);
        break;
    case SC_PARENT:
        mq_save_now(q);
        go(q, mq_prof(q)->used ? SC_MAP : SC_TITLE);
        break;
    case SC_QUIZ: case SC_FEEDBACK: case SC_OFFER:
        if (q->preview && q->now - q->gate.adult_t < 300000u) { /* the grown-ups' preview: back to their screen (a
                                                                   * grown-up's input in the last 5 minutes; else the gate again) */
            q->preview = false;
            q->fixed_sk1 = 0;
            open_parent(q);
            break;
        } /* fall through */
    case SC_INTRO: case SC_PLAY: case SC_DONE_LEVEL:
        q->preview = false;
        mq_save_now(q);
        go(q, SC_WORLD);
        break;
    case SC_EXAMPLE:
        if (q->ex_back == SC_QUIZ)
            go(q, SC_QUIZ);
        else
            go(q, SC_INTRO);
        break;
    case SC_FACTGAME: case SC_ACTION: mq_save_now(q); go(q, SC_ARCADE); break;
    case SC_TOYS:
        mq_save_now(q);
        if (q->toy.jam_until) /* the jam as a fun beat: on with the story */
            q->toy.jam_until = 0, q->beat = 0xFF, mq_fun_resume(q, q->toy.jam_ret);
        else if (q->toy.mode)
            mq_toys_open(q, 0);
        else
            go(q, SC_ARCADE);
        break;
    default: go(q, SC_MAP); break;
    }
}

static void softkey(mq_t *q, int i)
{
    switch (q->scene) {
    case SC_TITLE:
    case SC_MAP:
        if (q->scene == SC_TITLE && i == 3) { /* exit: back to the calculator */
            q->api->leave();
            return;
        }
        if (i == 4) { /* the grown-ups' gate (again: it goes) */
            if (q->gate.shown)
                mq_gate_close(q);
            else
                mq_gate_open(q);
            return;
        }
        if (q->scene == SC_MAP) {
            if (i == 0)
                mq_home_enter(q, SC_ROOM);
            else if (i == 1)
                mq_home_enter(q, SC_BOOK);
            else if (i == 2)
                press(q, B_ARCADE, 0);
            else if (i == 3)
                go(q, SC_TITLE);
        }
        return;
    case SC_PLAY:
        if (i == 0 || i == 2) /* (over the acted story or a teaching moment: those go first) */
            mq_game_unblock(q);
        if (i == 0 && q->sub == GS_PLAY) /* the lightbulb: the situation again, acted (never the answer) */
            mq_bulb_start(q);
        else if (i == 2) /* what do I do? */
            mq_guide_help(q);
        else if (i == 1)
            mq_game_key(q, 2, 0); /* a power-up */
        else if (i == 4)
            back(q);
        return;
    case SC_QUIZ:
        if (i == 0 && q->sub == 0)
            start_example(q, q->prob.skill, false, SC_QUIZ);
        else if (i == 4)
            back(q);
        return;
    case SC_PARENT:
        if (i < 4) {
            q->tab = (uint8_t)i;
            q->nbtn = 0, q->focus = 0;
            if (i == 3)
                mq_theme_scan(q);
            q->redraw = true;
        } else
            back(q);
        return;
    case SC_ROOM: case SC_SHOP: case SC_BOOK: case SC_SHOWOFF: case SC_WARDROBE:
        if (i == 4)
            back(q);
        else
            mq_home_softkey(q, i);
        return;
    case SC_MERGE:
        if (i == 2)
            mq_merge_help(q);
        else if (i == 4)
            back(q);
        return;
    default:
        if (i == 4)
            back(q);
        return;
    }
}

/* the number line / pie under a tap */
static void tap_model(mq_t *q, int id, int x, int y)
{
    mq_prob_t *p = &q->prob;
    if (id == B_FOCUSGRID) { /* the grown-ups' fact grid: a fact, or a row's table, in focus */
        const mq_btn_t *b = NULL;
        for (int i = 0; i < q->nbtn; i++)
            if (q->btn[i].id == B_FOCUSGRID)
                b = &q->btn[i];
        if (!b)
            return;
        int row = (y - b->y) / 12, col = (x - 30) / 12;
        if (row < 0 || row > 12)
            return;
        mq_pace_t *pc = mq_pace(q);
        if (col < 0) { /* the row's number: the whole table */
            if (q->parent_div && row >= 1)
                pc->focus_div ^= (uint16_t)(1u << row);
            else if (!q->parent_div)
                pc->focus_mul ^= (uint16_t)(1u << row);
        } else if (col <= 12 && !(q->parent_div && col == 0))
            mq_focus_toggle(q, mq_fact_index(row, col, q->parent_div));
        q->dirty = true, q->redraw = true;
        return;
    }
    if (id == B_LINE) {
        int ticks = p->v[1] * p->v[2], lx = 16, lw = 192 - 20;
        int t = ((x - lx) * ticks + lw / 2) / lw;
        q->sel = (int8_t)(t < 0 ? 0 : t > ticks ? ticks : t);
    } else {
        int cx = 6 + 96, cy = MQ_Y0 + 68 + 41, d = p->v[1] > 0 ? p->v[1] : 1, best = 0, bd = -0x40000000;
        for (int s = 0; s < d; s++) { /* the slice whose middle direction is nearest */
            int a = s * 360 / d + 180 / d;
            int dot = (x - cx) * mq_isin(a) - (y - cy) * mq_icos(a);
            if (dot > bd)
                bd = dot, best = s;
        }
        p->v[5] = (int16_t)(p->v[5] ^ (1 << best));
        p->v[4] = (int16_t)best;
    }
    q->redraw = true;
}

/* Swipes (MODX_EV_SWIPE, dir 0 left, 1 right, 2 up, 3 down): turn pages and places, wave
 * celebrations away, serve in the arcade. Keys do all of it too. */
/* A scroll wheel (the Mac's mouse wheel or trackpad, MODX_EV_SCROLL): only what scrolls or adjusts under the
 * pointer takes it -- the game's wheels, sliders, lines and clock hands, the grown-ups' list, the shop's pages.
 * Nowhere else does it do anything: never arrows, never a swipe, never the town's walker, never "back"
 * (the playtest: "the mouse scroll button does weird stuff"). */
static void swipe(mq_t *q, int dir, int x0, int y0);
static void scroll(mq_t *q, int dy, int x, int y)
{
    q->last_input = q->now;
    if (q->gate.shown) /* (nothing scrolls at the gate, nothing under it) */
        return;
    if (mq_game_scroll(q, dy, x, y)) {
        q->redraw = true;
        return;
    }
    switch (q->scene) {
    case SC_PLAY: return;
    case SC_PARENT:
        if (q->tab == 0) {
            int v = q->parent_scroll + dy * 2;
            q->parent_scroll = (uint8_t)(v < 0 ? 0 : v > MQ_NSTD - 8 ? MQ_NSTD - 8 : v);
        }
        break;
    case SC_SHOP: /* the shop's pages (not while an item is picked) */
        if (q->shop_pick)
            return;
        mq_home_press(q, HB_PAGE, dy > 0);
        break;
    default: return;
    }
    q->redraw = true;
}
static void swipe(mq_t *q, int dir, int x0, int y0)
{
    q->last_input = q->now;
    if (q->eat_swipe) { /* the flick of a press that began on another screen (touch) */
        q->eat_swipe = 0;
        return;
    }
    if (q->gate.shown) /* (the gate's pad takes taps; a flick does nothing there or under it) */
        return;
    if (mq_home_swipe(q, dir, x0, y0) || mq_game_swipe(q, dir, x0, y0) || mq_town_swipe(q, dir))
        return;
    switch (q->scene) {
    case SC_WORLD: /* the next or previous open world */
        if (dir <= 1) {
            mq_profile_t *p = mq_prof(q);
            for (int k = 1; k < MQ_NWORLD; k++) {
                int w = (q->world + (dir == 0 ? k : MQ_NWORLD - k)) % MQ_NWORLD;
                if (world_open(p, w)) {
                    press(q, B_WORLD, w);
                    break;
                }
            }
        }
        break;
    case SC_MAP: /* nothing: a swipe along the street only looks along it (the user: walking past buildings went
                  * into them -- a sideways swipe went into the home, an upward one into the arcade) */
        break;
    case SC_QUIZ:
        if (q->sub == 1) /* the cheer after a right answer: on to the next */
            after_right(q);
        break;
    case SC_PLAY:
        if (q->sub == GS_CHEER) /* the cheer: swiped on */
            after_right(q);
        break;
    case SC_DONE_LEVEL: case SC_COMPLETE: case SC_BREAK: /* celebrations: swiped away */
        if (q->scene == SC_DONE_LEVEL && q->anim_t - q->scene_t < DONE_GUARD_MS) /* (not her game's last flick) */
            break;
        press(q, B_CONT, 0);
        break;
    case SC_SURPRISE:
        if (q->surprise)
            press(q, B_CONT, 0);
        break;
    case SC_EXAMPLE: /* the trick: left for the next step */
        if (dir == 0)
            press(q, B_CONT, 0);
        break;
    case SC_FACTGAME: /* serve the order / dash ahead: the same as ok */
        if (dir == 1 || dir == 2) {
            if (mq_fg_finished(q))
                press(q, B_CONT, 0);
            else
                press(q, B_OK, 0);
        }
        break;
    default: break;
    }
    q->redraw = true;
}

static void key(mq_t *q, int k, int mod)
{
    q->last_input = q->now;
    if (q->eat_soft) { /* the soft key of a press that began on another screen (touch): not hers */
        q->eat_soft = 0;
        if (k >= K_F1 && k <= K_F5)
            return;
    }
    if (q->scene == SC_PARENT || q->preview)
        q->gate.adult_t = q->now;
    if (q->gate.shown && k >= K_F1 && k < K_F5) /* another soft key: the gate goes, the key does its job */
        mq_gate_close(q);
    if (q->gate.shown && k != K_F5) { /* the gate: the answer typed (digits, (-), DEL, ENTER); CLEAR closes */
        gate_result(q, mq_gate_key(q, k, q->api->key_char ? q->api->key_char(k, mod) : 0));
        return;
    }
    if (k >= K_F1 && k <= K_F5) {
        softkey(q, k - K_F1);
        return;
    }
    if (k == K_CLEAR) {
        back(q);
        return;
    }
    if (q->scene == SC_PARENT && q->tab == 1) { /* the fact grid: a digit puts its table in focus (0: 10) */
        int c = q->api->key_char ? q->api->key_char(k, mod) : 0;
        if (c >= '0' && c <= '9') {
            int n = c == '0' ? 10 : c - '0';
            mq_pace_t *pc = mq_pace(q);
            if (q->parent_div)
                pc->focus_div ^= (uint16_t)(1u << n);
            else
                pc->focus_mul ^= (uint16_t)(1u << n);
            q->dirty = true, q->redraw = true;
            return;
        }
    }
    if (q->scene == SC_DONE_LEVEL) { /* a round won: its ways on are widgets (the arrows choose, ENTER goes) */
        done_key(q, k);
        return;
    }
    if (q->scene == SC_CHAPTER && (k == K_ENTER || k == K_RIGHT || k == K_DOWN)) { /* a friend's story: on (only ENTER
                                                                                * ends it: an arrow never leaves) */
        if (k == K_ENTER || !mq_chapter_last(q))
            mq_chapter_next(q);
        q->redraw = true;
        return;
    }
    if (mq_merge_key(q, k, q->api->key_char(k, mod))) {
        q->redraw = true;
        return;
    }
    if (mq_game_key(q, k, q->api->key_char(k, mod)) || mq_toys_key(q, k, q->api->key_char(k, mod)) ||
        mq_action_key(q, k, q->api->key_char(k, mod)) || mq_fun_key(q, k, q->api->key_char(k, mod)) ||
        mq_home_key(q, k) || mq_town_key(q, k))
        return;
    if (q->scene == SC_QUIZ && q->sub == 1 && (k == K_ENTER)) {
        after_right(q);
        return;
    }
    if (q->scene == SC_QUIZ && q->sub == 0 && q->prob.kind == K_LINE && (k == K_LEFT || k == K_RIGHT)) {
        int ticks = q->prob.v[1] * q->prob.v[2];
        int s = q->sel < 0 ? 0 : q->sel + (k == K_RIGHT ? 1 : -1);
        q->sel = (int8_t)(s < 0 ? 0 : s > ticks ? ticks : s);
        q->redraw = true;
        return;
    }
    if (q->scene == SC_PARENT && q->tab == 0 && (k == K_UP || k == K_DOWN)) {
        int s = q->parent_scroll + (k == K_DOWN ? 4 : -4);
        q->parent_scroll = (uint8_t)(s < 0 ? 0 : s > MQ_NSTD - 8 ? MQ_NSTD - 8 : s);
        q->redraw = true;
        return;
    }
    if (k == K_LEFT || k == K_UP || k == K_RIGHT || k == K_DOWN) {
        q->kbd = true;
        if (q->nbtn) {
            int f = q->focus + ((k == K_RIGHT || k == K_DOWN) ? 1 : -1);
            q->focus = (uint8_t)((f + q->nbtn) % q->nbtn);
        }
        q->redraw = true;
        return;
    }
    if (k == K_ENTER) {
        bool typing = (q->scene == SC_QUIZ && q->prob.kind != K_CHOICE) || q->scene == SC_FACTGAME ||
                      (q->scene == SC_NAME && !q->kbd);
        if (typing && !(q->kbd && q->focus < q->nbtn && q->btn[q->focus].id != B_KEY)) {
            press(q, B_OK, 0);
            return;
        }
        bool home = q->scene == SC_WARDROBE || q->scene == SC_SHOWOFF || q->scene == SC_ROOM || q->scene == SC_SHOP ||
                    q->scene == SC_BOOK; /* (there ENTER presses only the button her arrows put the ring on: never an
                                         * unseen first one that leaves -- the bot's 18) */
        if (q->focus < q->nbtn && (q->kbd || !home)) {
            q->pressed = (int8_t)q->focus, q->pressed_t = q->now;
            press(q, q->btn[q->focus].id, q->btn[q->focus].arg);
        } else if (q->scene == SC_EXAMPLE || q->scene == SC_DONE_LEVEL)
            press(q, B_CONT, 0);
        return;
    }
    if (k == K_DEL) {
        backspace(q);
        return;
    }
    int c = q->api->key_char(k, mod);
    if (k == K_DIV)
        c = '/';
    else if (k == K_DOT)
        c = '.';
    else if (k == K_COMMA)
        c = ',';
    if (c)
        type_char(q, (char)c);
}

/* the pinch's scale (x 256) from the core (modx API size 5); an older core: a fixed squeeze */
static int pinch_scale(mq_t *q, int type)
{
    if (type != 5 && type != 6)
        return 256;
    if (q->api->size > offsetof(modx_api_t, touch_scale) && q->api->touch_scale)
        return q->api->touch_scale();
    return 200;
}

static void touch_in(mq_t *q, int arg)
{
    int type = (int)MODX_TOUCH_TYPE(arg), x = MODX_TOUCH_X(arg), y = MODX_TOUCH_Y(arg);
    q->last_input = q->now;
    if (type == 1 || type == 2) /* a finger down (the home story never moves her then) */
        q->finger = 1;
    else if (type == 3 || type == 4 || type == 7)
        q->finger = 0;
    if (q->scene == SC_PARENT || q->preview)
        q->gate.adult_t = q->now;
    if (y >= MQ_Y0 + MQ_H) /* the soft-key bar: its taps are the soft keys' (the core presses them) */
        return;
    q->tap_x = (int16_t)x, q->tap_y = (int16_t)y;
    if (q->gate.shown) { /* the gate takes every touch: its pad, "close" */
        gate_result(q, mq_gate_touch(q, type, x, y));
        return;
    }
    if (toast_touch(q, type, x, y)) { /* a tap on the toast puts it away (and nothing under it) */
        q->redraw = true;
        return;
    }
    if (q->scene == SC_CHAPTER) { /* a friend's story: a tap goes on */
        if (type == 4)
            mq_chapter_next(q);
        q->redraw = true;
        return;
    }
    if (q->scene == SC_DONE_LEVEL) { /* a round won: its ways on are widgets */
        done_touch(q, type, x, y);
        return;
    }
    if (mq_merge_touch(q, type, x, y)) {
        q->redraw = true;
        return;
    }
    if (q->scene == SC_PLAY && q->sub == GS_OFFER && type == 4) /* the offer's buttons: below */
        ;
    else if (mq_game_touch(q, type, x, y) || mq_toys_touch(q, type, x, y) || mq_action_touch(q, type, x, y) ||
        mq_fun_touch(q, type, x, y, pinch_scale(q, type))) { /* games, squishies */
        q->kbd = false;
        return;
    }
    if (mq_home_touch(q, type, x, y)) { /* the room: drag and drop, selecting */
        q->kbd = false;
        return;
    }
    if (mq_town_touch(q, type, x, y)) { /* the town: walk, look along the street */
        q->kbd = false;
        return;
    }
    if (type == 1 /* TEV_DOWN */) {
        q->pdrag_y = (int16_t)y, q->pdrag_s = q->parent_scroll; /* (a drag on the standards starts here) */
        return;
    }
    if (type == 3 /* TEV_UP */ || type != 4 /* TEV_TAP */) {
        if (type == 2 && q->scene == SC_PARENT && q->tab == 0) { /* a drag scrolls the standards: a row (13 px) a row,
                                                                  * the list following the finger (CR2-10) */
            int v = q->pdrag_s + (q->pdrag_y - y) / 13;
            q->parent_scroll = (uint8_t)(v < 0 ? 0 : v > MQ_NSTD - 8 ? MQ_NSTD - 8 : v);
            q->redraw = true;
        }
        return;
    }
    q->kbd = false;
    for (int i = q->nbtn - 1; i >= 0; i--) {
        const mq_btn_t *b = &q->btn[i];
        if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h) {
            q->focus = (uint8_t)i;
            q->pressed = (int8_t)i, q->pressed_t = q->now;
            if (b->id == B_LINE || b->id == B_PIE || b->id == B_FOCUSGRID)
                tap_model(q, b->id, x, y);
            else
                press(q, b->id, b->arg);
            q->redraw = true;
            return;
        }
    }
    if (q->scene == SC_EXAMPLE || q->scene == SC_DONE_LEVEL)
        press(q, B_CONT, 0);
}

/* One press, one act (the user: "sometimes it flips through a screen and then jumps to another without giving me a
 * chance to do the first question"). A finger that lands on one screen and lifts on another -- the landing itself
 * moved on (a pick as it lands, the last bubble of a beat, a pie slice), or a timer did while it was down (the
 * cheer, a beat's end, a friend's line) -- does nothing on the new one: its release is not that screen's "next",
 * "go on", "skip" or an answer, its drags move nothing there, a soft key under it is not pressed. The screen is
 * what she is looking at: the scene, the game's moment, the order, an overlay over it (a teaching moment and its
 * part, a friend's trick, the notebook, the acted story, the lightbulb), a beat, a friend's line, an action game's
 * end. A press that starts and ends on one screen is untouched (quick taps), and so is a tap with no DOWN before
 * it (tests, the console). */
static uint32_t screen_sig(mq_t *q)
{
    uint32_t h = 2166136261u;
#define SIG_MIX(v) (h = (h ^ (uint32_t)(v)) * 16777619u)
    SIG_MIX(q->scene);
    SIG_MIX(q->sub);
    SIG_MIX(q->run_i);
    for (const char *s = q->prob.text[0]; *s; s++)
        SIG_MIX((uint8_t)*s);
    SIG_MIX(q->prob.ans);
    SIG_MIX(mq_teach_on(q) ? 1 + q->teach.seg * 8 + q->teach.phase : 0);
    SIG_MIX(mq_strat_on(q));
    SIG_MIX(mq_cols_on(q));
    SIG_MIX(mq_diag_on(q));
    SIG_MIX(q->gm.scene_on);
    SIG_MIX(q->gm.bulb_t != 0);
    SIG_MIX(q->scene == SC_CHAPTER ? q->chap_step * 64 + q->chap_line : 0);
    SIG_MIX(q->scene == SC_FUN ? q->beat : 0);
    SIG_MIX(q->scene == SC_ACTION ? q->act.done : 0);
#undef SIG_MIX
    return h;
}

/* A press that lands within MQ_SETTLE_MS of the moment her own input changed the screen is the second half of a
 * double tap ("go on" twice on the cheer, a friend's line, a teaching card) and is not for the new screen either:
 * nobody reads an order and answers it in a fifth of a second (the bot: the second tap of a double tap on the cheer
 * answered the next order 160 ms after it appeared). Screens that change by themselves are not held back. */
#define MQ_SETTLE_MS 200u
/* (the screens with an order, its moments, a teaching card or a friend's lines: elsewhere -- the town, the room,
 * the shop, the toy box -- a quick second tap is only a quick tap) */
static bool settle_scene(int s)
{
    return s == SC_INTRO || s == SC_PLAY || s == SC_FEEDBACK || s == SC_EXAMPLE || s == SC_DONE_LEVEL ||
           s == SC_FACTGAME || s == SC_QUIZ || s == SC_CHAPTER;
}

static void touch(mq_t *q, int arg)
{
    int type = (int)MODX_TOUCH_TYPE(arg), x = MODX_TOUCH_X(arg), y = MODX_TOUCH_Y(arg);
    uint32_t before = screen_sig(q);
    if (type == 1) { /* a finger lands: the screen it is for */
        q->press_sig = before, q->press_on = 1, q->eat_soft = 0, q->eat_swipe = 0;
        q->press_x = (int16_t)x, q->press_y = (int16_t)y;
        if (q->in_change_t && (int32_t)(q->now - q->in_change_t) < (int32_t)MQ_SETTLE_MS && before == q->in_change_sig &&
            y < MQ_Y0 + MQ_H && settle_scene(q->scene)) {
            q->press_on = 2; /* (the second tap of a double tap, on the screen the first one brought: nobody's) */
            q->last_input = q->now;
            return;
        }
    } else if (type == 2 || type == 3 || type == 4 || type == 7) {
        if (type == 4 && q->press_on && (x != q->press_x || y != q->press_y))
            q->press_on = 0; /* (a tap is where its finger landed: this one is no release of that press -- a test's) */
        if (q->press_on == 1 && before != q->press_sig) { /* the screen changed under it: the press is over */
            q->press_on = 2;
            q->wg.press = -1, q->wg.carry = -1, q->wg.hover = -1, q->wg.downpick = false;
            q->last_input = q->now;
            touch_in(q, MODX_TOUCH(7, x, y)); /* (a cancel: whoever held something lets go of it) */
        }
        if (q->press_on == 2) {
            if (type != 2)
                q->press_on = 0;
            if (type == 4 && y >= MQ_Y0 + MQ_H)
                q->eat_soft = 1; /* (the soft key the core presses for this tap next) */
            if (type == 3 && (x - q->press_x) * (x - q->press_x) + (y - q->press_y) * (y - q->press_y) > 144)
                q->eat_swipe = 1; /* (the flick the core may make of this drag next) */
            return;
        }
        if (type != 2)
            q->press_on = 0;
    }
    touch_in(q, arg);
    uint32_t after = screen_sig(q);
    if (type != 2 && after != before)
        q->in_change_t = mq_mark_now(q), q->in_change_sig = after;
}

static void toast_tick(mq_t *q);
/* ------------------------------------------------------------------ frames */
static void frame(mq_t *q, uint32_t dt)
{
    q->anim_t += dt;
    q->eat_swipe = 0; /* (the core's flick follows its release at once, never a frame later) */
    sparks_step(q);
    if (q->scene == SC_FACTGAME)
        mq_fg_tick(q, dt);
    mq_home_frame(q, dt);
    mq_fun_frame(q, dt);
    mq_hstory_frame(q, dt);
    mq_action_frame(q);
    mq_game_frame(q);
    mq_toys_frame(q);
    mq_merge_frame(q);
    mq_meta_frame(q);
    mq_town_frame(q);
    toast_tick(q);
    /* time played: only while someone is playing (input in the last minute) */
    if (q->now - q->last_input < 60000u && q->scene != SC_TITLE && q->scene != SC_PARENT) {
        q->sec_acc += dt;
        while (q->sec_acc >= 1000) {
            q->sec_acc -= 1000;
            mq_profile_t *p = mq_prof(q);
            if (p->used) {
                mq_meta_second(q); /* the cafe's tips grow with play time */
                p->secs++;
                if (p->day_secs < 65535)
                    p->day_secs++;
            }
        }
    }
    /* the grown-ups' gate (its pause, nobody there); the grown-ups' screen left alone 5 minutes: back to her town */
    mq_gate_frame(q);
    if (q->scene == SC_PARENT && q->now - q->gate.adult_t > 300000u) {
        mq_save_now(q);
        go(q, mq_prof(q)->used ? SC_MAP : SC_TITLE);
    }
    /* the celebration of a right answer moves on by itself */
    if (q->scene == SC_QUIZ && q->sub == 1 && q->now - q->pressed_t > (uint32_t)mq_read_ms(q, q->praise)) /* (its reading time) */
        after_right(q);
}

static bool animating(mq_t *q)
{
    if (sparks_alive(q) || q->gate.shown || mq_fun_animating(q) || mq_juice_animating(q))
        return true;
    switch (q->scene) {
    case SC_FACTGAME: case SC_ACTION: case SC_PLAY: case SC_TOYS: case SC_MAP: case SC_CHAPTER: case SC_MERGE:
    case SC_TITLE: /* (the parade) */
        return true;
    case SC_ROOM: case SC_SHOP: case SC_BOOK: case SC_SHOWOFF: case SC_WARDROBE: return mq_home_animating(q);
    case SC_QUIZ: case SC_FEEDBACK: case SC_EXAMPLE:
        return q->anim_t - q->scene_t < 600 || (q->anim_t / 125) % 4 == 0 || q->sub;
    default: return (q->anim_t / 125) % 4 == 0; /* the cast hops a few times a second */
    }
}

/* q->now follows the clock but never goes back: `mq step` runs ahead of it (tests) */
static void clock_to(mq_t *q, uint32_t t)
{
    if ((int32_t)(t - q->now) > 0 || !q->now)
        q->now = t;
}

static void op_tick(void *rw, uint32_t now, int scr)
{
    mq_t *q = rw;
    clock_to(q, now);
    if (!q->last_frame)
        q->last_frame = now;
    uint32_t el = now - q->last_frame;
    if (el < MQ_FRAME_MS)
        return;
    if (el > 4 * MQ_FRAME_MS)
        el = 4 * MQ_FRAME_MS;
    q->last_frame = now;
    frame(q, el);
    if (q->redraw || animating(q)) {
        const modx_api_t *api = q->api;
        uint32_t c0 = api->cycles(), cpu = api->cycles_per_us();
        q->px_acc = 0;
        q->redraw = false;
        draw_scene(q);
        q->frame_px = q->px_acc;
        q->api->flush();
        q->frame_us = cpu ? (api->cycles() - c0) / cpu : 0; /* drawing and the flush (state mq_frame_us) */
    }
    if (q->dirty && (q->anim_t / 1000) % 2 == 0)
        mq_save_now(q);
    (void)scr;
}

static bool op_draw(void *rw, int scr, int rect[4])
{
    mq_t *q = rw;
    clock_to(q, q->api->millis());
    q->toy.kal_ok = 0, q->gm.hud_ok = 0; /* the whole screen: everything again */
    draw_scene(q);
    rect[0] = 0, rect[1] = MQ_Y0, rect[2] = MQ_W, rect[3] = MQ_H;
    (void)scr;
    return true;
}

static int op_event(void *rw, int ev, int arg)
{
    mq_t *q = rw;
    clock_to(q, q->api->millis());
    switch (ev) {
    case MODX_EV_ENTER:
        if (!arg) {
            q->last_input = q->now;
            go(q, SC_TITLE);
        } else
            set_softkeys(q);
        return 0;
    case MODX_EV_KEY: q->user_t = q->now; key(q, arg & 0xFF, arg >> 8); break;
    case MODX_EV_TOUCH: q->user_t = q->now; touch(q, arg); break;
    case MODX_EV_SWIPE: q->user_t = q->now; swipe(q, (int)MODX_SWIPE_DIR(arg), MODX_TOUCH_X(arg), MODX_TOUCH_Y(arg)); break;
    case MODX_EV_SCROLL: q->user_t = q->now; scroll(q, MODX_SCROLL_DY(arg), MODX_TOUCH_X(arg), MODX_TOUCH_Y(arg)); break;
    case MODX_EV_TEXT:
        if (!text_in(q, arg))
            return 0;
        break;
    case MODX_EV_SLEEP:
    case MODX_EV_LEAVE:
        if (mq_prof(q)->used)
            mq_coach_quit(q); /* where play stopped (the grown-ups' play log) */
        mq_save_now(q);
        return 0;
    default: return 0;
    }
    if (q->redraw) {
        q->redraw = false;
        draw_scene(q);
    }
    return 1;
}

/* ------------------------------------------------------------------ console and state */
static int parse_int(const mq_t *q, const char *s, long *v) { return q->api->con_parse_int(s, v) ? 0 : -1; }

static void grant_world(mq_t *q, int w)
{
    mq_profile_t *p = mq_prof(q);
    for (int l = 0; l < mq_world_levels(w); l++) {
        mq_skrec_t *s = &p->sk[mq_levels[w][l] - 1];
        s->plays = s->plays ? s->plays : 1, s->mastery = 100, s->right = 10, s->stars = 3, s->flags |= SK_PASSED;
    }
    mq_boss_set(p, w);
}

static int op_console(void *rw, int argc, char **argv)
{
    mq_t *q = rw;
    const modx_api_t *api = q->api;
    clock_to(q, api->millis());
    const char *sub = argc > 1 ? argv[1] : "info";
    long n = 0, m = 0;
    if (!api->strcmp(sub, "seed") && argc > 2 && !parse_int(q, argv[2], &n)) {
        q->rng = (uint32_t)n * 2654435761u + 1;
        return 0;
    }
    if (!api->strcmp(sub, "chapter") && argc > 2 && !parse_int(q, argv[2], &n)) { /* mq chapter SLOT: their next step */
        if (mq_chapter_console(q, (int)n))
            return api->con_error("no step to tell");
        draw_scene(q);
        return 0;
    }
    if (!api->strcmp(sub, "hearts") && argc > 3 && !parse_int(q, argv[2], &n) && !parse_int(q, argv[3], &m)) {
        mq_heart_add(q, (int)n, (int)m); /* mq hearts SLOT N */
        return 0;
    }
    if (!api->strcmp(sub, "day") && argc > 2 && !parse_int(q, argv[2], &n)) { /* mq day N: the play day (tests) */
        mq_prof(q)->days = (uint16_t)(n < 1 ? 1 : n > 60000 ? 60000 : n);
        if (q->scene == SC_MAP)
            mq_events_enter(q);
        draw_scene(q);
        return 0;
    }
    if (!api->strcmp(sub, "playtime") && argc > 2 && !parse_int(q, argv[2], &n)) { /* mq playtime MIN (tests) */
        mq_prof(q)->secs = (uint32_t)(n < 0 ? 0 : n) * 60u;
        draw_scene(q);
        return 0;
    }
    if (!api->strcmp(sub, "opengames")) { /* mq opengames: Cake Kitchen and Pet Shop, as their stories would */
        mq_extra(q)->games_open = 3;
        return 0;
    }
    if (!api->strcmp(sub, "event") && argc > 2) { /* mq event KIND [ARG]: today's special (tests) */
        for (int k = 0; k < EV_COUNT; k++) {
            mq_event_force(q, k, 0);
            if (!api->strcmp(argv[2], mq_event_key(q))) {
                mq_event_force(q, k, argc > 3 && !parse_int(q, argv[3], &n) ? (int)n : 0);
                if (q->scene == SC_MAP)
                    mq_events_enter(q);
                draw_scene(q);
                return 0;
            }
        }
        mq_event_force(q, -1, 0);
        return api->con_error("no event %s", argv[2]);
    }
    if (!api->strcmp(sub, "twist") && argc > 2 && !parse_int(q, argv[2], &n)) { /* mq twist M: this round's twist */
        q->gm.mod = (uint8_t)(n >= 0 && n < MOD_COUNT ? n : 0), q->gm.mod_order = q->gm.mod == MOD_REMIX ? MOD_MIRROR : 0;
        q->gm.order_t = q->anim_t, q->gm.hud_ok = 0;
        draw_scene(q);
        return 0;
    }
    if (!api->strcmp(sub, "merge")) {
        int r = mq_merge_console(q, argc, argv);
        draw_scene(q);
        return r;
    }
    if (!api->strcmp(sub, "gate")) { /* mq gate [seed N|answer|pass|wait]: the grown-ups' gate (the Mac's corner menu
                                       * "Grown-ups..."; the rest for tests: mq_gate.c) */
        int r = mq_gate_console(q, argc, argv);
        if (r < 0)
            return api->con_error("usage: mq gate [seed N|answer|pass|wait]");
        gate_result(q, r);
        draw_scene(q);
        return 0;
    }
    if (!api->strcmp(sub, "scene") && argc > 2) { /* mq scene off|on: orders acted out first (default on) */
        q->scenes_off = !api->strcmp(argv[2], "off");
        return 0;
    }
    if (!api->strcmp(sub, "new") && argc > 2) { /* mq new NAME [BUDDY] */
        q->engine_stop = false, q->fixed_sk1 = 0; /* a new player: the engine chooses (tests set `engine stop` after) */
        int slot = q->save.prof[0].used ? 1 : 0;
        q->save.cur = (uint8_t)slot;
        mq_profile_t *p = mq_prof(q);
        uint8_t *b = (uint8_t *)p;
        for (size_t i = 0; i < sizeof *p; i++)
            b[i] = 0;
        mq_home_init(q, slot);
        mq_extra_migrate(q, slot);
        mq_pname_set(q, slot, argv[2]);
        learn_reset(q, slot);
        q->guess_n = q->rapid = 0, q->first_run = 0;
        p->buddy = (uint8_t)(argc > 3 && !parse_int(q, argv[3], &n) ? n % MQ_NCAST : 2);
        p->used = 1;
        school_default(q);
        q->day_started = true;
        p->days = 1;
        mq_save_now(q);
        go(q, SC_MAP);
        draw_scene(q);
        return 0;
    }
    if (!api->strcmp(sub, "player") && argc > 2 && !parse_int(q, argv[2], &n)) {
        q->save.cur = (uint8_t)(n & 1);
        go(q, mq_prof(q)->used ? SC_MAP : SC_TITLE);
        draw_scene(q);
        return 0;
    }
    if (!api->strcmp(sub, "go") && argc > 2) {
        for (int s = 0; s < SC_COUNT; s++)
            if (!api->strcmp(argv[2], scene_name[s])) {
                if (s == SC_PARENT)
                    open_parent(q);
                else if (s == SC_ROOM || s == SC_SHOP || s == SC_BOOK || s == SC_SHOWOFF || s == SC_WARDROBE)
                    mq_home_enter(q, s);
                else
                    go(q, s);
                draw_scene(q);
                return 0;
            }
        return api->con_error("no scene %s", argv[2]);
    }
    if (!api->strcmp(sub, "town") && argc > 2 && !parse_int(q, argv[2], &n)) { /* stand at x on the street */
        mq_town_set(q, (int)n);
        q->redraw = true;
        return 0;
    }
    if (!api->strcmp(sub, "world") && argc > 2 && !parse_int(q, argv[2], &n) && n >= 0 && n < MQ_NWORLD) {
        press(q, B_WORLD, (int)n);
        draw_scene(q);
        return q->scene == SC_WORLD ? 0 : api->con_error("world %ld: not now", n);
    }
    if ((!api->strcmp(sub, "level") || !api->strcmp(sub, "boss")) && argc > 2 && !parse_int(q, argv[2], &n) &&
        n >= 0 && n < MQ_NWORLD) {
        q->fixed_sk1 = 0;
        bool boss = !api->strcmp(sub, "boss");
        if (!boss && (argc < 4 || parse_int(q, argv[3], &m) || m < 0 || m >= mq_world_levels((int)n)))
            return api->con_error("usage: mq level WORLD LEVEL");
        start_level(q, (int)n, boss ? 0 : (int)m, boss);
        draw_scene(q);
        return 0;
    }
    if (!api->strcmp(sub, "engine") && argc > 2) { /* `mq engine stop|on`: the stop's own skill, or the engine's choice */
        q->engine_stop = !api->strcmp(argv[2], "stop");
        return 0;
    }
    if (!api->strcmp(sub, "speed"))
        return mq_speed_console(q, argc, argv);
    if (!api->strcmp(sub, "focus"))
        return mq_focus_console(q, argc, argv);
    if (!api->strcmp(sub, "skill") && argc > 2 && !parse_int(q, argv[2], &n) && n >= 0 && n < MQ_NSKILL) {
        int lv, w = skill_world_level((int)n, &lv);
        q->fixed_sk1 = (uint8_t)(n + 1); /* the run asks this skill only (tests) */
        start_level(q, w, lv, false);
        new_problem(q, (int)n, argc > 3 && !parse_int(q, argv[3], &m) ? (int)m : 0);
        draw_scene(q);
        return 0;
    }
    if (!api->strcmp(sub, "answer") && argc > 2 && q->scene == SC_PLAY) { /* typed into the game, then ENTER */
        q->last_input = q->now;
        for (const char *s = argv[2]; *s; s++)
            mq_game_key(q, 0, *s);
        mq_game_key(q, K_ENTER, 0);
        draw_scene(q);
        return 0;
    }
    if (!api->strcmp(sub, "answer") && argc > 2) { /* type TEXT and press ok (choices: 1-based tile) */
        if (q->scene != SC_QUIZ && q->scene != SC_FACTGAME)
            return api->con_error("not playing");
        q->in_len = 0, q->input[0] = 0;
        for (const char *s = argv[2]; *s; s++)
            type_char(q, *s);
        if (q->scene == SC_QUIZ && q->prob.kind != K_CHOICE)
            submit_input(q);
        else if (q->scene == SC_FACTGAME)
            mq_fg_submit(q);
        draw_scene(q);
        return 0;
    }
    if (!api->strcmp(sub, "log"))
        return mq_coach_console(q);
    if (!api->strcmp(sub, "tracking")) { /* mq tracking: what her tracking holds (tests: it survives a reload) */
        mq_profile_t *p = mq_prof(q);
        const mq_learn_t *L = mq_learn(q);
        int seen = 0, rungs = 0, box[6] = {0}, rev = 0;
        for (int i = 0; i < MQ_NSEEN; i++)
            seen += L->seen[i].ease != 0;
        for (int k = 0; k < MQ_NSKILL; k++)
            rungs += p->sk[k].diff, rev += L->rev[k].box != 0;
        for (int f = 0; f < MQ_NFACT; f++)
            box[p->fact[f].box > 5 ? 5 : p->fact[f].box]++;
        api->con_printf("min %u seen %d rungs %d boxes %d %d %d %d %d %d review %d problems %lu round %u\n", mq_play_min(q), seen,
                        rungs, box[0], box[1], box[2], box[3], box[4], box[5], rev, (unsigned long)p->problems, p->round);
        return 0;
    }
    if (!api->strcmp(sub, "perf")) { /* mq perf [N]: the cost of N frames of this scene, and an MCU estimate */
        long nf = 50;
        if (argc > 2 && parse_int(q, argv[2], &nf))
            nf = 50;
        uint32_t fills = 0, px = 0, rows = 0, c0 = api->cycles(), cpu = api->cycles_per_us();
        for (long k = 0; k < nf; k++) {
            q->now += MQ_FRAME_MS, q->last_input = q->now;
            frame(q, MQ_FRAME_MS);
            q->fill_n = 0, q->px_acc = 0, q->dirty_y0 = 32767, q->dirty_y1 = 0, q->redraw = false;
            if (q->scene == SC_PLAY && k % 25 == 0)
                q->gm.hud_ok = 0; /* (a change at the top now and then) */
            draw_scene(q);
            fills += q->fill_n, px += q->px_acc;
            rows += q->dirty_y1 > q->dirty_y0 ? (uint32_t)(q->dirty_y1 - q->dirty_y0) : 0;
        }
        uint32_t host = cpu ? (api->cycles() - c0) / cpu / (uint32_t)nf : 0;
        uint32_t f = fills / (uint32_t)nf, p = px / (uint32_t)nf, r = rows / (uint32_t)nf;
        /* the MCU model: 0.35 us a rectangle, 6 ns a pixel (AXI SRAM), the flush 6 ms per 240 rows */
        uint32_t draw_us = f * 35 / 100 + p * 6 / 1000, flush_us = 6000u * r / 240u;
        api->con_printf("%s: %lu fills, %lu px, %lu rows a frame; host %lu us; MCU about %lu us drawing + %lu us flush\n",
                        scene_name[q->scene], (unsigned long)f, (unsigned long)p, (unsigned long)r, (unsigned long)host,
                        (unsigned long)draw_us, (unsigned long)flush_us);
        return 0;
    }
    if (!api->strcmp(sub, "correct") || !api->strcmp(sub, "wrong") || !api->strcmp(sub, "try")) {
        bool right = !api->strcmp(sub, "correct");
        q->last_input = q->now; /* (her answer, now: mq_judge times the decision to the input that gave it) */
        if (mq_toys_answer(q, right)) {
            draw_scene(q);
            return 0;
        }
        if (q->scene == SC_FACTGAME) {
            int f = q->fg == FG_STARS ? q->fall[q->fall[0].y >= q->fall[1].y ? 0 : 1].fact : q->fg_cur;
            api->snprintf(q->input, sizeof q->input, "%d", mq_fact_answer(f) + (right ? 0 : 1));
            q->in_len = 0;
            while (q->input[q->in_len])
                q->in_len++;
            mq_fg_submit(q);
            draw_scene(q);
            return 0;
        }
        if (q->scene == SC_PLAY) { /* the game action that gives a right (or a wrong) answer */
            if (q->sub != GS_PLAY)
                return api->con_error("not playing");
            mq_game_auto(q, right);
            if (!right && q->sub == GS_PLAY && q->tried_wrong && api->strcmp(sub, "try")) /* `mq wrong`: both tries */
                mq_game_auto(q, false);
            draw_scene(q);
            return 0;
        }
        if (q->scene != SC_QUIZ || q->sub)
            return api->con_error("not playing");
        mq_prob_t *p = &q->prob;
        if (right)
            submit_value(q, p->ans);
        else if (p->kind == K_CHOICE)
            submit_value(q, (p->ans + 1) % p->nchoice);
        else {
            submit_value(q, p->nwrong ? p->wrong[0] : p->ans + 1);
            if (q->retrying && api->strcmp(sub, "try")) /* `mq wrong`: both tries; `mq try`: one */
                submit_value(q, p->nwrong ? p->wrong[0] : p->ans + 1);
        }
        draw_scene(q);
        return 0;
    }
    if (!api->strcmp(sub, "press") && argc > 2) { /* press the button with this label text (tests) */
        return api->con_error("use touch tap");
    }
    if (!api->strcmp(sub, "next") && q->scene == SC_PLAY) { /* the round's moments */
        if (q->sub == GS_CHEER)
            mq_run_after_right(q);
        else if (q->sub == GS_WHY)
            mq_run_after_miss(q);
        else if (q->sub == GS_OFFER)
            press(q, B_NO, 0);
        else if (q->sub == GS_TRICK)
            mq_trick_next(q);
        else if (q->sub == GS_HELP)
            mq_helper_next(q);
        draw_scene(q);
        return 0;
    }
    if (!api->strcmp(sub, "bulb") && q->scene == SC_PLAY) { /* the lightbulb (tests) */
        mq_bulb_start(q);
        draw_scene(q);
        return 0;
    }
    if (!api->strcmp(sub, "help") && q->scene == SC_PLAY) { /* what do I do? (tests) */
        mq_guide_help(q);
        draw_scene(q);
        return 0;
    }
    if (!api->strcmp(sub, "preview") && argc > 2 && !parse_int(q, argv[2], &n) && n >= 0 && n < MQ_NSKILL) {
        press(q, B_PREVIEW, (int)n);
        if (argc > 3 && !parse_int(q, argv[3], &m))
            new_problem(q, (int)n, (int)m);
        draw_scene(q);
        return 0;
    }
    if (!api->strcmp(sub, "roundhelp") && argc > 2 && !parse_int(q, argv[2], &n)) { /* Hop Along's rounding help: 0..2, -1 hers */
        q->round_force = (uint8_t)(n >= 0 && n <= 2 ? n + 1 : 0);
        return 0;
    }
    if (!api->strcmp(sub, "tablerun") && argc > 2 && !parse_int(q, argv[2], &n)) /* mq tablerun T: a times-table run */
        return mq_fact_run_start(q, (int)n) ? 0 : api->con_error("no run of the %lds now", (long)n);
    if (!api->strcmp(sub, "mech") && argc > 2 && q->scene == SC_PLAY) { /* tests: this order in game M */
        for (int k = 0; k < ME_COUNT; k++)
            if (!api->strcmp(argv[2], mq_mech_name(k)) || (!parse_int(q, argv[2], &n) && n == k)) {
                q->gm.round_mech = (uint8_t)k;
                mq_game_order(q);
                draw_scene(q);
                return mq_mech_fits(k, &q->prob) ? 0 : api->con_error("%s does not fit this order", mq_mech_name(k));
            }
        return api->con_error("no game %s", argv[2]);
    }
    if (!api->strcmp(sub, "scroll") && argc > 2 && !parse_int(q, argv[2], &n)) { /* mq scroll DY [X Y]: a wheel (tests) */
        long sx = 160, sy = 120;
        if (argc > 4)
            parse_int(q, argv[3], &sx), parse_int(q, argv[4], &sy);
        scroll(q, (int)n, (int)sx, (int)sy);
        draw_scene(q);
        return 0;
    }
    if (!api->strcmp(sub, "notebook")) { /* mq notebook [LEVEL]: open the order's notebook; its plan */
        if (argc > 2 && !parse_int(q, argv[2], &n))
            for (int w = 0; w < WS_COUNT; w++)
                mq_pace(q)->ws_level[w] = (uint8_t)(n < 0 ? 0 : n > 2 ? 2 : n);
        if (argc > 2)
            mq_cols_close(q); /* (reopened at the new level) */
        if (q->scene == SC_PLAY && mq_cols_wanted(q) && !mq_cols_on(q))
            mq_cols_open(q);
        draw_scene(q);
        return mq_cols_console(q);
    }
    if (!api->strcmp(sub, "strat")) { /* mq strat [list|S [show|use|bulb] N..|act V|next|try|back|auto|order S|check|page] */
        int r = mq_strat_console(q, argc, argv);
        draw_scene(q);
        return r;
    }
    if (!api->strcmp(sub, "diag")) { /* mq diag [show PT|give V|next|done|reset]: error patterns */
        int r = mq_diag_console(q, argc, argv);
        draw_scene(q);
        return r;
    }
    if (!api->strcmp(sub, "teach")) { /* mq teach [IDEA|pick N|skip|next|done]: a teaching moment */
        int r = mq_teach_console(q, argc, argv);
        draw_scene(q);
        return r;
    }
    if (!api->strcmp(sub, "hits")) { /* DEBUG (read-only, tests/bot): every tap target: the buttons, then the widgets */
        for (int i = 0; i < q->nbtn; i++)
            api->con_printf("btn %d id %d arg %d at %d,%d %dx%d\n", i, q->btn[i].id, q->btn[i].arg, q->btn[i].x, q->btn[i].y,
                            q->btn[i].w, q->btn[i].h);
        if (q->scene == SC_PLAY) {
            mq_game_widgets(q);
            api->con_printf("game sub %d nopt %d right %d gone %d opts %ld %ld %ld %ld aim %d value %ld bw %d bh %d px %d py %d "
                            "mode %d ans %ld wrongv %ld fixh %d story %d\n", q->sub, q->gm.nopt, mq_game_right_opt(q), q->gm.gone,
                            (long)q->gm.opt[0], (long)q->gm.opt[1], (long)q->gm.opt[2], (long)q->gm.opt[3], q->gm.aim,
                            (long)q->gm.value, q->gm.bw, q->gm.bh, q->gm.px, q->gm.py, q->gm.mode, (long)q->prob.ans,
                            (long)(q->prob.nwrong ? q->prob.wrong[0] : q->prob.ans + 1), q->gm.fixh, q->gm.scene_on);
            return mq_wg_console(q); /* (the widgets are the game's: elsewhere they would be stale) */
        }
        if ((q->scene == SC_SHOP || q->scene == SC_WARDROBE) && mq_pay_on(q)) /* the counter's coins and keys */
            return mq_wg_console(q);
        if (q->scene == SC_DONE_LEVEL) { /* the done screen's ways on */
            done_layout(q);
            return mq_wg_console(q);
        }
        return 0;
    }
    if (!api->strcmp(sub, "widgets")) { /* the scene's widgets (tests) */
        if (q->scene == SC_PLAY)
            mq_game_widgets(q);
        if (q->scene == SC_DONE_LEVEL)
            done_layout(q);
        return mq_wg_console(q);
    }
    if (!api->strcmp(sub, "rung"))
        return mq_rung_console(q, argc, argv);
    if (!api->strcmp(sub, "tap") && argc > 3 && !parse_int(q, argv[2], &n) && !parse_int(q, argv[3], &m)) {
        touch(q, MODX_TOUCH(4, n, m)); /* a tap in logical coordinates (tests) */
        draw_scene(q);
        return 0;
    }
    if (!api->strcmp(sub, "next")) { /* continue / next step */
        if (q->scene == SC_FEEDBACK || q->scene == SC_OFFER)
            press(q, B_NEXT, 0);
        else
            press(q, B_CONT, 0);
        draw_scene(q);
        return 0;
    }
    if (!api->strcmp(sub, "stay")) { /* the done screen's "Stay & play again!" */
        press(q, B_STAY, 0);
        draw_scene(q);
        return 0;
    }
    if (!api->strcmp(sub, "showme")) {
        press(q, B_SHOWME, 0);
        draw_scene(q);
        return 0;
    }
    if (!api->strcmp(sub, "game") && argc > 2 && !parse_int(q, argv[2], &n) && n >= 0 && n < FG_COUNT) {
        press(q, B_GAME, (int)n);
        draw_scene(q);
        return 0;
    }
    if (!api->strcmp(sub, "settle")) { /* (tests) frames until the text on show moves on by itself: the cheer, the why, a
                                          * step of the trick or the helper (their reading time; at most 40 s) */
        mq_game_t *g = &q->gm;
        int sub0 = q->sub, st0 = g->help_step;
        bool way0 = mq_strat_on(q); /* (a way's model being shown: until it hands back) */
        for (int k = 0; k < 1000 && q->scene == SC_PLAY && q->sub == sub0 && g->help_step == st0 && mq_strat_on(q) == way0 &&
                        (way0 || q->sub == GS_CHEER || q->sub == GS_WHY || q->sub == GS_TRICK || q->sub == GS_HELP);
             k++) {
            q->now += MQ_FRAME_MS;
            q->last_input = q->now;
            frame(q, MQ_FRAME_MS);
        }
        q->trans_t = 0;
        draw_scene(q);
        return 0;
    }
    if (!api->strcmp(sub, "step")) { /* N frames of MQ_FRAME_MS, independent of the clock */
        if (argc > 2 && (parse_int(q, argv[2], &n) || n < 1 || n > 100000))
            return api->con_error("bad frame count");
        if (argc <= 2)
            n = 1;
        for (long k = 0; k < n; k++) {
            q->now += MQ_FRAME_MS;
            q->last_input = q->now;
            frame(q, MQ_FRAME_MS);
            api->watchdog_kick();
        }
        q->last_frame = api->millis();
        q->trans_t = 0; /* tests see the scene, not the curtain or coins in flight */
        for (int i = 0; i < 8; i++)
            q->fly[i].t0 = 0;
        draw_scene(q);
        return 0;
    }
    if (!api->strcmp(sub, "factstat")) { /* the facts' state: rounds, the boxes, the tables met (tests, the bot) */
        mq_profile_t *p = mq_prof(q);
        int box[6] = {0}, met[13] = {0}, fl = 0, tot = 0, triv_due = 0;
        for (int f = 0; f < 169; f++) {
            if (!mq_fact_required(p, f))
                continue;
            int a = f / 13, b = f % 13, t = a > b ? a : b;
            const mq_fact_t *x = &p->fact[f];
            if (mq_fact_trivial(f)) {
                triv_due += x->seen && x->due <= p->round;
                continue;
            }
            tot++, box[x->box > 5 ? 5 : x->box]++, fl += mq_fact_fluent(x);
            met[t] += x->seen ? 1 : 0;
        }
        api->con_printf("facts: round %u, rules %d, fluent %d of %d, boxes %d %d %d %d %d %d, trivial due %d, secs %u, rung %d\n",
                        (unsigned)p->round, mq_fact_rules_known(p) ? 1 : 0, fl, tot, box[0], box[1], box[2], box[3], box[4],
                        box[5], triv_due, (unsigned)p->secs, mq_engine_diff(q, S_FACTS));
        api->con_printf("met by table: 2:%d 3:%d 4:%d 5:%d 6:%d 7:%d 8:%d 9:%d 10:%d\n", met[2], met[3], met[4], met[5], met[6],
                        met[7], met[8], met[9], met[10]);
        return 0;
    }
    if (!api->strcmp(sub, "read")) { /* mq read [auto|slower|normal|faster] [N]: the reading speed; its time for N chars */
        for (int rs = 0; argc > 2 && rs < 4; rs++)
            if (!api->strcmp(argv[2], mq_read_speed_name(rs)))
                mq_pace(q)->read_speed = (uint8_t)rs, q->dirty = true;
        long nc = 30;
        if (argc > 3 && parse_int(q, argv[3], &nc))
            return api->con_error("usage: mq read [auto|slower|normal|faster] [CHARS]");
        api->con_printf("read: %s, grade %d, pace %d, %d chars %d ms, a letter %d ms, short %u of %u\n",
                        mq_read_speed_name(mq_pace(q)->read_speed), mq_read_grade(q), mq_read_pace(q), (int)nc,
                        mq_read_ms_len(q, (int)nc), mq_type_ms(q), (unsigned)q->read_short, (unsigned)q->read_n);
        return 0;
    }
    if (!api->strcmp(sub, "school")) { /* mq school [G W [default]]: where school is (G 0: not said); default: for
                                          * every player who has not had it said, and new ones (a page's preset) */
        long g2 = 0, w2 = 0;
        if (argc > 3 && (parse_int(q, argv[2], &g2) || parse_int(q, argv[3], &w2)))
            return api->con_error("usage: mq school GRADE WEEK [default]");
        if (argc > 4 && !api->strcmp(argv[4], "default")) {
            q->save.pad[0] = (uint8_t)(g2 >= 3 && g2 <= 5 ? g2 : 0);
            q->save.pad[1] = (uint8_t)(w2 < 0 ? 0 : w2 > MQ_SCHOOL_WEEKS ? MQ_SCHOOL_WEEKS : w2);
            uint8_t cur = q->save.cur;
            for (int s = 0; s < 2; s++)
                if (q->save.prof[s].used)
                    q->save.cur = (uint8_t)s, school_default(q);
            q->save.cur = cur;
            q->dirty = true;
        } else if (argc > 3)
            mq_set_school(q, (int)g2, (int)w2);
        int wk, sg = mq_school(q, &wk);
        api->con_printf("school: grade %d week %d (default %d %d)\n", sg, wk, q->save.pad[0], q->save.pad[1]);
        return 0;
    }
    if (!api->strcmp(sub, "grant") && argc > 2) { /* test hooks: grant world W | bosses | facts | all */
        mq_profile_t *p = mq_prof(q);
        if (!api->strcmp(argv[2], "facts") || !api->strcmp(argv[2], "all"))
            for (int f = 0; f < MQ_NFACT; f++)
                p->fact[f].box = 5, p->fact[f].t10 = 15, p->fact[f].seen = 5, p->fact[f].right = 5 | FACT_DAYS;
        if (!api->strcmp(argv[2], "bosses"))
            p->bosses = 0xFF, p->pad[1] = 0xFF;
        if (!api->strcmp(argv[2], "all"))
            for (int w = 0; w < MQ_NWORLD; w++)
                grant_world(q, w);
        if (!api->strcmp(argv[2], "world") && argc > 3 && !parse_int(q, argv[3], &n) && n >= 0 && n < MQ_NWORLD)
            grant_world(q, (int)n);
        if (!api->strcmp(argv[2], "stars") && argc > 3 && !parse_int(q, argv[3], &n))
            p->stars = (uint16_t)n;
        int tot, fl = mq_facts_fluent(p, &tot);
        if (fl == tot)
            p->sk[S_FACTS].flags |= SK_PASSED;
        q->dirty = true;
        q->redraw = true;
        return 0;
    }
    if (!api->strcmp(sub, "theme")) {
        q->theme_err = (int8_t)mq_theme_load(q, argc > 2 ? argv[2] : "");
        if (!q->theme_err)
            api->snprintf(q->save.theme, sizeof q->save.theme, "%s", argc > 2 ? argv[2] : "");
        q->redraw = true;
        return q->theme_err ? api->con_error("theme not loaded (%d): built-in cast", q->theme_err) : 0;
    }
    if (!api->strcmp(sub, "coverage")) {
        char err[64];
        int bad = mq_coverage_check(err, sizeof err, api);
        api->con_printf("%d standards, %d skills: %s\n", MQ_NSTD, MQ_NSKILL, bad ? err : "every standard is mapped");
        return bad ? -1 : 0;
    }
    if (!api->strcmp(sub, "save")) {
        mq_save_now(q);
        return 0;
    }
    if (!api->strcmp(sub, "info")) {
        char a[24];
        mq_format(q, &q->prob, q->prob.ans, a, sizeof a);
        api->con_printf("%s player %d world %d level %d skill %s: %s | %s | answer %s\n", scene_name[q->scene],
                        q->save.cur, q->world, q->level, mq_skill[q->prob.skill].name, q->prob.text[0],
                        q->prob.text[1], a);
        api->con_printf("id %08lx\n", (unsigned long)mq_prob_ident(&q->prob)); /* (the problem's identity: its history's key) */
        return 0;
    }
    if (!api->strcmp(sub, "act")) {
        int r = mq_action_console(q, argc, argv);
        draw_scene(q);
        return r;
    }
    if (!api->strcmp(sub, "fun")) {
        int r = mq_fun_console(q, argc, argv);
        draw_scene(q);
        return r;
    }
    if (!api->strcmp(sub, "story")) {
        int r = mq_hstory_console(q, argc, argv);
        draw_scene(q);
        return r;
    }
    int hr = mq_home_console(q, argc, argv);
    if (hr != -2) {
        draw_scene(q);
        return hr;
    }
    return api->con_error("usage: mq seed N|new NAME [BUDDY]|player N|go SCENE|world W|level W L|boss W|skill S [D]|"
                          "answer X|correct|wrong|next|showme|game G|step [N]|grant ...|theme [FILE]|coverage|save|info|"
                          "coins N|buy I|place I X Y [F]|room R|sel K|turn|flip|front|back|store|undo|style I|wear I|"
                          "gift|photo|catalog");
}

static void op_state(void *rw, void (*kv)(const char *, const char *, void *), void *user)
{
    mq_t *q = rw;
    const modx_api_t *api = q->api;
    mq_profile_t *p = mq_prof(q);
    char b[64];
    kv("mq_scene", scene_name[q->scene], user);
    kv("mq_gate", !q->gate.shown ? "off" : q->gate.wait_until && (int32_t)(q->now - q->gate.wait_until) < 0 ? "wait" : "up",
       user);
    kv("mq_gate_kind", q->gate.shown ? mq_gate_kind_name(q->gate.p.kind) : "", user);
    kv("mq_gate_text", q->gate.shown && !(q->gate.wait_until && (int32_t)(q->now - q->gate.wait_until) < 0) ? q->gate.p.text : "",
       user); /* (the problem on the screen; never its answer) */
    kv("mq_player", p->used ? mq_name(q) : "", user);
    { /* a card lands where it says (the bot's card-lands): the cards here, "ID:ARG=Name;...", and where she is now */
        char c[200];
        int n = 0;
        c[0] = 0;
        if (q->scene == SC_WORLD) {
            int mech[3], stop[3], nc = world_choices(q->world, mech, stop);
            for (int i = 0; i < nc && n < 180; i++)
                n += api->snprintf(c + n, sizeof c - (size_t)n, "%d:%d=%s;", B_WGAME, i == nc - 1 ? 3 : i,
                                   i == nc - 1 ? "finale" : mq_mech_name(mech[i]));
        } else if (q->scene == SC_ARCADE) {
            for (int g = 0; g < FG_COUNT && n < 180; g++)
                n += api->snprintf(c + n, sizeof c - (size_t)n, "%d:%d=%s;", B_GAME, g, game_name[g]);
            for (int g = 0; g < 3 && n < 180; g++)
                n += api->snprintf(c + n, sizeof c - (size_t)n, "%d:%d=%s;", B_GAME, 10 + g, mq_action_name(g));
        }
        kv("mq_cards", c, user);
        const char *here = "";
        if (q->scene == SC_PLAY)
            here = q->boss ? "finale" : q->gm.chosen && q->gm.round_mech < ME_COUNT ? mq_mech_name(q->gm.round_mech) : "";
        else if (q->scene == SC_FACTGAME)
            here = game_name[q->fg % FG_COUNT];
        else if (q->scene == SC_ACTION)
            here = mq_action_name(q->act.game);
        kv("mq_here", here, user);
    }
    api->snprintf(b, sizeof b, "%u %u %u %u", (unsigned)q->read_short, (unsigned)q->read_n, (unsigned)q->read_short_got,
                  (unsigned)q->read_short_need);
    kv("mq_read", b, user); /* texts too soon without a tap, texts that went by themselves, the last too soon: got, needed */
    api->snprintf(b, sizeof b, "%d", q->world);
    kv("mq_world", b, user);
    api->snprintf(b, sizeof b, "%d", q->level);
    kv("mq_level", b, user);
    kv("mq_skill", mq_skill[q->prob.skill].name, user);
    api->snprintf(b, sizeof b, "%d", mq_skill[q->prob.skill].grade);
    kv("mq_sk_grade", b, user); /* (the order's grade: the grade setting's tests) */
    kv("mq_q", q->prob.text[0], user);
    api->snprintf(b, sizeof b, "%s %s", q->prob.text[1], q->prob.text[2]);
    kv("mq_q_more", b, user); /* (its other lines: the numbers a question names, "3/8  ?  1/2") */
    mq_format(q, &q->prob, q->prob.ans, b, sizeof b);
    kv("mq_answer", b, user);
    kv("mq_mc", mq_mc_name[q->last_mc], user);
    kv("mq_input", q->input, user);
    api->snprintf(b, sizeof b, "%d", p->stars);
    kv("mq_stars", b, user);
    api->snprintf(b, sizeof b, "%d", q->combo);
    kv("mq_combo", b, user);
    api->snprintf(b, sizeof b, "%d/%d", q->run_i, q->run_n + q->extra);
    kv("mq_run", b, user);
    kv("mq_placing", mq_placing(q) ? "1" : "0", user);
    kv("mq_bulb", q->gm.bulb_t ? "1" : "0", user);
    api->snprintf(b, sizeof b, "%d", mq_focus_count(q, NULL, NULL));
    kv("mq_focus", b, user);
    kv("mq_demo", mq_guide_demo_on(q) ? "1" : "0", user);
    {
        const mq_pace_t *pc = mq_pace(q);
        api->snprintf(b, sizeof b, "%d %d %d %d", pc->strand[0], pc->strand[1], pc->strand[2], pc->strand[3]);
        kv("mq_strands", b, user);
        api->snprintf(b, sizeof b, "%d", mq_start_grade(q));
        kv("mq_start_grade", b, user);
        int swk, sgr = mq_school(q, &swk);
        api->snprintf(b, sizeof b, "%d %d", sgr, swk);
        kv("mq_school", b, user);
        api->snprintf(b, sizeof b, "%d", q->gm.mech < ME_COUNT ? mq_speed_level(q, q->gm.mech) : 0);
        kv("mq_speed", b, user);
    }
    int tot, fl = mq_facts_fluent(p, &tot);
    api->snprintf(b, sizeof b, "%d/%d", fl, tot);
    kv("mq_fluent", b, user);
    int ok = 0, all = 0;
    for (int s = 0; s < MQ_NSTD; s++)
        if (mq_std[s].grade <= 4)
            all++, ok += mq_std_passed(p, s);
    api->snprintf(b, sizeof b, "%d/%d", ok, all);
    kv("mq_standards", b, user);
    kv("mq_complete", p->complete ? "1" : "0", user);
    api->snprintf(b, sizeof b, "%d/%d", q->ex_step + 1, q->ex_n);
    kv("mq_example_step", b, user);
    kv("mq_theme", q->theme.loaded ? q->save.theme : "built-in", user);
    api->snprintf(b, sizeof b, "%d", q->fg_score);
    kv("mq_fg_score", b, user);
    api->snprintf(b, sizeof b, "%lu", (unsigned long)p->secs);
    kv("mq_secs", b, user);
    api->snprintf(b, sizeof b, "%d", p->sk[q->prob.skill].diff);
    kv("mq_diff", b, user);
    api->snprintf(b, sizeof b, "%lu", (unsigned long)p->stickers);
    kv("mq_stickers", b, user);
    api->snprintf(b, sizeof b, "%d", p->bosses);
    kv("mq_bosses", b, user);
    mq_home_state(q, kv, user);
    mq_hstory_state(q, kv, user);
    { /* the friends (mq_friends.c), the merge board */
        mq_extra_t *e = mq_extra(q);
        api->snprintf(b, sizeof b, "%d/%d/%d", q->chap_slot, q->chap_step, q->chap_line);
        kv("mq_chapter", b, user);
        int n = 0;
        for (int s = 0; s < MQ_NCAST; s++)
            n += api->snprintf(b + n, sizeof b - (size_t)n, s ? ",%d" : "%d", e->chap[s]);
        kv("mq_chapters", b, user);
        n = 0;
        for (int s = 0; s < MQ_NCAST; s++)
            n += api->snprintf(b + n, sizeof b - (size_t)n, s ? ",%d" : "%d", e->heart[s]);
        kv("mq_hearts", b, user);
        api->snprintf(b, sizeof b, "%d", e->energy);
        kv("mq_energy", b, user);
        mq_merge_state(q, b, sizeof b);
        kv("mq_kitchen", b, user);
        api->snprintf(b, sizeof b, "%d", mq_chapter_news(q));
        kv("mq_news", b, user);
        api->snprintf(b, sizeof b, "%d", e->games_open);
        kv("mq_games_open", b, user);
        kv("mq_event", mq_event_key(q), user);
        kv("mq_mod", q->scene == SC_PLAY ? mq_mod_name(mq_mod_now(q)) : "", user);
    }
    api->snprintf(b, sizeof b, "%lu", (unsigned long)q->fun_ms);
    kv("mq_fun", b, user);
    kv("mq_beat", q->scene == SC_FUN ? mq_beat_name(q->beat) : "", user);
    api->snprintf(b, sizeof b, "%d", mq_squish_count(mq_home(q)));
    kv("mq_squishies", b, user);
    kv("mq_mech", q->scene == SC_PLAY ? mq_mech_name(q->gm.mech) : "", user);
    kv("mq_together", q->scene == SC_PLAY && q->gm.hoh ? "1" : "0", user); /* (help together: a game that cannot be set to the answer retells it as a bubble) */
    kv("mq_lock", q->scene == SC_PLAY && q->gm.mech == ME_VAULT ? q->gm.lock : "", user);
    mq_strat_state(q, kv, user); /* a way to work it: its id, mode, phase, step */
    if (mq_teach_on(q)) { /* a teaching moment: its idea, part, phase */
        api->snprintf(b, sizeof b, "%d %d %d", q->teach.idea, q->teach.seg, q->teach.phase);
        kv("mq_teach", b, user);
    } else
        kv("mq_teach", "", user);
    if (mq_cols_on(q)) { /* the notebook: its kind, the help it gives, the box she is on */
        api->snprintf(b, sizeof b, "%d %d %d/%d", q->cols.kind, q->cols.level, q->cols.cur, q->cols.n);
        kv("mq_notebook", b, user);
        api->snprintf(b, sizeof b, "%d", q->cols.cur < q->cols.n && q->cols.cur < CW_STEPS ? q->cols.st[q->cols.cur].want : -1);
        kv("mq_nb_want", b, user); /* (the box's digit: the playtest bot writes it) */
    } else
        kv("mq_notebook", "", user), kv("mq_nb_want", "", user);
    { /* the error patterns: the friend showing one (on / off), then each pattern seen: index and status */
        int o = api->snprintf(b, sizeof b, "%s", q->diag.on ? "on" : "off");
        for (int pt = 0; pt < PT_COUNT && o < (int)sizeof b - 16; pt++)
            if (mq_diag_rec(q, pt)->state)
                o += api->snprintf(b + o, sizeof b - (size_t)o, " %d %s", pt, mq_pattern_status(mq_diag_rec(q, pt)->state));
        kv("mq_diag", b, user);
    }
    api->snprintf(b, sizeof b, "%d", q->scene == SC_PLAY ? mq_game_hidden(q) : 0);
    kv("mq_hidden", b, user); /* answers under the order board */
    { /* an order that needs its picture, in a game that shows it on its card: drawn this frame? (the bot) */
        int m = q->gm.mech;
        bool card = m == ME_POP || m == ME_CATCH || m == ME_GATES || m == ME_KART || m == ME_ZAP;
        kv("mq_pic", q->scene == SC_PLAY && q->sub == GS_PLAY && !q->gm.scene_on && card && mq_vis_needed(&q->prob)
                          ? (q->prob_pic_t && q->anim_t - (q->prob_pic_t & ~1u) < 400 ? "shown" : "missing") : "", user); /* (its time |1) */
    }
    { /* a Kart Race: her place now (play) or on the podium (the round's end) */
        char rb[8];
        q->api->snprintf(rb, sizeof rb, "%d", q->scene == SC_DONE_LEVEL ? q->gm.podium : q->gm.race_on ? mq_kart_place(q) : 0);
        kv("mq_race", rb, user);
        kv("mq_make", q->scene == SC_PLAY && q->gm.make ? "1" : "0", user);
    }
    kv("mq_toy", q->scene == SC_TOYS ? mq_toys_state(q, b, sizeof b) : "", user);
    kv("mq_beats", q->scene == SC_TOYS ? mq_beats_state(q, b, sizeof b) : "", user);
    kv("mq_round", mq_round_state(q, b, sizeof b), user);
    kv("mq_moment", q->scene == SC_PLAY ? mq_game_moment(q) : "", user);
    api->snprintf(b, sizeof b, "%d", q->gm.stars);
    kv("mq_round_stars", b, user);
    api->snprintf(b, sizeof b, "%lu", (unsigned long)q->frame_us);
    kv("mq_frame_us", b, user);
    api->snprintf(b, sizeof b, "%lu", (unsigned long)q->frame_px);
    kv("mq_frame_px", b, user);
    mq_meta_state(q, kv, user);
}

/* ------------------------------------------------------------------ entry */
static void copy(void *d, const void *s, size_t n)
{
    uint8_t *dd = d;
    const uint8_t *ss = s;
    while (n--)
        *dd++ = *ss++;
}

/* a save of versions 1-3 (71 skills) into this layout (86 skills): field by field */
static void save_from_v3(mq_t *q, const mq_save_v3_t *o, int v)
{
    mq_save_t *s = &q->save;
    copy(s, o, offsetof(mq_save_t, prof)); /* magic .. theme */
    for (int k = 0; k < 2; k++) {
        mq_profile_t *p = &s->prof[k];
        const mq_profile_v3_t *op = &o->prof[k];
        copy(p, op, offsetof(mq_profile_t, sk));
        copy(p->sk, op->sk, sizeof op->sk);
        copy(p->fact, op->fact, sizeof op->fact);
        if (v >= 2)
            copy(&s->home[k], &o->home[k], sizeof o->home[k]);
        if (v >= 3) {
            mq_learn_t *L = &s->learn[k];
            const mq_learn_v3_t *ol = &o->learn[k];
            copy(L->seen, ol->seen, sizeof ol->seen);
            copy(L->rev, ol->rev, sizeof ol->rev);
            L->share = ol->share;
            copy(L->recent, ol->recent, sizeof ol->recent);
            copy(L->pass_day, ol->pass_day, sizeof ol->pass_day);
            copy(L->t10, ol->t10, sizeof ol->t10);
            copy(L->hesit, ol->hesit, sizeof ol->hesit);
            copy(L->quit, ol->quit, sizeof ol->quit);
            copy(L->fact_par, ol->fact_par, sizeof ol->fact_par);
        }
    }
}

const uint32_t mq_module_rw_size = sizeof(mq_t);

int mq_module_init(const modx_api_t *api, void *rw, modx_ops_t *ops); /* modx.c (host), the descriptor (image) */
int mq_module_init(const modx_api_t *api, void *rw, modx_ops_t *ops)
{
    if (api->abi != MODX_ABI || api->size < sizeof(modx_api_t))
        return -1;
    mq_t *q = rw;
    q->api = api;
    q->scale = (uint8_t)(api->size > offsetof(modx_api_t, display_scale) && api->display_scale ? api->display_scale() : 1);
    q->rng = api->millis() * 2654435761u + 0x1234567u;
    q->pressed = -1;
    q->fun_ms = 15000; /* a little fun is allowed straight away (decorating what she owns) */
    q->beat = 0xFF, q->last_beat = 0xFF;
    if (api->save_max() < sizeof q->save)
        return -1;
    /* Saved progress. The format only grows at the end (append-only): version 1 is the profiles,
     * version 2 adds the homes, ... version 6 the long memory. An older save is migrated; a newer
     * one (a module downgrade) keeps the part this version knows. Anything else: fresh.
     * Versions 4 and up are prefixes of this layout: read straight into q->save. Versions 1-3 (71
     * skills, at most MQ_SAVE_V3_SIZE) are converted from scratch (the theme's pixel pool: no
     * theme is loaded yet). */
    uint16_t sz16 = MQ_SAVE_SIZE16(sizeof q->save);
    uint8_t *sb = (uint8_t *)&q->save;
    for (size_t i = 0; i < sizeof q->save; i++)
        sb[i] = 0;
    size_t have = api->save_size(SAVE_NAME); /* (API size 7: required above) */
    size_t got = api->save_get(SAVE_NAME, sb, sizeof q->save);
    if (have < got)
        have = got;
    mq_save_t *o = &q->save;
    bool ok = got >= MQ_SAVE_V1_SIZE && o->magic == MQ_SAVE_MAGIC && o->version >= 1;
    static const uint32_t vsize[5] = {MQ_SAVE_V1_SIZE, MQ_SAVE_V2_SIZE, MQ_SAVE_V3_SIZE, MQ_SAVE_V4_SIZE, MQ_SAVE_V5_SIZE};
    int v = ok ? o->version : 0;
    bool fresh = true;
    if (ok && v < 4 && o->size == vsize[v - 1] && got == vsize[v - 1] && vsize[v - 1] <= sizeof q->theme.px - 8) {
        uint8_t *raw = q->theme.px + ((8u - ((uintptr_t)q->theme.px & 7u)) & 7u); /* (aligned for the structs) */
        for (size_t i = 0; i < got; i++)
            raw[i] = sb[i], sb[i] = 0;
        for (size_t i = got; i < sizeof q->save; i++)
            sb[i] = 0;
        save_from_v3(q, (const mq_save_v3_t *)(const void *)raw, v); /* converted, then what it did not have yet */
        for (int k = 0; k < 2; k++)
            if (q->save.prof[k].used) {
                if (v < 2)
                    mq_home_migrate(q, k);
                if (v < 3)
                    mq_learn_migrate(q, k);
                mq_extra_migrate(q, k);
            }
        fresh = false;
    } else if (ok && v >= 4 && v < MQ_SAVE_VERSION && o->size == vsize[v - 1] && got == vsize[v - 1]) {
        for (int k = 0; k < 2; k++) /* the layout before the extras (4) or the long memory (5) */
            if (q->save.prof[k].used) {
                if (v < 5)
                    mq_extra_migrate(q, k);
                else
                    mq_more_migrate(q, k);
            }
        fresh = false;
    } else if (ok && v >= MQ_SAVE_VERSION && o->size >= sz16 && got == sizeof q->save && have >= sizeof q->save) {
        fresh = false; /* this version, or a newer one: the part this one knows */
    }
    if (fresh)
        for (size_t i = 0; i < sizeof q->save; i++)
            sb[i] = 0;
    else {
        q->save.version = MQ_SAVE_VERSION, q->save.size = sz16;
        uint8_t cur = q->save.cur; /* a save from before the ladders: its difficulties onto them */
        for (int k = 0; k < 2; k++)
            if (q->save.prof[k].used)
                q->save.cur = (uint8_t)k, mq_rung_migrate(q), mq_fact_migrate(q, k), mq_purse_migrate(q, k), mq_merge_migrate(q, k);
        q->save.cur = cur;
        for (int k = 0; k < 2; k++) { /* the name in full: an older save's (up to 10) copied over */
            char *nm = q->save.more[k].name;
            nm[sizeof q->save.more[k].name - 1] = 0;
            if (!nm[0] && q->save.prof[k].used) {
                char b[sizeof q->save.prof[k].name];
                for (int i = 0; i < (int)sizeof b; i++)
                    b[i] = i < (int)sizeof b - 1 ? q->save.prof[k].name[i] : 0;
                mq_pname_set(q, k, b);
            }
        }
    }
    for (size_t i = 0; i < sizeof q->theme.px; i++)
        q->theme.px[i] = 0;
    q->save.theme[sizeof q->save.theme - 1] = 0;
    if (q->save.theme[0])
        q->theme_err = (int8_t)mq_theme_load(q, q->save.theme);
    ops->size = sizeof *ops;
    ops->tick = op_tick;
    ops->event = op_event;
    ops->draw = op_draw;
    ops->console = op_console;
    ops->state = op_state;
    return 0;
}

MODX_DESCRIPTOR(MQ_MODULE, sizeof(mq_t), mq_module_init);
