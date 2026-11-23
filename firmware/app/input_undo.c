/*
 * Undo / redo for every editor (interface: app/input.h; help page "undo").
 *
 * The user drives it from the MENU context menu: MENU F1 undoes, MENU F2
 * redoes the last change of the current screen's editor (app/input_clip.c
 * runs the edit bar). Every editor_t is covered without code in the screens:
 * editor.c calls ed_change_hook (installed here) just before a line changes,
 * and the hook copies the line as it was. Each copy is one undo step.
 *
 * What becomes a step: recording is on only while app.c dispatches a key or
 * touch event (input_event_begin/end), and each editor gets at most one
 * snapshot per event, so a key that changes a line in several primitive
 * operations (a template, ENTER clearing the line, a menu insertion, a paste)
 * is undone at once. Consecutive keys that type letters, digits or "." into
 * the same editor coalesce into one step ("123" is one step, "123+4" three),
 * like the word-wise undo of desktop editors; any other event in between
 * (cursor keys too) ends the run.
 *
 * Storage: one byte pool (from input_init's arena carve-out) holding records
 * oldest first; a record is a rec_t header plus the line's tokens. Undo and
 * redo records share the pool, told apart by `kind`; when the pool is full
 * the oldest records are dropped. Records refer to editors by address only:
 * editors that are reused for another field (the matrix and list editors
 * edit one cell at a time, forms one row) call input_undo_forget() /
 * input_undo_new_field() when they move to another field, so undo only ever
 * affects the current field. Scratch editors used during evaluation are recorded too and age
 * out. Token ids are stable while the firmware runs, so the raw ids are
 * stored; nothing here is saved with the session (the log starts empty after
 * power-on).
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os") /* flash budget: UI code, size over speed */
#endif
#include <ctype.h>
#include <string.h>
#include "input.h"
#include "registry.h"
#include "session.h"

enum { R_UNDO = 0, R_REDO = 1 };

/* Record header; `len` tokens (uint16_t) follow, the whole record padded to
 * a multiple of 4 bytes. Copied with memcpy (the pool is a byte array). */
typedef struct {
    const editor_t *e; /* the editor the snapshot belongs to */
    uint32_t ev;       /* event number of the last change it covers (coalescing) */
    uint16_t len, cur; /* the line's length and cursor at snapshot time */
    uint8_t kind;      /* R_UNDO / R_REDO */
    uint8_t word;      /* made by a word-character key (letters, digits, ".") */
    uint16_t size;     /* bytes of the whole record */
} rec_t;

static uint8_t *pool;    /* the record pool (input_init) */
static size_t pool_cap;  /* its size */
static size_t pool_used; /* bytes in use, records packed from offset 0 */

static bool recording;   /* inside an event */
static bool paused;      /* input_record_pause() */
static uint32_t ev_no;   /* event counter */
static bool ev_word;     /* the current event types one word character */
static const editor_t *noted_e; /* editor already snapshotted in event noted_ev */
static uint32_t noted_ev;

static void hook(editor_t *e);

/* Set up the pool (called by input_init) and install the editor hook. */
void undo_init(void *mem, size_t size)
{
    pool = mem;
    pool_cap = size;
    pool_used = 0;
    recording = paused = false;
    noted_e = NULL;
    ed_change_hook = hook;
}

static void get(size_t off, rec_t *r) { memcpy(r, pool + off, sizeof *r); }
static void put(size_t off, const rec_t *r) { memcpy(pool + off, r, sizeof *r); }

/* Remove the record at `off` (later records move down). */
static void drop_at(size_t off)
{
    rec_t r;
    get(off, &r);
    memmove(pool + off, pool + off + r.size, pool_used - off - r.size);
    pool_used -= r.size;
}

/* Offset of the newest record of `kind` for `e` (any editor if NULL), or -1.
 * Records are scanned oldest first; the pool is at most a few KB. */
static long newest(int kind, const editor_t *e)
{
    long found = -1;
    for (size_t off = 0; off < pool_used;) {
        rec_t r;
        get(off, &r);
        if (r.kind == kind && (!e || r.e == e))
            found = (long)off;
        off += r.size;
    }
    return found;
}

/* Drop every record of `kind` for `e` (a new change makes the redo steps of
 * that editor meaningless). */
static void drop_all(int kind, const editor_t *e)
{
    for (size_t off = 0; off < pool_used;) {
        rec_t r;
        get(off, &r);
        if (r.kind == kind && r.e == e)
            drop_at(off);
        else
            off += r.size;
    }
}

/* Append a snapshot of `e` as a record of `kind`, dropping the oldest records
 * until it fits. A line that can never fit is not recorded. */
static void push(int kind, const editor_t *e, bool word)
{
    size_t need = (sizeof(rec_t) + (size_t)e->len * sizeof e->t[0] + 3) & ~(size_t)3;
    if (!pool || need > pool_cap)
        return;
    while (pool_used + need > pool_cap)
        drop_at(0);
    rec_t r = {e, ev_no, (uint16_t)e->len, (uint16_t)e->cur, (uint8_t)kind, word, (uint16_t)need};
    put(pool_used, &r);
    memcpy(pool + pool_used + sizeof r, e->t, (size_t)e->len * sizeof e->t[0]);
    pool_used += need;
}

/* ed_change_hook: `e` is about to change. Outside events, while paused, for
 * an editor already noted in this event, or for a line that is not a valid
 * editor state (scratch editors on the stack are sometimes cleared before
 * they were ever initialised), nothing is recorded. A word-character key
 * that continues the previous event's word run on this editor only extends
 * that record; anything else starts a new step. Either way the editor's
 * redo steps are dropped. */
static void hook(editor_t *e)
{
    if (!recording || paused || !pool)
        return;
    if (e->len < 0 || e->len > ED_MAX || e->cur < 0 || e->cur > e->len)
        return;
    if (e == noted_e && noted_ev == ev_no)
        return;
    noted_e = e;
    noted_ev = ev_no;
    drop_all(R_REDO, e);
    long off = newest(R_UNDO, e);
    if (ev_word && off >= 0) {
        rec_t r;
        get((size_t)off, &r);
        if (r.word && r.ev + 1 == ev_no) {
            r.ev = ev_no; /* same word: extend the step */
            put((size_t)off, &r);
            return;
        }
    }
    push(R_UNDO, e, ev_word);
}

/* Event bracket (app.c). A "word" event is a key whose text is one letter,
 * digit or "." (ui_key_text with the ALPHA case of this key press). */
void input_event_begin(int key, int mod)
{
    ev_no++;
    char t[16];
    ev_word = key && ui_key_text(key, mod, g.alpha_snapshot_for_key, t, sizeof t) && t[0] && !t[1] &&
              (isalnum((unsigned char)t[0]) || t[0] == '.');
    recording = true;
}

void input_event_end(void) { recording = false; }

void input_record_pause(bool pause) { paused = pause; }

/* Replace `e` with the record at `off` and remove the record; the current
 * line is first pushed as a record of kind `save` (no coalescing), so the
 * step can be redone / undone again. The record is copied out before the
 * push, which may drop old records and move the rest. */
static bool swap_with(editor_t *e, int take, int save)
{
    long off = newest(take, e);
    if (off < 0)
        return false;
    rec_t r;
    get((size_t)off, &r);
    uint16_t t[ED_MAX];
    int len = r.len <= ED_MAX ? r.len : ED_MAX;
    memcpy(t, pool + off + sizeof r, (size_t)len * sizeof t[0]);
    drop_at((size_t)off);
    push(save, e, false);
    memcpy(e->t, t, (size_t)len * sizeof t[0]);
    e->len = len;
    e->cur = r.cur <= len ? r.cur : len;
    e->scroll = 0;
    noted_e = NULL; /* the next change starts a new step */
    return true;
}

void input_undo_forget(const editor_t *e)
{
    drop_all(R_UNDO, e);
    drop_all(R_REDO, e);
    if (noted_e == e)
        noted_e = NULL;
}

void input_undo_new_field(editor_t *e)
{
    input_undo_forget(e);
    e->len = e->cur = e->scroll = 0; /* not ed_clear(): that would record the old field's text */
}

bool input_undo(editor_t *e) { return e && swap_with(e, R_UNDO, R_REDO); }
bool input_redo(editor_t *e) { return e && swap_with(e, R_REDO, R_UNDO); }

/* Records of `kind` for `e` (all editors if NULL). */
static int count(int kind, const editor_t *e)
{
    int n = 0;
    for (size_t off = 0; off < pool_used;) {
        rec_t r;
        get(off, &r);
        n += r.kind == kind && (!e || r.e == e);
        off += r.size;
    }
    return n;
}

int input_undo_count(const editor_t *e) { return count(R_UNDO, e); }
int input_redo_count(const editor_t *e) { return count(R_REDO, e); }

/* ------------------------------------------------------------ state guard
 * (input.h) The snapshot is the blob a persist_def_t writes for the saved
 * session, so any registered state can be guarded without an API of its own;
 * restoring calls the blob's load(), which resets the state first. Only one
 * snapshot exists; it is `armed` once the guarded key destroyed something. */
static uint8_t *gbuf;             /* snapshot buffer (input_init; NULL: no guard) */
static size_t gcap, glen;
static const persist_def_t *gdef; /* the blob snapshotted, NULL: none */
static const screen_t *gscr;      /* the screen whose undo restores it */
static bool armed;

void undo_guard_init(void *mem, size_t size)
{
    gbuf = mem;
    gcap = size;
    gdef = NULL;
    armed = false;
}

/* Snapshot blob `persist_key` (silently nothing if it is unknown or does not fit). */
void input_state_guard(const char *persist_key, const screen_t *s)
{
    gdef = NULL;
    armed = false;
    if (!gbuf)
        return;
    for (int i = 0; i < persist_registry_count; i++)
        if (!strcmp(persist_registry[i]->key, persist_key)) {
            pw_t w = {gbuf, gcap, 0, false};
            persist_registry[i]->save(&w);
            if (!w.overflow) {
                gdef = persist_registry[i];
                gscr = s;
                glen = w.len;
            }
            return;
        }
}

void input_state_guard_keep(bool keep)
{
    armed = keep && gdef;
    if (!armed)
        gdef = NULL;
}

void input_state_guard_forget(void)
{
    armed = false;
    gdef = NULL;
}

/* Restore the snapshot through the blob's load() (not recorded as an editor step). */
bool input_state_guard_undo(const screen_t *s)
{
    if (!armed || !gdef || s != gscr)
        return false;
    pr_t r = {gbuf, glen, 0, false};
    paused = true;
    gdef->load(&r, gdef->version);
    paused = false;
    input_state_guard_forget();
    return true;
}
