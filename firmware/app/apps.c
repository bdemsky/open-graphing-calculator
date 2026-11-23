/* APPS menu (registered apps, registry.c) and the MEM menu (APPS > Memory): about,
 * content modules (install / manage / remove: app/modules_ui.c), clearing
 * variables/history, resetting settings or everything.
 *
 * APPS is a dynamic menu over app_registry[] in table order. Also defines the
 * built-in app entries Graph, Table, Settings and Memory.
 *
 * MEM: About and "Save session now" act at once; the destructive items ask
 * "Are you sure?" first (a second menu, default No) and remember the pending
 * action in `pending`. Results are reported with ui_message(). */
#include <stdio.h>
#include <string.h>
#include "calc.h"
#include "evlog.h"
#include "input.h"
#include "hal.h"
#include "registry.h"
#include "safemode.h"
#include "modules_ui.h"
#include "session.h"
#include "ui.h"
#include "usb_app.h"

/* ------------------------------------------------------------ APPS */
/* Graph and 3D graph are not listed: the graph key (home F1, its 2D/3D switch) and graph cells
 * (ENTER) open them; CATALOG and the console `app` still do. */
extern const app_def_t app_graph, app_graph3d;
static bool listed(const app_def_t *a) { return a != &app_graph && a != &app_graph3d; }
static int apps_count(void)
{
    int n = 0;
    for (int i = 0; i < app_registry_count; i++)
        n += listed(app_registry[i]);
    return n;
}

static void apps_get(int k, menu_item_t *out)
{
    int i = 0;
    for (; i < app_registry_count - 1; i++)
        if (listed(app_registry[i]) && !k--)
            break;
    const app_def_t *a = app_registry[i];
    *out = (menu_item_t){a->name, NULL, NULL, i, a->help};
}

/* Close the APPS menu (returning to the screen it was opened from, so that
 * screen becomes `prev` for the app), then launch the app. */
static void apps_select(const menu_item_t *it)
{
    const app_def_t *a = app_registry[it->action];
    ui_returning = true; /* leave the menu without disturbing the screen below */
    ui_switch(menu_origin() ? menu_origin() : ui_home());
    ui_returning = false;
    a->open();
}

static const menu_def_t menu_apps = {"APPS", NULL, 0, apps_select, false, "apps", apps_count, apps_get};

void apps_open(void) { menu_open(&menu_apps); }

/* built-in apps */
/* Graph (CATALOG, the console app graph; not listed in APPS): from the home screen, a graph cell of its notebook */
static void graph_open(void)
{
    if (ui_current() == &scr_home)
        home_graph_new(false);
    else
        ui_switch(&scr_graph);
}
static void table_open(void) { ui_switch(&scr_table); }
const app_def_t app_graph = {"graph", "Graph", graph_open, {{0}}, "graph"};
const app_def_t app_table = {"table", "Table", table_open, {{0}}, "table"};
static void settings_open(void) { ui_switch(&scr_mode); }
const app_def_t app_settings = {"settings", "Settings", settings_open, {{0}}, "mode"};

/* ------------------------------------------------------------ MEM */
enum { M_ABOUT = 1, M_CLRVARS, M_CLRHIST, M_RESET_SET, M_RESET_ALL, M_SAVE, M_YES, M_NO, M_INSTALL, M_MODULES,
       M_RMCONTENT, M_USB, M_CLREVLOG };

static int pending; /* action waiting for confirmation */

/* MEM > About: a generated help page (firmware version, platform and build
 * tier, the content modules and safe mode, how the optional devices answered
 * at boot, CAS heap use, saved-session status). The buffer is static:
 * help_show_text() keeps the pointer to show the page again when the user
 * comes back to it from a link. */
extern const char *const build_tier; /* $(BUILD)/gen/config_gen.c (tools/kconfig.py) */
/* "## Last events": the event log (core/evlog.h), newest first. The texts
 * come from anywhere (the console `evlog add`), so the help markup's link,
 * key and code brackets in them are shown as plain parentheses. */
static void about_events(char *out, size_t n)
{
    size_t o = (size_t)snprintf(out, n, "## Last events\n");
    int k = evlog_count();
    if (!k && o < n)
        o += (size_t)snprintf(out + o, n - o, "- none\n");
    char line[112];
    for (int i = k - 1; i >= 0 && o < n; i--) {
        evlog_format(evlog_get(i), line, sizeof line);
        for (char *c = line; *c; c++)
            if (*c == '[' || *c == '{')
                *c = '(';
            else if (*c == ']' || *c == '}')
                *c = ')';
            else if (*c == '`')
                *c = '\'';
        o += (size_t)snprintf(out + o, n - o, "- %s\n", line);
    }
}

void app_about(void)
{
    static char t[2560 + EVLOG_N * 100];
    cas_mem_stats ms;
    cas_mem(g.cas, &ms);
    char sess[256] = "";
    session_state_text(sess, sizeof sess);
    char content[1536];
    modules_about(content, sizeof content);
    /* why the calculator last started, and the last crash: an unexpected
     * restart (a crash or the watchdog) can be read off this page */
    char crash[224] = "none since power-on";
    hal_crash_text(crash, sizeof crash);
    snprintf(t, sizeof t,
             "# Open Graphing Calculator\n\nFirmware `%s` on %s, %s tier.\n\n"
             "%s\n"
             "## Memory\n"
             "- CAS: %lu KB used of %lu KB\n"
             "- %s\n\n"
             "## Keys\n"
             "{shift} {menu} opens help for the current screen; [[keys|Keyboard]], [[index|Help index]].\n\n"
             "## Last start\n"
             "- %s\n"
             "- Crashes: %s\n\n",
             calc_version(), hal_platform_name(), build_tier, content, (unsigned long)(ms.used >> 10),
             (unsigned long)(ms.arena_size >> 10), sess, safemode_reset_text(), crash);
    size_t o = strlen(t);
    about_events(t + o, sizeof t - o);
    help_show_text("About", t);
}

/* MEM > Reset settings puts every MODE-screen setting back to its default:
 * the blobs that hold only settings are reset (settings: angle ... insert
 * mode; theme: theme and font; textbook: display, preview, repeat), and the
 * rows whose value is saved together with data (GRAPH with the graph entries,
 * ENTRY with the RPN stack) are set to their default, option 0, which keeps
 * the data. */
static const char *const settings_blobs[] = {"settings", "theme", "textbook"};
static const char *const settings_rows[] = {"GRAPH", "ENTRY"};

static void reset_settings(void)
{
    for (int i = 0; i < persist_registry_count; i++)
        for (size_t k = 0; k < sizeof settings_blobs / sizeof settings_blobs[0]; k++)
            if (!strcmp(persist_registry[i]->key, settings_blobs[k]) && persist_registry[i]->reset)
                persist_registry[i]->reset();
    for (int i = 0; i < mode_registry_count; i++)
        for (size_t k = 0; k < sizeof settings_rows / sizeof settings_rows[0]; k++)
            if (!strcmp(mode_registry[i]->label, settings_rows[k]))
                mode_registry[i]->set(0);
}

/* Carry out a confirmed MEM action:
 *   Clear CAS variables  cas_reset() discards all CAS variables, functions
 *                        and history (the compiled graph functions are
 *                        freed first); Y1..Y6 are redefined in the CAS on
 *                        the next sync.
 *   Clear home history   home_reset().
 *   Reset settings       every MODE setting to its default (reset_settings);
 *                        variables, history, lists, graph entries and the
 *                        RPN stack are kept.
 *   Reset all            every registered blob's reset(), an empty CAS and
 *                        an erased flash log (the next save writes anew).
 * The message box then returns to the screen MEM was opened from. */
static void do_action(int a);

/* SHIFT CLEAR ("clear all", keys.c), after the user said Yes: a fresh
 * calculation session. Deletes the CAS variables and functions (as MEM >
 * Clear CAS variables), the home entry line and history; settings, Y=,
 * lists, matrices, apps and the saved session file are kept (MEM > Reset
 * all covers those). The screen it was asked on is redrawn, and a toast
 * says what happened. */
static void clear_all_yes(void)
{
    funcs_invalidate(); /* before the reset: frees the compiled functions */
    cas_reset(g.cas);
    yvars_invalidate();
    home_reset();
    input_state_guard_forget(); /* an older history snapshot must not come back */
    ui_returning = true;
    ui_switch(ui_current());
    ui_returning = false;
    ui_toast("variables and history cleared");
}

void mem_clear_all(void) { ui_confirm("Clear variables + history?", clear_all_yes); }

static void do_action(int a)
{
    switch (a) {
    case M_CLRVARS:
        funcs_invalidate(); /* before the reset: frees the compiled functions */
        cas_reset(g.cas);
        yvars_invalidate();
        ui_message("MEM", "All CAS variables and functions were deleted.");
        break;
    case M_CLRHIST:
        home_reset();
        ui_message("MEM", "Home history cleared.");
        break;
    case M_RESET_SET:
        reset_settings();
        app_apply_settings();
        ui_message("MEM", "Settings reset to defaults.");
        break;
    case M_CLREVLOG:
        evlog_clear();
        session_save(false); /* else the saved copy would come back at the next start */
        ui_message("MEM", "Event log cleared.");
        break;
    case M_RESET_ALL:
        for (int i = 0; i < persist_registry_count; i++)
            if (persist_registry[i]->reset)
                persist_registry[i]->reset();
        cas_reset(g.cas);
        yvars_invalidate();
        app_apply_settings();
        session_clear();
        ui_message("MEM", "Everything was reset to factory defaults.");
        break;
    }
}

static const menu_item_t confirm_items[] = {
    {"No", NULL, NULL, M_NO, "mem"},
    {"Yes", NULL, NULL, M_YES, "mem"},
};

/* Yes: run the pending action; No: just leave the menus. */
static void confirm_select(const menu_item_t *it)
{
    if (it->action == M_YES)
        do_action(pending);
    else {
        ui_returning = true;
        ui_switch(menu_origin() ? menu_origin() : ui_home());
        ui_returning = false;
    }
}

static const menu_def_t menu_confirm = {"Are you sure?", confirm_items, 2, confirm_select, false, "mem"};

static const menu_item_t mem_items[] = {
    {"About", NULL, NULL, M_ABOUT, "mem"},
    {"Save session now", NULL, NULL, M_SAVE, "mem"},
    {"Clear CAS variables", NULL, NULL, M_CLRVARS, "mem"},
    {"Clear home history", NULL, NULL, M_CLRHIST, "mem"},
    {"Reset settings", NULL, NULL, M_RESET_SET, "mem"},
    {"Reset all", NULL, NULL, M_RESET_ALL, "mem"},
    {"Install content", NULL, NULL, M_INSTALL, "install-content"},
    {"Modules", NULL, NULL, M_MODULES, "install-content"},
    {"Remove content", NULL, NULL, M_RMCONTENT, "install-content"},
    {"Clear event log", NULL, NULL, M_CLREVLOG, "mem"},
    {"USB status", NULL, NULL, M_USB, "usb"},
};

/* MEM item chosen: About and Save act at once (a forced save, written even
 * if unchanged); everything else goes through the confirmation menu. */
static void mem_select(const menu_item_t *it)
{
    switch (it->action) {
    case M_ABOUT:
        app_about();
        break;
    case M_INSTALL:
        modules_install_all();
        break;
    case M_MODULES:
        modules_open();
        break;
    case M_RMCONTENT:
        modules_remove_all();
        break;
    case M_USB:
        usb_app_status_open(); /* app/usb_app.c */
        break;
    case M_SAVE: {
        int rc = session_save(true);
        ui_message("MEM", rc == 0 ? "Session saved." : "Saving failed (no flash?).");
        break;
    }
    default:
        pending = it->action;
        menu_open(&menu_confirm);
        break;
    }
}

static const menu_def_t menu_mem = {"MEMORY", mem_items, sizeof mem_items / sizeof mem_items[0], mem_select, false,
                                    "mem"};

void mem_open(void) { menu_open(&menu_mem); }
const app_def_t app_mem = {"mem", "Memory", mem_open, {{0}}, "mem"};
