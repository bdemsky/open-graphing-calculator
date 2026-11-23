/* Sparkle Town (modules/mathquest): what the module tells a high-resolution platform (modx_api_t.hr,
 * the Mac page) and where it puts things -- the regression tests of the render audit
 * (docs/audit/RENDER_AUDIT.md):
 *   - every sprite id names one thing: the same parameters wherever it is drawn (the Sky Castle drew as
 *     "town/sky", the sky's own id: H3);
 *   - dusk and night reach the pictures (the tint word of MODX_HR_IMAGE: H2);
 *   - a text says where it sits in its 1x box (left, centred: M2);
 *   - graphs, line plots and tables keep their words in their panel and apart (H4, H5, H6, M9);
 *   - toasts and banners have a lane of their own: never over the shop's tabs or page count, never over a
 *     game's field or its instruction, gone in time under the item card (H7, H8, H9, M3, M4, M5);
 *   - the town's signs stay on the screen (the audit's text-offscreen).
 * The module draws at display scale 2 into a recorder of its hr calls (nothing is drawn). Built and run by
 * `make test`. */
#include "mq_test_util.h"

#define FY (MQ_Y0 + 58)
#define FB (MQ_Y0 + MQ_H)

/* ---- the recorder */
typedef struct {
    char s[48];
    int st, align, x0, y0, x1, y1; /* logical */
    int32_t px16, pw16;            /* the box's left and width as given (physical x 16) */
} rtext_t;
static int measure_on; /* MODX_HR_MEASURE answers: a made-up font, 6 logical pixels a character */
typedef struct {
    char id[32];
    int nv, x, y, w, h;
} rspr_t;
static rtext_t txt[1200];
static int ntxt;
static rspr_t spr[600];
static int nspr;
static int img_tint[600], nimg;
static int shp[2000][4], nshp; /* shapes' boxes (logical) */
static int K = 2;

static int lg(int32_t v16) { return (int)((v16 / 16) / K); }
static int rec_hr(int op, const int32_t *a, int n, const void *p)
{
    switch (op) {
    case MODX_HR_TEXT:
        if (ntxt < 1200 && p && n >= 8) {
            rtext_t *t = &txt[ntxt++];
            snprintf(t->s, sizeof t->s, "%s", (const char *)p);
            t->st = MODX_HR_TEXT_STYLE(a[0]), t->align = MODX_HR_TEXT_ALIGN(a[0]);
            t->x0 = lg(a[4]), t->y0 = lg(a[5]), t->x1 = lg(a[4] + a[6]), t->y1 = lg(a[5] + a[7]);
            t->px16 = a[4], t->pw16 = a[6];
        }
        break;
    case MODX_HR_MEASURE:
        if (measure_on && p && n >= 2) {
            int chars = 0;
            for (const unsigned char *c = p; *c; c++)
                chars += (*c & 0xC0) != 0x80; /* (a character: not a UTF-8 continuation byte) */
            return chars * 6 * 16;
        }
        break;
    case MODX_HR_SPRITE:
        if (nspr < 600 && p && n >= 14) {
            rspr_t *s = &spr[nspr++];
            snprintf(s->id, sizeof s->id, "%s", (const char *)p);
            s->nv = a[11] - 2, s->x = lg(a[4]), s->y = lg(a[5]), s->w = a[12], s->h = a[13];
        }
        break;
    case MODX_HR_IMAGE:
        if (nimg < 600)
            img_tint[nimg++] = n >= 13 ? a[12] : -1;
        break;
    case MODX_HR_SHAPE:
        if (nshp < 2000 && n >= 7) {
            int *b = shp[nshp++];
            if (a[0] == MODX_SH_POLY) {
                int np = a[4];
                b[0] = b[1] = 1 << 20, b[2] = b[3] = -(1 << 20);
                for (int i = 0; i < np && 5 + 2 * i + 1 < n; i++) {
                    int x = lg(a[5 + 2 * i]), y = lg(a[6 + 2 * i]);
                    b[0] = x < b[0] ? x : b[0], b[2] = x > b[2] ? x : b[2], b[1] = y < b[1] ? y : b[1], b[3] = y > b[3] ? y : b[3];
                }
            } else if (a[0] == MODX_SH_CIRCLE || a[0] == MODX_SH_RING) {
                int r = lg(a[6]);
                b[0] = lg(a[4]) - r, b[2] = lg(a[4]) + r, b[1] = lg(a[5]) - r, b[3] = lg(a[5]) + r;
            } else
                nshp--;
        }
        break;
    default: break;
    }
    return 0; /* (the module draws everything itself as well) */
}
static void nop_fill(int x, int y, int w, int h, uint16_t c) { (void)x, (void)y, (void)w, (void)h, (void)c; }
static void nop_line(int x0, int y0, int x1, int y1, uint16_t c) { (void)x0, (void)y0, (void)x1, (void)y1, (void)c; }
static int nop_text(const font_t *f, int x, int y, const char *s, uint16_t fg, int bg)
{
    (void)x, (void)y, (void)fg, (void)bg;
    return gfx_text_width(f, s);
}
static int rec_scale(void) { return K; }
static uint32_t fake_ms(void) { return 1; } /* (time is `mq step`'s only: the drawing never moves it) */

static modx_api_t hapi;
static void hr_module(void)
{
    hapi = modx_api;
    hapi.fill_rect = nop_fill, hapi.rect = nop_fill, hapi.line = nop_line, hapi.text = nop_text;
    hapi.display_scale = rec_scale, hapi.hr = rec_hr, hapi.millis = fake_ms;
    memset(q, 0, mq_module_rw_size);
    memset(&ops, 0, sizeof ops);
    CHECK(mq_module_init(&hapi, q, &ops) == 0);
    CHECK(q->scale == K);
    q->fun_ms = 0, q->rng = 0x2545F491u, q->scenes_off = true;
}
static void frame(void)
{
    int r[4];
    ntxt = nspr = nimg = nshp = 0;
    q->redraw = true;
    q->gm.hud_ok = 0;
    ops.draw(q, 0, r);
}
static const rtext_t *find_text(const char *s, bool prefix)
{
    for (int i = 0; i < ntxt; i++)
        if (prefix ? !strncmp(txt[i].s, s, strlen(s)) : !strcmp(txt[i].s, s))
            return &txt[i];
    return NULL;
}
static bool meets(int ax0, int ay0, int ax1, int ay1, int bx0, int by0, int bx1, int by1)
{
    return ax0 < bx1 && bx0 < ax1 && ay0 < by1 && by0 < ay1;
}

/* ---- every sprite id names one thing: one parameter count wherever it is drawn */
static char sig_id[160][32];
static int sig_nv[160], nsig, sig_bad;
static void sig_take(void)
{
    for (int i = 0; i < nspr; i++) {
        int k = 0;
        while (k < nsig && strcmp(sig_id[k], spr[i].id))
            k++;
        if (k == nsig && nsig < 160)
            snprintf(sig_id[nsig], sizeof sig_id[0], "%s", spr[i].id), sig_nv[nsig++] = spr[i].nv;
        else if (k < nsig && sig_nv[k] != spr[i].nv && sig_bad++ < 4)
            fprintf(stderr, "sprite id %s: %d and %d parameters (two things, one name)\n", spr[i].id, sig_nv[k], spr[i].nv);
    }
}
static bool drew(const char *id)
{
    for (int i = 0; i < nspr; i++)
        if (!strcmp(spr[i].id, id))
            return true;
    return false;
}
static void test_sprite_ids(void)
{
    hr_module();
    con("new Mia 2"), con("grant all"), con("opengames"), con("go map"), con("step 10");
    bool castle = false, sky = false;
    for (int tod = 0; tod < 3; tod++) {
        char b[32];
        snprintf(b, sizeof b, "playtime %d", tod == 0 ? 0 : tod == 1 ? 12 : 15);
        con(b), con("step 2");
        for (int x = 20; x <= 1400; x += 115) {
            snprintf(b, sizeof b, "town %d", x);
            con(b), con("step 2");
            frame();
            sig_take();
            castle |= drew("town/castle"), sky |= drew("town/sky");
        }
    }
    CHECK(castle && sky); /* the Sky Castle has its own name, the sky its own */
    con("scene off"), con("grant bosses"), con("engine stop");
    static const char *mech[] = {"Bubble Pop", "Cloud Catch", "Gate Dash", "Kart Race", "Peekaboo", "Sort Slide", "Hop Along",
                                 "Sky Fling", "Pie Shop", "Build It", "Clock Tower", "Shop Till", "Treasure Dig", "Cake Kitchen"};
    static const int sk[] = {17, 17, 17, 17, 17, 14, 15, 25, 29, 3, 38, 45, 70, 0};
    for (int m = 0; m < (int)(sizeof mech / sizeof mech[0]); m++)
        for (int w = 0; w < MQ_NWORLD; w++) {
            char b[48];
            snprintf(b, sizeof b, "skill %d 1", sk[m]);
            con(b);
            char *argv[4] = {"mq", "mech", (char *)mech[m], NULL};
            ops.console(q, 3, argv);
            q->world = (uint8_t)w;
            con("step 20");
            frame();
            sig_take();
        }
    CHECK(sig_bad == 0);
    CHECK(nsig > 30);
    fresh_module();
}

/* ---- the town at night: its pictures say so (and by day they do not) */
static void test_image_tint(void)
{
    hr_module();
    con("new Mia 2"), con("grant all"), con("playtime 15"), con("go map"), con("step 10");
    int night = 0, other = 0;
    for (int x = 20; x <= 1400; x += 230) {
        char b[24];
        snprintf(b, sizeof b, "town %d", x);
        con(b), con("step 2"), frame();
        for (int i = 0; i < nimg; i++)
            night += img_tint[i] == TINT_NIGHT, other += img_tint[i] < 0;
    }
    CHECK(night > 0 && other == 0);
    con("playtime 0"), con("step 2"), frame();
    int day = 0;
    for (int i = 0; i < nimg; i++)
        day += img_tint[i] != 0;
    CHECK(nimg > 0 && day == 0);
    fresh_module();
}

/* ---- alignment: the header's title is left, a button's label centred */
static void test_text_align(void)
{
    hr_module();
    con("new Mia 2"), con("coins 300"), con("go shop"), con("step 4"), frame();
    const rtext_t *t = find_text("Store", false), *b = find_text("Decor", false);
    CHECK(t && t->align == MODX_HR_ALIGN_LEFT);
    CHECK(b && b->align == MODX_HR_ALIGN_CENTRE);
    fresh_module();
}

/* ---- the counter's line on a platform with a font of its own: "That's " "12c" "!" set side by side by the
 * platform's widths (MODX_HR_MEASURE), the amount one run with a real cent sign -- no gap (the Mac's
 * "That's   12c !") */
static const rtext_t *find_sub(const char *s)
{
    for (int i = 0; i < ntxt; i++)
        if (strstr(txt[i].s, s))
            return &txt[i];
    return NULL;
}
static void test_counter_runs(void)
{
    hr_module();
    con("new Mia 2"), con("coins 300"), con("go shop"), con("pay setup 0 0"), con("pay open Heart_chair");
    con("step 4");
    measure_on = 1;
    frame();
    const rtext_t *a = find_text("That's ", false), *v = find_sub("12\xC2\xA2"), *b = find_text("!", false);
    CHECK(a && v && b);
    if (a && v && b) {
        CHECK(a->pw16 == 7 * 6 * 16 * K && v->pw16 == 3 * 6 * 16 * K && b->pw16 == 6 * 16 * K); /* the platform's widths */
        CHECK(a->px16 + a->pw16 == v->px16 && v->px16 + v->pw16 == b->px16);                /* side by side */
        CHECK(a->align == MODX_HR_ALIGN_LEFT);
        int mid = (a->px16 + b->px16 + b->pw16) / 2 / 16 / K; /* centred where the 1x line is (the bubble's middle) */
        CHECK(mid >= 181 && mid <= 183);
    }
    measure_on = 0; /* without the platform's widths: the 1x layout, as before */
    frame();
    a = find_text("That's ", false);
    CHECK(a && !find_sub("\xC2\xA2"));
    fresh_module();
    /* the change: "Your change: $0.75", the amount right after the words by the platform's widths (a fixed
     * column left a gap on the Mac) */
    hr_module();
    con("new Mia 2"), con("coins 300"), con("go shop"), con("pay setup 2 0 0"), con("pay open Bookshelf"), con("pay put 1");
    con("pay go"), con("step 4");
    measure_on = 1;
    frame();
    const rtext_t *l = find_text("Your change:", false), *m = find_text("$0.00", false);
    CHECK(l && m);
    if (l && m) {
        CHECK(l->pw16 == 12 * 6 * 16 * K);                  /* the platform's width of the words */
        CHECK(m->px16 == l->px16 + l->pw16 + 5 * 16 * K); /* the amount just after them */
        if (m->px16 != l->px16 + l->pw16 + 5 * 16 * K)
            fprintf(stderr, "change: '%s' at %d+%d, '%s' at %d\n", l->s, (int)l->px16, (int)l->pw16, m->s, (int)m->px16);
    }
    measure_on = 0;
    fresh_module();
}

/* ---- the game's card: a graph, a line plot, a table, with every rung and two seeds */
static void card_skill(int sk, int rung, int seed)
{
    char b[32];
    con("new Mia 2"), con("engine stop"), con("grant bosses");
    snprintf(b, sizeof b, "seed %d", seed), con(b);
    snprintf(b, sizeof b, "skill %d %d", sk, rung), con(b);
    con("step 22"), con("step 60");
    frame();
}
static void test_cards(void)
{
    hr_module();
    con("opengames");
    int bad = 0;
    /* (a long name in the table: a theme pack's cast) */
    static const int sks[] = {S_GRAPH1, S_SCALED, S_LINEPLOT, S_LINEPLOT5, S_TABLE};
    for (int k = 0; k < 5; k++)
        for (int d = 0; d < mq_rungs(sks[k]); d++)
            for (int sd = 1; sd <= 2; sd++) {
                card_skill(sks[k], d, sd);
                if (!q->prob.vis)
                    continue;
                const mq_prob_t *p = &q->prob;
                /* every word of the card in the card (x 2..116), apart from every other */
                for (int i = 0; i < ntxt; i++) {
                    const rtext_t *t = &txt[i];
                    if (t->y0 < FY || t->y1 > FB + 1 || t->x0 > 116)
                        continue;
                    if (t->x0 < 2 || t->x1 > 117) {
                        if (bad++ < 6)
                            fprintf(stderr, "card skill %d rung %d: \"%s\" leaves the card (%d..%d)\n", sks[k], d, t->s, t->x0, t->x1);
                    }
                    for (int j = i + 1; j < ntxt; j++) {
                        const rtext_t *u = &txt[j];
                        if (u->x0 > 116 || !strcmp(t->s, u->s) || u->y0 < FY)
                            continue;
                        if (meets(t->x0, t->y0 + 2, t->x1, t->y1 - 2, u->x0, u->y0 + 2, u->x1, u->y1 - 2) && bad++ < 6)
                            fprintf(stderr, "card skill %d rung %d: \"%s\" and \"%s\" overlap\n", sks[k], d, t->s, u->s);
                    }
                }
                /* the shapes of the card (stars, crosses' marks) stay in it too */
                for (int i = 0; i < nshp; i++)
                    if ((shp[i][0] + shp[i][2]) / 2 < 110 && shp[i][1] >= FY && shp[i][3] <= FB && shp[i][2] > 117 && bad++ < 6)
                        fprintf(stderr, "card skill %d rung %d: a shape leaves the card (%d..%d)\n", sks[k], d, shp[i][0], shp[i][2]);
                if ((sks[k] == S_SCALED || sks[k] == S_GRAPH1) && p->v[6]) { /* the key has its number (H4) */
                    char key[16];
                    snprintf(key, sizeof key, "= %d", p->v[1] > 1 ? p->v[1] : 1);
                    if (!find_text(key, false) && bad++ < 6)
                        fprintf(stderr, "card skill %d rung %d: no key \"%s\"\n", sks[k], d, key);
                }
                if (sks[k] == S_TABLE) { /* names left of the column line (H6): the longest name a theme pack has room for */
                    mq_prob_t tp = q->prob;
                    q->theme.loaded = true, q->theme.nspr = 1, q->theme.role[0] = 2;
                    snprintf(q->theme.names[0], sizeof q->theme.names[0], "%s", "WWWWWWWWWWW");
                    ntxt = 0;
                    mq_visual(q, &tp, 4, FY + 4, 110, FB - FY - 10, 1000);
                    q->theme.loaded = false;
                    CHECK(find_text("WWW", true) != NULL);
                    for (int i = 0; i < ntxt; i++)
                        if (txt[i].y0 >= FY && txt[i].x0 < 60 && txt[i].x1 > 4 + 110 / 2 - 1 && bad++ < 6)
                            fprintf(stderr, "table: \"%s\" crosses into the numbers (%d)\n", txt[i].s, txt[i].x1);
                }
            }
    CHECK(bad == 0);
    fresh_module();
}

/* ---- toasts: the shop's lane is the header's middle; one at a time; gone in time with the card open */
static void steps(int n)
{
    char b[24];
    snprintf(b, sizeof b, "step %d", n);
    con(b);
}
static void test_toasts(void)
{
    hr_module();
    con("new Mia 2"), con("fun 36000"), con("coins 300"), con("go shop"), steps(1), frame();
    mq_toast(q, "New! Bouncy ball");
    mq_toast(q, "Pip moved into town!");
    mq_toast(q, "New! A coin bag");
    int seen = 0, bad = 0;
    for (int f = 0; f < 400; f++) { /* 400 frames: all three, one after the other, then none */
        steps(1), frame();
        const rtext_t *tab = find_text("Pets", false), *title = find_text("Store", false);
        for (int i = 0; i < ntxt; i++) {
            const rtext_t *t = &txt[i];
            bool toast = !strcmp(t->s, "New! Bouncy ball") || !strcmp(t->s, "Pip moved into town!") || !strcmp(t->s, "New! A coin bag");
            if (!toast)
                continue;
            seen |= !strcmp(t->s, "New! Bouncy ball") ? 1 : !strcmp(t->s, "Pip moved into town!") ? 2 : 4;
            if (t->y1 > MQ_Y0 + 20 || (tab && meets(t->x0, t->y0, t->x1, t->y1, tab->x0, tab->y0, tab->x1, tab->y1)) ||
                (title && meets(t->x0, t->y0, t->x1, t->y1, title->x0, title->y0, title->x1, title->y1))) {
                if (bad++ < 4)
                    fprintf(stderr, "toast \"%s\" at %d,%d..%d,%d: over the header's words or the tabs\n", t->s, t->x0, t->y0, t->x1, t->y1);
            }
        }
    }
    CHECK(seen == 7 && bad == 0);
    /* a toast stays its time whatever the clock says when it comes up (an even millisecond: its stamp once
     * ran a millisecond ahead of the clock, and it went at once) */
    for (int odd = 0; odd < 2; odd++) {
        q->now = (q->now & ~1u) + (uint32_t)odd;
        mq_toast(q, "Stays a while!");
        steps(1), frame();
        steps(30), frame();
        CHECK(find_text("Stays a while!", false) != NULL);
        steps((int)(mq_toast_ms(q) / MQ_FRAME_MS) + 20);
    }
    if (seen != 7)
        fprintf(stderr, "toasts seen: %d (scene %d, now %s)\n", seen, q->scene, q->earn_msg);
    CHECK(!mq_toast_busy(q));
    /* bought: "You got a ...!" with the item card open goes after its time (H8) */
    q->shop_pick = 1;
    mq_toast(q, "You got a Heart bed!");
    steps(1), frame();
    CHECK(find_text("You got a Heart bed!", false) != NULL);
    steps((int)(mq_toast_ms(q) / MQ_FRAME_MS) + 20), frame();
    CHECK(find_text("You got a Heart bed!", false) == NULL);
    q->shop_pick = 0;
    /* the new set's banner and the page count side by side (M4) */
    q->new_set = 1;
    steps(1), frame();
    const rtext_t *banner = find_text("New: the", true);
    int pc = -1;
    for (int i = 0; i < ntxt; i++)
        if (strchr(txt[i].s, '/') && txt[i].y0 > MQ_Y0 + 180)
            pc = i;
    CHECK(banner && pc >= 0);
    if (banner && pc >= 0)
        CHECK(!meets(banner->x0 - 4, banner->y0, banner->x1 + 4, banner->y1, txt[pc].x0, txt[pc].y0, txt[pc].x1, txt[pc].y1));
    fresh_module();
}

/* ---- a game: the twist's name, a toast, the combo's words and "+N" stay off the field (M3, M5, H9) */
static void test_game_lane(void)
{
    hr_module();
    con("new Mia 2"), con("scene off"), con("opengames"), con("grant bosses"), con("engine stop");
    con("skill 17 1");
    char *argv[4] = {"mq", "mech", "Bubble Pop", NULL};
    ops.console(q, 3, argv);
    con("twist 1"), steps(2), frame();
    const rtext_t *t = find_text("Night mode!", false);
    if (!t)
        for (int i = 0; i < ntxt; i++)
            if (txt[i].y0 < FY)
                fprintf(stderr, "top: \"%s\" y %d (scene %d sub %d mod %d)\n", txt[i].s, txt[i].y0, q->scene, q->sub, mq_mod_now(q));
    CHECK(t && t->y1 <= MQ_Y0 + 16); /* in the top line */
    steps(260); /* the demo and the goal said (their reading time), then a toast during play: in the top line, not over the instruction */
    mq_toast(q, "Pip moved into town!");
    steps(1), frame();
    t = find_text("Pip moved into town!", false);
    CHECK(t && t->y1 <= MQ_Y0 + 16);
    for (int i = 0; t && i < ntxt; i++)
        if (&txt[i] != t && txt[i].y1 <= MQ_Y0 + 17 && meets(t->x0, t->y0, t->x1, t->y1, txt[i].x0, txt[i].y0, txt[i].x1, txt[i].y1))
            CHECK(!"the toast shares the top line with other words");
    /* right answers: the combo's words in the order's band, "+N" in the top line; nothing new over the field */
    steps((int)(mq_toast_ms(q) / MQ_FRAME_MS) + 20);
    int bad = 0;
    for (int k = 0; k < 4; k++) {
        con("correct");
        for (int f = 0; f < 30; f++) {
            steps(1), frame();
            for (int i = 0; i < ntxt; i++) {
                const rtext_t *u = &txt[i];
                bool pop = (u->s[0] == '+' && strstr(u->s, "stars")) || strstr(u->s, " x") != NULL;
                if (pop && u->y1 > FY && bad++ < 4)
                    fprintf(stderr, "\"%s\" over the field (y %d)\n", u->s, u->y0);
            }
        }
        steps(40);
    }
    CHECK(bad == 0);
    con("twist 0");
    fresh_module();
}

/* ---- the town's signs: on the screen, or not drawn */
static void test_town_signs(void)
{
    hr_module();
    con("new Mia 2"), con("grant all"), con("go map"), steps(10);
    int bad = 0;
    for (int x = 0; x <= 1500; x += 17) {
        char b[24];
        snprintf(b, sizeof b, "town %d", x);
        con(b), steps(2), frame();
        for (int i = 0; i < ntxt; i++)
            if ((txt[i].x0 < 0 || txt[i].x1 > MQ_W) && bad++ < 4)
                fprintf(stderr, "town %d: \"%s\" off the screen (%d..%d)\n", x, txt[i].s, txt[i].x0, txt[i].x1);
    }
    CHECK(bad == 0);
    fresh_module();
}

/* ---- every teaching moment's lines fit the screen whole (the bot's 12: 336 px sentences at x 10) */
static void test_teaching_text(void)
{
    hr_module();
    con("new Mia 2"), con("level 0 0"), steps(2);
    int bad = 0;
    for (int idea = 0; idea < IDEA_COUNT; idea++) {
        char b[24];
        snprintf(b, sizeof b, "teach %d", idea);
        con(b);
        for (int k = 0; k < 24 && mq_teach_on(q); k++) {
            steps(2), frame();
            for (int i = 0; i < ntxt; i++)
                if ((txt[i].x1 > MQ_W - 2 || (strstr(txt[i].s, "...") && !strstr(txt[i].s, " ..."))) && bad++ < 6) /* (cut: "word...") */
                    fprintf(stderr, "teaching %d: \"%s\" (%d..%d)\n", idea, txt[i].s, txt[i].x0, txt[i].x1);
            con("teach next");
        }
        con("teach done");
    }
    CHECK(bad == 0);
    fresh_module();
}

int main(void)
{
    harness_init();
    test_sprite_ids();
    test_image_tint();
    test_text_align();
    test_counter_runs();

    test_cards();
    test_toasts();
    test_game_lane();
    test_town_signs();
    test_teaching_text();
    printf("test_mq_render: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
