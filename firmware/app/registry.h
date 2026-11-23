/*
 * Extension points of the calculator application. Every table is defined in
 * app/registry.c; a new feature adds its own source files plus ONE line in the
 * matching table there. See app/EXTENDING.md.
 */
#ifndef REGISTRY_H
#define REGISTRY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "session.h"
#include "ui.h"

/* ------------------------------------------------------------------ apps
 * An entry in the APPS menu, optionally with keys that open it from anywhere
 * (they take precedence over the built-in key table, app/keys.c), state that
 * is saved across power-off, and VARS entries. */
typedef struct {
    uint8_t key, mod; /* key code (core/gen/keymap_gen.h) and MOD_NONE / MOD_SHIFT / MOD_ALPHA */
} hotkey_t;

typedef struct app_def {
    const char *id;     /* stable short id ("stat"), used by the console `app` command */
    const char *name;   /* label in the APPS menu */
    void (*open)(void); /* launch (usually ui_switch(&scr_x) or menu_open(&menu_x)) */
    hotkey_t keys[3];   /* keys that launch it ({0} = unused) */
    const char *help;   /* help topic of the app */
} app_def_t;

extern const app_def_t *const app_registry[];
extern const int app_registry_count;
const app_def_t *app_find(const char *id); /* by id; NULL if no such app */

/* ------------------------------------------------------------------ state
 * Blobs saved on power-off / restored at boot, in table order (see
 * session.h). Blobs are saved and loaded in this order; a blob that no longer
 * fits in the session buffer is dropped (the ones after it may still fit), so
 * the large, variable-size CAS blob comes last. */
extern const persist_def_t *const persist_registry[];
extern const int persist_registry_count;

/* ------------------------------------------------------------------ VARS
 * A category on the first page of the VARS menu (app/vars.c). Selecting an item
 * inserts its `insert` text (and `suffix` after the cursor) into the current
 * editor, so the text must be something the CAS can evaluate. */
typedef struct vars_provider {
    const char *title;                    /* category label, e.g. "Statistics" */
    int (*count)(void);                   /* number of items now (0 hides the category) */
    void (*get)(int i, menu_item_t *out); /* item i; strings valid until the next call */
} vars_provider_t;

extern const vars_provider_t *const vars_registry[];
extern const int vars_registry_count;

/* ------------------------------------------------------------------ MODE
 * A row of the MODE (settings) screen, app/mode.c: a label and up to 12
 * options, one selected. The row only reads/writes the setting through get()
 * and set(); saving it is up to the feature's own persist_def_t. */
typedef struct mode_row {
    const char *label;          /* left column, <= 8 chars */
    const char *opts[12];       /* option labels (short) */
    int nopts;
    int (*get)(void);           /* index of the current option */
    void (*set)(int option);    /* select an option (then app_apply_settings() runs) */
    const char *help;           /* help topic (NULL: the MODE page) */
} mode_row_t;

extern const mode_row_t *const mode_registry[];
extern const int mode_registry_count;
const mode_row_t *mode_current_row(void); /* row highlighted on the MODE screen */

#endif
