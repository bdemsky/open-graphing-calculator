/*
 * Global keys: what a key does on every screen before the current screen's
 * own key handler sees it. The keyboard itself (legends, what each key types,
 * which global action it runs in which layer) is data in keys/layout.py,
 * generated into key_info[] (core/gen/keymap_gen.c); this file only resolves
 * the action names used there.
 *
 * Lookup order (keys_dispatch):
 *   1. hotkeys of registered apps (registry.c: app_registry[].keys), for keys
 *      without a printed function (layout.py actions are preferred);
 *   2. the action of the key in that layer (key_info[].action / .shift_action).
 * Soft keys F1..F5 never get here (app/softkeys.c).
 */
#include <stdio.h>
#include <string.h>
#include "hal.h"
#include "input.h"
#include "registry.h"
#include "rpn.h"
#include "ui.h"

/* HOME: back to the home screen (or the RPN screen that replaces it). */
static void quit(void) { ui_switch(ui_home()); }

/* SHIFT UP / SHIFT DOWN: change the backlight by d percent, clamped to
 * 5..100 % (never fully dark). The value in g.backlight is saved with the
 * settings. */
static void backlight_step(int d)
{
    int b = hal_backlight_get() + d;
    if (b < 5)
        b = 5;
    if (b > 100)
        b = 100;
    hal_backlight_set(b);
    g.backlight = b;
}
static void brighter(void) { backlight_step(10); }
static void dimmer(void) { backlight_step(-10); }
static void settings(void) { ui_switch(&scr_mode); }
static void catalog(void) { catalog_open(); } /* structured, searchable (app/input_catalog.c) */
static void cas_menu(void) { menu_open(&menu_cas); }
static void math_menu(void) { menu_open(&menu_math); }
static void angle_menu(void) { menu_open(&menu_angle); }

/* Launch a registered app by id (silently nothing if it is not registered). */
static void open_app(const char *id)
{
    const app_def_t *a = app_find(id);
    if (a)
        a->open();
}
static void matrix(void) { open_app("matrix"); }

/* SHIFT CLEAR ("clear all"): on every screen, ask, then clear the CAS
 * variables, home entry line and history (apps.c mem_clear_all). The RPN
 * screen keeps its own meaning, clear the stack (undoable with its undo soft
 * key), which is what "clear all" means there. */
static void clear_all(void)
{
    const screen_t *s = ui_current();
    if (s == &scr_rpn && s->key)
        s->key(KEY_CLEAR, MOD_SHIFT);
    else
        mem_clear_all();
}

/* action names used in keys/layout.py (tools/gen_keymap.py parses this table
 * and fails the build if layout.py names an action that is not here) */
static const struct {
    const char *name;
    void (*fn)(void);
} actions[] = {
    {"home", quit},          {"apps", apps_open},     {"menu", ui_context_menu}, {"settings", settings},
    {"catalog", catalog},    {"help", help_context},  {"cas", cas_menu},         {"math", math_menu},
    {"vars", vars_open},     {"matrix", matrix},      {"units", units_open},     {"const", units_open_constants},
    {"angle", angle_menu},   {"off", app_request_off}, {"brighter", brighter},   {"dimmer", dimmer},
    {"mem", mem_open},       {"clearall", clear_all},
};
#define NACT ((int)(sizeof actions / sizeof actions[0]))

/* Run the global action `name` (exported in ui.h for other callers). Returns
 * false if no such action exists. */
bool keys_action(const char *name)
{
    for (int i = 0; i < NACT; i++)
        if (!strcmp(actions[i].name, name)) {
            actions[i].fn();
            return true;
        }
    return false;
}

/* The legend printed on the keycap for `key`: the main legend (MOD_NONE) or
 * the SHIFT legend above the key (MOD_SHIFT). NULL for MOD_ALPHA or an
 * invalid key code. */
const char *keys_legend(int key, int mod)
{
    if (key <= 0 || key >= KEY_COUNT)
        return NULL;
    return mod == MOD_SHIFT ? key_info[key].shift : mod == MOD_NONE ? key_info[key].legend : NULL;
}

/* Called by app.c for every key (except SHIFT, ALPHA and the soft keys)
 * before the current screen sees it. Returns true if the key was consumed by
 * an app hotkey or a global action; false lets the screen handle it. A layer
 * that names an action missing from actions[] (only possible if the generated
 * table is stale) shows a "not implemented" message instead. ALPHA-layer keys
 * never run actions: they type letters. */
bool keys_dispatch(int key, int mod)
{
    for (int a = 0; a < app_registry_count; a++) {
        const app_def_t *app = app_registry[a];
        for (int k = 0; k < 3; k++)
            if (app->keys[k].key && app->keys[k].key == key && app->keys[k].mod == mod) {
                app->open();
                return true;
            }
    }
    if (key <= 0 || key >= KEY_COUNT)
        return false;
    const char *act = mod == MOD_SHIFT ? key_info[key].shift_action : mod == MOD_NONE ? key_info[key].action : NULL;
    if (!act)
        return false;
    if (!keys_action(act)) {
        static char msg[64];
        snprintf(msg, sizeof msg, "%s is not implemented yet.", keys_legend(key, mod));
        ui_message("NOT AVAILABLE", msg);
    }
    return true;
}
