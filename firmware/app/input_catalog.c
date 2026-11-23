/*
 * The structured CATALOG (SHIFT APPS; interface app/input.h, help page
 * "catalog") and the fuzzy matcher shared with the history search.
 *
 * What the user sees: the menu screen (app/menu.c) titled CATALOG, listing
 * every command A-Z. ALPHA lock is switched on, so letter keys type a query
 * (shown at the right of the title line, ⌫ erases, SHIFT ⌫ clears it) and
 * the list keeps only the entries that match, best match first. {◀} {▶}
 * step through the categories (Algebra, Calculus, Trigonometry, ...; the
 * title shows the category), F3 (categ) picks one from a list, ENTER inserts
 * the entry into the screen the catalog was opened from, SHIFT HELP opens
 * its help page. A category can also list apps ("▶ Finance"): ENTER opens
 * the app.
 *
 * Data: the categories are @category pages in help/input.txt, compiled by
 * tools/gen_help.py into help_categories[] and, per catalog entry,
 * help_catalog_cat[] and help_catalog_summary[] (app/help_data.h, flash bank
 * 2). Nothing is hand-maintained here, so commands added to the help files
 * later appear automatically (in the category of their group, else "Other").
 *
 * Ranking (fuzzy_score): every entry of the category is scored against its
 * label and its command name (help topic id); an entry that matches neither
 * may still match its summary line (queries of 3+ characters), ranked below
 * any name match. The scores are tiers, so the order is predictable:
 *   1000 exact name ("sin" -> sin()), 900 prefix, 700 substring at a word
 *   start, 600 substring elsewhere (both minus the position, so earlier is
 *   better), 300 +- bonuses for a subsequence ("intg" -> integrate: +8 per
 *   character that continues a run, +6 at a word start, -gap length up to
 *   10 per gap, -length/4 so shorter names win), and
 *   150 / 120 for a summary match at a word start / elsewhere.
 * Ties keep the alphabetical order. The ranked index (idx/score, from the
 * input_init carve-out) is rebuilt on every key: about 350 entries x a few
 * dozen byte compares, well under a millisecond on the MCU, so no caching.
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os") /* flash budget: UI code, size over speed */
#endif
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "help_data.h"
#include "input.h"
#include "registry.h"

/* ------------------------------------------------------------ matcher */
static char lc(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }
static bool word_start(const char *s, int p) { return p == 0 || !isalnum((unsigned char)s[p - 1]); }

/* Does q match s at its start (case-insensitive)? */
static bool starts_ci(const char *s, const char *q)
{
    for (; *q; s++, q++)
        if (lc(*s) != lc(*q))
            return false;
    return true;
}

int fuzzy_score(const char *q, const char *s)
{
    int nq = (int)strlen(q);
    if (!nq)
        return 1;
    if (starts_ci(s, q)) /* exact: the name, optionally with its "(" */
        return !s[nq] || (s[nq] == '(' && !s[nq + 1]) ? 1000 : 900;
    int best = 0;
    for (int p = 1; s[p]; p++)
        if (starts_ci(s + p, q)) {
            int sc = (word_start(s, p) ? 700 : 600) - (p < 50 ? p : 50);
            if (sc > best)
                best = sc;
        }
    if (best)
        return best;
    /* subsequence, greedy left to right */
    int sc = 300, last = -1, j = 0;
    for (int p = 0; s[p] && j < nq; p++) {
        if (lc(s[p]) != lc(q[j]))
            continue;
        if (last >= 0 && p == last + 1)
            sc += 8;
        else if (last >= 0)
            sc -= p - last - 1 < 10 ? p - last - 1 : 10;
        if (word_start(s, p))
            sc += 6;
        last = p;
        j++;
    }
    if (j < nq)
        return 0;
    sc -= (int)strlen(s) / 4; /* shorter names first: "fctr" -> factor before factorial */
    return sc < 1 ? 1 : sc > 499 ? 499 : sc;
}

/* ------------------------------------------------------------ query line */
void query_clear(query_t *q)
{
    q->s[0] = 0;
    q->nstep = 0;
}

bool query_key(query_t *q, int key, int mod)
{
    if (key == KEY_DEL) {
        if (!q->nstep)
            return false;
        if (mod == MOD_SHIFT)
            query_clear(q);
        else
            q->s[strlen(q->s) - q->step[--q->nstep]] = 0;
        return true;
    }
    char t[16];
    if (!ui_key_text(key, mod, g.alpha_snapshot_for_key, t, sizeof t))
        return false;
    size_t o = strlen(q->s), l = strlen(t);
    if (!l || o + l >= QUERY_MAX || q->nstep == (int)sizeof q->step)
        return true; /* full: swallow the key */
    memcpy(q->s + o, t, l + 1);
    q->step[q->nstep++] = (uint8_t)l;
    return true;
}

/* ------------------------------------------------------------ the index */
#define APP_BIT 0x8000  /* idx value: APP_BIT | app_registry index, */
#define PAGE_BIT 0x4000 /*   PAGE_BIT | help_topics index (a help page), else a help_catalog row */

static uint16_t *idx;  /* ranked entries (input_init) */
static int16_t *score; /* their scores */
static int cap, nidx;
static int cat;        /* 0 = all, k = help_categories[k - 1] */
static query_t query;
static char title[40];

void catalog_set_work(uint16_t *i, int16_t *s, int n)
{
    idx = i;
    score = s;
    cap = n;
    nidx = 0;
}

/* Score of catalog row i for the query (see the file comment). */
static int entry_score(int i)
{
    const help_cat_t *c = &help_catalog[i];
    int sc = fuzzy_score(query.s, help_strings + c->label);
    if (c->topic >= 0) {
        int t = fuzzy_score(query.s, help_strings + help_topics[c->topic].id);
        if (t > sc)
            sc = t;
    }
    if (!sc && strlen(query.s) >= 3) {
        int t = fuzzy_score(query.s, help_strings + help_catalog_summary[i]);
        sc = t >= 700 ? 150 : t >= 600 ? 120 : 0;
    }
    return sc;
}

/* Insert entry v with score sc keeping idx sorted by descending score,
 * after the entries with an equal score (stable: alphabetical ties). */
static void add(uint16_t v, int sc)
{
    if (nidx >= cap)
        return;
    int k = nidx++;
    while (k > 0 && score[k - 1] < sc) {
        idx[k] = idx[k - 1];
        score[k] = score[k - 1];
        k--;
    }
    idx[k] = v;
    score[k] = (int16_t)sc;
}

/* Index in app_registry of the app with this id (n bytes), -1 if none. */
static int app_index(const char *id, size_t n)
{
    for (int a = 0; a < app_registry_count; a++)
        if (strlen(app_registry[a]->id) == n && !strncmp(app_registry[a]->id, id, n))
            return a;
    return -1;
}

/* Add the apps listed by category k (help_categories[k].apps, "id,id"):
 * without a query first in the list (score 1000), with one if they match
 * (one point below a command with the same match). */
static void add_apps(int k)
{
    uint32_t o = help_categories[k].apps;
    if (!o)
        return;
    for (const char *p = help_strings + o; *p;) {
        size_t n = strcspn(p, ",");
        int a = app_index(p, n);
        if (a >= 0) {
            int sc = query.s[0] ? fuzzy_score(query.s, app_registry[a]->name) - 1 : 1000;
            if (sc > 0) /* - 1: an app ranks just below commands that match as well */
                add((uint16_t)(APP_BIT | a), sc);
        }
        p += n + (p[n] == ',');
    }
}

/* Help pages in the search: with a query in "All", the help pages (not the
 * @cmd pages, which are the commands themselves) whose title or id contain
 * it (substring or prefix, score >= 600; looser subsequence matches would
 * flood the list) are listed as "? Title" below the commands and apps that
 * match as well, so the CATALOG also finds features without a command
 * ("undo", "rpn", "degree", "copy"). ENTER opens the page. */
static uint8_t cmd_topic[(1024 + 7) / 8]; /* bit t: topic t documents a CATALOG command */
static bool cmd_topic_ready;

static void add_pages(void)
{
    if (!cmd_topic_ready) {
        for (int i = 0; i < help_ncatalog; i++)
            if (help_catalog[i].topic >= 0 && help_catalog[i].topic < 1024)
                cmd_topic[help_catalog[i].topic >> 3] |= (uint8_t)(1u << (help_catalog[i].topic & 7));
        cmd_topic_ready = true;
    }
    for (int t = 0; t < help_ntopics && t < 1024; t++) {
        if (cmd_topic[t >> 3] & (1u << (t & 7)))
            continue;
        int sc = fuzzy_score(query.s, help_strings + help_topics[t].title);
        int si = fuzzy_score(query.s, help_strings + help_topics[t].id);
        if (si > sc)
            sc = si;
        if (sc >= 600)
            add((uint16_t)(PAGE_BIT | t), sc - 2);
    }
}

/* Rebuild the ranked list for the category and query, and the title. The
 * full list without a query is the plain alphabetical catalog. */
static void rebuild(void)
{
    nidx = 0;
    bool q = query.s[0] != 0;
    for (int k = 0; k < help_ncategories; k++)
        if (cat == k + 1 || (cat == 0 && q))
            add_apps(k);
    if (cat == 0 && q)
        add_pages();
    for (int i = 0; i < help_ncatalog; i++) {
        if (cat && help_catalog_cat[i] != cat - 1)
            continue;
        int sc = q ? entry_score(i) : 0;
        if (!q || sc)
            add((uint16_t)i, sc);
    }
    snprintf(title, sizeof title, "%s", cat ? help_strings + help_categories[cat - 1].title : "CATALOG");
}

/* ------------------------------------------------------------ the menus */
static int cat_count(void) { return nidx; }

/* Entry i: a command (label, insert/suffix, help page) or an app. */
static void cat_get(int i, menu_item_t *out)
{
    unsigned v = idx[i];
    if (v & APP_BIT) {
        static char lab[40];
        const app_def_t *a = app_registry[v & ~APP_BIT];
        snprintf(lab, sizeof lab, "▶ %s", a->name);
        *out = (menu_item_t){lab, NULL, NULL, (int)v, a->help};
        return;
    }
    if (v & PAGE_BIT) {
        static char lab[48];
        const help_topic_t *t = &help_topics[v & ~PAGE_BIT];
        snprintf(lab, sizeof lab, "? %s", help_strings + t->title);
        *out = (menu_item_t){lab, NULL, NULL, (int)v, help_strings + t->id};
        return;
    }
    const help_cat_t *c = &help_catalog[v];
    *out = (menu_item_t){help_strings + c->label, help_strings + c->insert,
                         c->suffix ? help_strings + c->suffix : NULL, (int)v,
                         c->topic >= 0 ? help_strings + help_topics[c->topic].id : NULL};
}

/* ENTER: open the app, or insert the command where the catalog was opened. */
static void cat_select(const menu_item_t *it)
{
    if (it->action & APP_BIT) {
        ui_returning = false;
        app_registry[it->action & ~APP_BIT]->open();
        return;
    }
    if (it->action & PAGE_BIT) { /* a help page: open it (CLEAR in help returns to the catalog's origin) */
        g.alpha = 0;
        menu_close();
        help_open(it->help);
        return;
    }
    input_insert_into_origin(it->insert, it->suffix, false);
}

/* Typed keys: LEFT / RIGHT step the category (wrapping, 0 = all), other
 * keys edit the query (input.h query_key). */
static bool cat_filter(int key, int mod)
{
    if (key == KEY_LEFT || key == KEY_RIGHT) {
        int n = help_ncategories + 1;
        cat = (cat + (key == KEY_RIGHT ? 1 : n - 1)) % n;
    } else if (!query_key(&query, key, mod)) {
        return false;
    }
    rebuild();
    return true;
}

static const char *cat_query(void) { return query.s; }

static void open_categories(void);
static const softkeys_t cat_soft = {
    {{"▲ page"}, {"▼ page"}, {"categ", open_categories}, {"help", help_context}, {"back", menu_close}},
};
static const menu_def_t menu_cat = {title, NULL, 0, cat_select, true, "catalog", cat_count, cat_get, &cat_soft,
                                    cat_filter, cat_query};

void catalog_open(void)
{
    cat = 0;
    query_clear(&query);
    rebuild();
    menu_open(&menu_cat);
}

/* CATEGORIES (F3): "All" then every category with its number of commands. */
static int cats_count(void) { return help_ncategories + 1; }
static void cats_get(int i, menu_item_t *out)
{
    static char lab[48];
    int n = 0;
    for (int k = 0; k < help_ncatalog; k++)
        n += !i || help_catalog_cat[k] == i - 1;
    const char *help = "catalog";
    if (i) {
        const help_category_t *c = &help_categories[i - 1];
        snprintf(lab, sizeof lab, "%s (%d)", help_strings + c->title, n);
        if (c->topic >= 0)
            help = help_strings + help_topics[c->topic].id;
    } else {
        snprintf(lab, sizeof lab, "All A-Z (%d)", n);
    }
    *out = (menu_item_t){lab, NULL, NULL, i, help};
}
/* Choosing a category reopens the catalog on it (the query is kept). */
static void cats_select(const menu_item_t *it)
{
    cat = it->action;
    rebuild();
    menu_open(&menu_cat);
}
static const menu_def_t menu_cats = {"CATEGORIES", NULL, 0, cats_select, false, "catalog", cats_count, cats_get};
/* F3: the category list (letters are not needed there: end the ALPHA lock). */
static void open_categories(void)
{
    g.alpha = 0;
    menu_open(&menu_cats);
}

/* `state` keys: catalog_query, catalog_cat (the title), catalog_n. */
void catalog_state(void (*kv)(const char *, const char *, void *), void *user)
{
    char b[12];
    kv("catalog_query", query.s, user);
    kv("catalog_cat", title[0] ? title : "CATALOG", user);
    snprintf(b, sizeof b, "%d", nidx);
    kv("catalog_n", b, user);
}
