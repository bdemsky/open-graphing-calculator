/*
 * Math Quest: stories. Word problems are little dramas for the cast: a friend has a problem and a
 * feeling (a pout, a worry, a hurry), the player fixes it with a game action, and everyone cheers
 * and dances. Templates name cast roles, never cast members, so a theme pack's characters star in
 * them, with their favourite food, colour, hobby and thing (theme traits, docs/MATHQUEST.md).
 *
 * A template is expanded in two steps: the numbers with snprintf (each story kind has one fixed
 * argument list), then the placeholders:
 *   {A} {B}        the two friends of the story (A: the one being helped, B: a second friend)
 *   {fA} {FA}      A's favourite food, one / several (also {fB} {FB}); {kA} colour; {hA} hobby;
 *   {tA} {TA}      A's favourite thing, one / several; {pA} her / his / their; {oA} her / him / them
 *   {n:word}       word or its plural, agreeing with the last number written before it
 *   {a}            "a" or "an" for the word after it
 * The text is word-wrapped into the problem's three lines (font_small, 300 pixels); tests expand
 * every template with the longest names and traits and check the fit, grammar and kid words.
 * Feelings stay mild (a pout, a worry, a hurry) and every story ends happily.
 */
#include "mq.h"

/* ------------------------------------------------------------------ traits */
/* the built-in cast: food, colour, hobby, thing (singular nouns; plurals are made) and pronouns */
static const char bi_trait[MQ_NCAST][TR_COUNT][16] = {
    {"strawberry", "green", "planting seeds", "watering can"}, /* Pip */
    {"cracker", "blue", "sliding on ice", "postcard"},         /* Dot */
    {"cupcake", "pink", "baking", "whisk"},                    /* Momo */
    {"muffin", "purple", "reading", "storybook"},              /* Olli */
    {"cookie", "orange", "flying kites", "kite"},              /* Tango */
    {"lemon tart", "yellow", "painting", "paintbrush"},        /* Dabble */
    {"apple", "red", "gardening", "lantern"},                  /* Bramble */
    {"lettuce leaf", "teal", "racing", "scarf"},               /* Shelly */
    {"star cookie", "silver", "stargazing", "telescope"},      /* Nova */
};
static const uint8_t bi_pron[MQ_NCAST] = {PR_SHE, PR_HE, PR_SHE, PR_HE, PR_HE, PR_SHE, PR_SHE, PR_SHE, PR_THEY};

static const char generic[TR_COUNT][16] = {"cookie", "pink", "playing", "teddy bear"};

const char *mq_trait(mq_t *q, int slot, int tr)
{
    slot = slot >= 0 && slot < MQ_NCAST ? slot : 0;
    tr = tr >= 0 && tr < TR_COUNT ? tr : 0;
    if (q->theme.loaded && q->theme.trait[slot][tr][0])
        return q->theme.trait[slot][tr];
    if (q->theme.loaded && q->theme.has_traits) /* a pack with traits: its own cast, generic words */
        return generic[tr];
    return bi_trait[slot][tr];
}

int mq_pronoun(mq_t *q, int slot)
{
    slot = slot >= 0 && slot < MQ_NCAST ? slot : 0;
    if (q->theme.loaded && q->theme.pron[slot])
        return q->theme.pron[slot];
    if (q->theme.loaded && q->theme.has_traits)
        return PR_THEY;
    return bi_pron[slot];
}

/* ------------------------------------------------------------------ grammar */
static int slen(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}
static bool ends(const char *w, int n, const char *suf)
{
    int k = slen(suf);
    if (k > n)
        return false;
    for (int i = 0; i < k; i++)
        if ((w[n - k + i] | 32) != suf[i])
            return false;
    return true;
}
static bool vowel(char c)
{
    c = (char)(c | 32);
    return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u';
}

/* the plural of a noun phrase (its last word): cookie -> cookies, strawberry -> strawberries,
 * peach -> peaches, leaf -> leaves, mouse -> mice, sheep -> sheep */
void mq_plural(const char *w, char *out, int n)
{
    static const char irr[][2][10] = {{"child", "children"}, {"mouse", "mice"}, {"person", "people"},
                                      {"foot", "feet"},      {"tooth", "teeth"}, {"goose", "geese"},
                                      {"sheep", "sheep"},    {"fish", "fish"},   {"deer", "deer"},
                                      {"leaf", "leaves"},    {"loaf", "loaves"}, {"shelf", "shelves"},
                                      {"knife", "knives"},   {"wolf", "wolves"}, {"potato", "potatoes"},
                                      {"tomato", "tomatoes"}, {"mango", "mangoes"}, {"candy", "candies"}};
    int len = slen(w), k = 0;
    if (len + 4 >= n) { /* no room: as it is */
        for (; k < n - 1 && w[k]; k++)
            out[k] = w[k];
        out[k] = 0;
        return;
    }
    int start = len; /* the last word */
    while (start > 0 && w[start - 1] != ' ')
        start--;
    for (k = 0; k < len; k++)
        out[k] = w[k];
    out[len] = 0;
    for (size_t i = 0; i < sizeof irr / sizeof irr[0]; i++) {
        int il = slen(irr[i][0]);
        if (len - start == il && ends(w, len, irr[i][0])) {
            int j = 0;
            for (; irr[i][1][j] && start + j < n - 1; j++)
                out[start + j] = irr[i][1][j];
            out[start + j] = 0;
            return;
        }
    }
    if (len >= 2 && (w[len - 1] | 32) == 'y' && !vowel(w[len - 2]))
        out[len - 1] = 'i', out[len] = 'e', out[len + 1] = 's', out[len + 2] = 0;
    else if (ends(w, len, "s") || ends(w, len, "x") || ends(w, len, "z") || ends(w, len, "ch") || ends(w, len, "sh"))
        out[len] = 'e', out[len + 1] = 's', out[len + 2] = 0;
    else
        out[len] = 's', out[len + 1] = 0;
}

/* Expand the placeholders of `in` for friends a and b into out (n bytes). */
void mq_story_expand(mq_t *q, const char *in, int a, int b, char *out, int n)
{
    static const char pron[3][3][6] = {{"she", "her", "her"}, {"he", "him", "his"}, {"they", "them", "their"}};
    int o = 0;
    int32_t last = 2; /* the last number written */
    bool an_next = false;
    int an_at = -1;
    for (const char *s = in; *s && o < n - 1;) {
        char tmp[40];
        const char *put = NULL;
        if (*s >= '0' && *s <= '9') {
            last = 0;
            while (*s >= '0' && *s <= '9' && o < n - 1)
                last = last * 10 + (*s - '0'), out[o++] = *s++;
            if (*s == '/' || *s == '.' || *s == ':') /* fractions, decimals, times: plural */
                last = 2;
            continue;
        }
        if (*s != '{') {
            out[o++] = *s++;
            if (an_next && out[o - 1] != ' ') { /* the word after {a} has started */
                bool v = vowel(out[o - 1]);
                if (v && an_at >= 0 && o < n - 1) { /* "a" -> "an" */
                    for (int k = o; k > an_at + 1; k--)
                        out[k] = out[k - 1];
                    out[an_at + 1] = 'n';
                    o++;
                }
                an_next = false;
            }
            continue;
        }
        const char *e = s + 1;
        while (*e && *e != '}')
            e++;
        if (!*e)
            break;
        int len = (int)(e - s - 1);
        const char *t = s + 1;
        int who = len >= 2 && t[len - 1] == 'B' ? b : a;
        char c0 = t[0];
        if (len == 1 && (c0 == 'A' || c0 == 'B'))
            put = mq_cast_name(q, c0 == 'A' ? a : b);
        else if (len == 1 && c0 == 'a') {
            if (o < n - 1)
                out[o++] = 'a';
            an_at = o - 1, an_next = true;
            s = e + 1;
            continue;
        } else if (len == 2 && (c0 == 'f' || c0 == 'k' || c0 == 'h' || c0 == 't'))
            put = mq_trait(q, who, c0 == 'f' ? TR_FOOD : c0 == 'k' ? TR_COLOUR : c0 == 'h' ? TR_HOBBY : TR_THING);
        else if (len == 2 && (c0 == 'F' || c0 == 'T')) {
            mq_plural(mq_trait(q, who, c0 == 'F' ? TR_FOOD : TR_THING), tmp, sizeof tmp);
            put = tmp;
        } else if (len == 2 && (c0 == 's' || c0 == 'o' || c0 == 'p')) {
            int pr = mq_pronoun(q, who);
            put = pron[pr > 0 && pr <= 3 ? pr - 1 : 2][c0 == 's' ? 0 : c0 == 'o' ? 1 : 2];
        } else if (len > 2 && c0 == 'n' && t[1] == ':') {
            int k = 0;
            for (; k < len - 2 && k < (int)sizeof tmp - 1; k++)
                tmp[k] = t[2 + k];
            tmp[k] = 0;
            if (last != 1) {
                char pl[40];
                mq_plural(tmp, pl, sizeof pl);
                for (k = 0; pl[k]; k++)
                    tmp[k] = pl[k];
                tmp[k] = 0;
            }
            put = tmp;
        }
        if (put)
            for (int k = 0; put[k] && o < n - 1; k++) {
                out[o++] = put[k];
                if (an_next && put[k] != ' ') {
                    if (vowel(put[k]) && an_at >= 0 && o < n - 1) {
                        for (int j = o; j > an_at + 1; j--)
                            out[j] = out[j - 1];
                        out[an_at + 1] = 'n';
                        o++;
                    }
                    an_next = false;
                }
            }
        s = e + 1;
    }
    out[o] = 0;
}

/* word-wrap s into the problem's three lines (font_small, `width` pixels); returns the lines used
 * (4 = it did not fit) */
int mq_story_wrap(mq_t *q, const char *s, char lines[3][MQ_TXT], int width)
{
    const modx_api_t *api = q->api;
    for (int i = 0; i < 3; i++)
        lines[i][0] = 0;
    int line = 0, len = 0;
    while (*s == ' ')
        s++;
    while (*s) {
        int wl = 0;
        while (s[wl] && s[wl] != ' ')
            wl++;
        if (line >= 3)
            return 4;
        char test[MQ_TXT * 2];
        int tl = 0;
        for (int k = 0; k < len; k++)
            test[tl++] = lines[line][k];
        if (len)
            test[tl++] = ' ';
        for (int k = 0; k < wl && tl < (int)sizeof test - 1; k++)
            test[tl++] = s[k];
        test[tl] = 0;
        if ((api->text_width(api->font_small, test) > width || tl >= MQ_TXT) && len) {
            line++, len = 0;
            continue; /* the word again, on the next line */
        }
        if (tl >= MQ_TXT)
            return 4;
        for (int k = 0; k <= tl; k++)
            lines[line][k] = test[k];
        len = tl;
        s += wl;
        while (*s == ' ')
            s++;
    }
    return line + 1;
}

/* ------------------------------------------------------------------ the stories */
/* Each kind: templates (the problem, the feeling, the ask) and one happy ending each. The numbers
 * each kind's templates take, in order, are in the comment. `sad` marks templates where B pouts
 * until it is fixed. */
typedef struct {
    char set[132];
    char fix[48];
    uint8_t sad;
} mq_tale_t;

#define NT 5
static const mq_tale_t tales[ST_COUNT][NT] = {
    [ST_GROUPS] = { /* a groups of b: how many in all? (a, b) */
        {"{A} is baking for a party: %d trays with %d {FA} on each. How many {FA} in all?", "Enough for everyone!", 0},
        {"{A} sets up a picnic: %d blankets with %d friends on each. How many friends came?", "What a big picnic!", 0},
        {"{A} packs sleepover bags: %d bags with %d {kA} stickers in each. How many stickers?", "Best sleepover ever!", 0},
        {"{A} plants %d rows of flowers for {B}, %d in each row. How many flowers?", "{B} loves the garden!", 0},
        {"{A} makes %d bracelets with %d beads each for friends. How many beads?", "Sparkly bracelets!", 0}},
    [ST_SHARE] = { /* n shared fairly by k: how many each? (n, k) */
        {"{A} baked %d {FA} for %d friends. {B} got fewer and pouts! Make it fair: how many {FA} each?", "Fair shares! Hugs all round.", 1},
        {"{A} has %d {kA} beads for %d friends. {B} worries it isn't fair. How many beads each?", "Everyone gets the same!", 1},
        {"{A} cut a pizza into %d slices for %d friends. {B} looks sad. Same for all: how many slices each?", "Pizza party! Yum!", 1},
        {"{A} has %d seeds for %d pots. {B} says one pot looks lonely! How many seeds in each pot?", "Every pot is happy!", 1},
        {"%d {FB} for %d friends at {A}'s sleepover. {B} wants it fair! How many {FB} each?", "Fair and square!", 1}},
    [ST_HOWMANY] = { /* n into groups of k: how many groups? (n, k) */
        {"{A} has %d {FA} and puts %d in each gift box. How many boxes can {A} fill?", "So many presents!", 0},
        {"{A} has %d strawberries and puts %d in each basket. How many baskets?", "Baskets for everyone!", 0},
        {"%d friends come to {A}'s party, %d at each table. How many tables are needed?", "Party time!", 0},
        {"{A} makes %d cards and puts %d in each envelope. How many envelopes?", "Mail time!", 0},
        {"{A} has %d stickers, %d on each page of {pA} book. How many pages fill up?", "What a sticker book!", 0}},
    [ST_PERONE] = { /* n shared by k: how many on one? (n, k) */
        {"%d ladybugs sit on %d leaves, the same on each. {B} wonders: how many on one leaf?", "Ladybug party!", 0},
        {"{A} puts %d candles on %d cakes, the same on each. How many on one cake?", "Happy birthday!", 0},
        {"%d balloons are tied to %d chairs, the same on each. How many on one chair?", "So colourful!", 0},
        {"{A} hangs %d lights on %d windows, the same on each. How many on one window?", "The house glows!", 0},
        {"%d marbles fill %d jars, the same in each. {B} asks: how many in one jar?", "Shiny marbles!", 0}},
    [ST_TIMES] = { /* B has a, A has b times as many: how many for A? (a, b) */
        {"{B} found %d shells. {A} found %d times as many! How many did {A} find?", "What a beach day!", 0},
        {"{B} has %d stickers. {A} has %d times as many. How many stickers does {A} have?", "Sticker swap time!", 0},
        {"{B} baked %d {FB}. {A} baked %d times as many! How many did {A} bake?", "A big bake sale!", 0},
        {"{B} picked %d flowers. {A} picked %d times as many for a party. How many flowers?", "Pretty bunches!", 0},
        {"{B} jumped %d times. {A} jumped %d times as many! How many jumps?", "Boing boing!", 0}},
    [ST_BUS] = { /* n friends, k per car: how many cars, nobody left behind? (n, k) */
        {"%d friends go to {A}'s party by bus. Each bus fits %d. Nobody stays behind! How many buses?", "Everyone made it!", 1},
        {"%d friends ride the garden train, %d in each car. Everyone must ride! How many cars?", "All aboard!", 1},
        {"%d friends sleep over at {A}'s. Each tent fits %d. How many tents so nobody is left out?", "Cosy tents!", 1},
        {"{A} bakes %d {FA}. Each tray holds %d. Every one must bake! How many trays?", "All baked!", 1},
        {"%d friends go boating, %d in each boat. Nobody stays on shore! How many boats?", "Splash!", 1}},
    [ST_FULL] = { /* n into boxes of k: how many full boxes? (n, k) */
        {"{A} packs %d {FA} in boxes of %d for gifts. Only full boxes go! How many full boxes?", "Gifts are ready!", 0},
        {"{A} has %d eggs and fills cartons of %d. How many cartons are full?", "Breakfast is saved!", 0},
        {"{A} ties %d balloons in bunches of %d. How many full bunches?", "Balloon bunches!", 0},
        {"{A} has %d crayons, %d in each box. How many boxes are full?", "Colouring time!", 0},
        {"{A} puts %d books on shelves, %d on each. How many shelves are full?", "A tidy room!", 0}},
    [ST_LEFT] = { /* n into rows of k: how many left over? (n, k) */
        {"{A} plants %d seeds in rows of %d. {B} gets the leftovers. How many seeds are left over?", "{B} plants them too!", 0},
        {"{A} has %d {FA}, in bags of %d. {B} gets the extras. How many extras?", "Snack time for {B}!", 0},
        {"%d friends make teams of %d for a game. The rest cheer! How many cheer?", "Go team!", 0},
        {"{A} shares %d stickers in packs of %d. How many stickers are left over?", "Shiny stickers!", 0},
        {"%d chairs go in rows of %d for the show. How many chairs are left over?", "Showtime!", 0}},
    [ST_TAKE] = { /* a baskets of b, gives c away: how many left? (a, b, c) */
        {"{A} picks %d baskets of %d apples, then gives %d to {B}. How many apples are left?", "Apple pie time!", 0},
        {"{A} bakes %d trays of %d {FA} and shares %d with neighbours. How many {FA} are left?", "Sweet neighbours!", 0},
        {"{A} has %d packs of %d stickers and gives %d to {B}. How many stickers are left?", "{B} is so happy!", 0},
        {"{A} buys %d bags of %d balloons. %d float away! How many balloons are left?", "Still lots of balloons!", 0},
        {"{A} makes %d rows of %d cupcakes and %d get eaten. How many cupcakes are left?", "Yummy!", 0}},
    [ST_SHAREADD] = { /* a friends share n fairly, then each gets c more: how many each? (a, n, c) */
        {"%d friends share %d {FA} fairly. Then {A} gives each %d more. How many does each have?", "Everyone is full!", 0},
        {"%d friends share %d beads. Then each finds %d more. How many beads each now?", "Pretty necklaces!", 0},
        {"%d friends share %d stickers. Then {A} gives each %d more. How many stickers each now?", "Sticker smiles!", 0},
        {"%d snails share %d leaves. Then each finds %d more. How many leaves each now?", "Munch munch!", 0},
        {"%d friends share %d shells, then each finds %d more. How many shells each?", "Beach treasure!", 0}},
    [ST_ADD] = { /* a, then b more: how many? (a, b) */
        {"{A} collects %d shells, then finds %d more at the beach. How many shells now?", "Treasure!", 0},
        {"{A} delivers %d letters, then %d more for {B}'s party. How many letters?", "Party invites sent!", 0},
        {"{A} has %d stickers and {B} gives %d more. How many stickers now?", "A full sticker book!", 0},
        {"{A} counts %d stars, then %d more. How many stars in all?", "Twinkle twinkle!", 0},
        {"{A} made %d {FA}, then %d more for the party. How many {FA} in all?", "What a feast!", 0}},
    [ST_SUB] = { /* a, b go away: how many left? (a, b) */
        {"{A} had %d stamps and gives %d to {B} for a card. How many stamps are left?", "{B} loves the card!", 0},
        {"{A} had %d {FA} and shares %d at the picnic. How many {FA} are left?", "Picnic fun!", 0},
        {"{A} had %d beads and uses %d for a bracelet for {B}. How many beads are left?", "A sparkly gift!", 0},
        {"{A} has %d letters to deliver. %d are done. How many letters are left?", "Almost done!", 0},
        {"{A} blew up %d balloons and %d popped. How many balloons are left?", "Still a party!", 0}},
    [ST_MUL] = { /* a groups of b, bigger numbers (a, b) */
        {"{A} carries %d mail bags with %d letters each to the party. How many letters?", "Special delivery!", 0},
        {"{A} fills %d boxes with %d stickers each for the fair. How many stickers?", "The fair is ready!", 0},
        {"%d trays with %d {FA} each for the big bake sale. How many {FA}?", "Sold out!", 0},
        {"{A} plants %d rows with %d seeds each. How many seeds?", "A giant garden!", 0},
        {"%d buses with %d friends each come to the show. How many friends?", "Full house!", 0}},
    [ST_DIVR] = { /* n into k, the same each: how many each and left over? (n, k) */
        {"{A} sorts %d letters into %d bags, the same in each. How many letters in each bag, and how many left?", "All sorted!", 0},
        {"{A} shares %d {FA} on %d plates, the same on each. How many on each plate, and how many extra?", "Plates for all!", 0},
        {"%d beads go on %d bracelets, the same on each. How many beads on each, and how many left over?", "Bracelets done!", 0},
        {"{A} puts %d stickers on %d pages, the same on each. How many stickers on each page, and how many left over?", "A tidy book!", 0},
        {"%d seeds go in %d rows, the same in each. How many seeds in each row, and how many left?", "Neat rows!", 0}},
    [ST_READY] = { /* starts at s, takes d minutes: when is it done? (s, d) */
        {"{A}'s cake goes in the oven at %s and bakes %d minutes. Guests are coming! When is it done?", "Just in time!", 1},
        {"{A} starts getting ready for {B}'s party at %s. It takes %d minutes. When is {A} ready?", "Party time!", 1},
        {"{A}'s train ride starts at %s and lasts %d minutes. When does {A} arrive at {B}'s?", "Hello, {B}!", 1},
        {"The puppet show starts at %s and runs %d minutes. {A} wants to know: when does it end?", "What a show!", 0},
        {"{A} starts {hA} at %s for %d minutes. When is it time for lunch?", "Lunch time!", 0}},
    [ST_HOWLONG] = { /* from s1 to s2: how many minutes? (s) */
        {"The puppet show runs from %s. {A} wants to know: how many minutes is that?", "Bravo!", 0},
        {"{A} is {hA} from %s. How many minutes is that?", "Such fun!", 0},
        {"{B}'s party runs from %s. How many minutes of fun?", "Best party!", 0},
        {"The train ride to {B}'s goes from %s. How many minutes?", "We're here!", 0},
        {"{A} bakes {FA} from %s. How many minutes in the oven?", "Smells yummy!", 0}},
    [ST_CHANGE] = { /* price s, paid with $d: the change (s, d) */
        {"{A} buys {a} {tB} for {B}'s birthday for %s and pays with $%d.00. Give the right change!", "{B} loves the gift!", 0},
        {"{A} buys {a} {fA} for %s and pays with $%d.00. What change does {A} get?", "Yum!", 0},
        {"{B} buys a gift for {A}, %s, and pays $%d.00. How much change comes back?", "Surprise!", 0},
        {"Party hats cost %s. {A} pays with $%d.00. What is the change?", "Party hats on!", 0},
        {"{A} buys a kite for %s with $%d.00. How much change?", "Up it goes!", 0}},
    [ST_JUG] = { /* a liters, b poured: left? (a, b) */
        {"{A} made %d liters of lemonade and pours %d for friends. How many liters are left?", "Refreshing!", 0},
        {"{A}'s fish tank has %d liters. %d are scooped out to clean it. How many liters are left?", "Sparkly clean!", 0},
        {"A jug of juice has %d liters. {B} pours %d at the picnic. How many liters are left?", "Cheers!", 0},
        {"{A} fills a pool with %d liters and %d splash out! How many liters are left?", "Splash party!", 0},
        {"{A} makes %d liters of soup and serves %d. How many liters are left?", "Warm and cosy!", 0}},
    [ST_FAR] = { /* a m, then b m more: how far? (a, b) */
        {"{A} flies %d m, then %d m more to visit {B}. How far in all?", "Hi {B}!", 0},
        {"{A} walks %d m to the park, then %d m to {B}'s house. How far?", "Play date!", 0},
        {"{A}'s kite string is %d m, plus %d m more. How long now?", "So high!", 0},
        {"{A} swims %d m, then %d m more. How far in all?", "Splash!", 0},
        {"{A} rides %d m, then %d m more to the fair. How far?", "The fair!", 0}},
    [ST_FENCE] = { /* a garden w by h: the fence all round (w, h) */
        {"{A}'s garden is %d m by %d m. The bunny keeps hopping out! How much fence goes all around?", "The bunny is safe!", 1},
        {"{A} frames a %d by %d picture for {B}. How much ribbon goes all around?", "A pretty frame!", 0},
        {"{A}'s pond is %d m by %d m. The ducks need a fence all around. How many m of fence?", "Happy ducks!", 1},
        {"{A} puts lights around a %d by %d window. How long are the lights?", "It glows!", 0},
        {"{A}'s sandbox is %d by %d. How much wood goes all around?", "Sandcastle time!", 0}},
    [ST_RUG] = { /* a w by h floor: how many squares? (w, h) */
        {"{A}'s room is %d by %d squares. The old rug is too small! How many squares is a new rug?", "Cosy toes!", 1},
        {"{A} paints a %d by %d mural for {B}. How many squares of paint?", "{B} loves it!", 0},
        {"{A}'s garden bed is %d m by %d m. How many square m of soil?", "Room to grow!", 0},
        {"{A} sews a %d by %d quilt for {B}'s sleepover. How many squares?", "Snuggly!", 0},
        {"The dance floor is %d by %d tiles. How many tiles for {A}'s party?", "Dance time!", 0}},
    [ST_RECIPE] = { /* k friends each get n/d: how much in all? (k, n, d) */
        {"%d friends each get %d/%d of a cake at {A}'s party. How much cake in all?", "Cake for all!", 0},
        {"{A} makes a recipe %d times. It needs %d/%d cup of sugar each time. How much sugar?", "Sweet!", 0},
        {"%d friends each drink %d/%d of a jug of juice. How many jugs in all?", "Slurp!", 0},
        {"%d friends each eat %d/%d of a pie at the picnic. How much pie in all?", "Picnic pie!", 0},
        {"{A} feeds %d bunnies %d/%d of a carrot each. How many carrots in all?", "Crunch crunch!", 0}},
    [ST_MARBLES] = { /* a bags of b, gives c: left? (a, b, c) */
        {"{A} has %d bags with %d marbles each and gives %d to {B}. How many marbles are left?", "Marble fun!", 0},
        {"{A} has %d boxes of %d crayons and gives %d to {B}. How many crayons are left?", "Colour time!", 0},
        {"{A} has %d jars of %d buttons and uses %d for a craft. How many buttons are left?", "Crafty!", 0},
        {"{A} has %d packs of %d cards and gives %d to friends. How many cards are left?", "Game night!", 0},
        {"{A} has %d bags of %d {FA} and eats %d. How many {FA} are left?", "Munch!", 0}},
};

/* What the friend says on the board (a few words) and the stage's container and things, per
 * template: the numbers are on the stage (mq_stage.c), the full story is a tap away. */
typedef struct {
    char cap[30];
    uint8_t c, i;
} mq_cap_t;
static const mq_cap_t caps[ST_COUNT][NT] = {
    [ST_GROUPS] = {{"Treats for the party!", SP_TRAY, SI_COOKIE}, {"Friends at the picnic!", SP_BLANKET, SI_FRIEND},
                   {"Sleepover sticker bags!", SP_BAG, SI_STAR}, {"Flowers for {B}!", SP_ROW, SI_FLOWER},
                   {"Bracelets for friends!", SP_RING, SI_BEAD}},
    [ST_SHARE] = {{"Make it fair for {B}!", SP_TRAY, SI_COOKIE}, {"Same beads for everyone!", SP_BAG, SI_BEAD},
                  {"Same pizza for all!", SP_TRAY, SI_SLICE}, {"Seeds for every pot!", SP_POT, SI_SEED},
                  {"Fair treats for all!", SP_TRAY, SI_COOKIE}},
    [ST_HOWMANY] = {{"Fill the gift boxes!", SP_BOX, SI_COOKIE}, {"Fill the baskets!", SP_BASKET, SI_APPLE},
                    {"Tables for the party!", SP_TRAY, SI_FRIEND}, {"Cards into envelopes!", SP_BOX, SI_CARD},
                    {"Fill the sticker pages!", SP_PAGE, SI_STAR}},
    [ST_PERONE] = {{"Ladybugs on every leaf!", SP_LEAF, SI_BUG}, {"Candles for the cakes!", SP_CAKE, SI_CANDLE},
                   {"Balloons on every chair!", SP_CHAIR, SI_BALLOON}, {"Lights for the windows!", SP_PAGE, SI_LIGHT},
                   {"Marbles in every jar!", SP_JAR, SI_MARBLE}},
    [ST_TIMES] = {{"{A} found way more!", SP_BAG, SI_SHELL}, {"{A} has heaps more!", SP_BAG, SI_STAR},
                  {"{A} baked loads more!", SP_TRAY, SI_COOKIE}, {"{A} picked lots more!", SP_BAG, SI_FLOWER},
                  {"{A} jumped way more!", SP_BAG, SI_STAR}},
    [ST_BUS] = {{"Nobody stays behind!", SP_BUS, SI_FRIEND}, {"Everyone must ride!", SP_BUS, SI_FRIEND},
                {"A tent for everyone!", SP_TENT, SI_FRIEND}, {"Every one must bake!", SP_TRAY, SI_COOKIE},
                {"Nobody stays on shore!", SP_BOAT, SI_FRIEND}},
    [ST_FULL] = {{"Only full boxes go!", SP_BOX, SI_COOKIE}, {"Fill the egg boxes!", SP_BOX, SI_EGG},
                 {"Full balloon bunches!", SP_BAG, SI_BALLOON}, {"Full crayon boxes!", SP_BOX, SI_CARD},
                 {"Full book shelves!", SP_TRAY, SI_CARD}},
    [ST_LEFT] = {{"Leftovers for {B}!", SP_ROW, SI_SEED}, {"Extras for {B}!", SP_BAG, SI_COOKIE},
                 {"The rest cheer!", SP_ROW, SI_FRIEND}, {"Any stickers left?", SP_BAG, SI_STAR},
                 {"Any chairs left?", SP_ROW, SI_CHAIR}},
    [ST_TAKE] = {{"Apples for {B}, too!", SP_BASKET, SI_APPLE}, {"Treats for the neighbours!", SP_TRAY, SI_COOKIE},
                 {"Stickers for {B}!", SP_BAG, SI_STAR}, {"Oops, balloons float away!", SP_BAG, SI_BALLOON},
                 {"Yum, some got eaten!", SP_TRAY, SI_COOKIE}},
    [ST_SHAREADD] = {{"Fair shares, then more!", SP_TRAY, SI_COOKIE}, {"Fair beads, then more!", SP_BAG, SI_BEAD},
                     {"Fair stickers, then more!", SP_BAG, SI_STAR}, {"Leaves for the snails!", SP_TRAY, SI_LEAF},
                     {"Fair shells, then more!", SP_BAG, SI_SHELL}},
    [ST_ADD] = {{"More shells at the beach!", 0, SI_SHELL}, {"Party invites to deliver!", 0, SI_LETTER},
                {"A gift of stickers!", 0, SI_STAR}, {"Count the twinkles!", 0, SI_STAR},
                {"More for the party!", 0, SI_COOKIE}},
    [ST_SUB] = {{"Stamps for {B}'s card!", 0, SI_CARD}, {"Sharing at the picnic!", 0, SI_COOKIE},
                {"A bracelet for {B}!", 0, SI_BEAD}, {"Letters still to go!", 0, SI_LETTER},
                {"Pop! Some balloons burst!", 0, SI_BALLOON}},
    [ST_MUL] = {{"Mail for the party!", SP_BAG, SI_LETTER}, {"Sticker boxes for the fair!", SP_BOX, SI_STAR},
                {"The big bake sale!", SP_TRAY, SI_COOKIE}, {"A giant garden!", SP_ROW, SI_SEED},
                {"Buses to the show!", SP_BUS, SI_FRIEND}},
    [ST_DIVR] = {{"Sort the letters fairly!", SP_BAG, SI_LETTER}, {"Plates for all!", SP_TRAY, SI_COOKIE},
                 {"Same beads on each!", SP_RING, SI_BEAD}, {"Same stickers per page!", SP_PAGE, SI_STAR},
                 {"Same seeds in each row!", SP_ROW, SI_SEED}},
    [ST_READY] = {{"Guests are coming!", 0, 0}, {"Party soon!", 0, 0}, {"Off to {B}'s!", 0, 0},
                  {"The show is on!", 0, 0}, {"Lunch after this!", 0, 0}},
    [ST_HOWLONG] = {{"Showtime!", 0, 0}, {"{A}'s fun time!", 0, 0}, {"{B}'s party!", 0, 0}, {"The train ride!", 0, 0},
                    {"In the oven!", 0, 0}},
    [ST_CHANGE] = {{"A gift for {B}!", 0, 0}, {"Snack time!", 0, 0}, {"A surprise gift!", 0, 0}, {"Party hats!", 0, 0},
                   {"A new kite!", 0, 0}},
    [ST_JUG] = {{"Lemonade for friends!", 0, 0}, {"Cleaning the fish tank!", 0, 0}, {"Juice at the picnic!", 0, 0},
                {"Splash!", 0, 0}, {"Soup for everyone!", 0, 0}},
    [ST_FAR] = {{"Off to visit {B}!", 0, 0}, {"A walk to {B}'s!", 0, 0}, {"A longer kite string!", 0, 0},
                {"A long swim!", 0, 0}, {"Off to the fair!", 0, 0}},
    [ST_FENCE] = {{"Keep the bunny in!", 0, 0}, {"Ribbon all around!", 0, 0}, {"A fence for the ducks!", 0, 0},
                  {"Lights all around!", 0, 0}, {"Wood all around!", 0, 0}},
    [ST_RUG] = {{"A new rug!", 0, 0}, {"A mural for {B}!", 0, 0}, {"Soil for the garden!", 0, 0},
                {"A quilt for {B}!", 0, 0}, {"Tiles for the dance floor!", 0, 0}},
    [ST_RECIPE] = {{"Cake for all!", SP_TRAY, SI_SLICE}, {"Sugar for the recipe!", SP_CUP, SI_DROP},
                   {"Juice for everyone!", SP_CUP, SI_DROP}, {"Pie at the picnic!", SP_TRAY, SI_SLICE},
                   {"Carrots for the bunnies!", SP_TRAY, SI_CARROT}},
    [ST_MARBLES] = {{"Marbles for {B}!", SP_BAG, SI_MARBLE}, {"Crayons for {B}!", SP_BOX, SI_CARD},
                    {"Buttons for a craft!", SP_JAR, SI_MARBLE}, {"Cards for friends!", SP_BOX, SI_CARD},
                    {"Snack time!", SP_BAG, SI_COOKIE}},
};

/* a short line for the board (mq_stage.c), its placeholders expanded */
void mq_story_cap(mq_t *q, mq_prob_t *p, const char *cap)
{
    mq_story_expand(q, cap, p->ca, p->cb, p->cap, sizeof p->cap);
}

/* the slots of a kind that are real templates */
static int nt(int kind) { (void)kind; return NT; }

int mq_story_count(int kind) { return kind >= 0 && kind < ST_COUNT ? nt(kind) : 0; }

/* the two friends of a story: A is the player's best friend (cast 0) or the world's guide most
 * of the time, B anyone else */
static void pick_cast(mq_t *q, mq_prob_t *p)
{
    int g = mq_skill[p->skill].world >= G_SKY ? 8 : mq_skill[p->skill].world, r = mq_rnd(q, 0, 9);
    int a = r < 4 ? 0 : r < 8 ? g : mq_rnd(q, 0, MQ_NCAST - 1);
    int b = mq_rnd(q, 0, MQ_NCAST - 2);
    if (b >= a)
        b++;
    p->ca = (uint8_t)a, p->cb = (uint8_t)b;
}

/* write a story of `kind` into p (its text lines), template t (-1: any) with the numbers given */
void mq_story(mq_t *q, mq_prob_t *p, int kind, int t, int n0, int n1, int n2, const char *s)
{
    if (kind < 0 || kind >= ST_COUNT)
        return;
    pick_cast(q, p);
    if (t < 0 || t >= nt(kind))
        t = mq_rnd(q, 0, nt(kind) - 1);
    p->story = (uint8_t)(kind + 1), p->tale = (uint8_t)t;
    p->sn[0] = (int16_t)n0, p->sn[1] = (int16_t)n1, p->sn[2] = (int16_t)n2;
    /* the stage: only for a real story (some callers just pick the friends and tell their own) */
    p->stg = 0, p->cap[0] = 0;
    if (n0 || n1 || s) {
        p->stg = (uint8_t)(kind + 1), p->stg_c = caps[kind][t].c, p->stg_i = caps[kind][t].i;
        p->stg_n[0] = n0, p->stg_n[1] = n1, p->stg_n[2] = n2;
        q->api->snprintf(p->stg_s, sizeof p->stg_s, "%s", s ? s : "");
    }
    char a[200], b[220];
    const char *f = tales[kind][t].set;
    if (kind == ST_READY || kind == ST_CHANGE)
        q->api->snprintf(a, sizeof a, f, s, n0);
    else if (kind == ST_HOWLONG)
        q->api->snprintf(a, sizeof a, f, s);
    else
        q->api->snprintf(a, sizeof a, f, n0, n1, n2);
    mq_story_expand(q, a, p->ca, p->cb, b, sizeof b);
    if (mq_story_wrap(q, b, p->text, MQ_STORY_W) > 3) { /* too long for these names: the friends only */
        p->ca = 0, p->cb = 1;
        mq_story_expand(q, a, 0, 1, b, sizeof b);
        mq_story_wrap(q, b, p->text, MQ_STORY_W);
    }
    if (p->stg)
        mq_story_cap(q, p, caps[kind][t].cap);
}

/* B pouts until it is fixed (fairness, hurry, worry) */
bool mq_story_sad(const mq_prob_t *p) { return p->story && tales[p->story - 1][p->tale % NT].sad; }

/* the happy ending */
void mq_story_fix(mq_t *q, const mq_prob_t *p, char *out, int n)
{
    if (!p->story || !tales[p->story - 1][p->tale % NT].fix[0]) {
        static const char cheer[6][24] = {"Hooray!", "You did it!", "Yay, thank you!", "Perfect!", "Sparkly!",
                                          "Everyone cheers!"};
        q->api->snprintf(out, (size_t)n, "%s", cheer[(p->ident + p->ca) % 6]);
        return;
    }
    mq_story_expand(q, tales[p->story - 1][p->tale % NT].fix, p->ca, p->cb, out, n);
}

/* the oops: what a wrong answer means in the story ("Now Pip has 5 and Dot has 3, not fair!"),
 * else the misconception told by the friend */
void mq_story_oops(mq_t *q, const mq_prob_t *p, int32_t given, int mc, char *out, int n)
{
    char a[120];
    const modx_api_t *api = q->api;
    int kind = p->story ? p->story - 1 : -1;
    a[0] = 0;
    if (p->kind == K_NUM && given >= 0 && given < 100000) {
        int n0 = p->sn[0], n1 = p->sn[1]; /* the story's numbers */
        switch (kind) {
        case ST_SHARE:
            if (n1 > 0 && given * n1 < n0)
                api->snprintf(a, sizeof a, "%d each leaves %d on the plate. {B} still pouts!", given, n0 - given * n1);
            else if (n1 > 0 && given * n1 > n0)
                api->snprintf(a, sizeof a, "%d each needs %d, but there are only %d!", given, given * n1, n0);
            break;
        case ST_GROUPS:
        case ST_MUL:
            if (n1 > 0 && given < n0 * n1)
                api->snprintf(a, sizeof a, "That's not enough! Some spots are still empty.");
            else if (n1 > 0)
                api->snprintf(a, sizeof a, "That's too many! Some would be extra.");
            break;
        case ST_BUS:
            if (n1 > 0 && given * n1 < n0)
                api->snprintf(a, sizeof a, "Oh no! %d friends have no seat yet!", n0 - given * n1);
            else if (n1 > 0)
                api->snprintf(a, sizeof a, "That's an extra one, all empty!");
            break;
        case ST_FULL:
            if (n1 > 0 && given * n1 > n0)
                api->snprintf(a, sizeof a, "Oops, one box isn't full yet!");
            break;
        case ST_FENCE:
            if (mc == MC_AREA_PERIM)
                api->snprintf(a, sizeof a, "That's the inside, not the fence around!");
            else if (mc == MC_PERIM_TWO)
                api->snprintf(a, sizeof a, "The fence only goes on two sides. Hop hop, out it goes!");
            break;
        case ST_RUG:
            if (mc == MC_AREA_PERIM)
                api->snprintf(a, sizeof a, "That's the edge! The rug covers the inside.");
            break;
        default: break;
        }
    }
    if (!a[0]) {
        static const struct {
            uint8_t mc;
            char s[60];
        } say[] = {
            {MC_ADD_NOT_MUL, "Hmm, that's only a few! Every group counts."},
            {MC_ONE_GROUP, "That's just one group. There are more!"},
            {MC_WRONG_OP, "Oops, that went the wrong way! {A} scratches {pA} head."},
            {MC_DIV_MIX, "Hmm, groups and how many in each got mixed!"},
            {MC_REM_ROUND, "Look at the leftovers again!"},
            {MC_REM_IGNORED, "The leftovers matter here!"},
            {MC_HANDS_SWAP, "The big and little hands swapped! {A} would be late!"},
            {MC_ELAPSED_100, "An hour has 60 minutes, not 100!"},
            {MC_AREA_PERIM, "Inside or around? {A} mixed them up!"},
            {MC_PERIM_TWO, "Only two sides! The fence has a gap."},
            {MC_NO_REGROUP, "A ten got lost on the way!"},
            {MC_TIMES_AS_ADD, "Times as many is lots more than that!"},
            {MC_COORD_SWAP, "Across first, then up! The treasure is elsewhere."},
            {MC_BIG_DENOM, "More slices means smaller slices!"},
            {MC_ADD_DENOMS, "The slice size stays the same!"},
            {MC_CLOSE, "So close! {A} counts again."},
            {MC_NEIGHBOR, "So close! {A} counts again."},
            {MC_CARELESS, "Hmm, not that one. {A} looks again."},
        };
        for (size_t i = 0; i < sizeof say / sizeof say[0]; i++)
            if (say[i].mc == mc) {
                api->snprintf(a, sizeof a, "%s", say[i].s);
                break;
            }
        if (!a[0])
            api->snprintf(a, sizeof a, "Hmm! %s", mq_mc_help[mc < MC_COUNT ? mc : 0][0]); /* (never the log's name) */
    }
    mq_story_expand(q, a, p->ca, p->cb, out, n);
}
