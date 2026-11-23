/*
 * Clipboard, copy and paste, and the edit bar of the MENU context menu
 * (interface: app/input.h; help pages "edit-bar" and "clipboard").
 *
 * Keys: MENU opens the current screen's context menu; while it is open the
 * soft keys are input_edit_bar:
 *   F1 undo   F2 redo   F3 copy   F4 paste   F5 history (history search)
 *   SHIFT: F1 cut   F2 clips (choose an older clip)   F4 help   F5 back
 * Every action works on the screen the menu was opened from and returns to
 * it, the way choosing a menu item does (so screens that write a scratch
 * editor back in enter(), like Y=, see a normal menu return).
 *
 * Copy sources, first that applies (clip_copy_from):
 *   home screen with a history item selected ({▲})  that input or result;
 *   matrix / linear-system grid (xgrid_shown)        the cell being edited,
 *        else the selected cell, on the dimensions line the whole matrix;
 *   solver / finance forms (xform_shown)             the selected field;
 *   list editor                                      cell, or the list on the header;
 *   RPN stack                                        the selected level, else the line, else level 1;
 *   spreadsheet                                      the entry line, else the cell's value;
 *   geometry                                         the highlighted (else newest) measurement;
 *   any other screen with an editor                  its line (Y=, forms, ...).
 * Paste inserts at the cursor (on the home screen as 2D templates via
 * m2d_insert_linear); a matrix literal pasted on the matrix editor's
 * dimensions line resizes the matrix and fills every cell.
 *
 * State: the clipboard is a ring of CLIP_N display texts (newest first) in
 * the arena carve-out of input_init(); it is saved with the session as the
 * "clip" blob, because a calculator is switched off (or powers itself off)
 * between uses far more often than a computer, and losing the clipboard
 * then would make it useless for moving results between apps. The undo log
 * is not saved (input_undo.c).
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os") /* flash budget: UI code, size over speed */
#endif
#include <stdio.h>
#include <string.h>
#include "input.h"
#include "forms_ui.h"
#include "geo_core.h"
#include "m2d.h"
#include "rpn.h"
#include "session.h"
#include "sheet.h"
#include "stat.h"
#include "xform.h"

static char *clips;   /* clip_cap texts of CLIP_TEXT bytes, clips[0] newest */
static int clip_cap;  /* CLIP_N, or 2 in the small arena */
static int nclip;

#define CLIP(i) (clips + (size_t)(i) * CLIP_TEXT)

/* Carve the work area (input.h): clipboard ring, catalog index (2 x int16
 * per entry), the state-guard snapshot (big arena only), then the undo pool
 * gets the rest (about 14 KB; 1.9 KB in the small arena). */
size_t input_init(void *work, bool big)
{
    size_t size = big ? INPUT_WORK : INPUT_WORK_SMALL;
    uint8_t *a = work;
    undo_guard_init(big ? a : NULL, big ? INPUT_GUARD_WORK : 0);
    if (big)
        a += INPUT_GUARD_WORK;
    clip_cap = big ? CLIP_N : 2;
    clips = (char *)a;
    a += (size_t)clip_cap * CLIP_TEXT;
    nclip = 0;
    int ncat = big ? 1024 : 400; /* catalog entries + app entries */
    catalog_set_work((uint16_t *)a, (int16_t *)(a + 2 * (size_t)ncat), ncat);
    a += 4 * (size_t)ncat;
    undo_init(a, size - (size_t)(a - (uint8_t *)work));
    return size;
}

/* ------------------------------------------------------------ the ring */
/* Copy s into a clip slot, cut at a UTF-8 character boundary. */
static void set_slot(int i, const char *s)
{
    size_t n = strlen(s);
    if (n >= CLIP_TEXT) {
        n = CLIP_TEXT - 1;
        while (n && ((unsigned char)s[n] & 0xC0) == 0x80) /* don't split a character */
            n--;
    }
    memcpy(CLIP(i), s, n);
    CLIP(i)[n] = 0;
}

void clip_put(const char *text)
{
    if (!clips || !text || !text[0])
        return;
    char t[CLIP_TEXT];
    snprintf(t, sizeof t, "%s", text); /* text may point into the ring */
    int k = 0;
    while (k < nclip && strcmp(CLIP(k), t)) /* already there: move it to the front */
        k++;
    if (k == nclip) {
        if (nclip < clip_cap)
            nclip++;
        k = nclip - 1; /* the oldest is dropped when full */
    }
    memmove(CLIP(1), CLIP(0), (size_t)k * CLIP_TEXT);
    set_slot(0, t);
}

const char *clip_get(int i) { return clips && i >= 0 && i < nclip ? CLIP(i) : ""; }
int clip_count(void) { return nclip; }
void clip_clear(void) { nclip = 0; }

/* "clip" blob, version 1: u8 count, then the texts newest first. */
static void clip_save(pw_t *w)
{
    pw_u8(w, (uint32_t)nclip);
    for (int i = 0; i < nclip; i++)
        pw_str(w, CLIP(i));
}

/* Restore the clips in order; extra or truncated entries are dropped. */
static void clip_load(pr_t *r, unsigned version)
{
    nclip = 0;
    unsigned n = pr_u8(r);
    for (unsigned i = 0; i < n && !r->err; i++) {
        char t[CLIP_TEXT];
        pr_str(r, t, sizeof t);
        if (!r->err && t[0] && nclip < clip_cap)
            set_slot(nclip++, t);
    }
}

const persist_def_t persist_clip = {"clip", 1, clip_save, clip_load, clip_clear};

/* ------------------------------------------------------------ copy / paste */
/* The editor of screen s, with the undo log paused: some screens start an
 * edit (and clear their line) when asked for their editor, which is not a
 * change the user made. NULL if the screen has none. */
static editor_t *editor_of(const screen_t *s)
{
    if (!s || !s->editor)
        return NULL;
    input_record_pause(true);
    editor_t *e = s->editor();
    input_record_pause(false);
    return e;
}

/* The whole grid as a matrix literal "[[a,b],[c,d]]" (cells as displayed);
 * false if it does not fit in n bytes. */
static bool grid_literal(xgrid_t *gr, char *out, size_t n)
{
    size_t o = 0;
    char c[96];
    for (int r = 0; r < gr->rows; r++)
        for (int k = 0; k < gr->cols; k++) {
            gr->cell(r, k, c, sizeof c);
            const char *pre = k ? "," : r ? "],[" : "[[";
            o += (size_t)snprintf(out + o, o < n ? n - o : 0, "%s%s", pre, c);
            if (o >= n)
                return false;
        }
    o += (size_t)snprintf(out + o, o < n ? n - o : 0, "]]");
    return o < n;
}

bool clip_copy_from(const screen_t *s)
{
    char b[ED_MAX * 4]; /* on the stack: AXI SRAM is nearly full */
    b[0] = 0;
    if (!s)
        return false;
    if (s == &scr_home && home_selection_text(b, sizeof b)) {
        /* the selected history item */
    } else if (xgrid_shown && xgrid_shown_on == s) {
        xgrid_t *gr = xgrid_shown;
        if (gr->editing)
            ed_display(&gr->ed, b, sizeof b);
        else if (gr->sr < 0) {
            if (!grid_literal(gr, b, sizeof b))
                b[0] = 0;
        } else
            gr->cell(gr->sr, gr->sc, b, sizeof b);
    } else if (xform_shown && xform_shown_on == s) {
        ed_display(&xform_shown->ed, b, sizeof b);
    } else if (s == &scr_stat_edit) {
        input_record_pause(true);
        stat_edit_clip_text(b, sizeof b);
        input_record_pause(false);
    } else if (s == &scr_rpn) {
        rpn_clip_text(b, sizeof b);
    } else if (s == &scr_forms) {
        forms_clip_text(b, sizeof b); /* the selected alternate form */
    } else if (s == &scr_sheet) {
        sheet_clip_text(b, sizeof b);
    } else if (s == &scr_geo) {
        geo_clip_text(b, sizeof b);
    } else {
        editor_t *e = editor_of(s);
        if (e)
            ed_display(e, b, sizeof b);
    }
    if (!b[0])
        return false;
    clip_put(b);
    return true;
}

bool clip_parse_matrix(const char *text, int maxr, int maxc, int *rows, int *cols, uint16_t *off,
                       uint16_t *len)
{
    const char *p = text;
    int r = 0, ncols = -1;
    while (*p == ' ')
        p++;
    if (*p++ != '[')
        return false;
    for (;;) { /* one row "[e,e,...]" */
        while (*p == ' ')
            p++;
        if (*p++ != '[')
            return false;
        int c = 0;
        for (;;) { /* one element: up to ',' or ']' outside any brackets */
            const char *st = p;
            int d = 0;
            while (*p && !(d == 0 && (*p == ',' || *p == ']'))) {
                if (*p == '(' || *p == '[' || *p == '{')
                    d++;
                else if ((*p == ')' || *p == ']' || *p == '}') && --d < 0)
                    return false;
                p++;
            }
            if (!*p || r >= maxr || c >= maxc)
                return false;
            const char *en = p;
            while (st < en && *st == ' ')
                st++;
            while (en > st && en[-1] == ' ')
                en--;
            if (en == st)
                return false;
            off[r * maxc + c] = (uint16_t)(st - text);
            len[r * maxc + c] = (uint16_t)(en - st);
            c++;
            if (*p++ == ']')
                break;
        }
        if (ncols < 0)
            ncols = c;
        else if (c != ncols)
            return false;
        r++;
        while (*p == ' ')
            p++;
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p++ != ']')
            return false;
        break;
    }
    while (*p == ' ')
        p++;
    if (*p)
        return false;
    *rows = r;
    *cols = ncols;
    return true;
}

/* Paste a matrix literal into grid gr: resize, then store every cell
 * (display text -> CAS text as if typed). Errors end up in the grid's
 * message line. false if text is not a matrix that fits the grid. */
static bool paste_matrix(xgrid_t *gr, const char *text)
{
    enum { MAXN = 16 };
    uint16_t off[MAXN * MAXN], len[MAXN * MAXN];
    int mr = gr->maxr < MAXN ? gr->maxr : MAXN, mc = gr->maxc < MAXN ? gr->maxc : MAXN, r, c;
    if (!clip_parse_matrix(text, mr, mc, &r, &c, off, len))
        return false;
    if (!gr->resize(r, c, gr->msg, sizeof gr->msg)) {
        gr->err = true;
        return true;
    }
    gr->rows = r;
    gr->cols = c;
    for (int i = 0; i < r; i++)
        for (int k = 0; k < c; k++) {
            char d[CLIP_TEXT], cas[CLIP_TEXT * 2];
            snprintf(d, sizeof d, "%.*s", len[i * mc + k], text + off[i * mc + k]);
            x_cas_of(d, cas, sizeof cas);
            if (!gr->set(i, k, cas, gr->msg, sizeof gr->msg)) {
                gr->err = true;
                return true;
            }
        }
    return true;
}

bool clip_paste_into(const screen_t *s, const char *text)
{
    if (!s || !text || !text[0])
        return false;
    xgrid_t *gr = xgrid_shown && xgrid_shown_on == s ? xgrid_shown : NULL;
    if (gr && !gr->editing && gr->sr < 0 && paste_matrix(gr, text))
        return true;
    editor_t *e = editor_of(s);
    if (!e)
        return false;
    if (s == &scr_home)
        m2d_insert_linear(e, text); /* Textbook: templates */
    else
        ed_insert_text(e, text, NULL);
    return true;
}

/* The screen menus insert into: the menu's origin if it has an editor,
 * else the home screen. */
static const screen_t *dest_screen(void)
{
    const screen_t *o = menu_origin();
    return o && o->editor ? o : ui_home();
}

/* Back to `dest` as a menu return (it keeps its state; enter() may write a
 * scratch editor back). */
static void return_to(const screen_t *dest)
{
    ui_returning = true;
    ui_switch(dest);
    ui_returning = false;
}

void input_insert_into_origin(const char *text, const char *suffix, bool linear)
{
    const screen_t *dest = dest_screen();
    editor_t *e = editor_of(dest);
    if (e && text) {
        if (linear && dest == &scr_home)
            m2d_insert_linear(e, text);
        else
            ed_insert_text(e, text, suffix);
    }
    return_to(dest);
}

/* ------------------------------------------------------------ clips menu */
static int clips_count(void) { return nclip; }
/* Item i: the clip itself (the label is the text; long ones run off the row). */
static void clips_get(int i, menu_item_t *out)
{
    *out = (menu_item_t){CLIP(i), CLIP(i), NULL, i, "clipboard"};
}
/* ENTER: paste the chosen clip (it becomes the newest) and return. */
static void clips_select(const menu_item_t *it)
{
    const screen_t *dest = dest_screen();
    char t[CLIP_TEXT];
    snprintf(t, sizeof t, "%s", clip_get(it->action));
    clip_put(t);
    clip_paste_into(dest, t);
    return_to(dest);
}
static const menu_def_t menu_clips = {"CLIPBOARD", NULL, 0, clips_select, false, "clipboard", clips_count,
                                      clips_get};

/* ------------------------------------------------------------ the edit bar */
/* Each action works on the context menu's origin, then closes the menu
 * (menu_close returns to the origin like a menu selection). */
/* Undo: a kept state-guard snapshot of the origin (the home history wiped by
 * CLEAR, app.c) comes first, then the editor's steps. Actions that change
 * nothing visible say so with a toast (ui_toast) after returning. */
static void eb_undo(void)
{
    const screen_t *o = menu_origin();
    bool st = input_state_guard_undo(o);
    bool ok = st || input_undo(editor_of(o));
    menu_close();
    ui_toast(st ? "history restored" : ok ? NULL : "nothing to undo");
}
static void eb_redo(void)
{
    bool ok = input_redo(editor_of(menu_origin()));
    menu_close();
    ui_toast(ok ? NULL : "nothing to redo");
}
static void eb_copy(void)
{
    bool ok = clip_copy_from(menu_origin());
    menu_close();
    ui_toast(ok ? "copied" : "nothing to copy here");
}
static void eb_paste(void)
{
    bool have = clip_count() > 0;
    if (have)
        clip_paste_into(menu_origin(), clip_get(0));
    menu_close();
    ui_toast(have ? NULL : "clipboard is empty: copy first");
}
/* Cut: copy the editor's line, then clear it (an undo step). */
static void eb_cut(void)
{
    editor_t *e = editor_of(menu_origin());
    bool ok = e && !ed_empty(e);
    if (ok) {
        char b[ED_MAX * 4];
        ed_display(e, b, sizeof b);
        clip_put(b);
        ed_clear(e);
    }
    menu_close();
    ui_toast(ok ? "cut to the clipboard" : "nothing to cut");
}
static void eb_clips(void) { menu_open(&menu_clips); }
static void eb_help(void) { help_context(); }

const softkeys_t input_edit_bar = {
    {{"undo", eb_undo}, {"redo", eb_redo}, {"copy", eb_copy}, {"paste", eb_paste}, {"history", hist_search_open}},
    {{"cut", eb_cut}, {"clips", eb_clips}, {NULL}, {"help", eb_help}, {"back", menu_close}},
};

const softkeys_t *input_context_bar(const screen_t *origin)
{
    /* the forms browser has no editor but copies its selected form */
    return origin && (origin->editor || origin == &scr_forms) ? &input_edit_bar : NULL;
}

/* ------------------------------------------------------------ state */
void input_state(void (*kv)(const char *, const char *, void *), void *user)
{
    char b[16];
    kv("clip", clip_get(0), user);
    snprintf(b, sizeof b, "%d", nclip);
    kv("clips", b, user);
    snprintf(b, sizeof b, "%d", input_undo_count(NULL));
    kv("undo_n", b, user);
    snprintf(b, sizeof b, "%d", input_redo_count(NULL));
    kv("redo_n", b, user);
    catalog_state(kv, user);
    hist_state(kv, user);
}
