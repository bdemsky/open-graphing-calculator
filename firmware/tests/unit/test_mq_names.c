/* Sparkle Town (modules/mathquest): the name typesetting audit (backlog (f)). Every screen that says a name --
 * her own (up to 16 characters: "WWWWWWWWWWWWWWWW", "Zoe-Mae O'Briens", "Annabelle Rosali") or a friend's (the
 * built-in cast, and a theme pack's longest names: 11 characters, "MMMMMMMMMMM") -- is drawn, and every text
 * that holds a name must be whole on the screen, clear of every other text, and either the whole name or the
 * name cut with "..." (never a name cut silently). The screens: the title, the name entry, the town, every
 * world, an order in every world (its board, the friend's words), every friend's chapter, the arcade, the
 * stickers, the book, the shop, the room, the wardrobe, the show-off, the grown-ups' tabs, the toy box.
 * Built and run by `make test`. */
#include "mq_test_util.h"

typedef struct {
    int x, y, w, h;
    char s[64];
} tb_t;
static tb_t tb[160];
static int ntb;

static int log_text(const font_t *f, int x, int y, const char *s, uint16_t fg, int bg)
{
    if (ntb < 160 && s[0]) {
        tb_t *t = &tb[ntb++];
        t->x = x, t->y = y, t->w = modx_api.text_width(f, s), t->h = f->h;
        snprintf(t->s, sizeof t->s, "%s", s);
    }
    return modx_api.text(f, x, y, s, fg, bg);
}

static const char *names[] = {"WWWWWWWWWWWWWWWW", "Zoe-Mae O'Briens", "Annabelle Rosali"};
static int bad, checked, screens;

/* a text that holds a name: the whole of `nm`, or its first characters then "..." */
static int holds(const char *s, const char *nm)
{
    if (strstr(s, nm))
        return 2; /* whole */
    char pre[6];
    snprintf(pre, sizeof pre, "%.5s", nm);
    return strstr(s, pre) ? 1 : 0;
}

static void audit(const char *screen, const char *nm)
{
    int r[4];
    ntb = 0;
    ops.draw(q, 0, r);
    screens++;
    for (int i = 0; i < ntb; i++) {
        int h = holds(tb[i].s, nm);
        if (!h)
            continue;
        checked++;
        const tb_t *t = &tb[i];
        const char *why = NULL;
        if (t->x < 0 || t->x + t->w > 320 || t->y < 0 || t->y + t->h > 240)
            why = "leaves the screen";
        else if (h == 1 && !strstr(t->s, "..."))
            why = "cut without \"...\"";
        for (int j = 0; j < ntb && !why; j++)
            if (j != i && t->x + t->w - 1 > tb[j].x && tb[j].x + tb[j].w - 1 > t->x && t->y + t->h - 2 > tb[j].y &&
                tb[j].y + tb[j].h - 2 > t->y && tb[j].s[0] != ' ')
                why = tb[j].s;
        if (why && bad++ < 20)
            fprintf(stderr, "names: %s (\"%s\"): \"%s\" at %d,%d %dx%d: %s%s\n", screen, nm, t->s, t->x, t->y, t->w, t->h,
                    why[0] == 'l' || why[0] == 'c' ? "" : "over ", why);
    }
}

static void long_cast(bool on)
{
    q->theme.loaded = on;
    if (!on)
        return;
    q->theme.nspr = MQ_NCAST;
    for (int i = 0; i < MQ_NCAST; i++) {
        q->theme.role[i] = (uint8_t)i;
        snprintf(q->theme.names[i], sizeof q->theme.names[i], "%s", "MMMMMMMMMMM");
    }
}

static void go(const char *where, const char *nm)
{
    char b[48];
    snprintf(b, sizeof b, "go %s", where);
    con(b);
    con("step 30");
    audit(where, nm);
}

static void test_names(void)
{
    static modx_api_t api;
    api = modx_api, api.text = log_text;
    static const char *scenes[] = {"title", "name", "map", "arcade", "stickers", "book", "shop", "room", "wardrobe", "showoff",
                                   "parent", "toys", "merge"};
    for (int cast = 0; cast < 2; cast++)
        for (size_t n = 0; n < sizeof names / sizeof names[0]; n++) {
            memset(q, 0, mq_module_rw_size);
            memset(&ops, 0, sizeof ops);
            CHECK(mq_module_init(&api, q, &ops) == 0);
            q->rng = 0x2545F491u, q->scenes_off = true, q->fun_ms = 0;
            con("new X 2");
            mq_pname_set(q, q->save.cur, names[n]);
            con("grant all");
            long_cast(cast == 1);
            const char *nm = names[n];
            for (size_t i = 0; i < sizeof scenes / sizeof scenes[0]; i++)
                go(scenes[i], nm);
            for (int t = 0; t < 4; t++) { /* the grown-ups' tabs */
                char b[24];
                con("go parent");
                snprintf(b, sizeof b, "parent tab %d", t);
                con(b);
                con("step 4");
                audit("parent tab", nm);
            }
            for (int w = 0; w < MQ_NWORLD; w++) { /* every world and an order there: her name, the friends' */
                char b[24];
                snprintf(b, sizeof b, "world %d", w);
                con(b);
                con("step 20");
                audit("world", nm);
                audit("world", mq_cast_name(q, w < MQ_NCAST ? w : 0));
                snprintf(b, sizeof b, "level %d 0", w);
                con(b);
                con("step 60");
                audit("order", nm);
                for (int c = 0; c < MQ_NCAST; c++)
                    audit("order", mq_cast_name(q, c));
            }
            for (int c = 0; c < MQ_NCAST; c++) { /* every friend's chapter */
                char b[24];
                snprintf(b, sizeof b, "chapter %d", c);
                con(b);
                for (int l = 0; l < 6 && q->scene == SC_CHAPTER; l++) {
                    con("step 40");
                    audit("chapter", mq_cast_name(q, c));
                    audit("chapter", nm);
                    mq_chapter_next(q);
                }
            }
            long_cast(false);
        }
    printf("names: %d screens drawn, %d texts with a name, %d bad\n", screens, checked, bad);
    CHECK(bad == 0 && checked > 200);
}

int main(void)
{
    harness_init();
    test_names();
    printf("test_mq_names: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
