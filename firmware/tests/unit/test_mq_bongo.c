/* Sparkle Town (modules/mathquest) Bongo Beats (mq_toys.c), played as she plays it: real finger taps (the
 * core's DOWN then TAP, no UP) and keys, frame by frame, hitting the right bongo when its order's note is on
 * the hit line:
 *   - every right hit counts once ("perfect!"/"good!" stays, no "fizz", no dud, the combo grows);
 *   - a song of steady right hits never turns calm ("Listen!") -- one tap is one hit, not two;
 *   - the song ends after its orders and pays coins;
 *   - a wrong drum is gentle (the song waits) and the right one then goes on;
 *   - what is taught shows (the user: "not sure what is being taught"): the waiting order is on the cloud as
 *     "7 x 6 = ?" from the moment it is next, a right drum says the whole fact ("7 x 6 = 42"), and a player with a
 *     table to learn gets a song of that table (its facts, then counting by it: "Song of 7s"). */
#include "mq_test_util.h"

static int duds;
static modx_api_t tapi;
static void t_sfx(int id, int param)
{
    if (id == MODX_SFX_DUD)
        duds++;
    (void)param;
}

static int cur_note(void)
{
    const mq_toy_t *t = &q->toy;
    int best = -1;
    for (int i = 0; i < t->nn; i++)
        if (t->note[i].kind == 20 && t->note[i].state != 1 && (best < 0 || t->note[i].t < t->note[best].t))
            best = i;
    return best;
}

static void touch(int type, int x, int y) { ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(type, x, y)); }
static void tap_pad(int i)
{
    int x = 8 + i * 77 + 34, y = MQ_Y0 + 156 + 20;
    touch(1, x, y); /* TEV_DOWN */
    touch(4, x, y); /* TEV_TAP (a short press: no UP) */
}
#include "../../core/gen/keymap_gen.h"
static void key_pad(int i)
{
    static const int k[4] = {KEY_1, KEY_2, KEY_3, KEY_4};
    ops.event(q, MODX_EV_KEY, k[i]);
}

/* one song: by = 0 taps, 1 keys; wrong_first: a wrong drum on the third order first */
static void play_song(int by, bool wrong_first)
{
    memset(q, 0, mq_module_rw_size);
    memset(&ops, 0, sizeof ops);
    CHECK(mq_module_init(&tapi, q, &ops) == 0);
    q->rng = 0x2545F491u;
    con("new Ada 3");
    q->fun_off = true;
    mq_toys_open(q, TOY_BEATS);
    mq_toy_t *t = &q->toy;
    CHECK(q->scene == SC_TOYS && t->mode == TOY_BEATS);
    con("step 100"); /* the first-time demo (the song waits), then the count-in */
    duds = 0;
    int hits = 0, judged = 0, calm = 0, orders = 0, max_combo = 0, wrongs = 0;
    for (int f = 0; f < 3000 && !t->done; f++) {
        int ni = cur_note();
        if (ni >= 0) {
            uint32_t now = q->anim_t - t->t0;
            int32_t dt = (int32_t)(now - t->note[ni].t);
            if (dt >= -20 && dt <= 20 && t->note[ni].state != 1) {
                int o = t->note[ni].pad & 1, right = t->oright[o];
                int pad = right;
                if (wrong_first && orders == 2 && !wrongs)
                    pad = (right + 1) % 4, wrongs++;
                else
                    orders++;
                int score = t->score, combo = t->combo;
                bool retry = t->note[ni].state == 3; /* (after a wrong drum: it goes on, no points) */
                if (by == 0)
                    tap_pad(pad);
                else
                    key_pad(pad);
                if (pad == right && retry) {
                    CHECK(t->note[ni].state == 1 && !t->hold && t->score == score && t->last_j == 3);
                } else if (pad == right) {
                    hits++;
                    judged += t->last_j_t && (t->last_j == 1 || t->last_j == 2);
                    CHECK(t->combo == combo + 1 || t->calm);
                    CHECK(t->score > score);
                } else
                    CHECK(t->hold); /* the song waits */
            }
        }
        calm += t->calm;
        max_combo = t->combo > max_combo ? t->combo : max_combo;
        con("step 1");
        if (t->last_j_t && q->anim_t - t->last_j_t < 300)
            CHECK(!(t->fizz_t && q->anim_t - t->fizz_t < 300)); /* never "fizz" right after a good hit */
    }
    CHECK(t->done);
    CHECK(hits >= (wrong_first ? 7 : 8) && judged == hits);
    CHECK(calm == 0);
    CHECK(duds == wrongs);
    CHECK(max_combo >= (wrong_first ? 5 : 8));
    CHECK(mq_game_coins(q) > 0);
    printf("test_mq_bongo: by %s%s: %d hits, %d judged, combo %d, duds %d, score %d, coins %d\n", by ? "keys" : "taps",
           wrong_first ? " (one wrong)" : "", hits, judged, max_combo, duds, t->score, (int)mq_game_coins(q));
}

/* a song of one table: every order is that table's (a fact with it, or counting by it), the flash after each right
 * drum is the whole fact and right; a new player (the x0/x1 rules not shown yet): a mix */
static void table_song(void)
{
    memset(q, 0, mq_module_rw_size);
    memset(&ops, 0, sizeof ops);
    CHECK(mq_module_init(&tapi, q, &ops) == 0);
    q->rng = 0x2545F491u;
    con("new Ada 3");
    q->fun_off = true;
    mq_toys_open(q, TOY_BEATS);
    CHECK(q->toy.song_table == 0);
    con("grant facts");
    con("focus mul 7");
    mq_toys_open(q, TOY_BEATS);
    mq_toy_t *t = &q->toy;
    CHECK(t->song_table == 7);
    int orders = 0, facts = 0, counts = 0;
    for (int f = 0; f < 4000 && !t->done; f++) {
        int ni = cur_note();
        if (ni >= 0) {
            int o = t->note[ni].pad & 1;
            if (t->onterm[o])
                for (int i = 0; i < 4; i++)
                    CHECK(t->oterm[o][i] % 7 == 0 && t->oterm[o][i] > 0 && t->oterm[o][i] <= 70);
            else {
                int a = 0, b = 0;
                bool ok = sscanf(t->ocue[o], "%d \xC3\x97 %d", &a, &b) == 2 && (a == 7 || b == 7) && a * b == t->oans[o];
                CHECK(ok);
                if (!ok)
                    printf("  not of the 7s: %s = %d\n", t->ocue[o], t->oans[o]);
            }
            uint32_t now = q->anim_t - t->t0;
            if ((int32_t)(now - t->note[ni].t) >= -20 && t->note[ni].state == 0) {
                bool cnt = t->onterm[o];
                char want[40];
                if (cnt)
                    snprintf(want, sizeof want, "%d, %d, %d, %d", t->oterm[o][0], t->oterm[o][1], t->oterm[o][2], t->oterm[o][3]);
                else
                    snprintf(want, sizeof want, "%s = %d", t->ocue[o], t->oans[o]);
                tap_pad(t->oright[o]);
                CHECK(t->flash_t && !strcmp(t->flash, want));
                orders++, facts += !cnt, counts += cnt;
            }
        }
        con("step 1");
    }
    CHECK(t->done && orders == 8 && facts >= 4 && counts >= 1);
    con("focus clear");
    printf("test_mq_bongo: a song of 7s: %d orders, %d facts, %d counting runs\n", orders, facts, counts);
}

int main(void)
{
    tapi = modx_api;
    tapi.sfx = t_sfx;
    gfx_init(fb);
    static uint8_t arena[MODX_POOL + MODX_SAVE_MAX + 16];
    modx_mem_init(arena, true);
    q = calloc(1, mq_module_rw_size);
    (void)fresh_module;
    play_song(0, false);
    play_song(1, false);
    play_song(0, true);
    table_song();
    printf("test_mq_bongo: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
