/* Sparkle Town (modules/mathquest): a card lands where it says.
 *   - every world's "What shall we play?" card, tapped, starts the game it names, and every order of that
 *     round is played in it (the engine picks only maths the game plays: mq_game_plays) -- for new players
 *     of every grade and for a player who has played a while; the finale card starts the finale;
 *   - the playtest's path: the Shop's "Shop Till" and "Pet Shop" cards (they launched Bubble Pop);
 *   - the decorating store and the Shop world are different places with different names. */
#include "mq_test_util.h"

static void draw(void)
{
    int r[4];
    ops.draw(q, 0, r);
}
static void tap(int x, int y) { ops.event(q, MODX_EV_TOUCH, MODX_TOUCH(4, x, y)); }

/* the world screen's cards: their centres, in order (the finale last) */
static int cards(int cx[4], int cy[4])
{
    draw();
    int n = 0;
    for (int i = 0; i < q->nbtn && n < 4; i++)
        if (q->btn[i].id == B_WGAME)
            cx[n] = q->btn[i].x + q->btn[i].w / 2, cy[n] = q->btn[i].y + q->btn[i].h / 2, n++;
    return n;
}

static int bad_rounds, rounds;
/* tap card i of world w; every order of the round is in game `want` (-1: the finale) */
static void play_card(int w, int i, int want, const char *who)
{
    char b[48];
    snprintf(b, sizeof b, "world %d", w);
    CHECK(con(b) == 0);
    CHECK(q->scene == SC_WORLD);
    int cx[4], cy[4], n = cards(cx, cy);
    CHECK(i < n);
    if (i >= n)
        return;
    tap(cx[i], cy[i]);
    if (want < 0) { /* the finale */
        CHECK(q->scene == SC_PLAY && q->boss);
        con("go map");
        return;
    }
    CHECK(q->scene == SC_PLAY);
    rounds++;
    bool bad = false;
    for (int k = 0; k < 10 && q->scene == SC_PLAY; k++) {
        if (q->gm.mech != want) {
            if (!bad)
                fprintf(stderr, "%s: world %d card %d (%s): order %d is %s (skill %d)\n", who, w, i, mq_mech_name(want), k,
                        mq_mech_name(q->gm.mech), q->prob.skill);
            bad = true;
        }
        if (q->teach.on)
            con("teach done");
        if (mq_cols_on(q))
            mq_cols_close(q);
        if (con("correct"))
            break;
        con("step 60");
        for (int t = 0; t < 8 && q->scene == SC_PLAY && q->sub != GS_PLAY; t++) {
            con("next");
            con("step 40");
        }
    }
    bad_rounds += bad;
    CHECK(!bad);
    con("go map");
}

static void every_card(const char *who)
{
    for (int w = 0; w < MQ_NWORLD; w++) {
        int mech[3], stop[3], n = mq_world_games(w, mech, stop);
        for (int i = 0; i < n; i++)
            play_card(w, i, mech[i], who);
        play_card(w, n, -1, who);
    }
}

int main(void)
{
    harness_init();
    /* the playtest's path: the Shop, "Shop Till" and "Pet Shop" */
    fresh_module();
    con("new Mia 2"), con("fun off"), con("opengames");
    int mech[3], stop[3], n = mq_world_games(G_SHOP, mech, stop), till = -1, pets = -1;
    for (int i = 0; i < n; i++)
        till = mech[i] == ME_TILL ? i : till, pets = mech[i] == ME_PETS ? i : pets;
    CHECK(till >= 0 && pets >= 0);
    CHECK(!strcmp(mq_mech_name(ME_TILL), "Shop Till") && !strcmp(mq_mech_name(ME_PETS), "Pet Shop"));
    if (till >= 0)
        play_card(G_SHOP, till, ME_TILL, "shop till");
    if (pets >= 0)
        play_card(G_SHOP, pets, ME_PETS, "pet shop");
    /* every card of every world, new players of each grade and the same players a while later */
    static const char *const players[] = {"new Mia 2", "new Ada 3", "new Bo 4", "new Cy 5"};
    for (int p = 0; p < 4; p++) {
        fresh_module();
        con(players[p]), con("fun off"), con("opengames");
        every_card(players[p]);
        every_card(players[p]); /* (placement done, reviews due, patterns spotted: still the card's game) */
    }
    printf("test_mq_cards: %d rounds, %d not in their card's game; %d checks, %d failed\n", rounds, bad_rounds, checks, fails);
    return fails ? 1 : 0;
}
