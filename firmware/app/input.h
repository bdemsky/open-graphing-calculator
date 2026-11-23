/*
 * Input comforts shared by every screen that has an editor: undo/redo, a
 * clipboard, the structured CATALOG with type-to-search, and a searchable
 * home history. Help pages: help/input.txt.
 *
 * Keys (keys/layout.py is not changed; everything goes through the MENU key):
 *   MENU            the context menu of the current screen; while it is open
 *                   the soft keys are the edit bar (input_edit_bar):
 *   MENU F1 / F2    undo / redo the last change of the screen's editor
 *   MENU F3 / F4    copy / paste (MENU SHIFT F1: cut, SHIFT F2: older clips)
 *   MENU F5         search the home history
 *   SHIFT APPS      CATALOG: letters filter as you type, best matches first,
 *                   LEFT/RIGHT or F3 choose a category, DEL erases the query
 * So MENU acts like a prefix key, the way SHIFT does, without a new keycap.
 *
 * Files and where the state lives:
 *   input_undo.c     undo/redo: snapshots of editors in a byte pool carved
 *                    from the arena (not saved: it ends with the session)
 *   input_clip.c     clipboard ring (arena; saved as the "clip" blob), copy
 *                    and paste sources/targets, the edit bar, `state` keys
 *   input_catalog.c  CATALOG categories, the fuzzy matcher, the filtered and
 *                    ranked index (arena)
 *   input_hist.c     the HISTORY search menu over the home history
 * Framework hooks: ed_change_hook (app/editor.h) feeds the undo log;
 * input_event_begin/end are called by app.c around every key and touch
 * event; the menus use menu_def_t.filter_key / .query (app/ui.h) for the
 * typed query; softkeys.c asks input_context_bar() for the context menu's bar.
 */
#ifndef INPUT_H
#define INPUT_H
#include <stdbool.h>
#include <stddef.h>
#include "ui.h"

/* ------------------------------------------------------------ workspace */
/* Carve the module's buffers from `work` (from app_init, before the session
 * is restored); returns the bytes used: INPUT_WORK in a big arena,
 * INPUT_WORK_SMALL in the SDRAM-failure fallback arena. Clears the undo log
 * and the clipboard (the saved session refills the clipboard). */
#define INPUT_WORK (84u << 10) /* 64 KB of it: the state snapshot of input_state_guard() (the home notebook) */
#define INPUT_WORK_SMALL (4u << 10)
#define INPUT_GUARD_WORK (64u << 10)
size_t input_init(void *work, bool big);
/* Part of input_init (input_undo.c): the undo pool; installs ed_change_hook. */
void undo_init(void *mem, size_t size);
/* Part of input_init: buffer for input_state_guard() (size 0: no guard). */
void undo_guard_init(void *mem, size_t size);

/* ------------------------------------------------------------ state guard
 * One-step undo for a destructive key that is not an editor change, such as
 * CLEAR on an empty home line (it wipes the history). app.c snapshots a
 * persist_def_t blob (app/session.h) before the key, keeps the snapshot when
 * the key really destroyed something, and MENU F1 (undo) on that screen then
 * restores the blob with its load() before any editor step. Any later key
 * on the screen forgets it. */
void input_state_guard(const char *persist_key, const screen_t *s); /* snapshot before the key */
void input_state_guard_keep(bool keep); /* after the key: keep it as the undo step, or drop it */
void input_state_guard_forget(void);
/* Restore the kept snapshot if it belongs to screen s; false if there is none. */
bool input_state_guard_undo(const screen_t *s);

/* ------------------------------------------------------------ undo (input_undo.c) */
/* Called by app.c before a key or touch event is dispatched (key 0 for touch)
 * and after it: editor changes are recorded only in between, so loading a
 * session, previews and other background work never become undo steps. */
void input_event_begin(int key, int mod);
void input_event_end(void);
/* Undo / redo the last recorded change of editor `e` (restores its tokens and
 * cursor). Return false when there is nothing to undo / redo. No redraw. */
bool input_undo(editor_t *e);
bool input_redo(editor_t *e);
/* Number of undo / redo steps recorded for `e` (NULL: for all editors). */
int input_undo_count(const editor_t *e);
int input_redo_count(const editor_t *e);
/* Forget every undo / redo step of `e`: for editors that are reused for
 * another field (matrix cells, list cells, form rows), so undo never brings
 * back the text of a different field. */
void input_undo_forget(const editor_t *e);
/* Start editing a new field in shared editor `e`: forget its steps and empty
 * it without recording, so the field's first undo step starts from "". */
void input_undo_new_field(editor_t *e);
/* Stop / resume recording (nesting is not supported): for code that changes
 * editors as a side effect of reading them (screen_t.editor() of some
 * screens starts an edit). */
void input_record_pause(bool pause);

/* ------------------------------------------------------------ clipboard (input_clip.c) */
#define CLIP_N 8      /* clips kept, newest first */
#define CLIP_TEXT 256 /* bytes per clip (display text, UTF-8) */
/* Put `text` on the clipboard (newest). Empty texts are ignored; a text that
 * is already on it moves to the front instead of being stored twice. */
void clip_put(const char *text);
/* Clip i (0 = newest), "" if there is none; clip_count() clips are stored. */
const char *clip_get(int i);
int clip_count(void);
void clip_clear(void);
/* Copy from screen `s`: the selected home history item, the selected matrix
 * cell (the dimensions line: the whole matrix), the selected list cell or
 * column, or the line of the screen's editor. Returns false (clipboard
 * unchanged) when there is nothing to copy. */
bool clip_copy_from(const screen_t *s);
/* Paste `text` into screen `s` at the cursor of its editor (2D templates on
 * the home screen); a matrix literal pasted into the matrix editor's
 * dimensions line replaces the whole matrix. false: no target. */
bool clip_paste_into(const screen_t *s, const char *text);
/* Parse a matrix literal "[[a,b],[c,d]]": element i of row r starts at
 * text[off[r * maxc + c]] and has len[...] bytes. Returns false unless it is
 * a rectangular matrix of at most maxr x maxc; *rows / *cols get the size. */
bool clip_parse_matrix(const char *text, int maxr, int maxc, int *rows, int *cols, uint16_t *off,
                       uint16_t *len);
/* The soft keys of the MENU context menu opened on `origin`: the edit bar if
 * the screen has an editor, else NULL (the default menu bar). */
const softkeys_t *input_context_bar(const screen_t *origin);
extern const softkeys_t input_edit_bar;
/* Insert `text` (+ `suffix` after the cursor) into the editor of the screen
 * the open menu was opened from (or the home screen) and return there, like
 * choosing an item of a plain menu. `linear`: on the home screen convert it
 * to 2D templates (history and clipboard texts). */
void input_insert_into_origin(const char *text, const char *suffix, bool linear);
/* `state` keys: clip, clips, undo_n, redo_n, catalog_*, hist_*. */
void input_state(void (*kv)(const char *, const char *, void *), void *user);

/* ------------------------------------------------------------ catalog (input_catalog.c) */
/* Match score of `query` in `text` (case-insensitive), higher is better, 0 =
 * no match: exact > prefix > word start > substring > subsequence (scored by
 * runs and word starts; see input_catalog.c). Pure function. */
int fuzzy_score(const char *query, const char *text);
/* SHIFT APPS: the CATALOG, all categories, empty query. */
void catalog_open(void);
void catalog_state(void (*kv)(const char *, const char *, void *), void *user);
/* Work memory for the ranked index (from input_init): room for n entries. */
void catalog_set_work(uint16_t *idx, int16_t *score, int n);

/* ------------------------------------------------------------ history (input_hist.c) */
/* MENU F5: the HISTORY search menu over the home history, newest first. */
void hist_search_open(void);
void hist_state(void (*kv)(const char *, const char *, void *), void *user);

/* ------------------------------------------------------------ shared query line */
/* The typed filter of the CATALOG and HISTORY menus: key texts appended to
 * `s` (so ⌫ removes what one key typed, e.g. "sin("). */
#define QUERY_MAX 32
typedef struct {
    char s[QUERY_MAX];
    uint8_t step[16]; /* bytes each key added, for ⌫ */
    int nstep;
} query_t;
void query_clear(query_t *q);
/* Apply a key: text keys append, DEL removes the last key's text (SHIFT DEL:
 * all). Returns true if the query changed. */
bool query_key(query_t *q, int key, int mod);

#endif
