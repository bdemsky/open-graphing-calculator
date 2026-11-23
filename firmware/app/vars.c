/* VARS menu: categories from the registry (registry.c: vars_registry), the CAS
 * user variables/functions, Y-VARS (Y1(..)..Y6(..) defined in the CAS from the
 * Y= editor), and the CAS part of the saved session.
 *
 * The VARS key opens a two-level menu: the first page lists the registered
 * categories (vars_provider_t, app/registry.h) that currently have items;
 * choosing one opens a dynamic menu of its items, and choosing an item
 * inserts its text into the editor of the screen below (menu.c default).
 * The built-in providers here are "Variables" and "Functions" (enumerated
 * from the CAS with cas_vars()) and "Y-VARS".
 *
 * Also here: the editor hook that tells "f(2)" (call of a user function)
 * from "x(2)" (multiplication), the Y1..Y6 CAS definitions kept in sync with
 * the Y= editor, and the "cas" persist blob (variables, functions,
 * assumptions and ans as text records, cas_save()/cas_restore()). */
#include <stdio.h>
#include <string.h>
#include "graph.h" /* gx_params_flush */
#include "registry.h"
#include "ui.h"

/* ------------------------------------------------------------ menus */
static const vars_provider_t *prov; /* category being shown */

/* Second level: a dynamic menu forwarding to the chosen provider. Not const:
 * its title is set to the category name when it is opened. */
static int sub_count(void) { return prov ? prov->count() : 0; }
static void sub_get(int i, menu_item_t *out) { prov->get(i, out); }
static menu_def_t menu_sub = {"VARS", NULL, 0, NULL, false, "vars", sub_count, sub_get};

/* first page: categories with at least one item (registry indices; at most
 * 16 categories are shown). Recomputed by every count() call. */
static int visible[16], nvisible;

static int top_count(void)
{
    nvisible = 0;
    for (int i = 0; i < vars_registry_count && nvisible < 16; i++)
        if (vars_registry[i]->count() > 0)
            visible[nvisible++] = i;
    return nvisible;
}

static void top_get(int i, menu_item_t *out)
{
    const vars_provider_t *p = vars_registry[visible[i]];
    *out = (menu_item_t){p->title, NULL, NULL, visible[i], "vars"};
}

static void top_select(const menu_item_t *it)
{
    prov = vars_registry[it->action];
    menu_sub.title = prov->title;
    menu_open(&menu_sub);
}

static const menu_def_t menu_vars = {"VARS", NULL, 0, top_select, false, "vars", top_count, top_get};

void vars_open(void) { menu_open(&menu_vars); }

/* ------------------------------------------------------------ CAS names */
/* Sorted snapshot of the CAS names of one kind (variables or functions),
 * filled by collect() and shared by both providers; snap_kind says which kind
 * it holds, so a provider re-collects when the other one filled it. Names of
 * 24+ characters and Y1..Y6 (shown under Y-VARS) are left out; at most
 * MAXNAMES. */
#define MAXNAMES 128
static struct {
    char name[24];
    int np; /* number of parameters (functions) */
} names[MAXNAMES];
static int nnames;
static bool want_functions; /* kind collect_cb() keeps */
static int snap_kind = -1;  /* kind in names[]: 0 variables, 1 functions, -1 none */

static bool is_yvar(const char *n) { return n[0] == 'Y' && n[1] >= '1' && n[1] <= '0' + NFUNC && !n[2]; }

/* cas_vars() callback: keep names of the wanted kind, insertion-sorted. */
static void collect_cb(void *user, const char *name, int np)
{
    if ((np >= 0) != want_functions || is_yvar(name) || nnames >= MAXNAMES || strlen(name) >= sizeof names[0].name)
        return;
    /* sorted insert */
    int k = nnames;
    while (k > 0 && strcmp(names[k - 1].name, name) > 0) {
        names[k] = names[k - 1];
        k--;
    }
    snprintf(names[k].name, sizeof names[k].name, "%s", name);
    names[k].np = np;
    nnames++;
}

static int collect(bool functions)
{
    nnames = 0;
    want_functions = functions;
    cas_vars(g.cas, collect_cb, NULL);
    snap_kind = functions;
    return nnames;
}

/* Providers. count() takes a fresh snapshot; get() re-collects at item 0 or
 * when names[] holds the other kind (the other provider ran in between).
 * Item strings are static (valid until the next get). */
static int values_count(void) { return collect(false); }
static void values_get(int i, menu_item_t *out)
{
    static char ins[24];
    if (i == 0 || snap_kind != 0)
        collect(false);
    snprintf(ins, sizeof ins, "%s", i < nnames ? names[i].name : "?");
    *out = (menu_item_t){ins, ins, NULL, i, "vars"};
}
const vars_provider_t vars_cas_values = {"Variables", values_count, values_get};

static int functions_count(void) { return collect(true); }
static void functions_get(int i, menu_item_t *out)
{
    static char label[40], ins[28];
    if (i == 0 || snap_kind != 1)
        collect(true);
    if (i >= nnames) {
        *out = (menu_item_t){"?", NULL, NULL, i, "vars"};
        return;
    }
    snprintf(ins, sizeof ins, "%s(", names[i].name);
    snprintf(label, sizeof label, "%s(%d arg%s)", names[i].name, names[i].np, names[i].np == 1 ? "" : "s");
    *out = (menu_item_t){label, ins, NULL, i, "vars"};
}
const vars_provider_t vars_cas_functions = {"Functions", functions_count, functions_get};

/* Search state for is_user_function(). */
typedef struct {
    const char *name;
    bool found;
} fnfind_t;

static void fnfind_cb(void *user, const char *name, int np)
{
    fnfind_t *f = user;
    if (np >= 0 && !strcmp(name, f->name))
        f->found = true;
}

/* editor hook: is `name` a user function? Walks the CAS symbol table (called
 * by ed_cas() only for a one-letter name followed by "("). */
static bool is_user_function(const char *name)
{
    fnfind_t f = {name, false};
    if (g.cas)
        cas_vars(g.cas, fnfind_cb, &f);
    return f.found;
}

void vars_init(void) { ed_function_hook = is_user_function; } /* from app_init */

/* ------------------------------------------------------------ Y-VARS */
/* Y-VARS: always Y1..Y6; the label shows the current definition (cut to 30
 * bytes), selecting inserts "Yn(" so the user can write Y1(2). */
static int yvars_count(void) { return NFUNC; }
static void yvars_get(int i, menu_item_t *out)
{
    static char label[48], ins[16];
    char disp[40];
    ed_display(&g.f[i].ed, disp, sizeof disp);
    snprintf(ins, sizeof ins, "Y%d(", i + 1);
    snprintf(label, sizeof label, "Y%d=%.30s", i + 1, disp);
    *out = (menu_item_t){label, ins, NULL, i, "vars"};
}
const vars_provider_t vars_yvars = {"Y-VARS", yvars_count, yvars_get};

/* Define Y1(x)..Y6(x) in the CAS (cas_eval_quiet: ans is untouched); only the
 * functions whose text changed since the last call are redefined. */
static uint32_t synced[NFUNC];      /* hash of the CAS text last defined for Yn */
static bool synced_valid[NFUNC];    /* false: (re)define on the next sync */

/* 32-bit FNV-1a hash of a string (detects a changed definition cheaply). */
static uint32_t hash(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s)
        h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

void yvars_invalidate(void) { memset(synced_valid, 0, sizeof synced_valid); }

/* Called before every home/RPN evaluation. An empty Y= line purges Yn so a
 * stale definition cannot be used. Errors (e.g. a syntax error in a Y= line)
 * are ignored here; using Yn then fails like any undefined function. Slider
 * values stepped on the graph are stored first (gx_params_flush). */
void yvars_sync(void)
{
    char cas[ED_MAX * 4], cmd[ED_MAX * 4 + 32], out[64];
    gx_params_flush();
    for (int i = 0; i < NFUNC; i++) {
        ed_cas(&g.f[i].ed, cas, sizeof cas);
        uint32_t h = hash(cas);
        if (synced_valid[i] && synced[i] == h)
            continue;
        if (cas[0])
            snprintf(cmd, sizeof cmd, "Y%d(x):=%s", i + 1, cas);
        else
            snprintf(cmd, sizeof cmd, "purge(Y%d)", i + 1);
        cas_eval_quiet(g.cas, cmd, out, sizeof out);
        synced[i] = h;
        synced_valid[i] = true;
    }
}

/* ------------------------------------------------------------ persistence */
/* Session writer for cas_save(). The CAS blob is the last one in the
 * session (persist_registry order), so it may use what the other blobs left
 * of the buffer, minus `reserve` bytes of headroom for the blob framing. */
typedef struct {
    pw_t *w;
    size_t reserve;
} cas_sink_t;

/* Append one or more complete text records; a record that does not fit is
 * dropped (that variable is lost after a reboot) and saving continues, so one
 * huge matrix cannot cost the whole CAS state. */
static int cas_sink(void *user, const char *data, size_t n)
{
    cas_sink_t *s = user;
    if (s->w->len + n + s->reserve > s->w->cap)
        return 0; /* drop this record (too big for the session), keep going */
    pw_bytes(s->w, data, n);
    return 0;
}

static void cas_save_blob(pw_t *w)
{
    cas_sink_t s = {w, 64};
    cas_save(g.cas, cas_sink, &s);
}

/* Restore the records (added to the fresh CAS of app_init) and make the next
 * yvars_sync() redefine Y1..Y6. Bad records are skipped by cas_restore(). */
static void cas_load_blob(pr_t *r, unsigned version)
{
    cas_restore(g.cas, (const char *)r->buf, r->len);
    memset(synced_valid, 0, sizeof synced_valid);
}

/* MEM > Reset all: empty CAS state. */
static void cas_reset_blob(void)
{
    cas_reset(g.cas);
    memset(synced_valid, 0, sizeof synced_valid);
}

const persist_def_t persist_cas = {"cas", 1, cas_save_blob, cas_load_blob, cas_reset_blob};
