/*
 * History search (MENU F5 on any screen with an editor; interface
 * app/input.h, help page "history-search").
 *
 * A menu titled HISTORY lists the home screen's history, newest first, one
 * row per evaluation: "input ▶ result". Keys type a filter the same way they
 * type into the entry line (digits, operators, sin( ...; letters with ALPHA),
 * shown at the right of the title line; ⌫ erases the last key's text, SHIFT ⌫
 * the whole query. Only entries whose input or result matches stay, best
 * first (fuzzy_score, app/input_catalog.c; ties newest first).
 *   ENTER      insert the input where the menu was opened (home: as 2D templates)
 *   F3 result  insert the result instead (not for errors)
 *   F4 copy    put the input on the clipboard; SHIFT F4: the result
 * Number keys type into the filter, so they do not choose rows here.
 *
 * State: the query and the ranked item list (at most HIST_LIST entries of
 * the home history, read through home_hist_count / home_hist_text in
 * app/home.c) are module statics, rebuilt on every key; nothing is saved.
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os") /* flash budget: UI code, size over speed */
#endif
#include <stdio.h>
#include <string.h>
#include "input.h"
#include "m2d.h"
#include "nbdoc.h"

#define HIST_LIST 64 /* the 64 best calculations of the home notebook */

static query_t query;
static uint8_t item[HIST_LIST]; /* history indices, best first */
static int16_t score[HIST_LIST];
static int nitem;

/* Rank the history for the query: newest first, then a stable insertion by
 * descending score (the better of input and result). */
static void rebuild(void)
{
    nitem = 0;
    for (int i = home_hist_count() - 1; i >= 0 && nitem < HIST_LIST; i--) {
        if (home_cell_kind(i) != NBK_MATH)
            continue; /* (text, Python and graph cells are not calculations) */
        int sc = fuzzy_score(query.s, home_hist_text(i, false, NULL));
        int so = fuzzy_score(query.s, home_hist_text(i, true, NULL));
        if (so > sc)
            sc = so;
        if (!sc)
            continue;
        int k = nitem++;
        while (k > 0 && score[k - 1] < sc) {
            item[k] = item[k - 1];
            score[k] = score[k - 1];
            k--;
        }
        item[k] = (uint8_t)i;
        score[k] = (int16_t)sc;
    }
}

static int h_count(void) { return nitem; }

/* Row i: "input ▶ result" (the action is the history index). */
static void h_get(int i, menu_item_t *out)
{
    static char lab[96];
    const char *in = home_hist_text(item[i], false, NULL);
    snprintf(lab, sizeof lab, "%s ▶ %s", in, home_hist_text(item[i], true, NULL));
    *out = (menu_item_t){lab, in, NULL, item[i], "history-search"};
}

static void h_select(const menu_item_t *it) { input_insert_into_origin(it->insert, NULL, true); }

static bool h_filter(int key, int mod)
{
    if (!query_key(&query, key, mod))
        return false;
    rebuild();
    return true;
}

static const char *h_query(void) { return query.s; }

/* The highlighted row's text: its result if `output` (NULL for an error),
 * else its input; NULL if the list is empty. */
static const char *current(bool output)
{
    menu_item_t it;
    if (!menu_current_item(&it))
        return NULL;
    bool err = false;
    const char *t = home_hist_text(it.action, output, &err);
    return output && err ? NULL : t;
}

/* F3: insert the result. */
static void h_result(void)
{
    const char *t = current(true);
    if (t)
        input_insert_into_origin(t, NULL, true);
}

/* F4 / SHIFT F4: copy the input / the result, then return. */
static void copy_and_close(bool output)
{
    const char *t = current(output);
    if (t)
        clip_put(t);
    menu_close();
    ui_toast(t ? "copied" : "nothing to copy");
}
static void h_copy(void) { copy_and_close(false); }
static void h_copy_res(void) { copy_and_close(true); }

static const softkeys_t hist_soft = {
    {{"▲ page"}, {"▼ page"}, {"result", h_result}, {"copy", h_copy}, {"back", menu_close}},
    {{NULL}, {NULL}, {NULL}, {"copy res", h_copy_res}, {"help", help_context}},
};
static const menu_def_t menu_hist = {"HISTORY", NULL, 0, h_select, false, "history-search", h_count, h_get,
                                     &hist_soft, h_filter, h_query};

void hist_search_open(void)
{
    query_clear(&query);
    rebuild();
    menu_open(&menu_hist);
}

/* `state` keys: hist_query, hist_n (rows shown). */
void hist_state(void (*kv)(const char *, const char *, void *), void *user)
{
    char b[12];
    kv("hist_query", query.s, user);
    snprintf(b, sizeof b, "%d", nitem);
    kv("hist_n", b, user);
}
